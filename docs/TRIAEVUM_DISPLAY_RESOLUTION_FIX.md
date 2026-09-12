# Display resolution and confirmation

2026-09-10. Host/runtime correction; no title logic, ROM data or save format changes.

## Causes and fixes

1. A fractional render scale can produce an odd physical source width. The
   native 2:1 display filter then rounds the destination up by half a pixel.
   The planner treated that valid host rounding as an invalid native command,
   aborting with `PICA display transfer samples outside its source image`.
   Native command bounds are now checked before host rounding. The planner and
   NRI descriptor validator share `SamplingFitsSource`; the compute filter
   repeats the edge texel and the Vulkan blit fallback uses bounded source
   extents. Invalid native commands are still rejected.
2. Swapchain recreation deliberately retained offscreen targets, but their
   cache identity did not include output dimensions. A requested 4K output could
   continue rendering a 720p scene, and diagnostics reported the calculated
   extent rather than the cached allocation. At an idle frame boundary, actual
   target extents are now checked against the output/scale policy, including
   targets restored from a differently sized savestate. Stale targets and
   dependent effects are rebuilt through their existing lifecycle owners.
   This check runs after image acquisition, which can itself recreate the
   swapchain. Checking earlier left a one-frame mismatch during a resize,
   reproduced as a narrowed 4:3 scene with black side bands.
3. Resolution changes retain the last display images for presentation-only
   frames. Retired depth/guide associations also invalidate `SceneResolved` on
   those retained images: the effect graph cannot request resources that no
   longer exist. A fresh native display transfer republishes the scene
   capability. This avoids both empty intermediate frames and unresolved
   Scanout reads with Toon enabled. It does not fabricate guide data or change
   native composition order.
4. The display confirmation was inside F1, so hiding, collapsing or scrolling
   the panel could hide the only way to keep fullscreen. Confirmation is now a
   separate centered modal, registered with the normal host UI/input owner.
   It releases mouse capture, blocks game input and works with F1 closed.
   The 15-second rollback timer starts when the confirmation is drawn, not
   while the backend is still applying the new mode. Repeated UI frames do not
   renew the deadline. Keep persists the mode; Revert and timeout retain their
   original safety behavior.
5. F11 now submits the same typed presentation transaction as F1 instead of
   changing SDL directly behind the settings owner's back.

## Display contract

- Requested output settings are separate from observed output framebuffer and
  scene-image dimensions. F1 shows both actual extents; observations are not
  persisted as new user preferences.
- Borderless uses the desktop's actual dimensions; the requested-resolution
  controls are disabled in that mode. Internal scale remains independently
  adjustable. With an upscaler active, its quality owns the effective scale,
  and the inactive manual-scale slider is disabled rather than misleading.
- `ResolveScenePresentationPolicy` remains authoritative: wider-than-native
  output extends horizontal FOV, narrower output extends vertical FOV. Render
  scale does not change camera aspect. HUD/layout retain their separate native
  presentation contract. No stretching correction or scene-specific offset was
  introduced.

## Code map

Under `runtime/three_ds_recomp/`:

- `include/fast/oot3d/pica_display_transfer.h`,
  `src/fast/oot3d/pica_display_transfer.cpp`: shared native/host extent rules.
- `include/fast/oot3d/nri_pica_display_copy_pass.h`: NRI copy validation.
- `src/fast/backends/gfx_vulkan{,_pica}.cpp`: target lifetime, retained display
  capability invalidation, actual allocation reporting and fallback blit.
- `graphics_settings_runtime`, `graphics_settings_display_panel`,
  `graphics_settings_window`, `presentation_settings_transaction` under
  `fast/oot3d`: observations, controls and confirmation state machine.
- `src/fast/Fast3dGui.cpp`, `Fast3dWindow.cpp`: independent confirmation host,
  cursor/input ownership and F11 routing.

## Repeatable checks

- `oot3d_pica_display_transfer_tests.cpp`: native crop/filter cases, invalid
  commands, both-axis odd extents, and every integer percentage from 50 to 200
  at 720p, 4:3, 4K and an odd output extent (604 valid combinations). Six tests
  pass, including rejection of excess sampling and integer-overflow cases.
- `triaevum_f1_settings_smoke`: actual widgets, observed-versus-requested
  dimensions, confirmation while F1 is closed, delayed timer start and expiry.
  3,768 assertions pass, including UI frame invariants.
- `TRIAEVUM_DISPLAY_DIAGNOSTIC_SEQUENCE` is an explicit opt-in test schedule,
  applied through the normal settings transaction. Example:

```json
[{"frame":30,"scale":1.01},{"frame":60,"scale":0.73},
 {"frame":90,"width":1024,"height":768},
 {"frame":120,"width":1280,"height":720},
 {"frame":150,"width":3840,"height":2160},
 {"frame":180,"mode":1},{"frame":210,"confirm":true},
 {"frame":270,"mode":0,"width":1280,"height":720},
 {"frame":300,"confirm":true}]
```

Use `tools/triaevum_release/probe_renderer.py` with a private config and a
checkpoint matching the selected native/interpolated timing mode. Enable
`OOT3D_VULKAN_VALIDATION=1` for resource validation. Capture from the renderer,
not the desktop. Use an explicit frame count and wall-time safety bound.
The local harness and evidence are under
`J:/TriAevum-verify-20260910/probe-display.ps1` and `display-*` directories.

Final Windows/NRI run: `display-x2-180347`, 360 presented frames, interpolated
2x, the user's Toon/Grass settings, fractional scales 1.01 and 0.73, windowed
720p/4:3/4K, borderless, confirmation and return to windowed. Exit 0, zero
Vulkan/NRI validation errors and zero unresolved effect-graph reads. The
Vulkan validation layer reports 11 warnings; this is not a zero-warning claim.
Frame 120 no longer has the resize-boundary side bands. A separate held-4:3
run (`display-x2-175829`) checks stable framing at both scales. Captures are
direct framebuffer readbacks. Actual scene metrics include 1293x727 at 720p
and 1.01x, 748x561 at 1024x768 and 0.73x, and 2803x1577 at 4K and 0.73x.

These are correctness checks, not performance measurements. True high-resolution
targets cost more GPU memory and time than the previously upscaled cached image.
Windows/NVIDIA is the execution platform checked here; Linux/Android device
validation and the complete range of exclusive monitor modes remain separate.
