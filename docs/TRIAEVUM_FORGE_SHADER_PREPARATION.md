# Forge Shader Preparation

Date: 2026-09-09. Starting runtime: `26e7247` on `port/linux-nri`.
Goal: prepare as much shader work as possible while Forge installs the user's
ROM, without game recompilation, an SDK, gameplay automation, or changes to
canonical PICA output. This is not a promise of complete game coverage.

## Decision

Use two independent cache layers, not Citra driver binaries:

1. **Portable seed:** native PICA state -> TriAevum shader frontend -> canonical
   Vulkan and NRI SPIR-V. Build once, or build with small bundled tools in Forge.
2. **Device pipelines:** native vertex/fragment pair + vertex layout + complete
   raster/attachment state + enabled extension profile -> actual NRI/Vulkan
   device pipeline. Prepare on the destination GPU, and persist its driver cache.

SPIR-V can be reused across compatible Windows/Linux/Android Vulkan targets.
The device pipeline cache is not universally portable: key it by Vulkan
vendor/device, pipeline-cache UUID, driver and renderer/compiler contracts.
A Windows NVIDIA pipeline cache must never be presented as an Android Adreno
cache. See the [Vulkan pipeline-cache guidance](https://docs.vulkan.org/guide/latest/pipeline_cache.html).

Keep extension variants separate. Citra cannot supply our grass, toon,
interpolation, guide-attachment or other instrumented pipelines. Generate those
through the existing typed hooks and effect graph, without enumerating every
possible graphics-setting combination.

## Actual Archive Findings

The supplied `LoZOoT3D_Citra_cache.7z` contains:

| Member | Bytes | Contents |
|---|---:|---|
| `0004000000033500.bin` | 1,318,900 | 426 register snapshots |
| `0004000000033600.bin` | 1,318,900 | Byte-identical duplicate |

Both files SHA-256:
`d2efd93b9fce0ca3287de3935939e8276da572fef8a4c61f05ecc11016cc93ca`.
They contain legacy transferable version 1: raw entry kind 0, stage 2, 768
little-endian PICA registers per entry. There are **no vertex program/swizzle
payloads and no observed vertex/fragment pipeline pairings**.

Important ambiguity: before Citra commit
`50f22d1f594df31f32761f6c4f64d575eb5a7030`, `ProgramType` was VS/GS/FS.
The later generator uses VS/FS/GS, while the outer cache header still says 1.
Stage 2 is therefore not self-describing. Our importer requires an explicit
`citra-legacy-v1` or `azahar-v1` dialect; it never guesses from a title filename.
References: Citra's `gl_shader_gen.h` at that commit's parent and
[Azahar's cache reader](https://github.com/azahar-emu/azahar/blob/master/src/video_core/renderer_opengl/gl_shader_disk_cache.cpp).

Verified result using the legacy dialect and current TriAevum generators:

- 426/426 fragment configurations imported, zero rejected configurations.
- 352 unique canonical fragment sources plus 352 NRI variants: **704 modules**.
- All 704 compile successfully with the existing Vulkan-1.1 shaderc tool.
- Pack size: **5,478,464 bytes**, descriptor schema 3.
- 40 exact stage/source identities overlap the existing 79-module
  `oot3d_pica_boot_title_kokiri_effective.json`: union 743, not 783.
  This measures corpus growth, not percentage of the game's rendering.
- Actual Forge preparation service, Linux host: **5.60 s cold, 0.010 s reuse**.
  These are shader-only times, not ROM extraction or hardware pipeline timings.

Lighting LUT *values* are fetched at draw time, not baked into these sources.
The explicit `OfflineSource` generation purpose therefore accepts absent
resident lighting LUTs, while `RuntimeDraw` retains its original rejection.
Tests prove byte-identical generated source with/without resident LUT payload.
Procedural texture LUTs are different: they are embedded into current generated
code. Register-only offline inputs referencing them are explicitly rejected;
no zero-filled tables or scene-specific substitutes are invented.

## Implemented Boundaries

- `tools/oot3d/native_pica_frontend/oot3d_native_pica_transferable_cache.{h,cpp}`:
  bounded, read-only dialect-aware parser. Unknown versions, kinds, stage IDs,
  register/code lengths and truncated records fail without modifying the source.
- `oot3d_native_pica_cache_inventory.cpp`: offline fragment ingestion, source
  deduplication, per-input rejection report and canonical/NRI emission using
  the same generators as live rendering. Vertex and geometry payloads are
  parsed but reported as not imported; they are not silently considered covered.
- Existing `oot3d_native_pica_aot_compiler`: inventory union and SPIR-V pack
  compilation. No new shader compiler or runtime language is introduced.
- `tools/triaevum_release/shader_preparation.py`: Forge-owned preparation,
  input/tool hash validation, content-addressed reuse, atomic pack/receipt
  publication, bounded subprocesses, and explicit lack of device prewarm.
- `precompiled_titles.install_precompiled_title`: executes the optional stage
  after ROM verification and before transactional title activation.
- `forge.publish_private_runtime`: adds `--pica-aot-shader-pack` with portable
  path routing. Installed-runtime validation verifies the same pack/hash;
  installation migration relocates it without modifying saves or settings.

The optional `shader_preparation` object belongs to a verified title-catalog
entry, not an F1 setting or arbitrary filename scan. Its format is
`triaevum_shader_preparation_v1`, with `descriptor_schema_version` and:

- `mode: portable_pack`: `pack` is an integrity-checked artifact record.
- `mode: citra_transferable`: explicit `dialect`, `caches`, `importer`, `compiler`,
  optional extra `inventories`, and `dependencies` for dynamically linked tools.
  Artifact records use the existing `{path, bytes, sha256}` catalog convention.
  The seed contract, recipe and artifact hashes identify the prepared output.

No catalog object means no preparation job and unchanged existing installation
behavior. Partial imports cannot activate a pack. Cache receipts explicitly say
`device_pipeline_prewarm: not_performed` and `game_coverage_proven: false`.

## Remaining Work, In Order

1. Adapt PR #11's session persistence/prewarm after the review fixes below;
   isolate storage and queue scheduling from the large Vulkan backend class.
   Preserve the recent Android swapchain/pipeline lifetime fixes.
2. Add a renderer-owned **prepare-only** entry point, callable by Forge, with
   progress/cancel, no guest boot, no frame drops, and bounded work batches.
   Use actual NRI pipelines and the same device/cache directory as the game.
   Do not merely create unused Vulkan shader modules or the fallback pipeline.
3. Merge the imported fragment seed with our captured vertex programs and
   actual pipeline recipes; reconstruct additional vertex programs from the
   user's ROM where possible. Do not create a Cartesian product of every VS,
   FS, render target and extension setting. This archive alone is insufficient.
4. Qualify a clean install and a second launch on Windows, Linux and Android:
   separate GLSL compile count, NRI pipeline creations, cache hits and first-use
   hitches. Compare framebuffer output with native-fidelity baseline. Include
   driver changes, invalid caches, MSAA/F2 changes and interrupted preparation.
5. Add the verified tools/seed contract to actual release catalogs and Forge
   bundles. The source feature exists; **no current public catalog or release
   is enabled by this change**. Do not claim full first-launch prewarm yet.

## Distribution

Technical portability does not change the release allowlist. The supplied
archive, generated inventory and `.o3ps` remain private test outputs. Current
policy excludes game-derived shader-cache files. Do not silently publish them
because they are small or hardware-independent. A universal distributed seed
requires an explicit distribution-policy decision; an alternative is a
ROM-derived reconstruction recipe run locally by bundled tools. Runtime
discovery remains a correctness-preserving fallback for uncovered shaders.

## Reproduce

Build the isolated CMake targets (no title recompilation):

```text
cmake --build BUILD --target oot3d_native_pica_transferable_cache_tests oot3d_native_pica_cache_inventory oot3d_native_pica_aot_compiler --parallel 2
oot3d_native_pica_transferable_cache_tests
oot3d_native_pica_cache_inventory --input PRIVATE_CACHE.bin --dialect citra-legacy-v1 --output PRIVATE_INVENTORY.json
oot3d_native_pica_aot_compiler --inventory PRIVATE_INVENTORY.json --pack PRIVATE_PACK.o3ps --manifest PRIVATE_MANIFEST.json
```

Parser tests include all 3,100 truncation offsets of a complete synthetic
record, explicit dialect mismatch, multiple records and invalid lengths.
The native program-descriptor tests pass, including shaderc checks and the
new source-only/native-draw equivalence and procedural-LUT rejection tests.
Windows: 32 focused Forge tests pass (one additional real-tools test is skipped
without its explicit private-input environment). Linux: all 7 shader
preparation tests pass, including real importer + compiler execution; the wider
Forge suite was not run successfully under system Python (missing existing
Capstone development dependency). No game/GPU visual qualification is claimed.

Private evidence is under `I:/oot3dre_work/citra-cache-review/`; Linux tools and
corpus artifacts are under `/home/xander/triaevum-android-build/`. The existing
Linux frontend library's shader generation/draw-state source hashes were
checked against this checkout before reuse; changed generator units were
compiled afresh. No whole-AOT/title or Android APK rebuild was performed.
