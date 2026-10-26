# TriAevum user guide

This guide is for players. It explains how to install TriAevum — Echoes of
Time, where your files are, and how to update, repair, back up and remove it.
The last part explains Forge's messages in plain words.

It follows the current source tree. A few things here are newer than the
latest published release; the release notes say what each release contains.

The full list of folders and what a backup holds is in
[TRIAEVUM_USER_DATA.md](TRIAEVUM_USER_DATA.md).

## What you need

- Your own decrypted Ocarina of Time 3D cartridge dump, as a `.3ds` or `.cci`
  file. A folder with extracted `code.bin`, `exheader.bin` and `romfs.bin`
  also works. Forge checks the contents, not the file name.
- A supported revision. **About** in Forge lists the revisions this package
  supports.
- A desktop: Windows or Linux on x86-64, with a Vulkan graphics card and
  current drivers. Or an Android phone, when an APK is offered.

TriAevum ships no game data. Forge never asks for encryption keys and never
downloads any game files.

## Two programs: Forge and the game

- **TriAevum Forge** prepares the game from your ROM, and later updates,
  repairs and backs it up. Its pages are **Home**, **Enhancements**,
  **Data & backups**, **Diagnostics** and **About**.
- **TriAevum** is the game itself.

The first preparation takes a few minutes, because Forge also prepares the
graphics for your card. Later starts are quick.

## Windows (portable ZIP)

### Install and first launch

1. Download the **Windows-x64 ZIP** from the release page. Do not use
   GitHub's "Source code" archive.
2. Extract it into a folder you can write to, with a short path (for
   example a folder directly on a drive). Windows limits path length; if the
   folder is too deep, Forge tells you how many characters to remove.
3. Open `TriAevumForge.exe`.
4. On **Home**, choose **Browse...** and select your ROM. When it is
   recognized, choose the game language.
5. Press **Prepare** and wait. When it is done, press **Play**.

After the first preparation, `TriAevum.exe` is in the same folder as
`TriAevumForge.exe`. You can start the game from either one.

TriAevum is portable: nothing is installed outside its folder, and there is
no installer and no registry entry.

### Where your files are

Everything is inside the folder you extracted (here called
`<your TriAevum folder>`):

| What | Where |
|---|---|
| Saves | `<your TriAevum folder>\data\savedata\` |
| Save states | `<your TriAevum folder>\data\savestates\` |
| Settings | `<your TriAevum folder>\data\config\` |
| Game logs | `<your TriAevum folder>\data\logs\` |
| Forge logs | `<your TriAevum folder>\data\logs\` |
| Your texture pack | `<your TriAevum folder>\data\textures\load\<title id>\` |

The title id is `0004000000033600` for a European cartridge and
`0004000000033500` for a North American one. A texture pack you already
keep in the Azahar emulator's folder
(`%APPDATA%\Azahar\load\textures\<title id>\`) keeps working from there;
TriAevum only reads it.

Versions before this one kept save states in `<your TriAevum folder>\savestates\`
and game logs in `<your TriAevum folder>\logs\`. After an Update, the game
copies your save states to `data\savestates\` on its first start, checks
every copy and leaves the old folder as it was, with a `README-moved.txt`;
delete it yourself once you have checked your save states.

**Data & backups** in Forge shows every folder, with its size and an
**Open folder** button.

### Update

There are two ways. Both keep your saves and settings, and neither needs
your ROM.

<!-- TODO(WP-04B): verify this flow and its wording once the Windows ZIP
update lands (button names, signature check, downgrade question). -->

- **From Forge:** download the new ZIP, open `TriAevumForge.exe`, then go to
  **Home > Update > Choose the downloaded ZIP**.
- **By hand:** close the game and Forge. Extract the new ZIP into the same
  folder and let it replace the files. Open `TriAevumForge.exe` (not
  `TriAevum.exe`). Home says "A new TriAevum package is here". Press
  **Update**.

If your HUD layout differs from the standard one, Forge asks first:
**Keep mine** leaves it as it is, **Use standard** resets it. If the new
package is older than the installed one, Forge asks before installing it.
When the update is done, Forge lists what it changed in your settings.

### Uninstall

1. Back up your files (see [Back up and restore](#back-up-and-restore)).
2. Delete `<your TriAevum folder>`.

Nothing else was installed. If you used a texture pack in the Azahar folder,
it is still there; delete it yourself if you want. **Data & backups >
Uninstall help** shows the exact folders for your installation.

## Linux AppImage (and Steam Deck)

### Install and first launch

1. Download the **Linux-x86_64.AppImage**.
2. Make it executable: in your file manager's file properties, or with
   `chmod +x TriAevum-*.AppImage`.
3. Open it. The first time, Forge opens: select your ROM, choose the
   language, press **Prepare**, then **Play**.

From then on, opening the AppImage starts the game directly. To open Forge
again, start the AppImage with `--forge`. On systems without FUSE, add
`--appimage-extract-and-run`.

**Steam Deck:** prepare the game in Desktop Mode first. Then add the same
AppImage to Steam as a non-Steam game, and do not force Proton: this is a
native Linux program. In the shortcut's controller settings choose the Steam
Input template **Gamepad with Mouse Trackpad**: the sticks and buttons stay a
gamepad for the game, and the right trackpad moves the pointer and clicks, so
you can also use Forge in Gaming Mode. Forge also works with the keyboard
alone: Tab and Shift+Tab move between controls, Enter activates the
highlighted one, Esc closes a dialog. Forge repeats this under **Diagnostics >
Steam Deck and controllers**.

**No network?** If the HD textures or a mod cannot be downloaded, download the
archive from the mod page on another device, copy it anywhere on this machine
and pick it on **Enhancements** with **Select .zip...** or **Select .rar...**.
Forge checks it and keeps a copy in its downloads folder (see the table
below); the Enhancements page shows that folder with **Open folder**.

### Where your files are

Your files live outside the AppImage, in `~/.local/share/TriAevum/` (or
`$XDG_DATA_HOME/TriAevum/` if you set that variable):

| What | Where |
|---|---|
| Saves | `~/.local/share/TriAevum/data/savedata/` |
| Save states | `~/.local/share/TriAevum/data/savestates/` |
| Settings | `~/.local/share/TriAevum/data/config/` |
| Game logs | `~/.local/share/TriAevum/data/logs/` |
| Forge logs | `~/.local/share/TriAevum/data/logs/` |
| Downloaded archives | `~/.local/share/TriAevum/data/downloads/` |

A texture pack you add yourself goes, by default, into
`~/.local/share/TriAevum/data/textures/load/<title id>/` (`0004000000033600`
for a European cartridge, `0004000000033500` for a North American one). A
pack you already keep in `~/.local/share/azahar/load/textures/<title id>/`
keeps working from there.

### Update

1. Close the game.
2. Replace the AppImage file with the new one. If the file name changed,
   point your Steam shortcut or menu entry to the new file.
3. Open it. Because a new package is present, Forge opens instead of the
   game: press **Update**. Your ROM is not needed; saves and settings are
   kept.

### Uninstall

1. Back up your files.
2. Delete the AppImage.
3. If you do not want your saves any more, delete `~/.local/share/TriAevum/`.

## Linux Flatpak

### Install and first launch

1. Install the bundle:
   `flatpak install --user TriAevum-*-Linux-x86_64.flatpak`
2. Start TriAevum from your applications menu. The first time, Forge opens:
   select your ROM, choose the language, press **Prepare**, then **Play**.

Later starts open the game directly. To open Forge again:
`flatpak run io.github.coccofresco.TriAevum --forge`.

### Where your files are

The Flatpak keeps its files in its own sandbox folder,
`~/.var/app/io.github.coccofresco.TriAevum/data/TriAevum/`. Inside it, the
layout is the same as the AppImage's (`data/savedata/`,
`data/savestates/`, `data/config/`, `data/logs/`, `data/textures/`).

A texture pack you add yourself goes, by default, into
`data/textures/load/<title id>/` there. A pack you already keep in
`~/.var/app/io.github.coccofresco.TriAevum/data/azahar/load/textures/<title id>/`
keeps working from there.

The AppImage and the Flatpak do not share files. Moving from one to the
other: back up in one, restore in the other.

### Update

Install the new bundle with the same `flatpak install --user` command. The
next start opens Forge: press **Update**.

### Uninstall

1. Back up your files.
2. Run `flatpak uninstall io.github.coccofresco.TriAevum`.
3. Your files stay in
   `~/.var/app/io.github.coccofresco.TriAevum/data/TriAevum/`. Flatpak does
   not delete them. Delete that folder yourself if you do not want them any
   more.

## Android

### Install and first launch

1. Install the TriAevum APK. Android asks you to allow installing apps from
   the source you use.
2. Open **TriAevum Forge**. Press **Select ROM** and pick your ROM.
3. Choose the game language and press **Install**. The installation takes
   several minutes. It continues in the background, also while the phone is
   locked, and shows its progress in a notification.
4. Press **Play**.

### Where your files are

Your files are in the app's own storage, which Android removes when you
uninstall the app. On most phones that is
`Android/data/org.triaevum.android/files/` on the shared storage, with
saves in `data/savedata/` and settings in `data/config/`. Your touch control
settings are kept inside the app.

Use **Saves and backups** in the installer to copy your files out; you do
not need a file manager.

### Update

<!-- TODO(WP-09): verify the Android update flow once the installer shows
the installation state (InstallationStatus). -->

Install the new APK over the installed app. **Do not uninstall the app
first:** that deletes your saves. Then open TriAevum Forge. If **Play** is
not offered, select your ROM and press **Install** again; your saves and
settings stay.

### Uninstall

1. In **Saves and backups**, press **Back up your files…** and save the ZIP
   somewhere outside the app (for example your Downloads folder or a cloud
   drive).
2. Uninstall the app. This removes the app's own storage, including your
   saves, settings and the prepared game.

## Repair

Forge checks the installation every time it opens. When something is wrong,
**Home** says what and offers the fix:

- **Repair** rebuilds what is missing from the game data Forge kept from
  your ROM. Your saves and settings are kept. Your ROM is not needed.
- "Your game files are needed again to repair this installation" means the
  game data Forge kept is missing or damaged. Select your ROM again, then
  press **Repair**. You can still play until then, unless the prepared game
  itself was removed.
- **Check installation** reads the installation again, for example after
  another Forge finished.

**Data & backups** also has:

- **Rebuild prepared assets**: prepares the game again from the ROM data
  Forge kept, and installs it. Saves and settings stay. This can take
  several minutes.
- **Clear renderer cache**: deletes the renderer cache. The game builds it
  again, so the next start can take longer.

Repair, like Update, asks about your HUD layout first if it differs from the
standard one (**Keep mine** or **Use standard**).

## Back up and restore

A backup is one `.zip` file that you can keep anywhere.

**In a backup, always:** saves, save states and settings (graphics, HUD
layout, controls, game language, environments and the F1 window layout). On
Android, also your touch controls.

**Only if you tick them:** your texture pack, mods and downloads. Mods and
downloads can be downloaded again, so they are not ticked by default.

**Never:** the game data Forge made from your ROM, caches, logs and crash
reports. Forge makes them again, or they are not needed.

### Desktop (Windows and Linux)

- **Back up:** close the game. In Forge, open **Data & backups**, press
  **Back up your files...**, tick any extra items under "Also include", then
  press **Choose where to save...**.
- **Restore:** close the game. In **Data & backups**, press **Restore...**
  and pick the backup. Forge shows what it will replace, add and remove.
  Press **Restore these files** to go ahead.

### Android

Open **Saves and backups** in the installer:

- **Back up your files…** writes the ZIP to a place you choose.
- **Restore a backup or import saves…** restores a TriAevum backup. It also
  imports a ZIP of saves exported by TriAevum, or the game's own save files
  (`save00.bin` and following, `system.dat`).
- **Export saves only (for other apps)** writes only the save files.

### Good to know

- Forge refuses to back up, restore or clean up while the game is running:
  close the game first. Forge never closes it for you.
- Before a restore changes anything, Forge copies every file it replaces or
  removes into a restore folder inside `data/backups/`. Those copies stay
  until you delete them.
- If a restore is interrupted (power cut, crash), Forge puts your previous
  files back before you can play again. On the desktop, **Data & backups**
  shows **Finish the recovery**; Play is blocked until you press it. On
  Android this happens by itself when the installer opens.
- A backup from another system (for example Windows to Android) restores
  saves, save states, the HUD layout, the game language and environments.
  Display, controls and window settings stay as they are on the new system.
- A backup made for another game, or a file that is not a TriAevum backup,
  is refused, and nothing is changed.

## Removing only the prepared game

**Data & backups > Remove prepared game...** deletes the game data Forge
made from your ROM, to free space. Your saves, settings, mods and downloads
stay. To play again, select your ROM on Home, then press **Repair**.

## In the game

### First start

The game opens in a window that fits your screen. A help card shows the
most useful keys once; press OK (or B, or Esc) to close it.

### The in-game menu

- Open and close it with **Esc** on the keyboard, **R3** (click the right
  stick) on a pad, or a tap on **HOME** on Android.
- **The game pauses** while the menu is open, and continues exactly where it
  stopped when you close it.
- Move with the D-pad, the left stick or the arrow keys. Change a value with
  Left / Right. Confirm with A, Enter or Space. Go back with B or Backspace.
  Switch tabs with LB / RB (or Q / E). The mouse works too.
- The tabs are **Video**, **Graphics**, **Controls**, **Photo mode**,
  **Game** and **Help & folders**.
- A change is saved at once. The line under the rows says "Saved", or
  explains why it was not.
- **Video > Reset display settings** brings the window back to a safe
  default size and mode if the picture is wrong.
- **Game > Advanced settings (F1)** opens every setting. **Game > Quit game**
  closes the game. Quitting does not save your game progress: save in the
  game, or use a save state.
- **Help & folders** shows the controls of the device you are using, can
  show the help card again, and on the desktop opens your saves folder and
  your logs folder. On Android it tells you where saves and diagnostics are
  handled: close the game, then use the installer's **Saves and backups**
  and **Diagnostics**.

The button names in the menu follow the device you used last (Xbox,
PlayStation, Nintendo, Steam Deck, keyboard or touch). You can fix them in
**Controls > Button prompts**.

### Keys

| Key | Pad | Action |
|---|---|---|
| Esc | R3 | In-game menu |
| F1 | L3 + R3 | All settings |
| F5 / F8 | (Game tab) | Save / load a save state |
| F6 | (Photo mode tab) | Photo camera |
| F11 | | Fullscreen on / off |

On Android, a tap on HOME opens the menu and holding HOME opens all
settings.

## Optional enhancements

The **Enhancements** page in Forge offers:

- **HD UI textures** for the HUD and menus, in your game's language. Forge
  downloads the author's original archive, checks it and keeps it in your
  data folder. If the download fails, download the zip from the mod page and
  pick it with **Select .zip...**.
- **Optional mods**, such as a Link remodel. Forge downloads the author's
  archive and applies it to your local game data only.

A change on this page is applied by the next preparation: select your ROM on
Home and press **Prepare** again. Without enhancements, the game uses its
own original textures and models.

## Troubleshooting

### The game closed with an error

- On the desktop, the game shows a window "TriAevum could not continue" with
  the reason and the log folder. Forge then shows "The game closed with an
  error" on Home, with the end of the log.
- Press **Copy diagnostics** and include it in a bug report.

### The picture is wrong after changing display settings

If the game closes within its first minute, the next start resets the
display settings and says so. You can also use **Video > Reset display
settings** in the in-game menu. When you change the window mode or size, the
game asks "Keep these display settings?" and goes back by itself if you do
not answer.

### Reporting a problem

1. Open Forge's **Diagnostics** page.
2. Press **Save support bundle...** (Android: **Share diagnostics** in the
   installer's Diagnostics screen).
3. Attach the zip to your issue, with what you were doing when it went wrong.

The bundle holds logs, settings and Forge's records, with user names and
installation folders removed. It never holds saves, save states, game data,
mods or crash dump files. **Copy diagnostics** copies the same report as
text.

### Forge's messages

When something goes wrong, Forge shows a title, the cause and what to do
next. **Show details** shows the technical details. This table explains the
titles you may see.

| Forge says | What it means | What to do |
|---|---|---|
| A required input could not be read | A file Forge needs is missing, incomplete or unreadable. The details name it. | Select your ROM again. If the details name an installed file, press Check installation. |
| This input could not be read as decrypted game data | Forge found no readable game data in the file. It is often an encrypted dump. | Use a decrypted `.3ds` or `.cci`, or a folder of extracted data. |
| Encryption support is unavailable | Forge only works with decrypted files. | Use a decrypted ROM. |
| This input or operation is not supported by this build | The file type, or the action, is not supported. | Use a `.3ds` or `.cci` ROM, or an extracted-data folder. |
| This game revision is not supported / This ROM revision is not supported by this package | Your dump is a game version this package does not support. Renaming the file does not change that. | See **Supported revisions** in About, and use a matching dump. |
| This package is not compatible | The package was made for another system or device. | Download the package for your system. Your saves and settings are kept. |
| A compatible package component is missing or invalid | A file of the TriAevum package is missing or changed. | Download a clean copy of the package. Your data is kept. |
| A file did not pass verification | A file differs from what it should be. | For a package file, download the package again. For your own file, select it again. |
| Settings need attention / Settings could not be loaded | A settings file is damaged, or was written by a newer version. Forge keeps the file. | Restore a backup of your settings, or use the newer version. |
| The installation path is too long | Windows' path length limit would be exceeded in this folder. | Move TriAevum to a shorter folder. The details say how many characters to remove. |
| Not enough storage / Not enough storage to prepare the game | The drive is full, or does not have room for the preparation. The details give the amounts. | Free at least the amount shown, then press Retry. |
| Access to a required location was denied | The folder is read-only, or the system refused access. | Move TriAevum to a folder you can write to, then retry. |
| This destination cannot accept installation writes | The installation folder cannot be written. | Choose a writable folder. |
| Access to the selected ROM expired (Android) | Android no longer lets Forge read the file you picked. | Select the ROM again. |
| TriAevum files are in use / Close the game first | The game, or another Forge, is using the files. | Close the game, then retry. Forge never closes it for you. |
| Close TriAevum before updating | A running program holds a file the update must replace. | Close that program, then retry. Nothing was lost. |
| Another Forge is working on this installation | A second Forge window is busy. | Wait for it to finish, or close it. |
| The installation needs repair or recovery | Part of the installed game is not valid, or an earlier step was interrupted. | Press Repair on Home. |
| The game source is needed again | The game data Forge kept from your ROM is missing or damaged. | Select your ROM again, then Repair. Saves and settings are kept. |
| The game runtime could not be checked | The game's helper did not start. This alone does not mean your graphics card is the problem. | Open Diagnostics. If it names no cause, copy the report and download the package again. |
| The runtime could not start | A library the game needs is missing. The details name it. | Make sure the package is complete and your graphics drivers are installed. |
| The graphics device cannot run this configuration | Your graphics card or driver lacks something the game needs. | Check your driver, and keep the report for a bug report. |
| Preparation did not finish / A required preparation step failed | A step of the preparation failed. What was installed before still works. | Press Retry, or Repair. |
| Ready: graphics preparation incomplete | The game is installed, but an optional graphics step did not finish. | Play now, or retry the graphics preparation. |
| The optional enhancement is not ready | The HD textures or a mod could not be downloaded or checked. The game uses its own assets. | Continue without it, or download the archive yourself and select it. |
| The game is installed, but this setting was not saved | Installing worked, but one setting could not be written. | Retry, or check that the settings folder is writable. You can play. |
| Recovery needs more information | Files needed to undo an interrupted change are missing. Play is blocked. | Save a support bundle and report the problem. Do not delete files by hand. |
| A restore was interrupted | Forge stopped while restoring a backup. Your previous files are kept. | Open Data & backups and press Finish the recovery. |
| This backup is for another game / This backup cannot be restored | The file is not a TriAevum backup for this game, or it is damaged. Nothing was changed. | Choose another backup file. |
| This installation uses an older lock format | An older Forge left a lock file. | Close every Forge, then press Check installation. Forge does not delete the lock by itself. |
| Display settings were reset after an early exit | The game closed during startup, so its display settings were reset. | Set them again in F1 > Display, carefully. |
| Operation cancelled | You stopped the operation. It stopped at a safe point. | Start it again when you want. |
| Forge could not complete this operation / Forge could not process this request | Something unexpected happened inside Forge. It is not your ROM or your drivers. | Copy the diagnostics, reopen Forge and report the problem. |
