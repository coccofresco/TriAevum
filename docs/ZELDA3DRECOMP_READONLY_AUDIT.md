# Zelda3drecomp Read-Only Audit

## Current Alignment (2026-07-18)

The active immutable snapshot is
`tools/oot3d/decomp_support/evidence/zelda3drecomp/849140697187b895`.
It was copied read-only from `I:\Zelda3drecomp` revision
`2cec5ef08305fbe1e4fe00efaf3f2467ff5228b1`. Selection version 16 reads the
commit tree rather than the live checkout, so unrelated local modifications are
excluded. The 907 selected files and two derived catalogs match the canonical
`code.bin` SHA-256 below; generated and uncommitted checkout artifacts are excluded.
The semantic overlay contains 6,042 symbols, including 3,414 additions and 28
recorded conflicts.

The snapshot includes the complete A32 C++ backend and deterministic AOT generator,
plus all 62 committed C++/header files that define Zelda3drecomp's typed native UI
substitution boundary. Since snapshot `850483fb59713e87`, the executable A32 sources
and generator are byte-identical; only 30 inventory records gained refined UI names,
paths, and semantics. The in-tree A32 runtime therefore remains based on snapshot
`850483fb59713e87`, with its local extensions declared separately, while ABI,
semantic, and future UI consumers use this newer active snapshot. This avoids an
expensive no-op AOT regeneration without discarding the new native contracts.

## Scope

`I:\Zelda3drecomp` was initially inspected on `master` at `4d082fa` without
modifying the checkout. During the audit its owner committed the semantic stack
as `5aeeb990cb9de08a43979735a64453a0f2245ea4`. The checkout was re-audited at
`426ec7d94cc5ed18b3eb96d6854d1125712390c3`. The intervening committed work adds
complete Actor core, camera-mode, collision AT/AC and OC, one-point-cutscene,
EnHorse, EnGeldB, BossGoma, BossGanondrof, EnGo2, EnPeehat, and EnPoField workflows. Snapshot `399909ca448c11ea` imports the
focused evidence set needed by the active Actor core integration, while preserving
every source-file hash and dirty-worktree status in its manifest. Uncommitted
external tranche work is deliberately excluded. The checkout still contains a
small set of unrelated modified files. It is a structured `code.bin`
reconstruction and native/emulator toolchain, not yet a complete conventional
source decompilation. Its most useful
outputs for this project are evidence catalogs, typed Ghidra decompilations,
recovered native runtime contracts, and the A32 C++ backend. Its frontend, HUD,
and pause-menu implementation are not integration targets; both our OpenGL and
Vulkan routes retain N64 HUD/menu semantics.

The checkout is dirty. Treat every path below as read-only, record
the source file hash, and copy selected inputs into `I:\oot3dre` before running
any tool that may rewrite generated files.

## Binary And Provenance

- Target: European OOT3D `CTR-P-AQEP`, title ID `0004000000033600`.
- `code.bin`: 4,567,040 bytes, SHA-256
  `16A6B0AA4C4784680220A6F780F7F8A73CFB205557AA9F9F0E705179E0613220`.
- The external report states that this image is byte-identical to our canonical
  `E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin`.
- The external inventory imported all 3,099 symbols from our revision
  `920d8b979a40cab8556dbea15104a43bd93cf386`, so those entries are circular
  evidence and must not be counted as new findings.
- Comparing domain symbol catalogs by address against our 3,099-symbol map leaves
  1,062 distinct additional semantic addresses.
- Generated names and layouts carry confidence and source fields. Preserve them;
  do not flatten them directly into `manual_symbols.csv`.

## High-Value Evidence

### Actor And Skeleton Runtime

Priority: immediate. This is directly relevant to missing/wrong Kokiri actors,
Link equipment selection, closed eyes, animation speed, and transform origins.

- `analysis/codebin_actor_init_records.csv`: exact native profile records for
  432 actors, including callback addresses, object IDs, instance sizes, and owner
  types. `Player` is bounded at `0x2A4C`; `EnKo` at `0x0CA8`.
- `analysis/codebin_actor_core_*.csv`: complete high-confidence Actor lifecycle
  workflow. It recovers the exact `0x10` scene `Oot3dActorEntry`, the `0x20C`
  `Oot3dActorContext`, twelve category lists, spawn/init/update/delete ordering,
  init-chain decoding, movement/spatial helpers, attention ownership, and the
  associated native function addresses. This is the foundation for native actor
  population; profile-specific workflows are consumers of it.
- `analysis/codebin_actor_callback_signatures.csv`: typed lifecycle ABI for all
  1,654 callbacks.
- `analysis/codebin_actor_animation_table_*.csv`: 210 resident joint/morph table
  placements across 103 profiles, using the proven `0x34` transform stride.
- `analysis/codebin_actor_face_animation_*.csv`: 62 exact `0x1CC` three-channel
  face-animation sets across 61 profiles. This is the strongest lead for Link's
  eye/mouth state.
- `analysis/codebin_actor_material_binding_*.csv`: 464 exact model/material
  pointer slots across 257 profiles.
- `analysis/codebin_skelanime_draw_pipeline_*.csv` and
  `build/analysis/decomp_batches/skelanime_draw_pipeline_semantic_typed.md`:
  191 newly named draw-path entries and 222 recovered before/after-limb callback
  bodies. These callbacks drive limb visibility, attachments, equipment matrices,
  effect anchors, and per-limb state.
- `analysis/codebin_limb_callback_helper_*.csv` and
  `build/analysis/decomp_batches/limb_callback_helpers_semantic_typed.md` recover
  `Player_SetLimbMeshVisible` at `0x002B9BF8`. Its six gameplay callers write the
  native CMB resource-visibility bytes for hands, sheath/equipment and held-item
  geometry. Missing Link parts must therefore be diagnosed as visibility-state
  selection before treating them as an alpha/material defect.
- `analysis/codebin_animation_producers_signatures.csv` and the matching typed
  dossier recover `SkelAnime_GetFrameData`, CSAB sampling, bind-pose reset,
  actor-model pose construction/advance, and `PlayerCsab_SampleBone1Translation`.

### Native Curves And Material Animation

Priority: immediate after actor pose routing. This provides executable semantics,
not only file layouts.

- `analysis/codebin_native_curve_runtime_signatures.csv` and
  `build/analysis/decomp_batches/native_curve_runtime_typed.md` recover float
  linear, Hermite, step/hold, period wrapping, shortest-arc CSAB rotation, five
  MMAD channel decoders, camera curves, and CMB LUT sampling.
- The external asset-backed corpus reports 1,353 decoded curves and 3,913 keys.
- `analysis/codebin_cmb_model_instance_lifecycle_*.csv` recovers all five CMAB
  runtime entry classes and the exact `0x124` mutable per-material record.
- `analysis/codebin_actor_material_animation_*.csv` identifies exact embedded
  `0x98` CMAB states versus actor-held pointers; this distinction should replace
  assumptions in generic actor material playback.

### CMB Materials, Lighting, And Raster State

Priority: high. This can replace inferred lighting behavior with the native
source-to-PICA contract while retaining our OpenGL implementation.

- `analysis/codebin_cmb_material_lane_runtime.md`: exact `0x1CC` runtime material
  lane; exact/bounded CMB source records for colors, textures, transforms, six TEV
  stages, blend state, lights, and LUT inputs.
- `analysis/codebin_cmb_runtime_lighting_*.csv` and typed dossier: three native
  light packets, source and view-space directions, normal-matrix transformation,
  diffuse/ambient/specular payloads, runtime/fallback selection, and fog upload.
- `analysis/codebin_cmb_raster_material_*.csv`: texture format/type/filter/wrap
  conversion, alpha/depth/cull/blend state, six-stage TEV conversion, framebuffer
  access, texture matrices, and material uniforms.
- `analysis/codebin_cmb_tev_override_*.csv`: native replace/RGB/alpha/add/subtract/
  multiply operations for actor and room material overrides.
- `analysis/codebin_cmb_renderer_runtime.md`: native packet construction and patch
  flow. The shared `Oot3dCmbRenderState` is 99.47% typed, but the full renderer is
  only 21.05% typed; use the proven contracts, not guessed interior storage.

### Scene And Room Runtime

Priority: high for the playable Ship route.

- `analysis/codebin_scene_room_lifecycle_*.csv`: asynchronous room load, command
  install, transition actors, actor cleanup, and delayed native CMB teardown.
- `analysis/codebin_room_runtime_resources_*.csv`: six room CMAB categories selected
  by base name or `_d`, `_n`, `_c`, `_a`, `_t`, with exact `0x98` state arrays.
- `analysis/codebin_room_scene_callback_table.csv`: all 53 scene draw configurations
  and all 159 init/cleanup/prepare-draw callbacks.
- `analysis/codebin_room_scene_semantics.csv`: the 22 non-trivial callbacks and
  their effects. For Kokiri Forest, `RoomScene_KokiriForest_PrepareDraw` at
  `0x001EB3D8` computes a room/event-dependent TEV constant alpha for room 1.
  This is a concrete native behavior missing from a purely static scene loader.
- The lifecycle catalog identifies `PlayState+0x4C30` as the exact room context,
  plus current spawn, setup entrance list, room files, and transition actor state.

### Ghidra Automation

Priority: supporting infrastructure.

- `ghidra/scripts/ImportOot3DCode.java` and `ExportFunctionDossier.java` provide a
  reproducible import/export path.
- `ghidra/targets/` contains bounded semantic/family target sets, avoiding repeated
  whole-binary exports.
- `ghidra/overrides/oot3d_flow_overrides.csv` contains validated CFG repairs,
  notably the room runtime and scene callback indirect tail dispatches.
- `analysis/codebin_struct_fields.csv` contains 7,192 evidence-backed field rows;
  `analysis/codebin_function_signatures.csv` plus domain signature catalogs provide
  typed ABI imports.

## What Not To Import

- `oot3d_runtime/` is mainly the Azahar capsule, launch/config/storage layer. It
  does not implement the native asset renderer needed by Ship.
- Emulator frontend and PICA replay code are behavioral references, not runtime
  asset or state sources. Backend-independent contracts may be shared by our
  OpenGL and Vulkan implementations.
- HUD, file-select, pause, and single-screen patches conflict with the current
  decision to retain Ship/N64 HUD and menu logic.
- Tests primarily validate catalog consistency, coverage, and stable typed Ghidra
  output. Passing them does not by itself prove visual parity.
- AOT coverage does not prove ownership of indirect roots or host-service
  correctness. Those remain explicit runtime contracts even when the guest entry
  address is executable.

## Import Strategy

1. Verify the canonical `code.bin` hash before every import.
2. Build a namespaced evidence overlay keyed by address, preserving `confidence`,
   `source`, and notes. Never overwrite a conflicting maintained symbol silently.
3. Import in this order: native curves and animation producers; SkelAnime draw and
   limb callbacks; CMB material/lighting/raster contracts; room lifecycle and scene
   callbacks; remaining actor profiles.
4. Convert recovered behavior into generic OOT3D runtime APIs under
   `runtime/three_ds_recomp/src/oot3d` or source-backed tables under
   `tools/oot3d/decomp_support`. Do not add intro/Kokiri-specific renderer branches.
5. Validate each imported family against original assets and focused Azahar traces;
   emulator output remains validation evidence, never runtime data.
6. Copy only the selected CSVs/dossiers required by an active implementation unit,
   with a manifest recording the exact commit, file origin and SHA-256. Never use
   uncommitted semantic tranches as an operational baseline.

The current active snapshot is
`tools/oot3d/decomp_support/evidence/zelda3drecomp/849140697187b895/manifest.json`.
Earlier snapshots, including `399909ca448c11ea` and `df311a8041ebf855`, remain
immutable historical checkpoints and are not selected by operational inputs.

## Recommended First Use

Implement the native Actor core before importing another profile-specific state
machine. Generate a provenance-preserving runtime contract for `Oot3dActorEntry`,
`Oot3dActorContext`, its twelve category lists and lifecycle addresses; then load
the Kokiri setup actor list and instantiate `EnKo` through its existing native
profile/resource evidence. Unsupported callbacks must produce explicit native-gap
telemetry rather than falling back to Ship actors. This unlocks all subsequent
actor workflows and directly addresses the absent/N64 Kokiri population.
