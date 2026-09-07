# Toon outline behavior and approved defaults

## Restored pre-isolation response (2026-09-07, current)

The user requested the behavior from a few hours earlier, before total Grass
isolation, retaining the foreground-width correction. The reference is renderer
`45f94252` (10:36 CEST), parent of `57361450` (11:48, total isolation).
This is a scoped shader restoration, NOT a whole-project checkout/revert.
Implementation: renderer `09d55858`.

- Restore the UNORM normal-guide response, circular eight-sample footprint,
  original thresholds/softness and extension-marker neighbor rules. Grass is
  not independently outlined; its depth may participate behind native geometry.
- Preserve the native world-depth publication from later fixes. Raw hardware
  depth contains HUD/minimap and secondary-camera canvases as well as Grass.
  The first literal restoration reproduced outlines inside the minimap; it is
  rejected. `oot3d_outline_world_depth` reads mixed depth only for extension
  samples, and published world depth elsewhere. No asset/scene/UI IDs involved.
- Only where extension coverage intersects the footprint, recover an existing
  native contour from the independent guide. This prevents missing center or
  neighbor samples from narrowing the line. Its foreground owner determines
  whether Grass actually covers it. Grass behind the object cannot halve the
  silhouette; Grass in front can hide it.
- Keep shared compute/scanout shader ownership, native fog, pre-UI composition,
  guide lifecycle and MSAA support. The graph explicitly declares NormalGuide
  reads again for Outline, Composite and direct Scanout. No new resources or
  draws, no configuration options, no preset/settings changes.

`oot3d_toon_outline_gpu_tests.cpp` now executes two sets of six scenarios on
Vulkan: full-width/occlusion regression and historical guide response, including
normal-only scene detail, absent coverage, disabled detectors and a secondary
depth layer that must not create contours. The latter deliberately distinguishes
the restored response from the total-isolation kernel. 317/317 focused tests pass
in `I:/oot3dre_work/outline-restore-20260907-final-tests.log`.

Framebuffer evidence is under `I:/oot3dre_work/outline-restore-20260907-*`:
`kokiri-final` uses the HUD-test checkpoint with actual user Grass settings;
`intro-final` boots normally and captures every 240 frames through frame 1440.
The earlier unsuffixed runs document the rejected raw-depth restoration.
These diagnostic captures are not performance measurements or a claim of
pixel-identical reproduction of the entire historical binary.

Final validation: Kokiri 120 frames; intro 1500 frames with six framebuffer
captures; TAA composition and MSAA 4x each 120 Kokiri frames. All retained
diagnostic frames have zero NRI/Vulkan errors, failed/unresolved graph passes,
missing reads or geometry execution failures. Actual Grass draws are present.
Existing Vulkan warnings remain (counter 18); this is not a zero-warning claim.
Framebuffer inspection confirms the rejected minimap outlines are gone and no
logo-canvas rectangle appears in the inspected late-intro captures. Verification
processes exited normally. The executable remains in
`I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe`.

Diagnostic 7 remains the independent native reference; diagnostic 5 is the
restored scene response after visibility. The old strict on/off mask subset
comparison below described the isolated policy and is not the acceptance
criterion for this restoration. Use the GPU width regression plus real scene
captures. All sections below describe previous, superseded behavior/evidence.

## Foreground contour width (2026-09-07, retained correction)

Renderer implementation and GPU regression: `1be18f9b`.

User feedback refined the remaining failure: in Kokiri the contour shape was
correct, but became thinner where it overlapped background Grass. The preceding
strict mask tests could NOT detect this: they only required no new edges and
some occlusion, so incorrectly removing half a foreground contour also passed.

The failing visibility test used the native **center-pixel** depth. The circular
edge gather dilates silhouettes onto background pixels too. There Grass may be
closer than the background but still behind the object that generated the edge.
Comparing against the background wrongly discarded the outer half of the line.

`toon_outline_shader.cpp` now returns intensity plus **edge-owner depth** from
the same native gather. The strongest depth/normal discontinuity carries its
nearer contributing native surface. Equal-strength candidates choose the nearer
owner. Occlusion compares against that owner, not the background under the line.
Raw edge response, width, sample positions and user settings are unchanged.
No new attachments, passes, barriers, object/asset exceptions or classification
heuristics; direct scanout and pre-temporal composition use the same kernel.

New regression coverage: `tests/oot3d_toon_outline_gpu_tests.cpp` executes the
actual production GLSL on Vulkan, with six deterministic scenarios: no occluder,
Grass behind the object/in front of its background, Grass in front of everything,
Grass behind everything, equal-depth coverage, and a normal-only edge.
The previous shader demonstrably fails on the outer two of four contour pixels
in the background-Grass and equal-depth cases. The corrected shader passes all
48 samples, retaining all four contour pixels unless truly occluded in front.
This is an actual shader readback, not a duplicated CPU formula or string test.

The standalone CMake target `oot3d_toon_outline_gpu_tests` links just shaderc,
Vulkan, the contour library and GTest; it needs a Vulkan compute device but no
game assets/title module. The incremental `run_grass_outline_tests.ps1` suite
also includes it. Logs are `I:/oot3dre_work/outline-owner-20260907-before-tests.log`
(315 pass, new regression fails) and `...-after-tests.log` (316/316 pass).

Diagnostic investigation before the correction:
`outline-grass-regression-20260907-raw-on/off` contains six full-boot mask pairs
at frames 240,480,720,960,1200,1440. Each is pixel-identical. They did not support
changing guide generation or adding a separate depth buffer. The temporary
normal-marker diagnostic was removed; it is not an extension-coverage heuristic
in the production renderer.

Final game evidence: `I:/oot3dre_work/outline-owner-20260907-*`, exact Kokiri
HUD-test checkpoint, 120 fixed-work presentations, capture 110 at 2560x1440:

- `mode7-on/off`: 146,205 raw contour pixels each, **zero differences**.
- `mode5-on/off`: 131,122 versus 142,209 active final pixels; 11,673 changed,
  **zero increased pixels**. Foreground occlusion remains functional without
  Grass-generated edges. The stronger full-width guarantee comes from the
  adversarial GPU regression above, not this subset comparison alone.
- `msaa` / `taa`: both 120 frames, 118 actual Grass executions, real 4x MSAA
  and pre-temporal composition respectively; framebuffer inspection includes
  actor contours against Grass and blades in front of lower legs.
- All six runs: zero NRI/Vulkan validation errors, failed/unresolved graph
  passes, missing reads or Grass execution failures. Existing Vulkan warnings
  remain; no unqualified zero-warning or performance claim is made.
- The user changed Grass LOD fractions since the preceding tranche. Those
  values were preserved. A pixel comparison against that older Grass mask is
  not controlled evidence; current paired runs use identical full graphics
  settings except Grass enablement, now enforced by the comparator.
- Verification processes exited; the updated executable is in the normal
  `triaevum-direct-module-build` directory. No savestate or installed setting
  was rewritten by this correction.

## Historical contract: detection versus visibility (2026-09-07)

Renderer implementation: `a303ccd8`.

The user clarified that Grass must not generate/change native contours, but
**must hide them when in front**. This supersedes the earlier requirement below
that final Grass-on/off masks must be identical. The historical comparisons
still document the earlier behavior; they are not the current acceptance test.

- `oot3d_toon_outline_native_edge` gathers only `OutlineGeometryGuide`.
- `oot3d_toon_outline_edge` applies visibility AFTER detection. Occluders can
  remove existing contour pixels, never create new ones. Depth equality or an
  occluder behind the native surface does not suppress the contour.
- Grass publishes `1 - gl_FragDepth` into motion-guide alpha, after its native
  depth conversion and tuft fragment discard. Its real hardware depth test
  determines coverage. RGB retains the existing zero-motion output.
- `ConfigureOutlineCoverageBlend` shares MAX inverse-depth accumulation with
  the native transparency pass. Both writers preserve the nearest occluder;
  native foreground opaque draws retain their existing reset behavior.
- No native geometry/fog-guide writes, new attachments, descriptors, draw
  passes or settings. Color clear and MSAA lifecycle remain graph-owned. Both
  direct scanout and pre-temporal composition use the same shader library.
- Diagnostic view **7** is the raw contour detector, **5** the final occluded
  mask. Compare paired mode-7 captures for exact equality, and paired mode-5
  captures with `compare_outline_masks.mjs --occlusion` for strictly fewer
  visible contour pixels and no added pixels. Real GPU Grass draws are required.

The current user Grass snapshot is saved in the product default and installed
`GrassSavedPreset`: minimum spacing 1; collider height 1.69, radius 1.19, push
1.66, velocity response 0.87 and vertical margin 69. Every other Grass value is
preserved, including all six mask rules. x2, FOV 1.10, Toon and TopScreen defaults
remain unchanged. Existing unrelated user settings are not overwritten.

Verification artifacts: `I:/oot3dre_work/grass-outline-occlusion-20260907-*`.

- `mode7-on/off`: same binary/checkpoint/sample, 120 fixed-work presentations,
  capture 110, 2560x1440. **146,205 active raw-mask pixels each; zero differences**.
- `mode5-on/off`: **21,142 changed pixels, zero increased pixels**. Active final
  pixels are 120,960 versus 141,333. Grass only removes/attenuates covered native
  contours; it cannot introduce an edge. Both strict comparator modes pass and
  verify actual GPU Grass draws, not just enabled settings.
- All four runs: 120 frames, zero NRI/Vulkan errors, failed/unresolved effect
  passes, missing reads or Grass execution failures. Grass executes on 118/120
  frames for the on runs and 0/120 for off (restore initialization is excluded
  from the actual captured sample).
- `msaa` / `taa`: 60 presentations each; real 4x MSAA and pre-temporal composite
  paths, 58 Grass frames each, zero validation errors or graph failures. Their
  native framebuffer captures retain foreground actor contours and a separate
  HUD while blades hide covered lower contours.
- `intro`: full boot, 1500 presentations, clean exit; captures 1200 and 1440.
  All 400 retained final diagnostics (1101-1500) execute Grass; no validation
  errors or graph failures. Native title layers remain visible without canvas
  rectangle outlines in the inspected frames. This is not all-intro coverage.
- The same 18 existing Vulkan unused-interface/disabled-overlay warnings remain;
  NRI warnings are zero. These are correctness runs, not performance benchmarks.
- **315/315 renderer tests**, including real shader compilation, and **5/5
  product-default tests**, including export from the compiled executable.
  A structured comparison verifies complete Grass equality between the installed
  current settings, saved preset and product default (not just the six changes).
- All bounded verification processes have exited. No game logic, saves, native
  assets, UI behavior or unrequested default effects were changed.

2026-09-07, `release/triaevum-precompiled`; baseline application `c77c9d14a`,
renderer `45f94252`. Supersedes the remaining Grass/outline limitation in
`TRIAEVUM_GRASS_FIRST_FRAME_AND_GAMEPLAY_CAMERA.md`.
Renderer implementation: `57361450a1` (Isolate toon outlines from extension geometry).
Follow-up renderer: `3e01e996` (Fix native outline coverage and isolate it from extension depth).
That first implementation was incomplete. The follow-up below supersedes its
visual-success claims and documents the actual isolation checks.

## Default snapshot

- `tools/oot3d/native_game_runtime/triaevum_product_graphics.inc`: x2 visual
  interpolation, scene FOV 1.10, complete current Toon/Outline and Grass JSON.
  Includes six source/mask rules, interaction, wind, appearance, generation,
  budgets and near/far LOD. The saved Grass preset receives the same object.
- Grass changes from the preceding snapshot include nine blades per far tuft,
  density-reference distance 2282, segment transition 501-848, LOD fractions
  0.31/0.31, far density 1, texture influence 0.22 and root brightness 2.87.
  Values are the user's snapshot, not an automatic quality adjustment.
- `config/topscreen_ui.example.json`: complete current TopScreen settings,
  including HUD scale 0.80. The native config constant in
  `tools/oot3d/ui_topscreen/oot3d_top_screen_config.h` agrees. Remaining values
  already matched the installed profile; only floating-point representation
  differs between 0.8 and the saved float 0.800000011920929.
- Existing user configs/saves are not overwritten. Generic Authentic settings
  and unrequested effects remain unchanged. Forge and fresh product launches
  consume these defaults through the existing product/config owners.

## Root cause and contract

Excluding the Grass pixel by normal-guide alpha did not isolate the outline:
the eight-neighbor depth gather still saw generated blades. Even correct
coverage flags could not reconstruct the native surface underneath them.

The renderer now publishes **OutlineGeometryGuide**, a native-only auxiliary
attachment at location 6: `RGBA32_SFLOAT`, native unit normal in RGB and native
window depth in A. Clear value is `(0,0,0,1)`; a zero normal denotes no geometry.
Float32 avoids quantizing depth to half-float or packing bits into an MSAA-
resolved float. The existing fog, AO, material and motion guides are unchanged.

Only scene draws with RGB output, ordinary fragment operation, depth test,
depth write, replacement RGB blending and native normal semantics publish
geometry. PICA ONE/ZERO blending is opaque even when blending is enabled.
Alpha-composited draws remain transparent even when they write depth; they
preserve the underlying guide and only contribute native transparent occlusion.
Always-pass raster initializers invalidate the guide instead of becoming a
full-screen surface. Grass writes no channels of this attachment.

The shared outline kernel gathers this guide with nearest texel reads. It must
not sample the mixed native/extension scene depth, including at its center:
that was still allowing Grass to erase contours. Native transparent occlusion
uses nearest samples of the same native window-depth convention as geometry,
including explicit gl_FragDepth, not the untransformed gl_FragCoord.z. Native
plants remain native geometry and legitimately receive outlines.

## Ownership and lifecycle

Renderer paths below are relative to `runtime/three_ds_recomp/`:

- Shared `renderer3ds/pica_attachment_contract.h` defines the stable seven-MRT
  instrumented ABI; canonical Native Fidelity still uses one color attachment.
- `pica_shader_instrumentation.cpp` produces the auxiliary output without
  changing canonical color/depth instructions. Shader keys and pipeline
  manifests distinguish the new output; old manifests accept its absent flag.
- `effect_graph*`, `display_effect_plan.cpp` and `pica_guide_sampling_barriers`
  declare and own its lifetime, bindings and transitions.
- `nri_pica_render_target_owner`, `nri_pica_render_target_init_pass` and Vulkan
  target plumbing create, initialize, resolve and retire it, including MSAA.
- `nri_pica_memory_fill_clear_pass` resets it on native color clears only.
  A depth-only clear starts another depth layer without erasing existing color
  or its contour guide. The Vulkan fallback obeys the same rule.
  Clear plans now respect the actual attachment bundle, including Authentic.
- `toon_outline_shader`, `scene_composite_pass` and `pica_scanout_effects`
  consume it. The obsolete alpha-marker outline helper is removed.

There is no full-scene replay, CPU mesh copy, native draw reordering, camera
exception or asset-specific correction. Transparent native coverage has a small
auxiliary raster pass described below. Cost is one 16-byte/pixel instrumented
guide plus its MSAA image when enabled. These are derived rendering resources,
not a savestate schema change. They initialize empty on restore and are rebuilt
by native draws; old save files remain readable.

## Verification

Build uses the existing Clang incremental target `triaevum_public_runtime`,
three jobs, without regenerating/recompiling title AOT. Executable:
`I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe`, paired with the
installed launch profile, not copied over a catalogued release executable.

Reproduction harness: `tools/triaevum_release/tests/benchmark_gameplay.mjs`.
It copies user configs/save data privately. `validation=true` explicitly enables
the Vulkan validation layer for correctness runs; throughput tests default off.
All images are captured from the framebuffer, not from Windows.

Evidence directories share prefix `I:/oot3dre_work/native-outline-guide-20260907-`.
`gameplay-v1` / `gameplay-off-v1` are the paired Grass/outline captures; `intro-v1`
exposed the missing depth-test eligibility in title layers, so it is diagnostic
evidence, not the final visual result.

Historical results from the first implementation (not sufficient acceptance):

- `run_grass_outline_tests.ps1`: **311/311 tests**, including actual shader
  compilation, attachment contracts, graph bindings and manifest round-trips.
  Log: `native-outline-guide-20260907-tests-final2.log` under the work directory.
- Actual F1 widget smoke: **1467 assertions passed** (`...-f1.log`). Product
  defaults: **5/5 tests**, including export from the final executable.
- `intro-v2`: 6414 presentations over the bounded 150-second run, 21 framebuffer
  captures. Subsequent user feedback and guide inspection disproved the claim
  that this removed all title rectangles and Grass interference. Sparse color
  captures were insufficient; the mask comparisons below replace that check.
  All 2997 mixed-composition source frames drew Grass. Across all 5147 source
  frames, 13 had no Grass draw because visibility culled their anchors during a
  transition; this is not a claim that every source frame contains Grass.
  No geometry execution failures, schedule rejects or composition mismatches.
- `msaa-final`: actual **4x MSAA**, seven instrumented color attachments;
  framebuffer confirms native character/plant contours without blade contours.
- `authentic-final`: one canonical color attachment, no extension Grass/outline;
  framebuffer confirms the native scene remains visible.
- Both final 120-presentation runs: **zero NRI/Vulkan validation errors**, zero
  failed graph passes, missing reads or unresolved passes. Each reports the same
  18 Vulkan warnings for disabled host overlay layers and unused shader inputs/
  outputs; these are not a clean-warning claim. NRI reports zero warnings.
  Logs also report legacy-save clock initialization, old config normalization
  and audio underruns during diagnostic capture.

These are correctness runs with readbacks/diagnostics, not uncapped performance
measurements or exhaustive gameplay coverage. The final incremental build is
complete; test processes exited and no installed user config/save was changed.

## Follow-up: native coverage and stable occlusion

The user reproduced Grass interference, title rectangles and disappearing
outlines after the first implementation. Investigation used actual backend
draw traces (including interpolation replay), not only the frontend semantic
trace, which did not contain draws in this runtime path.

Corrections, confined to renderer instrumentation/composition:

- Removed the remaining mixed-depth center rejection. Grass no longer writes
  the native transparent-occlusion alpha channel of the motion attachment.
- Native opaque writers explicitly reset that alpha when no motion output is
  generated. Previously the attachment write mask could expose an undefined
  fragment output, making the entire scene appear occluded.
- Native transparency requires a real depth test and uses native window depth.
  Screen-space fades and unconditional overlays do not become depth occluders.
- Native depth-only clears preserve the contour guide. Unconditional raster
  initializers invalidate it. Depth compare is part of the shader-variant key.
- Replaced the assumption that depth-write means opaque with the actual native
  RGB blend contract. In the logo trace, a six-vertex SRC_ALPHA /
  ONE_MINUS_SRC_ALPHA draw also wrote depth. Its invisible rectangular canvas
  had been generating contours; it now preserves the geometry guide beneath it.

The initial follow-up still had 51 reproducible differing gameplay pixels. The
geometry guide was identical with Grass on/off; transparent occlusion differed
because hardware depth rejected native particles behind extension blades.
`outline_occlusion_pass.h` now isolates that coverage update:

- It reuses the native shader, bound geometry, textures, uniforms and scissor.
- Hardware depth test/write are disabled ONLY for the guide pass. The outline
  kernel compares its nearest native transparency against native geometry.
- Only motion-guide alpha is writable, using MAX of inverse window depth.
  Native color, depth, stencil and all other guide channels are read-only.
- The pass precedes the native color draw so it sees the original stencil;
  native stencil comparisons remain active but all stencil operations KEEP.
- NRI `DrawBoundGeometry` reuses already-bound resources without new uploads or
  descriptor allocation. Vulkan fallback follows the same contract.
- The primary transparent color pass cannot write this alpha. This makes the
  isolated pass its sole transparent producer; opaque reset behavior remains.
- Pipelines are cached separately, using the same compiled shader. This adds
  no image and no shader compilation. The covered Kokiri state uses up to 33
  coverage draws per frame, not a second scene replay. Diagnostics expose
  `outline_occlusion_draw_count` and CPU command timing includes this work.

No camera/asset IDs, native color/depth changes, Grass disabling or extra render
targets are used. Product defaults remain unchanged.

### Reproduction and evidence

Artifacts: `I:/oot3dre_work/outline-isolation-20260907-*`.

- `guides-before`: diagnostic view 6 proves that Grass depth rejected native
  contours and an initializer made the sky a valid contour surface.
- `logo-guide`: view 4 shows the transparent rectangular plane as solid guide
  geometry; backend trace at presentation 1800 identifies its blend/depth
  state. `checkpoint.oot3dsav` was captured by the game itself.
- `mask-on` / `mask-off`: same checkpoint, 120 fixed-work presentations,
  capture at 110, diagnostic view 5. Both have **48,573 nonzero contour pixels**
  at 2560x1440; zero differing pixels. **Not accepted as Grass-isolation proof:**
  later inspection found that this mid-title restore did not regenerate Grass,
  despite the enabled setting. Use the actual Grass-on Kokiri tests below.
- `intro-final`: 20 native framebuffer captures through presentation 6000.
  Inspected title frames retain geometric contours without text-plane boxes;
  subsequent scene captures retain contours. The watchdog interrupted final
  large diagnostics serialization after gameplay ended (`runtime.json` exists,
  `vulkan.json` is empty). This run is visual evidence, NOT a clean validation
  exit or a performance measurement.
- `kokiri-final-on` / `kokiri-final-off`: **141,333 nonzero pixels** in each
  diagnostic framebuffer (HUD remains separately composited), zero differing
  pixels. Native character/plant contours were also inspected visually, so this
  is not an empty-mask or HUD-only success. Repeating the Grass-on run before
  the last fix reproduced its exact 51-pixel error; the new pass removed it.
- Both Kokiri validation runs: exit 0; zero NRI/Vulkan errors, failed effect
  passes or missing reads. Vulkan reports 18 pre-existing unused-interface/
  disabled-overlay-layer warnings; NRI reports zero warnings.
- `kokiri-prepass`: final ordering (coverage before native stencil mutations)
  reproduces the exact same mask with Grass drawing; zero validation errors.
- `certified-on` / `certified-off`: final executable, recorded SHA-256 and mode,
  same checkpoint and temporal sample. The strengthened comparator passes in
  **grass_isolation** mode: actual GPU Grass draw versus none, 141,333 nonzero
  pixels on both sides, zero differing pixels. This is the authoritative paired
  acceptance test; the earlier title restore comparison is expressly excluded.
- `msaa-prepass`: final code, 4x MSAA, actual Grass/coverage draws, zero
  NRI/Vulkan errors or failed/missing effect reads over 60 presentations.
- `title-prepass`: native contours and text-plane correction remain visible,
  but no Grass was drawn after this restore, so it is not an isolation test.
- `boot-prepass`: final code from complete boot, 2100 presentations, seven
  framebuffer captures, clean exit. Frame 1200 explicitly records a Grass draw
  and 16 isolated native coverage draws; the image retains native contours
  without rectangle outlines around the logo/text planes. Retained diagnostics
  are bounded (last 1200 frames), not a full-frame-history validation claim.
- Unit suite: **313/313**, including shaderc compilation of explicit/fixed depth,
  opaque resets, depth-writing transparency, screen overlays, Always/Less cache
  separation, native guide contracts, shared contour kernel isolation and
  proof that the auxiliary pass cannot write native color/depth/stencil or
  other guide channels.

Tools:

- `TRIAEVUM_GUIDE_DIAGNOSTIC_VIEW=4`: native outline geometry guide.
- Value `5`: the actual pre-fog contour mask, white on black.
- Value `6`: valid guide green, extension-depth rejection red, native
  transparent-depth presence blue. Diagnostic only; not the production kernel.
- `TRIAEVUM_OUTLINE_TRACE_FIRST` / `..._LAST`: bounded actual draw trace.
- `benchmark_gameplay.mjs saveAt=N`: capture an exact checkpoint during a run.
  Use the same `state`, default fixed-work mode, `frames` and `captureStart`
  for both mask captures. Use `outline_grass_off.json` as the only override.
- `compare_outline_masks.mjs on.bmp off.bmp`: checks dimensions, compares every
  pixel and fails on differences or empty masks. Its default strict mode also
  requires matching executable SHA-256, checkpoint, captured temporal sample,
  fixed guest work, diagnostic mode 5 and GPU evidence that Grass actually drew
  in the first capture and not the second. Retain `vulkan.json` for that frame.
  `--pixels-only` deliberately bypasses those requirements for exploratory
  comparisons and cannot certify Grass isolation. Old captures without the
  newly recorded invocation metadata are pixel-only evidence.

The diagnostic mask path currently targets direct scanout (the user profile);
temporally baked composition may bypass it. It is not evidence for TAA history
equivalence. Native renderer validation is performed separately with bounded
diagnostic retention, rather than retaining every draw of a long intro.
The native geometry guide still shares native rasterization's depth test;
new extension insertion orders must revalidate late opaque writers as well as
transparency. The paired geometry images were identical in the covered Kokiri
state. The separate mid-title savestate Grass-regeneration issue is not fixed
or hidden by this outline change; boot remains the title reference.
