# F2 Native Presentation Override

Date: 2026-09-07. Owner: shared renderer settings runtime; keyboard routing belongs
to the NRI window host. No gameplay, title module, asset or PICA semantic changes.

## Behavior

- F1 starts closed on every launch, including configs saved with that window open.
- F2 toggles a session-only override of Grass, Toon including outline, CACAO,
  and both reflection providers. These effects remain Off until F2 is pressed again.
- No saved values, presets, mask rules, material assignments or settings files are
  changed by the toggle. F1 edits and profile changes still update the configured
  state while effects are suspended. Restoring uses those latest values, not an
  earlier backup. Restarting clears the override.
- The fixed header above all F1 tabs explains F2 and explicitly shows ACTIVE or
  inactive status. ACTIVE is highlighted, not communicated by color alone.
- This is **not** the Authentic/Native Fidelity preset: interpolation, FOV,
  resolution, AA/upscaling, TopScreen, texture packs and directional shadows are
  unchanged. Only the four effect categories requested for comparison are disabled.

## Implementation Boundaries

Paths below are relative to `runtime/three_ds_recomp/`.

- `graphics_settings_runtime.{h,cpp}` under `include/fast/oot3d/` and
  `src/fast/oot3d/`: `Snapshot()` and `SnapshotWithRevision()` retain configured
  values. `SnapshotForRendering()` applies the temporary mask to a copy under the
  same lock. A toggle increments the revision without staging or persisting values.
- `src/fast/backends/gfx_vulkan.cpp`: takes the effective snapshot once per frame,
  before extension-graph, shader and provider decisions. Existing Off paths own
  resource reuse and native composition. Changes in effect activation reset TAA
  history rather than blending the previous presentation into the new one.
- `src/fast/Fast3dWindow.cpp`: F2 is event-driven, not guest-tick/presentation-polled.
  The pressed-key set rejects repeat and preserves short down/up taps. Both edges
  are consumed before legacy F2 mouse-capture/fullscreen and controller-deck routing.
  This reservation applies only to the NRI host, not legacy backends.
- `src/fast/Fast3dGui.cpp`: ignores previous F1 open state at construction and keeps
  F1/F2 out of independent ImGui shortcuts. F1 still toggles and controls cursor capture.
- `src/fast/oot3d/graphics_settings_window.cpp`: status header only; no duplicate
  effect settings or UI-owned restoration buffer.

## Verification

1. Build `triaevum_public_runtime` in the existing direct-module build. No AOT/title
   regeneration or release-installation executable replacement is necessary.
2. `tools/triaevum_release/tests/run_f1_settings_smoke.ps1`: actual compiled widgets
   and memory persistence, **1,723 assertions**. Covers all nine Toon/reflection
   mode combinations, exact restoration after edits while suspended, unchanged
   unrelated settings, no toggle writes, and real active/inactive header text.
3. `python -m unittest tools.triaevum_release.test_product_graphics_defaults` with
   `TRIAEVUM_PRODUCT_TEST_EXE` pointing to the build: **5 tests**, saved defaults intact.
4. `tools/triaevum_release/tests/verify_renderer_hotkeys.ps1 -Exe <dev-exe>
   -Profile <launch-profile> -State <hudtest-state> -Output <new-directory>`:
   bounded 1,000-frame gameplay run with private config/saves and Vulkan validation.
   It targets the SDL window explicitly, injects held/repeated and short F2 presses,
   opens/closes F1, and captures the **renderer framebuffer**, not the desktop.
   `verify_renderer_hotkey_frames.mjs` verifies the actual executed pass counters.
   This requires a profile with Grass, Toon and CACAO enabled; reflection mode
   coverage is also checked by the runtime/widget test, even when the scene has none.

Evidence: `I:/oot3dre_work/f2-hotkeys-20260907-d/`. The five tested intervals are
configured, overridden, restored, overridden with F1 visible, restored with F1
closed. GPU validation and failed effect-graph counters are zero. Frame 90 starts
without F1 despite `menu=true`; frame 720 shows the active header; frame 900 shows
restored Grass/outline with no F1 overlay. Release logging filters INFO, so the test
must not use informational toggle messages as proof of rendering or input delivery.
Synchronous captures and validation are not performance measurements.

## Repeated-toggle Lifetime Repair

Renderer commit: `71a575b3`. Runtime-only build passed; actual F1 widgets passed
1,723 assertions, renderer foundation/GPU suites passed 328 tests. Checkpoint
test passed 1,000 frames with MSAA 4x actually active:
`I:/oot3dre_work/f2-lifetime-msaa4-20260907-b/`, zero validation errors and no
native target recreation during the F2 sequence.

The first hotkey test did not cover retained scanout during the intro. The user's
next run failed with `native PICA display transfer output has no GPU snapshot`.
Changing effect attachments called `ResetNativePicaRenderTargets`, which retired
native color/depth and display-transfer images together with optional guides.
That is not a legal consequence of enabling/disabling an extension: the guest
can still present an earlier transfer without redrawing it.

`gfx_vulkan_pica_attachments.cpp` now owns attachment transitions. Native images
and their published generations remain alive. Auxiliary storage grows lazily,
is cleared independently on reactivation and remains cached while unused.
Active render/clear attachment counts come from the frame graph, not the cached
storage capacity. Canonical and instrumented framebuffer variants share the same
native images, including the MSAA path. No full reset or device idle is requested
by an effect activation change. Resolution/sample-count changes still retain
their separate, explicit reset path; this is not a redesign of those operations.

Bounded intro stress: `verify_renderer_hotkeys.ps1 ... -Boot -Stress`.
Evidence: `I:/oot3dre_work/f2-lifetime-{before,after}-20260907/`.
The old executable produced NRI descriptor-pool exhaustion (128 storage-clear
bindings) from continual target recreation. The fixed executable passed 473
F2 presses, 2,979 frames, 1,162 enhanced and 1,811 native transfer frames, with
zero GPU/graph errors. Native target allocations dropped from 686 (4,122 images)
to 2 (18 images) in these bounded runs. These are lifetime/allocation counters,
not an FPS benchmark or identical-timestamp image comparison.

The checkpoint test also asserts that no native targets are recreated after
warmup. `-Overrides tools/triaevum_release/tests/renderer_hotkeys_msaa4.json`
requests MSAA 4x, and the verifier requires that diagnostic sample count actually
be observed; a silently disabled AA mode is not MSAA coverage.

General performance findings and the next priorities are in
[TRIAEVUM_F2_LIFETIME_AND_PERFORMANCE.md](TRIAEVUM_F2_LIFETIME_AND_PERFORMANCE.md).
