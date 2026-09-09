# F1 settings rationalization

Date: 2026-09-06. Worktree: `triaevum-release`.

2026-09-07: [F2 native presentation override](TRIAEVUM_F2_NATIVE_PRESENTATION.md)
adds a session-only effect comparison and persistent header status. F1 now starts
closed even when the previous session saved it open.

## Scope and ownership

This change repairs the existing F1 control surfaces, not the rendering algorithms.
No title code, save format, native HUD composition, shader math, or effect-graph
scheduling has been replaced.

| Surface | Sections | Owner |
| --- | --- | --- |
| Renderer | Display, Antialiasing, Lighting, Reflections, Toon | GraphicsSettingsRuntime |
| Grass | Sources, Generation, Appearance, Performance, Wind, Interaction | GraphicsSettingsRuntime / grass module |
| Textures | Load, dump, directories, reload, diagnostics | GraphicsSettingsRuntime / texture-pack module |
| Controls | Devices, Bindings, Aiming, Motion | NativeControlConfigRuntime |
| TopScreen 2.1.1 | HUD, Camera, D-pad | TopScreenUiConfigRuntime |

Input still owns host-device routing and sensitivity. TopScreen owns game-camera
behavior, layout and D-pad actions. Its C-stick smoothing really is consumed by
the aiming policy despite the historical member name `FreeCameraSmoothing`;
it was therefore **not** moved to an unrelated free-camera control.

## Corrected defects

1. Selecting Authentic/Enhanced/Toon no longer immediately relabels it Custom.
   Selecting Custom preserves the current values instead of resetting them.
2. Unsupported AA, upscaler, shadow and reflection choices are disabled individually,
   with validation reasons on hover. Off remains selectable after capability loss.
   The menu probes the existing validator, rather than duplicating its policy.
3. Shadow mode no longer evaluates a changed value to decide whether to pop a
   disabled scope. The old code could underflow ImGui's disabled stack.
4. The numeric output-resolution input no longer uses the unsupported
   `EnterReturnsTrue` scalar flag. It commits on edit completion and no longer
   leaves a stale draft after focus without modification. Added 4:3 presets.
5. Keep/Revert display confirmation is above the tab bar, visible from every tab.
   Window bounds follow the viewport; long content scrolls below section navigation.
6. Graphics changes remain live while dragging, but disk writes are coalesced
   until release. Closing F1 also flushes pending changes. Failed saves are
   explicit and retryable; display changes remain unpersisted until accepted.
7. Texture settings no longer bypass validation by calling Configure from the UI.
   The existing backend revision consumer applies validated settings. Explicit
   texture-index reload remains a module command.
8. Toggling custom toon bands preserves the user's values. Changing band count
   resamples the profile instead of discarding it. Reset remains an explicit action.
9. Removed the ineffective DLSS/XeSS sharpness control. SSSR shows its effective
   thickness range; Hi-Z-only distance/edge parameters are identified as fallback
   parameters when SSSR is selected. Material assignments stay with Reflections.
10. Controls and TopScreen have fixed save/reload footers; save errors are not
    hidden by the unsaved indicator. Successful previews adopt validated runtime
    state. New external revisions cannot be overwritten by stale panel copies.
11. A custom input profile is displayed as Custom; a disconnected explicitly
    selected controller is not misreported as Automatic.

Graphics auto-save remains distinct from the explicit Controls/TopScreen save
commands. No configuration files belonging to the user are reset or migrated.

## Code map

Current control ownership and first-frame/LOD update (2026-09-07): see
[First-Frame Grass and Gameplay Camera](TRIAEVUM_GRASS_FIRST_FRAME_AND_GAMEPLAY_CAMERA.md).
Camera, aiming and D-pad behavior now live in Controls; TopScreen contains HUD
and layout only. JSON ownership remains unchanged.

Renderer files under `runtime/three_ds_recomp/`:

- `src/fast/oot3d/graphics_settings_window.cpp`: navigation and application.
- `src/fast/oot3d/graphics_settings_{display,antialiasing,lighting,reflection,toon}_panel.cpp`:
  separate section implementations.
- `include/fast/oot3d/settings_panel_widgets.h`: validated choice widget and
  complete apply-result formatting.
- `src/fast/oot3d/graphics_settings_runtime.cpp`: persistence state and coalescing.
- `src/fast/oot3d/graphics_settings.cpp`: Custom semantics and band resampling.
- `src/fast/Fast3dGui.cpp`: viewport constraints and save flush on close.
- `src/fast/oot3d/{grass_settings_panel,azahar_texture_pack_panel}.cpp`.

Application files under `tools/oot3d/native_game_runtime/`:

- `oot3d_native_controls_settings_panel.cpp`
- `oot3d_top_screen_settings_panel.cpp`

## Repeatable verification

First build the regular runtime target, without rebuilding the title:

```powershell
cmake --build I:/oot3dre_work/triaevum-direct-module-build --target triaevum_public_runtime --parallel 3
tools/triaevum_release/tests/run_f1_settings_smoke.ps1 -Build I:/oot3dre_work/triaevum-direct-module-build -Dependencies I:/oot3dre_work/whole-aot-product-consumer/_deps -Compiler I:/oot3dre_tools/llvm-22.1.6/bin/clang++.exe
```

The standalone test links the **actual built panels and runtime libraries**.
It uses a private ImGui build with assertions and item hooks, synthetic ImGui
mouse events, and memory-only configuration. It requires neither a ROM nor a GPU.
Its cached ImGui objects avoid recompiling the test framework on every iteration.

Coverage: preset selection; capability loss and Off; CACAO quality; reflection,
MSAA and upscaler selectors; toon-profile preservation; every top-level and nested
tab; application preview and external-revision reconciliation; display confirmation
from Grass; compact panel footers; deferred persistence and save failure/retry.
The test also checks balanced UI scopes and finite geometry on every UI frame.

Grass distance update (2026-09-07): Performance now separates Draw distance
(up to 50,000) from Density falloff distance. The latter migrates from the old
visibility value and remains fixed when visibility is changed. The old Room
blade budget label is now Visible blade budget; its persisted key is unchanged.
The static cache has a separate internal memory/device limit. Both distance
widgets are exercised by the real F1 smoke. See
`TRIAEVUM_GRASS_INTRO_STABILITY.md` for the cache/LOD contracts and measurements.
Assertion counts include those per-frame invariants, not independent gameplay tests.

Soft Grass LOD follow-up (2026-09-07): the same Performance section now includes
final-distance fade, density fade, a configurable individual-blade/tuft transition,
separate distant tuft quantity and spread, and stable segment-transition spread.
Old profiles retain their distances. All six added controls are covered by the
actual-widget smoke (1,618 assertions). See
`TRIAEVUM_GRASS_SOFT_DISTANCE_LOD.md` for units, defaults, rendering contracts and
the CPU/GPU and framebuffer verification results.

The bounded Vulkan run uses `tools.triaevum_release.validate_cacao_run`, a private
configuration/save directory, and the renderer's framebuffer capture:
`I:/oot3dre_work/f1-menu-diagnostics/vulkan-menu/`.
It completed normally with 213 recorded frames, 179 CACAO passes and zero
Vulkan/NRI validation errors. The captured F1 panel is visible and legible.
This is a smoke test with validation enabled, **not an FPS benchmark**.

## Texture dropdown follow-up

The initial empty-catalog smoke test missed a live-game interaction defect in
`texture_catalog_viewer.cpp`: each selectable's ImGui ID incorporated its label,
including the constantly changing observation count, and its sorted row index.
The pressed widget could cease to exist before mouse release. Live sorting could
also move a different texture under the pointer.

The added populated-catalog test **failed on the previous implementation** with
`texture selection lost while observations changed`. The correction gives rows
an ID based on the complete texture identity (hash, dimensions, native format),
independent of labels or rank. An open popup retains its ordered metadata snapshot
until closed; reopening refreshes it. Neither game data nor GPU texture contents
are frozen. The old 96-entry truncation is replaced by a scrollable clipped list.

Regression coverage now includes selection while counts change, rank changes
between mouse move and release, actual Grass/reflection assignments, and wheel
navigation to an entry beyond the first 96. The full actual-widget smoke passes
with 1,032 assertions, including per-frame scope checks.

## Limits

### Portable Widget Test (2026-09-09)

The existing real-widget fixture is also available through CMake:

```sh
cmake --build BUILD --target triaevum_f1_settings_smoke --parallel 3
BUILD/tools/triaevum_release/tests/triaevum_f1_settings_smoke
```

Use `.exe`/the configuration subdirectory on Windows where applicable. The
target is excluded from normal builds and uses a private test-engine ImGui.
SDK Clang 19.1.7 on Linux passes 1,761 assertions, including the complete,
reversible shoulders/triggers exchange. It links actual panels/configuration,
not a mock UI. Hardware button detection still needs device validation.

### Remaining Device Coverage

Hardware motion calibration, subjective sensitivity and all GPU/provider
combinations still require their respective devices. A selectable setting is
validated and wired to its existing consumer; this does not assert that every
effect is visually perfect in every game scene. No new named-profile manager,
gameplay feature, or renderer effect is introduced in this change.

The developer executable remains
`I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe`.
The existing local launcher
`I:/oot3dre_work/cacao-release-diagnostics/Avvia-TriAevum-corretto.cmd`
uses it with the user's installed launch profile. The catalogued release
installation is not overwritten with an unpaired executable.
