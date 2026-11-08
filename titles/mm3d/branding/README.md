# MM3D (Moonfall Curse) branding

Artwork supplied by the project owner on 2026-11-08 for the MM3D product and
its Forge. Local to `titles/mm3d` (never exported or published), modelled on
`tools/triaevum_release/branding/README.md` (OoT3D), whose mechanism it
reuses: the same generator, the same sizes, the same consumers selected per
title.

| Master | Owner's file | What it is | Used as |
| --- | --- | --- | --- |
| `masters/game_icon.png` (1024 px) | "Maschera di Majora, anello turchese.png" | Majora's Mask in a turquoise ring | The MM3D game's icon: the executable (`mm3d_windows.rc`, `TRIAEVUM_PRODUCT_TITLE=mm3d`) and its window (`TitleSession::WindowIcon`) |
| `masters/forge_icon.png` (1024 px) | "Maschera di Majora in acciaio.png" | Majora's Mask in steel | The MM3D Forge's icon: `TriAevumForge.exe` built with `--features title-mm3d`, its window |
| `masters/forge_background.jpg` (1672x941) | "Clock Town sotto la luna cadente (1).png" | Clock Town under the falling moon | The MM3D Forge window's background, compiled into `TriAevumForge` with `title-mm3d` |

Source hashes (the owner's originals as received, copied under the
generator's names to `I:/_triaevum_work/mm3d-port/branding-sources-mm3d`,
`SOURCES.sha256`):

| Source | SHA-256 |
| --- | --- |
| `game_icon_source.png` (1254x1254 RGBA) | `9e7d4f5cfa94a833fbc0779b63bc9a101f6159fcf352dc1b4b947e35302b804f` |
| `forge_icon_source.png` (1254x1254 RGBA) | `a276f75e1c1bf71358b9b9ab309a0d0f0e9bd42aaf9ec7ae65f731e6240b3b9e` |
| `forge_background_source.png` (1672x941 RGB) | `6943d311c9154761f9f41f241f632b2f93bf933de57888f414769991d9899368` |

The motifs (Majora's Mask, the falling moon, Clock Town) are associated with
Nintendo's *The Legend of Zelda: Majora's Mask*. Nothing here grants rights in
Nintendo trademarks (`LICENSE_SCOPE.md`, "Excluded material").

## Derived files

Never edit these by hand; change a master and regenerate:

    python -I tools/triaevum_release/branding/generate_branding.py --title mm3d
    python -I tools/triaevum_release/branding/generate_branding.py --title mm3d --import-sources DIR

`--import-sources DIR` rebuilds the masters exactly as for OoT3D: the emblems
are cropped to their art with a 2 % margin, the near-transparent haze
(alpha < 12) is cleared and they are scaled to 1024 px; the background is
stored as a JPEG (quality 90, 4:4:4). Output bytes are stable for one
Pillow/zlib version (generated with Pillow 10.4.0, Python 3.13.8).

| Output | Sizes | Consumer |
| --- | --- | --- |
| `windows/triaevum.ico` | 16, 20, 24, 32, 40, 48, 64, 128, 256 (PNG at 256) | `mm3d_windows.rc`, chosen in `CMake/oot3d/game.cmake` by `TRIAEVUM_PRODUCT_TITLE=mm3d` (default `oot3d`: `tools/oot3d/native_game_runtime/triaevum_windows.rc`); SDL gives the game window the executable's first icon on Windows |
| `windows/forge.ico` | same | `tools/triaevum_forge/crates/forge-desktop/build.rs` with the `title-mm3d` feature (`embed_windows_icon`) |
| `forge/forge_window_icon.png` | 256 | The MM3D Forge's window icon (`theme.rs`, `#[cfg(feature = "title-mm3d")]`) |
| `mm3d_window_icon_rgba.inc` | 64x64 RGBA | `titles/mm3d/adapter/mm3d_window_icon.cpp` (`Mm3dWindowIcon`), returned by the MM3D title session's `WindowIcon()`; the host passes it to `Fast::WindowIconImage` / `SDL_SetWindowIcon` (X11, macOS) |

Not generated for MM3D:

- Android launcher icons: Android is owner-deferred.
- Linux hicolor icons: the per-title release preparation (INTEG's
  `title_release.py`) has no Linux desktop entry for the MM product (the
  AppImage and Flatpak entries name only `io.github.coccofresco.TriAevum`).
  When an MM desktop entry exists, set `"linux": True` for `mm3d` in
  `generate_branding.py` `TITLES` with its own application id.
- `forge-update-helper`: not built per title (`title_release.py`
  `forge_features` names forge-desktop and forge-cli only), so it keeps the
  OoT3D Forge icon.

## The Forge window

The MM3D Forge uses the same theme, panels and crop rule as OoT3D's
(`crates/forge-desktop/src/theme.rs`): the background covers the window
without stretching; on a wide window the page panel keeps 62 % of the width
and the subject shows beside it; on a narrow or tall one it stays near the
right edge behind the translucent panel. The subject is the clock tower's
face, anchored at (0.84, 0.40) of the art (OoT3D: the sword at
(0.775, 0.45)); the falling moon sits above and left of it (about
(0.60, 0.15)) and shows on wide windows above the panel's right edge, partly
behind it.

The WCAG AA test (`text_on_the_page_panel_meets_wcag_aa_over_the_brightest_art`)
runs against the MM3D art under `--features title-mm3d`: its brightest pixel
is pure white (the sun behind the town wall), the same worst case as OoT3D's,
so the ratios are identical (body text 11.44:1, lowest error text on an
error-tinted card 4.88:1). The shared theme was not changed.
