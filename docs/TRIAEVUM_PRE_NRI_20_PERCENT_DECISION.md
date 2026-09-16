# Pre-NRI optimization: measured stop decision

2026-09-16. Starting revision: `9496ac7`. Windows, adult Link in Hyrule Field.

## Decision

**No optimization is promoted. No new AOT optimization prototype is justified
by this investigation.** The requested 20% reduction requires about 1.54-1.58 ms
per native update. Existing resource-validation and isolated AOT-loop proposals
do not demonstrate that much removable work. Do not extend them on the basis of
instruction counts, inclusive stack observations or improvements in unit tests.

This is a cost/feasibility result, not a performance improvement. It does not
prove a larger optimization is impossible; it establishes that we have not yet
identified one with sufficient evidence to spend another implementation tranche.

## Baseline and controls

- Same frozen title module, checkpoint, neutral inputs, audio and native graphics.
- 3,000 native updates/run, first 180 excluded, 2,820 steady-state observations.
- No interpolation, VSync, frame limiter, pacing, screenshots or stack sampler.
- One original-host control, three CPU-diagnostic runs, two diagnostic-host runs
  with the probe inactive, one final build with the probe compiled out.
  No compiler running during any game measurement.
- Original host: **7.715 ms pre-NRI**, 105.35 whole-application updates/s.
- Probe-inactive host: **7.912 / 7.904 ms pre-NRI**, 102.78 / 102.77 whole updates/s.
- Final compile-disabled host: **7.750 ms pre-NRI**, 104.38 whole updates/s,
  original fingerprints/counts preserved, probe unavailable as intended. This
  single control does not establish an optimization. The original executable
  was then restored at the normal launch path and its SHA-256 rechecked.
- These are capacity measurements, not a change to the game's native clock.
- The uninstrumented controls were not interleaved ABBA tests. Their ~2.5%
  difference does not distinguish probe-boundary overhead from host variability.
  The final implementation compiles probe scopes out by default. Preserve the
  original executable as the working reference; do not call this a faster build.

The measured pre-NRI envelope includes AOT, guest services/GSP, PICA frontend,
resource capture, plan/queue construction, host/input, DSP and audio output.
Renderer-start, visual presentation/NRI execution and final presentation are
excluded. Guest work resumed while draining PICA is counted exactly once.
`pre_backend_cpu` is a legacy JSON name: its existing values are **wall times**.

## CPU versus off-CPU

The opt-in probe measures the main thread, not all process threads. Nested scopes
are exclusive and cover the same post-warmup window. `QueryThreadCycleTime`
attributes actual scheduled cycles to phases; cycles are NOT converted to ms.

`GetThreadTimes` is too coarse to assign CPU seconds to tiny individual scopes:
charges spill across adjacent scopes even when many samples are accumulated.
Use only its telescoping **whole-window** total, not per-phase CPU differences.
The first diagnostic artifact contains raw per-phase counters; the summary tool
deliberately ignores those distributions. Final JSON exposes only window CPU.

A conservative pre-NRI CPU bound follows without assigning driver waits to AOT:

```
off_cpu_upper = max(0, window_wall - window_thread_cpu + endpoint_allowance)
pre_nri_cpu_lower = max(0, pre_nri_wall - off_cpu_upper)
pre_nri_cpu_upper = min(pre_nri_wall, window_thread_cpu + endpoint_allowance)
```

The 32 ms allowance covers two 15.625 ms accounting ticks on this machine
(`GetSystemTimeAdjustment` returned 156250 in 100 ns units). Across 2,820 updates
this adds 0.01135 ms/update. Whole-ledger wall time closes against the benchmark
window within microseconds. Off-CPU includes preemption and external contention;
it is **not** evidence of removable locks or host sleeps.

| Diagnostic run, ms/update | 1 | 2 | 3 |
| --- | ---: | ---: | ---: |
| Whole measured wall | 10.437 | 10.451 | 10.559 |
| Whole main-thread CPU | 10.206 | 10.245 | 10.300 |
| Pre-NRI wall | 8.583 | 8.503 | 8.676 |
| Pre-NRI CPU lower bound | 8.341 | 8.285 | 8.406 |
| Pre-NRI off-CPU upper bound | 0.242 | 0.217 | 0.270 |

Thus at least **96.8%** of this diagnostic pre-NRI interval is CPU work. There is
no demonstrated 1.5+ ms host-wait budget to remove. Instrumentation adds overhead:
diagnostic wall times are 8.50-8.68 ms, controls 7.72-7.91 ms. Never use them as
an optimization A/B result or subtract the difference from arbitrary phases.

## What is actually large enough?

Exclusive wall scopes below are diagnostic cost ceilings, **not promised gains**.
The independent cycle breakdown corroborates that AOT is the dominant CPU user.

| Phase | Observed ms/update | Interpretation |
| --- | ---: | --- |
| AOT dispatch/execution | 4.97-5.07 | Dominant; includes useful game work, not all removable overhead |
| PICA frontend excluding capture | 0.56-0.57 | Command/state processing, decode and enqueue |
| Vertex plus texture capture | 0.60-0.61 | Includes mandatory snapshot/copy work as well as validation |
| PICA plan | 0.84-0.86 | At most ~10% of diagnostic pre-NRI time |
| PICA drain/queue excluding nested work | 0.53-0.57 | Includes transfers/completions and composition handling |
| Other SVC | 0.28-0.29 | Services excluding PICA frontend |
| Guest scheduler | 0.096-0.098 | Not the alleged multi-ms synchronization bottleneck |
| DSP plus audio output | 0.28 | Audio remains active |
| Draw-queue lock acquisition | 0.032-0.033 | Includes uncontended acquisition and probe overhead, not pure blocking |

Native `SleepThread` and `WaitSynchronizationN` return guest scheduling states;
they do not call an OS sleep in these paths. `AdvanceGuestClockTo` can execute
guest work; its elapsed time must not be subtracted as idle time. Explicit pacing
is disabled and its wrapper measures about 0.0006 ms/update. Other uninstrumented
locks cannot hide more off-CPU time than the whole-window bound above, although
busy-spin work would count as CPU, not off-CPU.

Candidate sizing:

1. **Precise dirty ranges instead of repeated resource comparisons:** structurally
   reasonable, but even removing the entire 0.60 ms capture scope is insufficient.
   A real implementation must retain copies, faults, aliases and old-module
   compatibility, so its attainable gain is smaller than that ceiling.
2. **Parallel/cached plan construction:** the whole plan scope is only 0.86 ms.
   Plan plus all capture is about 1.44-1.47 ms in the instrumented run, below 20%
   even under the impossible assumption that all this work disappears for free.
3. **AOT lowering across broad regions / direct guest-memory contract:** the only
   current AOT direction with a plausibly large *surface*, but not yet a measured
   20% opportunity. It would need roughly one third of the AOT cost removed.
   Existing helper stack shares are not exclusive timings, and a valid memory
   operation is not redundant merely because its translation is verbose.
   The previous three-function pilot was too small and did not improve the game.
4. **Fusing the entire frontend/capture/plan pipeline:** its larger gross scope
   does not establish enough duplicate work to remove. Deleting native processing,
   changing ordering or making snapshots stale would not be an optimization.

No candidate currently satisfies the requested evidence threshold. **Stop here,
rather than selecting another small function and calling the experiment progress.**

## Reproduction and invariant checks

Private artifacts: `C:/Users/xander/triaevum-prebackend-cpu-budget/`, subdirectories
`control-before`, `ledger`, `control-disabled`, `compiled-out`. Each run contains exact arguments,
logs, JSON timing, private writable configuration/save data. ROM-derived data
and these artifacts must not enter public packages.

Baseline title SHA-256:
`304dae27dde2d287f0d911efd2d66e194359ad9403f5bd9083b05fc16f5ffe6b`.
Original host SHA-256:
`f316277adaad1a77565d5cef389a6311572cfb094f285e50a7693f859d145b2f`.
Measured diagnostic host SHA-256:
`e9f34e9bd3a04d3b67eed053c246e745ffe607d39c8fcedd407c93eba61f2b03`.

All seven 3,000-update runs agree on:

- Memory-content fingerprint: `11407936860247825508`.
- Process-state fingerprint: `7994768381779420407`.
- PCM fingerprint: `5640381280546040506`, audio frames suppressed: zero.
- Submitted draws: `284962`; display transfers: `6000`.

These are deterministic workload checks, not a claim of pixel-by-pixel visual
qualification. No renderer algorithm, title logic, gameplay or assets changed.

Developer instrumentation is isolated in `oot3d_cpu_phase_probe.h`. Build with
`-DTRIAEVUM_ENABLE_CPU_PHASE_PROBE=ON` (Windows only), then use
`tools/renderer/tev_program/measure_prebackend.py --thread-cpu-ledger` with the
frozen invocation/module above. Default builds compile the probe scopes out.
Use `summarize_cpu_budget.py <ledger/measurements.json>` for correct bounds.
It rejects failed clocks/window closure; CPU-diagnostic runs are rejected by the
runner's A/B optimization mode. No title-module or AOT recompilation is needed.

Tests: the C++ phase-ledger test covers exclusive nesting, invalid clocks,
inactive scopes and real Windows nested-scope restoration. Three Python tests
cover quantized-counter spill, invalid windows and the window-only CPU schema.
The CMake test target is `triaevum_cpu_phase_probe_tests`.
