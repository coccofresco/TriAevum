# Controller Donor Assessment

Research date: 2026-09-14. Source review, not integration or hardware qualification.
Goal: simpler, reliable Windows/Linux/Steam Deck/Android controls without another
competing input stack. Keep host device normalization separate from 3DS input
semantics, TopScreen behavior, UI capture and the renderer.

## Existing Baseline

- `runtime/three_ds_recomp/src/ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.cpp`:
  SDL lifecycle, mapping database, hotplug and ownership.
- `runtime/three_ds_recomp/tests/controller/controller_tests.cpp`:
  virtual controller lifecycle, buttons and axes regression tests.
- `tools/oot3d/native_game_runtime/oot3d_native_control_config.cpp`:
  presets, persistence, bindings and automatic mouse/controller defaults.
- `tools/oot3d/native_game_runtime/oot3d_native_a32_input_tests.cpp`:
  existing tests for controller aim and fresh-install input routing.
- `runtime/triaevum_module/include/triaevum/input_service.h`:
  shared service boundary to preserve rather than replace with donor globals.

The AppImage candidate ships SDL2-compat and SDL3. This does NOT mean our source
already uses the SDL3 gamepad API. Migrating API/event ownership is separate from
shipping the compatibility libraries; do not casually mix two SDL device owners.

## Ranked Candidates

1. **SDL and its official testcontroller utility.** Best foundational donor.
   Reuse device capabilities, standardized mappings, sensor events, virtual
   gamepads and mapping/diagnostic workflows. The utility is reference code,
   not a drop-in F1 widget; adapt its logic, not its separate window/event loop.
   SDL uses zlib licensing; inspect each copied test/helper's header too.
   [Gamepad API](https://wiki.libsdl.org/SDL3/CategoryGamepad),
   [testcontroller.c](https://github.com/libsdl-org/SDL/blob/main/test/testcontroller.c).

2. **SDL_GameControllerDB (mdqinc).** Maintain the existing dependency rather
   than add another controller database. Pin upstream revisions, retain local
   overrides and validate mapping changes. It covers hardware-to-standard-pad
   mapping, not gameplay bindings, calibration or menu behavior. Zlib license.
   [Project](https://github.com/mdqinc/SDL_GameControllerDB).

3. **GamepadMotionHelpers, Julian "Jibb" Smart.** Best narrowly scoped new
   dependency: header-only sensor fusion, gyro bias calibration and coordinate
   conversion. One state per device, driven by sample timestamps; retain manual
   calibration and avoid silently treating deliberate slow aim as drift.
   Its units are degrees/second and g, with Y-up coordinates. Convert explicitly
   at the existing host/3DS boundary; never apply this filter to mouse deltas or
   already synthesized Steam Input mouse motion. MIT license.
   [Project and behavior](https://github.com/JibbSmart/GamepadMotionHelpers),
   [license](https://github.com/JibbSmart/GamepadMotionHelpers/blob/main/LICENSE).

4. **Zelda64Recomp.** Closest product-level reference: input scanning, cancel
   behavior, UI/gameplay capture boundaries and SDL plus GamepadMotionHelpers
   integration. Keep its N64 semantics, global state and UI framework out of our
   shared 3DS module. Main project GPL-3.0: check exact file licenses and our
   combined distribution terms before copying, not merely before publication.
   [Input implementation](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/dev/src/game/input.cpp),
   [bindings](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/dev/src/game/controls.cpp).

5. **PPSSPP.** Strong reference for cross-device configuration, press-to-bind,
   automatic mapping and separating frontend devices from emulated controls.
   Useful especially for the future desktop/Android common configuration model.
   Do not bring in its UI toolkit or PSP control model. Project GPL-2.0-or-later;
   verify headers/dependencies of any selected implementation.
   [SDL backend](https://github.com/hrydgard/ppsspp/blob/master/SDL/SDLJoystick.cpp),
   [mapping UI](https://github.com/hrydgard/ppsspp/blob/master/UI/ControlMappingScreen.cpp).

6. **Azahar.** Existing donor, useful specifically for the 3DS-side contract,
   motion emulation and Android touch integration. Not a reason to duplicate
   its full input framework or inherit Qt configuration machinery. Preserve
   per-file donor notices and compare against functionality already imported.
   [input_common](https://github.com/azahar-emu/azahar/tree/master/src/input_common),
   [motion emulation](https://github.com/azahar-emu/azahar/blob/master/src/input_common/motion_emu.cpp).

## Alternatives Not Chosen as the Core

- **JoyShockMapper:** useful MIT reference for gyro/stick response and optional
  gestures. Its modern implementation already uses SDL plus GamepadMotionHelpers;
  do not ship an external keyboard/mouse injector or its full binding language.
  [Project](https://github.com/JibbSmart/JoyShockMapper).
- **JoyShockLibrary:** MIT device library, but adds a competing device backend
  where SDL already covers the main need. Reserve for a demonstrated device gap.
  [Project](https://github.com/JibbSmart/JoyShockLibrary).
- **Current DuckStation input_manager.cpp:** inspected header is
  `CC-BY-NC-ND-4.0`; exclude from direct code donation under the ordinary project
  contribution model. Do not assume an old project's license applies to HEAD.
  [File](https://github.com/stenzek/duckstation/blob/master/src/util/input_manager.cpp).

## Steam Deck Boundary

Steam Input interoperability is a platform contract, not an open-source donor.
Keep Steam environment/mappings intact and use a single effective input route
per device; do not sum physical and virtual copies. A virtual gamepad does not
guarantee raw sensor access. SDL3's Steam handle can bridge to Steam Input API
when available, but obtaining the handle alone does not implement that bridge.
Do not add a mandatory Steamworks SDK dependency for non-Steam AppImage users.
[Valve guidance](https://partner.steamgames.com/doc/features/steam_controller/getting_started_for_devs),
[SDL Steam handle](https://wiki.libsdl.org/SDL3/SDL_GetGamepadSteamHandle).

## Recommended Implementation Sequence

1. Consolidate existing device ownership and diagnostics. Show selected device,
   real capabilities and effective bindings; test hotplug, focus loss and neutral
   state on disconnect. Preserve settings and controller support in fresh installs.
2. Improve the existing F1 input surface using SDL/Zelda64Recomp/PPSSPP patterns:
   one guided press-to-bind flow, cancel/reset, conflict feedback and controller
   navigation. Do not introduce a second profile store or a second input menu.
3. Add GamepadMotionHelpers behind a small device-local motion adapter if its
   calibration/fusion improves the measured behavior. Mouse aim remains separate;
   final conversion feeds the same native 3DS contract. Use timestamped tests.
4. Evaluate direct SDL3 API migration as a separate change only where it removes
   real complexity or exposes needed capabilities. It is not required before
   fixing configuration, reconnect behavior or integrating the motion helper.

Before any import: pin donor commit and file list, retain notices, record local
changes, add boundary tests and run Windows/Linux package checks. This research
does not establish a new runtime regression or promise untested device support.
