# AOT structural surface research

Status: research only, 2026-09-16. No runtime optimization promoted and no new
game-level speedup measured. Continues `TRIAEVUM_PRE_NRI_20_PERCENT_DECISION.md`.

Follow-up 2026-09-17: isolated real-input replay is now implemented and checked
against unchanged game execution; see `TRIAEVUM_AOT_REAL_INVOCATION_REPLAY.md`.
The earlier absence of that harness below describes the preceding research step.
No new optimization or 20% speedup has been established.

## Question and evidence

Can one general translation change remove at least 20% of pre-NRI time without
changing game execution? The earlier ledger places AOT around 58-60% of pre-NRI
time and bounds off-CPU time below 3.2%. Removing waits cannot meet the target.

This investigation reuses three existing intrusive stack captures (2,214 AOT
leaf observations), the structural AOT program and matching original code.
It does not rerun the game, disable audio, change clocks or compile a new title.
The sampled module is the earlier CMP-fused diagnostic module, NOT the frozen
performance baseline. Shares below select candidates; they are neither exclusive
CPU timings nor measured removable costs.

## Concrete generator limitations

`tools/oot3d/native_a32_runtime/whole_aot_cpp.py`, `_render_function`, normally
emits GPR references to `state.R[]` and a reference to shared lazy flags. This
avoids full-state copies across calls, but is not independent scalar state
across function boundaries. References alone do not prove generated machine code
spills: compiler output must establish the actual cost.

The experimental `memory_region` path uses local GPRs, flags and block budgets,
plus deferred memory-generation updates. Its eligibility excludes direct calls.
Simply replacing references with values in the normal path is incorrect:
`_emit_terminal` passes shared state to callees and relies on commit/reload
contracts, including the return address and nonlocal exits.

`whole_aot_memory_regions.py::region_memory_safe` also conservatively rejects all
coprocessor register transfers. This includes CP10/11 VFP transport, not just
system-register operations. Treating VFP transport separately expands the
candidate surface. **No runtime eligibility check was relaxed.** FPSCR effects,
instruction support and exception behavior still require qualification.

## Structural coverage

The new offline tool computes a least fixed point of closed, nonrecursive direct
calls. It rejects indirect control, missing/external callees, unaccounted edges,
open CFGs and memory/system observers. It does not certify callbacks, memory
aliasing, fault ordering or scheduler-budget behavior.

| Structural filter | Static functions | Sampled functions | AOT observations |
| --- | ---: | ---: | ---: |
| Existing exclusions, call-free | 2,490 | 97 | 510 / 2,214 (23.0%) |
| Existing exclusions, closed direct calls | 3,347 | 144 | 619 / 2,214 (28.0%) |
| Separate VFP transport, call-free | 2,798 | 149 | 745 / 2,214 (33.6%) |
| Separate VFP transport, closed direct calls | 4,536 | 258 | 1,187 / 2,214 (53.6%) |

The last cohort covers 54.1%, 55.1% and 51.8% in the individual captures.
Its observations include reads (291), block-entry helpers (185), floating-point
helpers (224), writes (52), translated bodies including inlined helpers (434),
and one other sample. These are useful operations as well as translation costs;
none of these categories can be assumed removable in full.

Caller rankings count only observed paths containing that caller. Shared callee
samples reached elsewhere are not credited to it. The greedy top-16 root list
deduplicates paths and covers 684 observations (30.9%), including already-tested
audio routines. Even treating those shares as time and eliminating that entire
cohort for free would suggest only about 18% overall. This is a screening
calculation, not a rigorous bound from an unbiased CPU profile.

The widest cohort would require approximately 62-64% less execution cost within
it to reach 20% overall, assuming its observed share represents CPU time. There
is no measurement demonstrating a reduction of that size. A shortlist of small
leaf rewrites does not provide the requested opportunity.

## Decision and next useful experiment

Do not expand the unsuccessful three-function memory pilot or launch a complete
AOT rebuild on the strength of this coverage. Do not claim memory latency is
the bottleneck merely because inlining and caches previously failed to help.

The remaining structural hypothesis is region-wide state and memory lowering
across direct calls, including VFP transport. Before implementation, inspect
optimized machine code for representative groups and quantify genuinely
avoidable work with a small replay of real invocations. Shared-state accesses
may already be optimized; normal guest loads and arithmetic are not overhead.
If this cannot support the required whole-window budget, stop this hypothesis.

A positive local result would still require an unchanged-game, order-controlled
A/B test against the frozen baseline, without sampling instrumentation. Preserve
memory/process/audio fingerprints, draw/transfer counts and visual behavior.
Only repeatable pre-NRI wall-time improvement counts as delivered performance.

## Reproduction

### Existing machine-code inspection

Inspected the existing optimized diagnostic DLL using LLVM objdump, without
recompilation. `audit_aot_machine_code.py` records its hash, link-map hash,
bounded symbol spans and static call-site categories. This independently checks
that helper boundaries survive compilation; it does not infer timing from C++.

| Guest entry | ARM instructions | x64 instructions in symbol span | Calls: memory read/write | Calls: block entry / FP / guest |
| --- | ---: | ---: | ---: | ---: |
| `002BBF74` | 123 | 1,843 | 44 / 14 | 17 / 14 / 2 |
| `0033B11C` | 42 | 928 | 20 / 4 | 0 / 16 / 0 |
| `003FAD68` material state | 115 | 1,758 | 38 / 9 | 24 / 3 / 12 |
| `00324758` collision line test | 466 | 8,266 | 154 / 46 | 64 / 37 / 12 |

These are static instruction and call sites, including cold/error paths, not
instructions executed per invocation. ARM multi-register operations, exception
handling, entry dispatch and guest memory validation expand into multiple host
instructions. A large ratio alone does not demonstrate an equivalent speedup.
Some helper bodies are also outside the measured span.

At `002BBF74`, existing assembly uses `rbx` for architectural state and reads and
writes it around helper calls. Its first guest stack store includes page lookup,
page-boundary checks and a write-generation increment; subsequent stores call a
write helper. Consequently both inline and out-of-line costs exist. Merely
inlining helpers cannot eliminate validation or shared-state visibility costs.
The actual host registers retain some temporary values already: the observation
does not justify claiming that every guest register access is a spill.

The 42-instruction routine has no guest callees and no out-of-line block-entry
call but retains 16 FP helper calls. Thus cross-guest-call region lowering alone
does not address all of this cohort. Conversely the collision and material-state
routines retain substantial block-entry and memory-helper boundaries.

**Decision remains unchanged:** there is machine-code evidence of expansion,
but no measured removable budget sufficient for the 20% objective. Do not
substitute these static counts for real-invocation replay or unchanged-game A/B.
There is no ready-made function-capsule replay in the inspected TEV tooling;
building that capture path is additional work, not a completed validation.

Private machine audit: `C:/Users/xander/triaevum-aot-causal-analysis/machine-code-audit.json`.
Module/map: `I:/TriAevum-aot-lab-debug/plugin/`. Run the audit with `--module`,
`--map`, `--objdump`, `--output` and repeated `--entry` for the addresses above.
Two tests cover static parsing/classification and empty output. The existing
sample provenance establishes module/map pairing; the audit itself does not
validate PDB identity. This is still the diagnostic module, not a new baseline.

### Dynamic leaf-path follow-up (2026-09-17)

Reclassified the instruction pointers in the same three real-game captures
against bounded disassembly ranges. This is more informative than static call
counts, but remains intrusive sampling, not a replay timing or hardware profile.

| Observed part | Observations |
| --- | ---: |
| Read32 entry/argument checks | 129 |
| Read32 page lookup | 94 |
| Read32 load/result/trace gate | 239 |
| Read32 trace body / fallback | 0 / 0 |
| Read32 return | 39 |
| Block budget and initial gates | 104 |
| Block bloom test | 62 |
| Block exact lookup | 98 |
| Block state flush/callback/reload | 3 |
| Block return | 36 |

Read32 totals 501 observations; block-entry totals 303, out of 2,214 AOT leaves.
Zero trace/fallback observations hold in all three captures. This does not prove
those paths never execute, but does not support treating them as the main cost.
Most block-entry observations are filtering/accounting, not actual callbacks.
The state-flush range excludes time spent inside the callback itself.

The common read-load/result range contains 239 observations, including 168 at
the result-store instruction and 58 immediately afterward. Instruction-pointer
sampling cannot distinguish pending load latency, retirement stalls, output
aliasing or sampling bias here. **Do not label this a proven memory-latency or
store-forwarding bottleneck.** Such a diagnosis requires hardware counters or a
controlled equivalent replay, not instruction counts.

Together the two complete helpers cover 36.3% of AOT observations. If that share
approximated CPU cost and AOT remained 58-60% of pre-NRI time, achieving 20%
overall from these helpers alone would require removing approximately 92-95%
of their cost. They include necessary loads, return paths and scheduler work.
This does not justify a new narrow inlining, page-cache or hook-lookup pilot.
It also does not bound unobserved inline copies or the effects of a broader
translation/ABI change.

The planned real-invocation replay has NOT been implemented or measured. Memory
copy/CaptureState primitives exist, but a faithful invocation capsule must also
preserve architectural and context state, callback boundaries and scheduler
exits. Building that harness is not a demonstrated optimization. Under the
current 20% evidence requirement, no production change or expensive build is
justified by this follow-up. The original baseline remains untouched.

Tool: `analyze_aot_leaf_ranges.py`; three tests validate exclusive leaf-only
counting with relocated module bases, overlap rejection and missing modules.
Range definitions are binary-hash-bound and private at
`C:/Users/xander/triaevum-aot-causal-analysis/helper-leaf-ranges.json`;
results are `helper-leaf-observations.json` in the same directory. Pass
`--module`, `--ranges`, repeated `--capture`, and `--output`. Input hashes and
per-run counts are recorded; capture-to-binary identity depends on provenance.

### Structural tool reproduction

Tool: `tools/renderer/tev_program/analyze_aot_region_surface.py`.
Inputs are private evidence, not distributable project assets:

- Program and manifest: `I:/TriAevum-aot-lab-evidence/baseline/`, files
  `aot_program.json` and `whole_aot_cpp_manifest.json`.
- Code: `E:/ppssppvr/oot3d_decomp/work/extract/exefs/code.bin`.
- Analyses under `C:/Users/xander/triaevum-aot-causal-analysis/`:
  `field-stacks/analysis.json`, `field-repeat/analysis0.json`,
  `field-repeat/analysis1.json`.

Pass `--program`, `--manifest`, `--code`, three repeated `--analysis` options,
and `--output`. Repeat with `--allow-vfp-transport` for the wider sizing case.
The tool verifies code/program/manifest identity, records input hashes, rejects
mixed reported module identities and checks exclusive stack-path accounting.
Private output: `region-surface-vfp.json` under the analysis directory above.

Eight unit tests cover observer classification, recursion/missing/indirect
boundaries, unaccounted edges, shared-callee attribution and invalid sample
accounting. These validate the analysis tool, not an optimized game.
