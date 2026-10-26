# Where TriAevum keeps your files

This page lists every folder TriAevum uses, per platform, as Forge's
**Data & backups** page resolves it today. It also says what a backup holds
and what Forge never deletes. For everyday steps, see the
[user guide](USER_GUIDE.md).

The source of truth is the inventory in
`tools/triaevum_forge/crates/forge-core/src/inventory.rs`. The same list is
printed by the command-line tool: `triaevum-forge inventory` (on Windows,
`forge\triaevum-forge.exe inventory` in your TriAevum folder).

## Three roots

Every location is resolved from three roots:

- **Package** (`P`): the files you downloaded. Read-only on AppImage,
  Flatpak and Android.
- **Activation** (`A`): the writable folder that holds the installed game.
  The game runs with `A` as its working folder.
- **Data** (`D`): your private data folder, `A/data`.

| Platform | Package `P` | Activation `A` | Data `D` |
|---|---|---|---|
| Windows (portable ZIP) | `<your TriAevum folder>` | same as `P` | `<your TriAevum folder>\data` |
| Linux AppImage / Steam Deck | inside the AppImage (`usr/lib/triaevum`) | `$XDG_DATA_HOME/TriAevum`, or `~/.local/share/TriAevum` when the variable is unset | `A/data` |
| Linux Flatpak | `/app/lib/triaevum` in the sandbox | `~/.var/app/io.github.coccofresco.TriAevum/data/TriAevum` (the sandbox's `$XDG_DATA_HOME/TriAevum`) | `A/data` |
| Android | the APK | the app's external files folder (`getExternalFilesDir`, usually `Android/data/org.triaevum.android/files`) | `A/data` |

A plain Linux folder (not AppImage, not Flatpak) behaves like Windows: `A`
is the package folder.

## The data tree

Paths are relative to the roots above. "Backup" says what a backup does with
each location by default.

### Your files

| What (as Data & backups names it) | Where | Backup |
|---|---|---|
| Saves | `D/savedata/` | Always |
| Save states | `D/savestates/` on every platform. A folder chosen in the save-state settings (`Directory` in `savestates.json`) wins. Windows installations made before this version kept them in `P/savestates/` (beside `TriAevum.exe`): see [Moving to the data folder](#moving-to-the-data-folder). | Always |
| Save states left in the earlier folder | the default folder, when you chose another one and the default still exists; on Windows also `P/savestates/` after its states were copied to `D/savestates/` | If you choose it |
| Settings folder | `D/config/` | Always |
| Game settings file | `D/config/TriAevum.json` (Android: `TriAevum.android.json`) | Always |
| HUD layout | `D/config/topscreen_ui.json` | Always |
| Controls | `D/config/oot3d_controls.json` | Always |
| Game language | `D/config/game_language.json` | Always |
| Mod choices | `D/config/romfs_mods.json` | Always |
| Save-state list | `D/config/savestates.json` | Always |
| Moves the game made | `D/config/migrations.json` (for example the save-state copy below) | Always |
| Environments | `D/config/environments/` | Always |
| F1 window layout | `A/data/imgui.ini` (or `A/imgui.ini` when `A/data` does not exist) | Always |
| Touch controls (Android) | the app's shared preferences; a backup stores them as JSON | Always |
| Android display settings | `A/TriAevum.android.host.json` | If you choose it |
| Your texture pack | see [Texture packs](#texture-packs) | If you choose it |
| Dumped textures | see [Texture packs](#texture-packs) | Never |
| Mods | `D/mods/` | If you choose it |
| Downloads | `D/downloads/` | If you choose it |

The settings files sit beside the game settings file, so if a launch profile
moves `TriAevum.json`, the controls, language, save-state list and
environments move with it. Forge's own mod choices always stay in
`D/config/`.

### Made by Forge from your ROM (never backed up)

| What | Where |
|---|---|
| Game data kept from your ROM | `D/sources/` |
| Prepared game | `D/titles/` |
| Prepared caches | `D/cache/` |
| Renderer cache | `D/cache/renderer/` unless the launch profile names another folder |
| Graphics preparation data | `D/shader-seeds/` |
| Interior lighting data | `D/interior-sun/` |
| Translator cache | `D/translator-cache/` (only when present) |

These are made from your ROM. They must not be shared, and Forge can make
them again. `D/sources/` is what lets Update and Repair work without your
ROM: if it is removed, Forge asks for the ROM again.

### Logs and reports (never backed up)

| What | Where |
|---|---|
| Game logs | `D/logs/` (`oot3d_native_game.log` and earlier ones, `last-recovery.json`, `early-errors.log`; Android also `native-stdout.log` and `native-stderr.log`) |
| Forge logs | `D/logs/` (`forge.log` and up to four older ones, `game-stderr.log`, `last-game-run.json`, `preflight-error.json`) |
| Crash reports (Windows) | `D/logs/crashes/` |
| Earlier logs and crash reports | `A/logs/` and `D/savedata/crashes/` from earlier versions: still listed, never backed up, left in place |
| Startup reports (Android) | the app's internal `files/diagnostics/` |

On Windows all logs are therefore in `<your TriAevum folder>\data\logs`.
Forge's **Diagnostics** page reads them, and its support bundle picks the
files it needs (see [TRIAEVUM_DIAGNOSTICS.md](TRIAEVUM_DIAGNOSTICS.md)).

The game writes its log in `D/logs` whatever folder it is started from, and
even when the package folder is read-only. If it stops before it knows that
folder (for example with an unreadable launch profile), the message goes to
`triaevum-early.log` in the installation's state folder (Windows `D`,
elsewhere `A`; without one, the system's temporary folder `TriAevum/`), and
the next start moves it into `D/logs/early-errors.log`. A log folder that
cannot be written falls back to the temporary folder's `TriAevum/logs`.

An installation that Forge has not updated since this version (its launch
profile has no `--log-directory`) still writes game logs to `A/logs/` and
crash reports to `D/savedata/crashes/`; Data & backups shows them as "older
version". Update or Repair writes the new launch profile.

### Installation records (never backed up)

| What | Where |
|---|---|
| Launch profile | Windows: `D/TriAevum.launch.json` (an older installation's `A/TriAevum.launch.json` is still honoured). Linux: `A/TriAevum.launch.json`. Android: `A/TriAevum.android.launch.json` |
| Title library record (Android) | `A/TriAevum.android.title.json` |
| Title plugins (desktop) | `A/private-plugins/` |
| Activation journal, backups and the game session lock | `.forge-activation/` (Windows: in `D`; elsewhere: in `A`) |
| Installation lock | `.forge-activation.lock` beside it |
| Launch marker | `TriAevum.json.launching` beside the game settings file, while a launch is in its first minute |
| Installed game record | `D/active-title.json` |
| Runtime state | `D/runtime-state.json` |
| Restore snapshots | `D/backups/restore-<date and time>/` |

The game holds `.forge-activation/game-session.lock` while it runs. Forge
reads it to refuse a backup, restore, cleanup, update or repair while the
game is open.

### Texture packs

The game reads a custom texture pack from the first of these that is set
(each of the load and dump folders on its own):

1. the environment variables `OOT3D_AZAHAR_LOAD_DIRECTORY` /
   `OOT3D_AZAHAR_DUMP_DIRECTORY`;
2. the folders chosen in F1 (stored in `TriAevum.json`,
   `Graphics.TexturePacks.Azahar.LoadDirectory` / `DumpDirectory`);
3. `OOT3D_AZAHAR_USER_DIRECTORY`, when set: its `load/textures/<title id>/`
   and `dump/textures/<title id>/`;
4. the installation's own folders, `D/textures/load/<title id>/` and
   `D/textures/dump/<title id>/`. A pack that is already in the Azahar
   emulator's folder (below) keeps loading from there while the
   installation's load folder holds no file, so an installed pack never
   disappears; once a pack is imported there (Forge > Enhancements, F1
   Import...), the installation's folder wins and the Azahar folder is an
   inactive row on Data & backups ([enhancements](TRIAEVUM_ENHANCEMENTS.md));
5. the Azahar emulator's user folder: `load/textures/<title id>/` and
   `dump/textures/<title id>/` (installations whose launch profile does not
   name the folders of step 4 yet).

The Azahar emulator's user folder is:

| Platform | Azahar user folder |
|---|---|
| Windows | `%APPDATA%\Azahar` |
| Linux AppImage | `$XDG_DATA_HOME/azahar`, else `~/.local/share/azahar` |
| Linux Flatpak | `~/.var/app/io.github.coccofresco.TriAevum/data/azahar` (the sandbox's data folder) |
| Android | `A/Azahar` |

The title id is the one of the game you installed, read from your ROM:
`0004000000033600` for a European cartridge, `0004000000033500` for a North
American one (its code runs the European title module, but its texture
folder keeps its own id). `OOT3D_AZAHAR_TITLE_ID` names another. An
installation whose launch profile has no `--title-id` yet uses
`0004000000033600`.

The Azahar emulator's folders are outside the installation, so they are
yours: Forge never moves or deletes them.

### Moving to the data folder

Forge writes the new locations into the launch profile (the
`--log-directory`, `--crash-directory`, `--savestate-directory`,
`--texture-load-directory`, `--texture-dump-directory` and `--title-id`
options) for a game version that supports them, at install, Update or
Repair. An older launch profile keeps working with the earlier folders.

On Windows the first start with the new profile copies your save states
from `P/savestates/` to `D/savestates/`:

- only when you did not choose a save-state folder yourself;
- every file is copied and compared byte for byte before it counts; a file
  that already exists in `D/savestates/` is never overwritten (an identical
  one is skipped, a different one stays only in the old folder and is
  listed in the old folder's `README-moved.txt`);
- the last state (F8) then points to its copy, and the copy is recorded in
  `D/config/migrations.json`, so the next start copies nothing;
- nothing is deleted: `P/savestates/` keeps every file, with
  `README-moved.txt`. Delete it yourself once you have checked your states;
- if anything fails, the game keeps using `P/savestates/` and tries again
  at the next start.

## What a backup holds

A backup is one zip (`triaevum_backup_v1`): a `manifest.json` (version,
game, installed revision, platform, every file with its size and SHA-256)
and the files under `files/`.

- **By default:** every location marked "Always" above: saves, save states,
  settings and, on Android, the touch controls.
- **Only if you choose it** (owner decision 2): your texture pack, mods,
  downloads, the Android display settings and save states left in an
  earlier folder. Mods and downloads can be downloaded again, so they are
  never selected by default.
- **Never:** dumped textures, everything made from your ROM, caches, logs,
  crash reports and installation records. A folder that is backed up leaves
  out what is nested in it and never backed up (for example the crash
  reports inside the save folder).

Before a restore writes anything, Forge checks the whole backup: the game,
safe file names, and every file's size and hash. It shows a preview, copies
every file it will replace or remove into `D/backups/restore-<date and
time>/snapshot/`, and keeps a journal there. If the restore stops halfway,
Forge puts the previous files back before the game can be played again.
The snapshot stays after a successful restore.

A backup made on another platform restores saves, save states, the HUD
layout, the game language and environments. Display, controls and window
settings stay as they are on the new system (owner decision 13). A setting
that names a folder that does not exist on the new system is left as it is.

## What Forge never deletes

- **Your saves, settings, mods and downloads.** No cleanup action deletes
  them. "Remove prepared game" and uninstall help both recommend keeping
  mods and downloads, so a new installation does not download them again.
- **Anything outside the installation's own folders,** for example a
  texture pack in the Azahar folder, or a save-state folder you chose
  elsewhere.
- **Anything you chose yourself** (shown as "Set by you" on Data & backups),
  even inside the installation.
- **Restore snapshots** in `D/backups/`.

Forge deletes only on these actions, always after showing the same plan
first:

| Action | Deletes |
|---|---|
| Clear renderer cache | the renderer cache |
| Remove prepared game | everything "made by Forge from your ROM" above, and `D/active-title.json` |
| Restore | the files the preview lists as replaced or removed, after copying them into the snapshot |

Forge never uninstalls itself. **Data & backups > Uninstall help** shows
the steps for your platform with the real folders:

| Platform | Uninstall |
|---|---|
| Windows | Back up, then delete `<your TriAevum folder>`. Nothing else was installed. |
| Linux AppImage | Back up, delete the AppImage, and delete `A` if you do not want your saves any more. |
| Linux Flatpak | Back up, then `flatpak uninstall io.github.coccofresco.TriAevum`. Your files stay in `A`; delete them yourself if you want. |
| Android | Back up (export) first. Uninstalling removes the app's own storage, including `A`. |

The uninstall help also lists every folder of yours outside the
installation (for example a texture pack) and says Forge never deletes it.
