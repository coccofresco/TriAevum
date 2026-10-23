# TopScreen textures in the base installation

TriAevum's TopScreen profile is the typed port of **SingleScreen 3.0**
(the successor of TopScreen / Single Screen Experience, by M-1 / rlgcarrot,
<https://gamebanana.com/mods/695893>). Since 2026-10-17 SingleScreen 3.0 is
integrated into TriAevum with its author's permission: the package carries its
low-resolution graphics, so installing needs nothing beyond the ROM. TopScreen
2.1.1 is no longer used (its archive is no longer published either).

## User flow

1. Select the supported decrypted `.3ds` or `.cci` ROM in Forge as before.
2. Forge verifies the precompiled module and the original game data.
3. Forge installs the SingleScreen 3.0 UI atlas and Link animations and the
   SingleScreen 3.1 HD font shipped in the package. No download, no archive,
   no network access; only the optional HD UI texture pack is downloaded, on
   the user's request.
4. The launch profile receives the installed pack and the game is ready.

## What ships, what does not

Shipped, unchanged, from the official `singlescreen_30_25aeb.zip` (28,883,512
bytes, SHA-256 `5668640fec8dc2b47b4516fd0b871c4d7d90ca1c2203acc2a22adb59aaab5993`),
in `resources/topscreen/singlescreen-3.0/` (repository:
`runtime/triaevum_resources/topscreen/singlescreen-3.0/`):

| File | Bytes | SHA-256 | Use |
|---|---|---|---|
| `custom_ui01.ctxb` | 131,144 | `20290e417c56973cd19daa1f61a6c3963f075d61776db3d996954ee3296d07d8` | 3.0 UI atlas (profile texture `oot3d/topscreen/3.0/ui_atlas`) |
| `zelda_link_ext.zar` | 186,404 | `06d60e05c493e86677497eb935b49951d613249c43f896816ae898bb8b005902` | Roll Dash and landing animations |

SingleScreen 3.1 (`singlescreen_31_989c3.zip`, 28,384,023 bytes, SHA-256
`f91485999a6b8e89b8d574064dc03c1c20704f2c718e93c5187251b6ef69dbcb`), the
release GameBanana now offers, carries both files unchanged. TriAevum also
ships its HD Latin font, unchanged, in `resources/topscreen/singlescreen-3.1/`
(repository: `runtime/triaevum_resources/topscreen/singlescreen-3.1/`):

| File | Bytes | SHA-256 | Use |
|---|---|---|---|
| `ltn16.qbf` | 427,592 | `8b83e081f721c6e1842b0a9cd90b71a3fb2849674d2b1040f3dc5c3221594169` | HD Latin font, EUR `message/eu` and USA `message/us` (the archive's two copies are identical) |

The files are identical in the archive's EUR, USA and JPN folders. Their notice
is `LICENSES/SingleScreen-3.0.md`; the release audit accepts them only at these
paths with these hashes (`topscreen_singlescreen_asset` role).

Not shipped:
- the archive's IPS patch, ExHeader and ARM payload: TriAevum reimplements the
  behavior as typed host code and never applies them;
- its `sys8.qbf`, which is not an A4 coverage font;
- the HD UI atlas (`tex1_512x256_1FAF9B5B2ADF6038_13_mip0.png`, 4096x2048), by
  chubbycubby. It comes from chubbycubby's HD UI texture pack when the user
  obtains it (`docs/TRIAEVUM_UI_TEXTURE_PACK.md`). The SingleScreen archives'
  hashes are forbidden in the package; Forge no longer downloads them.

## HD font and HD UI atlas

Every preparation adds `font_coverage.zip`: the shipped `ltn16.qbf` paired with
the ROM's own font (64x64 coverage cells, density 4, native metrics).

With the HD UI texture pack prepared, Forge also installs its atlas as
`ui_atlas_hd.png`, passed to the game with `--topscreen-ui-atlas-hd`; it applies
to the exact native atlas bytes, and a user texture pack that replaces the
atlas wins. Without the pack the game uses the native atlas resolution.

## Ownership and files

- `tools/triaevum_release/topscreen_assets.py`: verifies the shipped assets,
  builds the O3TU pack, installs the animations and the HD font coverage, and
  the HD UI atlas from the prepared texture pack; content-addressed cache with
  atomic writes.
- `tools/triaevum_release/ui_texture_pack.py`: the HD UI texture pack (its
  download URL, catalogue and verified extraction); its archive contract,
  `vanilla_ui_hud_3_2_archive`, is in `supported_revisions.json`.
- `tools/oot3d/decomp_support/scripts/build_topscreen_texture_override_pack.py`:
  `build_texture_pack(singlescreen_ui_atlas=...)` and O3TUv2 output.
- `precompiled_titles.py` prepares the pack before transactional activation;
  `forge.py` writes the launch arguments and the runtime receipt.
- Runtime consumers: `tools/oot3d/ui_topscreen/oot3d_top_screen_texture_overrides.*`,
  `tools/oot3d/native_game_runtime/oot3d_native_ui_texture_provider.*`. The
  Items hint (SELECT ?), the D-pad cycle marks, the HUD button circles, the
  D-pad cross, the ZR/ZL labels and the Ocarina page arrows draw from the
  shipped custom_ui01 atlas; nothing reads the retired 2.1.1 menu atlas or
  2.1.1's replaced menu_top_parts00.

Private outputs live in `data/mods/topscreen/<source-key>/`:
`atlas_overrides.o3tu`, `zelda_link_ext.zar`, `import.json`,
`font_coverage.zip`, `fonts_import.json`, and with the HD UI texture pack
`ui_atlas_hd.png`.
