# Desktop Packaging Decision

Decision: 2026-09-09. Windows releases are portable ZIPs. The first native
Linux/Steam Deck release uses a portable tar.gz and Steam Linux Runtime 4.
This defines the packaging target; it does not claim Linux release qualification.

## Windows: Portable Is the Default

- Extract into a user-writable folder, run `TriAevumForge.exe`, select the
  decrypted personal ROM, then run `TriAevum.exe`. No installer, administrator
  permission, registry registration or compiler is required.
- Application-managed content, configuration, profiles, saves, downloads and
  caches remain beside the application. `data/` owns persistent user data;
  the existing root launch profile and private-plugin activation directory
  remain package-relative. Copy the **whole folder** to relocate it.
- Never silently fall back to AppData/home when the directory is unwritable or
  unprepared. Show an actionable error. Explicit developer path overrides and
  user-selected external texture directories remain explicit, not portable.
- Ship app-local redistributable dependencies and their notices. GPU drivers
  remain an OS prerequisite, not bundled files. OS/driver-managed shader caches,
  temporary extraction and system diagnostics are outside the application's
  persistence guarantee.
- Update by extracting a clean release and preserving the previous private
  installation through Forge's existing verified import/migration path. Never
  delete saves/configs, overwrite a running executable, or publish a used folder.
  Qualification must check a moved installation and an update before shipping.

GUI Forge, CLI Forge and the native module host now agree on `<package>/data`
even before the first import. Active-title references resolve relative to their
manifest. Older explicit absolute references remain readable; use
`forge migrate-paths` before relocating such installations. No save format changes.

## Linux and Steam Deck: First Deliverable

Use one `TriAevum-<version>-linux-x86_64.tar.gz` containing a writable directory:

```text
TriAevum/
  TriAevum                 native game executable
  TriAevumForge            frozen GUI, no system Python requirement
  _internal/              private Forge dependencies
  lib/                    qualified application libraries, not GPU drivers
  forge/                  neutral module
  titles/                 catalogued precompiled title logic
  recipes/ resources/ source/ LICENSES/ docs/
  data/                   created locally after import, never distributed
```

Keep the same installation and activation code as Windows. tar.gz retains Unix
permissions and avoids requiring FUSE. The archive tool sets executable modes
from declared runtime/Forge roles, including when packaging on Windows.

The **execution environment is part of the package contract**: build and qualify
against Steam Runtime 4, then launch within its matching container. A tarball of
SDK-built ELF files is not automatically compatible with every host distribution.
Do not ship libc, the ELF loader, Vulkan ICDs, host GPU drivers or the SDK.
Steam Runtime supplies the runtime ABI and integrates host graphics drivers;
it does not emulate the game or replace NRI/Vulkan.

For the GitHub release (not a Steam-store application), finish a small launcher
adapter that uses an installed Steam Runtime 4, including non-default Steam
libraries, or reports how to install/select it. Use Valve's entry point, not
our own container implementation. Runtime acquisition must be explicit and
verified; never run arbitrary downloaded shell installers. Both Forge and the
game must use the same qualified environment. Detect an already-active runtime
to avoid recursive launch and preserve arguments and exit codes.

First-run workflow: extract in the user's home or an executable, writable game
library, launch Forge from Desktop Mode, import the ROM, then add the game
launcher as a non-Steam game. Test that exact shortcut in Gaming Mode. Do not
assume Steam automatically selects Runtime 4 for an arbitrary non-Steam ELF.
No `sudo`, SteamOS read-only-root changes, system package installation or Proton
should be required by this **native Linux** package.

## Why Not AppImage or Flatpak First?

| Format | Assessment for this codebase |
| --- | --- |
| tar.gz + Steam Runtime 4 | Reuses the already-built SDK/runtime and writable Forge installation; smallest new surface. Selected first. |
| AppImage | Useful future standalone download, but not an ABI fix: still needs a compatible build baseline/dependency closure. Its image is read-only; Forge currently activates beside itself. Requires a separate writable data/activation root and its qualification. |
| Flatpak | Worth evaluating for Discover and managed updates later. Requires a qualified Flatpak runtime build, external-ROM file portal, GPU/audio/controller permissions and writable activation outside `/app`. Avoid broad home/device grants as a shortcut. |
| deb/rpm | Additional distro-specific maintenance without addressing Steam Deck first. Not planned for the first release. |

Do not implement four packagers in parallel. Keep portable desktop installation
as the first policy. A later read-only package should supply resource, activation
and user-data roots through one host layout adapter, consumed by Forge and the
runtime. No Flatpak/AppImage branches in PICA, title code, TopScreen or effects.
Android can then supply its own sandbox/storage adapter; it does not inherit
desktop paths or an x86-64 title binary.

## Implemented and Remaining

Implemented: unified portable defaults; relative native active-title lookup;
relocation tests; one audited archive tool with deterministic timestamps,
normalized permissions/owners, a containing directory, exclusive output creation
and failed-write cleanup. It packages only the existing audited public manifest;
it cannot upgrade a candidate into a qualified release. PR attribution and
corresponding source remain ordinary audited package contents.

Verification for this tranche: 257 Python tests on each host, with 2 Windows
skips and 5 Linux skips; all executed tests pass. The five archive tests include
full audit after extraction, reproducible output, Linux executable permissions,
private-data rejection and failure cleanup. The native path/relocation suite
also compiles and passes with GCC 16 on Linux. No complete runtime rebuild or
new final-package gameplay qualification is claimed by these checks.

```sh
python -m tools.triaevum_release.archive_release \
  --package /path/to/clean-staged-package \
  --output /path/to/TriAevum-version-linux-x86_64.tar.gz
```

Use `.zip` for Windows; `prepare_release --archive <path.zip>` optionally invokes
the same tool. The full `prepare_release` build/staging driver remains Windows-
specific; Linux uses the common `package_release` layout/audit boundary.

Remaining before Linux release: finish the end-user runtime launcher, final
dependency/license closure and exact-source staging; qualify ROM-only import,
reimport/update/relocation, F1, controller/audio and GPU launch from the **final
extracted archive**. Close the known stalls/black flashes and test an actual
Steam Deck. Unit tests and native desktop proofs do not replace those checks.

## Primary References

- [Valve Steam Runtime guidance](https://github.com/ValveSoftware/steam-runtime):
  Runtime 4 SDK/execution pairing and installation via Steam (app ID 4183110).
- [AppImage concepts](https://docs.appimage.org/introduction/concepts.html):
  build ABI baseline, excluded system/graphics libraries and read-only AppDir image.
- [Flatpak permissions](https://docs.flatpak.org/en/latest/sandbox-permissions.html):
  private writable storage, file portals and least-privilege device access.
- Local [platform architecture](TRIAEVUM_PLATFORM_RELEASE_ARCHITECTURE.md),
  [SDK/build evidence](TRIAEVUM_STEAM_RUNTIME_BUILD.md) and
  [precompiled release contract](TRIAEVUM_PRECOMPILED_RELEASE.md).
