#include "renderer.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "core/log.h"
#include "shaders_gen.h"

namespace superior {
namespace {

[[noreturn]] void Fail(const char* what, HRESULT hr) {
    char buf[300];
    std::snprintf(buf, sizeof buf, "%s failed (0x%08X)", what, static_cast<unsigned>(hr));
    SERR("%s", buf);
    throw std::runtime_error(buf);
}

#define HR(expr)                               \
    do {                                       \
        const HRESULT hr_ = (expr);            \
        if (FAILED(hr_)) Fail(#expr, hr_);     \
    } while (0)

ComPtr<ID3DBlob> Compile(const char* src, const char* name, const char* entry, const char* target) {
    ComPtr<ID3DBlob> code, err;
    const HRESULT hr = D3DCompile(src, std::strlen(src), name, nullptr, nullptr, entry, target,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr)) {
        std::string msg = err ? std::string(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize())
                              : std::string("unknown error");
        SERR("Shader %s:%s failed: %s", name, entry, msg.c_str());
        throw std::runtime_error("Shader compile failed (" + std::string(entry) + "): " + msg);
    }
    return code;
}

struct RenderCb {
    float srcW, srcH, outW, outH;
    float offX, offY, scale, frame;
    float sharp, oledLevel, oledIntensity, pad;
};
static_assert(sizeof(RenderCb) == 48, "constant buffer layout");

struct FlowCb {
    float srcW, srcH, pad0, pad1;
};

}  // namespace

void Renderer::InitDevice() {
    ComPtr<IDXGIFactory1> factory;
    HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

    // Pick the adapter with the most dedicated VRAM (the discrete GPU on a hybrid laptop).
    ComPtr<IDXGIAdapter1> best;
    SIZE_T bestMem = 0;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> a;
        if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        SLOG("Adapter %u: %ls, VRAM %llu MB, flags 0x%X", i, d.Description,
             static_cast<unsigned long long>(d.DedicatedVideoMemory / (1024 * 1024)), d.Flags);
        if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (!best || d.DedicatedVideoMemory > bestMem) {
            best = a;
            bestMem = d.DedicatedVideoMemory;
        }
    }
    if (!best) throw std::runtime_error("No hardware GPU found");
    DXGI_ADAPTER_DESC1 bd{};
    best->GetDesc1(&bd);
    adapterName_ = bd.Description;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL got{};
    HR(D3D11CreateDevice(best.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2,
                         D3D11_SDK_VERSION, &dev_, &got, &ctx_));
    ComPtr<ID3D11Multithread> mt;
    if (SUCCEEDED(ctx_.As(&mt))) mt->SetMultithreadProtected(TRUE);

    auto vsBlob = Compile(kRenderHlsl, "render.hlsl", "VS", "vs_5_0");
    auto psMidBlob = Compile(kRenderHlsl, "render.hlsl", "PSMid", "ps_5_0");
    auto psFinBlob = Compile(kRenderHlsl, "render.hlsl", "PSFinal", "ps_5_0");
    auto csBlob = Compile(kFlowHlsl, "flow.hlsl", "CSFlow", "cs_5_0");
    HR(dev_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_));
    HR(dev_->CreatePixelShader(psMidBlob->GetBufferPointer(), psMidBlob->GetBufferSize(), nullptr, &psMid_));
    HR(dev_->CreatePixelShader(psFinBlob->GetBufferPointer(), psFinBlob->GetBufferSize(), nullptr, &psFinal_));
    HR(dev_->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &csFlow_));

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    HR(dev_->CreateSamplerState(&sd, &sampler_));

    D3D11_BUFFER_DESC bd2{};
    bd2.Usage = D3D11_USAGE_DYNAMIC;
    bd2.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd2.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd2.ByteWidth = sizeof(RenderCb);
    HR(dev_->CreateBuffer(&bd2, nullptr, &cbRender_));
    bd2.ByteWidth = sizeof(FlowCb);
    HR(dev_->CreateBuffer(&bd2, nullptr, &cbFlow_));

    // Stats buffer (3 counters) + staging copy. Only 16 bytes ever cross to the CPU.
    D3D11_BUFFER_DESC sb{};
    sb.ByteWidth = 16;
    sb.Usage = D3D11_USAGE_DEFAULT;
    sb.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    sb.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    sb.StructureByteStride = 4;
    HR(dev_->CreateBuffer(&sb, nullptr, &statsBuf_));
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = 4;
    HR(dev_->CreateUnorderedAccessView(statsBuf_.Get(), &ud, &statsUav_));
    sb.Usage = D3D11_USAGE_STAGING;
    sb.BindFlags = 0;
    sb.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    HR(dev_->CreateBuffer(&sb, nullptr, &statsStaging_));

    SLOG("Device ready on %ls (feature level 0x%X)", adapterName_.c_str(), static_cast<unsigned>(got));
}

void Renderer::CreateOutput(HWND hwnd, UINT w, UINT h) {
    ComPtr<IDXGIDevice> dxgiDev;
    HR(dev_.As(&dxgiDev));
    ComPtr<IDXGIAdapter> adapter;
    HR(dxgiDev->GetAdapter(&adapter));
    ComPtr<IDXGIFactory2> factory;
    HR(adapter->GetParent(IID_PPV_ARGS(&factory)));

    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = w;
    d.Height = h;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.Scaling = DXGI_SCALING_NONE;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    d.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    ComPtr<IDXGISwapChain1> sc1;
    HR(factory->CreateSwapChainForHwnd(dev_.Get(), hwnd, &d, nullptr, nullptr, &sc1));
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    HR(sc1.As(&sc_));
    HR(sc_->SetMaximumFrameLatency(1));
    waitable_ = sc_->GetFrameLatencyWaitableObject();

    ComPtr<ID3D11Texture2D> bb;
    HR(sc_->GetBuffer(0, IID_PPV_ARGS(&bb)));
    HR(dev_->CreateRenderTargetView(bb.Get(), nullptr, &bbRtv_));
    outW_ = w;
    outH_ = h;
}

void Renderer::DestroyOutput() {
    if (ctx_) ctx_->ClearState();
    bbRtv_.Reset();
    sc_.Reset();
    waitable_ = nullptr;
    prev_ = Tex{};
    cur_ = Tex{};
    mid_ = Tex{};
    srcW_ = srcH_ = 0;
    frames_ = 0;
}

void Renderer::MakeTex(Tex& t, UINT w, UINT h, bool renderTarget) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | (renderTarget ? D3D11_BIND_RENDER_TARGET : 0);
    t = Tex{};
    HR(dev_->CreateTexture2D(&d, nullptr, &t.tex));
    HR(dev_->CreateShaderResourceView(t.tex.Get(), nullptr, &t.srv));
    if (renderTarget) HR(dev_->CreateRenderTargetView(t.tex.Get(), nullptr, &t.rtv));
}

void Renderer::SetSourceSize(UINT w, UINT h) {
    if (w == srcW_ && h == srcH_ && prev_.tex) return;
    srcW_ = w;
    srcH_ = h;
    frames_ = 0;
    MakeTex(prev_, w, h, false);
    MakeTex(cur_, w, h, false);
    MakeTex(mid_, w, h, true);

    blocksX_ = (w + 15) / 16;
    blocksY_ = (h + 15) / 16;
    D3D11_TEXTURE2D_DESC fd{};
    fd.Width = blocksX_;
    fd.Height = blocksY_;
    fd.MipLevels = 1;
    fd.ArraySize = 1;
    fd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    fd.SampleDesc.Count = 1;
    fd.Usage = D3D11_USAGE_DEFAULT;
    fd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    flowTex_.Reset();
    flowSrv_.Reset();
    flowUav_.Reset();
    HR(dev_->CreateTexture2D(&fd, nullptr, &flowTex_));
    HR(dev_->CreateShaderResourceView(flowTex_.Get(), nullptr, &flowSrv_));
    HR(dev_->CreateUnorderedAccessView(flowTex_.Get(), nullptr, &flowUav_));
    SLOG("Source size %ux%u, flow grid %ux%u", w, h, blocksX_, blocksY_);
}

void Renderer::PushFrame(ID3D11Texture2D* src, UINT w, UINT h) {
    SetSourceSize(w, h);
    std::swap(prev_, cur_);
    D3D11_TEXTURE2D_DESC sd{};
    src->GetDesc(&sd);
    D3D11_BOX box{0, 0, 0, std::min(w, sd.Width), std::min(h, sd.Height), 1};
    ctx_->CopySubresourceRegion(cur_.tex.Get(), 0, 0, 0, 0, src, 0, &box);
    ++frames_;
}

void Renderer::UpdateCb(ID3D11Buffer* cb, const void* data, size_t bytes) {
    D3D11_MAPPED_SUBRESOURCE m{};
    HR(ctx_->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m));
    std::memcpy(m.pData, data, bytes);
    ctx_->Unmap(cb, 0);
}

FlowStats Renderer::ComputeFlow() {
    FlowCb cb{static_cast<float>(srcW_), static_cast<float>(srcH_), 0, 0};
    UpdateCb(cbFlow_.Get(), &cb, sizeof cb);

    const UINT zeros[4] = {0, 0, 0, 0};
    ctx_->ClearUnorderedAccessViewUint(statsUav_.Get(), zeros);

    ctx_->CSSetShader(csFlow_.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[2] = {prev_.srv.Get(), cur_.srv.Get()};
    ctx_->CSSetShaderResources(0, 2, srvs);
    ID3D11UnorderedAccessView* uavs[2] = {flowUav_.Get(), statsUav_.Get()};
    ctx_->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    ID3D11Buffer* cbs[1] = {cbFlow_.Get()};
    ctx_->CSSetConstantBuffers(0, 1, cbs);
    ctx_->Dispatch(blocksX_, blocksY_, 1);

    ID3D11UnorderedAccessView* nullUav[2] = {nullptr, nullptr};
    ctx_->CSSetUnorderedAccessViews(0, 2, nullUav, nullptr);
    ID3D11ShaderResourceView* nullSrv[2] = {nullptr, nullptr};
    ctx_->CSSetShaderResources(0, 2, nullSrv);

    ctx_->CopyResource(statsStaging_.Get(), statsBuf_.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    HR(ctx_->Map(statsStaging_.Get(), 0, D3D11_MAP_READ, 0, &m));
    const uint32_t* p = static_cast<const uint32_t*>(m.pData);
    FlowStats s;
    s.badBlocks = p[0];
    s.totalBlocks = p[1];
    s.staticBlocks = p[2];
    ctx_->Unmap(statsStaging_.Get(), 0);
    return s;
}

void Renderer::RenderMid() {
    RenderCb cb{};
    cb.srcW = static_cast<float>(srcW_);
    cb.srcH = static_cast<float>(srcH_);
    UpdateCb(cbRender_.Get(), &cb, sizeof cb);

    ID3D11RenderTargetView* rtv = mid_.rtv.Get();
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0, 0, static_cast<float>(srcW_), static_cast<float>(srcH_), 0, 1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(vs_.Get(), nullptr, 0);
    ctx_->PSSetShader(psMid_.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[3] = {prev_.srv.Get(), cur_.srv.Get(), flowSrv_.Get()};
    ctx_->PSSetShaderResources(0, 3, srvs);
    ID3D11SamplerState* smp = sampler_.Get();
    ctx_->PSSetSamplers(0, 1, &smp);
    ID3D11Buffer* cbs[1] = {cbRender_.Get()};
    ctx_->PSSetConstantBuffers(0, 1, cbs);
    ctx_->Draw(3, 0);

    ID3D11ShaderResourceView* nulls[3] = {nullptr, nullptr, nullptr};
    ctx_->PSSetShaderResources(0, 3, nulls);
    ID3D11RenderTargetView* nullRtv = nullptr;
    ctx_->OMSetRenderTargets(1, &nullRtv, nullptr);
}

void Renderer::PresentFinal(Src s, const Settings& st) {
    const float scale = std::min(static_cast<float>(outW_) / srcW_, static_cast<float>(outH_) / srcH_);
    RenderCb cb{};
    cb.srcW = static_cast<float>(srcW_);
    cb.srcH = static_cast<float>(srcH_);
    cb.outW = static_cast<float>(outW_);
    cb.outH = static_cast<float>(outH_);
    cb.offX = (outW_ - srcW_ * scale) * 0.5f;
    cb.offY = (outH_ - srcH_ * scale) * 0.5f;
    cb.scale = scale;
    cb.frame = static_cast<float>(frameNo_++ % 1024);
    cb.sharp = st.sharpness;
    cb.oledLevel = static_cast<float>(st.oledLevel);
    cb.oledIntensity = st.oledIntensity;
    UpdateCb(cbRender_.Get(), &cb, sizeof cb);

    ID3D11RenderTargetView* rtv = bbRtv_.Get();
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0, 0, static_cast<float>(outW_), static_cast<float>(outH_), 0, 1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(vs_.Get(), nullptr, 0);
    ctx_->PSSetShader(psFinal_.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srv = (s == Src::Mid) ? mid_.srv.Get() : cur_.srv.Get();
    ctx_->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* smp = sampler_.Get();
    ctx_->PSSetSamplers(0, 1, &smp);
    ID3D11Buffer* cbs[1] = {cbRender_.Get()};
    ctx_->PSSetConstantBuffers(0, 1, cbs);
    ctx_->Draw(3, 0);

    ID3D11ShaderResourceView* nullSrv = nullptr;
    ctx_->PSSetShaderResources(0, 1, &nullSrv);

    const HRESULT hr = sc_->Present(1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        Fail("Present (GPU device lost)", dev_->GetDeviceRemovedReason());
    }
    if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) Fail("Present", hr);
}

}  // namespace superior
