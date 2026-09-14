# OOT3D native shader surface and NRI migration

Date: 2026-09-14. Status: source/asset investigation and executable census,
not an implemented renderer replacement or a measured stutter elimination.

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

## References

- [zeldaret loader placeholders](https://github.com/zeldaret/oot3d/blob/a87ddae43252cb3add71bf1003e7391bbe006033/src/functions/functions_410000s.cpp#L616)
- [zeldaret renderer placeholders](https://github.com/zeldaret/oot3d/blob/a87ddae43252cb3add71bf1003e7391bbe006033/src/functions/functions_3E0000s.cpp#L458)
- [nihstro binary layout](https://github.com/neobrain/nihstro/blob/master/include/nihstro/shader_binary.h)
- [noclip independent CMB/LUT reader](https://github.com/magcius/noclip.website/blob/master/src/OcarinaOfTime3D/cmb.ts#L790)
- [Architecture](OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md)
