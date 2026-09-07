# OOT3D NRI Renderer Recovery Status

Last updated: 2026-07-26

## Scope

This document tracks restoration of renderer-owned graphics features in the
Vulkan/NRI backend:

- display resolution, window mode, borderless/fullscreen, and VSync;
- MSAA and CACAO;
- interactive grass and its authoring controls;
- TopScreen 1.2 HUD placement and aspect-independent scaling.

Gameplay and OOT3D asset interpretation remain outside this renderer tranche
unless a narrow bridge is required to provide typed renderer input.

## Architecture

Renderer settings are authored through `GraphicsSettingsRuntime`. Backend
implementations consume immutable snapshots and report transaction outcomes.
Feature-specific policy and state transitions belong in small modules under
`runtime/three_ds_recomp/src/fast/oot3d`; `gfx_vulkan.cpp` only coordinates those modules
with Vulkan/NRI resources.

The host application must not duplicate swapchain, effect, or layout policy.
Local JSON files are runtime inputs and are not source authority.

## Completed

### Presentation Settings Consumer

Nested renderer commit: `55583233 fix(renderer): apply NRI presentation settings`

Files:

- `runtime/three_ds_recomp/include/fast/oot3d/renderer_presentation_controller.h`
- `runtime/three_ds_recomp/src/fast/oot3d/renderer_presentation_controller.cpp`
- `runtime/three_ds_recomp/src/fast/backends/gfx_vulkan.cpp`

The controller distinguishes:

1. initial persisted settings;
2. a candidate awaiting user confirmation;
3. automatic or explicit rollback.

The Vulkan backend applies the request before frame acquisition, recreates the
swapchain through the existing safe transaction, and acknowledges success or
rejection to `GraphicsSettingsRuntime`. Presentation timeout ticking no longer
depends on the F1 panel being visible.

Validation:

- focused controller/transaction tests: 4 passed;
- `oot3d_native_game` incremental build: 6 compile actions, 10 total;
- bounded NRI run started with a `1600x900` host canvas and recorded the
  configured `1280x720` renderer output in
  `I:\oot3dre_work\native_game\presentation_runtime_vulkan_20260725.json`.

An immediate frame-zero BMP can predate the presentation transaction. Delayed
framebuffer captures and Vulkan diagnostics are authoritative for the applied
output size.

### MSAA

The existing NRI PICA targets, dynamic-rendering pipelines, and resolve path
were exercised with 4x MSAA. After the target recreation, real PICA frames
reported `msaa_samples=4` while dynamic draw and scanout remained active.

Evidence:

- `I:\oot3dre_work\native_game\nri_msaa4_validation_20260725.json`

### CACAO And Scene-View Availability

The runtime now publishes a recovered perspective frustum independently of
`PlayState`. Camera `eye/at` metadata enriches that view when gameplay supplies
it. This distinction lets projection-only effects such as CACAO run during
cutscenes and startup intervals without fabricating a camera, while temporal
and camera-dependent effects remain gated.

On the Kokiri checkpoint, CACAO enabled produced 78 dispatch, normal-guide,
and ambient-composite frames out of 130. The disabled run produced none.
Delayed framebuffer captures at presentation frame 100 differ in both hash
and pixels; the scene-only right crop has normalized RMSE `0.0139191`.

Evidence:

- `I:\oot3dre_work\native_game\nri_cacao_on_frame100_validation_20260725.json`
- `I:\oot3dre_work\native_game\nri_cacao_off_frame100_validation_20260725.json`
- `I:\oot3dre_work\native_game\nri_cacao_on_frame100_20260725.bmp`
- `I:\oot3dre_work\native_game\nri_cacao_off_frame100_20260725.bmp`

The earlier byte-identical black captures sampled presentation frame zero,
before the loaded scene and CACAO became active; they were not evidence of a
neutralized composite.

### Interactive Grass

Grass is now a renderer-owned NRI pass with independently testable blade
geometry. The settings contract and F1 panel expose:

- root and tip color;
- global height scale;
- one through twelve segments per blade;
- per-blade wind-phase randomness;
- per-texture blade height and width ranges.

The settings schema is version 4. Version 3 documents migrate to the previous
visual defaults. Every placement property participates in the placement-cache
fingerprint, so editing height, width, density, slope, channel, or seed cannot
reuse stale anchors.

Two integration gaps had prevented the existing pass from receiving native
scene geometry:

1. top-screen targets are stored in either `400x240` or rotated/scaled
   orientation; target classification now uses the logical extent rather than
   a fixed width/height order;
2. immutable PICA draw plans no longer repeat texture payload bytes. Grass and
   reflection matching now consume the native content hash when available and
   use bytes only as a hash fallback.

The native PICA consumer also publishes typed CMB position and UV0 semantics.
It resolves the native SEPD/VATR stream order through the live
attribute-to-input-register map; the renderer no longer guesses shader input
locations.

Validation from the Kokiri checkpoint with an automated temporary material
assignment found 37 source meshes and 36 texture selectors. The pass rendered
540 blades, ran on 145 of 200 captured frames, and supplied temporal motion for
146 frames. Maximum recorded Grass GPU time was approximately `0.12 ms`.

Evidence:

- `I:\oot3dre_work\native_game\ri_grass_semantics_validation_20260726.json`
- `I:\oot3dre_work\native_game\ri_grass_semantics_runtime_20260726.json`
- `I:\oot3dre_work\native_game\ri_grass_semantics_frame160_20260726.bmp`

Automatic assignment remains diagnostic-only. Production rendering still
requires explicit native texture rules, so unrelated materials are never
classified as grass implicitly.

### TopScreen Logical Presentation

Nested renderer commit: `26fd358b feat(renderer): centralize UI presentation layout`

The renderer now owns one pure contained-presentation transform shared by:

- native PICA scanout;
- native input coordinate mapping;
- host-rendered UI overlays.

TopScreen primitives remain authored and clipped in their native `400x240`
logical domain. The renderer fits that domain to the output; no producer embeds
the former `13.333`-pixel offset for a presumed 16:9 host canvas. Non-TopScreen
UI continues to use its independent `1280/3 x 240` widescreen canvas.

The focused layout tests cover 4:3, 16:9, ultrawide, and invalid extents.
TopScreen profile tests cover clipping, promoted pause edges, and camera-option
labels in native coordinates.

The same renderer module now owns the inverse target-to-logical transform
(`ac3ffbac feat(renderer): invert UI presentation coordinates`).
TopScreen pointer input first enters the contained `400x240` presentation,
then the native consumer converts its X coordinate back to the unchanged
`320x240` CTR touch domain. This matches the mod's `400/320` pause projection
without duplicating aspect or letterbox policy in input code. The default
OoT3D profile retains direct `320x240` lower-screen mapping. Focused tests
cover the recovered Gear, Map, and Items centers at 16:9 and 4:3 and reject
touches in TopScreen letterbox regions.

Runtime validation loaded the same HUD checkpoint for 140 presentation frames
at `1280x720` and `1024x768`. Both runs selected the TopScreen profile,
observed gameplay composition, and drew 2502 UI primitives. The 16:9
framebuffer is byte-identical to the pre-change baseline (`RMSE 0`). At 4:3,
the corrected host HUD now shares the scene's centered 5:3 viewport instead of
being inset into an unrelated 16:9 viewport.

Evidence:

- `I:\oot3dre_work\native_game\ri_topscreen_aspect_16x9_fixed_runtime_20260726.json`
- `I:\oot3dre_work\native_game\ri_topscreen_aspect_16x9_fixed_frame120_20260726.bmp`
- `I:\oot3dre_work\native_game\ri_topscreen_aspect_4x3_fixed_runtime_20260726.json`
- `I:\oot3dre_work\native_game\ri_topscreen_aspect_4x3_fixed_frame120_20260726.bmp`

The complete Items-page path was also exercised at `1720x720`. The renderer
fits the `400x240` page to a centered `1200x720` viewport while leaving the
ultrawide scene visible at the sides. The late framebuffer capture contains
nonzero image data (`mean=0.209651`) and the run records 33,640 native PICA
draws, 298 NRI scanouts, 150 NRI presents, and no presentation failure.

Evidence:

- `I:\oot3dre_work\native_game\ri_topscreen_items_ultrawide_late_runtime_20260726.json`
- `I:\oot3dre_work\native_game\ri_topscreen_items_ultrawide_late_validation_20260726.json`
- `I:\oot3dre_work\native_game\ri_topscreen_items_ultrawide_late_frame140_20260726.bmp`

### Window And Output Modes

Nested renderer commit:
`ded7a6cc fix(renderer): honor NRI presentation modes`

The NRI presentation transaction now records the accepted window mode,
requested output extent, actual swapchain extent, and VSync state on every
diagnostic frame. Fullscreen success is validated against SDL's actual window
flags instead of the configuration variable alone.

The previous exclusive-fullscreen path always installed the desktop display
mode before entering fullscreen. Consequently, a requested `1280x720`
exclusive output produced a `3840x2160` swapchain on the test display. The SDL
boundary now selects the closest exclusive display mode requested by NRI.
Borderless intentionally retains the desktop mode; generic non-NRI fullscreen
entry retains its previous desktop-mode behavior.

Runtime results:

| Requested mode | Requested extent | Actual swapchain |
| --- | ---: | ---: |
| Windowed | `1280x720` | `1280x720` |
| Borderless | `1280x720` | `3840x2160` desktop |
| Exclusive fullscreen | `1280x720` | `1280x720` |

All three runs used the NRI-owned swapchain with no fallback, failed apply, or
rollback. Four bounded diagnostics tests and 21 focused presentation, MSAA,
CACAO, Grass, and UI-layout tests pass. The incremental product build required
three compile actions and six total actions after the final SDL isolation
change.

Evidence:

- `I:\oot3dre_work\native_game\ri_presentation_windowed_20260726.json`
- `I:\oot3dre_work\native_game\ri_presentation_borderless_20260726.json`
- `I:\oot3dre_work\native_game\ri_presentation_exclusive_final_20260726.json`

## Verification Closure

The recovery block covered by this document is closed:

1. CACAO executes from recovered projection metadata and has an enabled versus
   disabled framebuffer difference.
2. MSAA uses the NRI PICA multisample and resolve path.
3. Grass is a renderer-owned pass with configurable colors, height, segments,
   and wind randomness sourced from native CMB semantics.
4. TopScreen HUD and pause surfaces share one renderer-owned logical
   presentation and inverse pointer transform across 4:3, 16:9, and ultrawide.
5. Windowed, borderless, exclusive fullscreen, resolution, and VSync settings
   reach the NRI swapchain transaction and expose their accepted state.

Future feature work must retain these boundaries rather than moving effect,
aspect, or presentation policy back into the native game host.
