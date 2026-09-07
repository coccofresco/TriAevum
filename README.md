# TriAevum

TriAevum is an experimental native PC runtime for Nintendo 3DS software. Its
first title integration targets a user-provided copy of *The Legend of Zelda:
Ocarina of Time 3D*, with native PICA rendering through NRI/Vulkan, audio,
input, save states, widescreen presentation and optional visual extensions.

**[Download the Windows alpha](https://github.com/coccofresco/TriAevum/releases/tag/v0.6.0-alpha.1)**
and make the presentation your own with configurable graphics and texture packs.

## In game

These three unretouched 1280x720 framebuffer captures come from the public
alpha with its default profile: toon shading and outlines, procedural grass,
1.10x scene FOV, x2 visual interpolation and the TopScreen single-screen UI.
The images show the current experimental build, not a promise of complete
visual accuracy. No optional community HD texture pack is used.

![Hyrule Field at night with the default grass and toon profile](docs/images/default-hyrule-night.png)
*Hyrule Field during the opening sequence.*

![Kokiri Forest gameplay with procedural grass and the TopScreen HUD](docs/images/default-kokiri-forest.png)
*Kokiri Forest gameplay with the default single-screen HUD.*

![The title sequence with Link and Epona in daylight](docs/images/default-title.png)
*The title sequence as the lighting changes toward daylight.*

## Graphics and customization

TriAevum is also a platform for experimenting with the game's presentation.
Open **F1** to configure the renderer and save named profiles:

- **Toon shading and outlines:** adjust lighting bands and edge appearance.
- **Procedural grass:** tune density, shape, distribution, distance detail and
  wind, with game lighting, fog and actor interaction.
- **Custom textures:** load and dump textures using the Azahar-compatible
  texture-pack workflow. Additional community packs are user-provided.
- **Optional effects:** explore ambient occlusion, reflections, additional
  shadows and anti-aliasing. These are experimental and their GPU cost varies;
  ambient occlusion, reflections and additional directional shadows are off
  in the default profile shown above.
- **Presentation:** adjust widescreen framing and FOV, or use x2/x3 visual
  interpolation for 60/90 Hz presentation while retaining the game's native
  30 Hz simulation and normal gameplay speed. Actual performance depends on
  the scene, settings and hardware.
- **Single-screen UI:** configure the integrated TopScreen HUD and menus.

**F2** temporarily switches the main added effects off and back on without
erasing your configuration. It is a quick comparison, not a switch to another
game renderer. F1 starts hidden; existing user profiles are preserved.

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
object cache, private diagnostic captures, ROM inputs or save states wholesale.
The three curated README screenshots are documentation media, not asset packs.

## Project status

TriAevum Runtime/Forge separation, publisher-side title compilation and the
dump-to-open-title vertical are implemented. A binary must still be produced
by the audited release workflow; an arbitrary developer build must not be
described as a public ROM-free release.

## Thanks

Special thanks to the two principal donor projects:

- **[Ship of Harkinian / Shipwright](https://github.com/HarbourMasters/Shipwright)**
  and its contributors, whose platform, resource and renderer foundations
  helped make this project possible.
- **[Azahar](https://github.com/azahar-emu/azahar)** and its contributors, for
  their 3DS research and implementation work, including PICA rendering and
  audio code and behavior used by this project.

Thanks also to [libultraship](https://github.com/Kenix3/libultraship),
[NVIDIA NRI](https://github.com/NVIDIA-RTX/NRI), and **M-1**, author of
[TopScreen](https://gamebanana.com/mods/695893), for the mod's reverse
engineering, programming and testing. The original game is the work of
Nintendo and Grezzo; this independent project does not imply their endorsement
or that of any donor.

See [the full third-party notices](THIRD_PARTY_NOTICES.md) for contribution
scope, additional credits and licenses. Renaming components never removes
their history or attribution obligations.
