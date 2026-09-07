# OOT3D Native Scene Index

This workspace exports OOT3D scene/room indices from native ZSI payloads with:

```powershell
$env:PYTHONPATH='I:\oot3dre\tools\oot3d\oot3d_asset_tool\src'
python -m oot3d_asset_tool export-zsi-scene-index 'E:\ppssppvr\oot3d_decomp\work\extract\romfs\scene' --scene link --scene spot04 --output .\tools\oot3d\decomp_support\analysis\oot3d_native_scene_index_link_spot04.json --markdown-output .\tools\oot3d\decomp_support\analysis\oot3d_native_scene_index_link_spot04.md --full-entries --sample-limit 32
```

Source-like C support files for decompilation are generated from the same complete native decode with:

```powershell
$env:PYTHONPATH='I:\oot3dre\tools\oot3d\oot3d_asset_tool\src'
python -m oot3d_asset_tool export-zsi-scene-index 'E:\ppssppvr\oot3d_decomp\work\extract\romfs\scene' --scene link --scene spot04 --output .\tools\oot3d\decomp_support\analysis\oot3d_native_scene_index_link_spot04.json --markdown-output .\tools\oot3d\decomp_support\analysis\oot3d_native_scene_index_link_spot04.md --c-output-dir .\tools\oot3d\decomp_support\src\assets\scenes --full-entries --sample-limit 32
```

The RomFS-wide source-like C dump for all decoded OOT3D scene indices is generated with:

```powershell
$env:PYTHONPATH='I:\oot3dre\tools\oot3d\oot3d_asset_tool\src'
python -m oot3d_asset_tool export-zsi-scene-index 'E:\ppssppvr\oot3d_decomp\work\extract\romfs\scene' --output .\tools\oot3d\decomp_support\analysis\oot3d_native_scene_index_all_full.json --markdown-output .\tools\oot3d\decomp_support\analysis\oot3d_native_scene_index_all_full.md --c-output-dir .\tools\oot3d\decomp_support\src\assets\scenes --full-entries --sample-limit 32 --no-room-mesh-summaries
```

The RomFS-wide lightweight index is generated with:

```powershell
$env:PYTHONPATH='I:\oot3dre\tools\oot3d\oot3d_asset_tool\src'
python -m oot3d_asset_tool export-zsi-scene-index 'E:\ppssppvr\oot3d_decomp\work\extract\romfs\scene' --output .\tools\oot3d\decomp_support\analysis\oot3d_native_scene_index_all_summary.json --markdown-output .\tools\oot3d\decomp_support\analysis\oot3d_native_scene_index_all_summary.md --sample-limit 8 --no-room-mesh-summaries
```

The scene-command decompilation workorder queue is generated from the RomFS-wide index with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_command_decompilation_workorders.py
```

Primary outputs:

- `tools/oot3d/decomp_support/analysis/scene_command_decompilation_workorders.md`
- `tools/oot3d/decomp_support/analysis/scene_command_decompilation_workorders.csv`
- `tools/oot3d/decomp_support/analysis/scene_command_decompilation_workorders.json`
- `tools/oot3d/decomp_support/analysis/scene_command_handler_function_splits.csv`
- `tools/oot3d/decomp_support/analysis/scene_command_handler_export_entries.txt`

The source-oriented OOT3D equivalent of the scene/setup/room indices is generated from the full native index with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_native_scene_source_index.py
```

Primary outputs:

- `tools/oot3d/decomp_support/analysis/oot3d_native_scene_source_index.md`
- `tools/oot3d/decomp_support/analysis/oot3d_native_scene_source_index.json`
- `tools/oot3d/decomp_support/analysis/oot3d_native_scene_source_index_scenes.csv`
- `tools/oot3d/decomp_support/analysis/oot3d_native_scene_source_index_setups.csv`
- `tools/oot3d/decomp_support/analysis/oot3d_native_scene_source_index_rooms.csv`
- `tools/oot3d/decomp_support/analysis/oot3d_native_scene_source_index_commands.csv`

This index is the review/query layer over the generated C sources. It maps every native scene basename to its generated C file and `Oot3dSceneIndex` symbol, every setup to its materialized payload symbols, every room binding to its object-prefixed actor array/object-bank array, and every scene command to its decoded payload count plus OOT3D `code.bin` support level. The current generated source map covers 114 scenes, 175 setups, 1050 scene-room bindings, 610 unique room files, 2132 scene commands, 8092 room actor entries, 6573 room object-bank entries, 598 spawn entries, and 1467 light-setting records. No scene command class is still marked semantic/consumer pending in that index; native path lists `0x0D` and exit lists `0x13` are now represented from OOT3D ZSI payloads plus OOT3D `code.bin` consumer evidence.

The workorder generator also reads the native `code.bin` scene command handler table at `0x0053CC84` from `E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin`. The current table resolves 14 non-null handlers for the commands observed in ZSI scene setup lists. Apply the generated function splits to the Ghidra project with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\oot3d\decomp_support\scripts\ghidra-apply-function-splits.ps1 -GhidraRoot 'E:\azahar pcvr\tools\pcvr-re-tools\ghidra' -JdkRoot 'E:\azahar pcvr\tools\pcvr-re-tools\jdk21' -ProjectDir 'E:\ppssppvr\oot3d_decomp\work\ghidra_project' -ProjectName 'oot3d_code' -SplitsCsv '.\analysis\scene_command_handler_function_splits.csv'
```

Then export the handler bodies, callers, and callees with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\oot3d\decomp_support\scripts\ghidra-export-selected.ps1 -GhidraRoot 'E:\azahar pcvr\tools\pcvr-re-tools\ghidra' -JdkRoot 'E:\azahar pcvr\tools\pcvr-re-tools\jdk21' -ProjectDir 'E:\ppssppvr\oot3d_decomp\work\ghidra_project' -ProjectName 'oot3d_code' -ExportDir 'analysis\scene_command_handlers_ghidra_export' -EntriesFile 'analysis\scene_command_handler_export_entries.txt' -IncludeCallers -IncludeCallees -KeepSelectedArtifacts
```

The runtime extraction report is generated from that focused Ghidra export with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_command_handler_runtime_map.py
```

Runtime-map outputs:

- `tools/oot3d/decomp_support/analysis/scene_command_handler_runtime_map.md`
- `tools/oot3d/decomp_support/analysis/scene_command_handler_runtime_map.json`

Downstream consumers for scene indices are scanned with Ghidra offset access evidence. The current focused scan covers `0x13 exit_list` at `play+0x5C1C` and `0x0D path_list` at `play+0x5C20`:

```powershell
$ghidra='E:\azahar pcvr\tools\pcvr-re-tools\ghidra'
$jdk='E:\azahar pcvr\tools\pcvr-re-tools\jdk21'
$project='E:\ppssppvr\oot3d_decomp\work\ghidra_project'
$env:JAVA_HOME=$jdk
$env:PATH="$jdk\bin;$env:PATH"
& (Join-Path $ghidra 'support\analyzeHeadless.bat') $project 'oot3d_code' -process code.bin -noanalysis -scriptPath '.\tools\oot3d\decomp_support\ghidra_scripts' -postScript FindOot3dOffsetAccesses.java 'tools\oot3d\decomp_support\analysis\scene_command_exit_path_offset_accesses.csv' '0x5c1c 0xc1c 0x5c20 0xc20 0x5c18 0xc18 0x5c02 0xc02'
python .\tools\oot3d\decomp_support\scripts\build_scene_command_index_consumer_workorders.py
python .\tools\oot3d\decomp_support\scripts\build_transition_request_helper_semantics.py
python .\tools\oot3d\decomp_support\scripts\build_scene_exit_transition_tables.py
python .\tools\oot3d\decomp_support\scripts\build_scene_command_exit_semantics.py
```

Consumer-workorder outputs:

- `tools/oot3d/decomp_support/analysis/scene_command_index_consumer_workorders.md`
- `tools/oot3d/decomp_support/analysis/scene_command_index_consumer_workorders.json`
- `tools/oot3d/decomp_support/analysis/scene_command_index_consumer_export_entries.txt`
- `tools/oot3d/decomp_support/analysis/transition_request_helper_semantics.md`
- `tools/oot3d/decomp_support/analysis/transition_request_helper_semantics.json`
- `tools/oot3d/decomp_support/analysis/scene_exit_transition_tables.md`
- `tools/oot3d/decomp_support/analysis/scene_exit_transition_tables.json`
- `tools/oot3d/decomp_support/include/oot3d/scene_transition.h`
- `tools/oot3d/decomp_support/src/code/z_scene_exit_transition_tables.c`
- `tools/oot3d/decomp_support/analysis/scene_command_exit_semantics.md`
- `tools/oot3d/decomp_support/analysis/scene_command_exit_semantics.json`

The native OOT3D scene-id to scene-resource table is exported from fixed `code.bin` records with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_resource_table.py
```

Scene-resource outputs:

- `tools/oot3d/decomp_support/analysis/scene_resource_table.md`
- `tools/oot3d/decomp_support/analysis/scene_resource_table.json`
- `tools/oot3d/decomp_support/analysis/scene_resource_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_resource.h`
- `tools/oot3d/decomp_support/src/code/z_scene_resource_table.c`

This export decodes 0x8C-byte native records with a raw 4-byte metadata trailer, a ZSI path, and a ZAR path. OOT3D function `0x002EAFB4` copies 14 selected records (`0x7A8` bytes) into the runtime table at `0x00545484`: the normal source is `0x004DC400` and the alternate source is `0x004DCBA8`. Scene ids `0x00..0x0D` are therefore selected-copy rows; scene ids `0x0E..0x6E` come from the static runtime tail at `0x00545C2C`. The current coverage is 111 scene rows, 102 matching extracted native ZSI files, 9 unindexed test/debug rows, and 14 indexed alternate selected-record variants. Rows now also bind to the generated native scene-index registry where available: 102/111 default rows and 14/14 alternate rows resolve to an `oot3d_scene_index_*` source symbol. The metadata trailer is preserved raw; byte0 matches the N64/SoH `unk_10` field for 110/110 compared labels and byte1 matches the N64/SoH scene draw config for 106/110 compared labels, but those are recorded as secondary correlations until the OOT3D consumer code is fully named.

The OOT3D global entrance table rows referenced by decoded native exit lists and verified high-remap rows are exported from `code.bin` with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_global_entrance_table.py
```

Global-entrance outputs:

- `tools/oot3d/decomp_support/analysis/scene_global_entrance_table.md`
- `tools/oot3d/decomp_support/analysis/scene_global_entrance_table.json`
- `tools/oot3d/decomp_support/analysis/scene_global_entrance_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_global_entrance.h`
- `tools/oot3d/decomp_support/src/code/z_scene_global_entrance_table.c`

This export infers the native OOT3D table extent from the `code.bin` table region itself, currently `0x0632` 4-byte entries at `0x00543BB8`, ending before the first 16-entry zero run. Referenced values outside that inferred extent are retained as unpromoted worklist entries in JSON/Markdown and are not emitted as `EntranceInfo` rows. Scene-id path resolution is primary-native via `scene_resource_table.json`; N64/SoH names remain secondary labels/fallback hints only. All 135 decoded referenced entrance rows currently resolve through the native scene-resource table to a generated `oot3d_scene_index_*` source symbol. The Kokiri slot 5 evidence resolves global entrance `0x0211` to scene id `0x55`, local entrance `3`, field `0x4204`, `spot04_info.zsi`, source symbol `oot3d_scene_index_spot04_info`, and validation status `native_kokiri_slot5_entrypoint_confirmed`.

The source-oriented OOT3D scene table joins native scene resources, referenced global entrances, and generated ZSI scene-index source files with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_source_table.py
```

Scene-source-table outputs:

- `tools/oot3d/decomp_support/analysis/scene_source_table.md`
- `tools/oot3d/decomp_support/analysis/scene_source_table.json`
- `tools/oot3d/decomp_support/analysis/scene_source_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_source_table.h`
- `tools/oot3d/decomp_support/src/code/z_scene_source_table.c`

This integration table is derived only from OOT3D-native artifacts. It currently emits 111 scene rows, 14 alternate selected-record variant rows, and 135 contiguous entrance references grouped by scene. 102/111 default rows resolve to generated native scene-index source symbols, the remaining 9 are the same unindexed test/debug resource rows reported by `scene_resource_table.json`, and 14/14 alternate variants resolve. `spot04_info.zsi` resolves to `oot3d_scene_index_spot04_info` and carries the referenced Kokiri entrance `0x0211` through the grouped entrance-ref array.

The source-oriented OOT3D scene-command table joins native ZSI command rows, `code.bin` command-handler evidence, and scene-source rows with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_command_source_table.py
```

Scene-command-source-table outputs:

- `tools/oot3d/decomp_support/analysis/scene_command_source_table.md`
- `tools/oot3d/decomp_support/analysis/scene_command_source_table.json`
- `tools/oot3d/decomp_support/analysis/scene_command_source_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_command_source_table.h`
- `tools/oot3d/decomp_support/src/code/z_scene_command_source_table.c`

This table currently emits 2132 scene-command rows and 14 handler rows. 1957 command rows resolve to a native `code.bin` handler, while the 175 missing-handler rows are all command `0x14` end markers with `native_control_marker` support. Scene binding is complete for emitted command rows: 2081 bind to default scene-source rows and 51 bind to alternate selected-record variants (`bdan_dd_info.zsi` and `ice_doukutu_dd_info.zsi`). The 46 decoded-count mismatches are all command `0x00` spawn-list rows where the native handler selects the active entrance spawn from the decoded list.

The source-oriented OOT3D scene-setup table groups those command rows back into their native ZSI setup lists with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_setup_source_table.py
```

Scene-setup-source-table outputs:

- `tools/oot3d/decomp_support/analysis/scene_setup_source_table.md`
- `tools/oot3d/decomp_support/analysis/scene_setup_source_table.json`
- `tools/oot3d/decomp_support/analysis/scene_setup_source_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_setup_source_table.h`
- `tools/oot3d/decomp_support/src/code/z_scene_setup_source_table.c`

This table currently emits 175 native setup rows and 2132 setup-command references, preserving the exact command slice for each setup plus generated payload symbols for spawns, entrances, exits, transition actors, light settings, paths, and cutscenes. Binding is complete: 171 setup rows bind to default scene-source rows and 4 bind to alternate selected-record variants. The 175 missing handler counts are the same `0x14` native end markers already classified by the command-source table, so every setup still has zero unresolved commands. `spot04_info.zsi` has 13 setup rows bound to native scene id `0x55`, which keeps the Kokiri slot 5 scene/setup evidence reachable without consulting N64 tables.

The source-oriented OOT3D scene-room table promotes native room bindings and decoded setup room-list references with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_room_source_table.py
```

Scene-room-source-table outputs:

- `tools/oot3d/decomp_support/analysis/scene_room_source_table.md`
- `tools/oot3d/decomp_support/analysis/scene_room_source_table.json`
- `tools/oot3d/decomp_support/analysis/scene_room_source_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_room_source_table.h`
- `tools/oot3d/decomp_support/src/code/z_scene_room_source_table.c`

This table currently emits 1050 native scene-room binding rows and 334 setup-room references decoded from command `0x04` room lists. Every setup-room reference resolves to a generated native room row, and every room row resolves to either a default scene-source row or an alternate selected-record variant. The table keeps `setupRefStart/setupRefCount` as contiguous C slices, making the generated source usable as a direct source-like index for room membership. Room actor/object payload coverage matches the native source index: 8092 room actor entries, 6573 room object-bank entries, 786 object-prefixed actor lists identified, 264 room files without an actor-list candidate, and 5 currently unknown OOT3D object IDs retained numerically. `spot04_info.zsi` exposes three room rows bound to native scene id `0x55`, each referenced by all 13 Kokiri setup rows.

The source-oriented OOT3D actor placement table promotes native setup and room actor payloads with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_actor_source_table.py
```

Scene-actor-source-table outputs:

- `tools/oot3d/decomp_support/analysis/scene_actor_source_table.md`
- `tools/oot3d/decomp_support/analysis/scene_actor_source_table.json`
- `tools/oot3d/decomp_support/analysis/scene_actor_source_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_actor_source_table.h`
- `tools/oot3d/decomp_support/src/code/z_scene_actor_source_table.c`

This table currently emits 9087 native actor placement rows: 598 setup spawn entries from command `0x00`, 2 standard actor entries from command `0x01`, 395 transition actor entries from command `0x0E`, and 8092 room actor entries from object-prefixed room actor lists. Every setup actor row resolves to a scene row, setup row, and command-source row; every room actor row resolves to a scene row and room-source row. The row-level `validationStatus` keeps the evidence tier visible instead of flattening it: 8092 room actors are `native_room_actor_object_prefix_confirmed`, 395 transition actors are direct native payloads, 98 setup actors are strong selected candidates, 405 are partial-strong, 84 are candidate, and 11 are weak candidates retained as an explicit low-confidence worklist. `spot04_info.zsi` contributes 100 room actors, 45 selected setup spawn entries, and 29 transition actors, all bound to native scene id `0x55`.

The source-oriented OOT3D room object-bank table promotes the object prefixes that precede native room actor lists with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_object_source_table.py
```

Scene-object-source-table outputs:

- `tools/oot3d/decomp_support/analysis/scene_object_source_table.md`
- `tools/oot3d/decomp_support/analysis/scene_object_source_table.json`
- `tools/oot3d/decomp_support/analysis/scene_object_source_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_object_source_table.h`
- `tools/oot3d/decomp_support/src/code/z_scene_object_source_table.c`

This table currently emits 6573 native room object-bank rows from 786 object-prefixed room actor lists. Every row resolves to a room-source row and scene-source row, with zero room object-count mismatches against `scene_room_source_table.json`. 6568 rows are known object IDs and 5 rows remain `unknown_oot3d_object_id`, preserved numerically as explicit worklist entries: `OBJECT_0x03db` in `hakaana2_0_info.zsi`, `OBJECT_0x036f` in both `hidan_19_info.zsi` variants, and `OBJECT_0x0194` in `spot04_0_info.zsi` plus `tokinoma_1_info.zsi`. `spot04_info.zsi` contributes 21 object-bank rows across its three room files, all bound to native scene id `0x55`.

The source-oriented OOT3D light-settings table promotes native command `0x0F` payload records with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_light_source_table.py
```

Scene-light-source-table outputs:

- `tools/oot3d/decomp_support/analysis/scene_light_source_table.md`
- `tools/oot3d/decomp_support/analysis/scene_light_source_table.json`
- `tools/oot3d/decomp_support/analysis/scene_light_source_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_light_source_table.h`
- `tools/oot3d/decomp_support/src/code/z_scene_light_source_table.c`

This table currently emits 1467 native `0x1C` light-setting rows from 175 setup light commands. Every row resolves to a generated setup-source row and command-source row, with zero scene/setup/command binding gaps and zero setup or command light-count mismatches. The promoted field layout is the OOT3D-native runtime layout consumed by `code.bin` function `0x0045DD50`: ambient RGB, two signed light directions, two light RGB colors, and fog/environment RGB. The earlier `legacy_env_light_settings_prefix_candidate` and `actor_vs_packet_candidate` fields remain diagnostic-only in the full JSON index and are not promoted as primary source semantics. `spot04_info.zsi` contributes 105 light rows, all bound to native scene id `0x55`.

The source-oriented OOT3D local entrance/exit table promotes native setup command `0x06` and `0x13` payload records with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_entrance_exit_source_table.py
```

Scene-entrance/exit-source-table outputs:

- `tools/oot3d/decomp_support/analysis/scene_entrance_exit_source_table.md`
- `tools/oot3d/decomp_support/analysis/scene_entrance_exit_source_table.json`
- `tools/oot3d/decomp_support/analysis/scene_entrance_source_table.csv`
- `tools/oot3d/decomp_support/analysis/scene_exit_source_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_entrance_exit_source_table.h`
- `tools/oot3d/decomp_support/src/code/z_scene_entrance_exit_source_table.c`

This table currently emits 644 local entrance rows from 175 entrance commands and 666 exit rows from 157 exit commands. Every row resolves to a generated setup-source row and command-source row, with zero setup or command count mismatches. Entrance rows bind their native room byte to `scene_room_source_table.json` where possible: 636 resolve to native room rows, 4 are preserved as none/negative-room cases, and 4 remain explicit unresolved-room worklist rows. Exit rows preserve the signed `s16` value, raw `u16`, and category derived from OOT3D `code.bin` transition evidence: 639 direct exits resolve to decoded global entrance rows, 12 direct exits remain outside the inferred global table extent, 6 are preserved as negative signed direct values, and 9 high-remap rows are kept as code.bin-confirmed runtime-entrance-dependent delta cases. `spot04_info.zsi` contributes 48 local entrance rows and 48 exit rows, all bound to native scene id `0x55`.

The source-oriented OOT3D scalar setup-setting table promotes single-record native setup commands with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_setting_source_table.py
```

Scene-setting-source-table outputs:

- `tools/oot3d/decomp_support/analysis/scene_setting_source_table.md`
- `tools/oot3d/decomp_support/analysis/scene_setting_source_table.json`
- `tools/oot3d/decomp_support/analysis/scene_setting_source_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_setting_source_table.h`
- `tools/oot3d/decomp_support/src/code/z_scene_setting_source_table.c`

This table currently emits 782 scalar setup-setting rows: 145 special-file records, 175 skybox-setting records, 175 sound-setting records, 112 cutscene references, and 175 misc-setting records. Every row resolves to a generated setup-source row and command-source row, with zero setup, command, or kind count mismatches. The row fields mirror the native typed structs already emitted in `scene.h`: `Oot3dSpecialFiles`, `Oot3dSkyboxSettings`, `Oot3dSoundSettings`, `Oot3dCutsceneReference`, and `Oot3dMiscSettings`. All rows remain marked `semantic_pending` because their handlers are `code_bin_handler_confirmed` but still carry naming/open-question notes in `scene_command_source_table.json`; the table therefore exposes values and handler evidence without pretending final subsystem names are complete. `spot04_info.zsi` contributes 13 special-file, 13 skybox, 13 sound, 13 misc, and 10 cutscene-reference rows, all bound to native scene id `0x55`.

The source-oriented OOT3D cutscene table promotes native command `0x17` references and conservative timeline subdecodes with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_cutscene_source_table.py
```

Scene-cutscene-source-table outputs:

- `tools/oot3d/decomp_support/analysis/scene_cutscene_source_table.md`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_source_table.json`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_source_table.csv`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_header_candidate_table.csv`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_command_table.csv`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_camera_point_table.csv`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_entry_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_cutscene_source_table.h`
- `tools/oot3d/decomp_support/src/code/z_scene_cutscene_source_table.c`

This table currently emits all 112 native cutscene references from command `0x17`, matching both the command-source table and scalar setting table, with zero scene/setup/command/setting binding gaps. OOT3D `code.bin` handler `0x0023449C` relocates the scene-relative pointer, helper `0x0037573C` stores it at `play+0x229C` and clears `play+0x22AC`, and helper `0x00357EA0` returns `play+0x229C`. The conservative strict-N64-compatible subdecode resolves 9 cutscene payloads into 36 timeline commands, 253 camera points, and 9 timeline entries; the remaining 101 payloads with plausible header candidates and 2 fully undecoded payloads remain explicit OOT3D-native worklist rows rather than inferred N64 replacements. `spot04_info.zsi` contributes 10 cutscene references, of which setup 7 and setup 8 currently expose strict camera/timeline rows, keeping Kokiri cutscene investigation tied to native OOT3D data.

The runtime context for those native cutscene references is generated with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_cutscene_runtime_context.py
```

Scene-cutscene-runtime-context outputs:

- `tools/oot3d/decomp_support/analysis/scene_cutscene_runtime_context.md`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_runtime_context.json`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_runtime_context_functions.csv`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_runtime_context_fields.csv`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_runtime_context_workorders.csv`

This report links command `0x17` to the OOT3D runtime path: handler `0x0023449C` relocates the scene-relative payload pointer, helper `0x0037573C` writes `play+0x229C` and clears `play+0x22AC`, frame loop `0x00321F50` advances the cutscene frame and calls `Cutscene_ProcessCommands(play, csCtx, *(play+0x229C))`, and interpreter `0x002C5BA0` reads the OOT3D-native payload header. The proven header has a `0x10`-byte prefix (`+0x00` word, `+0x04/+0x06` halfwords, `+0x08` command count, `+0x0C` end frame) before command records start at `+0x10`. The report maps 10 runtime functions, separates same-offset non-PlayState false positives, records 127 interpreter command IDs, and promotes the next workorder as lifting `Cutscene_ProcessCommands` into an OOT3D-native payload decoder rather than continuing strict-N64 header probing.

The OOT3D-native cutscene payload decoder is generated with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_cutscene_native_decode_table.py
```

Scene-cutscene-native-decode-table outputs:

- `tools/oot3d/decomp_support/analysis/scene_cutscene_native_decode_table.md`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_native_decode_table.json`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_native_decode_table.csv`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_native_command_table.csv`

This table lowers command `0x17` payloads with the native OOT3D layout consumed by `Cutscene_ProcessCommands`: the asset command points at a 16-byte envelope, the effective header starts at envelope `+0x10`, header word `+0x00` is `0x51444220` (`QDB `), `+0x08` is native command count, `+0x0C` is end frame, and command records start at header `+0x10`. The decoder currently resolves 110 of 112 cutscene references into 1154 native command rows across six structural categories (`counted_12word_entries`, `counted_3word_entries`, `packed_3word_pairs`, `camera_list`, `fixed16`, and `blob_u32_size`). The two undecoded rows are 16-byte `_dd` stubs without the `QDB ` envelope. `spot04_info.zsi` is fully covered: all 10 cutscene references decode natively into 86 command rows, while the previous strict-N64-compatible subdecode remains only subset evidence for setup 7 and setup 8.

The C-facing OOT3D-native cutscene source table is generated with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_cutscene_native_source_table.py
```

Scene-cutscene-native-source-table outputs:

- `tools/oot3d/decomp_support/analysis/scene_cutscene_native_source_table.md`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_native_source_table.json`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_native_source_table.csv`
- `tools/oot3d/decomp_support/analysis/scene_cutscene_native_command_source_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_cutscene_native_source_table.h`
- `tools/oot3d/decomp_support/src/code/z_scene_cutscene_native_source_table.c`

This table promotes the validated native decode into compact C structs without copying full cutscene payloads out of the original ZSI files. It exposes all 112 cutscene references and all 1154 native command rows through contiguous command slices, with decoded rows required to have `QDB ` magic and the observed envelope `+0x10` header delta. Command rows keep their native command id, interpreter-derived structural category, conservative semantic kind, sizes, offsets, and raw prefix for diagnostics. The generated validation currently reports zero bad-magic rows, zero non-`+0x10` decoded rows, and zero command-slice mismatches. Strict-N64-compatible rows remain marked only as subset evidence through `strictDecoded`; the promoted C table is sourced from OOT3D-native payloads and OOT3D `code.bin` interpreter behavior.

The source-oriented OOT3D path table promotes native command `0x0D` path records and their `Vec3s` point arrays with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_path_source_table.py
```

Scene-path-source-table outputs:

- `tools/oot3d/decomp_support/analysis/scene_path_source_table.md`
- `tools/oot3d/decomp_support/analysis/scene_path_source_table.json`
- `tools/oot3d/decomp_support/analysis/scene_path_source_table.csv`
- `tools/oot3d/decomp_support/analysis/scene_path_point_source_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_path_source_table.h`
- `tools/oot3d/decomp_support/src/code/z_scene_path_source_table.c`

This table currently emits 69 native path records from 42 path commands, plus 975 `Vec3s` path point rows. Every path row resolves to a generated setup-source row and command-source row, with zero setup, command, point-count, or raw-length mismatches. Each path row keeps a contiguous `pointRefStart`/`pointRefCount` slice into the point row array and preserves the raw 8-byte record. The promoted layout is code.bin-confirmed: handler `0x002985F0` relocates record `+4` pointers by scene base and stores the table at `play+0x5C20`, while `Path_GetByIndex` at `0x00348FF0` indexes 8-byte records. Byte `+0` is point count and word `+4` is the `Oot3dVec3s` point-list pointer; bytes `+1..+3` remain named conservatively as `unk01/unk02` until a consumer proves additional meaning. `spot04_info.zsi` contributes 9 path rows and 403 point rows, all bound to native scene id `0x55`.

The source-oriented OOT3D collision table promotes native command `0x03` collision-header references, decoded header summaries, water boxes, bg camera records, and polygon surface-type usage with:

```powershell
python .\tools\oot3d\decomp_support\scripts\build_scene_collision_source_table.py
```

Scene-collision-source-table outputs:

- `tools/oot3d/decomp_support/analysis/scene_collision_source_table.md`
- `tools/oot3d/decomp_support/analysis/scene_collision_source_table.json`
- `tools/oot3d/decomp_support/analysis/scene_collision_header_source_table.csv`
- `tools/oot3d/decomp_support/analysis/scene_collision_command_ref_table.csv`
- `tools/oot3d/decomp_support/analysis/scene_collision_water_source_table.csv`
- `tools/oot3d/decomp_support/analysis/scene_collision_bgcam_source_table.csv`
- `tools/oot3d/decomp_support/analysis/scene_collision_surface_type_source_table.csv`
- `tools/oot3d/decomp_support/analysis/scene_collision_polygon_type_usage_table.csv`
- `tools/oot3d/decomp_support/include/oot3d/scene_collision_source_table.h`
- `tools/oot3d/decomp_support/src/code/z_scene_collision_source_table.c`

This table currently emits 35 unique native collision header rows referenced by 175 setup command `0x03` rows. 172 command references resolve to a decoded header; the 3 unresolved references all belong to `yousei_izumi_yoko_info.zsi` cutscene setups and point to the same zero-filled native payload window, so they are kept as `native_collision_header_zero_stub_unpromoted` worklist rows instead of being guessed. The generated child tables expose 83 plausible effective water boxes, 187 effective bg camera rows, 1000 complete native surface-type rows with `data1/data2`, and 1000 polygon surface-type usage rows. All 35 promoted headers are `native_collision_header_code_bin_handler_confirmed_effective_tables_selected`: the generated offsets preserve raw/effective table selection for vertex, polygon, surface-type, bg camera, camera-position, and water-box payloads. `spot04_info.zsi` contributes one collision header bound to scene id `0x55`, referenced by all 13 Kokiri setup rows, with 2315 vertices, 3858 effective polygons, 47 surface types, 15 bg camera records, and 1 water box. The table is sourced from OOT3D ZSI plus OOT3D `code.bin` handler evidence; no N64 collision table is read.

The selected consumer bodies are exported with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\oot3d\decomp_support\scripts\ghidra-export-selected.ps1 -GhidraRoot 'E:\azahar pcvr\tools\pcvr-re-tools\ghidra' -JdkRoot 'E:\azahar pcvr\tools\pcvr-re-tools\jdk21' -ProjectDir 'E:\ppssppvr\oot3d_decomp\work\ghidra_project' -ProjectName 'oot3d_code' -ExportDir 'analysis\scene_command_index_consumers_ghidra_export' -EntriesFile 'analysis\scene_command_index_consumer_export_entries.txt' -IncludeCallers -IncludeCallees -KeepSelectedArtifacts
```

The JSON records native scene setup commands, room references, gameplay spawn `ActorEntry` records, entrance lists, transition actors, native `0x1C` light-setting records, collision/camera/water summaries, embedded room CMB summaries, and object-prefixed room actor lists. The generated C files in `src/assets/scenes/` serialize the same decoded native structures through `include/oot3d/scene.h`, making the OOT3D scene/room indices usable as decompilation support data. Actor and keep-object IDs are emitted as `ACTOR_*`/`OBJECT_*` enum symbols from `include/oot3d/actor_object_semantics.h` when the native value is known; unknown fallback IDs remain numeric so the export does not introduce unproven labels.

The current full source dump covers 114 scene source files, plus generated `scene_index_registry.h` and `scene_index_registry.c`, 610 bound room files, 175 scene setups, 598 spawn entries, 8092 room actor entries, 6573 room object-bank entries, and 1467 native PICA light-setting records with zero parse errors. The registry exposes `oot3d_scene_index_registry[]` as native OOT3D stem/path/source-basename mappings, which keeps `_dd` and non-`_dd` scene variants distinct. Room actor and object-bank arrays are named from the full room ZSI basename, not only the room index, so paired files such as `bdan_0_dd_info.zsi` and `bdan_0_info.zsi` produce distinct source symbols.

The C export keeps command records as raw native `{ word, argument }` pairs and also emits typed setup payloads for special files, standard actor lists, actor spawns, entrances, transition actors, path records plus same-ZSI `Vec3s` point arrays, light-setting records, exits, skybox settings, sound settings, cutscene references, misc settings, room references, and room actor lists. Path records are promoted to `Oot3dPathRecord`: `code.bin` handler `0x002985F0` stores the table at `play+0x5C20`, `Path_GetByIndex` at `0x00348FF0` indexes 8-byte records, and exported consumers read byte `+0` as point count and word `+4` as a relocated `Vec3s` point-list pointer. The path decoder now validates the observed `0x10`-byte prefixed file payload case and decodes all 69 native path records to `decoded_vec3s_points`; `rawPointsOffset` and `Oot3dPathPointsStatus` remain in the C struct so unresolved records can still be represented if a later asset set exposes them. Exit lists are emitted as `Oot3dExitEntry { value, rawU16, category }`, where `value` preserves the signed native `s16`, `rawU16` preserves the original bits, and `category` is derived from OOT3D `code.bin` consumer evidence: handler `0x002A9E2C` stores the table at `play+0x5C1C`, consumer `0x003365B0` reads `s16 exit = *(play->exitList + collisionResult*2 - 2)`, signed values `< 0x7FF9` are direct, `0x7FFF` is special, and `0x7FF9..0x7FFE` use high-remap delta table `0x0053A1E7` plus entrance table `0x0053C094`. `build_scene_exit_transition_tables.py` now emits those high-remap tables as source-like `scene_transition.h` and `z_scene_exit_transition_tables.c`: the delta table is complete for the closed high-remap range, while the entrance table is emitted only through the asset-proven native ZSI coverage window. The transition request helper report ties those branches to helper bodies at `0x003348E8` and `0x003716F0`, where `play+0x5C32` is the pending transition entrance/index, `play+0x5C2D` is the pending transition trigger/state byte, and `play+0x5C76` is the transition effect/type byte. N64/SoH source is used only for semantic labels and category names; exported data comes from OOT3D ZSI files and OOT3D `code.bin`.

Each exported scene command also carries `decompilation_evidence`. The `support_level` field now separates fully supported `code_bin_confirmed` commands, `code_bin_handler_confirmed` commands whose handler stores/relocations have been lowered from OOT3D `code.bin`, and `code_bin_handler_confirmed_semantic_pending` commands whose handler is known but final gameplay field naming remains pending. As of this export, no scene command remains semantic/consumer pending in the source index. `0x0F` light settings is fully `code_bin_confirmed`; `0x0D` path list and `0x13` exit list are `code_bin_handler_confirmed`, with path payloads and exit categories represented from OOT3D ZSI plus OOT3D `code.bin` evidence.
