# Android Installer and First-Play Tests

The device procedure for the installer (`InstallerActivity`, `InstallService`, process
`:forge`), updates and first play (UX plan WP-09, WP-04A Android). Unit tests cover the state
decisions on the desktop; these checks need a phone. Record results with the APK's commit, the
device model and Android version only: never serial numbers, account names or local paths.

## Before touching the phone

- The phone belongs to someone. Before every `adb install`, `am start` or tap, check the app
  in front and stop if it is not TriAevum or the launcher:

  ```sh
  adb shell dumpsys activity activities | grep -i resumed
  ```

  An install kills a running game; a launch takes over the screen.
- Back up the app's files off the device first; never overwrite real saves without a copy.
  `adb pull` cannot read another app's folder; read it as the app:

  ```sh
  adb exec-out run-as org.triaevum.android sh -c \
    'cd /storage/emulated/0/Android/data/org.triaevum.android/files && tar cf - .' > backup.tar
  ```

  Restore with the reverse (`sh -c 'cd ... && tar xf -' < backup.tar`). `adb push` into
  `Android/data` silently does nothing on recent Android versions; stream through `run-as`.
- Use `MSYS_NO_PATHCONV=1` for adb commands from Git Bash on Windows, so device paths are not
  rewritten.

## Build

The APK needs a Forge package (`FORGE_PACKAGE`, the output of
`tools/triaevum_release/android_forge_package.py`) for the installer tests; without one the
installer only plays a title prepared beforehand. See `README.md` ("Forge on the device").

An update test needs two APKs whose runtime or title library differ (any native change, or two
commits): install the older one first.

## Cases

Each case lists what to do and what the installer must show. The state card always shows the
shared installation state (`ForgeNative.inspect`) and its explanation; Play is visible only for
"Ready to play" (or "Your ROM is needed again" while the installed title can still start).

1. **Fresh install with the picker.** Open TriAevum, Select ROM, pick a decrypted `.3ds`/`.cci`.
   The card names the revision and its languages; Install. Expect "Installing the game", a
   progress notification, then "Ready to play" and Play.
2. **Install with the screen locked.** Start an install, lock the phone right away, wait several
   minutes, unlock. Expect "Ready to play". Samsung's freezer stops background processes that
   have no foreground service; if the job stalled (no new lines in `data/logs/forge.log`, see
   below), the foreground service did not start: note the Android version and the log.
3. **Process killed mid-job.** Start an install and kill `:forge` while the log shows the stage
   (`extract`, then `shaders`, then `activate`):

   ```sh
   adb exec-out run-as org.triaevum.android sh -c \
     'tail -n 5 /storage/emulated/0/Android/data/org.triaevum.android/files/data/logs/forge.log'
   adb shell run-as org.triaevum.android sh -c 'kill -9 $(pidof org.triaevum.android:forge)'
   ```

   Reopen TriAevum. Expect "The last installation did not finish" with "Retry" when the
   document provider let the app keep its read grant (most local providers do), or "Select the
   ROM again" otherwise; "Your installed game is unchanged" with Play when a previous install
   existed. Killed during `activate`: the card may instead say the installation needs a repair,
   with Repair. Retry or Repair must reach "Ready to play". Never a revived job.
4. **Notifications denied.** `adb shell pm revoke org.triaevum.android
   android.permission.POST_NOTIFICATIONS` (or deny the prompt), then install. The install runs;
   the yellow banner "Notifications are off..." is shown while it runs, with "Turn on
   notifications" opening the app's notification settings.
5. **APK replaced (update).** With case 1 done on the older APK, `adb install -r` the newer one
   and open TriAevum. Expect "TriAevum was updated" (or "A new TriAevum version is installed"),
   then the update starts by itself: "Installing the update", no Play while it runs. If the HUD
   layout differs from the standard profile, "Reset the HUD layout to the standard profile?"
   asks first (Keep mine keeps it). Expect "Updated ... Your saves and settings were kept." and
   "Ready to play" without picking the ROM. Kill `:forge` during the update: reopening shows
   "The update did not finish" with Update.
6. **Backup round trip.** Saves and backups, "Back up your files...", save the ZIP; change or
   delete a save; "Restore a backup or import saves...", pick the ZIP, confirm. Saves, save
   states, settings and touch controls come back; the replaced files are in `data/backups`.
7. **First-play card.** After a fresh install, Play: once the game is presented, the card
   "Tap HOME: game menu. Hold HOME: all settings." appears once. Quit and Play again: no card.
   The menu's Help tab shows it again on request.
8. **Menu to installer pages.** In game, HOME, Help tab, "Export saves": "Close the game?",
   then "Close and open". The game ends and Saves and backups opens (the stderr log has
   `TRIAEVUM_INSTALLER_PAGE saves requested`). Same for "Share diagnostics" (Diagnostics opens).
9. **Touch regressions (host layer).** Guest input timelines do not drive the overlay, so check
   on the phone: a tap on a menu or the file select presses only that item (touch is not a
   mouse click on A: `oot3d_controls.json` has mouse input off on Android); taps during a scene
   transition are not lost or doubled; the Back key does what the in-game menu documentation
   says and never ends a session without the Quit confirmation (record what it does).
10. **HD interface pack, Wi-Fi.** On an unmetered Wi-Fi network, the "HD interface pack
    (optional)" card says it is not on the device and shows its size, author and license.
    Nothing downloads until Download is tapped (owner decision 5). Tap it: "Getting the HD
    interface pack", progress in sizes ("120 MB of 452 MB") on the screen and in the
    notification, no Play while it runs. Expect "The HD interface pack is on this device...",
    the card then says "Downloaded. It is applied the next time you install the game from your
    ROM"; Select ROM and Install, then in game the HUD uses the HD textures. Cancel during the
    download: "Cancelled; the HD interface pack was not stored", the card offers Download again,
    and `data/downloads` holds no pack (a leftover hidden `.part` file from a killed process is
    never taken for the pack).
11. **HD pack, mobile or metered network.** Turn Wi-Fi off (mobile data on), or mark the Wi-Fi
    network as metered in the system's network settings. Download asks "Download on a mobile
    network?" with the size; Cancel downloads nothing, "Download anyway" downloads. There is no
    Wi-Fi-only rule.
12. **HD pack offline.** Airplane mode, Download: "No internet connection" and nothing starts.
13. **HD pack import.** Download the pack's zip from its page on any device, copy it to the
    phone, "Import .zip...", pick it. Expect the same "is on this device" result; a different
    zip ends with an explanation that it is not the original archive and nothing is kept.
14. **Installer language (decision 8).** System language Italian, no ROM yet: the installer is
    in Italian. Select a USA ROM (English, French, Spanish): the installer switches to English
    (Italian is not one of its languages) without losing the recognized ROM or the chosen game
    language. "Language: ..." offers only the ROM's languages; pick Español: the installer is
    in Spanish now and after a restart (kept as `ui_language` in `data/config/forge.json`). A
    European ROM afterwards: Spanish stays. Android 13+ applies it as the app's own language;
    a running game is not restarted by the change.

## Evidence

Keep per case: the screenshot of the state card (`adb exec-out screencap -p`), the tail of
`data/logs/forge.log`, `adb logcat -d -s TriAevumForge TriAevum` and, for case 7/8,
`logs/native-stderr.log`. Strip device identifiers before attaching anything to an issue.
