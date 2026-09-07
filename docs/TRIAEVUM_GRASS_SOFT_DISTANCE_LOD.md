# Grass: soft distance LOD and distant tuft controls

Implemented 2026-09-07 in the shared NRI Grass renderer. No title-specific
placement fixes, native shader changes, or outline shader changes are involved.
Renderer commit: `144c2c41` (parent `09d55858`).

## Controls

F1 -> Grass -> Performance owns all distance controls. Existing draw distance,
density falloff, segment distances, source masks and saved presets are retained.
The following additive fields live in `Grass.Performance` and are included in
the existing Grass preset and named graphics-profile persistence paths.

| F1 label | JSON field | Default / range | Meaning |
| --- | --- | --- | --- |
| Final fade range | `DrawFadeFraction` | 0.15 / 0-1 | Fade over the final fraction of Draw distance, ending at that distance. |
| Density fade softness | `DensityFadeFraction` | 0.10 / 0-1 | Fade individual anchors as distance-dependent retention approaches their stable removal threshold. |
| LOD transition range | `TuftTransitionFraction` | 0.20 / 0-1 | Transition width after Distant LOD starts, measured relative to Density falloff distance. |
| Distant tuft quantity | `FarTuftDensity` | 1.0 / 0.1-4.0x | Multiplier on distant anchor retention, ramped across the tuft transition. |
| Distant tuft spread | `FarTuftSpread` | 1.0 / 0.25-4.0x | Spacing and total width of the silhouettes inside each distant tuft. Does not change the number of silhouettes. |
| Segment transition spread | `SegmentLodSoftness` | 0.5 / 0-1 | Spread the two topology changes across stable anchors instead of switching a complete distance ring at once. |

Blades per distant tuft remains the silhouette-count control. Quantity changes
how many accepted placement anchors survive the distant LOD; spread changes the
footprint at each surviving anchor. Neither creates roots outside source masks.
The visible instance budget and the available placement density remain upper
bounds, so a 4x setting does not guarantee exactly four times as many draws.

Example: with falloff distance 2,000, Distant LOD starts 0.6 and transition range
0.3, the individual-blade/tuft transition runs from 1,200 to 1,800 world units.
Draw distance is independent: a value of 10,000 with final fade 0.2 starts fading
at 8,000 and reaches zero at 10,000. A zero fade or transition width restores a
hard threshold for that control, not the entire previous LOD algorithm.

Old JSON documents missing these six fields remain valid. They acquire these
defaults without moving their existing thresholds or reporting missing-field
errors. Invalid present values are validated/clamped, including non-finite values.
The user's installed configuration was not overwritten during implementation.

The table above describes generic renderer defaults. The user's subsequently
tuned Grass preset is now the title-owned product default; see
`TRIAEVUM_GRASS_PRESET_RENDER_COST.md` for its exact values and the lossless
selection/indexed-geometry optimizations added with it.

## Rendering and performance contracts

- Anchor identity, position and selection seed remain stable. LOD does not
  regenerate placement or maintain a camera-history placement list.
- Each anchor has one submitted topology, never simultaneous near and far draws.
  A deterministic seed distributes the switch across the transition band. A new
  tuft starts at one-blade width and grows continuously after its own switch.
  This is a statistical spatial transition, not a two-mesh pixel crossfade.
- Retention accounts for the mean growth of the selected tuft population:
  `meanGrowth(w) = w + (1-w) * log(1-w)`, with stable endpoint handling.
  The quantity multiplier is independent of this coverage compensation.
- Increasing distant quantity can make retention non-monotonic. BVH rejection
  and sorted cluster prefixes therefore use a conservative retention upper bound,
  not just the nearest-point retention. Frustum bounds include maximum tuft spread.
- Segment LOD still uses at most three segment counts. The stable switch spread
  does not introduce a draw bin for every intermediate count.
- Per-anchor retention fading and final-distance fading use blade-local stipple,
  independent of frame number and screen coordinates. Discard happens before
  color, depth, material, motion and guide writes. Faded fragments cannot leave
  invisible occluders, and no transparency-sorting pass is added.
- Native fog/lighting, scene-before-UI ordering and the existing Grass/outline
  visibility contract are unchanged. This work does not redefine outline behavior.
- Static anchors remain 36 bytes, compact visible indices 4 bytes, and instances
  56 bytes. The previously unused normal W carries the stable selection value in
  both GPU compaction and CPU fallback. Environment records grow by 32 bytes per
  published surface, not per blade. No static cache rebuild or doubled upload is
  required for the new distance controls.

Higher quantity costs instances; wider tufts can cost fill rate. The final fade
does not remove the draw-distance budget. Abrupt native camera cuts, exhausted
budgets and per-anchor topology differences are not eliminated by this feature.

## Ownership

Under `runtime/three_ds_recomp`:

- `include/fast/oot3d/graphics_settings.h` and
  `src/fast/oot3d/graphics_settings{,_persistence}.cpp`: defaults, validation, JSON.
- `src/fast/oot3d/grass_settings_panel.cpp`: the single F1 control surface.
- `include/fast/oot3d/grass_distant_tuft.h`: CPU/GPU tuft and fade equations.
- `src/fast/oot3d/grass_visibility.cpp`: LOD selection and conservative culling.
- `src/fast/oot3d/interactive_grass_pass.cpp`: environment parameters, expansion,
  shading and pre-depth fade discard.
- `src/fast/oot3d/grass_gpu_instance_compactor.cpp`: stable seed delivery without
  changing instance stride.

## Verification

Build only `triaevum_public_runtime` in
`I:/oot3dre_work/triaevum-direct-module-build`, parallelism 3. No title/AOT rebuild.

- `tools/triaevum_release/tests/run_grass_outline_tests.ps1`: **324 tests passed**,
  including real Vulkan execution of the shared Grass LOD shader against CPU
  equations, stable topology mixing, monotonic segment selection, sparse-retention
  fade, boosted-density culling bounds, clamps, old-profile loading and round trips.
- `tools/triaevum_release/tests/run_f1_settings_smoke.ps1`: **1,618 assertions**
  passed on actual compiled F1 widgets, including edits to all six new controls.
  This count includes per-frame UI invariants, not 1,618 gameplay tests.
- `python -m unittest tools.triaevum_release.test_product_graphics_defaults`:
  four passed, one optional runtime/compiler probe skipped.
- Bounded game runs used private configuration/save copies and framebuffer
  captures, not Windows screenshots: Kokiri quantity 0.25x versus 4x, then spread
  1x versus 3x; intro from boot for 1,500 frames with six captures. Grass is present
  in the checked images and the changed distant controls have visible effects.
- All four final runs recorded zero Vulkan/NRI validation errors, zero failed
  Grass/effect-graph passes and zero missing/unresolved graph reads. The existing
  Vulkan warning counter remained 18. These validation/readback runs are visual
  verification, **not performance benchmarks**.

Local evidence, not release payloads:
`I:/oot3dre_work/grass-soft-lod-20260907-{sparse,dense,spread,intro-final}/`.
Logs: `grass-soft-lod-20260907-{release-build,accepted-tests,accepted-f1}.log`
in `I:/oot3dre_work/`. The three JSON overrides under
`tools/triaevum_release/tests/grass_soft_lod_*.json` are reusable with
`benchmark_gameplay.mjs`; the spread override is applied on the dense configuration.

The new GPU test shares `tests/oot3d_vulkan_compute_fixture.h` with the existing
outline GPU test. The fixture extraction changes no production outline code.
