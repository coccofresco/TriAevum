# HD UI texture pack

Forge's highlighted **HD UI textures** section installs chubbycubby's
**OoT3D Vanilla UI/HUD** 3.2 (<https://gamebanana.com/mods/701929>, CC BY-NC-ND
4.0, credits HenrikoMagnifico and M-1): an Azahar texture pack of HD redraws of
the HUD, the menus and SingleScreen's UI, with variants per game language. It
replaces the SingleScreen 3.1 download Forge used to offer: TopScreen's HD UI
atlas now comes from this pack, and SingleScreen 3.1's HD Latin font ships in
the TriAevum package (`docs/TRIAEVUM_TOPSCREEN_INSTALLATION.md`).

TriAevum never distributes the pack (`LICENSES/OoT3D-Vanilla-UI-HUD.md`); the
release audit forbids the archive's and every texture's SHA-256.

## User flow

1. **Download HD textures** fetches `oot3d_vanilla_ui_hud_32.zip` from
   GameBanana (`https://gamebanana.com/dl/1857740`). The checkbox, on by
   default, does the same as the first step of **Prepare and install** when
   the archive is not stored yet; a failed download keeps the native textures
   and never stops the installation.
2. If the download fails, **Select .zip...** copies the zip the user downloaded
   from the mod page, and **Select extracted folder...** imports a folder
   extracted with any tool (the pack's folder, its `load/` folder, or
   `load/textures/0004000000033500/`). An archive placed beside
   `TriAevumForge.exe` or under `mods/` beside it is found too.
3. Forge keeps the archive in `data/downloads/` only when its size
   (451,655,008 bytes) and SHA-256
   (`5ff478db312c3f57f54263cd84f4caef15bbb391fd1dda3bdd3c25c4917938a1`) match
   the revision recipe. A newer release on the page fails verification.
4. **Prepare and install** extracts exactly the 76 catalogued textures and
   verifies each one's size and SHA-256 (`tools/triaevum_release/ui_texture_pack.py`).

## Languages

The pack keeps language-specific art in folders below `UI/`: the native menu
language folders (`00_JP_JAPANESE` ... `08_EU_ITALIAN`, as in
`rom:/menu/<folder>/`) and family folders (`GERMAN`, `SPANISH`, `ITALIAN`,
`JAPANESE`). The same texture hash appears in several folders with different
art (for example `BB00B25B152582B6` in `UI/` and `UI/01_US_ENGLISH/`), so an
emulator loading every folder picks one at random. TriAevum loads only the
boot language's folders, in order:

| Game language | Folders, first wins |
|---|---|
| Japanese | `00_JP_JAPANESE`, `JAPANESE` |
| English | `02_EU_ENGLISH`, `01_US_ENGLISH` |
| French | `04_EU_FRENCH`, `05_US_FRENCH` |
| German | `03_EU_GERMAN`, `GERMAN` |
| Italian | `08_EU_ITALIAN`, `ITALIAN` |
| Spanish | `06_EU_SPANISH`, `07_US_SPANISH`, `SPANISH` |

then `common/`, everything outside the language folders (`UI/`, `BOSH RUSH/`,
`LINK/`). Europe comes before North America because TriAevum runs the European
executable. The boot language is the one chosen in Forge's **Game language** or
in the in-game language setting, which applies at the next start; the texture
folders follow it the same way (`UiTexturePackLanguageFolders` in
`tools/oot3d/native_game_runtime/oot3d_game_language.cpp`).

## Runtime

The launch profile passes the prepared pack with `--ui-texture-pack <dir>`;
the directory holds `common/`, `languages/<folder>/` and Forge's `source.json`
receipt, which the installed-title check pins by SHA-256. The window setup
gives the runtime the existing language folders and `common/` as base
directories (`AzaharTexturePackRuntime::SetBaseDirectories`). They load
beneath the user's own texture pack, whether or not F1's custom textures are
on: a texture of the user's pack wins, then the earlier base directory. A user
pack in the other Azahar hash mode keeps working; the base pack is then left out
and F1's texture-pack status says so.

TopScreen takes the pack's `UI/tex1_512x256_1FAF9B5B2ADF6038_13_mip0.png`
(4096x2048) as its HD UI atlas (`--topscreen-ui-atlas-hd`). The pack's
`sys8.qbf` is not an A4 coverage font and is not used.

Private outputs: `data/mods/ui-textures/vanilla_ui_hud-3.2/`.
