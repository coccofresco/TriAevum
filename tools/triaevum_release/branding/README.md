# TriAevum branding

Artwork supplied by the project owner for TriAevum (Echoes of Time). The
provenance note shipped with packages is `LICENSES/TriAevum-artwork.md`.

| Master | What it is | Used as |
| --- | --- | --- |
| `masters/game_icon.png` (1024 px) | Blue-violet ocarina with the golden Triforce in a gold and teal ring | The game's icon: `TriAevum.exe`, its window, the Linux desktop entry (AppImage, Flatpak), the Android launcher |
| `masters/forge_icon.png` (1024 px) | Silver ocarina with the Triforce in a silver ring | Forge's icon: `TriAevumForge.exe`, its window, `triaevum-update-helper.exe`, the desktop entry's "Prepare or repair game" action |
| `masters/forge_background.jpg` (1672x941) | Cathedral with the Master Sword in golden light | The Forge window's background, compiled into `TriAevumForge` |

The motifs (Triforce, Master Sword, Ocarina) are associated with Nintendo's
*The Legend of Zelda* series. Nothing here grants rights in Nintendo
trademarks (`LICENSE_SCOPE.md`, "Excluded material").

## Derived files

Never edit these by hand; change a master and regenerate:

    python -I tools/triaevum_release/branding/generate_branding.py

`--import-sources DIR` first rebuilds the masters from the owner's full-size
originals (`game_icon_source.png`, `forge_icon_source.png`,
`forge_background_source.png`): the emblems are cropped to their art with a
2 % margin, the originals' near-transparent haze is cleared and they are
scaled to 1024 px; the background is stored as a JPEG (quality 90, 4:4:4).
`--android-preview PNG` also writes a review sheet of the launcher icon under
the common masks. The script needs Pillow; output bytes are stable for one
Pillow/zlib version.

| Output | Sizes | Consumer |
| --- | --- | --- |
| `windows/triaevum.ico` | 16, 20, 24, 32, 40, 48, 64, 128, 256 (PNG at 256) | `tools/oot3d/native_game_runtime/triaevum_windows.rc` in `CMake/oot3d/game.cmake`; SDL gives the game window the executable's first icon |
| `windows/forge.ico` | same | `tools/triaevum_forge/build_support/windows_icon.rs` (forge-desktop and forge-update-helper `build.rs`): a compiled resource handed to the linker, no resource compiler |
| `forge/forge_window_icon.png` | 256 | Forge's window icon (`theme.rs`, `ViewportBuilder::with_icon`) |
| `linux/hicolor/<N>x<N>/apps/io.github.coccofresco.TriAevum.png` | 64, 128, 256, 512 | `Icon=` of the desktop entry; AppImage (`appimage_package.py`: hicolor tree, root icon, `.DirIcon`) and Flatpak (`flatpak_package.py`, manifest) |
| `linux/hicolor/<N>x<N>/apps/io.github.coccofresco.TriAevum.Forge.png` | 64, 128, 256, 512 | `Icon=` of the "Prepare or repair game" desktop action |
| `tools/oot3d/native_demo_host/triaevum_window_icon_rgba.inc` | 64x64 RGBA | The game window's icon on X11 and macOS (`Fast::WindowIconImage`, `SDL_SetWindowIcon`) |
| `ports/android/app/src/main/res/mipmap-*` | adaptive 108 dp layers, legacy 48 dp square and round, mdpi to xxxhdpi | `android:icon` and `android:roundIcon`; the whole emblem fits the 66 dp safe circle (96 % of it) over a deep teal radial background |

## The Forge window

The background covers the window without stretching, cropped so that the
sword stays in view: on a wide window the page panel keeps 62 % of the width
and the sword shows beside it; on a narrow or tall one it stays near the right
edge behind the translucent panel. The theme is dark and warm with gold
accents (`crates/forge-desktop/src/theme.rs`): the current tab and the primary
action are gold with dark text, pages sit on a dark panel at 89 % opacity, the
footer at 96 %. A unit test there checks WCAG AA (4.5:1) for body, weak,
accent, warning and error text on the panel composited over the art's
brightest pixel (pure white); the lowest ratio is error text on an error-tinted
card, 4.9:1, body text is 11.4:1.
