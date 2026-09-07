# Current Grass preset and rendering cost

2026-09-07. Scope: preserve the user's current Grass appearance as the product
default and reduce avoidable renderer work, without lowering its density,
distance, source masks, interactions or effects.
Renderer commit: `8edf3cc9` (parent `144c2c41`).

## Default snapshot

`tools/oot3d/native_game_runtime/triaevum_product_graphics.inc` now contains the
complete current `Graphics.Grass` object, captured from the installed config.
`triaevum_product_info.cpp` already derives `GrassSavedPreset` from this object.
New installations and the product's default/reset path therefore share the same
Grass preset. Existing installations are not silently rewritten.

The snapshot matches the captured user object exactly, including all six masks:
1,048.8 blades/m2, spacing 0.6, visibility 50,000, visible budget 500,000, five
near segments, one far segment, four silhouettes per distant tuft, quantity 2x,
spread 1x, transition width 0.48, density fade 1.0 and segment softness 0.75.
The existing 2x presentation, 1.10 FOV, toon and TopScreen defaults are retained.
No machine-specific paths or unrelated effect settings are imported.

## Findings and implementation

All renderer paths below are under `runtime/three_ds_recomp`.

1. **Repeated selection of an unchanged view.** Kokiri reevaluated about 215,000
   anchors each frame to reproduce the same 198,738 visible instances. The new
   `include/fast/oot3d/grass_selection_cache.h` validates exact projection/eye,
   ordered static-placement revision, full LOD policy, conservative blade bound,
   culling switch and budget. A match reuses the existing indices and topology
   batches, without another copy of the instance list. Any view or policy change
   rebuilds selection. There is no tolerance, stale camera, or quantized motion.
   Wind, actor interactions, lighting, fog, jitter and draw pushes are refreshed
   on every presentation, including cache hits. Reset and exceptions invalidate it.

2. **Expanded duplicate vertices.** `grass_indexed_topology.h` replaces procedural
   triangle-list duplication with a 2,604-byte static index stream in a 4 KiB
   buffer. A five-segment plane needs 11 unique vertices instead of 27; distant
   quads need four instead of six. Triangle order, winding, endpoints and vertex
   attributes remain identical. This reduces vertex-shader work, not triangles
   or visible coverage. The measured total Grass GPU saving is much smaller than
   the theoretical unique-vertex reduction because other GPU costs remain.

3. **Serial fallback at a saturated budget.** Selection previously disabled its
   worker path whenever the conservative candidate count exceeded the remaining
   visible budget. `grass_selection_budget.h` retains the exact original anchor
   prefix across sorted worker topology bins. Work remains bounded per worker;
   deterministic merging preserves serial selection and the global budget.
   This is not claimed as a measured standalone improvement in the current intro.

4. **Duplicated cluster checks while moving.** The measured broad-phase and
   prefix-preparation stages separately repeated distance, frustum and retention
   work on tens of thousands of clusters. `SelectGrassClusterWork` in
   `src/fast/oot3d/grass_visibility.cpp` fuses them and computes the prefix from
   the same bound. The older two-stage entry points remain usable as reference
   tests. CPU diagnostic output now separates cluster work and anchor evaluation.

`src/fast/oot3d/interactive_grass_pass.cpp` integrates these small modules into
the existing Grass geometry provider. No native PICA, scene composition, outline,
fog, UI, gameplay or title-specific rendering exceptions were added.

## Measurement method

Tool: `tools/triaevum_release/tests/benchmark_gameplay.mjs`, NRI/Vulkan on the
RTX 3060, current installed 2560x1440 configuration, MSAA/AA Off. Other current
effects and audio remain enabled. Each run uses private config/save copies.

The comparison uses full native-frame throughput: VSync, pacing and SDL frame
limits disabled, fixed native delta 1/30, no screenshots or Vulkan validation.
This measures work capacity, not interpolated presentation FPS or gameplay speed.
Kokiri starts from `hudtest.oot3dsav`, 420 frames with the first 120 excluded.
Intro starts from boot, 1,500 frames with the first 240 excluded. Shader startup,
placement initialization and loading are outside the reported measurement window.
The Vulkan timestamps are read asynchronously; the final two frames have no GPU
sample. CPU and GPU scopes overlap and must not be added together.

Results at equal preset:

| Workload / metric | Before | Optimized |
| --- | ---: | ---: |
| Kokiri native-frame throughput | 39.40 fps | 55.01 fps |
| Kokiri Grass CPU mean / p95 | 6.214 / 7.828 ms | 1.080 / 1.533 ms |
| Kokiri Grass GPU mean / p95 | 3.453 / 4.558 ms | 3.297 / 3.967 ms |
| Intro native-frame throughput | 36.96 fps | 40.82 fps |
| Intro Grass CPU mean / p95 | 11.909 / 20.807 ms | 10.637 / 17.441 ms |
| Intro Grass GPU mean / p95 | 2.099 / 2.969 ms | 1.993 / 2.728 ms |

An earlier Kokiri run with the indexed/cache stage measured 50.54 fps and
1.167 ms Grass CPU. The final table includes fused traversal. Individual short
runs have scheduling variance: observed Kokiri improvement is about 28-40%,
versus about 10% over the moving intro. Do not extrapolate the stationary-view
improvement to all gameplay.
Grass Off measured 56.24 native frames/s at Kokiri as an attribution check, not
as the proposed preset. No quality settings were reduced to obtain these results.

## Verification and limits

- CPU tests compare every indexed triangle with the previous expanded stream.
  A real Vulkan test executes the production decoder for all 25 topologies.
- Exact selection-cache invalidation covers every policy field, view, geometry
  revision, budget and radius. Deterministic worker-prefix tests cover all budgets
  over a mixed topology stream and 1/2/3/10/12 worker partitions.
- Fused traversal is compared with the two-stage reference over moving views,
  frustum modes and distant-density multipliers 0.1/1/2/4.
- The final focused suite passes **328 tests**. The actual F1 widgets pass
  **1,618 assertions**; all **five product-default tests** pass, including the
  compiled product-info probe.
- The product-default tests include `--product-info` from the built executable,
  verifying both Grass and the saved reset preset, not just the source JSON.
- Framebuffer/validation runs are separate from performance measurements.
  Indexed/cache/budget intro captures show Grass and intact native scene/UI
  composition, with zero Vulkan/NRI validation errors or failed effect passes.
- The final fused/indexed/cache build's Kokiri framebuffer at native frame 110
  is **pixel-identical** to the pre-change capture: zero changed RGB pixels at
  2560x1440. Its validation run also records zero Vulkan/NRI or effect-graph errors.
  This is evidence for that matched frame, not a claim of exhaustive game parity.

The cache deliberately cannot skip selection for a changed camera. Large-field
cluster traversal, GPU interaction compaction, cutout fill and guide writes still
cost time. The current work does not promise 60/90 native rendering frames/s in
every view, and does not replace the preset with a lower-quality approximation.
Further work should profile those scopes separately before changing density or
adding occlusion mechanisms. `OOT3D_GRASS_DIAGNOSTICS=1` exposes cluster/evaluation
timings and workload counts, avoiding another investigation based only on FPS.

## Reproduction and local evidence

Runtime target: `triaevum_public_runtime` in
`I:/oot3dre_work/triaevum-direct-module-build`, `--parallel 3`; no title/AOT rebuild.
The final incremental code builds took roughly 7-9 seconds.

Evidence prefix: `I:/oot3dre_work/grass-preset-opt-20260907-`.
Directories `before-kokiri`, `off-kokiri`, `after-kokiri`, `accepted-kokiri`, `before-intro`,
`fused-intro` contain immutable invocation/config snapshots and timing reports.
`selection-profile` contains the separated CPU findings. `before-capture` and
`intro-verify` / `accepted-capture` are visual evidence, not benchmarks. Captures, saves and generated
reports stay local and are not release payloads.

Run the focused regression suite via `run_grass_outline_tests.ps1`; the product
suite is `python -m unittest tools.triaevum_release.test_product_graphics_defaults`
with `TRIAEVUM_PRODUCT_TEST_EXE` pointing at the built runtime.
