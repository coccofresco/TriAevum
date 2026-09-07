# Grass: Intro Stability and Frustum-Driven Selection

Current first-frame admission, distant tuft LOD and outline completion:
[First-Frame Grass and Gameplay Camera](TRIAEVUM_GRASS_FIRST_FRAME_AND_GAMEPLAY_CAMERA.md).
Earlier cold-start measurements below describe superseded behavior.

Current implementation: [static room cache and extended visibility](#static-room-cache-and-extended-visibility-2026-09-07).
Follow-up: [cold admission and effective camera-cut continuity](TRIAEVUM_GRASS_CAMERA_CUT_CONTINUITY.md)
closes the pending initial-placement gap and the same-room synthetic camera-cut
problem, with final September 7 framebuffer evidence.
Earlier sections retain the history and superseded measurements.

## Scope

NRI/Vulkan Grass extension. Preserve native depth, lighting, fog, UI ordering,
the user's texture rules and visual settings. No scene IDs, model names,
frame-number workarounds, decompilation changes or native shader changes.

Starting point: project `f5ff2408b`, renderer `93ba845f`.
Renderer implementation: `630a838d`.

## Causes and Corrections

1. **Composition starvation.** A second opaque run after transparent/atmospheric
   content invalidated the whole-world extension anchor. This happens when the
   native title models enter the draw sequence. Grass stopped despite three
   valid, cached terrain sources. `PicaCompositionSchedule` now exposes a
   separate published-geometry boundary at the end of the first opaque prefix.
   Only Grass uses it. Whole-scene lighting/postprocessing anchors retain their
   conservative validation. Native draw order is unchanged.
2. **Index-prefix starvation.** Large source meshes consumed the extraction
   budget before later triangles were considered. Candidate quotas now span the
   whole eligible surface proportionally to candidate count. Masks, spacing,
   cluster distribution and slope rejection still apply. Candidate work is
   bounded; the budget is a maximum, not a promise to fill it after rejection.
3. **Camera/sample mismatch.** Distance culling used the current guest eye while
   drawing could use the preceding/interpolated PICA transform. A captured cut
   showed grass disappearing one frame before the terrain camera changed.
   `GrassCameraFromProjection` recovers the eye from the exact position-to-clip
   matrix in the stable anchor coordinate system. Distance, LOD and the shader's
   camera-position input use that eye. Orthographic/singular matrices retain
   the existing semantic-camera fallback.
4. **Unnecessary visibility work.** Immutable world placements now include a
   balanced cluster hierarchy with conservative bounds, including blade bending.
   Traversal rejects off-frustum/out-of-range branches before testing blades.
   Candidate order is restored before budget decisions; worker count is based
   on visible candidate work, not the size of the entire world.
5. **Unstable enumeration.** Mesh matches are sorted by instance identity so
   hash-container iteration cannot reorder uploads or budget priority.

## Identity and Lifetime

- Only visible blade indices are compacted into the per-frame GPU draw stream.
- Positions and random appearance are deterministic from the source/rule/seed,
  independent of frame number, traversal order and camera motion.
- No per-blade historical position is needed to leave and re-enter the frustum.
  Immutable source placements remain a bounded cache, not an animated history.
  This patch does not yet implement per-cell eviction/regeneration of that cache.
- The small current candidate-index vector reuses its capacity. Static anchors
  are not re-uploaded on every camera movement.
- Density/segment LOD and user draw-distance limits remain intentional controls.
- The new composition boundary covers **already-published first-prefix geometry**.
  It is not permission to treat multiple independent world/view groups as one
  completed scene or to run arbitrary postprocessing before UI.

## Code and Verification

Renderer paths are relative to `runtime/three_ds_recomp`:

- `src/fast/renderer3ds/pica_composition_schedule.cpp`: declared insertion point.
- `src/fast/oot3d/grass_surface_extractor.cpp`: bounded whole-surface generation.
- `src/fast/oot3d/grass_world_placement_cache.cpp`: immutable spatial hierarchy.
- `src/fast/oot3d/grass_visibility.cpp`: exact camera and hierarchical query.
- `src/fast/oot3d/interactive_grass_pass.cpp`: selection/compaction integration.
- `tests/oot3d_grass_world_placement_cache_tests.cpp`: projection flips, off-axis
  frusta, camera return, and indexed-vs-exhaustive cluster selection.
- Foundation/effect-graph/async-placement tests cover stable source identity,
  budgets, zero-density/black masks and mixed native composition.

The maintained test runner is `tools/triaevum_release/tests/run_grass_outline_tests.ps1`.
That revision passed 291 tests, including actual shader compilation.

Run from the project root with a fresh private output directory:

```powershell
node tools/triaevum_release/tests/benchmark_gameplay.mjs exe=<TriAevum.exe> profile=<TriAevum.launch.json> boot=true output=<new-directory> mode=play vsync=true frames=0 seconds=130 timeout=155 capture=true captureStart=180 captureEvery=180 diagnostics=true grassDiagnostics=true grassTransitions=true
node tools/triaevum_release/tests/analyze_grass_visibility.mjs <new-directory>
```

This copies config and save data, boots without a savestate, records the full
sequence and closes the test process. It is a **functional verification, not an
uncapped FPS benchmark**. `grassTransitions=true` additionally captures the
framebuffer on status changes, avoiding continuous readbacks that disturb pacing.
Sidecars identify host/renderer frames and interpolation samples.

Raw local evidence: `I:/oot3dre_work/grass-intro-20260907/` (private, not packaged).
Baseline: 1,611 drawing frames / 4,815 source-published frames; no Grass during
the 3,125 source frames with mixed opaque composition. After the composition and
budget fixes: 4,781 / 4,824; the remaining short culls motivated exact-camera
selection. Source publication alone does not prove that grass should be visible:
startup, sky-facing cameras and distance/mask exclusions must be distinguished
from provider failures using the framebuffer and diagnostics.

Final transition-triggered 130-second run (`final-transitions`):

- 4,780 drawing frames / 4,814 source frames; 0 provider failures, missing-input
  errors, rejected schedules or composition execution mismatches.
- The 34 non-drawing source frames are one asynchronous startup frame, one
  synthetic camera-cut frame and 32 final sky-facing frames. The startup count
  is **not** a latency guarantee: the diagnostic readback allows workers to finish
  between frames. The ordinary sparse-capture run needed 10 startup frames.
- The sampled query forwards on average 5.81% of the 220,384 clusters to blade
  selection (95th percentile 10.73%). Final selection cost: mean 1.58 ms, p95
  2.71 ms. Grass GPU pass: mean 0.266 ms, p95 0.531 ms. These scoped timings are
  not a claim about uncapped gameplay FPS; the run uses VSync, x2 and readbacks.
- Three builds total, no recurring placement builds during camera motion;
  648,479 cached immutable anchors, versus 1,203,824 in the prefix-starved baseline.

## Separate Interpolation Finding

`final-transitions/framebuffer_002798.bmp` and its JSON sidecar capture the
remaining synthetic sample (`previous_source=1395`, `current_source=1396`,
`alpha=0.5`). The **whole scene camera** points at sky/mountain, unlike the next
native sample in `framebuffer_002799.bmp`. The visible image contains no eligible
grass terrain. Culling is correct there; retaining previous grass would draw it
in the wrong view. Camera-cut continuity remains a separate temporal-renderer
issue, not claimed fixed by this Grass change. This sample is a reproducible
starting point for that investigation; no camera override was added here.

## View-Focused Budget and Distance Work (2026-09-07)

Renderer commit: `f1fee1e8` (on top of `630a838d`).

This follow-up supersedes the whole-world allocation above. It does not change
native composition, lighting, fog, UI, camera control or the user's saved preset.

### Policy and Ownership

- `GrassViewPlacementBuilder` is a renderer-independent scheduler, with immutable
  worker inputs and no Vulkan/host/UI access. `InteractiveGrassPass` only supplies
  scoped sources and the exact rendered view, then consumes ready placements.
- `MaxInstancesPerRoom` now bounds the **shared generation candidate budget**
  across matching sources, not a separate allowance for every mesh. Masks,
  minimum spacing and cluster rejection may leave fewer accepted anchors.
- Visible area is estimated by clipping world triangles against a guarded view
  and distance bounds. Area and inverse-square distance priority determine each
  source's share. Saturated capacities redistribute unused quota deterministically.
- Large triangles are subdivided independently of the camera into bounded
  sampling patches (400 world units maximum edge where the subdivision safety
  limit permits it). Within each source, allocation weights are 90% guarded
  view/distance demand and 10% coarse whole-surface reserve. The reserve supports
  unpredicted cuts while new placements are generated; it is not drawn off-screen.
  The weights are not a strict final 90/10 count after capacity saturation/masking.
- Each patch uses progressive samples seeded by source/patch/candidate identity.
  Retained blades keep their position and appearance when the quota changes.
  Density/spacing rejection can change membership; this is not a guarantee that
  every blade remains visible irrespective of LOD or budget.
- The guard is 12% of draw distance. Translation beyond half the guard, rotation
  beyond the angular tolerance, or significant projection changes request a new
  placement. Two consistent view observations reject isolated transient samples.
  Exact current-view culling remains immediate, including camera cuts.
- Source/rule/transform/height changes invalidate the source cache. Camera/budget
  changes advance the published content version, so GPU buffers cannot silently
  retain old anchors. While workers run, the last ready view remains available.
- A shared budget batch is frozen until its workers complete, then published
  together. This avoids asynchronous quota oscillation and partial-result static
  uploads. Completed views are reused even after worker-cache eviction.
- Distance density now follows `far + (1-far) * remaining^4`, with full density
  through the configured near boundary and the configured far-density floor.
  At halfway through reduction, only 7.19% remains with a 1% far floor, versus
  50.5% for the old linear curve. Stable selection does not reshuffle every frame.
- Geometry uses at most three segment bands: near, approximately half-detail,
  and far, over thirds of the user-configured segment interval. The independent
  single-plane distance still applies. This reduces draw variants as well as
  distant triangle count. Cluster hierarchy rejection still precedes blade work.

### Verification and Cost

Private evidence: `I:/oot3dre_work/grass-frustum-budget-20260907/`.
`focused` is the first implementation; `cohort` is the final coordinated version.
The full suite passes **297 tests**, including budgets/capacity saturation,
extreme weights, view clipping, near/far priority, stable retained samples, black
masks, camera guards/FOV invalidation, pending-result continuity, source changes,
transient-view rejection and all supported segment-band policies.

The same 130-second boot/intro capture protocol and unchanged user configuration
were used for `final-transitions` and `cohort`: 906.6 blades/m2, shared configured
limit 500,000, 2560x1440, x2, FOV 1.1, native fog/lighting, toon/outline and CACAO.
Final result: **4,778 drawing / 4,809 source-published frames**, zero placement or
provider failures, rejected schedules, missing inputs or composition mismatches.
The non-drawing ranges remain startup, the separate synthetic camera-cut sample,
and the final sky-facing view. Captures were inspected directly from the native
framebuffer, including terrain, actors, logo and foreground/background grass.

Indicative smoke-log samples (periodic plus state changes, **not time-weighted
FPS measurements**, and not counting pending old/new/cache copies as residency):

| Metric | Whole-world allocation | View-focused final |
| --- | ---: | ---: |
| Active placement anchors, mean | 648,479 | 284,384 |
| Visible blades, mean | 17,377 | 34,977 |
| Grass draw calls, mean / p95 | 9.78 / 14 | 6.96 / 9 |
| Grass render-thread CPU, mean / p95 | 1.96 / 3.03 ms | 2.01 / 3.14 ms |
| Grass GPU, full drawing-frame mean / p95 | 0.266 / 0.531 ms | 1.389 / 2.463 ms |

This is substantially denser foreground coverage with fewer calls and a smaller
active placement, **not a claim that total GPU cost fell**. Near grass occupies
more pixels and uses more detailed geometry. Worker builds are newly recurrent:
315 builds in the final sequence, 20.01 seconds summed worker time, versus 544 /
29.75 seconds in the first view-focused implementation. The old whole-world
version built only three times but left most of the budget outside the view.

Separate `throughput` run: VSync, SDL limiter, realtime pacing, captures and heavy
diagnostics disabled; 3,000 host iterations with the first 180 excluded.
**2,820 full native frames in 38.715 seconds = 72.84 native frames/s** with the
unchanged 1440p effects profile. Fixed native delta is 1/30; this measures complete
native-frame work, not synthetic x2 throughput and not real-time gameplay speed.
There is no matched uncapped pre-change baseline for an FPS speedup claim.

### Remaining Boundaries

The cache still stores immutable view snapshots, not GPU-resident patch streaming.
Publishing a changed snapshot uploads its static buffers; this is amortized by
the guard and coordinated publication, not eliminated. Unpredicted cuts can
briefly show the coarse reserve until the new density is ready. Mask-aware
candidate rejection remains authoritative, and a black mask is never filled to
exhaust a budget. No scene-, texture-, actor- or intro-frame-specific overrides
were introduced. Future optimization should measure partial patch uploads and
near-pixel overdraw before increasing global density or adding more controls.

## Static Room Cache and Extended Visibility (2026-09-07)

Renderer implementation: `dbab79e8` on `fix/cacao-effect-guide-contracts`.
Playable development binary: `I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe`,
with `I:/TriAevum-0.6.0-candidate-r1/TriAevum.launch.json` arguments. The installed
release executable and private title module are unchanged.

This supersedes the view-focused scheduler in renderer `f1fee1e8` / project
`e71fc761c`. That scheduler redistributed the small generation budget with the
camera, but caused 315 asynchronous builds per intro and delayed dense coverage
after cuts. Its first replacement with a small whole-room pool was rejected:
it avoided rebuilds but halved visible density. The final path uses a larger,
compact static pool, not camera-triggered snapshots.

### Ownership and Contracts

- `grass_static_placement_cache.{h,cpp}` replaces `grass_view_placement_builder`.
  Candidate demand is measured from immutable source geometry and world area.
  Allocation is independent of camera, draw distance, draw budget and frame rate.
  It remains source/rule/transform/version dependent, including mask and height.
- The bounded pool admits up to 8,388,608 generation candidates across current
  sources, further capped by the physical device's storage-buffer range. Masks,
  spacing and clumping still reject candidates; no black regions are filled to
  exhaust the allocation. This is not a promise of that many visible blades.
- The worker cache retains inactive placements by LRU, up to 32 entries and
  768 MiB of completed placement vectors. This is a **RAM cache**, not a disk
  cache. Pending construction, active references, temporary worker storage,
  static GPU buffers and upload staging are additional memory. One oversized
  protected entry can exceed the worker cache's eviction threshold.
- `grass_anchor_codec.h` defines procedural direction packing. World positions,
  heights, widths and wind phase remain float32; directions use SNORM16 and
  octahedral normals. CPU anchors shrink from 60 to 36 bytes, GPU anchors from
  64 to 36. Tests bound normal component error to 0.00015 and width-axis error
  to 0.00002. CPU fallback and GPU compaction consume the same nine-word layout.
- `grass_world_placement_cache.cpp` stores each cluster/node's minimum stable
  visibility threshold. `grass_visibility.cpp` discards entire distant branches
  whose anchors cannot survive LOD, **before sorting or individual blade work**.
  Sorted cluster prefixes are resolved once; worker dispatch uses this retained
  count instead of raw off-detail candidates. Indexed and exhaustive queries
  are tested for equivalent selection, including distant thinning.
- `interactive_grass_pass.cpp` consumes cached placement and current-view
  visibility. Static uploads occur on placement changes, not camera changes.
  Native PICA shaders, lighting/fog, depth, composition domains and the declared
  Grass insertion anchor are unchanged. No scene IDs or intro-frame rules enter
  placement or scheduling. The lower-level view-sampling helpers remain available
  for tests, but are not used by the production static scheduler.

### Controls and Migration

F1 / Grass / Performance is the only control owner:

- **Visible blade budget** keeps the existing JSON member
  `Grass.Budget.MaxInstancesPerRoom` and limits per-frame draws, not static cache.
- **Draw distance** accepts up to 50,000 world units.
- **Density falloff distance** is independently persisted as
  `Grass.Performance.LodReferenceDistance`. Increasing visibility therefore does
  not increase detail near the camera. Beyond that reference distance, retained
  density decreases with inverse-square distance; distant blades remain on the
  existing reduced segment/single-plane topology.
- Old profiles without the new field inherit their old draw distance once.
  Preset selection sets a coherent reference; subsequent manual changes remain
  independent. User configuration and saved presets were not overwritten.

### Verification Protocol

Private evidence: `I:/oot3dre_work/grass-static-cache-20260907/`.
`static-5k` is the rejected sparse prototype, `compact-20k` adds compact storage,
and `indexed-20k` additionally prunes LOD-empty spatial branches.

302 tests pass, including immutable reuse across cuts, distance changes and a
source disappearing/returning; invalidation by height; byte-budget eviction;
codec precision; LOD-prefix bounds; conservative indexed culling; persistence
migration and independent distances. The real F1 smoke passes 1,439 assertions,
including setting visibility to 20,000 and falloff to 5,000 through the widgets.

The 130-second framebuffer run covers boot, the complete title intro and its
return to the sky view. It uses the existing 2560x1440 profile, 906.6 blades/m2,
x2 interpolation, FOV 1.1, toon/outline, CACAO, native fog/light, but extends
visibility from 5,000 to 20,000 without extending density falloff.

`indexed-20k`: 4,787 drawing / 4,820 source-published frames; zero failures,
rejected schedules, unavailable inputs or composition mismatches. All 3,125
mixed-composition source frames draw Grass. The only non-drawing ranges are
initial pending placement and the final sky-facing view. Three initial builds,
**zero subsequent builds** through all camera cuts. Roughly 3.92 million accepted
static anchors; sampled visible mean 32,173, p95 48,724. First complete placement
was observed at host frame 243: this is not an instantaneous cold start.

Indicative, non-time-weighted smoke samples and GPU timestamps:

| Metric at 20,000 visibility | Compact, no LOD tree pruning | Final indexed |
| --- | ---: | ---: |
| Selection CPU mean / p95 | 5.62 / 11.68 ms | 2.59 / 4.34 ms |
| Candidate cluster fraction, mean | 16.08% | 3.31% |
| Visible blades, mean | 32,255 | 32,173 |
| Grass GPU mean / p95 | 0.900 / 1.662 ms | 0.876 / 1.630 ms |

The tiny blade-count difference is due to sampled animated views and startup
timing, not a change in LOD policy. Do not turn these diagnostic runs into FPS
claims. Steady final Grass CPU samples excluding pending/static upload work are
2.84 ms mean / 4.64 ms p95. Initial worker cost is 9.03 seconds **summed across
workers**, versus 20.01 seconds of recurrent work in the old view-focused run.

Repeatable full-intro overrides are in
`tools/triaevum_release/tests/grass_long_distance_settings.json`; use with
`benchmark_gameplay.mjs overrides=... mode=play vsync=true frames=0 seconds=130
capture=true captureStart=180 captureEvery=180 diagnostics=true grassDiagnostics=true
grassTransitions=true`. The harness always copies configuration/save data.

### Remaining Boundaries

Cold room admission still generates and uploads its placements once. Returning
sources reuse RAM entries only while their complete cache key and quota match;
eviction, changed source membership/quotas or edited generation rules may require
a rebuild. Returning through real gameplay was not manually exercised here;
the disappear/return cache contract is tested. Framebuffer checks cover the full
intro, not every game room. The separate occasional synthetic camera-cut defect
recorded above was not reproduced by this run and is not claimed fixed.

The next useful optimization, if cold admission remains intrusive, is a
versioned local derived-placement disk cache or source-level preload through the
existing geometry publication contract. Neither requires scene-specific timing.
Do not reintroduce camera-dependent generation to fix cold-cache latency.

### Uncapped Comparison

`throughput-5k` and `throughput-20k` run the same native work window: 2,400 host
iterations, first 900 excluded, 1,500 measured native frames at fixed delta 1/30.
VSync, SDL limiter, real-time pacing, screenshots and heavy diagnostics are off.
The initial boot/admission interval is outside the measured window. Both use
the same 1440p effects configuration and static placement; only visibility
changes. The window covers the later title-animation views, not interactive
gameplay or every intro camera.

| Visibility / density reference | Native frames | Measured wall time | Native frame throughput |
| --- | ---: | ---: | ---: |
| 5,000 / 5,000 | 1,500 | 20.806 s | 72.09 frames/s |
| 20,000 / 5,000 | 1,500 | 21.525 s | 69.69 frames/s |

Fourfold visibility costs approximately 3.34% throughput in these single matched
runs. This is **not** synthetic x2 FPS, a guaranteed minimum, or a before/after
speedup over the preceding commit: that older 72.84 result used a different
measurement window. The demonstrated gain is removal of recurrent generation
and practical extension of visibility at a small incremental frame cost.
