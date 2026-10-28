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
  persistent button on every tab (with the menu key beside its label). The
  footer names these controls with the button prompts of the device in use
  (see "Button prompts").
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
| Video | Graphics preset (Authentic / Enhanced / Toon; Custom is the result of any change), Window mode, Aspect ratio (Original 3DS 5:3 / 16:9 / 21:9 / Free), Resolution (the common sizes; a custom size is typed in F1), VSync, Frame rate, **Reset display settings**, Render scale, Anti-aliasing, Upscaler and quality, Scene FOV, Extended draw distance, HUD size (Native / Optimized, the TopScreen profile), Button labels (Controller / Original 3DS, see "HUD button labels") |
| Graphics | Lighting model, Global illumination, Ambient occlusion, Shadows (modern lighting only), Reflections, Toon, Grass quality, Distant grass cards, Textures > Custom textures (F1's "Load custom textures"; keeps the preset) |
| Controls | Keyboard / Mouse / Controller / Capture mouse, Rumble (Off / Light / Strong, F1 > Controls > Devices > Controller), Button prompts, Control preset, Aim source, Invert aim X / Y; with the TopScreen profile, Camera (Gameplay camera, speed, smoothing, invert X / Y, Camera zoom, Camera FOV) and C-Stick aiming (speed, invert X / Y) |
| Photo mode | Photo camera (F6), Hide HUD, Photo camera speed, Back to the game's view, then the camera's controls as read-only rows (F6, WASD / left stick, Q / E / shoulders, mouse drag / right stick, Shift / Ctrl) |
| Game | With the TopScreen profile, Gameplay (Quick put away, Landing roll, Roll dash, Aiming reticle, 3D rain, Rain density, SELECT button); Game language; Audio (Master volume in 5 % steps, Mute, and on desktop Mute when unfocused; see below); Save states (F5 / F8, with or without settings; loading resumes the game); Session: **Advanced settings (F1)**, **Quit game** |
| Help & folders | The controls summary of the device in use, with its button prompts (keyboard: Esc, F1, F5 / F8, F6, F11; a pad: its menu stick, L3 + R3, select / back, tabs; touch: tap / hold HOME), Show the help card; desktop: Open saves folder, Open logs folder; Android: where saves and diagnostics are handled (the installer) |

The camera rows were renamed so the two cameras cannot be confused:
Controls > Camera "Free camera" is now **Gameplay camera** (the TopScreen
camera orbiting Link) and Photo mode's "Free camera" is **Photo camera**, in
the menu and in F1 alike. The owners and the configuration keys are
unchanged (`FreeCameraEnabled`, `FreeCameraSpeedLevel`, ... and the
developer free camera).

**Audio** (Game) binds to the host audio service's output policy, the one
owner of `Audio.MasterVolume` / `Audio.Muted` / `Audio.MuteWhenUnfocused`
in the runtime configuration, exactly as F1 > Game > Audio does
([audio settings](TRIAEVUM_AUDIO_SETTINGS.md)). The rows are built game-neutrally
(`tools/oot3d/ui_menu/oot3d_menu_audio.cpp`). The volume row is unavailable
while muted. Since the game is paused behind the menu, a volume change (or
unmuting) plays the host's short preview tone through the same output gain,
so the new level is heard without running any game audio.

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
browser to open: its tab has "Export saves" and "Share diagnostics" rows
(`InGameMenuHost::OpenInstallerPage`). The installer's Saves and
Diagnostics pages run in the installer's process and refuse a save import
while the game's process lives, so a row asks "Close the game?" first, then
records the page (`ports/android/runtime/android_host.cpp`) and quits
through the same path as Quit game; as the game's activity ends, it opens
the installer with that page (`TriAevumActivity.openRequestedInstallerPage`).
A host without the hook names the route instead.

## First-play help card

A card drawn by the same UI stack, shown once per installation when the game
is first presented (after the preparation screen) and no F1 is open:

- Keyboard: "[Esc]: game menu. [F1]: all settings. [F5] / [F8]: save /
  load a state. The game pauses while its menu is open. Your saves: <the
  resolved save-data folder>" (the path is resolved at runtime and stored
  nowhere). With a pad: "[RS]: game menu. [LS] + [RS]: all settings. Save
  states: the game menu's Game tab." with that pad's labels (see "Button
  prompts").
- Touch (Android): "Tap [HOME]: game menu. Hold [HOME]: all settings."

The card is the menu opened with the card alone (`Model::OpenCard`), so the
game is held while it is up. OK, B / Back or Esc / R3 dismiss it; dismissing
records `Help.Shown: true` in the platform's runtime configuration
(`config/TriAevum.json` on desktop, `TriAevum.android.json` on Android), and
the card is not shown again. Help & folders > Show the help card re-opens it
over the menu. Scripted sessions (captures, fixed steps, frame or time
limits, input timelines, schedules, benchmarks, the developer lab) never see
it; `TRIAEVUM_HELP_CARD=1` / `0` overrides that for qualification. The keys
are drawn as button prompts of the device in use, and an open card follows a
change of device.

## Button prompts

The footer hints, the Resume button's keys, the Help & folders controls
summary and the help card show the buttons of the device in the user's hands
(UX plan WP-08, issues #75 and #115):

| Family | Select / Back | Tabs | Change | Menu / F1 |
| --- | --- | --- | --- | --- |
| Xbox | A / B | LB RB | D-pad | RS / LS + RS |
| PlayStation | cross / circle | L1 R1 | D-pad | R3 / L3 + R3 |
| Nintendo | A / B (the printed labels: SDL maps a Nintendo pad by label) | L R | D-pad | RS / LS + RS |
| Steam Deck | A / B | L1 R1 | D-pad | R3 / L3 + R3 |
| Generic | A / B (SDL's names for the south and east positions) | L R | D-pad | R3 / L3 + R3 |
| Keyboard | return / backspace (their key symbols) | Q E | left / right arrow keys | Esc / F1 (and F5 / F8, F6, F11 in the summary) |
| Touch | a tap (no Back or tab hint: a tap does it) | | | tap HOME / hold HOME |

- **Who decides.** Input owns it. The controls owner
  (`NativeControlConfigRuntime`) keeps the user's choice and resolves the
  family (`ActivePromptSet`, `ThreeDsRecomp::Input::ResolvePromptSet`): a
  fixed choice shows that family; **Automatic** (the default) follows the
  device used last, as the host poll reports it each poll
  (`ObserveInputActivity`: a key of a keyboard, a mouse button or a movement
  of 3 px, any button or stick of the selected pad, a finger or an
  on-screen control). A pad shows its own family; with the used pad gone a
  pad still selected gives its family, else the platform's own. Before any
  input a desktop shows its selected pad (a Steam Deck in Gaming Mode has no
  keyboard) or the keyboard; Android shows touch. A finger also moves the
  system pointer, so mouse activity within a second of a touch is that touch.
- **A pad's family** comes from SDL (`ResolveSdlController` fills
  `DeviceDescriptor::Type` and `Prompts`, `PromptSetForController`): Xbox
  360 / One, Luna, Stadia, Shield: Xbox; PS3 / PS4 / PS5: PlayStation;
  Switch Pro and Joy-Cons: Nintendo. Steam's virtual gamepad (Valve
  28de:11ff, an Xbox 360 type to SDL) shows Steam Deck labels when
  `SteamDeck=1` is in the environment (SteamOS sets it) and Xbox labels
  elsewhere; the Deck's own controls (28de:1205) are Steam Deck. A type SDL
  does not know falls back on the USB vendor (Microsoft, Sony, Nintendo),
  then Generic. Two pads: the selected one (Controls > Devices > Controller,
  automatic or preferred) gives the family; the menu reads only that pad.
- **The choice**: F1 > Controls > Devices > **Button prompts** (Automatic,
  Xbox, PlayStation, Nintendo, Steam Deck, Generic, Keyboard, Touch; "Now:
  <family>" beside Automatic), saved with Save controls, and the menu's
  Controls > **Button prompts** row, saved at once. Stored as
  `button_prompts` in `oot3d_controls.json`, written only when it is not
  Automatic so the file stays readable by older builds; presets keep it.
- **While the menu is open** the family is re-read every frame: the footer
  changes at once, the Help & folders tab is rebuilt with the focus kept on
  its row (`Model::ReplaceTab`), and an open help card's lines follow.
- **The glyphs** are TriAevum's own art, one set for the menu and the HUD
  (`tools/oot3d/ui_menu/oot3d_menu_button_glyphs.*`, drawn in text by
  `oot3d_menu_glyphs.*`): discs, pills, keycaps, a stick ring, the
  PlayStation symbols as plain strokes, a D-pad, a mouse, a tap, a house for
  HOME, and keycaps with the keyboard's conventional symbols instead of words
  (space bar, shift, return, backspace, tab, caps lock, arrows, page up /
  down, home, end, insert, delete; letters, digits and F-keys stay text).
  Black and white only (owner rule, 2026-10-27): a white body with a black
  mark or label and a thin black edge, the same on the dark panel and over
  the game; one simple sign per button (no four-button diamond with one
  lit). They are signed distances rasterized at first use into an atlas in
  three sizes, 32, 64 and 128 texels a cell (`triaevum/button_glyphs/<cell>`,
  straight RGBA, served by the menu's texture provider); the menu's text
  draws from the 64-texel one. No font, picture, console logo or
  third-party glyph set is used. Text carries them as Private Use Area
  codepoints, so the menu's text layout measures, wraps, elides and draws
  them with the letters: an icon is one codepoint (U+E000...), a labelled
  button is an opening codepoint, its label in the menu's heading font and a
  closing one (U+E100...). The TopScreen HUD's own button letters follow
  the same families (see "HUD button labels").

## HUD button labels

The letters that name the 3DS buttons on the TopScreen HUD (A, B, X, Y, ZL,
ZR) show the glyph of the button or key the player has bound to that 3DS
button, in the prompt family of the device in use (UX decision 7):

| Family | A / B / X / Y bound to a face button | Shoulder | Trigger | Stick click | D-pad |
| --- | --- | --- | --- | --- | --- |
| Xbox | disc with A / B / X / Y | LB / RB | LT / RT | ring LS / RS | D-pad, arm filled |
| PlayStation | cross / circle / square / triangle cut out of a disc | L1 / R1 | L2 / R2 | ring L3 / R3 | D-pad |
| Nintendo | disc with the printed letter (SDL maps by label) | L / R | ZL / ZR | ring LS / RS | D-pad |
| Steam Deck | disc with A / B / X / Y | L1 / R1 | L2 / R2 | ring L3 / R3 | D-pad |
| Generic | disc with A / B / X / Y (SDL's names for the positions) | L / R | L2 / R2 | ring L3 / R3 | D-pad |
| Keyboard | a keycap with the key's symbol (space bar, shift, return, backspace, tab, arrows) or its name (C, 1, Esc, Ctrl, Num8); without a key, the mouse with its bound button filled | | | | |

- **Which input.** The binding of that 3DS button in the controls owner
  (`oot3d_controls.json`, F1 > Controls > Bindings): a pad family shows the
  pad button, the keyboard its first key, else its second, else its mouse
  button (while the mouse is enabled). A 3DS button with nothing bound on
  that device keeps its letter. Touch keeps every letter (the on-screen
  controls own theirs, see the touch layout). The family is the one the
  menu uses (`ActivePromptSet`), read at every HUD composition, so the HUD
  changes with the device in the hands and with a binding at the next frame.
- **The setting.** TopScreen owns it (`TopScreenUiConfig::HudButtonLabels`,
  `hud_button_labels` in `topscreen_ui.json`, written only when
  `original` so older builds still read the file): **Controller** (the
  default) or **Original 3DS**. F1 > Display > TopScreen HUD > **HUD button
  labels** is its control; the menu's Video > HUD > **Button labels** row
  mirrors it. Both apply and save at once.
- **Which letters.** Identified by their native owners, never by where they
  are drawn, so a UI texture pack (the Vanilla UI/HUD pack, the HD
  `--topscreen-ui-atlas-hd` atlas or a user pack) changes nothing
  (`oot3d_top_screen_button_labels.h`): X, Y and the horseback B are
  TopScreen's hub labels (owner 0x005C9940, touch quads 34, 35, 36 on layer
  13), ZR and ZL its touch labels (quads 0 and 1 on layer 13), B the native
  PauseTouchButton's B letter (owner 0x005D4A8C, copy 34). The A is baked
  into the native A ring of the HUD atlas (texels 78..89 x 1..11 of 256, the
  same on the pack's 16x art); the ring quad is read from the Quest render
  buffer after its transform, at the materialized hook (read only), and the
  letter's box mapped through its UVs.
- **How they are drawn.** In the UI domain, as part of the HUD's own
  primitive list: each glyph takes its label's place in the list (its layer
  and order, so an item icon drawn after a label still covers its glyph, as
  it covered the letter), centred on the letter's ink and one texel taller,
  with the label's colour and alpha, so it scales with the HUD size and fades
  and hides with the HUD. The A glyph is drawn after the HUD over the native
  ring and covers its letter (a glyph is opaque wherever it is drawn: a thin
  black edge around a white body). Black and white like the menu's glyphs
  (the white takes only the label's own dimming); the art is the same set
  (`oot3d_menu_button_glyphs.*`, atlas `triaevum/button_glyphs/<cell>`);
  each glyph samples the smallest atlas that
  is at least as tall as it is drawn (the UI renderer has no mipmaps), and a
  label uses the menu's 72 px heading font: crisp from 720p to 4K.
- **Off.** With Original 3DS the HUD list is not touched and no guest
  memory is read for it (with touch the ring is read, never written, and the
  list is not touched): frames are byte-identical to a build without
  the feature (measured on the Epona and Kokiri states, with and without the
  Vanilla UI pack, at HUD 100 % and 70 %).
- MM3D's HUD keeps its letters.

### Ocarina notes and pause hints

The same setting and the same binding-to-glyph rules cover the other 3DS
button letters TopScreen shows (owner request, 2026-10-27): the ocarina's
notes and the pause pages' hints. With Original 3DS, or with touch, nothing
of this runs (no list is touched, no guest memory is read for it).

| Letters | Identity (never a screen position) | How the glyph is drawn |
| --- | --- | --- |
| Ocarina song box notes (the songs browsed and the reference notes while the ocarina is out) | SingleScreen 3.0 `0x005D2078` quads 6..13; each carries its native note type as its entry in the note atlas table (`descriptor_address` = `0x0050A1E0` + 4 x type) | replaces the note in place (layer 20), 15/16 of the quad, the note's alpha |
| Native message staff notes (the notes played, a song taught or played back, "You played ...") | the message context's draw elements (`0x005C1878` + `0x568`, drawn by `0x0042CBCC` through `0x002F1280`) whose model is one of the five note models (`0x96` + type) of that window's model table (`+0x168`), with the draw's own gates (window state, element and model visibility) | over the native note, after the list (layer 24), centred on the element renderer's world matrix (`+0x0C`), 15.5 model units scaled by it, the renderer's colour and alpha (`+0xF0`); the native shadow stays beside it |
| Pause L / R tab hints | `0x005D4A0C` glyph quads 2 ("<-L", custom_ui01 rect 20) and 3 ("R->", rect 21) | the key and its arrow stay; the glyph follows the hint in the list, centred on the letter's ink (crop x 20..28.5 or 9..17.5, y 7..19.7), as tall as the ink box's diagonal so the disc covers the letter; the held hint's dimming |
| The item hint's SELECT | `0x005E0E6C` quad 0, custom_ui01 (277, 222, 64, 16); the "?" stays | replaces the word; ends where the word's ink ends, beside the "?"; the hint's pulse and fade |
| Legend discs (View Model, its controls line, the D-pad picker, the Items shortcut hold) | any custom_ui01 crop that is one of the mod's button discs: (46 / 81 / 116 / 151 / 186, 89, 32, 32) = A / B / X / Y / ZL (records `0x0060A2D4`, `0x0060A728`) | replaces the disc in place, 26/32 of the crop |
| Items page slot labels X, Y, I (ZR), II (ZL) | native Items page state `0x005066F8`: its label model (`+0x30`, drawn by `0x00434C80`), quads whose UVs are the X (67, 51) / Y (83, 51) 11 x 11 or I (63, 34) / II (80, 34) 18 x 13 letters of the pause parts atlas (the Vanilla UI pack draws "ZR" / "ZL" in the I / II crops), at the positions drawn (the model's position lane, as the Items cursor reads it), + 80 / 84 on the upper screen | the native label quad hidden for the draw (see below); the glyph at its place (layer 14), as tall as the crop's diagonal, with the page at rest (state 2, `+0x60` clear) |

The note types are the N64 ones; the 3DS plays them with L (A), R (C-down),
Y (C-right), X (C-left) and A (C-up), so each note shows the glyph of the
input bound to that 3DS button (the native song tables `0x004D541C` give
Zelda's Lullaby 3 4 2 = X A Y). I and II on the Items page are SingleScreen's
ZR and ZL item slots (`ResolveTopScreenItemQuery`: ZR uses Item I, ZL Item
II; the HUD shows the same items on ZR / ZL), so they show the glyphs of the
inputs bound to ZR and ZL. The D-pad in the legends is not a letter and
stays.

**Frame alignment.** The glyphs drawn over native art (the staff notes, the
Items labels, the A ring) are not read from the latest guest state at
composition: the guest submits a frame's command list on the update after
the one that built it, so the native frame presented lags the guest. At the
end of every guest update with native draws the window records what those
letters are (`TopScreenNativeLabelFrames`, keyed by the last draw submission
decoded), and when a top-screen transfer is selected for presentation the
record of the update that built that frame (the record before the first one
through the frame's last draw) becomes the presented one; the composition
draws those glyphs. Measured on the staff: a note dropping in now fades in
as a glyph on the same presented frames, at the same place and alpha, as
the native note (before: four presented frames earlier). With Original 3DS
nothing is recorded.

All of these are identified by native owner, quad, model or atlas crop, so
the Vanilla UI/HUD pack and the HD atlas change nothing, and they carry no
language: the pause pages' words (ITEMS / OGGETTI ...) are not touched.

Verification: deterministic rgb8 captures from the stone state (pause Items
with the SELECT hint, then the ocarina: the song box with Zelda's Lullaby and
the six notes played on the staff), without and with the Vanilla UI pack,
and an Italian boot to the Temple of Time (Equip. / Mappa / Oggetti, "Ninna
nanna di Zelda"): Xbox, PlayStation, Nintendo and Keyboard each show their
glyphs on every family above. Original 3DS frames are byte-identical to the
build before this change (native art and Italian state runs; with the pack,
identical to a baseline run whose pack had loaded, since the pack's own load
timing varies between runs of either build).

Transitions, frame by frame (every presented frame, stone state: pause open,
page R and L, pause close, ocarina out, the song played, notes dropping on
the staff; `compare_ocarina_pause.jpg` has a strip): the staff's notes are
covered on every frame (an automated check finds no frame among 106 where a
native note's pixels are left unchanged under the glyph run).

**Hide what is replaced (Items slot labels).** The Items page's labels are
separate quads, so the native letter is not only covered but hidden: at the
Items page draw call (`0x0041EE74`, a catalogued product site, SingleScreen's
Items sequence), `TopScreenWideItemsRuntime` clears the vertex alpha of the
label quads that get a glyph (X, Y, I = ZR, II = ZL, by their UV crops;
`SetHiddenItemsLabels` from the window, from the bindings of the device in
use) and writes the native colours back at the next draw call, before a
save state is written, or as soon as no label is replaced (Original 3DS,
touch, an unbound button). Nothing native rebuilds those colours, so the
draws decoded in between never see the letters. A label without a glyph
keeps its letter. The native letter therefore never shows: not at rest
(the glyph takes its place), not while the page slides in or out on pause
open / close or on an R / L page change (the page's labels are hidden then
too). Hook counter (`TRIAEVUM_BUTTON_LABEL_TRACE=1`, one line per Items
draw): over every transition of the capture run, 122 Items draws hid 488
label quads and left 0 with alpha.

Where a glyph only covers native art, the letter is part of a larger
element and hiding it would remove more than the letter: the A ring (its
letter is baked into the ring's texture), the staff notes (letter and disc
are one note model) and the L / R tab hints (one crop holds the key, the
arrow and the letter). The SELECT word and the legend discs are TopScreen's
own primitives and are replaced in place, so no native copy exists.

A frame whose submitting update already finds the page away from rest
(the slide starts there) draws no label glyph either, so a glyph never
stays at the rest position over a moving page.

Open: while a page slides, the label spots show neither letter nor glyph:
the glyph is drawn only with the page at rest, because the label positions
read do not carry the slide (the slide is drawn by the pause shell's page
transition; the Items state is already closed when the page slides out).
A glyph that follows the slide needs that transition's transform.

The SingleScreen Items page's native "BACK" key (with its B) is not drawn on
the pages TopScreen shows and is not covered.

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
  re-presents the last scanout), with the menu drawn over it. Effects that
  keep their own time instead of the game's (the grass wind and bending)
  hold too: the host sets `VisualClock::SetHostHold` in the frame's render
  order, so the held frame does not move.
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
Graphics change makes it Custom, as in F1. Graphics > Textures > **Custom
textures** (UX plan WP-07) is `GraphicsSettings::TexturePacks.Azahar.
LoadCustomTextures`, the same owner and value as F1 > Materials > Texture
packs > "Load custom textures"; presets preserve texture packs, so the row
keeps the preset. The rows come from `oot3d_menu_textures.h` (tested in
`oot3d_menu_tests`); Forge > Enhancements and F1's Import... put a pack in the
managed folder.

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

A sixth lab run set (2026-10-26, UX plan WP-06, developer build, stone save
state, 4K window, lab driven): the menu opened at guest frame 184158 and the
guest frame stayed 184158 for 155 presented frames (waits, captures, tab
changes, Reset display settings and its keep question) and moved on only
after the menu closed (184164, then 184195 thirty frames later). Two captures
of the held frame thirty presented frames apart differ in 0.37 % of the
pixels by one level, the renderer's run-to-run noise (before the grass hold
the wind moved 28 % of them by up to 95 levels). With `TRIAEVUM_HELP_CARD=1`
the card was up alone (`cardOnly`), the game held, OK closed it and wrote
`Help: {Shown: true}`; a second launch on that file showed no card. Game >
Quit game > Quit after a non-persisted lab graphics change closed the game
by itself (exit 0, `TRIAEVUM_MENU_QUIT`), wrote the pending change
(`FovMultiplier` 1.2) and left no `config.json.launching`. Inertness with the
menu closed: the same deterministic runs (stone with the guest pause screen
by input timeline, Epona without one; rgb8) on the build before WP-06 and
after: 10 of 12 frames byte-identical, 2 differ by one level in a small
region, as a repeat of the new build against itself does. `oot3d_menu_tests`
covers the card, the notice modal, Resume, the Help tab, the quit question
and the status vocabulary. Not verified here: a real pad or touch session
(the lab injects the host controls), "Open saves / logs folder" opening a
file browser (unit-tested through the host action only), and the default
window fit on a small screen (unit-tested; scripted runs keep the stored
size).

Not done yet: menu sounds (the host has no sound asset of its own) and a
5:3 canvas for the native TopScreen layout (the menu always draws on the
16:9 canvas, which a wider or narrower picture fits and centres).
