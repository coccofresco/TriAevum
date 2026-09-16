# AOT hotspot diagnosis - 2026-09-16

## Scope and result

Goal: reduce the CPU cost of each independent native update, including original
graphics/GSP/PICA preparation and excluding NRI/Vulkan execution. 200 updates/s
means a 5 ms budget, not counting interpolated frames or accelerating gameplay.

This investigation identifies concrete costs INSIDE the compiled AOT module.
It does not claim an implemented speedup. No title logic, renderer, audio,
callbacks, guest timing or published module was changed. The independent AOT
worktree and external decompilation were read only.

The strongest candidates are memory-access helpers, per-block callback routing,
and scalar floating-point helpers. Merely increasing compiler optimization,
inlining everything, or changing indirect dispatch is not the indicated fix.

## Evidence and reproducibility

Added `tools/renderer/tev_program/sample_aot_windows.py`, a bounded user-mode
x64 instruction-pointer sampler. It briefly suspends only threads belonging to
the explicitly selected process, always resumes them in a finally block, and
chooses the thread with the largest cycle increase since the previous poll.
It does not require the WPR profiling privilege, which was unavailable.

IMPORTANT measurement limits:

- This is intrusive instruction-pointer observation, NOT ETW/PMU exclusive CPU
  timing. The percentages below are shares of samples **inside the DLL**, not
  shares of total frame time or attainable speedups.
- Thread selection, scheduler timing and polling can bias observations. Two
  matching stationary runs are useful evidence, not coverage of the whole game.
- Shared helpers can be folded by the linker. An EnterBlock symbol containing
  a particular function name does NOT attribute that cost to that function.
- Translated-body samples include any inlined memory/float work. Helper costs
  are therefore not a complete classification of all memory/float activity.
- The symbol-bearing DLL is diagnostic, not a replacement performance baseline.
  A link map must belong to precisely the module supplied to the sampler.

Fixture: adult-Link field checkpoint used by the existing AOT benchmark,
neutral input, native 30 Hz simulation delta, throughput mode, no interpolation,
no VSync, no limiter, no screenshots. Each steady run: 4,200 updates, first 180
excluded from timing, sampler delayed 10 seconds and active for 25 seconds.
Both warm runs report zero SPIR-V compilations. No shader preparation is used
to explain their AOT samples.

Private artifacts, not release payloads:

| Evidence | Location |
| --- | --- |
| Initial cold diagnostic | `I:/TriAevum-public/aot-native-sampling-20260916/0/` |
| Two steady diagnostic runs | `I:/TriAevum-public/aot-native-steady-20260916/{0,1}/` |
| Unmodified DLL control | `I:/TriAevum-public/aot-native-baseline-check-20260916/0/` |
| Symbol DLL and map | `I:/TriAevum-aot-lab-debug/plugin/` |
| Original invocation | `I:/TriAevum-aot-lab-runs/host-ab2/0-shipped/command.json` |

Symbol DLL SHA256:
`2e91d80b432354886fd89145c18ef4118d6c655066c26d973786e3ca78c7a38c`

Link map SHA256:
`f9dc158e1d0b29713de291a5bfc30698114a8afa0fa4a6c5edec29573523e453`

Both 4,200-update diagnostic runs and the control end with memory fingerprint
`14624622702799942933`. This checks consistency of this workload, not complete
semantic equivalence. All runs finished normally and were closed.

The control reports 104.94 whole-loop FPS and 7.73 ms/update of pre-backend host
envelope (7.50 ms accounted). These are different scopes: 104.94 includes the
renderer; 7.73 ms is about 129 updates/s capacity before it. Do not compare the
earlier loaded-machine 40 FPS runs against this as an optimization result.

To repeat, use a NEW output directory; the benchmark copies writable configs
and save data. Reuse/warm its private shader cache before interpreting steady
samples. Do not use a sampled run as a clean A/B timing result.

```powershell
python tools/renderer/tev_program/measure_prebackend.py `
  --invocation I:/TriAevum-aot-lab-runs/host-ab2/0-shipped/command.json `
  --executable I:/TriAevum-public/ui-dependencies/bin/TriAevum.exe `
  --output I:/TriAevum-public/aot-investigation-new `
  --runs 2 --frames 4200 `
  --diagnostic-plugin I:/TriAevum-aot-lab-debug/plugin/triaevum_title_aot.dll `
  --sample-map I:/TriAevum-aot-lab-debug/plugin/title.map
```

## Ranked observations

5,468 and 5,467 total observations; respectively 2,618 and 2,581 inside AOT.
5,467 observations in each run were on the dominant execution thread; the
remaining single observation in the first run was another thread.

| Category inside AOT | Run A samples/share | Run B samples/share |
| --- | ---: | ---: |
| ReadFast helpers | 712 / 27.20% | 675 / 26.15% |
| WriteFast helpers | 141 / 5.39% | 141 / 5.46% |
| EnterBlock routing/accounting | 380 / 14.51% | 357 / 13.83% |
| Scalar float helpers | 426 / 16.27% | 404 / 15.65% |
| Whole-AOT dispatch helpers | 63 / 2.41% | 84 / 3.25% |
| Translated bodies, including inlined helpers | 885 / 33.80% | 909 / 35.22% |
| Other/unresolved | 11 / 0.42% | 11 / 0.43% |

### 1. Repeated scalar guest memory access

Owner: `tools/oot3d/native_game_runtime/oot3d_native_a32_memory.h`,
`ReadFast<T>` / `WriteFast<T>`; generated loads/stores in
`tools/oot3d/native_a32_runtime/whole_aot_cpp.py`.

The native disassembly of `ReadFast<uint32_t>` (diagnostic RVA `0xA1A0`)
contains a real function prologue/epilogue, page-boundary check, two dependent
page-table loads, null checks, scalar load, result-pointer store and diagnostic
flag test. The caller then loads the result and checks success. This sequence
repeats for adjacent elements and guest stack accesses. The most sampled
instructions include the scalar result store and function boundary; this does
NOT prove DRAM latency is the cause.

The old entry-frequency profile reports about 523,426 guest memory instructions
per update. That explains why modest per-access machinery can dominate. It is
a separate earlier profile, not an instruction count derived from RIP samples.

Best targeted experiment: guarded span/page reuse for proven contiguous loads
and stores in closed regions, starting with a few measured hot loops. Resolve
the mapping once, then perform exact scalar operations through that span.
Keep the slow path for crossing pages, permission/fault cases, tracing, unknown
aliasing or remapping. Preserve partial-write/fault order and write-generation
semantics; invalidate cached pointers at observable calls that can change maps.

Do NOT repeat global force-inline or the single flat-heap window experiment:
the parallel lab already measured those as neutral/worse in-game. Fewer repeated
translations per region is distinct from merely making each translation inline.

Useful pilot loop entries are `0x004A022C` and `0x004A0338`: fixed-length 160-
element loops with many indexed reads, writes and guest-stack temporaries.
Read-only decomp evidence is in
`I:/oot3decomp/src/runtime/owner_runtime/z_owner_runtime_accel_long_tail_14.c`.
Its generic semantic labels do not establish a gameplay owner or justify blindly
substituting those C bodies. The emitted ARM semantics remain the differential
oracle. Mesh command and transform routines are additional pilot candidates.

### 2. Callback membership work at every basic block

Owner: `oot3d_native_whole_aot_runtime.h`, `Oot3dAotEnterBlock`,
`Oot3dAotShouldNotifyBlock`, `Oot3dAotBlockEntryFilter`.
Emitter: `_render_function` in `whole_aot_cpp.py`.

The existing filter is a 4,096-bit hash bitmap followed by exact binary search.
Every basic block decrements budget, increments consumed count, checks callback
state, hashes the PC and sometimes searches the hook list. The translated body
contains an out-of-line call for this even for very small blocks.

The shared EnterBlock implementation has diagnostic RVA `0x6F90`. In its exact
search range `0x700C..0x705C`, the two steady runs captured 122 and 105 samples:
roughly a third of this helper's samples. This proves search executes in the hot
path; it does NOT yet quantify how many searches are hash false positives.

Best targeted experiment: exact sparse instruction-PC membership pages, or
region-specialized hook tests, to avoid hashing plus binary search for every
block. Keep title addresses in the adapter. Preserve dynamically requested
hooks, first-block suppression, all-block tracing, callback state flush/reload,
and exact block-limit resume behavior. Do NOT disable hooks, profiling contracts
or budgets to manufacture a speedup.

This differs from the lab's already-unhelpful EnterBlock force-inline experiment:
the objective is less membership work, not the same work duplicated everywhere.

### 3. Software floating-point rounding on common arithmetic

Owners: `oot3d_native_a32_vfp_ops.cpp` and pinned
`upstream/recomp/a32_vfp_scalar.cpp`: `F32Add`, `F32Mul`, `RoundFinite`,
multiply-accumulate and conversion helpers.

RoundFinite alone has 193 and 184 observations. Together the float helpers
account for approximately 16% of DLL observations. Guest floating-point is
being implemented through substantial integer/rounding machinery, not just a
native host add/multiply.

The independent lab's guarded hardware path (`2c3a755`, documented at `f788150`)
was bit/exception tested but did not demonstrate a statistically convincing
game speedup. Therefore do not promise its microbenchmark gain in this scene.
First measure per-operation eligibility/rejection and actual FPSCR modes;
then expand only the frequently reached exact cases. Preserve denormals, NaNs,
rounding modes, signed zero, accumulated exceptions and non-fused multiply-add.
Neither fast-math nor deleting FPSCR behavior is acceptable.

### 4. Shared architectural state and oversized translated regions

`_render_function` currently promotes GPRs as references to `state.R[]`, not
independent host values across the complete region. Generated callback and
memory helper boundaries limit LLVM's ability to retain those values. Native
disassembly of `0x004A0338` shows repeated shared-state loads/stores.

The remaining body sample share is not all useful arithmetic, but this run
cannot separately quantify state traffic. Inspect optimized IR and assembly for
the selected memory-region pilots; promote live values within those regions and
flush only at required observable exits. Do not restart an all-function register
rewrite: the previous local-register microexperiment was not a game-level win.

### Not the first priority: top-level dispatcher

Only about 2-3% of the observed DLL positions are in named top-level dispatch
helpers. Calls and transitions can also cost time elsewhere, so this is not an
absolute upper bound. Still, a wholesale dispatcher rewrite is less supported
by present evidence than memory-region and callback-membership work.

## Execution order and acceptance

### Subsequent evidence from the independent lab

After the original diagnosis, the lab reported experiments 10 and 11 in its
`docs/TRIAEVUM_AOT_OPTIMIZATION_RESULTS.md` (read-only evidence): a per-block read
page cache increased code size by 27% and median process CPU by 3.4%; a 64 KiB
open-addressed exact hook table increased median process CPU by 7.4%. Both were
reverted there. Those specific implementations should not be repeated. The
measurements reject those variants; they do not prove that memory latency is
the root cause or that every possible region/filter representation is optimal.

The next isolated experiment split `ReadFast` fault and diagnostic
handling into non-inlined tail-call targets. It adds no state and does not change
the page table, membership filtering, floating-point behavior or module ABI.
The ordinary 32-bit load probe compiles without a stack frame or nonvolatile
register saves, unlike the previous helper. 2,376 comparisons against checked
reads cover 8/16/32/64-bit loads, mapped/read-only/partial/unmapped pages,
unaligned and crossing accesses, wraparound, tracing on/off, fault addresses,
unchanged destination on failure, null destination, and unchanged write state.
This was a code-generation/correctness result, not a speedup. The production
header was restored after the in-game result below.

Build/reproduction tool: `build_aot_header_experiment.py` next to the benchmark
runner. It builds an existing generated source set against this repository's
headers in a private cache. `measure_prebackend.py --comparison-plugin ...
--baseline-plugin ... --runs 8` selects A-B-B-A order and records process CPU
time including startup as an additional metric. No runtime profiling or
instruction-pointer sampling is permitted in this comparison mode.

The chosen generated source set includes the lab's earlier consumed-compare
fusion. Its matched existing control is plugin `330145eb...`, SHA256
`30c1ae5c28526ef6e7821242bfa01ee428cd02768114845ba25a4ce8756fde79`;
using only the older shipped module would confound the two changes.

### Cold-read split: completed, not retained

Private evidence: `C:/Users/xander/triaevum-aot-cold-read/`.
The candidate header is preserved as `candidate-memory.h`, the compiled module
and map in `plugin/`, and eight runs in `abba/`. These are developer experiments,
not user installation or release inputs. The working game module was never
replaced.

- All eight runs completed 900 native updates with the same final memory
  fingerprint `15598764887955759453`; 720 updates/run were timed after warmup.
- No interpolation, VSync, limiter, runtime profiling or RIP sampling.
- Shared warmed private shader cache; separately copied writable save/configs.
- Candidate size 87,911,936 bytes versus matched control 86,794,240 (+1.29%).
- 257 C++ units compiled in 374.6 seconds at two workers, followed by ThinLTO.
  A smaller probe is not a substitute for this full-module game verification.

Candidate/control ratios below use adjacent pairs with reversed execution order
in every second pair. Lower is better. The CPU metric includes startup; guest
and pre-backend metrics use only the post-warmup window.

| Pair (control, candidate) | Process CPU | Guest time | Pre-backend envelope |
| --- | ---: | ---: | ---: |
| 0, 1 | 1.2834 | 1.3080 | 1.3144 |
| 3, 2 | 1.0207 | 1.0323 | 1.0362 |
| 4, 5 | 0.9345 | 0.9484 | 0.9527 |
| 7, 6 | 0.9925 | 0.9560 | 0.9664 |
| Median | **1.0066** | **0.9941** | **1.0013** |

The host load visibly changed after the first run: even control throughput fell
from 92.3 to approximately 70 FPS. Do not cherry-pick the last pair's apparent
gain or the first pair's loss. This batch does not establish an improvement.
Decision: **do not promote; restore the original ReadFast implementation**.
Keep correctness tests and the reproducible comparison tools. In particular,
do not label the ordinary helper's simpler assembly as a game-level speedup.

### Updated direction

The helper-level variants have not recovered the roughly 2.7 ms/update required
by the quiet control's 5 ms pre-backend target. The next experiment must remove
repeated work across a substantial region, not just rearrange the same scalar
accesses or introduce another cache at each access. Two distinct candidates
remain, neither is implemented or performance-qualified by this tranche:

- A bounded hot-loop kernel using resolved spans once per invocation, with an
  exact fallback for insufficient block budget, interior callbacks, tracing,
  aliasing, faults and unexpected mappings. Begin with a small cohort rather
  than the rejected global 82,000-site page-cache rewrite. Final architectural
  state and observable guest-stack writes must also match, not just output data.
- A host-owned direct guest-memory mapping with a versioned module contract,
  eliminating the translation walk itself. This is a coordinated memory/ABI
  change, not the already-rejected per-access flat-window check. Protection,
  remapping, savestates and host/PICA access must remain coherent on all targets.

Do not restart the rejected all-site page cache, 64 KiB hook table, force-inline
or cold-read split without genuinely new evidence. The hot-loop and mapping
proposals require their own measured pilots; a 200-update/s result is not proven.

1. Pilot guarded memory-region lowering on a small hot cohort; compare exact
   state, memory, faults, callbacks and budget exits with the existing module.
2. If the bounded region pilot cannot recover meaningful time, evaluate the
   host-owned mapping contract before another all-site helper rewrite.
3. Measure VFP fast-path eligibility before expanding it. Integrate work from the
   parallel AOT agent rather than duplicate its experiments.
4. Only then combine retained improvements; inspect real assembly and repeat the
   same checkpoint plus another workload with different gameplay/graphics load.

Each experiment needs unsampled alternating A/B runs on the same executable,
cache, settings and machine conditions, reporting guest time, whole pre-backend
time, p95, code size and final fingerprints. Reject gains that vanish outside a
microbenchmark. Keep audio and original graphics active. Compile only the pilot
shards/support first; a full AOT rebuild is justified only for retained changes.

Current control leaves about 2.73 ms/update to remove to reach a 5 ms pre-backend
envelope in this fixture. The sampled categories identify where to attack that
gap; they do not prove it can all be removed. The PICA preparation costs from
`TRIAEVUM_NON_RENDERER_200FPS_ASSESSMENT.md` remain additional, separate work.

## Call-Free Region Pilot (2026-09-16)

The emitter now accepts repeatable `--callback-free-region 0xENTRY` options.
Default output remains unchanged. The pilot selects `0x004A022C` and
`0x004A0338`, not a title-specific replacement implementation.

For a closed call-free CFG, a wrapper tests the sorted callback-PC interval once
per invocation/resume. When no callback can apply, the identical translated
instructions execute with `Oot3dAotConsumeBlock`, without repeated hook lookup,
architectural flush/reload or an `EnterBlock` call. Any possible hook, including
full tracing, selects the original body. Calls, SVC and external CFG edges are
rejected. Budget counters remain updated per block; this pilot does NOT defer
observable state or bypass checked memory, writes, faults, flags or stack stores.

Validation tools:

- `tools/oot3d/native_a32_runtime/test_whole_aot_cpp.py`: 36 tests, including
  opt-in isolation, cache identity, unknown entries and rejected observable edges.
  Full `test_whole_aot*.py` discovery: 60 tests passed.
- `run_region_probe.py` / `region_probe.cpp`: 624 compiled differential cases
  against the original body. Covers budgets 0..12, loop/return/invalid entry,
  memory faults, write fingerprints, no/full/sparse hooks, register/flag-mutating
  callbacks and first-block suppression. These are synthetic correctness tests,
  not in-game performance evidence.
- Generated pilot changes only shards 243/244 relative to the frozen original
  12,439-function source set (affinity placement, 256 shards). Headers and all
  other source hashes match. Reused 255 objects, compiled 2 in 9.10 seconds;
  ThinLTO linking is separate from that timing.
- `build_aot_header_experiment.py --object-cache ... --max-new-objects 2`
  prevents an accidental full compilation when iterating this emitter-only pilot.

Private artifacts: `C:/Users/xander/triaevum-aot-region-probe/`.
The installed title module is not replaced. The in-game pilot does not establish
a gain and is NOT enabled in the normal build.

Reproduction (developer-only): generate from the frozen `aot_program.json`,
matching code/selection, with `--shards 256 --shard-strategy affinity
--callback-free-region 0x004A022C --callback-free-region 0x004A0338`.
Use the same runtime headers and compiler as the baseline, then compare with
`measure_prebackend.py --baseline-plugin <control> --comparison-plugin <pilot>
--runs 8 --frames 900`. Do not compare against the other lab's CMP-fused module:
this pilot uses the original source set. The original archive
`1110f696...` and pilot archive `4f9206f9...` share exactly 255 object identities.

Eight real-game ABBA runs completed: 900 updates each, 180 warmup and 720
measured, same checkpoint, no interpolation/limiter/vsync/capture/profiler.
All final memory fingerprints: `15598764887955759453`.
Paired candidate/control ratios, pairing (A0,B1), (A3,B2), (A4,B5), (A7,B6):

| Metric | Pair ratios | Median ratio |
| --- | --- | --- |
| Process CPU, including startup | 1.0614 / 0.9877 / 1.0188 / 1.0113 | 1.0151 |
| Guest measured time | 1.0330 / 0.9609 / 1.0152 / 1.0052 | 1.0102 |
| Pre-backend envelope | 1.0327 / 0.9649 / 1.0174 / 1.0104 | 1.0139 |
| Pre-backend p95 upper bound | 1.0341 / 0.9775 / 1.0233 / 1.0000 | 1.0116 |

No demonstrated benefit: the median is slightly worse, with pair-to-pair noise.
These measurements do not establish a statistically precise regression size.
Pilot DLL: 86,876,672 bytes (+16,896), SHA-256
`255868fb2a71524c41e002eb52c266271641d01b2e52f76a0507ae7447661924`.
Raw runs: private `abba/measurements.json` and per-run `runtime.json`.

This rejects hoisting callback membership alone as a retained optimization for
this cohort. It does not test direct host memory windows, elimination of repeated
guest-address translation, or local register ownership across a complete loop.
Those would be separate changes, not benefits to attribute to this pilot.

A separate intrusive 4,200-update run confirms the fast bodies really execute:
44 RIP observations in `004A0338_Unobserved`, 25 in `004A022C_Unobserved`.
No observations in their observed bodies (not proof of zero executions).
Final fingerprint `14624622702799942933` matches the earlier 4,200-update control.
Among 2,478 observations inside the DLL, 668 land in read helpers, 143 in write
helpers, 343 in block-entry helpers and 382 in scalar floating-point helpers.
These are sampled locations, not exclusive timings. Do not use this diagnostic
run's FPS as performance evidence. The unchanged broad profile and unsuccessful
ABBA pilot argue against expanding callback-only specialization to all functions.
Next region work must address repeated memory translation and/or architectural
register reloads together, with exact fault, write, callback and budget contracts.

## Initial Profiling Checks

- Four unit tests cover link-map image-base handling, folded aliases, invalid
  maps and helper classification; all pass.
- Three diagnostic game runs completed without sampling API errors.
- Two steady runs plus unmodified-module control matched final memory state.
- Runtime binaries and AOT output were not rebuilt or changed for profiling.
- Existing diagnostic results predate automatic hash/category fields added to
  the sampler; the hashes and aggregated counts are recorded above. Raw RVAs,
  symbols and counts remain in their `native-samples.json` files.
