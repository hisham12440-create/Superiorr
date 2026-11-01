#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <functional>
#include "core/config.h"
#include "hardware/monitor.h"
namespace superior {
class Panel {
public:
    void Open(HINSTANCE instance, ID3D11Device* device, Settings* settings,
              std::function<void()> save);
    void Close();
    void Show();
    bool Visible() const;
    void Draw(bool running, bool paused, const hardware::Snapshot& hardware);
private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    HWND window_ = nullptr;
    Settings* settings_ = nullptr;
    std::function<void()> save_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    bool initialized_ = false;
};
}
