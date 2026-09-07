# First-Frame Grass and Gameplay Camera

2026-09-07, `release/triaevum-precompiled`. Supersedes the coarse first-frame
reserve, not historical measurements. Native PICA ordering, simulation timing,
save formats and the title module remain unchanged.

Renderer commit: `45f94252`. Application baseline: `72a6bb5ca`; the following
application commit carries the camera wiring, controls/defaults and this report.

## Approved Defaults

`tools/oot3d/native_game_runtime/triaevum_product_graphics.inc` captures x2 visual
interpolation, scene FOV 1.10, current toon/outline and all Grass settings/preset.
Changes since the preceding capture: first light level 0.168, outline width 0.95,
normal sensitivity 0.02, collision push 1.69 and a sixth texture-mask rule.
Unrequested effects and machine-specific paths are not imported.

The installed TopScreen configuration already equals
`config/topscreen_ui.example.json`, used by Forge for new installs. Free camera
remains off by default as in that profile. Existing user configs are preserved;
new Grass options migrate normally. Five product tests check the compiled
default export, complete Grass JSON and TopScreen template.

## Grass

Files below belong to `runtime/three_ds_recomp/`:

- `grass_async_placement_builder.{h,cpp}`: immutable shared-future completion
  tickets survive cache eviction; cancellation resolves as failure.
- `grass_static_placement_cache.cpp`: queue all new source jobs, then await
  complete placement before admitting the first terrain draw. Workers remain
  parallel; the old 16,384-candidate coarse placeholder is removed.
- `grass_distant_tuft.h`: isolated CPU/GLSL transition and procedural silhouette.
- `grass_visibility.cpp`: tuft footprint growth and inverse instance retention
  share the same policy across BVH, sorted cluster prefixes and individual roots.
  Near detail and stable roots remain unchanged. Visibility distance is still
  independent of density-reference distance.
- `interactive_grass_pass.cpp`: one extra topology, six-vertex cutout quads,
  preserving lighting, fog, wind, interaction, depth and composition contracts.
- `grass_settings_panel.cpp`: Performance owns Distant tufts and Blades per
  distant tuft (2-9, default 5), alongside Distant LOD starts.

At full transition, five silhouettes use one instance/six vertices instead of
five instances/fifteen vertices for the individual single-triangle far LOD:
80% fewer instances, 60% fewer vertices in that topology, not an FPS claim.
Cutout adds fragment cost. Coverage is approximated, not an exact regrouping of
five extracted roots. Transition runs from LOD-end fraction to density-reference
distance. Disabling tufts retains the individual-blade policy.

Cold readiness still costs time: measured large-room admission was 7.35 seconds
before revealing terrain, with all four placements ready (3,897,490 anchors).
Cache is RAM-only. Eviction or generation edits may wait again. Camera cuts do
not regenerate placements; no scene-ID preload or timing exception is used.

## Outline

Follow-up: `TRIAEVUM_NATIVE_OUTLINE_GUIDE.md` replaces the alpha-marker approach
below with a dedicated native-only geometry guide. This section records the
historical limitation, not the current implementation.

`pica_composition_schedule.cpp` publishes SceneResolved/BeforeUi at the last
consumed draw of a known UI-free scene target even when opaque/transparent runs
interleave. Ambiguous mid-scene anchors remain withheld. Mixed UI/world targets
do not get this completion shortcut. Native draws are not reordered.

`toon_outline_shader.cpp` passes raw normal-guide alpha into neighbor exclusion,
not coverage reconstructed from transparent depth. Transparency can no longer
erase that exclusion marker. This fixes the contract, not every silhouette:
paired gameplay captures still show some unwanted dark edges adjacent to Grass.
Do not call complete Grass/outline visual separation solved. Avoid tuning a
scene-specific depth threshold; any follow-up must distinguish canonical scene
guides from Grass occlusion while preserving foreground-object outlines.

## Product Camera and Controls

Root cause: product mode excluded TopScreen camera observation points and its
callback together with experimental gameplay overrides. The F1 toggle could be
enabled while zero updates reached the mod.

- `oot3d_native_a32_window.cpp` enables only existing TopScreen camera entries,
  not the experimental gameplay dispatcher. Native ownership resumes with the
  already-observed entry skipped once, avoiding a repeated-entry trap.
- `oot3d_native_a32_process.cpp` invokes this optional callback in ordinary
  dispatch and synchronous guest calls.
- `ui_topscreen/oot3d_top_screen_camera.cpp` admits free camera only in normal
  gameplay without a title/demo cutscene index. Main-camera, player-state,
  status and native-setting blockers remain active. Intro, cutscenes and
  noninteractive states keep native ownership; ordinary walking is not blocked.

F1 / Controls now groups Devices, Bindings, Aiming, Camera, Actions and Motion.
`oot3d_top_screen_control_widgets.h` contains the title widgets. Camera includes
enable/speed/inversion/zoom/FOV/input source; Actions includes child/adult D-pad,
SELECT and Items exit. Bindings report shared assignments without banning them.
TopScreen now contains HUD/layout only, without duplicate camera controls.
Input config still owns routing, TopScreen config still owns mod behavior.
Fresh snapshots preserve concurrent HUD edits. Save controls also persists
modified camera/actions through the existing TopScreen config owner.

## Verification

Developer exe: `I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe`, used
with the installed launch profile. Do not copy an unpaired development binary
over a catalogued release. User saves/configs were not overwritten.

- 310 renderer tests: completion/eviction/cancellation, complete first admission,
  LOD/coverage, matching BVH/individual selection, disabled old policy, strict
  UI isolation and native shader contracts.
- 1,467 actual-widget F1 assertions, including preview, external HUD changes,
  all sections, catalog and compact footers. Includes repeated frame invariants.
- TopScreen mod-profile and native-input executables pass, including native
  ownership blockers and relative-mouse accumulation.
- `controls-outline-20260907-final`: hudtest, 300 presentations, 122 camera
  updates / 69 active; before/after framebuffer views show rotation.
- `freecam-intro-blocked-20260907-final`: boot, camera enabled, held C-stick,
  1,100 presentations, 576 updates / zero active. Physical mouse sensitivity
  and every noninteractive transition have not been manually tested.
- `intro-tuft-outline-20260907-v1`: 180 seconds, 8,714 presentations. All 4,934
  mixed-order source frames execute Grass and outline. Four initial builds,
  zero subsequent builds through full intro, indoor transition and return.
  No geometry failures, rejected schedules, missing inputs or composition
  mismatches. The sole 18 source-without-Grass frames are culled/faded; their
  capture is black, not visible bare terrain. This broad run precedes the
  raw-alpha follow-up; final product captures include it.

Broad-run Grass GPU mean/p95: 1.18/1.70 ms. Periodic steady CPU mean/p95:
6.91/12.93 ms. Diagnostics, readbacks and build activity were present: not an
uncapped FPS benchmark, nor comparable with different old profiles.

Reproduce with `benchmark_gameplay.mjs` using private `topscreen=...`, `input=...`,
`state=...` or `boot=true`, bounded timeouts and framebuffer capture.
`verify_gameplay_camera.mjs gameplay/runtime.json intro/runtime.json` asserts
real progress/input, gameplay activation and no intro takeover.
Use `analyze_grass_visibility.mjs` plus framebuffer inspection for scene coverage.
