# CIA input audit

Read-only inspection on 2026-09-08 of the two user-supplied CIA files.
These inputs are **not qualified or supported by Forge**. No recipe or
acceptance rule was added from filenames or title metadata alone.

| Input filename suffix | Title ID | Bytes | Complete SHA-256 |
| --- | --- | --- | --- |
| `(U).legit.cia` | `0004000000033500` | 477205504 | `29f2094e1c02d903ad915b44ef705f011219f96c7f796ea90b7dd01a8cd14a09` |
| `(E).piratelegit.cia` | `0004000000033600` | 483685376 | `cbc2a672c8c11e76c225b582897590095a1edca7035b041d5e9bff2d5093811d` |

Both have title version 16 (`v0.1.0`), boot content index 0, and two included
contents. Every content has TMD type flag `0x0001` (encrypted). The application
content does not expose a plaintext NCCH header. Section offsets and content
extents were checked against the file sizes. Ticket signatures and legitimacy
claims in filenames were not authenticated.

The layout and encryption flag interpretation follow the primary Azahar
[CIA container](https://github.com/azahar-emu/azahar/blob/master/src/core/file_sys/cia_container.cpp)
and [TMD definitions](https://github.com/azahar-emu/azahar/blob/master/src/core/file_sys/title_metadata.h).

## Consequence and next step

Forge currently accepts already-decrypted `.3ds`/`.cci` inputs and contains no
key acquisition or decryption path. Renaming a CIA cannot make it compatible.
The four previously audited cartridge images do not prove the contents of
these encrypted packages identical. Version 16 is not by itself a mapping to
a cartridge catalogue revision.

Obtain decrypted copies from the user's own dump, then compare decompressed
code, ExHeader and RomFS with the existing recipes. Reuse a matching recipe;
add an exact variant only if the decoded differences justify it. A decrypted
CIA additionally needs a bounded, tested CIA container reader in Forge; a
decrypted `.3ds`/`.cci` can use its existing reader. Do not relax hash checking
or add a duplicate regional port to bypass this prerequisite.

No game launch was attempted: encrypted contents cannot supply the required
inputs. Private reproducible audit script and JSON are under
`I:/oot3dre_work/catalog-rom-inputs/inspect_cia.py` and `cia-audit.json`.
No ROM, ticket, key or content payload is included in this document.
