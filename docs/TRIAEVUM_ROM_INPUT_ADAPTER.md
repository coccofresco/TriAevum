# Offline ROM input adapter

Status, 2026-09-08: **experimental USA-input installation, boot, title intro
and file-selection menu verified**. Not yet qualified for an entire playthrough
or published in the GitHub release. Branch: `feat/rom-input-adapter`.

## Decision

Adapt the install inputs to the existing EUR execution contract. Do not make
a second port, relocate runtime hooks, recompile the game, or convert assets
per frame. The published NRI/Vulkan runtime, precompiled game DLL, renderer,
audio, interpolation and TopScreen code remain unchanged.

This runs **canonical EUR logic with resources derived from the supplied USA
ROM**, not a newly recompiled USA executable. Regional behavior differences
are not automatically preserved by this approach. The full dump catalogue
identity is still unresolved; see [input identity audit](TRIAEVUM_USA_REV1_PORT.md).
Only the exact extracted input triplet is accepted, not every file named USA
Rev 1. The existing EUR import path remains available and unchanged.

## What changes offline

| Input | Action |
| --- | --- |
| Decompressed code | COPY-only recipe reconstructs the exact canonical image expected by the existing DLL. Both input and output SHA-256 are checked. |
| USA ExHeader | Preserved byte for byte. Its parsed process/thread configuration matches the baseline. |
| RomFS | Produce a Level-3 service view; do not pretend the modified image retains valid IVFC integrity hashes. |
| Regional paths | Rename `message/us` and `misc/us` to `eu`, and `us.qm` to `eu.qm`; rebuild the RomFS hash chains. |
| QM language table | EU slots 2/4/6 reference the original USA English/French/Spanish payloads in slots 1/5/7. |
| Models, scenes, textures, audio, dialogue payloads | Retain USA file payloads; no replacements from an EUR ROM. |

The current code recipe contains 55,745 COPY operations and is 1,825,834
bytes of JSON. It contains source offsets and lengths, not literal replacement
bytes. This is whole-image normalization, **not a claim that only a handful of
instructions differ**. The publisher uses both images to construct the recipe;
the end user needs only their matching USA ROM and the packaged recipe.

Do not synthesize missing German/Italian dialogue. Some regional hint-movie
paths also differ and still need scenario qualification. A successful boot
does not prove those paths, all saves, or every localized gameplay flow.

## Code boundaries

| File under `tools/triaevum_release/` | Responsibility |
| --- | --- |
| `input_copy_adapter.py` | Bounded COPY schema, publisher generation, strict application and output verification. No arbitrary patch instructions. |
| `oot3d_region_assets.py` | OoT3D-owned RomFS/QM interpretation; preserves payloads, validates extents and ancestry. |
| `input_adapters.py` | Verify original identities, normalize in a new staging directory, verify execution identities, retain provenance. |
| `forge_gui.py` | Select the source recipe, invoke adaptation before the existing preparation/activation transaction. |
| `precompiled_titles.py` | Bind recipe, adapter, execution inputs and existing module identity together. |
| `build_oot3d_input_adapter.py` | Publisher-only metadata overlay for the exact audited USA input; references the existing DLL and corresponding source. |
| `audit_release.py`, `release_policy.json` | Explicit `input_copy_adapter` artifact role, strict schema/hash binding, reject uncatalogued adapters. Existing ROM/asset/IPS/BPS prohibitions remain. |

Packaged `recipes/oot3d.json` takes precedence over the embedded source-tool
fallback. A revision still needs a matching verified precompiled catalog;
adding a filename or hash to a recipe cannot bypass this check.

Source identity and execution identity are deliberately separate. Each
normalized source directory retains `input-adaptation.json`, recording both
triplets, the original program ID, adapter identity, resource changes and zero
compiled objects. Failed adaptation removes only its newly created staging
output; it does not activate anything or touch the user's ROM/saves.

## Verified results

- Exact canonical code output SHA-256:
  `16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220`.
- Unchanged USA ExHeader SHA-256:
  `dbe5fa0174d73bffb75d7cf0fbaa05d3e0ee080df0e257afc58b6c45ae5de3d0`.
- Adapted Level-3 RomFS, 473,522,176 bytes, SHA-256:
  `011c0b6933f3932c704ff1c6ab562023f407c2929661a6d54ecea1ca81072f99`.
- Independently resolved all 1,944 output file paths. 1,943 file payloads
  match USA byte for byte. The sole changed file is QM: only the three slot
  references per record change; all 2,510 records and dialogue payloads were checked.
- Frozen Forge installed from the supplied ROM in **27.14 seconds**, including
  **14.02 seconds** for adaptation and hash verification. No game compilation;
  official TopScreen ZIP already present, so no network-download time included.
  These are this-PC observations, not promises for other storage/hardware.
- The regenerated TopScreen pack uses normalized USA resources and the official
  mod archive. It does not import the mod's executable patch.
- Installed executable SHA-256 remains
  `dd80b05e6c3b2129d3023150f00cf4fadfbd3ff546ebf481b74f5819af3dba36`;
  game DLL remains
  `106aa7a6b1be71b2f2cee9c0f4baf2a3f0d43a3c4a1443e84ff365837c536549`.
- The actual Forge-generated installation rendered the intro and accepted
  Start to reach file selection. Framebuffer captures, not desktop screenshots,
  confirm scene, actors, logo, and menu. Test ran 2,602 presentations / 1,280 game
  updates, exited normally, and was not an uncapped performance benchmark.

## Reproduction and packaging

Publisher, from a clean existing release and privately extracted inputs:

```powershell
python -m tools.triaevum_release.build_oot3d_input_adapter `
  --package <existing-release-directory> `
  --source-inputs <USA-extracted-directory> `
  --canonical-code <private-EUR-code.bin> `
  --output <new-adapter-overlay-directory>
```

Merge the generated `layout-overlay.json` entries into the package layout:
replace the recipes/catalog entries and add the explicit COPY-adapter entry.
Build Forge from this branch; reuse the release's game executable, DLL and
corresponding title sources. The user's installation requires no reference EUR
image, Python, compiler, SDK or developer checkout.

Do not bundle the private normalized inputs, ROM, TopScreen ZIP or capture files.
The adapter is an explicit transformation artifact, not an asset or a generic
JSON loophole. Its source implementation belongs in the matching source archive;
the existing game-derived code/source release policy still applies. This
engineering audit is not a new legal determination about distribution rights.

Focused regression suite (set `PYTHONPATH` to the checkout and release-tools
directory, and use a temporary directory on a drive with space):

```powershell
python -m unittest test_input_adapters test_input_copy_adapter `
  test_oot3d_region_assets test_forge_gui test_precompiled_titles `
  test_precompiled_release test_release_audit test_ctr_rom `
  test_forge test_topscreen_assets test_bundle_paths -q
```

70 tests pass: source/output mismatch rejection, COPY bounds and no literals,
path/hash reconstruction, QM slot semantics, payload preservation, rollback,
publication/reuse, unchanged EUR path, precompiled identity and release auditing.

Local private evidence is under `I:/oot3dre_work/usa-rev1-port/`:
`forge-install.log`, `payload-conservation.json`, `boot-probe/`,
`forge-boot-menu-probe/` and `candidate-installation/`. The latter is installed
with fresh saves and can be tested without changing the published installation.

## Before general release support

1. Qualify new-game creation, playable Kokiri, saving/reopening, pause/input and
   representative scene transitions using the adapted installation.
2. Exercise English/French/Spanish dialogue and regional hint resources. Fix
   only evidenced offline input contracts; do not add per-scene runtime patches.
3. Check repeat-install and EUR-install behavior, package/source correspondence
   and the existing release audit before publishing a new package.

No public release or online README was changed to claim USA support in this step.
