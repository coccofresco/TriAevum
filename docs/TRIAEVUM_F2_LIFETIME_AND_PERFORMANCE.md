# F2 Lifetime and General Rendering Costs

Date: 2026-09-07. Scope: shared Vulkan/NRI renderer, existing native title module.
No decompilation, asset substitutions, visual-quality reduction or preset edits.

## Applied

Baseline: root `fb7915583`, renderer `173eb7d6`. Repair: renderer `71a575b3`.
The F2 crash repair preserves
native render targets and display transfers when optional attachment requirements
change. Auxiliary guides and framebuffer variants are reused instead of replacing
native images. Implementation and repeatable stress procedure:
[TRIAEVUM_F2_NATIVE_PRESENTATION.md](TRIAEVUM_F2_NATIVE_PRESENTATION.md).

This removes an actual unnecessary GPU-wide synchronization/allocation path and
the loss of retained scanout. It does **not** establish a steady-state FPS gain.
The before executable is retained privately as
`I:/oot3dre_work/triaevum-direct-module-build/TriAevum.before-f2-lifetime.exe`.

## Measured Costs

Checkpoint `hudtest.oot3dsav`, 2560x1440, installed custom Grass, Toon/outline,
CACAO Low (8 blur passes), TopScreen, audio. Unchanged title module and settings.
Full-native-frame tests: 480 frames, first 120 excluded, no VSync or frame limiter,
no screenshot/validation, bounded diagnostics enabled. Native work deliberately
advances at throughput speed; this is not the ordinary x2 presentation rate.

| Measure | Before | After |
| --- | ---: | ---: |
| Full native frames/s | 54.22 | 52.44 |
| GPU frame, warm mean | 10.32 ms | 10.42 ms |
| GPU native raster incl. Toon | 3.45 ms | 3.55 ms |
| GPU Grass | 3.30 ms | 3.30 ms |
| GPU CACAO | 1.03 ms | 1.05 ms |
| GPU scanout | 1.05 ms | 1.07 ms |
| CPU native draw handling | 4.76 ms | 4.81 ms |
| CPU command/publication/binding portion | 2.67 ms | 2.67 ms |
| CPU shader-variant cache misses/frame | 0 | 0 |

One paired sample is insufficient to call the 3.3% throughput difference a
regression or improvement. Warm renderer CPU/GPU costs are effectively unchanged
in this sample. GPU subscopes overlap (Toon is inside native raster); do not sum
them as independent costs. CPU draw-state totals also include Grass processing.

Separate ordinary wall-clock run: `mode=play uncapped=true frames=0 seconds=20
warmup=240`, diagnostics/validation/captures off, VSync/pacer off. Result: **59.59
presentations/s**, 792 measured presentations over 13.291 s after warmup, with
native-rate gameplay and adaptive visual interpolation. This is one stationary
Kokiri view, not a whole-game minimum, and does not promise a locked 60 FPS.
The legacy `sdl_frame_limiter_enabled` field does not pace the Vulkan host;
`pacing_enabled=false` and `vsync=false` describe this run.

Private evidence directories under `I:/oot3dre_work/`:

- `f2-general-performance-before-20260907`
- `f2-general-performance-after-20260907`
- `f2-general-uncapped-20260907`

## Next Priorities Beyond Grass

The subsequent no-effects measurements below take precedence: the performance
request is independent of the crash and must target the native baseline as well.

1. **Cache immutable draw-plan structure and publish only changed scene data.**
   `tools/oot3d/native_game_runtime/oot3d_native_pica_vulkan_plan.cpp` rebuilds vertex
   layouts and resource-binding vectors per draw; the consuming path already
   shares/moves large buffers, so do not reintroduce the obsolete bulk-texture-copy
   diagnosis. Use native layout/program/content identities for immutable plans;
   uniforms, transforms, order and buffer generations remain per draw. In the
   measured run the plan phase totals 0.913 s over 480 frames, including warmup
   (about 1.90 ms/frame, not a warm-only mean). Follow the ownership boundary into
   `PicaSceneFrame::Record`, which belongs to the 2.67 ms command/publication region.
   Instrument subregions before claiming their individually recoverable costs.
2. **Remove duplicate binding preparation and redundant commands.**
   `runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica.cpp` prepares legacy
   Vulkan descriptors even for NRI-owned draws, then prepares NRI bindings.
   Move preparation behind the selected ownership path; cache static descriptor
   components and vertex layouts, and emit dynamic state only when changed.
   Reset any command-state cache at command-buffer and effect/pass boundaries.
   Preserve draw ordering, stencil, auxiliary outline coverage and UI separation.
   Descriptor preparation alone is only about 0.07 ms in this sample: eliminating
   that alone is not a major speedup. Profile the whole binding/publication region.
3. **Reduce unused auxiliary render-target bandwidth.**
   An instrumented profile currently retains the seven-color-attachment ABI,
   including motion/material outputs not consumed when temporal AA/reflections
   are off. Compile sparse active output/attachment contracts from the graph,
   rather than allocate/write all guides for every effect combination. Keep the
   new separation between allocated capacity and active bindings. Native raster
   costs 3.55 ms here; this is a cost surface, not a promise of 3.55 ms recoverable.
   Test all consumers, especially outline's native-vs-mixed depth and transparent
   coverage, before changing the shader output ABI.
4. **Avoid shader invalidation on unrelated F1 settings.**
   `src/fast/oot3d/pica_shader_pipeline_cache.cpp::BeginFrame` clears instrumented
   variants on every graphics revision. Key variants by all actual shader inputs
   and retain reusable variants under a bounded cache. Toon settings currently
   generate shader constants, so simply removing invalidation is incorrect.
   This targets option-change spikes, not warm gameplay: this sample already has
   107 shader-variant hits and zero misses per frame.

CACAO multi-pass work and scanout are secondary measured GPU targets (roughly
1.05 ms each), not an excuse to change the user's preset. Assess buffer reuse,
redundant transitions and pass fusion without altering the declared effect order.
Guest execution is also substantial (4.210 s over all 480 frames in this run);
renderer-only work cannot remove that entire cost. A title/compiler investigation
should be a separate measured task, not mixed into this crash repair.

## Acceptance Method

Keep native title/config/checkpoint identical, use A/B/A runs with warmup excluded,
and report CPU/GPU ms plus ordinary wall-clock presentations separately. Use
bounded framebuffer/validation tests separately from performance runs. Require
stable native output with effects Off, unchanged configured output with effects
On, and no lost targets across repeated toggles, transfers or camera cuts.
Do not claim these proposed optimizations are implemented or their gains measured.

## Native Baseline: Effects Are Not the Main Limit

User clarification: improving performance is independent of F2, and the game
must also be substantially faster without extensions. All optional effects and
AA were explicitly disabled in private config copies; TopScreen, audio, FOV and
game behavior retained. In this first comparison the visual interpolation path
was still active, although every presentation advanced a complete logical update.
This is not the clean no-interpolation baseline. 720 frames, 120 warmup excluded, identical
checkpoint, no limiter/VSync/captures/validation. This measures capacity for
complete game updates, not interpolation-only or duplicate presentations.

| No-effects run | 2560x1440 | 640x360 |
| --- | ---: | ---: |
| Full native frames/s, measured window | 60.39 | 61.31 |
| GPU frame, warm mean | 4.64 ms | 2.50 ms |
| Backend native draw CPU, warm mean | 3.08 ms | 2.95 ms |
| Guest-phase CPU, all 720 frames | 8.49 ms/frame | 8.41 ms/frame |
| Draw planning, all 720 frames | 1.83 ms/frame | 1.79 ms/frame |

### Corrected Baseline: No Interpolation Work

The user correctly requested explicit exclusion of x2 processing. A frame-count
ratio alone does not exclude its matching/analysis/replay overhead. The runner
now accepts `interpolation=false`, forces `Graphics.FrameRate.Mode=Original30`
and passes `--disable-visual-interpolation`. It rejects results if interpolation
remains active, interpolated/repeated lists are executed, or no game updates
occur. Summary output now reports actual game updates, presentations, interpolated
lists and direct current frames; it no longer labels all throughput runs as an
unqualified native baseline.

| Clean no-effects/no-interpolation | 2560x1440 | 640x360 |
| --- | ---: | ---: |
| Complete native frames/s | **68.54** | **68.53** |
| Measured frames after warmup | 600 | 600 |
| Measured host seconds | 8.754 | 8.755 |
| Logical updates / presentations, entire run | 719 / 720 | 719 / 720 |
| Interpolated / repeated lists | **0 / 0** | **0 / 0** |
| Direct current frames | 719 | 719 |
| Guest-phase CPU, all 720 frames | 8.20 ms/frame | 8.22 ms/frame |
| Backend replay CPU, all 720 frames | 3.38 ms/frame | 3.42 ms/frame |
| Draw planning CPU, all 720 frames | 1.77 ms/frame | 1.77 ms/frame |

No VSync, pacer, validation or screenshots. Throughput mode bypasses the 30 Hz
wall-clock pacing to measure processing capacity; it does not change native tick
semantics and is not an ordinary-speed gameplay FPS claim. The initialization
presentation accounts for the one-frame difference from logical updates.
These two clean runs replace 60-61 FPS as the **native** capacity baseline and
strengthen the CPU-bottleneck conclusion: a 16x pixel reduction gives no throughput
gain. They are not a performance improvement from a code optimization.

Evidence: `I:/oot3dre_work/native-performance-20260907-pure-{1440,360}/`.

In the first comparison, both output extents and zero effect execution were
verified in renderer diagnostics. Reducing output pixels 16-fold cuts GPU time 46%, but increases
native throughput only 1.5%: the principal steady-state limit here is CPU-side,
not Grass, CACAO or GPU fill rate. Do not derive a promised speedup merely from
the hardware's theoretical power.

An additional run at 640x360 with `profileRuntime=true` enables the existing
`--profile-a32-runtime` counters, no renderer diagnostics. It independently
reports 61.31 full native frames/s. Across all 720 frames, including warmup:

- Compiled-code dispatch interval: 5.023 s, about **6.98 ms/frame**.
- Host system services: 1.053 s, about **1.46 ms/frame**.
- GSP/PICA frontend submission: 0.830 s, about **1.15 ms/frame**, inside services.
- Draw-plan creation: 1.307 s, about **1.81 ms/frame**.
- Backend replay: 2.540 s, about **3.53 ms/frame**, broader than the draw-only scope.
- DSP mixing: 0.506 s, about **0.70 ms/frame**.

`guest_seconds` includes services, and `pica_submit_seconds` includes plan
creation and guest completion-drain execution: these scopes overlap. They are
not additive slices. These are measured phase totals, not sampled per-function
attribution of the compiled module; investigate that interval with CPU sampling
before changing title execution/compilation.
This attribution run belongs to the first comparison with visual-pipeline work
enabled; do not silently substitute its total FPS for the clean baseline above.

The native resource cache is working, but does redundant comparisons:
1.348 GB of texture bytes compared for 1.50 MB actually copied, 76,943 texture
hits and 67 misses. Geometry: 300 MB compared, 9.73 MB copied. This is not the old
"copy all textures every frame" failure. Texture capture costs about 0.26 ms/frame
and vertex capture 0.28 ms/frame: useful version-based savings, but insufficient
alone for a major FPS improvement. Shader cache also already works: 76,989
fragment hits/21 misses, 77,008 vertex hits/2 misses.

Revised priority:

1. Attribute the **compiled dispatch interval** with host CPU sampling; distinguish
   actual game work from memory/dispatch ABI overhead. Keep the current working
   module as the A/B reference; do not restart decompilation or change game timing.
2. Reduce repeated frontend/plan/binding work with immutable, versioned contracts
   and demand-driven scene publication. Preserve every consumer and UI boundary;
   disabling instrumentation is not a substitute for making it cheaper.
3. Eliminate comparison/upload redundancy where mutation identities are reliable;
   retain content comparison for data whose mutation version is unavailable.
4. Optimize GPU attachments/passes and optional effects independently, using the
   same no-effects baseline as a regression guard.

Reproducible override files: `tools/triaevum_release/tests/renderer_native_performance.json`
and `renderer_native_performance_360p.json`. Evidence directories:
`I:/oot3dre_work/native-performance-20260907-{1440,360,profile}/`.
These baseline investigations do not themselves constitute a general-performance fix.
The subsequent sampled CPU analysis and implemented +15% native-throughput
improvement are documented in `TRIAEVUM_NATIVE_CPU_COSTS.md`.
