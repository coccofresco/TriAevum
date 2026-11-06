# Recipe-owned offline model catalogs

The existing Forge water step accepts an optional `water_catalog` object in the
verified revision recipe. Its layout and semantic name rules are title facts;
keep their source under `titles/<title>/catalogs/`. The shared Python and Rust
readers contain no title IDs, scene lists or image classifiers.

Without the field, both Forge implementations retain the original water
walker, rules, catalog bytes, cache keys and receipts. With it, they read:

- the declared top-level RomFS directories, in extracted, Level-3 or IVFC form;
- ZSI models and GAR v2 CMB members, optionally wrapped in bounded LzS;
- the declared CMB version/texture offsets, with bounds inside each model;
- base mip bytes only, tiled as stored, with the renderer's FNV offset basis;
- ordered semantic name rules (first match), excluding named non-water classes.

Unknown CMB versions, malformed compressed data, duplicate archive members and
out-of-bounds texture payloads fail the preparation. External textures and
unsupported texture encodings are not assigned invented hashes. Every role is
one or more of the renderer's existing bits: surface 1, river 2, waterfall 4,
underwater illumination 8. Duplicate identities combine role bits.

The output format is unchanged: `triaevum-water-textures 1`. Forge's existing
activation passes the generated path through `--water-texture-catalog`. The
receipt adds `profile_sha256`, the SHA-256 of sorted compact ASCII JSON; changing
any layout or rule invalidates the cache without touching another title's cache.
Output and receipt writes remain atomic. ROM-derived catalogs stay on the user's
machine; this feature does not expand the precompiled-release allowlist.

Developer generation (not the user's install path):

```text
python tools/triaevum_release/water_textures.py ROMFS OUTPUT --profile TITLE_PROFILE.json
```

Tests: `test_model_catalog.py`, the unchanged `test_water_textures.py`, Rust
`forge-assets` unit/preparation tests and `python_oracle_assets`. The latter
compares bytes AND receipt/cache paths for synthetic compressed models in all
three RomFS forms, alongside the old OoT fixtures. Optional local verification:

```text
TRIAEVUM_FORGE_REAL_ROMFS=... TRIAEVUM_FORGE_WATER_PROFILE=... TRIAEVUM_FORGE_PYTHON=...
cargo test -p forge-assets --test real_romfs profiled_water_matches_python -- --ignored --nocapture
```

This supplies one catalog preparation step, not a new supported-ROM recipe,
translated title module, full shader corpus or installed PBR material pack.
Those require their own publication and qualification contracts.
