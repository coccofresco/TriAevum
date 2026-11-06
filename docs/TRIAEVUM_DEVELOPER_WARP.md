# Developer warp menu (F1 > Dev Tools)

2026-10-07. Developer builds only: F1 > **Dev Tools** reproduces Ship of
Harkinian's Dev Tools > Warping column over OOT3D, so a developer can jump
between scenes, entrances and saved places.

## Build gate

`TRIAEVUM_DEVELOPER_TOOLS` (CMake option, default OFF) compiles the tab and its
runtime into `oot3d_native_game` (`TRIAEVUM_DEVELOPER_TOOLS=1`). Without it
nothing of the feature is compiled. `--product-info` reports
`capabilities.developer_tools`; `prepare_release.py` refuses a runtime that has
it and `audit_release.py` rejects a runtime contract that does not exclude it
(`test_release_audit.py` covers true and missing). A developer configures with
`-DTRIAEVUM_DEVELOPER_TOOLS=ON`.

## What mirrors SoH

| SoH (Dev Tools > Warping) | TriAevum F1 > Dev Tools |
|---|---|
| Better Debug Warp Screen (default on) | same checkbox: the Better list (scenes, then entrances), Child/Adult, Day/Night; off: the vanilla scene list, current time kept |
| Debug Warp Screen Translation | same checkbox: German or French names when the game runs in that language (the F1 font has no Japanese) |
| Debug Warp Screen lists (`sScenes`, `sBetterScenes`, `sBetterGrottos`) | the same lists and names, generated from SoH's `z_select.c` |
| Grotto entries (`Select_Grotto_LoadGame`) | same: content byte, return entrance, room, position and playerParams `0x4FF` in `respawn[RETURN]`, `respawnFlag` 0 |
| Warp Points: name + "+", Warp, Boot, Delete | same; points stored with SoH's fields (`entranceId`, `roomNum`, `pos`, `rotY`, `bootToPoint`, `useSceneEntrance`) |
| Warp point arrival at the saved place: `respawn[DOWN]` = the point, playerParams `0xDFF`, `respawnFlag` 1, void damage skipped (`VB_INFLICT_VOID_DAMAGE`) | same writes; the void-out damage Player_Init arms (`Player+0x249E`, -2) is reset to 0 once, at the respawned player; `useSceneEntrance` arrives at the entrance (`respawnFlag` 0) |
| Boot to warp point (Boot Sequence) | the boot point is warped to when gameplay first starts in a session: OOT3D needs a loaded file, there is no debug save |

Not offered: the Master Quest toggle (OOT3D's Master Quest is a save-wide mode
with a mirrored world, not a per-scene dungeon swap) and SoH's in-game Debug
Warp Screen game state and its button combination (the request is an F1 menu).

## How a warp runs

`DevWarpRuntime` (`oot3d_dev_warp.{h,cpp}`) never builds a scene. At the guest
frame boundary where `NativeScenarioBootstrap` advances, during normal gameplay
(game mode 0, no title/demo cutscene index, the TopScreen free-camera
admission) and with the PlayState transition slot free, it sets the age the
native way, PlayState `linkAgeOnLoad` (`+0x5C00`: the N64 tail maps onto
OOT3D's at -0xC1E8, checked on five fields; the game applies it when the
scene ends and swaps the age equipment, like SoH's SwitchAge), and the time of
day at SaveContext `+0x0C` (noon or midnight; the scene derives night from it,
`+0x10` reads 1 afterwards). Writing the save age under a live player faults
the title. It writes the respawn state SoH's warp writes (below; a scene warp
clears `respawnFlag`, as `Select_LoadGame`). It then calls code.bin's own transition request
(`FUN_003348E8`, trigger 20) and effect preparation (`FUN_0035DA3C`), the
structural scenario contract (`oot3d_dev_warp_process.cpp`). Arrival is the
requested scene in the PlayState after the transition slot was seen busy.

## Catalog and validation

`tools/oot3d/dev_warp/generate_dev_warp_catalog.py <soh root> <out.inc>`
generates `oot3d_dev_warp_catalog.inc` from SoH's `z_select.c`,
`entrance_table.h` and `scene_table.h` (their SHA-256 are in the file header):
125 vanilla scenes, 50 Better scenes with 300 entrances, the grottos. Each
entrance carries the scene and spawn SoH expects.

At the first gameplay frame the runtime reads OOT3D's global entrance table
(`0x00543BB8`, 1586 rows of scene and spawn). A destination whose OOT3D row
differs is listed disabled, with the reason in its tooltip; nothing is guessed.
Against the EUR code.bin 391 of 397 entrance references match; the six others
are debug scenes OOT3D numbers differently (SRD Map, Treasure Chest Warp,
Hyrule Garden Game).

## Respawn fields

OOT3D keeps the N64 respawn state 0x188 bytes further than the N64
SaveContext (`gameMode` 0x135C there, `+0x14E4` here): `respawnFlag` at
SaveContext `+0x14EC` (s32) and `RespawnData respawn[3]` from `+0x14F0`, 0x1C
bytes each with the N64 layout (pos, yaw `+0x0C`, playerParams `+0x0E`,
entranceIndex `+0x10`, roomIndex `+0x12`, data `+0x13`, temp flags
`+0x14`/`+0x18`): DOWN `+0x14F0`, RETURN `+0x150C`, TOP `+0x1528`. Read from
code.bin: `Gameplay_SetupRespawnPoint` (`0x0036C494`) writes those rows from the
player, `Gameplay_TriggerVoidOut` (`0x0036EBDC`) sets the flag to 1 and requests
the DOWN entrance, the great-fairy return (`0x0033DE98`) sets -3 and requests
the RETURN entrance, and Player_Init (`0x00191844`) places the player from the
row `respawnFlag - 1` and arms the void damage for 1 and -1. (An earlier try
used `+0x4EC`/`+0x4F0`, relative to the decomp's second SaveContext base
`0x00588958`, against `0x00587958`: the game ignored them.)

## Free camera

F1 > Dev Tools > Free camera (or F6) detaches the camera from Link. While it
is on, the main camera's `Camera_Update` (0x002D84C4) is replaced: the free
view is written to the camera's at/eye (+0x80, +0x8C, +0xA4) and the
PlayState view (eye +0x1B8, at +0x1C4, up +0x1D0), and the function returns,
so the game itself draws, culls and animates from it. It starts from the
game's view. Keyboard: WASD move, Q/E down/up, Shift faster, Ctrl slower;
right mouse button held to look. Pad: left stick moves, right stick looks,
L/R shoulders down/up. These are read from the devices themselves, not
through the game's bindings (the keyboard and mouse profile binds the right
mouse button to R). The game sees no pad, sticks or touch meanwhile, so Link
stays still. "Back to the game's view" restarts it from the game's camera.
`TRIAEVUM_DEV_FREECAM=<x>,<y>,<z>,<yaw>,<pitch>` (degrees; yaw 0 faces +Z)
starts with it on at that pose. The placed lights' gizmos also show while it
is on.

## Storage

`developer-tools.json` beside the config: `WarpPoints` (SoH's format) and
`DebugWarpScreen` (Better, Translation, the selected scene and entrance, age,
night), written atomically; a read or write error is shown in the tab.

## Verification

`oot3d_dev_warp_tests` (fake guest, 32 checks): catalog against the table
(grottos with their way back), scene and vanilla warps (age on load, time kept
or set, respawn flag cleared), the warp point's DOWN row and flag and the void
damage cancelled once, a `useSceneEntrance` point, the grotto's RETURN row,
arrival, gameplay and transition-slot admission, SoH-format persistence and the
boot point. Against the EUR code.bin the 28 grottos and their return entrances
all match.

Live, developer launch from the play quick save (Kokiri Forest), log lines
`TRIAEVUM_DEV_WARP` (stderr): Hyrule Field "Near Drawbridge" as adult at night
arrives in 70 guest frames as adult (`age 0`), `day_time 0x0000`, night flag
1, health unchanged, capture showing adult Link at the drawbridge; the same
warp keeping child and day arrives likewise. A boot warp point at Hyrule Field
(300, 0, 1200), yaw 0x4000, arrives exactly there with that yaw, the armed void
damage (-2) cancelled and health 48 unchanged 90 frames later. The chest
grotto "Kokiri Forest (Song of Storms)" arrives in the grotto (scene 62); with
an input timeline walking onto the exit light, Link leaves to Kokiri Forest at
the grotto's place (minimap and capture). `TRIAEVUM_DEV_WARP=<scene>:
<entrance>:<child|adult>:<day|night>` requests a Better-list warp at the first
gameplay frame for such checks. `TRIAEVUM_DEV_WARP_ENTRANCE=<entrance>:
<scene>:<child|adult>:<day|night>` requests any native entrance the same way,
for oracle fixtures outside the list: `0x28A:81:adult:day` (the Lon Lon fence
jump) arrives in Hyrule Field mounted on Epona, matching the Azahar oracle's
Gateway-cheat transition from the same save.

Free camera, developer launch from the play quick save with
`TRIAEVUM_DEV_FREECAM=-32.6,600,1627.4,180,-45`: `TRIAEVUM_DEV_FREECAM
applied` at the first main-camera update, the capture shows Kokiri Forest from
600 units up looking down at the stump with Link out of view, 150 frames
complete.

## Majora's Mask 3D (Moonfall Curse)

2026-11-06. MM3D plugs into the same runtime and tab through its title ABI
(`titles/mm3d/adapter/mm3d_dev_tools.h`, `Mm3dDevTools`), in the same
`TRIAEVUM_DEVELOPER_TOOLS` builds only; its settings live in
`developer-tools-mm3d.json`. The donor is 2Ship2Harkinian (CC0-1.0), MM's
counterpart of SoH.

Catalogue: `titles/mm3d/dev_warp/generate_mm3d_dev_warp_catalog.py <2s2h root>
titles/mm3d/adapter/mm3d_dev_warp_catalog.inc` reads only 2S2H files (their
SHA-256 sit in the header): the scene table's human names in Better Map Select
order, the spawns of each scene's entrance group from the decomp entrance
table, and spawn names by meaning from the randomizer region exits
("1: from South Clock Town (to lower)"). MM3D keeps the N64 groups; its own
rows (34 added spawns, the empty N64 grotto slot 7, the telescope spawn that
leads to MM3D scene 0x73, the two fishing-hole groups) are stated in the
generator as indices. The runtime checks every row against MM3D's live entrance
table (0x69FB44) as before. `test_generate_mm3d_dev_warp_catalog.py` checks the
committed file and, with `TRIAEVUM_2S2H_ROOT`, regenerates it byte-identically.

The tab adds, for a title that declares them:

- **Zones**: 69 curated destinations by category (each proven by id in the
  generator's explicit `PROVEN_ZONES` list after a reviewed live receipt; a new
  zone is shown "(unverified)" until its id is added) (Clock Town, Termina Field,
  the four regions and their temples, Ikana, the Moon, boss lairs, interiors,
  mini-dungeons, each with a suggested day, time and form); Warp (an error
  that keeps it from queuing is shown below it), and
  **Start here on launch** (the boot zone, taken once when gameplay first
  starts, after a file or a savestate loads; it replaces a boot warp point
  and the reverse).
- **Arrival**: Day (1-3), Time (06:00 ... 05:00) and Form (Human, Deku, Goron,
  Zora, Fierce Deity), each keep, zone preset or a value. The Debug Warp
  Screen's Warp applies them too (a preset keeps the value there).

Arrival writes, in the same rollback transaction as the transition and before
the trigger is published: day Common+0x14, form Common+0x26 with the equipped
mask Common+0x08 cleared, and the time staged in nextDayTime (Common+0x36A0),
the Sun's Song reload path: the new scene's Play_Init copies it into the time
(Common+0x20) and the sky time before it derives isNight (Common+0x0C; MM3D:
night before 06:00 or from 18:00) and before the actors initialise. The scene
being left keeps its time: written straight into the time, its day/night actor
(En_Test4) saw a 06:00 crossing from night and ran "Dawn of a new day" (the
moon crash on day 3). Play_Init advances the day when nextDayTime is exactly
noon, so noon is staged as 0x8001. eventDayCount stays, as 2S2H's map select. A
day or time write is refused while nextDayTime is staged; Fierce Deity only in
the boss lairs. A day is accepted only together with a time: MM3D has no native
field that stages an arbitrary day (Play_Init only advances it by one, at noon,
for NEXT_TIME_DAY), so the day is written at once and is always paired with a
staged time, giving the new scene's Play_Init and En_Test4_Init one complete
day and time to start from (isNight, sky, the day 3 bells), as the map select
and Song of Double Time do. A day alone is refused by Apply, disables the Warp
buttons (the title's `ArrivalError`) and is a hook error for an entrance; every
zone preset with a day has a time. After arrival the runtime checks day, time (within 0x800 units),
isNight, form and mask, and fails the warp naming the field that differs. That
check, and the settle countdown, wait until gameplay resumes: an arrival during
an entrance cutscene stops the arrival timeout at once, but the scene is not
settled until its cutscene ends (status "waiting for the entrance cutscene to
end"); a cutscene that lasts 3600 more guest frames is reported distinctly as
`arrived_in_cutscene` (the lab answers `arrived-in-cutscene`), with the options
unchecked. Destinations whose scene sets its own clock (Cutscene Scene 0-2 and
4, the Moon 0-1, `MM3D_OWN_CLOCK`) show a warning, not a failure, when a time is
requested, and their arrival does not compare the time. Each
axis has its own gate in `Mm3dDevProof`; while a gate is off a zone's preset
for it keeps the current value and an explicit value is refused.

Agent hook: `TRIAEVUM_DEV_WARP=mm3d:<entrance>[:day=<1-3>][:time=<HHMM>][:form=<human|deku|goron|zora|deity>]`
or `mm3d:zone=<id>[:...]` (an entrance keeps what it does not name, a zone
takes its preset; `keep` overrides a preset). The log lines add the arrival
request, position, yaw and "Day 2 12:00, Human, health 320" (16 per heart).

"Launch directly into a zone" is a base savestate made from the 100% save plus
the boot zone or the hook, e.g. `TRIAEVUM_DEV_WARP=mm3d:zone=stock-pot-inn` with
`--load-state saves/states/sct_day1_idle.oot3dsav`: the warp is requested at
the first gameplay frame and arrives about 145 guest frames later.

Verification: `mm3d_dev_tools_tests` (fake guest): gates off refuse with zero
writes, arrival writes precede the transition and roll back with it, the
nextDayTime and Fierce Deity refusals, the arrival checks and the 06:00 night
rule, zone/catalogue consistency, the hook grammar, the boot zone taken once,
persistence beside SoH's keys, and an arrival whose time did not take failing.
`oot3d_dev_warp_tests` stays at its 32 checks.

Live, 2026-11-06, developer launch from `sct_day1_idle` (100% EUR save,
Authentic preset, no input, each run under the crash/timeout watchdog), all
requested at guest frame 1081, arrived at 1226-1227, settled 90 frames later:

- `mm3d:zone=stock-pot-inn` with the gates off: Stock Pot Inn, current day,
  time and form kept;
- `mm3d:0xD620:time=2200`: North Clock Town at 22:00, night flag set, night sky
  and the HUD clock at 10:02 PM;
- `mm3d:0xD210:day=2:time=1200`: East Clock Town, day 2 at noon with day 2's
  rain and no dawn card;
- `mm3d:zone=deku-palace`: Deku Link with the Deku action labels;
- `mm3d:zone=odolwas-lair:form=deity`: Fierce Deity in Odolwa's Lair (the B
  button stays empty when the save never used the mask);
- a persisted boot zone (Milk Bar, preset day 1 22:00) taken at the first
  gameplay frame without any hook;
- a boot warp point at East Clock Town spawn 1's place: respawnFlag -8 arrives
  through spawn 0's entrance (0xD200) exactly at the point (742, 100, 240, yaw
  0x4000, room 0), health 320 unchanged 90 frames later. Warp points, their
  "+" and Boot are therefore enabled (`WarpPointRespawn`).

The F1 actual-widget smoke (developer build with MM3D) drives the Zones and
Arrival widgets (12,468 assertions). OoT3D regression on the same tree: its 32
unit checks and a live Hyrule Field "Near Drawbridge" adult night warp from the
play quick save (arrived in 70 guest frames, adult, night flag 1, health 48
unchanged), Authentic preset.

### Live sweep (2026-11)

The developer lab drives MM3D too (`TRIAEVUM_DEV_LAB`, title-neutral ops
only): `{"op":"script","script":"mm3d:zone=deku-palace"}` runs the title's
TRIAEVUM_DEV_WARP script, the same WarpTo/WarpToZone requests as the tab's Warp
buttons, and acknowledges once settled with the scene, entrance and arrival
state; `{"op":"panel","tab":"Dev Tools"}` shows F1 on that tab so a lab
`capture` records it (the capture includes ImGui). The lab's tab request,
F1 show/hide and `TRIAEVUM_CRASH_DIALOG=0` exist only in developer builds
(`TRIAEVUM_RENDERER_DEVELOPER_TOOLS` in the shared runtime). The MM3D work tree's
`tools/devwarp_verify.py` runs a plan through `run_safe.sh` (its watchdog and
GPU lock): one warp after another from `sct_day1_idle`, a fresh process after a
fault or a warp that never left a cutscene, one rgb8 JPEG per warp with the
expected place and the observed result burned in.

Results (`analysis/devwarp/verify_2026-11`, `summary.tsv` per entry):

- Options: every day, every time choice and every form, Fierce Deity in the
  five lairs, keep on all three axes, and the Fierce Deity refusal outside the
  lairs: all as requested.
- Zones: all 69 arrived at their entrance with their preset and are proven.
  Clock Tower Rooftop arrives in the final-night scene (Skull Kid's dialogue);
  Pirates' Fortress (interior) is guarded (the guards stop Link at once); the
  Moon sets its own clock (07:45), so its zone keeps the day and time.
- All 490 destinations, each with day 1, 12:00 and Human: 468 settled; 6
  arrived in scenes that set their own clock (Cutscene Scene 0-2 and 4, the
  Moon 0-1; a requested time is now a warning there); Deku Palace 4 ("from
  Deku Shrine") ends in Deku Shrine (spawn 0) instead, twice, not yet
  explained, so it is refused with that reason until explained; 15 end MM3D's
  title process (svcBreak at 0x14C210 or a guest dispatch exit), each
  reproduced alone in a fresh process. Those 16 are `MM3D_REFUSED` in the
  generator: shown disabled with the reason, refused by Apply before any write
  (its error is the reason) and so by the hook.
- Spawn names checked by meaning on the Termina Field and South Clock Town
  sets (each "from X" faces the exit to X). A spawn is never named after its
  own scene (an exit within the scene); such a spawn reads "n: spawn".

Two fixes came from the sweep: the time staging above (night-to-day warps
arrived on the next day at 06:01 or after a "Dawn of" reload), and an arrival
during an entrance cutscene (Twinmold's intro outlasts the 900-frame window)
now counts for the timeout, as the title requires only the entrance and the
player (the options check and settling wait for gameplay, above). The
Inverted Stone Tower zone uses spawn 1 (spawn 0 is the flip from the upright
tower and lands back on it).
