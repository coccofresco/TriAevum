# Parallel presentation: design

Status: design, branch `perf/aot-levers`. Nothing here is enabled by default.

## Goal

Overlap guest simulation with presentation while keeping every presented
image, every interpolation decision and every guest-visible effect identical
to the serial loop. The renderer (NRI/Vulkan backend) is not changed; only
who calls it, and when.

## Why, in the product mode

The product default is `native30_interpolated` with `FrameRate =
Interpolated2x`: a 30 Hz guest and 60 Hz presentation. Measured on the Field
checkpoint (host `fca3e07`, `mod-vfp-pgo`, no-present, unpaced, fixed 1/60 s
host delta; `analysis/interp_profile.py`), per guest update:

| Work | Share of the main thread |
| --- | ---: |
| Guest (title code, SVCs, synchronous PICA capture) | 40.7% |
| Presentation `Execute` (native frame + 2 samples) | 31.2% |
| Transition preparation (draw matching N-1 -> N) | 8.8% |
| Draw-plan build | 5.4% |
| Interpolated sampling | 1.9% |
| UI, audio, other | about 6% |

Each guest update executes the draw list three times: the non-presented native
frame that keeps the canonical target complete, the first interpolated sample
and the repeated sample on the following host frame. Guest and presentation
are summed today; overlapped, the frame costs their maximum (about 50% against
about 44% of today's frame), up to about 1.8-2x throughput when CPU-bound.

Interpolation already presents one guest frame late: samples show the
transition N-1 -> N only after frame N is complete. The split below uses
exactly that boundary.

## Stages

Per host frame, the loop in `RunOot3dNativeA32Window` does:

1. frame start: SDL events and input, ImGui `StartDraw`, `api.StartFrame`
   (the frame is open before the guest refresh, because dependency flushes
   record into it);
2. guest refresh (when due): guest execution, SVCs, audio, PICA drain, draw
   plans captured into the presentation scheduler's accumulator, dependency
   flushes for raw copies;
3. presentation: top/bottom transfer selection, `FinishFrame`, native frame,
   transition, samples, bottom frontend, N64 UI, ImGui `EndDraw`, screenshot,
   pacing, `window.EndFrame`.

Split:

- **Guest stage (G)**, main thread: 1 without the renderer calls, all of 2,
  and from 3 the parts that read guest state: transfer selection
  (`hostServices` framebuffer and LCD state, `displayTransfersByOutput`),
  `FinishFrame` (the accumulator is fed by G), `PublishNativeActorInteractions`
  (reads guest memory), UI lifecycle queries and UI primitive building, copy
  references to pin.
- **Render stage (R)**, worker thread: every call on `api`, the execution side
  of the presentation scheduler, `previousVisualFrame`/`latestVisualFrame`,
  `preparedVisualTransition`, `visualSampleCadence`, continuity tracker and
  epoch, temporal samples, `GrassInteractionBridge` sample selection, N64 UI
  rendering, screenshot, pacing and `window.EndFrame`.

G hands R one **presentation packet** per host frame, by value:

| Field | Source today |
| --- | --- |
| framebuffer extent | `window.GetWidth/Height` |
| optional new visual frame (moved) | `picaPresentationScheduler.FinishFrame` |
| selected top/bottom transfer copies, LCD black | `hostServices`, `displayTransfersByOutput` |
| native-frontend presentation flags | `uiLifecycleBridge` |
| UI primitive lists per subsystem | `uiLifecycleBridge.Build*Presentation`, ocarina text, free camera option |
| copy references to pin | `submissionQueue` and accumulator pending draws |
| presentation clock, host frame and guest frame counters | loop locals |
| screenshot request | `hostArgs`, `screenshotState` |

R keeps every piece of interpolation state it owns today and consumes packets
in order, so the sequence of renderer calls is the serial sequence.

## Render command stream

The packet above is carried by a more general mechanism: an ordered **render
command stream**. Every piece of R work is a command (a closure that owns
copies of its inputs) submitted by G in program order:

- `BeginFrame`: `window.StartFrame` (renderer framebuffers), framebuffer
  parameters, `api.StartFrame`, `BeginPresentation`, clear; applies a pending
  pacing/composition policy change to R-owned interpolation state.
- In-refresh commands, where the serial loop calls the renderer during the
  guest refresh: `PublishSceneView` (from `publishNativeSceneView`),
  `GrassInteractionBridge::PublishFrame` (guest memory is read by G, only the
  publish is deferred), and synchronous dependency flushes.
- `Present`: the presentation packet, N64 UI rendering from G-built
  primitives, screenshot, pacing and `window.EndFrame`.

In **inline mode** each command runs at submission, which is exactly today's
order: phase 1 and 2 can be qualified against the serial loop by
construction. In **worker mode** one thread executes the stream in order;
`Run` (synchronous) waits for completion; at most one frame is in flight; an
exception in the worker is rethrown in G at the next submission; `Drain`
waits for an empty stream. Counters and phase timings written by commands
live in R-owned structures and are merged into the report after a drain.

Two serial-loop behaviours need an explicit rule in worker mode:

- **No acquired frame** (`api.HasActiveFrame()` false: minimized window or a
  surface change). The serial loop skips the guest refresh for that frame.
  Worker mode learns it one frame late; it then drains and returns to inline
  mode until frames are acquired again. At most one extra guest refresh runs
  in that transition; deterministic captures never minimize.
- **SDL calls from R.** `Interpreter::StartFrame` reads the window size and
  the Vulkan window backend's swap calls do no SDL work; this is safe on
  Windows. Other platforms stay inline until verified.

## Presentation scheduler split

`Oot3dPicaPresentationScheduler` holds both the accumulator (`Capture`,
`FinishFrame`, `ViewDependencyWork`, pending draws) and execution state
(`Execute`, `PresentExisting`, snapshots, executed-draw tracking per
presentation). The accumulator moves to G; execution state to R. Savestates
capture both halves at a barrier.

## Feedback from R to G (synchronization points)

1. **Dependency flush (raw copies).** `flushNativeDependencies` executes a
   native prefix whose raw texture copies read and write guest memory through
   `SetPicaPhysicalMemoryAccess` and signal PPF interrupts. G enqueues the
   flush as an ordered command and blocks until R completes it; R runs the
   memory callbacks while G is blocked. The callbacks assert that they run
   only inside such a flush; anything else is a hard error, not a race.
2. **Savestate save/load, quick save/load.** Barrier: drain R, capture or
   restore both halves, resume.
3. **Graphics settings changes and resize.** They reconfigure the renderer:
   barrier, apply in R, resume. (This section first assumed ImGui to be
   CPU-only because the base `Gui` render hooks are empty. It is not:
   `Fast3dGui` overrides them, and `RenderImGuiDrawData` also triggers the
   final display composition through `PrepareOverlay`. See "Phase 3 as
   implemented".)
4. **Deterministic captures (`--screenshot*`), probes, semantic traces.**
   Screenshots are ordered in R. Traces that record at presentation time run
   serially unless proven order-independent.
5. **Errors and shutdown.** Exceptions in R are rethrown in G at the next
   packet handoff or barrier; shutdown drains R.

Queue depth is one packet: G may run guest frame f+1 while R presents frame f.

## Latency

Pipelining as first built added one presentation frame of latency: G pumped
and polled frame f+1's input as soon as it had queued frame f's end, so that
input waited behind frame f. Measured with `TRIAEVUM_INPUT_LATENCY=1` (SDL
pump to the return of the frame end, RTX 3060, VSync 60 Hz): 33.3 ms median
parallel against 16.6 ms serial.

Input admission (default) closes it: G waits until frame f has ended before
it pumps events for f+1 (`NativeRenderCommandStream::WaitForFramesInFlight(0)`),
and still runs guest f+1 while R waits for f+1's frame slot and swapchain
image. Interleaved runs, parallel / admission / serial, median (p95) ms and fps:

| Scene | Parallel | Admission | Serial |
|---|---|---|---|
| Hyrule Field, 720p | 33.3 (35.1), 59.8 | 15.9 (25.9), 59.8 | 16.6 (22.4), 59.9 |
| Kokiri, 4K Enhanced (GPU-bound) | 36.0 (38.9), 55.5 | 17.8 (21.4), 55.3 | 18.5 (23.6), 54.8 |
| Barinade fight, 4K Enhanced | 31.7 (45.0), 59.8 | 16.0 (28.1), 59.9 | - |

Uncapped (VSync off, no pacer) the lost overlap costs throughput: 588 against
523 fps in the field.

Late sampling (default, on top of admission) then begins the renderer frame
synchronously (frame slot fence and swapchain image acquisition, where VSync
waits) before the input section, and pumps the events again before polling:
the 8-13 ms the input spent waiting for the image are gone. Interleaved
admission / late, median (p95) ms and fps:

| Scene | Admission | Late sampling |
|---|---|---|
| Hyrule Field, 720p | 16.1 (25.5), 59.9 | 9.1 (12.4), 59.8 |
| Kokiri, 4K Enhanced | 17.6 (22.0), 56.2; 18.5 (22.6), 53.9 | 8.3 (12.7), 55.1; 8.3 (13.1), 53.3 |
| Barinade fight, 4K Enhanced | 16.2 (32.0), 60.0 | 10.2 (17.4), 59.9 |

GPU-bound Kokiri loses 0.6-1.1 fps (1-2 %); uncapped the field runs 520
(admission) and 555 fps (late). `TRIAEVUM_INPUT_ADMISSION=1` keeps only the
admission, `=0` restores the early sampling. The interpolation delay is on top
of these host times.

## Qualification

- Deterministic captures in `Interpolated2x` (and `Original30`), serial
  against parallel: byte-identical framebuffers.
- `runtime.json` `visual_interpolation` and `frame_rate` counters identical
  (matched, interpolated and sample draws, samples presented, continuity
  epoch, cadence), plus the guest memory/process fingerprints and PCM.
- Sheik raw copy scene for the dependency-flush path.
- Throughput: ABBA in `Interpolated2x` with the unpaced fixed-delta harness.

## Phases

0. Interpolated-mode ABBA harness (unpaced, fixed 1/60 s delta, no-present).
1. Extract R as a `NativePresentationStage` object fed by an explicit packet,
   still called inline. Qualify: identical captures and counters.
2. Split the scheduler; snapshot copy references into the packet. Qualify.
3. Worker thread, depth-one queue, barriers, serial fallback, option
   `--parallel-presentation` (off by default). Qualify and measure.
4. Transition preparation off the critical path, if R becomes the bottleneck.

## Phase 3 as implemented

Parallel presentation is the default (since the user decision of
2026-09-29). `--serial-presentation` selects the single-threaded loop, in which
the stream runs every command at submission; `TRIAEVUM_PARALLEL_PRESENTATION`
set to `0` or `1` overrides the launch choice for harnesses.

- **Frames in flight: one.** The guest may submit frame f+1 while the worker
  runs frame f. The end-of-frame command (screenshot, pacing, `EndFrame`) is
  the frame boundary.
- **`Run` executes on the caller.** It drains the worker and then runs the
  command on the guest thread. The renderer is still used by one thread at a
  time, and thread-affine work stays on the thread that owns the window:
  Windows delivers window operations (`SetWindowPos` from display settings)
  to that thread, so running them on the worker while the guest waited
  deadlocked the first frame.
- **Display settings.** `TitleRenderBackend::SetPresentationSettingsHeld`:
  a frame begun on the worker holds display-setting application and the
  confirmation timeout; the guest begins synchronously while a presentation
  transaction is active, and that frame applies them.
- **ImGui.** `Fast3dGui::SetRendererRouting`: backend initialization and the
  font upload run through `Run` (only while the backend is not ready), and
  `RenderImGuiDrawData` (which also runs `PrepareOverlay`, the display
  composition and scanout) is queued in renderer order with a copy of the
  frame's draw lists. Found with the Vulkan validation layer's thread-safety
  checks: the guest thread recorded barriers on the renderer's command pool.
- **Frame begin.** Asynchronous, except when it can reconfigure the renderer
  or must stop the guest: the first frame, a window size change, a graphics
  settings revision change, a native-presentation override toggle, or a
  previous renderer frame without an acquired image. Those use `Run`: after a
  resize the renderer recreates PICA targets and reads GPU copies back into
  guest memory, which is only legal while the guest waits.
- **Frame without an image, discovered late.** The worker marks the frame
  skipped: drawing commands (top and bottom presentation, UI, screenshot) do
  nothing, `EndFrame` and the pacing reset still run, the UI keeps its
  prepared textures, and a dependency flush waits (PPF is delivered at the
  next flush, as a slow GPU would deliver it). The guest sees the flag at its
  next frame begin and takes the synchronous path until frames are active
  again. The serial loop never advances the guest in such a frame; the worker
  mode advances it by at most one frame. Minimizing is the only practical
  trigger.
- **Guest state from the renderer.** Every guest-memory callback registered
  with `SetPicaPhysicalMemoryAccess` (read, write-back, deferred write, PPF,
  materialization) checks `InSynchronousCommand()`: it runs only inline or
  inside `Run`, otherwise it throws. Deferred copy bytes read by the guest go
  the other way: the reader runs in the renderer through `Run`.
- **UI textures.** `N64UiFast3dRenderer::PrepareTextures` reads texture
  payloads (guest memory, via the ocarina text and native texture
  providers) on the guest side; `Render` consumes them. The first use of a
  key is prepared at the same point the serial loop resolved it.
- **Policy.** The guest keeps copies of the composition policy and the
  interpolation flag for change detection; the renderer owns the live
  values. The realtime pacer is renderer-owned (observe, wait, reset).
- **Timing.** Renderer timings go to a separate `NativeFramePhaseTiming` and
  merge after the final drain; per-frame benchmark phases then exclude
  renderer work, and frame times are guest-loop times (bounded by the
  renderer through the frame limit).
- **Reports.** The loop ends with a drain; every renderer-owned counter is
  read after it.

Qualification (2026-09-29, host with `mod-vfp-pgo`): Interpolated2x Field
and Sheik copy, inline and worker, identical to the phase 0 baseline
(framebuffers, guest contract, interpolation and frame-rate counters);
native-fidelity Field and copy, inline and worker, identical to the control
hashes. The worker run was also clean under the Vulkan validation layer's
thread-safety checks.

Throughput (`measure_interpolated.py`, Interpolated2x, unpaced, no-present,
background window, ms per presented frame, ABBA):

| Scene | Baseline | Comparison | Pairs | Median ratio | Range | ms/frame |
|---|---|---|---|---|---|---|
| Field | host6 (fca3e07) | worker | 4 | 0.616 | 0.605-0.651 | 5.61 -> 3.46 |
| Sheik copy | host6 (fca3e07) | worker | 4 | 0.768 | 0.635-0.779 | 12.79 -> 9.83 |

The first worker build reached only 0.908 on Field: per-thread cycle counts
showed the guest at 0.59 and the renderer at 0.55 of a core. The dependency
flush issued a `Run` (a full drain) at every guest refresh and PICA drain
pass, work or not. The guest now checks for dependency work in its own
accumulator first; with that, 0.88 and 0.79 of a core. The Sheik copy scene
flushes raw copies in most frames, so it keeps more synchronization.
An inline build against host6 measured 1.038 (0.969-1.050, four pairs); a
stack profile of both showed the same area split within sampling noise.

Remaining shared state, accepted: `GraphicsSettingsRuntime` and
`GrassInteractionBridge` are mutex-protected; the scheduler's statistics are
separate fields per side; the SDL window size/position queries and the vsync
console variable read in `EndFrame` are read-only on the worker.

## Risks

- The loop is one 9,300-line function with shared locals; phase 1 is the bulk
  of the work and the main regression risk, which is why it is qualified
  serially before any thread exists.
- Hidden renderer or UI state touched from G would become a race. Phase 3
  starts with debug checks that `api` is only called from R.
- Latency, above.
