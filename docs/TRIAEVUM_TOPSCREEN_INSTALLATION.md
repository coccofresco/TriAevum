# TopScreen textures in the base installation

Implemented in the `release/triaevum-precompiled` worktree. This extends the
default TopScreen behavior with the mod's modified native textures; it does not
replace the title adapter with the mod's executable patch.

## User flow

1. Select the supported decrypted `.3ds` or `.cci` ROM in Forge as before.
2. Forge verifies the precompiled module and the original game data.
3. Forge imports the official TopScreen 2.1.1 texture changes automatically.
4. The launch profile receives the verified local pack and the game is ready.

Only the ROM is requested through the normal UI. First installation needs
internet access unless `topscreen211.zip` is already beside `TriAevumForge.exe`,
under `mods/` beside it, or in `data/downloads/`. Archive size and SHA-256 must
match the recipe; a same-name archive is not automatically trusted. Later
installations reuse the verified local pack without another download.

The existing installation remains active if acquisition/import fails. Forge
reports a retry/offline-archive instruction; it does not silently activate a
texture-incomplete replacement. Saves and valid user configurations are preserved.

## Exact payload and boundary

- Official source: [Single Screen Experience by M-1](https://gamebanana.com/mods/695893).
- Version: 2.1.1, download `https://gamebanana.com/dl/1772020`.
- Archive: 179,430,508 bytes; SHA-256
  `e0c143c872ccf4ad72033caab768753b147a4033d260a701204a63ee6bc16df4`.
- Supported revision: the existing EUR recipe. Other revisions do not inherit
  this import implicitly.
- Inputs: 18 localized CTXB files (`menu_top_parts00` and `menu_cursor00`,
  nine languages), plus `custom_menu00` and `custom_font00` profile textures.
- The existing native CTXB decoder/pack builder deduplicates identical original
  texture identities. The verified real pack occupies 3,211,552 bytes.
- The optional 4K PNG pack is **not** imported by this base path.
- No IPS, ExHeader replacement or injected ARM code is extracted/applied.
- No ROM, mod archive or derived texture pack is included in the public release.
  These are private installation products downloaded/derived on the user's PC.

## Ownership and files

- `tools/triaevum_release/topscreen_assets.py`: bounded HTTPS acquisition,
  pinned integrity checks, atomic import and content-addressed cache.
- `supported_revisions.json`: revision-specific source URL, size and hash.
- `tools/oot3d/decomp_support/scripts/build_topscreen_texture_override_pack.py`:
  reusable `build_texture_pack`, native CTXB/RomFS decoding and O3TUv2 output.
- `precompiled_titles.py`: prepares the pack before transactional activation.
- `forge.py`: portable `--topscreen-texture-overrides` launch argument and
  texture hash/path in the `forge-state.json` runtime receipt.
- `installed_runtime.py`: validates the file, hash and unique launch routing.
- Existing `tools/oot3d/ui_topscreen/oot3d_top_screen_texture_overrides.*` and
  `tools/oot3d/native_game_runtime/oot3d_native_ui_texture_provider.*` remain
  the consumers. No title-specific behavior is added to the generic renderer.

Private outputs live in `data/mods/topscreen/<source-key>/`: `atlas_overrides.o3tu`
and `import.json`. The latter records archive/RomFS identities, importer version,
pack hash and attribution. Download temporaries are removed on failure.

## Verification

Unit tests cover verified archive reuse, offline reuse of the imported pack,
corruption rejection, bounded/atomic downloads, other-title exclusion, actual
CTXB import without executable extraction, and installed launch/hash validation.

The frozen Forge executable was rebuilt and used against the real ROM in
`I:/TriAevum-0.6.0-candidate-r1`. Reinstallation with the verified pack cached
took **16.574 seconds**, with **zero compiled game objects**. This does not
include a fresh 179 MB network download and is not a modest-PC timing claim.

A second frozen-Forge proof used an empty private installation, the real ROM
and the verified official archive, with no imported pack or generated data.
It completed extraction, texture import and activation in **16.124 seconds**,
again with zero compiled objects. The worker log and generated receipt are in
`I:/oot3dre_work/grass-outline-verification/forge-cold-import/`. This verifies
the importer inside the packaged executable, not only the Python source path.
The release suite passes **153 tests, one skipped**.

A real Vulkan/NRI run loaded the pack and reported actual PICA texture
substitutions (`applied=2`), not only successful parsing. Framebuffer captures
under `I:/oot3dre_work/grass-outline-verification/native-fog-topscreen-02` and
`native-fog-point-guides` show the HUD in Kokiri. Test processes exited normally.

The local candidate's Forge and private install have been updated. A new public
release archive has **not** been promoted or audited by this change; use the
normal allowlist release workflow, never distribute that installed directory.
