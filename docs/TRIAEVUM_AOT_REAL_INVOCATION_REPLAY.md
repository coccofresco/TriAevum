# Real-input AOT replay

Developer diagnostic and subsequent CPU/memory qualification, begun 2026-09-17.
Replay alone is not a renderer change or a delivered speedup. Later native-memory
and renderer changes, including rejected performance candidates, are recorded
separately below. This supersedes the earlier statement that invocation replay
was not implemented in `TRIAEVUM_AOT_STRUCTURAL_SURFACE_RESEARCH.md`.

Subsequent algorithm-level experiment:
`TRIAEVUM_NATIVE_PROJECTION_KERNEL_EXPERIMENT.md` records a faster native CPU
projection on captured inputs, distinct from the unsuccessful memory-region pilot.

## Implementation

`tools/renderer/tev_program/aot_invocation_probe.cpp` builds a standalone proxy
DLL, outside the shipped runtime. It forwards execution to the unchanged AOT
module and adds one filtered entry callback. On the 64th occurrence it:

1. Runs any original callback before capturing guest input.
2. Copies native guest memory and registers; original memory remains untouched.
3. Executes 16 replicas, resetting all copied memory and guest state before each.
4. Alternates baseline/candidate in ABBA order (same module for A/A validation).
5. Stops at the captured LR, with a bounded 100,000-block budget. External calls
   or another original callback invalidate the isolated replay rather than
   invoking services against copied memory.
6. Compares registers, CPSR/FPSCR, VFP, exclusive state, memory content/write
   generation, consumed blocks and full exit result. Then normal play continues.

Memory copies and fingerprints are outside the timed region. QPC measures elapsed
time, and thread-cycle counters record execution cycles separately. Neither
proves the cost of individual instructions. First-use outliers, memory reset
cache effects and very short calls prevent treating these samples as game FPS.
The proxy removes its extra hook on following outer executions after capture.

## Verified in real game execution

Same adult-Link field checkpoint, 360 native updates, unchanged graphics/audio:

- `0033B11C`: 16 matching replicas of a one-block invocation.
- `002BBF74`: 16 matching replicas of a 158-block invocation with internal calls.
- The latter repeated through the automated build/run driver: another 16 matches.
- A separate run used the original module without the proxy.

All four runs have identical final workload checks:

| Check | Value |
| --- | ---: |
| Memory content | 14004724930905175187 |
| Memory state | 16777719094724974201 |
| Process state | 11508416489629890540 |
| PCM | 15063050011472423652 |
| Draws | 34200 |
| Display transfers | 720 |
| Audio frames suppressed | 0 |

This checks unchanged execution in these runs, not framebuffer pixel equivalence
or universal compatibility of the diagnostic with arbitrary modules/callbacks.
The existing host executable hash remains
`f316277adaad1a77565d5cef389a6311572cfb094f285e50a7693f859d145b2f`.

The automated proxy compile/link took **1.68 seconds**, one object plus existing
support library; no title source regeneration, title recompilation or host build.
Initial one-block timing had a large wall-time outlier. Warm samples were around
3.5-4.6 us; the family samples varied around 8.6-18.3 us. They are A/A diagnostic
observations, not optimization results. A candidate must exceed A/A variability
repeatably and then pass clean, unchanged-game pre-NRI A/B measurement.

## Reproduce

Use `tools/renderer/tev_program/run_aot_invocation_probe.py` with:

- `--compiler I:/oot3dre_tools/llvm-22.1.6/bin/clang-cl.exe`
- `--support` pointing to a freshly built `triaevum_title_whole_aot_support.lib`
- `--include C:/vcpkg/installed/x64-windows-static/include`
- `--baseline` pointing to a title module rebuilt for memory-observation ABI 3
- `--executable` pointing to a host rebuilt from the same current headers
- `--invocation %USERPROFILE%/triaevum-aot-memory-region/abba/0/command.json`
- `--entry 002BBF74`
- `--output` pointing to a new private directory.

Omit `--candidate` for the A/A control. Supply an ABI-compatible candidate module
only for local experiments. Existing developer headers, support library and both
modules must agree on C++ memory layout/toolchain; ABI version alone is not a
proof of this compatibility. This prototype is Windows-only, not a new platform
dependency in the runtime. The declared external callback boundary is rejected;
it is not a replay of arbitrary host side effects or multi-threaded subsystems.

The driver records build commands and hashes, copies writable configuration via
the existing bounded runner, and fails if capture does not occur, replay rejects
an observer/exit, replicas differ, or original memory changes. Runs are bounded
and processes exited. No ROM-derived data is saved by the proxy: only counters
and timing rows. Diagnostic binaries, logs and existing saves remain private.

Private results under `%USERPROFILE%/triaevum-aot-causal-analysis/`:
`invocation-aa-game`, `invocation-family-aa-game`, `invocation-control-game`,
`invocation-repro-check`; first two CSV files sit in that parent directory.

## First candidate evaluation

Replayed the existing scoped-memory/local-state candidate at `004A0338`, without
recompiling title logic. Candidate DLL SHA-256:
`1b7d7821d631628ae62998f9bd48eb12792e03884e6688d603ecb67c2a326784`.
This is the previously built second pilot, whose whole-game comparison had been
interrupted; it is not a newly implemented optimization.

Two candidate runs and one A/A control each replayed 16 copies of the captured
2,472-block invocation. All registers, memory content/generation, block counts
and exits matched, with original memory unchanged. Full game runs completed.

Each quartet uses ABBA order. Below is the median of four within-quartet ratios
(mean B / mean A); lower is faster. The final column separately excludes the
first quartet to expose first-use sensitivity, NOT to select a favorable result.

| Run | Elapsed ratio | Thread-cycle ratio | Cycles excluding first quartet |
| --- | ---: | ---: | ---: |
| Candidate, first | 1.236 | 1.229 | 1.053 |
| A/A control | 0.986 | 0.988 | 0.948 |
| Candidate, repeat with hook metadata | 1.037 | 0.882 | 0.933 |

The A/A cycle ratios across individual quartets range from 0.709 to 1.418.
This spread is too large for small local speedup claims. The candidate does not
demonstrate a repeatable advantage; neither 22.9% regression nor 11.8% improvement
is established from the individual median values. Whole-game diagnostic FPS is
not used as optimization evidence.

The probe now records original callback PCs. The capture had 183 original hooks,
none in `[004A0338, 004A0570]`, the candidate's conservative region interval.
Thus hook-list overlap is not the explanation for failure to improve here.
This metadata does not itself prove which branch was taken: the candidate also
checks memory trace eligibility, and no optimized-path hit counter was added.

Private directories under the analysis root: `invocation-audio-candidate`,
`invocation-audio-control`, `invocation-audio-routing`. Each contains exact
input/module hashes, compilation commands, copied configuration, CSV and logs.

**Decision:** do not expand or promote this pilot. Correctness for these real
inputs is demonstrated, performance benefit is not. Even its earlier 5.2% AOT
sample share could not independently supply the requested 20% pre-NRI reduction.
The hypothesis of a broader transformation remains unproven; these tests do not
justify a large rebuild or substituting static coverage for removable cost.

## Independent Dynarmic executor

`build_dynarmic_replay.py` builds a private DLL from an existing Azahar/Dynarmic
developer build. `dynarmic_replay_bridge.h` exposes only POD state and C callbacks:
the donor's dynamic CRT and the AOT module's static CRT do not exchange STL
objects. No donor JIT, SDK, ROM data or new dependency enters the product package.
Pass the resulting DLL as `run_aot_invocation_probe.py --dynarmic-bridge`.

The CPU replay compares the same captured input with:

- AOT/AOT, as a noise control;
- AOT/Dynarmic with checked native memory callbacks;
- AOT/Dynarmic with a private writable-page table;
- optionally, AOT/candidate generated module.

The page-table mode deliberately omits AOT write-generation bookkeeping. It is
not a drop-in runtime implementation or an apples-to-apples total-cost result.
Executable writes, exclusive operations, host services and interior observers
exclude a capsule rather than silently approximating their semantics. The ARM
entry PC comes from the captured callback: the AOT guest's lazily materialized
`r15` is not necessarily that entry.

JIT translation and full-memory fingerprints are outside timing. Warm batches
reset only changed page-contained chunks, including partial region boundaries;
registers, flags and exits are checked throughout. Full memory is checked at
warmup and pair end, with changed chunks checked between batches. Each report
marks which rows received a full-memory check. Batches run on one allowed
logical processor and restore the original thread affinity afterward.

`summarize_aot_abba.py --cpu-replay` always discards the first complete quartet.
Timing requires the A/A median within 5% and every retained quartet within 20%,
for both elapsed time and thread cycles. Architectural agreement and timing
qualification are separate results; noisy samples do not become speedup claims.
The initial five-root corpus (`002BB780`, `002BBF74`, `00324758`, `003FAD68`,
`0040C744`) reached matching architectural results, but failed the A/A timing
gate even after batching and pinning. Those timing observations are rejected.
Those runs also used a support archive predating the current memory-layout
contract. They are historical diagnostics, not qualification of the current
host/module pair. Memory ABI 3 changes the native fast-page stride: rebuild every
title shard, wrapper, support library, host and diagnostic together. Relinking
an old shard archive with a new wrapper does not establish compatibility.

The native bridge probe exercises checked/page-table execution, overflow, warm
reset, bounded loops, memory faults and observer stops. The memory-reset probe
exercises unaligned region boundaries and rejects executable-memory mutation.
These are executor-boundary checks, not renderer or whole-game speed evidence.

### Offline IR decision

Do not adopt another product backend on these results. Dynarmic's
`frontend/A32/translate/a32_translate.h` does expose instruction/block translation
into IR. Its x64 emitter, however, takes `BlockOfCode`, `UserConfig` and `A32::Jit`,
and owns runtime dispatch tables, fast-memory fallbacks, RSB terminals and block
patching (`backend/x64/a32_emit_x64.h`). Offline IR reuse would need a static
object/relocation backend and explicit integration with the native memory,
observer, fault and suspension contracts; serializing its code cache is not that
implementation. The core declares the permissive 0BSD license, but dependency
and downstream notices still need separate preservation.

The measured question is not yet “which IR wins”: the checked comparison is
noisy, and the page-table comparison changes memory bookkeeping. The broad
register-residency trial below was rejected against matched AOT controls.
Keep Dynarmic as an independent developer oracle; no runtime JIT fallback and
no Forge compilation path.

## Native memory and broad register residency

Default functions bind promoted GPRs and lazy flags directly to architectural
state. Local residency remains confined to the existing explicitly selected
callback-free/memory regions; the attempted broad rollout below is rejected.
Local-state exits commit explicitly: no caller scope-exit cleanup can overwrite
a suspended callee's newer state. Conditional terminal emission uses the same
structured conditional helper as instructions, so both taken returns and
untaken fallthroughs cross the correct boundary.

`run_aot_state_boundary_probe.py` compiles a real synthetic title DLL and executes
nested returns/flags, observer mutation and reload, snapshotless callee suspension,
callee memory faults, and taken/untaken conditional returns. These native checks
and the 62 whole-AOT generator tests pass with the current memory ABI.

Resource snapshot certificates now belong to `NativeA32Memory`, not a renderer
page map. Typed fast stores update a sparse fast-page epoch directly; checked,
cross-page, byte, fill, host and deferred-device writes update every affected
page. Architectural write counts remain unchanged. A private epoch bias prevents
restores and moved/replaced images from reviving old certificates. Escaped mutable
pointers remain uncertifiable; consumers compare bytes instead.

The renderer can therefore reuse immutable resource bytes without registering a
write callback on a singly mapped resource. Distinct physical aliases still need
write observation for actual byte synchronization. GPU-copy ownership retains its
separate byte-range invalidation contract. Different or unavailable alias tokens
fall back to content comparison, never a hash of unrelated opaque tokens.

Native memory and PICA submission tests cover unrelated-page writes, crossing
stores, deferred publication, alias visibility, retained immutable snapshots,
device materialization, pointer escape, restore and move replacement. The move
replacement case reproduced a certificate collision before its fix and passes
afterward. These are correctness results, not a whole-game speedup claim.

The ABI-3 reference's own 360-update field report confirms 9,759,835 geometry
bytes compared, 4,788,480 texture bytes compared, and 2,044,992 geometry bytes
copied (`v3-field-control/runtime.json`). These reductions are observed in the
native-owner implementation too; they still do not imply a CPU-time reduction.

The first renderer-owned callback prototype reduced compared geometry bytes from
160,064,280 to 9,759,835 and texture bytes from 792,207,360 to 4,788,480 over the
360-update field capture, with identical captured framebuffers. It nevertheless
regressed guest CPU time by 5.3% in the coherent-module ABBA run. That prototype
is rejected; its byte-scan reduction is not evidence for the new native-owner
implementation's performance.

### Full-title ABI 3 capture qualification

Both matched variants contain all 12,439 selected functions in the same 256
shards. The control binds GPRs/flags to architectural state with empty local
synchronization; the candidate keeps them local. IR, selection, placement,
compiler options and memory support are otherwise matched.

The isolated host accepts the current synthetic module and rejects the frozen
memory-ABI-2 module through its actual `--verify-title-plugin` entry point.
Compiled synthetic stores also change the host's range certificates, while
snapshotless and fault exits without stores leave them unchanged.

The complete candidate, not just replay replicas, then executed both native
scenarios with identical deterministic framebuffer files and structural results:

| Scenario | Updates | Memory content | Memory state | Draws | Transfers |
| --- | ---: | ---: | ---: | ---: | ---: |
| Field | 360 | 6672342775686176555 | 7203007105803801745 | 34,200 | 720 |
| Sheik/raw copy | 400 | 17615798149623760801 | 2578277339581967901 | 41,671 | 1,118 |

Field captures at updates 120/240 and raw-copy captures at 120/240/360 match the
frozen control byte-for-byte. Both end with zero pending draws/transfers. The
matched ABI-3 reference also passes the field comparison. This establishes
equivalence in these scenarios, not every game state or a CPU speedup.

Private evidence under `I:/_triaevum_work/cpu-dynarmic/`:
`v3-field-control`, `v3-field-local`, `v3-copy-baseline`, `v3-copy-local`.
Each candidate directory has `equivalence.json` with full framebuffer hashes.
The original live executable remains unchanged.

| Artifact | SHA-256 |
| --- | --- |
| Isolated host, `runtime-v3b` | `3624791e146934157e1b3aeb2fccf38ae0d1c3f32aa9ebb6539f4a4d3a33ae70` |
| Matched reference module | `5d3777a22304e2715c4e15502fbb1db23c14ca6b9037a7a289000cef9341d6c4` |
| Local-register module | `b3116a7d4c9814ab305da3042ba0aabf228295f5d227fed557e6f46a1c3918a4` |
| Shared memory support | `b770d06e87a2aa9799782ba91c5cbd98ca2538827b7c2a6630df3d3e37b2783f` |

Code footprint is a separate risk: PE `.text` grows from 75,096,922 bytes in the
matched reference to 123,266,442 bytes in the local-register candidate. This is
measured machine-code size, not an inferred instruction-cache cost or speedup.

### Broad-lowering performance verdict: rejected

Each quartet runs 900 updates and measures the final 720. All A/A and A/B
quartets have workload fingerprint `17842070870211788197`. Ratios below are
candidate time / control time; lower is better, not an FPS percentage.

| Comparison | Pre-backend paired ratios | Guest excluding clock paired ratios |
| --- | --- | --- |
| ABI-3 reference A/A | 0.99905, 1.03472 | 0.99918, 1.03482 |
| Local vs matched reference | 1.06045, 1.07664 | 1.08702, 1.10510 |
| New host + local vs frozen pair | 1.07507, 1.03040 | 1.09027, 1.04525 |

The A/A gate passes, with maximum pair deviation about 3.48%. Isolated broad
lowering regresses median pre-backend time by 6.85% and guest time by 9.61%;
both directions regress beyond that A/A spread. The combined candidate also
regresses, by 5.27% and 6.78% respectively. The 20% pre-backend reduction target
is not met. Architectural equivalence does not justify deploying this lowering.
The generator default has therefore returned to architectural references;
the rejected full-title binaries and receipts remain private evidence.

The independent ABI-3 replay passes architectural comparison on all five
captured roots (`002BB780`, `002BBF74`, `00324758`, `003FAD68`, `0040C744`),
but none passes its stricter microbenchmark A/A timing gate. Those timings
cannot establish a qualified AOT/Dynarmic speed ratio or a gameplay speedup.
Evidence: `v3-corpus`, `v3-aa`, `v3-lowering-abba`, `v3-combined-abba`, and
`v3-qualification.json` under the private evidence root.

### Isolated native-owner result

Without broad register residency, the ABI-3 host/reference pair gives
pre-backend ratios `0.96332, 0.97802` and guest-excluding-clock ratios
`0.95149, 0.96914` against the frozen pair. Median time reductions are 2.93%
and 3.97%; both directions improve. The weaker pair, however, does not exceed
the preceding A/A maximum deviation. This is favorable bounded evidence, not
a robust significant-speedup qualification or the 20% target.
The measured window and fingerprint match the previous comparisons.
Evidence: `v3-memory-abba/summary.json`.

### Support ThinLTO experiment

The existing title shards use ThinLTO, but the matched support archive contains
native objects. The private experiment rebuilds its five memory/VFP translation
units with exactly the captured CMake compile commands plus `-flto=thin`, then
relinks the same reference shard objects. All five support objects are verified
as LLVM bitcode. It does not change scalar algorithms, ARM floating-point
semantics, memory ABI, selected functions or shard placement.

The resulting support archive SHA-256 is
`3b68bf72c208526b50c0797444e19f01ca3b0d8d06bc12cc36b35e91f75b32d2`;
the module SHA-256 is
`2208d95bf96ddf752e6acf8b97e3f13019f8cc6a844297279173a7f969596d7a`.
Its `.text` is 79,989,418 bytes. The six native state-boundary checks pass.
These facts establish a real compiler experiment, not gameplay qualification.

The first native launch failed loading the checkpoint with `bad allocation`,
before any frame. A concurrent test run also reported a full system volume.
Inactive diagnostic cache/generated artifacts were moved, not deleted, from
`%USERPROFILE%/triaevum-aot-memory-region/` to the private evidence root's
`reclaimed-memory-region/`; live binaries and benchmark input configurations
were retained in place. After relocation, all 62 generator and 13 backend tests
pass. The allocation-failure evidence is preserved in
`v3-field-lto-failed-allocation`; it is not a semantic divergence result.

After reclaiming capacity, the complete ThinLTO candidate passes both the field
and raw-copy scenarios with the same memory fingerprints, draw/transfer counts
and captured framebuffer hashes as the frozen pair.

| Comparison | Pre-backend paired ratios | Guest excluding clock paired ratios |
| --- | --- | --- |
| Fresh reference A/A | 0.95938, 1.02353 | 0.96196, 1.02146 |
| ThinLTO support vs matched reference | 1.01480, 0.97935 | 1.01729, 0.97631 |
| Native owner + ThinLTO vs frozen pair | 0.96435, 0.94135 | 0.95337, 0.92515 |

Support-only median reductions are 0.29% pre-backend and 0.32% guest, with
opposite-signed pair results: no qualified benefit. Do not enable support
ThinLTO by default on this evidence. The combined candidate's guest reduction
is 6.07%, with both pairs exceeding its A/A spread; its 4.72% pre-backend
reduction does not clear that stricter test in both directions. It does not
meet the 20% pre-backend target. `v3-final-qualification.json` retains the
separate decisions; the original executable SHA-256 remains unchanged.

### Profile-guided function layout

A bounded intrusive RIP profile of the matched reference records 5,397
observations, 1,971 inside the title module, with no sampler errors. Of these,
490 fall in read helpers, 123 in write helpers, 293 in block-entry helpers,
332 in scalar-float helpers, 672 in translated bodies (including inlined
helpers), 49 in dispatch and 12 elsewhere. These are observations, not exclusive
CPU-time percentages or an instruction-cache-miss measurement.

The 151 symbols covering 1,774 of the title observations span 74,160,000 bytes
of the original image, despite summing to 1,556,395 bytes of link-map symbol
ranges. Relinking the identical reference objects/support with a frequency
order file reduces that hot-symbol span to 3,544,112 bytes. The candidate
SHA-256 is `f9cfd6c11080dfc9a203c24542546f47c9ecbb1264da1f5fb248dd49af87e476`.
No lowering, floating-point algorithm or memory semantic is changed.
This proves that the requested layout was applied, not that it is faster.
Evidence: `v3-layout-profile/0/native-samples.json` and
`v3-layout/plugin/receipt.json`.

The complete layout candidate passes both native scenarios with identical
memory fingerprints, draw/transfer counts and framebuffer hashes
(`v3-field-layout/equivalence.json`, `v3-copy-layout/equivalence.json`).
Its first timing campaign is not qualified: the reference A/A pair spread
reaches about 10.6%, and the matched A/B quartet stops at run 2 with a
savestate-load `bad allocation`, before guest rendering starts. The two
completed A/B runs are not a complete ABBA result. No layout speedup or
promotion is claimed.

`capacity-after-layout-failure.json` records 77,396,615,168 committed bytes
against an 80,142,331,904-byte limit and 255,598,592 free bytes on the system
volume. The unrelated Gradio process holds 15,093,944,320 private bytes.
Further clean timing needs stable commit headroom; user workloads are not
terminated automatically to obtain it.

The user subsequently authorized stopping that Gradio workload. Only PID
37808 was present in the terminated tree. The post-stop snapshot reports
72,549,732,352 committed bytes against the same limit
(`capacity-after-authorized-stop.json`). The interrupted A/A and A/B
directories are preserved with the `-resource-limited` suffix; the restart
uses entirely new complete quartets, not appended missing runs
(`v3-layout-restart.json`).

The restarted campaign completes all twelve runs without an allocation failure.
All three quartets retain fingerprint `17842070870211788197` and 720 measured
updates per run.

| Comparison after resource release | Pre-backend paired ratios | Guest excluding clock paired ratios |
| --- | --- | --- |
| Reference A/A | 1.05690, 1.20356 | 1.06084, 1.21440 |
| Ordered vs matched reference | 1.00588, 1.02367 | 1.00882, 1.02625 |
| Native owner + ordered vs frozen pair | 0.99494, 1.00742 | 0.98856, 0.99863 |

The fresh A/A fails the noise gate: its median deviation is 13.02% pre-backend
and 13.76% guest, with a pair exceeding 20%. The ordered module shows no
observed matched-control advantage (median time increases of 1.48% and 1.75%);
the failed noise gate prevents attributing those small deltas to the layout.
The combined candidate is effectively unchanged within this variation.
Do not promote the layout or repeat measurements until a convenient percentage
appears. The complete verdict is `v3-layout-qualification.json`: no qualified
layout gain, no qualified combined gain and no 20% pre-backend improvement.

**Delivery decision:** broad register residency was reverted as a measured
regression; support ThinLTO and profile-guided ordering remain rejected/private
experiments, not new defaults. Native page certificates and snapshot reuse are
correctness-verified and reduce byte comparisons, but this investigation does
not establish the requested major CPU speedup. No runtime JIT or Forge compile
fallback was introduced. The shipped/live-path executable remains SHA-256
`1176593d834f57b2313a5c4c36a830ff1bec7140ec1d50914752cda45335742f`.

Verification scope: 62 generator tests, 13 title-backend tests and five
noise-gate tests pass. Native memory-only, PICA submission and architectural
boundary checks also pass. The full process-test suite is not claimed: its
packed-registry dispatch-override fixture (`TestNativeFunctionAtDispatchEntry`)
does not qualify in the product-mode build used here. No meaningful dispatch
test was removed to hide that limitation.

One-off private orchestration, shutdown and reporting scripts have been removed.
Their completed JSON receipts, exact compiler/linker/launch commands, generated
sources, binaries, order file and deterministic capture evidence remain in the
private evidence tree. The reusable replay, boundary-probe and comparison tools
remain in the repository. No public package or title catalogue was replaced.

## Dedicated CPU optimization branch

Subsequent hot/cold helper and exact hardware VFP development is recorded in
[AOT CPU optimization development](TRIAEVUM_AOT_CPU_OPTIMIZATION.md).
Its new correctness and timing evidence does not retroactively qualify the
rejected residency, support-ThinLTO or layout experiments above.
