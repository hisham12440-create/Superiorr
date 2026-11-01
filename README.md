# Superior — experimental Windows game enhancer

Superior captures a game window with Windows.Graphics.Capture, processes its
image with DirectX 11 and displays it in a click-through borderless overlay.
The blue/black control panel uses Dear ImGui. **This is a prototype, not a
completed implementation of the proposed 13-stage roadmap.**

No injection, hooks, drivers or admin privileges are used. This is **not an
anti-cheat compatibility certification**. No real-GPU image-quality, latency,
startup-time, thermal or power-saving result has been measured here.

## Download and run

Open the successful **build** run for your branch under GitHub **Actions** and
download **Artifacts → Superior**. Extract `Superior-portable.zip` and run
`Superior.exe`. The zip contains this guide, a verifier manifest example, license
notices and `SHA256SUMS.txt`. A checksum is not an Authenticode signature.

1. Set the game to **Borderless/Windowed**, not Exclusive Fullscreen.
2. Click the game, then press the default **Ctrl+Alt+S** to start/stop capture.
   Alternatively select **Start** from the tray menu, then click the game within
   three seconds. The no-frame warning recommends borderless mode; it does not
   reliably distinguish exclusive fullscreen from blocked capture.
3. Close the panel to keep Superior in the tray. Reopen it through **Superior
   Control Center**. Use tray **Exit** or the Quit shortcut to terminate it.
4. The overlay hides while the target is minimized or not foreground. Opening
   Control Center therefore pauses its visible output. Closing the game stops
   capture. Monitor/DPI changes during a session still require stop/restart.

The panel exposes AA, sharpening, vibrance, contrast, saturation, brightness,
gamma, OLED emulation, image presets and before/after split preview. Colour
controls are fused into the final pixel pass; the entire pipeline has **not**
been converted into one compute pass. Presets are aesthetic parameter sets,
not measured quality rankings. The left split is the current real source with
bilinear scaling, not a synchronized quality/latency reference. Adjustments on
a paused capture appear with the next captured/presented frame.

**OLED is an emulation: an LCD cannot produce real OLED blacks.** It provides
local contrast, a smooth black-crush/S-curve and saturation/specular adjustments.
CPU tests cover the scalar reference curve, not every property of the RGB shader.

## Language and shortcuts

Select **Language** in the panel, or use Ctrl+Alt+L by default. Arabic labels use
Windows GDI `DrawTextW` complex-script shaping/bidi with the installed Segoe UI
font. Text aligns right and Arabic labels sit above sliders/combo controls. Text
is not manually reversed. This is a custom RTL label/layout layer, **not a fully
mirrored native ImGui backend**. Technical key names, paths, vendor names, window
title and some OS error details retain English/Latin forms. Mixed-direction
paths, diacritics, fallback fonts and high-DPI appearance need manual validation.
No Arabic font is downloaded or redistributed. UI text textures are unrelated
to game-frame readback. Their cache is limited to 256 entries / approximately
64 MiB between insertions and retained until the current UI frame is submitted.

| Default shortcut | Action |
|---|---|
| Ctrl+Alt+S | Start / stop |
| Ctrl+Alt+G | Toggle frame generation |
| Ctrl+Alt+O | Cycle OLED Off / Light / Medium / Strong |
| Ctrl+Alt+Up / Down | OLED intensity |
| Ctrl+Alt+Right / Left | Sharpness |
| Ctrl+Alt+A | Toggle anti-aliasing |
| Ctrl+Alt+PageUp / PageDown | Vibrance |
| Ctrl+Alt+L | Language |
| Ctrl+Alt+Q | Quit |

**CUSTOM SHORTCUTS** edits all 12 actions. Choose a key and Ctrl/Alt/Shift/Win,
or Disabled. At least one modifier is required; Windows reserves F12. Apply
validates duplicates before registration. If another app owns a combination,
Superior attempts to restore the old set and reports errors. If restoration
also fails, use the tray menu. Editing is a draft until **Apply shortcuts**.

## Settings, game profiles and safety

Settings, `profiles/` and rotating `superior.log` live beside the exe if writable,
otherwise under `%LOCALAPPDATA%\Superior`. This is not automatic migration
between locations. Windows Unicode paths are supported. Reads are size-limited;
settings/profile replacement is atomic and failed replacement cleans its temp
file. Saves use the UI thread; concurrent multi-process writes are unsupported.
The normal log flushes each line but is **not a crash dump or a guarantee
against power-failure data loss**.

**GAME PROFILES** identifies the target through limited process-query rights,
normalizes its full executable path, and loads a versioned profile. Games with
the same executable name in different folders stay separate. The path is stored
locally, never uploaded. Protected processes may deny identification; global
settings are used instead. This does not identify the GPU rendering the game.

While a game profile is active, image/AA/generation/mode edits save for that game;
global image defaults return when capture stops. Language, shortcuts, split
preview and the profiles toggle stay global. The toggle applies next session.
Missing/invalid profiles leave current defaults unchanged; edits may replace them.
The stable hash is a filename mechanism, not a cryptographic/security guarantee.
There is no game launcher or auto-start integration yet.

Run `Superior.exe --safe` to disable interpolation regardless of hotkeys/saved
preferences. Spatial processing remains available; the panel shows a banner.
Event/process/single-instance handles use RAII. Startup checks report event,
class, control-window and tray failures; failed waits exit rather than spin.
Automatic crash recovery, local crash dumps and device-loss reinitialization are
not implemented. The app never changes game caps or system settings.

## Hardware, Comfort and interpolation

The panel reports selected GPU/vendor, dedicated/shared memory and a **heuristic**
VRAM tier. Device selection still prefers most dedicated VRAM, not the game's
adapter. DXGI local-segment **process** usage/budget is polled every second.
Additional allowance is `min(tier cap, budget - usage - 400 MiB)`, saturated at
zero. It is not a measurement of all allocations made by the game. Optional
interpolation stops on battery/power saver, allowance below 32 MiB, safe mode or
timing backoff. Pool shrinking, calibrated caps and hybrid routing are absent.

Comfort (default), Balanced and Max have provisional budgets of 25%, 40%, 65%
of display interval. Asynchronous timestamp queries measure **separate work
groups** (capture/flow, final rendering), not their total per output interval
or end-to-end latency. The panel reports the largest completed group in the
latest poll. This is **not verified enforcement of a total 25% GPU target**.
Overrun disables interpolation; recovery waits for five seconds of sampled
headroom. Incomplete queries use non-flushing checks and never block. No sample
means `n/a`. Temperature, watts, clocks and ghost score remain unavailable.
The panel redraws at approximately 30 Hz to reduce UI work.

The shader block matcher runs in both directions. It checks forward/backward
consistency at the matched block, colour agreement, flow variance, boundary
validity and matching error. Low confidence uses cross-fade; cuts/near-static
pairs select the current real image on the GPU. **Cross-fade can show double
edges.** Flow statistics never use synchronous CPU staging readback. Static
regions still incur a flow dispatch; tile caching/work skipping is absent.

Choose maximum x2, x3 or x4. Comfort caps at x2. Selection requires an integer
capture-delivery/display-interval ratio within 0.15: 30/60 = x2, 40/120 = x3,
30/120 = x4. A x4 ratio under Comfort falls back to real frames rather than
forcing uneven x2 cadence. Capture delivery intervals are not internal game FPS.

Phases 1/m, 2/m, ... are rendered just before presentation and use display-interval
deadlines; the real frame is last. New captures replace old pending work to avoid
stale textures, so overload/jitter can drop queued outputs. This bounds depth,
but **does not prove perfect pacing, presented FPS or latency**. The matcher
is still a 16x16 field, not hierarchical/subpixel flow or a full occlusion-aware
compositor. Bidirectional GPU cost and x3/x4 quality need real laptop testing.

## Offline reference verification — the 99% target

```text
Superior.exe --verify manifest.json report.json
```

See `docs/verification-manifest.example.json`. Supply P5/P2 **8-bit grayscale PGM**
files for a real reference middle frame, generated candidate and two sources.
Relative paths resolve beside the manifest. Limits: 8192x8192/image, 10,000
records, 8 MiB manifest. JSON and `report.json.txt` contain actual computations:

- PSNR on normalized luma; exact matches use JSON null + `exact_match: true`
  to represent infinity without invalid JSON.
- SSIM: non-overlapping 8x8 box windows, population variance, L=1, K1=.01, K2=.03;
  **not Gaussian-window SSIM**.
- Mean absolute difference of normalized Sobel edge magnitude.
- Source-edge/photometric disagreement coverage: a **ghosting proxy** that can
  flag correct motion/disocclusion, not ground-truth ghost labels.

Defaults: SSIM >= .95, edge error <= .05, proxy coverage <= 1%. Target: at least
99% of supplied records pass. Thresholds/target are configurable. Exit codes:
0 = dataset target met; 2 = evaluated but target missed; 1 = invalid input/error.
Outputs cannot overwrite the manifest or input images.

The evaluator **does not record gameplay, generate candidate frames or test the
live shader**. Identical-image fixtures only test metric correctness. No real-game
pass rate has been measured. The UI honestly shows `n/a`; no runtime validator,
safety-net pass-rate loop or claim of achieving 99% has been implemented.

## Optional offline neural SR — not live neural upscale

```text
Superior.exe --neural input.pgm output.pgm model.onnx
```

Put trusted DirectML-enabled ONNX Runtime 1.20.1-compatible `onnxruntime.dll` and
its required runtime/DirectML dependencies inside `Superior/` next to the exe.
DLL lookup is restricted to that folder and Windows system directories. Normal
live capture never loads these components and continues spatial processing when
missing. No models or runtime binaries are bundled/downloaded automatically.

Supply a float32 NCHW luma model with one `[1,1,H,W]` input and one same-aspect
1x..4x output. Input is limited to 1024x1024. Add its license record at
`model.onnx.license.txt`: this is **not automated license verification** or a grant
of model rights. Only trusted models/DLLs should be loaded. Output/report paths
cannot overwrite input, model or license.

The actual DirectML inference command disables ORT telemetry and CPU EP fallback,
uses sequential execution, and writes `output.pgm.report.json` with measured
session/inference **wall times** or an explicit error. These are not GPU stage
measurements or quality claims. No model inference has been verified in CI.
Offline tensors use CPU staging **outside live capture**. Live zero-readback
D3D11/D3D12 interop, tiled inference, reviewed model weights, FP16/INT8
qualification and per-machine quality/performance gating remain unimplemented.

## Build, architecture and test scope

Builds run only in GitHub Actions: Windows x64 Release, CMake, MSVC static CRT
`/MT`, fxc validation, CPU tests, offline CLI smoke tests, portable packaging.
Core uses C++17; the app uses C++20 for C++/WinRT. Image-processing shader bytecode
is compiled at build time and embedded. Dear ImGui still compiles its own small
UI shaders at startup. Dependencies use FetchContent; the optional ORT C API
header is pinned by SHA256. Notices are under `docs/licenses/` (MIT projects
Dear ImGui, nlohmann/json, ONNX Runtime); optional binaries/models have separate
redistribution requirements.

```text
WGC GPU frames -> prev/current textures -> bidirectional block matching
  -> scheduled phase warp / confidence fallback
  -> AA -> spatial upscale (FP16 intermediate) -> colour/OLED/sharpen/dither
  -> flip-model waitable presentation
core/         config, files, hotkeys, profiles, log, timing/pacing
hardware/     DXGI/power monitor and conservative policy
interpolate/  CPU phase scheduling / selection
benchmark/    offline PGM metrics and verification
neural/       optional offline DirectML command
ui/           blue/black ImGui + Windows Arabic text layer
app/          capture, renderer, tray/session orchestration
```

40 CPU tests cover config, reference OLED curve, timing/pacing, budgets/governor,
hotkey validation, Unicode storage/log rotation, profiles, phase scheduling,
analytic metric cases, PGM handling and input preservation. CI also starts the
Windows EXE's verifier on an identical-image fixture and checks the neural
missing-input error path. **A green build is not GPU-quality validation.**

## Manual acceptance checklist / remaining work

- Test capture, game resize/close, stop/start repeatedly, Alt-Tab and minimize.
- Try Arabic connected letters, lam-alef, diacritics, mixed paths and 100/150/200%
  DPI; inspect all dropdowns, sliders and scrolling.
- Apply a custom shortcut, restart, test conflicts and duplicate rejection.
- Verify two games retain separate image profiles and global defaults return.
- Run from an Arabic path and a nonwritable directory; inspect storage fallback.
- Test x2 30/60, x3 40/120, x4 30/120 on NVIDIA/AMD/Intel/hybrid hardware. Inspect
  fast motion, HUD and cuts; record dropped output/presentation cadence.
- Test battery, Comfort and `--safe` behavior; do not infer watt savings.
- Evaluate actual exported reference/candidate pairs; do not use static fixtures
  as proof of interpolation quality.
- Test optional offline inference on a DX12 machine with reviewed luma weights.

Still missing: game-render adapter selection, pools/calibration, thermal/vendor
telemetry, Pareto quality governor, temporal/neural **live** SR, hierarchical
flow, occlusion-aware/layered/higher-order motion, HUD masks, LUTs/full filter set,
recorder/runtime quality validator, installer/signing/release automation and
measured 99% results. No game depth/motion vectors are available; **this is not a
DLSS equivalent**. The overlay and optional components require real Windows/GPU
acceptance testing before relying on them in daily play.
