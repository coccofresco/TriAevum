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

Implementation commit: `581b9f4`. The release-tool suite passed **193 tests**.
The metadata-only candidate passed the release allowlist audit (52 files),
reusing the published runtime, Forge and game DLL without rebuilding them.

Four fresh installations were exercised with the frozen Forge executable,
without a compiler on PATH. All four installed successfully, compiled zero
objects, and exited normally after bounded NRI/Vulkan intro runs. Framebuffer
captures at frame 600 were visually inspected for all four inputs; the EUR
original also reached the TopScreen file-selection menu (frame 1800).

| Input | Package tested | Forge installation time |
| --- | --- | --- |
| EUR original | New metadata-only candidate | 22.57 s |
| EUR Rev 1 | New metadata-only candidate | 12.35 s |
| USA original | Unchanged published alpha.1b | 25.74 s |
| USA Rev 1 | Unchanged published alpha.1b | 27.08 s |

Times are installer-reported on this PC, excluding extraction from 7z and
network download: the official TopScreen archive was already available locally.
They are not cross-machine guarantees. Candidate location:
`I:/oot3dre_work/catalog-rom-inputs/candidate-package`.

Private evidence lives under `I:/oot3dre_work/catalog-rom-inputs/`:
`audit.json`, `exheader-comparison.json`, `package-overlay/` and `proof/`.
Unit tests exercise exact recognition, rejection, unchanged legacy/USA behavior,
shared title/source bindings and idempotency. Frozen Forge and real NRI boot
qualification are recorded there separately; these are not full-playthrough
or clean-Windows certification. The existing published alpha.1b is not silently
replaced by this development change.

## Japanese, Korean and Taiwanese releases (not supported)

Four further user-provided images were inspected on 2026-10-13 and extracted
only into private work storage. v0.8.0-beta.1 deliberately supports EUR and
USA only; Forge declines these inputs at the `verify` stage ("does not match
any supported game revision"), within seconds and without leaving data behind.

| Release | Complete decrypted SHA-1 | Program ID |
| --- | --- | --- |
| Japan | `51c2d669ede49a1db3e7022aeafe7a8349f363de` | `0004000000033400` |
| Japan Rev 1 | `3ab17e24ad3ae7ad331327d7e423c7c5ead6a0f9` | `0004000000033400` |
| Korea | `6c2a6f5129220ff884fe31fe000058828489f281` | `000400000008F800` |
| Taiwan (Tw, Cn) | `6ad4ad1e6b3d5756f99960b55747ae80bc3cf720` | `000400000008F900` |

| Consumed input | Japan (both revisions) | Korea | Taiwan |
| --- | --- | --- | --- |
| Code SHA-256 | `0b53433a8fdc195c80d9a3335dc85059da2b2cdcf3357651ceb1b0359a4e81bb` | `d3c92811d47bee5fc8f9676c465bf33c563de337f892260cb94d454b851b5b8e` | `a81f237b97cbf2db8d891915699cf4c5d11dcbf1e937dc1c0d5aae564994f9d4` |
| Code bytes | 4,567,040 | 4,624,384 | 4,624,384 |
| ExHeader SHA-256 | `1236a85a23098f1212ff95aaf7bc62c207bc5c202a7e3817d481544c582ab4b7` | `dff03f63ea6f890b5c9b3a0bd6c23222654b002a075e04c0dcc47f1bf62aee90` | `71e6a02cb7781f3f8719b1c307b816bf0c1504393ae82160181e7b597c3f1208` |
| RomFS SHA-256 | `297ce95a28fc726584f263b354e22764ae9378a561c707a4ebc6ae82cf05efab` | `c9436f203e9208863cf7e171f1a4ac821737fb55f488e6d321c12cdc4be4ee9e` | `804f3944051fc0ee630eb303d1262335844f06c7ce33941b5df4e66681c72c4a` |

Why the USA method (canonical EUR code plus a RomFS language view) does not
extend directly:

- The EUR executable selects text by CFG region and language: EU region maps
  EN/FR/DE/IT/ES to QM slots 2/4/3/8/6, any other region to slot 2. Its
  slot-kind table (`0x4ed37c`) still names a JP kind (`message/jp/jp.qm`,
  `std16.qbf`, `ruby8.qbf`), but no EU language reaches it.
- EU text is UTF-8 with 8-bit `7f XX` control codes. JP, KR and CN text is
  UTF-16 with 16-bit control codes and arguments (including ruby/furigana
  codes `0x20..0x22`). Korea and Taiwan use QM v5 (120-byte records, 13 slots:
  JP 0, EN 2, CN 10, TW 11, KR 12) and the `han16`/`scc16`/`tcc16` fonts.
- A metadata-only view presenting the Japanese QM and fonts at the EU paths
  faults on boot in the EUR code (`kind=4 pc=0x002C5074`, null `[r1]` in the
  constructor at `0x2c4f00`); the QM alone is sufficient to trigger it.
  Selecting the JP kind for slot 2 at runtime faulted identically.
- Scene and actor assets are format-compatible (Japan changes 46 scene/actor
  files; Korea/Taiwan differ from EUR only in tool-name header bytes).

Candidate routes, none adopted: transcode UTF-16 text into the EU format at
install time (loses furigana and native name entry; requires the control-code
mapping and confirmation that the EU renderer draws CJK glyphs), enable the
native JP text path (a runtime guest-data change, and still no KR/CN path), or
separate per-region precompiled titles (a different method with per-region
adapter addresses).
