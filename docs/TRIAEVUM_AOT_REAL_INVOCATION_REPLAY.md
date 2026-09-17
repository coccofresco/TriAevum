# Real-input AOT replay

Developer diagnostic, 2026-09-17. Not a renderer change or delivered speedup.
This supersedes the earlier statement that invocation replay was not implemented
in `TRIAEVUM_AOT_STRUCTURAL_SURFACE_RESEARCH.md`.

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
- `--support J:/TriAevum-verify-20260910/runtime/triaevum_title_whole_aot_support.lib`
- `--include C:/vcpkg/installed/x64-windows-static/include`
- `--baseline I:/TriAevum-aot-lab-evidence/baseline/triaevum_title_aot.dll`
- `--executable I:/TriAevum-public/ui-dependencies/bin/TriAevum.exe`
- `--invocation C:/Users/xander/triaevum-aot-memory-region/abba/0/command.json`
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

Private results under `C:/Users/xander/triaevum-aot-causal-analysis/`:
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
