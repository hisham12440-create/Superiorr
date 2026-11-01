#include "panel.h"
#include "localized.h"
#include "filters/presets.h"
#include "core/profiles.h"
#include <stdexcept>
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace superior {
namespace {
void Check(HRESULT result, const char* message) {
    if (FAILED(result)) throw std::runtime_error(message);
}
}
LRESULT CALLBACK Panel::Proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, w, l)) return 1;
    if (message == WM_CLOSE) { ShowWindow(window, SW_HIDE); return 0; }
    return DefWindowProcW(window, message, w, l);
}
void Panel::Open(HINSTANCE instance, ID3D11Device* device, Settings* settings, std::function<void()> save, ApplyHotkeys applyHotkeys) {
    settings_ = settings;
    draftKeys_ = settings->hotkeys;
    applyHotkeys_ = std::move(applyHotkeys);
    save_ = std::move(save);
    WNDCLASSEXW wc{sizeof wc};
    wc.hInstance = instance;
    wc.lpfnWndProc = Proc;
    wc.lpszClassName = L"SuperiorPanel";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) throw std::runtime_error("Cannot register settings window");
    window_ = CreateWindowExW(0, wc.lpszClassName, L"Superior | Control Center",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 920, 980, nullptr, nullptr, instance, nullptr);
    if (!window_) throw std::runtime_error("Cannot create settings window");
    device->GetImmediateContext(&context_);
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
    Check(device->QueryInterface(IID_PPV_ARGS(&dxgi)), "UI DXGI device failed");
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Check(dxgi->GetAdapter(&adapter), "UI adapter failed");
    Microsoft::WRL::ComPtr<IDXGIFactory> factory;
    Check(adapter->GetParent(IID_PPV_ARGS(&factory)), "UI factory failed");
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    RECT client{};
    if (!GetClientRect(window_, &client)) throw std::runtime_error("UI client bounds unavailable");
    width_ = static_cast<UINT>(client.right);
    height_ = static_cast<UINT>(client.bottom);
    desc.BufferDesc.Width = width_;
    desc.BufferDesc.Height = height_;
    desc.OutputWindow = window_;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    Check(factory->CreateSwapChain(device, &desc, &swap_), "UI swap chain failed");
    Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
    Check(swap_->GetBuffer(0, IID_PPV_ARGS(&buffer)), "UI back buffer failed");
    Check(device->CreateRenderTargetView(buffer.Get(), nullptr, &target_), "UI render target failed");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 0;
    style.FrameRounding = 6;
    style.GrabRounding = 6;
    style.FramePadding = ImVec2(12, 8);
    style.ItemSpacing = ImVec2(12, 14);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(.025f,.035f,.065f,1);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(.06f,.10f,.18f,1);
    style.Colors[ImGuiCol_Button] = ImVec4(.05f,.28f,.65f,1);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(.08f,.40f,.90f,1);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(.15f,.65f,1,1);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(.15f,.55f,1,1);
    if (!ImGui_ImplWin32_Init(window_)) throw std::runtime_error("UI Windows backend failed");
    if (!ImGui_ImplDX11_Init(device, context_.Get())) {
        ImGui_ImplWin32_Shutdown();
        throw std::runtime_error("UI DirectX backend failed");
    }
    initialized_ = true;
    Show();
}
void Panel::Show() { if (window_) { ShowWindow(window_, SW_SHOW); SetForegroundWindow(window_); } }
bool Panel::Visible() const { return window_ && IsWindowVisible(window_); }
void Panel::Draw(bool running, bool paused, const hardware::Snapshot& hw, bool safeMode) {
    if (!initialized_ || !Visible()) return;
    const ULONGLONG now = GetTickCount64();
    if (now - lastDrawMs_ < 33) return;
    lastDrawMs_ = now;
    RECT client{};
    if (!GetClientRect(window_, &client) || IsIconic(window_) || client.right <= 0 || client.bottom <= 0) return;
    const UINT width = static_cast<UINT>(client.right), height = static_cast<UINT>(client.bottom);
    if (width != width_ || height != height_) {
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        target_.Reset();
        Check(swap_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "UI resize failed");
        Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
        Check(swap_->GetBuffer(0, IID_PPV_ARGS(&buffer)), "UI resized back buffer failed");
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        context_->GetDevice(&device);
        Check(device->CreateRenderTargetView(buffer.Get(), nullptr, &target_), "UI resized target failed");
        width_ = width; height_ = height;
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    Microsoft::WRL::ComPtr<ID3D11Device> uiDevice;
    context_->GetDevice(&uiDevice);
    localized::BeginFrame(uiDevice.Get(), settings_->language == "ar");
    RECT r{};
    GetClientRect(window_, &r);
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(r.right),static_cast<float>(r.bottom)));
    ImGui::Begin("Superior", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    localized::TextColored(ImVec4(.2f,.65f,1,1), "S U P E R I O R   /   CONTROL CENTER");
    if (safeMode) localized::TextColored(ImVec4(.3f,.7f,1,1), "SAFE MODE: frame generation is disabled");
    localized::TextDisabled("DirectX 11  |  Capture only  |  Prototype");
    ImGui::Separator();
    localized::Text("SESSION   %s", localized::Value(!running ? "Ready" : paused ? "Paused - game not foreground" : "Capturing").c_str());
    localized::TextWrapped("Use your Start / stop shortcut on the borderless game, or tray > Start with a 3-second delay. Close this panel to keep Superior in the tray.");
    localized::SeparatorText("HARDWARE / EFFICIENCY");
    localized::TextWrapped("%s | %s | Tier %s", hw.adapter.c_str(), hw.vendor.c_str(), localized::Value(hardware::TierName(hw.tier)).c_str());
    localized::Text("Dedicated %.0f MiB | Shared %.0f MiB", static_cast<double>(hw.dedicated) / hardware::MiB, static_cast<double>(hw.shared) / hardware::MiB);
    if (hw.memoryKnown) localized::Text("DXGI process usage %.0f MiB | Budget %.0f MiB | Extra allowance %.0f MiB",
        static_cast<double>(hw.usage) / hardware::MiB, static_cast<double>(hw.budget) / hardware::MiB, static_cast<double>(hw.available) / hardware::MiB);
    else localized::TextDisabled("VRAM budget: n/a");
    if (hw.timingKnown) localized::Text("Measured GPU group time %.2f ms | Group budget %.2f ms", hw.gpuMs, hw.targetMs);
    else localized::TextDisabled("GPU render time: n/a (waiting for completed timestamp queries)");
    localized::TextDisabled("Temperature / watts / clocks / ghost score: n/a");
    localized::TextWrapped("%s", hw.reason.c_str());
    bool changed = false;
    int language = settings_->language == "ar" ? 1 : 0;
    const char* languages[] = {"English", "Arabic"};
    if (localized::Combo("Language", &language, languages, 2)) {
        settings_->language = language ? "ar" : "en";
        changed = true;
    }
    if (localized::CollapsingHeader("GAME PROFILES")) {
        changed |= localized::Checkbox("Load / save per-game image settings (next session)", &settings_->autoProfiles);
        localized::TextWrapped("%s", profileStatus_.c_str());
        localized::TextWrapped("Game profiles use the full executable path. While active, image edits save to that game; global image defaults are restored when capture stops.");
    }
    if (localized::CollapsingHeader("CUSTOM SHORTCUTS")) {
        localized::TextWrapped("Choose a key and modifiers. Disabled removes the shortcut. Changes apply only when you press Apply. Windows may reject shortcuts already used by another app.");
        for (size_t i = 0; i < draftKeys_.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            localized::SeparatorText(kHotkeyNames[i]);
            ImGui::SetNextItemWidth(160);
            if (localized::BeginCombo("Key", KeyName(draftKeys_[i].key).c_str())) {
                for (uint32_t key = 0; key <= 255; ++key) {
                    if (SupportedKey(key) && localized::Selectable(KeyName(key).c_str(), draftKeys_[i].key == key))
                        draftKeys_[i].key = key;
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            int mods = static_cast<int>(draftKeys_[i].modifiers);
            localized::CheckboxFlags("Ctrl", &mods, 2); ImGui::SameLine();
            localized::CheckboxFlags("Alt", &mods, 1); ImGui::SameLine();
            localized::CheckboxFlags("Shift", &mods, 4); ImGui::SameLine();
            localized::CheckboxFlags("Win", &mods, 8);
            draftKeys_[i].modifiers = static_cast<uint32_t>(mods);
            ImGui::PopID();
        }
        localized::TextDisabled("F12 is reserved by Windows; at least one modifier is required.");
        if (localized::Button("Apply shortcuts")) {
            if (applyHotkeys_(draftKeys_, hotkeyStatus_)) hotkeyStatus_ = "Shortcuts applied";
        }
        ImGui::SameLine();
        if (localized::Button("Edit default shortcuts")) draftKeys_ = DefaultHotkeys();
        ImGui::SameLine();
        if (localized::Button("Discard shortcut edits")) { draftKeys_ = settings_->hotkeys; hotkeyStatus_.clear(); }
        localized::TextWrapped("%s", hotkeyStatus_.c_str());
    }
    const char* modes[] = {"Comfort", "Balanced", "Max"};
    changed |= localized::Combo("Operating mode", &settings_->comfortMode, modes, 3);
    changed |= localized::Checkbox("Frame generation", &settings_->frameGen);
    if (settings_->language != "ar") ImGui::SameLine(350);
    changed |= localized::Checkbox("Anti-aliasing", &settings_->antiAlias);
    changed |= localized::SliderInt("Maximum multiplier (Comfort caps x2)", &settings_->maxMultiplier, 2, 4);
    changed |= localized::SliderFloat("Sharpness", &settings_->sharpness, 0, 1, "%.2f");
    changed |= localized::SliderFloat("Contrast", &settings_->contrast, .5f, 2, "%.2f");
    changed |= localized::SliderFloat("Saturation", &settings_->saturation, 0, 2, "%.2f");
    changed |= localized::SliderFloat("Brightness", &settings_->brightness, -.25f, .25f, "%.2f");
    changed |= localized::SliderFloat("Gamma", &settings_->gamma, .5f, 2, "%.2f");
    changed |= localized::Checkbox("Before / after split (left is original)", &settings_->splitPreview);
    const char* presetNames[] = {"Clean", "Vivid", "Cinematic", "Competitive", "OLED"};
    if (localized::BeginCombo("Apply image preset", "Choose preset")) {
        for (int i = 0; i < 5; ++i) {
            if (localized::Selectable(presetNames[i])) {
                filters::Apply(static_cast<filters::Preset>(i), *settings_);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    changed |= localized::SliderFloat("Vibrance", &settings_->vibrance, 0, 1, "%.2f");
    const char* levels[] = {"Off", "Light", "Medium", "Strong"};
    changed |= localized::Combo("OLED emulation", &settings_->oledLevel, levels, 4);
    changed |= localized::SliderFloat("OLED intensity", &settings_->oledIntensity, 0, 1, "%.2f");
    ImGui::Separator();
    localized::TextDisabled("OLED is an emulation; LCD black levels cannot be changed.");
    localized::TextWrapped("Frame generation requires a game cap near half your display refresh. GPU quality and latency are not yet validated on hardware.");
    if (localized::CollapsingHeader("Neural SR")) {
        localized::TextWrapped("Neural live path is unavailable; spatial fallback is active.");
        localized::TextWrapped("Offline DirectML inference: Superior.exe --neural input.pgm output.pgm model.onnx");
    }
    if (localized::CollapsingHeader("Quality verification")) {
        localized::TextWrapped("Ground-truth pass rate: n/a (run --verify on a reference corpus)");
        localized::TextWrapped("99% is a dataset target, not a runtime guarantee.");
    }
    if (localized::Button("Reset image settings")) {
        Settings defaults;
        CopyGameSettings(*settings_, defaults);
        changed = true;
    }
    ImGui::SameLine();
    if (localized::Button("Hide to tray")) ShowWindow(window_, SW_HIDE);
    if (changed) { settings_->Clamp(); save_(); }
    ImGui::End();
    ImGui::Render();
    const float clear[] = {.025f,.035f,.065f,1};
    auto* target = target_.Get();
    context_->OMSetRenderTargets(1, &target, nullptr);
    context_->ClearRenderTargetView(target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    Check(swap_->Present(0,0), "UI present failed");
    target = nullptr;
    context_->OMSetRenderTargets(1, &target, nullptr);
}
void Panel::Close() {
    if (initialized_) { ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); }
    localized::Shutdown();
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    initialized_ = false;
    target_.Reset(); swap_.Reset(); context_.Reset();
    if (window_) { DestroyWindow(window_); window_ = nullptr; }
}
}
