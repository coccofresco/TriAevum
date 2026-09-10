# TopScreen Product Item Dispatch

2026-09-10, based on `port/linux-nri` at `5c6d93f`. Selectively adapted from
[999sian's PR #13](https://github.com/coccofresco/TriAevum/pull/13), head
`800345379958fc7d9c03555e8ecb28c1f8e44dea`. This closes a product routing gap,
not every remaining TopScreen input issue.

## Implementation

The product registered camera exits but not item getters/slot resolution or
Items-page assignment continuations. Merely registering candidate functions did
not interrupt their compiled callers. The product callback also rejected those
PCs. Both sides now use the same item-hook registry.

- `tools/oot3d/ui_topscreen/oot3d_top_screen_item_dispatch.{h,cpp}` owns the seven
  entry points, eligibility, query/slot execution, assignment continuation and
  counters. It reuses existing verified mod/native query contracts.
- `oot3d_native_a32_window.cpp` registers that list for both product and
  development paths and delegates to the module. Camera behavior is unchanged.
- Ordinary calls stay compiled. An observed but declined hook resumes compiled
  execution once, preserving the existing skip-initial-observation behavior.
- Assignment destinations are validated before clearing action flags or changing
  selection/cursors. A declined hook does not return partially modified guest
  memory to native code. Loadstate resets pending selection as before.
- Guest addresses stay in the OOT3D adapter. No renderer, platform backend,
  title-module ABI, save format or user configuration change is required.

Not adopted: PR #13's six-refresh press expiry, host input booleans and consuming
latch. The existing pressed snapshot remains a tick-wide query: two calls in
one tick may both observe it; held-only input in a later tick is not a new press.
Short host taps between guest polls and any future pending-edge policy remain
work for the input owner, not this dispatch module.

## Verification

`oot3d_top_screen_mod_profile_tests` now also runs
`oot3d_top_screen_item_dispatch_tests.cpp`. Passing cases include the shared
registry, inactive/unknown calls, all four getters, repeated queries, native
suppression/results, held versus pressed, ordinary/special slots, both assignment
targets, release before return, native refusal, incomplete memory and load reset.
The existing profile/controls/camera tests also pass.

Linux incremental product and test builds completed without title recompilation.
The new module and tests also compile with NDK r29 for Android ARM64. This is a
compile check, not Android gameplay verification. No Windows product rebuild or
physical controller test was performed in this tranche.

Live Linux Vulkan comparison used the same private `hudtest.oot3dsav`, native
fixed timing, no interpolation and 600 presentations on each executable:

| Counter | Before | After |
| --- | --- | --- |
| Item query calls (I pressed, II pressed, I held, II held) | 0 / 0 / 0 / 0 | 80 / 78 / 122 / 82 |
| True query results | 0 / 0 / 0 / 0 | 2 / 2 / 82 / 82 |
| Query entries reached | not reported | 362 |
| Assignment begins / completions | not reported | 0 / 0 |
| Slot overrides | 0 | 0 |

The input sequence supplies two ZR and two ZL presses, D-pad inputs and pause
open/close. The Items-page framebuffer was inspected: this checkpoint's
inventory is empty. **Positive assignment and equipped-item use are therefore
not qualified by that comparison.** Do not interpret getter hits as proof that an
object was assigned/used. A subsequent original-save fixture now provides a
populated adult inventory: boot and rendering pass, but assignment still records
zero begins/completions. Paired guest-refresh phases produce different getter
results. See [native-save verification](TRIAEVUM_NATIVE_SAVE_VERIFICATION.md)
for reproducible fixtures and the remaining input/assignment investigation.
Nor do these four presses establish exactly-once behavior for all input timing.

Both runs exit normally; all five framebuffer captures and final guest memory
and process fingerprints match. A separate 900-presentation title run produces
six captures byte-identical to the previous baseline. These are correctness
checks, not FPS measurements. Private evidence is retained outside the repo in
the Linux test workspace's `triaevum-pipeline-live-proof/pr13-*` directories.

## Repeatable Probe

`tools/triaevum_release/probe_renderer.py` accepts `--load-state` and
`--input-timeline`, retaining its bounded lifetime and private settings/save/cache
outputs. Native-save seeding and checkpoint capture are also supported; its four
Python tests pass. The checked-in sequence is
`tools/oot3d/native_game_runtime/input_timelines/topscreen_product_items_probe.json`;
its frame origin is the run, independent of checkpoint age. ROMs and save states
are not included.

```sh
python3 tools/triaevum_release/probe_renderer.py INSTALL RUNTIME OUTPUT \
  --profile TriAevum.linux.launch.json --native-fidelity --frames 600 \
  --seconds 60 --capture-interval 100 --load-state CHECKPOINT \
  --input-timeline tools/oot3d/native_game_runtime/input_timelines/topscreen_product_items_probe.json
```

Attribution is preserved in [Community Contributions](TRIAEVUM_CONTRIBUTIONS.md)
and the integration commit. This selective integration does not close the PR.
