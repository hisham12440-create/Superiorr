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
is enabled. Application shaders are compiled by fxc during the Windows build and embedded as
bytecode. The Dear ImGui backend still compiles its own small UI shaders at startup.

### Current scope and remaining foundation work

This is a prototype foundation update, **not completion of the 13-stage plan**.
No GPU performance, startup-time, quality or anti-cheat compatibility guarantee
has been measured. Capture-only means no injection or hooks, not certification
by anti-cheat vendors. Flow statistics now stay on the GPU (see the hardware/Comfort update below).

The Control Center currently uses English. The existing Arabic tray strings
remain available via Ctrl+Alt+L, but **full Arabic UI, bidi/contextual shaping
and font embedding are not implemented**. Hotkeys are still fixed, not user
customizable. Monitor/DPI changes during capture require stopping and restarting.
Exclusive fullscreen is not reliably distinguished from blocked/no-frame capture;
the no-frame warning recommends borderless/windowed mode. The complete module
split, robust device-loss recovery, game-aware hardware selection and full quality-per-millisecond governor are
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

## Hardware / Comfort update

The control panel now displays the selected adapter/vendor, dedicated/shared
memory and heuristic memory tier. DXGI local-segment **process** usage and budget
are polled once per second with a 400 MiB safety margin; this is not a claim to
measure the game's complete allocation or system-wide free VRAM. Optional
interpolation backs off when extra allowance falls below 32 MiB or the system
reports battery / power saver. No system settings or game caps are changed.

Comfort (default), Balanced and Max are saved in settings. Their provisional
budgets are 25%, 40%, 65% of the display interval. Asynchronous D3D11 timestamp
queries measure separate capture/flow and final-render groups (not the sum of
all work per output interval, and not end-to-end latency). The panel reports the
largest completed group in the latest poll; this is **not enforcement of a total
25% GPU target**. Optional interpolation drops immediately on a measured overrun
and recovers only after five seconds of stable sampled headroom. No completed
query means `n/a`, not zero. Temperature, watts, clocks and ghost score remain
`n/a`; there is no thermal prediction without a temperature source.

Flow scene-cut/static counters now stay on the GPU. The midpoint shader reads
them directly and selects the current real image on cuts/near-static pairs.
The old synchronous staging-buffer readback has been removed. Timestamp queries
use non-flushing readiness checks and never wait for unavailable measurements.
This does not implement tile-level caching or eliminate the flow dispatch on
static frames. Capture resize now drops the old surface before pool recreation.

The artifact includes SHA256SUMS.txt for Superior.exe. This is an integrity
checksum, not an Authenticode signature. These changes still require laptop
validation: compare plugged-in vs battery behavior, inspect the measured GPU
group time, resize capture repeatedly and test scene cuts / small HUD updates.

### Status of the requested roadmap

| Area | Current implementation | Not yet implemented / verified |
|---|---|---|
| Foundation | WinRT GPU capture, tray, blue/black ImGui, settings, CPU timing tests | Full Arabic shaping/RTL, configurable hotkeys, device-loss recovery |
| Hardware | Adapter identity, heuristic VRAM tier, DXGI budget, battery policy | Game-render adapter detection, hybrid routing, pools, calibration, NVML/ADLX |
| Comfort | Mode budgets, asynchronous group timing, interpolation backoff | Measured Pareto quality selection, tiles, thermal prediction |
| Upscale | Spatial edge-adaptive upscale, sharpening, FP16 intermediate | Temporal/neural SR, history, licensed model integration |
| Frame generation | Shader block matching, x2 midpoint, GPU cut/static fallback | Bidirectional/hierarchical flow, x3/x4, HUD masks, runtime ghost validation |
| Filters | AA, sharpening, vibrance, OLED emulation | LUTs, full filter set, fused compute conversion, per-game presets |
| Benchmark | CPU logic tests only | Clip recorder, real ground truth, measured 99% pass rate, exports |
| Distribution | Windows CI portable exe/zip, SHA256 | Installer, release tags workflow, signing, safe mode |

A green build validates compilation and CPU tests, not real GPU quality. None of
the unimplemented features in this table is represented by a placeholder result.

## Colour controls and safe startup

Contrast (0.5–2), saturation (0–2), brightness (-0.25–0.25), and gamma (0.5–2)
now run inside the existing final pixel-shader pass, after OLED processing and
before dither. They are saved with the other settings. This is pass fusion, not
a conversion of the entire pipeline to a compute shader. Presets **Clean,
Vivid, Cinematic, Competitive, OLED** apply explicit colour/sharpness parameters;
these are aesthetic choices, not measured quality rankings. Presets do not
change the operating mode, language or generation preference.

**Before / after split** shows the current real frame with basic bilinear scaling
on the left and the enhanced pipeline on the right (including a generated frame
if selected). It is a visual comparison, not a synchronized quality metric or
latency benchmark. On a static paused capture, adjustments become visible with
the next captured/presented frame. The control window is resizable and scrolls
on small displays.

Launch `Superior.exe --safe` to force interpolation off regardless of the saved
setting or hotkey. Spatial upscale and image filters remain available. The panel
shows a safe-mode banner. This does not implement automatic crash recovery or
a neural-runtime loader. Capture history is invalidated when focus changes so
old frames are not interpolated into a resumed session.

The earlier roadmap table's filter/distribution rows are superseded only by
these colour controls, presets, split preview and safe-mode additions. LUTs,
per-game profiles, learned models, full RTL and the 99% validation corpus are
still absent. No real-GPU pass rate or watt-saving number is available.

## Custom shortcuts, game profiles and writable storage

**CUSTOM SHORTCUTS** in Control Center edits all 12 global actions using a key
selector and Ctrl/Alt/Shift/Win toggles. Choose Disabled to remove an action.
At least one modifier is required. F12 is excluded because Windows reserves it.
The editor is a draft until **Apply shortcuts** is pressed. Duplicate or invalid
bindings are rejected before touching the active set. If Windows rejects a
registration (for example another app owns it), Superior attempts to restore the
old set and displays the failure; if restoration also fails, use the tray menu.
The default shortcuts in the usage table are defaults, not fixed bindings.

**GAME PROFILES** enables automatic image-profile loading when capture starts.
The target executable is queried with limited process information rights, without
admin privileges or injection. Its normalized full path identifies the profile,
so two executables with the same name in different folders remain separate.
The full executable path is stored locally in the profile; it is never uploaded.
Protected processes may deny the query; Superior reports that condition and uses
global settings without creating a game profile. This identification does not
identify which GPU renders the game.

While a game profile is active, edits to colour, OLED, AA, frame generation and
operating mode save to `profiles/<stable-key>.json`. Global image defaults return
when capture stops. Language, shortcuts, split preview and the profiles toggle
stay global. The toggle applies to the **next** session. Missing/invalid profiles
leave current image defaults unchanged; edits can then create a replacement.
Profile files have a schema version and verify the stored executable identity
before applying any settings. Profile hashing is a filename mechanism, not a
security or cryptographic guarantee. There is no auto-start/game launcher yet.

Superior first checks whether it can write beside its executable. If not, it
uses `%LOCALAPPDATA%\Superior`. Settings, profiles and logs all use that selected
location. This is a storage fallback, not automatic migration between locations.
Windows Unicode paths are used for settings, profiles **and rotating logs**.
Settings/profile files are size-limited on read and replaced atomically; failed
replacement cleans its temporary file. Saves occur on the UI thread and are not
intended for concurrent multi-process writers.

The control panel redraw is capped at roughly 30 Hz to avoid unnecessary UI GPU
work. Resuming a game still invalidates interpolation history. Capture-event,
process-query and single-instance handles now have RAII ownership. Startup
checks for event, window class, control-window and tray creation failures; it
exits rather than leaving an invisible app with no tray controls. Failed message
waits exit instead of spinning. WinRT initialization errors have a visible
Windows error dialog. The ordinary log flushes each line but is **not a crash
dump or a guarantee against data loss on power failure**.

### Verification for this update

CPU tests cover key validation/duplicates and JSON round trips, per-game profile
isolation, corrupt/mismatched profile rejection, preservation of global
preferences, bounded file reads, atomic replacement and failed-replacement
cleanup, Unicode settings filenames and Unicode log rotation. Windows hotkey
registration, permissions fallback and game integration require manual testing:

1. Change Start/stop to Ctrl+Shift+F2, apply, restart, and test on a game.
2. Try a duplicate binding: the editor must reject it without changing shortcuts.
3. Occupy a shortcut in another app and confirm the error and rollback behavior.
4. Save different image settings for two games; verify restore when switching.
5. Stop capture and verify global image defaults return; language/hotkeys stay.
6. Run from an Arabic-named directory, check saved settings and `superior.log`.
7. Run from a nonwritable directory, verify files under LocalAppData\Superior.
8. Verify slider edits during a game profile do not overwrite global image defaults.

The roadmap's earlier statements about fixed shortcuts, absent per-game image
profiles and Arabic-path logging are superseded by this section. Full shaped
Arabic/RTL UI, neural SR, bidirectional flow, real quality benchmarking and the
99% validation target remain unfinished and unmeasured.

## Arabic rendering, bidirectional interpolation and offline evaluation

### Arabic / English

Choose **Language** in Control Center (or the tray language shortcut). Arabic
labels are rendered into a bounded UI texture cache through Windows GDI
`DrawTextW` complex-script shaping and bidi, using the installed Segoe UI font.
Text/paragraphs align right; Arabic labels sit above numeric sliders and combo
controls. The original Unicode strings are not reversed. Numeric values,
Ctrl/Alt/Shift/Win names, file paths and vendor/model names retain their native
forms. No externally downloaded font is required. The image textures belong to
the settings UI only; this is not readback of captured game frames.

This is a custom RTL label/layout layer, not a native mirrored ImGui backend.
Mixed-direction paths, Arabic diacritics, font fallback and high-DPI layout need
manual Windows verification. OS error details can remain English. The underlying
Windows window title and some technical terms are unchanged. Texture cache is
capped at 256 entries / approximately 64 MiB between insertions, and current-frame
references remain alive through rendering. No screenshot-based localization
verification has been performed in CI.

### Bidirectional x2 / x3 / x4 (experimental)

The existing shader block matcher now runs in **both** directions. Interpolation
checks forward/backward consistency at the matched block location, boundary
validity, colour disagreement, local flow variance and matching error. Failed
confidence returns to cross-fade; scene cuts/near-static pairs use the current
real image. Cross-fade itself can show double edges; this is not ghost-free.
This is still a 16x16 block field, not a hierarchical/subpixel optical-flow engine
or a full occlusion-aware compositor.

Select a maximum multiplier from 2 through 4. Comfort caps at 2; battery, safe mode,
memory pressure and timing backoff can disable generation. Multipliers are chosen
only near an integer ratio of capture-delivery interval to display interval
(within 0.15). Examples: 30 to 60 Hz = x2, 40 to 120 Hz = x3, 30 to 120 Hz = x4.
A x4 ratio under Comfort falls back to real frames instead of forcing an uneven
x2 cadence. The timing samples are delivery times, not the game's internal FPS.

Generated phases are rendered just before presentation at 1/m, 2/m, ... and
separated by display-interval deadlines. The real frame is last. A newly captured
pair replaces pending work from an older pair to keep textures/history consistent;
under jitter or overload pending outputs can be dropped. This bounds queue depth,
but **does not prove perfect pacing**, presented FPS or latency. Bidirectional
search costs more GPU time and is unmeasured on actual laptops.

### Ground-truth quality evaluation

The same portable EXE supports a CPU-only offline command (no GPU initialization):

```text
Superior.exe --verify manifest.json report.json
```

Use `docs/verification-manifest.example.json` as the manifest shape. Supply
8-bit normalized grayscale PGM images (P5 binary or P2 ASCII) for each actual
reference middle frame, generated candidate and two source frames. Relative image
paths resolve beside the manifest. Limits: 8192x8192 per image, 10,000 frame records,
8 MiB manifest. A JSON report and `report.json.txt` are written. Exit codes:
0 = dataset target met; 2 = evaluated but target missed; 1 = invalid input/error.

Metrics are real computations on the supplied images:
- PSNR on normalized luma; exact matches use JSON null plus `exact_match: true`
  to represent infinite PSNR without invalid JSON.
- SSIM using non-overlapping 8x8 box windows, population variance, L=1,
  K1=.01 and K2=.03 (not Gaussian-window SSIM).
- Mean absolute difference of normalized Sobel edge magnitude.
- Source-edge/photometric disagreement coverage, explicitly a **ghosting proxy**.
  Real motion/disocclusions can trigger it even in a correct frame.

Default pass rule: SSIM >= .95, edge error <= .05, proxy coverage <= 1%.
The target is 99% of supplied records passing. Thresholds and target are
configurable. This command **does not create generated frames, record gameplay or
validate the live shader**. Metrics tests verify formulas/reference cases, not
99% quality of interpolation. No real-game dataset result has been measured and
the UI therefore shows `n/a`, never a fabricated live pass rate. Do not interpret
a result on duplicate/static fixtures as performance on games.

### Optional offline neural SR / DirectML

```text
Superior.exe --neural input.pgm output.pgm model.onnx
```

This is a real optional ONNX Runtime DirectML inference path for **offline luma
images**, not a live neural checkbox. Put a trusted DirectML-enabled ONNX Runtime
1.20.1-compatible `onnxruntime.dll` and its required DirectML/runtime dependencies
inside `Superior/` next to the EXE. DLL resolution is restricted to that directory
and Windows system directories. The live app never loads these DLLs and continues
using spatial upscale when they are absent.

Supply a model with one float32 NCHW `[1,1,H,W]` input and one same-aspect
`[1,1,sH,sW]` output, scale 1–4. Input is limited to 1024x1024. Place its license
text at `model.onnx.license.txt`. This is a required local record, **not automated
license verification**. No model, model license rights, or runtime DLLs are bundled
or downloaded automatically. ONNX Runtime's C API header is pinned by SHA256 at
build time (MIT project: https://github.com/microsoft/onnxruntime).

The command disables ORT telemetry and CPU execution-provider fallback, uses
sequential execution, and exports `output.pgm.report.json` with measured wall time
for session creation and inference or an explicit error. These are not GPU-stage
measurements or quality claims. Only trusted models/DLLs should be loaded.
Offline tensors use CPU staging by design; **this path is never called from live
capture**. Live zero-readback D3D11/D3D12 tensor interop, tiled neural SR, model
license review, FP16/INT8 qualification and per-machine quality/performance gates
remain unimplemented. Missing/incompatible optional components do not stop the
normal app from starting.

### Required manual checks

- Switch Arabic/English repeatedly; inspect connected Arabic letters, lam-alef,
  mixed Arabic/Latin paths, dropdowns, scrolling and scaling at 100/150/200% DPI.
- Try x2 on 30/60, x3 on 40/120, x4 on 30/120; inspect fast motion, cuts and HUD.
  Check Comfort, battery and safe-mode fallback. Record capture/present cadence.
- Evaluate actual exported candidate/reference frame pairs with `--verify`.
- Test `--neural` with missing dependencies, then a reviewed luma model on a DX12
  machine. No neural model run has been verified by this repository's CPU CI.

This section supersedes earlier x2-only/Arabic-absent status text, but it does not
mean all 13 requested stages are complete. Runtime per-generated-frame validation,
corpus recording, live learned SR, layered/higher-order flow and a verified 99%
pass rate are still missing. The application remains experimental.
