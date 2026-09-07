# TriAevum

TriAevum is an experimental native PC runtime for Nintendo 3DS software. Its
first title integration targets a user-provided copy of *The Legend of Zelda:
Ocarina of Time 3D*, with native PICA rendering through NRI/Vulkan, audio,
input, save states, widescreen presentation and optional visual extensions.

## First public alpha

**v0.6.0-alpha.1** targets Windows x64 with a Vulkan-capable GPU and current
graphics drivers. This is an experimental pre-release, not a claim of complete
game compatibility or stable performance on every PC.

Download the **Windows-x64 ZIP** from the GitHub release, extract the entire
archive into a writable folder, then run `TriAevumForge.exe`. GitHub's automatic
"Source code" downloads are for developers and do not contain a playable build.
Select your own supported decrypted **EUR** `.3ds` or `.cci` ROM; Forge rejects
other revisions instead of attempting an unsupported installation. Do not run
the executables from inside the ZIP.

Forge prepares the game locally without installing a compiler or SDK. Keep
the `data/` folder: it contains your imported game, saves and configuration.
TopScreen textures are downloaded separately from the official mod archive;
an Internet connection is required on first setup unless that archive is
provided locally. `F1` opens settings and starts hidden; `F2` temporarily
disables the configured visual enhancements without erasing their settings.

See [release notes and known limitations](docs/releases/v0.6.0-alpha.1.md).

TriAevum is not affiliated with or endorsed by Nintendo. This repository and
its public release artifacts must not contain ROMs, keys, original game assets,
or mod payloads. Catalogued translated game logic is explicitly included under
the precompiled release model; it is not described as title-neutral. Users are
responsible for complying with the law that applies to
their own copy and jurisdiction.

## Release model

The distribution follows the static-recompilation model used by Xbox 360 ports:

1. **Publisher build:** the reusable runtime and optimized title logic are
   compiled once, before packaging. The release includes the precompiled title
   DLL, exact revision catalog and corresponding source, but no original assets.
2. **User install:** Forge accepts a supported decrypted ROM, verifies and
   extracts it, prepares local data and activates the already compiled module.
   No compiler, SDK acquisition, IR generation or title compilation runs here.

The mature NRI/Vulkan host, TopScreen, F1 settings, audio and visual interpolation
remain unchanged. A missing or incompatible precompiled module is an install
error, never a hidden fallback to compilation. Developer-only `build-title`
commands remain available for producing updated title modules.

See [the current precompiled release contract](docs/TRIAEVUM_PRECOMPILED_RELEASE.md).
It supersedes the local-compilation requirement in earlier release documents.

See [the release architecture](docs/TRIAEVUM_RELEASE_ARCHITECTURE.md),
[Forge usage and status](docs/TRIAEVUM_FORGE.md),
[development and install workflows](docs/TRIAEVUM_DEVELOPMENT_AND_RELEASE_WORKFLOWS.md),
[whole-AOT module migration](docs/TRIAEVUM_WHOLE_AOT_MODULE_MIGRATION.md),
[provenance policy](docs/TRIAEVUM_PROVENANCE_POLICY.md),
[license scope](LICENSE_SCOPE.md), and
[third-party notices](THIRD_PARTY_NOTICES.md).

For normal local setup, double-click `TriAevumForge.exe` and select a decrypted
`.3ds` or `.cci` ROM from your own supported copy. Forge extracts and verifies
the required inputs itself, stores all private generated data under `data/`
beside the application, and enables `Launch game` when preparation completes.
No separate extractor, compiler installation or game key is requested.
Advanced and automated workflows retain the explicit Forge command-line
interface.

The reconstructed TopScreen UI profile is enabled by default. Forge also imports
the modified native HUD/menu textures from the official TopScreen 2.1.1 archive:
it downloads and verifies the archive on first installation, or uses
`topscreen211.zip` placed beside Forge for offline installation. Subsequent
installs reuse the verified local texture pack. See
[TopScreen texture installation](docs/TRIAEVUM_TOPSCREEN_INSTALLATION.md).
Press `F1` while the Vulkan/NRI game window is active to open the renderer panel.
New installations use x2 visual interpolation, 1.10x scene FOV, and the maintained
toon/outline and grass style. Existing user profiles are preserved. The shared
renderer's Authentic preset remains unchanged; see
[graphics defaults and measurements](docs/TRIAEVUM_GRAPHICS_DEFAULTS_AND_GRASS_PERFORMANCE.md).

## License

Original TriAevum contributions are available under
`GPL-3.0-or-later`. Donor code keeps its existing license and notices; see
`LICENSE_SCOPE.md`, `THIRD_PARTY_NOTICES.md` and `LICENSES/`. Public binary
packages must include complete corresponding source and all required
attributions.

## Repository layout

- `runtime/three_ds_recomp` contains the reusable 3DS runtime contracts.
- `tools/oot3d` contains the current title adapter, native service hosts and
  local preparation tooling.
- `tools/triaevum_release` contains release-policy, audit and Forge tooling.
- `scripts/triaevum` contains release and local preparation entry points.
- `docs` contains architecture, validation and provenance records.

The remaining Fast3D and `Ship::` compatibility APIs are implementation
history under active extraction. New title and renderer work must use neutral
runtime contracts.

## Developer build

The validated development path is documented in
`docs/OOT3D_WHOLE_AOT_PRODUCT.md`. It consumes private local inputs and produces
a development executable. Promote only individually verified title DLLs and
their sources through the release allowlist; never upload a development folder,
object cache, captures, ROM inputs or save states wholesale.

## Project status

TriAevum Runtime/Forge separation, publisher-side title compilation and the
dump-to-open-title vertical are implemented. A binary must still be produced
by the audited release workflow; an arbitrary developer build must not be
described as a public ROM-free release.

## Attribution

This repository retains work and knowledge from Shipwright / Ship of
Harkinian, libultraship, Azahar, NVIDIA NRI and the independently developed
TopScreen modifications. Exact scope, revisions and licenses are recorded in
`THIRD_PARTY_NOTICES.md`; renaming components never removes their history or
license obligations.
