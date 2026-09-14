# OOT3D native shader surface and NRI migration

Date: 2026-09-14. Status: implemented opt-in parametric fragment path with
built-in SPIR-V and offline-translated native vertex family; not yet fully
precompiled vertex/extension/pass shaders or measured stutter elimination.

## Decision

Move native fragment configuration out of generated source and into GPU data.
Retain a small precompiled vertex family and a general PICA fragment evaluator.
Precreate the remaining graphics pipelines before interactive rendering.
Do not replace the current renderer wholesale, skip draws, or reduce native
lighting/TEV semantics to approximate PBR materials.

The original game already has a parameterized CMB vertex program. Its fragment
lighting and TEV are fixed-function PICA state, not thousands of original
fragment programs. Our specialization of that state introduces shader variants.
However, eliminating source compilation alone does not eliminate driver pipeline
creation, resource uploads, or GPU saturation. No current frame-time attribution
was measured in this investigation.

## Evidence identity

- ROMFS: `E:/ppssppvr/oot3d_decomp/work/extract/romfs`.
- `CmbVShader.shbin`: 3,144 bytes; SHA256
  `1125cfb9193e6c605ae4a3f8a80accdae948af2fb35bbaf432ef426e66de076a`.
- `E:/ppssppvr/oot3d_decomp/work/extract/exefs/code.bin`: SHA256
  `16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220`.
- `I:/oot3decomp`: commit `e0b6c5f7d19488cf8a9333288901d8bd22ce5a83`.
- `I:/Zelda3drecomp`: commit `9fd2f56b14c1e51273eb607862d4852282bab68d`.
- Public zeldaret: `a87ddae43252cb3add71bf1003e7391bbe006033`.

External repositories were read only. Addresses below refer to the local
EUR-rev0 evidence profile, not portable addresses for every supported ROM.
Untracked historical exports are supporting evidence, not guaranteed by their
repository commit. Maintained catalogs sometimes rename their functions.

## Measured asset surface

Read-only scan of 1,186 `.zar`, `.zsi`, and `.cmb` files:

| Quantity | Observed |
| --- | ---: |
| Embedded CMB v6 occurrences | 1,998 |
| Material records | 11,173 |
| Referenced TEV stage occurrences | 14,412 |
| Distinct RGB/alpha operation pairs | 22 |
| Vertex-lighting-only material records | 9,932 |
| Fragment-lighting-only material records | 205 |
| Neither lighting flag enabled | 1,036 |
| CMBs with one LUT curve | 111 |
| CMBs with no LUT curves | 1,887 |
| LUT keyframes | 874 |
| Malformed/rejected candidate tables | 0 |

These are occurrences, not deduplicated resources or runtime shader counts.
The 22 pairs do not account for source selectors, operands, scales, runtime
overrides, procedural textures or non-CMB draws. The scan does not decompress
arbitrary containers and is not proof of all possible gameplay GPU states.

Reproduce without a game run or writes to either evidence repository:

```powershell
python -B tools/oot3d/native_pica_frontend/inspect_cmb_shader_surface.py --romfs E:/ppssppvr/oot3d_decomp/work/extract/romfs --code E:/ppssppvr/oot3d_decomp/work/extract/exefs/code.bin
python -B -m unittest discover -s tools/oot3d/native_pica_frontend -p test_inspect_cmb_shader_surface.py
```

The tool emits metadata only. Six tests cover material/stage addressing,
truncation, negative indices and stage-count limits. The complete asset scan
also validates SHBIN tables and LUT keyframe extents.

## SHBIN and native loader

`CmbVShader.shbin` contains one DVLE vertex entry, 310 instruction words and
54 swizzle entries. Main begins at word 0 and its end marker is 14; this is
not the length of the called model/board subroutines. ROMFS also contains
`profile.shbin`: 664 bytes, 25 instruction words, two vertex entries. Therefore
one CMB program must not be assumed to cover every shader the game can submit.

`src/genuine/z_genuine_cohort_30.c` in `I:/oot3decomp` contains the startup call
using `rom:/CmbVShader.shbin` at native string address `0x00416BD8`.
The original code bytes confirm this string. Loader and uniform resolution:

| Entry | Verified operation |
| --- | --- |
| `0x0041724C` | Open/read SHBIN, allocate resource, binary-shader load, attach, then program setup via helper calls/vtable |
| `0x00420F7C` | Binary-shader helper called by loader with format `0x6000`; inspect before replacing upload semantics |
| `0x004174EC` | Resolve 180 named locations through `glGetUniformLocation` |

Maintained bodies: `I:/oot3decomp/src/runtime/owner_runtime/z_whole_residual_21_all_residual_21_21.c`,
entry comments `@ 0x0041724C` and `@ 0x004174EC`. The latter's literal points
to a name table at `0x0054C958`. The 180 engine locations are not 180 shader
programs. Native `gl*` names here denote the 3DS wrapper, not desktop GLSL.

The original SHBIN symbol table confirms these float-uniform indices
(SHBIN encoded indices subtract 0x10): projection c0..3, model-view c4..7,
diffuse c8, ambient c9, texture matrices c10..19, palette c20..49,
inverse-view c76..79, three light groups c80..88, texture-slot/shader-mode c89,
attribute scales c90..91, mapping-method/bone-count c92. Boolean uniforms
b1..b10 select texture, skinning, smooth skinning, ortho-stereo, color,
three UV attributes, vertex lighting and fragment lighting.

The palette symbol covers 30 vec4 rows, not 30 matrices. A renderer-side
20-matrix working palette does not prove 20 matrices fit this shader binding;
respect primitive remapping and each upload window.

### Gist review

[M-1-RLG's assembly](https://gist.github.com/M-1-RLG/c0a3ef277241781297ddeb4763dfbc88)
is a useful instruction-level reference, not a trusted commentary layer.
Its uniform names match the original symbol table. Two concrete comment errors:
`mov outQuat.w, const0.x` writes zero, not one; comparison of constants 3/4
against the bone count tests 3/4 <= count, not count <= 3/4. The quaternion
calculation uses the transformed normal at that point, so naming it world-space
unconditionally is misleading. No complete reassembly/byte equivalence was
performed; port instruction semantics and native output mappings, not comments.

## Public versus local decompilation

At the pinned [zeldaret commit](https://github.com/zeldaret/oot3d/tree/a87ddae43252cb3add71bf1003e7391bbe006033),
the requested renderer entries are largely assembly placeholders:

- `src/functions/functions_410000s.cpp:616`: `FUN_0041724c`; line 624: `FUN_004174ec`.
- `src/functions/functions_3E0000s.cpp:458`: `FUN_003f9b5c` and neighboring renderer entries.
- `src/functions/functions_310000s.cpp`: TEV/uniform helper address family.

Use zeldaret for entry identity and surrounding architecture, not as an already
implemented typed material renderer. The richer local semantic map is in
`I:/Zelda3drecomp/analysis/codebin_cmb_{renderer,material_lane,runtime_lighting,tev_override}_symbols.csv`
and `analysis/codebin_material_binding_struct_fields.csv`.

Read actual bodies in `I:/Zelda3drecomp/build/analysis/decomp_batches/`:
`cmb_material_lane_representative_typed.md`, `cmb_material_lane_family_typed.md`,
`cmb_raster_material_family_typed.md`, `cmb_runtime_lighting_family_typed.md`,
`cmb_geometry_family_typed.md`, and `cmb_tev_override_family_typed.md`.

Some older exports call `0x003F9B5C` BuildLightList: its stage loop is TEV,
not a list of scene lights. The maintained name is BuildMaterialCommandList.
The representative export corrects the manager's material-collection pointer;
the older family export can still show it inline. Do not copy stale types or
lost VFP arguments into the renderer. "100% bounded layout" is not executable
semantic equivalence.

## Consumer map

Names below are the maintained Zelda3drecomp aliases, keyed by entry address.
The newer oot3decomp often uses longer generated names; resolve through
`I:/oot3decomp/symbols/manual_symbols.csv` to its exact `source_file`.

| Entries | Consumer / renderer responsibility |
| --- | --- |
| `0031FF64` | CmbResource_BuildRuntime: joins CMB chunks; broad body still needs careful ABI interpretation |
| `004C34AC` | CmbMaterialCollection_Construct: MATS records, shared TEV table, per-material runtime lanes |
| `004C6364`, `004C6264` | CmbMaterialLightingBinding_Construct / PicaMaterialLightingState_Construct |
| `003F9B5C` | CmbRenderer_BuildMaterialCommandList: uniforms, stages, matrices, native material setup |
| `003146E4`, `0031466C`, `003146D0` | EmitTevStage, DisableTevStage, ConvertBufferUpdate |
| `003F9D9C`, `0031448C` | ApplyTevConstantColors / EmitTevConstantColor |
| `003FA198`, `003F9F68` | ApplyTextureState / ApplyMatrixState |
| `003FAC2C`, `003FAD68` | ConfigureMesh / SetRenderState: shader flags and raster/depth/alpha/blend |
| `003142DC`, `0031485C`, `003142F0` | Material shader/color/texture-config uniforms |
| `003130A4`, `00466EA0`, `00466F00` | PrepareLights / UploadLightColors / UploadLightDirection |
| `003FA34C`, `003FA5D0` | PackFallbackLights / PackRuntimeLights |
| `00308498` | PicaMaterialState_EmitLighting |
| `0040CDD8`, `0040D040`, `0040D15C`, `0040D1A8` | EncodeConfig / EncodeLutInputs / EncodeEnabledLights / EncodeLight |
| `004C382C`, `0040F74C`, `004003FC`, `00408EF8` | LUT construction, lookup, packed value/difference conversion and command upload |
| `0030F4D0`, `0045259C`, `00452934`, `00452B40` | SubmitDrawHandle, mesh packet creation, transform and UV updates |
| `003688A8`, `00358964`, `003589CC`, `00373BEC` | Instance/shared TEV overrides and material animation updates |

Do not replace gameplay animation consumers with a static CMB-only material.
Capture the resulting native state after animation, actor overrides and light
updates. CMB indexing can prepare immutable data, but is not the final draw state.

## MATS and LUTS contracts

CMB v6 has relative MATS/TEX/LUTS offsets at header +0x28/+0x2C/+0x34.
MATS count is +8, records begin +0x0C, stride 0x15C. Shared TEV records follow
the material array with stride 0x28. In cube.cmb, declared MATS size 0x18C
is four bytes shorter than the extent to TEX (0x190); do not use it to reject
the last complete TEV record. Native pointer arithmetic and the TEX boundary
resolve this, without an asset-specific exception.

| Material offset | Meaning |
| --- | --- |
| `00/01/02/03` | Fragment light / vertex light / fog / render layer |
| `10`, `58` | Three 0x18 texture mappers and three 0x18 transforms |
| `A0..B0`, `B4` | Emission, ambient, diffuse, two specular colors; six TEV constants |
| `CC` | 0x54 lighting source including six 0x08 LUT descriptors |
| `120`, `124` | Stage count and six signed stage indices; not texture indices |
| `130..15B` | Alpha/depth/blend/logic operations and blend constants |

A 0x28 TEV record contains RGB/alpha operations (+0/+2), scales (+4/+6),
buffer updates (+8/+A), RGB sources/operands (+C/+12), alpha sources/operands
(+18/+1E), and constant index (+24). Native enum values require conversion;
they are not the same encoding as PICA register bitfields.

The runtime lane is 0x1CC; lighting state 0x1B0 has eight 0x2C light slots
and seven LUT input records. A source LUT descriptor is absolute mode u8,
texture index s8, input enum u16 and scale f32. Keep source flags distinct
from enabled-light counts and runtime register semantics.

LUTS contains curves, not GLSL. Native `004C382C` reads count +8 and offsets
at +0x10, evaluates 257 samples, and builds 0x800-byte float value/difference
storage per curve. `004003FC` quantizes/encodes for upload; `00408EF8` emits
the selector and packed block. Its newer C body still contains VFP/argument
artifacts: confirm packing against original instructions before translating it.
Preserve signed indexing, absolute modes, quantization and interpolation.
Do not substitute noclip's display-oriented 512-wide 8-bit texture for native
PICA precision. Curves/tables belong in data resources, never shader source keys.

## Actual TriAevum changes required

1. `tools/oot3d/native_pica_frontend/oot3d_native_pica_shader_gen.cpp:1006`
   already keys vertex code/swizzles/interface, NOT changing uniform values.
   `BuildOot3dPicaVertexUniformState` already packs booleans and constants.
   Preserve this work; pretranslate identified program/interface families.
2. `oot3d_native_pica_fragment_shader_gen.cpp:463` hashes TEV operations,
   sources, modifiers, scales, fog/alpha configuration, lighting structure and
   some procedural LUT contents. `GenerateOot3dPicaFragmentShader` emits
   corresponding GLSL expressions. This is the principal architectural
   variant source identified here, not a measured percentage of frame time.
3. Introduce a shared `PicaFragmentProgramState` containing six stage records,
   texture modes, lighting controls, alpha/fog state and references to LUTs.
   Serialize explicitly for GPU layout; never memcpy guest structs into UBOs.
   Extend the existing fragment uniform builder instead of adding another owner.
4. Implement a precompiled fragment evaluator: fixed six-stage loop, dynamic
   sources/operands/ops/scales, native delayed buffer updates and clamping,
   primary/secondary lighting terms, LUTs, alpha test, fog, procedural/shadow
   texture semantics. Preserve gradient/LOD behavior across branches. Do not
   force early-fragment tests around discard or depth-changing operations.
5. Start with one general fragment module and the CMB vertex module, with
   separate precompiled modules where attachment types or genuinely different
   program interfaces require them. Additional profiles are bounded shader
   families, not material permutations or one specialization per light count.
   Measure mobile GPU cost before splitting lit/unlit variants. Fewer shaders
   can still mean more GPU work; no blanket performance claim is justified.
6. `runtime/three_ds_recomp/src/fast/backends/gfx_vulkan.cpp:3808`
   ResolveNativePicaShaderSpirv currently compiles on pack absence/miss unless
   strict mode is set. The new path must load module IDs without generating
   strings or invoking shaderc in draw submission. Keep the specialized path
   available for development comparison, not a silent shipping fallback.
7. `runtime/three_ds_recomp/src/fast/renderer3ds/nri_pica_pipeline_bridge.cpp:519`
   still calls CreateGraphicsPipeline. Precreate a normalized manifest of
   layout, topology, attachments, MSAA, raster, blend, depth/stencil and logic
   variants via this shared bridge. Use supported dynamic state only through
   the backend capability contract; shaders alone cannot replace these states.
8. Retain Forge's corpus and device cache. Use the corpus to seed raster
   combinations and regression draws, rather than as the sole material
   coverage mechanism. Compile/prewarm for the installed device; on driver or
   device change rebuild before gameplay, with progress. Unknown PSOs still
   invalidate a zero-stutter claim until covered by the bounded manifest.
9. Keep `pica_nri_shader_contract` and the compiled effect graph authoritative.
   Provide explicit normal/motion/material outputs for extensions; migrate
   required hooks without string-patching the new shader. World effects finish
   before UI, even when world and UI share a vertex program. Optional effects
   must have their own precompiled modules/prewarm and remain inert when off.

## Acceptance and remaining uncertainty

- Differential GPU tests against the existing canonical path and Azahar for
  the corpus plus mechanically synthesized legal TEV/light/LUT configurations.
  Preserve original ordering and vertex arithmetic, quaternion conventions,
  normal/light spaces, fog, discard, blend and depth behavior.
- Framebuffer captures, not desktop screenshots. Include title/logo, skinning,
  animated materials, water, particles, HUD, menus and transition states.
- Test cold game preparation separately from warm gameplay; report shaderc
  calls, source generations, PSO creation/wait time and actual guest/present
  frame times. Interpolated frames must not inflate native throughput.
- Shipping objective after preparation: zero native source generations,
  zero shader compiler calls, zero synchronous first-use PSO creation in tested
  gameplay. Never achieve that by skipping objects or suppressing diagnostics.
- Static CMB coverage is strong but does not prove dynamic/non-CMB coverage.
  Program identities, geometry output modes, procedural paths, driver pipeline
  behavior and extension variants remain to be validated across Windows,
  Linux/Deck-compatible drivers and Android. No performance gain measured yet.

The initial analysis added only the inventory tool/tests and this report.
The subsequent TEV implementation is tracked below; it is not yet selected by
the playable renderer.

## Implementation checkpoint: immutable TEV core

`runtime/three_ds_recomp/include/fast/renderer3ds/pica_tev_program.h` and
`src/fast/renderer3ds/pica_tev_program.cpp` introduce a 112-byte std140/std430
program decoded directly from the six native TEV register groups and buffer
update bits. The shared `Renderer3dsPicaCore` target owns this module. It has
no title addresses, scene exceptions, compiler dependency or scheduling policy.
Unsupported encodings reject the complete program rather than silently fixing
it. The immutable GLSL evaluator implements source/operand selection, RGB and
alpha operations, byte rounding, scales and delayed combiner buffer updates.
Textures, material constants and lighting results are inputs, not shader keys.

`tools/renderer/tev_program` is a standalone, small CMake test project requiring
Vulkan and shaderc from the developer SDK. Configure it in a separate build
directory, build Release, and run `ctest --test-dir <build> -C Release
--output-on-failure`. This does not build title logic/AOT. The test creates an
offscreen RGBA32F framebuffer, compiles one fragment module during test setup,
and renders 4,096 deterministic TEV configurations with one pipeline and draw.
Six configurations isolate prefixes of a chained quantization case. Readback
is compared against a scalar CPU implementation, and `pica_tev_gpu.ppm` is
written in the test build directory. Invalid register encodings are also tested.

Windows RTX 3060 result: 16,384 channels, zero mismatches, maximum error zero.
The first scalar reference used floating-point division by 255; making the
reciprocal multiplication explicit on both sides removed its rounding-boundary
disagreements. This is CPU/GPU agreement for the new evaluator, **not proof of
bit-identical original hardware arithmetic**, canonical-generator parity or
cross-vendor GPU validation. No measured gameplay performance gain yet.

Next integration boundary: compare against the existing specialized GPU path;
then introduce a versioned per-draw program binding without breaking the current
2,112-byte fragment uniform prefix or visual savestates. Preserve per-draw
extension metadata even when material shader identities collapse. Move lighting,
fog and procedural texture state to bounded modules/data, then precreate PSOs.
The current game still uses its existing shader generator/cache. No F1 setting,
save, external decomp or release payload was changed by this checkpoint.

## Canonical expression differential and repairs

Follow-up to `12b2c97`: the test now also executes 64 specialized six-stage
programs with 64 independently randomized color inputs each (4,096 cases).
`tools/renderer/tev_program/canonical_tev.h` assembles these test functions using
the **same expression emitters** as the game's fragment generator, extracted
into `oot3d_native_pica_tev_expressions.h`. It preserves inline modifiers,
primary rounding, operation order, scales and delayed buffer updates. Programs
share a test-only switch dispatch in one module. This is TEV expression parity,
not an execution of the complete game fragment shader: textures and lighting
are supplied colors, and fog, discard, attachments and composition are outside
this harness. Do not describe this as Azahar/hardware or in-game validation.

The comparison exposed two distinct problems:

- Inverted RGB/alpha operands were emitted without parentheses. Combining
  `1 - a` with multiplication/subtraction could change operator precedence.
  The canonical shared emitters now parenthesize inverted operands. With the
  production-style inline expressions, mismatching channels decreased from
  2,304 to 55 out of 16,384; the initial maximum error was 1.0.
- Remaining specialized/dynamic differences disappeared when quantization
  boundaries and stage results used GLSL `precise`. The canonical generator
  now uses precise byte-round intermediates and primary/stage results. Explicit
  PICA multiply-add still uses `fma`; this does not disable that operation.

Windows RTX 3060 final result: zero mismatches and zero maximum error for both
scalar test batches and the canonical-versus-parametric GPU batch, at the
unchanged 1e-5 rejection threshold. Full test about 12 seconds. An additional
OBJECT target compiles the actual fragment generator with these changes without
building/linking the game or AOT. Framebuffer readbacks are written separately
as `pica_tev_gpu.ppm` and `pica_tev_canonical_gpu.ppm` in the build directory.

The canonical shader state identity now includes compiler-semantics revision
`0x54455602`. Previously prepared shader/pipeline caches must not silently stand
in for the corrected source; reprepare the corpus for a future distribution.
Existing visual savestates carrying old shader source are not automatically
rewritten. No release/cache package was rebuilt in this step.

Remaining: integrate the parametric program binding, dynamic lighting/fog and
bounded sampler families, preserve extension hooks, regenerate preparation
artifacts, precreate PSOs, and validate full frames on the supported devices.
The playable executable was not rebuilt/launched here, and no reduction of
in-game stutter is claimed.

## Parametric draw consumer checkpoint

The real fragment generator accepts `Oot3dPicaTevMode::Parametric`; the Vulkan
draw planner selects it through `Oot3dPicaVulkanShaderSourceCache::TevMode`.
Default remains `Specialized`. This is an explicit developer integration mode,
not a new F1 setting or a claimed shipping migration. Both cache hits and misses
populate the per-draw TEV program from native registers. The mode participates
in state keys so switching it cannot return a shader from the other path.

`Oot3dPicaFragmentUniformState::TevProgram` travels through the existing NRI draw
bridge. The packed fragment UBO retains its entire 2,112-byte legacy prefix and
appends the 112-byte program (total 2,224). GLSL uses the same append-only layout.
Only referenced texture samples are populated; existing sampler/LOD, lighting,
alpha, depth, fog and native output code remains in the real generator. Existing
typed hook metadata is still computed per material. The resolved-primary TEV
entry consumes `rounded_primary_color` after authorized lighting hooks without
rounding again; using the raw primary would bypass directional-shadow hooks.

Visual replay writes PVRA/V10 and reads V1-V9 as before. V10 adds the native
program after the fragment uniform payload. The uniform codec is isolated in
`oot3d_native_pica_fragment_uniform_codec.h` and used by the actual serializer,
not duplicated in tests. Legacy states keep their embedded specialized sources;
their absent program defaults to zero and is ignored by those shaders. The full
V10 state format is not backwards-readable by older executables.

Verification in the standalone TEV build:

- Previous framebuffer differential tests still pass.
- Real generator: 16 different material programs give identical parametric
  source, different specialized/parametric keys, preserved semantic/texture
  hooks, and correct program uniforms. SPIR-V reflection checks offset 2,112.
- Another eight cases cover lit/unlit, ordinary/Shadow2D/projected/type-5
  samplers, fog and alpha test: 48 total SPIR-V compilations pass. These are
  compilation/interface tests, not complete-frame visual comparisons.
- Production uniform codec: exact V10 byte round-trip, V9-prefix compatibility,
  and rejection of all 2,212 truncated serialized payload lengths pass.
- Actual generator, planner, bridge and visual serializer translation units
  compile independently of the game/AOT. Full visual-savestate regression test
  was extended but not run: the standalone link requires the wider frontend.

Important remaining work: executable selection and real NRI-frame validation,
full savestate and submission tests, then separating module identity from
material/hook identity. For now the planner still uses conservative material
keys; equal GLSL does **not** yet imply one PSO. Forge preparation, dynamic
lighting/fog families and bounded PSO precreation remain pending. No game was
launched, no package prepared and no gameplay stutter improvement measured in
this checkpoint. External decompilation repositories were not changed.

## Real NRI execution: 2026-09-14

The launcher now accepts `--pica-parametric-tev` and applies the selection to
the live draw planner cache. The default remains specialized. The bootstrap
diagnostic representation also records the requested mode.

The actual Windows runtime was incrementally rebuilt from this worktree in
`J:/TriAevum-verify-20260910/runtime`, using its existing Clang 22.1.6/Ninja
configuration and precompiled title module. The inspected build comprised 37
actions, with **no gameplay AOT compilation**. The shader corpus preparation
step is separate from gameplay AOT. Both complete tests now ran successfully:
`oot3d_native_pica_visual_savestate_tests` and
`oot3d_native_pica_vulkan_bridge_tests`. This supersedes the earlier limitation
of only compiling their consumer translation units.

`tools/renderer/tev_program/run_native_comparison.py` reuses an existing private
invocation fixture, copies its configuration and savedata into separate run
directories, removes input/save commands, and executes both TEV modes with a
timeout. It preserves the fixture's explicit fixed timestep, frame cap and
capture sequence. `--from-start` removes the load-state argument. The effective
shader inventory must contain the parametric evaluator only in the parametric
run; mismatching capture sets or pixels fail the comparison. Captures come
from the runtime framebuffer, not the Windows desktop.

Validated with NRI/Vulkan on RTX 3060, native-fidelity configuration, no frame
interpolation, 1280x720, 200 presentation frames per run, captures at 120/150/180:

| Fixture | Draws per run | Effective modules, specialized -> parametric | SPIR-V compilations | PSOs created |
| --- | ---: | ---: | ---: | ---: |
| Hyrule Field savestate | 19,000 | 41 -> 15 | 44 -> 18 | 37 -> 37 |
| From boot, first 200 frames | 11,198 | 35 -> 17 | 36 -> 18 | 39 -> 39 |

All six capture pairs are pixel-identical (zero mean/max RGB difference) and
nonblank. Effective inventories confirm actual execution of the new evaluator.
The run/frame/draw counts agree between modes. Guest process fingerprints do
not agree, so this is not a claim of bit-identical whole-process state.

Each run used a fresh application renderer-cache directory and the existing
815-entry pack. Advanced pass shaders still compiled 21 times in each run.
Driver-level cache state was not reset. These synchronous-capture runs are
**not FPS benchmarks**: reduced module counts do not establish a frame-time
gain. PSO creation remained and took longer in the parametric runs; this must
be measured and addressed through preparation rather than hidden by shader
count statistics. Neither zero runtime compilation nor zero stutter is achieved.

Private evidence (not release inputs):

- `J:/TriAevum-diagnostics/tev-native-20260914/comparison.json`
- `J:/TriAevum-diagnostics/tev-native-boot-20260914/comparison.json`
- Corresponding mode subdirectories contain invocation, runtime report,
  effective shader inventory, stderr counters and framebuffer/temporal metadata.
- Tested executable SHA-256:
  `8a3be5a0746f4b07ad1eb0370922dfdc3c89643b686dc438797378f154900953`.

Reproduction uses Python with Pillow and a user-owned invocation fixture:

```powershell
python tools/renderer/tev_program/run_native_comparison.py --invocation <fixture.json> --output <new-directory>
python tools/renderer/tev_program/run_native_comparison.py --invocation <fixture.json> --output <another-new-directory> --from-start
```

Historical next-step proposal superseded below: expanding Forge/cache coverage
is not the objective of the cache-independent native path. Linux/Android parity
and full-intro coverage are still unverified for this path.

## Cache-independent lighting, fog and alpha: 2026-09-14

The mandate is to reconstruct the stable programs and native parameter
contracts from the recovered game behavior, not collect additional shader
variants or require a cache-preparation step in Forge. The legacy cache path
remains available for comparison; it is not the architecture of the replacement.

This implementation reuses the existing register decoder and its documented
native material/LUT evidence; it does not claim new decompilation of external
repositories. `fast/renderer3ds/pica_lighting_program.h` now owns a shared,
256-byte std140 lighting program and stable GLSL equations. Per-draw data carries:

- Native light count and permutation, directional/positional and two-sided flags.
- Diffuse/ambient/specular accumulation, highlight clamp and geometric factors.
- LUT input selectors, absolute/signed addressing and scales, environment
  support masks, D0/D1, reflectance RGB, spot/distance attenuation and Fresnel.
- Normal/tangent bump preparation and Z reconstruction, shadow factor inversion,
  per-light shadow participation and primary/secondary/alpha application.

Fog mode/flip and all eight alpha-test functions are also per-draw data, rather
than generated expressions. Existing fog lookup arithmetic and native test
ordering are preserved. The live frontend selects these implementations with
the existing developer `--pica-parametric-tev` switch (now a broader fragment
path). Default shipping behavior remains specialized during migration.

The fragment UBO preserves the old prefix and TEV offset 2,112. Lighting begins
at 2,224, fragment controls at 2,480; total size is 2,496 bytes. SPIR-V reflection
checks these offsets. PVRB/V11 appends the 272 new bytes to the replay format;
V1-V10 remain readable. V11 is not readable by older executables. Typed material
and texture hooks remain per draw, including the normal/tangent variables and
fog factor visible to authorized extensions. No pass ordering was changed.

Verification:

- 384 combinations of light count, permutation, environment, LUT selectors,
  fog and alpha test produce one identical source for the fixed sampler family.
- `pica_lighting_gpu_tests`: 64 register configurations with synthetic packed
  24x256 PICA LUT entries, evaluated on Vulkan by both the actual canonical
  generator and one parametric lighting body. Covers bump normal/tangent modes,
  shadow inversion, geometric factors, LUTs and Fresnel. 512 float components,
  zero failures at 1e-5 tolerance; measured maximum difference 5.96046e-8.
  This is an equation differential, not an emulator-oracle comparison.
- Existing 4,096-case TEV GPU differential remains passing. Uniform codec tests
  cover exact V11 roundtrip, V9/V10 prefixes and every truncated V11 payload.
- Full game bridge and visual-savestate tests pass. The current runtime was
  incrementally rebuilt; no gameplay AOT compilation was required.
- Paired NRI framebuffer checks use Hyrule Field and early boot. The initial
  six pairs were identical; all six final current-binary pairs are also identical
  and are stored separately below. Inventory
  inspection found **no fragment-lighting draws in these fixtures**: they test
  real TEV/fog/alpha integration and absence of regressions, not in-game LUT or
  bump coverage. Effective module inventories went 41 -> 9 and 35 -> 9, but this
  is neither an FPS measurement nor proof of stutter elimination.

Private verification paths (never package these):

- `J:/TriAevum-diagnostics/lighting-program-build`: standalone CMake/CTest.
- `J:/TriAevum-diagnostics/lighting-native-field-20260914/comparison.json`.
- `J:/TriAevum-diagnostics/lighting-native-boot-20260914/comparison.json`.
- Final current-binary reruns: `I:/TriAevum-diagnostics/lighting-final-field-20260914`
  and `I:/TriAevum-diagnostics/lighting-final-boot-20260914`. An initial final-run
  attempt exhausted J: before inventory output; its two incomplete temporary
  directories were removed. It is not counted as a successful comparison.

The standalone Windows build uses Clang 22.1.6, Ninja and Vulkan SDK 1.4.350.0.
On this machine, configure it with `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`
and `CMAKE_EXE_LINKER_FLAGS=/MANIFEST:NO` to avoid the absent Windows resource
compiler during console-test configuration. This does not change the game build.

### Remaining implementation, not cache work

| Surface | Actual remaining work |
| --- | --- |
| CMB/profile vertex programs | Implement and validate the recovered SHBIN behavior as the fixed native vertex family, including skinning, UV mapping, vertex lighting and billboard paths; the legacy vertex translator is still used. |
| Texture interface | Regular sampler enable/reference masks, UV routing and bump/shadow selection are data-driven. Procedural unit 3 now uses a stable program too. Preserve float/integer descriptor families. |
| Procedural textures | Stable register/LUT-driven program implemented and GPU-tested below. Validate actual procedural draws against emulator evidence and profile the packed uniform transport on target platforms. |
| Lighting resources | Current code retains LUT/no-LUT descriptor families. Bump/shadow sample selection is now data-driven. The no-LUT family's lookup functions are unreachable by its decoded flags, not a fallback for missing LUT data. |
| Native output | Audit remaining shadow-write, depth/output and sampler helper specialization and define the bounded legitimate pipeline families. |
| Delivery | Build the finite shader artifacts with the developer build, bind them directly in NRI, and initialize required pipelines without depending on collected or persistent shader caches. Runtime source generation is not yet removed. |
| Validation | Execute fragment-lit materials in real game scenes, broader intro/gameplay/effect cases, and Linux/Android. |

Do not mark the full cache-independent renderer complete based on this tranche.
The next implementation priority is the fixed native vertex/texture contract,
not repopulating a shader pack with the new runtime-generated variants.

## Native Texture Selection Implemented (2026-09-14)

`fast/renderer3ds/pica_texture_program.h` supplies immutable regular-texture
selection code. Native registers 0x80 and 0x83 occupy the two formerly reserved
fragment-control words; the UBO remains 2,496 bytes. The decoded TEV control
contains an effective texture-reference mask respecting operation arity and
stage-zero source resolution. Lighting flags carry bump/shadow texture units.
Texture enable, unit-2 UV routing, projected sampling and disabled sampling
therefore no longer generate material-specific source in the parametric path.

Float sampling and active Shadow2D integer sampling retain distinct descriptor
interfaces. A disabled Shadow2D slot uses the float family because the backend
binds its normalized-color fallback there; the disabled branch returns zero.
This is a resource-type contract, not an asset exception. Draw order, native
metadata and pass scheduling are unchanged. Old replay shaders ignore the newly
used reserved words; no replay format or UBO size change is required.

Verification on the final binary:

- 224 regular sampler configurations share one source, with native sampled-mask
  parity. All four standalone tests pass, including TEV/lighting GPU differentials
  and uniform codec compatibility. Runtime bridge and savestate executables pass.
- Six paired framebuffer captures (early boot and Hyrule Field) are pixel-identical
  to the specialized reference. Private evidence is under
  `C:/Users/xander/AppData/Local/Temp/TriAevum-sampler-final-field-20260914`
  and `TriAevum-sampler-final-boot-20260914`.
- Initial sampler-run inventories decreased from 41/35 to 3 entries per fixture:
  one vertex program, one canonical fragment program and its NRI adaptation.
  This is not an FPS measurement or evidence of full-game coverage. These
  fixtures still do not exercise fragment lighting; its GPU differential is a
  separate test, not an in-game validation claim.

At this sampler milestone, procedural source specialization was still present;
the following milestone removes it. The replacement remains opt-in. Runtime
vertex translation, fragment wrapper generation and pipeline creation have not
yet been eliminated. Do not replace those tasks with cache growth.

## Parametric Procedural Textures (2026-09-14)

`fast/renderer3ds/pica_proctex_program.h` now owns the shared procedural shader
and its typed data contract. The OOT3D frontend supplies native registers 0x80,
0xA8-0xAD and 896 packed LUT words (noise, color map, alpha map, color and signed
color differences). No title addresses, asset identifiers or scene exceptions
are embedded in the shared implementation.

Implemented: coordinate routing, five clamp modes, three shift modes, ten
coordinate combiners, signed noise, separate alpha, six filters, native bias
and mip selection. The terminal mip does not evaluate a nonexistent next level
when its interpolation weight is zero. Lookup tables are draw data, not GLSL
constants. The native packed representation uses 3,616 bytes including control,
rather than expanding all LUT entries into float vectors. The fragment UBO is
now 6,112 bytes; every earlier field offset is preserved. This increases per-draw
transport versus the preceding milestone and is not a performance improvement
claim; resource reuse/upload costs still need profiling.

UNORM decode is integer-pattern based and correctly rounded to float, avoiding
GPU reciprocal rounding differences from CPU-decoded canonical literals. An
initial GPU test exposed two 8-bit output mismatches at a rounding boundary;
the conversion fix eliminates them without relaxing the original 1e-5 float
tolerance. No material-specific adjustment was used.

Integration and verification:

- Parametric generation accepts offline register-only shader input: LUT values
  are required at draw time, not at shader-build time. The legacy specialized
  emitter retains its old requirement and remains the differential reference.
- 120 combinations of registers/LUT values, including enabled/disabled state,
  produce identical parametric source. SPIR-V reflection checks the new block
  at byte 2,496. Shader compiler revision is 0x54455605.
- `pica_proctex_gpu_tests` checks every 8-bit and 12-bit UNORM value exactly
  against CPU decode, plus the terminal mip. It compares 60 procedural
  configurations and 1,920 samples against the existing canonical generator:
  six filters, clamp/shift/combine modes, noise, alpha, zero/positive/negative/
  fractional biases and synthetic signed LUT differences. Maximum float error
  6.3777e-6, zero failures at 1e-5, zero clamped 8-bit differences.
- The procedural differential supplies explicit UV derivatives to compute
  shaders. It verifies equations, not fragment-quad derivative scheduling or
  hardware PICA equivalence. Small groups avoid compiling one giant diagnostic
  shader. The reusable two-buffer harness is `vulkan_compute_test.h`.
- PVRC/V12 appends the packed procedural block. V1-V11 stay readable; older
  executables cannot read V12. Codec tests cover exact nonzero LUT roundtrip,
  V9/V10/V11 prefixes and every truncated payload. Real visual replay and Vulkan
  bridge tests also pass. The full runtime builds without recompiling title AOT.

All five standalone test targets pass. Final real-game comparisons produced six
pixel-identical, nonuniform framebuffer pairs across Hyrule Field and early
boot. Private reports (never package them):

- `C:/Users/xander/AppData/Local/Temp/TriAevum-proctex-final-field-20260914/comparison.json`
- `C:/Users/xander/AppData/Local/Temp/TriAevum-proctex-final-boot-20260914/comparison.json`

Their specialized shader inventories contain zero procedural samplers: these
fixtures are regression checks, not a claim that active procedural materials
have been exercised in game. Neither full-game coverage, zero runtime
compilation nor Linux/Android parity is claimed. At this milestone, pipeline
lookup still used material keys; the following milestone removes that coupling.

Next: fixed recovered CMB/profile vertex programs, bounded native output and
pipeline families, then direct use of developer-built shader artifacts. The
parametric path must not depend on collected shader caches or Forge warm-up.

## Effective Program and Pipeline Identity (2026-09-14)

The live Vulkan/NRI owner now uses
`fast/renderer3ds/pica_pipeline_identity.h` instead of material vertex/fragment
state keys for pipeline lookup. The identity combines:

- Effective vertex, fragment and NRI-fragment source identities (primary hash,
  secondary hash, size), with the actual NRI descriptor contract.
- Original vertex binding/attribute layout, including collection lengths. Both
  the native Vulkan fallback and normalized NRI pipeline retain this ownership.
- The exact fixed-function state returned by `BuildPicaNriPipelineState`, also
  passed to creation: topology, culling/front face, sample count, alpha coverage,
  depth, both stencil faces, logic operation, active attachment formats and blend/
  write masks. Dynamic-rendering versus render-pass mode is explicit.

Material IDs, texture/LUT values, geometry contents, target addresses, viewport
and other dynamic data do not create pipeline variants. Original material and
command identities remain in the diagnostic inventory; no draw sorting,
composition-domain reassignment, resource lifetime or effect scheduling changed.
Auxiliary outputs affect the key through the same resolved attachment state as
creation, rather than a second interpretation of effect flags. New immutable
pipeline state must extend this serializer and its mutation tests together.

This is an in-memory registry of GPU objects, not a collected shader cache and
not a Forge warm-up requirement. It removes duplicate pipeline creation for
identical programs/state; it does not pre-create every possible pipeline.

Verification:

- `pica_pipeline_identity_tests`: 1,024 material/draw identities reuse one key;
  36 fixed GPU-state mutations remain distinct, as do changes to source identity,
  vertex layout, NRI contract and render-pass mode. Active auxiliary attachment
  formats/write masks are distinguished; inactive attachment slots are ignored.
- Six standalone suites pass. Current Windows NRI build completed incrementally
  without rebuilding title AOT.
- Six final framebuffer pairs (Hyrule Field and early boot) are pixel-identical
  between specialized/parametric modes and also to the captures made before this
  pipeline change. Optional-effect MRT behavior is covered structurally here,
  not by a new full graphical-effects gameplay campaign.
- With empty application device-pipeline caches, actual NRI creation counts are
  36 specialized / 25 parametric in Field, and 37 / 26 in early boot. Both modes
  use the NEW identity implementation; these numbers are not a before/after
  measurement of this commit. Each run is 200 presentation frames. Their final
  frames reuse 95 and 69 primary-draw pipelines respectively, with zero new
  primary-draw pipeline creations.

`native_pica_cpu` now reports `pipeline_lookup_hits`, `pipeline_creations`, and
`pipeline_entries` through the existing renderer diagnostic service. The first
two count primary-draw lookups; entries is the current registry size, including
other owners/preparation. The existing `nri_pipeline_compilation` totals provide
the actual NRI creation count. The comparison harness enables the existing
diagnostic environment only for its isolated child processes and writes
`renderer.json` alongside captures. No user configuration is changed.

Private final evidence, never package:

- `C:/Users/xander/AppData/Local/Temp/TriAevum-pipeline-final-field-20260914`
- `C:/Users/xander/AppData/Local/Temp/TriAevum-pipeline-final-boot-20260914`

These are structural and framebuffer tests, not FPS benchmarks. Remaining:
fixed recovered vertex programs, direct binding of developer-built shader
artifacts, avoiding repeated source/variant processing, and initialization of
bounded immutable pipeline families. The current bridge still creates both a
Vulkan fallback and an NRI-owned pipeline; removing that duplication requires
an explicit ownership change, not a fake Vulkan handle. Shader-program lookup
itself still uses legacy material aliases. Full cache-independent operation and
cross-platform/advanced-effect validation are not complete.

## Offline native vertex family

The opt-in `--pica-parametric-tev` path now selects developer-translated vertex
code for all three entries in `CmbVShader.shbin` and `profile.shbin`. It no longer
calls the PICA instruction decompiler for these vertex programs. The specialized
path still translates instructions as the comparison reference. Unknown programs
in the new path fail explicitly; they are not silently translated at runtime.

Ownership and reproducibility:

- Shared `fast/renderer3ds/pica_vertex_program.h` defines program identity and
  selection, including entrypoint, multiplication semantics, instruction and
  swizzle identities. Hashing is explicitly little-endian. It contains no title
  addresses, asset names, uniforms, gameplay or output mapping assumptions.
- Title-owned `oot3d_translated_vertex_programs.h` contains translated GLSL
  bodies, not raw SHBIN data. Its adjacent provenance JSON records original
  SHA256 identities, generated-source hash and the Citra/Azahar translator donor.
  This is explicitly translated title code, not a title-neutral renderer module.
  No release was prepared here; catalogue it and its corresponding source in
  the release boundary before promotion. Do not distribute the original inputs.
- `export_vertex_programs` in `tools/renderer/tev_program` is a developer tool,
  NOT a Forge/user installation operation. It validates SHBIN capacities,
  entrypoints and vertex stage; exports all entries; and compares translations
  with altered unused instruction/swizzle tails before allowing prefix matching.
  Trailing upload memory is not a new shader variant. Active instructions and
  swizzles must match; uniform values and native output mapping stay dynamic.
- `GenerateOot3dPicaVertexShader(..., requireTranslatedProgram=true)` retains the
  existing composition wrapper, native depth/viewport conventions, transform,
  skeleton and UV metadata, and previous-frame interpolation hooks. The planner
  selects this policy only for the opt-in path. Invalid upload extents are rejected
  before forming program spans. Draw order and effect scheduling are unchanged.

Developer regeneration (private input directory supplied by the developer):

```powershell
export_vertex_programs.exe tools/oot3d/native_pica_frontend/oot3d_translated_vertex_programs.h <romfs>/CmbVShader.shbin <romfs>/profile.shbin
oot3d_native_pica_frontend_tests.exe <romfs>/CmbVShader.shbin <romfs>/profile.shbin
```

The standalone CMake project now also requires the existing `spdlog` donor
package for the developer translator; Windows validation uses
`-DCMAKE_PREFIX_PATH=C:/vcpkg/installed/x64-windows-static`.

Verification on Windows NRI:

- Seven standalone suites pass, including a new vertex identity contract test
  with changed instructions/swizzles, entrypoint, arithmetic mode, truncated
  uploads, malformed identity lengths, empty bodies and little-endian encoding.
- Twenty-four real-SHBIN differential cases (three entries, eight output maps
  and uniform configurations) produce identical GLSL, state keys, previous-frame
  bodies, hook offsets and uniform payloads. Altered/truncated programs reject
  rather than falling back. The ordinary asset-free frontend suite also passes.
- Planner, visual savestate and Vulkan bridge suites pass. Runtime was built incrementally
  without recompiling title AOT. No external decompilation repository was modified.
- Three framebuffer pairs from early boot and three from Field are pixel-identical;
  final evidence is under the private Temp folders below. Each final parametric
  report records two vertex source misses, exercising the translated-only selection,
  followed by 11,196 (boot) and 18,998 (Field) vertex source reuses.
  These are native30/no-interpolation/effects-off fixtures, not an advanced-effect,
  Linux/Android or complete-game coverage claim.

Private evidence, never package:

- `C:/Users/xander/AppData/Local/Temp/TriAevum-fixed-vertex-final-boot-20260914`
- `C:/Users/xander/AppData/Local/Temp/TriAevum-fixed-vertex-final-field-20260914`

**Remaining distinction:** offline PICA-to-GLSL translation is now implemented;
GLSL-to-SPIR-V compilation, interface/extension source assembly and driver PSO
creation are NOT all offline yet. Existing SPIR-V/pass caches still report
compilations in cold application runs. Next work must bind developer-built
desktop shader artifacts directly and prepare bounded fixed-state pipeline
families, not collect more gameplay variants. Shader-module ownership still
uses material aliases and the fallback Vulkan/NRI creation duplication remains.
No FPS improvement or complete elimination of shader stuttering is claimed here.

## Built-in fragment SPIR-V family

The native parametric fragment evaluator is now compiled in the developer
workflow and embedded in the renderer. It is resolved BEFORE both the collected
`.o3ps` pack and the application SPIR-V cache. No Forge operation, ROM input,
gameplay recording, downloaded shader collection or driver-cache file is needed
to build or use this family.

The family has eight structural configurations, each in canonical combined-sampler
and NRI separate-sampler form (16 modules total):

- Normalized-color versus enabled integer Shadow2D texture 0.
- Lighting LUT descriptor absent versus present.
- Normal fragment output versus native shadow-buffer write pass.

TEV operations, fog, alpha test, lighting controls, procedural texture values and
material uniforms remain data, not variants. Both interfaces use the existing
native generators and descriptor adapter; pass scheduling, GPU state, resources,
native ordering, UI isolation and effect hooks are unchanged. The fixed programs
retain the existing canonical multiplication/rounding semantics.

Code ownership:

- `tools/renderer/tev_program/build_fragment_artifacts.cpp` builds all eight
  configurations from constructed valid PICA states, not captured states. It
  verifies 128 material-data variations do not change those sources. Compilation
  uses the existing Vulkan 1.1 / shaderc performance-optimization contract.
- `fast/renderer3ds/pica_native_fragment_binaries.h` is the generated SPIR-V
  artifact, with all corresponding maintained generator sources in this repo.
  It contains neither GPU-specific driver caches nor original-game shader binary
  payloads. Unlike the translated vertex artifact, these programs are compiled
  from the renderer's maintained PICA equations. Donor notices remain applicable.
- `fast/renderer3ds/pica_fragment_artifact.h` provides a small shared resolver.
  Both source hashes, source size and sampler-interface kind must match; no
  scene/material identity is consulted. An incompatible source is not replaced
  by an approximately equivalent shader.
- `GfxRenderingAPIVulkan::ResolveNativePicaShaderSpirv` selects built-ins first
  and counts their uses independently. Legacy/instrumented programs not in this
  family still use their existing resolver; this is deliberately not a claim
  that all runtime compilation has been removed.

Developer commands (never an end-user Forge step):

```powershell
build_fragment_artifacts.exe runtime/three_ds_recomp/include/fast/renderer3ds/pica_native_fragment_binaries.h
build_fragment_artifacts.exe --check
build_fragment_artifacts.exe --check-exact runtime/three_ds_recomp/include/fast/renderer3ds/pica_native_fragment_binaries.h
```

`--check` recompiles the family, checks all stored source/interface identities,
walks stored SPIR-V instruction extents and verifies fragment entrypoints. It
does not require a different platform's shaderc to produce byte-identical output.
`--check-exact` additionally verifies exact reproducibility with the same toolchain.
Both checks passed with Vulkan SDK 1.4.350.0 on Windows. Eight standalone suites
pass; the shared resolver tests also reject wrong interface/source, empty source,
truncated header and bad SPIR-V magic. These checks are not a full SPIR-V validator
or evidence that every structural configuration has been exercised on a GPU.

Real NRI validation, native30/no interpolation, optional effects off:

| Fixture, no collected pack | Built-in fragment resolutions | Pack entries | Other SPIR-V compilations | Auxiliary pass compilations |
| --- | ---: | ---: | ---: | ---: |
| Early boot, 200 frames | 44 | 0 | 3 | 21 |
| Hyrule Field, 200 frames | 48 | 0 | 5 | 21 |

Six paired framebuffer captures are pixel-identical between specialized and
parametric modes without the collected pack. Each run has a new application
cache directory; no claim is made that the OS/driver cache was cold. The fragment
counts are resolver requests, NOT distinct module or pipeline counts. Remaining
NRI pipeline creations are 26 (boot) and 25 (Field). A preliminary Field run with
the old pack retained also produced identical captures and 48 built-in resolutions.

`run_native_comparison.py --no-shader-pack` removes the CLI pack and strict flag,
clears pack/prewarm environment overrides in the child, and verifies both positive
built-in use and zero pack entries. It still compares deterministic framebuffers,
not FPS. Private evidence, never package:

- `C:/Users/xander/AppData/Local/Temp/TriAevum-fragment-binary-nopack-boot-20260914`
- `C:/Users/xander/AppData/Local/Temp/TriAevum-fragment-binary-nopack-field-20260914`

Remaining work is concrete: precompile the vertex interface/interpolation families,
the presentation/auxiliary passes and supported extension families; remove repeated
source processing/material-alias shader-module creation; and initialize bounded
pipeline states with correct NRI ownership. The current vertex translation is
offline, but vertex desktop compilation is not yet entirely offline. The 21 pass
compilations above must not be hidden behind the completed fragment work. Full
cache independence, advanced-effect coverage and Linux/Android GPU verification
are still pending. No new release or FPS/stutter-elimination claim is made.

## Native Vertex Binaries (2026-09-14)

The next implemented step removes desktop shader compilation for the three
recovered native vertex interfaces. The title adapter supplies six immutable
SPIR-V modules: canonical and typed temporal versions of CmbVShader entry 0 and
profile entries 0/13. Output maps come from the original SHBIN tables, not from
scene captures. Uniforms, transforms and material state remain draw-time data.

Ownership and reproduction:

- `tools/renderer/tev_program/build_vertex_artifacts.cpp` reads private SHBIN
  inputs, verifies translated GLSL against the existing translator, applies the
  existing typed temporal hooks and compiles at developer build time.
- `oot3d_native_vertex_binaries.h` and its adjacent provenance JSON explicitly
  identify translated title code. They are not a neutral shader cache.
- `PicaDrawView::VertexArtifacts` borrows a title-owned immutable family.
  `pica_vertex_artifact.h` is the shared source-identity lookup; the backend
  selects against the effective source after instrumentation, only on module
  creation. No per-draw source scanning or title-specific shared-core lookup.
- `build_vertex_artifacts --check <CmbVShader.shbin> <profile.shbin>` verifies
  all six source identities, stored instruction extents and vertex entrypoints.
  `OOT3D_SHADER_TEST_ROMFS` optionally registers it in the standalone CTest suite.
  Original files remain private. Regeneration is never a Forge task.

Final Windows/NRI validation with fresh application caches and no collected pack:

| 200-frame fixture | Vertex artifact requests | Fragment artifact requests | Other SPIR-V compilations | Auxiliary compilations |
| --- | ---: | ---: | ---: | ---: |
| Early boot | 22 | 44 | 2 | 21 |
| Hyrule Field | 24 | 48 | 4 | 21 |

Six deterministic framebuffer captures match specialized rendering pixel for
pixel and also match the pre-change fragment-only build captures. Evidence is
in private `C:/Users/xander/AppData/Local/Temp/TriAevum-vertex-six-{boot,field}-20260914`.
Pack entries are zero. Counts are resolution requests, not unique modules.
The direct six-module check passes, as do bridge, visual-savestate, planner and
frontend tests. The temporal variants are compiled and structurally checked;
these native30/effects-Off runs do not prove temporal GPU coverage.

This supersedes the preceding statement that the canonical vertex interfaces
still require runtime compilation. It does not close all shader stuttering:
auxiliary/presentation and extension variants, repeated material-alias module
creation, and first-use NRI pipeline creation remain separate work. Unmatched
effective wrappers still take the existing resolver, never a mismatched binary.
No FPS improvement, cross-platform GPU validation, complete interface coverage
or release readiness is inferred from these parity tests.

## Precompiled Renderer Passes (2026-09-14)

The maintained `BuildBuiltinPassShaders()` catalogue is now compiled offline
by `tools/renderer/tev_program/build_pass_artifacts.cpp`, not reconstructed from
captures or prepared in Forge. It contains the existing 22 Vulkan 1.2 pass
programs (including scanout, transfer, TAA, SMAA, grass and reflection helpers).
Two Vulkan 1.1 compatibility-scanout programs are also emitted, for 24 artifacts.
This keeps the old and NRI descriptor/target contracts explicit and separate.

`fast/renderer/pass_shader_artifact.h` matches exact source, stage, ordered
macro definitions and Vulkan target. `CachedPassShaderCompiler::Resolve`
selects a built-in before compiler identification or disk-cache lookup.
`FindBuiltinPassShaderSpirv` supplies the compatibility path without copying
the generated catalogue into backend translation units. Unknown variants retain
the existing resolver, not a visually similar replacement. No pass scheduling,
draw order, effect parameters, depth/blend state or UI composition changes.

Developer reproduction, no ROM or capture input:

```text
build_pass_artifacts runtime/three_ds_recomp/include/fast/renderer/builtin_pass_binaries.h
# Rebuild the tool after generation, then:
build_pass_artifacts --check
```

`--check` recompiles every source, checks stored SPIR-V extents and execution
models, and exercises the actual runtime resolver with no cache directory.
All 22 Vulkan 1.2 catalogue entries must resolve without compiler/cache requests.
Source, macro, stage and unsupported-target mutations are rejected. This is
not byte-for-byte cross-compiler equivalence or an exhaustive GPU-effect test.
The generated header contains corresponding source and retains donor references;
it is renderer-owned code, not game assets or a harvested shader cache.
SHA256: `675702ab67c8218a1d533023f87b104a1107b2e089ca0971fd55e7858b7a956b`.

Final Windows/NRI tests, 200-frame fixtures, native30, optional effects Off,
fresh application cache directory, no collected shader pack:

| Fixture | Pass compilations before/after | Other SPIR-V before/after | Built-in pass requests | Compatibility artifacts |
| --- | ---: | ---: | ---: | ---: |
| Early boot | 21 / 0 | 2 / 0 | 21 | 2 |
| Hyrule Field | 21 / 0 | 4 / 2 | 21 | 2 |

Six framebuffer pairs remain pixel-identical, including comparison with the
pre-pass-change build, not only between two modes of the modified executable.
Nine standalone suites pass. Private evidence (not distributed):
`C:/Users/xander/AppData/Local/Temp/TriAevum-pass24-{boot,field}-20260914`.
The comparison harness now fails if fixed passes touch the compiler/cache.

The two Field compilations are precisely `oot3d_17301768_1.vert/.frag`, through
the legacy `CreateAndLoadNewShader` combiner path, not the new PICA material
family. An inventory-enabled run logs actual compiler misses by stage/name;
normal gameplay does not gain per-draw diagnostic logging. Handle this legacy
feature interface generally, not by adding a scene-specific shader-ID exception.

Remaining: legacy combiner coverage, optional/instrumented PICA fragment variants,
material-alias module duplication, first-use NRI pipeline creation and broader
platform/effect validation. The boot fixture has zero measured GLSL/SPIR-V
compilations, but still creates 26 NRI pipelines; Field creates 25. This does not
establish universal stutter elimination or an FPS gain. No release is produced.

## Effective Program Ownership (2026-09-14)

`pica_shader_module_identity.h` now keys GPU program ownership by both complete
source identities, descriptor schema, NRI interface availability and typed
fragment output layout. Canonical and instrumented programs remain in separate
owning maps. Material/command aliases no longer create duplicate shader modules
and SPIR-V vectors. Draw and legacy manifest prewarm use the same key; prewarm
still checks its NRI source and output contracts. Insertion moves the program
instead of copying its SPIR-V vectors. Resource destruction order is unchanged.

The draw reuses identities already computed by the variant cache after typed
instrumentation, without adding a per-draw source hash. The shared key accepts
the output layout as a type parameter, with no title-specific addresses or
effect scheduling logic. Unit tests exercise 100 aliases sharing one owner,
different source hashes/sizes, descriptor schemas, NRI contracts, output layouts,
and missing source rejection.

Windows/NRI Field, native30/effects Off/no pack/fresh application cache:

- Program creations: 24 previously, now 1 canonical owner.
- Vertex artifact resolutions: 24 -> 1; fragment resolutions: 48 -> 2
  (canonical and separate-sampler representations).
- NRI pipelines: still 25; these include real fixed-function differences.
- Three framebuffer captures are pixel-identical between modes and against the
  previous `TriAevum-pass24-field-20260914` build. Nine standalone suites pass.
- The two legacy combiner compilations remain; this change does not replace
  that family or establish a measured FPS gain.

The harness now offers `--taa`, modifying only the isolated copied config
(Custom preset, AA=TAA), and requires instrumented-owner evidence. Two repeated
Field comparisons produced the same strict failure: frames 120/180 match;
frame 150 differs at one pixel, red only, maximum 8/255. This is NOT reported
as a passed TAA parity test, nor attributed to the ownership change without
a pre-change TAA baseline. Keep the strict test; do not add a tolerance or a
scene-specific correction to conceal it.

TAA exercised 1 canonical + 3 instrumented program owners and 4 vertex artifact
resolutions, with no vertex compilation. Six fragment compilations remain
(three instrumented programs, canonical and NRI forms), plus the legacy pair.
The first TAA run created 28 NRI pipelines, reporting 4.12 seconds total creation
time. This is evidence of remaining driver pipeline preparation work, not a
steady-state FPS benchmark or proof of cold driver caches.

Private diagnostics:
`C:/Users/xander/AppData/Local/Temp/TriAevum-module-owners-{field,taa,taa-repeat}-20260914`.

Next work must cover the legacy combiner as a semantic family, instrumented
fragment interfaces from the typed generators, and pipeline preparation/lifetime.
Do not confuse the smaller program-owner count with fewer required raster states
or declare the TAA discrepancy solved. Linux/Android GPU verification is pending.

## External Source Links

- [zeldaret loader placeholders](https://github.com/zeldaret/oot3d/blob/a87ddae43252cb3add71bf1003e7391bbe006033/src/functions/functions_410000s.cpp#L616)
- [zeldaret renderer placeholders](https://github.com/zeldaret/oot3d/blob/a87ddae43252cb3add71bf1003e7391bbe006033/src/functions/functions_3E0000s.cpp#L458)
- [nihstro binary layout](https://github.com/neobrain/nihstro/blob/master/include/nihstro/shader_binary.h)
- [noclip independent CMB/LUT reader](https://github.com/magcius/noclip.website/blob/master/src/OcarinaOfTime3D/cmb.ts#L790)
- [Architecture](OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md)
