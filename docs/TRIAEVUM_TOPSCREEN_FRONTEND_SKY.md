# TopScreen Frontend Sky Composition

## Defect and Evidence

The TopScreen file-select menu rendered on black instead of the original
upper-screen sky. Native framebuffer capture reproduced the report.
The PICA trace proved that the sky was still drawn, but into a different target
from the menu. Removing CommonBackground00 and the menu clear was insufficient.

The recovered target mapper at payload `0x005C7834` swaps native command
`0x400` to renderer binding `+0x3C` and `0x401` to `+0x38`. Its disassembly is
in `tools/oot3d/decomp_support/analysis/topscreen_payload_ghidra_export/disassembly_selected.txt`.
Our desktop frontend requires the original sky behind the relocated menu,
not that target swap. The native top transfer also occurs before the relocated
menu draws, so simply binding both to the top target would capture only the sky.

## Ownership and Fix

`tools/oot3d/native_game_runtime/oot3d_top_screen_frontend_composition.h`
owns this title/profile-specific composition policy:

- Only an active native frontend under TopScreen uses it. Gameplay, pause-page
  routing and the unmodified OoT3D profile keep their existing policies.
- Both scene and relocated menu bind the native upper target. The scene uses
  the full native upper viewport; the menu retains its centered native viewport.
- Backdrop suppression matches the actual background texture's target AND
  viewport, so it cannot discard the scene's clear on the now-shared target.
- Only the host's selected top snapshot moves after the final draw to that
  target. Draw order, resources, guest commands, guest interrupt timing, and
  unrelated display transfers are not changed.
- No sky image is synthesized or loaded as a replacement, no bottom framebuffer
  is promoted, and no shared NRI/PICA renderer code is modified.

`oot3d_native_a32_window.cpp` is the integration boundary: native target routing,
background-canvas classification, and the completed frame before interpolation.

## Verification

Fast ROM-free regression test (about three seconds including compilation):

```powershell
./tools/triaevum_release/tests/run_frontend_composition_tests.ps1 -Build I:/oot3dre_work/triaevum-direct-module-build -Compiler I:/oot3dre_tools/llvm-22.1.6/bin/clang++.exe
```

It covers both native viewport widths, inactive/disabled/unknown commands,
scene-clear preservation, target separation, selected snapshot ordering,
unrelated transfers, idempotence and empty frames.

Bounded boot-to-file-select capture with isolated configuration and save data:

```powershell
python -m tools.triaevum_release.validate_frontend_run --runtime I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe --profile I:/TriAevum-0.6.0-candidate-r1/TriAevum.launch.json --config I:/TriAevum-0.6.0-candidate-r1/data/config/TriAevum.json --output I:/oot3dre_work/frontend-sky-diagnostics/new-run --seconds 60
```

Add `--trace` for native PICA JSONL evidence. Validation and full traces are
deliberately not performance measurements. Structural success requires native
file-select activity, no bottom promotion/overlay and no Vulkan/NRI errors;
framebuffer images must also be inspected. Presentation frame numbers are not
guaranteed to align across cold starts; compare the same native menu state.

2026-09-06 evidence remains private under `I:/oot3dre_work/frontend-sky-diagnostics/`:
`before/framebuffer_000180.bmp` reproduces the black background;
`after/pica.jsonl` proves the shared target with the sky clear retained and only
the menu backdrop/clear suppressed (293 diagnostic frames, zero Vulkan/NRI errors);
`after-captures/framebuffer_000240.bmp` shows the complete file-select menu over
the native blue sky. These are frontend-specific results, not a claim of
game-wide visual parity or testing every other menu.

The rebuilt executable is `I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe`.
The development launcher `I:/oot3dre_work/cacao-release-diagnostics/Avvia-TriAevum-corretto.cmd`
uses it with the installed user's profile. The catalogued installed release
executable is not silently overwritten; release packaging must refresh its
matching catalogue and source receipts.
