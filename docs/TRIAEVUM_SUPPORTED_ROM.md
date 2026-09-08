# Supported ROM identification

## Catalogue reference

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

## What the release actually verifies

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
No complete catalogue-matching image has been tested here. The catalogue
reference identifies the intended edition; the extracted-input checks remain
the authoritative statement of this alpha's tested compatibility.

No acceptance rules were relaxed to infer compatibility from the filename,
region, product code or version field alone.
