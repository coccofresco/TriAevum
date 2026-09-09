# Platform and Release Boundaries

## Objective

One maintained runtime and release pipeline, with explicit platform adapters.
Windows and Linux must not diverge into gameplay/renderer forks. Android is a
future port, not a currently supported release target. Preserve the renderer
boundaries in [NRI architecture](OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md).

## Ownership

| Layer | Owns | Must not own |
| --- | --- | --- |
| Shared 3DS input (`tools/three_ds/input`) | Host binding semantics, native channels, motion transforms | SDL polling, TopScreen item policy |
| Platform host | Devices, window/surface, sensors, file access, lifecycle | Title addresses, HUD rules, PICA shader semantics |
| OOT3D adapter | TopScreen behavior, native item/camera contracts, title assets | OS-specific copies of gameplay |
| Shared renderer/NRI | Canonical rendering, extension graph, resource lifetime | Platform-specific game rules |
| Forge/import core | Verified input, recipes, catalog and atomic activation | GUI-specific rules or user-side recompilation |
| Release platform | Target triple, artifact names and binary/dependency contract | ROM matching or title behavior |
| Common package audit | Inventory, hashes, corresponding source, provenance | Guessing target from the machine running the audit |

The shared service ABI is unchanged. Binary target, service ABI, ROM/content
identity, and renderer capabilities are distinct contracts. A Windows DLL is
not a Linux module; an x86-64 ELF is not an Android arm64 module.

## Implemented

- `release_platform.py` is the artifact/target registry. Old catalogs/layouts
  without a target retain their original Windows meaning, never the current host.
- `release_policy.json` separates common rules from platform allowlists and
  mandatory files. `platform_policy.py` composes them without mutating shared
  rules. Linux rejects Windows binaries and bundled host libc/loader/Vulkan loader.
- `package_release.py` records the target and probes the correct executable.
  `audit_release.py` checks target consistency with the precompiled catalog and
  applies the same source/private-content checks to both platforms.
- `validate_title` is host-independent metadata/integrity validation.
  `select_title` additionally enforces the executing host. Cross-auditing a Linux
  package on Windows cannot accidentally permit installing its ELF on Windows.
- Qualification is target-bound. A completed Windows readiness document cannot
  approve Linux. Candidate packaging retains every content/integrity check and
  does not claim release qualification.
- Verified download/retry is shared; TopScreen acquisition/import policy is not.
- Real F1 widget tests have a portable CMake target. No ROM or GPU is required;
  tests do not inject private ImGui instrumentation into the runtime.
- The developer-only native compiler policy uses the same target registry as
  packaging. Compiler driver mode remains explicit after symlink resolution;
  unsupported Android/ARM targets cannot inherit desktop x64 flags or artifacts.
  The synthetic title ABI and real loader have an isolated CMake target, without
  renderer/UI dependencies. Community provenance is recorded in
  [PR credits](TRIAEVUM_CONTRIBUTIONS.md).

## Next Release Checklist

Distribution choice: [portable Windows ZIP and one Linux/Steam Deck Flatpak
with Forge integrated](TRIAEVUM_DESKTOP_PACKAGING.md). The package format never replaces
native dependency and installation qualification.

1. Obtain issue #5's configuration and verify remapped items in real gameplay.
   See [investigation](TRIAEVUM_ISSUE_5_CONTROLS.md); do not mark it resolved based
   only on mapping tests.
2. Finish the Linux dependency closure and matching notices/sources for the
   exact artifacts in the selected Freedesktop runtime. Prior Steam Runtime
   builds are reuse candidates, not Flatpak qualification. Materialize validated internal bundle links
   when staging public files; do not relax the no-symlink publication policy.
3. Use this shared packager/auditor for the final Linux artifact, replacing the
   private qualification stager. Bind the final runtime, Forge, title and source
   archives to their actual build identities, not the stale remote checkout HEAD.
4. Qualify fresh ROM-only installation, reinstallation preserving saves/config,
   visible F1/controller UI, native audio, and native GPU boot from the final
   installed Flatpak on Linux and final portable ZIP on Windows. Existing private
   proofs are not final-package qualification.
5. Resolve/measure the Linux stalls and black flashes before Steam Deck claims.
   The latest bounded run still has a 13.50-second maximum interval, many cold
   shader misses, and significant frame-start/backend time. Do not call its
   interpolated presentation count native gameplay FPS. Test uncapped steady
   native updates separately from interpolation and cold-cache startup.
6. Qualify actual Steam Deck hardware. Keep unsupported effect providers explicit;
   SSSR parity and Linux shader delivery are not closed by the packaging work.

Already verified: native SDK runtime/title boot; published UI sources; binding
tests; real F1 widgets; current Linux frozen Forge GUI (760x443, mapped controls,
no clipping). Windows controller hardware regression and final release builds
have not been repeated for these changes. No new public tag/package yet.

## PR Integration Verification (2026-09-09)

Selective integration starts at `f1c05a8` (original PR #6: `505d7b8`).
The full release Python suite runs 251 tests on each host: Windows Python 3.13
passes with 2 skips; Linux Python 3.14 passes with 5 skips. The isolated actual-loader/support
build took 2.81 seconds on the Linux host with three compiler jobs. The synthetic
title build and six ABI execution checks passed in 1.57 seconds using Clang
22.1.8 invoked as `/usr/bin/clang`, not `clang++`: explicit driver policy fixes
the symlink/name hazard. Actual-loader query/entry/sealing checks and empty-stub
rejection also pass. All 12 service/module tests pass under GCC 16.2.1,
including the C ABI test. These timings are small-test iteration costs, not a
full-title build, game FPS, or Steam Deck gameplay qualification.

The new Linux CI job uses `tools/triaevum_release/native_probe` directly. It
does not configure SDL, shaderc, NRI or the UI to verify the plugin ABI. The full
runtime continues using the already-corrected distro shaderc lookup. Developer
title creation and catalog promotion now propagate the same explicit target
and profile, while legacy manifests without a target remain Windows.
Installation migration uses platform artifact names and tests repeated migration,
relocation and preservation of save/configuration bytes. No compilation is added
to ROM-only end-user installation.

CI run `34310058002` passes the Linux policy/module and native title ABI jobs.
The Windows native link exposed a pre-existing `/NOEXP` option unsupported by
the runner's LLD. The developer linker and profiling relinker no longer request
it: any auxiliary export file stays in the temporary link directory, and only
the verified DLL is promoted. A regression test verifies both the command and
the absence of auxiliary files in the cache. This is independent of PR #6's
Linux contribution and does not require changing the Windows title ABI.

## Android Extension Point

Add an arm64 target only together with its build/loader, dependency policy and
qualification. Use an Android activity/surface/input/storage adapter, not a fork
of TopScreen or PICA. Pause/resume, background audio, lost surfaces, controller
disconnects and sensor timestamps belong to host lifecycle contracts. Touch
locations are transformed into canonical native coordinates exactly once.

Keep ROM access behind an import port: Android document-provider handles are not
desktop paths. Preserve the same verified recipe/catalog and activation test
vectors; extract a portable import library when needed rather than transplanting
the Python/Tk desktop UI. Android-specific packaging/signing and Vulkan support
must be qualified separately. Unsupported targets currently fail explicitly.
