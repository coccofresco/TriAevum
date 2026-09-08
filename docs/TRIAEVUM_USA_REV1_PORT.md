# USA candidate revision port

Status: **intake and address-window audit complete; not playable or supported yet**.
Branch: `port/oot3d-usa-rev1`. Baseline public source: `aa4cfe5`.
Do not add this candidate to the supported release recipes or reuse the EUR DLL.

## Input identity

User-provided file:
`I:/zeldaroms/Legend of Zelda, The - Ocarina of Time 3D (USA) (En,Fr,Es) (Rev 1) Decrypted.3ds`.
Source was read only; private extraction is in `I:/oot3dre_work/usa-rev1-port/inputs`.

| Input | Bytes | SHA-256 |
| --- | --- | --- |
| ROM | 536,870,912 | `42d2bd2313e2cdd8b1d7b56f8b2476419fcb0239f4bbc08960e39c688d7632a1` |
| Decompressed code | 4,567,040 | `ef210566e1d9d16879a746dfb063fcbad232f0171d860de906531ecc526cc020` |
| ExHeader | 2,048 | `dbe5fa0174d73bffb75d7cf0fbaa05d3e0ee080df0e257afc58b6c45ae5de3d0` |
| RomFS | 473,526,272 | `dd6def65af151d40fcbba7202c36bbdd8ed5b5b431373dc6a8c89bfd708af690` |

Program ID is `0004000000033500`; text base is `0x00100000`, declared text
size 3,971,496 bytes, mapped text size 3,973,120 bytes.
The ExHeader remaster-version field is 0. Do not infer the catalogue revision
from that field or from the filename alone.

The ROM SHA-1 is `cc9a07e4c741194ed5c4b55405527fbd069f5861`. It differs from
both the full decrypted [No-Intro USA Rev 1 record 1259](https://datomatic.no-intro.org/index.php?page=show_record&s=64&n=1259)
(`50d6b33de7298d397fa9f0dde78f8b0f23256564`) and
[USA record 0033](https://datomatic.no-intro.org/index.php?page=show_record&s=64&n=0033)
(`71d872eecd859b68153edbf12c19edfcead22c18`), checked 2026-09-08.
That does not prove the game code is modified, but rules out claiming an exact
catalogue dump match. Port to the explicit extracted identity first.

## Findings

`tools/triaevum_release/revision_port_audit.py` compares aligned 32-byte windows
at the existing 12,419 selected function entries. It records ambiguous matches,
never infers a location from neighbouring matches, and does not rewrite code.

| Window result | Function entries |
| --- | ---: |
| Unique match at the same address | 10,010 |
| Unique match at another address | 973 |
| Repeated, ambiguous window | 961 |
| Changed or missing window | 475 |

Relocated unique matches split into 184 at -36 bytes and 789 at -32 bytes.
**These counts do not measure validated functions, semantic equivalence or
playability.** Branch targets, literal pools and bodies outside the window can
still differ. Do not turn these findings into a blanket address-range offset.

The lexical source audit finds 978 distinct text-range literals in
`native_game_runtime`: 161 relocated windows, 101 changed/missing, 18 ambiguous,
12 unsuitable for a complete text window, and 686 unique unchanged windows.
Some literals may be data or masks, not active hooks. `ui_topscreen` adds its
own audit: 43 addresses, including 9 relocated and 7 changed/missing windows.
Neither audit covers globals outside the text segment.

Examples of exact matching windows requiring call-site/body verification:

| Owner | EUR | Candidate |
| --- | --- | --- |
| PauseUi_Update | `0x0041E968` | `0x0041E944` |
| PauseUi_Draw | `0x0041EC50` | `0x0041EC2C` |
| PauseInput_UpdateTouchState | `0x004289DC` | `0x004289B8` |
| FileSelect_Update | `0x0042F9A8` | `0x0042F984` |
| FileChoose_Init | `0x00450B60` | `0x00450B40` |

The current `whole_aot_source_backend.py` loads the EUR inventory, supplemental
entries, boundary audit, selection and product contract. Their identities are
coupled to EUR. Generating with the USA bytes but those unchanged contracts is
not a port, even when an entry prefix happens to match.

## Implementation order

1. Build a candidate-specific function/CFG inventory from its code, using these
   window matches only as labels and discovery seeds. Resolve the ambiguous and
   changed entries with control flow, call references and literal/data evidence.
   Preserve complete coverage and the existing no-interpreter product contract.
2. Make frontend inventory, supplemental entries, boundary evidence, selection
   and product manifest a revision-owned bundle. Include its identity in the
   existing build cache. Keep the EUR bundle untouched.
3. Move active host hook addresses and guest global locations into a revision
   profile owned by the title adapter. Verify each patch's expected instruction
   and resume address. Start with boot/services and native top-screen rendering;
   then port timing, interpolation and TopScreen consumers. Keep NRI revision-neutral.
4. Compile the candidate DLL with the existing LLVM/support-library pipeline,
   using an isolated build/cache and bounded worker count. Do not invalidate
   the working EUR build or install development tools on end-user machines.
5. Qualify boot, title intro, file selection, gameplay, pause and controls with
   fresh candidate-owned saves and framebuffer capture. Do not load EUR memory
   savestates: guest PCs and pointers may differ. Check region/language routing,
   audio, asset resolution and TopScreen texture compatibility.
6. Only then publish a second exact recipe and precompiled title entry, with
   corresponding source and offline Forge installation proof. Keep both revisions
   selectable automatically by extracted identity; do not replace EUR support.

## Repeatable audit

Run from the source checkout; use the actual private input paths:

```powershell
python -m tools.triaevum_release.revision_port_audit `
  --reference-code <EUR-code.bin> --reference-exheader <EUR-exheader.bin> `
  --target-code I:/oot3dre_work/usa-rev1-port/inputs/code.bin `
  --target-exheader I:/oot3dre_work/usa-rev1-port/inputs/exheader.bin `
  --selection tools/oot3d/native_a32_runtime/whole_aot_functions.json `
  --source-root tools/oot3d/native_game_runtime `
  --output I:/oot3dre_work/usa-rev1-port/address-audit.json
```

Repeat with `--source-root tools/oot3d/ui_topscreen` for the separate UI module.
Reports contain hashes, addresses and source references, not game bytes.
Seven synthetic tests cover matching, relocation, ambiguity, bounds and
changed windows. No candidate runtime build or boot has been claimed.
