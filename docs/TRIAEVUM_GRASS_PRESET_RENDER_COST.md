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


## Round 37: mask and placement cache

2026-10-11. The native capture path memoizes source/mask/neither outcomes; the
worker persists final placements, including every mask and baked RGB. Details,
limits and invalidation are in [the overlay cache contract](TRIAEVUM_GRASS_OVERLAY_MASKS.md#round-37-draw-memo-and-final-placement-persistence).

Claude's supplied pre-round baseline (`dc66dad`), throughput, 600 frames with
180 warmup, play-kokiri Custom settings, all other effects retained:

| Scene | Grass | Frame mean / p95 ms | Pre-backend CPU ms | PICA submit ms |
| --- | --- | ---: | ---: | ---: |
| Graveyard 16-21-37 | On | 17.97 / 19.50 | 6.72 | 3.63 |
| Graveyard | Off | 18.00 / 19.38 | 6.39 | 3.17 |
| Kokiri night 11-21-57 | On | 29.59 / 31.38 | 7.83 | 4.31 |
| Kokiri | Off | 26.00 / 27.75 | 7.33 | 3.57 |

No post-round live frame/GPU result is claimed here: Claude owns game runs.
The roughly 3.6 ms Kokiri difference includes blade vertex/fill/guide/lighting
cost that placement caching cannot eliminate. No density, shape, distance,
coverage or shadow quality is reduced.

### Same-method offline comparison

Private Graveyard fixture, Release, isolated run, median of five new-worker
requests per path, identical request and 490,943 final anchors:

| Full placement request | Before (build, no disk cache) | After (disk hit) |
| --- | ---: | ---: |
| Worker completion, including request/key/I/O/validation | 104.016 ms | 29.691 ms |
| Extraction calls | 1 | 0 |
| Exact anchor bytes compared | reference | identical |

The final file is 31,455,784 bytes. Saving about 74.3 ms (71.5%) is an offline
**scene-entry** result, not a per-frame or GPU saving. The fixture uses actual
room triangles/native samples but assumed TEV/primary values; it does not replace
live validation. Warm overlay lookup measured 0.554 us, 19 mesh republishes plus
lookup 3.030 us, empty lookup 0.023 us. These exclude native capture. Source mask
extraction before/with overlays was 70.963/66.913 ms, 542,260/490,943 roots.

### Expected per-stage changes

| Stage | Warm behavior / expected saving |
| --- | --- |
| Eligibility, texture hash, TEV/UV/alpha capture, geometry registry | No calls on a matching memo entry; recover their measured reference cost. |
| World placement reconstruction | Camera inverses are reused; exact per-draw clip/world rigidity checks and dynamic source shading remain. |
| Overlay capture total | Signature lookup, clip check and bridge publication remain; the prior ~0.14 ms is not all removable. |
| Source publication | Same bounded lookup/refresh; no new claim about frontend work outside this boundary. |
| Placement request/membership/budget lookup | Remain; warm final placement avoids extraction as before. Disk saves repeat scene-entry bakes. |
| Selection | Existing exact view cache remains; moving views still select. |
| Visible-index upload | Reused per fenced frame slot for an unchanged selection; saves `visible roots * 4` bytes/upload. |
| GPU compaction | Zero dispatch only for the tested actor-free, resting, non-motion case; normal gameplay retains interaction history. |
| Blade draws, dynamic native lighting, grass shadow receivers | No promised reduction; all remain dynamic. Grass is not added as a native shadow-map caster. |

### One-line frame telemetry

`TRIAEVUM_GRASS_PROFILE=1` opts into CPU timers and Vulkan timestamp scopes. Off
means no new clock reads, profile allocations, locks, log output or GPU query
commands (only disabled guards). Existing renderer telemetry is unchanged.
`TRIAEVUM_GRASS_DRAW_MEMO=0` forces the cold capture path while still recording
repeated signatures. `TRIAEVUM_GRASS_DISK_CACHE=0` disables disk loads/writes;
it does not disable the pre-existing resident placement cache. These switches
are process-level diagnostics, not product/F1 settings.

`stderr.log` contains one `TRIAEVUM_GRASS_PROFILE frame=...` line per completed
sample, read two frames later without waiting for GPU completion. Final two
frames lack resolved samples. Missing GPU scopes are `-1`, never assumed zero.
CPU fields sum all invocations; GPU scopes follow the existing profiler's one
query pair per scope per frame, so use the ordinary single scene-grass pass for
attribution, not multi-target lab views.

- `<stage>_ms`, `<stage>_n`: `source`, `overlay`, `memo`, `eligibility`,
  `texture_hash`, `tev`, `geometry`, `world`, `publish`, `requests`, `placement`,
  `selection`, `compaction`, `lighting`, `shadows`, `draw`, `prepare`,
  `interaction`, `upload`. `memo` includes key building and live refresh.
  Eligibility has separate early and rule/twin scopes. Inclusive scopes nest;
  do not sum source/overlay/prepare with their children, or CPU with GPU.
- `memo_hit`, `memo_miss`, `neither_hit`, `signature_repeat`, `clip_same`,
  `clip_changed`, `world_invalidations`. Signature repeat counts identical
  non-camera inputs even with memo reuse disabled. Clip counters distinguish
  unchanged/moving captures when reuse is on; world changes still invalidate.
- `placement_hit`, `placement_miss`, `disk_hit`, `disk_miss`, `disk_reject`,
  `disk_write`, `worker_ms`; worker time includes load/build/write and can overlap
  across workers. Use the first frames for scene-entry attribution, and exclude
  warmup for steady-state comparisons.
- `selection_hit`, `selection_miss`, `selection_upload_reuse`, `compaction_reuse`.
- `gpu_total_ms`, `gpu_compaction_ms`, `gpu_blades_ms`, `gpu_shadows_ms`,
  `gpu_lighting_ms`. Shadows measure the grass shadow-receiver grid; lighting
  measures its native surface-lighting producer. Neither is a blade-mask sample.

### Exact live commands (Claude)

The checked local coordinator script uses the same profile, savestates, cache
root and `frames=600 warmup=180 diagnostics=true timeout=240` arguments as the
supplied baseline. It makes fresh output directories; the benchmark tool makes
private config/save copies. It does not modify play-kokiri. The moving timeline
walks forward/right/left during frames 180-599 and is identical for both variants.
Run each pair sequentially, preferably interleaved over at least three repeats:

```powershell
$run = 'I:/_triaevum_work/contact-shadows/coord/round37-benchmark.ps1'
& $run -Kind profile -Variant reference -Motion static
& $run -Kind profile -Variant cached -Motion static
& $run -Kind profile -Variant reference -Motion moving
& $run -Kind profile -Variant cached -Motion moving
# A second cached process should report disk hits at scene entry.
& $run -Kind profile -Variant cached -Motion static
# Compare final frame capacity without the new instrumentation overhead.
& $run -Kind throughput -Variant reference -Motion static
& $run -Kind throughput -Variant cached -Motion static
& $run -Kind throughput -Variant reference -Motion moving
& $run -Kind throughput -Variant cached -Motion moving
```

`reference` is the same binary's repeated-capture path with persistence disabled,
not an assertion that every cold-start side effect equals the old executable.
For the exact historical baseline, pass the retained `dc66dad` executable with
`-Exe` to a throughput run; do not substitute another settings snapshot.
To summarize a returned output directory:

```powershell
& 'C:/ProgramData/miniconda3/python.exe' `
  'I:/_triaevum_work/contact-shadows/coord/round37-profile-summary.py' `
  '<returned-output-directory>/stderr.log' 180
```

Live acceptance: matching warm draws should produce hits with zero cold
`texture_hash_n`/`tev_n`/`geometry_n`; moving-camera clip counts should change
without rebaking. Inspect world invalidations if that fails. Restart the same
view, confirm `disk_hit`, then edit source channel/levels/rects/twin rule/slope/
normal offset and overlay mode/strength in F1. Confirm a rebuild and changed
coverage immediately on the next grass batch; restore settings and confirm the
original distribution. Re-enter the room, reload a texture pack, and verify
native-mask policy and no stale placements. Keep F1 closed for timing; measure
its requested histograms separately.

For deterministic grass-On comparison, run the same two capture commands with
no movement, and optionally repeat both with `-Motion moving`:

```powershell
& $run -Kind capture -Variant reference -Motion static
& $run -Kind capture -Variant cached -Motion static
```

Both restore the same state and capture native frame 599. Compare RGB and grass
coverage at the same render size, visual clock and native frame. Acceptance on
the same GPU/compiler is **zero changed RGB pixels (tolerance 0)**. Record any
mismatch rather than relaxing density/coverage. Captures/validation are separate
from throughput results. Also compare the original executable at that exact
frame before claiming full pre/post live parity.

### Verification in this round

`TriAevum.exe`, foundation tests, actual F1 smoke and Vulkan grass-motion test
built. Foundation: **386 passed**, one optional private benchmark skipped in the
generic run; that fixture passed separately. Actual F1 Masks widgets: **489
assertions passed**. Vulkan on RTX 3060: unchanged output bytes and matched
color/material/motion images identical; selection/static/actor/field/motion/reset
invalidation and existing bend-history/reprojection tests passed. The test now
uses the production shader resolver; missing built-in grass vertex/fragment
artifacts were compiled from the current maintained source, as runtime fallback
does. No shader source or packaged binary was changed by this round.

Logs and exact offline reproduction are in the round37 coordinator report.
No game runs, git commands, or edits to play-kokiri were performed by Codex.
