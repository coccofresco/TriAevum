# Dusklight Control Alignment

2026-09-17. Scope: shared host input, not renderer or GameCube gameplay.
The consolidated F1 UI remains authoritative; the deferred F1/F12 frontend
branch is not restored by this work.

## Source Findings

Pinned Dusklight: `97d46baec1c9b1af11b9c9d52314ef1d234911be`.
Pinned Aurora submodule: `7f2801cd0133c9333eadb4e2e6b24100c328d328`.

- [Aurora lib/input.cpp](https://github.com/encounter/aurora/blob/7f2801cd0133c9333eadb4e2e6b24100c328d328/lib/input.cpp):
  SDL device ownership, controller identities and persistent port preferences.
  GUID alone identifies a model; normalized serial distinguishes physical pads.
  Aurora also implements transport fallback using serial and VID/PID.
- [Aurora PAD](https://github.com/encounter/aurora/blob/7f2801cd0133c9333eadb4e2e6b24100c328d328/lib/dolphin/pad/pad.cpp):
  separates host bindings, dead zones, sensor capability and console PAD output.
- [Dusklight controller_config.cpp](https://github.com/TwilitRealm/dusklight/blob/97d46baec1c9b1af11b9c9d52314ef1d234911be/src/dusk/ui/controller_config.cpp):
  grouped devices/buttons/triggers/sticks/rumble/actions; neutral-before-listen
  rebinding; suppression until release after controller assignment; native
  device button labels and persistence of mappings.
- [Dusklight ui/input.cpp](https://github.com/TwilitRealm/dusklight/blob/97d46baec1c9b1af11b9c9d52314ef1d234911be/src/dusk/ui/input.cpp):
  separate UI input blocking, navigation repeats and menu-chord consumption.

These are architectural references, not a reason to import Aurora's renderer,
RmlUi, GameCube PAD format or another SDL event loop. Dusklight's root license
is CC0-1.0; Aurora is MIT. The serial normalization adaptation retains attribution
and the full MIT notice. No game code is imported here.

## Implemented Boundary

`SDL owner -> shared host selection/sampling -> existing 3DS mapping -> title`

- `ConnectedPhysicalDeviceManager` remains the only SDL controller owner.
- `three_ds_input` owns deterministic model/serial matching without SDL/UI.
- `three_ds_sdl_controller.h` borrows existing handles, describes connected
  devices, selects one, reads normalized buttons/triggers/axes, converts sensor
  units/orientation once and returns neutral state for detached devices.
- Both `PollNativeA32Input` and `TriAevumOot3dInputBackend` use that same path;
  duplicated selector, button switch and sensor conversion blocks were removed.
- Automatic selection retains an attached active pad. On disconnect it chooses
  a deterministic remaining candidate. Explicit selection does not silently
  route a different device when the requested identity is absent.
- Config v1 adds optional `controller_serial`; older GUID-only files still
  load. Session IDs are never persisted. Presets preserve device preference.
- F1 selection and runtime use the same matching policy. The list distinguishes
  instances, shows the actual active device and warns when no serial exists.
- Right-stick filter state is cleared when the selected device changes.
  Virtual mouse aim orientation is not reset by unrelated controller hotplug.
- Existing bindings, dead zones, mouse aim, TopScreen eligibility, press-to-bind,
  persistence/error handling and Start delivery semantics are retained.

## Deliberate Limits

This is not full feature parity with Dusklight. Per-device mapping/calibration
profiles, transport fallback, native button glyphs, rumble UI and post-bind
navigation-release suppression still need their own integration/verification.
Without a serial, a persisted preference identifies a model, not one physical
unit of that model; the runtime retains its selected instance while attached.
Do not claim stronger identity from a transient instance ID.

Steam Input's virtual controller is consumed as one SDL device; readings are
never added across pads. This does not automatically detect physical/virtual
duplicates or guarantee Steam gyro access. Explicit selection remains available.
The shared code has no OS-specific title mappings; actual Linux/Steam Deck,
Android and macOS hardware parity is not established by Windows tests.

## Verification

Targets: `three_ds_recomp_input_tests`, `three_ds_sdl_controller_tests`,
`oot3d_native_a32_input_tests`, `triaevum_f1_settings_smoke`, `oot3d_native_game`.
The SDL test uses a real virtual controller, not mocked SDL return values.
It covers selection, buttons, triggers, axis orientation/extremes, menu-owned
axis suppression, unavailable sensors and neutral output after disconnect.
Pure tests cover duplicate models, serial normalization, reordered enumeration,
explicit missing preferences, automatic retention and new IDs on reconnect.
The F1 fixture selects one of two same-model pads and checks preset preservation.
Completed Windows verification:

- Shared input, SDL virtual-controller and native OOT3D input suites pass,
  including legacy GUID-only config loading and serial round-trip.
- Actual consolidated F1 widget smoke: 3,924 assertions passed.
- Release audit unit suite: 11 tests passed; Aurora MIT notice required in
  packages and included in the reference packaging layout.
- Game executable rebuilt; the separate module-host input consumer compiles
  against the same shared adapter. No AOT compilation was necessary.
- Bounded Vulkan run from the user's Sages checkpoint: 60 frames, exit 0,
  without an injected input timeline (real host polling).
  Private evidence: `C:/Users/xander/triaevum-issues-40-43/controller-alignment/`.
  This checks startup/polling integration, not a physical-controller playthrough.

Hardware behavior must not be inferred from the existence of a test target.
