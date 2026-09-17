# Direct AOT cohort integration

Developer experiment, 2026-09-17. Follows
[live cohort activation](TRIAEVUM_AOT_LIVE_COHORT_ACTIVATION.md).
The shipped executable, renderer, module and defaults are unchanged.

**Decision: do not promote or expand this implementation on the strength of the
current results.** Direct execution works in the tested scene, but the two clean
ABBA series do not show a repeatable total pre-NRI reduction.

## Boundary

`tools/renderer/tev_program/build_aot_direct_cohort.py` emits a private module
with direct entry wrappers in selected existing AOT shards. A specialized closed
family shares the caller's frame, promoted registers, memory, statistics and
remaining block budget. It does not create an ABI invocation, unwind the original
stack, refund a block, or return through the host dispatcher to enter the family.

The root-specific observer guard checks every member's address interval. Full
tracing and active observers inside the family use the observed implementation.
Only exact root entries can select the specialization; internal resume PCs use
the observed implementation. The callback is restored by RAII on normal returns,
budget exhaustion and exceptions. Budget/fault exits are never retried.
Declared tail calls remain inside the local family. Existing native external
owners, indirect calls, recursion and memory/system observers are excluded.

Unlike the earlier ABI replay adapter, this is already inside the native caller:
there is no synthetic stop-PC/return-to-host contract. No game callback is invoked
with the private replay module's TLS. This prototype does not substitute the
triangle kernel: it measures callback-free group lowering alone.

## Sparse build and provenance

- Reuse object files only after verifying their hashes, source cache keys,
  compiler identity, common dependencies and program/code identity.
- The sole admitted header difference is the existing layout-neutral memory-view
  friend declaration. No memory-region specialization is enabled by this tool.
- Require an existing populated developer ThinLTO cache. Do not use a new
  per-experiment cache and do not write the public release object cache.
- Compile the small exported ABI wrapper without LTO, as in the previous sparse
  pilot. Keep the link bounded to two workers.
- Generate a matched control and candidate with the same current generator.
  Their unchanged shards come from the same archive. This matters because the
  archived debug objects contain an earlier CMP/branch fusion experiment.
- Keep generated title sources, objects, DLLs and evidence outside the public
  repository. `build.json` records input hashes, commands and rebuilt sources.

The archive in `I:/TriAevum-aot-lab-debug/native-objects/archives/` is **not** keyed
by the manifest in `I:/TriAevum-aot-lab-evidence/baseline/`. Its actual source
manifest is under `I:/TriAevum-aot-lab-cache/generated/`
`fd5c3e92b5c1fb9ef04863b1c3248fc0c4a0416c6aab5d6c058f156846b4ba59/`.
The latter path is recoverable from the LLVM object's `source_filename`.
Program/code equality alone does not establish generated-source equality.
The builder rejects a mismatched object/source association before compilation.

## Initial scope and decision rule

The first direct-link pilot is root `003723C0`, a 22-function closed line-test
group. It replaces shard 137 and reuses 256 objects per variant. It does **not**
mean all 303 selected functions have been integrated or verified in this form.
The 88 roots that previously passed replay qualification span 55 shards.

ThinLTO reuse must be measured separately from C++ source reuse: changing one
shard can invalidate imported code in otherwise unchanged backend objects.
The initial attempt with a new cache was stopped rather than accepted as an
iteration workflow. Reusing a populated cache still exposed significant backend
invalidation. Do not expand to 55 shards on the assumption that source-object
reuse makes relinking cheap.

Use the existing bounded `measure_prebackend.py` harness with the same checkpoint,
neutral input, 180 warmup frames and 360 total frames. Compare matched control and
candidate in ABBA order, without presentation pacing, interpolation or profilers.
Require unchanged memory-content, memory-state and process-state fingerprints.
Count startup failures separately; no incomplete run is a timing sample.

The unit tests cover generated observer guards, local tail calls, callback
restoration, observed resume routing, rejection of SVC and absence of ABI
handoff/budget refunds. They are not gameplay qualification or a speedup claim.

No production promotion is justified until repeatable total pre-NRI savings are
measured in the actual game. A failed or marginal pilot must not be multiplied by
the number of selected functions and presented as a whole-game improvement.

## Measured outcome

Private evidence: `C:/Users/xander/triaevum-aot-causal-analysis/`.

| Series | Control pre-NRI mean | Candidate pre-NRI mean | Candidate time change |
| --- | ---: | ---: | ---: |
| `direct-line-v2-abba` | 8.224 ms | 8.928 ms | +8.55% |
| `direct-line-v2-abba-repeat` | 9.010 ms | 8.957 ms | -0.59% |

Each series is control/candidate/candidate/control with the same binaries and
360 complete frames per run, measuring the last 180. No interpolation, Vsync,
pacing, SDL limiter, sampling or compilation was active during these eight runs.
The reference pairs themselves vary; neither a precise intrinsic regression nor
a useful speedup can be inferred from the aggregate. In particular, these results
do not justify the requested 20% total pre-NRI reduction.

All eight runs have identical final fingerprints, also matching the earlier
frozen reference:

- Memory contents: `14004724930905175187`.
- Memory state: `16777719094724974201`.
- Process state: `11508416489629890540`.

Separate activation evidence: `direct-line-v2-sampling/0/native-samples.json`.
A 25-second intrusive sampler collected 5,455 observations, including 2,091 in
the candidate module and **19 inside specialized computation bodies**:
17 in `BgCheck_LineTestImpl` and two in `FUN_002bded8`, both in namespace
`Direct_003723C0`. One further observation was in its eligibility guard.
Thus this was not merely loading an unused alternative DLL. These observations
are not exhaustive per-function coverage or exclusive CPU-time percentages.
Do not use the sampled run's FPS as a performance result.

The linker map was obtained by relinking the exact candidate with `/MAP`; the
DLL SHA-256 remained identical to its benchmarked build. Relinking unchanged
inputs took about two seconds, but creating the matched pair initially took
465.67 seconds because of backend invalidation. These are different costs.

`test_*aot*.py` discovery passed 48 tests, including five new direct-cohort
generation tests. No Linux qualification or complete-game claim is made.
All benchmark and diagnostic processes completed and closed.

## What this establishes, and what it does not

The exception-based adapter is no longer necessary to exercise specialized
families on live state. Shared context/budget and existing native callbacks can
be retained at a direct generated-call boundary. However, local replay gains
still fail to translate into a demonstrated whole-game gain in this experiment.
The old proxy's 20 ms result and this matched-control result are different tests;
their difference is not a production speedup.

Before another broad integration, require evidence for the actual aggregate
cost of eligible groups in the unmodified game, excluding overlapping callees.
Also distinguish eligibility checks, specialized computation and code-layout/
inlining effects. This experiment does not identify which of those explains the
remaining difference, and guessing is not a reason to rebuild 55 shards.

If further direct-link work becomes justified, keep the cohort implementation in
a reusable translation unit rather than replicating complete overlapping
families in many root shards. The current in-shard emission is a minimal pilot,
not the final 303-function integration architecture.
