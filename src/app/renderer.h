#pragma once
#include <d3d11_4.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <cstdint>
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
    int FramesHeld() const { return frames_ < 2 ? frames_ : 2; }

    FlowStats ComputeFlow();
    void RenderMid();
    void PresentFinal(Src s, const Settings& st);

private:
    struct Tex {
        ComPtr<ID3D11Texture2D> tex;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11RenderTargetView> rtv;
    };
    void MakeTex(Tex& t, UINT w, UINT h, bool renderTarget);
    void UpdateCb(ID3D11Buffer* cb, const void* data, size_t bytes);

    ComPtr<ID3D11Device> dev_;
    ComPtr<ID3D11DeviceContext> ctx_;
    std::wstring adapterName_;

    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> psMid_, psFinal_;
    ComPtr<ID3D11ComputeShader> csFlow_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11Buffer> cbRender_, cbFlow_;

    ComPtr<IDXGISwapChain2> sc_;
    ComPtr<ID3D11RenderTargetView> bbRtv_;
    HANDLE waitable_ = nullptr;
    UINT outW_ = 0, outH_ = 0;

    Tex prev_, cur_, mid_;
    UINT srcW_ = 0, srcH_ = 0;
    int frames_ = 0;
    uint32_t frameNo_ = 0;

    ComPtr<ID3D11Texture2D> flowTex_;
    ComPtr<ID3D11ShaderResourceView> flowSrv_;
    ComPtr<ID3D11UnorderedAccessView> flowUav_;
    ComPtr<ID3D11Buffer> statsBuf_, statsStaging_;
    ComPtr<ID3D11UnorderedAccessView> statsUav_;
    UINT blocksX_ = 0, blocksY_ = 0;
};

}  // namespace superior
