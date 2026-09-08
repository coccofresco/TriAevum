# TriAevum

A native PC recompilation of *The Legend of Zelda: Ocarina of Time 3D*, with
single-screen controls and menus, widescreen support and customizable graphics.

**[Download v0.6.0-alpha.1 for Windows x64](https://github.com/coccofresco/TriAevum/releases/tag/v0.6.0-alpha.1)**

## Install

Requires a Vulkan-capable GPU with current drivers and your own supported
decrypted copy of the **EUR cartridge release, product code `CTR-P-AQEP`,
Title ID `0004000000033600`**, matching the supported revision below.

**Supported revision:** `oot3d-eur-project-baseline-16a6b0aa`.
Its decompressed ExeFS `.code` is **4,567,040 bytes**, with SHA-256:

```text
16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220
```

This is the hash of the **decompressed game code, not the entire ROM**.
Forge checks it automatically along with the ExHeader and RomFS against the
[exact revision catalogue](tools/triaevum_release/supported_revisions.json).
An EUR label or matching Title ID alone does not establish compatibility;
other revisions and modified ROMs are not supported.

1. Download the **Windows-x64 ZIP** from the release page and extract it into
   a writable folder.
2. Run `TriAevumForge.exe` and select your `.3ds` or `.cci` ROM.
3. When setup finishes, select **Launch game**. Afterwards, use `TriAevum.exe`.

No compiler or SDK is needed. Forge checks ROM compatibility and downloads the
TopScreen texture package during setup; an Internet connection is required
unless the package is [provided locally](docs/TRIAEVUM_TOPSCREEN_INSTALLATION.md).
Keep the `data/` folder: it contains your game data, saves and settings.

This is an experimental alpha. See the
[release notes and known limitations](docs/releases/v0.6.0-alpha.1.md).

## Features

- Single-screen HUD and menus through the integrated **TopScreen** mod.
- Widescreen framing and adjustable field of view.
- **60/90 Hz visual interpolation**, retaining the original 30 Hz game logic
  and normal gameplay speed.
- Configurable **toon shading, outlines and procedural grass**.
- Optional ambient occlusion, reflections, shadows and anti-aliasing.
- **Azahar-compatible texture packs**, texture dumping and named graphics profiles.
- Audio, configurable controls and save states.

Press **F1** for settings. **F2** temporarily toggles the main added graphics
effects off and back on without losing your configuration. Performance depends
on your hardware and selected effects.

## Screenshots

Captured in game with the default profile: toon shading, outlines, grass,
1.10x FOV, x2 interpolation and TopScreen. No additional HD texture pack.

![Hyrule Field at night](docs/images/default-hyrule-night.png)

![Kokiri Forest gameplay with the TopScreen HUD](docs/images/default-kokiri-forest.png)

![Link and Epona in the title sequence](docs/images/default-title.png)

## Source and license

Original contributions are licensed under **GPL-3.0-or-later**. Third-party
components retain their own licenses and notices:
[license scope](LICENSE_SCOPE.md) and [third-party notices](THIRD_PARTY_NOTICES.md).

For development and packaging, see the
[developer workflows](docs/TRIAEVUM_DEVELOPMENT_AND_RELEASE_WORKFLOWS.md).

No ROM or original game assets are included. TriAevum is an independent project,
not affiliated with or endorsed by Nintendo.

## Thanks

Special thanks to **[Ship of Harkinian / Shipwright](https://github.com/HarbourMasters/Shipwright)**
for its platform and renderer foundations, and **[Azahar](https://github.com/azahar-emu/azahar)**
for its 3DS rendering, audio and research contributions.

Thanks also to [libultraship](https://github.com/Kenix3/libultraship),
[NVIDIA NRI](https://github.com/NVIDIA-RTX/NRI), and **M-1**, author of
[TopScreen](https://gamebanana.com/mods/695893), for the mod's reverse engineering
and development. The original game was created by Nintendo and Grezzo.

[Full credits and licenses](THIRD_PARTY_NOTICES.md).
