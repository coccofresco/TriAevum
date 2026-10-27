# F1 settings rationalization

## Texture pack Browse and Import (2026-10-26)

UX plan WP-07: Materials > Texture packs keeps its folder fields and gains
**Browse...** beside "Custom texture folder" and "Texture dump folder" (the
chosen folder fills the field and applies like "Apply folders") and
**Import...**, which copies a chosen pack into the managed load folder Forge's
launch names (`AzaharTextureHostDefaults::LoadDirectory`,
`D/textures/load/<title id>`), following the rules Forge's import shares
(`fast/extension/azahar_texture_pack_import.{h,cpp}`: title-folder detection,
PNG/JSON checks, limits, links refused, staging and swap, the current pack
kept on any failure). After an import, "Load custom textures" is turned on
and the explicit custom folder cleared through the graphics settings owner
(`DrawTextureSettings` applies the panel's change) and the pack is reloaded;
a pack in the managed folder now wins over one in Azahar's own folder. The
folder dialog (Windows `IFileOpenDialog` on a COM STA worker; Linux zenity,
then kdialog) and the copy run on a worker thread; the panel shows "Choosing
a folder...", "Importing...", "Imported N textures into <folder>" or "Import
refused: <reason>". Without a managed folder Import... is disabled with "Start
the game from Forge to use the managed texture folder"; on Android both
buttons are disabled with the reason. The in-game menu mirrors "Load custom
textures" as Graphics > Textures > Custom textures (same owner).

Verification: the actual-widget smoke (`CheckTexturePackBrowseAndImport`)
injects the folder chooser (`SetAzaharTextureFolderPicker`, no native
dialog): Import... disabled without a managed folder; Browse... fills and
applies both folders, a cancelled Browse... changes nothing; Import... of
`textures/<title id>/tex1_32x32_..._13_mip0.png` ends with the file in the
managed folder, custom textures on, the explicit folder cleared and the
runtime loading from the managed folder; an invalid pack (a `.png` without the
PNG signature) is refused with its reason and leaves the imported pack.
Worker waits count as one assertion each, so the total is stable.
Actual-widget smoke: **11,303 assertions** on this tree. The import rules have
unit tests in `oot3d_graphics_foundation_tests` (`AzaharTexturePackImport*`,
including a junction on Windows) and the folder precedence in
`HostDefaultsRankBelowEveryChoiceAndKeepAnInstalledPack`.

## Audio (2026-10-26)

UX plan WP-06C ([audio settings](TRIAEVUM_AUDIO_SETTINGS.md)): **Game >
Audio**, a section beside Save states and Language, is the one F1 surface
of the host's audio output settings: "Master volume" (0..100 %, heard while
dragged, saved once on release), "Mute" (keeps the volume, disables the
slider) and, on desktop, "Mute when the window loses focus". The owner is
`Ship::AudioOutputPolicy` in the host audio service; the section keeps no
draft and resolves the owner when drawn (F1's tabs exist before the host
context). Status: "Audio: saved", "Audio: session only", or the save error
with "Retry saving audio". There are no separate music / effects volumes: the
game's DSP mixer has no verified categories.

Verification: the actual-widget smoke (`CheckAudioPanel`) types and drags
the volume (no write during the drag, one on release), mutes and unmutes,
turns the focus mute on and off, shows a failed save and retries it, and
checks session only, a missing owner and the Android variant without the
focus mute. Actual-widget smoke: **11,192 assertions** on this tree.

## Button prompts (2026-10-26)

UX plan WP-08 (menu part; `TRIAEVUM_IN_GAME_MENU.md` "Button prompts" has
the rest): Controls > Devices gains **Button prompts** (Automatic, Xbox,
PlayStation, Nintendo, Steam Deck, Generic, Keyboard, Touch), below "Capture
mouse during gameplay", with "Now: <family>" beside Automatic (what the
controls owner resolves from the device used last). It is the controls
owner's setting (`NativeControlConfig::ButtonPrompts`, `button_prompts` in
`oot3d_controls.json`, written only when fixed), saved by Save controls,
mirrored by the in-game menu's Controls > Button prompts row. It is not a
binding: choosing it never makes the preset Custom, and applying a preset
keeps it.

Verification: the actual-widget smoke selects PlayStation (the owner's
choice and the resolved family follow), Automatic (the keyboard used last),
and Steam Deck in the persistence fixture (the preset stays, Save controls
writes it). Actual-widget smoke: **11,548 assertions**.

## Unsaved preview marker, camera names (2026-10-26)

UX plan WP-06B (`TRIAEVUM_IN_GAME_MENU.md` has the menu side):

- **"Preview, not saved"** (#68): scene light edits show at once but are
  written only by their Save. While any is unsaved, the Lighting tab and its
  Lights & Shadows section carry ImGui's unsaved-document dot, Lights &
  Shadows opens with the "Preview, not saved" line, and a line above the tabs
  names it from every other tab ("Lighting > Lights & Shadows: Preview, not
  saved. Save the scene lights there to keep the edits."). A tab reports its
  state through `GraphicsSettingsPanelTab::HasUnsavedPreview()` (the scene
  lights' panel: its runtime's dirty drafts); saving or reverting clears the
  marker.
- Controls > Camera's "Free camera" section is now **Gameplay camera**
  ("Gameplay camera source", "Gameplay camera mouse sensitivity"), and Photo
  mode's switch is **Photo camera (F6)** with "Photo camera speed", the same
  names as the in-game menu. Owners and configuration keys are unchanged.

Verification: the actual-widget smoke covers the marker (edit the scene
lights, leave for Ambient Occlusion: marked; back in Lights & Shadows:
marked; Save: cleared) and the renamed widgets.

## Aspect ratio (2026-10-20)

Display > Screen gains **Aspect ratio** (Original 3DS 5:3, 16:9 default,
21:9, Free) under Window mode, with the same row in the in-game menu's Video
tab. It is host-owned like the window mode and the output size
(`Graphics.Output.Aspect`; presets keep it), so choosing it never makes the
preset Custom. A fixed ratio is the presentation picture: the largest
rectangle of that shape in the window or the screen, centred, black bars on
the remaining sides (`fast/extension/presentation_picture.h`); Free is the whole
output as before. Choosing a fixed ratio for a windowed output also resizes
the window to that shape once (width kept, height follows), so no bars show
until the user resizes it; the window stays resizable and any other shape is
letterboxed, SDL having no aspect lock. The observed-state lines show the
picture when it differs from the output framebuffer. The renderer sizes the
scene targets to the picture and clears the bars; the scene FOV policy, every
UI canvas, the in-game menu and the mouse/touch mapping use the picture, so
the HUD and the menu stay centred and clicks land where they are drawn.

Verification (developer build): actual-widget smoke **11,161 assertions**;
dev lab menu run with captures at 21:9 (85 / 86 px bars in a 720p window),
5:3 (40 px pillars) and Free, see `TRIAEVUM_IN_GAME_MENU.md`.

## Photo mode (2026-10-20)

A top-level **Photo mode** tab, in every build (issue #102 asked for a way to
hide the HUD while recording):

- **Free camera (F6)**: the camera detached from Link, formerly Dev Tools >
  Free camera (developer builds only). Speed and "Back to the game's view" as
  before; Dev Tools keeps only warping, so the camera has one surface. WASD /
  Q / E move, Shift / Ctrl change speed, the pad's sticks and shoulders drive
  it, and dragging with the left or right mouse button looks around.
  Looking with the mouse never worked before: the condition was
  `!io.WantCaptureMouse`, and the full-window "Main Game" ImGui surface sets
  that flag all the time. The input poll now reports a held button only while
  no host menu or window is open (`DevMouseLookHeld`), as the game's own mouse
  routing does. Left and right were also mirrored: the game's look-at takes
  right as up x forward, so facing +Z the right is +X and a growing yaw
  turns right (as the scripted tour already assumed); strafing, mouse and
  right-stick turning now follow the screen.
- **Hide HUD**, beside the free camera: a session state (`PhotoModeState`),
  never saved, off at every launch. The UI bridge then sees TopScreen
  `render_hud` off (`PublishTopScreenUiConfig`; the saved configuration keeps
  its value), and the game's own gameplay HUD draws (UI view 0x0042B9F4, the
  action icon on the A button and the minimap among them), which the
  TopScreen compositor does not draw, are left out of presentations
  (`Oot3dPicaPresentationScheduler::SetNativeGameplayHudVisible`,
  `OmittedHudDraws`). A saved `render_hud` off does the same. Native-memory
  execution keeps every draw; dialogue, menus and the pause screen are other
  UI views and stay.

Verification (developer build, field checkpoint, 1280x720; the product
`release-build` builds and starts): with `render_hud` off no HUD element is
left, A button and minimap included; the Items pause screen opens and closes
normally. Actual-widget smoke **11,161 assertions** (`CheckPhotoModePanel`:
the camera switch, speed and reset, Hide HUD beside it, off at start, on and
off). Scheduler test: the HUD draw is left out only for presentations with
the HUD hidden, the transfer anchored on it still runs.

## TopScreen standard profile (2026-10-17)

The HUD tab is gone. TriAevum's standard TopScreen profile (the defaults of
`TopScreenUiConfig` and `config/topscreen_ui.example.json`) fixes the HUD
layout: HUD 0.70, margins 4/1, magic bar Y 2, D-pad Y -1, Normal layout, and
every SingleScreen gameplay option on, 3D rain included. For the layout F1
offers only **Display > TopScreen HUD > HUD size**, Native (100%) or Optimized
(70%, the default), with the SingleScreen credit below it; the TopScreen
sections of Controls (Shortcuts, Gameplay, C-stick aiming, free camera) stay. The choice applies and saves at once
(`TopScreen: saved`, or the save error with the size kept for the session;
`TopScreen: session only` without a configuration path). The section is an
application tab in the new `kDisplaySettingsGroup`, drawn below the renderer's
Display sections; TopScreenUiConfigRuntime still owns the setting.

Forge keeps every installation on the standard layout: on each install or
update it rewrites the layout fields of an existing v2 `topscreen_ui.json`
(`TOPSCREEN_STANDARD_LAYOUT_FIELDS`) and resets a HUD size outside the two F1
sizes; Controls fields, the minimap toggle and the Items order stay the user's.
Developer configuration files may still set any field.

Actual-widget smoke: **10,934 assertions**, including the absence of the HUD
tab and of the layout controls, both sizes, an external change surviving a size
choice, the immediate save and a visible failed save.

## TopScreen HUD button labels (2026-10-27, UX decision 7)

**Display > TopScreen HUD > HUD button labels**, below the HUD size:
**Controller** (the default: the HUD's A, B, X, Y, ZL and ZR letters show the
glyph of the input bound to each, in the prompt family of the device in use)
or **Original 3DS** (the letters). TopScreenUiConfigRuntime owns it
(`hud_button_labels`, written only when `original`); it applies and saves at
once like the size, and the in-game menu's Video > HUD > Button labels row
mirrors it. The bindings and the family stay Controls' (Bindings, Devices >
Button prompts); the HUD reads them, it has no control of its own for them.
Design, rules and captures: [In-game menu, HUD button labels](TRIAEVUM_IN_GAME_MENU.md#hud-button-labels).

The same control also covers the ocarina's note letters (the song box and the
message staff) and the pause pages' hints (L / R tabs, SELECT, the legends'
A / B / X / Y discs, the Items page's X / Y slot labels) since 2026-10-27:
one owner, one control; only its tooltips name the wider scope. Actual-widget
smoke after that change: 11,956 assertions (full run, unchanged control).

Actual-widget smoke: **11,956 assertions** (full run), including the control
below the HUD size, Controller as the default, Original 3DS applied alone (the
size and an external margin kept) and Controller again.

## Grass masks (2026-10-11, round 36)

Grass > Masks identifies current overlay/source overlaps with thumbnails,
catalog badges, estimated coverage and an optional world wireframe. Automatic,
Custom and Off persist beside Sources; Custom reuses the source mask editor
and adds strength. Manual add, removal, reset, capability-safe Off, explicit
save failure/retry, and F1/tab/F2 highlight cleanup are exercised by
`triaevum_f1_settings_smoke --grass-masks` (489 assertions). The full smoke
still stops at the previously documented Azahar-identity assertion.
See [overlay masks](TRIAEVUM_GRASS_OVERLAY_MASKS.md) for the persistence,
presentation boundary, diagnosis numbers and outstanding native-alpha lab check.

## Group environment standards (2026-10-11, round 34)

**Lighting > Lights & Shadows > Scene lights** shows the authored scene group:
Indoor, Outdoor time flows, Outdoor time at entry, or Outdoor fixed time.
The first readable installed room fixes the group for the visit, including
mixed dungeons. Unknown groups retain the previous fallback behavior.

Each slot shows **Scene's own**, **Group standard**, **Base**, or (for a night
without either night file) **Day result**, plus the selected source filename.
Day resolves scene -> group -> Base; night resolves scene -> group -> day.
An own day file therefore no longer prevents inheriting the group's night.
The existing blend and independent drafts remain; fixed-time and other
non-clock-driven scenes still expose one profile.

**Save as group standard** confirms replacing the group's current slot and
explains that scene-owned files keep precedence. **Use group standard**
confirms deleting the scene's current-slot file and discarding custom values
and unsaved edits. Ordinary Save still writes only the scene's file. Both
actions disable for unknown groups and surface persistence errors. An open
confirmation disables its commit when the scene or editor revision changes;
its popup ID is outside the day/night tabs, so Cancel remains usable across
a tab change. Capability-safe Off controls are retained.

The actual-widget fixture `triaevum_f1_settings_smoke --scene-environment`
passes **2,660 assertions**, including group labels/provenance, save/use
cancel and confirm, scene override creation, shared night standards, stale
confirmation after a scene/tab change, and visible group-write failure.
`triaevum_environment_tests` passes **655,553 checks**; native room/header
tests pass, including all four classes, selected headers, capture offsets,
unknown/mixed behavior and directory/Level-3/IVFC inventory equivalence.
TriAevum, the environment tool and all touched test targets build.

The full F1 smoke was also run and still stops at the existing unrelated
material-editor assertion: "creation used the catalog hash instead of the
native texture's Azahar identity". No game was run for round 34. See
[environment tooling](TRIAEVUM_SCENE_ENVIRONMENT_TOOLING.md) for the native
addresses, group file schema, hot reload and offline `list-groups` command.

## Day/night profiles (2026-10-10, round 32)

**Lighting > Lights & Shadows > Scene lights** now separates the editor from
playback: **Day / Night** tabs edit independent drafts at any game time, while
**Auto / Force day / Force night** controls the preview. The live percentage
indicator reports the published blend. F2 and closing F1 restore Auto;
preview is never saved. Fixed-lighting scenes have one profile and no
clock-related controls. This remains one `SceneEnvironmentRuntime` owner.

Create night copies day to the new file. Copy day -> night confirms replacing
the night draft, then Save night persists it. Remove night confirms deleting
its file and draft. Each tab saves/reverts separately; time changes retain
both unsaved drafts. The existing two-file schema remains compatible.

Focused actual-widget smoke passes **2,364 assertions**, including editing
Night by day, both dirty drafts across native time changes, preview/percentage
agreement, copy cancel/confirm/save, removal cancel/confirm, per-tab revert,
F2/close reset and absence of day/night controls for fixed scenes. The full
suite still stops earlier at the pre-existing material-editor assertion
"creation used the catalog hash instead of the native texture's Azahar
identity" (also recorded in round 28). The native curve and cache simulation
are documented in [Scene environment profiles](TRIAEVUM_SCENE_ENVIRONMENT_TOOLING.md).

## Interior sun / offline (2026-10-10, round 29)

The base default remains JSON `shadowCasterFaces: "interiorShells"`. The actual
F1 choice now reads **Interior sun / offline** and explains authored interiors,
offline preparation, inner occluders and the two-sided fallback. This remains
owned by `SceneEnvironmentRuntime`; no duplicate graphics setting was added.
Focused actual-widget smoke passes **1,978 assertions**, including selection
of the new label. See [the builder and Forge contract](ROOM_SHADOW_SHELLS.md).

## Independent rim light (2026-10-10, round 28)

**Lighting > Lights & Shadows > Scene lights > Rim light** is owned solely by
`SceneEnvironmentRuntime`, persisted as the optional scene-profile `rimLight`.
Enable, source, intensity, sharpness, RGB color, camera-relative azimuth,
coverage and Actors / Actors and world preview and save through that owner.
Independent is the default: warm white, intensity 1, sharpness 4, top (0 degrees),
coverage .25, actors. Scene lights retains the previous direct-light response;
it disables color/azimuth/coverage without erasing them. The ambient control
was removed; its legacy JSON member is accepted but ignored.

The actual-widget fixture edits each control (including all three ColorEdit3
components), switches both sources and scopes, checks disabled controls and
Off stability, the 0..8 intensity range, renderer publication, save/reload,
native/F2 bypass and scene/day/night transitions. Profiles lacking the new
fields default to Independent; a missing whole object remains off. Modern-only
and capability gating are unchanged. See [RIM_LIGHT.md](RIM_LIGHT.md) for the
formulation, migration and game lab.

Verification: `TriAevum.exe` and touched tests build with `build-host.cmd`.
Focused `triaevum_f1_settings_smoke --scene-environment`: **1,978 assertions**.
Environment owner: **92 checks**; pipeline: **54/54**; foundation: **352/352**;
RTX 3060 GPU equations: **121 cases**, with both complete PBR shader variants
compiled. The full F1 suite still stops before this panel at the unchanged
material-editor assertion "creation used the catalog hash instead of the
native texture's Azahar identity". No game was run.

## Extended draw distance and presented actor culling (2026-10-09)

**Display > Extended draw distance** (off by default, off in Authentic) turns
on Ship of Harkinian's actor draw distance at 5x, the native foliage groups'
5x spawn ranges and the end of the Kokiri fade, as one choice. Distant actors
are also updated, as in SoH, so gameplay can differ from the title's. SoH's
LOD and glitch-actor options are not carried over. The setting persists as
the optional `Graphics.Camera.ExtendedDrawDistance`; older documents keep the
native ranges.

Actor culling now always matches the presented view. The title culls actors
with its own view-projection (play+0x5BB4), built without the frustum hook, so
at 16:9 and a wider FOV actors at the margins of the picture were culled as if
the screen were 5:3. The culling decision is re-evaluated with the presented
view's extent, derived from that matrix through `ResolvePerspectiveFov`; it
has no control.

Owner: `GraphicsSettings::ExtendedDrawDistance` (Display), snapshotted each
host frame into the title adapter's `NativeActorVisibilityState`; the pure
policy is `oot3d_actor_visibility_policy.{h,cpp}` and the EUR hooks
`oot3d_native_a32_actor_visibility.{h,cpp}`, all at existing A32 block
entries:

- `0x00461904` Actor_UpdateCulling entry (`r0` = play): the presented
  volume is resolved once per walk from the live play state.
- `0x00461AC4` Actor_UpdateCulling result: an extended pass sets the title's
  own pass (state 0, `r0 = 1`), so the native fade and update flag follow;
  a failure inside the extended depth becomes "outside", not "far". En_Viewer
  keeps the native test.
- `0x003BC5DC`/`0x003CC298` Obj_Mure: its distance table and retirement
  margin are written (5x or native) before each use, host-owned, known EUR
  values only.
- `0x003B30AC`/`0x003AEF48` Obj_Mure2: the distance scalar the update just
  stored becomes at least 25 (5x on squared distances) and sees the presented
  width.
- `0x0034E854` En_Wood02 offspring: extended, spawn in the extended volume.
- `0x001B5FE0`/`0x001B60E8` En_Ko: extended, the fade distance reads 0.
- `0x00341CD8` Actor_UpdateAlphaByDistance, Mido's and Saria's fade (as in
  SoH's Kokiri option): extended, their distance reads 0, so they fade in
  with the title's smoothing; Mido's house and Saria's cutscene stay native.

Verification (developer build): actual-widget smoke **9,400 assertions**
(toggle applies and saves once, and restores); policy tests; foundation
352/352 with the persistence round trip and the Authentic reset;
deterministic walks from a Kokiri Forest savestate, off and on: on shows the
distant Kokiri and otherwise the same frames; facing the Lost Woods tunnel,
Mido is visible at once instead of when Link gets close.
## Title-owned capabilities (2026-10-07)

The window title comes from the game frontend
(`InstallGraphicsSettingsWindowTitle`, before the GUI is created; the
ImGui identity stays `OOT3D Graphics`, so saved layouts carry over). Two
capabilities belong to the title, not the renderer, and the frontend sets
them from its adapter: `TitleSceneView` (the title publishes its scene view)
and `TitleSceneFov` (the title applies the global scene FOV). Without a
scene view F1 shows, above the preset, which effects stay inactive and that
their settings are kept; without FOV hooks **Global scene FOV** is disabled
and shows the reason, keeping the stored value. OoT3D sets both; Majora's
Mask 3D applies the scene FOV (its frustum hook) but publishes no scene view
yet. Actual-widget smoke (developer build of the MM3D
fork): 8,444 assertions, including both capabilities lost and restored.

## User graphics presets (2026-10-07)

**Graphics preset** lists the built-in presets (Authentic, Enhanced, Toon),
then **Your presets**, then Custom; choosing a user preset loads it. **Save or
delete presets** (below the selector) saves the current settings under a new
name, or **Overwrite preset** when the name is one of the user's, and deletes
the selected user preset after **Confirm delete** (the current settings stay as
they are). Built-in presets cannot be changed: their names, and Custom's, are
refused whatever the case or padding, and they are never in the user library.
Any edit leaves a loaded user preset, which the selector then shows as Custom;
the selector shows a user preset whenever the settings equal it.

A user preset holds what a built-in preset sets. Loading it keeps window,
output, VSync, texture packs, grass sources, reflection materials and the
saved grass preset (`GraphicsSettingsService::KeepHostOwnedSettings`, now
shared with the built-in presets), and it applies as Custom, never as
Authentic's native fidelity profile.

Owner: `GraphicsPresetLibrary` (`graphics_preset_library.{h,cpp}`), held by
`GraphicsSettingsRuntime` and written at once through the persistence port's
new `StoreGraphicsPresets` into the top-level `GraphicsPresets` config block
(schema 1; each entry a name and a `Graphics` document without the host-owned
state). A failed write is reported and leaves the presets unchanged; with
persistence suppressed (developer environment overrides) the action holds for
the session and says so. Invalid or duplicate entries are skipped and logged;
a preset saved by a newer settings schema is kept as it is and not offered,
and a newer library schema makes the library read-only.

Verification (developer build): actual-widget smoke **9,045 assertions**
(built-in name refused, save, edit, load, overwrite, failed write, delete only
after confirmation); foundation tests 340/340 including the runtime save, load,
delete and failure paths and the library round trip through the config store's
flattening; a game start on a config with a `GraphicsPresets` block loads the
valid preset without warnings and skips one named "Toon".

## Enhanced default, Lights & Shadows and credits (2026-10-06)

**Enhanced** is the maintainer's current play configuration (captured
2026-10-06 in `EnhancedPresetDocument()`): Motion and camera (Interpolated
2x, scene FOV 1.10x), the Modern lighting model, virtual shadow maps and
point-light cube shadows, MXAO (solid angle, face normals, temporal) and the
custom grass profile; the other renderer settings are the same configuration's
(antialiasing Off, GI, reflections and Toon off). Presets now set Motion and
camera too: choosing one sets the frame rate and the scene FOV, and still keeps
window, output, VSync, texture packs, grass sources, reflection materials and
the saved grass preset. A new installation starts from Enhanced:
`--product-info` exports the preset with the title's grass source rules
(`triaevum_product_graphics.inc` now holds only those), and Forge's contract
requires `"Preset": "Enhanced"`. The HUD default is unchanged and equals the
maintainer's current HUD (`config/topscreen_ui.example.json`); presets never
change the HUD.

**Lighting** sections: **Lighting & GI**, **Lights & Shadows** (Modern only),
**Ambient Occlusion** (formerly Occlusion), **Reflections**, **Toon**. Lights &
Shadows shows the scene lights (formerly the Scene environment tab, owned by
`SceneEnvironmentRuntime`, saved per scene) above the directional and point
shadows (owned by `GraphicsSettingsRuntime`, every scene). Both act only in
the Modern model, so the section is absent with Native. A scene without its
own profile takes the **base environment**, `BaseSceneEnvironmentProfile()`:
the title intro's tuned profile (scene 0x6B). **Save scene environment** stores
the edited lights for the current scene only.

Credits: **HUD** thanks M-1, author of the TopScreen mod; **Ambient
Occlusion** closes with MXAO's credit (Pascal Gilcher, iMMERSE), its reserved
rights and the licence exception (permission granted to TriAevum only, outside
TriAevum's GPL). Builds without the iMMERSE module show no MXAO credit.

Verification (developer build): actual-widget smoke **8,810 assertions** (the
section order with Native and with Modern, absence of the former sections, the
base environment on a scene without a profile and after a transition, the
credits); foundation tests 337/337; release tests including the compiled
`--product-info` export. Game check from the Hyrule Field checkpoint with the
play configuration: without a scene profile the frames are byte-identical to
the same run with an explicit copy of the intro profile, and differ from the
environment-off run.

## Two lighting models (2026-10-06)

**Lighting > Lighting & GI > Lighting model** offers **Native** and **Modern**.
Native is the former Native + PBR: original native shading, with PBR only on
supported pack materials (identical to plain native shading without a material
pack). Modern is the former Per-pixel + PBR (materials without a pack use
per-pixel lighting). Validation migrates saved choices: Per-pixel becomes
Modern and plain Native becomes Native (NativePbr). Plain native shading stays
internal: it is the capability fallback when the renderer cannot relight (the
Native choice then selects it), the Authentic preset and the F2 override.
Panel text refers to "the Modern lighting model" where it named the per-pixel
models. Actual-widget smoke: **8,726 assertions** (only the two choices are
offered, migration, Modern disabled after capability loss); foundation tests
337/337 with the migration case.

## Tab rationalization (2026-10-06)

The F1 window has seven top-level tabs grouped by purpose, instead of nine. No
option was added, removed or given another owner. The **Graphics preset**
selector (Authentic / Enhanced / Toon / Custom) and its last-change status sit
above the tabs because the preset covers every graphics tab.

| Tab | Sections | Former location |
| --- | --- | --- |
| **Display** | Screen (window mode, resolution, internal render scale, VSync); Antialiasing and upscaling; Motion and camera (frame rate, scene FOV); TopScreen HUD size (TopScreen profile only, since 2026-10-17) | Renderer > Display, Renderer > Antialiasing, HUD |
| **Lighting** | **Lighting & GI** (lighting model, global illumination); **Shadows** (directional, point light); **Occlusion**; **Reflections** (provider and parameters); **Scene environment** (per-scene profile, Navi/fairy lights); **Toon** | Renderer > Lighting, Environment, Renderer > Reflections, Renderer > Toon |
| **Grass** | unchanged | Grass |
| **Materials** | **Texture packs**; **Material editor**; **Reflection materials** (texture assignments) | Textures, Material editor, Renderer > Reflections |
| **Controls** | unchanged | Controls |
| **HUD** | removed 2026-10-17: the standard profile fixes the layout | TopScreen 2.1.1 |
| **Game** | **Save states**; **Language** | Save states, Game |

Application tabs declare a group (`GraphicsSettingsPanelTab::Group`): the
scene environment joins the built-in Lighting tab and Save states and Language
form the Game tab; other application tabs stay top-level in installation order.
The shadow controls moved from the application's Environment panel to
`graphics_settings_shadows_panel.cpp` because `GraphicsSettingsRuntime` owns
them; they now apply through the same coalesced path as the other renderer
pages. Cross-references in panel text name the new locations.

Actual-widget smoke (developer build): **8,750 assertions**, including the
top-level order, the Lighting section order, the Materials sections and the
absence of the former Renderer, Textures, Environment and TopScreen 2.1.1 tabs;
every earlier control interaction runs at its new location.

2026-10-05 **Save states** (application tab, `oot3d_savestate_panel.cpp`): the
F5/Shift+F5 and F8/Shift+F8 actions as buttons, the save folder (default
`savestates/` beside the executable, in-game browser, **Use default folder**),
the folder listing and **Choose a state file...** that pick the state F8 loads,
and the last outcome. The tab owns the folder and the last state
(`savestates.json` beside the configuration); the settings a state carries are
applied through their own owners. Actual-widget smoke: **8,639 assertions**,
including every button, the listing, both browsers, the default folder and a
visible failure. Contract and limits:
[`TRIAEVUM_SAVESTATES.md`](TRIAEVUM_SAVESTATES.md).

Ambient occlusion lives in **Lighting** as
**Off / Multiscale GTAO / FidelityFX CACAO**. GTAO exposes independent
**Fine radius / Fine strength** and **Broad radius / Broad strength**;
fine radius cannot exceed broad radius. Original CACAO exposes radius,
strength, detail strength, horizon-angle threshold and blur passes.
Quality remains **Low / Medium**. Both providers require native depth,
normals and scene-view metadata; Off remains usable without those capabilities.

Schema 14 preserves saved CACAO, GTAO and Off selections and their numeric
settings. Missing fine radius defaults to one quarter of broad radius, but
CACAO is no longer converted to GTAO. The actual-widget fixture passes
**6,650 assertions** in the combined source qualification, including both providers, original CACAO controls,
independent fairy controls, capability loss, persistence and Native/F2 restoration.
Fresh runtime evidence and limits are recorded in
[`TRIAEVUM_RUNTIME_PROGRESS_QUALIFICATION.md`](TRIAEVUM_RUNTIME_PROGRESS_QUALIFICATION.md).
The earlier GTAO runtime verification also passed six live Low/Medium/Off transitions with Hi-Z/TAA,
SSSR coexistence and an Off run with zero AO/normal-guide passes; all three
report zero Vulkan/NRI validation errors. Native captures distinguish fine and
broad contributions and preserve the original framebuffer with AO disabled.
See the [GTAO architecture and measurements](OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md#multiscale-gtao-and-reflection-performance).

Directional shadows now live only in **Environment**, above the per-scene
profile controls, and offer **Off / Virtual Shadow Maps**. They are active only
with **Per-pixel** or **Per-pixel + PBR** selected in Lighting. **Native** and
**Native + PBR** disable VSM with an explicit reason but keep Off usable; switching
lighting or using F2 does not erase the saved preference.
F2 temporarily forces **Native** from every lighting model, including both
Per-pixel choices; the next toggle restores the configured choice. The actual
window/framebuffer verification is recorded in
[`TRIAEVUM_F2_NATIVE_PRESENTATION.md`](TRIAEVUM_F2_NATIVE_PRESENTATION.md).

The global controls are **Shadow atlas budget** (2048/32 MiB or 4096/128 MiB,
including the static depth cache and composed atlas),
distance (50Ã¢â‚¬â€œ65,536 native world units), strength (0Ã¢â‚¬â€œ4x), PCF radius (0Ã¢â‚¬â€œ2),
and advanced constant/slope bias. Stabilization is inherent to world-fixed
virtual pages, not a toggle. These settings save automatically and are not
part of **Save scene environment**. Schema 12 migrates the old single-cascade
choice and removes its depth-padding/stabilization fields. Actual-widget smoke
passes **6,650 assertions**, covering gating, persistence, capability-safe Off,
the two-atlas memory budget and F2 lighting restoration.
The NRI architecture document records GPU/camera evidence.

2026-09-23 amplified directional shadows: Lighting **Shadow strength** now
accepts **0Ã¢â‚¬â€œ4x**, including persistence, cascade planning and GPU receiver
evaluation. Above 1x amplifies partial occlusion; visibility saturates at zero
instead of producing negative light. The tooltip describes that limit.
The real-widget fixture passes **4,593 assertions**, including live 4x/zero
and capability-safe Off. Deterministic Kokiri captures verify distinct 0/1/4x
output while preserving native-dependency color and alpha/depth/stencil.
Existing values and the separate Toon shadow-tint control are unchanged.

2026-09-23 surface lighting: Lighting owns the persistent **Lighting model**
selector (**Native**, **Per-pixel**, **Per-pixel + PBR**, **Native + PBR**).
Native stays available
when the renderer capability is missing; unsupported choices are disabled and
capability loss normalizes the active mode. Texture packs retains the sole
folder/load/reload controls, now with material counts, pending indexing and
explicit asynchronous errors. Actual-widget smoke passes 4,083 assertions,
including all three selections and capability loss. The Kokiri GPU captures
and material format are documented in
[`OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md`](OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md#native-per-pixel-lighting-and-material-packs-2026-09-23).

2026-09-23 live PBR controls (historical; superseded by the material editor below):
Lighting then owned **Normal strength**
(0Ã¢â‚¬â€œ8Ãƒâ€”), **Glossiness strength** (0Ã¢â‚¬â€œ2Ãƒâ€”), **Material view** (Shaded / Surface
normals / Roughness), and **Reset PBR controls**, shared by both PBR models.
Both strengths default to 1; zero disables mapped-normal perturbation or makes
the material matte, respectively. Changes persist and update live uniforms
without reloading textures. Reflection intensity keeps its existing owner.
Diagnostic views affect active PBR surfaces, not HUD or menu composition.
Actual-widget smoke now passes **4,200 assertions**, covering both sliders,
both diagnostic selections and reset. Separate deterministic GPU captures prove
normal strengths 0/1/4 and glossiness 0/1 change material output without changing
native-memory RGBA or compared alpha/depth/stencil attachments. The architecture
document above records the formula, captures and source-lighting requirement.

2026-09-24 **Native + PBR** limits relighting to supported authored materials.
These use existing native scene lights, including on baked/ambient-only
surfaces; other draws keep native lighting. Native projected shadows remain active,
while VSM, scene Environment and fairy-group overrides are inactive without
discarding saved preferences/profiles. F2 temporarily restores Native and
restores the configured Native + PBR choice when released; Authentic remains
canonical. Texture packs retains the sole material folder/load controls.

Actual-widget smoke passes **5,111 assertions**, including the fourth selection,
serialized `NativePBR`, shared material controls, capability loss, F2 restoration,
inactive Environment edits and capability-safe VSM Off. Nine deterministic
Kokiri captures verify normal and glossiness response in **Shaded**, exact
native fallback, Environment isolation and Authentic. A further 180-frame run
matches the prior Native framebuffer and guest state through deferred copies.
The initial diagnostic-only checks did not establish visible shaded response;
the corrected qualification does.
See the [Native + PBR contract and evidence](OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md#native--pbr-2026-09-24).

2026-09-25 global height control (historical; superseded below): **Height strength**
in Lighting was shared by **Per-pixel + PBR** and **Native + PBR**. It accepted 0Ã¢â‚¬â€œ4x, defaulted to 1x,
and multiplies the material's height scale, with effective depth capped at
0.1 UV. Zero disables parallax; a missing height map remains inert.
The control persists as `Graphics.Effects.PBR.HeightStrength`, updates live
without a texture reload, and participates in **Reset PBR controls** and
temporal-history invalidation. F2 preserves the configured value.
The actual-widget smoke passes **5,190 assertions**, including live edits,
persistence, zero, range normalization, F2 restoration and reset.
Three deterministic Shaded captures at 0/1/4x also verify the renderer consumer:
0x exactly matches the previous no-height framebuffer, 1x preserves the pack,
and 1xÃ¢â€ â€™4x changes 215,295 pixels. Compared native geometry, registers and lights
stay identical. Vulkan/NRI report zero errors and the same 11 baseline Vulkan
warnings. Private receipts are under `I:/_triaevum_work/f1-height/`.

2026-09-25 global dielectric specular control (historical; superseded below):
Lighting owned **Specular level**, persisted as `Graphics.Effects.PBR.SpecularLevel`. Range 0Ã¢â‚¬â€œ2x
maps dielectric F0 to 0Ã¢â‚¬â€œ8%; 1x retains the previous 4%. Fully metallic response
and roughness are unchanged. Zero is base reflectance zero, not a global
specular-off switch: Schlick's grazing term remains. Direct lighting and the
reflection guide share F0; the existing SSR/SSSR strength control stays separate.
Changes apply live, invalidate PBR temporal history, survive F2, and reset to 1x
with **Reset PBR controls**. Actual-widget smoke passes **5,269 assertions**,
including persistence, live updates, range limits and restoration.
Eight deterministic captures verify the shaded consumer and Hi-Z guide:
the old profile without the new key is pixel-identical to the prior PBR build,
Native remains pixel-identical at 2x, and fully metallic output is identical
between 0x and 2x. Dielectric 0xÃ¢â€ â€™1x and 1xÃ¢â€ â€™2x change 205,239 and 201,630 pixels.
The Hi-Z material debug view changes F0 from 4% to 8% in 262,711 pixels;
the host's RGBA5551 screenshot readback expands those red values to 8 and 16,
not the unquantized RGB8 values 10 and 20. Compared native draw state is unchanged.
These eight runs have zero Vulkan/NRI errors, no failed or unresolved graph
passes, and the same 11 baseline Vulkan warnings.
SSSR runtime verification is blocked in this scenario: both the current build
and the previous `bin-height-f1` build time out before a completed frame with
Vulkan errors for resources destroyed while referenced by a command buffer.
The shared guide producer is updated, but these runs do not establish successful
SSSR execution or fallback-free SSSR behavior. Private captures, receipts and
the paired failure logs are under `I:/_triaevum_work/f1-specular/`.

2026-09-25 material editor: F1 now has an always-available **Material editor** tab.
The scene-wide PBR controls are removed; the material JSON owns normal scale,
glossiness, dielectric reflectance, height depth and the new **Minimum roughness**
(0.045Ã¢â‚¬â€œ1, default 0.045). Lighting retains the model selector; Textures retains
the pack folder/load controls. Obsolete `Graphics.Effects.PBR` values are ignored,
not copied into every material.

Choose an existing material or create one from the shared Grass-style texture
catalog. The editor uses the native Azahar hash, not the catalog content hash.
It exposes base color, normal, ORM, emissive and height image slots with a file
browser, plus packed ORM or separate occlusion/roughness/metallic inputs.
Separate inputs select their source channel or a constant and save as one ORM
PNG; mismatched dimensions are errors rather than implicit resampling.

Numeric edits preview only the selected material. **Save material** commits its
JSON and imported PNGs, then requests complete asynchronous bundle publication.
Map edits apply after Save; diagnostic Material view is transient. Dirty switches
require Save / Discard / Keep editing. Decode and atomic-write failures preserve
the previous files and leave an explicit, retryable draft error.

The real-widget smoke passes **6,294 assertions**, including texture-based
creation, existing-material selection, every slot and the browser, ORM channel
packing, per-material isolation, F2/capability loss, dirty switching, malformed
images, mismatched channel dimensions, and failed-save recovery.
Ten deterministic renderer captures preserve default PBR and Native output,
exercise minimum roughness in shaded output and Hi-Z guides, and retain identical
compared native draw state. All have zero Vulkan/NRI errors and the same 11
baseline Vulkan warnings. Private evidence: `I:/_triaevum_work/material-editor/`.
The SSSR limitation above is unchanged.

2026-09-25 per-material reflection weights: **Material editor** adds **Specular
weight** (0Ã¢â‚¬â€œ1, default 1) and **Screen-space reflection weight** (0Ã¢â‚¬â€œ2, default 1).
The first scales complete direct and screen-space specular response, including
metals and grazing Fresnel; zero leaves diffuse/emission intact. The second
affects only screen-space reflections and multiplies the first. Both preview
only the selected material and persist in its JSON as `specularWeight` and
`screenSpaceReflectionWeight`. **Dielectric reflectance** is the clearer name
for the existing `specularLevel` control; its F0 semantics and JSON key stay
unchanged. Reset restores both weights to 1; old descriptors need no migration.

The actual-widget smoke passes **6,395 assertions**, including the two weights'
save, independent preview, F2 retention and reset. A production-kernel Vulkan
regression checks zero/neutral/2x/fractional gain on adjacent pixels and partial
workgroups in the Hi-Z filter and SSSR material resolve; zero does not acquire
a neighbor's reflection and temporal reactivity is not mistaken for coverage.
This qualifies the SSSR resolve consumer, not the previously failing full SSSR
scenario. The complete provider's pre-existing qualification limit is unchanged.

Eighteen deterministic 32-frame runs include a previous-executable Hi-Z reference
and the staged private runtime. Neutral PBR/Hi-Z remain pixel-identical; Native
bypass and direct-only lighting are unaffected by the screen-space weight.
Specular zero affects dielectric and metallic materials, Hi-Z zero matches
Reflections Off, and the combined specular-zero/reflection-2 guide remains zero.
All 110 compared native draws retain composition, geometry, texture, light and
raster state. Every run reports zero Vulkan/NRI errors, no failed/missing/
unresolved graph passes/resources, and the same 11 baseline Vulkan warnings.
Foundation, pipeline-cache and scene-frame suites pass 292/292, 41/41 and 19/19.
Evidence and invocation recipes: `I:/_triaevum_work/material-weights/`.
The private `Avvia-PBR-Height.cmd` now selects `bin-material-weights/TriAevum.exe`;
the interactive profile, authored pack and already-running game are untouched.

2026-09-25 auxiliary UV seams: **Material editor Ã¢â€ â€™ Material Ã¢â€ â€™ UV seam softening**
adds per-material enable (default Off), strength 0Ã¢â‚¬â€œ1, width 0.1Ã¢â‚¬â€œ25% of the shared
native edge, and independent normal/AO/roughness/metallic/emission/height channels.
Default width is 5%; emission is excluded by default. Save writes the selected
descriptor's `seamSoftening` object; live preview and Reset use the existing
material owner. Off remains available when renderer capabilities are absent.
The panel reports the selected material's last native-draw status.

Only auxiliary maps are blended. Base color retains its original sampling and
unsmoothed relief; native alpha remains native. Normals use each chart's posed
tangent frame; emission mixes in linear space. Height smoothing affects only
auxiliary-map relief. The renderer requires typed affine UVs and unambiguous,
smooth manifold adjacency inside one resolved native geometry submission; it
does not join separate draws, hard/open/non-manifold edges or opposing coplanar
layers.

Actual-widget smoke: **6,607 assertions**, including saved enable/strength/width/
channels, isolated live edits, capability-safe Off and reset. Production Vulkan
regressions cover rotated/mirrored charts, three-chart corners, scalar continuity,
linear emission, height, exact base sampling, zero and unselected channels, and
indexed strips with degenerates/base vertices. Eight deterministic 32-frame
captures preserve the old framebuffer when disabled, and preserve zero-strength
and Native bypass exactly. Width 1%/5%/15% changes 2,991/17,916/59,439 pixels at
the wall's UV seams. Native state remains identical across all 110 compared draws.
Every diagnostic frame has zero Vulkan/NRI and graph errors; the same 11 baseline
warnings remain. The staged executable matches the qualified enabled image.
Evidence: `I:/_triaevum_work/material-seams/`.

`Avvia-PBR-Height.cmd` now selects `bin-material-seams/TriAevum.exe`. The user's
interactive profile, material pack and existing game process are not modified.

2026-09-17 camera/motion separation: free camera offers only disabled, digital
look, mouse, right stick and automatic. Sensor sources and their camera-only
sensitivity are removed; aiming keeps its sensor controls. Legacy motion-camera
profiles migrate to right stick. Shared/native input tests pass, Windows game
build succeeds, and actual-widget smoke passes 3,978 assertions, including the
real source dropdown and stick selection. No physical-controller playthrough
was repeated for this change.

2026-09-17 Azahar motion/touch extension: consolidated Devices owns the controller
touchpad switch/index, reported capabilities and sensor calibration status.
Calibration requires received sensor data, reports movement/cancellation/save
errors and persists device identity. Actual-widget smoke: 3,950 assertions,
including live touchpad enable/disable. No second control surface was introduced.

2026-09-17 controller alignment: the consolidated Devices panel now shares
runtime controller selection semantics, preserves optional serial identity,
shows the active device and labels serial-less choices as model preferences.
Real-widget smoke passes 3,924 assertions, including selecting between two
same-model devices and preserving identity when applying a preset. See
`TRIAEVUM_DUSKLIGHT_CONTROL_ALIGNMENT.md` for donor provenance and hardware limits.

2026-09-17: the current branch restores the consolidated ImGui F1 surface.
The deferred retained F1 / advanced F12 split is preserved on
`feature/deferred-f1-f12-menus` at `a144833`; commits `0b30a6e`, `9892379`
and `23961d8` are reverted here without discarding that work. The rebuilt
actual-widget smoke passes 3,896 assertions. Reintegrate through that branch,
not by adding a second settings owner to this one.

Date: 2026-09-06. Worktree: `triaevum-release`.

2026-09-10: [Game language selection](TRIAEVUM_GAME_LANGUAGE.md) adds the
application-owned Game tab, shared with Forge and limited to detected ROM
languages. Changes apply on a full restart, not on save-state load.

2026-09-10: [Display resolution and confirmation](TRIAEVUM_DISPLAY_RESOLUTION_FIX.md)
supersedes the in-panel display confirmation below. The modal now works with
F1 closed; output and scene resolution report actual renderer extents.

2026-09-07: [F2 native presentation override](TRIAEVUM_F2_NATIVE_PRESENTATION.md)
adds a session-only effect comparison and persistent header status. F1 now starts
closed even when the previous session saved it open.

## Controls redesign (2026-09-10)

The previous five-column binding table combined keyboard, alternate key, mouse
and controller in a content-proportional layout. Full-width combo boxes fed their
current widths back into auto-fit sizing. The replacement uses explicit stretch
weights, no persisted table widths and no nested table scrolling.

- Four sections: **Bindings**, **Camera**, **Devices**, **Shortcuts**. Bindings
  opens first, with separate Keyboard / Mouse / Controller views and collapsible
  Movement, Game buttons, D-pad, Menu shortcuts and Look directions groups.
- Search controls by name; search assignments inside their dropdowns; select
  **Unassigned** to clear one source. Keyboard primary/alternate remain separate.
  Shared assignments are indicated on their row with the other actions in a
  tooltip, not an expanding list above the table. They remain allowed because
  context-dependent mappings can intentionally share a source.
- Preset selection does not mutate live input until **Apply preset** is pressed.
  Controller preference and motion calibration survive a preset change.
- Camera contains free-camera behavior, native gyro aiming and C-stick aiming;
  Devices contains enablement, controller selection, analog settings and motion
  calibration. Shortcuts contains the child/adult D-pad and menu actions.
  Full-width fields place labels above their values and remain usable at the
  minimum F1 window width.
- **Save controls** / **Revert changes** stay below the single scrolling body.
  Revert works before the first save using the initial profile. It parses
  existing required files before applying either, and restores only the
  control-owned TopScreen fields, preserving live HUD layout. Failures remain
  visible, with the complete message available on hover.

The UI still uses `NativeControlConfigRuntime` and `TopScreenUiConfigRuntime`.
No new input poller, gameplay routing, SDL mapping, JSON schema or save format.
Reusable field layout is in `oot3d_control_settings_widgets.h`; title-specific
widgets and the control-owned TopScreen field copy are kept together in
`oot3d_top_screen_control_widgets.h`.

The real-widget smoke covers 120 consecutive frames at each of 520, 760, 1100,
then 520 pixels, binding search/clear/reassignment on all three devices, preset
confirmation/cancel, external revisions, and the compact footer on all sections.
Isolated JSON fixtures exercise save, revert, preserved HUD state and failure
atomicity on reload. This tests UI wiring and geometry, not physical-controller
ergonomics; the existing shared input tests cover routing. Windows incremental
runtime build recompiles the panel and relinks only, with no title/AOT rebuild.

Validation on Windows: **3,569 actual-widget assertions**, shared native input
tests passing, and the rebuilt Vulkan runtime loads the existing complete-save
checkpoint and renders a native framebuffer (bounded run, exit 0). No UI inputs
were injected in that game run; the menu's interaction/layout tests use the real
widgets in the headless fixture. Linux/Android execution was not repeated for
this panel-only change.

## Scope and ownership

This change repairs the existing F1 control surfaces, not the rendering algorithms.
No title code, save format, native HUD composition, shader math, or effect-graph
scheduling has been replaced.

Current surfaces (since the 2026-10-06 tab rationalization above):

| Surface | Sections | Owner |
| --- | --- | --- |
| Display | Screen, Antialiasing and upscaling, Motion and camera, TopScreen HUD size and button labels | GraphicsSettingsRuntime; TopScreenUiConfigRuntime (HUD size, HUD button labels) |
| Lighting | Lighting & GI, Shadows, Occlusion, Reflections, Scene environment, Toon | GraphicsSettingsRuntime; SceneEnvironmentRuntime (Scene environment) |
| Grass | Sources, Masks, Generation, Appearance, Performance, Wind, Interaction | GraphicsSettingsRuntime / grass module |
| Materials | Texture packs, Material editor, Reflection materials | GraphicsSettingsRuntime / texture-pack module / material JSON |
| Controls | Bindings, Camera, Devices, Shortcuts | NativeControlConfigRuntime / TopScreenUiConfigRuntime |
| Game | Save states, Language | SavestateLibrary, GameLanguageSettings |
| Photo mode | Free camera, Hide HUD | DevFreeCamera, PhotoModeState (session only) |

Input still owns host-device routing and sensitivity. TopScreen owns game-camera
behavior, layout and D-pad actions. Its C-stick smoothing really is consumed by
the aiming policy despite the historical member name `FreeCameraSmoothing`;
it was therefore **not** moved to an unrelated free-camera control.

## Corrected defects

1. Selecting Authentic/Enhanced/Toon no longer immediately relabels it Custom.
   Selecting Custom preserves the current values instead of resetting them.
2. Unsupported AA, upscaler, shadow and reflection choices are disabled individually,
   with validation reasons on hover. Off remains selectable after capability loss.
   The menu probes the existing validator, rather than duplicating its policy.
3. Shadow mode no longer evaluates a changed value to decide whether to pop a
   disabled scope. The old code could underflow ImGui's disabled stack.
4. The numeric output-resolution input no longer uses the unsupported
   `EnterReturnsTrue` scalar flag. It commits on edit completion and no longer
   leaves a stale draft after focus without modification. Added 4:3 presets.
5. Keep/Revert display confirmation is above the tab bar, visible from every tab.
   Window bounds follow the viewport; long content scrolls below section navigation.
6. Graphics changes remain live while dragging, but disk writes are coalesced
   until release. Closing F1 also flushes pending changes. Failed saves are
   explicit and retryable; display changes remain unpersisted until accepted.
7. Texture settings no longer bypass validation by calling Configure from the UI.
   The existing backend revision consumer applies validated settings. Explicit
   texture-index reload remains a module command.
8. Toggling custom toon bands preserves the user's values. Changing band count
   resamples the profile instead of discarding it. Reset remains an explicit action.
9. Removed the ineffective DLSS/XeSS sharpness control. SSSR shows its effective
   thickness range; Hi-Z-only distance/edge parameters are identified as fallback
   parameters when SSSR is selected. Material assignments stay with Reflections.
10. Controls and TopScreen have fixed save/reload footers; save errors are not
    hidden by the unsaved indicator. Successful previews adopt validated runtime
    state. New external revisions cannot be overwritten by stale panel copies.
11. A custom input profile is displayed as Custom; a disconnected explicitly
    selected controller is not misreported as Automatic.

Graphics auto-save remains distinct from the explicit Controls/TopScreen save
commands. No configuration files belonging to the user are reset or migrated.

## Code map

Current control ownership and first-frame/LOD update (2026-09-07): see
[First-Frame Grass and Gameplay Camera](TRIAEVUM_GRASS_FIRST_FRAME_AND_GAMEPLAY_CAMERA.md).
Camera, aiming and D-pad behavior now live in Controls; the TopScreen section
of Display offers only the HUD size (2026-10-17). JSON ownership remains
unchanged.

Renderer files under `runtime/three_ds_recomp/`:

- `src/fast/extension/graphics_settings_window.cpp`: navigation (top-level tabs,
  Lighting and Materials sections, application-tab groups) and application.
- `src/fast/extension/graphics_settings_{display,antialiasing,lighting,shadows,reflection,toon}_panel.cpp`:
  separate section implementations.
- `include/fast/extension/settings_panel_widgets.h`: validated choice widget and
  complete apply-result formatting.
- `src/fast/extension/graphics_settings_runtime.cpp`: persistence state and coalescing.
- `src/fast/extension/graphics_settings.cpp`: Custom semantics and band resampling.
- `src/fast/Fast3dGui.cpp`: viewport constraints and save flush on close.
- `src/fast/extension/{grass_settings_panel,azahar_texture_pack_panel}.cpp`.

Application files under `tools/oot3d/native_game_runtime/`:

- `oot3d_native_controls_settings_panel.cpp`
- `oot3d_top_screen_settings_panel.cpp`

## Repeatable verification

The post-1c audit repairs the Windows runner's missing language objects and adds
`tools/triaevum_release/tests/run_f1_settings_smoke.sh RUNTIME_BUILD [DEPENDENCIES]`
for Linux. Set `TRIAEVUM_NLOHMANN_INCLUDE` when JSON headers use a private prefix.
Both runners now pass 3,896 assertions against the current built runtime objects.

First build the regular runtime target, without rebuilding the title:

```powershell
cmake --build I:/oot3dre_work/triaevum-direct-module-build --target triaevum_public_runtime --parallel 3
tools/triaevum_release/tests/run_f1_settings_smoke.ps1 -Build I:/oot3dre_work/triaevum-direct-module-build -Dependencies I:/oot3dre_work/whole-aot-product-consumer/_deps -Compiler I:/oot3dre_tools/llvm-22.1.6/bin/clang++.exe
```

The standalone test links the **actual built panels and runtime libraries**.
It uses a private ImGui build with assertions and item hooks, synthetic ImGui
mouse events, and memory-only configuration. It requires neither a ROM nor a GPU.
Its cached ImGui objects avoid recompiling the test framework on every iteration.

Coverage: preset selection; capability loss and Off; GTAO and CACAO selection,
quality and provider-specific controls; independent Navi/fairy controls;
reflection, MSAA and upscaler selectors;
toon-profile preservation; every top-level and nested
tab; application preview and external-revision reconciliation; display confirmation
from Grass; compact panel footers; deferred persistence and save failure/retry.
The test also checks balanced UI scopes and finite geometry on every UI frame.

Grass distance update (2026-09-07): Performance now separates Draw distance
(up to 50,000) from Density falloff distance. The latter migrates from the old
visibility value and remains fixed when visibility is changed. The old Room
blade budget label is now Visible blade budget; its persisted key is unchanged.
The static cache has a separate internal memory/device limit. Both distance
widgets are exercised by the real F1 smoke. See
`TRIAEVUM_GRASS_INTRO_STABILITY.md` for the cache/LOD contracts and measurements.
Assertion counts include those per-frame invariants, not independent gameplay tests.

Soft Grass LOD follow-up (2026-09-07): the same Performance section now includes
final-distance fade, density fade, a configurable individual-blade/tuft transition,
separate distant tuft quantity and spread, and stable segment-transition spread.
Old profiles retain their distances. All six added controls are covered by the
actual-widget smoke (1,618 assertions). See
`TRIAEVUM_GRASS_SOFT_DISTANCE_LOD.md` for units, defaults, rendering contracts and
the CPU/GPU and framebuffer verification results.

The current bounded Vulkan runner is
`tools.triaevum_release.validate_ambient_occlusion_run`; select a disposable
launch profile as well as private configuration/save paths.
The earlier CACAO smoke under
`I:/oot3dre_work/f1-menu-diagnostics/vulkan-menu/` completed with 213 recorded
frames, 179 CACAO passes and zero Vulkan/NRI validation errors. Its captured
F1 panel was visible and legible. That historical result is not GTAO validation
or an FPS benchmark.

## Texture dropdown follow-up

The initial empty-catalog smoke test missed a live-game interaction defect in
`texture_catalog_viewer.cpp`: each selectable's ImGui ID incorporated its label,
including the constantly changing observation count, and its sorted row index.
The pressed widget could cease to exist before mouse release. Live sorting could
also move a different texture under the pointer.

The added populated-catalog test **failed on the previous implementation** with
`texture selection lost while observations changed`. The correction gives rows
an ID based on the complete texture identity (hash, dimensions, native format),
independent of labels or rank. An open popup retains its ordered metadata snapshot
until closed; reopening refreshes it. Neither game data nor GPU texture contents
are frozen. The old 96-entry truncation is replaced by a scrollable clipped list.

Regression coverage now includes selection while counts change, rank changes
between mouse move and release, actual Grass/reflection assignments, and wheel
navigation to an entry beyond the first 96. The full actual-widget smoke passes
with 1,032 assertions, including per-frame scope checks.

## Limits

### Direct Input Assignment And Mouse Recapture (2026-09-10)

Each binding selector now offers `Listen...` as well as the searchable list.
The capture waits for the selected device's held inputs to be released, then
assigns the next supported keyboard key, mouse button, controller button or
trigger. Escape, Cancel, closing F1, or a 20-second timeout cancels it. Escape
remains reserved for releasing the mouse; primary and alternate keys are
assigned independently. Existing custom configurations are not reset.

Ownership remains split by responsibility:

- `tools/three_ds/input/three_ds_input.{h,cpp}` owns the portable capture state
  machine, reusing the existing host binding vocabulary, without SDL or ImGui.
- `oot3d_native_control_config` owns the synchronized capture instance and
  presets. Keyboard + Mouse uses mouse aim and mouse free look; Controller
  uses the right stick for both. Physical motion remains separately selectable.
- `oot3d_native_a32_window` supplies the existing raw host poll before gameplay
  filters, including disabled devices during assignment. F1 suppresses gameplay
  keyboard, controller, motion and native-touch input while retaining sensor
  observation for calibration. No second SDL event pump is introduced.
- `oot3d_native_controls_settings_panel` owns the modal and draft assignment;
  the usual Preview, Save and Revert paths persist the result.

The mouse recapture defect was in `Fast3dWindow::MouseButtonDown`: it used
`ImGuiIO::WantCaptureMouse` to reject the click after Escape. The full-window
`Main Game` surface also sets that flag, even with F1 closed. Recapture now
uses the actual host menu/window visibility, consistently with the gameplay
input poll. The resume click is still consumed, and Escape still releases
capture rather than closing the game. Native TopScreen camera eligibility,
including cutscene restrictions, is unchanged.

Verification on Windows:

- Shared input tests, native input tests and TopScreen contract tests pass.
- The actual F1 widget smoke passes 3,756 assertions (including UI-frame
  invariants), exercising direct assignments, opening-gesture release, triggers,
  alternate keys, cancellation and the real ImGui `Main Game` capture flag.
- Two bounded 180-presentation-frame Vulkan runs from the same gameplay state
  complete normally. A supplied C-Stick command produces 60 active camera
  updates versus zero without input. Renderer framebuffer captures confirm
  different camera orientations. This verifies the camera consumer, not physical
  mouse delivery; the latter still needs the user's hardware confirmation.

The runtime report now includes `hid_input.mouse_polling` counters for eligible,
released, host/native UI-owned polls, capture transitions and physical movement.
Together with `free_camera_input` and TopScreen camera counters, these distinguish
capture, delivery and native-camera eligibility without another instrumented build.

Current local build: `J:/TriAevum-verify-20260910/runtime/TriAevum.exe`; evidence:
`J:/TriAevum-verify-20260910/camera-consumer-20260910-162418/` (command) and
`camera-consumer-20260910-162456/` (control). Only host runtime/UI libraries were
rebuilt; the title AOT module, game data and savestate format are unchanged.

### Portable Widget Test (2026-09-09)

The existing real-widget fixture is also available through CMake:

```sh
cmake --build BUILD --target triaevum_f1_settings_smoke --parallel 3
BUILD/tools/triaevum_release/tests/triaevum_f1_settings_smoke
```

Use `.exe`/the configuration subdirectory on Windows where applicable. The
target is excluded from normal builds and uses a private test-engine ImGui.
SDK Clang 19.1.7 on Linux passes 1,761 assertions, including the complete,
reversible shoulders/triggers exchange. It links actual panels/configuration,
not a mock UI. Hardware button detection still needs device validation.

### Remaining Device Coverage

Hardware motion calibration, subjective sensitivity and all GPU/provider
combinations still require their respective devices. A selectable setting is
validated and wired to its existing consumer; this does not assert that every
effect is visually perfect in every game scene. No new named-profile manager,
gameplay feature, or renderer effect is introduced in this change.

The developer executable remains
`I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe`.
The existing local launcher
`I:/oot3dre_work/cacao-release-diagnostics/Avvia-TriAevum-corretto.cmd`
uses it with the user's installed launch profile. The catalogued release
installation is not overwritten with an unpaired executable.

## Grass Nearby Rim (2026-09-11)

Grass > Appearance groups the enable checkbox and start/end distance controls
under Nearby toon rim. Distances use meters in the UI and world units in storage;
disabled rim disables its distance widgets. The real-widget smoke edits start
to 3 m and end to 12 m, checks 300/1200 world units, and toggles off/on. Global
toon rim strength/color remain shared rather than duplicated in Grass settings.

## Scene Environment (2026-09-23)

F1 **Environment** owns the current native scene's authored lighting profile.
It displays the scene ID and file path, and edits ambient color/intensity,
native scene/local/ambient contribution, native fog contribution, and added
key/fill color, intensity, azimuth and elevation. **Native scene direct
contribution** uses both live native environment directions on world and actors.
**Native local contribution** retains other native per-draw assignments.
Neither controls the added key/fill or fairy groups. Ambient has no direction.
An active added key casts shadows; otherwise the dominant native environment
direction is eligible. Added fill is unshadowed. Lighting model and normal/gloss controls
remain exclusively in **Lighting**. Global shadow quality belongs exclusively
to the **Directional shadows** section of **Environment**.
The **Navi and fairy point lights** controls expose separate enable, intensity,
radius scale and color tint for Navi and other fairies. They remain independent
of the ambient/directional profile's enabled state. Switching a group Off
also removes its old native vertex-light direct contribution in both per-pixel
models, without removing the independent native ambient term. It does not
silently fall back to the original vertex light.

**Preserve scene brightness** keeps the native ambient reference and authored
unlit color when original light gains are zero. It owns the ambient baseline,
so **Native ambient contribution** is visibly inactive until preservation is
disabled; its saved value is not erased. Added lights remain independent and
can brighten the scene. This is not automatic exposure or a total-luminance lock.


Profiles use `environments/scene-XXXX.environment.json` beside the global
graphics configuration. Kokiri is `scene-0055.environment.json`, shared by all
rooms associated with `spot04_info.zsi`. No native ZSI is rewritten.
A scene with day and night (Kakariko Village, Hyrule Field) can keep a second
profile for the night, `scene-XXXX.night.environment.json` (same schema, plus
`"timeOfDay": "night"`). **Create night profile** saves the values shown as that
file; while the game's night flag (SaveContext `+0x10`, `0x00587968`) is set
the night profile is the one in use and edited, by day the scene's own. The
switch follows the flag (dusk and dawn, not a blend) and drops an unsaved
preview, as a scene change does. **Remove night profile** (confirmed) deletes the
file and the scene's own profile applies at night again. Day and night are
separate render scopes, so the shadow history does not carry across.
Version 3 separates `nativeLocalStrength` from the scene-wide
`nativeDirectStrength`. Versions 1 and 2 retain their old gain for both.
Version 1 acquires default point groups; version 2 retains its saved groups.
Version 4 adds `preserveSceneBrightness`: new profiles enable it, while versions
1Ã¢â‚¬â€œ3 keep authored-only brightness. Loading does not rewrite the user's file;
explicit saves write version 4.
**Save scene environment** persists the live preview atomically; **Reload
scene environment** reads the file. The unsaved warning explicitly requires
saving before leaving the scene. Save/reload errors are visible and a failed
reload retains the last valid same-scene preview.

**Use native environment** remains available when capabilities disappear.
Native lighting, Authentic and F2 bypass the profile without erasing it.
Inactive ambient/directional edits publish unchanged effective rendering inputs,
so they cannot invalidate temporal or shadow histories. Point-group edits remain
effective independently. Missing or invalid destination
profiles cannot inherit another scene's authored light.

Both the CMake and PowerShell actual-widget build paths include the title-owned
profile and panel. The fixture exercises key/fill tabs, live controls, real
file persistence, reload, F2, scene transitions, invalid files, write failures
and capability-safe Off. Deterministic renderer captures separately verify
normal-map response, key direction, ambient/fill, static-world shadows,
disabled-path identity and unchanged native RGBA/alpha/depth/stencil.
See the scene-environment section of the NRI architecture document for the
schema, ranges, owning boundaries and capture evidence.
The brightness-reference increment passes 6,648 actual-widget assertions,
including the active control, ambient ownership, persistence, legacy versions
1Ã¢â‚¬â€œ3, F2/native bypass, scene transitions and visible save/reload failures.

Final verification: 4,516 actual-widget assertions passed, including the
inactive-profile rendering-input invariant. Two additional GPU runs compare
the user's combined CACAO/Hi-Z/PBR/shadow configuration with an effects-off
baseline at the same persisted 1920Ãƒâ€”1080 output extent. Native-memory RGBA and
all compared alpha/depth/stencil attachments remain exact. The real-time
interactive runtime also survived framebuffer readback and the following
20 seconds; its fresh renderer framebuffer was inspected.

## Enhanced Preset Profile (2026-10-01)

Superseded on 2026-10-06: see "Enhanced default, Lights & Shadows and credits"
above (new capture, default of new installations, presets set the camera).

The **Enhanced** preset was the user's tuned Kokiri play configuration
("enhanced"): Per-pixel + PBR lighting, virtual shadow maps, point-light cube
shadows, MXAO from depth normals, the custom grass profile, DLSS and fixed-60
presentation. `EnhancedPresetDocument()` (graphics settings persistence) holds
its "Graphics" block and the preset decodes it once. As for every preset,
choosing it keeps the user's own state: window, output, presentation, camera,
texture packs, grass sources, reflection materials and the saved grass preset.
Scene lighting profiles are per-scene files and are not part of it. **Toon**
keeps its own base (FXAA, VSM, GTAO, Medium grass) instead of following
Enhanced. Tests: re-encoding the preset gives the captured document back,
ownership and Toon base unit cases, and the actual-widget smoke selecting
Enhanced with a texture pack (7,811 assertions).

## Key light direction (2026-10-02)

Environment > Key light adds **Key light direction**: *Manual*, *Main scene
light* (with **Azimuth offset** and **Elevation offset**) or *Ambient light
orientation*. Outside Manual the azimuth/elevation sliders become the fallback
used while the scene has no environment light. Saved in scene profile version
5. Actual-widget smoke: **7,988 assertions**, including the three modes, the
offsets reaching the rendering snapshot and the saved profile.
