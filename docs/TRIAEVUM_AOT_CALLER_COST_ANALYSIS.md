# AOT caller-cost analysis

**Later decision:** the implementation recommendations below are not approval to
extend the unsuccessful small-region pilot. See
[the measured pre-NRI 20% decision](TRIAEVUM_PRE_NRI_20_PERCENT_DECISION.md).
The new budget investigation found no sufficiently demonstrated 20% candidate;
no AOT optimization was promoted.

Date: 2026-09-16. Starting revision: `5207181`.
Scope: identify the next AOT optimizations, not implement another speculative
emitter change. No game/runtime/renderer/module was rebuilt or replaced.
External decompilation and the independent AOT worktree were read-only evidence.

## Decision

Stop optimizing individual helper calls. Implement **validated whole-loop memory
regions with local architectural state**, first for command-buffer copies and
then for the fixed-size integer audio-effect loops. The improvement must remove
address translation, state traffic, intermediate flag materialization and helper
calls together. Callback hoisting alone is not that optimization.

This is a concrete implementation target, not a demonstrated speedup or a promise
of 200 updates/s. Existing evidence cannot honestly assign exact recoverable
milliseconds to these families. It does establish their callers and redundant
machine operations, which previous instruction-pointer-only profiles did not.

## What changed in the investigation

Added an external x64 Windows stack sampler using the OS-supported
[StackWalk64 API](https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-stackwalk64).
It reads only the selected process, temporarily suspends its busiest sampled
thread, collects context plus unwind frames, and resumes through RAII. No guest
callback instrumentation or title rebuild is needed. Randomized 2..6 ms sleeps
reduce periodic sample aliasing. The existing bounded benchmark owns process
cleanup, including failures/timeouts of the sampler.

`analyze_aot_stacks.py` attributes a helper sample to its first actual translated
caller, not to the arbitrary function name in an ICF-folded helper symbol.
Caller return PCs are resolved at PC-1. Inclusive rows deduplicate recursion and
are explicitly overlapping. Samples whose leaf is outside the AOT DLL are not
charged to AOT, even when AOT exists farther up their stack.

Eight tests cover map parsing, ICF classification, return boundaries, recursive
inclusive accounting, host-leaf exclusion, unresolvable callers and wrong modules.

### Limits

These remain **intrusive stack observations, not ETW/PMU exclusive CPU timings**.
They identify who calls expensive-looking code but do not prove DRAM latency,
instruction-cache misses or an exact time saving. Previous claims in the parallel
lab that negative experiments prove memory-latency domination are not established
by these measurements. Do not repeat those claims as facts.

All three captures concern the same adult-Link Hyrule Field stationary checkpoint.
They establish this fixture's priorities, not universal coverage of the game.
No interpolation, frame limiter, VSync or screenshots. Audio and original scene
processing remain active. Sampler-affected FPS must not be used for speed claims.

## Measurements

Three independent 6,000-update runs, 25-second sampling windows starting after
10 seconds, 180-update benchmark warmup. The symbol-bearing module is the earlier
diagnostic CMP-fused module, **not** the rejected callback-region pilot:

- DLL SHA-256: `2e91d80b432354886fd89145c18ef4118d6c655066c26d973786e3ca78c7a38c`.
- Map SHA-256: `f9dc158e1d0b29713de291a5bfc30698114a8afa0fa4a6c5edec29573523e453`.
- Samples: 1,581 / 1,605 / 1,601; AOT leaf samples: 738 / 719 / 757.
- Total: **4,787 observations, 2,214 inside AOT**.
- A translated caller was resolved for **2,159/2,214 AOT observations (97.5%)**.
- Context failures: zero. Later runs: zero non-progress/depth-limit stops.
- Suspension time: 1.84 / 1.52 / 1.44 seconds per 25-second capture. This overhead
  is reported, not hidden. StackWalk termination is not itself an unwind error.
- All three final memory fingerprints: `6860679424316641920`.
- Every adjacent caller/callee pair in the ten leading paths from the first run
  was verified against direct-call edges in the frozen AOT structural program.

### Caller families, not entry frequencies

Shares below use the 2,214 AOT-leaf observations as denominator. Inclusive rows
overlap; do not sum parents with their children or convert shares into exact ms.

| Family/entry | Samples | Share | Actual work/evidence |
| --- | ---: | ---: | --- |
| `0030F4D0`, inclusive | 289 | 13.1% | Mesh/material preparation and command submission |
| `00485618`, inclusive | 243 | 11.0% | Audio callback chain through `00489BA4`, `0049359C` |
| `002C198C` ExecuteEffect, inclusive | 179 | 8.1% | Subset of audio: fixed-point effect buffers |
| `004A0338` + `004A022C`, direct incl. helpers | 177 | 8.0% | Two fixed-size integer audio kernels, not gameplay loops |
| `0032DB24`, inclusive | 169 | 7.6% | BgCheck_RaycastFloorImpl and its callees |
| `004541B4`, inclusive | 186 | 8.4% | Another substantial call family; not yet a safe replacement contract |
| `00461344`, inclusive | 128 | 5.8% | Includes repeated `0047AF24` traversal |
| `00466E2C`, inclusive | 62 | 2.8% | MeshCommandPacket_Submit; subset of `0030F4D0` |
| `0033B11C`, direct incl. helpers | 42 | 1.9% | Matrix/vector projection, including strict scalar float work |

Mesh submission share in the three runs: 14.0%, 12.0%, 13.2%.
Audio ExecuteEffect share: 8.1%, 9.0%, 7.1%. The broad families recur; individual
low-count function rankings should not be treated as statistically precise.

The previous pilot removed repeated callback checks in the two audio kernels.
Those particular checks account for **38/2,214 = 1.72%** of AOT observations,
not the earlier 16.2% block-entry frequency of those routines. Even that share
includes budget work the pilot retained. That was an inadequately sized target
for a large global gain; expanding the same callback-only change is not justified.

## Concrete machine-level excess

`00466E2C`, loop `00466E7C..00466E90`, is six ARM instructions:
two loads, two stores, decrement/flags and branch. It copies eight bytes per
iteration with a carried/read-ahead word.

In the exact diagnostic DLL, normal mapped/untraced x64 paths are:

- `1802C8160..1802C8210`
- `1802C823D..1802C82F4`
- `1802C831D..1802C832A`

Together: **102 instructions**, including **three calls** whose callee bodies
are additional work, and **17 operands accessing the architectural-state object**
through RBX. Two reads still call ReadFast; stores inline full page translation,
trace checks and write-generation updates. The loop materializes flags and
reloads/stores guest registers throughout. This is concrete emitted machinery,
not a claim that every instruction costs an independent cycle.

Important correctness finding: the original loop reads the next first word even
on its last iteration. A naive memcpy replacement would remove an observable
read/fault and leave R3 wrong. Validate source bytes **plus the four-byte lookahead**,
or preserve the checked path. Overlap, permissions, address wraparound, callbacks,
budget/resume, final GPR/flags and per-store generation semantics also matter.

Decomp evidence read, not copied as an authoritative native implementation:

- `I:/oot3decomp/src/runtime/owner_runtime/z_owner_closeout_bounded_03.c:305`:
  command packet copy and command-list pointer advancement.
- `I:/oot3decomp/src/genuine/z_genuine_cohort_18.c:2403`:
  `0030F4D0` callers, visibility selection, matrix/material prep and submission.
- `I:/oot3decomp/src/runtime/owner_runtime/z_owner_closeout_expanded_02.c:61`:
  ExecuteEffect selects `004A022C/004A0338`, buffers at 0x280-byte intervals.
- `I:/oot3decomp/src/runtime/owner_runtime/z_owner_runtime_accel_long_tail_14.c`:
  integer effect loops. `004A0338` processes 160 samples for each of two channels,
  with circular buffers, fixed-point shifts and feedback. Preserve modulo-32-bit
  arithmetic; signed-overflow C is not an acceptable drop-in implementation.
- `I:/oot3decomp/src/genuine/z_genuine_cohort_20.c:6539`: `0033B11C` projection.
- `I:/oot3decomp/src/genuine/z_genuine_cohort_33.c:23017`: `0047AF24` scans at
  most 50 records. Its frequent entries did not make it a dominant cost:
  29 direct observations, 1.3% of AOT. Do not select it just for entry frequency.

The command loop was checked against code.bin ARM disassembly and the exact DLL
x64 disassembly, not only against the decompiler's reconstruction.

## Next implementation, in order

1. **One shared region mechanism, two useful kernels.** Add a runtime-owned
   validated memory-view contract, used by closed AOT loop lowering. Resolve
   ranges once, retain independent local registers, and materialize state/flags
   at observable exits. Start with the command-copy loop to validate the mechanism,
   then apply the SAME mechanism to both fixed-size audio kernels. Do not spend
   another full optimization tranche on just callback membership or ReadFast's
   prologue. Title-specific entry selection belongs in the adapter/pilot manifest.
2. **Extend to the measured mesh preparation family.** `00452934`, `00452B40`
   and their matrix/command callees belong to the 13.1% family. Preserve the
   original commands and data. The task is lowering traversal/loads/stores more
   efficiently, not skipping materials, drawing or bypassing the NRI contract.
3. **Collision and matrix regions.** `0032DB24` and `00324758` traverse original
   collision data; use validated immutable ranges while keeping query-local
   working state in locals. Consider `0033B11C` and related matrix kernels only
   with exact float ordering, FPSCR and non-fused multiply-add. The prior global
   hardware-VFP test was inconclusive; do not assume a per-op replacement solves it.

For each loop: prove ranges/aliasing and absence of observers over the chunk,
compute the exact budget requirement (or execute bounded chunks with precise
resume state), preserve lookahead/partial-fault behavior, and retain the existing
path when proof fails. Do not globally flatten memory, force-inline every access,
delete state tracking, remove audio or disable faults to manufacture results.

A region pilot must expose qualification counts: fast chunks versus fallback
reasons (callback, budget, mapping, overlap, tracing). Tests cover every exit and
boundary against the unchanged AOT; actual game A/B retains all graphics/audio.
Compilation must reuse untouched shards. This is a compiler/runtime optimization,
not a new decompilation campaign or manual rewrite of thousands of functions.

## Whole-budget check

An additional 3,000-update phase-profile run, without stack sampling, reports:

| Scope, all 3,000 updates | Seconds | ms/update |
| --- | ---: | ---: |
| AOT dispatch/execution | 15.7975 | 5.266 |
| Host SVC | 3.8441 | 1.281 |
| GSP queue, subset of SVC | 3.3131 | 1.104 |
| PICA frontend, subset of GSP | 3.1228 | 1.041 |
| RomFS reads | 0 | 0 |

Post-warmup pre-backend envelope in the same run: **8.160 ms**, of which 7.923 ms
is accounted; the 0.236 ms residual is not silently assigned to AOT. Its interval
is 2,820 updates, so do not add/subtract its totals with the all-update table.
These are elapsed-scope diagnostic times, not privileged CPU-cycle measurements.

There is no ARM fallback in this run. The dispatch count is 368,296 across 3,000
updates, not evidence of a missing whole-function compilation or JIT execution.

The target 5 ms pre-backend envelope would require about 3.16 ms removed from
this measured workload. Callback checks in two functions cannot provide that.
Neither can a perfect command-copy kernel alone. AOT regional lowering must
cover multiple families, and the pre-NRI host work remains part of the budget.

Separate, measurable pre-NRI issue: texture snapshots compare **6.602 GB** across
3,000 updates while copying just **32 KiB**, with 314,960 hits and two misses.
Geometry snapshots compare **1.334 GB** while copying **17.28 MB**; version checks
miss 1,229,943 times vs 47,943 hits. Capture times are 0.804 s textures and 0.824 s
vertices (~0.543 ms/update combined). This supports **range-specific dirty versions**
instead of invalidating cache comparisons on unrelated global guest writes.
It is host-memory/PICA work, NOT an AOT speedup, and requires all store paths,
host pointer writes and savestate restoration to participate correctly.
There is no evidence here for texture memcpy of those entire 6.6 GB.

Source confirmation: `oot3d_native_a32_memory.cpp:221` returns the global
`mWriteGeneration` even for a range query. `oot3d_native_pica_submission.cpp:234`
uses that value before geometry byte comparison; the texture cache path near
line 430 performs byte/hash comparison without a range-version fast acceptance.
Changing the write contract must version/check compatibility with precompiled
title modules: old modules cannot be assumed to populate new dirty metadata.
Retain conservative validation for modules that lack the new writer contract.

## Reproduction and private artifacts

Root: `C:/Users/xander/triaevum-aot-causal-analysis/`.

- `field-stacks/0/native-stacks.json`, `field-repeat/{0,1}/native-stacks.json`:
  raw PCs, module load bases, counts, suspension/termination diagnostics.
- `field-stacks/analysis.json`, `field-repeat/analysis{0,1}.json`: caller reports.
- `phase-profile/0/runtime.json`: nested service/PICA timing and traffic counters.
- Each run stores its exact `command.json` and private writable configuration.
- Diagnostic module/map: `I:/TriAevum-aot-lab-debug/plugin/`.
- Structural program: `I:/TriAevum-aot-lab-evidence/baseline/aot_program.json`.
- Code SHA-256: `16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220`.

Build only `sample_aot_stacks_windows.cpp` with clang-cl, C++20, the existing
nlohmann include directory, and `dbghelp.lib`. Use the existing benchmark with
`--diagnostic-plugin <dll> --sample-map <map> --stack-sampler <sampler.exe>
--frames 6000`, then `analyze_aot_stacks.py --input <native-stacks.json>
--module <dll> --map <map> --output <report.json>`.

No executable optimization was promoted during this analysis. The earlier
experimental callback-region option remains disabled.
