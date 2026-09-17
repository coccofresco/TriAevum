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

This is not full feature parity with Dusklight. Multiple per-device mapping/calibration
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

## Azahar Motion And Touch Extension

The shared input path now also adapts Azahar's SDL sensor/touchpad boundary,
not its event thread. Reference: [sdl_impl.cpp at c2237de](https://github.com/azahar-emu/azahar/blob/c2237de04d8c08cb5ad0ba3fb98e5a9640203257/src/input_common/sdl/sdl_impl.cpp).

- SDL acceleration in m/s^2 becomes native gravity units `(x,-y,z)/9.80665`;
  angular velocity in rad/s becomes native degrees/s `(-x,y,-z)*180/pi`.
  Nonfinite values and unavailable sensors are not valid samples.
- Enable HIDAPI and extended PS4/PS5 Bluetooth reports before opening devices.
  Defaults have SDL default priority: environment/application overrides win.
  Switch and Joy-Con drivers inherit the global HIDAPI preference. SDL owns
  their model/transport orientation; no title-specific brand axis fixes.
- SDL's standard motion channels for a combined Joy-Con pair use the right
  controller; do not sum both controllers. See [SDL Switch driver](https://github.com/libsdl-org/SDL/blob/SDL2/src/joystick/hidapi/SDL_hidapi_switch.c).
- The chosen pad alone provides buttons, sticks, motion and optional touchpad.
  Touchpad coordinates map directly to native 320x240 touch space, independent
  of the window aspect ratio. Native UI eligibility and host menu capture still
  apply. A pressed mouse touch or explicit shortcut takes precedence. Release
  and disconnect clear touch; a touchpad click is not required for contact.
- F1 Devices owns touchpad enable/index, capability display and calibration.
  No new TopScreen or renderer input settings are introduced.
- Motion sensors are exclusive to native aiming. The shared camera/C-stick
  routing accepts mouse, right stick, digital look, automatic or disabled;
  automatic never falls through to a sensor. Native horizontal aim inversion
  covers both yaw components, matching the virtual-mouse sensor convention.
  Native sensor samples remain full three-axis observations for game processing.
- Manual calibration rejects strong movement/free fall and counts fresh sensor
  timestamps, not repeated render polls. Older/timestamp-less drivers retain
  poll-based sampling. Sixty accepted samples complete the calibration;
  holding still is required, and slow intentional rotation can look like bias.
- Calibration aborts on selected-device change/disconnect. New results are
  scoped to GUID plus serial when available; another controller receives neutral
  calibration without erasing the saved result. One calibration is stored, not
  an unlimited device database. Legacy unbound calibration remains compatible.
  Save failures remain visible in F1 instead of being discarded.

### Compatibility Boundaries

DualShock 4, DualSense, Switch Pro, Joy-Con and compatible pads use capabilities
reported by SDL, not a product-name whitelist. A compatible pad exposing only
XInput cannot supply motion merely because its case contains an IMU. Steam
Input/remapping software can expose a virtual pad without physical sensors;
select an exposed sensor-capable device or configure the upstream remapper.
Serial-less calibration/preferences identify a model rather than an individual
unit. Changing USB/Bluetooth GUID may require selecting/calibrating again.

[Extended PlayStation reports](https://wiki.libsdl.org/SDL2/SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE)
can affect compatibility with non-SDL DirectInput software until power cycling
the controller. Users can opt out through the SDL environment hints. Missing
sensor support does not disable ordinary buttons/sticks.

Existing Circle Pad, C-stick, ZL/ZR, keyboard/mouse virtual aiming and Android
overlay paths are retained. Microphone, cameras, lid events and NFC are separate
3DS services and are **not** implemented by this controller extension. Android
device IMU integration and physical hardware qualification on other platforms
remain separate tasks; desktop SDL tests cannot establish those guarantees.

### Extension Verification (Windows, 2026-09-17)

- Shared input tests pass: native axes/units, yaw versus roll, touch bounds,
  nonfinite samples, duplicate timestamps, stationary restart and partial sensors.
- SDL virtual-device tests pass: report setup/explicit override precedence,
  unavailable sensors/touch, button/axis routing and disconnect neutralization.
- Native input tests pass: config compatibility, calibration persistence/device
  isolation, disconnect cancellation, partial calibration and reset while active.
- Consolidated F1 real-widget smoke passes 3,950 assertions. Release audit unit
  suite passes 11 tests. Game builds and the source-module consumer compiles.
- Vulkan game probe from the existing Sages state completes 60 frames and exits
  0, using real host polling with no injected timeline. Private logs:
  `C:/Users/xander/triaevum-issues-40-43/motion-touch-alignment/`.
  This is an integration smoke, not physical motion/touchpad qualification.
- No PS4/PS5/Switch USB/Bluetooth hardware matrix, Linux, Android or macOS
  execution was completed for this extension. Test those before claiming parity.

## Camera/Motion Separation (2026-09-17)

Supersedes the motion-camera feature in `46c8132`: gyro/accelerometer never
control free camera. `NormalizeCameraSource` is enforced by the shared routing,
configuration loading, runtime preview/initialization and serialization. Legacy
camera sources `controller_gyroscope`, `controller_accelerometer` and
`controller_motion` migrate to `right_stick`, without changing aim configuration.
Legacy `free_camera.motion_sensitivity` is accepted but ignored and no longer
written. Its C++ setting and F1 slider are removed, not merely hidden.

F1 offers distinct source lists for aim and camera. Existing native aim settings,
TopScreen gameplay/cutscene eligibility and UI capture keep their existing owners.
This change does not introduce a second camera or change the title's aim logic.

Verification: shared/native input tests pass for automatic and all three legacy
motion sources (camera neutral with moving sensors; stick still works; native
aim sensors remain valid), config migration and serialization. Actual F1 widget
smoke passes 3,978 assertions. Windows executable rebuilt and module-host input
consumer compiled. No new physical-controller or other-platform run is claimed.
