# Linux Probe Stalls

2026-09-10. Scope: periodic freezes seen during Forge shader-cache qualification,
Linux RTX 4060, Wayland, 1280x720. Not an unlocked FPS benchmark.

## Findings

The six periodic freezes are synchronous diagnostic captures, not shader misses.
The native probe schedules screenshots at host frames 120, 270, 420, 570, 720,
870. All six largest post-startup CPU-frame outliers occur at the corresponding
renderer frame IDs 121, 271, 421, 571, 721, 871. The effects run has the same
correlation. Independent shaderc audit: zero compiles in both configurations.

Controlled follow-up used the same executable, launch profile, prepared pack,
cache, VSync/timing and telemetry, removing only screenshot capture. No concurrent
build was running during these comparisons. Both probes completed 900 presents
and exited normally. Post-startup interval below is renderer frames 100-900,
explicitly excluding boot/loading. It is not a claim that boot is stall-free.

| Configuration | Capture run: pauses >100 ms | No captures: pauses >100 ms | Maximum CPU-frame time after frame 99 |
|---|---:|---:|---|
| Native, no interpolation | 6 | 0 | 519.90 -> 19.79 ms |
| Current effects/interpolation | 6 | 0 | 609.36 -> 35.85 ms |

Do not convert these diagnostics into simulated-game FPS: the native probe uses
fixed simulation delta, the effects probe uses interpolation, and both inherit
VSync/host limiting. They isolate large stalls, not maximum throughput.

**Separate real runtime issue:** effects runs still stall before the first
capture. Recorded startup stalls: 18.605 s, 17.564 s, and 20.572 s without captures.
The corresponding CPU `grass_render_ms` is 18.457 s, 17.488 s, and 20.497 s.
The last run's shaderc audit is zero. Attribution is to the Grass CPU execution
region; finer allocation/generation/pipeline timing is needed before selecting
a fix. Do not call this resolved or explain it away as a screenshot/shader stall.

## Owners And Evidence

- `tools/triaevum_release/probe_renderer.py --no-captures`: explicit capture-free
  mode, strips inherited screenshot options; `invocation.json` records
  `synchronous_captures=false`. Capturing remains the default for visual checks.
- `test_probe_renderer.py`: verifies isolation and removal of inherited captures
  without dropping renderer diagnostics or the bounded runtime.
- `tools/oot3d/native_demo_host/oot3d_demo_host_screenshot.cpp`: synchronous
  `ReadFramebufferToCPU`, BMP conversion/write, metadata write inside host loop.
- `runtime/three_ds_recomp/src/fast/backends/gfx_vulkan.cpp`:
  `ReadFramebufferToCPU` waits on a fence, allocates a staging buffer, submits
  immediate transfer commands, copies/converts pixels and destroys staging.
  The observed full pause is not attributed solely to the fence/GPU.
- Private evidence under `/home/xander/triaevum-pipeline-live-proof/`:
  `forge-pass-cache-native-v2/{first-launch,second-launch}`,
  `forge-pass-cache-effects/{first-launch,second-launch}`,
  `native-pacing-no-captures`, `effects-pacing-no-captures`.
  Each has `gpu.json`, `runtime.json`, `invocation.json`, `launch.log`.

Keep visual correctness and pacing runs separate. A future asynchronous capture
module could reduce diagnostic stalls, but is not required to establish their
cause and must not be confused with fixing Grass startup latency.
