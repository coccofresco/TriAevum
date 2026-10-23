# AOT memory-region pilot

Date: 2026-09-16. Follows [caller-cost analysis](TRIAEVUM_AOT_CALLER_COST_ANALYSIS.md).

## Scope

This is an opt-in compiler/runtime optimization. It does not change the renderer,
audio algorithms, simulation rate, saves, ROM contents or the release package.
External decompilation repositories remain read-only. No manual replacement of
guest audio/gameplay functions is involved.

`generate_whole_aot_cpp.py --memory-region 0xENTRY` selects functions. The pilot
selects `00466E2C`, `004A022C`, `004A0338`, based on the measured callers. Addresses
are selection inputs, not conditions embedded in the generic memory mechanism.
Default generated code remains unchanged.

## Mechanisms

- `whole_aot_memory_regions.py` proves a carried-word pair-copy instruction
  pattern, independent of addresses and register allocation. The emitter keeps
  the original body and inserts a qualified chunk before it.
- `oot3d_native_a32_memory_region.h` validates the full read/write spans, including
  the final word loaded but not stored by that chunk. Disjoint spans allow bulk
  copying. Overlap, wrapping addresses, insufficient budget, hooks, tracing or
  invalid mapping keep the original instruction path, including partial faults.
- A chunk consumes exactly the original number of basic blocks and stores. It
  restores source/destination pointers, carried/scratch registers, count and
  flags before any exit. A budget-limited chunk resumes at the original block.
- Closed call-free CFGs use local scalar GPRs/flags and scoped memory views.
  Views retain a validated allocation range and its permissions; every access
  still checks containment. This is NOT elimination of every bounds check.
  Misses resolve the new range or use the original checked access. Guest store
  order and arithmetic remain generated from the original instructions.
- Write generations accumulate locally only while no observer can execute;
  checked slow paths and all exits commit the exact store count. Full/sparse
  hooks that intersect the function and write tracing select the original body.
  Exclusive/atomic/system operations, calls and external CFG edges are rejected.

The memory view is synchronous and noncopyable. It must never survive a mapping
change, service/callback, save restore, yield or external observer.
`NativeA32Memory` adds a friend declaration and a non-virtual eligibility query:
no data member or virtual method changes, so its binary layout is unchanged.
Deferred writes, observed ranges, access watches and write tracing prohibit the
unobserved path; pending device-owned memory must retain checked observation.

## Validation

- `test_whole_aot_memory_regions.py`: instruction-shape recognition/rejection
  and rejection of exclusive operations. Compiled differential execution, not
  generated-source substring assertions, qualifies state and memory behavior.
- `run_memory_region_probe.py` / `memory_region_probe.cpp`: compiled differential
  execution against the ordinary generated body. With the original audio code,
  6,912 cases compare registers, CPSR, flow/PC/fault detail, block budgets, memory
  bytes, write generations, trace fingerprints and callback sequence.
  Cases include zero counts, short budgets, eight consecutive resume chunks,
  signed fixed-point values, overlap, invalid/protected memory, partial faults,
  full/sparse callbacks and write tracing. View hits and copy/fallback counters
  are checked separately. These are correctness tests, not game-speed claims.
- `test_whole_aot*.py`: 64 tests passed before the in-game pilot.
- The normal object cache includes the region header when the generated manifest
  enables it. Each affected source also embeds its exact contract hash.

## Fast developer iteration

`tools/renderer/tev_program/build_aot_region_pilot.py` compares source hashes and
verifies every reused object and common dependency against the frozen control.
Its sole permitted header exception removes the exact added friend declaration
before hash comparison. No other header or layout change is accepted.

This private experiment recompiles shards 230, 243, 244 and reuses 254 objects.
It does not insert mixed-header objects into the release cache. Link workers are
limited to two, with a persistent ThinLTO cache for subsequent iterations.
Production qualification must use the normal complete build/cache contract.

Private artifacts: `%USERPROFILE%/triaevum-aot-memory-region/`.
Frozen source manifest/module: `I:/TriAevum-aot-lab-evidence/baseline/`.
Support library: `J:/TriAevum-verify-20260910/runtime/triaevum_title_whole_aot_support.lib`.
Compiler: `I:/oot3dre_tools/llvm-22.1.6/bin/clang-cl.exe`.

## In-game qualification

First candidate: eight unsampled ABBA runs, 1,800 updates each (180 warmup), same
adult-Link Hyrule Field checkpoint, graphics/audio enabled, no interpolation,
VSync, limiter or captures. All final memory fingerprints matched
`12933266925516870560`. Paired median candidate/control pre-backend envelope:
**1.0174**, not an improvement. Guest excluding clock: **1.0254**. Not promoted.
Evidence: private `abba/measurements.json` and `abba/summary.json`.

Assembly showed out-of-line region ReadFast/WriteFast calls. The second candidate
inlines the view access and keeps block budgets local until exit. All 6,912
differential cases and 64 Python tests still pass. Its sparse build including
link took 17.95 seconds with the populated ThinLTO cache (initial build: 396.57 s).
DLL SHA-256: `1b7d7821d631628ae62998f9bd48eb12792e03884e6688d603ecb67c2a326784`.

The second comparison was intentionally stopped during its first candidate run
when the user requested a strategic review. It is INCOMPLETE, not a runtime
crash, not performance evidence. The unchanged control alone measured 7.669 ms,
versus 8.685 ms median in the previous campaign: cross-campaign timing variability
is material. No performance improvement or production readiness is claimed.
The installed game/module and default generation remain unchanged.

## Source adoption qualification (2026-09-26)

The surviving pilot sources were not independently usable: the current emitter
lacked the opt-in argument and the memory view lacked its friend declaration.
Three Python cases failed with an argument-count error before repair; the
compiled probe then exposed the missing access declaration. The CLI, emitter,
manifest/cache identity and runtime declaration are now connected.

Fresh verification passes 6,912 compiled differential cases plus a deferred-read
and copy-fallback regression, 62 whole-AOT Python tests, 13 release object-cache/
backend tests and two ABBA summarizer tests. The probe uses a freshly built
`triaevum_title_whole_aot_support` library matching the runtime headers; an old
support library without `DeferWrites` cannot qualify the current contract.
Two source-substring tests were removed rather than repinned to the emitter.
The CLI generated the three selected functions as a four-shard private product.
Without opt-in, all generated C++/header hashes match the previous emitter;
disabling opt-in restores those exact files. Changing the helper contract in a
private copy invalidates affected shard hashes, and the normal object-cache
dependency set includes the helper only for enabled manifests.

The historical ABBA inputs were re-summarized, not re-benchmarked: the
pre-backend ratio remains 1.0174 and the experiment is still not promoted.
The old frozen control is not permission to reuse objects across subsequent
memory/header changes; the sparse helper must reject any difference outside
its documented friend-only exception. No title DLL was installed or promoted.
Private evidence is under `I:/_triaevum_work/remaining-progress-qualification/`.
The combined renderer/audio/pacing qualification and its separate limitations
are in [`TRIAEVUM_RUNTIME_PROGRESS_QUALIFICATION.md`](TRIAEVUM_RUNTIME_PROGRESS_QUALIFICATION.md).
