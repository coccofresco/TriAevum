# Integrated Application Menus

## Product Boundary

F1 is a retained RmlUi frontend, not an ImGui window. F12 retains the existing
advanced ImGui panels (Grass, Toon/outline, CACAO, reflections, textures and
renderer presets). F2 remains the native-presentation override. Both settings
surfaces start closed, are mutually exclusive and block gameplay input until
the closing gesture is released. Native OOT3D/TopScreen HUD is not replaced.

The previous ImGui sidebar in commit `9892379` was not the requested frontend.
Its input ownership and setting consumers are reused; its standard window is
no longer registered. `DrawStandard()` remains only for the old widget fixture.

## Modules

- `fast/appui/SettingsModel.h`: toolkit-neutral pages and typed field callbacks,
  validation, availability and explicit persistence errors.
- `fast/appui/SettingsFrontend.h` and `src/fast/appui/SettingsFrontend.cpp`:
  RmlUi document, layout, focus, hit testing, navigation and confirmation modal.
- `cmake/ApplicationUi.cmake`: independently built static frontend and pinned
  RmlUi 6.1 dependency, SHA256
  `4e3561190cf7e6867388d5c89e15604f4f7bd83e112316ac0832a6a2d061eace`.
- `BuildApplicationSettingsPages()` in `graphics_settings_window.cpp`: graphics
  adapter to the existing GraphicsSettingsRuntime and presentation transaction.
- Registered Controls, TopScreen and Game-language adapters append their pages
  without putting title semantics into the reusable frontend.
- `Fast3dGui`: sole host event/capture owner and exclusive F1/F12 routing.

The frontend shares the already-owned font atlas and final overlay mesh
transport with the diagnostic UI. It emits RmlUi triangles into that transport;
it does not use ImGui windows/widgets for F1. The final existing Vulkan overlay
pass remains after scene effects and native composition. No PICA states,
shaders, native geometry or scene-effect scheduling are modified.

Supported RmlUi rendering subset: indexed geometry, premultiplied vertex color
converted to the existing straight-alpha overlay contract, atlas text and
rectangular scissoring. Documents deliberately do not require CSS filters,
stencil clipping, external images or arbitrary texture uploads. Adding those
requires extending the declared overlay contract, not mutating a scene pass.

## Settings and Input

Pages cover Display, Antialiasing, Devices/presets, Analog sticks, Aiming,
Camera, TopScreen shortcuts, Motion calibration, four binding pages, TopScreen
HUD and Game language. Existing setting services remain authoritative. Numeric
input commits on Enter/blur or explicit increment/decrement, not every typed
digit. Unsupported graphics choices retain their validation reasons; Off
remains reachable. Display changes use the existing timed Keep/Revert transaction.

Binding pages support direct selection and Listen for each native action and
source slot. Capture waits for neutral input, suppresses menu navigation,
times out and cancels on menu closure. Controller Back is not stolen from an
existing game binding. Keyboard Tab/Shift-Tab, arrows, Enter and mouse input
drive RmlUi; controller D-pad navigates, A activates and B returns. Physical
devices still belong to the existing shared input service, not Aurora.

## Donor and Licensing

Dusklight/TwilitRealm revision `158abde3816042eeb52f9bda5a7fac50487d4451`
provides the retained-document and navigation model. Relevant references:
`src/dusk/ui/settings.cpp`, `window.cpp`, `nav_group.cpp`, `input.cpp`,
`controller_config.cpp`. This is an adaptation, not an exact asset/style clone.
No Twilight Princess game content, Aurora renderer or RmlUi donor document
payload is copied. RmlUi itself is MIT; retain `LICENSES/RmlUi-MIT.txt`.

## Reproducible Validation

```sh
cmake --build BUILD --target triaevum_rml_settings_smoke triaevum_f1_settings_smoke --parallel 3
BUILD/tools/triaevum_release/tests/triaevum_rml_settings_smoke
BUILD/tools/triaevum_release/tests/triaevum_f1_settings_smoke
```

Use `.exe` on Windows. The new test operates real RmlUi documents and hit
testing, verifies generated mesh data, resize, reversible setting changes,
navigation, Resume and advanced-surface routing. The old fixture still checks
the advanced widgets and existing settings consumers.

For deterministic in-game framebuffer validation only, set
`TRIAEVUM_DIAGNOSTIC_MENU=standard` for the test process. This explicitly opens
F1 once at the first UI frame. Combine with the existing bounded runtime and
`--screenshot` arguments. Never set this variable in user launch profiles.
Normal runs remain closed by default. Captures are not performance samples.

Platform compilation and physical controller/touch qualification must be
reported independently; a passing document test is not a Linux/Android/device
certification. No AOT optimization or decompilation is part of this change.

## Windows Integration Evidence (2026-09-16)

- Real-document smoke passes, including mouse hit testing at 800x600,
  1280x720 and 1920x1080, controller activation, D-pad/stick selection,
  modal rollback, Resume and advanced-surface routing.
- The owning-settings fixture covers all 14 retained pages as well as the
  existing ImGui widgets. Assertion counts include repeated frame invariants.
- Bounded native Vulkan boot passed (exit 0) with F1 open. Captures at frames
  30 and 150 show the integrated document above the native scene.
- Initial live attempts failed: one save load ran out of memory and another
  crashed in Vulkan presentation with OBS capture in the stack. The passing
  run disabled OBS via `DISABLE_VULKAN_OBS_CAPTURE=1` for that process only.
  This is not an application workaround or proof that OBS alone caused it.
- Private captures: `%TEMP%/TriAevum-rml-menu-live`. Current executable:
  `I:/TriAevum-public/ui-dependencies/bin/TriAevum.exe`.
- Build tree: `J:/TriAevum-verify-20260910/runtime`, target
  `oot3d_native_game`. Archives/executables and Rml build output are redirected
  to `I:/TriAevum-public/ui-dependencies` because J is full. This does not
  replace the executable used by parallel AOT work.
- RmlUi attribution is required by the public-package audit and included in
  the example release layout; no new release was packaged here.

Still requiring physical/platform qualification: controller hotplug, touch,
Linux/Steam Deck and Android, and changes of fullscreen mode on real displays.
The document test is not a claim of those validations.

Storage maintenance removed about 3.2 GiB of obsolete release-test binaries,
shader caches and generated installation data across C, I and J. Source,
save files, the active build and parallel AOT work were excluded. Private
per-file manifests are under `I:/TriAevum-public/ui-dependencies/cleanup-*-manifest.json`.
The latest owning-settings fixture passed 11,585 assertions; this is not a
count of independently tested features. Failed persistence tests on a full C
drive passed when rerun in an available temporary directory on I.
