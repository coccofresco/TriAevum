# Non-renderer throughput: 200 FPS assessment

**Scope amended by the user:** include original graphics generation, GSP,
PICA CPU frontend and plan/capture preparation up to execution by NRI/Vulkan.
The earlier estimate excluding GSP below is historical and is NOT the target.

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

## Pre-backend measurement implementation

`benchmark_window.pre_backend_cpu` now covers the same post-warmup frames as
the throughput counter. `oot3d_prebackend_budget.h` owns the disjoint accounting.

- Guest includes AOT and host SVC/GSP/PICA work. It now also includes
  `AdvanceGuestClockTo`: this function can run sleeping guest threads, which
  the previous `guest_seconds` timer omitted. The included clock scope is
  separately exposed, and contains scheduling overhead as well as AOT.
- Submission includes plan construction, capture, interrupt delivery and
  nested guest resumes. Subtract those resumes once; expose plan and remaining
  queue/capture work separately. Do not add the plan again to total submission.
- Add measured host-start, input, DSP and audio-output scopes.
- A second, conservative host envelope subtracts renderer-start, visual
  presentation, and final presentation from the completed frame interval.
  Final presentation already contains pacing; do not subtract pacing twice.
  Report the residual between this envelope and accounted scopes explicitly
  as unclassified host/UI time. It is not automatically attributed to AOT.
- CPU operations inside NRI execution, GPU time, and the visual-presentation
  wrapper are excluded. CPU plan/capture and the original graphical command
  production are included. This is not a benchmark with rendering disabled.
- Timing is elapsed wall time, so OS preemption can affect it. 30 Hz simulation
  remains unchanged, advanced interpolation is disabled for the measurement,
  and capacity FPS must not be confused with real-time gameplay speed.

The focused arithmetic test covers overlap and invalid interval detection.
`tools/renderer/tev_program/measure_prebackend.py` clones writable inputs from
a qualified invocation, uses an unchanged title plugin, bounds every run, and
rejects pacing, vsync, interpolation, incomplete runs or invalid intervals.
No AOT cache/module or external worktree is modified.

WPR CPU sampling was attempted but Windows rejected enabling the profiling
policy (`0xc5585011`). No ETW recording was started. Exact exclusive per-function
CPU rankings therefore remain unmeasured; do not replace them with block-entry
frequency or report a hardware-cache bottleneck as established fact.

## Measured amended scope (2026-09-16)

New private results: `I:/TriAevum-public/prebackend-20260916/measurements.json`.
Three runs, unchanged baseline title plugin, 900 native updates each, 180 warmup,
720 measured, no screenshots, interpolation, vsync or pacing; audio preserved.
All three memory-content fingerprints are `15598764887955759453`.
No invalid accounting intervals. No AOT recompilation or gameplay changes.

| Post-warmup, ms/update | Run 1 | Run 2 | Run 3 |
| --- | ---: | ---: | ---: |
| Guest, including SVC/GSP/PICA | 14.851 | 15.107 | 15.854 |
| PICA plan construction | 1.568 | 1.585 | 1.740 |
| Queue/capture excluding plan and nested guest | 1.009 | 0.930 | 1.024 |
| Measured host-start + input | 0.446 | 0.578 | 0.525 |
| DSP + output | 0.753 | 0.748 | 0.767 |
| Accounted pre-backend total | 18.627 | 18.948 | 19.910 |
| Unclassified host/UI residual | 0.537 | 0.508 | 0.543 |
| Conservative host envelope | 19.165 | 19.456 | 20.453 |
| Envelope p95 upper bound | 31.375 | 31.875 | 32.500 |
| Whole application throughput | 42.72 FPS | 42.26 FPS | 40.09 FPS |

**These are loaded-host observations, not a replacement for the earlier quiet
baseline.** The Windows total CPU counter reached 84%, with other applications
active. A separate legacy-runtime control under the same general conditions
produced 40.94 FPS (`prebackend-control-20260916`). That does not establish an
instrumentation slowdown, nor does one control prove zero overhead. Do not
scale these numbers to an assumed quiet frequency or promise speedups from them.

The previously omitted guest-clock scope was only 1.8-2.0 microseconds/update
in this stationary fixture: a real measurement gap, but NOT its bottleneck.
Nested guest resumes deducted from submission were about 0.74-0.79 ms/update.

## Concrete bottleneck candidates from code and diagnostic run

`I:/TriAevum-public/prebackend-profile-20260916/0/runtime.json` is an additional
instrumented run. Its detailed counters cover all 900 updates, not the 720-frame
benchmark window, and must not be added to the table as disjoint phases.

1. **AOT dominates:** dispatch 11.083 s vs SVC 2.712 s, about 80/20 within those
   scopes. Exclusive function CPU sampling is still required to choose the
   highest-value AOT transformation. Block-entry rankings are not a substitute.
2. **Coarse geometry invalidation:** 369,000 version misses vs 14,400 hits.
   `NativeA32Memory::RangeWriteGeneration` returns the global `mWriteGeneration`
   after checking region validity, not a range-specific generation. Thus unrelated
   writes force geometry comparisons. 400,160,700 bytes compared vs only 5,109,120
   bytes copied. The vertex-capture scope costs 0.511 s over this diagnostic run;
   not all of that scope is recoverable comparison overhead.
3. **Unconditional texture revalidation:** `CaptureTextureResources` compares
   cached bytes (or hashes them) on every lookup. 94,500 hits, zero misses,
   1,980,518,400 bytes compared, zero copied. Texture capture costs 0.480 s.
   The problem here is validation traffic, not a missing texture cache or
   repeated texture allocation/upload. Exact range/dirty tracking should serve
   both geometry and textures, with every AOT/host writer, alias and restore
   path covered. This requires coordination with the memory ABI owner, not a
   plugin-only unchecked window or a new shader cache.
4. **Vertex structural identity churn:** 3,600 structural entries vs only two
   vertex shader source entries. `BuildOot3dPicaVulkanDrawPlanAndConsumeResources`
   keys structural lookup by program/swizzle mutation identities. Investigate
   equal-content reuploads and bounded/content-based structural reuse. The
   observation is not proof that every entry is redundant or the cause of all
   plan-construction time; validate effective program/interface identity first.

Owning files: `tools/oot3d/native_game_runtime/oot3d_native_a32_memory.cpp`,
`oot3d_native_pica_submission.cpp`, `oot3d_native_pica_vulkan_plan.cpp`.
Existing precise memory tracking experiments in the parallel worktree must be
reviewed before implementing another version. Preserve compiled-plugin ABI and
all content/fingerprint checks; no stale-resource reuse is acceptable.

The capture costs combined are only 0.99 s/900 updates even in this loaded run.
Their complete removal would not bridge the whole gap to 5 ms. They are concrete
secondary opportunities, while a demonstrated major AOT gain remains necessary.
The reliable next performance claim requires a quiet-host run of this new,
inclusive metric plus exclusive CPU profiling, not extrapolation from the old
5.2-5.5 ms estimate which excluded the now-requested graphics preparation.
