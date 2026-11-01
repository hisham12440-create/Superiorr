# Superior — experimental Windows game enhancer

Superior captures a game window with Windows.Graphics.Capture, processes its
image with DirectX 11 and displays it in a click-through borderless overlay.
The blue/black control panel uses Dear ImGui. **This is a prototype, not a
completed implementation of the proposed 13-stage roadmap.**

No injection, hooks, drivers or admin privileges are used. This is **not an
anti-cheat compatibility certification**. No real-GPU image-quality, latency,
startup-time, thermal or power-saving result has been measured here.

See [the development plan](docs/DEVELOPMENT_PLAN.md) for priorities, acceptance
criteria and the remaining work.

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
Relative paths resolve beside the manifest. Limits: 8192 maximum dimension and 16,777,216 pixels/image, 10,000
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
0 = target and optional baseline met; 2 = target/baseline missed;
1 = invalid input/error; 3 = cancelled.
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
present/      API-side session counters and JSON snapshots
benchmark/    offline PGM metrics and verification
neural/       optional offline DirectML command
ui/           blue/black ImGui + Windows Arabic text layer
app/          capture, renderer, tray/session orchestration
```

56 CPU tests cover config, reference OLED curve, timing/pacing, budgets/governor,
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


## Verification panel update

Open **Quality verification → Choose manifest and run verification**, choose the
reference JSON and an output report path. One below-normal-priority CPU worker
runs independently of UI rendering. Progress counts completed frame records.
Cancel is cooperative at file-read/metric checkpoints; closing to tray keeps the
job running, exiting cancels and joins it. Capture cannot start while a job or
its file picker is active; tools cannot start during capture. Reports remain
local. **Copy report path** copies the selected destination, which may contain an
error/cancellation report rather than results. OS error details may be English.

Each manifest record may supply `content_class` (1–64 UTF-8 bytes, default
`unclassified`). Schema-2 JSON and text reports summarize actual pass counts,
SSIM and edge error per supplied class; no automatic classifier is implied.
Optional `baseline_percent` (0–100) adds a non-regression gate: `accepted` requires
both target and baseline. `target_achieved` alone does not imply acceptance.
Compare only like datasets, thresholds and configurations; the current scalar
baseline gate does not prove corpus identity. Missing/invalid records abort the
evaluation rather than being skipped. Cancellation exports no final pass rate. Once output/input path preflight has
completed, the text report is replaced with an incomplete-status notice too.
Earlier preflight failures/cancellation leave existing report files untouched;
the UI status/exit code is authoritative, not an older file at the destination.

PGM reads use a row buffer, and Sobel metrics are accumulated without four
full-size double edge maps. Four maximum-size float images require approximately
256 MiB of pixel storage (not a measured total-process working set). The UI,
JSON, optional runtimes and driver allocations are additional. This is still an
offline evaluator, not a recorder or a live shader quality test. GUI responsiveness,
file pickers, shutdown/cancellation and real-GPU behavior need manual validation.


## Session observations and export

The **SESSION OBSERVATIONS** panel shows the current or most recently stopped
session. Counts reset only after a new session starts. It separates delivered
capture callbacks, frames accepted for processing, and callbacks ignored while
paused. WGC frames discarded inside the capture pool are not observable here;
these counters are **not game-engine frame counts**.

Each real/generated path accounts for scheduled, S_OK-submitted, occluded,
other successful status, failed, discarded and pending outputs. Discards are
classified by replacement on a new capture, pause, source resize or stop. Stop
drains pending work. CPU tests enforce conservation of the counts. Generated
path submissions may select a real-frame/cross-fade shader fallback; they do
not count successfully interpolated or ghost-free images.

Timing samples are CPU wall time around rendering + the Present call, lateness
at the beginning of that work versus its planned deadline, and intervals
between S_OK returns. They are **not GPU execution time, scan-out intervals or
input-to-photon latency**. Pauses do not bridge interval samples. Active and
paused wall times are separate; stopped snapshots no longer advance. Missing
samples export as null. No total GPU budget or output FPS claim is inferred.

**Export session JSON** freezes the snapshot visible when export was requested,
then uses a native save dialog. S_OK means DXGI accepted a call, not proof of
screen display. The report deliberately contains `scanout_fps: null` and
`ground_truth_pass_rate: null`. It includes no game image, executable path or
clip. Export is local and does not upload telemetry. Concurrent offline tools
and save dialogs are disallowed; exiting while a dialog is open closes it before
tearing down the panel. File picker/export interaction requires manual Windows
validation. JSON snapshots are for diagnosis, not benchmark scores.

Manual checks: run x2/x3/x4 sessions, pause/resume, resize the source, replace
pending work with a faster capture, and stop with a nonempty queue. Export before
and after stopping; verify each path's scheduled count equals submitted +
occluded + other + failed + discarded + pending. Test Arabic labels and exiting
from the tray while the export dialog is open. Device-loss recovery and real
screen presentation measurements remain future work in the development plan.

### Control Center: pages, eight filters and OLED

The blue/black panel now has a persistent navigation rail: **Overview**, **Image filters**,
**OLED display**, **Quality tools**, and **Preferences**. The rail moves to the right in
Arabic. Each page scrolls independently; filters use two columns when space permits.
Profiles, shortcut editing, session JSON export and offline verification remain available.

Eight actual shader controls: anti-aliasing (with strength), sharpness, contrast,
saturation, brightness, gamma, vibrance and warm/cool **color temperature**. Each has
an independent bypass that keeps its slider value. Temperature is an artistic RGB gain,
not calibrated Kelvin or HDR conversion; it is fused into the existing final pass.
New values and bypasses are saved globally and in game profiles, with defaults for old JSON.

The separate OLED page offers **Light / Medium / Strong**, an enable switch, intensity,
and an OLED-only reset. Selecting a level enables it; zero intensity now bypasses OLED
exactly (before final dithering). This simulates tone response; it cannot turn an LCD into
an OLED. The four image presets no longer change OLED or AA preferences. Filter-only reset
also leaves OLED intact. The Preferences reset retains its existing image/session scope.
Windows CI validates compilation, shaders and CPU references, not the rendered appearance;
Arabic shaping, DPI, window resizing and real GPU output still require manual Windows testing.

### Expansion 1: setup guidance and portable settings

**Setup guide** adds a bilingual in-app walkthrough: borderless preparation, safe x2
starting point, shortcut/tray launch, comparison, session reporting and troubleshooting.
Readiness observations show the already-initialized DX11 UI, known/unknown DXGI budget
and safe-mode restriction. They do not probe WGC compatibility, certify anti-cheat
acceptance or invent a benchmark. Device-loss recovery is still not implemented.

**Preferences → Share image and session settings** exports/imports a version-1
`superior-settings` JSON bundle through native file pickers. Capture and offline work
must be stopped. Import replaces image/session values (including split preview), but
preserves language, key bindings and automatic-profile preference. Importing does not
start capture. Export excludes these local preferences, paths and hardware identities.

Imports require the complete known v1 field set and correct types; unknown fields,
future versions, excessive nesting, malformed files and files above 64 KiB are rejected
without changing settings. Numeric image ranges are clamped as in local settings.
Exports use atomic replacement and the picker asks before overwriting. Local config
JSON, session reports and verifier manifests are not interchangeable with bundles.
This adds functionality, not filler; no 20–50 MB size claim is made.
