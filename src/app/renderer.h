#pragma once
#include <d3d11_4.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <cstdint>
#include <array>
#include <optional>
#include <string>

#include "core/config.h"

namespace superior {

using Microsoft::WRL::ComPtr;

struct FlowStats {
    uint32_t badBlocks = 0;
    uint32_t totalBlocks = 0;
    uint32_t staticBlocks = 0;
};

enum class Src { Cur, Mid };

// All GPU work. Every COM object is held in a ComPtr (RAII); every D3D call is checked
// and failures throw std::runtime_error.
class Renderer {
public:
    void InitDevice();
    ID3D11Device* Device() const { return dev_.Get(); }
    const std::wstring& AdapterName() const { return adapterName_; }

    void CreateOutput(HWND hwnd, UINT w, UINT h);
    void DestroyOutput();
    HANDLE Waitable() const { return waitable_; }

    void SetSourceSize(UINT w, UINT h);
    void PushFrame(ID3D11Texture2D* src, UINT w, UINT h);
    void ResetHistory() { frames_ = 0; }
    int FramesHeld() const { return frames_ < 2 ? frames_ : 2; }

    void ComputeFlow();
    std::optional<double> PollGpuTime();
    void BeginGpuTiming();
    void EndGpuTiming();
    void RenderMid();
    void PresentFinal(Src s, const Settings& st);

private:
    struct Tex {
        ComPtr<ID3D11Texture2D> tex;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11RenderTargetView> rtv;
    };
    void MakeTex(Tex& t, UINT w, UINT h, bool renderTarget, DXGI_FORMAT fmt = DXGI_FORMAT_B8G8R8A8_UNORM);
    void Draw(ID3D11RenderTargetView* rtv, UINT w, UINT h, ID3D11PixelShader* ps, ID3D11ShaderResourceView* srv);
    void UpdateCb(ID3D11Buffer* cb, const void* data, size_t bytes);

    struct TimingSlot {
        ComPtr<ID3D11Query> disjoint, start, end;
        bool pending = false;
    };
    std::array<TimingSlot, 6> timings_;
    int activeTiming_ = -1;
    bool timingAvailable_ = false;
    ComPtr<ID3D11Device> dev_;
    ComPtr<ID3D11DeviceContext> ctx_;
    std::wstring adapterName_;

    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> psMid_, psAA_, psUp_, psFinal_;
    ComPtr<ID3D11ComputeShader> csFlow_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11Buffer> cbRender_, cbFlow_;

    ComPtr<IDXGISwapChain2> sc_;
    ComPtr<ID3D11RenderTargetView> bbRtv_;
    HANDLE waitable_ = nullptr;
    UINT outW_ = 0, outH_ = 0;

    Tex prev_, cur_, mid_, aa_, up_;
    UINT srcW_ = 0, srcH_ = 0;
    int frames_ = 0;
    uint32_t frameNo_ = 0;

    ComPtr<ID3D11Texture2D> flowTex_;
    ComPtr<ID3D11ShaderResourceView> flowSrv_;
    ComPtr<ID3D11UnorderedAccessView> flowUav_;
    ComPtr<ID3D11Buffer> statsBuf_;
    ComPtr<ID3D11UnorderedAccessView> statsUav_;
    ComPtr<ID3D11ShaderResourceView> statsSrv_;
    UINT blocksX_ = 0, blocksY_ = 0;
};

}  // namespace superior
