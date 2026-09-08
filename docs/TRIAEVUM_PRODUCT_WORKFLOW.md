# TriAevum product workflow

> Distribution update (2026-09-05): [Precompiled release contract](TRIAEVUM_PRECOMPILED_RELEASE.md)
> supersedes this document's user-side compilation and no-translated-title-code
> requirements. The material below remains developer/history context; users now
> import their ROM into a package containing precompiled title logic, without SDKs.

## Product identity

The Windows runtime targets Windows 10 version 1903 or later (including Windows
11). Its embedded manifest selects a UTF-8 process code page so command-line,
JSON and filesystem paths share an encoding without changing the system locale.
See Microsoft's [UTF-8 process code-page contract](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page).

The playable public host is `oot3d_native_game` with
`OOT3D_DIRECT_AOT_PLUGIN=ON`, built by `triaevum_public_runtime` as `TriAevum.exe`.
The earlier module host is diagnostic only (`TriAevumModuleDiagnostic`).
CMake emits `triaevum-runtime-targets-Release.json` with exact target paths.

`TriAevum.exe --product-info` requires no title or GPU initialization. It reports
the source commit, ABI, NRI/F1/TopScreen capabilities, private-plugin presence
and default configuration using the renderer's own settings serializer.
Forge initializes those defaults only when the config file does not exist.
Existing preferences, including explicit Original30, remain untouched.

Default: native simulation, visual x2, native rendering without optional effects,
TopScreen enabled. Authentic remains native30; it is not silently redefined.
`doctor --inventory` lists components; plain `doctor` actually compiles, links
and executes a small C++ probe. A passing local probe does not prove a hermetic
toolchain or qualify a clean Windows installation.

## Migrating Existing Paths

Before moving an existing installation, run the explicit migration while its
old paths still exist (from the checkout, use `python tools/triaevum_release/forge.py`
in place of `TriAevumForge.exe`):

```powershell
TriAevumForge.exe migrate-paths --installation "D:/TriAevum" `
  --prepared-directory "D:/TriAevum/data/titles/RECIPE/CONTENT_KEY" `
  --data-root "D:/TriAevum/data"
```

This verifies the installed tuple and private inputs, then rewrites only the
profile, process manifest, index, receipt and matching active-title selection.
Preferences and save files are not rewritten or converted. The operation uses
the activation journal and rolls back on failure; a second run reports
`unchanged`. No title compilation occurs. External inputs remain explicitly
external: they must stay available at their original location. A moved external
input is not guessed by filename. Migration of an already-broken installation
is rejected instead of silently associating another ROM or title.

## Build and Package

Run from the clean, committed release checkout, against its configured build:

```powershell
python tools/triaevum_release/prepare_release.py `
  --cmake "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" `
  --build-dir I:/oot3dre_work/triaevum-direct-module-build `
  --llvm I:/oot3dre_tools/llvm-22.1.6 `
  --include C:/vcpkg/installed/x64-windows-static/include `
  --vc-redist-dir "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT" `
  --work I:/oot3dre_work/triaevum-release-artifacts `
  --output I:/TriAevum-0.5.1-candidate `
  --version 0.5.1 --candidate
```

This builds the correct host/stub/support, freezes Forge, regenerates the
corresponding source archive and packages from the allowlist. The package query
rejects the wrong host, wrong source commit or a private plugin. Its manifest
contains the runtime contract/hash and `qualification=candidate`; source ZIP
identity must match. It never copies an entire used installation into a release.

These are developer arguments, not end-user requirements. Users still select
only their supported decrypted `.3ds`/`.cci` ROM in Forge.

The output path must not exist. Keep immutable package staging separate from
private test installations. Never replace individual files in a published
archive: rebuild it with a matching manifest/source archive.

## Qualification still required

The ABI-v2 clean-machine compile/link/boot result remains pending, explicitly.
`--candidate` permits an auditable, title-free test package, not a claim of
release readiness. Do not copy the old ABI-v1 completion into this gate.

Other open review work: narrower ABI identity, hermetic sysroot/cache identity,
fully portable installation-context routing,
real-time x2/x3/framebuffer qualification and measured renderer optimizations.

Implemented build safeguards: GUI compile jobs reserve CPU/RAM headroom;
kernel-owned cache locks release on process death. Persistent lock files are
intentional and must not be deleted while a build is alive. Legacy PID locks
are not stolen automatically. Plugin keys now include the linker and builder;
implicit C++/Windows sysroot identity still needs closure.

## Private activation and recovery

Forge publishes immutable private plugins under
`private-plugins/<sha256>/triaevum_title_aot.dll`. It invokes the actual host with
`--verify-title-plugin <path>` before atomically publishing the launch profile.
The Windows host loads the explicitly selected DLL on demand and retains it for
its lifetime; an update does not overwrite a DLL used by an existing game.
The legacy DLL beside the executable is a compatibility fallback only.

`TriAevum.launch.json` selects the generation using `--title-plugin`. The same
profile works for direct double-click launches. Forge checks the selected data
directory, host/plugin hashes and profile before launching. Publication uses a
kernel lock and recoverable journal; retry the same title installation after an
interrupted activation. Do not delete pending journals or private generations.
User savedata is outside the transaction and never reset by activation.

`validate_product_run.py` audits a candidate, copies it to a private test
directory, clones publication metadata and reuses existing title inputs read-only.
It runs real Forge publication and native ABI preflight before a bounded game
run. This is not a cold ROM-to-plugin or clean-machine qualification.

## Explicit private toolchain

`forge.py build-title --sysroot PATH` passes a verified Windows sysroot through
the complete title-build API into the whole-AOT backend. `InstallRequest.sysroot`
provides the same path to both the GUI worker's preflight and compilation.
An explicitly invalid sysroot fails verification instead of using host headers.
This plumbing does not yet constitute a GUI acquisition workflow: the visual
license/download setup and selection of the prepared generation remain to wire.

The GUI worker now automatically selects a prepared generation at
`<data-root>/toolchain` when no explicit sysroot was provided. Selection checks
the six native proof results, compiler/support hashes and complete sysroot
inventory identity. An absent generation preserves the existing path; an
invalid present generation fails explicitly, without falling back to host tools.
The visual license/download setup still remains to wire. Prepare the generation
at this location using the private preparation command; no extra GUI field is
required for reuse. Changes to compiler/support require a new qualified generation.

Release preparation accepts `--toolchain-setup PATH`. It validates the setup
before building and allowlist-packages exactly three metadata files under
`forge/`: `toolchain-setup.json`, `toolchain-plan.json`, `toolchain-license.txt`.
The descriptor must reference the bundled `clang` resource directory/version 22.
Pinned hashes and Microsoft HTTPS payload contracts are checked; private
`installed_metadata` paths are rejected. SDK/CRT archives and extracted files
are never pulled into the package by this option. Applicable license contents
and the acquisition plan must still be qualified before publishing a release.

## GUI worker lifetime

GUI installation runs in a separate worker process. On Windows its descendants
are bound to a kill-on-close Job Object before any compiler is launched.
Confirmed GUI closure terminates the worker tree, not the separately launched
game. Progress/results use JSON lines; a completion event is accepted only after
worker exit. CLI direct builds retain their existing lifetime behavior.
Interrupted temporary files can remain; verified object cache entries survive
and activation journals remain authoritative on retry. Do not remove journals
as a substitute for recovery. Frozen GUI interruption during full title build
still requires end-to-end qualification.

The worker also holds a synchronization handle to the GUI process. Owner exit
causes immediate worker exit and job cleanup, including when PyInstaller's
one-file bootstrap adds an intermediate process. This owner watch is separate
from progress-pipe handling and does not require the worker to emit output.
