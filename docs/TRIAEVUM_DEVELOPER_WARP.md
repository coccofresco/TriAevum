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
