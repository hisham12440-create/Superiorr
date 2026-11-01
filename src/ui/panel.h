#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <functional>
#include <future>
#include <atomic>
#include <memory>
#include <filesystem>
#include "core/config.h"
#include "hardware/monitor.h"
#include "present/session_stats.h"
namespace superior {
using ApplyHotkeys = std::function<bool(const Hotkeys&, std::string&)>;
class Panel {
public:
    void Open(HINSTANCE instance, ID3D11Device* device, Settings* settings,
              std::function<void()> save, ApplyHotkeys applyHotkeys);
    void SetSessionSnapshot(const present::SessionSnapshot& snapshot) { session_ = snapshot; }
    void SetProfileStatus(const std::string& text) { profileStatus_ = text; }
    bool OfflineBusy() const { return verification_.valid() || toolDialogOpen_; }
    bool ToolDialogOpen() const { return toolDialogOpen_; }
    void CancelToolDialog();
    void PollTools();
    void Close();
    void Show();
    bool Visible() const;
    void Draw(bool running, bool paused, const hardware::Snapshot& hardware, bool safeMode);
private:
    struct VerifyState {
        std::atomic<bool> cancel{false};
        std::atomic<size_t> completed{0}, total{0};
    };
    void DrawSetup(const hardware::Snapshot& hardware, bool safeMode);
    void DrawSettingsTransfer(bool running);
    void ChooseSettingsBundle(bool importing);
    std::string transferStatus_;
    bool DrawFilters();
    bool DrawOled();
    int page_ = 0;
    int lastOledLevel_ = 2;
    void DrawSession();
    void ExportSession();
    present::SessionSnapshot session_;
    std::string sessionStatus_;
    void ChooseVerification();
    void DrawVerification(bool running);
    bool toolDialogOpen_ = false, cancelDialog_ = false;
    std::future<int> verification_;
    std::shared_ptr<VerifyState> verifyState_;
    std::filesystem::path lastReport_;
    std::string toolStatus_ = "No verification has been run";
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
