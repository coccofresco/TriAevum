# 303-function AOT cohort

2026-09-17. Developer-only extension of
[the closed-family experiment](TRIAEVUM_AOT_CLOSED_FAMILY_EXPERIMENT.md).
This is specialization of translated AOT groups, not replacement with semantic
decompiled gameplay. No production executable, renderer or release was changed.

Follow-up: [actual call-chain activation and its performance limit](TRIAEVUM_AOT_LIVE_COHORT_ACTIVATION.md).

## Scope and selection

The existing three field stack profiles contain observed callers whose eligible
closed groups cover 432 distinct functions. `select_aot_cohort.py` selects from
these callers, prioritizing additional observed stack samples, then additional
distinct functions. It stops only after the requested minimum is reached.

The resulting cohort contains **303 distinct functions, 106 entry roots**:
58 roots have multi-function closures and 48 are call-free operations. This is a
union of groups, not one call that necessarily executes 303 functions. Shared
callees are emitted once. No unobserved root is added just to reach the quota.

Representative groups:

| Root | Role / recovered name | Functions in its closure |
| --- | --- | ---: |
| `002BB780` | Entity sphere/wall collision | 30 |
| `002CF684` | Recovered bounded group, semantic name still absent | 25 |
| `003723C0` | Complete line-test caller | 22 |
| `00402E1C` | Recovered bounded group, semantic name still absent | 18 |
| `003FAD68` | CMB render-state preparation | 15 |
| `00368704` | Actor model render-state binding | 14 |
| `003FA34C` | Fallback-light packing | 13 |
| `00338AC8` | Camera target calculation | 12 |
| `002C1CE4` | Entity ceiling collision | 10 |

The selection additionally includes pose transforms, uniform/packet preparation,
matrix/vector operations and audio processing. Names describe static membership,
not successful activation: some material/UI groups still have live observers.
All included bodies derive from the same hashed program/code pair as the frozen
reference. Existing native external owners are excluded using its manifest.

Deduplicated sampling support is 1,071/2,214 AOT observations (48.4%). This includes
roots subsequently rejected by dynamic guards. It is **not** CPU time removed,
dynamic function coverage, or evidence of a 20% total pre-NRI improvement.

## Implementation

- `select_aot_cohort.py`: reusable deterministic selection, transitive closure,
  observed-root restriction, manifest/program/code hashes and private JSON report.
- `build_aot_family_probe.py`: consumes that hashed selection, checks its exact
  closure, compiles one shared module, enforces a total distinct-function budget.
- Observer checks are now **per root's transitive closure**. An observer belonging
  only to another root no longer disables this root. Interior observers remain
  authoritative; they are not suppressed to increase coverage.
- Declared tail calls are followed inside the shared module with the same promoted
  state and block budget. Only targets explicitly present in the closed graph are
  dispatched. Other branches, faults and limits remain exits. A return address
  inside the root's closure conservatively delegates to the original.
- `--coverage` creates a separate diagnostic build with per-function entry counters.
  The timing build has no increments or branches for those counters.
- `aot_invocation_probe.cpp` / `run_aot_invocation_probe.py` support a batch of roots
  in one game process. Each root gets one A/B pair on private state/memory copies.
  Batch results are correctness evidence **only**, never performance measurements.
  The existing single-root 16-trial ABBA mode remains available for timings.
- A visited function counts only if the enclosing root comparison passes, memory
  remains unchanged in the live game, and the candidate records a real root hit.
  Fallback, incomplete reports, mismatches and uncaptured roots are separate.
- A small lifecycle file distinguishes proxy loading, first execution and missing
  captures. Diagnostic processes retain the existing bounded cleanup behavior.

Build times on this machine: initial non-instrumented cohort 11.61 seconds;
tail-aware instrumented cohort 13.63 seconds; final non-instrumented cohort
12.34 seconds. No whole-AOT rebuild, full host build or public object-cache change.

## Verification and discoveries

Initial batch: 84 matching candidate roots, 165 distinct visited functions,
18 rejected roots and four not captured. Four rejected candidate calls exposed a
real limitation: the prior family wrapper returned at a tail-call branch instead
of completing the operation. The affected roots were `0034338C` (memcpy),
`00368704` (model state), `00466EA0` and `00466F00` (light uploads).

The general tail-call composition above fixes all four in a dedicated real-input
batch. The complete instrumented repeat, at occurrence 2, then reports:

- **88 matching candidate roots**, each compared with the frozen original DLL.
- **171 distinct functions visited inside successful candidate comparisons**.
- 14 excluded roots: 12 conservative observer-related fallbacks, plus two UI
  roots whose reference invocation reaches an observer and cannot be privately
  replayed by this harness. No candidate mismatch remains among captured roots.
- Four roots not reached at the requested occurrence: `002D4AD4`, `003222DC`,
  `0032D474`, `003FA34C`.

Thus **303 compiled does not mean 303 verified in execution**. The measured dynamic
coverage is 171/303 (56.4%), on these inputs; the other 132 are not independently
qualified, and traversing a function does not cover all of its branches.

The final build **without coverage counters** was also tested in the full batch at
occurrence 2: the same 88 roots pass, with the same 14 exclusions and four missing
roots. Its zero visit counts are expected because entry instrumentation is absent;
the 171-function coverage figure comes only from the separate instrumented build.

Comparisons include GPR/VFP state, CPSR/FPSCR, memory contents/write generation,
exit result and consumed blocks. Completed runs preserve the established live
memory-content, memory-state and process-state fingerprints:
`14004724930905175187`, `16777719094724974201`, `11508416489629890540`.
This establishes replay noninterference, **not activation in ordinary gameplay**.
No framebuffer or Linux validation was performed for this developer DLL.

Two full-repeat attempts timed out before producing any capture. They were killed
by the harness and excluded. The tail-only run and later lifecycle-instrumented
full repeat completed. The cause of those pre-capture stalls is not established;
do not attribute them to AOT arithmetic or count them as successful tests.

The diagnostic Python suite has 39 passing tests. New tests cover observed-only
selection, deduplicated support, no quota padding, and rejecting fallback,
mismatch, live-state mutation and partial output from credited batch coverage.

## Reproduce and continue

1. Run `select_aot_cohort.py` with `--program`, `--code`, `--manifest`, repeated
   `--analysis`, `--minimum 300`, and a private `--output` JSON.
2. Run `build_aot_family_probe.py` with that `--selection`, `--max-functions 320`,
   `--optimized`, normal compiler/support/input arguments and a new private output
   directory. Add `--coverage` only for entry coverage, not performance.
3. Run `run_aot_invocation_probe.py` with the same `--selection`, `--occurrence 2`,
   frozen `--baseline`, partial `--candidate` and the established field invocation.
   Inspect `batch.json` and individual `invocation.csv.<decimal-entry>.csv` files.
4. Keep observer-sensitive groups excluded until their owning contracts support
   private replay. Broader inputs or additional safe roots can extend dynamic
   coverage without falsifying counters or disabling native hooks.
5. Before production promotion, qualify budgets/fault paths/platform behavior and
   actual call-chain activation; use non-instrumented repeated whole-game A/B for
   net benefit. Neither batch timing nor the previous local line-test improvement
   establishes a speedup for this entire 303-function cohort.

Private evidence root: `C:/Users/xander/triaevum-aot-causal-analysis/`.
Selection: `cohort300-selection.json`. Builds: `cohort303-opt`,
`cohort303-coverage`, `cohort303-coverage-v2`, `cohort303-final`.
Runs: `cohort303-batch1`, `cohort303-tail-v2`, `cohort303-batch2-traced`,
`cohort303-final-batch2`.
Excluded timed-out runs: `cohort303-batch1-v2`, `cohort303-batch2-v2`.
Each successful build/run has input and binary hashes; generated title code,
memory, save data and executables remain private. External decomp was not modified.
