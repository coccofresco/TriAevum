# OoT3D NRI PICA AOT renderer parity

## Objective

Execute native OoT3D PICA rendering through the NRI/Vulkan backend without
compiling known game shaders during play. PICA state remains the authority;
the offline path converts the same canonical state into SPIR-V ahead of time.
Renderer enhancements configured through F1 remain optional passes after the
native game image and are not encoded into the native shader corpus.

## Implemented pipeline

1. `PicaDrawState` is decoded into a versioned canonical program descriptor.
   Schema 3 covers the vertex program, TEV fragment state, fog, alpha test,
   texture sampling, native Shadow2D producer/consumer state and the render
   state that changes generated source.
2. The descriptor deterministically generates the effective GLSL consumed by
   NRI. Source identity uses two hashes plus byte size; the pack loader rejects
   collisions and descriptor-schema mismatches.
3. A semantic trace records descriptors rather than GPU handles or temporary
   runtime objects. `oot3d_native_pica_trace_inventory` reduces traces to a
   deterministic effective-shader inventory.
4. `oot3d_native_pica_aot_compiler` merges one or more inventories, validates
   every source identity, compiles each module to SPIR-V and writes a verified
   `.o3ps` pack plus a JSON manifest.
5. The NRI backend resolves SPIR-V from the pack before invoking shaderc.
   Development mode may compile a missing module dynamically. Strict mode
   rejects the first miss and is mandatory in the product golden gate.
6. Vulkan pipeline objects still use the persistent driver pipeline cache.
   The first run on a new driver may create pipelines; subsequent runs reuse
   the driver cache without changing shader identity or output.
7. Development runs may also capture a typed graphics-pipeline inventory.
   `oot3d_native_pica_pipeline_manifest` merges inventories offline, and the
   NRI/Vulkan backend can optionally prewarm the matching attachment, MSAA,
   Native Fidelity and typed instrumentation-feature profile from that
   manifest and the verified `.o3ps` pack. This changes creation timing only;
   it does not generate state or submit synthetic draws.

The checked-in baseline inventory is:

`tools/oot3d/native_pica_frontend/profiles/oot3d_pica_boot_title_kokiri_effective.json`

It currently contains 79 unique vertex and fragment modules observed across
the complete boot/open-title, the Kokiri cutscene and deterministic Kokiri
gameplay. The default pipeline inventory additionally covers the retained
Authentic, TAA, CACAO+TAA, SSSR+TAA, Toon, FSR, screenshot/readback and
directional-shadow renderer profiles, plus the native fragment-lighting
Lakeside Laboratory surface, for 194 current schema-3 modules.
Building `oot3d_native_game` generates
`oot3d_pica_default.o3ps`; the normal launcher discovers it next to the
executable and the product packager includes it at the package root.

## Correctness work included

- Display-transfer decoding, crop, scale, orientation and presentation now use
  one renderer-owned transfer contract shared by upload and copy paths.
- Texture state preserves native mip levels and LOD selection instead of
  treating every CTXB snapshot as level zero.
- TEV multiplication follows PICA fixed-point behavior rather than ordinary
  floating-point multiplication.
- Missing vertex outputs use the PICA default value of `1.0`; position output
  is sanitized at the native NDC boundary and primary color applies the
  observed absolute-value saturation rule.
- Portable visual savestates retain the CPU-side target, texture and transfer
  state needed to reconstruct Vulkan resources without serializing GPU
  handles.

The changes are general state interpretation. No shader, mesh, actor, room or
frame receives a content-specific correction.

## Qualification

Validated on 2026-08-24 with VSync and pacing disabled at 1280x720:

| Surface | Frames | Framebuffer checkpoints | AOT resolutions | Misses |
| --- | ---: | ---: | ---: | ---: |
| Boot and complete open-title | 901 | 10/10 exact | 96 | 0 |
| Kokiri cutscene | 481 | 9/9 exact | 75 | 0 |
| Kokiri gameplay with input | 61 | 3/3 exact | 75 | 0 |

The cutscene and gameplay PCM hashes also match their product golden. Across
the three runs, 1,952,118 whole-AOT entries completed with zero retained A32
fallback, memory fault, unsupported exit or block-limit exit. Dynamic shader
generation and AOT execution were separately captured and were bit-identical
at every one of the 22 framebuffer checkpoints.

The final gate output is stored outside Git at:

`I:\oot3dre_work\visual-parity\golden-aot-strict-20260824-06`

The no-op native-game build completes in about five seconds on the current
machine; regenerating the 194-module pack adds about three seconds only when
its inventory changes.

Schema 3 was requalified on 2026-08-26. A 30-frame Kokiri NRI/Vulkan run
resolved 66/66 modules from the adjacent default pack and produced a
byte-identical framebuffer to dynamic generation. The complete 901-frame
boot/title comparison is byte-identical at all 10 captured checkpoints. The
older product golden hashes after frame zero predate recent justified PICA
lighting corrections; they are not silently promoted by this tranche.
Reproducible inventories, manifests, runtime reports and framebuffer captures
are under `I:/oot3dre_work/visual-parity/pica-shadow2d-native-20260826/`.

Fragment-feature schema 2 and visual replay `PVR8` were qualified on
2026-08-26 without changing descriptor schema 3 or generated shader identity.
The injected native `hylia_labo_info_entry_0043` transition completes after
287 frames and observes 87 fragment-lighting draw submissions with two active
lights. Its dynamic and strict framebuffer captures are byte-identical; the
expanded default pack resolves 90/90 modules with zero misses. The complete
901-frame boot/title surface remains byte-identical to its prior dynamic run
at all 10 checkpoints and resolves 96/96 modules. Evidence is under
`I:/oot3dre_work/visual-parity/pica-fragment-lighting-scene-20260826/`.

The first typed pipeline-manifest gate was qualified on 2026-08-26. A
60-frame Authentic Kokiri capture produced and merged 39 canonical pipeline
entries. Optional prewarm created all 39 with zero skips; per-frame
diagnostics reported a 39-entry resident pipeline cache, and all 3,307 draws
completed. Lazy and prewarmed framebuffer captures are byte-identical at
SHA-256
`bae563168bce554f84401ba2012cabe4a3531de5f22efb6a1e4bcccd58434c53`.
Evidence is under
`I:/oot3dre_work/visual-parity/pica-pipeline-prewarm-20260826/`.

Manifest schema 2 was then qualified across Kokiri, complete boot/title and
seven renderer profiles. The deterministic merge contains 232 unique pipeline
descriptors from 66,918 observations: 87 canonical and 145 instrumented, all
with an NRI fragment module. Profile-aware prewarm selects 39 pipelines for
Authentic, 36 each for TAA, CACAO+TAA and SSSR+TAA, 54 for directional
shadows, and 109 logical Toon descriptors; Toon creates 91 Vulkan objects and
reuses 18 equivalent ones. Every paired 60-frame Kokiri lazy/prewarm capture
is byte-identical at the SHA-256 above and records zero skips. Evidence is
under `I:/oot3dre_work/visual-parity/pica-pipeline-corpus-20260826/schema2/`.

## Build and validation

```powershell
.\scripts\oot3d\Build-Oot3dLlvm.ps1 `
  -BuildDirectory I:\oot3dre_work\whole-aot-product-consumer `
  -Target oot3d_native_game -Parallel 4

.\scripts\oot3d\Test-Oot3dWholeAotGolden.ps1 `
  -ProductExecutable `
    I:\oot3dre_work\whole-aot-product-consumer\oot3d_native_game.exe `
  -Repetitions 1
```

The golden test automatically selects the adjacent default pack and enables
strict mode. For a deliberate dynamic-generation comparison, pass
`-DisableDefaultPicaAotShaderPack` to `Invoke-Oot3dNativeGame.ps1`.

## Extending coverage

When a new scene or setting causes a strict miss:

1. Re-run that complete deterministic surface with
   `-DisableDefaultPicaAotShaderPack` and
   `-PicaEffectiveShaderInventory <path>`.
2. Merge the resulting inventory with the checked-in baseline by passing both
   files as repeated `--inventory` arguments to
   `oot3d_native_pica_aot_compiler`, and publish the merged result through
   `--merged-inventory`.
3. Rebuild `oot3d_native_game` and run the complete surface in strict mode.
4. Compare framebuffer captures from dynamic and strict runs. They must be
   byte-identical before accepting the expanded corpus.
5. Promote changed golden hashes only when the underlying semantic correction
   is independently justified against native data or an instrumented Azahar
   framebuffer capture.

This is coverage expansion, not runtime specialization: inventory entries are
deduplicated solely by generated source identity and remain reusable in every
scene that emits the same native state.

## Remaining renderer work

The current runtime corpus does not prove PICA features that these surfaces do
not exercise. Native Shadow2D sampling, fragment-lighting shadow factors and
the Shadow2D R32UI producer pass are implemented from the retained PICA/Azahar
contracts and compile through production `shaderc`, but still require a game
capture that activates them. ShadowCube, gas mode, border/asymmetric wrap
modes and less common framebuffer formats likewise need targeted evidence
before they can be declared complete.

The offline pipeline manifest and optional startup prewarm are implemented.
Boot/title and the principal instrumented profiles are represented in the
qualified schema-2 corpus. Hylia remains excluded because the current injected
entrance reproduces an independent whole-AOT sentinel fault with and without
inventory collection. The merged union is intentionally not packaged as a
default: Toon and directional-shadow validation use 33 resident pipelines but
the union prewarms 109 and 54 logical descriptors respectively. Product
prewarm requires workload-scoped manifests selected by external package or
scenario metadata, never scene-specific renderer branches. Until those inputs
prove a net benefit, the persistent Vulkan driver cache and strict shader pack
remain the default. Prewarm must remain an optimization only: generated
SPIR-V, native state authority and framebuffer output may not change.
