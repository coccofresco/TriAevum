# Non-renderer throughput: 200 FPS assessment

Date: 2026-09-16. Runtime checkout `23961d8`; evidence read without modifying
the parallel AOT worktree (`perf/aot-independent`, inspected HEAD `f788150`).
This is an assessment of existing measurements, not a new benchmark or speedup.

## Define the target

200 independent native updates per second of computation means a 5 ms budget.
It does not mean changing simulation from 30 Hz, accelerating the game, or
counting interpolated presentations as updates. F2 disables optional effects;
it does not remove the native renderer, its CPU frontend, or presentation.

Two distinct targets must not be confused:

- **Non-renderer compute below 5 ms/update:** plausibly close in the measured
  scene, but not yet separately benchmarked across representative scenarios.
- **Whole application above 200 native updates/s with the renderer unchanged:**
  not supported by current measurements or the completed AOT experiments.

## Existing evidence

Parallel report: `I:/TriAevum-aot-lab/docs/TRIAEVUM_AOT_OPTIMIZATION_RESULTS.md`.
Raw profiling: `I:/TriAevum-aot-lab-runs/runtime-profile/runtime.json`.
Clean timing example: `I:/TriAevum-aot-lab-runs/host-ab2/0-shipped/runtime.json`.

The clean example measures 720 updates after 180 warmup frames, with fixed
native delta, no interpolation, no VSync, no SDL limiter and pacing disabled.
It reports 112.16 updates/s, 8.916 ms mean, 10.875 ms p95 upper bound.
The report's earlier quiet baseline median is 106.5 updates/s. These are
Windows Hyrule Field stationary-state results, not general game guarantees.

The separate instrumented run covers 900 updates:

| Scope | Total | Per update | Interpretation |
| --- | ---: | ---: | --- |
| Guest execution | 5.3173 s | 5.908 ms | Inclusive of host service calls |
| AOT dispatch/execution | 4.2716 s | 4.746 ms | Largest measured non-host scope |
| Host SVC | 1.0318 s | 1.146 ms | Mostly graphics, not OS scheduling |
| GSP command queue | 0.9015 s | 1.002 ms | Subset of SVC |
| PICA frontend submission | 0.8439 s | 0.938 ms | Subset of GSP, not additive |
| DSP mix | 0.2582 s | 0.287 ms | Outside guest scope |

Removing GSP from the accounting, then retaining DSP and ordinary host work,
puts the non-renderer estimate around 5.2-5.5 ms/update, rather than 10 ms.
This is arithmetic attribution, NOT a measured headless throughput. Original
game graphics-command generation still resides inside AOT. A stricter exclusion
of all graphics would also need to classify that work.

Do not sum every `phase_timing` field: `PicaSubmitSeconds` includes drain-loop
guest resumes, and visual replay/backend scopes overlap. The counters cover
all 900 updates while `benchmark_window` excludes the first 180. Instrumented
and clean measurements must not be treated as the same sample window.

## Bottlenecks and honest ranking

1. **AOT execution, 4.75 ms/update:** the main eligible budget. Existing block
   entry sampling identifies hot regions but does not rank exclusive CPU time.
   Frequent entries include `FUN_004a0338`, `FUN_004a022c`, mesh command
   submission, collision line tests and material/mesh transforms. No exact
   millisecond saving can yet be assigned to any of these functions.
2. **Guest memory/data access and code locality:** strong candidates inside
   AOT, not a hardware-counter-proven diagnosis. About 523k guest memory
   instructions/update were estimated. A flat-window experiment was slower;
   forced memory inlining increased CPU cost and code size. Do not repeat those
   strategies or equate fewer instructions with lower wall time.
3. **Scalar VFP:** substantial microbenchmark cost, but the verified hardware
   fast path achieved only about 0.7% in paired game tests, below significance.
   It is not evidence for the originally projected 0.5 ms/frame saving.
4. **Non-graphics host/audio:** substantially smaller. DSP is about 0.29 ms;
   non-GSP SVC about 0.145 ms. Useful after a proven AOT hotspot, not a plausible
   source of a 5 ms saving. Disabling audio would violate the target.
5. **GSP/PICA (~1 ms) and NRI execution:** separate renderer scope. A broader
   definition of runtime may include them, but they must then be explicitly
   counted rather than described as gameplay cost.

ThinLTO, state promotion and lazy flags already exist. The parallel experiments
found no significant gain from flag fusion, block-entry inlining or global O3.
The report infers memory-latency domination from these negative results; that
inference alone cannot exclude branch misses, instruction-cache pressure,
dependency chains or sample aliasing. Its older VFP recommendation is superseded
by the later hardware-path measurements in the same report.

## Fastest useful next investigation

1. Use the same qualified module and fixed native workload on a quiet host.
   Keep warmup, vsync/pacing/interpolation checks explicit; benchmark menu closed.
2. Obtain sampled **exclusive CPU time with stacks**, symbols for the generated
   plugin and host, and scheduling/context-switch data. Attribute nested host
   calls separately. Hardware cache/branch/stall counters would distinguish
   the memory/code-locality hypotheses where available.
3. Partition samples into gameplay, original graphics production, translated
   memory/VFP helpers, guest scheduler/IPC, DSP, and the modern renderer. Align
   timing scopes to the same post-warmup update interval without double counts.
4. Choose one or two measured function families that together own at least
   1 ms of non-renderer time. Optimize data traversal/batching, region lowering
   or a semantically equivalent host routine only when the profile supports it.
   Preserve guest memory, ordering, thread events and numerical contracts.
5. Use alternating A/B order and identical state fingerprints; then repeat in
   Kokiri, the adult Field checkpoint and a moving cutscene. Require median and
   p95 budgets, not one best FPS result. Do not change another agent's artifacts.

The budget arithmetic matters: halving a 4.75 ms AOT component inside a 9.4 ms
total yields roughly 7.0 ms (143 FPS), not 200. Conversely, reducing a genuinely
isolated 5.3 ms non-renderer path to 5 ms needs only about 6% improvement.
There is currently no demonstrated set of fixes guaranteeing a whole-game 2x
speedup without renderer changes. The next deliverable should be the exclusive
CPU cost table, not another unprofiled emitter rewrite.
