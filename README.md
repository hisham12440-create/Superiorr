# Superior (v0.1 first prototype)

Real-time game enhancer for Windows 10/11 (DirectX 11). Captures one game window (Windows.Graphics.Capture, no injection, no hooks, no admin) and shows an enhanced image in a click-through overlay.

الاستعمال / Usage
1. Run the game in **Borderless/Windowed** mode, ideally at a lower resolution than your screen (e.g. 1280x720 or 1600x900 on a 1080p screen).
2. Run `Superior.exe` (tray icon appears). Click the game window, then press **Ctrl+Alt+S** (or tray menu > Start, then click the game within 3 seconds).
3. Press **Ctrl+Alt+S** again to stop.

| Hotkey | Action |
|---|---|
| Ctrl+Alt+S | Start/stop on the foreground window |
| Ctrl+Alt+G | Frame generation on/off |
| Ctrl+Alt+O | OLED mode: Off / Light / Medium / Strong |
| Ctrl+Alt+Up / Down | OLED intensity |
| Ctrl+Alt+Right / Left | Sharpness |
| Ctrl+Alt+A | Anti-aliasing on/off |
| Ctrl+Alt+PageUp / PageDown | Vibrance (colour richness) |
| Ctrl+Alt+L | Arabic / English (tray menu) |
| Ctrl+Alt+Q | Quit |

Settings are saved in `settings.json` next to the exe; the log is `superior.log`.

## What is implemented
- Window capture on the GPU, overlay with flip-model waitable swap chain, tray icon, hotkeys, Arabic/English tray menu.
- Image pipeline (3 GPU passes, FP16 intermediate, dithered output):
  1. edge-directed anti-aliasing at source resolution (FXAA-style, optional);
  2. edge-adaptive Lanczos upscale in approximate linear light: 4x4 taps, kernel stretched along edges using a luma structure tensor, de-ringing clamp;
  3. contrast-adaptive sharpening (no halos), vibrance with skin-tone protection, OLED emulation, triangular dither.
- Frame generation x2: block-matching motion search (compute shader), per-pixel confidence (color consistency, flow disagreement, match error, fast-motion guard). Low confidence falls back to a plain cross-fade. Scene cuts and duplicate frames are detected and skipped.
- Picks the GPU with the most VRAM (the NVIDIA GPU on a hybrid laptop).

## Honest limits
- **No ghosting guarantee.** The confidence check reduces ghosting a lot, but "100% ghost-free" cannot be promised for every game. Not measured yet.
- Frame generation only runs when the display refresh is about 2x the game FPS (30 on 60 Hz, 72 on 144 Hz). Cap the game to half your refresh rate.
- No game motion vectors/depth, so this is not a DLSS equivalent. No neural upscaling yet.
- An LCD cannot produce real OLED blacks: OLED mode is a contrast/black-crush emulation.
- Exclusive fullscreen games cannot be captured; use Borderless.
- No ImGui window yet (tray menu + hotkeys only). Hotkeys are fixed.
- **Not yet tested on a real GPU.** Only CPU logic is unit-tested in CI.

## Build
GitHub Actions builds on every push: open **Actions > latest run > Artifacts > Superior** and download `Superior.exe`.

## Control Center (foundation update)

Superior now opens a Dear ImGui control panel with a blue/black theme. Adjust
anti-aliasing, sharpening, vibrance, frame generation and OLED emulation live.
Close the window or choose **Hide to tray** to keep the process running; reopen
it with **Superior Control Center** in the tray menu. Use the tray **Exit** or
Ctrl+Alt+Q to quit. The overlay hides while the target is minimized or not the
foreground window. The panel itself pauses capture display while focused.

### Architecture and data flow

```text
Windows.Graphics.Capture -> GPU source textures -> flow / midpoint
  -> AA -> spatial upscale -> sharpen / colour / OLED -> waitable presentation
core/ : JSON settings, rotating log, pacing and monotonic timing
app/  : WinRT capture, DirectX renderer, tray / session orchestration
ui/   : Dear ImGui Win32 + DX11 Control Center
```

Builds use GitHub Actions only: push the working branch, open its **build** run,
wait for all checks, and download the **Superior** artifact. The zip contains
the portable executable and README. No local compilation is required. The
Windows app uses C++20 for C++/WinRT; portable core logic uses C++17. Static CRT
is enabled. Shader sources are embedded but still compiled at startup.

### Current scope and remaining foundation work

This is a prototype foundation update, **not completion of the 13-stage plan**.
No GPU performance, startup-time, quality or anti-cheat compatibility guarantee
has been measured. Capture-only means no injection or hooks, not certification
by anti-cheat vendors. Flow statistics still perform synchronous CPU readback;
zero-readback flow needs a later GPU-side redesign.

The Control Center currently uses English. The existing Arabic tray strings
remain available via Ctrl+Alt+L, but **full Arabic UI, bidi/contextual shaping
and font embedding are not implemented**. Hotkeys are still fixed, not user
customizable. Monitor/DPI changes during capture require stopping and restarting.
Exclusive fullscreen is not reliably distinguished from blocked/no-frame capture;
the no-frame warning recommends borderless/windowed mode. The complete module
split, robust device-loss recovery, hardware selection and comfort governor are
not implemented. No neural models or vendor runtimes ship with this build.
Settings are saved beside the executable; use a writable folder. Settings writes
now use a temporary file plus atomic replacement; write failures are reported.

### Manual Windows test checklist

- Start from a writable folder, confirm blue/black panel and tray icon.
- Change settings, exit, restart and confirm persistence.
- Start capture on a borderless game with Ctrl+Alt+S; stop and repeat 20 times.
- Verify live sliders, AA toggle and OLED modes; check for blank/frozen output.
- Alt-Tab and minimize the game: overlay must disappear, resume on return.
- Resize the game, then close it; report artifacts or capture failure.
- Try 30 FPS on 60 Hz and 60 FPS on 120 Hz; report pacing and ghosting honestly.
- Repeat on Intel/AMD/NVIDIA hardware and hybrid laptops where available.
- Check `superior.log` after failures. Arabic-path logging remains a known gap.
