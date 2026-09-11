# Desktop Package Parity

## Ownership

The shared Forge installer owns validation, shader preparation, ABI preflight,
immutable plugin generations and transactional activation. Platform packaging
only supplies compatible executable artifacts. It must not maintain a second
installer or relax activation receipts to accommodate an update.

`tools/triaevum_release/package_update.py` detects a catalogued runtime/plugin
change before normal launch. It checks the new catalog and all old bindings
other than the replaced executable, then reuses `install_precompiled_title` on
the existing prepared inputs. Corrupt profiles/plugins and pending journals
remain errors. User configuration is retained. Installation failures prevent
launch. No ROM selection, title translation or compilation is introduced.

The normal launcher still validates the published tuple under its lock after
the update. Developer packages without a catalog retain the previous behavior.
An update that changes the content contract incompatibly must fail with a
repair/import error, not reinterpret the user's assets speculatively.

## Portable Forge Build

Run inside the pinned SDK described in `TRIAEVUM_STEAM_RUNTIME_BUILD.md`:

```sh
bash scripts/run-in-steamrt4.sh "$SDK" "$WORK" \
  bash /home/src/scripts/build-steamrt4-forge.sh /home/work/forge
```

The script stages exact Debian package versions through the SDK's authenticated
APT indexes without changing the host or SDK. It bundles Python 3.13/Tcl/Tk,
records downloaded package SHA-256 values and runs the frozen doctor. Never
substitute a Forge frozen against the development host's newer glibc.

Stage the SDK-built `oot3d_native_pica_aot_compiler` and its `libshaderc.so.1`
using `stage_linux_forge_candidate.py --shader-compiler ...
--shader-dependency ...`. The catalog binding then makes normal Forge prepare
renderer pass shaders before activation, through the same code as Windows.
The game's private PICA seed and device pipeline preparation are separate:
22 renderer pass modules do not mean the entire game's shaders are covered.

## Evidence (2026-09-11)

- Windows discovery: 358 tests, 10 skips, no failures.
- Linux discovery: 358 tests, 13 skips, no failures, using the existing test
  virtual environment and isolated Tcl/Tk libraries.
- Update/activation/installed-runtime subset: 26 passing tests on each OS.
- Real isolated Windows installation updated in 3.45 seconds, then validated;
  a second call was a no-op. Both existing configuration files were unchanged.
  This fixture contained no save files; it is not a save compatibility test.
- The updated Windows copy then booted in a bounded 25-second run, exit 0.
  Its older reference catalog had no renderer-preparation contract: the cold
  launch compiled 23 pass and 51 PICA modules. This verifies activation, not
  complete Forge prewarming; the Flatpak helper test below is independent.
- SDK Forge rebuilt successfully; installed private Flatpak includes current
  Forge and the shader preparation tool/contract.
- Packaged helper: 22 cold compilations in 2.104 seconds, then 22 hits and zero
  compilations/writes. No game boot was needed for either preparation.
- Normal Flatpak Forge-to-game launch: exit 0 with a runtime report in a
  bounded 35-second run (before adding automatic update handling).
- Frozen GUI widgets are created without clipping, but the desktop repeatedly
  minimized the smoke-test window. Visible-widget qualification remains open;
  the probe retries presentation without accepting unmapped widgets as success.

Private Flatpak candidate: `TriAevum-Linux-forge-parity-update.flatpak`,
commit `6d1bdd819a6e76449260c2b90478d3ed11d9ff17daf5631059531f7b5eab4172`.
It is not a newly published release. Earlier runtime/title qualification stays
in `TRIAEVUM_POST_1C_REGRESSION_AUDIT.md`.

## Still Open

Packaged end-to-end update/GUI qualification on Linux, portable SSSR parity,
the letterboxed weapon-aim outline rectangle and the remaining packaged
gameplay replays are not closed by these installer fixes. Retain their explicit
status in the regression audit; successful boot is not complete parity.
