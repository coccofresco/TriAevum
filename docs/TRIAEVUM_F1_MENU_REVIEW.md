# F1 settings rationalization

Date: 2026-09-06. Worktree: `triaevum-release`.

2026-09-10: [Game language selection](TRIAEVUM_GAME_LANGUAGE.md) adds the
application-owned Game tab, shared with Forge and limited to detected ROM
languages. Changes apply on a full restart, not on save-state load.

2026-09-10: [Display resolution and confirmation](TRIAEVUM_DISPLAY_RESOLUTION_FIX.md)
supersedes the in-panel display confirmation below. The modal now works with
F1 closed; output and scene resolution report actual renderer extents.

2026-09-07: [F2 native presentation override](TRIAEVUM_F2_NATIVE_PRESENTATION.md)
adds a session-only effect comparison and persistent header status. F1 now starts
closed even when the previous session saved it open.

## Controls redesign (2026-09-10)

The previous five-column binding table combined keyboard, alternate key, mouse
and controller in a content-proportional layout. Full-width combo boxes fed their
current widths back into auto-fit sizing. The replacement uses explicit stretch
weights, no persisted table widths and no nested table scrolling.

- Four sections: **Bindings**, **Camera**, **Devices**, **Shortcuts**. Bindings
  opens first, with separate Keyboard / Mouse / Controller views and collapsible
  Movement, Game buttons, D-pad, Menu shortcuts and Look directions groups.
- Search controls by name; search assignments inside their dropdowns; select
  **Unassigned** to clear one source. Keyboard primary/alternate remain separate.
  Shared assignments are indicated on their row with the other actions in a
  tooltip, not an expanding list above the table. They remain allowed because
  context-dependent mappings can intentionally share a source.
- Preset selection does not mutate live input until **Apply preset** is pressed.
  Controller preference and motion calibration survive a preset change.
- Camera contains free-camera behavior, native gyro aiming and C-stick aiming;
  Devices contains enablement, controller selection, analog settings and motion
  calibration. Shortcuts contains the child/adult D-pad and menu actions.
  Full-width fields place labels above their values and remain usable at the
  minimum F1 window width.
- **Save controls** / **Revert changes** stay below the single scrolling body.
  Revert works before the first save using the initial profile. It parses
  existing required files before applying either, and restores only the
  control-owned TopScreen fields, preserving live HUD layout. Failures remain
  visible, with the complete message available on hover.

The UI still uses `NativeControlConfigRuntime` and `TopScreenUiConfigRuntime`.
No new input poller, gameplay routing, SDL mapping, JSON schema or save format.
Reusable field layout is in `oot3d_control_settings_widgets.h`; title-specific
widgets and the control-owned TopScreen field copy are kept together in
`oot3d_top_screen_control_widgets.h`.

The real-widget smoke covers 120 consecutive frames at each of 520, 760, 1100,
then 520 pixels, binding search/clear/reassignment on all three devices, preset
confirmation/cancel, external revisions, and the compact footer on all sections.
Isolated JSON fixtures exercise save, revert, preserved HUD state and failure
atomicity on reload. This tests UI wiring and geometry, not physical-controller
ergonomics; the existing shared input tests cover routing. Windows incremental
runtime build recompiles the panel and relinks only, with no title/AOT rebuild.

Validation on Windows: **3,569 actual-widget assertions**, shared native input
tests passing, and the rebuilt Vulkan runtime loads the existing complete-save
checkpoint and renders a native framebuffer (bounded run, exit 0). No UI inputs
were injected in that game run; the menu's interaction/layout tests use the real
widgets in the headless fixture. Linux/Android execution was not repeated for
this panel-only change.

## Scope and ownership

This change repairs the existing F1 control surfaces, not the rendering algorithms.
No title code, save format, native HUD composition, shader math, or effect-graph
scheduling has been replaced.

| Surface | Sections | Owner |
| --- | --- | --- |
| Renderer | Display, Antialiasing, Lighting, Reflections, Toon | GraphicsSettingsRuntime |
| Grass | Sources, Generation, Appearance, Performance, Wind, Interaction | GraphicsSettingsRuntime / grass module |
| Textures | Load, dump, directories, reload, diagnostics | GraphicsSettingsRuntime / texture-pack module |
| Controls | Bindings, Camera, Devices, Shortcuts | NativeControlConfigRuntime / TopScreenUiConfigRuntime |
| TopScreen 2.1.1 | HUD and layout | TopScreenUiConfigRuntime |

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

### Direct Input Assignment And Mouse Recapture (2026-09-10)

Each binding selector now offers `Listen...` as well as the searchable list.
The capture waits for the selected device's held inputs to be released, then
assigns the next supported keyboard key, mouse button, controller button or
trigger. Escape, Cancel, closing F1, or a 20-second timeout cancels it. Escape
remains reserved for releasing the mouse; primary and alternate keys are
assigned independently. Existing custom configurations are not reset.

Ownership remains split by responsibility:

- `tools/three_ds/input/three_ds_input.{h,cpp}` owns the portable capture state
  machine, reusing the existing host binding vocabulary, without SDL or ImGui.
- `oot3d_native_control_config` owns the synchronized capture instance and
  presets. Keyboard + Mouse uses mouse aim and mouse free look; Controller
  uses the right stick for both. Physical motion remains separately selectable.
- `oot3d_native_a32_window` supplies the existing raw host poll before gameplay
  filters, including disabled devices during assignment. F1 suppresses gameplay
  keyboard, controller, motion and native-touch input while retaining sensor
  observation for calibration. No second SDL event pump is introduced.
- `oot3d_native_controls_settings_panel` owns the modal and draft assignment;
  the usual Preview, Save and Revert paths persist the result.

The mouse recapture defect was in `Fast3dWindow::MouseButtonDown`: it used
`ImGuiIO::WantCaptureMouse` to reject the click after Escape. The full-window
`Main Game` surface also sets that flag, even with F1 closed. Recapture now
uses the actual host menu/window visibility, consistently with the gameplay
input poll. The resume click is still consumed, and Escape still releases
capture rather than closing the game. Native TopScreen camera eligibility,
including cutscene restrictions, is unchanged.

Verification on Windows:

- Shared input tests, native input tests and TopScreen contract tests pass.
- The actual F1 widget smoke passes 3,756 assertions (including UI-frame
  invariants), exercising direct assignments, opening-gesture release, triggers,
  alternate keys, cancellation and the real ImGui `Main Game` capture flag.
- Two bounded 180-presentation-frame Vulkan runs from the same gameplay state
  complete normally. A supplied C-Stick command produces 60 active camera
  updates versus zero without input. Renderer framebuffer captures confirm
  different camera orientations. This verifies the camera consumer, not physical
  mouse delivery; the latter still needs the user's hardware confirmation.

The runtime report now includes `hid_input.mouse_polling` counters for eligible,
released, host/native UI-owned polls, capture transitions and physical movement.
Together with `free_camera_input` and TopScreen camera counters, these distinguish
capture, delivery and native-camera eligibility without another instrumented build.

Current local build: `J:/TriAevum-verify-20260910/runtime/TriAevum.exe`; evidence:
`J:/TriAevum-verify-20260910/camera-consumer-20260910-162418/` (command) and
`camera-consumer-20260910-162456/` (control). Only host runtime/UI libraries were
rebuilt; the title AOT module, game data and savestate format are unchanged.

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

## Grass Nearby Rim (2026-09-11)

Grass > Appearance groups the enable checkbox and start/end distance controls
under Nearby toon rim. Distances use meters in the UI and world units in storage;
disabled rim disables its distance widgets. The real-widget smoke edits start
to 3 m and end to 12 m, checks 300/1200 world units, and toggles off/on. Global
toon rim strength/color remain shared rather than duplicated in Grass settings.
