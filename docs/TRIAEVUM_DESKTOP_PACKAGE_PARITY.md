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
- Frozen GUI qualification subsequently passed with mapped, unclipped widgets.
  Actual ROM preparation passed in 8.21 seconds on the existing installation;
  a new isolated activation passed in 25.15 seconds, including shader preparation.
  The first direct-helper probe omitted the launcher's library path and failed
  preflight; use the real launcher's environment for frozen GUI qualification.
  Flatpak reserves XDG variables during startup: isolation must apply
  `XDG_DATA_HOME` through the child `env` command, not `flatpak --env` alone.

Private Flatpak candidate: `TriAevum-Linux-forge-parity-update.flatpak`,
commit `6d1bdd819a6e76449260c2b90478d3ed11d9ff17daf5631059531f7b5eab4172`.
It is not a newly published release. Earlier runtime/title qualification stays
in `TRIAEVUM_POST_1C_REGRESSION_AUDIT.md`.

## Still Open

The newer private SSSR package activated through normal frozen Forge on Linux:
the runtime receipt changed and the game launched without ROM reselection or
manual hash edits. However, the bounded session's termination produced allocator
errors, and the runtime also normalized an invalid reflection configuration.
Consequently this is not a clean configuration-preservation/shutdown test.
The candidate (`686b76c9b140d4f8b15609d49589afd06e4da969dbaac785c43979134eb91776`)
was withdrawn in favor of the preceding private Flatpak. Preserve the failed
evidence; do not publish it as qualified. See `TRIAEVUM_SSSR_PORTABLE_BACKEND.md`.

Release test discovery now covers 364 tests on each OS: no failures, 9 Windows
skips and 13 Linux skips. Use the provisioned Linux virtual environment, not
system Python without its required Capstone package.

Clean packaged end-to-end update/shutdown qualification on Linux, portable SSSR parity,
the letterboxed weapon-aim outline rectangle and the remaining packaged
gameplay replays are not closed by these installer fixes. Retain their explicit
status in the regression audit; successful boot is not complete parity.

### September 12 Follow-Up

The Linux update/shutdown path above now passes after removing signal-time
`exit()` from the donor shutdown handler. Normal frozen Forge reactivation plus
45 seconds of gameplay and SIGTERM terminates with exit 0. See
[shutdown qualification](TRIAEVUM_DESKTOP_SHUTDOWN.md) for the exact installed
candidate and limitations. SSSR is built into this candidate; Windows binary
qualification and the remaining gameplay replays are still open. Current
release discovery is 365 tests per OS, no failures. The suspected outline
rectangle was subsequently excluded after geometry-guide and user inspection;
see `TRIAEVUM_ISSUES_5_12_17_19_20.md`. No outline workaround was applied.

### Packaged Gameplay Replays (September 12)

The physical Linux desktop reran three existing Windows/host qualification
scenarios using the runtime and title from the private Flatpak payload, not
the development executable. The shared `probe_renderer.py` isolated saves and
configuration. These launches used the package's physical staged files and
library environment; Flatpak sandbox/Forge launching was qualified separately
above. No user save or graphics default was edited.

| Replay | Presentations | Observed result |
| --- | ---: | --- |
| Gyro aiming, 2x | 420 | 208 interpolated frame lists; SDK pitch -0.04524119570851326, within 1.2e-8 of the Windows reference. All three repaired gyro gains retained. |
| Bow/longshot, 2x | 560 | 278 interpolated frame lists; one arrow consumed, 50 -> 49; framebuffer shows the deployed longshot and TopScreen HUD. |
| First Song of Storms learning, native | 1100 | Zero interpolated frame lists; all five checks in `verify_storms_learning.py` pass. Quest 109DD620 -> 109FD620, gate 0600 -> 0E00, completion 8000 -> 8020. |

All three reached their requested frame/checkpoint counts, returned exit 0,
reported zero whole-AOT memory faults, and preserved 30 Hz simulation. Captures
were taken directly from the framebuffer. These are correctness replays, not
unlocked throughput measurements or physical-controller tests.

Runtime SHA-256: `629afe741ca4ae262b5ed60c8385381ba62ee7becabc474c27b0702cfc88e5b6`.
Title SHA-256: `97de67f0e595375761661831fa65c4e14f9a70eee52849a4a55d89459d79be15`.
Private runner: `/home/xander/probe-packaged-parity.py`; results and isolated
inputs: `/home/xander/triaevum-packaged-gameplay-20260912/`. No game-derived
fixtures or captures are committed. All test processes were closed afterward.

Remaining: Windows SSSR/shutdown binary rebuild and qualification, packaged
mounted-HUD/restricted-item contexts, and physical Steam Deck/input coverage.
The Windows build drive has only 737,280 bytes free. Cleanup was requested but
the execution tool rejected recursive deletion before PowerShell ran; no
deleted bytes are claimed for that attempt. Do not trigger a full rebuild or
discard the active incremental cache to work around the lack of space.
