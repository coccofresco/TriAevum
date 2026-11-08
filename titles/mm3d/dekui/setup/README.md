# Local MM language/UI preparation

## Native Forge path

`forge-assets::mm3d_ui::prepare` is called by the normal native install/apply
worker for the exact supported MM code identity. It needs no Python or compiler
at installation time. The Enhancements page explains the local input folder:

```text
D/imports/mm3d-restoration/romfs/layout/...
```

Supply original Project Restoration resources there. The six layout inputs and
outputs are checked against `setup-fingerprints.json`; nothing is downloaded or
guessed. Unsupported code identities leave other titles' preparation unchanged.
This stage does **not** manufacture an approved MM executable/module recipe:
the normal verified MM product recipe/catalogue is still a separate prerequisite.

For MM EUR code
`6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d`, the worker:

1. Performs the native GAR/MFL transformations, including the new
   `layout/Joker.Main.New.gar` resource.
2. Validates English/German/French/Spanish/Italian GMSG tables and language masks
   `0x10/20/40/80/100`. Dutch's empty native archives are not advertised.
3. Compiles `reviewed-dialogue.json`, using our own wording, including DE/IT;
   no dekui-pal translations or extracted dialogue corpus are distributed.
   Locale-specific native page/control boundaries are retained.
4. Applies the numeric ownership contract in `price-plan.json` to local source
   text. Only currency literals in explicitly owned messages are changed;
   native prose, reward numbers and controls are retained. Native 10-bit price
   metadata is guarded, with sentinel/overflow checks. Known dynamic-only rows
   are explicit; an unexpected unmatched literal aborts preparation.
5. Rebuilds allocations where necessary, but publishes **original** text and
   metadata as the initial archive state. Equal-size guarded before/after rows
   leave application, idempotence, independent toggles and restoration to the
   existing native Restoration owner. Dialogue/price ownership cannot overlap.
6. Prepares MM-native HD font coverage as described below.
7. Publishes an immutable generation under `D/prepared/mm3d-ui`, then atomically
   switches `current.json`. Inputs, generated content and generator identity
   participate in cache identity. Reuse verifies all eleven files, the receipt,
   messages and font pack. Unexpected files/directories or symlinks are rejected.
   Corruption creates a new generation; invalid input/cancellation preserves the
   active pointer. The caller serializes jobs. Old generations are retained.

The generation contains `romfs/`, `romfs.receipt.json`, `message_edits.json` and
`font_coverage.zip`. The receipt is **beside**, not inside, the mounted layer.
The supported launcher handoff is an argv pair:

```text
--title-romfs-layer <generation>/romfs
--title-message-edits <generation>/message_edits.json
```

Publication and installed-runtime preflight pin/check both assets and their
routing. The runtime composes optional O3RO user mods **under** required title UI
through a read-only view, then mounts exactly one RomFS service view. Additions
are supported. An inherited developer RomFS-layer variable cannot override or
combine with this installed path. An explicit message argument wins over the
old developer-only message environment fallback. Other titles cannot mount MM UI.

An optional complete reviewed specification can be supplied at
`D/imports/mm3d-message-specs.json`; it must cover all five locales. Failures do
not silently substitute English or publish incomplete coverage.

All extracted bytes, text, metadata, rebuilt archives, font coverage and
receipts stay private to the user's data directory. **Never commit or distribute
these generated files.** The original ROM/image is never modified. Savestates
from older, differently allocated developer message archives are not migration
proof; runtime guards fail closed rather than modifying a mismatched message.

### Native MM font proof

MM uses `message/ltn16.gzf` (**GZFX**, Morton-tiled A4 sheets), not OoT's QBF blit
ABI. Forge uses the verified bundled HD QBF only as glyph *coverage artwork*.
It keeps MM's own codepoints, advances, bearings, cells, baselines, UVs and native
ink bounds. Unavailable or ambiguous glyph aliases retain native coverage.
The EUR source has 468 character records; 196 cells receive HD artwork.

The shared texture resolver accepts a separate static-coverage format. It binds
replacement sheets to exact complete native source bytes, format and dimensions,
uses immutable 4x coverage at output heights >=480, and leaves native memory and
metrics untouched. It requires **no OoT PC**, blit observer or guest-pointer
cache. Changed/foreign/mipped sheets and native-resolution rendering remain
native. State restore cannot transfer coverage identity to different bytes.
OoT's existing QBF observation path remains unchanged and title-gated.

### Offline acceptance

```text
cargo test --offline -p forge-assets -p forge-core -p forge-native --lib
cargo check --offline -p forge-desktop
cargo run --offline -p forge-assets --example mm3d_ui_prepare -- \
  <package-root> <data-root> <romfs.bin> <code.bin>
```

Run these from `tools/triaevum_forge`. The ignored
`mm3d_ui::tests::local_atomic_preparation_contract` additionally requires explicit
`TRIAEVUM_MM_TEST_PACKAGE`, `TRIAEVUM_MM_TEST_ROMFS` and
`TRIAEVUM_MM_TEST_SOURCE` (the folder containing `romfs/`). It checks local cache
reuse, corruption repair, extra-file rejection and failure/cancellation rollback.
No ROM fixture is embedded.

Native counterparts:

- `oot3d_game_language_tests [romfs.bin]`: populated languages, restart-only
  selection, unsupported languages, malformed preferences and save errors.
- `oot3d_mm3d_restoration_tests [code.bin] [message_edits.json]`: all 795 prepared
  rows, apply/restore/idempotence, independent toggles and language guards.
- `oot3d_native_a32_romfs_layers_tests`: O3RO/layer composition; optional actual
  image/layer through `OOT3D_ROMFS_OVERLAY_IMAGE` / `TRIAEVUM_TEST_TITLE_LAYER`.
- `oot3d_native_hd_font_runtime_tests [font_coverage.zip] [ltn16.gzf]`: unchanged
  OoT behavior and all three native MM sheets, thresholds and negative guards.

## Python preparation oracle (legacy, not the installed job)

`prepare_install.py` remains a stricter, fixed-allocation development oracle:

```text
python -B titles/mm3d/dekui/setup/prepare_install.py \
  --romfs <romfs.bin> --restoration <project-restoration> --code <code.bin> \
  --code-sha256 6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d \
  --output <private-output> [--specs <reviewed-locale-specs.json>]
python -B -m unittest discover -s titles/mm3d/dekui/setup -p test_prepare_install.py -v
```

Unlike the native worker, no `--specs` means native dialogue/prices are retained
and reported `not-prepared`. Historical `single-screen.json` is US wording and
must not be silently applied to EUR. The oracle emits O3RO plus message arguments;
it does not supply native Forge HD coverage or the expanded-allocation workflow.

## Qualification boundary

The native preparation/worker/UI/handoff, complete five-locale numeric plan and
MM font path are implemented. Local kernel/asset tests and individual safe
locale captures are evidence, not blanket FULL F13 closure. Approved MM recipe
activation, complete controller-glyph/device variants, live shop/toggle coverage
and remaining visual routes still need their acceptance gates. Generation GC
and older-developer-state migration are not implemented here.
