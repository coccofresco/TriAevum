# OOT3D NRI Shader Mutation Audit

## Scope

This audit classifies every transformation formerly chained inside `PicaShaderVariantCache`. The canonical PICA frontend output is now owned by `PicaShaderPipelineCache`; optional mutations live in its separate instrumentation domain.

| Transformation | Changes native color/depth | Required data | Target contract |
| --- | --- | --- | --- |
| Toon material | Yes, color | PICA lighting and material point | `LightingContributor` |
| Temporal vertex | Position jitter plus auxiliary outputs | Current/previous uniforms | `AuxiliaryOutput` |
| Directional-shadow caster | Replaces clip position in a depth-only variant | Typed vertex hooks and native view-position output | Typed vertex variant |
| Directional-shadow receiver | Auxiliary normal output | Geometry, normal, light | `AuxiliaryOutput` then `LightingContributor` |
| CACAO ambient guide | Auxiliary ambient output | PICA fragment lighting | `AuxiliaryOutput` |
| Scene-domain guide | Auxiliary coverage metadata | Draw domain and alpha | `AuxiliaryOutput` |
| Reflection material | Auxiliary material output | TEV/specular semantics and texture profile | `AuxiliaryOutput` |
| Reactive mask | Auxiliary material alpha | Transparent temporal draw | `AuxiliaryOutput` |
| Rigid motion | Auxiliary motion output | Current/previous clip state | `AuxiliaryOutput` |
| Grass texture classification | No shader mutation | Canonical fragment source | `Observer` / `GeometryProvider` |
| NRI combined-sampler lowering | No semantic mutation | Canonical descriptors | Native backend lowering |

## Implemented Boundary

- Canonical source identity includes exact source content, not only shader keys or lengths.
- Canonical entries survive graphics-setting revisions.
- Instrumented entries use a distinct cache, feature mask and GPU shader-module map.
- Reactive output is requested only by temporal rendering, never by the extension-off path.
- `Authentic` activates strict Native Fidelity: temporal instrumentation is suppressed and any remaining extension request is rejected.
- Vulkan diagnostics report profile, canonical/instrumented draw counts and both cache sizes.
- The default AOT corpus contains both canonical and currently supported instrumented modules; their runtime cache domains remain separate.

## Canonical Frontend Output Contract

The production frontend publishes a versioned structural output contract with
native color at location 0 and native depth ownership. It emits no normal,
material, ambient or motion MRT. `PicaShaderPipelineCache` independently audits
each unique canonical fragment source once and rejects an auxiliary output, an
invalid color location, duplicate native color or disagreement with the direct
frontend contract before caching the shader.

The source audit is a boundary assertion, not production semantic discovery.
Fragment hooks, texture routing and TEV consumption come directly from the
frontend. A compatibility analysis remains available only for tests and old
callers and is counted separately in Vulkan diagnostics.

## Typed Fragment Hooks (2026-08-25)

- `PicaShaderHookLayout` exposes typed global, main, lighting, depth, native-color and epilogue insertion points plus resolved PICA semantics.
- `BuildPicaFragmentInstrumentationVariant` now resolves normal, ambient, scene-domain, reflection-material, reactive and rigid-motion outputs together and composes one fragment variant. The production cache no longer chains the six legacy fragment source mutators.
- Composition copies every canonical primary-color and depth statement unchanged. It can only add declared guide outputs and assignments at typed insertion points.
- A compatibility test proves exact source and variant-key equality with the previously supported transformation sequence; nested blocks and functions after `main` are also covered, and conflicting MRT locations are rejected without producing a shader.
- Native Fidelity validation over 60 frames recorded 9,870 canonical draws, zero instrumentation requests, attachment count/mask `1/0`, strict AOT `66/0` hits/misses and zero Vulkan/NRI errors. The instrumented profile recorded 5,286 instrumented draws, attachment count/mask `5/0x0B`, strict AOT `72/0` and zero rejects or validation errors.

## Typed Directional-Shadow Caster (2026-08-26)

- `BuildPicaDirectionalShadowCasterShader` consumes only the frontend-published
  `PicaVertexShaderHookLayout` and `ViewPositionOutput` semantic. Production code
  no longer searches for `void main`, braces or generated variable declarations.
- The canonical hook layout is interned with the scene shader. Missing, stale or
  conflicting metadata rejects the caster before shader compilation.
- Tests cover misleading `void main` text, nested blocks, a helper after main,
  successful production shader compilation, missing semantics and stale hook
  offsets. The focused shader and scene suites pass `36/36` and `15/15`.
- Dynamic and strict-AOT 60-presentation Kokiri captures are byte-identical at
  SHA-256 `a83b5d41bbdabb1cb19955ac03edc881a6342f9b57197d706015fb6c054e1008`;
  the strict pack resolves `69/0` hits/misses. Evidence is under
  `I:/oot3dre_work/visual-parity/typed-shadow-caster-20260826/`.

## Frontend Closure Evidence (2026-08-25)

- The frontend directly emits fragment hooks, native output ownership, exact
  texture-coordinate routing, primary/secondary TEV consumption and the typed
  temporal vertex program.
- Production temporal instrumentation refuses a missing or stale typed vertex
  program. The former GLSL analysis fallback remains only in the explicitly
  named compatibility API.
- Reflection and reactive instrumentation now declare and initialize the
  material guide themselves. The canonical frontend is no longer required to
  predeclare location 2, and reflection eligibility uses typed TEV consumption
  rather than searching generated source.
- Standalone toon, reflection, reactive and rigid/temporal source mutators have
  no production callers. They remain compatibility/test APIs; NRI combined
  sampler conversion remains a backend lowering rather than a semantic shader
  mutation.
- A 48-frame Kokiri `Authentic` run audited 19 canonical fragment programs,
  executed 101 canonical draws per active frame with attachment count/mask
  `1/0`, and recorded zero compatibility analyses, output-contract rejects,
  typed-contract rejects or Vulkan/NRI errors.
- The matching TAA run executed 101 instrumented draws per active frame with
  attachment count/mask `5/0x06`. Dynamic and strict-AOT framebuffer captures
  are SHA-256 identical; the strict run resolves `60/0` shader hits/misses.
- Full graphics-foundation coverage passes `228/228`; focused pipeline-cache
  coverage passes `26/26`. The default AOT corpus now contains 254 modules and
  reproduces both TAA and SSSR+TAA with `60/0` strict hits/misses.
- Reproducible logs, inventories, diagnostics and framebuffer captures are in
  `I:/oot3dre_work/visual-parity/canonical-output-contract-20260825/`.
