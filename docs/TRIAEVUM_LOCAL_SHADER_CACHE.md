# Per-user shader cache and startup prewarm

TriAevum generates host shaders from live PICA state, so the shader set cannot be
enumerated ahead of time. The runtime now removes *repeat* hitches the way
Citra/Azahar do (`src/video_core/renderer_vulkan/vk_shader_disk_cache.cpp`,
`vk_pipeline_cache.cpp`): persist what was discovered, and rebuild all of it
before the game runs. A brand-new install still compiles on first encounter;
closing that gap needs a first-launch compile of the public inventory, which is
not implemented.

| Azahar | TriAevum |
| --- | --- |
| transferable cache: shader configs + SPIR-V + pipeline configs, per title | `local_pica_shaders.o3ps` (SPIR-V, keyed by shader source identity) and `local_pica_pipelines.json` (complete pipeline state incl. shader identities) under the SDL pref path `oot3d_native_vulkan/shader_cache/` |
| driver pipeline cache per vendor/device | `pipeline_cache.bin`, validated against vendor/device/UUID (pre-existing) |
| `InitPLCache` rebuilds every pipeline behind a load callback | `PrewarmNativePicaPipelines` builds every entry matching the active renderer profile; the guest is held and an ImGui "Preparing shaders N / M" overlay is drawn until the batch drains |
| async `TryBuild(false)` skip-draw fallback | not implemented; a pipeline created on a draw is the hitch being removed, and skipped draws would break rendering parity |

## Runtime behaviour

- Every shader compiled at runtime (a miss in the shipped pack) and every pipeline
  created at runtime is recorded; both files are rewritten at renderer shutdown.
  Close the game normally to persist a session. A crash loses that session's new
  entries (not the existing cache); append-on-discovery is deferred until the
  I/O cost is measured off the render thread.
- On the next launch the cache is loaded, then `Fast3dGui` shows the overlay while
  `GfxRenderingAPIVulkan::StartFrame` creates the pipelines in budgeted slices
  (`OOT3D_PICA_PIPELINE_PREWARM_BUDGET_MS`, default 50). The native host loop
  (`oot3d_native_a32_window.cpp`) advances no guest frame while
  `NativePicaPipelinePrewarmProgress().Blocking` is set, including the frame in
  which a renderer-profile change enqueues a new batch.
- Both files carry the descriptor schema; a mismatch with the shipped pack
  discards the local cache. Stale entries are harmless: unknown shader
  identities simply never match.
- `OOT3D_PICA_LOCAL_SHADER_CACHE=0` disables the cache. `--pica-aot-shader-strict`
  disables it implicitly: strict validation must answer whether the selected pack
  covers the content, not whether this machine has seen it before.
- The shipped `.o3ps` pack and an optional `--pica-pipeline-manifest` +
  `--pica-pipeline-prewarm` pair use the same prewarm queue. The pack is
  game-derived and is forbidden from public release payloads
  (`tools/triaevum_release/release_policy.json`), which is why Forge does not pass
  one; the per-user cache is what every user gets today, for content already seen.

## Extending a shared corpus locally

`scripts/run-linux-title.sh` records inventories with `TRIAEVUM_SHADER_INVENTORY=1`
and `scripts/extend-pica-corpus.sh` folds them into a candidate pack and manifest
under `<runtime>/pica-corpus/`. Launch with `TRIAEVUM_PICA_CORPUS=1`
(+ `TRIAEVUM_PICA_STRICT=1` to validate, `TRIAEVUM_PIPELINE_PREWARM=1` to prewarm).

The manifest schema change is covered by
`tests/oot3d_pica_pipeline_manifest_tests.cpp`
(`OutlineOcclusionFlagIsCompatibleWithOlderManifests`). The `tests/` CMake tree
currently does not build with Linux Clang (it passes the MSVC-only `/WX-` flag to
any Clang, and `oot3d_effect_graph_tests` lacks a `Vulkan::Vulkan` dependency);
until that is fixed, build the target's source list directly against system gtest
and nlohmann/json:

```sh
cd runtime/three_ds_recomp && clang++ -std=c++20 -Iinclude \
  tests/oot3d_pica_pipeline_manifest_tests.cpp \
  src/fast/oot3d/pica_pipeline_manifest.cpp src/fast/oot3d/pica_aot_shader_pack.cpp \
  -lgtest -lgtest_main -pthread -o /tmp/manifest_tests && /tmp/manifest_tests
```

## Verification (2026-09-09, Intel Arc Meteor Lake, Mesa)

Fixed-step replays (`--frames 1500 --fixed-delta-seconds 0.016666`) from an empty
cache directory:

| Run | prewarm batches | runtime shader compiles | runtime pipeline creations |
| --- | ---: | ---: | ---: |
| cold | 0 | 18 misses (16 new modules) | 58 |
| warm 1-3 | 58 pipelines, 15-26 ms each, before the first guest frame | 0 | 0 |

Strict mode with the baseline pack and a populated local cache aborts on the first
uncovered shader; with the 322-module candidate corpus it passes 66/66.
The "Preparing shaders" overlay could not be captured: `--screenshot` reads the
game framebuffer, not the composited swapchain.
