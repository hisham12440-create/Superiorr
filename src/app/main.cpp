#include <unknwn.h>
#include <windows.h>
#include <shellapi.h>
#include <winrt/Windows.Foundation.h>

#include <deque>
#include <exception>
#include <stdexcept>
#include <string>

#include "app/capture.h"
#include "app/renderer.h"
#include "core/config.h"
#include "core/log.h"
#include "core/pacing.h"

using namespace superior;

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT_PTR kStartTimer = 1;
enum Cmd { CMD_START = 101, CMD_FG, CMD_OLED, CMD_LANG, CMD_EXIT };
enum Hot { HK_START = 1, HK_OLED, HK_FG, HK_OLED_UP, HK_OLED_DOWN, HK_SHARP_UP, HK_SHARP_DOWN, HK_LANG, HK_QUIT };

struct App {
    HINSTANCE inst = nullptr;
    HWND main = nullptr;
    HWND overlay = nullptr;
    HWND target = nullptr;
    std::wstring dir;
    std::string settingsPath;
    Settings st;
    Renderer gfx;
    Capture cap;
    HANDLE frameEvent = nullptr;
    NOTIFYICONDATAW nid{};

    bool running = false;
    std::deque<Src> queue;
    IntervalEma ema;
    double lastFrameMs = 0.0;
    double displayIntervalMs = 16.667;
    double displayHz = 60.0;
    double startMs = 0.0;
    uint64_t framesRecv = 0;
    bool warnedNoFrames = false;
    UINT srcW = 0, srcH = 0;
    bool genActive = false;
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
    swprintf_s(buf, L"Superior | %s %s | OLED %s %d%% | %s %d%% | FG %s", g.running ? L"ON" : L"idle",
               g.running && g.genActive ? L"x2" : L"", OledName(), static_cast<int>(g.st.oledIntensity * 100.0f + 0.5f),
               T(L"Sharp", L"حدة"), static_cast<int>(g.st.sharpness * 100.0f + 0.5f),
               g.st.frameGen ? L"on" : L"off");
    g.nid.uFlags = NIF_TIP;
    wcsncpy_s(g.nid.szTip, buf, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

void SaveAndRefresh() {
    g.st.Clamp();
    SaveSettings(g.settingsPath, g.st);
    UpdateTip();
}

LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST) return HTTRANSPARENT;  // click-through
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, m, w, l);
}

void StopSession(const wchar_t* why) {
    if (!g.running && !g.overlay) return;
    g.cap.Stop();
    g.gfx.DestroyOutput();
    if (g.overlay) {
        DestroyWindow(g.overlay);
        g.overlay = nullptr;
    }
    g.queue.clear();
    g.running = false;
    g.genActive = false;
    SLOG("Session stopped: %s", ToUtf8(why).c_str());
    UpdateTip();
    if (why && *why) Notify(L"Superior", why);
}

void StartSession(HWND target) {
    if (g.running) return;
    wchar_t cls[64] = {};
    if (target) GetClassNameW(target, cls, 64);
    const bool bad = !target || !IsWindow(target) || !IsWindowVisible(target) || target == g.main ||
                     !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") ||
                     !wcscmp(cls, L"SuperiorOverlay");
    if (bad) {
        Notify(L"Superior", T(L"Click the game window first, then start again.", L"اضغط على نافذة اللعبة أولاً ثم ابدأ مجدداً."));
        return;
    }
    try {
        HMONITOR mon = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof mi;
        GetMonitorInfoW(mon, &mi);
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
        if (!g.cap.Start(target, g.gfx.Device(), g.frameEvent, err)) {
            throw std::runtime_error("Capture failed: " + err);
        }
        g.target = target;
        g.running = true;
        g.queue.clear();
        g.ema.Reset();
        g.lastFrameMs = 0.0;
        g.framesRecv = 0;
        g.warnedNoFrames = false;
        g.startMs = NowMs();
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

    g.gfx.PushFrame(tex, w, h);
    g.srcW = w;
    g.srcH = h;

    g.queue.clear();
    const bool canGen = g.st.frameGen && g.gfx.FramesHeld() >= 2 &&
                        ShouldGenerate(g.ema.Value(), g.displayIntervalMs, g.ema.Count());
    g.genActive = false;
    if (canGen) {
        const FlowStats fs = g.gfx.ComputeFlow();
        if (IsStaticFrame(fs.staticBlocks, fs.totalBlocks)) {
            return;  // duplicate frame: skip all work, the last image stays on screen
        }
        if (!IsSceneCut(fs.badBlocks, fs.totalBlocks)) {
            g.gfx.RenderMid();
            g.queue.push_back(Src::Mid);
            g.genActive = true;
        }
    }
    g.queue.push_back(Src::Cur);
}

void Tick() {
    g.cap.Poll(OnFrame);

    if (!g.queue.empty() && g.gfx.Waitable() && WaitForSingleObject(g.gfx.Waitable(), 0) == WAIT_OBJECT_0) {
        const Src s = g.queue.front();
        g.queue.pop_front();
        g.gfx.PresentFinal(s, g.st);
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
    AppendMenuW(m, MF_STRING, CMD_START,
                g.running ? T(L"Stop", L"إيقاف") : T(L"Start (3s - click the game now)", L"ابدأ (3 ثوانٍ - اضغط على اللعبة)"));
    AppendMenuW(m, MF_STRING | (g.st.frameGen ? MF_CHECKED : 0), CMD_FG, T(L"Frame generation x2", L"توليد الفريمات x2"));
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
        case CMD_START: ToggleStart(true); break;
        case CMD_FG: g.st.frameGen = !g.st.frameGen; SaveAndRefresh(); break;
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
                case HK_LANG: g.st.language = Ar() ? "en" : "ar"; SaveAndRefresh(); break;
                case HK_QUIT: PostMessageW(h, WM_CLOSE, 0, 0); break;
                default: break;
            }
            return 0;
        case WM_CLOSE:
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
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Superior.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Superior is already running (see the tray icon).", L"Superior", MB_ICONINFORMATION);
        return 0;
    }

    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    g.dir = path;
    g.dir.resize(g.dir.find_last_of(L'\\') + 1);
    Log::Open(ToUtf8(g.dir + L"superior.log"));
    g.settingsPath = ToUtf8(g.dir + L"settings.json");
    LoadSettings(g.settingsPath, g.st);
    SLOG("Superior starting");

    // Be polite to the game: below-normal CPU priority.
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    int exitCode = 0;
    try {
        winrt::init_apartment();
        g.gfx.InitDevice();
    } catch (const std::exception& e) {
        SERR("Init failed: %s", e.what());
        const std::string msg = std::string("Superior could not start:\n") + e.what() + "\n\nSee superior.log";
        MessageBoxA(nullptr, msg.c_str(), "Superior", MB_ICONERROR);
        return 1;
    }

    g.frameEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"SuperiorMain";
    RegisterClassExW(&wc);
    WNDCLASSEXW oc{sizeof oc};
    oc.lpfnWndProc = OverlayProc;
    oc.hInstance = inst;
    oc.lpszClassName = L"SuperiorOverlay";
    oc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&oc);

    g.main = CreateWindowExW(0, L"SuperiorMain", L"Superior", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);

    g.nid.cbSize = sizeof g.nid;
    g.nid.hWnd = g.main;
    g.nid.uID = 1;
    g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g.nid.uCallbackMessage = WM_TRAY;
    g.nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    Shell_NotifyIconW(NIM_ADD, &g.nid);
    UpdateTip();

    const UINT mod = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
    struct { int id; UINT vk; } keys[] = {{HK_START, 'S'},     {HK_OLED, 'O'},        {HK_FG, 'G'},         {HK_OLED_UP, VK_UP},
                                          {HK_OLED_DOWN, VK_DOWN}, {HK_SHARP_UP, VK_RIGHT}, {HK_SHARP_DOWN, VK_LEFT}, {HK_LANG, 'L'},
                                          {HK_QUIT, 'Q'}};
    for (auto& k : keys) {
        if (!RegisterHotKey(g.main, k.id, mod, k.vk)) SERR("RegisterHotKey %d failed (%lu)", k.id, GetLastError());
    }
    Notify(L"Superior", T(L"Ready. Ctrl+Alt+S starts/stops on the game window.", L"جاهز. Ctrl+Alt+S للتشغيل/الإيقاف على نافذة اللعبة."));

    bool quit = false;
    while (!quit) {
        HANDLE handles[2] = {g.frameEvent, g.gfx.Waitable()};
        DWORD n = 1;
        if (g.running && !g.queue.empty() && handles[1]) n = 2;
        MsgWaitForMultipleObjects(n, handles, FALSE, g.running ? 100 : INFINITE, QS_ALLINPUT);

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                exitCode = static_cast<int>(msg.wParam);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g.running) {
            try {
                Tick();
            } catch (const std::exception& e) {
                SERR("Runtime error: %s", e.what());
                const std::string msgText = e.what();
                StopSession(L"");
                Notify(L"Superior", std::wstring(T(L"Stopped after an error: ", L"توقف بسبب خطأ: ")) +
                                        std::wstring(msgText.begin(), msgText.end()));
            }
        }
    }

    for (const auto& k : keys) UnregisterHotKey(g.main, k.id);
    SaveSettings(g.settingsPath, g.st);
    SLOG("Superior exit");
    CloseHandle(g.frameEvent);
    if (mutex) CloseHandle(mutex);
    return exitCode;
}
