# Azahar Android Controls

This Android library ports Azahar's actual input overlay, not a visual imitation.
It has no emulator, title-code, SDL or renderer dependency. The Vulkan/NRI game
surface remains owned by TriAevum's Android host.

## Preservation Contract

- The four `InputOverlay*.kt` classes preserve drawing, button artwork, pointer
  tracking, D-pad and button sliding, Circle Pad/C-stick movement, relative stick
  centering, haptic feedback, layout editing, portrait/landscape defaults, control
  visibility, global/per-control scale and opacity preference behavior.
- All referenced artwork/density variants and layout integers come from the same
  donor snapshot. No substitute shapes or game-derived textures are introduced.
- Adaptations are limited to namespace, application context and host callbacks;
  an unused activity viewmodel and unrelated drawer-lock setting are omitted.
- `AzaharInputAdapter` translates donor IDs to the existing 3DS HID bits. It
  preserves the donor JNI's Y inversion and circular stick normalization. It
  never maps them to keyboard keys or game-specific actions. HOME, screen swap
  and turbo remain separate host actions, not fictitious HID buttons.
- Original copyright headers and `license.txt` remain. The manifest records
  every source hash and each imported/adapted output hash. The upstream overlay
  is last changed at `3716f6b9b63ee5271fe5a1c4327972e02158834d`; the donor snapshot
  is the same `beb5681...` already used by this project's Azahar imports.

## Host Integration

1. Implement `Native3dsInputTarget` against the existing native input owner.
   Preserve event edges and merge overlay/controller sources there; never run
   gameplay or renderer commands synchronously on the Android UI thread.
2. Bind `OverlayHost` with an `AzaharInputAdapter` before constructing
   `InputOverlay` with the activity context. Add it above the SDL game surface
   with the original transparent overlay composition.
3. Pixel touch callbacks are **not** normalized guest input. Apply the existing
   inverse presentation/TopScreen transform, including letterboxing and aspect
   ratio, before producing guest touchscreen coordinates. Never use a fixed
   `x/width`, `y/height` full-window mapping for single-screen UI.
4. The app must release input on cancel, loss of focus, pause, controller layout
   changes and teardown, detach the old overlay before `unbind`, and reset the
   donor pointer trackers before reusing the view. Rebind before resuming.
5. Reuse donor preference keys and original editor operations for settings;
   `setIsInEditMode`, `refreshControls`, `resetButtonPlacement` and
   `EmulationMenuSettings` remain available.

## Status And Remaining Work

The AAR and four JVM adapter tests compile/pass on Linux (JDK 17, Gradle 8.13,
AGP 8.13.2, Kotlin 2.0.20, API 35 SDK, min API 29). Offline tests also verify the
donor hashes, resource closure and shared HID ABI. These tests do **not** verify
real Android MotionEvents, overlay composition, layout-editor UI or gameplay.

Still required for a completed 1:1 in-game port: the APK/SDL host and JNI input
consumer, donor settings-menu wiring, lifecycle/cancel integration, physical
multitouch tests and visual comparison on the device. The imported donor's
existing quirks are not silently changed: for example its joystick change
notification compares both axes with `&&`; isolate and test any future fix,
rather than claiming the adapter tests validate that gesture logic.

## Reproducible Checks

```sh
python3 -m unittest tools.android.test_azahar_overlay_import
# Optional donor verification, read-only. Not needed for normal builds.
python3 tools/android/import_azahar_overlay.py /path/to/azahar --check
export ANDROID_HOME="$HOME/triaevum-android-tools/sdk"
"$HOME/triaevum-android-tools/gradle-8.13/bin/gradle" -p ports/android \
    :controls:assembleDebug :controls:testDebugUnitTest --console=plain
```

No SDK, build output or game input is committed. Tool installation uses the
[official Android SDK tools](https://developer.android.com/tools/sdkmanager).
