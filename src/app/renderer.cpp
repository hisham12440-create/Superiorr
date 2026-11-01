#include "renderer.h"


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

struct RenderCb {
    float srcW, srcH, outW, outH;
    float offX, offY, scale, frame;
    float sharp, oledLevel, oledIntensity, vibrance;
    float contrast, saturation, brightness, gamma;
    float splitPreview, pad0, pad1, pad2;
};
static_assert(sizeof(RenderCb) == 80, "constant buffer layout");

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

    HR(dev_->CreateVertexShader(kShaderVS, sizeof kShaderVS, nullptr, &vs_));
    HR(dev_->CreatePixelShader(kShaderPSMid, sizeof kShaderPSMid, nullptr, &psMid_));
    HR(dev_->CreatePixelShader(kShaderPSAA, sizeof kShaderPSAA, nullptr, &psAA_));
    HR(dev_->CreatePixelShader(kShaderPSUp, sizeof kShaderPSUp, nullptr, &psUp_));
    HR(dev_->CreatePixelShader(kShaderPSFinal, sizeof kShaderPSFinal, nullptr, &psFinal_));
    HR(dev_->CreateComputeShader(kShaderCSFlow, sizeof kShaderCSFlow, nullptr, &csFlow_));

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

    // Stats stay on the GPU; midpoint shader chooses safe fallback on cuts.
    D3D11_BUFFER_DESC sb{};
    sb.ByteWidth = 16;
    sb.Usage = D3D11_USAGE_DEFAULT;
    sb.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    sb.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    sb.StructureByteStride = 4;
    HR(dev_->CreateBuffer(&sb, nullptr, &statsBuf_));
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = 4;
    HR(dev_->CreateUnorderedAccessView(statsBuf_.Get(), &ud, &statsUav_));
    D3D11_SHADER_RESOURCE_VIEW_DESC statsDesc{};
    statsDesc.Format = DXGI_FORMAT_UNKNOWN;
    statsDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    statsDesc.Buffer.NumElements = 4;
    HR(dev_->CreateShaderResourceView(statsBuf_.Get(), &statsDesc, &statsSrv_));

    timingAvailable_ = true;
    for (auto& slot : timings_) {
        D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        if (FAILED(dev_->CreateQuery(&q, &slot.disjoint))) { timingAvailable_ = false; break; }
        q.Query = D3D11_QUERY_TIMESTAMP;
        if (FAILED(dev_->CreateQuery(&q, &slot.start)) || FAILED(dev_->CreateQuery(&q, &slot.end))) {
            timingAvailable_ = false;
            break;
        }
    }
    SLOG("Device ready on %ls (feature level 0x%X)", adapterName_.c_str(), static_cast<unsigned>(got));
}

std::optional<double> Renderer::PollGpuTime() {
    std::optional<double> result;
    if (!timingAvailable_) return result;
    // Do not flush or wait: incomplete samples remain in the ring.
    for (auto& slot : timings_) {
        if (!slot.pending) continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
        UINT64 start = 0, end = 0;
        const HRESULT ready = ctx_->GetData(slot.disjoint.Get(), &disjoint, sizeof disjoint, D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (ready == S_FALSE) continue;
        if (FAILED(ready)) { slot.pending = false; continue; }
        if (ctx_->GetData(slot.start.Get(), &start, sizeof start, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            ctx_->GetData(slot.end.Get(), &end, sizeof end, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        slot.pending = false;
        if (!disjoint.Disjoint && disjoint.Frequency && end >= start)
            result = std::max(result.value_or(0), 1000.0 * static_cast<double>(end - start) / static_cast<double>(disjoint.Frequency));
    }
    return result;
}
void Renderer::BeginGpuTiming() {
    if (!timingAvailable_ || activeTiming_ >= 0) return;
    for (size_t i = 0; i < timings_.size(); ++i) {
        auto& slot = timings_[i];
        if (slot.pending) continue;
        activeTiming_ = static_cast<int>(i);
        ctx_->Begin(slot.disjoint.Get());
        ctx_->End(slot.start.Get());
        break;
    }
}
void Renderer::EndGpuTiming() {
    if (activeTiming_ < 0) return;
    auto& slot = timings_[static_cast<size_t>(activeTiming_)];
    ctx_->End(slot.end.Get());
    ctx_->End(slot.disjoint.Get());
    slot.pending = true;
    activeTiming_ = -1;
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
    MakeTex(up_, w, h, true, DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void Renderer::DestroyOutput() {
    EndGpuTiming();
    if (ctx_) ctx_->ClearState();
    bbRtv_.Reset();
    if (waitable_) CloseHandle(waitable_);
    waitable_ = nullptr;
    sc_.Reset();
    prev_ = Tex{};
    cur_ = Tex{};
    mid_ = Tex{};
    aa_ = Tex{};
    up_ = Tex{};
    srcW_ = srcH_ = 0;
    frames_ = 0;
}

void Renderer::MakeTex(Tex& t, UINT w, UINT h, bool renderTarget, DXGI_FORMAT fmt) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = fmt;
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
    MakeTex(aa_, w, h, true);

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

void Renderer::ComputeFlow() {
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
    ID3D11ShaderResourceView* srvs[4] = {prev_.srv.Get(), cur_.srv.Get(), flowSrv_.Get(), statsSrv_.Get()};
    ctx_->PSSetShaderResources(0, 4, srvs);
    ID3D11SamplerState* smp = sampler_.Get();
    ctx_->PSSetSamplers(0, 1, &smp);
    ID3D11Buffer* cbs[1] = {cbRender_.Get()};
    ctx_->PSSetConstantBuffers(0, 1, cbs);
    ctx_->Draw(3, 0);

    ID3D11ShaderResourceView* nulls[4] = {nullptr, nullptr, nullptr, nullptr};
    ctx_->PSSetShaderResources(0, 4, nulls);
    ID3D11RenderTargetView* nullRtv = nullptr;
    ctx_->OMSetRenderTargets(1, &nullRtv, nullptr);
}

void Renderer::Draw(ID3D11RenderTargetView* rtv, UINT w, UINT h, ID3D11PixelShader* ps, ID3D11ShaderResourceView* srv) {
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0, 0, static_cast<float>(w), static_cast<float>(h), 0, 1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(vs_.Get(), nullptr, 0);
    ctx_->PSSetShader(ps, nullptr, 0);
    ctx_->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* smp = sampler_.Get();
    ctx_->PSSetSamplers(0, 1, &smp);
    ID3D11Buffer* cbs[1] = {cbRender_.Get()};
    ctx_->PSSetConstantBuffers(0, 1, cbs);
    ctx_->Draw(3, 0);

    ID3D11ShaderResourceView* nullSrv = nullptr;
    ctx_->PSSetShaderResources(0, 1, &nullSrv);
    ID3D11RenderTargetView* nullRtv = nullptr;
    ctx_->OMSetRenderTargets(1, &nullRtv, nullptr);
}

void Renderer::PresentFinal(Src s, const Settings& st) {
    BeginGpuTiming();
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
    cb.vibrance = st.vibrance;
    cb.contrast = st.contrast;
    cb.saturation = st.saturation;
    cb.brightness = st.brightness;
    cb.gamma = st.gamma;
    cb.splitPreview = st.splitPreview ? 1.0f : 0.0f;
    UpdateCb(cbRender_.Get(), &cb, sizeof cb);

    ID3D11ShaderResourceView* src = (s == Src::Mid) ? mid_.srv.Get() : cur_.srv.Get();
    if (st.antiAlias) {
        Draw(aa_.rtv.Get(), srcW_, srcH_, psAA_.Get(), src);
        src = aa_.srv.Get();
    }
    Draw(up_.rtv.Get(), outW_, outH_, psUp_.Get(), src);
    // Final shader t1 is the unfiltered real source for split preview.
    ID3D11ShaderResourceView* original = cur_.srv.Get();
    ctx_->PSSetShaderResources(1, 1, &original);
    Draw(bbRtv_.Get(), outW_, outH_, psFinal_.Get(), up_.srv.Get());
    original = nullptr;
    ctx_->PSSetShaderResources(1, 1, &original);

    EndGpuTiming();
    const HRESULT hr = sc_->Present(1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        Fail("Present (GPU device lost)", dev_->GetDeviceRemovedReason());
    }
    if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) Fail("Present", hr);
}

}  // namespace superior
