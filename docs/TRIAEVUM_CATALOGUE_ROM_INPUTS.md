# Catalogue ROM input qualification

## Scope

Four user-provided 7z archives were inspected read-only on 2026-09-08. Only
their CCI entries were extracted into private work storage. The complete,
decrypted 512 MiB images match the following No-Intro records by SHA-1:

| Region / revision | No-Intro | Complete decrypted SHA-1 |
| --- | --- | --- |
| Europe original | [0004](https://datomatic.no-intro.org/index.php?page=show_record&s=64&n=0004) | `8a1875bd21a47fad2b6de2e20637376563077729` |
| Europe Rev 1 | [1168](https://datomatic.no-intro.org/index.php?page=show_record&s=64&n=1168) | `43dc87494690361fab8846b5aa2946fb009470f4` |
| USA original | [0033](https://datomatic.no-intro.org/index.php?page=show_record&s=64&n=0033) | `71d872eecd859b68153edbf12c19edfcead22c18` |
| USA Rev 1 | [1259](https://datomatic.no-intro.org/index.php?page=show_record&s=64&n=1259) | `50d6b33de7298d397fa9f0dde78f8b0f23256564` |

The full images are different, but Forge consumes the decompressed `.code`,
ExHeader and RomFS of the application partition, not the cartridge update
software. Within each regional pair, all three consumed files are identical.
This does not assert that every file in the complete cartridge is identical.

## Minimum support change

**USA original and Rev 1 already match the existing alpha.1b USA input adapter.**
Their consumed files also match the earlier, non-catalogue-matching USA image.
No new recipe, COPY program, normalization rule, game DLL or runtime is needed.
The old filename's revision still cannot be established from those shared files,
but it is no longer necessary for recognizing either verified catalogue input.

**Both EUR catalogue inputs share one new, direct recipe:**
`oot3d-eur-catalogue-a584b067`. No adapter or data transformation is applied.

| Input | Bytes | SHA-256 |
| --- | --- | --- |
| Decompressed code | 4,567,040 | `16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220` |
| ExHeader | 2,048 | `a584b067427f81edcca739497ec7b4ce1217eb9612090e6ae5b846e5674d12c1` |
| RomFS | 479,260,672 | `a3ad05dcf12893de5f1617a60f4e80f54f5e1c80069922a96a4f9c8c8cbac8ca` |

Code and RomFS already match the old EUR recipe byte for byte. Only its
ExHeader differs: 512 changed bytes, comprising the flag byte at `0x00D`
(`0x03` in the older project input, `0x01` in the catalogue images) and 511
bytes within the `0x400..0x5FF` signature/public-key area. Outside those areas
the files are identical. The existing process-manifest builder produces
identical complete `process` and `primary_thread` objects from both headers.
The new header is preserved, not patched into the old one. Full hashes remain
mandatory; this is not permission to ignore arbitrary ExHeader changes.

## Implementation

- `supported_revisions.json` declares one additional exact EUR triplet, with
  both catalogue records attached to it. The legacy EUR recipe remains intact.
- `precompiled_variants.py` binds explicitly reviewed ExHeader variants to the
  existing base title and corresponding sources. It rejects changed code,
  RomFS, process descriptors, adapter inheritance, conflicting bindings and
  duplicate input triplets. Publication is idempotent.
- `prepare_release.py` includes those bindings when promoting a title build.
  No recognition by filename, region or catalogue number is added at runtime.
- Existing alpha.1b Forge already understands the resulting recipes/catalog.
  A metadata-only candidate can reuse its executable, Forge and game DLL.
  A release must include the updated recipes **and** matching catalog, not
  simply append a hash or overwrite files in a used installation.

Publisher overlay, applied to a clean alpha.1b package:

```powershell
python -m tools.triaevum_release.precompiled_variants `
  --package <clean-alpha.1b-package> --output <new-overlay-directory>
```

Merge its two `layout-overlay.json` entries into the explicit publisher layout
and run `package_release` and the normal release audit. No ROM, extracted data,
7z archive, private save or official TopScreen texture ZIP is distributed.
Forge still asks for the **extracted decrypted `.cci` or `.3ds`**, not a 7z.

## Verification boundary

Private evidence lives under `I:/oot3dre_work/catalog-rom-inputs/`:
`audit.json`, `exheader-comparison.json`, `package-overlay/` and `proof/`.
Unit tests exercise exact recognition, rejection, unchanged legacy/USA behavior,
shared title/source bindings and idempotency. Frozen Forge and real NRI boot
qualification are recorded there separately; these are not full-playthrough
or clean-Windows certification. The existing published alpha.1b is not silently
replaced by this development change.
