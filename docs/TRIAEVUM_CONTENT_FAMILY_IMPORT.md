# Automatic EUR / USA content-family import

## Player workflow

Select a decrypted `.3ds` or `.cci` ROM in Forge. No extraction, recipe
selection, second ROM or compiler is required. Exact known inputs retain
their existing fast path. Otherwise Forge recognizes qualified content
families without requiring a new whole-file hash recipe for every repack.

This is implemented in Forge, not in the renderer or game runtime. A new
frozen Forge and matching recipe/catalog metadata are required; the current
published alpha.1b remains unchanged.

## Compatibility, not blind acceptance

- Decompressed code remains exact: it includes initialized data and tables
  used by the precompiled title, not only instructions. Different compression
  or cartridge packaging does not change this identity.
- ExHeader identity excludes its signature/public-key area `0x400..0x5FF` and
  the two storage/compression bits at `0x0D`. All other bytes remain checked,
  including process layout, stack, permissions and program identity. The
  supplied header is retained; no original signature bytes are distributed.
- RomFS identity hashes all directory paths, file paths, sizes and complete
  file contents. Physical offsets, padding, file order and the IVFC wrapper
  do not define the identity. Both tree traversal and hash-table lookup are
  validated, including bounds, cycles, duplicate paths and overlapping payloads.
- The USA family reuses the existing offline code COPY and regional-resource
  adapter. Its source and execution families are checked separately. No USA
  assets are replaced with assets from another user's EUR ROM.
- The catalog binds the family contract to the existing precompiled module.
  Installation validates the actual prepared files again before activation.
  A recipe-only relaxation cannot bypass the catalog. Older recipes without
  a family contract retain their exact-input behavior.

Encrypted packages, unknown code revisions, changed process configuration,
missing files and altered native asset payloads are not accepted merely
because a filename or title ID says EUR or USA. Custom textures remain a
separate supported renderer feature, not permission to ignore source changes.

## Owners

| File under `tools/triaevum_release` | Responsibility |
| --- | --- |
| `romfs_identity.py` | Bounded logical filesystem identity and lookup validation |
| `data_compatibility.py` | Family schema, normalized header and code/data verification |
| `build_content_families.py` | Publisher qualification from exact audited source and execution inputs |
| `forge_gui.py` | Automatic fallback from exact identity to qualified family |
| `input_adapters.py` | Verify both sides of offline regional adaptation |
| `forge.py`, `precompiled_titles.py` | Preserve actual file identities and validate module-bound execution data |

## Reproduce publisher metadata

Supply a private JSON specification with `families`, each containing a
packaged `recipe` ID and `source`/`execution` extracted-directory paths.
For EUR the same directory supplies both. For USA execution uses the audited
output of the existing adapter. The publisher rejects files that do not
exactly match the original recipe on either side before generating families.

```powershell
python -m tools.triaevum_release.build_content_families `
  --package <clean-package> --specification <private-qualification.json> `
  --output <new-overlay-directory>
```

Merge the emitted `layout-overlay.json` into the publisher's explicit layout,
ship the rebuilt Forge with its corresponding source, and run release audit.
The output contains fingerprints and counts, not asset payloads or keys.
Do not ship the publisher's inputs or synthetic test ROMs.

## Qualification

Private evidence: `I:/oot3dre_work/catalog-rom-inputs/family-overlay`,
`family-forge`, `family-proof`, `families.json`, `repack_for_test.py` and
`qualify_families.py`. Frozen Forge was built in approximately 20 seconds;
the game DLL and runtime were not rebuilt.

The qualified EUR tree has 1,974 files / 52 directories; USA has 1,944 files /
50 directories. A measured EUR logical-tree scan took approximately 0.40 s on
this PC with warm storage; this is not a cold-storage or end-to-end promise.

Tests deliberately reverse the physical placement of every file payload and
replace ExHeader signature/key bytes, while retaining the native files and
execution layout. These are private synthetic repacks, not additional verified
catalogue editions or cryptographically authenticated cartridge images. Their
IVFC integrity trees are not rebuilt: the service-view importer independently
verifies every logical file instead of claiming signature/IVFC authenticity.

205 automated tests passed, including altered payload/code/header rejection,
equivalent layout acceptance, legacy strictness, lookup corruption rejection,
publisher preconditions and catalog binding. Real frozen-Forge import and NRI
intro runs of both repacks completed normally; frame-600 framebuffer captures
were visually inspected. Installation took 20.88 s (EUR) and 23.33 s (USA),
including the existing USA adapter, with zero compiled objects. TopScreen's
official archive was available locally, so these times exclude its download.
Full gameplay and all regional behavior are not certified by bounded boot tests.
