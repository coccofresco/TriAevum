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
Use `--controls-config <path>` to select another file. A missing file starts
from the Keyboard + Mouse preset and is created by `Apply and save`.

Built-in presets:

- `keyboard`: WASD and the existing native-game keyboard layout; numpad
  directions drive aiming/freecam.
- `keyboard_mouse`: the keyboard layout plus mouse buttons and mouse motion.
- `controller`: left stick for movement, standard SDL buttons/triggers,
  controller motion for native aiming and right stick for freecam.
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
- gyroscope ring at HID offset `0x154`, 32 entries, 101 Hz;
- accelerometer input is expressed in `g` and quantized at 512 units/g;
- gyroscope input is expressed in degrees/second and quantized with the
  native coefficient exposed by `HID:GetGyroscopeCoefficient`;
- raw-entry axis wiring follows the CTR/Azahar HID implementation.

Mouse motion is converted to angular velocity using elapsed host-poll time.
Right-stick and digital sources produce the same physical unit, so the guest
continues to own aiming state, filtering and gameplay behavior.

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
