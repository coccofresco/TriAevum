# Guest thread: overlapping guest execution with rendering

Status: design, implementation in progress on `feature/guest-thread`, gated by
`--guest-thread` (default off).

## Why

Measured on a 6-minute Kokiri session (Linux, Intel Arc Meteor Lake, Mesa,
Interpolated2x at 60 Hz, no captures):

| phase | seconds | per presentation frame |
| --- | ---: | ---: |
| guest step (ARM11 whole-AOT, DSP, VBlank) | 61.4 | 5.0 ms |
| visual presentation (PICA replay + Vulkan submit) | 60.3 | 4.9 ms |
| StartFrame (present wait, fence, acquire, prewarm) | 62.7 | 5.1 ms |
| plan build + submit bookkeeping | 18.4 | 1.5 ms |
| pacer sleep | 135.5 | — |

12,210 presentations in 359.7 s = 34 fps against a 60 Hz target, with 4,624
deadline misses. Average CPU work is 16.6 ms per frame, exactly the budget, so
variance alone produces the misses. One thread does everything; the other 21
logical cores are idle (`/proc/<pid>/task/*/stat`: main 99.4%, next 1.0%).

Azahar's answer is the same one used here: keep the guest serial (it is one
ARM11 program) but run it on its own thread and let rendering overlap it.

## What is already thread-safe

- `Oot3dPicaVisualFrame` owns its data. Vertex/index/texture bytes are copied
  out of guest memory into immutable `shared_ptr<const vector>` at submission
  (`oot3d_native_pica_submission.cpp:259-311, 439-497`); no draw plan points
  into guest RAM (`oot3d_native_pica_vulkan_plan.h`).
- Guest-visible PICA completion is signaled at capture, never at GPU execution
  (`oot3d_native_a32_window.cpp` drain loop), so the guest never waits on the
  renderer.
- `GrassInteractionBridge`, `GraphicsSettingsRuntime` and the submission queue
  are mutex-guarded. The Vulkan present queue already runs on a worker.

## Split

Main thread keeps everything with a hard main-thread requirement: SDL event
pump, ImGui, `api.StartFrame` (settings apply, swapchain recreate, prewarm),
`window.EndFrame`, savestate execution, shutdown.

A guest thread owns `process`, `hostServices`, `dspHle`, audio output,
`submissionQueue` drain and plan build, the visual-frame accumulator
(`Capture`/`FinishFrame`), `displayTransfersByOutput`, `uiLifecycleBridge`
mutation and every guest-memory read the old loop performed between guest
steps. It produces a `GuestPacket` per presentation.

Per presentation `k`, the old loop was

    probes(k) → poll input(k) → StartFrame → step(k) → present-half(k)
    → drain(k) → HUD(k) → EndDraw → pacer → EndFrame

where present-half(k) consumes frames from drain(k-1) and HUD(k) reads guest
memory after drain(k). The new schedule keeps every guest-memory read at the
same guest phase:

    main:   poll input(k) ─┐                 present-half(k) ─┐  HUD(k)  EndDraw pacer EndFrame
    guest:  [tokens(k), input(k)] step(k) → packet A(k) ─┘ drain(k) → HUD(k) → packet B(k) ─┘

- `packet A` (after `step(k)` and `FinishFrame`): the finished
  `Oot3dPicaVisualFrame` for the selected top transfer, the selected top and
  bottom transfers, `LcdForceBlack`, `NativeFrontendPresentationActive`, the
  scene view (`publishNativeSceneView` payload), and the actor interaction
  publication for the grass bridge.
- `packet B` (after `drain(k)`): TopScreen/shadow HUD primitives per subsystem
  and the probe flags (`nativeFrontendTouchEnabled`, native game mode) that
  main needs to poll input for `k+1`.

Main waits for A before present-half and for B before rendering the HUD.
In the current implementation only `drain(k)+HUD(k)` overlaps
present-half(k); `step(k)` still runs while main waits, because its input is
polled after `StartFrame` and moving that poll earlier changes input phase
relative to the single-thread path (the byte-identity gate). Overlapping the
step with `StartFrame` is the next step and needs the explicit packet handoff
below rather than shared loop locals. Tokens (`guestRefreshesDue` from
`presentationScheduler.Advance`) and the polled `physicalInputFrame` are sent
to the guest thread each presentation; the guest thread blocks on them, so it
never runs ahead of input and audio buffering stays bounded.

`Oot3dPicaPresentationScheduler` is split: the accumulator (`Capture`,
`FinishFrame`) becomes guest-thread state; `Execute`, `PresentExisting`,
`HasSnapshot` and `mSnapshotCompletions` stay on main.

## Hazards and how each is handled

1. **Savestate / loadstate / PICA reset** span both halves. They run on main at
   the join point after packet B with the guest thread parked; `loadState`
   additionally clears both packets and the interpolation frame pair.
2. **Frame-rate mode change** resets the previous/latest frame pair and the
   continuity epoch on main. Packets carry the epoch they were produced under;
   a packet from an older epoch is presented as a resynchronized current frame,
   never interpolated against the new pair.
3. **StartFrame stalls** (prewarm, swapchain recreate, present wait) delay the
   join, so the guest thread simply waits for the next tokens; it cannot run
   ahead. No guest-side mutex is ever held across a main-thread stall.
4. **Interpolation pair lifetime**: `preparedVisualTransition` points into
   `previousVisualFrame`/`latestVisualFrame`; both are main-owned, moved out of
   packet A, and only reset on main.
5. **Shutdown and errors**: `window.Close()` requests from the guest step and
   exceptions inside it become a stop flag plus a stored `exception_ptr` that
   main rethrows at the join; main joins the guest thread before
   `WaitForAllPresents`/`vkDeviceWaitIdle` and before `process` destruction.

## Verification gate

`--guest-thread` is accepted only when a deterministic replay
(`--fixed-delta-seconds 0.016666 --input-timeline …`) produces framebuffer
captures byte-identical to the single-thread run at every checkpoint, the
held-trigger and quick-state replays complete, and an interactive session
shows the guest thread and main thread both busy in
`/proc/<pid>/task/*/stat`.
