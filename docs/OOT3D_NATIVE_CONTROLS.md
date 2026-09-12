# OoT3D native controls

## Runtime boundary

OoT3D uses the cross-title contract in
`docs/THREE_DS_RECOMP_INPUT_ARCHITECTURE.md`. Its desktop path is split into
four layers:

1. `tools/three_ds/input/three_ds_input.*` owns physical source types, the
   hardware profiles and the canonical standard/Extra HID, touch and motion
   frame.
2. `oot3d_native_control_config.*` owns presets, bindings, calibration and the
   persistent `oot3d_native_controls_v1` JSON schema.
3. `MapNativeControlInput` is the title adapter. Its 22 regular actions map
   one-to-one to cross-title native controls; Gear/Map/Items compile to native
   lower-screen touch positions.
4. `PollNativeA32Input` is the SDL/three_ds_recomp_runtime adapter. It selects a
   controller, polls physical inputs and converts SDL sensor coordinates.

There is no separate TopScreen input channel. ZL/ZR use Extra HID button bits
and freecam consumes the canonical C-Stick sample. Standard HID transport
masks Extra HID bits before publishing the original shared-memory structure.

The F1 `Controls` tab edits this model. Renderer code only hosts a generic
`GraphicsSettingsPanelTab`; it has no knowledge of game actions or SDL.
Edits are previewed immediately. `Apply and save` only adds persistence; it is
not required to make a control active for the current session.

## Configuration

The default path is `oot3d_controls.json` beside `oot3d_native_game.json`.
Use `--controls-config <path>` to select another file. A missing file uses
`NativeControlDefaults`: keyboard/mouse bindings plus an enabled controller,
with automatic source selection. Saving creates the configuration file.

Built-in presets:

- `keyboard`: WASD and the existing native-game keyboard layout; numpad
  directions drive aiming/freecam.
- `keyboard_mouse`: the keyboard layout plus mouse buttons and mouse motion.
- `controller`: left stick for movement, standard SDL buttons/triggers,
  right stick for both native aiming and freecam. Physical motion is opt-in.
- `custom`: assigned automatically after editing a preset.

Every binding has two keyboard slots, one mouse button and one controller
button. The title labels describe intent, but their output remains native:
movement is Circle Pad, look is C-Stick, buttons remain their HID buttons, and
page shortcuts are touch. Analog dead zones, trigger threshold, selected
controller GUID and sensitivity values are stored in the same JSON document.

## Native aiming

Aim and freecam have independent sources. Native aim does not patch camera
gameplay: it writes the original CTR HID shared-memory rings only when the
guest enables the corresponding `hid:USER` sensor.

The producer matches the native layout and cadence:

- accelerometer ring at HID offset `0x108`, 8 entries, 104 Hz;
- gyroscope section at HID offset `0x158`, samples at `0x178`, 32 entries, 101 Hz;
- accelerometer input is expressed in `g` and quantized at 512 units/g;
- gyroscope input is expressed in degrees/second and quantized with the
  native coefficient exposed by `HID:GetGyroscopeCoefficient`;
- raw-entry axis wiring follows the CTR/Azahar HID implementation.

Mouse motion is converted to angular velocity using elapsed host-poll time.
Right-stick and digital sources produce the same physical unit, so the guest
continues to own aiming state, filtering and gameplay behavior.

### Coherent Virtual Motion (2026-09-10)

The old virtual producer treated horizontal movement as roll (neutral Z),
and always supplied a neutral accelerometer, even while simulating pitch.
The native game consumes X/Y pitch/yaw; its sensor fusion corrected the
contradictory gravity back toward neutral. This reproduced the user's forced
returns in a bounded bow-aiming probe, independently of mouse capture.

`ThreeDsRecomp::Input::VirtualMotionState` now retains the virtual device's
pitch. The shared mapper produces body-space angular velocity and gravity
from the same pose. At neutral, yaw uses Y; when pitched, world-up yaw is
projected onto body Y/Z. Mouse down produces positive sensor pitch; mouse
right produces negative sensor yaw, matching the native view consumer's
subtraction of motion angles. Stick/digital synthesis uses the same model.
No idle decay, mouse-origin rectangle, tilt clamp or camera-memory override
is introduced. Native action limits and sensor filtering still apply: this
is not a replacement with an unrestricted FPS camera.

The runtime owns this state alongside polling, not in the renderer or a
global singleton. Interpolated presentation-only polls do not advance it.
When input stops, angular velocity is zero but gravity retains the pose.
Cold checkpoint loading and F8 restore pitch from the HID accelerometer
already present in the savestate and clear pending mouse movement; no save
schema or title AOT changes are required. The separate module-host adapter
also retains the same shared state. Physical controller sensor vectors and
calibration remain on their existing path. Freecam still consumes C-Stick.

Evidence and verification:

- Read-only native evidence: `I:/oot3decomp/src/runtime/shared_semantic/`
  `z_shared_n64_semantic_mass11_agent_wave2r_collision_runtime.c`,
  `PlayerView_UpdateStickAndMotionAngles` at `0x002C036C`; SDK reader
  `CalculateGyroscopeAxisStatus` at `0x002FA5EC`. No new decompilation/import.
- [Azahar virtual motion](https://github.com/azahar-emu/azahar/blob/master/src/input_common/motion_emu.cpp)
  likewise derives gravity and angular velocity from the same device pose.
  Its drag-origin, tilt limit, thread and release-to-neutral behavior are
  deliberately not adopted for relative PC mouse input.
- Shared tests cover neutral axes, both inversion switches, tilted yaw,
  stationary gravity for 600 polls, 30/60/90/120 Hz displacement consistency,
  presentation-only polls, restored/absent gravity, automatic idle retention,
  and unchanged physical sensor data. Native-adapter and F1 tests also pass.
- Paired native Vulkan probes: 30 degrees/second of pitch for 40 native
  frames, then zero rate. At frame 280 the fixed-gravity camera is back at
  **+0.571 degrees** pitch; coherent gravity retains **-57.354 degrees**.
  These are observed camera angles, not a claim of one-to-one sensor gain.
- Timelines emitted by the **actual updated mapper** produce a retained
  pitched view and correctly directed horizontal aiming in the game. Local
  evidence is under `J:/TriAevum-verify-20260910/aim-sensor-*` and
  `aim-mapped-{pitch,yaw}`; captures come from the renderer framebuffer.
  Physical mouse feel and remaining native angle limits need user validation.

To reproduce mapper output without hand-authoring sensor vectors:

```sh
oot3d_native_a32_input_tests --emit-mouse-aim mouse-pitch.json pitch
oot3d_native_a32_input_tests --emit-mouse-aim mouse-yaw.json yaw
```

Supply one generated file to `--input-timeline` with the existing private
Hyrule Field checkpoint (bow on ZR), native 30 Hz, 320 frames, screenshots at
120/200/280 and a checkpoint at 280. The fixture holds the bow from 60 to 310,
moves the mouse during 100-139, then holds still. No game data is distributed.

`Automatic` selects an input that is actually moving: mouse first, then
right stick, then controller motion. Merely connecting a controller with a
valid gyro no longer masks active mouse or stick input.

SDL controller motion is normalized before mapping:

- accelerometer: m/s2 to `g`, with SDL-to-CTR axis conversion;
- gyroscope: rad/s to degrees/s, with SDL-to-CTR axis conversion.

`Calibrate controller motion` averages 60 valid host samples. Gyroscope bias
and the neutral accelerometer pose are persisted; the latter is used by the
freecam tilt source while native HID retains the physical gravity vector.

## TopScreen freecam

The TopScreen camera feature remains owned by
`TopScreenUiConfigRuntime` (enable, native speed and inversion). Its physical
source and sensitivities are owned by the controls profile. The F1 `Controls`
tab presents both in one section, avoiding duplicate or conflicting controls
in the `TopScreen 1.2` tab.

Supported physical sources are mouse, digital bindings, right stick,
controller gyroscope, controller accelerometer, combined controller motion
and automatic selection. They resolve to the canonical C-Stick channel used
by TopScreen. None are routed into native gyro aiming unless that source is
independently selected for the native motion channels.

Mouse input is relative: it bypasses the 3DS analog-stick dead zone and the
native-stick magnitude clamp. C-stick speed, inversion and smoothing remain
limited to the native aiming path; TopScreen free-camera speed and inversion
remain owned by TopScreen. SDL relative motion is drained while F1 or native
touch owns the pointer, and the first sample after a capture transition is
discarded so menu movement cannot rotate the gameplay camera.

## Verification

`three_ds_recomp_input_tests` exhaustively covers the native routes and 3DS
hardware variants. `oot3d_native_a32_input_tests` covers the OoT3D adapter,
presets, JSON round trips, native-touch shortcuts, physical units and
calibration persistence.

`oot3d_native_a32_ctr_host_tests` maps HID shared memory and verifies the pad,
touch, accelerometer and gyroscope ABI byte-for-byte, including indices,
quantization, axis wiring and runtime profiles.
