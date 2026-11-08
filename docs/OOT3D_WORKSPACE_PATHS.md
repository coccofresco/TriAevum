# OOT3D workspace paths: translated code, savestates, scenes

Date: 2026-09-23. Written while completing the raw TextureCopy work, after
several sessions were spent re-discovering where things are. Read this together
with `tools/oot3d/operational_inputs.json`, which is the machine-readable index
(`scripts/oot3d/*.ps1` resolve their paths from it, and
`scripts/oot3d/Test-Oot3dOperationalInputs.ps1` validates it). This document is
the narrative map; the manifest is the authority for exact paths.

Rule: **check `tools/oot3d/operational_inputs.json` before searching the disks.**
If a path below changes, update the manifest first, this file second.

Two trees are involved and they are not the same:

| Tree | Path | Role |
| --- | --- | --- |
| Public clean repository | `<public-work>/source-repository-clean` | The tree under review, tracked, `port/linux-nri`. No ROM-derived data, no captures. |
| Development workspace | `<local>/oot3dre` (`repoRoot` in the manifest) | Where the code.bin reconstruction, generated trees and experiments live. Many manifest entries point here. |
| Separate renderer checkout | `<local>/oot3d-native-renderer` | Its own git history (`integration-source`, remote `<local>/oot3dre-vulkan/libultraship`) with a scratch build in `.renderer-dev/build`. Not this repository and not the development workspace; check before assuming a path belongs to either. |
| Verification build | `<verify-20260910>/runtime` | Ninja + clang-cl tree configured against the clean repository. Test executables land in `<public-work>/ui-dependencies/bin`. |

## 1. Translated title code

The title logic ships precompiled (`triaevum_title_aot.dll`); its corresponding
source is what to read when a question is about what the guest actually does.

| What | Path |
| --- | --- |
| Generated whole-AOT C++, one directory per product hash: `cpp/oot3d_whole_aot_generated.{h,cpp}`, `cpp/oot3d_whole_aot_shard_NNN.cpp`, `aot_program.json`, `product_artifacts.json`, `whole_aot_archive.json` | `<oot3d-work>/oot3d-whole-aot-product-cache/<hash>/` |
| Loose generated trees from earlier experiments | `<oot3d-work>/native_game/whole_aot_kokiri_main_top12/`, `<oot3d-work>/native_game/whole_aot_navi_pica_flush/` |
| Generator and selection indices | `tools/oot3d/native_a32_runtime/generate_whole_aot_cpp.py`, `whole_aot_cpp.py`, `whole_aot_program.py`, `whole_aot_functions.json`, `whole_aot_product_manifest.json`, `whole_aot_product_golden.json` |
| Structured decomp evidence inside the repository (struct layouts, symbols, metadata, analysis, Ghidra helpers) and the native actor layout headers the game includes (`include/oot3d/actor{,_spawn}.h`) | `tools/oot3d/decomp_support/` |
| Corresponding source shipped with the package | `<public-work>/releases/v0.6.0-alpha.3/title-build-source/` (`SOURCE_ARCHIVE_MANIFEST.json` pins the commit and the renderer subtree hash) |
| External reconstruction checkout (read-only evidence) | `<legacy-recomp>` (with `analysis/`, `ghidra/`, `build/`), original image at `<ppssppvr>/oot3d_decomp/work/extract/exefs/code.bin`, ROMFS at `.../extract/romfs` |

Legacy A32 trees: `oot3d_a32_generated.h` also exists at
`<local>/oot3d-native-renderer/.renderer-dev/build/oot3d_a32_generated/`,
`<oot3d-work>/mods/topscreen_single_screen/a32_generated/` and twice under
`<legacy-recomp>/build/`. All of them are **declaration-only stubs** of
162-301 bytes (`GetA32GeneratedRegistry()` and nothing else), not translations;
do not treat them as generated title code, and do not expect them to satisfy a
target that needs the real A32 registry. The legacy generator writes
`<build>/oot3d_a32_generated/` (`tools/oot3d/build_fast_dev.ps1`), the current
one `<build>/oot3d_whole_aot_cpp/` (CMake option `OOT3D_WHOLE_AOT_GENERATED_DIR`).

The verification build does **not** regenerate this code:
`OOT3D_REBUILD_WHOLE_AOT=OFF` in its `CMakeCache.txt`, and
`OOT3D_WHOLE_AOT_GENERATED_DIR` (empty today) names the directory that would
hold a regenerated tree. Read the product cache above instead.

Measured on the cache entry inspected on 2026-09-23 (260 generated files,
8,690,740 lines): `ReadFast` 311,191 uses, `WriteFast` 189,552, `ReadBytes` 0,
`GetReadPointer` 0, `GetWritePointer` 0. The generated title code therefore
reaches guest memory only through our typed memory object; it never asks for a
raw pointer. That matters for any plan that has to know whether guest code can
hold a pointer into a surface the renderer is about to replace.

Build and iteration entry points: `scripts/oot3d/Build-Oot3dLlvmWholeAot.ps1`,
`Configure-Oot3dWholeAotProduct.ps1`, `tools/oot3d/build_fast_dev.ps1`
(`-Target`, `-BuildDirectory`, `-PreflightOnly`, `-Configure`).

## 2. Savestates

| What | Path |
| --- | --- |
| Scenario checkpoints used by the product scenario list, plus their JSON profiles (117 files) | `<oot3d-work>/native_game/checkpoints/` |
| Issue-specific savestates and captures: `sheik-*` (raw TextureCopy, revelation, overlap), `gameover-*` (composition variants), `zero-health`, `sheik-before-event` | `<diagnostics>/issues45-46/` |
| Iteration states: `sheik-near-copy/checkpoint.oot3dsav` sits 73 run frames before the first raw copy, so a 400-frame run (about 37 s) reaches the phase instead of replaying 3,500 frames; provenance and commands in its README. Use it for iteration, the full run for qualification. | `<diagnostics>/issues45-46/` |
| Other topic evidence: performance campaigns, lighting/TEV probes, Y2R codec, revelation freeze diagnostics | `<diagnostics>/*` |
| Scenario list with checkpoint filename, SHA-256, input profile and frame budget (8 scenarios: `file_select_empty_slots`, `link_dream`, `navi_kokiri_flight`, `kokiri_preload_transition`, `link_house_gameplay`, `kokiri_hud_gameplay`, `kokiri_ladder_interaction`, `topscreen_items`) | `tools/oot3d/native_a32_runtime/whole_aot_product_scenarios.json` |

A savestate is loaded read-only with `--load-state <path>`; the probe copies
configuration and save data into its own output directory and never writes the
installation. `--save-state-frame N` writes `checkpoint.oot3dsav` into the probe
output, which is how a run is turned into a reusable state.

## 3. Moving between situations

Three mechanisms, in increasing order of effort:

1. **Deterministic input timelines (frontend paths).**
   `tools/oot3d/native_game_runtime/input_timelines/*.json` — title to file
   select, to an existing save, to a second slot, to empty-slot name entry,
   empty slot to name entry, new-file intro, ladder interaction, cutscene
   dialogue advance, no input. Used with `--input-timeline`.
   Graphics settings can change at run frames with
   `--graphics-settings-schedule <schedule.json>` (developer qualification;
   schema `oot3d.graphics_settings_schedule.v1`, steps of `frame` plus a
   partial `graphics` block merged onto the current settings and applied
   through the F1 commit path without persisting; stderr
   `TRIAEVUM_GRAPHICS_SETTINGS_SCHEDULE`). See
   `tools/oot3d/native_game_runtime/oot3d_graphics_settings_schedule.h`.
2. **Structural scenario catalog (scenes without a savestate per scene).**
   One quiescent gameplay state is used only as a live anchor; the original game
   code then performs the transition, so scene, room, actors, camera, lighting
   and cutscenes are built by the title, not fabricated by the harness.
   - Entry point: `scripts/oot3d/Update-Oot3dStructuralScenarioCatalog.ps1`;
     generator `tools/oot3d/native_a32_runtime/generate_structural_scenario_catalog.py`
     (joins the entrance table, scene setups, actor placements, light records and
     cutscene references); validator
     `scripts/oot3d/Test-Oot3dStructuralScenarioMatrix.ps1`.
   - The catalog is tracked at
     `tools/oot3d/native_a32_runtime/oot3d_structural_scenarios.json`
     (format `oot3d_structural_scenario_catalog_v2`, 82 recipes across 27
     scenes, each with entrance index, scene id, local entrance and validation
     status). It carries the runtime contract
     (`request_transition_function`, `prepare_transition_effect_function`,
     `direct_call_return_address`, `current_entrance_address`, `play_state`
     offsets) and per-recipe frame windows. Documentation:
     `docs/OOT3D_STRUCTURAL_SCENARIO_COVERAGE.md`.
   - Output and matrices: `<oot3d-work>/structural-scenarios/`
     (`all-entrances-v2/`, `compiled/`, `matrices/latest.json`,
     `matrices/<timestamp>/structural_scenario_matrix.json`).
   - Runtime: `--scenario-catalog <catalog.json> --scenario <id>`, implemented by
     `NativeScenarioBootstrap`
     (`tools/oot3d/native_game_runtime/oot3d_native_scenario_bootstrap.cpp`).
     It needs a **quiescent gameplay state as anchor** through `--load-state`:
     the recorded matrices use
     `<oot3d-work>/native_game/checkpoints/hud_minimap_visible_20260721.oot3dsav`.
     Starting from the title screen instead faults (`native A32 process faulted`,
     the transition request has no valid PlayState to run in).
3. **Named scenarios with their own checkpoint** (section 2), for paths that
   cannot be reached from a transition request.

Ready commands (private playable installation, packaged runtime):

```powershell
# revelation raw TextureCopy window, extended diagnostics, no captures
python tools\triaevum_release\probe_renderer.py `
  '<public-work>/releases/v0.6.0-alpha.3b/qualification-windows' `
  '<public-work>/ui-dependencies/bin/TriAevum.exe' <work>/attr3 `
  --native-fidelity --frames 3500 --seconds 400 --no-captures --extended-diagnostics `
  --load-state '<diagnostics>/issues45-46/sheik-native-trigger/checkpoint.oot3dsav' `
  --input-timeline tools\oot3d\native_game_runtime\input_timelines\native_cutscene_dialogue_advance.json

# structural scenario run through a native transition
python tools\triaevum_release\probe_renderer.py `
  '<public-work>/releases/v0.6.0-alpha.3b/qualification-windows' `
  '<public-work>/ui-dependencies/bin/TriAevum.exe' <work>/scenario `
  --native-fidelity --frames 600 --seconds 200 --capture-interval 60 `
  --load-state '<oot3d-work>/native_game/checkpoints/hud_minimap_visible_20260721.oot3dsav' `
  --scenario-catalog 'tools/oot3d/native_a32_runtime/oot3d_structural_scenarios.json' `
  --scenario link_info_entry_00bb
```

## 4. Building and verifying

| What | Command or path |
| --- | --- |
| Product and tests | `ninja -C <verify-20260910>/runtime <target>`; executables are written to `<public-work>/ui-dependencies/bin` |
| Title-neutral renderer boundary gate | `ninja -C <build> verify_renderer_3ds_cross_title_boundary` (also runs as a dependency of the cross-title canary and capture-conformance tests) |
| Actual-widget F1 smoke | `tools/triaevum_release/tests/run_f1_settings_smoke.ps1 -Build <build> -Dependencies <deps> -Compiler <clang++>`; it resolves the archive directory from the build cache |
| Probe and captures | `tools/triaevum_release/probe_renderer.py` (see section 3) |
| Renderer/AOT C++ oracles (separate CMake project) | `tools/renderer/tev_program/CMakeLists.txt`; `pica_ui_canvas_tests` builds standalone, `pica_lighting_gpu_tests` needs the Vulkan SDK shaderc |
| Known build gaps, unchanged | gmock (vendored googletest, no consumer) fails under `-Werror,-Wcharacter-conversion`; `oot3d_native_mass_aot_tests` needs a generated header this tree does not produce; the two `native_demo_host` executables need sources this tree does not carry; `oot3d_native_game_runtime_tests` needs `oot3d_native_runtime_host.lib` |

## 5. Cautions

- `<legacy-recomp>`, `<local>/oot3dre`, the decomp checkouts and the extracted ROMFS
  are read-only evidence. Copy what is needed into the repository deliberately.
- Never commit ROM images, keys, extracted assets, `code.bin`, savestates,
  captures, `game.tam`, `content.tap` or shader caches; the manifest's
  `known_absences` records where the originals belong.
- Savestates and captures are per-machine evidence: reference them by path in
  documents, do not copy them into the repository.
- Do not run a global reset or clean to "start over": other instances work in
  sibling worktrees and non-committed files have been destroyed that way before.
