# MM3D offline catalogs (local-only)

`water.json` owns the MM3D layout and semantic water-name rules. CMB v10 adds a
chunk pointer before materials (tex at header +0x30; texture bytes +0x44), ZSI
uses version 9, and models live under `scenes` and `actors` in ZSI/GAR v2/LzS.
The layouts agree with the existing DL room model reader and dekui GAR reader.
The offline EUR survey decoded 995 containers / 8,426 textures / 5,014 names
without rejected models. No texture pixels are decoded or statistically scored.

Water rules distinguish underwater light layers before surface water and sea
surfaces before waterfall names. The `taki_` room-asset prefix does not make its
trees, walls or ceiling waterfalls. Mist (`mizukemuri`), vegetation, Water Bomb
bodies, water Deku Baba bodies and torch fixtures are excluded. These are texture
semantics, not per-scene lighting lists. Unknown/ambiguous names remain outside
this catalog; collision water-body recognition continues independently. The
catalog is not a claim that every animated or unnamed water asset is identified.

## Local preparation

```text
python tools/mm3d/prepare_catalogs.py --code-bin CODE --romfs ROMFS --data-root DATA
```

This invokes the shared water generator and DL's existing guarded room-emitter
generator. It writes hash-checked caches plus `DATA/catalogs/mm3d/manifest.json`
with the two runtime CLI arguments and `recipe-fragment.json`. Rejected rooms
prevent publication of the manifest. No game boot, compiler, framebuffer or
user configuration mutation is performed.

Merge the `water_catalog` and `room_catalog` members of the fragment into the
**verified local MM3D revision recipe** to use native Rust Forge preparation,
repair and activation (with a matching Python oracle). The fragment is NOT a supported-revision contract: it contains no
ExHeader/code contract, title module or publication signature. There is no
qualified MM3D recipe in `supported_revisions.json` yet. Room analysis remains a
publisher-only Python step; the player-side Rust installer verifies the actual
RomFS and all decompressed room hashes before installing the derived table.
Three unused room files absent from the guarded native scene table are explicit
hash-bound exclusions, not manual scene rules. Changed/unknown rooms fail closed.

## Ownership and remaining work

| Family | Status at this slice |
| --- | --- |
| Room emitters / emission follow set | Existing DL analysis/consumer, now with native Forge publisher-table install/repair. 310 rooms / 145 emitters; 313 files verified including three native-table exclusions. Not a full material census. |
| Water roles | New shared bounded readers, title rules, Forge recipe routing; 45 distinct local identities. |
| Static shadow actors | PM0 per-title seam exists but MM uses empty default. OoT's referenced aggregation script is absent from the repo; MM needs stability evidence and all-variant semantic review before populating. |
| PBR texture-hash packs / reflection rules | Existing shared authoring mechanisms. Material maps require owner-authored content; neither native fullbright nor a name creates a PBR emission map. |
| Shader corpus / fragment families | Optional portable title vertex/pass pack and exact-context replay are implemented. Observed Gzw/Layout coverage is not an all-title corpus; raw-irradiance uber coverage and release qualification remain open. See `docs/TRIAEVUM_TITLE_SHADER_PACKS.md`. |
| Grass rules | GRASS lane; actual rule/mask authoring belongs to owner. |
| Environment profiles | GP lane, values untouched here. |
| Interior-sun masks | Requires an MM room-shell producer plus native Forge/runtime prebuild support. |

The release exception permits computed room-light placements and portable shader
corpora under the same qualification rules as OoT, NOT raw ROM geometry, textures,
code, states or captures. This slice ships no generated MM catalogs or maps.
Tests: `python -m unittest discover -s tools/mm3d -p test_prepare_catalogs.py`;
shared reader tests and local Python/Rust byte comparison are documented in
`docs/TRIAEVUM_OFFLINE_MODEL_CATALOGS.md`.
