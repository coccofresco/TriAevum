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

`scripts/package-appimage.sh` enforces the two tool SHA256 values qualified in
this tranche, limits compression to two workers, refuses existing destinations,
and never fetches tools implicitly. Acquire `appimagetool-x86_64.AppImage` from
AppImage/appimagetool and `runtime-x86_64` from AppImage/type2-runtime official
GitHub releases. A replaced `continuous` asset must fail its digest check until
the publisher deliberately reviews and updates the tool pin.

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

### Private Candidate Qualification, 2026-09-14

- Source/runtime/Forge snapshot: `7b88099bbec37d3911526bec4b2e87ac500252bd`.
  Native runtime rebuilt incrementally (product identity and link only); title
  AOT unchanged. Frozen Forge rebuilt in the existing Steam Runtime 4 SDK.
- Payload: 526 audited files, 248,816,671 bytes. Both SDL2-compat and SDL3 plus
  their copyright notices are included; SDL3 is dynamically loaded by compat
  and would be missed by an `ldd`-only dependency check.
- Candidate: `TriAevum-x86_64.AppImage`, 100,076,024 bytes; SHA256
  `8587b02574a1fab2bb1ca2e0d7c1cf7b5f4407b7bbe3053afb78de923f23136c`.
  Private Linux workspace: `/home/xander/triaevum-appimage-r2-20260914`.
- 24 Python tests pass on Windows and Linux. Real SDL regression executable
  passes with the packaged libraries/database: initialization, mappings,
  buttons, both sticks, refresh, hotplug and subsystem ownership. 491 mappings
  loaded. These are virtual-controller tests, not a physical Deck test.
- Frozen Forge real-widget import passes: 27.96 seconds, installation ready,
  no clipped/unmapped widgets. ROM and generated data are outside AppDir.
  The first candidate's missing Tk import was caught by this test; the rebuilt
  candidate includes Python Tk, `_tkinter`, Tcl/Tk libraries and data. Preserve
  all those SDK paths when freezing; CLI `doctor` alone cannot validate the GUI.
- Mounted AppImage launches from the prepared activation with a changing mount
  path. A bounded native exit returns 0, produces a framebuffer and 240 recorded
  diagnostic frames. Extract-and-run also launches (956 recorded frames in its
  bounded observation). A separate renderer probe returns 0 with 1,239 frames
  and three framebuffer captures; title rendering was visually inspected.
  These counts are evidence of rendering, **not FPS measurements**.
- First unrestricted observations were stopped by the harness; mounted
  diagnostics were not flushed then. The clean-exit framebuffer run above is
  the reliable mounted-launch evidence. All test game processes were closed.

Remaining release qualification: actual Gamescope session, Steam Input routing
end-to-end, AMD/Mesa hardware, suspend/resume and a clean supported distribution;
Flatpak-to-AppImage migration; full AppImage runtime redistribution/source
notices. The Linux test PC is not Steam Deck. Do not publish this candidate as
Deck-verified or claim those remaining cases passed. Alpha.2c stays unchanged.

References: [AppDir](https://docs.appimage.org/reference/appdir.html),
[FUSE fallback](https://docs.appimage.org/user-guide/troubleshooting/fuse.html),
[Valve controller requirements](https://partner.steamgames.com/doc/steamhardware/compat),
[Deck input FAQ](https://partner.steamgames.com/doc/steamhardware/steamdeck/faq).

### Post-alpha.2c Parity Candidate, 2026-09-17

Built from `6ad3d832ca594442d009b619dcfc66cd08561799`, following
[the Windows/Linux regression review](TRIAEVUM_POST_ALPHA2C_REGRESSION_REVIEW.md).
This is a local test AppImage, not a newly published release or Deck certification.

- `TriAevum-6ad3d83-x86_64.AppImage`: 126,798,328 bytes; SHA256
  `fd8b2ce4940efddd32f7605717d22a8b6469125b32874ccef8fbca46fd05f7c7`.
- Runtime incrementally rebuilt inside Steam Runtime 4 from the exported source
  snapshot, not the stale Git identity of the Linux development mirror. Frozen
  Forge rebuilt with the same SDK and explicit Tcl/Tk discovery. GLIBC ceiling
  remains 2.38. NRI, consolidated F1 and TopScreen capabilities verified.
- Retained the movie-capable title module verified by the preceding review;
  its translated-source files were checked against their inventory and packaged
  with paired build source. The existing title build reports no pending work.
  Catalog hashes were refreshed for runtime, neutral module and title module.
- Retained the previously qualified EUR/USA input recipes and COPY adapter,
  bundled portable shader corpus, pipeline recipes and dependency notices.
  Added the newly required Aurora notice. The audit initially caught a missing
  notice and loss of USA coverage when substituting the unexpanded base recipe;
  both were corrected before producing the AppImage. No audit bypass was used.
- Audit passed for 527 allowlisted files / 362,442,925 payload bytes, both before
  and after AppDir staging. No personal ROM, extracted assets, saves, captures,
  downloaded TopScreen payload or driver cache was included.
- Real frozen Forge GUI imported a personal USA ROM in **12.81 seconds**, with
  no clipped/unmapped widgets and installation ready. This used a previously
  downloaded TopScreen archive in private user storage, outside the package;
  the number is not a fresh-network-download guarantee.
- Mounted AppImage playback: 794 presentations / 25,270 submitted draws, zero
  guest memory faults, native 30 Hz simulation with x2 presentation active.
  `APPIMAGE_EXTRACT_AND_RUN=1` playback: 1,101 presentations / 38,884 draws,
  zero faults. Both exited normally and produced framebuffer captures; the
  mounted capture was visually inspected. These are bounded functionality
  checks, not FPS benchmarks. No SDK/Python/library path was inherited by play.
- Packaged-library SDL virtual-device regression passed. `ldd` found no missing
  runtime dependencies. Actual Steam Input/Gamescope, AMD/Mesa, suspend/resume
  and physical Steam Deck remain the earlier qualification limits.

The packaged default shader path was deliberately preserved: these runs used
the legacy specialized path, not the review's opt-in `--pica-parametric-tev`.
They logged 25 cold runtime compilations and 12 additional variants in the
longer fallback observation. The package must not be described as eliminating
all first-use shader compilation. The opt-in path and its separate limits are
documented in the regression review.

Private Linux evidence: `~/triaevum-appimage-20260917/` contains package audit,
GUI import report, invocation logs, bounded runtime reports and framebuffers.
Publisher drivers: `~/triaevum_appimage_refresh.py` and
`~/triaevum_appimage_check.py`. Tests used an isolated XDG root and restored
their temporary diagnostic launch-profile/receipt changes before completion.

Available for local testing on the Linux desktop as
`~/Scrivania/TriAevum-6ad3d83-x86_64.AppImage`, with a separate applications entry
**TriAevum (AppImage)**. It uses the normal XDG storage contract above; existing
Flatpak storage and the separate development launcher were left intact.
The first ordinary launch requests a ROM if this XDG installation is unprepared.
The same artifact is retained on the Windows publisher under
`I:/TriAevum-public/artifacts/appimage-20260917/`.
