# Local MM language/UI preparation

`prepare_install.py` is a local preparation oracle and launcher contract, **not
an integrated native Forge recipe**. All extracted text, original bytes, hashes,
modified archives and overlays stay in the user's private output directory.
Do not add its generated files to Git or distribute them.

```text
python -B titles/mm3d/dekui/setup/prepare_install.py \
  --romfs <local-romfs.bin> --restoration <local-project-restoration> \
  --code <local-code.bin> \
  --code-sha256 6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d \
  --output <private-prepared-directory> [--specs <reviewed-locale-specs.json>]
```

The original image is never changed. The oracle:

1. Pins MM EUR v1.1 code identity and fingerprints its inputs/generator.
2. Validates native GMSG tables for English, German, French, Spanish and Italian;
   verifies language masks 0x10/20/40/80/100, records coverage per locale.
3. Runs the existing fingerprint-guarded local Restoration/layout transforms.
4. Builds O3RO replacements **and additions** in existing RomFS directories.
   Restoration's `layout/Joker.Main.New.gar` does not exist in the native image;
   a replacement-only overlay is insufficient. File metadata is relocated,
   parent/file-hash heads are updated, and old offsets/chains stay valid.
5. Publishes an immutable generation and atomically replaces `prepared.json`.
   Hash-valid repeated inputs reuse the generation. Corruption creates a fresh
   generation; failure/cancellation leaves the previous pointer untouched.
   The caller must serialize preparation for a given output directory. Old
   generations are retained so an active game is never modified underneath.
6. Returns an argument **array**, not a shell command:
   `--romfs-overlay <file> --title-message-edits <file>`.

`--title-message-edits` is the normal runtime handoff for MM. The old environment
variable is only a developer-build fallback; an explicit launcher argument wins.
A malformed/oversized file fails title setup before editing guest text. Price edits carry original **and** edited metadata, both required;
text/hash/metadata guards and independent price/single-screen toggles remain in
the Restoration owner. OoT receives no message-edit behavior.

## Reviewed text is explicit, never guessed

Without `--specs`, native dialogue/prices are retained and every locale reports
`not-prepared`; layout preparation can still succeed. The existing historical
`single-screen.json` is US wording: its save message does not match this EUR
image. It must not be silently applied to German/Italian or called complete.

A specs file is an object keyed by `en`, `de`, `fr`, `es`, `it`. Each dialogue
spec has `language_masks` and `edits` in the existing single-screen schema.
The original is decoded locally, replacements must match and fit their native
allocation, and any skipped row aborts publication. DE/IT wording must be our
own reviewed writing, never dekui-pal's AI translations. Native prompt strings
already live in `../prompt_text.h`; this tool does not replace font rendering.

Price rules use `language_mask` and a `prices` array. Each row supplies a hex
message `id`, exact `original_sha256`, `original_metadata`, optional
`replacements` (`old`/`new`, each matching exactly once), and `double_fields`
(selected 10-bit native fields at bit 0/10/20). Sentinel 1023 is preserved,
overflow is rejected, controls cannot be introduced, and text cannot grow past
its allocation. Native reward numbers are never heuristically doubled. Review
these local rules against the user's source; no ROM-derived price corpus is
shipped here. A row cannot be owned simultaneously by dialogue and price rules;
that requires a future explicit combined-variant contract.

The codec preserves plural/extended controls and rejects truncation. Runtime
loading enforces integer ranges, paired metadata, exact 16-digit hashes, equal
byte extents and table/aggregate limits. Existing runtime guards handle actual
application, idempotence and restoration when a toggle is disabled.

## Tests and omissions

```text
python -B -m unittest discover -s titles/mm3d/dekui/setup -p test_prepare_install.py -v
```

Fixtures are synthetic: five language masks, strict GMSG bounds, control
round-trips, text allocation, price text/metadata, O3RO addition/replacement,
cache reuse/corruption and atomic failure. Runtime counterparts are
`oot3d_mm3d_restoration_tests` and `oot3d_mm3d_dekui_language_tests`.

Still open: native Forge recipe/UI/job wiring, complete reviewed dialogue and
price specifications for all five locales, HD font preparation and visual
prompt/font acceptance, migration/garbage collection of old generations, and
integration with other independently generated O3RO overlays (one launcher
argument owns one overlay). This work does not claim FULL F13 closure.
