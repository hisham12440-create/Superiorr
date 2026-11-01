#include "panel.h"
#include "localized.h"
#include "filters/presets.h"
#include "core/profiles.h"
#include "benchmark/quality.h"
#include "core/log.h"
#include <commdlg.h>
#include <shellapi.h>
#include <chrono>
#include <vector>
#include <algorithm>
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
    if (message == WM_GETMINMAXINFO) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(l);
        limits->ptMinTrackSize = {680, 480};
        return 0;
    }
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
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 780, nullptr, nullptr, instance, nullptr);
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
    style.ChildRounding = 8;
    style.FrameRounding = 6;
    style.GrabRounding = 6;
    style.FramePadding = ImVec2(12, 8);
    style.ItemSpacing = ImVec2(12, 14);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(.025f,.035f,.065f,1);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(.035f,.05f,.085f,1);
    style.Colors[ImGuiCol_Border] = ImVec4(.10f,.17f,.27f,1);
    style.Colors[ImGuiCol_Header] = ImVec4(.05f,.28f,.65f,1);
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
    PollTools();
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
    bool changed = false;
    int language = settings_->language == "ar" ? 1 : 0;
    const char* languages[] = {"English", "Arabic"};
    if (localized::Combo("Language", &language, languages, 2)) {
        settings_->language = language ? "ar" : "en";
        changed = true;
    }
    const bool rtl = settings_->language == "ar";
    const float bodyHeight = std::max(180.f, ImGui::GetContentRegionAvail().y - 8.f);
    if (ImGui::BeginTable("ControlLayout", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Left", rtl ? ImGuiTableColumnFlags_WidthStretch : ImGuiTableColumnFlags_WidthFixed, rtl ? 1.f : 165.f);
        ImGui::TableSetupColumn("Right", rtl ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch, rtl ? 165.f : 1.f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(rtl ? 1 : 0);
        ImGui::BeginChild("Navigation", ImVec2(0, bodyHeight), ImGuiChildFlags_Borders);
        const char* pages[] = {"Overview", "Image filters", "OLED display", "Quality tools", "Preferences", "Setup guide", "Diagnostics"};
        for (int i=0; i<7; ++i) {
            ImGui::PushID(i);
            if (localized::Selectable(pages[i], page_==i)) page_=i;
            ImGui::PopID();
            ImGui::Spacing();
        }
        ImGui::Separator();
        if (localized::Button("Hide to tray")) ShowWindow(window_, SW_HIDE);
        ImGui::EndChild();
        ImGui::TableSetColumnIndex(rtl ? 0 : 1);
        ImGui::PushID(page_);
        ImGui::BeginChild("Page", ImVec2(0, bodyHeight), ImGuiChildFlags_Borders);
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .60f);
        localized::SeparatorText(pages[page_]);
        if (page_ == 0) {
            localized::Text("SESSION   %s", localized::Value(!running ? "Ready" : paused ? "Paused - game not foreground" : "Capturing").c_str());
            localized::TextWrapped("Use your Start / stop shortcut on the borderless game, or tray > Start with a 3-second delay. Close this panel to keep Superior in the tray.");
            const char* modes[] = {"Comfort", "Balanced", "Max"};
            changed |= localized::Combo("Operating mode", &settings_->comfortMode, modes, 3);
            changed |= localized::Checkbox("Frame generation", &settings_->frameGen);
            changed |= localized::SliderInt("Maximum multiplier (Comfort caps x2)", &settings_->maxMultiplier, 2, 4);
            localized::TextWrapped("Frame generation requires a game cap near half your display refresh. GPU quality and latency are not yet validated on hardware.");
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
            DrawSession();
        } else if (page_ == 1) {
            changed |= DrawFilters();
        } else if (page_ == 2) {
            changed |= DrawOled();
        } else if (page_ == 3) {
            if (localized::CollapsingHeader("Neural SR")) {
                localized::TextWrapped("Neural live path is unavailable; spatial fallback is active.");
                localized::TextWrapped("Offline DirectML inference: Superior.exe --neural input.pgm output.pgm model.onnx");
            }
            DrawVerification(running);
        } else if (page_ == 6) {
            DrawDiagnostics(running, paused, hw, safeMode);
        } else if (page_ == 5) {
            DrawSetup(hw, safeMode);
        } else {
            DrawSettingsTransfer(running);
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
                if (localized::Button("Edit default shortcuts")) draftKeys_ = DefaultHotkeys();
                if (localized::Button("Discard shortcut edits")) { draftKeys_ = settings_->hotkeys; hotkeyStatus_.clear(); }
                localized::TextWrapped("%s", hotkeyStatus_.c_str());
            }
            if (localized::Button("Reset image settings")) {
                Settings defaults;
                CopyGameSettings(*settings_, defaults);
                changed = true;
            }
        }
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::PopID();
        ImGui::EndTable();
    }
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
void Panel::DrawSession() {
    if (!localized::CollapsingHeader("SESSION OBSERVATIONS")) return;
    if (!session_.available) { localized::TextWrapped("Start capture to collect session observations"); return; }
    localized::TextWrapped("API-side counters, not displayed FPS or ghost-free frame counts");
    localized::Text("Elapsed %.1f s | Active %.1f s | Paused %.1f s", session_.elapsedMs/1000, session_.activeMs/1000, session_.pausedMs/1000);
    localized::Text("Delivered %llu | Accepted %llu | Ignored while paused %llu",
        static_cast<unsigned long long>(session_.received), static_cast<unsigned long long>(session_.accepted), static_cast<unsigned long long>(session_.ignoredWhilePaused));
    localized::Text("Last planned multiplier x%d", session_.plannedMultiplier);
    const auto row=[](const char* label,const present::FrameCounts& c) {
        localized::SeparatorText(label);
        localized::Text("Scheduled %llu | S_OK %llu | Pending %llu", static_cast<unsigned long long>(c.scheduled),static_cast<unsigned long long>(c.submitted),static_cast<unsigned long long>(c.pending));
        localized::Text("Discarded %llu | Occluded %llu | Other %llu | Failed %llu",static_cast<unsigned long long>(c.discarded),static_cast<unsigned long long>(c.occluded),static_cast<unsigned long long>(c.otherStatus),static_cast<unsigned long long>(c.failed));
    };
    row("Real-frame path",session_.real); row("Generated-frame path (may use shader fallback)",session_.generated);
    localized::Text("Drops: replaced %llu | pause %llu | resize %llu | stop %llu",static_cast<unsigned long long>(session_.replaced),static_cast<unsigned long long>(session_.droppedOnPause),static_cast<unsigned long long>(session_.droppedOnResize),static_cast<unsigned long long>(session_.droppedOnStop));
    if (session_.callWallMs.samples) localized::Text("Render + Present call wall time: mean %.2f ms | max %.2f ms",session_.callWallMs.Mean(),session_.callWallMs.maximum);
    if (session_.lateStartMs.samples) localized::Text("Start lateness: mean %.2f ms | max %.2f ms",session_.lateStartMs.Mean(),session_.lateStartMs.maximum);
    if (session_.returnIntervalMs.samples) localized::Text("Between S_OK returns: mean %.2f ms | max %.2f ms",session_.returnIntervalMs.Mean(),session_.returnIntervalMs.maximum);
    localized::TextWrapped("S_OK is an accepted Present call, not proof that a frame reached the screen. Timings above are CPU wall times, not GPU latency.");
    ImGui::BeginDisabled(OfflineBusy());
    if (localized::Button("Export session JSON")) ExportSession();
    ImGui::EndDisabled();
    if (!sessionStatus_.empty()) localized::TextWrapped("%s",sessionStatus_.c_str());
}
void Panel::ExportSession() {
    if (!session_.available || OfflineBusy()) return;
    toolDialogOpen_=true; cancelDialog_=false;
    struct DialogScope { bool& open; ~DialogScope() { open=false; } } scope{toolDialogOpen_};
    std::vector<wchar_t> file(32768);
    const std::wstring name=L"Superior-session.json";
    std::copy(name.begin(),name.end(),file.begin());
    OPENFILENAMEW dialog{};
    dialog.lStructSize=sizeof dialog; dialog.hwndOwner=window_;
    dialog.lpstrFile=file.data(); dialog.nMaxFile=static_cast<DWORD>(file.size());
    dialog.lpstrFilter=L"JSON report (*.json)\0*.json\0\0"; dialog.lpstrDefExt=L"json";
    dialog.Flags=OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    dialog.lpstrTitle=settings_->language=="ar" ? L"حفظ تقرير الجلسة" : L"Save session report";
    // Freeze exactly the snapshot visible when export was requested; capture may
    // stop while the modal dialog pumps messages, without changing this report.
    const auto snapshot=session_;
    if (!GetSaveFileNameW(&dialog) || cancelDialog_) {
        if (CommDlgExtendedError()) sessionStatus_="Session report dialog failed";
        return;
    }
    sessionStatus_=present::SaveSessionReport(std::filesystem::path(file.data()),snapshot) ? "Session snapshot exported" : "Session report export failed";
}

void Panel::PollTools() {
    if (!verification_.valid() || verification_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
    try {
        const int code = verification_.get();
        toolStatus_ = code == 0 ? "Verification finished: target and baseline met" :
                      code == 2 ? "Verification finished: target or baseline missed" :
                      code == 3 ? "Verification cancelled; no pass rate reported" : "Verification failed; no new result is guaranteed";
    } catch (const std::exception& e) {
        toolStatus_ = "Verification worker failed";
        SERR("Verification worker: %s", e.what());
    }
}
void Panel::CancelToolDialog() {
    cancelDialog_ = true;
    HWND popup = GetLastActivePopup(window_);
    if (popup && popup != window_) PostMessageW(popup, WM_CLOSE, 0, 0);
}
void Panel::ChooseVerification() {
    if (OfflineBusy()) return;
    toolDialogOpen_ = true; cancelDialog_ = false;
    struct DialogScope { bool& open; ~DialogScope() { open = false; } } scope{toolDialogOpen_};
    std::vector<wchar_t> file(32768);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"JSON manifest (*.json)\0*.json\0\0";
    dialog.lpstrFile = file.data(); dialog.nMaxFile = static_cast<DWORD>(file.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    dialog.lpstrTitle = settings_->language == "ar" ? L"اختر ملف مجموعة الاختبار" : L"Choose reference manifest";
    if (!GetOpenFileNameW(&dialog)) {
        const DWORD error = CommDlgExtendedError();
        if (error) { toolStatus_ = "File dialog failed"; SERR("Open manifest dialog: %lu", error); }
        return;
    }
    if (cancelDialog_) return;
    const std::filesystem::path manifest(file.data());
    auto proposed = manifest.parent_path() / (manifest.stem().wstring() + L".report.json");
    const auto proposedName = proposed.wstring();
    if (proposedName.size() + 1 >= file.size()) { toolStatus_ = "Report path is too long"; return; }
    std::copy(proposedName.begin(), proposedName.end(), file.begin()); file[proposedName.size()] = 0;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    dialog.lpstrFilter = L"JSON report (*.json)\0*.json\0\0"; dialog.lpstrDefExt = L"json";
    dialog.lpstrTitle = settings_->language == "ar" ? L"اختر مكان تقرير التحقق" : L"Save verification report";
    if (!GetSaveFileNameW(&dialog)) {
        const DWORD error = CommDlgExtendedError();
        if (error) { toolStatus_ = "File dialog failed"; SERR("Save report dialog: %lu", error); }
        return;
    }
    if (cancelDialog_) return;
    const std::filesystem::path report(file.data());
    auto state = std::make_shared<VerifyState>();
    try {
        verification_ = std::async(std::launch::async, [state,manifest,report] {
            if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL))
                SERR("Verification priority unavailable (%lu)", GetLastError());
            benchmark::VerifyControl control;
            control.cancelled = [state] { return state->cancel.load(); };
            control.progress = [state](size_t completed, size_t total) {
                state->total.store(total); state->completed.store(completed);
            };
            return benchmark::Verify(manifest,report,control);
        });
        verifyState_ = state; lastReport_ = report;
        toolStatus_ = "Running offline verification (not live GPU validation)";
    } catch (const std::exception& e) {
        toolStatus_ = "Could not start verification worker";
        SERR("Start verification: %s", e.what());
    }
}
void Panel::DrawVerification(bool running) {
    if (!localized::CollapsingHeader("Quality verification")) return;
    localized::TextWrapped("99% is a dataset target, not a runtime guarantee.");
    localized::TextWrapped("Choose a reference manifest to evaluate supplied images. This does not record gameplay or generate test frames.");
    ImGui::BeginDisabled(running || OfflineBusy());
    if (localized::Button("Choose manifest and run verification")) ChooseVerification();
    ImGui::EndDisabled();
    if (running) localized::TextWrapped("Stop capture before starting offline tools");
    localized::TextWrapped("%s", toolStatus_.c_str());
    if (verifyState_ && OfflineBusy()) {
        const size_t total=verifyState_->total.load(), completed=verifyState_->completed.load();
        const float progress=total ? static_cast<float>(completed)/static_cast<float>(total) : 0.f;
        ImGui::ProgressBar(progress,ImVec2(-1,0));
        localized::Text("Completed %u / %u frames", static_cast<unsigned>(completed), static_cast<unsigned>(total));
        if (localized::Button("Cancel verification")) verifyState_->cancel.store(true);
        if (verifyState_->cancel.load()) localized::TextWrapped("Cancellation requested; waiting for a safe checkpoint");
    }
    if (!lastReport_.empty()) {
        localized::TextWrapped("%s", lastReport_.u8string().c_str());
        ImGui::BeginDisabled(OfflineBusy());
        if (localized::Button("Copy report path")) ImGui::SetClipboardText(lastReport_.u8string().c_str());
        ImGui::EndDisabled();
    }
}
void Panel::Close() {
    if (verifyState_) verifyState_->cancel.store(true);
    if (verification_.valid()) {
        verification_.wait();
        PollTools();
    }

    if (initialized_) { ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); }
    localized::Shutdown();
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    initialized_ = false;
    target_.Reset(); swap_.Reset(); context_.Reset();
    if (window_) { DestroyWindow(window_); window_ = nullptr; }
}
}
