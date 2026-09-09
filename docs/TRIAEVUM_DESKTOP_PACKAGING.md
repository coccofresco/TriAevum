# Desktop Packaging Decision

Decision updated: 2026-09-09, following the maintainer's Flatpak request.
Windows releases are portable ZIPs. Linux desktop and Steam Deck share one
x86-64 Flatpak containing Forge and the native game runtime.
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

Use app ID `io.github.coccofresco.TriAevum` and Freedesktop Platform/SDK 25.08
as the first qualification baseline. This supersedes the earlier tar.gz plus
Steam Runtime launcher proposal. A tar.gz remains useful for developer staging,
not a second end-user distribution to maintain.

```text
/app/                         read-only installed application
  bin/                        single launcher and explicit Forge entry
  lib/triaevum/               runtime, Forge, catalog, recipes and resources
  share/                      desktop integration, source and notices
$XDG_DATA_HOME/TriAevum/       writable private activation root
  private-plugins/            verified active title module
  TriAevum.launch.json        existing launch-profile contract
  data/                      ROM-derived data, saves, settings and caches
```

The launcher opens Forge on an unprepared installation and the game after a
successful import. Keep an explicit Forge action for reimport/repair. On Deck,
prepare the ROM in Desktop Mode and use the same app from Gaming Mode. Forge
selects a personal decrypted .3ds/.cci through the file portal and performs the
existing verified import without compilation. Subsequent launches must not need
access to the original ROM. Do not publish ROM data, saves or TopScreen payloads.

One host layout adapter separates immutable package resources from writable
activation and data roots. Windows maps these roots to its portable directory;
Flatpak maps them to /app and XDG private storage. The same importer, receipts,
atomic activation, save format and update logic consume that adapter. Locks,
journals, downloaded textures and launch profiles must never be written to /app.
Do not copy the complete installed application into the home directory to make
its current write assumptions work. No Flatpak branches in PICA or title logic;
future Android supplies its own storage/lifecycle adapter and native binaries.

Forge's current Tk chooser needs an isolated portal adapter; selecting a ROM must
not require `--filesystem=home` or `--filesystem=host`. Scope GPU, audio, display,
network for TopScreen, and controller access explicitly. Qualify controller
motion separately: input access alone does not guarantee hidraw availability.
The SDK is developer-only. Both Forge and the game run in the same Flatpak
environment; no nested Steam Runtime, Proton, root changes or user compiler.

The existing Steam SDK-built ELF artifacts are reuse candidates, not proof of
Flatpak compatibility. Check the complete dependency closure and execute it in
the chosen Freedesktop runtime; rebuild only incompatible components. Private
Forge Python/Tk dependencies must be qualified too. Let Flatpak provide its GPU
extensions rather than bundling host drivers. Ship corresponding source and
notices under the existing audit policy, including adopted PR contributions.

Deliver a single .flatpak bundle initially, with the runtime repository recorded.
Flatpak installation manages runtime downloads; a future signed update repository
can retain the same app ID and private data. Do not claim Flathub publication or
Steam Deck qualification before those have actually happened.

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

Flatpak prerequisites are now installed and verified on the Linux test host:
Flatpak 1.18.1, flatpak-builder 1.4.10, with no missing package-owned files.
Freedesktop Platform/SDK 25.08 and the host-matching NVIDIA extension are
installed per user; sandbox startup and SDK compiler execution pass. A small
Vulkan probe compiled in the SDK and run in the Platform sandbox enumerates
the physical RTX 4060 (Vulkan 1.4.341), not only software rendering. This is
host preparation, not a game rendering test or a completed TriAevum Flatpak.

Remaining before Linux release, in dependency order:

1. Implement/test the separate package/activation layout with unchanged Windows
   defaults; move the existing import and launch transactions to its writable root.
2. Integrate the ROM file portal and first-run/game launcher. Qualify read-only
   /app, missing/cancelled portal selections, spaces in paths and preserved saves.
3. Stage audited binaries/sources, add the manifest and desktop integration, then
   build/install the candidate bundle under the Freedesktop runtime.
4. Qualify ROM-only import, reimport/update, F1, controller/audio and GPU launch
   from that installed bundle. Check Gaming Mode on actual Steam Deck hardware.
5. Close the known stalls/black flashes and unsupported-effect gaps. Packaging
   checks and private desktop proofs do not replace final gameplay qualification.

## Primary References

- [Flatpak build workflow](https://docs.flatpak.org/en/latest/first-build.html):
  matching runtime/SDK and a single-file bundle with its runtime repository.
- [Flatpak conventions](https://docs.flatpak.org/en/latest/conventions.html):
  application identity, /app, XDG storage and desktop integration.
- [Flatpak permissions](https://docs.flatpak.org/en/latest/sandbox-permissions.html):
  private writable storage, file portals and least-privilege device access.
- Local [platform architecture](TRIAEVUM_PLATFORM_RELEASE_ARCHITECTURE.md),
  [SDK/build evidence](TRIAEVUM_STEAM_RUNTIME_BUILD.md) and
  [precompiled release contract](TRIAEVUM_PRECOMPILED_RELEASE.md).
