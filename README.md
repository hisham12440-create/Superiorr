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
| Ctrl+Alt+L | Arabic / English (tray menu) |
| Ctrl+Alt+Q | Quit |

Settings are saved in `settings.json` next to the exe; the log is `superior.log`.

## What is implemented
- Window capture on the GPU, overlay with flip-model waitable swap chain, tray icon, hotkeys, Arabic/English tray menu.
- Upscaling (Catmull-Rom) + halo-limited sharpening + OLED emulation in one pixel pass.
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
