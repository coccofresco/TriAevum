# Grass admission and camera-cut continuity

Updated 2026-09-07. Starting point: project `62893b18d`, renderer `fac84084`.
Shared renderer implementation: `297fda7e`.

## Causes

- The static placement scheduler returned no placement during cold generation.
  A screenshot on its first missing frame stalled presentation long enough for
  workers to catch up, masking the real startup delay in earlier diagnostics.
- The visual continuity tracker required both poor draw matching and a large
  uniform change. A cut inside the same room retains almost every draw, so it
  could interpolate across unrelated cameras, temporarily looking away from
  terrain. Grass culling correctly rejected the terrain in that invented view.
- Shader hook names alone were insufficient: the captured intro uses identity
  view rows and precomposes its camera in the model/palette rows. Measuring only
  `ViewFirstUniform` did not detect these cuts. The rejected intermediate version
  and its raw matrices remain in private diagnostic directories, not the product.

## Ownership and implementation

- `grass_static_placement_cache.cpp` creates a bounded, camera-independent reserve
  on cold admission using the same extractor, masks, wrapping, slopes, spacing,
  seed and transforms as refinement. At most 16,384 generation candidates are
  shared across the current sources. No scene-specific asset or frame rules.
- The reserve stays drawable while the full static placement builds. The user's
  density, draw distance and steady-state pool are unchanged. Complete entries
  still reuse the worker cache. Edited source/rule/transform data never reuse a
  stale reserve; inactive reserve metadata is bounded and cleared on reset.
- This is progressive cold admission, **not instantaneous full-density loading**
  or a disk cache. Masks may legitimately yield no grass. Increasing the candidate
  budget adds samples in the same deterministic triangle sequences; minimum
  spacing can reject a previously coarse candidate when refinement adds neighbors.
- `renderer3ds/pica_camera_temporal_policy.h` measures effective rigid view
  transforms from typed shader hooks, including precomposed model/view matrices.
  Translation is normalized by native PICA projection depth and model scale;
  basis rotation and projection changes have dimensionless presentation limits.
  These are renderer continuity heuristics, **not recovered native camera rules**.
- The visual-frame consumer evaluates matched opaque-world draws, excludes active
  skeleton palettes, and requires a majority of discontinuous rigid draws. A
  moving rigid object cannot normally reset the whole view; HUD/atmosphere and
  changing bone poses do not vote. Unknown layouts keep the existing fallback.
- Rejected transitions use the existing authoritative resynchronization path:
  no invented midpoint, stale grass, cross-room contact sweep, or guest time change.
  Continuous transitions still use x2/x3 visual samples. No savestate format change.

## Defaults

`triaevum_product_graphics.inc` now snapshots the September 7 installed values:
x2, FOV 1.10, PicaMaterial Toon and outline, all grass categories and five masks.
`test_product_graphics_defaults.py` compares the actual compiled product export
against that snapshot. New Forge installs inherit it; existing profiles and
unrequested effects/output settings are not overwritten.

## Repeatable verification

Use `tools/triaevum_release/tests/benchmark_gameplay.mjs` with private config/save
copies, `boot=true mode=play frames=0`, and a bounded duration. This is framebuffer
verification, not an FPS benchmark. Heavy readbacks delay the intro; use source
sequence IDs, not host frame numbers or wall time, to establish coverage.

- `OOT3D_VISUAL_TRANSITION_DIAGNOSTICS=1` records candidate votes and decisions.
- `OOT3D_SCREENSHOT_CAMERA_TRANSITIONS=1` captures the first three presentations
  after each continuity epoch change when screenshot-sequence capture is enabled.
- `grassDiagnostics=true grassTransitions=true diagnostics=true` records placement,
  visibility, composition and framebuffer evidence. `analyze_grass_visibility.mjs`
  distinguishes published sources from actual drawing.
- `analyze_camera_cuts.mjs <capture-directory>` joins cut decisions with captured
  temporal metadata, verifies authoritative resynchronization and reports missing
  captures or synthetic samples crossing a rejected transition.
- Run `oot3d_native_pica_vulkan_plan_tests` and `run_grass_outline_tests.ps1`.
  Regressions cover matching geometry across camera cuts, precomposed cameras,
  translation, model-scale invariance, singular views, origin crossing, isolated
  actor motion, cold admission, black masks, cache reuse and source invalidation.

Private evidence is under `I:/oot3dre_work/grass-cuts-20260907-*`. At the exact
previously bad transition `1395 -> 1396`, `composed` observes 31 discontinuous
rigid draws out of 48 despite 98.75% geometry coverage. Its framebuffer at host
2519 is the correct river/tree view with 124,056 grass blades, not the old tilted
sky/mountain intermediate. The following presentation remains in that same view.
The old comparison pair is in `grass-intro-20260907/final-transitions`, host
2798/2799. These host indices are evidence only and are never used by runtime code.

## Final validation

`grass-cuts-20260907-final`, 210-second bounded run, unchanged installed graphics
profile, 2560x1440, x2 and FOV 1.10; isolated config and save copies:

- Captured source sequence through 3452, including the title sequence, subsequent
  indoor showcase and return to the field. All 16 detected camera cuts have
  authoritative captured presentations; zero captured synthetic crossings and
  zero uncovered cut decisions. This validates the continuity contract, not
  pixel parity with the original game or every possible camera motion.
- First terrain presentation draws 154 blades from 9,390 coarse anchors while
  three full placements are pending. Cold reserve generation takes 17.053 ms;
  the normal camera-independent pool eventually contains 3,923,538 anchors.
- Three full placement builds, zero failures and zero regeneration after they
  complete, including returning to the field. No composition mismatches,
  unavailable inputs or rejected Grass scheduling. All 2,867 eligible mixed
  composition presentations draw Grass.
- The only 31 presentations with published sources but no visible blades occur
  during the end-of-sequence transition. Boundary framebuffer captures show no
  visible terrain; the subsequent indoor showcase has no assigned Grass surface.
  They are not counted as missing Grass on visible terrain. Captures do not
  constitute an exhaustive per-pixel inspection of every presentation.
- The final incremental build, including protection against evicting active
  reserve metadata in scenes with more than 32 sources, succeeds. All 306
  Grass/renderer tests, the visual-frame/plan suite and four product-default tests
  pass. No whole-AOT/title module rebuild was required.

Readbacks, diagnostics and pacing are enabled in this run. Its throughput must
not be published as gameplay FPS or used to claim a performance improvement.
