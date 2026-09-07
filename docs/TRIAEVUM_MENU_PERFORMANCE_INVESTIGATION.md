# Menu-era performance investigation (2026-09-06)

## Result and limits

A severe **diagnostic-mode** bottleneck is reproduced and fixed. The ordinary
gameplay regression reported by the user is **not yet reproduced reliably**.
Do not claim that repairing diagnostics proves the user's normal launch fixed.

Renderer commit `7092f721` removes full-history JSON serialization from the
frame loop. Baseline for this investigation: root `875a6751c`, renderer
`0ba40262`. The installed, unmodified September 5 candidate executable is the
pre-CACAO/F1 comparison artifact; the same installed title plugin is used in
both runs. No gameplay/AOT rebuild or graphics quality reduction was made.

## Reproduced cause

`Oot3dVulkanDiagnostics::EndFrame()` wrote the complete growing frame history
to disk. `SetGpuTimings()` wrote the same history again when delayed GPU
timestamps arrived. Both ran on the render thread. Phase reports incorrectly
made the time look like large FrameStart/Present costs, even with validation
and VSync disabled. This cost predates the menu refactor, but affects the
instrumented runs used to verify it. It is not evidence of slow game logic.

Normal diagnostic capture now retains its bounded in-memory history and delayed
GPU samples, and writes the existing JSON schema on explicit `Flush()` and
renderer shutdown. No worker thread or renderer scheduling changes are needed.
For crash/hang investigation only, `OOT3D_VULKAN_DIAGNOSTICS_FLUSH_INTERVAL=N`
opts into a synchronous snapshot every N frames. Default 0 means no periodic
disk writes. Nonzero values can cause stalls and invalidate throughput results.
Forced termination with default settings may lose the final in-memory capture;
bounded tests should let the runtime exit normally.

## Measurements

Private artifacts: `I:/oot3dre_work/menu-performance/`. Checkpoint:
`I:/oot3dre_work/native_game/checkpoints/hudtest.oot3dsav`, confirmed by an actual
framebuffer capture to be playable Kokiri Forest beside Saria. Config copied
from the installed profile: 2560x1440 output, scale 1, CACAO Low, custom grass
settings retained, TopScreen, audio, visual x2. No grass is generated in the
stationary framing: this is not a high-density grass stress qualification.

| Run directory | Measured result | Meaning |
| --- | --- | --- |
| baseline-01 | 64.44 full native frames/s | Before menu changes; 180 frames after 60 warmup |
| current-01 | 64.74 | Before this fix, diagnostics off, same conditions |
| current-menu-01 | 62.38 | F1 open, before this fix, diagnostics off |
| current-diag-01 | 8.04 | Before fix, diagnostics on, validation off |
| fixed-diag-02 | 56.56 | After fix, diagnostics on; about 7x diagnostic throughput |
| baseline-motion-01 | 60.04 | 240 full native frames after 120 warmup, deterministic movement |
| fixed-motion-02 / fixed-motion-03 | 52.50 / 62.37 | Repeated movement test; variation prevents claiming a stable ordinary-path speedup/regression |
| fixed-paced-01 | 60.00 presentations/s | Ordinary wall-clock x2, VSync on, 754 measured frames |
| fixed-paced-menu-01 | 59.88 | Same wall-clock playback with F1 open, 746 measured frames |
| uncapped-play-02 | 123.56 presentations/s | Wall-clock gameplay, adaptive interpolation, no VSync/pacer; 2,142 measured frames after 240 warmup |

The paced run produced 439 logical game frames and 874 presentations overall,
with 437 interpolation executions. This is not a claim of 60 Hz game logic.
Full-native-frame throughput intentionally does not measure duplicated or
interpolated presentations. The runtime benchmark window excludes warmup and
initial loading; aggregate phase timings include warmup and must not be divided
by the measured-only frame count.

The uncapped playback test retains the installed visual settings and audio, with
no capture/validation/dump, using `mode=play uncapped=true frames=0 seconds=20
warmup=240 implicitLayers=true`. The run reports adaptive interpolation with zero
clamped repeated samples, approximately 30 logical game updates per second,
and 123.56 rendered presentations/s after warmup. This is a scene-specific mean,
not a whole-game minimum. The legacy `sdl_frame_limiter_enabled` summary field
is misleading here: `GfxWindowBackendSDL2::SwapBuffersBegin()` only uses that
limiter on non-Vulkan backends. The actual native presentation pacer is disabled.
`uncapped-play-01` is rejected: the environment-based graphics override skipped
the installed config and retained default VSync. Set the frame-rate mode in the
private config instead, as the runner now supports; do not compare changed presets.

`fixed-native-env-01/framebuffer.bmp` verifies the scene, not performance:
synchronous framebuffer capture is excluded from benchmark claims. Tests with
external Vulkan implicit layers enabled also ran successfully; no machine-wide
driver/layer settings were changed. GPU timings after the fix (`fixed-diag-02`)
average approximately 4.57 ms/frame, including 1.34 ms CACAO, on the RTX 3060.

## Reproduce

`tools/triaevum_release/tests/benchmark_gameplay.mjs` accepts `key=value` arguments:

```powershell
node tools/triaevum_release/tests/benchmark_gameplay.mjs exe=I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe profile=I:/TriAevum-0.6.0-candidate-r1/TriAevum.launch.json state=I:/oot3dre_work/native_game/checkpoints/hudtest.oot3dsav output=I:/oot3dre_work/menu-performance/NEW frames=360 warmup=120
```

- Add `diagnostics=true` to collect GPU timings without Vulkan validation.
- Add `input=tools/triaevum_release/tests/gameplay_motion_timeline.json` for movement.
- Use `mode=play frames=0 seconds=15 vsync=true implicitLayers=true` for ordinary
  paced playback. A nonzero frame limit intentionally bypasses runtime pacing.
- `menu=true` opens F1; `capture=true` captures the final framebuffer and marks
  the run as visual verification, not a benchmark.
- Run builds, captures, benchmarks and other GPU workloads separately. The runner
  clones configuration and saves, refuses existing output directories, imposes
  a timeout and reaps its own process. It never edits the installed profile.

## Tests and next evidence

Six `oot3d_vulkan_diagnostics_tests` pass, including bounded history, delayed
GPU timing persistence, no writes from the default frame loop, and explicit
live snapshot cadence. The real-widget F1 smoke test passes 1,032 assertions.

The remaining user-reported ordinary-launch slowdown needs the affected
checkpoint/configuration and confirmation whether it occurs with F1 closed,
in a specific panel, after applying an option, or during camera movement with
generated grass. Do not infer that it is fixed from an idle scene or lower
graphics settings. No automatic decomp synchronization or unrelated refactor
belongs to this investigation.

Developer executable: `I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe`.
Existing launcher: `I:/oot3dre_work/cacao-release-diagnostics/Avvia-TriAevum-corretto.cmd`.
The catalogued installed candidate has deliberately not been overwritten.
