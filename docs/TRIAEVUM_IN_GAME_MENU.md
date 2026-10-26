# In-game menu

A controller-friendly settings menu drawn by the game's own UI renderer, in
the style of Dusklight's settings screen, holding the everyday subset of the
settings. F1 is unchanged and keeps everything the menu has no row for (grass
rules and masks, PBR materials, the lighting environments, texture packs,
presets by name, the dev tools and the rest).

## Using it

- **Esc** on the keyboard or **R3** (right stick click) on a pad opens and
  closes it (on Android, a tap on HOME). Neither reaches the game while it is
  open, and **the game pauses** while the menu is open (UX decision 3b, which
  replaced Dusklight's keep-running behaviour): no game time, input, audio or
  physics advance, the last frame stays behind the menu, and closing resumes
  exactly where it stopped (see "Pause" below).
- **D-pad / left stick** move between rows, **Left / Right** change a value
  in place, **A / Enter / Space** confirm (open a choice list, flip a
  toggle, run an action), **B / Backspace** go back (leave the list, then
  resume), **LB / RB** or **Q / E** switch tabs. **Resume** is the footer's
  persistent button on every tab (with the Esc / R3 keys beside its label).
- **Mouse**: the pointer is free while the menu is open (gameplay capture is
  released). Moving over a row, a tab or a value highlights it and focuses
  the row; a click flips a toggle, opens a choice list or picks a value from
  it, runs an action, selects a tab, or presses one of the ‹ › arrows of a
  number (held, the arrow repeats). The wheel moves between rows, or along
  the open list. The Resume button at the bottom right closes the menu, and
  the questions' buttons take clicks. A click outside any of these does
  nothing.
- A changed value is written to its owner at once and saved, exactly as the
  same control in F1 would (same validation, same files). The status line
  under the rows reports the outcome in one vocabulary for every owner
  (`oot3d_menu_help.h`): **Saved**; **Session only** (this launch saves no
  settings, or the owner has no file); **Preview: press Save in F1 to keep**
  (applied while F1 still holds an edit); **Could not save: Retry**, which
  also asks whether to retry through the same owner. Green is information,
  red an error.
- "Modified" (the dot before a value) compares with the product's default
  graphics, `ProductDefaultGraphicsSettings()` owned by the graphics runtime
  (`kProductDefaultGraphicsPreset`, the Enhanced preset with the default
  window), not with the C++ struct defaults (the Authentic look). The
  Resolution row is modified once the user chose a size (provenance `user`).
- A display change (window mode, resolution) asks "Keep these display
  settings?" with the same countdown as F1; the question is asked once, in
  the menu while it is open, in F1's window otherwise.
- Opening F1 (or the menu bar) closes the in-game menu.

## Tabs

| Tab | Rows |
| --- | --- |
| Video | Graphics preset (Authentic / Enhanced / Toon; Custom is the result of any change), Window mode, Aspect ratio (Original 3DS 5:3 / 16:9 / 21:9 / Free), Resolution (the common sizes; a custom size is typed in F1), VSync, Frame rate, **Reset display settings**, Render scale, Anti-aliasing, Upscaler and quality, Scene FOV, Extended draw distance, HUD size (Native / Optimized, the TopScreen profile) |
| Graphics | Lighting model, Global illumination, Ambient occlusion, Shadows (modern lighting only), Reflections, Toon, Grass quality |
| Controls | Keyboard / Mouse / Controller / Capture mouse, Control preset, Aim source, Invert aim X / Y; with the TopScreen profile, Camera (Gameplay camera, speed, smoothing, invert X / Y, Camera zoom, Camera FOV) and C-Stick aiming (speed, invert X / Y) |
| Photo mode | Photo camera (F6), Hide HUD, Photo camera speed, Back to the game's view, then the camera's controls as read-only rows (F6, WASD / left stick, Q / E / shoulders, mouse drag / right stick, Shift / Ctrl) |
| Game | With the TopScreen profile, Gameplay (Quick put away, Landing roll, Roll dash, Aiming reticle, 3D rain, Rain density, SELECT button); Game language; Save states (F5 / F8, with or without settings; loading resumes the game); Session: **Advanced settings (F1)**, **Quit game** |
| Help & folders | The controls summary (desktop: Esc · R3, F1 · L3 + R3, F5 / F8, F6, F11; Android: tap / hold HOME), Show the help card; desktop: Open saves folder, Open logs folder; Android: where saves and diagnostics are handled (the installer) |

The camera rows were renamed so the two cameras cannot be confused:
Controls > Camera "Free camera" is now **Gameplay camera** (the TopScreen
camera orbiting Link) and Photo mode's "Free camera" is **Photo camera**, in
the menu and in F1 alike. The owners and the configuration keys are
unchanged (`FreeCameraEnabled`, `FreeCameraSpeedLevel`, ... and the
developer free camera).

**Reset display settings** (Video) is the host-owned recovery for a user
whose F1 is out of sight (#93, #109, #51): `GraphicsSettingsRuntime::
ResetDisplay()` applies the same reset as the safe presentation after a
launch that ended in its first minute (`ResetDisplaySettings` in
`graphics_settings.h`: windowed at the default size with provenance
`default`, native render scale, no anti-aliasing or upscaler), through the
display transaction (a window mode change asks to keep it) and saved.

**Advanced settings (F1)** opens F1 (which closes the menu). It makes F1
reachable from the pad; F1 itself is still made for a mouse.

**Quit game** asks "Quit the game?" (settings are kept; the game's own
progress is saved only by saving in the game or by a save state). Quit
writes what the host owners still hold (`GraphicsSettingsRuntime::
SavePending`, the runtime configuration) and closes the window through the
normal exit path, which clears the `.launching` marker
(`oot3d_native_game.cpp`). The other owners (controls, TopScreen, language)
save at each change, so nothing of theirs is pending; F1's unsaved drafts stay
unsaved, as when F1 is closed.

**Help & folders**: "Open saves folder" and "Open logs folder" hand a
`file://` URL of the resolved folder to the desktop's file browser
(`SDL_OpenURL`); the saves row's help names the folder. Android has no file
browser to open: the tab names the route instead (close the game, then the
installer's Saves and Diagnostics pages, which run in the installer's
process and refuse a save import while the game's process lives). The hook
`InGameMenuHost::OpenInstallerPage` turns those into "Export saves" and
"Share diagnostics" rows once a JNI bridge with a quit-then-open flow
exists; it is not wired yet.

## First-play help card

A card drawn by the same UI stack, shown once per installation when the game
is first presented (after the preparation screen) and no F1 is open:

- Desktop: "Esc or R3: game menu. F1 or L3+R3: all settings. F5 / F8: save /
  load a state. The game pauses while its menu is open. Your saves: <the
  resolved save-data folder>" (the path is resolved at runtime and stored
  nowhere).
- Android: "Tap HOME: game menu. Hold HOME: all settings."

The card is the menu opened with the card alone (`Model::OpenCard`), so the
game is held while it is up. OK, B / Back or Esc / R3 dismiss it; dismissing
records `Help.Shown: true` in the platform's runtime configuration
(`config/TriAevum.json` on desktop, `TriAevum.android.json` on Android), and
the card is not shown again. Help & folders > Show the help card re-opens it
over the menu. Scripted sessions (captures, fixed steps, frame or time
limits, input timelines, schedules, benchmarks, the developer lab) never see
it; `TRIAEVUM_HELP_CARD=1` / `0` overrides that for qualification. The key
glyphs come from WP-08 once it lands; until then the text names keyboard and
Xbox keys.

## Pause

The pause is a hold of the guest at the window host's frame owner
(`NativeA32WindowSession::RunFrame`), not a feature of the menu:

- At the start of each presented frame the host reads `InGameMenu::
  HoldsGame()` (the menu, or the card, as the previous frame left it). While
  held, the presentation scheduler is not advanced: no guest refresh is due,
  so no guest code, HID sample, DSP/audio frame, guest clock tick or physics
  step runs, and the scheduler's phase is kept for the resume. Audio stops
  with the guest (no new samples reach the output player; what is buffered
  plays out).
- The frame still runs the host side: input polling for the menu, the menu's
  update and drawing, F5 / F8 servicing (a save state captures the held
  moment), and the presentation of the last scene frame at the
  interpolation sample it last showed (`heldVisualInterpolationAlpha`: an
  interpolated mode repeats its last transition sample, the direct mode
  re-presents the last scanout), with the menu drawn over it.
- Closing the menu resumes on the next frame from the kept clock phase, so
  the game continues as if no time had passed. The decision is taken once per
  frame from the previous frame's state, so a frame never changes its mind
  halfway (the opening frame still advances; the closing frame is the last
  held one).
- With the menu closed nothing of this runs: `guestHeld` is false, the
  scheduler advances exactly as before, and deterministic captures and
  `--input-timeline` runs produce the same frames (verified byte-identical,
  see Verification).
- The developer lab advances at guest refresh boundaries; while the game is
  held it advances once per presented frame instead, so its menu presses,
  waits and captures keep working over the held game.
- Android shares the same frame owner, so the HOME tap's menu holds the game
  there too.

The TopScreen rows are exactly the TopScreen settings F1 offers (Display >
TopScreen HUD, Controls > Camera, Shortcuts' SELECT action, Gameplay), read
and written through the same `TopScreenUiConfigRuntime`, so the file F1 saves
is the file the menu saves. "Modified" compares with the standard profile,
which is the configuration's default (HUD Optimized 70%, free camera off at
speed 4 / 7 with Default smoothing, zoom and FOV 100%, every gameplay option
on, rain density 100%, SELECT opens the Save screen). The D-pad shortcut
bindings, "B exits Items to Save screen" and the HUD layout stay in F1 with the
other bindings; the minimap has no row anywhere but the SELECT action.

The game language is written to `config/game_language.json` by the same
`GameLanguageSettings` F1 uses, so F1's combo shows the new choice at once;
the game reads the file at its next start (a save state does not restart it).

Window mode, aspect ratio, resolution and VSync are host-owned (a preset never
sets them), so changing them keeps the current preset; any other Video or
Graphics change makes it Custom, as in F1.

The aspect ratio is the shape of the presentation picture
(`fast/extension/presentation_picture.h`, persisted as `Graphics.Output.Aspect`,
default 16:9). A fixed ratio is the largest rectangle of that shape fitted
into the window or the screen, centred, with black bars on the remaining
sides; Free uses the whole output (the window as the user shapes it, the whole
screen in a fullscreen mode). Choosing a fixed ratio for a windowed output also
resizes the window to that shape (the width stays, the height follows), so no
bars show until the user resizes it; the window stays freely resizable and any
other shape is letterboxed, since SDL has no aspect lock and a forced resize
would fight the user's drag. The renderer sizes the scene targets to the
picture, fits the scanout into it and clears the bars, the scene FOV policy
reads the picture's shape (a wider picture shows more at the sides, never
stretched), and every UI canvas, the menu and the mouse mapping are fitted into
the picture, so the menu stays centred in the picture and a click lands where
it is drawn whatever the window's shape. A choice the renderer cannot take right now (an
upscaler the GPU lacks, a shadow mode without modern lighting) is listed
greyed out and refused with a message, through the same validator as F1's
combos.

## Implementation

- `tools/oot3d/ui_menu` (library `oot3d_ui_menu`): `oot3d_menu_model` is the
  content and navigation (tabs of rows read and written through callbacks,
  focus, choice list, modal question, key repeat), `oot3d_menu_presentation`
  draws one frame of it as `UiPrimitive`s on a canvas (window, tab bar, rows,
  help pane, scrollbar, status and hints), `oot3d_menu_font` draws text from
  baked glyph atlases. `oot3d_menu_tests` covers them.
- `tools/oot3d/native_game_runtime/oot3d_in_game_menu.*` builds the document
  from the setting owners (`GraphicsSettingsRuntime`, the controls
  configuration runtime, `TopScreenUiConfigRuntime` when the TopScreen
  profile is active, `GameLanguageSettings`, `PhotoModeState`,
  `DevFreeCamera`, `SavestateLibrary`) and nothing else: the menu holds no
  setting of its own. The window session feeds it the host controls once per
  presented frame and renders its primitives with the integrated N64 UI
  renderer on the widescreen canvas, after every other UI subsystem, so it
  covers the HUD and its dim covers the window. No ImGui. The session builds
  the menu only after the renderer window exists: the menu's first snapshot
  creates the graphics settings runtime, which reads the settings file through
  the persistence port the window's GUI installs; built before the window, the
  runtime started without the port and the whole launch ran on the defaults
  (Native lighting, no grass, "session only", nothing saved). The runtime now
  reports a port installed after its start as
  `TRIAEVUM_GRAPHICS_SETTINGS_PERSISTENCE_LATE` on stderr.
- Fonts: Fira Sans Condensed Regular (body) and Bold (keys, tabs, headings),
  SIL OFL 1.1, rasterized once by
  `tools/oot3d/ui_menu/font/generate_menu_font_atlas.py` (Pillow) into
  8-bit coverage atlases stored as PNG in
  `oot3d_menu_font_atlas_data.cpp`, decoded at start with stb_image and
  resolved as alpha-only textures (`triaevum/menu/font/body|heading`) by
  `MenuFontTextureProvider`, which defers every other texture to the provider
  the renderer had. Fira Sans has no geometric triangles, so the arrows are
  single guillemets; a code point the font lacks is skipped by the generator
  and the tests check the symbols the menu uses are in the atlases. No
  proprietary font or image is used; the window, rows and scrollbar are solid
  quads.
- Display confirmation: while the menu is open it asks the keep/revert
  question itself (`DeferDisplayConfirmation`), and F1's ImGui confirmation
  window stays hidden so the question is asked once.
- Input: `InGameMenuHostInput` in the polling state is filled from the
  arrows / D-pad / left stick, Enter / Space / A, Backspace / B, Q / LB,
  E / RB, Esc / R3, plus the mouse (window position and the presentation
  picture, left button, the window's wheel accumulator while a host surface
  is up; the legacy `WheelHandler` is never updated in the native game and
  is not read); while the menu is open
  `hostGuiVisible` withholds the game's input as an ImGui window would.
- Pointer: the presentation records a hit region for every target it draws
  (tabs, focusable rows, the choice list's values, the number arrows, the
  modal's buttons, the close hint) in canvas units and hands them to the
  model as its `Layout`. The host maps the mouse onto the widescreen canvas
  with the same `FitUiPresentation` the UI renderer uses, inside the
  presentation picture (the aspect ratio's letterbox) rather than the window,
  so the mapping does not depend on the window's shape; the model resolves
  hover, click (press edge), held arrows (key-repeat timing) and wheel
  against the previous frame's layout. Only a pointer that moved
  refocuses a row, and where the mouse rests when the menu opens is not a
  move, so an idle mouse never fights the keys or the pad.

## Verification

Developer launch (`TRIAEVUM_DEV_LAB`), play state at the Kokiri ledge,
1280x720, standard TopScreen profile, driven by the dev lab `menu` op:

```json
{"id": 3, "op": "menu", "steps": ["down", "down", "confirm"], "stepFrames": 4}
```

Each step (`toggle`, `up`, `down`, `left`, `right`, `confirm`, `back`,
`tab_prev`, `tab_next`) is held one guest frame at the start of a
`stepFrames` period, replacing the host controls; the answer carries the
menu's state once the last press was released (`open`, `tab`, `row`,
`value`, `pane`, `statusLine`, `statusError`, `modal`, and with
`"regions": true` the hit regions as last drawn). Pointer steps address
those regions: `move:<kind>[:<index>]` places the mouse on a region's
centre, `click:...` presses it for one frame, `press:...` holds it for the
period, `wheel:<notches>` turns the wheel, `pointer_out` removes the
pointer; a region that was not drawn answers `missing_target`. A capture after each
state gives the frames reviewed on 2026-10-20: open on Video, the Resolution
list open and left, VSync off and on with the preset kept, the Graphics,
Controls, Photo mode and Game tabs, wrap-around at both ends,
Scene FOV right and left (1.05x then 1.00x), close by Esc, reopen, close by
B. `oot3d_menu_tests` passes, and the F1 widget smoke test still passes
with the deferred confirmation.

A second lab run checked the TopScreen rows against the standard profile
(every value at its default and unmarked: HUD size Optimized, free camera
off, 4 / 7, Default, zoom and FOV 100%, C-Stick speed 4 / 9, gameplay rows
on, rain density 100%, SELECT opens the Save screen), that HUD size Native
and Camera FOV 105% save to the run's `topscreen-config` and return to the
default, and that choosing Italiano writes `selected: "it"` to the run's
`game_language.json` with the "Saved. Restart the game to apply it." status
(57 checks). A cold boot on that file through the title-to-file-select input
timeline then reached the file-selection screen in Italian.

A third lab run drove the menu with the pointer steps (hover on VSync and
Resolution, click to open the list and to pick a value, VSync off and on by
click, Scene FOV through its arrows with a held arrow repeating, the wheel,
tabs by click and hover, a click outside any target, the close hint, reopen
by Esc); `oot3d_menu_tests` covers the same against the layout of a drawn
frame, the modal's buttons included.

A fourth lab run (2026-10-20) drove the Aspect ratio row by pointer and
checked the result on the captures (black edge rows and columns counted) and
through the hit regions: 21:9 snaps the windowed output to 1280 x 549 with no
bars; 1280 x 720 chosen again letterboxes the 21:9 picture with 85 / 86 px
bars above and below, and in that letterbox a click on VSync, the Scene FOV
arrows and the tabs still land (the pointer is mapped through the picture);
Original 3DS snaps to 1280 x 768 and, back at 720p, pillarboxes with 40 px
bars at each side; Free fills the window and leaves the output size alone;
16:9 returns unmarked. The menu is centred in the picture in every capture,
and the scene shows more at the sides at 21:9 (53 of 55 checks; the two
"failures" were the script hovering before the rest-at-open sample). The F1
actual-widget smoke passes with the new Display combo (11,161 assertions).

A fifth lab run (2026-10-20, after the construction-order fix above) opened
the menu on the play configuration (Custom preset, Modern lighting, virtual
shadow maps, custom grass) and read those values back from the rows
(Lighting model Modern, Shadows Virtual Shadow Maps, Grass Custom, Graphics
preset Custom), then changed them with the keys: Lighting model Native and
Modern again, Grass High then Medium, preset Authentic then Toon, each
answered "Saved" and each visible in the capture taken with the menu closed
(the Native frame brighter and flat, the Authentic frame without grass or
shadows), and the run's `config.json` held the last choices (preset Toon,
grass Medium). Before the fix the same run read Native / Off / Authentic and
answered "Applied for this session only".

Not done yet: menu sounds (the host has no sound asset of its own) and a
5:3 canvas for the native TopScreen layout (the menu always draws on the
16:9 canvas, which a wider or narrower picture fits and centres).
