#include <unknwn.h>
#include <windows.h>
#include <shellapi.h>
#include <winrt/Windows.Foundation.h>

#include <deque>
#include <exception>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <vector>

#include "app/capture.h"
#include "app/hotkey_manager.h"
#include "app/unique_handle.h"
#include "core/profiles.h"
#include "core/files.h"
#include "app/renderer.h"
#include "core/config.h"
#include "core/log.h"
#include "core/pacing.h"
#include "ui/panel.h"
#include "interpolate/pacer.h"
#include "benchmark/quality.h"
#include "neural/offline.h"
#include "present/session_stats.h"

using namespace superior;

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT_PTR kStartTimer = 1;
enum Cmd { CMD_PANEL = 100, CMD_START = 101, CMD_FG, CMD_AA, CMD_OLED, CMD_LANG, CMD_EXIT };
enum Hot { HK_START = 1, HK_OLED, HK_FG, HK_OLED_UP, HK_OLED_DOWN, HK_SHARP_UP, HK_SHARP_DOWN, HK_LANG, HK_QUIT, HK_AA, HK_VIB_UP, HK_VIB_DOWN };

struct App {
    HINSTANCE inst = nullptr;
    HWND main = nullptr;
    HWND overlay = nullptr;
    HWND target = nullptr;
    std::wstring dir;
    std::string settingsPath;
    Settings st;
    Settings globalImageDefaults;
    bool profileSession = false;
    std::string gameExecutable;
    HotkeyManager hotkeys;
    Renderer gfx;
    Capture cap;
    Panel panel;
    hardware::Monitor monitor;
    bool paused = false;
    bool safeMode = false;
    bool exitAfterDialog = false;
    UniqueHandle frameEvent;
    NOTIFYICONDATAW nid{};

    bool running = false;
    struct QueuedFrame { Src source; float phase; double dueMs; };
    std::deque<QueuedFrame> queue;
    IntervalEma ema;
    present::SessionStats session;
    double lastFrameMs = 0.0;
    double displayIntervalMs = 16.667;
    double displayHz = 60.0;
    double startMs = 0.0;
    uint64_t framesRecv = 0;
    bool warnedNoFrames = false;
    UINT srcW = 0, srcH = 0;
    bool genActive = false;
    int activeMultiplier = 1;
} g;

double NowMs() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return 1000.0 * static_cast<double>(c.QuadPart) / static_cast<double>(freq.QuadPart);
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) throw std::runtime_error("UTF-8 conversion failed");
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    s.resize(static_cast<size_t>(n) - 1);
    return s;
}

bool Ar() { return g.st.language == "ar"; }
const wchar_t* T(const wchar_t* en, const wchar_t* ar) { return Ar() ? ar : en; }

const wchar_t* OledName() {
    switch (g.st.oledLevel) {
        case 1: return T(L"Light", L"خفيف");
        case 2: return T(L"Medium", L"متوسط");
        case 3: return T(L"Strong", L"قوي");
        default: return T(L"Off", L"إيقاف");
    }
}

void Notify(const std::wstring& title, const std::wstring& text) {
    NOTIFYICONDATAW n = g.nid;
    n.uFlags = NIF_INFO;
    wcsncpy_s(n.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(n.szInfo, text.c_str(), _TRUNCATE);
    n.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void UpdateTip() {
    wchar_t buf[128];
    const std::wstring multiplier = g.running && g.genActive ? L"planned x" + std::to_wstring(g.activeMultiplier) : L"";
    swprintf_s(buf, L"Superior | %s %s | OLED %s %d%% | %s %d%% | FG %s | AA %s | Vib %d%%", g.running ? L"ON" : L"idle",
               multiplier.c_str(), OledName(), static_cast<int>(g.st.oledIntensity * 100.0f + 0.5f),
               T(L"Sharp", L"حدة"), static_cast<int>(g.st.sharpness * 100.0f + 0.5f),
               g.st.frameGen ? L"on" : L"off", g.st.antiAlias ? L"on" : L"off",
               static_cast<int>(g.st.vibrance * 100.0f + 0.5f));
    g.nid.uFlags = NIF_TIP;
    wcsncpy_s(g.nid.szTip, buf, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

std::string ExecutableForWindow(HWND window) {
    DWORD processId = 0;
    if (!GetWindowThreadProcessId(window, &processId)) return {};
    UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
    if (!process) return {};
    std::vector<wchar_t> path(32768);
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.Get(), 0, path.data(), &length)) return {};
    // Normalize through the Windows Unicode casing rules before hashing.
    if (length && !CharLowerBuffW(path.data(), length)) return {};
    return ToUtf8(std::wstring(path.data(), length));
}

void SaveAndRefresh() {
    g.st.Clamp();
    Settings globals = g.st;
    if (g.profileSession) {
        CopyGameSettings(globals, g.globalImageDefaults);
        if (!SaveGameProfile(std::filesystem::path(g.dir) / L"profiles", g.gameExecutable, g.st)) {
            g.panel.SetProfileStatus("Game profile save failed. Check folder permissions.");
            Notify(L"Superior", L"Game profile could not be saved. Check folder permissions.");
        } else g.panel.SetProfileStatus("Game profile saved: " + g.gameExecutable);
    }
    if (!SaveSettings(g.settingsPath, globals))
        Notify(L"Superior", L"Settings could not be saved. Check folder permissions.");
    UpdateTip();
}

bool ApplyShortcuts(const Hotkeys& keys, std::string& error) {
    if (!g.hotkeys.Apply(g.main, keys, error)) return false;
    g.st.hotkeys = keys;
    SaveAndRefresh();
    return true;
}

LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST) return HTTRANSPARENT;  // click-through
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, m, w, l);
}

void DiscardPending(present::DropReason reason) {
    for (const auto& frame : g.queue)
        if (!g.session.Discard(frame.source == Src::Mid, reason)) SERR("Session queue accounting mismatch");
    g.queue.clear();
}

void StopSession(const wchar_t* why) {
    if (!g.running && !g.overlay) return;
    KillTimer(g.main, kStartTimer);
    g.cap.Stop();
    g.gfx.DestroyOutput();
    if (g.overlay) {
        DestroyWindow(g.overlay);
        g.overlay = nullptr;
    }
    DiscardPending(present::DropReason::Stop);
    g.session.Stop(NowMs());
    if (g.profileSession) CopyGameSettings(g.st, g.globalImageDefaults);
    g.profileSession = false;
    g.gameExecutable.clear();
    g.panel.SetProfileStatus("No active game profile. Global image defaults restored.");
    g.running = false;
    g.genActive = false;
    g.activeMultiplier = 1;
    SLOG("Session stopped: %s", ToUtf8(why).c_str());
    UpdateTip();
    if (why && *why) Notify(L"Superior", why);
}

void StartSession(HWND target) {
    if (g.running) return;
    g.panel.PollTools();
    if (g.panel.OfflineBusy()) {
        Notify(L"Superior", T(L"Wait for offline verification or cancel it before starting capture.", L"انتظر انتهاء التحقق أو ألغِه قبل بدء الالتقاط."));
        return;
    }
    wchar_t cls[64] = {};
    if (target) GetClassNameW(target, cls, 64);
    const bool bad = !target || !IsWindow(target) || !IsWindowVisible(target) || target == g.main ||
                     !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") ||
                     !wcscmp(cls, L"SuperiorOverlay") || !wcscmp(cls, L"SuperiorPanel");
    if (bad) {
        Notify(L"Superior", T(L"Click the game window first, then start again.", L"اضغط على نافذة اللعبة أولاً ثم ابدأ مجدداً."));
        return;
    }
    try {
        HMONITOR mon = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof mi;
        if (!GetMonitorInfoW(mon, &mi)) throw std::runtime_error("Monitor information unavailable");
        const int x = mi.rcMonitor.left, y = mi.rcMonitor.top;
        const UINT w = static_cast<UINT>(mi.rcMonitor.right - mi.rcMonitor.left);
        const UINT h = static_cast<UINT>(mi.rcMonitor.bottom - mi.rcMonitor.top);
        DEVMODEW dm{};
        dm.dmSize = sizeof dm;
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1) {
            g.displayHz = dm.dmDisplayFrequency;
        } else {
            g.displayHz = 60.0;
        }
        g.displayIntervalMs = 1000.0 / g.displayHz;

        g.overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                                    L"SuperiorOverlay", L"Superior", WS_POPUP, x, y, static_cast<int>(w), static_cast<int>(h),
                                    nullptr, nullptr, g.inst, nullptr);
        if (!g.overlay) throw std::runtime_error("CreateWindowEx failed");
        SetLayeredWindowAttributes(g.overlay, 0, 255, LWA_ALPHA);
        ShowWindow(g.overlay, SW_SHOWNOACTIVATE);

        g.gfx.CreateOutput(g.overlay, w, h);
        std::string err;
        if (!g.cap.Start(target, g.gfx.Device(), g.frameEvent.Get(), err)) {
            throw std::runtime_error("Capture failed: " + err);
        }
        g.target = target;
        g.running = true;
        g.paused = false;
        g.gameExecutable = ExecutableForWindow(target);
        g.profileSession = g.st.autoProfiles && !g.gameExecutable.empty();
        g.globalImageDefaults = g.st;
        if (g.profileSession) {
            const bool loaded = LoadGameProfile(std::filesystem::path(g.dir) / L"profiles", g.gameExecutable, g.st);
            g.panel.SetProfileStatus(std::string(loaded ? "Loaded game profile: " : "New game profile (saved on image edits): ") + g.gameExecutable);
        } else g.panel.SetProfileStatus(g.st.autoProfiles ? "Cannot identify target executable; using global settings" : "Profiles disabled for this session");
        g.queue.clear();
        g.ema.Reset();
        g.lastFrameMs = 0.0;
        g.framesRecv = 0;
        g.warnedNoFrames = false;
        g.startMs = NowMs();
        g.session.Start(g.startMs, g.displayIntervalMs);
        SLOG("Session started: display %ux%u @ %.0f Hz", w, h, g.displayHz);
        UpdateTip();
    } catch (const std::exception& e) {
        SERR("StartSession: %s", e.what());
        const std::string msg = e.what();
        StopSession(L"");
        Notify(L"Superior", std::wstring(T(L"Could not start: ", L"تعذّر التشغيل: ")) + std::wstring(msg.begin(), msg.end()));
    }
}

void OnFrame(ID3D11Texture2D* tex, UINT w, UINT h) {
    const double now = NowMs();
    if (g.lastFrameMs > 0.0) g.ema.Add(now - g.lastFrameMs);
    g.lastFrameMs = now;
    ++g.framesRecv;

    DiscardPending((g.srcW && (g.srcW != w || g.srcH != h)) ? present::DropReason::Resize : present::DropReason::NewCapture);
    g.gfx.BeginGpuTiming();
    g.gfx.PushFrame(tex, w, h);
    g.srcW = w;
    g.srcH = h;

    const int maximum = g.st.comfortMode == 0 ? 2 : g.st.maxMultiplier;
    int multiplier = interpolate::ChooseMultiplier(g.ema.Value(), g.displayIntervalMs, g.ema.Count(), maximum);
    const bool canGen = g.st.frameGen && !g.safeMode && g.monitor.Data().allowGeneration && g.gfx.FramesHeld() >= 2 && multiplier > 1;
    g.genActive = canGen;
    if (canGen) g.gfx.ComputeFlow();
    else multiplier = 1;
    const bool planChanged = g.activeMultiplier != multiplier;
    g.activeMultiplier = multiplier;
    g.session.Plan(multiplier);
    if (planChanged) UpdateTip();
    for (const auto& sample : interpolate::Schedule(multiplier, now, g.displayIntervalMs)) {
        const bool generated = sample.phase < 1;
        if (g.session.Schedule(generated)) g.queue.push_back({generated ? Src::Mid : Src::Cur, sample.phase, sample.dueMs});
    }
    g.gfx.EndGpuTiming();
}

void Tick(bool ready) {
    const bool paused = IsIconic(g.target) || GetForegroundWindow() != g.target;
    if (paused != g.paused) {
        g.paused = paused;
        g.session.Pause(paused, NowMs());
        g.gfx.ResetHistory();
        ShowWindow(g.overlay, paused ? SW_HIDE : SW_SHOWNOACTIVATE);
        DiscardPending(present::DropReason::Pause);
        g.ema.Reset();
        g.lastFrameMs = 0;
    }
    g.cap.Poll([&](ID3D11Texture2D* tex, UINT w, UINT h) {
        g.session.Receive(!g.paused);
        if (!g.paused) OnFrame(tex, w, h);
    });

    if (!g.queue.empty() && NowMs() >= g.queue.front().dueMs && g.gfx.Waitable() &&
        (ready || WaitForSingleObject(g.gfx.Waitable(), 0) == WAIT_OBJECT_0)) {
        const auto sample = g.queue.front();
        g.queue.pop_front();
        const double begin = NowMs();
        try {
            const auto result = g.gfx.PresentFinal(sample.source, g.st, sample.phase);
            const auto outcome = result == PresentResult::Submitted ? present::Outcome::Submitted :
                                 result == PresentResult::Occluded ? present::Outcome::Occluded : present::Outcome::OtherStatus;
            if (!g.session.Complete(sample.source == Src::Mid, outcome, sample.dueMs, begin, NowMs()))
                SERR("Session submission accounting mismatch");
        } catch (...) {
            g.session.Complete(sample.source == Src::Mid, present::Outcome::Failed, sample.dueMs, begin, NowMs());
            throw;
        }
    }

    if (g.cap.Closed() || !IsWindow(g.target)) {
        StopSession(T(L"The game window was closed. Superior stopped.", L"أُغلقت نافذة اللعبة. تم إيقاف Superior."));
        return;
    }
    if (g.framesRecv == 0 && !g.warnedNoFrames && NowMs() - g.startMs > 3000.0) {
        g.warnedNoFrames = true;
        Notify(L"Superior",
               T(L"No frames received. If the game uses Exclusive Fullscreen, switch it to Borderless/Windowed and a lower resolution.",
                 L"لا توجد فريمات. إذا اللعبة بوضع Exclusive Fullscreen حوّلها إلى Borderless/Windowed وبدقة أقل."));
    }
}

void ShowMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, CMD_PANEL, L"Superior Control Center");
    AppendMenuW(m, MF_STRING, CMD_START,
                g.running ? T(L"Stop", L"إيقاف") : T(L"Start (3s - click the game now)", L"ابدأ (3 ثوانٍ - اضغط على اللعبة)"));
    AppendMenuW(m, MF_STRING | (g.st.frameGen ? MF_CHECKED : 0), CMD_FG, T(L"Frame generation x2", L"توليد الفريمات x2"));
    AppendMenuW(m, MF_STRING | (g.st.antiAlias ? MF_CHECKED : 0), CMD_AA, T(L"Anti-aliasing", L"تنعيم الحواف"));
    std::wstring o = std::wstring(T(L"OLED mode: ", L"وضع OLED: ")) + OledName();
    AppendMenuW(m, MF_STRING, CMD_OLED, o.c_str());
    AppendMenuW(m, MF_STRING, CMD_LANG, Ar() ? L"English" : L"العربية");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_EXIT, T(L"Exit", L"خروج"));
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(g.main);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | (Ar() ? TPM_LAYOUTRTL : 0), p.x, p.y, 0, g.main, nullptr);
    PostMessageW(g.main, WM_NULL, 0, 0);
    DestroyMenu(m);
}

void ToggleStart(bool delayed) {
    if (g.running) {
        StopSession(T(L"Stopped.", L"تم الإيقاف."));
    } else if (delayed) {
        SetTimer(g.main, kStartTimer, 3000, nullptr);
    } else {
        StartSession(GetForegroundWindow());
    }
}

void CycleOled() {
    g.st.oledLevel = (g.st.oledLevel + 1) % 4;
    SaveAndRefresh();
}

void Command(int id) {
    switch (id) {
        case CMD_PANEL: g.panel.Show(); break;
        case CMD_START: ToggleStart(true); break;
        case CMD_FG: g.st.frameGen = !g.st.frameGen; SaveAndRefresh(); break;
        case CMD_AA: g.st.antiAlias = !g.st.antiAlias; SaveAndRefresh(); break;
        case CMD_OLED: CycleOled(); break;
        case CMD_LANG: g.st.language = Ar() ? "en" : "ar"; SaveAndRefresh(); break;
        case CMD_EXIT: PostMessageW(g.main, WM_CLOSE, 0, 0); break;
        default: break;
    }
}

LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_TRAY:
            if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_LBUTTONUP || LOWORD(l) == WM_CONTEXTMENU) ShowMenu();
            return 0;
        case WM_COMMAND: Command(LOWORD(w)); return 0;
        case WM_TIMER:
            if (w == kStartTimer) {
                KillTimer(h, kStartTimer);
                StartSession(GetForegroundWindow());
            }
            return 0;
        case WM_HOTKEY:
            switch (w) {
                case HK_START: ToggleStart(false); break;
                case HK_OLED: CycleOled(); break;
                case HK_FG: g.st.frameGen = !g.st.frameGen; SaveAndRefresh(); break;
                case HK_OLED_UP: g.st.oledIntensity += 0.1f; SaveAndRefresh(); break;
                case HK_OLED_DOWN: g.st.oledIntensity -= 0.1f; SaveAndRefresh(); break;
                case HK_SHARP_UP: g.st.sharpness += 0.1f; SaveAndRefresh(); break;
                case HK_SHARP_DOWN: g.st.sharpness -= 0.1f; SaveAndRefresh(); break;
                case HK_AA: g.st.antiAlias = !g.st.antiAlias; SaveAndRefresh(); break;
                case HK_VIB_UP: g.st.vibrance += 0.1f; SaveAndRefresh(); break;
                case HK_VIB_DOWN: g.st.vibrance -= 0.1f; SaveAndRefresh(); break;
                case HK_LANG: g.st.language = Ar() ? "en" : "ar"; SaveAndRefresh(); break;
                case HK_QUIT: PostMessageW(h, WM_CLOSE, 0, 0); break;
                default: break;
            }
            return 0;
        case WM_CLOSE:
            if (g.panel.ToolDialogOpen()) {
                g.exitAfterDialog = true;
                g.panel.CancelToolDialog();
                return 0;
            }
            KillTimer(h, kStartTimer);
            g.hotkeys.Clear();
            StopSession(L"");
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g.nid);
            PostQuitMessage(0);
            return 0;
        default: return DefWindowProcW(h, m, w, l);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    g.inst = inst;
    int argumentCount = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments) {
        if (argumentCount == 5 && wcscmp(arguments[1], L"--neural") == 0) {
            std::vector<wchar_t> exe(32768);
            const DWORD count = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
            if (!count || count >= exe.size()) { LocalFree(arguments); return 1; }
            const auto runtime = std::filesystem::path(std::wstring(exe.data(),count)).parent_path() / L"Superior";
            const std::filesystem::path input(arguments[2]), output(arguments[3]), model(arguments[4]);
            LocalFree(arguments);
            return neural::RunOffline(runtime,input,output,model);
        }
        if (argumentCount == 4 && wcscmp(arguments[1], L"--verify") == 0) {
            const std::filesystem::path manifest(arguments[2]), report(arguments[3]);
            LocalFree(arguments);
            return benchmark::Verify(manifest, report);
        }
        for (int i = 1; i < argumentCount; ++i)
            if (wcscmp(arguments[i], L"--safe") == 0) g.safeMode = true;
        LocalFree(arguments);
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    UniqueHandle mutex(CreateMutexW(nullptr, TRUE, L"Superior.SingleInstance"));
    if (!mutex) { MessageBoxW(nullptr, L"Could not create single-instance guard.", L"Superior", MB_ICONERROR); return 1; }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Superior is already running (see the tray icon).", L"Superior", MB_ICONINFORMATION);
        return 0;
    }

    try {
        std::vector<wchar_t> path(32768);
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) throw std::runtime_error("Executable path unavailable");
        auto directory = std::filesystem::path(std::wstring(path.data(), length)).parent_path();
        auto probe = directory / L".superior-write-probe";
        if (!WriteTextAtomic(probe, "write check")) {
            std::vector<wchar_t> local(32768);
            const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
            if (!count || count >= local.size()) throw std::runtime_error("No writable settings location");
            directory = std::filesystem::path(std::wstring(local.data(), count)) / L"Superior";
            std::filesystem::create_directories(directory);
            probe = directory / L".superior-write-probe";
            if (!WriteTextAtomic(probe, "write check")) throw std::runtime_error("Settings location is not writable");
        }
        std::error_code error;
        std::filesystem::remove(probe, error);
        g.dir = directory.wstring() + L"\\";
        if (!Log::Open(ToUtf8(g.dir + L"superior.log"))) throw std::runtime_error("Log file cannot be opened");
        g.settingsPath = ToUtf8(g.dir + L"settings.json");
        if (!LoadSettings(g.settingsPath, g.st)) {
            if (std::filesystem::exists(std::filesystem::path(g.dir) / L"settings.json"))
                MessageBoxW(nullptr, L"Settings could not be loaded. Defaults will be used. Check superior.log before saving changes.", L"Superior", MB_ICONWARNING);
            SLOG("Using default settings (no valid settings file)");
        }
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Superior: storage error", MB_ICONERROR);
        return 1;
    }
    SLOG("Superior starting");

    // Be polite to the game: below-normal CPU priority.
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    int exitCode = 0;
    try {
        winrt::init_apartment();
        g.gfx.InitDevice();
        g.monitor.Init(g.gfx.Device());
        g.panel.Open(inst, g.gfx.Device(), &g.st, SaveAndRefresh, ApplyShortcuts);
    } catch (const winrt::hresult_error& e) {
        SERR("WinRT init failed (0x%08X)", static_cast<unsigned>(e.code().value));
        MessageBoxW(nullptr, e.message().c_str(), L"Superior: Windows runtime error", MB_ICONERROR);
        g.panel.Close();
        return 1;
    } catch (const std::exception& e) {
        SERR("Init failed: %s", e.what());
        const std::string msg = std::string("Superior could not start:\n") + e.what() + "\n\nSee superior.log";
        MessageBoxA(nullptr, msg.c_str(), "Superior", MB_ICONERROR);
        g.panel.Close();
        return 1;
    }

    g.frameEvent.Reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!g.frameEvent) {
        MessageBoxW(nullptr, L"Could not create capture event.", L"Superior", MB_ICONERROR);
        g.panel.Close();
        return 1;
    }

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"SuperiorMain";
    if (!RegisterClassExW(&wc)) {
        SERR("Main window class failed (%lu)", GetLastError());
        g.panel.Close(); return 1;
    }
    WNDCLASSEXW oc{sizeof oc};
    oc.lpfnWndProc = OverlayProc;
    oc.hInstance = inst;
    oc.lpszClassName = L"SuperiorOverlay";
    oc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&oc)) {
        SERR("Overlay window class failed (%lu)", GetLastError());
        g.panel.Close(); return 1;
    }

    g.main = CreateWindowExW(0, L"SuperiorMain", L"Superior", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);

    if (!g.main) {
        MessageBoxW(nullptr, L"Could not create the session control window.", L"Superior", MB_ICONERROR);
        g.panel.Close(); return 1;
    }

    g.nid.cbSize = sizeof g.nid;
    g.nid.hWnd = g.main;
    g.nid.uID = 1;
    g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g.nid.uCallbackMessage = WM_TRAY;
    g.nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!Shell_NotifyIconW(NIM_ADD, &g.nid)) {
        MessageBoxW(nullptr, L"Could not create tray icon. Superior will exit to avoid leaving an inaccessible process.", L"Superior", MB_ICONERROR);
        DestroyWindow(g.main); g.panel.Close(); return 1;
    }
    UpdateTip();

    std::string hotkeyError;
    if (!g.hotkeys.Apply(g.main, g.st.hotkeys, hotkeyError)) {
        SERR("Hotkeys: %s", hotkeyError.c_str());
        Notify(L"Superior", L"Shortcuts unavailable. Use the tray menu and edit shortcuts in Control Center.");
    } else Notify(L"Superior", T(L"Ready. Use your Start / stop shortcut on the game window.", L"جاهز. استخدم اختصار التشغيل على نافذة اللعبة."));

    bool quit = false;
    while (!quit) {
        HANDLE handles[2] = {g.frameEvent.Get(), g.gfx.Waitable()};
        DWORD n = 1;
        if (g.running && !g.queue.empty() && handles[1] && NowMs() >= g.queue.front().dueMs) n = 2;
        DWORD timeout = g.panel.Visible() ? 16 : g.running || g.panel.OfflineBusy() ? 100 : INFINITE;
        if (g.running && !g.queue.empty()) timeout = std::min(timeout, static_cast<DWORD>(std::max(1.0, std::ceil(g.queue.front().dueMs - NowMs()))));
        const DWORD wake = MsgWaitForMultipleObjects(n, handles, FALSE, timeout, QS_ALLINPUT);
        if (wake == WAIT_FAILED) {
            SERR("Message wait failed (%lu)", GetLastError());
            StopSession(L"");
            exitCode = 1; break;
        }
        const bool ready = n == 2 && wake == WAIT_OBJECT_0 + 1;

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                exitCode = static_cast<int>(msg.wParam);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!quit) {
            try {
                if (auto time = g.gfx.PollGpuTime()) g.monitor.RecordGpuTime(*time);
                g.monitor.Poll(NowMs(), static_cast<hardware::Mode>(g.st.comfortMode), g.displayIntervalMs);
                g.panel.SetSessionSnapshot(g.session.Snapshot(NowMs()));
                g.panel.Draw(g.running, g.paused, g.monitor.Data(), g.safeMode);
                if (g.exitAfterDialog && !g.panel.ToolDialogOpen()) {
                    g.exitAfterDialog = false;
                    PostMessageW(g.main, WM_CLOSE, 0, 0);
                }
            } catch (const std::exception& e) {
                SERR("UI error: %s", e.what());
                g.panel.Close();
            }
        }
        if (g.running) {
            try {
                Tick(ready);
            } catch (const std::exception& e) {
                SERR("Runtime error: %s", e.what());
                const std::string msgText = e.what();
                StopSession(L"");
                Notify(L"Superior", std::wstring(T(L"Stopped after an error: ", L"توقف بسبب خطأ: ")) +
                                        std::wstring(msgText.begin(), msgText.end()));
            }
        }
    }

    StopSession(L"");
    g.panel.Close();
    g.hotkeys.Clear();
    if (!SaveSettings(g.settingsPath, g.st)) SERR("Settings save failed on exit");
    SLOG("Superior exit");
    Log::Close();
    g.frameEvent.Reset();
    return exitCode;
}
