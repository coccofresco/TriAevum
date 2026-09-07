# 3DS recomp input architecture

## Contract

`tools/three_ds/input` is the cross-title input boundary. Keyboard, mouse,
controllers and host motion sensors are physical sources; they are never game
controls by themselves. The mapper resolves them to the union of real 3DS
input channels:

- standard HID buttons and Circle Pad;
- lower-screen touch;
- accelerometer and gyroscope;
- Circle Pad Pro/New 3DS Extra HID: secondary stick plus ZL/ZR.

`HardwareProfile` describes which subset exists on Old 3DS, Old 3DS with
Circle Pad Pro, and New 3DS. `NativeRouteFor` is the single route table for all
abstract digital controls. A route without a native endpoint is an error.

## Layers

1. The host adapter polls a platform API and produces `PhysicalInputState` and
   held `HostBinding` values. It owns device discovery and unit conversion,
   but no title behavior.
2. `ResolveInput` maps that state to `InputFrame`. This is the only canonical
   gameplay input surface shared by 3DS recompilations.
3. A title adapter gives native channels user-facing semantic names and may
   compile convenience commands into native input sequences. For example,
   OoT3D Gear/Map/Items shortcuts synthesize the original touch targets.
4. The title's service adapter projects the frame to its standard HID and
   Extra HID transports. Hardware transport layout must not leak back into
   host bindings or title logic.

Host-shell commands such as opening F1, taking a screenshot or quitting are
outside `InputFrame`; they are not gameplay controls and are never delivered
to the title.

## Non-redundancy rules

- Do not add title fields such as `FooButtonHeld`, `TopScreenRightStickX` or a
  second motion vector beside `InputFrame`.
- Do not add an abstract action unless it has a route to a standard HID,
  Circle Pad, touch, motion, or Extra HID channel.
- A title convenience action must synthesize native input rather than create a
  parallel gameplay API.
- Device profiles select physical sources; they do not change the native
  channel consumed by the game.
- Application features consume canonical native channels. OoT3D TopScreen
  freecam consumes C-Stick input; native aiming consumes gyroscope and
  accelerometer input.

## Hardware evidence

The standard button mask and Extra HID split follow the local Azahar service
implementation:

- `E:/azahar pcvr/src/core/hle/service/ir/extra_hid.h`
- `E:/azahar pcvr/src/core/hle/service/ir/extra_hid.cpp`
- `E:/azahar pcvr/src/core/hle/service/ir/ir_rst.h`
- `E:/azahar pcvr/src/core/hle/service/ir/ir_rst.cpp`

Standard HID occupies bits 0-13. Extra HID carries ZL/ZR and the secondary
stick for Circle Pad Pro/New 3DS. The canonical frame represents each once;
the service adapter performs the transport split.

## Verification

`three_ds_recomp_input_tests` exhaustively checks that all 22 abstract digital
controls have a native route, that Old 3DS exposes its 16 supported routes,
and that CPP/New 3DS expose all 22. It also checks concrete channel output,
motion/pointer ownership, transient C-Stick delivery and host-axis conversion.

`oot3d_native_a32_input_tests` checks that OoT3D contributes exactly those 22
digital routes plus three native-touch shortcuts, with no unhandled or
parallel action.
