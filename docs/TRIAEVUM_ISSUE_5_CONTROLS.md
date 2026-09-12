# Issue 5: controller remapping

Source: [Possible Glitch](https://github.com/coccofresco/TriAevum/issues/5).
Status on 2026-09-09: investigated; reporter configuration requested; **not yet
confirmed fixed on the reporting controller**.

## Findings and Changes

- The published alpha.1c and current window use the same input path. SDL shoulder
  buttons and trigger axes are separate physical sources. F1 bindings resolve
  them into native L/R/ZL/ZR bits before the TopScreen item consumer reads them.
- A complete four-way reassignment survives serialization and reaches the correct
  native bits. A partial reassignment can leave L and ZL, or R and ZR, sharing a
  source. This produces simultaneous commands, but is not established as the
  reporter's cause. Do not silently rewrite existing custom configurations.
- F1 > Controls > Bindings now has **Swap shoulders / triggers**. It exchanges
  both physical pairs across the whole custom mapping, including item bindings,
  without changing keyboard/mouse bindings, saves, or gameplay. Pressing it again
  restores the original bindings. **Save controls** persists the result.
- The reusable implementation is `SwapGamepadSources` in
  `tools/three_ds/input/three_ds_input.{h,cpp}`. It has no SDL, window, renderer or
  OOT3D dependency. The F1 panel only edits the existing configuration.
- Standard TopScreen uses ZR/ZL for item I/II. Restoration uses ZR+X/Y instead;
  do not treat these as interchangeable profiles.

## Verification

- `oot3d_native_a32_input_tests`: all 24 permutations of the four sources,
  persisted/reloaded configuration, each individual press/hold/release (288
  input samples). No extra L/R bit can leak into a remapped item-only sample.
- `three_ds_recomp_input_tests`: reversible exchanges, repeated/shared bindings,
  unrelated bindings, other devices, and None/no-op safety.
- `triaevum_f1_settings_smoke`: actual renderer/Controls/TopScreen widgets, 1,761
  assertions, including live complete swap and reversal. SDK Clang 19.1.7.
- `oot3d_top_screen_mod_profile_tests`: passes. An existing icon-position fixture
  now explicitly selects its intended 90% scale instead of inheriting the new
  80% user default. Runtime layout was not changed to satisfy the test.
- Updated Linux runtime completed a 60.02-second physical GPU run, exit 0.
  Renderer framebuffer captures show title and scene. This verifies boot, not
  controller item use in gameplay. No physical controller reproduction yet.

The issue requests controller model, binding JSON/F1 screenshot, remapping layer
(F1 / Steam Input / controller software), and TopScreen layout. Keep it open
until the reported case is reproduced and corrected or the reporter verifies
the complete swap. Do not claim unit tests prove the reported gameplay behavior.

## Forge Comment in the Same Issue

A second user reports a TopScreen download failure. The exact error was
requested. A complete download through Forge's acquisition code succeeded:
179,430,508 bytes, pinned SHA-256 verified, 7.87 seconds. This does not establish
availability from another user's network.

The shared `tools/triaevum_release/verified_download.py` now provides at most
three attempts for transient HTTP/network failures and truncated downloads,
within one total time budget. Hash mismatch, oversized content, insecure URLs
and permanent HTTP failures remain errors. No partial archive becomes active;
the final diagnostic includes the source error and the offline archive option.
TopScreen extraction remains in its title-specific owner and never imports the
mod's executable patch. Tests cover transient recovery and integrity failures.

Private evidence (not for publication):
`I:/oot3dre_work/linux-port-proof/issue5-runtime.json`, `issue5-frame720.bmp`;
Linux `~/triaevum-forge-proof/issue5-gui.json` and
`~/triaevum-steamrt4-play/issue5-captures/`.
