# CACAO and effect guide correction

Historical CACAO diagnosis. CACAO has since been replaced by
[multiscale GTAO](OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md#multiscale-gtao-and-reflection-performance).
The shared guide/coordinate and SSSR corrections described here remain
relevant; the CACAO provider, generator and CACAO-only tests are retired.
Current GPU regression: `tools/renderer/tev_program/gtao_gpu_tests.cpp`.
Current coordinate/graph checks:
`tests/pica_surface_coordinates_tests.cpp` and
`tests/ambient_occlusion_guide_tests.cpp` under the renderer runtime.

Date: 2026-09-06. Worktree: `I:/oot3dre_work/triaevum-release`.
Starting revisions: project `86fe95865`, renderer `eeb78ce3`.
Renderer correction commit: `e33d7b20` (`fix/cacao-effect-guide-contracts`).

## Scope and findings

The reported activation crash was **not reproduced**. Windows recorded a
`0xc0000409` fast-fail in the installed r1 candidate at 11:40:10, offset
`0x12df4bd`; its retained WER record has no dump or useful stack. This does not
prove a buffer overflow or that any one fix below caused that particular crash.

The investigation did establish these independent defects:

1. CACAO SPIR-V storage-image formats disagreed with the provider's image
   allocations. Examples: R32f versus R16_SFLOAT depth, Rgba32f versus
   RGBA8_SNORM normals, Rg32f versus RG8_UNORM AO. Vulkan reports undefined
   contents for these mismatches, not merely reduced precision.
2. Live attachment changes could destroy render targets during command
   recording. They now retire GPU work and dependent resources at StartFrame,
   before any PICA clears/draws, using the existing complete resize reset.
3. CACAO uses positive view depth; native camera normals use right-handed,
   negative camera Z. Medium consumed a guide through an identity transform,
   whereas Low reconstructed normals from depth.
4. Native depth/color/guide **texel storage is rotated**. The normal vectors
   themselves are in the unrotated camera basis. CACAO, Hi-Z rays, camera-only
   motion reprojection and reflection material resolve did not consistently
   make that distinction.
5. Vertex-lit PICA shader branches can explicitly zero the normal quaternion.
   Instrumentation treated zero as a valid +Z normal, making whole surfaces
   look camera-facing. Captured shaders contain the branch assigning
   `pica_output5.xyzw = uniforms.f[93].xxxx` while still exporting the view
   vector from the transformed position. The new auxiliary guide uses the
   derivatives of that position only when the quaternion is zero. Valid native
   quaternions and material/bump normals remain authoritative.
6. FidelityFX SSSR requires world-space normals, including its normal history.
   It previously received the camera-space MRT directly. Its GLSL callbacks
   also declared several 32-bit storage formats inconsistent with the SDK's
   compact allocations. Both boundaries have been corrected.

## Coordinate contract

`runtime/three_ds_recomp/include/fast/extension/pica_surface_coordinates.h` owns the
mapping, derived from the same display-transfer flags used by native scanout:

- Presentation UV `(x,y)` maps to stored UV `(1-y,x)`, with the transfer's
  optional second-coordinate flip.
- Stored UV maps back to canonical camera NDC before inverse projection.
- Shared normals stay in canonical camera space. Do not globally rotate or
  invert them: native PICA lighting and grass already use that basis.
- CACAO swaps the projection's diagonal FOV terms for storage axes, converts
  normal XY into those axes and camera Z into positive-depth Z. CPU packing
  matches the donor's column-major HLSL matrix and `mul(normal,matrix)`.
- Hi-Z reflection ray projection, motion reprojection and reflection resolve
  use the same explicit mapping. Exact per-draw motion already uses native
  storage coordinates and is not rotated a second time.
- SSSR gets storage-space projection/current/previous clip matrices, but an
  unrotated camera view matrix. `normal_space_pass.{h,cpp}` converts only its
  private normal input to world space, on GPU, with NRI ownership/barriers.
  This also makes SDK normal history stable under camera rotation.
- Depth-pyramid scalar linearization and isotropic outline normal differences
  require no vector rotation; their images remain co-located. Grass's guide
  already projects world normals onto camera side/up/negative-forward.
- Temporal AA/upscalers receive the corrected motion field. Their quality is
  not exhaustively revalidated by this patch.

No change to native lighting, gameplay, meshes, texture colors or scene IDs.
This validation covers the standard game projection, not future asymmetric VR
projections, every possible cutscene or every two-sided/bump-mapped material.

## Code ownership

All paths below are relative to `runtime/three_ds_recomp`:

- `tools/generate_cacao_spirv.py`: checked, build-local donor overlay; explicit
  storage formats; equivalent integer texture-load addresses for modern DXC;
  validates actual SPIR-V image formats before publishing the shader stamp.
- `cmake/dependencies/oot3d-cacao.cmake`: reuse modern NRI DXC; provider object
  depends on shader completion, so shader fixes cannot silently miss relinking.
- `cmake/dependencies/oot3d-sssr.cmake`: checked build-local GLSL format overlay
  matched to allocations in pinned `sdk/src/components/sssr/ffx_sssr.cpp`.
- `src/fast/extension/pica_shader_instrumentation.cpp`: missing-normal reconstruction
  at the existing auxiliary-output hook; canonical shader statements unchanged.
- `src/fast/extension/cacao_pass.cpp`, `fidelityfx_sssr_pass.cpp`: provider contracts.
- `src/fast/extension/normal_space_pass.cpp`: isolated NRI vector-space adapter,
  no image rotation, no scene-specific state and no native-output mutation.
- `src/fast/backends/gfx_vulkan{,_pica}.cpp`: early target retirement and wiring.
- `include/fast/extension/pica_guide_diagnostics.h`: framebuffer-only guide views.
  Their depth/normal reads are declared in `display_effect_plan.cpp`, including
  barriers; debug views are not allowed on the UI overlay domain.

## Reproducible evidence

Local evidence root: `I:/oot3dre_work/cacao-release-diagnostics`.
These contain user-ROM-derived images/shaders: **never package them**.

| Run | Result |
| --- | --- |
| `live-before-2`, `live-before-no-grass` | All six quality changes completed; original crash not reproduced; storage-format warnings present |
| `live-after` | Six changes, no validation errors or CACAO format warnings |
| `coordinates-live-hiz-taa-full` | Six changes with Hi-Z/TAA; 241 rendered frames, 60 AO passes, no validation errors or format warnings |
| `guide-normal-bound` | Actual MRT capture exposed the uniform +Z normal defect |
| `guide-agreement-geometric-capture` | Corrected normal/depth agreement: 1,403,933 diagnostic pixels, 1,381,558 (98.4%) above dot 0.95; 7,063 (0.50%) opposite-hemisphere pixels, principally visible silhouette/depth discontinuities |
| `sssr-world-normal-live` | New world-space adapter ran, but validation revealed pre-existing SDK GLSL format mismatches; not a passing graphics test |
| `sssr-formats-world-normal-live` | Six changes, 260 rendered frames, SSSR active without fallback, zero validation errors and no storage-format warnings |
| `medium-final-user-config` | Medium, user's copied grass/settings retained; 315 rendered frames, 277 AO/guide passes, native framebuffer captured, clean exit and validation |

Discarded diagnostic evidence: `guide-agreement` was captured before its debug
shader reads were declared, so its unused descriptor slots sampled color, not
the guides. `guide-agreement-geometric` exited before its requested capture
frame and produced no image. Neither establishes visual correctness. The runner
now requires an actual framebuffer file for a passing guide-view test.

The guide comparison is geometric, not a comparison of artistic shading.
Silhouettes cross unrelated surfaces in the depth finite difference; a nonzero
edge mismatch is not evidence for flipping those materials. It is not a claim
of game-wide 98.4% fidelity.

### Historical fast tests (retired CACAO tooling)

The following command and results describe the original correction, not a
current build step:

```powershell
python -m unittest test_generate_cacao_spirv -v
```

Four Python tests pass; generation verified the formats of all 66 CACAO SPIR-V
modules. Standalone C++ tests also pass:

- `tests/cacao_surface_coordinates_tests.cpp`: 128 transfer combinations,
  2,048 UV/SSSR clip round trips, six reconstructed depth/normal planes,
  positive/negative camera-Z handling.
- `tests/cacao_normal_guide_tests.cpp`: missing quaternion reconstruction,
  valid material normal preservation, disabled identity, declared diagnostic
  reads with/without TAA, UI exclusion. Link the existing renderer library and
  `src/fast/renderer/extension_resource_graph.cpp`; no game/AOT compilation.
- The corresponding broader GTest normal-input test was updated, but that
  GTest target is not built by this product configuration.

From the project root, the current GTAO bounded runner uses:

```powershell
python -m tools.triaevum_release.validate_ambient_occlusion_run --runtime <developer-runtime> --profile <disposable-profile> --config <copied-config> --output <new-directory> --quality 0 --live --live-start 40 --live-stride 20 --with-sssr --seconds 45
```

Replace `--with-sssr` with `--with-hiz-taa` for the other reflection path. Use
`--quality 1 --guide-view 3` without live switching for geometric comparison;
views 1/2 are linearized depth and normal RGB. The runner copies graphics
configuration, uses private empty save data, times out and reaps its own process.
The supplied profile must also reference disposable TopScreen and other mutable
settings paths; do not pass an installed user profile directly. Grass remains
enabled in this recipe. Validation is enabled:
**these runs are not performance benchmarks**.

## Build and deployment

Only the runtime and provider shaders were rebuilt; the precompiled title DLL
was reused unchanged. Build target: `triaevum_public_runtime`, existing build
`I:/oot3dre_work/triaevum-direct-module-build`, parallelism 3.

The installed r1 candidate has NOT been overwritten. Its hash-validated local
catalog/receipt must not be invalidated by replacing just the EXE. A local test
launcher `I:/oot3dre_work/cacao-release-diagnostics/Avvia-TriAevum-corretto.cmd`
runs the corrected developer runtime with the existing installed profile,
configuration and saves. It is not a redistributable release package.

## Sources

- [Pinned CACAO implementation](https://github.com/GPUOpen-Effects/FidelityFX-CACAO):
  allocation formats, positive-depth reconstruction and normal matrix use.
- [DXC SPIR-V image-format attributes](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst):
  explicit storage-image format annotations.
- [Vulkan image-view lifetime](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyImageView.html):
  referencing GPU work must complete before destruction.
- Pinned FidelityFX SDK local `gpu/sssr/ffx_sssr_common.h` and callbacks,
  `components/sssr/ffx_sssr.cpp`: projection Y inversion, world-normal contract,
  storage allocation formats. No donor files were changed by the new overlays.
