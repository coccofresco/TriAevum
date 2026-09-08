# Supported ROM identification

## Catalogue verification, 2026-09-08

| Verified decrypted catalogue input | Availability |
| --- | --- |
| USA original (0033) and Rev 1 (1259) | Already accepted by published alpha.1b, with the existing USA adapter. |
| Europe original (0004) and Rev 1 (1168) | Supported in alpha.1c through one shared direct recipe. |

All four complete images were SHA-1 matched to No-Intro and tested with frozen
Forge and real NRI boot. Within each region their consumed inputs are identical.
See [exact identities, implementation and qualification](TRIAEVUM_CATALOGUE_ROM_INPUTS.md).
Alpha.1c also recognizes [equivalent content families](TRIAEVUM_CONTENT_FAMILY_IMPORT.md).
The older alpha.1b ZIP does not contain the new EUR recipe.

## European input: catalogue reference

The target is the original European cartridge release, application version 0:

- **No-Intro Nintendo 3DS record 0004**
- **Legend of Zelda, The - Ocarina of Time 3D (Europe) (En,Fr,De,Es,It)**
- Product code: `CTR-P-AQEP`
- Title ID: `0004000000033600`
- Languages: English, French, German, Spanish, Italian

Source: [No-Intro DAT-o-MATIC record 0004](https://datomatic.no-intro.org/index.php?page=show_record&s=64&n=0004),
consulted 2026-09-08. The catalogue name has no revision suffix.
The separately catalogued
[Rev 1, record 1168](https://datomatic.no-intro.org/index.php?page=show_record&s=64&n=1168),
has the same product code and Title ID: those identifiers alone are insufficient.

### Complete decrypted dump, as catalogued

| Field | Value |
| --- | --- |
| Size | 536,870,912 bytes (512 MiB) |
| CRC32 | `704f766d` |
| MD5 | `2d5e050996d10c0a4cbc317978b6c3fc` |
| SHA-1 | `8a1875bd21a47fad2b6de2e20637376563077729` |
| SHA-256 | `b14626b70330fe3c700d2472e3fb8377142418ab9e4f255bba5210053a3557fd` |

These are catalogue values for the entire decrypted image, not hashes of
individual extracted files. The encrypted entry is not an input accepted by
Forge. No ROM download is provided.

## Legacy EUR input contract

Forge accepts decrypted `.3ds` or `.cci` containers and verifies their
extracted contents against
[`supported_revisions.json`](../tools/triaevum_release/supported_revisions.json):

| Input | Bytes | SHA-256 |
| --- | --- | --- |
| Decompressed ExeFS `.code` | 4,567,040 | `16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220` |
| ExHeader | 2,048 | `9f065aec342b7df2421998deacedde44d475d36264f861cffd3381a1adf8900a` |
| RomFS | 479,260,672 | `a3ad05dcf12893de5f1617a60f4e80f54f5e1c80069922a96a4f9c8c8cbac8ca` |

Trimming or container-level changes can change the complete-image hash without
changing those inputs. Do not reject a trimmed dump solely because its file
size or whole-file hash differs; let Forge verify its contents.

## Qualification and limits

The locally tested input is a 483,672,064-byte decrypted, trimmed CCI, with
SHA-1 `618fdc30cdf8ced2996bef7cdf581a64ddc93419`. Its main NCCH header reports
`CTR-P-AQEP` / `0004000000033600`, and its ExHeader application
remaster-version field is 0, consistent with the original release.

It is **not a byte-identical match** for the complete No-Intro image above.
Virtually restoring either zero or FF padding to 512 MiB did not match the
catalogue SHA-256, so trimming alone does not establish the correspondence.
The later four-image audit now verifies complete catalogue-matching images.
Their EUR code and RomFS match this legacy input; their ExHeader requires the
additional exact recipe documented above. The legacy recipe remains available.

No acceptance rules were relaxed to infer compatibility from the filename,
region, product code or version field alone.

## USA input: experimental support in alpha.1b

The exact tested USA input is now supported through an installation-time
adapter. No EUR ROM is needed by a user installing from this USA input.
Product code: `CTR-P-AQEE`; Title ID: `0004000000033500`.

| Input | Bytes | SHA-256 |
| --- | --- | --- |
| Tested complete decrypted ROM | 536,870,912 | `42d2bd2313e2cdd8b1d7b56f8b2476419fcb0239f4bbc08960e39c688d7632a1` |
| Decompressed ExeFS `.code` | 4,567,040 | `ef210566e1d9d16879a746dfb063fcbad232f0171d860de906531ecc526cc020` |
| ExHeader | 2,048 | `dbe5fa0174d73bffb75d7cf0fbaa05d3e0ee080df0e257afc58b6c45ae5de3d0` |
| RomFS | 473,526,272 | `dd6def65af151d40fcbba7202c36bbdd8ed5b5b431373dc6a8c89bfd708af690` |

The tested filename was `Legend of Zelda, The - Ocarina of Time 3D (USA)
(En,Fr,Es) (Rev 1) Decrypted.3ds`. Its SHA-1 is
`cc9a07e4c741194ed5c4b55405527fbd069f5861`: it does not match the full decrypted
[No-Intro USA record 0033](https://datomatic.no-intro.org/index.php?page=show_record&s=64&n=0033)
or [USA Rev 1 record 1259](https://datomatic.no-intro.org/index.php?page=show_record&s=64&n=1259)
checked in the input audit. Do not infer a verified catalogue revision from
this filename. As with EUR, Forge uses the extracted hashes above to recognize
compatible containers, including equivalent trimmed `.3ds`/`.cci` images.

Forge derives a canonical EUR code image and compatible regional resource
index from the USA input, then activates the existing precompiled game DLL.
Models, textures, scenes, audio and dialogue payloads come from the supplied
USA ROM. This is **USA-input compatibility with EUR execution**, not a separate
USA recompilation. Regional behavior may differ; no German/Italian dialogue is
invented. See [adapter details and verification](TRIAEVUM_ROM_INPUT_ADAPTER.md).

Installation, title intro and file selection have been tested. Full-playthrough,
all hint/language paths and cross-region-save compatibility remain unqualified.
This is not blanket support for other regions, revisions or modified inputs.

The subsequent catalogue audit confirmed that both complete USA records 0033
and 1259 have exactly the extracted triplet above. Both were installed and
booted using the unchanged published alpha.1b package; no additional USA recipe
or adapter is required.
