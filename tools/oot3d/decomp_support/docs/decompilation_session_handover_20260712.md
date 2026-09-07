# OOT3D Decompilation Session Handover

## Objective

Progressively reconstruct maintainable C source and native data definitions for Ocarina of Time 3D from the European OOT3D executable and assets. The output must explain and reproduce the original OOT3D program, not adapt N64 data at runtime and not encode values observed only in an emulator.

Evidence order:

1. OOT3D `code.bin`, target ARM disassembly, native RomFS assets, literal/data tables, cross-references, and object-code comparisons.
2. Maintained OOT3D source and structure definitions already present in this project.
3. N64 zeldaret/OOT source as secondary architectural and semantic guidance. It may suggest names, roles, state machines, and gameplay behavior, but every promotion must be checked against OOT3D.
4. Azahar traces, PICA captures, savestates, and screenshots as validation and target-discovery evidence. Values seen only at runtime must be traced back to `code.bin` or native assets before becoming source or engine behavior.

The canonical policy is in `docs/native_source_decompilation_goal.md`.

## Repository And Current Revision

- Active repository: `I:\oot3dre`
- Active root branch: `oot3d-port`
- Current root checkpoint when this report was written: `9a3dfda58` (`Fix native horse dust atlas orientation`)
- OOT3D engine fork: `I:\oot3dre\libultraship`
- Current engine checkpoint: `8aa0fd46` (`Preserve native EffectSs atlas row order`)
- Original/full decomp workspace: `E:\ppssppvr\oot3d_decomp\oot3d_decomp_git`
- Vendored working subset: `I:\oot3dre\tools\oot3d\decomp_support`

Do not assume a clean worktree. At report creation the following tracked changes already belonged to the user/current work and must not be reverted:

- `tools/oot3d/decomp_support/analysis/cmab_material_animation_native_runtime.md`
- `tools/oot3d/oot3d_asset_tool/src/oot3d_asset_tool/cmb.py`

Numerous ignored/untracked captures and Ghidra exports also exist. Do not bulk-delete or bulk-add them.

## Verified Original Inputs

- Game image identity: European OOT3D, title ID `0004000000033600`, product code `CTR-P-AQEP`.
- Recorded game-image SHA-256: `D4670C962A6DD5C9CB953DE63E6316B1B03214D78CE3FD52B75CCA6BCFE6AEF2`.
- Extracted ExeFS: `E:\ppssppvr\oot3d_decomp\work\extract\exefs`
- Main executable: `E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin` (4,567,040 bytes).
- Extracted RomFS: `E:\ppssppvr\oot3d_decomp\work\extract\romfs`
- Other extraction roots: `E:\ppssppvr\oot3d_decomp\work\extract\contents` and `...\ncch0`.
- Extraction metadata: `metadata/oot3d_ctrtool_info.txt` and `metadata/romfs_manifest.json`.
- Full input/status record: `docs/status.md`.

Raw game files and full asset dumps must remain outside Git. This repository stores parsers, source reconstruction, compact reports, symbols, and selected evidence only.

## Ghidra Environment

- Installed Ghidra launcher: `E:\azahar pcvr\tools\pcvr-re-tools\ghidra\ghidraRun.bat`
- Ghidra installation root: `E:\azahar pcvr\tools\pcvr-re-tools\ghidra`
- Project automation: `tools/oot3d/decomp_support/ghidra_scripts`
- Generated baseline export: `tools/oot3d/decomp_support/ghidra_export`
- Full disassembly: `ghidra_export/disassembly.txt`
- Function indices: `ghidra_export/functions.csv` and `ghidra_export/functions_selected.csv`
- Decompiled baseline: `ghidra_export/decompiled/`
- Maintained function names: `symbols/manual_symbols.csv`
- Maintained data labels: `symbols/data_symbols.csv`

Important separation:

- `ghidra_export/` is generated evidence. Do not hand-maintain pseudocode there.
- `src/`, `include/`, `symbols/`, and selected analysis documents are the maintained reconstruction.
- Focused exports under `analysis/*_ghidra_export/` are disposable/reproducible investigation packets unless explicitly promoted.

Preferred focused checkpoint:

```powershell
cd I:\oot3dre\tools\oot3d\decomp_support
.\scripts\ghidra-export-selected.ps1 -Entries 0029041c,002907a4 -ApplyManualSymbols -IncludeCallers -IncludeCallees
```

Export all pending reviewed symbols only when refreshed neighborhoods are needed:

```powershell
.\scripts\ghidra-export-selected.ps1 -PendingManualSymbols -ApplyManualSymbols -IncludeCallers -IncludeCallees
```

Full re-export is a milestone operation, not an inner-loop gate:

```powershell
.\scripts\ghidra-apply-manual-symbols.ps1 -Reexport
python .\scripts\analyze_exports.py
```

Useful Ghidra scripts include:

- `ExportOot3d.java`: complete export.
- `ExportOot3dSelected.java`: selected targets and neighborhoods.
- `ApplyManualSymbols.java`, `ApplyDataSymbols.java`, `ApplyFunctionSplits.java`: maintained metadata application.
- `ExportOot3dReferences.java`, `ExportOot3dDataWords.java`: targeted evidence extraction.
- `FindOot3dOffsetAccesses.java`: structure-field access discovery.
- `FindOot3dPicaRegisterWriters.java`, `FindOot3dDirectPicaCommandWriters.java`: graphics-state producers.
- `FindOot3dPicaPacketCopies.java`, `FindOot3dDescriptorPacketCopies.java`: packet ownership/copy paths.
- `FindOot3dLightSettingConsumers.java`: scene-light consumers.

## Maintained Source Layout

- General headers and recovered structures: `include/oot3d/`
- Maintained source: `src/`
- Recovered game/system code: `src/code/`
- Recovered actor lanes: `src/overlays/actors/`
- Exact/structural seed source: `src/oot3d_public_structural_exact.c`
- Semantic declarations: `src/semantic_labels.c`
- Sources included in exact object comparison: `metadata/matched_sources.txt`
- Structured, not-yet-exact units: `metadata/structured_port_units.csv`
- N64/OOT3D mappings: `metadata/n64_port_map.csv` and `metadata/n64_to_oot3d_adapters.csv`

Substantial maintained scene/cutscene reconstruction already exists under `src/code/`, including:

- scene, setup, room, object, path, light, actor, entrance, exit, and collision source tables;
- native cutscene source/command tables and runtime context;
- player actions, miscellaneous actions, lighting, set-time, and transition handoff;
- camera blobs, keyframes, CMAD curves, curve evaluation, and camera runtime;
- open-title orchestration, actors, camera, logo, player motion, and playback runtime.

These files are useful decompilation results and testable hypotheses, but their exactness status must be read from the generated comparison reports rather than inferred from compilation alone.

## Native Asset Parsers And Engine Evidence

- Native asset tool: `tools/oot3d/oot3d_asset_tool`
- Tool documentation: `tools/oot3d/oot3d_asset_tool/README.md`
- Python package: `.../src/oot3d_asset_tool/`
- Principal formats include ZAR, ZSI, CMB, CSAB, CMAB, CTXB, and related native scene/material records.
- Native engine implementation: `runtime/three_ds_recomp/src/oot3d/`
- OOT3D asset/engine tests: `libultraship/tests/oot3d_native_resource_contract_tests.cpp`

The parsers and engine are valuable semantic evidence because they encode many structure layouts already validated against assets and PICA output. They are not automatically decompiled source. When a parser field or engine behavior is promoted into reconstructed game source, identify its native producer/consumer in assets or `code.bin`.

Recent examples of code-backed reconstruction are `Oot3dNativeEffectSs.cpp` and the focused exports in `analysis/title_intro_effectss_dust_ghidra_export/`. They demonstrate the expected chain: decode constants and function paths from `code.bin`, validate against PICA, then implement the general native contract.

## Scene And Cutscene Data Already Extracted

The most useful machine-readable native indexes are in `analysis/`:

- `scene_source_table.{json,csv,md}`
- `scene_setup_source_table.*`, `scene_room_source_table.*`, `scene_object_source_table.*`
- `scene_actor_source_table.*`, `scene_path_source_table.*`, `scene_light_source_table.*`
- `scene_collision_source_table.*` and its header/surface/water/bgcam CSVs
- `scene_command_source_table.*` and `scene_command_decompilation_workorders.*`
- `scene_cutscene_source_table.*`
- `scene_cutscene_native_source_table.*` and `scene_cutscene_native_decode_table.*`
- `scene_cutscene_player_action_table.*`, `scene_cutscene_misc_action_table.*`
- `scene_cutscene_lighting_table.*`, `scene_cutscene_set_time_table.*`
- `scene_cutscene_camera_blob_table.*`, `scene_cutscene_camera_keyframe_table.*`
- `scene_cutscene_camera_cmad_table.*`, `scene_cutscene_intro_camera_timeline.*`
- `scene_cutscene_runtime_context.*` and transition handoff tables
- `title_intro_opening_orchestration.{json,md}` for a complete worked example.

Regenerate these with the corresponding `scripts/build_scene_*.py` and `scripts/build_title_intro_*.py` tools. JSON/CSV tables are generated outputs; change the parser, maintained source, symbol map, or input evidence rather than patching generated rows manually.

## Decompilation Queues And Status Reports

Start a new session by reading:

1. `docs/native_source_decompilation_goal.md`
2. `docs/status.md`
3. `analysis/native_source_decompilation_queue.md`
4. `analysis/c_conversion_readiness.md`
5. `analysis/c_reconstruction_frontier.csv`
6. `analysis/structured_c_match_gate.md`
7. `analysis/porting_metrics.md`
8. `analysis/symbol_overlay.md`

Refresh the OOT3D-first queue:

```powershell
cd I:\oot3dre\tools\oot3d\decomp_support
python .\scripts\build_native_source_decompilation_queue.py
```

At the last documented queue generation, the recommended smallest structured target was entry `00398484`, `oot3d_boss_va_zapper_intro`, in `src/overlays/actors/ovl_Boss_Va/z_boss_va.c`:

```powershell
python .\scripts\structured_c_match_gate.py --unit boss_va_zapper
```

Re-run the queue before accepting that priority because reports may have changed during later engine work.

## ARM Compile And Exact-Match Lane

The project has repeatedly used GNU `arm-none-eabi` tools. The supported explicit tool root documented by the project is:

```text
C:\Users\xander\Downloads\arm-gnu-toolchain-15.2.rel1-mingw-w64-x86_64-arm-none-eabi
```

Build and compare maintained source:

```powershell
cd I:\oot3dre\tools\oot3d\decomp_support
.\scripts\build-matched-objects.ps1 -ToolRoot C:\Users\xander\Downloads\arm-gnu-toolchain-15.2.rel1-mingw-w64-x86_64-arm-none-eabi
python .\scripts\report_porting_metrics.py
```

Outputs are under ignored `build/matched/` and include compiled objects, objdump output, a build manifest, and normalized comparison reports.

Rules:

- `exact-c` may be promoted after semantic review.
- `codegen-near` remains a work item; explain the mismatch before promotion.
- Compiling successfully is not proof of equivalence.
- Local optimization attributes are acceptable only when a focused probe proves the target profile.
- Inline assembly is an explicitly documented interim anchor, not the desired final reconstruction.
- Keep non-exact structured work outside `metadata/matched_sources.txt`.

Useful focused commands:

```powershell
python .\scripts\extract_target_function.py --help
python .\scripts\structured_c_match_gate.py
.\scripts\refresh-structured-port-units.ps1
python .\scripts\probe_leaf_pseudocode_c.py --promote-exact
```

## Symbol Promotion Workflow

Read `docs/manual_decompilation.md`. For a confirmed function:

```powershell
python .\scripts\promote_manual_symbol.py `
  --entry 0036df4c `
  --new-name oot3d_copy_u32x3 `
  --source-file src/runtime/runtime_helpers.c `
  --confidence high `
  --notes "Copies three 32-bit words from src to dst."
python .\scripts\validate_manual_symbols.py
python .\scripts\validate_data_symbols.py
```

Use `promote_manual_symbols_batch.py` for reviewed batches. Do not rename from an N64 similarity score alone. Confirm control flow, constants, offsets, callers/callees, and runtime role against OOT3D.

The Python symbol overlay lets reports use pending maintained names before a costly Ghidra checkpoint. Consult `analysis/symbol_overlay.md` to distinguish overlay-only names from names already applied to the Ghidra project.

## N64 Source: Correct Use

Read `docs/n64_porting_workflow.md`. The local zeldaret/OOT checkout is referenced by the decomp scripts through the project’s external source configuration. N64 is strategically useful for:

- actor state-machine architecture and helper roles;
- scene/cutscene command semantics;
- names for gameplay structures, enums, actions, and fields;
- candidate C source shape where OOT3D retained gameplay logic.

It is not proof for:

- 3DS graphics/PICA backend behavior;
- OOT3D-specific resource formats and archive ownership;
- struct offsets, function boundaries, compiler lowering, or constants that differ in OOT3D.

Fast no-Ghidra refresh:

```powershell
.\scripts\refresh-n64-porting-plan.ps1
```

Key outputs:

- `analysis/n64_importability.md`
- `analysis/n64_port_candidate_rank.md`
- `analysis/n64_batch_porting_plan.md`
- `analysis/n64_lane_sources/`
- `analysis/port_packets/` and `analysis/port_batches/`

Prefer `direct-import-test` rows for exact C experiments. Treat `split-or-subsystem` and fan-out rows as structure/architecture work, not direct rename candidates.

## Azahar Validation Environment

- Active emulator executable: `E:\azahar pcvr\build-pcvr-qt\bin\Release\azahar.exe`
- Instrumented source repository: `E:\azahar pcvr`
- Ghidra tools bundled with that workspace: `E:\azahar pcvr\tools\pcvr-re-tools\ghidra`
- Captures in this project: `I:\oot3dre\captures\azahar_pica`
- Trace launcher: `scripts/azahar-trace.ps1`
- Documentation: `docs/azahar_runtime_trace.md` and `docs/emulator_instrumentation.md`
- Trace schema: `schemas/runtime_trace_event.schema.json`

Known savestate references from the engine work:

- slot 4: Link’s house investigations;
- slot 5: Kokiri Forest after leaving Link’s house;
- slot 6: opening title intro, already in progress;
- slot 8: title-intro mountain/ring view;
- slot 9: title-intro tree view.

Do not repeatedly restart or trace the emulator without a specific unresolved question. Use static `code.bin` and existing captures first. Instrument Azahar when runtime ownership, register production, dynamic dispatch, or temporal ordering cannot be established statically. Emulator output validates the reconstruction; it must not become the runtime data source.

PICA-focused evidence and analyzers already cover lighting, fog, TextureEnv, material scalars, sky/environment primitives, lens effects, actor draws, skeletal animation, and EffectSs. Search `captures/azahar_pica/` and matching `analysis/*findings.md` before creating another capture.

## Recommended Next-Session Procedure

1. Confirm `git status` and preserve the two known tracked user changes.
2. Read the goal, status, queue, structured gate, and metrics files listed above.
3. Regenerate `native_source_decompilation_queue` and select one bounded target or one coherent table subsystem.
4. Open its current Ghidra pseudocode, target disassembly, callers/callees, data references, maintained source, and any N64 candidate packet.
5. Run a focused Ghidra export only if the current export lacks the required neighborhood, split, type, or data words.
6. Write structured C/types in `src/` and `include/`; do not edit generated Ghidra pseudocode as source.
7. Compile with the ARM matched lane and inspect the normalized instruction delta.
8. Use Azahar only for a specific remaining runtime semantic question.
9. Promote names/data labels only after OOT3D evidence is coherent.
10. Validate symbols, rebuild the relevant structured unit, regenerate metrics, and commit each meaningful exact or semantic milestone.

## Strategic Priorities

For broad game decompilation, prefer work that unlocks many consumers:

1. Stable core structs and runtime context fields proven by offset-access clusters.
2. Scene/cutscene command dispatch, tables, and readers already indexed from native assets.
3. Actor profile, lifecycle, object/resource ownership, and common animation interfaces.
4. Shared math, memory/copy, list, archive, and resource helpers.
5. Coherent actor/subsystem batches with strong N64 architectural correspondence.
6. PICA/backend producers only where needed to explain native material/environment behavior; do not conflate emulator implementation with game source.

Avoid a loop of tiny gates. Use exact matching as a promotion criterion, not as a reason to stop all semantic reconstruction. A useful work unit should either become exact C, establish reusable structures/prototypes, decode a general native table, or remove a well-defined blocker for a coherent subsystem.

## Commit And Verification Discipline

- Commit meaningful progress frequently, as requested by the project owner.
- Report exact-match counts from `analysis/porting_metrics.md` after source-porting milestones.
- Record addresses, input files, source hashes where relevant, and the evidence chain in maintained notes.
- Never commit raw game images, complete proprietary asset dumps, large transient captures, build outputs, or savestates.
- Never revert unrelated user changes or clean the large capture workspace as part of decompilation work.
- Keep engine changes in the `libultraship` fork minimal and native-format-oriented; reconstructed game logic belongs in `decomp_support`, while native asset delivery belongs in the asset/parser layer.

## Quick Start

```powershell
cd I:\oot3dre
git status --short
cd .\tools\oot3d\decomp_support
Get-Content .\docs\native_source_decompilation_goal.md
python .\scripts\build_native_source_decompilation_queue.py
Get-Content .\analysis\native_source_decompilation_queue.md
Get-Content .\analysis\structured_c_match_gate.md
Get-Content .\analysis\porting_metrics.md
```

Then choose one bounded queue item, inspect its OOT3D target evidence, and use the matched ARM object lane as the implementation loop.
