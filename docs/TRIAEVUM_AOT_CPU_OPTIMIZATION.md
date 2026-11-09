# AOT CPU optimization development

Branch: `perf/aot-cpu-hotpaths`, created from `d654122` on 2026-09-27.

## Baseline and scope

The working tree already contained 93 modified tracked files and 17 untracked
paths. Graphics/UI work outside the CPU dependency set remains unstaged and is
not part of these commits. The initial tracked diff is preserved privately at
`<work>/cpu-dynarmic/branch-baseline.patch`.

The CPU baseline includes memory-observation ABI 3, native per-page write
certificates, alias-safe immutable resource snapshots, corrected AOT state
boundaries, and developer-only replay/qualification tools. These changes and
their completed verification precede this branch; see
[real-input replay](TRIAEVUM_AOT_REAL_INVOCATION_REPLAY.md). They are not a newly
qualified major speedup. The original `TriAevum.exe` and release catalogue are
not replaced by this development work.

The matched reference is the existing ABI-3 module
`<work>/cpu-dynarmic/v3-control/plugin/triaevum_title_aot.dll`, SHA-256
`5d3777a22304e2715c4e15502fbb1db23c14ca6b9037a7a289000cef9341d6c4`, with the isolated
`TriAevum-cpu-pages-v3b.exe` host. Earlier broad register residency, support-only
ThinLTO and function ordering did not establish a qualified improvement; they
are not defaults for this work.

Current decision: **no optimization candidate is promoted**. The measured
forced-inline and operation-local-MXCSR variants regress. Their runtime and
build-integration edits were removed from the working tree; the verified
baseline remains the source implementation. Private binaries and receipts
preserve the results, not a new default. The user's subsequent request redirects
the work to structural diagnosis before further speculative variants.
The only subsequent runtime correction is diagnostic attribution for synchronous
invocations; it does not change guest execution or the disabled profiler path.

## Implementation sequence

1. Separate normal typed memory access and block-entry checks from diagnostic,
   checked-memory and observer slow paths. Constant PCs should reach the native
   optimizer without an out-of-line hash calculation.
2. Evaluate flat read-page lookup without dropping write certificates, aliases,
   deferred reads, watched-page traps or precise fault behavior. Change the
   memory ABI if the compiled object layout changes.
3. Implement and qualify exact hardware VFP paths. Preserve guest FPSCR,
   rounding, NaN payload/default-NaN rules, input/output subnormal behavior,
   exception flags, non-fused multiply-accumulate and host FP state.
4. Evaluate, rather than assume, a static Dynarmic-IR backend and legal
   contained-function deduplication. Runtime JIT cache serialization, an
   interpreter fallback and user-install compilation are not product options.

Each implementation boundary has its own commit and evidence entry below.
Correctness qualification and performance qualification remain separate.

## Invariants and acceptance

- Keep block-budget accounting, skip-first notification, filtered/unfiltered
  observers, register/flag reload and observable-exit behavior unchanged.
- Preserve exact reads/writes across page boundaries, protected/unmapped pages,
  watch traps, physical aliases, deferred device writes and pointer escape.
  Same-value writes still invalidate relevant ownership/certificates.
- Keep native PICA ordering, transfers, immutable snapshot bytes and the
  renderer/UI composition contract unchanged.
- A header change requires rebuilding every affected AOT shard with dependency
  identities that include the changed header. No mixed-layout module may load.
- Run owning-boundary checks and actual compiled-module smoke probes. Compare
  deterministic framebuffers, memory/state fingerprints and structural workload
  diagnostics in field and raw-copy scenarios.
- Measure matched candidates with fixed workload, warmup, A/A and ABBA controls.
  Failed noise gates do not establish a speedup or justify repeated sampling
  until a favorable percentage appears.
- No ROM-derived source, IR, captures, savestates, binaries, caches or SDKs are
  committed. Evidence stays private and documentation identifies exact inputs.

## Source-guided investigation

Inspected `fearkov/3dsrecomp` revision
`298836bf1a69ca55d1d36c94989f5c71b69e223b`:
[ABI/helpers](https://github.com/fearkov/3dsrecomp/blob/298836bf1a69ca55d1d36c94989f5c71b69e223b/abi/recomp.h),
[generator](https://github.com/fearkov/3dsrecomp/blob/298836bf1a69ca55d1d36c94989f5c71b69e223b/src/codegen/mod.rs),
[verification](https://github.com/fearkov/3dsrecomp/blob/298836bf1a69ca55d1d36c94989f5c71b69e223b/src/verify.rs).
Small direct-memory helpers and block-local budget checks are useful patterns;
its interpreter-relative performance and NaN-tolerant verification are not a
performance/fidelity oracle for this runtime.

Private investigation: `<work>/cpu-dynarmic/upstream-study/`.
`findings.json`, `current-codegen.json`, `probe-results.json` and
`probe-commands.json` record the source identities, real disassembly and probes.
The existing intrusive RIP profile has 1,971 samples inside the title module:
613 memory helpers, 293 block entry, 332 scalar FP. Of the block-entry samples,
150 are in prologue/budget/filter, 114 in exact sorted-hook search, five in the
callback/state-transfer segment and 24 at common return. These are not exclusive
CPU-time measurements.

An executed standalone frontend probe translated five real entry blocks
(62 ARM instructions) with Dynarmic v6K, CPSR `0x10`, FPSCR `0`. Its own IR passes
reduced 432 operations to 288, GPR reads 106 to 57, and GPR writes 56 to 36, with
memory-operation counts preserved and donor IR verification completed. It did
not execute guest code, emit a static title module or compare speed against our
optimized machine code. Get/Set elimination must respect observable exits;
the donor itself disables it with `check_halt_on_memory_access`.

A separate executed semantic probe reproduced plain-C addition plus result-FZ
handling and found mismatches against the exact local VFP helper for inexact
flags, positive rounding, subnormal input with FZ and default NaN. This was an
independent reproduction of the arithmetic strategy, not execution of the
upstream library. Hardware acceleration must not copy that weaker contract.

Instruction-address containment found 429 candidates across 12,439 selected
functions, covering 29,102 of 963,498 per-function instruction occurrences.
Containment alone does not prove that entry, return and observation contracts
permit merging the generated function bodies.

## Development record

### Baseline preservation

The initial CPU-only commit preserves the existing ABI-3 and replay work before
new hot-path changes. Historical verification and its explicit process-test
limitation are recorded in the replay document. Unrelated graphics/UI edits,
private evidence and generated title inputs are excluded from the commit.

Fresh branch-baseline checks: the 36 `test_whole_aot_cpp.py` tests pass.
The real generated-DLL boundary probe passes nested return/flags, observer
reload, snapshotless unwind, callee memory fault, conditional return and compiled
page epochs. Receipt: `branch-baseline-boundaries/receipt.json` under the private
CPU evidence root. This does not remove the historical full-process-test limit.

### Hot/cold separation

The first candidate inlined budget consumption, first-entry state and the
constant-PC filter check. Exact PC lookup and callback/state materialization
remained shared out-of-line paths. Empty PC lists still mean
unfiltered callbacks; filter collisions never substitute for exact membership.
Budget exhaustion precedes notification, including the next block of a
partially executed function.

Its typed reads and writes inlined page lookup, access and write-epoch stamping.
Checked access and trace comparison/fingerprinting had shared cold bodies.
The candidate kept memory layout and memory-observation ABI version 3. Its
title-cache dependencies included the experimental compiler-attribute header.

Executed checks: 2,376 read-path comparisons; 36 generator tests; seven actual
generated-DLL state-boundary checks, including hook/budget precedence and a
deliberate filter collision. Evidence: `hotpaths-memory-smoke/receipt.json` and
`hotpaths-split-boundaries/receipt.json`. The initial zero-budget fixture was
incorrect: the public executor rejects zero before entry, in both baseline and
candidate. Exhaustion is tested inside execution instead. Full-title scene and
performance qualification is separate from these boundary checks.

The complete 12,439-function module passes Field (360 native updates) and Raw
Copy (400): all five captured framebuffers, memory content/state, process state,
draw/transfer counts and drained queues match the ABI-3 control. Receipts:
`hotpaths-field/equivalence.json` and `hotpaths-copy/equivalence.json`.
The code-size cost is material: `.text` grows from 75,096,922 to 127,352,618 bytes.
The timing qualification below rejects this tradeoff.

The clean A/A gate passes: paired pre-backend ratio 1.02425 and guest ratio
1.02850, within the predeclared 5% median/20% individual-pair limits.
The following ABBA rejects forced inlining: pre-backend 10.8334 to 11.3064 ms,
paired ratio 1.04393; guest excluding clock 8.3314 to 8.7898 ms, ratio 1.05538.
Both pairs worsen. Memory fingerprints and 720 measured updates match in every
run. Evidence: `hotpaths-performance.json`.

A second candidate shared the normal typed-memory and block-entry gates out
of line. Its full-title build was cancelled after the user's request for
structural diagnosis. It has no qualified speed or scene-equivalence result.
An untested binary64-intermediate VFP draft was also set aside. Their source
is preserved in `unpromoted-cpu-experiments.patch` and the companion
`unpromoted-oot3d_aot_compiler.h`, outside the repository. Neither remains in
the runtime implementation.

### Flat read-table decision

Retain the sparse ABI-3 table in this cutover; do not infer a flat-table win
from the old out-of-line read-helper profile. On x64, a flat 32-bit-address-space
read table at 4 KiB granularity occupies 8 MiB per memory image. The current
top-level table occupies 32 KiB, plus 6 KiB per populated 1 MiB group: for a
contiguous 128 MiB mapping, 800 KiB of fast metadata. A heap-resident flat table
also retains a root-pointer load; its gains would come from removing the
group-selection branch, denser read metadata and invariant-root hoisting, not
from eliminating every dependent pointer load.

Correct replacement requires one owner for map/reset/copy/move rebuilding,
partial pages and protections, watch traps, deferred-write installation and
release, and observer restoration after snapshot replacement. Public escaped
read pointers are not a legal way to build a private flat cache: they prevent
deferred GPU ownership. Writes still need per-page epochs and alias-aware
observation; they cannot use an upstream-style raw writable table.

No flat-table implementation or performance improvement is claimed. The
evaluation identifies a memory-contract redesign, not evidence that flattening
the table alone closes the emulator performance gap.

### Exact hardware VFP design

The first candidate used operation-local SSE2 environments, not a guest-wide
floating-point mode. Each helper saved/restored the entire host MXCSR, masked
host traps, applied guest rounding and returned newly raised guest flags.
Callbacks and memory boundaries did not inherit guest FP state; VMSR/VMRS and
eager guest FPSCR updates needed no new context fields or ABI.

The fast domain is normal/zero binary32 inputs, safely bounded normal/zero
results and no host exception other than inexact. NaNs, infinities, denormals,
underflow/overflow boundaries and exceptional arithmetic use the unchanged
integer implementation. MLA/MLS and negative variants retain two rounded
operations and the union of their flags, never a fused FMA. Binary64, comparisons
and float-to-integer conversion retain the exact existing implementation.
The candidate enabled SSE2 for x64 Clang/MSVC with strict floating-environment
pragmas; other compiler/architecture combinations retained integer execution.

A standalone strategy probe executed 9,600,000 comparisons across six operations
and all 16 rounding/FZ/DN combinations. All 7,387,521 accepted hardware results
matched bits and exception flags; the complete host environment was preserved.
This qualifies the strategy, not the production integration or its speed.
Evidence: `hardware-vfp-strategy/receipt.json`.

Production-helper qualification then executed 19,200,000 comparisons against
the pinned instruction-level scalar oracle: 12 operations, all 16 control
combinations, boundary vectors and deterministic random operands. Result bits,
new exception flags and the complete host MXCSR match; incoming sticky FPSCR
flags do not contaminate the returned operation flags. The helper translation
unit was compiled without a command-line `/fp:strict` override, exercising its
own strict-environment pragmas. Receipt: `hardware-vfp-qualified/receipt.json`.

The rebuilt native architecture test passes rounding, non-fused accumulation,
intermediate underflow, FZ/DN, signaling NaN and signed/unsigned conversion
boundaries under a deliberately different host environment. The native process
memory-only suite also passes; the unrelated historical full-process fixture
limitation remains.

The FP-only title relink replaces exactly the scalar VFP archive member; the
other four support objects are byte-identical to the frozen control. Field and
Raw Copy again match all captured framebuffers and structural/state contracts.
Receipts: `hardware-vfp-title/receipt.json`,
`hardware-fp-only-field/equivalence.json` and
`hardware-fp-only-copy/equivalence.json`.

The isolated FP-only ABBA rejects this implementation despite correctness:
pre-backend 13.5698 to 19.2172 ms, paired ratio 1.41681 (individual ratios
1.38530 and 1.44832). The combined new-host/new-title variant also regresses:
paired pre-backend ratio 1.37301. Both campaigns preserve the same memory
fingerprint and 720 measured updates. These are within-campaign comparisons,
not a claim that the earlier A/A establishes an unchanged noise floor across
all later runs. Evidence: `cpu-performance.json`.

The combined host also passes both native scenes, but correctness cannot
qualify a performance regression. Its receipts are
`cpu-combined-field/equivalence.json` and
`cpu-combined-copy/equivalence.json`. The operation-local MXCSR implementation
is not adopted. Its exact semantic boundary cases remain useful regression
guards for any future backend.

### Contained-function evaluation

The executed analysis now uses the generator's normalized functions and actual
terminal emitter, rather than instruction-address containment alone. Of 429
candidates, 52 would change native control transfer. For example, child entry
`0x0016ED70` inside `0x0016ECD4` returns a branch to `0x0016ED8C` at block
`0x0016ED78`; merging into the container turns that exit into an internal goto.
That changes unwinding/dispatch behavior, not just code duplication.

The remaining 377 candidates cover 26,055 of 963,498 instruction occurrences
(2.70%). They pass this control-shape screen but are not proven equivalent for
liveness and observable exits. Nor is that percentage a binary-size or CPU
gain: native prologues, register pressure, ICF sharing and hot layout differ.
No containment rewrite is adopted. It cannot safely be used as an automatic
offset for the hot-path code-size increase.
Evidence: `containment-contract.json`, with per-candidate rejection details.

### Dynarmic static-emission decision

The read-only donor is `azahar-emu/dynarmic` at
`526227eebe1efff3fb14dbf494b9c5b44c2e9c1f`.
Its [x64 emitter](https://github.com/azahar-emu/dynarmic/blob/526227eebe1efff3fb14dbf494b9c5b44c2e9c1f/src/dynarmic/backend/x64/a32_emit_x64.h)
takes `BlockOfCode`, runtime configuration and a `Jit*`; emitted blocks contain
process-local code pointers, runtime patch records and exception-handler state.
That is not a COFF/ELF static-object interface. Persisting those pointers or
invoking the JIT during installation would violate the precompiled release
contract. A static backend needs explicit symbols/relocations and our complete
observable-state and memory contracts; no donor runtime path is integrated.

The [donor pipeline](https://github.com/azahar-emu/dynarmic/blob/526227eebe1efff3fb14dbf494b9c5b44c2e9c1f/src/dynarmic/backend/x64/a32_interface.cpp)
disables Get/Set elimination when memory-access halt observation is enabled.
The same five-block frontend experiment was therefore executed without that
pass: 432 IR operations become 356, GPR reads 106 become 105 and all 56 GPR
writes remain. The 18 memory reads and 61 writes remain unchanged. This sharply
limits the earlier 106-to-57 register-read result under the stricter boundary.
Receipt: `observable-frontend/results.json`.

Both frontend probes use only CPSR `0x10` and FPSCR `0`. FPSCR control bits are
part of Dynarmic's location identity, so neither probe qualifies runtime VMSR
mode changes, a whole-title static backend, or machine-code performance.
Decision: retain the verified static C++ baseline for now. Neither a static
Dynarmic wrapper nor the slower exact hardware VFP candidate is adopted.
No new donor code or runtime dependency is shipped.

### Structural diagnosis requested by the user

The earlier clean benchmark's `guest_seconds` is not exclusive AOT CPU time:
its declared scope includes GSP/PICA, and
`thread_cpu_ledger.available_in_build` is false. The intrusive RIP profile also
contains 1,988 unresolved host samples out of 5,397 total; its 1,971 title-module
samples are not a complete attribution of the main thread or exclusive time.
This is a methodology gap, not evidence that fidelity explains the difference
from the emulator.

A new executed analysis follows sampled function `0x004A0338` in the exact
profiled control image. Its 143 ARM instructions, 16 basic blocks and 15
dispatch entries occupy a 9,840-byte native symbol with 2,420 static x86
instructions and 117 call sites: 16 block-entry, 51 classified memory-read,
22 classified memory-write and 28 other calls. The full symbol is disassembled,
not the earlier truncated 8 KiB window. These counts include cold paths and
are not dynamic instruction counts or a whole-title speed prediction.
Evidence: `structural-diagnosis/execution-shape.json`.

The inspected emulator configures Dynarmic's page table and leaves
`all_safe_optimizations`, `unsafe_optimizations = false` and
`check_halt_on_memory_access = false` at their defaults. Its
[`BlockOfCode` FP transitions](https://github.com/azahar-emu/dynarmic/blob/526227eebe1efff3fb14dbf494b9c5b44c2e9c1f/src/dynarmic/backend/x64/block_of_code.cpp)
switch MXCSR at guest entry/exit rather than around every scalar arithmetic
helper. The execution model is materially different; merely substituting an
SSE instruction inside the existing helper ABI does not reproduce it.

The existing exclusive ledger was enabled in a separate developer host, not
in the public/default build. It also exposed a real coverage defect:
`NativeA32Process::InvokeFunctionWithState` did not enter the AOT/SVC phases,
so synchronous guest work inherited its caller's phase. The owning method now
uses the same scopes as normal `Run`. No caller signatures or behavior change.
A real generated-module invocation reproduces correct guest results but zero
AOT intervals before the fix; after relinking the process library it has one
AOT interval and restores the caller phase. Evidence:
`synchronous-cpu-before/receipt.json` and `synchronous-cpu-after/receipt.json`.
This is a diagnostic correction, not a speed optimization.

The corrected Field run measures 720 frames after 180 warmup frames:

| Exclusive phase | Wall ms/frame | Thread cycles/frame |
| --- | ---: | ---: |
| AOT | 6.9291 | 16,889,928 |
| PICA frontend | 0.7435 | 1,780,461 |
| PICA plan | 1.0795 | 2,632,502 |
| PICA queue | 0.6571 | 1,601,550 |
| Vertex capture | 0.2900 | 690,363 |
| Texture capture | 0.1064 | 221,229 |
| Queue lock | 0.0489 | 94,536 |
| Visual presentation/backend execution | 3.7065 | 9,034,425 |
| Renderer start | 1.5829 | 265,176 |

The whole window is 16.6615 ms/frame wall and 14.8003 ms/frame main-thread CPU.
AOT accounts for 45.82% of main-thread cycles and 62.30% of pre-NRI cycles;
PICA/queue/capture phases total 2.9254 wall ms/frame. Phase wall times are not
exclusive CPU seconds, and cycles must not be converted to seconds using an
assumed frequency. The pre-NRI CPU bound is 9.2841–11.1897 ms/frame; its
off-CPU upper bound includes preemption, not only deliberate waits.

The inactive-probe control measures 10.8585 ms pre-backend, versus 11.2216
with the original ledger and 11.1926 with corrected coverage. These are
single diagnostic windows, not ABBA proof of a speed change. All three have
identical memory content/state, process state, frame/refresh count, 85,500
draws, 1,800 transfers and empty queues. Both active runs have 87,297 AOT
intervals: the synchronous-call attribution defect does **not** explain this
Field workload's performance gap.

Evidence: `structural-diagnosis/qualification.json` and
`structural-ledger-{off,on,fixed}/measurements.json`. The corrected diagnostic
host SHA-256 is
`a3cea39ab29160af8e25db4e3bff353021755d31fa1237aae9e48ff7af3fea94`.
`TriAevum.exe` remains unchanged at
`1176593d834f57b2313a5c4c36a830ff1bec7140ec1d50914752cda45335742f`.

Conclusion: the generated execution model is expensive, but it is not the
entire measured frame. The evidence supports investigating register/state,
memory and FP boundaries at execution-region granularity, with the same
captured guest invocations, rather than treating helper substitution as a
backend optimization. A matched native-code/cycle comparison must precede
another whole-title variant. It must preserve observable exits and compare
the same guest work, not assume that emulator FPS and presented-frame FPS are
identical units. No quantified emulator/AOT speed ratio is claimed here.

Final qualification: the restored baseline plus the diagnostic correction
passes native architecture/FP, memory-only process, PICA submission and ledger
checks; 36 generator and nine source-archive tests; three CPU-budget analyzer
tests; and all seven real generated-DLL boundary checks. Field and Raw Copy
again match all five framebuffer captures and structural/state contracts:
`cpu-diagnostic-final-{field,copy}/equivalence.json`.
The known unrelated full-process fixture limitation remains unchanged.

### Evidence retirement

After qualification, 22 owned one-off drivers/probe sources were archived as
`cpu-hotpaths-development-drivers.zip` and removed from the active work tree.
`development-cleanup.json` records every member checksum and verified archive
integrity. Receipts, exact commands, frozen binaries, disassembly, captures and
the unpromoted experimental patch remain private. No user input, unrelated
development file or public package was removed or replaced.
