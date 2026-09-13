# Linux AppImage and Steam Deck

Decision: 2026-09-14. Replace Flatpak with one Linux x86-64 AppImage for desktop
Linux and Steam Deck Gaming Mode. Windows portable ZIP is unchanged. This is
the next-package target, not a claim of Steam Deck Verified certification.

## User Flow

1. Download the AppImage, make it executable, and open it in Desktop Mode.
2. The same bundled Forge asks for a decrypted personal `.3ds` / `.cci` ROM.
   It imports content, prepares the bundled shader corpus for the local GPU,
   and downloads the TopScreen texture package as already disclosed by Forge.
   No compiler, SDK, second installer or Flatpak runtime is needed.
3. Add **that AppImage** as a non-Steam game. Launch it from Gaming Mode with
   Steam Input enabled, using a Gamepad layout. Do not force Proton for this
   native Linux executable. Once prepared, launching opens the game directly.
4. `--forge` explicitly returns to preparation/repair. For F1 configuration,
   a Steam Input back button can send F1 and a trackpad can send mouse/click.
   Keep the ordinary gamepad controls mapped as a gamepad, not WASD.

The existing SDL controller owner handles sticks, buttons, triggers and hotplug.
AppRun must preserve Steam's controller mappings, preload/overlay environment,
display selection, and Gamescope session. No separate Deck input implementation,
forced X11, root access, Steam account-file edits or bundled GPU drivers.
Gyro availability is separate from gamepad compatibility: Steam Input may map
gyro to mouse/right-stick; do not promise direct sensor access for a virtual pad.

## Storage and Updates

`AppDir/usr/lib/triaevum` contains only audited immutable package resources.
`$XDG_DATA_HOME/TriAevum`, or `$HOME/.local/share/TriAevum` when unset, owns
activation, receipts, data, saves, profiles and caches. Mount path/name changes
must not change this directory. AppRun also works from an extracted AppDir.
The shared transactional Forge update path rebinds packaged runtime updates;
never patch a launch profile without updating its activation receipt.

Legacy Flatpak data remains under
`~/.var/app/io.github.coccofresco.TriAevum/data/TriAevum`.
Do not move/delete it automatically or let two installations write it together.
Migration must use the existing verified import/path-migration contract and be
qualified before offering a one-click migration. Uninstalling Flatpak is not
required to test AppImage in an independent data root.

## Publisher Boundary

`tools.triaevum_release.appimage_package` stages an AppDir from the existing
Linux release manifest, auditing before and after the copy. The wrapper adds
only AppRun, a desktop entry and a title-neutral icon. Source, donor notices,
precompiled catalog and shader corpus remain in the audited payload.

```sh
python -m tools.triaevum_release.appimage_package \
  --package /path/to/audited-linux-package --output /path/to/TriAevum.AppDir
```

Build type-2 AppImage with publisher-pinned appimagetool and runtime versions;
record their SHA256 and license/source provenance in qualification. Do not
download tools on the end user's machine. Provide the standard extract-and-run
route for hosts without FUSE, and test it as well as mounted launch.

Wrapping Flatpak payloads alone is **not** portability qualification. Current
alpha.2c ELF runtime and frozen Python require GLIBC 2.38; verify the selected
SteamOS baseline and document the Linux minimum. Bundle SDL2 and the required
non-system dependency closure from the controlled build SDK, with notices;
leave glibc, display services and GPU drivers with the host. Test without SDK
library paths, developer Python, Flatpak services or build-directory files.

## Completion Evidence

Implemented: immutable AppDir staging, executable permissions, shared XDG
activation policy, mount-independent paths and preservation of Steam Input
environment. Tests cover audit/private-file rejection, failure cleanup and
Windows/legacy Flatpak compatibility.

Before publication: build the updated frozen Forge; audit dependency closure;
run first import, second launch from another AppImage mount, update and repair;
test mounted and extract-and-run launches; qualify framebuffer output/audio,
fullscreen/resize and SDL gamepad/hotplug in a Gamescope-compatible session.
Use 1280x800 and 1280x720 without stretching. Steam Deck hardware is unavailable:
report Linux-PC results and Deck conformance separately, never as a hardware pass.

References: [AppDir](https://docs.appimage.org/reference/appdir.html),
[FUSE fallback](https://docs.appimage.org/user-guide/troubleshooting/fuse.html),
[Valve controller requirements](https://partner.steamgames.com/doc/steamhardware/compat),
[Deck input FAQ](https://partner.steamgames.com/doc/steamhardware/steamdeck/faq).
