# N64 Porting Workflow

This workflow turns the local zeldaret/oot checkout into a repeatable triage
source for OOT3D symbol recovery.

## Rank Candidates

Global queue:

```powershell
python scripts\rank_n64_port_candidates.py --max-oot3d 100 --top-n64-per-oot3d 6
```

Focused pause/item/message queue:

```powershell
python scripts\rank_n64_port_candidates.py --domain "pause|item|inventory|equip|slot|message" --include-small-constants --max-oot3d 80 --top-n64-per-oot3d 6 --out-json analysis\n64_port_candidate_rank_pause_item.json --out-md analysis\n64_port_candidate_rank_pause_item.md --out-csv analysis\n64_port_candidate_rank_pause_item.csv
```

Use the focused queue when small enum values are meaningful. Keep the global
queue's default small-constant filter on, otherwise offset/case sequences drown
out semantic evidence.

## Batch File-Level Acceleration

The fastest default path is now file/subsystem batching, not isolated
function-by-function promotion:

```powershell
.\scripts\refresh-n64-porting-plan.ps1
```

This is the no-Ghidra inner loop. It regenerates the manual-symbol overlay
report, global/focused rank queues, the batch plan, and the top batch packets:

- `analysis/symbol_overlay.md`: whether manual symbols are already applied in
  the current Ghidra export or only active through Python overlay.
- `analysis/n64_port_candidate_rank.md`: global function candidate queue.
- `analysis/n64_port_candidate_rank_pause_item.md`: focused queue for
  pause/item/message work.
- `analysis/n64_batch_porting_plan.md`: ranked N64 source-file lanes.
- `analysis/n64_batch_porting_plan.csv`: sortable lane summary.
- `analysis/n64_batch_porting_plan.json`: machine-readable work queue.
- `analysis/n64_importability.md`: exact-oriented classification of top N64
  hits into direct import tests, large direct ports, split/subsystem rows, and
  semantic-only rows.
- `analysis/n64_importability.csv`: sortable importability queue; start here
  when the goal is matched C rather than broad semantic naming.
- `analysis/port_batches/`: related OOT3D decompiles plus the local
  `..\external\oot` N64 source path or `n64_source.c` copy for the top lanes.
- `analysis/n64_lane_sources.md`: deduplicated N64 function extracts for the
  top ranked lanes.
- `analysis/n64_lane_sources/`: one directory per lane with unique N64
  function bodies and a mapping back to all selected OOT3D candidates.

Add `-CopyN64Source` to `refresh-n64-porting-plan.ps1`, or
`--copy-n64-source` to `plan_n64_batch_porting.py`, when an offline packet
should contain a local `n64_source.c` copy. The default keeps large zeldaret/oot
files referenced from the external checkout instead of duplicating them inside
this repository.

Tune source extraction with `-LaneSources` and `-LaneSourceMaxHits`:

```powershell
.\scripts\refresh-n64-porting-plan.ps1 -LaneSources 12 -LaneSourceMaxHits 32
```

Use `-SkipLaneSources` only when regenerating rank reports and packets without
refreshing the N64 source extract cache.

Use lane actions this way:

- `batch-promote-and-port`: review the lane, promote all non-fan-out and
  non-conflicting `high`/`medium` rows that pass control-flow/data checks,
  refresh the plan without Ghidra, then port matching seeds.
- `matching-seed-batch`: build a small maintained source file for the lane and
  add only `match-seed` functions to the runtime build after they compile.
- `subsystem-review-batch`: port structs, enums, state fields, macros, and
  helper names from the referenced N64 source before direct symbol promotion.
- `single-lane-review`: use the old per-function packet flow only if the row is
  still useful after the higher-ranked lanes have been mined.

Use importability buckets this way:

- `direct-import-test`: first queue for producing matched C from N64. Extract
  the focused packet, port the N64 body into an isolated structured source, and
  compile/compare before promoting it into the exact baseline.
- `large-direct-port`: likely real port shape, but too large for a quick exact
  test. Mine it for struct offsets and helper prototypes before attempting a
  full source import.
- `split-or-subsystem`: not a direct import. One N64 function or subsystem
  explains multiple OOT3D helpers, so recover shared state/roles first and only
  then write per-helper source.
- `semantic-name-port`: useful for names, constants, and enum/table mapping,
  but not enough evidence for an immediate matched C import.
- `already-started`: already tracked in `metadata/n64_port_map.csv`; use the
  structured unit status reports instead of re-triaging it.

Rows flagged `fanout-review` are intentionally not direct rename candidates:
they mean one N64 function currently explains too many OOT3D functions. Treat
those rows as strong evidence for shared subsystem reconstruction, then split
and name the OOT3D functions manually after reviewing their local control flow.

Rows flagged `name-conflict-review` are also intentionally not direct rename
candidates yet: several OOT3D functions would receive the same proposed
N64-derived name. Resolve their split/helper identity inside the lane, assign
unique reviewed names, then promote them with the batch promotion tool.

The intended fast loop is:

1. Run `.\scripts\refresh-n64-porting-plan.ps1`.
2. For matched-source work, take the first `direct-import-test` row from
   `analysis/n64_importability.md`.
3. Promote confirmed names into `symbols/manual_symbols.csv`.
4. Run `.\scripts\refresh-n64-porting-plan.ps1`.
5. Work the highest lane that has either non-fan-out promotion rows or useful
   shared structures.
6. Open `analysis/n64_lane_sources/<lane>/README.md` to review one N64 source
   function once, mapped to all selected OOT3D candidates in that lane.
7. Promote confirmed names in one batch and refresh again without Ghidra.
8. Port lane-level structs/macros into `include/oot3d/` or a maintained source
   file.
9. Add only small, compilable matching seeds to `src/`, then run the runtime
   object comparison.
10. Quarantine near misses outside the build until they are exact or clearly
   understood.

## Ghidra Checkpoints

Do not run a full Ghidra export after every promotion. Use Python overlay for
the inner loop, then checkpoint Ghidra only when refreshed pseudocode or local
call neighborhoods are needed.

Selective checkpoint for specific entries:

```powershell
.\scripts\ghidra-export-selected.ps1 -Entries 002ef9b4,0042d0e4 -ApplyManualSymbols -IncludeCallers -IncludeCallees
```

Selective checkpoint for manual symbols that are pending in
`analysis/symbol_overlay.md`:

```powershell
.\scripts\ghidra-export-selected.ps1 -PendingManualSymbols -ApplyManualSymbols -IncludeCallers -IncludeCallees
```

Use the old full export only after a larger milestone or after broad type work:

```powershell
.\scripts\ghidra-apply-manual-symbols.ps1 -Reexport
```

## Prepare A Review Packet

```powershell
python scripts\prepare_n64_port_packet.py --entry 00424324 --rank-json analysis\n64_port_candidate_rank_pause_item.json --top 5
```

The packet contains:

- `oot3d.c`: the current Ghidra pseudocode with manual-symbol overlay applied
  by default.
- `NN_N64Function.c`: extracted N64 candidate bodies.
- `README.md`: evidence summary and a promotion-command template.

## Promote After Review

After confirming a candidate by control flow and data meaning:

1. Add the new prototype to `src/semantic_labels.c`.
2. Promote the symbol with `scripts/promote_manual_symbol.py`.
3. Run `.\scripts\refresh-n64-porting-plan.ps1`.
4. Use selective Ghidra export only when refreshed pseudocode/call context is
   needed.
5. Validate manual/data symbols and runtime objects.

For more than one reviewed symbol, use a CSV decision file with
`entry,new_name,source_file,confidence,notes` and run:

```powershell
python scripts\promote_manual_symbols_batch.py --decisions analysis\reviewed_promotions.csv
```

The batch command validates all rows first and rejects duplicate entries or
duplicate symbol names before writing `symbols/manual_symbols.csv`.

The ranker improves immediately after each manual-symbol promotion because the
Python overlay rewrites known `FUN_*` calls into manual names before Ghidra is
rerun.

## Direct Shape Anchors

Before trying to make a structured N64 port match, check whether the target
assembly already tells us the C shape it wants:

```powershell
.\scripts\refresh-direct-conversion-analysis.ps1 -SkipExtractN64PortUnits
```

The refresh now writes:

- `analysis/direct_shape_anchor_windows.md`: target-derived base windows such
  as `play + 0x28a0`, grouped by lane and domain.
- `analysis/direct_shape_anchor_windows.csv`: sortable anchor queue with source
  rewrite counts and candidate declarations.
- `analysis/direct_shape_anchor_families.csv`: recurring conversion structures
  such as message PlayState windows, Player high banks, and BossVa instance
  banks.

Use this report before compiler-profile tuning:

1. Pick a lane from `analysis/direct_conversion_lane_blueprints.csv`.
2. Open the matching rows in `analysis/direct_shape_anchor_windows.md`.
3. Introduce the target-derived base pointer in the converted C.
4. Rewrite absolute offsets inside the window to `anchor + local_offset`.
5. Run `scripts/structured_c_match_gate.py` and keep the edit only if the
   target comparison improves.

For example, `003438a4 oot3d_message_textbox_common` is not just a
`Message_Update` semantic port. The target assembly builds a `play + 0x28a0`
base and then stores at local offsets such as `0x301`, `0x300`, `0x2e2`, and
`0x1e0`. The direct converter should emit that base shape instead of leaving
all accesses as absolute `play + 0x2bxx` expressions.

## Convertible Port Queue

When focusing only on files that are already convertible or closest to
promotion, refresh the promotion queue before editing:

```powershell
.\scripts\refresh-convertible-port-audit.ps1
```

For faster follow-up after a known build, use:

```powershell
.\scripts\refresh-convertible-port-audit.ps1 -SkipBuild
```

This writes:

- `analysis/convertible_ports.md`: exact C already promoted, exact C still
  ready to promote, and the next shape-convertible functions.
- `analysis/direct_shape_anchor_apply_plan.md`: target-observed source rewrites
  for only the `shape-convertible` queue by default.

Apply the queue mechanically only after reviewing that plan:

```powershell
python .\scripts\apply_direct_shape_anchors.py --apply
```

The applicator is intentionally conservative: without `--all`, it ignores
semantic-gap rows even if they have useful target anchors. Use `--entry` for a
single function or `--all` for broader batch experiments, then keep only edits
that compile and do not regress the structured gate.
