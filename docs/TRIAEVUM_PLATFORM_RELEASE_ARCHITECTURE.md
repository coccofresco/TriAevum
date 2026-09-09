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

## Next Release Checklist

1. Obtain issue #5's configuration and verify remapped items in real gameplay.
   See [investigation](TRIAEVUM_ISSUE_5_CONTROLS.md); do not mark it resolved based
   only on mapping tests.
2. Finish the Linux dependency closure and matching notices/sources for the
   exact Steam Runtime build. Materialize only validated internal bundle links
   when staging public files; do not relax the no-symlink publication policy.
3. Use this shared packager/auditor for the final Linux artifact, replacing the
   private qualification stager. Bind the final runtime, Forge, title and source
   archives to their actual build identities, not the stale remote checkout HEAD.
4. Qualify fresh ROM-only installation, reinstallation preserving saves/config,
   visible F1/controller UI, native audio, and native GPU boot from the final
   archive on Linux and Windows. Existing private proofs are not a final archive.
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
