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
- Adaptations cover namespace, application context, host callbacks, native-drawn
  controls and touch cancellation/ownership. An unused activity viewmodel and
  unrelated drawer-lock setting are omitted. Stick change notification now fires
  when either axis changes, including a return to centre on only one axis.
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
4. Use `InputOverlay.cancelTouches()` on activity pause or controller hide. It
   resets every local pointer/visual state and cancels the sink as a batch, without
   generating a HOME tap. CANCEL, focus loss, detach, refresh and edit-mode changes
   use the same boundary. Late motion cannot re-arm until a fresh DOWN. Detach the
   old overlay before `unbind`; repeated cancellation after unbind is harmless.
5. Reuse donor preference keys and original editor operations for settings;
   `setIsInEditMode`, `refreshControls`, `resetButtonPlacement` and
   `EmulationMenuSettings` remain available.

## Status And Remaining Work

The controls JVM suite passes 12 tests: five adapter tests and seven Robolectric
API-29 MotionEvent tests of the production view/drawables. The latter cover
single-axis movement, centre/up, multi-pointer ownership, cancel without HOME tap,
late motion, edit/refresh, focus loss and detach. Controls and app Kotlin/Java
compile on Windows (JDK 17, Gradle 8.13, AGP 8.13.2, Kotlin 2.0.20, SDK 35).
Offline checks verify donor hashes, resource closure and shared HID ABI.
**JVM Android simulation is not a device/JNI/gameplay qualification.**

The APK now hosts the overlay (`app/.../TriAevumActivity.java`,
`TouchControls.kt`). Controls reach the game's on-screen pad through JNI
(`ports/android/runtime/android_input_bridge.cpp`,
`tools/three_ds/input/three_ds_virtual_pad.h`). The input poll merges that pad
with the physical devices before its bindings, so there is still one input owner.
Touches that miss every control go to SDL as touches. HOME is the host menu key:
a tap toggles the in-game menu, a long press opens F1. The app releases the
overlay's input on pause and focus loss.

With TopScreen's touch layout (`touch_layout`, default on Android), OoT3D
draws every control the overlay shows, in every scene: a vector base (disc,
pill, cross, circle pad with its knob; `tools/three_ds/input/three_ds_touch_skin.h`
rasterizes them at run time, no title data), lit while its button is held, and
its letter or icon. The HUD's content for A, B, X, Y, ZL, ZR and the D-pad (the
item, counts, the A action word, the cross icons) draws inside it when there
is any, and the letter eases to a corner badge meanwhile; the HUD's own rings,
cross and letters are not drawn, so a control never switches between two
looks. Those HUD parts leave the HUD and the minimap takes the top-right
corner. Only the controls that do something show: in gameplay as much as the
game shows its HUD buttons (none while it hides them), the item buttons
without an item stepping back; in a dialogue only A, drawn as a
right-pointing "next" triangle; in a choice that triangle with the pad and the
D-pad; in a cutscene without text none; in the title demo A and START; in
native menus, where the page takes touches, the item controls at 60%; after
8 s without input every control dims to half. A double tap on the game
surface hands the view back from the free camera (a drag takes it). TriAevum seeds its own default layout (version 4 in
`TouchControls.kt`; Reset layout restores it): pad and D-pad on the left, face
buttons on the right, L/ZL and R/ZR stacked in the top corners, mostly in the
bars beside a 5:3 picture. The overlay reports each control's bounds and draws its own art only
while the game does not draw the controls (touch layout off) or while the
layout is edited (`InputOverlay.kt` host step in
`tools/android/import_azahar_overlay.py`). A touch on no control is not the
overlay's: it reaches the game surface with its own pointer id, so menus, the
3DS touch screen and the free camera (a drag during gameplay) see every finger.
On desktop, `TRIAEVUM_TOUCH_ANCHORS` simulates the overlay's layout.

The overlay owns and saves its settings (shown, opacity, size, vibration, hide
with a controller); the in-game menu's Controls tab (Android) and F1 > Controls
> Touch show them and ask the overlay for changes, layout editing (Done / Reset over the overlay) and
the default layout through `TouchOverlaySettings`
(`tools/three_ds/input/three_ds_touch_overlay_settings.h`). A game controller
hides the overlay while that option is on, and the game then draws its whole
HUD; a touch shows the overlay again.

The activity asks for unbuffered dispatch: each touch gesture (overlay and
game surface) and, from Android 11, stick motion arrive as they happen instead
of batched to the next display frame.

Still required: physical multitouch, vibration, JNI/gameplay and layout-editor
visual acceptance. The single-axis and cancellation defects have failing-before
and passing-after JVM evidence; adaptation steps remain reproducible in
`tools/android/azahar_touch_adaptation.py`. MM3D native-HUD touch anchors remain a
separate integration gap; OoT's TopScreen path does not qualify MM3D.

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
