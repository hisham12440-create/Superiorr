#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <functional>
#include "core/config.h"
#include "hardware/monitor.h"
namespace superior {
using ApplyHotkeys = std::function<bool(const Hotkeys&, std::string&)>;
class Panel {
public:
    void Open(HINSTANCE instance, ID3D11Device* device, Settings* settings,
              std::function<void()> save, ApplyHotkeys applyHotkeys);
    void SetProfileStatus(const std::string& text) { profileStatus_ = text; }
    void Close();
    void Show();
    bool Visible() const;
    void Draw(bool running, bool paused, const hardware::Snapshot& hardware, bool safeMode);
private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    HWND window_ = nullptr;
    Settings* settings_ = nullptr;
    std::function<void()> save_;
    ApplyHotkeys applyHotkeys_;
    Hotkeys draftKeys_ = DefaultHotkeys();
    std::string hotkeyStatus_;
    std::string profileStatus_ = "No game selected";
    ULONGLONG lastDrawMs_ = 0;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    UINT width_ = 0, height_ = 0;
    bool initialized_ = false;
};
}
