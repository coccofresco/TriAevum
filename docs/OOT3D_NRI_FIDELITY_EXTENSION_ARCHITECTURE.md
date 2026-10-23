# OOT3D NRI Fidelity and Extension Architecture

## Goal

Build an NRI renderer that reproduces the native OOT3D/PICA output faithfully while exposing enough structured scene and pipeline data for future advanced rendering. Extensions must integrate at the correct rendering stage; they must not be limited to generic post-processing.

AO offers [multiscale GTAO and the original FidelityFX CACAO](#multiscale-gtao-and-reflection-performance),
plus [iMMERSE MXAO](#immerse-mxao) when built with its author's permission.
All providers share the scene graph, native guides and particle-coverage fix.
The saved provider is preserved; selecting one does not migrate the others away.

## Non-Negotiable Rules

1. The canonical PICA path is authoritative and has no dependency on extensions.
2. With extensions disabled, shader results, depth/stencil, blending, draw order, targets and display transfers remain native.
3. OOT3D assets and PICA state are the source of truth; scene-specific visual corrections are forbidden.
4. Extensions declare their inputs, outputs and scheduling. They may not mutate hidden backend state or patch shader source text.
5. World, atmosphere, transparency, display transfer and UI are explicit composition domains.
6. Scene access uses stable, versioned GPU references; per-frame geometry and texture copies are avoided.
7. Game formats and native GPU semantics enter through title adapters. Scheduling, resource lifetime management and NRI pass execution must not depend on OOT3D identities, addresses, dimensions or scene-specific rules.

## Architecture

```text
3DS PICA commands/registers
    -> Shared 3DS/PICA Renderer Layer
    -> Canonical PICA IR and draw state
    -> Native Shader Compiler
    -> Generic NRI Render/Extension Core

OOT3D assets and game semantics
    -> OOT3D Title Adapter
    -> shared PICA capabilities + title-owned semantic payloads

Canonical draw state -----------------> PicaSceneFrame
Runtime scene semantics --------------> NativeSceneView
Published scene capabilities ---------> Extension Render Graph
Native ordered surfaces --------------> Native Frame Composer
```

- **PICA Canonical Frontend** decodes native registers, TEV, lighting, LUTs, procedural textures, fog, Shadow2D and fixed-function state.
- **Native Shader Compiler** emits and caches only shaders required by canonical PICA semantics.
- **Cache-independent native path (migration)** moves recovered native material
  behavior into stable shader programs with per-draw register-derived data.
  Collected shader caches and Forge cache preparation are not the solution or
  a runtime requirement of this path. Keep the legacy specialized path as a
  comparison oracle until the replacement is verified. Track remaining vertex,
  sampler, procedural-texture and pipeline work explicitly; a parametric TEV
  alone is not completion. See `TRIAEVUM_OOT3D_UBERSHADER_ANALYSIS.md`.
- **NRI Native Render Core** performs the faithful world render and remains usable without any extension system.
- **PicaSceneFrame** is a passive, low-level, read-only view of resolved draws and GPU resources. It is never the rendering source of truth.
- **NativeSceneView** joins stable draw IDs with camera, lights, transforms, current/previous skeleton state, geometry, materials, textures and object identity when those semantics are available.
- **Extension Render Graph** owns declared dependencies, resource lifetimes, barriers and pass ordering.
- **Native Frame Composer** preserves the original ordering of world, atmosphere, transparent content, display transfer and UI.

Composition sequences (schema 3) declare `Presentation` or `NativeMemory`.
The latter completes guest-visible raster/copy dependencies with canonical
shaders, without temporal jitter, extension passes or publication into the
extension scene view. It does not mutate the user's visual settings. Its target
namespace and execution deduplication are separate from interpolated visual
samples, which retain the complete ordered frame and its configured effects.
Guest PPF completion must not wait for a host presentation or consume those
retained plans. Completed prefixes survive save/restore without repeating guest
writes; visual copies are recaptured for each host sample. Frontend UI composition
belongs after canonical completion, not in guest-visible memory.

### Cross-Title 3DS Reuse Boundary

The intended reuse boundary is specifically **other Nintendo 3DS games**. It is
not a claim that unrelated console backends share PICA contracts. The renderer
is split into three layers so another 3DS title can retain its own assets and
game semantics while reusing the same decoded PICA model, shader compiler,
composition vocabulary and NRI providers.

```text
Per-game 3DS title package
    assets, scene tables, game semantics and title-specific command ownership
        -> title adapter
            |
Shared 3DS/PICA renderer layer
    PICA registers/commands, canonical draw/material/raster state,
    shader compilation, scene capabilities and 3DS composition semantics
            |
Generic renderer-extension core
    schedule/resource/publication contracts, lifetimes, bindings and NRI passes
```

- A title adapter owns asset decoding, scene/actor identity, game-derived draw
  classification, title command ownership and the conversion of those facts
  into shared 3DS/PICA capabilities. OOT3D assumptions stop at this boundary.
- The shared 3DS/PICA layer owns register and command semantics common to the
  platform, canonical PICA state, shader lowering, 3DS surface/composition
  contracts and typed capability identities. It cannot inspect OOT3D scenes,
  actors, asset names or hardcoded physical addresses.
- The generic renderer core owns stage contracts, pass identities, scheduling
  authorization, resource lifetimes, barriers, publication mechanics and NRI
  execution. It has no 3DS, PICA or game vocabulary.
- Effect providers consume only an authorized generic boundary plus declared,
  versioned resources. A provider cannot reconstruct scheduling from current
  Vulkan state or final-frame dimensions.
- Adding another 3DS game requires its title adapter and any genuinely
  title-specific payload schemas. It must not require a fork of the generic NRI
  backend, reusable providers or shared PICA compiler.
- Gameplay, audio and UI policy remain outside this renderer boundary. They may
  have their own portable host contracts, but renderer reuse does not make one
  title's behavior authoritative for another.

## Extension Contracts

An extension may use only explicit contracts:

- **Observer**: read scene and draw data without changing rendering.
- **Auxiliary output**: produce normals, IDs, motion or material data while preserving native color and depth.
- **Geometry provider**: add geometry to a declared world domain with native depth, fog and ordering rules.
- **Lighting contributor**: consume scene lights and material data at a defined PICA lighting hook.
- **World pass replacement**: replace a declared world domain while preserving native composition and UI.
- **Composer pass**: operate at an explicit boundary such as atmosphere, transparency or pre-UI.

Supported scheduling points are `SceneResolved`, `BeforeDepth`, `AfterDepth`, `BeforeOpaque`, `NativeLighting`, `AfterOpaque`, `BeforeTransparent`, `AfterTransparent`, `Atmosphere`, `BeforeUi` and `AfterUi`.

An auxiliary export may declare a later fallback anchor through `BeforeUi`.
The compiler restricts such passes to imported inputs and exported auxiliary
outputs without same-frame consumers; explicit dependencies cannot cross the
latest declared stage. This preserves resource lifetimes and barriers when the
native sequence lacks the primary anchor. The shared 3DS scheduler selects a
real primary anchor first, never a fabricated opaque boundary.

Published geometry may use the separately declared end-of-first-opaque-prefix
boundary even when later native draws contain another opaque group. That contract
authorizes only geometry already published at the boundary, not whole-scene
postprocessing or a relaxation of UI isolation. See
`TRIAEVUM_GRASS_INTRO_STABILITY.md` for the Grass consumer and verification.

Shader integration is built from typed PICA IR hooks, not source-string injection. Canonical shaders, instrumentation variants and effect shaders use separate cache keys. Instrumentation variants must prove that native primary color and depth are unchanged.

## Required Scene Data

The extension view exposes stable IDs and version counters for geometry, material, texture, transform and skeleton state, plus current/previous transforms, camera, native lights and fog, draw classification, render-target identity and resolved PICA material state. Missing semantics remain explicitly unavailable; extensions must not infer fixture-specific values.

## Implementation Order

1. Audit and classify every current shader mutation in `PicaShaderVariantCache`.
2. Extract a canonical native shader cache with no instrumentation or advanced-effect settings.
3. Add a `Native Fidelity` profile that disables every extension and auxiliary output.
4. Complete PICA semantic parity and native frame composition against Azahar evidence.
5. Stabilize `PicaSceneFrame`, `NativeSceneView`, typed shader hooks and the extension render graph.
6. Move existing optional effects behind these contracts one at a time, without changing their behavior.
7. Optimize pipeline creation, uploads, resource reuse and graph scheduling.
8. Add new advanced effects only after the fidelity and extension contracts pass their gates.

Current status: steps 1-3 are implemented by `PicaShaderPipelineCache`, split canonical/instrumented GPU shader caches, strict `Authentic` selection and Vulkan domain diagnostics. The frontend-output portion of step 4 is closed by a directly published native color/depth contract and a rejecting cache-boundary audit; broader PICA parity remains active. The detailed classification is in `OOT3D_NRI_SHADER_MUTATION_AUDIT.md`. Step 5 now owns typed auxiliary-output requirements, the display-effect graph, graph-derived resource/barrier plans, directional shadows and the first real `GeometryProvider`. Native Fidelity physically allocates only native color, depth and Shadow2D resources; instrumented profiles retain the stable five-MRT guide ABI. Step 5 remains active until every optional effect has migrated and the scene-view contract is complete.

### Attachment Contract Evidence (2026-08-24)

2026-09-07: effect activation changes must preserve native color/depth and
retained display-transfer images. Cached auxiliary storage may remain allocated
when unused, but the frame graph alone selects active render/clear bindings.
Authentic cold start still allocates only native surfaces. See
`TRIAEVUM_F2_NATIVE_PRESENTATION.md` for the isolated attachment lifecycle owner
and repeated-switch validation; turning effects Off is not target invalidation.

2026-10-03: location 6 becomes the shared **geometry guide**
(`R32G32B32A32_UINT`, `renderer3ds/pica_geometry_guide.h`). With global
illumination the instrumented layout already uses eight color attachments, the
device maximum, so the face normal every effect needs could not get its own
attachment: it shares this one. R holds the native normal before relief and
normal maps, octahedral unorm16x2 (0 = no native surface); A the native window
depth as float bits. Both keep the outline contract above: written only by
native depth-tested, depth-writing, RGB-replacing scene geometry. G holds the
face normal of the visible surface, from the screen derivatives of the
rasterized triangle's view-space position (taken in the main prologue, before
the native alpha test), view space, oriented toward the eye, octahedral
unorm16x2 (0 = unknown, for programs without a view vector); B its kind (0 none,
1 native scene, 2 extension geometry). G and B follow the scene depth: every
draw that writes depth and the normal guide writes them, blended depth writers
and Grass included (Grass writes G/B only). Per-channel write masks keep the two
pairs independent in one output. The format is integer: never blended, and a
multisample resolve keeps one sample (dynamic rendering resolves attachment 6
with `SAMPLE_ZERO`) instead of averaging packed values. The cleared value
(0, 0, 0, bits of 1.0) equals the previous float clear. Octahedral round trip:
at most 0.004 degrees, far below the 0.1 degrees the GI self-intersection test
tolerates (RGB8 normals: 0.1-0.2 degrees). Outline readers decode R and A;
their results are unchanged. Readers fetch the guide without a sampler
(`utexture2D`, `GL_EXT_samplerless_texture_functions`): a linear sampler may
not be paired with an integer view. A pass whose shader declares the guide in
a frame that has none binds a 1x1 cleared integer image, never the color
source. The face normal is published when a reader
requests `FaceNormalGuide`: `EffectsReadFaceNormal` names the readers (MXAO's
`Geometry` normals and its face-normal debug views, 2026-10-03); the other AO
and GI readers move to it one at a time, each with its own captures.
`OOT3D_GRAPHICS_FACE_NORMAL_GUIDE=1` also requests it for diagnostics. The
coherence check against MXAO's normals from depth exposed a projection error of
every depth reconstruction (see "Sample projection" in
`TRIAEVUM_BRIXELIZER_GI.md`); with it fixed they agree on every interior pixel.

2026-09-07: `OutlineGeometryGuide` appends location 6 (`RGBA32_SFLOAT` native
normal/window depth), making the instrumented ABI seven color attachments.
Only native depth-tested/depth-writing, RGB-replacing scene geometry publishes
it. Transparent depth-writing canvases preserve it; unconditional initializers
invalidate it. The restored scene-guide outline response reads native world
depth from this attachment and mixed depth only for extension-marked samples;
UI/layer depth must never become a contour input. Normal-guide response and
extension neighbor eligibility follow the pre-isolation renderer. Where Grass
intersects an existing native contour, its complete native footprint replaces
the masked gather, preserving line width. This supersedes total exclusion of
extension depth from the detector, not the scene/UI composition boundary.
Native transparency and depth-visible extension geometry publish inverse window
depth into shared occlusion alpha (MAX); it can hide a completed contour only
when in front of the native surface that generated the edge. A dilated silhouette
retains its contributing foreground depth even over background pixels; comparing
only against the center pixel incorrectly halves its width against Grass.
Occlusion cannot write the native geometry guide. Color clears, not
depth-only layer clears, reset this guide. Native Fidelity remains color-only.
Native transparent coverage uses an isolated guide-only raster pass with reused
GPU bindings: mixed extension depth cannot reject its fragments. It writes only
native-occlusion alpha, before native stencil mutations, with no scene-color,
depth or stencil writes. Its depth comparison is performed against the native
geometry guide in the shared visibility step, never against mixed scene depth.
The graph owns both consumers, MSAA resolve and native-clear lifecycle. See
`TRIAEVUM_NATIVE_OUTLINE_GUIDE.md` for the current contract and verification.

Current extension: the native-fog outline guide appends attachment 5, bringing
the instrumented ABI to six color attachments. Canonical Native Fidelity still
uses one. `NativeFogFactor` is a typed frontend capability; the extension graph
owns its auxiliary output and pre-UI consumption. Earlier counts below are
historical measurements, not the current instrumented layout. See
`TRIAEVUM_GRASS_OUTLINE_UPDATE.md` for implementation and validation.

- Unit coverage: 9/9 shader-pipeline/attachment tests and 4/4 Vulkan-diagnostics tests pass.
- Native Fidelity, 180 frames: 14,341 canonical draws, color attachment count `1`, auxiliary mask `0`, two targets backed by six images, no instrumented/requested draws, rejects, Vulkan warnings or NRI warnings.
- Instrumented profile, 180 frames: 28,410 requested draws, color attachment count `5`, auxiliary mask `0x0B` (normal/material/ambient), two targets backed by fourteen images, no rejects, Vulkan warnings or NRI warnings.
- Strict shader resolution reports 69/0 and 75/0 AOT hits/misses respectively against the 95-entry pack.
- A Native Fidelity savestate written with the reduced target set reloads successfully at guest frame 10,621; canonical capture synthesizes schema-compatible guide defaults and restore omits uploads for physically absent guide targets.
- Reproducible artifacts are under `I:/oot3dre_work/visual-parity/` with the `native-fidelity-attachment-*` and `instrumented-attachment-*` prefixes.

### Typed Extension Graph Status (2026-08-25)

- `EffectGraph` now compiles typed contract kinds, the eleven scheduling stages above, declared resource reads/writes, dependency order and resource lifetimes.
- Compilation rejects stage inversions, cycles, missing producers, unordered writers, observer writes and auxiliary passes that overwrite native resources.
- `BuildPicaExtensionGraph` is the single authority mapping enabled extensions to auxiliary PICA outputs. Its compiled requirements now drive the frame attachment contract; the former parallel settings-to-mask resolver has been removed.
- `BuildDisplayEffectPlan` declares the concrete inputs and outputs of guides, CACAO, HiZ/reflections, outline, motion, temporal/spatial AA, upscalers, composition and scanout.
- The graph is rebuilt only when the feature request set changes, not per frame. A dedicated 5-test graph suite and the 9-test shader/attachment suite pass.
- Runtime revalidation over 60 frames preserves Native Fidelity at `1/0` attachment count/mask (4,900 canonical draws) and the instrumented profile at `5/0x0B` (9,651 draws), with zero Vulkan/NRI warnings, errors or rejected scene draws.
- The compiled graph now emits abstract resource barriers for external-to-read and write hazards. The first Vulkan migration uses its attachment mask for bidirectional guide sampling barriers, so unrequested guide images are no longer transitioned.
- A 90-frame CACAO validation resolved mask `0x09` (normal/ambient), executed 44 CACAO passes and reported 12 physical guide-image barriers on active frames, with zero Vulkan/NRI warnings, errors or rejected draws. The corresponding strict run also exposed that CACAO variants are not yet covered by the 95-entry AOT shader pack (first missing fragment key `0xf5d712347969f6ac`); this affects extension-pack completeness, not Native Fidelity.
- The graph currently validates declarations, owns attachment selection and drives the migrated guide barriers. Existing GPU effect commands still execute through their established backend sequence; migrating their dispatch and remaining barriers is the next step and must preserve current output.
- Display-effect dispatch now resolves typed pass identities directly from the compiled graph. The former booleans remain compatibility projections populated only after graph compilation; production Vulkan dispatch no longer treats them as a second authority.
- Each compiled pass exposes its declared resource bindings and the diagnostics stream records the aggregate binding contract. A 30-frame Native Fidelity run resolved `1` pass, `2` resources/bindings and attachment count/mask `1/0`; a TAA run resolved `4` passes, `8` resource lifetimes, `13` bindings, `6` declared barriers and attachment count/mask `5/0x06`.
- The TAA run executed 29 temporal and 29 motion passes, used direct typed fragment/vertex programs for all 5,202 instrumented draws and reported zero legacy analyses, rejected draws or Vulkan/NRI warnings and errors. Reproducible artifacts use the `graph-dispatch-native-*` and `graph-dispatch-taa-*` prefixes.
- Every compiled display pass now has a fixed-size execution ledger. It distinguishes newly executed, reused, fused, skipped, failed and unresolved work, rejects duplicate or undeclared records, and reports both pass masks and active binding counts without per-frame heap allocation.
- Ledger integration exposed two declaration/runtime mismatches. FSR now declares and depends on its real HiZ depth preparation rather than claiming native-depth input, and non-world display surfaces cannot request world effects. Duplicate presentations of the same world surface now reuse the already generated guide and TAA outputs instead of silently scanning out an unprocessed image (motion is computed by each execution since 2026-10-12, see Upscaler motion).
- Native Fidelity validation over 30 frames records 59 declared/executed scanouts, 5,202 canonical draws, attachment count/mask `1/0`, and zero skipped, failed, unresolved, duplicate or undeclared outcomes. The equivalent TAA run records 29 motion and 29 TAA executions, 29 fused and 87 reused logical passes across 59 presentations, and zero skipped, failed or unresolved outcomes. Both runs have zero Vulkan/NRI warnings and errors; artifacts use the `graph-execution-native-*-v2` and `graph-execution-taa-*-v3` prefixes.
- `DisplayEffectResourceTable` is the first concrete graph-binding layer. It maps every typed `EffectResource` to a versioned image or semantic identity using a fixed array, validates declared reads, and carries native image/view handles, format, extent and color-space metadata without per-frame allocation.
- Scanout now selects its primary input from the compiled graph and resolves the seven auxiliary descriptor slots by resource identity rather than parallel image/view conditionals. A compatibility fallback is explicit and counted. Native Fidelity and TAA each resolve all `59/59` scanout reads with zero missing resources or fallbacks, while preserving the execution counts above and zero Vulkan/NRI warnings or errors. Artifacts use the `graph-bindings-native-*-v1` and `graph-bindings-taa-*-v1` prefixes.
- The resource table is now populated incrementally as concrete effect producers complete or are reused. TAA resolves its scene/composite color and motion-vector inputs from the pass declarations and publishes `TemporalColor` with explicit linear-color metadata; its production dispatch no longer selects those images through parallel feature conditionals.
- A 30-frame TAA validation resolved all `146/146` declared TAA and scanout reads, executed 29 motion and 29 temporal passes, recorded 87 reused and 29 fused logical passes, and reported zero missing resources, compatibility fallbacks, skipped/failed/unresolved work or Vulkan/NRI warnings and errors. Native Fidelity remained at `59/59` resolved reads, 5,202 canonical draws and attachment count/mask `1/0`. Artifacts use the `graph-bindings-*-pass-*-v1` prefixes.
- Motion now binds `NativeSceneView` as a semantic resource using the stable runtime object identity and its real monotonic camera serial as the generation. Native depth, material guide and rigid-motion guide descriptors are resolved from the graph resource table before dispatch, and `MotionVectors` is published through the same producer path.
- The corresponding 30-frame TAA run resolved all `262/262` Motion, TAA and scanout reads with zero missing resources or compatibility fallbacks, while executing 29 motion and 29 temporal passes without Vulkan/NRI warnings or errors. Native Fidelity remained unchanged at `59/59` reads, 5,202 canonical draws and attachment count/mask `1/0`. The graph/resource suite now passes 11/11 tests; artifacts use the `graph-bindings-motion-*-v1` prefixes.
- HiZ depth preparation now resolves `NativeDepth` through the graph table and publishes `HierarchicalDepth`. Its same-frame reuse is keyed by render-target namespace and physical display address; the previously write-only `mHiZExecutedThisFrame` state now prevents duplicate depth-pyramid dispatches.
- A 30-frame HiZ-reflection run resolved all `88/88` HiZ and scanout reads with no fallback, reduced HiZ dispatches from 58 to 29 and recorded the other 29 graph outcomes as reused. Reflections still dispatches 58 times and is intentionally reported as the next scheduling/migration defect. Native Fidelity remains at `59/59` reads, 5,202 canonical draws and `1/0` attachments, with zero Vulkan/NRI warnings or errors. Artifacts use the `graph-bindings-hiz-*-v1/v2` prefixes.
- Reflection contracts now distinguish the real HiZ and FidelityFX SSSR inputs. Both consume the versioned `NativeSceneView`; SSSR additionally depends on Motion, consumes the versioned `PicaSceneFrame`, native depth and motion vectors, and publishes `LinearWorkingColor` explicitly instead of silently rebinding `SceneColor`. Scanout and downstream reconstruction declare the corresponding producer dependency.
- Both reflection providers now resolve concrete images from the graph table and reuse their output only for the same frame and scene-surface identity. A 20-frame HiZ run resolved `153/153` reads and executed 19 HiZ plus 19 reflection passes. The equivalent SSSR run resolved `286/286` reads and executed 19 each of HiZ, Motion, SSSR, linear-color, IBL and material-resolve work; the previous baseline executed the four reflection subpasses 38 times. Neither run used a fallback or emitted Vulkan/NRI errors.
- A combined SSSR+TAA run resolved `343/343` reads, executed HiZ, Motion, SSSR, Composite and TAA 19 times each, and recorded 114 reused plus 19 fused outcomes with zero skipped, failed or unresolved passes. Native Fidelity remained at attachment count/mask `1/0`, 3,413 canonical and zero instrumented draws over 20 frames. The graph/resource suite now passes 12/12 tests; artifacts use the `graph-bindings-reflection-*-v1/v2` prefixes.
- CACAO now resolves the versioned native scene view, depth and normal guide declared by the graph. Its projection remains semantic scene-view state rather than a fabricated image resource. A direct 20-frame CACAO run resolved `172/172` reads and executed CACAO 19 times with zero missing resources, compatibility fallbacks or Vulkan/NRI errors; artifacts use the `graph-bindings-cacao-direct-runtime-v1` prefix.
- The graph now describes the real fused-composition contract. CACAO publishes AO while the composer consumes AO plus ambient guide; reflections publish reflection color while the composer consumes it plus material guide; outline is a fused depth/normal operation and no longer claims a nonexistent `OutlineColor` image. Direct scanout declares the same auxiliary reads whenever no intermediate composite is required.
- The Composite dispatch resolves every color, guide, depth and effect input through `DisplayEffectResourceTable`, including format and linear-color metadata. A combined CACAO+TAA run resolved `286/286` reads, executed CACAO, Composite and TAA composition 19 times each, and recorded 95 reused plus 19 fused outcomes with zero skipped, failed, unresolved or fallback work. Explicit TAA composition state prevents duplicate ambient accounting when the temporal output is reused by a second presentation; artifacts use the `graph-bindings-cacao-taa-runtime-v3` prefix.
- Native Fidelity regression remains unchanged over 20 frames: `39/39` reads resolved, 3,413 canonical and zero instrumented draws, with zero fallback or Vulkan/NRI errors.
- Motion now publishes both `MotionVectors` and the distinct `ReactiveMask` resource. Temporal upscalers declare native scene-view semantics, provider-specific depth, motion and reactive inputs; FSR and DLSS resolve every descriptor, format, extent and color-space flag from that contract before dispatch.
- NIS and SMAA resolve their source color from the graph and publish versioned reconstruction outputs. Upscaler reuse is keyed by provider plus render-target namespace/physical address and carries output extent, linear-color and fused-effect state, preventing a second presentation from redispatching or losing composition. This also fixes NIS with active effects: the runtime now executes the graph-declared Composite producer rather than sampling uncomposed scene color.
- Boot validation executed 114 NIS dispatches plus 114 same-surface reuses with `342/342` reads resolved, and 35 SMAA dispatches plus 35 reuses with `105/105` reads resolved. A NIS+CACAO transition run resolved `198/198` reads, then executed the full CACAO/Composite/NIS chain on every stable active frame. All runs reported zero binding fallbacks, failed/unresolved work or Vulkan/NRI errors; artifacts use the `graph-bindings-nis-*` and `graph-bindings-smaa-*` prefixes.
- FSR/DLSS are structurally migrated. The apparent first-dispatch FSR block was traced to packaging rather than provider execution: `lus_deploy_oot3d_nri_runtime` already described the FidelityFX/NGX runtime copies but no executable invoked it, so `amd_fidelityfx_vk.dll` was absent from the native-game directory. All Vulkan hosts now invoke that deployment contract. A checkpoint run with the deployed runtime completed `12/12` frames, dispatched FSR in all ten active world frames, resolved `13/13` graph reads per active frame and reported zero missing resources, binding fallbacks or NRI/Vulkan errors. DLSS remains hardware-dependent and is not inferred from FSR coverage.
- PICA depth/guide transitions are now selected from compiled resource hazards and resolved through `DisplayEffectResourceTable`; production no longer reconstructs the set from a parallel attachment mask and raw-image structure. The physical batch exposes declared, resolved and missing counts, and runtime execution rejects an incomplete barrier plan instead of silently omitting a resource.
- CACAO+TAA runtime validation resolves all five required physical surfaces (depth, normal, material, rigid-motion and ambient) in both directions, preserves `286/286` graph reads and reports zero fallback, failed/unresolved work or Vulkan/NRI errors. Two presentations currently produce 20 image barriers per active frame; pruning the redundant second transition is intentionally assigned to the upcoming lifetime/reuse schedule. The focused graph/resource suite remains 12/12. The monolithic foundation target still has unrelated pre-existing Clang narrowing and stale `NriPicaDisplayCopyDesc::Width/Height` compile errors.
- `EffectGraphPhysicalPlan` now converts every declared lifetime and concrete binding into a fixed-capacity physical plan without per-frame heap allocation. It classifies semantic state, external images, native attachments, transient images and temporal history; reports missing bindings and unique physical images; computes peak live transients; and proposes aliases only for non-overlapping resources with identical storage class, format and extent. It does not yet mutate backend allocation.
- The plan exposed a producer-contract defect on duplicate presentations: `CompositeColor` was recorded as reused but not republished. Composite execution now has explicit same-frame identity and linear-color state, so both immediate consumers and reused downstream passes receive the same versioned binding.
- Runtime validation over 20 frames resolves Native Fidelity at `78/78`, TAA at `344/344`, and CACAO+TAA at `496/496` physical resources across 39 display invocations each. CACAO+TAA also preserves `286/286` declared reads. All three have zero missing bindings, compatibility fallbacks, failed/unresolved work or Vulkan/NRI errors. TAA peaks at two live transients plus one temporal history; CACAO+TAA peaks at three. The current concrete formats and overlapping lifetimes yield no safe alias opportunity, preventing speculative memory reuse. Artifacts use the `graph-physical-*-v1/v3` prefixes.
- Guide transitions are now scheduled at the first pass that actually executes and reads each native depth/guide resource. A fixed resource mask prevents repeated transitions within one invocation, direct scanout requests only the guides used by effects that remain unfused, and restore returns exactly the transitioned subset to attachment layouts. Reused TAA/composite/upscaler output therefore causes no synthetic guide work.
- With identical graph contracts, CACAO+TAA guide barriers fall from `380` to `190` and TAA from `228` to `114` over 20 frames. CACAO without reconstruction remains correct: its first presentation transitions depth/normal/ambient while the reused direct scanout still transitions ambient, producing `152` barriers, 19 CACAO dispatches and 38 ambient compositions. The three runs preserve physical closure (`496/496`, `344/344`, `268/268`), declared read closure (`286/286`, `172/172`, `172/172`) and zero fallback, failed/unresolved work or Vulkan/NRI warnings/errors. Native Fidelity remains `78/78`, `39/39`, 3,413 canonical and zero instrumented draws with no guide barriers.
- The focused graph/resource suite passes 14/14 and Vulkan diagnostics passes 4/4. The next physical scheduling step is to move pass-internal image transitions behind the same graph authority; the monolithic foundation target remains blocked only by the previously documented Clang narrowing and stale display-copy test fields.
- `EffectPassBarrierPlan` is the first graph-derived provider-state contract. It maps each declared pass output to its dispatch and completion access, retaining write-only state when no later graph pass reads that output. `MotionVectorPass` now consumes this contract rather than hardcoding both outputs as shader-readable; TAA transitions `MotionVectors` for its consumer while leaving the unused `ReactiveMask` write-only, whereas FSR/DLSS plans keep both outputs readable.
- A 20-frame TAA validation emitted `39` internal Motion image transitions and elided `37` of the former candidate transitions while preserving `344/344` physical resources, `172/172` declared reads, 19 Motion and 19 TAA executions, and the already reduced `114` guide barriers. Native Fidelity emits no internal effect transitions and remains at `78/78` physical resources, `39/39` reads, 3,413 canonical draws and zero instrumented draws. Both runs have zero binding fallback, failed/unresolved work or Vulkan/NRI warnings/errors; artifacts use the `graph-pass-barriers-*-v1` prefixes. The focused graph/resource suite now passes 17/17 tests and Vulkan diagnostics remains 4/4.
- The same graph-owned output contract now drives `SceneCompositePass`, `TemporalAaPass` and `NriPicaScanoutPass`. A shared fixed-size execution report separates graph-planned transitions from provider-private temporal transitions; TAA no longer prepares an unread history image, and the remaining history-read candidates are reported independently rather than hidden in provider barrier totals.
- Over 20 frames, TAA emits exactly `155` graph transitions (39 Motion, 38 TAA and 78 Scanout), elides 37 graph candidates and all 18 no-op private history candidates, while preserving `344/344` physical resources, `172/172` reads and 114 guide barriers. CACAO+TAA adds 38 Composite transitions for a total of `193`, preserving `496/496` resources, `286/286` reads and 190 guide barriers. Native Fidelity intentionally reports only the 78 graph-owned Scanout transitions across 39 presentations, with `78/78` resources, `39/39` reads, 3,413 canonical draws and zero instrumented draws. All profiles report zero fallback, failed/unresolved work or Vulkan/NRI warnings/errors; artifacts use the `graph-pass-barriers-public-*-v2` prefixes. The focused graph/resource suite now passes 19/19 and Vulkan diagnostics remains 4/4.
- Reconstruction outputs are migrated through the same contract. NIS, FSR and DLSS derive `UpscaledColor` dispatch/completion access from their selected graph pass; SMAA derives `AntiAliasedColor` from the graph while keeping edge and blend-weight images explicitly provider-private. No internal SMAA image is exposed as a fabricated graph resource.
- A 120-frame NIS boot run executes 115 NIS passes and 230 scanouts, producing exactly `690` graph transitions with `690/690` physical resources and `345/345` reads. The equivalent SMAA run executes all three stages 112 times plus 224 scanouts, producing `672` graph transitions and exactly `448` private intermediate transitions, with `672/672` resources and `336/336` reads. A stable nine-frame CACAO/Composite/NIS interval closes `162/162` resources and `81/81` reads while executing all three passes nine times; the first activation frame remains separately visible as a startup producer-publication transient. Native Fidelity remains unchanged at 78 Scanout transitions, `78/78` resources, `39/39` reads, 3,413 canonical draws and zero instrumented draws. All validated intervals have zero binding fallback, failed/unresolved work or Vulkan/NRI warnings/errors; artifacts use the `graph-pass-barriers-reconstruct-*-v2` prefixes. At this validation point FSR was still blocked in provider dispatch and DLSS was hardware-dependent; the later FSR closure below supersedes that provider result. The focused graph/resource suite now passes 20/20.
- `HiZDepthPyramidPass` now treats every mip as a physical subresource of the graph-owned `HierarchicalDepth` output. The graph selects dispatch and completion access, while the provider retains only the required per-mip execution order. Scratch transition arrays are allocated with the pyramid in `Configure` and reused, removing two per-dispatch vector allocations. `HiZReflectionPass` similarly exposes only filtered `ReflectionColor`; its raw ray result and the barrier between ray and filter dispatch remain explicitly private. Output-specific execution reports allow the HiZ fallback provider to implement only `ReflectionColor` even when an SSSR graph also declares `LinearWorkingColor`.
- A 20-frame HiZ reflection run executes 19 eleven-mip pyramids, 19 ray/filter pairs and 39 scanouts. Their exact accounting is `418 + 38 + 78 = 534` graph transitions plus 38 private raw-reflection transitions, with `306/306` physical resources, `229/229` reads and 152 guide barriers. The exact rebuilt executable preserves Native Fidelity at 78 graph-owned Scanout transitions, `78/78` resources and `39/39` reads; every sampled draw remains canonical and none is instrumented. Both runs have zero fallback, failed/unresolved work or Vulkan/NRI warnings/errors; artifacts use the `graph-pass-barriers-hiz-*-v4` prefixes. The focused graph/resource suite now passes 21/21.
- The FidelityFX SSSR provider consumes the abstract `Reflections` barrier plan without exposing its implementation stages as fabricated graph passes. The later explicit `WorkingColor` graph pass owns `LinearWorkingColor`, while `ReflectionMaterialResolvePass` owns `ReflectionColor`; generated environment/BRDF images and the raw FidelityFX target remain provider-private. A 20-frame SSSR run executes 19 each of HiZ, Motion, IBL, linear conversion, SSSR and material resolve plus 39 scanouts. It emits `611` graph transitions and elides 37, exactly closing all `648` planned transitions; its 42 private transitions are the four one-time IBL transitions plus two raw-target clear transitions per active frame. The run closes `496/496` physical resources and `362/362` reads with 190 guide barriers. SSSR+TAA emits 687 graph transitions plus the same 37 elisions, 42 private transitions plus 18 elided history candidates, and closes `572/572` resources and `400/400` reads with 152 guide barriers. Authentic regression remains at one attachment/mask zero, 1,905 canonical and zero instrumented draws, 78 graph Scanout transitions, no private transitions, `78/78` resources and `39/39` reads. All three profiles have zero binding fallback, missing reads, failed/unresolved work or Vulkan/NRI warnings/errors; final artifacts use the `graph-pass-barriers-sssr-*-v2/v3` prefixes. This run exposed 63 corrected temporal variants missing from the then-current optional 95-entry AOT pack; the refreshed corpus closure below removes those dynamic resolutions from the covered profiles. The focused graph/resource suite remains 21/21 and Vulkan diagnostics remains 4/4.
- CACAO closes the last public output transition left in the display-effect graph. The pinned FidelityFX CACAO implementation (`0ddca95e`) performs `UNDEFINED -> GENERAL` before writing its output and `GENERAL -> SHADER_READ_ONLY_OPTIMAL` after the final dispatch. `CacaoPass` now validates the graph-derived `AmbientOcclusion` contract and accounts for those exact two provider-emitted graph transitions without duplicating them in the host or exposing CACAO scratch images as graph resources. A 20-frame direct run executes 19 CACAO passes and 39 scanouts, emitting exactly `38 + 78 = 116` graph transitions with no private transitions, while closing `268/268` physical resources and `172/172` reads. CACAO+TAA emits 231 graph transitions plus 37 elisions and 18 elided private TAA history candidates, closing `496/496` resources and `286/286` reads. Authentic remains at one attachment/mask zero, 1,905 canonical and zero instrumented draws, 78 scanout transitions, `78/78` resources and `39/39` reads. All profiles report zero fallback, missing reads, failed/unresolved work or Vulkan/NRI warnings/errors; final-executable artifacts use the `graph-pass-barriers-cacao-*-v2` prefixes. The focused suite now passes 22/22 and Vulkan diagnostics remains 4/4.
- All display-graph public image producers now consume graph-derived dispatch/completion contracts. Remaining direct barriers are intentionally outside this graph: canonical PICA targets, uploads, clears and display copies belong to the native render core; CACAO/FidelityFX scratch images remain provider-private; and grass compaction belongs to `GeometryProvider`. Extension resources must be modeled by their truthful schedule rather than widening the display graph into a second backend authority.
- Directional shadows are now a typed `DirectionalShadowMap` auxiliary output plus a `DirectionalShadowLighting` contributor at the native PICA lighting hook. A two-slot NRI-owned D32 history crosses the unavoidable draw-capture boundary; no resolve texture, final-color rewrite or display-copy composer remains. The provider consumes the graph-derived `DepthAttachment -> ShaderRead` contract, and receiver instrumentation derives from the compiled pass rather than a parallel settings gate.
- The former non-native real-time-shadow implementation is not a compatibility baseline and may be replaced wholesale. It remains useful only as historical evidence for host integration points; its visual policy, receiver selection, resource model and private ownership must not be carried into the typed NRI path. Shadow acceptance is defined by typed PICA/decomp semantics plus framebuffer/validation evidence, not by preserving that implementation.
- Validation exposed and fixed two native-composer ownership defects. Display copy now allocates immutable descriptor sets per invocation until command submission, rather than updating a set already bound by an earlier copy. Scanout now binds auxiliary images only for resources declared as reads by its compiled pass; inactive slots retain the valid primary image instead of silently binding attachment-layout guide images.
- The reworked map uses exact typed PICA `ClipToWorld`/`ViewToWorld` transforms, including generated-shader viewport-Y and clip-Z conversion. Native lights are transformed to world space and clustered by direction; no duplicate register decoder or global draw-local light tuple remains.
- Shadow caster eligibility includes rigid and skinned opaque perspective geometry with valid typed view transforms. The former skeleton-only gate excluded the static level and was removed; native receiver classification remains separate from caster eligibility. Native mode preserves its original receiver policy, while per-pixel/PBR lighting shadows the matching direct-light contribution on actors and static surfaces. Selection never uses scene, actor, shader or texture IDs.
- Earlier `nativeview_v6` Kokiri validation covered projected actor shadows only: 180 frames executed 89 maps, with 19 skinned caster draws, 38 native light candidates in two world-space clusters, and 29 rigid baked receivers. Its extensions-off/on RMSE was `0.00701319`, with clean Vulkan/NRI validation. These historical artifacts remain under `I:/oot3dre_work/visual-parity/directional-shadow-rework-20260825/`; they are not evidence of static-world casting. The scene-environment verification below covers that correction.
- `InteractiveGrass` is now the first closed `GeometryProvider`. Its compiled pass reads only the versioned `PicaSceneFrame` and `NativeSceneView`, writes and exports `ExtensionGeometry`, and schedules at `BeforeTransparent`. Source inspection, publication, execution, reuse, fallback and rejection are recorded in a fixed ledger; settings can no longer authorize grass when the compiled pass is absent.
- Grass-only no longer requests fabricated normal/material/ambient guides. The graph therefore keeps the canonical one-color-attachment path, while grass+TAA selects the stable five-MRT ABI only because TAA declares material and rigid-motion guides. `InteractiveGrassPass` owns matching canonical/instrumented pipeline and fragment-shader variants, selected from `PicaAttachmentRequirements`; this fixed a validation error where the former fixed five-MRT grass pipeline was bound inside a one-attachment rendering scope.
- The normal Kokiri path reaches the declared boundary directly. The display-transfer safety path now runs only for an eligible world/top-screen target that has not already attempted the declared insertion, rather than being invoked for every top/bottom transfer and reported as fallback work.
- Validation over 24 Kokiri frames closes all three gates. Grass-only publishes 23 sources and executes 22 providers after one asynchronous placement-build skip, with zero fallback, unavailable inputs, schedule rejects or failures; it keeps attachment count/mask `1/0`, 2,559 canonical and zero instrumented draws. Grass+TAA preserves the same provider counts, executes 23 Motion and 23 TAA passes, resolves `207/207` reads and `414/414` physical resources, and selects `5/0x06` with 2,559 direct-hook instrumented draws and zero legacy analyses. Authentic declares no provider, publishes or executes no grass, keeps `1/0`, 2,559 canonical and zero instrumented draws, and resolves `46/46` reads plus `92/92` resources.
- All three runs have zero graph binding fallback, missing reads/resources, failed/unresolved work and Vulkan/NRI errors. Their final Vulkan warning count is the same 11-message machine/PICA-interface baseline documented by the shadow tranche; the grass migration adds none. Reproducible artifacts use the `geometry-provider-grass-only-*-v1/v3`, `geometry-provider-grass-taa-*-v1` and `geometry-provider-authentic-*-v1` prefixes under `I:/oot3dre_work/visual-parity/`.
- `NativeSceneView` is now a real schema-versioned, renderer-owned semantic object rather than an alias for the camera singleton. It references the current `PicaSceneFrame` directly, retains current/previous camera state and projects stable per-draw geometry identities/content versions, canonical PICA material-state identities and native texture handles/content hashes without copying geometry, uniforms or image payloads.
- Object identity remains explicitly unavailable because the native draw stream does not expose a stable object owner. Current/previous transform and skeleton semantics are published only when the CMB vertex program is structurally decoded and, for previous state, when the renderer owns exact temporal history; none is inferred from scene, shader hash or asset fixture.
- `DisplayEffectResourceTable` can now bind and resolve typed semantic objects by object identity, monotonic generation and exact schema version. Both `PicaSceneFrame` and `NativeSceneView` use this path; grass source discovery and insertion consume the same stable view, and the TAA display graph resolves both semantic resources rather than an opaque camera identity.
- The frontend-to-renderer draw contract now carries a versioned, typed PICA fragment-feature view. Fragment lighting, fog enable/mode and mode-5 flip are decoded from the native register state rather than inferred from generated GLSL. Visual replay schema `PVR3` persists this state while retaining read compatibility with `PVR1/PVR2` captures.
- A shared native scene-semantic decoder reads the three PICA vertex-light records, depth scale/offset/W-buffer state, fog color and fog LUT identity from the live packed uniform slices. The full LUT and all uniform payloads remain in their existing GPU-backed storage; `PicaSceneFrame` retains only compact summaries, versions and buffer ranges, and `NativeSceneView` exposes those ranges without copying them.
- Grass lighting and fog now consume that general draw environment. Its only additional work is the required view-to-world light-direction conversion and on-demand LUT decode; the former generated-shader string search has been removed.
- Final 24-frame validation preserves all previous counts. Grass-only publishes 23 sources, executes 22 providers and records one asynchronous skip; grass+TAA executes 23 Motion and 23 TAA passes and closes `207/207` reads plus `414/414` physical resources; Authentic declares no geometry provider. Every profile records 2,559 scene draws and 2,559 available native-lighting plus native-fog states, zero rejected draws, missing resources, binding fallback, failed/unresolved work or Vulkan/NRI errors. Attachments remain `1/0`, `5/0x06` and `1/0` respectively. Artifacts use the `native-scene-environment-*-v1` prefixes. Focused coverage is now 29/29 graph/resource tests, 8/8 PICA-frame/scene-view tests, 19/19 shader/attachment tests, 2/2 grass-coordinate tests and 4/4 diagnostics tests, plus passing frontend, Vulkan-plan, bridge and `PVR3` visual-savestate suites.

### Typed Shader Hook Status (2026-08-25)

- Fragment instrumentation now resolves one typed hook/semantic layout and composes normal, ambient, scene-domain, reflection-material, reactive and rigid-motion outputs in one pass. Production no longer feeds each feature the GLSL text produced by the previous feature.
- The typed composer preserves the supported legacy source and variant keys exactly, rejects MRT conflicts before shader compilation and locates the real `main` epilogue rather than relying on the final brace in the file. Its dedicated cache suite passes 16/16 tests, including direct-layout use, stale-layout fallback and byte/key equivalence for toon and temporal-vertex composition.
- Runtime validation preserves Native Fidelity at 9,870 canonical draws and attachment count/mask `1/0`; the instrumented profile resolves 5,286 instrumented draws at `5/0x0B`. Both strict AOT runs have zero misses, rejected draws or Vulkan/NRI errors.
- The canonical fragment frontend now emits a versioned hook/semantic layout with the generated GLSL. The draw-plan cache retains it, the Vulkan bridge carries it through the public renderer contract and the fragment composer consumes it directly. Source-size/schema validation leaves text analysis only as an explicit compatibility fallback for legacy or restored shaders.
- Direct-path runtime validation over 60 CACAO frames recorded 9,870 instrumentation requests, 9,870 direct-hook draws, zero legacy analyses, attachment count/mask `5/0x09`, 59 CACAO passes and zero rejected draws or Vulkan/NRI warnings and errors. Native Fidelity remains 9,870 canonical draws at `1/0`, with strict AOT resolution `66/0` hits/misses.
- Toon now emits a typed instrumentation plan and is composed with the other fragment outputs against the original canonical source. A 30-frame strict run recorded 5,174 toon instrumentation requests, 5,174 direct-hook draws, zero legacy analyses, zero validation errors and exact AOT resolution `72/0` hits/misses.
- The canonical vertex frontend now emits a versioned temporal-program layout and pre-resolved previous-state/main programs. The draw-plan cache retains this immutable program, the Vulkan bridge exposes it through the renderer contract and the pipeline cache assembles temporal variants without production source analysis or token rewriting.
- Fragment sampler routing is now part of the versioned frontend hook layout. It records which native sampler is actually reached from `main`, so helper functions that exist but are not called no longer fabricate texture use. Grass source classification consumes this typed route; GLSL analysis remains only an explicit stale/legacy-layout compatibility path.
- The vertex hook layout now carries a typed texture-coordinate program. The frontend derives CMB affine UV selection, route enables/scales, matrix rows and homogeneous mode structurally from decoded PICA bytecode and output semantics. It does not use a generated-GLSL pattern, shader hash, scene identity or configured texture hash. The grass consumer resolves the live transform from this contract and current uniform bytes, and refuses unsupported typed programs instead of guessing.
- Native texture identity is split by purpose. Full decoded mip-chain content identifies GPU resources and cache entries; base-level content identifies semantic material use and remains compatible with Azahar dump/load names. Both identities cross draw state, semantic trace, savestate reconstruction, Vulkan plan/bridge and `PicaSceneFrame`, preventing mip changes from aliasing GPU resources while preserving stable material matching.
- Real generated shaders exposed and fixed a legacy temporal defect: the duplicated PICA helper and main-local identifiers were not scoped independently, producing invalid GLSL when TAA was enabled. Both the typed producer and compatibility fallback now apply the complete generated-vertex identifier contract.
- A 30-frame TAA run recorded 5,202 instrumentation requests, 5,202 direct fragment layouts, 5,202 direct temporal vertex programs, zero legacy fragment/vertex analyses, 29 TAA passes, 29 motion passes, zero rejected draws and zero Vulkan/NRI warnings or errors. Artifacts use the `typed-temporal-hooks-*-v3` prefix under `I:/oot3dre_work/visual-parity/`.
- A real Kokiri grass draw now resolves its UV transform (`U scale 3.03993e-05`, native V flip and offset) through the typed bytecode-derived layout on every covered frame. The prior rejection changed from an unsupported generated-GLSL pattern to successful source publication without retaining a fixture-specific parser or correction.
- The then-published 71-entry optional-effect AOT pack did not contain these corrected temporal modules and the covered run reported 69 dynamic shader resolutions. The refreshed default corpus below supersedes that pack while preserving this result as the evidence that required regeneration.
- Vertex-hook schema 3 adds typed `ModelViewProjection3x4` and `MatrixPalette3x4` layouts. The frontend follows PICA `DP4`/`DP3`, `MOVA`, indexed uniform access, `CALL`, `IFU` and bone-index/weight feeds in bytecode; it does not parse generated GLSL or key behavior by shader hash, scene, actor or texture.
- The read-only decomp corroborates this boundary. `I:/oot3decomp/src/code/sys_matrix.c` and `include/oot3d/matrix.h` define the explicit row-major 0x30-byte `Oot3dMtxF`; `z_renderer_vertex_layout_runtime.c` converts CTXB/CMB declarations through typed semantic/format maps; `z_model_render.c` and `docs/reconstruction/model_render_submit.md` keep queue submission and model `prepareRender` ownership separate. The renderer therefore publishes uniform layouts and references rather than fabricating CPU scene objects.
- `PicaSceneFrame` and `NativeSceneView` schema 3 retain compact transform/skeleton summaries plus versioned current and previous vertex-uniform slices. Palette, geometry and uniform payloads remain renderer-owned; previous state reuses the temporal path's existing uploaded slice and is unavailable when exact history is absent.
- The real canonical vertex program `0x78765df8bf6684cc` remains structurally decoded from 512 program words and 54 swizzles: projection `f0`, view `f4`, model/palette `f20`, position/normal inputs `0/1`, skeleton booleans `b2/b3`, bone index/weight inputs `6/7` and up to four influences. A four-frame scene capture covered 351/351 transforms, 75 active skeleton draws and 72 multi-influence draws. The final rebuilt boot capture reconfirmed the same immutable dictionary and 9/9 draw-local transform states; its interval intentionally contained no active skeleton draw. Static layout and dynamic branch state are separate trace records, so first-use pose cannot alter shader identity. Artifacts are `transform-bytecode-capture-v3.jsonl` and `transform-bytecode-capture-v5.jsonl` under `I:/oot3dre_work/visual-parity/`.
- Fixed-delta 24-frame validation records transform coverage equal to every scene draw: Authentic `1,277/1,277`, Grass-only `2,437/2,437`, and Grass+TAA `2,437/2,437`. Skeleton states are `275`, `525` and `525`; only the exact-history TAA profile publishes previous state (`1,304` transforms and `456` skeletons). Timing modes differ between these profile configs, so cross-profile draw totals are not performance comparisons. All runs report zero rejected draws, missing reads/resources, failed passes or Vulkan/NRI errors.
- Focused closure is green after the shadow rework: 11/11 PICA-frame/scene-view/lighting-semantic tests, 21/21 shader/attachment tests and 4/4 Vulkan diagnostics. The whole-AOT product audit remains 12,419 compiled functions, three host boundaries and zero residual A32 entries. The next tranche is physical lifetime and aliasing for graph-owned extension resources.
- Physical lifetime ownership has started at renderer commit `4f41f8be`. `BuildEffectTransientAllocationPlan` turns compiled graph lifetimes plus explicit image requirements into fixed-capacity physical slots before any provider dispatch; it rejects missing lifetimes, duplicate/invalid declarations and overlapping aliases, and unions image usage only across compatible non-overlapping resources.
- `NriEffectGraphTransientImageArena` owns those physical slots and exposes borrowed typed bindings to providers. `SceneCompositePass` is the first migrated consumer: it retains only pipeline, descriptors, barriers and execution state, while `CompositeColor` allocation/destruction belongs exclusively to the arena. Resolution/sample-count/state invalidation clears the borrower before destroying arena images.
- A deterministic 20-frame Kokiri CACAO+TAA run configured the arena 36 times across multiple display transfers, executed 18 composite passes, allocated one physical image once and reused the same configuration 35 times. It reported zero binding fallback, missing reads, failed/unresolved passes, Vulkan errors or NRI warnings/errors. The extensions-off regression performs zero arena/composite work, keeps 302 canonical and zero instrumented draws, and retains only the documented 11-warning machine/PICA-interface baseline. Artifacts are under `I:/oot3dre_work/visual-parity/effect-transient-arena-20260825/`.
- A plan that a cached configuration covers reuses it (2026-10-14): every requested resource exists there with the same requirement and storage class, and resources in distinct slots of the plan stay in distinct images. The TopScreen pause plan is the gameplay plan without `CompositeColor`; a second configuration made every pass see new images at each toggle and rebuild its screen resources behind a `CompleteRecordedWork` drain (`composite_taa` about 34 ms). The covering configuration keeps its own plan; an exact match is still preferred.
- Renderer commit `d80883b2` realizes the first cross-provider alias. `ReflectionMaterialResolvePass`, `SceneCompositePass` and the public SMAA output now borrow graph-owned bindings; SMAA retains ownership only of its two private edge/weight images. The arena also owns the physical-image state tracker, so a reused slot carries its real Vulkan state from reflection completion into the later SMAA write instead of restarting from a fictitious undefined state.
- The actual SSSR+SMAA display graph deterministically maps three RGBA16F resources to two physical images: `ReflectionColor` and `AntiAliasedColor` share one slot because their lifetimes are disjoint, while `CompositeColor` remains separate because it overlaps both adjacent pass boundaries. A 30-frame Kokiri run covered 28 reflection resolves and 28 SMAA executions; 56 arena configurations allocated two slots once, reused the plan 55 times and reported one alias opportunity per presentation. Vulkan/NRI validation recorded zero errors. The composer now binds only optional guide inputs declared by its compiled pass, removing a hidden `AmbientGuide` attachment-layout dependency exposed by the validation run. Artifacts are under `I:/oot3dre_work/visual-parity/effect-transient-alias-20260825/`.
- Follow-up renderer commit `868230ce` closes the multi-presentation SMAA mismatch at its source. The preparatory overlay presentation reuses the same world output, but the old key compared the graph-input extent against the packed display-transfer extent. Reuse is now keyed to the declared SMAA input domain; SSSR+SMAA records zero skipped passes, missing reads/resources and fallbacks across both presentations. SMAA-only also allocates its graph output without requiring an unrelated guide target and closes the same gates. Both validation profiles report zero Vulkan/NRI errors; the final artifacts use the `v3` and `smaa_only` suffixes in the same directory.
- Renderer commit `b536a989` moves `LinearWorkingColor` and the public filtered HiZ `ReflectionColor` into the same graph arena. HiZ retains only its raw ray result as provider-private scratch. Temporal AA history is deliberately excluded: it is a persistent two-image cross-frame contract, not a transient output, and therefore cannot participate in physical aliasing.
- The final SSSR+SMAA run plans four logical outputs into three physical images per presentation (`8/6` aggregate), with one safe alias and a peak of three live resources. HiZ+SMAA plans three logical outputs into two images per presentation (`6/4` aggregate), again with one alias and a peak of two. Both paths execute every declared pass with zero skips, missing reads/resources, binding fallbacks, failures, unresolved passes or Vulkan/NRI errors. Their framebuffer captures are valid and the extensions-off regression performs zero arena work. Reproducible configs, diagnostics, runtime reports and captures are under `I:/oot3dre_work/visual-parity/effect-transient-public-outputs-20260825/`.
- Renderer commit `1d889e12` migrates the paired `MotionVectors` and `ReactiveMask` outputs. `MotionVectorPass` now borrows exact typed images and state trackers and retains only compute, uniform and descriptor ownership. In the real SSSR+SMAA graph, six logical outputs occupy four physical images per presentation: motion later aliases composite, reflection later aliases SMAA, and the differently formatted reactive mask remains separate. Runtime accounting is `12/8` aggregate resources/slots, four alias opportunities, peak live count three, and zero graph or validation failures.
- CACAO+TAA independently exercises motion next to persistent temporal history. It executes Motion, CACAO, Composite and TAA with `6/6` aggregate arena resources/slots, no aliases, valid history and a valid framebuffer. The earlier direct TAA-only capture exposed that packed physical `SceneColor` and logical guide-domain color were being conflated; that evidence is retained under `I:/oot3dre_work/visual-parity/effect-transient-motion-20260825/`.
- Renderer commit `656613a7` closes that source-domain gate with an explicit `WorkingColor` graph pass. It samples the immutable packed display image using its real physical extent, writes graph-owned RGBA16F `LinearWorkingColor` at the guide/render-target extent, and publishes that typed resource to direct TAA, temporal upscalers and SSSR. Reflection output encoding is tracked independently, so an SSSR-to-HiZ fallback cannot disable or relabel the shared working color.
- Duplicate presentations of the same scene identity reuse the first `WorkingColor` result. This is part of the resource contract rather than a descriptor workaround: it prevents a second dispatch from updating an already-recorded Vulkan descriptor set and records the later logical pass as reused.
- Validation-enabled direct TAA now produces a complete framebuffer and reports one working-color conversion per frame, `6/6` aggregate transient resources/slots, peak live count three, and zero missing reads, binding fallbacks, skipped/failed/unresolved passes or Vulkan/NRI errors. SSSR+SMAA retains `12/8` aggregate resources/slots, four safe aliases and peak three; CACAO+TAA remains `6/6` and correctly omits the redundant working pass because Composite already establishes the logical domain. The extensions-off profile performs zero arena and working-color work. Artifacts are under `I:/oot3dre_work/visual-parity/effect-working-color-20260825/`.
- Renderer commit `2e3b0e89` moves the public `UpscaledColor` output into the graph arena. `NriUpscalerPass` now borrows the typed image and its persistent physical-state tracker; NIS/FSR/DLSS retain ownership only of provider objects, histories and scratch state. Screen invalidation releases the borrower before the arena destroys or aliases its physical slot.
- Upscaler sizing now preserves the actual producer domain. Direct NIS consumes packed `SceneColor` and resolves `480x853 -> 720x1280`; NIS after Composite consumes the physical PICA guide domain and resolves `960x853 -> 1440x1280`. The former display-derived output extent mixed those domains and violated the provider's maximum render-resolution contract. The corrected CACAO+NIS run has zero NRI errors, missing reads, fallbacks or failed/skipped/unresolved passes, and its captured scanout remains correctly oriented at `1280x720`. Existing machine/PICA-interface and pinned-CACAO format warnings are unchanged by this migration. Artifacts are under `I:/oot3dre_work/visual-parity/effect-transient-upscaler-20260825/`.
- Renderer commit `2b474442` moves `AmbientOcclusion` into the graph arena and adds a two-phase plan-change transaction: the backend waits for submitted GPU work, releases every pass or temporal client that can retain an arena descriptor, then replaces physical images. The transaction also accepts an empty plan so disabling all world effects releases old resources, while lower-screen composition cannot invalidate top-screen resources recorded in the same frame. `CacaoPass` now owns only its FidelityFX context and private scratch resources; its public R32F image and persistent physical state belong to the graph.
- Validation over direct CACAO, CACAO+TAA and CACAO+NIS executes CACAO from the borrowed NRI image in every captured frame and reports zero missing reads, binding fallbacks, skipped/failed/unresolved passes or Vulkan/NRI errors. Their captured arena resource/slot counts are `2/2`, `8/8` and `6/6` respectively, and all three framebuffer captures are complete at `1280x720`. The 21 pinned-CACAO format warnings exactly match the pre-migration combined-provider baseline and are not lifetime regressions. Artifacts are under `I:/oot3dre_work/visual-parity/effect-transient-cacao-20260825/`.
- Renderer commit `abde2daa` closes transient-image ownership by moving `HierarchicalDepth` into the graph arena. Multi-mip bindings expose one persistent state tracker per physical mip; aliases share those trackers through their physical slot, while single-mip consumers retain the existing whole-image tracker. `HiZDepthPyramidPass` borrows the exact R32F image and per-mip state, owns only its pipeline, descriptor pool and explicitly releasable mip views, and cannot represent a partially transitioned pyramid as one fictitious whole-image state.
- Direct HiZ and HiZ+SMAA runs each build the complete eleven-mip pyramid, execute reflection and produce complete `1280x720` framebuffer captures. The direct 20-frame allocation run allocates two physical slots once, reuses them thereafter and executes HiZ on all 18 active frames. Both profiles report zero missing reads/resources, binding fallbacks, skipped/failed/unresolved passes or Vulkan/NRI errors; the 11 Vulkan warnings remain the pre-existing machine/PICA-interface baseline. Artifacts are under `I:/oot3dre_work/visual-parity/effect-transient-hiz-20260825/`.
- Focused closure is now 35/35 effect-graph/lifetime/contract tests and 4/4 Vulkan diagnostics; the whole-AOT audit remains 12,419 compiled functions, three host boundaries and zero residual A32 entries. All nine real public transient image outputs are graph-owned. Temporal histories and provider scratch remain deliberately outside the transient arena because their lifetimes are cross-frame or provider-private rather than transient graph outputs.
- FSR provider execution is runtime-closed on the covered Vulkan machine. The permanent post-build validation is under `I:/oot3dre_work/visual-parity/effect-fsr-provider-20260825/`: `runtime-v2.json` completes 20/20 frames, dispatches FSR and HiZ in all 18 active world frames, resolves `234/234` reads and reports zero skipped, failed, unresolved, missing or fallback work and zero NRI/Vulkan errors. Renderer commit `82b3d027` keeps the HiZ requirement active for duplicate presentations and records the existing surface output as reused instead of incorrectly reporting a skip. The first active frame includes FidelityFX context creation and dominates CPU time; steady-state provider timing remains a separate performance gate rather than an availability fallback.
- The default PICA AOT corpus now merges the existing boot/title/Kokiri inventory with fresh Authentic, TAA, CACAO+TAA, SSSR+TAA, Toon, FSR and directional-shadow captures. It contains 224 deterministic modules (111 canonical fragments, 111 NRI fragments and two vertices), up from 95 modules and 1,917,012 packed bytes. The build-produced `oot3d_pica_default.o3ps`, rather than only the intermediate pack, passes the first six strict checkpoint runs with hit/miss counts `57/0`, `57/0`, `60/0`, `57/0`, `57/0` and `57/0`; the shadow profile adds `57/0`. This is 402 hits and zero runtime compilation fallbacks in aggregate. The generated inventories, manifests, strict host reports and captured logs are under `I:/oot3dre_work/visual-parity/shader-corpus-20260825/` and `I:/oot3dre_work/visual-parity/directional-shadow-artifact-20260825/`.
- Directional-shadow receiver filtering remains inside the typed `PicaLighting` contribution. The old nearest box count exposed only ten opacity levels at radius one and produced visible square/banded coverage. The replacement translates a bilinear footprint across the box-PCF kernel and collapses shared taps to `(2r+2)^2` depth comparisons: 16 fetches at radius one and 36 at radius two. It changes neither caster/receiver policy nor light selection and produces a continuous framebuffer transition in `I:/oot3dre_work/visual-parity/aa-color-contract-20260825/shadow-custom.bmp`.
- Steady-state figures use 600 fixed-delta 1/60 presentations, discard 120 warm-up presentations, measure the remaining 480, disable VSync and audio, and load the same real Kokiri checkpoint. The corrected results are Authentic `100.75 FPS`, FSR `87.93 FPS`, SSSR+TAA `92.24 FPS` and shadow-only `102.11 FPS`; the shadow result is within run variance of Authentic and therefore shows no measurable steady-state regression. The earlier free-delta figures counted duplicate presentations and are invalid. The earlier `shadow-config.json` result is also not a shadow-only comparison because that file enabled FXAA, AO, grass and a texture-pack path; `shadow-clean-config.json` is the accepted one-setting profile. Artifacts are under `I:/oot3dre_work/visual-parity/nri-steady-benchmark-20260825/`.
- The configuration audit also exposed that the former FXAA shader was a contrast-gated isotropic five-sample average. It has been replaced by edge-direction search and sampling along the resolved edge, with luma evaluated in an sRGB response domain while color remains in its declared source domain. A one-setting direct-framebuffer comparison is under `I:/oot3dre_work/visual-parity/aa-color-contract-20260825/`; shadow-only preserves the baseline tone, while the FXAA path no longer applies a generic cross blur. Validation profiles must use `Preset: Custom`, because `Preset: Authentic` intentionally normalizes every extension back off.
- Effect image bindings now carry the typed `SceneColorEncoding { Unknown, Linear, Srgb }` contract instead of an ambiguous linear-color boolean. Native PICA scene color is explicitly display-encoded: a linear-working pass decodes it once and scanout encodes it once, while SMAA preserves the encoded domain. The extensions-off framebuffer is byte-identical before and after the change (RMSE `0`); the TAA mean luminance moves from the broken double-encoded `0.673` to `0.442` against a `0.441` baseline, with expected temporal-filter RMSE `0.026-0.028`. A 26-frame active TAA capture records 26 source-sRGB working conversions, 52 encoded scanouts and zero missing reads, binding fallbacks, failed passes or Vulkan/NRI errors. Reproducible profiles, diagnostics and in-frame captures are under `I:/oot3dre_work/visual-parity/aa-color-contract-20260825/`.
- The encoding contract now remains typed through the resource table, scene-surface registry, Composite, TAA, SMAA and NIS/FSR/DLSS provider boundaries. Conversion to boolean flags occurs only at shader push constants or the external NRI provider ABI. Scanout no longer reconstructs a parallel source or encoding from feature booleans: the compiled graph binding is authoritative, and an absent or `Unknown` color binding is rejected. Fixed-delta 48-frame Authentic, FXAA, TAA, SMAA, FSR and NIS runs complete with strict shader-pack resolution `57/0`, zero missing reads, binding fallbacks, failed/unresolved passes or Vulkan/NRI errors. At frame 36, Authentic mean luminance is `0.360247`, FXAA `0.360104` and TAA `0.361213`, excluding the former whole-frame gamma shift. Artifacts are under `I:/oot3dre_work/visual-parity/typed-color-propagation-20260825/`; focused coverage passes `36/36` graph, `4/4` Vulkan-diagnostics and `21/21` pipeline-cache tests, while the full foundation target remains `223/226` on the same three unrelated pre-existing failures.
- Native GSP command-list ownership now crosses the frontend, visual replay, Vulkan bridge and `PicaSceneFrame` as the typed `Scene/Ui/Unknown` composition domain together with command-list identity. Replay schema `PVR4` preserves the domain while retaining `PVR1-PVR3` compatibility, and diagnostics expose exact per-domain draw counts. A 12-frame Kokiri checkpoint run classifies every active guest update as `89/90` scene draws on the top target plus `11` native UI draws on the lower target, with zero unknown draws and zero Vulkan/NRI errors. The visible TopScreen gameplay HUD is composed by the host after scanout; the four depth-disabled top-target draws are therefore scene overlays, not HUD. This evidence keeps AA and scene effects on the scene surface without inventing an asset-based UI classifier. Artifacts are under `I:/oot3dre_work/visual-parity/composition-domain-contract-20260825/`; the full foundation target now passes `226/226` after updating its stale native-light and graph-order fixtures.

### Scene Aspect And UI Presentation Contract (2026-08-25)

- The native scene baseline is the OOT3D top-screen aspect, `400x240` (`5:3`). `ScenePresentationPolicy` extends horizontal FOV for wider outputs and vertical FOV for narrower outputs; it never crops or stretches the scene. Thus `16:9` resolves to `16/15` horizontal expansion and `4:3` to `5/4` vertical expansion.
- Perspective scene projection alone consumes this policy. Native orthographic calls remain unchanged, and HUD/UI continues through its independent `400x240` logical canvas and uniform-fit viewport. Pointer/touch mapping uses the same fitted rectangle, so visual placement and interaction cannot diverge.
- The top PICA render target follows the full output extent at every valid aspect. This produces real `1280x720` and `1024x768` scene frames instead of widening only one case or letterboxing the other.
- Since 2026-10-20 the "output" of that contract is the presentation picture (`fast/oot3d/presentation_picture.h`): the user's aspect ratio (`Graphics.Output.Aspect`: Original 3DS 5:3, 16:9 default, 21:9, Free), fitted into the swapchain, centred, with black bars on the remaining sides; Free is the whole swapchain and keeps the previous behaviour. The Vulkan backend resolves the picture once per frame from the acquired swapchain (`mPresentationPicture`), sizes the native PICA targets to it (`NativePicaOutputExtent`), fits the scanout into it and leaves the bars to the scanout clear; the window host resolves the same picture from the window size for the scene FOV policy, the UI canvases (`N64UiOutputRegion`), the in-game menu and the pointer/touch mapping. The scene is never cropped or stretched: a wider picture widens the FOV, a narrower one heightens it, exactly as a window of the picture's shape did before.
- The existing persisted `Graphics.Camera.FovMultiplier` contract remains compatible and is now composed in the typed scene projection policy before aspect extension. It changes both scene axes around the original optical center and cannot affect orthographic UI geometry.
- Full foundation coverage passes `228/228`. Deterministic strict-NRI Kokiri runs report `57/0` PICA AOT hits/misses; `16:9` modifies 42 perspective calls with horizontal expansion `1.0666667`, while `4:3` modifies 42 with vertical expansion `1.25`; both leave 21 orthographic calls untouched. All 30 HUD primitive descriptors are identical across both aspects and FOV `1.0/1.25`. Reproducible configs, diagnostics and framebuffer captures are under `I:/oot3dre_work/visual-parity/scene-aspect-fov-contract-20260825/`.

### Canonical Shader Output Closure (2026-08-25)

- The native PICA fragment frontend owns exactly location-0 color and native
  depth and publishes that structural contract with its typed hook layout.
  Optional guide MRTs cannot enter the canonical cache: each unique source is
  independently audited and a contract disagreement is rejected.
- Texture routing and TEV color consumption are frontend metadata. Temporal
  vertex instrumentation likewise requires the direct typed frontend program;
  production no longer recovers either contract by parsing generated GLSL.
- Material, reactive, normal, ambient and motion outputs are declared only by
  the instrumented composer. The material guide has a neutral baseline and is
  then populated from an explicit texture profile or a genuinely consumed PICA
  secondary color; it is not assumed to exist in canonical shaders.
- On the Kokiri checkpoint, a 48-frame `Authentic` run audits 19 canonical
  fragments and records 101 canonical draws per active frame, attachment
  count/mask `1/0`, and zero compatibility analyses or contract rejects. TAA
  records 101 instrumented draws, attachment count/mask `5/0x06`, and zero
  rejects, missing resources, binding fallbacks or Vulkan/NRI errors.
- TAA dynamic and strict-AOT framebuffer captures are byte-identical. The
  refreshed default corpus now contains 274 current modules and resolves the
  covered TAA camera-motion run at `81/0` hits/misses. Full foundation coverage
  remains `228/228`; artifacts are under
  `I:/oot3dre_work/visual-parity/canonical-output-contract-20260825/` and
  `I:/oot3dre_work/visual-parity/aa-reactive-mask-20260825/`.

### Temporal AA Reactive Contract Closure (2026-08-25)

- Reactive coverage is derived from the complete native PICA output-merger
  contract: RGB equation and factors, color mask, fragment-operation mode,
  depth state and typed `Scene/Ui/Unknown` composition domain. A blend-enabled
  `ONE/ZERO` replacement is opaque and therefore no longer invalidates history;
  UI and non-color fragment operations cannot enter the scene reactive mask.
- Destination-dependent source-alpha and source-color blends write continuous
  coverage from the native fragment output. Other destination-dependent modes
  use full coverage. The R8 material-guide alpha attachment accumulates
  overlapping reactive layers with `MAX`, without changing canonical PICA
  color or depth.
- On the deterministic Kokiri camera-motion sequence, reactive draws fall from
  the incorrect `93.25` mean (`112` maximum) to `35.39` mean (`49` maximum).
  TAA history weights `0.0` and `0.9` now differ on all 14 captured motion
  frames after their identical initialization frame; before this correction
  every frame was byte-identical because nearly the whole scene was reactive.
- Authentic, FXAA and SMAA1x frame 70 remain byte-identical to their pre-change
  captures. The corrected default-TAA frame changes mean luminance by only
  `0.061%` with RMSE `0.02697`, excluding a renewed gamma conversion while
  retaining the expected temporal resolve.
- The 27 focused shader-pipeline tests and all `228/228` graphics-foundation
  tests pass. The build-produced 274-module default AOT pack resolves the
  covered TopScreen run at `81/0`; strict structural smoke passes `3/3` across
  Link's House, Kokiri Forest and Hyrule Field. Runtime evidence reports zero
  missing reads, failed passes, Vulkan errors or NRI validation errors. All
  configs, inventories, manifests, diagnostics and framebuffer sequences are
  under `I:/oot3dre_work/visual-parity/aa-reactive-mask-20260825/`.

### Strict Display-Effect Read Ownership (2026-08-25)

- Renderer commit `63786f53` makes a resolved `EffectResourceReadSet` the only
  input view available to CACAO, Motion, HiZ, WorkingColor, Reflections,
  Composite, TAA, FSR, DLSS, NIS, SMAA and Scanout. Undeclared table entries
  and parallel raw Vulkan-image fallbacks are no longer reachable by those
  consumers; an incomplete declared read set rejects the pass.
- Color-source resolution preserves the pass declaration order. This matters
  when a pass reads both its primary scene color and an auxiliary color such
  as `ReflectionColor`; enum order is not a semantic priority.
- Seven deterministic 48-frame profiles cover Authentic, TAA, SMAA, NIS, FSR,
  CACAO+TAA and SSSR+TAA. They resolve all `3,128/3,128` declared reads with
  zero missing resources, binding fallbacks, failed or unresolved passes, and
  zero Vulkan/NRI errors.
- All 28 corresponding pre/post framebuffer captures are byte-identical. The
  comparison caught and rejected an intermediate SSSR color-priority bug that
  aggregate counters alone could not expose. Focused coverage passes `38/38`
  graph/resource tests and `4/4` Vulkan-diagnostics tests; evidence is under
  `I:/oot3dre_work/visual-parity/strict-effect-bindings-prepost-20260825/`.

### Native Azahar Injection Coverage (2026-08-25)

- Renderer coverage is driven from a generated native scenario catalog rather
  than manual gameplay. The read-only decomp supplies scene, setup, cutscene
  and actor tables; `code.bin` supplies the native entrance table. The current
  catalog contains 1,692 scenarios: 1,582 valid entrance rows, representatives
  for all 111 known scenes and 400 scene/local-entrance pairs, plus 110
  decomp-proven cutscene setup selectors.
- A seed savestate establishes a valid running process only. The instrumented
  Azahar build then requests the catalog entrance through the original OOT3D
  transition state (`pending entrance`, trigger `0x14` and the native
  entrance-table transition effect). It waits until the requested entrance is
  causally observed and the expected scene is active before any evidence is
  captured. No room, actor, material or renderer state is fabricated by the
  injector.
- Transition submission remains anchored at `Player_Update` (`0x001E1B54`),
  while completion can also be observed at the producer-closed
  `GameState_Update` boundary (`0x00417014`). The latter accepts only the
  decomp-proven Play initialization/main callbacks (`0x00449440` and
  `0x004523AC`) before reading `PlayState.sceneId`; playerless cutscenes no
  longer time out merely because no Player actor is updating.
- `Invoke-Oot3dAzaharCoverageScenario.ps1` installs an explicit seed into an
  isolated slot for one process, restores the previous slot in `finally`, and
  requires complete PICA and optional direct-framebuffer evidence. Missing or
  truncated savestates are rejected before allocation; they cannot underflow
  into the former `vector too long` failure.
- Every draw publishes a native shader identity from Azahar's `ShaderSetup`
  program/swizzle hashes and `FSConfig::Hash`, combined with geometry-shader
  state, topology and draw mode into a pipeline identity. The summarizer also
  records enabled texture format/type pairs and native shadow-register states.
  This evidence validates the NRI frontend; it is never consumed as runtime
  scene data or used for fixture-specific corrections.
- Coverage uses two evidence levels. Smoke and renderer-difference runs retain
  full PICA traces plus a direct framebuffer. Broad matrices retain only the
  causal status, identity/count summary and first scenario for every identity;
  any novel or failing scenario can then be replayed with full forensic data.
  Matrix state is atomically checkpointed after every scenario and supports
  resume and failed-case retry.
- The generator keeps unresolved selection mechanisms explicit. At this point
  28 non-cutscene gameplay setups and two alternate resource setups lack a
  proven native selector and are not guessed. Four entrance rows targeting
  scene id 111 are outside the decomp-proven 0..110 scene table and are also
  excluded rather than coerced.
- The native resource table contains 102 indexed, launchable scenes and nine
  catalog-only tail records. Eight retain a scene name but no archive; the
  ninth (`hairal_niwa2`) names an archive but has no indexed ZSI, setup, room or
  command source in this build. A native transition probe confirms these stay
  in the transition state rather than reaching the requested scene, so the
  matrix records them as unavailable, not as renderer failures.
- The complete Vulkan representative gate classifies all 111 records: all
  **102/102 launchable scenes pass**, zero fail, and nine are explicitly
  unavailable. Across 4,773 draws it observes one vertex program, 290 native
  fragment configurations, 291 pipeline identities, eight enabled texture
  format/type combinations and 14 shadow states. A deterministic greedy cover
  reduces full forensic replay to 60 scenarios while preserving all 313
  pipeline/texture/shadow identity tokens; selection-file replay passes its
  focused 2/2 execution gate. The retained full-trace Vulkan replay passes all
  **60/60** selected scenarios and records 4,341 draws, 304 fragment
  configurations, 306 pipeline identities, ten enabled texture format/type
  combinations and 15 shadow states. Two targeted 12-frame replays recover the
  three timing-dependent source pipeline identities not repeated in the first
  three-frame forensic pass, so the detailed corpus contains every one of the
  original 313 cover tokens as evidence rather than only as a compact summary.
- `oot3d_native_pica_azahar_corpus_validator` streams those retained snapshots
  through the production `Oot3dPicaDrawPacket`, raster-state decoder, fragment
  feature analyzer and fragment shader generator. The complete gate validates
  all 4,341 draws and 306/306 Azahar pipeline identities with zero capture,
  register, raster, identity-mapping or shader-generation failures. Native
  direct fragment lighting closes the former `hylia_labo_info_entry_0043`
  (`pipeline_be4877a070bdca85`) gap and is carried through Vulkan uniform
  packing, visual interpolation and replay schema `PVR6`; its generated GLSL
  also passes the production Vulkan `shaderc` toolchain. Packed vertex and
  fragment uniform offsets now have one renderer-owned ABI in
  `fast/oot3d/pica_uniform_layout.h`; the same contract is consumed by packing,
  scene semantics and reflection/IBL, correcting the former fog/global-ambient
  reads at offsets `144/1184` to the native packed offsets `128/2080`.
  The corpus gate also deduplicates generated sources by structural state key,
  rejects key/source collisions and compiles every unique source through the
  production Vulkan 1.1 `shaderc` path: 209/209 shaders pass, covering all
  304 fragment configurations and 306 pipeline identities. Native
  fragment-lighting LUT state is now decoded directly from PICA registers
  `0x1C5/0x1C8..0x1CF`, retained as immutable copy-on-write snapshots and
  uploaded once per content identity as a `256x24 R32_UINT` image. Generated
  shaders implement signed and unsigned LUT interpolation, D0/D1, Fresnel,
  reflectance RGB, spotlight attenuation and per-light distance attenuation;
  Vulkan binding 13 and NRI split bindings 13/14 consume the same resource.
  `PVR6` stores unique LUT snapshots in a checked dictionary and references
  them by hash from draw plans, avoiding a 24 KiB copy per draw while retaining
  `PVR1`-`PVR5` read compatibility. The observed Azahar corpus contains no
  LUT-consuming pipeline, so the new paths are gated by an all-features
  synthetic shader compiled through production `shaderc`, frontend cursor and
  snapshot tests, replay round-trip/deduplication tests, and a real 1,009-draw
  NRI/Vulkan runtime smoke. Native bump state is now decoded from lighting
  config 0 and supports both normal-map and tangent-map modes, the native
  texture-unit selector, texture-2/coordinate-1 routing and the optional Z
  reconstruction bit. The generated material normal is exposed through the
  typed frontend contract; auxiliary consumers request a normal guide without
  modifying the canonical one-color-attachment shader. The actor asset audit
  identifies the three active normal-map materials in Freezard, the Stone of
  Agony and Morpha, while the Freezard fixture now gates the native CMB source
  fields directly. Production `shaderc` gates pass for the generated normal-
  and tangent-map variants; the renderer pipeline gate passes 29/29 tests and
  the graphics foundation passes 230/230. Dynamic Authentic and CACAO Kokiri
  runs retain 1,817 draws over 36 presentation frames and produce visible
  framebuffer sequences. Kokiri is a regression scene rather than a bump
  activation scene; the asset and shader gates retain that distinction.
  Evidence is under `I:/oot3dre_work/visual-parity/pica-bump-20260826`.
- Native Shadow2D is now a typed frontend/backend contract rather than a type-5
  fallback. Texture type 2 decodes as tiled R32UI depth24/intensity8 data,
  samples with the native four-tap compare and perspective/orthographic bias,
  and applies primary, secondary, inverted and alpha shadow factors at the
  PICA lighting terms selected by registers `0x1C3/0x1C4`. Fragment operation
  mode 3 writes the native R32UI target through the constant/linear f16 bias
  rule and atomic compare-swap. The stable fragment UBO is 2,112 bytes;
  visual replay schema `PVR7` stores the four added uniforms while retaining
  `PVR1-PVR6` read compatibility. Vulkan and NRI share storage binding 5,
  integer texture sampling, producer/consumer barriers and physical-address
  framebuffer aliasing. Synthetic consumer and producer GLSL both pass the
  production `shaderc` path, and the full graphics foundation passes 230/230.
  Fresh boot/title, Kokiri and seven renderer-profile captures replace stale
  shader generations with the then-current deduplicated 184-module schema-3
  default pack.
  Strict NRI resolves 66/66 modules on the 30-frame Kokiri gate and is
  byte-identical to dynamic generation; all ten 901-frame boot/title
  checkpoints also match dynamic generation. Evidence is under
  `I:/oot3dre_work/visual-parity/pica-shadow2d-native-20260826/`.
- The Azahar corpus validator accepts legacy capture summaries only when their
  retained PICA frame list is still present and readable; an explicit false
  retention marker remains a hard rejection. The surviving five-scenario
  sample validates 294/294 draws, 58/58 mapped pipelines and 43/43 unique
  Vulkan fragment shaders with zero structural failures. Typed feature
  accounting shows 231 fog draws across 32 pipelines and no observed fragment
  lighting, procedural-texture or native-shadow activation, so this sample is
  a broad regression gate but is not claimed as Shadow2D activation evidence.
  The report is `native_pica_frontend_validation.json` beside the retained
  forensic matrix.

### Native Fragment-Lighting Scene Contract (2026-08-26)

- Fragment feature schema 2 now carries a versioned
  `PicaFragmentLightingLayout` decoded directly from the PICA lighting
  registers. It preserves all eight native light indices, evaluation
  permutation, per-light directional/two-sided/geometric/shadow/spot/distance
  flags, seven LUT routes and scales, environment and Fresnel selection, bump
  routing and native shadow-factor structure. Legacy schema-1 draws remain
  readable and are never assigned invented layout values.
- Dynamic specular, diffuse, ambient, position-or-direction, spot direction,
  distance attenuation and global ambient values are decoded from the shared
  2,112-byte fragment UBO. `PicaSceneFrame` publishes both structural and
  dynamic state through `NativeSceneView` schema 7 while retaining the older
  CMB vertex-light summary as a separate compatibility contract. Diagnostics
  distinguish layout availability from fragment-lighting-enabled draws.
- Visual replay schema `PVR8` serializes the typed layout and retains read
  compatibility with `PVR1-PVR7`. Semantic traces expose the complete layout,
  so activation and feature routing can be established without inspecting
  generated GLSL or matching scene-specific shader hashes.
- The full graphics foundation passes 230/230, scene-frame coverage passes
  11/11 and Vulkan diagnostics passes 4/4. Frontend descriptor, bridge,
  semantic-trace and PVR8 round-trip suites also pass, and the complete product
  rebuild retains 12,419 whole-AOT functions, three host boundaries and zero
  residual A32 entries.
- A 36-frame Kokiri dynamic/strict pair publishes 3,729 typed layouts and zero
  enabled fragment-lighting draws, with zero rejected draws and an identical
  framebuffer. This is retained as negative activation evidence rather than
  being promoted to a lighting proof.
- The decomp-indexed `hylia_labo_info_entry_0043` entrance reaches the original
  Lakeside Laboratory transition path and completes a 287-frame capture with
  11,044 submitted draws and zero A32 fallback. Diagnostics observe 171 active
  fragment-lighting draw records; the semantic trace contains 87 native draw
  submissions with two lights evaluated in permutation `0,1`. Dynamic and
  strict NRI produce the same useful-frame SHA-256
  `f5a2a329a0dfc5b8bb119d219720d1c76a7ff0d108b1b4eb34333c338b4a4fe5`.
- The default schema-3 AOT pack now includes the ten previously absent Hylia
  modules and contains 194 modules. Hylia resolves 90/90 modules with zero
  strict misses; the complete 901-frame boot/title gate resolves 96/96 and
  remains identical to the prior dynamic run at all 10 checkpoints. Evidence
  is under
  `I:/oot3dre_work/visual-parity/pica-fragment-lighting-scene-20260826/`.
- This closes the typed scene-access contract and direct fragment-lighting
  activation gate. It does not claim runtime activation of LUT consumption,
  bump mapping or Shadow2D shadow factors when the captured draw does not
  enable those native features.

### Offline PICA Pipeline Manifest And Prewarm (2026-08-26)

- The renderer can now capture every immutable graphics-pipeline descriptor
  into `oot3d_pica_pipeline_manifest_v2`. Each entry owns shader source
  identities, canonical/instrumented domain, requested/applied typed
  instrumentation features, attachment/sample profile, topology, vertex
  layout, raster, blend, depth/stencil and typed auxiliary outputs.
  Observation metadata is deliberately excluded from structural identity, so
  inventories from independent scenes merge deterministically.
- `oot3d_native_pica_pipeline_manifest` merges repeated `--inventory` inputs,
  validates descriptor schemas and rejects structural hash collisions. The
  launcher exposes `-PicaPipelineInventory`, `-PicaPipelineManifest` and
  `-PicaPipelinePrewarm`; prewarm additionally requires the matching `.o3ps`
  shader pack and never submits a synthetic draw.
- Vulkan/NRI prewarm creates only entries matching the active attachment mask,
  MSAA sample count, Native Fidelity domain and typed instrumentation feature
  mask. This prevents an attachment-compatible Toon, temporal or shadow
  variant from leaking into another F1 profile. Shader-key/source/output
  mismatches, absent strict AOT modules and incompatible NRI contracts are
  skipped without weakening the normal lazy path. Pipeline destruction or a
  profile-feature change invalidates the corresponding prewarm key.
- Pipeline output metadata now comes from the typed instrumentation result.
  Pipeline construction no longer searches generated shader source for scene
  overlay marker strings.
- A controlled 60-frame Authentic Kokiri capture produced 39 canonical
  pipeline entries. The merged manifest remained at 39 after duplicate input;
  prewarm created 39 pipelines with zero reused and zero skipped entries. The
  lazy, original-prewarm and diagnostics-prewarm framebuffers share SHA-256
  `bae563168bce554f84401ba2012cabe4a3531de5f22efb6a1e4bcccd58434c53`.
  All runs submitted 3,307 draws. Evidence is under
  `I:/oot3dre_work/visual-parity/pica-pipeline-prewarm-20260826/`.
- The qualified schema-2 corpus merges Kokiri, complete boot/title and seven
  renderer profiles into 232 unique descriptors across 66,918 observations:
  87 canonical and 145 instrumented, all with an available NRI fragment
  module. Paired lazy/prewarm Kokiri runs select 39 pipelines for Authentic,
  36 each for TAA, CACAO+TAA and SSSR+TAA, 54 for directional shadows, and
  109 logical Toon descriptors. Toon creates 91 Vulkan pipelines and reuses
  18 equivalent objects. Every paired framebuffer is byte-identical at the
  SHA-256 above, with zero prewarm skips. Evidence is under
  `I:/oot3dre_work/visual-parity/pica-pipeline-corpus-20260826/schema2/`.
- Diagnostics publish manifest, cache, enabled, created, reused and skipped
  counts per frame. Focused gates pass 5/5 manifest, 31/31 shader pipeline,
  4/4 Vulkan diagnostics, 11/11 scene-frame and 230/230 graphics-foundation
  tests, plus all eight native PICA descriptor/pack/submission/plan/trace/
  capture/bridge/savestate harnesses.
- Prewarm remains optional. Boot/title and the principal instrumented renderer
  profiles are now represented in the retained corpus. Hylia is deliberately
  excluded from the qualified merge because the current executable reproduces
  a whole-AOT sentinel fault on that injected entrance both with and without
  pipeline inventory; the earlier fragment-lighting capture remains valid but
  does not qualify a pipeline manifest for packaging. Startup cost and corpus
  coverage still need measurement before a packaged manifest can become the
  default.
- The startup decision gate rejects the 232-entry union as a default runtime
  manifest. In representative 60-frame lazy runs, Authentic, Toon and
  directional-shadow workloads accumulated 13.848, 17.968 and 16.193 ms of
  pipeline work, with largest single-frame contributions of 11.094, 12.355
  and 13.345 ms. The union prewarms 39/109/54 logical descriptors while those
  workloads actually retain 39/33/33 pipelines. Paired host windows add about
  0.16/0.83/0.89 seconds respectively on this machine. These single-run
  timings are diagnostic rather than a general performance claim, but the
  structural over-selection is deterministic.
- Therefore the union remains a coverage artifact and explicit diagnostic
  option. A product prewarm input must be workload-scoped by package or
  scenario metadata outside the renderer; native rendering code must not gain
  scene-name conditionals. The persistent Vulkan driver cache and strict AOT
  shader pack remain the default product path until scoped manifests prove a
  net startup/stutter benefit.

### Resolved PICA Material Scene Contract (2026-08-26)

- `PicaSceneFrame` schema 7 publishes
  `PicaSceneResolvedMaterialState` schema 1 for every accepted draw. The
  versioned record preserves canonical topology, culling/front-face, fragment
  operation and color mask, logic operation, complete blend state and constant
  color, alpha test, depth test/write/compare, stencil state and the typed
  fragment-feature layout. Texture/sampler bindings, uniform slices and render
  targets remain separate versioned references rather than being copied into
  the material.
- The Vulkan bridge copies these values directly from the already decoded
  `PicaDrawView`; `PicaSceneFrame` performs no second register decode and rejects
  a record without the material schema. `NativeSceneView` schema 7 exposes a
  borrowed, read-only resolved-state reference, so extensions can inspect the
  exact native material without owning geometry, texture or uniform payloads.
  Directional-shadow pipeline selection now consumes the same material record
  instead of parallel topology/raster fields.
- A deterministic 60-frame Authentic Kokiri run records 3,201 scene-frame
  draws and 3,201 resolved material states, with zero mismatch frames, rejected
  draws, canonical/instrumentation contract rejects or Vulkan/NRI warnings and
  errors. Its direct framebuffer SHA-256 is
  `bae563168bce554f84401ba2012cabe4a3531de5f22efb6a1e4bcccd58434c53`,
  byte-identical to the qualified extensions-off baseline. Runtime report,
  diagnostics and framebuffer are retained under
  `I:/oot3dre_work/visual-parity/resolved-pica-material-contract-20260826/`.
- Focused scene-frame, Vulkan-diagnostics, shader-pipeline and graphics-
  foundation gates pass 11/11, 4/4, 31/31 and 230/230 respectively, and all
  eight native PICA descriptor/pack/submission/plan/trace/capture/bridge/
  savestate harnesses pass. The complete product still builds with 12,419
  whole-AOT functions, three host boundaries and zero residual A32 entries.
- This closes resolved material publication, not composition classification.
  Raster and target publication is closed by the following section; the still-
  undemonstrated world/atmosphere/transparency boundaries must come from native
  evidence rather than being inferred from material values or scene identities.

### Resolved PICA Raster And Render-Target Scene Contract (2026-08-26)

- `PicaSceneFrame` and `NativeSceneView` schema 7 publish one versioned raster
  and render-target state for every accepted draw. Raster state retains both the
  native PICA viewport/scissor coordinates and the exact scaled Vulkan values,
  together with depth range, near plane, W-buffering and framebuffer orientation.
  Render-target state retains namespace, native color/depth addresses, dimensions,
  formats and sample count.
- Color and depth are exposed as compact, read-only GPU image references with
  backend handle, dimensions, format, sampleability and stable resource generation.
  The Vulkan target stores the generations returned when its surfaces are
  published, so scene-frame recording does not repeat registry lookups and never
  copies image contents. `NativeSceneView` borrows these records directly.
- The bridge copies the already resolved native and Vulkan values at draw
  submission; `PicaSceneFrame` performs no register or coordinate re-decode and
  rejects missing raster or target schemas. Directional-shadow caster selection
  consumes the same target record. Diagnostics now accept the typed scene-frame
  statistics object instead of maintaining a parallel positional argument list.
- A deterministic 60-presentation-frame, native-30 Kokiri run records 3,201
  scene-frame draws and exactly 3,201 raster states, render-target states, GPU
  target references and resolved materials. It executes 29 directional-shadow
  passes with 606 queued casters and reports zero rejected draws, mismatches or
  Vulkan/NRI warnings and errors. Its direct framebuffer remains byte-identical
  to the qualified extensions-off baseline at SHA-256
  `bae563168bce554f84401ba2012cabe4a3531de5f22efb6a1e4bcccd58434c53`.
  Evidence is retained under
  `I:/oot3dre_work/visual-parity/resolved-pica-raster-target-contract-20260826/`.
- Focused scene-frame, Vulkan-diagnostics, shader-pipeline and graphics-foundation
  gates pass 11/11, 4/4, 31/31 and 230/230 respectively. All eight native PICA
  descriptor/pack/submission/plan/trace/capture/bridge/savestate harnesses pass,
  and the complete product remains at 12,419 whole-AOT functions, three host
  boundaries and zero residual A32 entries.
- Strict AOT execution of this instrumented shadow profile is now closed. Four
  modules observed in the real runtime inventory were added to the default pack,
  which now contains 198 modules and resolves the covered run with `69/0`
  hits/misses. Dynamic and strict-AOT captures are byte-identical at SHA-256
  `a83b5d41bbdabb1cb19955ac03edc881a6342f9b57197d706015fb6c054e1008`.
  Composition classification beyond the evidenced `Scene` and `Ui` domains
  remains open and must not be inferred from blend state or scene identity.

### Native Composition Layers And Provenance (2026-08-26)

- The native command-list path now publishes the typed layers
  `OpaqueWorld`, `TransparentWorld`, `Atmosphere`, `Ui` and `Unknown`, together
  with provenance, native source PC and native selector value. Classification
  follows byte-exact native writer ranges; it never inspects textures, blend
  state, shaders, assets or scene names.
- `CmbRenderer_SubmitDrawHandle@0x0030F4D0` is balanced against all ten proven
  return PCs. Its native pass byte maps pass `0` to opaque world and pass `1`
  to transparent world. Mesh packet spans come from the packet byte count and
  command-list cursor. Native UI lifecycle ownership maps independently to the
  UI layer.
- The formerly unknown steady Kokiri draws are emitted by the native kankyo
  render-context chain `0x003FBBA8 -> 0x003FB5EC -> 0x00313444`. The only xref
  to `0x003FB5EC` is the proven kankyo/effect submit. The primitive builder has
  exactly three native continuations; its context cursor supplies the exact
  begin/end range. Runtime observation confirms every packet is `36` words
  (`144` bytes), matching the existing decompilation evidence without storing
  that size as a runtime correction.
- Attribution crosses the canonical frontend, Vulkan plan and bridge,
  `PicaSceneFrame` schema 9 and `NativeSceneView` schema 9. Visual replay schema
  `PVR9` serializes it and retains read compatibility with `PVR1-PVR8`;
  diagnostics and semantic traces report both layer and provenance.
- A deterministic 12-presentation Kokiri run records, in each of the five
  fully observed guest frames, `59` opaque, `15` transparent, `16` atmosphere
  and `11` UI draws with zero unknown draws. The first restored frame retains
  `89` unknown scene draws because the legacy checkpoint already contains its
  replay command list and therefore predates native writer observation; it is
  not relabelled heuristically. CMB entries/exits are balanced at `156/156/312`,
  all `162` primitive packets enter and exit, and every range/return/read error
  count is zero.
- The direct framebuffer remains byte-identical to the qualified Authentic
  baseline at SHA-256
  `bae563168bce554f84401ba2012cabe4a3531de5f22efb6a1e4bcccd58434c53`.
  Evidence is retained under
  `I:/oot3dre_work/visual-parity/composition-layer-contract-20260826/`, with
  `targeted-primitive-runtime-v1` as the qualified trace.
- An exhaustive low-level-writer diagnostic prototype resolved the same owner
  chain but reduced the 600-presentation benchmark from the historical
  `100.75 FPS` to `66.82 FPS`; it was removed from the production path. The
  final targeted contract measures `109.37 FPS` and `96.81 FPS` in two
  repeated 480-frame windows after 120 warm-up frames, with VSync and audio
  disabled. Their `103.09 FPS` mean straddles the historical baseline and
  proves no structural regression; the run-to-run spread is not claimed as a
  renderer speedup or slowdown.
- Coverage passes `231/231` graphics-foundation, `12/12` scene-frame/view and
  `4/4` Vulkan-diagnostics tests, plus frontend, descriptor, submission,
  composition, Vulkan-plan, semantic-trace, Azahar-capture, Vulkan-bridge and
  PVR9 savestate harnesses. The complete product remains at `12,419` whole-AOT
  functions, three host boundaries and zero residual A32 entries.
- This closes the layer/provenance transport and the covered Kokiri owner
  surface. Broader scenarios may expose additional native owners and must keep
  `Unknown` until proved. The ordered scheduling contract that consumes this
  transport is qualified in the next section; effect migration beyond the first
  geometry provider remains active.

### Native Composition Schedule Consumption (2026-08-26)

- The presentation scheduler now publishes the complete selected draw sequence
  before any native fill, draw or transfer. It reuses one metadata-only scratch
  vector and assigns a monotonic nonzero sequence identity; it does not copy
  geometry, textures, command lists or framebuffer contents.
- The Vulkan backend compiles that sequence into exact contiguous runs, render
  targets and typed anchors for `BeforeOpaque`, `AfterOpaque`,
  `BeforeTransparent`, `AfterTransparent`, `Atmosphere`, `SceneResolved`,
  `BeforeUi` and `AfterUi`. Native draw order is never changed. Every submitted
  draw must consume the next exact sequence entry, including target, domain,
  layer and provenance, or the submission fails rather than silently drifting.
- World anchors are deliberately withheld for an unknown layer, a scene target
  without an opaque world pass, a noncontiguous opaque pass or a noncanonical
  post-opaque tail. A UI-only target is represented separately as a non-scene
  target and is not reported as a missing world pass. Proven boundaries at the
  end of a target remain explicit instead of fabricating a following draw.
- `InteractiveGrass` is the first production consumer. Source inspection now
  accepts only `Scene + OpaqueWorld` draws, and execution occurs only at the
  schedule's exact `BeforeTransparent` anchor, including its explicit target-end
  form. The old render-state insertion heuristic and its tests have been
  removed, and the native-composer fallback invocation is no longer used.
- A 12-presentation Authentic Kokiri run publishes and consumes all `504/504`
  selected draws across five sequences with zero execution mismatches or
  publication failures. Four fully observed world targets expose their anchors;
  five UI-only targets are non-scene, and only the restored legacy first frame
  retains one unknown target. Missing-opaque, noncontiguous and noncanonical
  scene-target counts are all zero. The direct framebuffer remains byte-identical
  to the qualified baseline at SHA-256
  `bae563168bce554f84401ba2012cabe4a3531de5f22efb6a1e4bcccd58434c53`.
- A 30-presentation Grass-only run publishes and consumes `2,625/2,625` draws
  across 26 sequences. It invokes the declared boundary 25 times, executes the
  provider 24 times after one expected asynchronous placement-build skip, and
  reports zero fallback invocations, schedule rejects, failures or execution
  mismatches. Its 25 qualified world targets and 26 UI-only targets contain no
  false missing-opaque classification.
- Focused coverage passes 41/41 effect-graph/schedule, 4/4 Vulkan diagnostics,
  the native Vulkan bridge, 229/229 graphics-foundation, 12/12 scene-frame/view
  and 33/33 shader-pipeline tests. Two steady Authentic windows use 600 fixed
  1/60 presentations, discard 120 warm-up frames and disable VSync, pacing and
  audio; they measure `106.19` and `94.84 FPS`, with a `100.51 FPS` mean against
  the historical `100.75 FPS` baseline. This proves no measurable structural
  regression and is not claimed as a speedup.
- Evidence is retained under
  `I:/oot3dre_work/visual-parity/composition-schedule-20260826/`. Remaining work
  is to migrate the other optional effects to these typed boundaries and to
  extend proven native-owner coverage through the structural scenario corpus;
  neither is inferred from the qualified Kokiri sample.

### Typed Display Composition Contract (2026-08-26)

- Source-to-display transfers now resolve one typed snapshot from the consumed
  native composition schedule. A target is `Scene` only after its exact
  `SceneResolved` anchor has been reached, a proven UI-only target is `Ui`, and
  every incomplete, stale or ambiguous transfer remains `Unknown`. The display
  snapshot retains the composition sequence identity and this resolution state;
  it is not reclassified from dimensions, blend state or framebuffer address.
- Restored legacy display images deliberately start as `Unknown` because their
  old schema did not preserve composition ownership. The first observed native
  transfer replaces that state. Savestate compatibility therefore does not
  manufacture evidence for a pre-existing image.
- Display depth association and display-effect world eligibility now require
  `Scene + SceneResolved`. Grass source inspection separately requires
  `Scene + OpaqueWorld` and still executes only at the exact
  `BeforeTransparent` schedule anchor. Reflection and temporal material paths
  retain transparent scene draws where their own depth/material contracts allow
  them, while directional-shadow casters require `OpaqueWorld`.
- Scene-domain guide generation, Toon, reflection-material classification and
  direct shader instrumentation consume the same typed composition domain.
  CACAO and NIS/FSR/DLSS dispatch are authorized by the typed display-effect
  plan instead of output extent. All scene-only instrumentation requests are
  gated before variant construction, with a second domain check retained inside
  the individual classifiers.
- The former `400x240` top-screen predicate, equivalent direct `240/320/400`
  thresholds, their production includes and obsolete tests have been removed.
  Crossed tests prove that a `320x240` `Scene` draw is accepted while a
  `400x240` `Ui` draw is rejected, preventing the old heuristic from returning
  through a new extension consumer. The remaining `320x240` check is solely the
  user-selectable output-window minimum and carries no composition semantics.
- The final 12-presentation Authentic run publishes and consumes `504/504`
  draws, resolves four scene snapshots, four `SceneResolved` snapshots, five UI
  snapshots and one expected restored-legacy unknown snapshot. It reports zero
  schedule mismatches, graph read failures or Vulkan/NRI errors. The direct
  framebuffer remains byte-identical to the qualified baseline at SHA-256
  `bae563168bce554f84401ba2012cabe4a3531de5f22efb6a1e4bcccd58434c53`.
- The final 30-presentation Grass run publishes and consumes `2,625/2,625`
  draws, resolves `25` scene and `26` UI snapshots plus the one legacy unknown,
  invokes `25` declared geometry boundaries and executes `24` after one expected
  asynchronous build skip. It records zero fallback, failure, schedule reject,
  mismatch or missing graph read.
- The final 30-presentation TAA run publishes and consumes `1,413/1,413` draws,
  resolves `13` scene and `14` UI snapshots plus the legacy unknown, executes
  `26` temporal-AA and `26` motion passes, and balances `56` display-graph
  invocations as `134` executed and `104` reused passes with zero missing or
  unresolved reads. All three direct captures retain the canonical framebuffer
  hash above.
- The extended runtime matrix also executes `26` CACAO passes, `26` SSSR passes
  with zero fallback, `26` NRI FSR passes, `26` NRI NIS passes, and `13`
  directional-shadow passes with `247` opaque-world casters. The Toon profile
  records `1,481` eligible draws. TAA, CACAO, SSSR and FSR each request and apply
  instrumentation to exactly `1,259` scene draws while leaving all `154` UI
  draws canonical; Toon likewise leaves all `286` UI draws canonical in its
  30-frame profile. Every profile reports zero composition mismatch, failed or
  unresolved graph pass, missing read, and Vulkan/NRI validation error.
- Strict AOT repetitions resolve the qualified CACAO+TAA profile at `69/0`
  hits/misses and Toon at `60/0`; the distinct direct-CACAO and Toon+TAA
  diagnostic profiles remain valid dynamic-corpus probes rather than being
  misreported as default-pack coverage.
- Qualification passes `41/41` effect-graph/schedule, `4/4` Vulkan diagnostics,
  `34/34` shader-pipeline, `228/228` graphics-foundation and `12/12` scene-frame
  tests, plus the native Vulkan bridge. The complete product audit remains at
  `12,419` compiled functions, three host boundaries and zero residual A32
  entries.
- The accepted steady Authentic pair uses 600 fixed 1/60 presentations, discards
  120 warm-up presentations and disables VSync, pacing and audio. The two warm
  windows measure `102.32` and `103.53 FPS`, mean `102.93 FPS`, against the
  historical `100.75 FPS` baseline. This proves no measurable structural
  regression and is not a speedup claim. Evidence is retained under
  `I:/oot3dre_work/visual-parity/display-composition-contract-20260826/` in
  the `*-qualified` directories, including the two `*-strict-qualified`
  profiles and `benchmark-owner-qualified-1/2`.
- The target-to-display ownership path is now closed for the qualified corpus.
  Remaining extension work is to move shadow, composite and later geometry
  consumers onto the proven schedule anchors and typed snapshot resources, then
  grow native-owner coverage through broader structural scenarios without
  converting `Unknown` into a heuristic fallback.

### Native Frame Composition And View Families (2026-08-26)

- Gameplay, physics, input, audio and the authoritative animation clock remain
  native at 30 Hz. `Interpolated2x` presents at 60 Hz with one synthetic sample
  per source transition; `Interpolated3x` presents at 90 Hz with two. The fixed
  phases are derived mechanically from the selected multiplier, never from
  scene, actor or animation identities. The persisted legacy `Fixed60` value is
  read as `Interpolated2x`.
- `NativeFrameTemporalSample` schema 1 is the single renderer-owned description
  of presentation time. It publishes source-frame identities, continuity epoch,
  interpolation mode, multiplier, ordinal, alpha and actual sample duration to
  `PicaSceneFrame` schema 8 and `NativeSceneView` schema 8. Cuts, savestate loads,
  mode changes and discontinuities advance the epoch and reset temporal history;
  TAA and temporal upscalers consume this same contract.
- Draw matching and transition analysis execute once per pair of authoritative
  source frames. The prepared transition is then sampled at `1/2` for x2 or at
  `1/3` and `2/3` for x3, avoiding repeated analysis while retaining typed PICA
  replay. No final-image blending is used.
- `NativeViewFamily` schema 1 separates camera state from presentation time. A
  family contains one stable mono view or a stable left/right pair, and each view
  versions pose and projection independently so later positional tracking, FOV
  control and asymmetric stereo projection do not alter the native gameplay
  clock. Every view in a family consumes the one temporal sample owned by the
  scene frame. Current production remains mono; headset tracking, stereo
  rendering, distortion, compositor submission and VR input are explicitly out
  of scope for this tranche.
- Controlled runs from the same Kokiri checkpoint produced `120` x2
  presentations from `60` simulation ticks and `180` x3 presentations from the
  same `60` simulation ticks, with exact x2 ordinals `0,1`, exact x3 ordinals
  `0,1,2`, zero dropped guest refreshes and zero Vulkan/NRI validation errors.
  Final structural evidence is retained as `runtime_x2_final.json` and
  `runtime_x3_final.json` under
  `I:/oot3dre_work/native_game/frame_composer_validation/`.
- A direct framebuffer gate now joins every captured presentation to the NRI
  temporal diagnostics. From one deterministic moving sequence, all `11/11`
  authoritative samples in both x2 and x3 match the native-30 capture bit for
  bit. The ten x2 synthetic samples and twenty x3 synthetic samples are all
  distinct and have zero hashes in common with the native capture. All nine
  complete x2 transitions are balanced between their endpoints; all eighteen
  complete x3 samples progress in the expected `1/3`, `2/3` order. Their
  normalized framebuffer RMSE from adjacent endpoints is `0.02..0.11`, proving
  that the renderer emits new images rather than repeated native frames.
  `Test-Oot3dNativeFrameCompositionPixels.ps1` makes this gate repeatable; the
  retained report is
  `frame_composition_pixel_validation.json` under
  `I:/oot3dre_work/native_game/frame_composer_validation/pixel_probe/`.
- Canonical shader sources now publish one immutable dual-hash-and-size identity
  when the frontend creates them. `PicaShaderPipelineCache` audits a published
  identity on its first canonical insertion and uses that identity for later
  hits; it no longer hashes both complete GLSL strings for every replayed draw.
  Instrumented variants compute a new identity after their typed mutation, and
  legacy callers without a published identity retain the exact-source path.
  Over the same 180-frame x3 profile, shader-variant CPU time fell from
  `719.890 ms` to `5.053 ms`, while total native-PICA CPU time fell from
  `1215.730 ms` to `442.896 ms` across `17,674` draws.
- With VSync disabled, audio disabled, strict AOT enabled, fixed deterministic
  deltas and loading excluded, x2 now sustains `92.951` real FPS over 600
  presentations and x3 sustains `128.031` real FPS over 900 presentations.
  The former baselines were `67.063` and `79.012` FPS. Both before/after pairs
  execute exactly `60,296` x2 or `90,394` x3 replay draws and 300 simulation
  ticks, with identical memory-content and process-state fingerprints and zero
  dropped guest refreshes. Evidence is retained as
  `benchmark_x2_source_identity.json`, `benchmark_x3_source_identity.json` and
  `vulkan_x3_source_identity.json` in the validation directory above.
  The mono x2 and x3 presentation targets are therefore qualified. Future VR
  activation still requires a separate two-view plus compositor budget and is
  not inferred from this mono result.
- Focused coverage passes 231/231 graphics-foundation, 33/33 shader-pipeline,
  12/12 scene-frame/view, 4/4 Vulkan-diagnostics and the native runtime,
  frame-rate, visual-transition, frontend, descriptor, shader-pack, submission,
  trace, capture, bridge, plan and savestate suites. The complete product remains
  at 12,419 whole-AOT functions, three host boundaries and zero residual A32
  entries.

### Portable Extension Scheduling And Shadow Anchor (2026-08-26)

- `fast/renderer/extension_contract` and `extension_schedule` are the first
  title-neutral renderer modules. They define generic stages, stable pass IDs,
  surface identities, exact boundary kinds and authorization decisions; they
  contain no OOT3D, PICA, 3DS, framebuffer-size or physical-address policy.
- `fast/oot3d/pica_extension_schedule` is the title adapter. It maps an exact
  compiled PICA composition anchor and target signature to the generic contract,
  rejecting copied, stale or fabricated anchors. The NRI directional-shadow
  provider now requires that generic authorization and no longer runs from the
  display-copy path or infers a boundary from backend state.
- The generic scheduler has its own title-independent 2/2 test executable. The
  OOT3D adapter/effect graph passes 42/42 tests, Vulkan diagnostics 4/4,
  scene-frame/view 12/12 and shader-pipeline 34/34.
- A deterministic 30-presentation Kokiri shadow run reaches 13 exact
  `AfterOpaque` boundaries, authorizes and executes all 13, and records 247
  casters with zero rejected boundaries, duplicates, composition mismatches or
  Vulkan/NRI errors. The corresponding Authentic run records zero shadow
  boundaries and zero shadow passes. Direct framebuffer capture succeeds in
  both profiles.
- The shadow capture remains byte-identical to the previously qualified shadow
  capture at SHA-256
  `bae563168bce554f84401ba2012cabe4a3531de5f22efb6a1e4bcccd58434c53`.
  This proves scheduling migration parity, not additional visual shadow parity;
  the selected final frame also matches Authentic and therefore is not used to
  claim that the pre-existing shadow implementation is visually complete.
- Evidence is under
  `I:/oot3dre_work/visual-parity/portable-extension-schedule-20260826/`.
  The product audit remains at 12,419 compiled functions, three host boundaries
  and zero residual A32 entries.
- Cross-title extraction is not complete. `EffectResource`, the physical graph
  plan, `PicaSceneFrame` and `NativeSceneView` still carry OOT3D-specific schemas.
  The next reusable tranche is to separate generic resource handles, lifetimes
  and view publication from title-owned resource enums and semantic payloads,
  then migrate another production pass through that boundary.

### Portable Resource Dependency Graph (2026-08-26)

- `fast/renderer/extension_resource_graph` now owns the title-neutral resource
  identity, access, origin and initial-barrier contracts. Its compiler validates
  catalogs and ordered pass dependencies, then derives resource lifetimes,
  producer/consumer hazards, initial external barriers, ordered writers and
  exported lifetimes without referring to PICA or a game asset.
- The OOT3D graph is now an adapter over that compiler. It maps the existing
  `EffectResource` enum into the versioned `OOT3DRES` namespace, classifies
  external and semantic resources, and retains PICA guide-to-attachment policy.
  Existing effect, physical-plan and backend APIs remain unchanged, so the
  extraction does not require title behavior to move into the generic core.
- The generic resource compiler passes an independent 4/4 test suite covering
  external barriers, produced resources, transitive dependencies, unordered
  writers, invalid catalogs and exports. The existing OOT3D effect graph remains
  at 42/42, Vulkan diagnostics 4/4, graphics foundation 228/228, scene-frame
  12/12 and shader pipeline 34/34.
- In a deterministic 30-presentation Kokiri shadow run, the portable compiler
  emits two passes, four live resources, five bindings and one graph hazard. The
  runtime executes 13 authorized shadow passes and 26 graph-owned image
  transitions with zero missing reads, failed or unresolved graph passes,
  schedule/composition mismatches, or Vulkan/NRI errors. Authentic emits no
  shadow work and has the same zero-error result.
- Both direct captures preserve SHA-256
  `bae563168bce554f84401ba2012cabe4a3531de5f22efb6a1e4bcccd58434c53`.
  Evidence is retained under
  `I:/oot3dre_work/visual-parity/portable-resource-graph-20260826/`.
- The concrete post-dispatch binding table and its physical alias summary still
  use OOT3D enums. Transient image allocation has moved to the portable layer
  described below; format selection, native attachment lookup and scene-view
  payload schemas deliberately remain title/provider responsibilities.

### Portable Transient Resource Allocation (2026-08-26)

- `fast/renderer/extension_resource_allocation` compiles transient image
  requirements against the portable graph lifetimes. Resource and storage-class
  identities are opaque and versioned; the core validates ownership and writer
  lifetimes, computes peak liveness, aliases only compatible non-overlapping
  images and merges usage flags without knowing PICA formats or OOT3D enums.
- `BuildEffectTransientAllocationPlan` is now an OOT3D adapter. It translates
  `EffectResource`, residency and storage policy into generic descriptors, then
  maps the compiled result back into the existing fixed-capacity arena contract
  and diagnostic masks. Vulkan format/extent/usage selection remains in the NRI
  provider, so a future title supplies policy rather than forking the allocator.
- `CompiledEffectGraph` retains the compiled portable resource plan instead of
  rebuilding lifetimes inside the adapter. This gives scheduling, hazards and
  allocation one shared source of truth while preserving the current typed API
  during migration.
- The independent allocation suite passes 4/4 tests for disjoint aliasing,
  overlapping lifetimes, storage/format incompatibility, merged usage, missing
  writers and invalid declarations. Generic resource-graph and scheduler suites
  pass 4/4 and 2/2; the OOT3D graph remains 42/42, Vulkan diagnostics 4/4,
  graphics foundation 228/228, scene-frame/view 12/12 and shader pipeline 34/34.
- A deterministic 30-presentation SSSR+SMAA run exercises up to 14 logical
  transient resources, 10 physical slots, four alias opportunities and four
  simultaneously live resources per presentation. All 52 arena invocations
  configure successfully, 51 reuse an existing configuration after the initial
  allocation, and no graph read, pass, composition or Vulkan/NRI validation
  error is reported.
- Shadow and Authentic regressions preserve their prior scheduling behavior;
  all three direct captures retain SHA-256
  `bae563168bce554f84401ba2012cabe4a3531de5f22efb6a1e4bcccd58434c53`.
  Evidence is under
  `I:/oot3dre_work/visual-parity/portable-transient-allocation-20260826/`.
  The product audit remains at 12,419 compiled functions, three host boundaries
  and zero residual A32 entries.
- The physical binding plan and portable resource publication surface have now
  moved to the reusable layer described below. Native handles, color encoding,
  semantic object schemas and OOT3D diagnostic masks remain in the adapter.

### Portable Physical Binding And Residency (2026-08-26)

- `fast/renderer/extension_resource_binding` defines versioned opaque object
  identities, image/semantic binding metadata, an allocation-free binding-table
  view and a physical planner with caller-owned output storage. It contains no
  game, PICA, Vulkan-handle, scene-size or color-policy knowledge.
- The generic planner validates publication uniqueness and binding kind, joins
  bindings with graph lifetimes and title-supplied residency policy, counts and
  deduplicates physical images, computes transient liveness and proposes only
  compatible non-overlapping aliases. It performs no heap allocation in the
  per-presentation path; a future title chooses its own fixed storage capacity.
- `DisplayEffectResourceTable` keeps OOT3D native image/view handles, color
  encoding and typed semantic pointers, while publishing a parallel portable
  metadata view. Existing read masks and typed lookup APIs are adapters over
  that view, so no NRI pass has to consume a title-neutral payload unsafely.
- `BuildEffectGraphPhysicalPlan` now delegates all residency accounting,
  binding-kind validation, physical-image deduplication, liveness and alias
  selection to the portable planner. Its remaining work is conversion between
  `OOT3DRES`/`OOT3DSTC` identities and the established fixed OOT3D diagnostics.
- The independent binding/physical-plan suite passes 4/4 tests covering read
  resolution, malformed/duplicate publications, all residency classes,
  physical-image deduplication, missing and wrong-kind bindings, aliasing,
  invalid policy and insufficient caller storage. Scheduler, resource graph and
  transient allocation remain 2/2, 4/4 and 4/4; OOT3D graph, diagnostics,
  foundation, scene-frame and shader-pipeline suites remain 42/42, 4/4,
  228/228, 12/12 and 34/34.
- Deterministic 30-presentation Authentic, shadow and SSSR+SMAA runs match the
  prior portable-allocation baseline in every selected per-frame structural
  field: zero mismatches for physical plans, graph reads, transient arenas,
  scheduling, composition and validation. SSSR+SMAA resolves 788/788 aggregate
  physical resources with zero missing; an active presentation reaches 30
  declared resources, 22 distinct images, 14 transient resources, 10 alias
  slots and four alias opportunities.
- All three framebuffer captures retain SHA-256
  `bae563168bce554f84401ba2012cabe4a3531de5f22efb6a1e4bcccd58434c53`,
  with zero Vulkan/NRI validation errors. Evidence is under
  `I:/oot3dre_work/visual-parity/portable-physical-binding-20260826/`.
  The product remains at 12,419 compiled functions, three host boundaries and
  zero residual A32 entries.
- Remaining cross-title work is concentrated above these mechanics:
  `PicaSceneFrame`, `NativeSceneView`, provider capability discovery and
  canonical composition payloads still expose title-owned schemas. They should
  gain portable publication/view contracts while native register decoding and
  asset semantics stay in each title package.

### Reusable Renderer Core Build Boundary (2026-08-26)

- CMake now builds `renderer_extension_core` as an explicit static library, with
  the stable alias `ThreeDsRecomp::RendererExtensionCore`. Its source list is
  deliberately explicit: schedule, resource graph, transient allocation and
  physical binding are the only current members. New files cannot enter the
  reusable layer merely by matching a broad source glob.
- `three_ds_recomp_runtime` and every generic or OOT3D graph test link the same core
  artifact. The four implementation files are no longer compiled into the
  monolith and then recompiled separately by six test targets. This makes the
  dependency direction visible to CMake and reduces both accidental title
  coupling and iterative build work.
- A second identical aggregate build performs no C++ compile or relink; only
  the pre-existing shader-generation check and whole-AOT closure audit run.
  The independently linked generic suites pass 14/14 tests, while the OOT3D
  graph, diagnostics, foundation, scene-frame and shader-pipeline gates remain
  42/42, 4/4, 228/228, 12/12 and 34/34.
- A fresh 30-presentation SSSR+SMAA run from the relinked product has zero
  structural differences from the pre-split physical-binding run, preserves
  framebuffer SHA-256
  `bae563168bce554f84401ba2012cabe4a3531de5f22efb6a1e4bcccd58434c53`,
  and reports zero graph, composition or Vulkan/NRI validation errors. Evidence
  is under
  `I:/oot3dre_work/visual-parity/portable-renderer-core-target-20260826/`.
- A future 3DS title can now link the core and supply its own schedule/resource
  identities, residency/storage policy and native binding publication. It does
  not need to fork the allocator or NRI scheduling mechanics. A later physical
  repository/package extraction is mechanical once scene publication and pass
  provider interfaces no longer include OOT3D schemas.

### Cross-Title 3DS Scene Publication And Capabilities (2026-08-26)

- `fast/renderer/extension_scene_publication` adds a generic, versioned and
  allocation-free scene-capability table. It validates unique publications,
  one coherent frame identity and exact or ranged payload schemas without
  knowing PICA, 3DS, OOT3D or backend handles.
- `renderer_3ds_pica_core` is now a separate static target with the stable alias
  `ThreeDsRecomp::Renderer3dsPicaCore`. Its first shared contracts identify a
  resolved PICA draw stream, semantic scene view and perspective camera. It
  depends on `RendererExtensionCore`; neither generic core depends on OOT3D.
- `PicaScenePublicationAdapter` remains title-owned. It validates and publishes
  the concrete `PicaSceneFrame` and `NativeSceneView` plus the shared 3DS camera
  payload through common capability identities, and retains title-payload
  resolution inside the OOT3D boundary.
- The production grass `GeometryProvider` now resolves these publications
  instead of reading backend-local scene/frame availability booleans. Reset,
  resource destruction and frame publication all invalidate or replace one
  coherent snapshot; no geometry, uniform, texture or scene payload is copied.
- Independent coverage passes 4/4 generic publication tests and 2/2 shared
  3DS/PICA capability tests. Existing gates pass 42/42 OOT3D graph/provider,
  13/13 scene-frame/publication, 34/34 shader-pipeline, 4/4 diagnostics and
  228/228 graphics-foundation tests.
- A 180-presentation Kokiri run declares the provider in all 180 frames,
  performs 13,327 source inspections and publishes 178 coherent source views.
  It reports zero unavailable/schema-invalid inputs, schedule rejects,
  composition mismatches or Vulkan/NRI validation errors. This checkpoint does
  not schedule a grass execution boundary, so the evidence proves the real
  provider publication/authorization path but does not claim a provider draw.
  The product remains at 12,419 compiled functions, three host boundaries and
  zero residual A32 entries. Evidence is under
  `I:/oot3dre_work/visual-parity/portable-3ds-scene-publication-20260826/`.
- Cross-title 3DS extraction remains incomplete at the frame/view level:
  concrete resolved draw streams and semantic scene views still live under
  `fast/oot3d`. Camera, material, raster and target payload extraction is closed
  by the next tranche; scene/actor, asset and gameplay meanings remain in each
  3DS title adapter.

### Shared 3DS/PICA State And Scene Payloads (2026-08-26)

- `fast/renderer3ds/pica_native_state` now owns the platform-level PICA types:
  blend/cull/sampler state, topology and vertex formats, depth/stencil/logic
  operations, native texture/vertex views, packed lighting LUT references and
  the complete typed fragment-lighting/fog feature layout.
- `fast/renderer3ds/pica_scene_payloads` owns the versioned perspective camera,
  buffer/texture references, native and resolved viewport/scissor state,
  renderer-owned target references and exact resolved material state. These
  contracts contain no OOT3D scene, actor, asset or gameplay identity.
- The legacy `Oot3d::Renderer` names are aliases to the shared types rather than
  parallel definitions. `PicaSceneFrame` embeds the shared payloads directly,
  and compile-time identity tests prevent a future adapter-local copy from
  silently reappearing.
- The production grass provider now accepts
  `Renderer3ds::PicaPerspectiveCameraState` directly. The OOT3D camera runtime
  remains the title hook and publishes that shared payload without conversion
  or per-frame bulk copies.
- Coverage passes 4/4 shared 3DS/PICA tests, 13/13 scene/publication, 42/42
  graph/provider, 34/34 shader-pipeline, 4/4 diagnostics and 228/228 graphics
  foundation tests; native frontend and Vulkan bridge suites also pass. A full
  product build closes at 12,419 functions, three host boundaries and zero
  residual A32 entries. An identical second build compiles and relinks no C++.
- A deterministic 60-presentation Kokiri run submits 9,233 draws, performs
  4,901 grass source inspections and publishes 58 coherent source views. All
  386 comparable scalar Vulkan diagnostics fields are identical to the
  pre-extraction run in every frame, with zero unavailable inputs, schedule or
  composition mismatches, and zero Vulkan/NRI warnings or errors. The new
  framebuffer capture is taken at the actual scene frame; the older comparison
  image was captured before useful presentation and was black, so it is not
  used as pixel-parity evidence. Artifacts are under
  `I:/oot3dre_work/visual-parity/portable-3ds-shared-payloads-20260826/`.
- Remaining cross-title work is now narrower and explicit: publish a genuinely
  shared resolved-draw stream and semantic view, extract common PICA shader-hook
  contracts from the legacy title namespace, and keep object, asset and
  game-state enrichment in each title adapter. Composition and source identity
  extraction are closed by the following tranche.

### Shared PICA Composition And Shader Identity (2026-08-26)

- `fast/renderer3ds/pica_composition` now owns the 3DS-wide scene/UI domains,
  ordered world/transparent/atmosphere/UI layers, provenance, native target
  identity and frame-lifetime composition-sequence view. No title scene or
  gameplay policy is encoded in these types.
- `fast/renderer3ds/pica_shader_source_identity` owns deterministic dual-hash
  shader-source identities. The legacy `Oot3d::Renderer` headers are thin
  aliases to both new contracts, so frontend, cache and backend continue to use
  the same ABI while a second 3DS title can include the common API directly.
- `PicaSceneFrame` and `NativeSceneView` now name the shared composition types
  directly. Compile-time identity tests prohibit duplicate legacy payloads, and
  common tests cover stable source IDs plus ordered composition metadata.
- Coverage passes 6/6 shared 3DS/PICA tests, 13/13 scene/publication, 42/42
  graph/provider, 34/34 shader pipeline, 4/4 diagnostics and 228/228 graphics
  foundation tests; native frontend and Vulkan bridge suites pass. The product
  remains at 12,419 compiled functions, three host boundaries and zero residual
  A32 entries, and a second identical build performs no C++ compile or relink.
- Two otherwise identical 60-presentation Kokiri runs have zero differences in
  all 386 comparable scalar Vulkan fields. Their useful framebuffer captures
  are byte-identical at SHA-256
  `4f368098a185480a3fb94eaa0f1505460e4f7e24d47fc4d6a21dad7f4b86eb74`,
  with zero composition, provider, Vulkan or NRI errors. Evidence is under
  `I:/oot3dre_work/visual-parity/portable-3ds-composition-identity-20260826/`.
- The next dependency to extract is the typed PICA shader-hook program. Only
  after that contract is title-neutral should the shared resolved-draw stream
  expose shader, geometry and uniform references; semantic actor/asset identity
  remains an adapter capability rather than part of PICA.

### Shared PICA Shader Hook Contract (2026-08-26)

- `fast/renderer3ds/pica_shader_hooks` now owns the complete typed PICA GPU
  program contract: canonical fragment outputs, fragment and vertex hook
  locations, texture-coordinate programs, model/view/projection transforms,
  matrix-palette skinning and temporal vertex-program views. These are reusable
  PICA semantics for other Nintendo 3DS games, not OOT3D gameplay semantics.
- `oot3d/renderer/pica_shader_hooks` is only a compatibility wrapper. Its legacy
  names alias the shared types and operators exactly; no conversion, copy or
  second schema exists. Compile-time identity checks protect that boundary.
- Direct shared-core tests construct and validate fragment, vertex and temporal
  hook layouts without including an OOT3D API. Coverage passes 8/8 shared
  3DS/PICA tests, 13/13 scene/publication, 42/42 graph/provider, 34/34 shader
  pipeline, 4/4 diagnostics and 228/228 graphics foundation tests; native
  frontend and Vulkan bridge suites also pass.
- The full product remains at 12,419 compiled functions, three host boundaries
  and zero residual A32 entries. A second identical build invokes only
  ShaderMake and the whole-AOT closure audit, with no C++ compilation or link.
- A deterministic 60-presentation Kokiri run matches the preceding build in all
  386 comparable structural fields on every frame and preserves framebuffer
  SHA-256
  `4f368098a185480a3fb94eaa0f1505460e4f7e24d47fc4d6a21dad7f4b86eb74`.
  It submits 9,233 native draws and reports zero provider-schedule rejection,
  composition mismatch, Vulkan error or NRI error. Evidence is under
  `I:/oot3dre_work/visual-parity/portable-3ds-shader-hooks-20260826/`.
- The next cross-title extraction is a shared resolved PICA draw stream built
  from these common shader, state, composition and GPU-resource contracts. Each
  game adapter will continue to own actor, asset, scene-state and gameplay
  enrichment; those meanings must not leak into the shared 3DS renderer.

### Shared Resolved PICA Draw Stream (2026-08-26)

- `fast/renderer3ds/pica_scene_semantics`, `pica_vertex_input_layout` and
  `pica_resolved_draw_stream` now own the platform-level draw environment,
  packed PICA vertex layout, resolved draw record and frame-lifetime stream
  view. This is a reuse contract for other Nintendo 3DS/PICA titles; it is not
  a generic cross-console scene model.
- `PicaSceneFrame` stores the shared records directly and publishes an
  allocation-free view over its existing draw and vertex-binding storage. The
  publication adapter exposes that shared view under the versioned
  `ResolvedDrawStream` capability, so consumers do not copy geometry, uniforms,
  textures or per-draw metadata.
- Directional-shadow caster selection remains OOT3D/provider policy in a
  parallel title-owned sidecar. It is deliberately absent from the shared draw
  record: another 3DS game can reuse native PICA state without inheriting an
  OOT3D effect classification.
- Legacy OOT3D state and vertex-input headers are compatibility aliases to the
  shared types. Shared-core coverage passes 9/9 tests, scene/publication 13/13,
  graph/provider 42/42, shader pipeline 34/34, diagnostics 4/4 and graphics
  foundation 228/228; native frontend and Vulkan bridge suites also pass.
- The full product remains at 12,419 compiled functions, three host boundaries
  and zero residual A32 entries. An identical second build performs no C++
  compilation or relink.
- An unchanged warm 60-presentation Kokiri run matches the preceding shader-hook
  baseline in all 386 comparable Vulkan diagnostics fields and preserves
  framebuffer SHA-256
  `4f368098a185480a3fb94eaa0f1505460e4f7e24d47fc4d6a21dad7f4b86eb74`.
  It submits 9,233 draws and reports zero schedule, composition or Vulkan/NRI
  errors. Evidence is under
  `I:/oot3dre_work/visual-parity/portable-3ds-resolved-draw-stream-20260826-v2/`.
- The immediate first run is retained separately because one empty diagnostic
  presentation shifted the subsequent frame sequence; the unchanged rerun is
  the accepted parity evidence. A separate 180-presentation shadow-enabled
  configuration produced zero provider attempts and zero casters in the current
  checkpoint, just as the immediately preceding baseline did. It therefore
  validates only a clean no-provider path, while title-side caster preservation
  remains covered by tests rather than claimed as runtime execution evidence.
- The next shared 3DS tranche is a platform semantic scene view over these draw
  records. Camera, transform, skeleton, native lighting and fog availability can
  be shared PICA semantics; actor, object, asset and game-state identities must
  remain enrichment supplied by each title adapter.

### Shared 3DS/PICA Semantic Scene View (2026-08-26)

- `fast/renderer3ds/pica_semantic_scene_view` now owns the versioned semantic
  projection of a resolved PICA draw stream. It exposes stable geometry,
  material, raster, target, texture and uniform references together with native
  transform, skeleton, lighting, fragment-lighting, depth and fog availability.
  The implementation is part of `renderer_3ds_pica_core` and contains no OOT3D
  scene, actor, object, asset or gameplay vocabulary.
- The view stores only scalar identities and spans over the resolved stream. A
  draw lookup validates its own binding range in constant time and returns
  pointers to renderer-owned state; it performs no geometry, uniform, texture
  or per-frame draw copy.
- `NativeSceneView` remains the OOT3D title adapter. Its legacy PICA reference
  names alias the shared types, its draw description delegates to the common
  projector, and title-owned `ObjectIdentity` enrichment remains outside the
  shared base view. Future OOT3D actor/asset facts therefore cannot become an
  accidental requirement for another 3DS title.
- `PicaScenePublicationAdapter` now publishes the shared semantic payload and
  schema under `SemanticSceneView`; the resolved draw stream and perspective
  camera capabilities remain independently versioned. Existing OOT3D geometry
  provider requirements consume these common 3DS payloads without changing
  provider behavior.
- Direct shared-core coverage passes 10/10 tests, scene/publication 13/13,
  graph/provider 42/42, shader pipeline 34/34, diagnostics 4/4 and graphics
  foundation 228/228; native frontend and Vulkan bridge suites also pass. The
  product audit remains at 12,419 compiled functions, three host boundaries and
  zero residual A32 entries.
- The accepted final 60-presentation Kokiri run is structurally identical to
  the resolved-stream baseline in all 384 comparable scalar Vulkan fields. It
  submits 9,233 draws, performs 4,901 source inspections and 58 coherent source
  publications, with zero schedule, composition or Vulkan/NRI errors. The top
  framebuffer is byte-identical at SHA-256
  `4f368098a185480a3fb94eaa0f1505460e4f7e24d47fc4d6a21dad7f4b86eb74`.
  Evidence is under
  `I:/oot3dre_work/visual-parity/portable-3ds-semantic-scene-view-20260826-v5/`.
- Earlier captures in that evidence family are retained but rejected as parity
  gates: two used the native OOT3D lower-screen profile instead of the baseline
  TopScreen profile, and one cold TopScreen run contained the known empty
  diagnostic presentation. The unchanged warm profile above is the accepted
  comparison.
- The next cross-title 3DS boundary is renderer-facing view-family and temporal
  metadata plus a shared provider-requirement helper. Pose/projection history
  can be common renderer data; camera selection, actor identity and gameplay
  state remain responsibilities of each game's adapter.

### Shared 3DS View Family And Frame Timing (2026-08-26)

- `fast/renderer3ds/pica_frame_timing` now owns the presentation-only temporal
  sample and fixed x2/x3/adaptive composition policy, while
  `fast/renderer3ds/pica_view_family` owns mono/stereo view descriptions and
  pose/projection versions. These contracts target reuse by other Nintendo 3DS
  games; they neither advance simulation nor select a title camera.
- The existing OOT3D frame-composer and view-family APIs are exact aliases and
  forwarding builders over the shared contracts. This preserves source and ABI
  behavior while removing duplicate title schemas or conversion copies.
- `PicaSemanticSceneView` publishes frame-lifetime pointers to the temporal
  sample plus current and previous view families. It validates compatible view
  history without allocation, and the OOT3D adapter continues to own the
  objects and all gameplay-specific camera policy.
- Common geometry providers now obtain their exact resolved-stream, semantic-
  view and perspective-camera requirements from
  `BuildPicaGeometryProviderSceneRequirements`. The OOT3D helper is only a
  compatibility forwarder, so another 3DS title can publish and consume the
  same PICA requirements without depending on OOT3D headers.
- Coverage passes 11/11 shared 3DS/PICA tests, 13/13 scene/publication, 42/42
  graph/provider, 34/34 shader pipeline, 4/4 diagnostics and 228/228 graphics
  foundation tests; native frontend and Vulkan bridge suites also pass. The
  full product remains at 12,419 compiled functions, three host boundaries and
  zero residual A32 entries, and a second build performs no C++ compile or
  relink.
- The 60-presentation runtime capture preserves all 9,233 produced native draws
  and reports zero schedule, composition, Vulkan or NRI errors. The recorder
  inserted one additional empty diagnostic presentation, reused the preceding
  snapshot and left the final 176-draw frame pending. Aligning by source
  submission instead of presentation index gives 58 common source frames and
  zero differences across 22,214 comparable scalar fields. The differing final
  framebuffer therefore records a different source instant and is not accepted
  as a pixel-parity comparison. Evidence is under
  `I:/oot3dre_work/visual-parity/portable-3ds-view-family-timing-20260826-v3/`.
- The next reusable 3DS boundary is the provider execution plan and composition
  scheduling input. Shared code may describe PICA dependencies and ordered
  render work; each game adapter must continue to decide which scene, camera,
  actors and gameplay state produce that work.

### Shared 3DS/PICA Composition Scheduling (2026-08-26)

- `fast/renderer3ds/pica_composition_schedule` now compiles the exact ordered
  PICA draw sequence into target runs and conservative extension-stage anchors.
  It never reorders native work and withholds a world boundary when layer or
  target evidence is ambiguous. Its input is the shared compact composition
  record projected from the shared Nintendo 3DS `PicaDrawView` facade.
- `fast/renderer3ds/pica_extension_schedule` maps only a declared PICA target
  and anchor into the renderer-neutral NRI extension scheduler, rejecting stale
  or fabricated anchors. `pica_display_composition` resolves scene/UI scanout
  from that same schedule. All three modules are reusable by other Nintendo 3DS
  titles and contain no OOT3D actor, scene or gameplay policy.
- The legacy OOT3D schedule and display headers are exact type aliases. The
  zero-copy draw-to-composition projection now lives with the shared backend
  facade. The OOT3D effect graph still owns pass names, enablement and resource
  policy; only its selected pass is handed to the common scheduler.
- Direct shared coverage passes 12/12 tests, including schedule compilation,
  boundary authorization, fabricated-anchor rejection and display resolution.
  OOT3D graph/provider coverage remains 42/42, scene/publication 13/13, shader
  pipeline 34/34, diagnostics 4/4 and graphics foundation 228/228; native
  frontend and Vulkan bridge tests also pass.
- The full product remains at 12,419 compiled functions, three host boundaries
  and zero residual A32 entries. A second identical build runs only ShaderMake
  and the closure audit, with no C++ compilation or relink.
- A warm 60-presentation Kokiri run is identical to the semantic-scene-view
  baseline in all 23,040 compared scalar frame fields and in framebuffer
  SHA-256
  `4f368098a185480a3fb94eaa0f1505460e4f7e24d47fc4d6a21dad7f4b86eb74`.
  Both runs submit 9,233 draws, inspect 4,901 geometry sources, publish 58
  coherent sources and report zero composition mismatch, Vulkan error or NRI
  error. Evidence is under
  `I:/oot3dre_work/visual-parity/portable-3ds-composition-schedule-20260826/`.
- The next reusable 3DS boundary is the capability-driven core of geometry-
  provider authorization. A shared plan may validate PICA scene requirements
  and invocation stage; each title must still define its own effect graph,
  feature enablement and resulting extension geometry.

### Shared 3DS Geometry-Provider Authorization (2026-08-26)

- `fast/renderer3ds/pica_geometry_provider_authorization` now owns the immutable
  capability requirement set, extension stage and world-geometry gate used to
  authorize a PICA geometry provider. It resolves the shared scene publication
  table without allocation and preserves the exact generic capability failure
  for diagnostics.
- The OOT3D `EffectGeometryProviderPlan` still compiles title-owned pass names,
  reads, writes, exports and native-composer fallback policy. It selects the
  required PICA capabilities once, then delegates runtime capability, stage and
  observed-geometry decisions to the shared 3DS plan. Another Nintendo 3DS game
  can therefore reuse authorization without adopting OOT3D's effect graph.
- Direct shared coverage passes 13/13 tests, including complete publications,
  missing capability, wrong stage and unavailable geometry. OOT3D
  graph/provider coverage remains 42/42, scene/publication 13/13, shader
  pipeline 34/34, diagnostics 4/4 and graphics foundation 228/228; native
  frontend and Vulkan bridge tests pass.
- The full product remains at 12,419 compiled functions, three host boundaries
  and zero residual A32 entries. The unchanged second build performs no C++
  compilation or relink.
- The accepted warm 60-presentation Kokiri run matches the preceding shared
  composition baseline in all 23,040 scalar frame fields and preserves
  framebuffer SHA-256
  `4f368098a185480a3fb94eaa0f1505460e4f7e24d47fc4d6a21dad7f4b86eb74`.
  It submits 9,233 draws, performs 4,901 provider source inspections and 58
  publications, with zero unavailable input, schedule rejection, composition
  mismatch, Vulkan error or NRI error. Evidence is under
  `I:/oot3dre_work/visual-parity/portable-3ds-provider-authorization-20260826-v3/`.
- Two earlier captures are retained as harness evidence: each inserted one
  empty diagnostic presentation at a different index and left the final
  176-draw frame pending. Their 58 source-aligned frames still had zero
  differences across 22,214 fields, but they are not used as the accepted pixel
  comparison.

### Shared Nintendo 3DS/PICA Backend Facade (2026-08-26)

- `fast/renderer3ds/pica_render_backend` now owns the complete transient PICA
  draw view, display-transfer and memory-fill views, portable texture/target/
  presentation snapshots and the backend virtual interface. Its entry points
  use PICA names rather than OOT3D names and carry no title actor, camera,
  gameplay, UI or effect-activation policy.
- The draw-to-composition projection also belongs to this shared 3DS contract.
  It retains only compact ordered metadata and does not copy geometry,
  uniforms, textures or shader source. The Vulkan backend derives directly
  from the shared interface and consumes the same spans and immutable views as
  before.
- `oot3d/renderer/pica_render_backend.h` is now a compatibility header made of
  exact type aliases. The OOT3D submission and presentation code remains a
  title adapter: it decides how OOT3D command ownership produces PICA work,
  then submits that work through the shared Nintendo 3DS interface. Another
  3DS game must supply its own adapter and must not inherit OOT3D gameplay or
  scene policy.
- Compile-time compatibility checks prove identity for draw, transfer,
  presentation-state and backend types. Direct shared coverage passes 15/15,
  OOT3D scene/publication 13/13, graph/provider 42/42, shader pipeline 34/34,
  Vulkan diagnostics 4/4 and graphics foundation 228/228; native frontend,
  Vulkan bridge and visual-savestate harnesses also pass.
- The complete product compiles through the migrated Vulkan backend and
  retains 12,419 compiled functions, three host boundaries and zero residual
  A32 entries. A later relink with full PDB/map output encountered an external
  inactive `lld-link` wait after all objects and tests had built; it was
  terminated rather than reported as a code or test failure.
- The accepted 60-presentation Kokiri run is identical to the preceding
  provider-authorization baseline in all 23,040 compared scalar Vulkan frame
  fields and preserves framebuffer SHA-256
  `4f368098a185480a3fb94eaa0f1505460e4f7e24d47fc4d6a21dad7f4b86eb74`.
  Both runs submit 9,233 draws and perform 4,901 geometry-source inspections,
  with zero composition mismatch, unavailable provider input, schedule
  rejection, Vulkan error or NRI error. Evidence is under
  `I:/oot3dre_work/visual-parity/portable-3ds-backend-facade-20260826-v3/`.
- The next Nintendo 3DS reuse boundary is PICA/GSP lifecycle transport still
  mixed into `GfxRenderingAPI`: completion signaling and reset semantics can
  become shared platform contracts, while OOT3D overlay preparation,
  presentation preferences and scene policy must remain in its title adapter.

### Shared Nintendo 3DS/PICA Lifecycle (2026-08-26)

- `PicaRenderBackend` now owns the platform lifecycle entry points
  `QueuePicaCompletion`, `TakePicaCompletions`, `ResetPicaState` and
  `PublishPicaFrameTemporalSample`. `GfxRenderingAPI` no longer redeclares
  title-prefixed copies or includes the OOT3D frame-composer compatibility API.
- Completion IDs are deliberately opaque renderer fence tokens. Each 3DS title
  adapter retains ownership of the mapping from those IDs to its guest GSP
  relay, interrupt bookkeeping and service control flow; none of that policy
  entered the shared renderer contract.
- Reset means invalidating backend state derived from the PICA stream. It does
  not reset gameplay or guest services. Extension-provider caches derived from
  those surfaces are backend state; OOT3D clocks, interaction bridges and scene
  publications are now reset separately by the title adapter below.
- Overlay preparation, window/presentation preferences and OOT3D camera-scene
  publication remain title/host methods. Another Nintendo 3DS game can reuse
  PICA transport, frame timing and lifecycle without adopting those methods or
  Zelda-specific interrupt meanings.
- Direct shared coverage remains 15/15 and now exercises completion transport,
  reset and temporal publication through the title-neutral interface. OOT3D
  scene/publication passes 13/13, graph/provider 42/42, shader pipeline 34/34,
  Vulkan diagnostics 4/4 and graphics foundation 228/228; Vulkan bridge and
  runtime tests pass. The Vulkan backend and real game-loop translation unit
  compile, and the product audit remains at 12,419 functions, three host
  boundaries and zero residual A32 entries.
- No rendering behavior changed in this tranche. A fresh monolithic product
  relink was not accepted as runtime evidence because full PDB/map `lld-link`
  again entered an inactive all-thread wait after every affected object and
  library had compiled; the process was terminated and no linker or game
  process was left running. The preceding byte-identical Kokiri capture remains
  the current visual baseline rather than being relabeled as new evidence.
- The next implementation boundary is to isolate title publication and
  extension-cache invalidation from the platform reset implementation, then
  remove the remaining OOT3D overlay/scene hooks from the reusable backend
  surface without changing their title-adapter behavior.

### OOT3D Title Render Adapter Boundary (2026-08-26)

- `fast/oot3d/title_render_backend` now contains the optional OOT3D-only
  renderer interface and its presentation and scene-view payloads.
  `GfxRenderingAPI` no longer declares or includes OOT3D overlay, window,
  camera-scene or title-reset contracts.
- Vulkan implements both independent interfaces: the shared Nintendo 3DS
  `PicaRenderBackend` and the per-game `Oot3d::TitleRenderBackend`. This is
  deliberate multiple-interface composition, not inheritance of Zelda policy
  by the shared 3DS renderer.
- The game runtime resolves the title adapter once from the active generic
  backend. Guest camera publication and title reset use it when available;
  the PICA reset remains unconditional and shared. The legacy UI adapter also
  treats title overlay preparation as optional, preserving the previous
  non-Vulkan behavior without adding OOT3D methods back to the generic API.
- OOT3D grass interaction, visual clock, camera runtime, native scene view and
  scene-capability publications moved from `ResetPicaState` to
  `ResetTitleState`. PICA targets, textures, schedules, temporal GPU state and
  extension resources remain owned by the backend reset.
- Shared 3DS coverage passes 15/15. OOT3D scene/publication now passes 14/14,
  including the separate title-adapter contract; Vulkan bridge and runtime
  tests pass. The OpenGL and Vulkan backends, legacy UI adapter and real game
  loop compile, while the product audit remains 12,419 functions, three host
  boundaries and zero residual A32 entries.
- The old transform, fog, alpha-test, texture-unit and Shadow2D helpers still
  present in `GfxRenderingAPI` are not silently classified as title policy.
  The next extraction must determine which are obsolete compatibility calls
  and which are genuine reusable PICA operations before moving or deleting
  them.
- A strategically useful next gate is a second-title Nintendo 3DS canary, not
  a parallel full port: its adapter must compile without any `fast/oot3d`
  include and replay a small real PICA corpus covering opaque, transparent,
  TEV/material, UI and display-transfer work. A structurally different title
  provides stronger evidence than another Zelda title; gameplay remains out
  of that validation target.

### Cross-Title Nintendo 3DS Consumer Canary (2026-08-26)

- `renderer_3ds_cross_title_canary_tests` models an independent title frontend
  and links only `Renderer3dsPicaCore` plus its test runner. It includes no
  OOT3D compatibility API and submits one representative frame through the
  shared backend contract.
- The frame exercises ordered opaque, transparent, atmosphere and UI draws,
  canonical shader/material identities, native texture identity, scene fog,
  clear/fill, fixed 30-to-60 presentation timing, display transfer/present,
  completion transport, presentation capture and PICA reset. The recording
  backend compiles and consumes the common composition schedule in exact native
  order rather than reconstructing title policy.
- The build runs `VerifyRenderer3dsCrossTitleBoundary.cmake` before compiling
  the canary. It scans all shared `renderer3ds` headers and sources plus the
  consumer and rejects title-specific tokens. Configuration also rejects a
  direct dependency from `renderer_3ds_pica_core` to the monolithic runtime or
  an OOT3D target. The positive gate checks 24 files; a deliberate negative
  invocation with an OOT3D header is rejected.
- The canary passes 1/1, shared PICA coverage remains 15/15, and the OOT3D title
  adapter remains 14/14. This tranche changes no production rendering path, so
  it does not claim new framebuffer evidence; the accepted 60-presentation
  parity corpus remains `portable-3ds-backend-facade-20260826-v3`.
- This is a structural cross-title proof, not evidence from a second commercial
  game. The next semantic gate is a small, legally obtained corpus of roughly
  5-10 representative PICA frames from a structurally different Nintendo 3DS
  title, replayed by a thin adapter against this same target. Building that
  title's gameplay, audio, assets or UI is explicitly unnecessary.

### Real Second-Title PICA Conformance (2026-08-26)

- The second-title gate now uses a legally supplied Super Mario 3D Land image
  through Azahar's generic PICA command recorder. ROM data and raw captures
  remain external; only a privacy-scrubbed aggregate report is produced.
- The stable corpus covers 67 command lists and 169 draws, including 141
  programmable primitive-setup draws, 135 fragment-lit draws, 118 fogged
  draws, 63 stencil-tested draws, 109 ETC textures and 42 distinct TEV states.
  All `169/169` are representable by the shared contract with zero unknown
  native-state or ordering categories.
- The synthetic canary now exercises the observed feature classes, while a
  standalone streaming inspector provides repeatable checks without linking a
  title or importing game data. The boundary scanner covers the diagnostic
  implementation as well as the canary.
- This establishes real cross-title state/transport coverage, not full visual
  replay: the captured command stream includes shader uploads but no explicit
  shader-identity record or texture payload. Provenance, hashes, exact results,
  limitations and repeat commands are in
  `docs/RENDERER3DS_SECOND_TITLE_PICA_CORPUS.md`.

### Shared Nintendo 3DS NRI PICA Execution (2026-08-26)

- Pipeline creation, descriptor ownership, draw binding, sparse upload arenas
  and the combined-sampler shader ABI now live in the title-neutral
  `Renderer3dsNriPicaExecution` target. `Renderer3dsPicaCore` remains portable
  and independent from Vulkan/NRI.
- OOT3D keeps a thin compatibility adapter that supplies only Vulkan/NRI
  interop services and translates its existing environment options. The
  shared implementation contains no title token or gameplay policy; the
  cross-title boundary gate now covers 38 files.
- The shared target, OOT3D adapter and both Vulkan backend translation units
  compile. The 15 focused attachment/shader/upload tests pass, the complete
  product links, and an unchanged second build performs no C++ compilation or
  relink. Product closure remains 12,419 functions, three host boundaries and
  zero residual A32 entries.
- A deterministic 60-presentation Kokiri validation submits 9,233 draws with
  zero rejected scene draws, composition mismatches or Vulkan/NRI validation
  warnings and errors. Its final framebuffer is byte-identical to the accepted
  backend-facade baseline at SHA-256
  `4f368098a185480a3fb94eaa0f1505460e4f7e24d47fc4d6a21dad7f4b86eb74`.
  Evidence is under
  `I:/oot3dre_work/visual-parity/portable-3ds-nri-executor-20260826/`.
- This closes the execution-layer relocation only. A second-title frontend has
  not yet executed draws through this target, and texture decode/upload,
  render-target ownership and display transfer remain separate future
  extraction candidates. Details and exact restart order are in
  `runtime/three_ds_recomp/docs/RENDERER3DS_NRI_PICA_EXECUTION_HANDOFF.md`.
- Cross-title separation stops at this boundary. The current shared core,
  executor and OOT3D adapter are a committed, tested restart point; no additional
  extraction candidate is active. Future work must select one candidate from the
  handoff as an explicit bounded tranche rather than reopening the separation as
  an open-ended refactor.

### Typed Directional-Shadow Caster Closure (2026-08-26)

- The depth-caster vertex variant no longer discovers `void main` or closing
  braces in generated GLSL. The PICA frontend publishes a typed
  `ViewPositionOutput` semantic from native output mappings, and the caster
  builder inserts declarations and the light-space position override only at
  versioned vertex-hook offsets.
- Canonical vertex-hook metadata is interned with shader identity. A stale source
  contract, missing view-position semantic or conflicting hook layout is rejected
  before shader compilation; no title, actor, scene, texture or shader-ID rule is
  used.
- Frontend coverage passes, the focused shader and scene suites pass `36/36` and
  `15/15`, and the complete product links with 12,419 whole-AOT functions, three
  host boundaries and zero residual A32 entries. An unchanged rebuild performs
  no C++ recompilation or relink.
- A strict-AOT 60-presentation Kokiri run resolves `69/0` shader hits/misses,
  executes 28 shadow maps and 582 caster draws through stage 7, and reports zero
  scene rejects, composition mismatches or NRI/Vulkan errors. Its framebuffer is
  byte-identical to the dynamic variant at SHA-256
  `a83b5d41bbdabb1cb19955ac03edc881a6342f9b57197d706015fb6c054e1008`.
  Shadow and matching Authentic validation produce the same 640 pre-existing
  Vulkan warnings, so this variant adds none.
- The fixed-delta shadow profile sustains `95.85` real FPS over 480 measured
  presentations after 120 warm-up presentations, with VSync, pacing and audio
  disabled and zero dropped guest refreshes. Reproducible inventories, logs,
  diagnostics and framebuffers are under
  `I:/oot3dre_work/visual-parity/typed-shadow-caster-20260826/`.
- This is the closed restart point. The next renderer task may consume the typed
  caster contract but must not resume cross-title separation or shader-text
  mutation unless a new, explicitly scoped tranche is approved.

### Native Per-Pixel Lighting and Material Packs (2026-09-23)

- `Graphics.Effects.SurfaceLighting` has one F1 owner: **Lighting model** in
  Lighting. Values are `Native` (default), `PerPixel`, `PerPixelPBR`, and
  `NativePBR` (**Native + PBR**).
  Missing capability or unsupported typed shader metadata leaves the native
  path intact. PBR replaces toon lighting only on material-enabled draws;
  geometric outlines remain independent.
- The OOT3D adapter observes the lights actually accepted and assigned to the
  native three CMB slots. Identity, world position, radius and unattenuated
  point color travel with the draw. It does not select a different set of
  lights or sum the scene into a fabricated global source. Interpolation
  requires matching source kind and identity.
- Shared typed vertex metadata supplies rigid/skinned normals and unlit tint.
  Per-pixel lighting evaluates the selected native sources. PBR uses
  GGX/Smith/Schlick in linear color space before native fog; native diffuse and
  ambient material coefficients remain separate, including ambient-only
  materials whose diffuse coefficient is zero. Native fragment-lighting
  permutations and attenuation LUTs are retained where that path is used.
- Native TEV alpha is evaluated with the original lighting, even when alpha
  depends on RGB or `DOT3_RGBA`. Alpha tests, blending, depth/stencil, native
  draw order, transfers and UI composition are not replaced. Shadow visibility
  is applied to the matching selected direct-light slot, not ambient or
  emission. Mapped normals, roughness and reflectivity feed existing AO and
  reflection guides; the compiled graph continues to own pass order.

#### Native + PBR (2026-09-24)

`NativePBR` applies the existing material BRDF only to draws with a supported,
loaded PBR material. Draws without a material or supported typed shader hooks
retain their native lighting shaders, rather than falling back to Per-pixel.
An optional material color replacement is bound only after PBR instrumentation
has actually succeeded. Native projected/blob shadows remain in native order;
VSM is inactive without erasing its saved preference.

PBR-authored surfaces use the shared native scene-light evaluator, including
native directional sources and reconstructed per-pixel point attenuation.
Native source attribution and deduplication remain authoritative. This relights
those surfaces, including baked/ambient-only world materials; it does not
relight materials without a supported PBR pack. No synthetic lights or
asset-specific corrections are added. `UsesFullSceneLighting` remains limited
to Per-pixel and Per-pixel + PBR.

Scene Environment overrides and fairy-group overrides are inactive in this
mode; their saved profiles are retained. The F1 **Material editor** owns
per-material parameters, diagnostic previews and temporal-history invalidation.
Normal maps require existing direct light or reflections to affect shading.
Authentic and NativeMemory remain canonical. F2 and mandatory mobile native
presentation temporarily mask this new mode to Native, then restore the
configured mode when the desktop override is released.

The initial native-assignment-only implementation loaded the maps but left
baked world surfaces without direct illumination. Its diagnostic material views
did not establish a visible Shaded response. The corrected contract above was
qualified with nine deterministic 32-frame Kokiri runs, all in **Shaded**, plus
a 180-frame native-copy run against the previous Native reference.
Native output and the no-pack/Authentic fallbacks matched the previous runtime
byte-for-byte; extreme Environment changes were byte-inert. The material runs
kept all 220 draws: 219 canonical and one instrumented material draw.
Normal strength 0→1 changed 216,434 pixels and 1→8 changed 215,746.
Glossiness 0→1 changed 171,114 pixels and 1→2 changed 198,148.
All 466 sampled opaque heart pixels stayed exact.

Guest memory/content/process fingerprints matched across the Kokiri variants.
The native-copy scenario also matched the prior runtime's fingerprints and
Native framebuffer, with 108 deferred copies, 216 GPU bindings, one final
materialization and no prefix waits. Vulkan/NRI validation and graph checks
recorded zero errors; the same 11 Vulkan warnings occurred in the old Native
reference and new runs. The focused CPU suites passed 292/41/19 tests and
native-presentation policy checks passed. Two ambient-only world cases failed
before the fix; all 17 production lighting GPU cases passed afterward.
Real F1 widgets passed 5,111 assertions. Private captures, isolated profiles
and receipts remain under `I:/_triaevum_work/native-pbr/`, notably
`corrected-proof-20260924-224119` and `corrected-copies-20260924-224222`;
they are not release assets.

#### Pack authoring

Use the original Azahar texture hash, not a replacement image's pixel hash.
The pack's existing `use_new_hash` and `flip_png_files` options also apply.
Enable **Load custom textures**, select the pack directory in Texture packs,
then select **Per-pixel + PBR** or **Native + PBR** in Lighting.

| File | Meaning |
| --- | --- |
| `HASH_n.png` | Tangent-space normal; linear data, default `+Y` |
| `HASH_orm.png` | Linear R=occlusion, G=roughness, B=metallic |
| `HASH_e.png` | Emissive color, decoded from sRGB |
| `HASH_h.png` | Linear height in R; white is the native surface, black recessed |
| `HASH.material.json` | Optional parameters and explicit map paths |

`HASH` is hexadecimal. Use the short hash suffix names above, not
`tex1_..._n.png`. Existing Azahar `tex1_....norm.png` and mapped
`custom.norm.png` normal sidecars are also recognized and never indexed as
albedo replacements. Base color defaults to the bound native texture or its
ordinary Azahar color replacement. Optional `baseColor.texture` overrides it
only in the PBR material bundle, without entering the ordinary color cache.
Maps alone are sufficient; absent ORM defaults to roughness 1 and metallic 0,
and absent emission defaults to zero.

Example `0123456789ABCDEF.material.json`:

```json
{
  "schema": "triaevum.pbr-material",
  "version": 1,
  "shading": "metallic_roughness",
  "baseColor": { "source": "bound_texture", "texture": null, "factor": [1, 1, 1, 1] },
  "specularLevel": 1,
  "specularWeight": 1,
  "screenSpaceReflectionWeight": 1,
  "seamSoftening": {
    "enabled": false,
    "strength": 1,
    "width": 0.05,
    "channels": {
      "normal": true, "occlusion": true, "roughness": true,
      "metallic": true, "emissive": false, "height": true
    }
  },
  "normal": {
    "texture": "0123456789ABCDEF_n.png",
    "scale": 1,
    "convention": "+Y"
  },
  "orm": {
    "texture": "0123456789ABCDEF_orm.png",
    "occlusionStrength": 1,
    "roughnessFactor": 1,
    "metallicFactor": 1,
    "glossinessStrength": 1,
    "minimumRoughness": 0.045
  },
  "emissive": {
    "texture": "0123456789ABCDEF_e.png",
    "factor": [1, 1, 1]
  },
  "height": {
    "texture": "0123456789ABCDEF_h.png",
    "scale": 0.02
  }
}
```

Paths are relative to the JSON file and must remain inside the pack, including
after symlink resolution. Unknown fields, unsupported versions, invalid PNGs
and unresolved duplicate maps are errors. A section or its `texture` set to
`null` disables suffix discovery for that role. Normal scale accepts 0–8;
normal convention accepts `+Y` or `-Y`. Base and ORM factors accept 0–1;
base alpha must remain 1. Emissive factors accept 0–65504.
Height scale accepts 0–0.1 in UV space, default 0.02 when a map is present.
Optional `textureSlot` selects 0, 1 or 2 when native TEV uses multiple possible
color samplers; without it, an unambiguous final-RGB sampler is required.
Paint emissive coverage for the material's native blend mode: a uniformly
emissive additive particle quad will also illuminate its otherwise black edges.

Height uses bounded parallax occlusion mapping (8–32 view-ray steps, explicit
texture gradients and a grazing-angle fade). Without seam softening, base color,
normal, ORM and emission sample the same displaced UV; the tangent frame is derived
from the original coordinates. Native TEV alpha is replayed with the original UV
and lighting. This is surface relief, not geometry displacement: silhouettes,
depth, collisions and cast-shadow geometry remain native. It does not invent
ambient occlusion or light-ray self-shadowing from height. A missing/disabled
height map, scale zero or white height leaves the surface coordinates unchanged.
The material's **Height depth (UV)** directly edits `height.scale`; no scene-wide
height multiplier remains.

Optional `seamSoftening` blends auxiliary maps at geometric UV-chart boundaries;
it never filters or blends base color. It defaults Off. Strength accepts 0–1;
width accepts 0.001–0.25 of the shared native edge length, displayed in F1 as
0.1–25%. It is not a pixel radius and does not change with camera distance.
Channel selection is independent for normal, occlusion, roughness, metallic,
emissive and height; all except emissive are selected by default. Missing maps
remain inert. Height softening modifies only auxiliary-map parallax; base-color
relief retains its original unsmoothed coordinates and native alpha retains its
original replay.

The shared 3DS renderer reconstructs adjacency within a resolved native geometry
submission from exact positions, compatible bone data, smooth authored normals
and opposite-oriented manifold edges. It does not join separate draws, open
boundaries, non-manifold ownership, authored hard edges or opposing coplanar
layers. Typed affine UV metadata is required; projected/procedural UVs are not
guessed. Native indices, primitive order, strip degenerates, base vertices,
depth/stencil/blending and transfers remain unchanged.

`pica_pbr_seams` caches topology separately from pose. `pica_vertex_geometry`
evaluates the same typed rigid/palette contract for seam frames and shadow bounds;
`pica_texture_coordinates` is also the typed Grass affine decoder. Fragment
primitive IDs select declared read-only binding 26: seven vec4 records per
primitive, followed by packed corner-fan references. A draw without seam data
binds a valid 16-byte view there, because the always-available surface
programs declare the buffer. Immutable frame-arena slices
retain the correct data when one geometry is submitted with multiple poses.
The NRI and Vulkan paths share this ABI; the material UBO is 352 bytes.

Edge targets sample both charts with explicit gradients. Normals are transformed
through each posed geometric/UV frame before blending; ORM channels blend before
their material factors, and emission blends in linear space. An interpolatory
edge partition and shared corner-fan values handle overlapping seam bands and
multi-chart junctions. Off/zero-strength/empty loaded-channel selection uses the
existing shader variant without primitive-ID or seam-buffer reads. Unsupported
GPU primitive-ID capability leaves the effect inactive. F1 reports the selected
material's last native-draw status, including unavailable geometry/contracts.
Enabled bands require additional samples of the selected maps. Selecting height
can run a second parallax trace for auxiliary maps; it never reuses that trace
for native color. Width and enabled channels therefore have a GPU cost.

The existing asynchronous pack worker owns indexing, JSON parsing and PNG
decoding. **Reload custom textures** requests a new generation; the old valid
material remains usable until its replacement is complete. CPU publication and
GPU publication each commit a complete material bundle, including an optional
color replacement. PBR uploads are row-sliced within the shared 32 MiB/frame
budget; that bound does not remove the legacy color loader's existing first
oversize-image exception. GPU images are reused by decoded content and old
images retire behind the frame fence. F1 reports pending indexing and material
errors rather than claiming that an asynchronous reload already succeeded.

#### F1 Material editor (2026-09-25)

The top-level **Material editor** tab remains available without PBR capabilities,
with texture loading disabled, and during Native/F2 bypass. Lighting owns only
the lighting-model choice; Textures retains the sole pack folder/load controls.
`Graphics.Effects.PBR` is no longer read or written. Existing material descriptors
retain their authored values and neutral defaults; obsolete scene multipliers
are not silently baked into every file in the pack.

Select a descriptor or maps-only material in **Existing PBR material**, or open
**Create from texture** and use the same observed-texture selector and preview
as Grass. Creation uses the texture's actual Azahar identity and current pack
hash mode, never the catalog's different FNV content hash. Identity remains
available while replacement decoding is pending or loading is disabled.
New materials become renderable on their first explicit **Save material**.

**Material** edits the selected material only:

- **Normal strength** directly edits `normal.scale` (0–8); zero uses the geometric
  normal. The separate normal convention supports `+Y` and `-Y`.
- **Glossiness strength** edits `orm.glossinessStrength` (0–2, default 1).
  With `r = clamp(ORM.g * roughnessFactor, 0, 1)`, effective roughness is
  `clamp(1 - (1 - r) * glossinessStrength, minimumRoughness, 1)`.
- **Minimum roughness** edits `orm.minimumRoughness` (0.045–1, default 0.045).
  It limits the smoothest areas in both direct GGX lighting and reflection guides.
- **Height depth (UV)** edits `height.scale` (0–0.1); zero disables parallax.
  A height map is required; native geometry, collisions and alpha stay unchanged.
- **Dielectric reflectance** edits the existing `specularLevel` (0–2, default 1):
  `F0_dielectric = 0.04 * specularLevel`. The material mixes it with metallic base
  color once; GGX/Fresnel and reflection guides share that result. Fully metallic
  response is unchanged. Zero removes base reflectance, not grazing Fresnel.
- **Specular weight** edits `specularWeight` (0–1, default 1). It multiplies the
  complete direct GGX specular lobe after Fresnel and also the screen-space
  reflection contribution. Zero removes both, including grazing and metallic
  specular response, without changing diffuse or emission.
- **Screen-space reflection weight** edits `screenSpaceReflectionWeight` (0–2,
  default 1). It additionally scales only Hi-Z/SSSR reflection contribution, not
  direct highlights. The effective local reflection gain is the product of both
  weights. Reflections retains its separate scene-wide strength.
- **UV seam softening** owns the per-material enable, strength, edge-relative width
  and six channel selections described above. Off stays editable without renderer
  capabilities; Reset disables seam blending. These controls persist only in the
  selected descriptor's `seamSoftening` object.
- Base color, occlusion, roughness, metallic and emissive factors, plus the native
  texture-slot override, edit their existing descriptor fields.
- **Material view** is a transient selected-material Shaded / Surface normals /
  Roughness preview, not JSON state. Native alpha, blending, depth/stencil and
  UI composition remain native. **Reset material parameters** resets this draft,
  not other materials or scene settings.

Numeric edits preview live without texture reloads or per-value shader variants;
seam blending has a separate active/inactive specialization.
The runtime overlays only the matching material identity. Switching dirty
documents requires Save / Discard / Keep editing. Save persists the draft;
Discard reloads the descriptor. Native/F2 bypasses these previews.

The RGBA8 material guide carries reflectivity/F0 in R, roughness in G, encoded
reflection gain in B, and temporal reactivity in A. B no longer stores a material
class: byte 127 is neutral gain 1, byte 254 is gain 2, and zero is off
(`encode = gain * 127 / 255`, `decode = B * 255 / 127`). This preserves exact
neutral defaults with 1/127 gain precision. Native reflection profiles emit
neutral gain; their reflectivity and roughness remain separate. Hi-Z applies the
receiving pixel's gain after bilateral filtering so adjacent reflective pixels
cannot leak into a zero-weight surface. SSSR applies it in the final material
resolve, after denoising and BRDF weighting. Neither weight modifies F0,
roughness, native draw order or UI composition.

**Images** exposes Base color, Normal, ORM, Emissive and Height slots. Its file
browser and path input accept PNG, JPEG, BMP and TGA, decoded asynchronously by
the existing pack worker. Imports are copied into the pack as PNG on save.
Existing PNG paths remain valid and relative to the descriptor.

ORM accepts one packed image or three separate inputs. Each input selects R/G/B/A
or uses a constant when absent (occlusion 1, roughness 1, metallic 0). Inputs must
have identical dimensions; packing never silently resizes or gamma-converts
them. The result is one RGBA8 PNG with R=occlusion, G=roughness, B=metallic, A=255.
**Preview packed ORM** displays it before committing.

Imported/generated images use immutable content-addressed names under
`material-images/HASH/`. The descriptor is committed last through the existing
atomic configuration writer. Decode, channel-size and write errors retain the
previous descriptor and renderer bundle, keep the draft, and remain explicit
in F1. Save requests the existing asynchronous complete CPU/GPU bundle reload;
map edits do not become partially visible. Numeric revisions and completed GPU
bundle publication invalidate PBR temporal history.

Qualification: 6,395 real-widget assertions cover creation with distinct catalog
and Azahar hashes, all image slots and the browser, packed/separate ORM, selected
material isolation, weight persistence/preview/reset, capability loss, F2, dirty switching, corrupt images,
dimension mismatch and retry after an atomic-write failure. Foundation,
shader-pipeline and scene-frame suites pass 292/292, 41/41 and 19/19.

Ten deterministic 32-frame Kokiri captures preserve the prior default PBR
framebuffer exactly. Native remains pixel-identical with both modified parameters
and a PBR-only base-color override. Raising minimum roughness to 0.75 changes
128,034 shaded pixels and raises 219,450 Hi-Z roughness-guide pixels to the
RGBA5551-expanded floor value 189. Specular, normal sign/zero and base-color
changes affect their authored surface; all 256 compared native trace records
remain identical. Every run reports zero Vulkan/NRI errors, zero failed/missing/
unresolved graph resources or passes, and the same 11 baseline Vulkan warnings.
Evidence is private under `I:/_triaevum_work/material-editor/`.
SSSR was not requalified: the pre-existing scenario failure recorded in the F1
review remains a limit; Hi-Z guide verification is not proof of SSSR execution.

Per-material weight qualification: 18 deterministic 32-frame runs, including a
previous-executable Hi-Z reference and the staged private runtime, preserve
neutral PBR and Hi-Z framebuffers exactly. Native output is unchanged by either
weight; screen-space weight 0/2 leaves direct-only shading identical. Specular
weight 1→0 changes 211,099 pixels on the authored material, and 231,081 with a
fully metallic ORM. Hi-Z weight zero matches Reflections Off; neutral→2 changes
528 reflected pixels in this scene. Specular weight zero with reflection weight
2 produces the same guide as reflection weight zero, without changing F0.
Guide values on the wall away from HUD composition are the expected
RGBA5551-expanded 0/123/255 for gain 0/1/2.

All runs preserve the 110 compared native draws, including composition metadata,
geometry, textures, light selection, targets and depth/stencil/blend state.
Every run reports zero Vulkan/NRI errors and failed/missing/unresolved graph
resources or passes, with the same 11 baseline Vulkan warnings. All 59 maintained
pass artifacts were regenerated and checked. The production-kernel
`reflection_weight_gpu_tests` Vulkan regression checks adjacent zero/neutral/2x/
fractional weights, linear sampling and partial workgroups in both the Hi-Z
filter and SSSR material resolve. This proves the SSSR resolve consumer, not the
complete SSSR provider scenario. Private evidence, invocation recipes and
comparison receipts: `I:/_triaevum_work/material-weights/`.

UV-seam qualification (2026-09-25): `pbr_seam_gpu_tests` executes the production
PBR shader and geometry cache on Vulkan. It verifies rotated/mirrored normal
frames, scalar and linear-emission continuity, auxiliary-only height relief,
exact base-UV/color/alpha invariance, zero strength, channel isolation, finite
width, indexed strips with degenerate primitives and a nonzero base vertex, and
three-chart corner consensus. Topology regressions reject hard/open/disconnected,
non-manifold and opposing-overlap boundaries. The opposing-face case failed
before the ownership correction and passes afterward.

The actual F1 widget smoke passes 6,607 assertions, including seam enable,
strength, width, channel persistence, independent live preview, capability-safe
Off and Reset. Foundation, pipeline-cache, scene-frame and typed Grass-coordinate
suites pass 292/292, 41/41, 19/19 and 2/2.

Eight deterministic 32-frame native-runtime captures include the staged private
executable. Off exactly matches the previous build; zero strength and Native
bypass are pixel-identical to their controls. Widths 1%, 5% and 15% change 2,991,
17,916 and 59,439 pixels respectively on the authored wall, localized to its UV
seams. All captures preserve the 110 native draws across 21 compared state fields;
all 32 diagnostic frames per run have zero Vulkan/NRI errors and zero failed,
missing or unresolved graph passes/resources. The 11 baseline Vulkan warnings
remain. The staged executable matches the qualified enabled framebuffer exactly.
Evidence, invocation recipes and comparison receipts:
`I:/_triaevum_work/material-seams/`. This qualification does not extend the
existing full-provider SSSR claim.

Historical global-control qualification, superseded by the editor above:

The supplied `6CD66D0B74E16AEA` material was checked in six deterministic Kokiri
GPU runs at guest frame 23339: normal view at strengths 0/1/4, roughness view
at glossiness 0/1, and Shaded at 1/1. Each exited successfully and used a PBR
pipeline. Normal 0→1 changed 280,590 framebuffer pixels, normal 1→4 changed
302,492, and glossiness 0→1 changed 306,687. All six framebuffers were inspected;
native-memory RGBA and compared alpha/depth/stencil attachments were identical.
Evidence stays private under
`I:/_triaevum_work/kokiri-material-6CD66D0B74E16AEA/controls/`.
The rebuilt foundation and shader-pipeline suites passed 293/293 and 40/40;
the shared-renderer boundary passed for 77 files. The real F1 widget fixture
passed 4,200 assertions, including both live strengths, both diagnostic choices,
reset and capability loss. GPU captures exercise the configured uniform values;
the headless widget fixture separately exercises their live UI application.

#### Verification and limits

Kokiri Forest checkpoint `hud_minimap_visible_20260721.oot3dsav` was loaded for
Native, Native with a maps-only pack, Per-pixel, PBR, and PBR with directional
shadows/Hi-Z reflections. All five captures reached guest frame 23759 at
1280×720. Native with and without the maps-only pack was byte-identical;
native-memory RGBA and all compared alpha/depth/stencil attachments remained
identical across modes. Live inventories recorded 28 per-pixel and 21 PBR
pipeline variants. The first GPU capture exposed the ambient-only albedo bug;
the corrected capture retains the terrain and wall textures.

The private fixture contains 194 synthetic materials, not a distributable art
pack. Captures, inventories, isolated launch profiles and comparison evidence
remain under `I:/_triaevum_work/pbr-implementation/captures/`, outside public
packages. This checkpoint exercised two real directional source identities
and no selected point lights. Point transform/attenuation and native
fragment-lighting/LUT variants were probed separately; their presence in this
Kokiri GPU run is not claimed. GPU allocation failure was not induced.
The ten generated vertex/fragment variants compiled and passed SPIR-V
validation. Foundation tests passed 293/293, shader-pipeline tests 40/40,
the shared renderer boundary passed for 77 files, and the actual F1 widget
smoke passed 4,083 assertions.
Pipeline-manifest serialization and prewarm filtering also passed 8/8 tests.

#### Uniform arena catch-up lifetime

A real-time Kokiri session exposed `native PICA per-frame uniform arena
exhausted` after framebuffer readback: multiple catch-up refreshes share one
submitted frame and can exceed the original 4 MiB arena. The backend now grows
only when needed, reserving every aligned uniform block of a draw together
before uploading any block. Previously recorded buffers remain alive until
the owning frame fence completes; their contents are not copied or overwritten.
Legacy texture-descriptor caches are cleared on rollover because their keys
implicitly reference the active uniform buffer. NRI's existing capacity-change
fallback preserves bindings already recorded against its earlier upload arena.

The rebuilt runtime completed the real-time reproduction for 45 seconds:
1,530 presentations, 594 batched presentations and 21 framebuffer captures
through presentation 1,500, with no arena-exhaustion or scheduled-presentation
failure. The final framebuffer was inspected. The private reproduction profile
and evidence are in
`I:/_triaevum_work/kokiri-material-6CD66D0B74E16AEA/realtime-probe/`;
this GPU smoke requires the external title/checkpoint and is not a standalone
unit test.

### Optional Scene Environment Profiles (2026-09-23)

Release defaults (2026-10-12): a package ships read-only profiles in
`resources/environments/` (source `runtime/triaevum_resources/environments/`,
release role `default_environment_profile`). A slot resolves the user's scene
file, the release's scene file, the user's group standard, the release's group
standard, then the base profile; F1 shows the release's files as "TriAevum
default" and always saves to the user's directory. "Use group standard"
removes the user's scene file, so a scene the release profiles returns to that
profile.

`oot3d_scene_environment` owns authored lighting profiles in the OOT3D adapter.
The key is the native scene ID read from `PlayState + 0x104`, not a room,
texture hash, actor, or renderer-generated identifier. Kokiri Forest is scene
`0x0055`, associated with `spot04_info.zsi`; all its rooms share one profile.
No ZSI, native asset, translated title logic, or canonical shader is modified.

Profiles live beside the global graphics configuration:
`environments/scene-XXXX.environment.json`, with a four-digit uppercase
hexadecimal scene ID. The minimal complete schema is:

```json
{
  "schema": "triaevum.scene-environment",
  "version": 4,
  "sceneId": 85,
  "enabled": true,
  "preserveSceneBrightness": true,
  "nativeDirectStrength": 0.0,
  "nativeLocalStrength": 0.0,
  "nativeAmbientStrength": 0.0,
  "ambientColor": [1.0, 1.0, 1.0],
  "ambientIntensity": 0.12,
  "fogStrength": 0.35,
  "directionalLights": [
    {"azimuth": 45.0, "elevation": 35.0, "color": [1.0, 0.96, 0.9], "intensity": 1.0},
    {"azimuth": -135.0, "elevation": 30.0, "color": [0.75, 0.85, 1.0], "intensity": 0.0}
  ],
  "pointLights": [
    {"enabled": true, "intensity": 1.0, "radiusScale": 1.0, "colorTint": [1.0, 1.0, 1.0]},
    {"enabled": true, "intensity": 1.0, "radiusScale": 1.0, "colorTint": [1.0, 1.0, 1.0]}
  ]
}
```

Ambient is uniform and has no direction. The first directional light is the
shadow-casting key; the second is an unshadowed fill. Directions point toward
the source in OOT3D's Y-up world: azimuth 0 is +Z, 90 is +X, and elevation 90
is overhead. The adapter resolves these angles before publishing the shared
`PicaEnvironmentLighting` value. Colors accept 0–1, native contribution and
ambient intensity 0–2, light intensity 0–4, and fog contribution 0–1.
Angles accept azimuth -180–180 and elevation -90–90 degrees.
The two point-light groups are Navi, then other fairies. Each has an independent
enable, intensity multiplier 0–8, radius multiplier 0.1–8 and linear RGB tint
0–1. They remain editable and effective without the ambient/directional override.
Version 1 profiles remain readable and acquire the default point-light groups.
Version 3 separates the scene-wide native directional gain (`nativeDirectStrength`)
from other native per-draw direct sources (`nativeLocalStrength`). Versions 1 and
2 initialize both from their old direct gain; explicit saves write version 4.
No existing file is rewritten merely by loading it.
Version 4 adds `preserveSceneBrightness`. New profiles enable it; versions 1–3
retain authored-only brightness until the user enables the new control.
The reference keeps native ambient/material response and authored unlit color
independent of native direct/local/ambient gains. It is not exposure or a lock
on total image luminance: key/fill and point lights can add illumination.
While enabled, it owns the ambient baseline; the saved native ambient gain is
inactive and remains available again when the reference is disabled.


F1 **Environment** is the sole owner of these controls and file operations.
Edits preview immediately; **Save scene environment** writes atomically.
Unsaved edits are discarded on scene change, with an explicit UI warning.
**Reload scene environment** reads manual file edits; there is no file poller.
Missing profiles use the native environment. An invalid new scene profile
cannot inherit the previous scene's light; failed same-scene reloads retain
the last good preview and display the error. Files are limited to 64 KiB and
must match the schema, version, scene identity, finite ranges and versioned
light layout. **Use native environment** remains available after capability loss;
save that disabled state to persist it.

The renderer consumes only the effective snapshot. Native lighting mode,
Authentic and F2 bypass authored lighting without erasing the saved profile.
The lighting model and normal/gloss controls belong to F1 **Lighting**.
Directional-shadow quality belongs only to **Environment**, with global
automatic persistence separate from scene-profile saves.
Per-pixel lighting uses authored diffuse illumination;
PBR uses the normal map and GGX. Both paths retain native material reflectance:
authored ambient uses the material ambient coefficient, and authored direct
uses diffuse, or ambient reflectance for an entirely ambient-only material.
Thus those materials still receive directional light without whitening lit
materials or losing black diffuse channels. Painted lighting in albedo remains.
Fog contribution scales the native fog blend; it does not invent a fog color.
The optional graph still owns map scheduling/resources and scene/UI separation.
Caster admission covers depth-writing perspective geometry; alpha-tested
casters run their native alpha test (see Alpha-Tested Shadow Casters below).
Transparent, non-depth-writing caster support is not claimed.
Surface-lighting admission also requires typed world-layer ownership.
Atmosphere and unknown owners retain native appearance, including sky models
whose attribution travels through a deferred native queue. See
[`TRIAEVUM_SURFACE_LIGHTING_OWNERSHIP.md`](TRIAEVUM_SURFACE_LIGHTING_OWNERSHIP.md)
for the reference policy, queue provenance and current qualification evidence.

Eleven deterministic Kokiri captures at guest frame 23399 passed. Disabled
profiles with different parameters were byte-identical; Native mode with the
profile enabled/disabled was also byte-identical. In Shaded mode, normal 0→1
changed 120,016 pixels and 1→4 changed 207,199. Changing key direction,
ambient-only and fill-only lighting each changed the expected scene output.
After removing the skeleton-only gate, shadows changed 50,022 pixels,
including 6,756 on the far static wall, versus zero there before the fix.
Native-memory RGBA and compared alpha/depth/stencil attachments stayed exact.
The owning caster regression failed before the fix and all 18 scene-frame
tests passed afterward. Foundation and pipeline suites passed 293/293 and
40/40; the shared-renderer boundary covered 78 files. Captures and structural
comparison evidence remain private under `I:/_triaevum_work/scene-environment/`.

Inactive profiles collapse to one neutral rendering snapshot: editing their
stored values cannot reset TAA or shadow histories. The actual-widget fixture
passes 4,516 assertions, including this invariant and persistence failures.
Two further GPU runs exercise the user's combined CACAO/Hi-Z/PBR/shadow
settings and an effects-off baseline at matching 1920×1080 output extent;
native-memory RGBA and compared alpha/depth/stencil remain identical.

#### World-fixed lighting under camera movement

Native CMB matrix-chain labels do not establish world-space ownership. The
captured Kokiri program uploads identity to its post-palette `view` rows;
camera transforms are already folded into the model/bone matrices. Treating
those rows as the camera made authored lighting camera-relative.

Reconstructing a separate camera from the latest PlayState eye/at was also
insufficient: that camera can belong to a newer simulation step than the
geometry being replayed. The failed approach showed approximately 6.5 degrees
of light-direction drift on the same static surfaces during movement.

The OOT3D adapter now captures the native world-to-view and post-view matrices
at command-packet submission, using the same native producers as CMB geometry
and light preparation. Their composed column-major transform belongs to the
immutable `PicaNativeLightBinding.WorldToView`, including atmospheric draw
provenance without changing native light selection. No guest addresses enter
the shared renderer, and no scene/asset-specific anchors are inferred.

Visual sampling interpolates this transform with the same alpha as the
geometry uniforms. Replay format PVRG preserves it; older replay formats have
no fabricated replacement and acquire it when new native commands arrive.
Authored key/fill directions, point positions, shadow receivers and each caster
use the transform paired with their own draw, never `LatestPerspective()`.
Shadow replay retains native vertex inputs and culling while projecting onto
world-fixed virtual pages, not a camera-fitted shadow frustum. Missing transform
data fails closed rather than consulting a live camera or treating a model/bone
palette as a world basis.

Resolved draw-stream schema 3 carries the captured transform and shadow-caster
bounds/content identity to replay.
Perspective payload schema 3 removes the obsolete reconstructed `WorldToView`;
the separate eye/at perspective capability remains available to its existing
consumers. Environment files need no migration; the VSM settings migration is
described below.

Regression coverage exercises immutable native packets after camera changes,
non-commuting post-view transforms, geometry/light agreement at interpolation
endpoints and midpoint, and replay persistence. The earlier directional-light
and shadow-matching tests remain in place.

Six deterministic GPU runs cover stationary Link, walking/turning and a
free-camera orbit, with the authored environment off and on. On 25 matched
static geometries across 10,419 observed draw samples, maximum world-direction
delta falls from `0.11411135` to `9.46e-8` (less than `0.00001` degrees).
Captured transforms also reconstruct the same static world anchors for shadow
receivers/casters. Native-memory RGBA and compared alpha/depth/stencil remain
identical; all three disabled-profile framebuffer captures exactly match the
previous binary. These are renderer captures and geometry-relative diagnostics,
not Windows screenshots or a test against a separately reconstructed camera.
Private evidence: `I:/_triaevum_work/environment-geometry-anchor/verification.json`
and its `verified` / `verified-off` captures.

#### Material reflectance and amplified shadow strength

The authored profile initially replaced material response with unit reflectance.
Captured actor materials use diffuse `127/255`, ambient `0.4`, and a native
TEV RGB gain of 2. Removing their coefficients while retaining TEV overexposed
them. `BuildPicaSurfaceLightingUniforms` now weights authored radiance by those
coefficients once, shared by per-pixel and PBR paths. For zero RGB diffuse,
the material's ambient reflectance supplies the new direct response; this is
material semantics, not an actor, texture, or scene exception.

F1 Environment **Shadow strength** accepts 0–4x through validation, persistence,
shadow planning, receiver packing and the shader. Visibility is
`max(0, 1 - coverage * strength * directFraction)`. Values above 1 amplify
partial occlusion, never negative light; already extinguished direct light
cannot darken further. Existing values through 1 retain their response.
The shadow variant key advances to V3; canonical shader keys are unchanged.

The material regression fails before the fix and passes afterward, covering
reduced/colored reflectance, black channels and ambient-only surfaces.
Six deterministic GPU runs at guest frame 23399 use the current user's frozen
Kokiri settings: before/after with the environment on/off, then shadow strength
0 and 4. The compared static wall is byte-identical; 70,957 changed pixels in
the Link region have mean display luma 116.47 before and 58.92 after.
Strength 0 vs 1 changes 92,524 pixels; 1 vs 4 changes 96,343, with decreasing
mean brightness. Disabled-profile framebuffers are identical.
Native-dependency namespace 1 RGBA and both namespaces' alpha/depth/stencil
remain exact. Captures and `verification.json` stay private under
`I:/_triaevum_work/actor-light-shadow/`.

Pipeline, graphics foundation/persistence and scene-frame suites pass
42/293/18 tests. The real F1 widget fixture passes 4,593 assertions, including
live 4x and zero strength plus capability-safe Off; the shared-renderer boundary
passes for 78 files. No user setting or environment profile is reset.

### Paged Virtual Shadow Maps

Directional shadows are exclusive to **Per-pixel** and **Per-pixel + PBR**.
The stored preference survives Native lighting and the F2 native override, but
their effective snapshots disable shadow execution and receiver instrumentation.
There is no baked/native-lighting multiplier or single-cascade renderer fallback.
The sole F1 surface is **Environment > Directional shadows**; Off remains
selectable when lighting or renderer capabilities cannot support VSM.

The shared `renderer3ds/virtual_shadow_map` owns world-fixed page keys and
residency. Up to ten 8x8 clipmap grids start at half-unit virtual texels and
double scale with distance, covering up to 65,536 native world units. Camera
rotation does not refit the projection; translation scrolls integer page keys.
Float32 view/inverse-view light-direction roundoff does not reset the cache.
Real light changes and incompatible layouts invalidate it.

Two persistent D32 atlases retain static depth and final composed depth.
Their 2048/4096 extents consume 32/128 MiB total per target, with 128-texel pages
and three-texel resident PCF borders. Residency reserves coarse fallback,
prioritizes the receiver LOD rings and their blend regions from near to far,
then spends remaining capacity on additional pages. Surviving selected pages
are protected before eviction/allocation. Under pressure, missing fine pages
fall back to resident coarser levels; the smaller budget can reduce distant
detail. Empty geometric pages require no physical allocation.

Typed CPU caster bounds use the same captured view transform, packed attributes,
fixed/per-instance inputs, position scale and skeleton palette as native replay.
Rigid geometry/pose/raster identities permit reuse; unproven animated content
invalidates its intersected pages. Movement/removal invalidates old footprints
as well as new ones. Failed recording cannot commit a dirty page as valid.
Caster admission covers depth-writing native scene geometry; an alpha-tested
caster keeps its native cutout (see Alpha-Tested Shadow Casters below).
Transparent, non-depth-writing caster support is not claimed.

The compiled graph declares both histories and the outputs of the
`BeforeOpaque` producer (see Current-Sample Shadow Casters below), with
`BeforeUi` as its late anchor, and owns attachment, transfer and receiver
transitions. Receivers sample the map composed from their own sample's
casters; the update preserves native draw order. The Grass insertion point is
unchanged; a late update is published for subsequent native draws/frames
rather than moving Grass across transparency.
Dirty static tiles replay static casters. Each dirty final tile first copies
its cached static depth, including PCF borders, then replays current dynamic
casters. Departed actors therefore cannot leave old depth behind.

Shadow keyframes (2026-10-02). A light that moves in every frame (animated
time of day) is followed by up to three layouts per target, each with its own
static atlas, residency and region of the composed atlas (one region for a
still light, three side by side while the light moves; 96 MiB at 2048 against
32). Two layouts are displayed and dissolve into each other over a whole
number of frames; the third is constructed at the light's current direction,
at most 16 to 64 static pages per frame, and composed as it is drawn, so a
rotation moves no page. `renderer3ds/virtual_shadow_keyframes.h` decides
(still light, construction, rotation, cut on a new continuity epoch or layout
geometry); the pass owns the atlases. The receiver block holds both layouts,
the weight and the atlas offsets; the second layout is read, uploaded and
bound only while the dissolve runs, and a fragment looks its visibility up
once however many lighting terms ask for it. A device whose largest image is
narrower than three regions keeps one layout and relayouts in steps of 0.25
degrees. Design and measurements: `TRIAEVUM_ANIMATED_LIGHTING_STRATEGY.md`,
section 5.5 and phase P2.
The per-draw page table is immutable. Receivers sample only the composed atlas,
using 1/4/9 gather operations for
PCF radii 0/1/2 and a narrow continuous LOD transition. NRI's bottom-left
viewport flag produces positive Vulkan viewport height: physical Y must not
be mirrored. Scene/UI composition and canonical shaders remain unchanged.

Graphics settings schema 12 migrates legacy `SingleCascade` to
`VirtualShadowMaps`, retaining Off, strength, PCF and bias choices. Atlas budgets
below 2048 become 2048. The old camera-fitted distance is replaced by the
65,536-unit VSM range; obsolete `DepthPadding` and `Stabilize` fields are removed.
Scene-environment files keep their existing schema and ownership.

Verification uses the frozen Kokiri checkpoint on RTX 3060 at 2560x1440,
Original30, atlas 4096, PCF 2 and strength 2. Across 238 steady samples, GPU mean
falls from **14.514 to 13.339 ms** (8.1%), GPU p95 from **15.439 to 13.984 ms**,
and CPU mean from **29.923 to 28.181 ms**. The corrected moving-camera run
averages **13.342 ms GPU**, with 24–62 dirty pages and at least 306 reused pages
per measured frame; no global residency reset or unmapped page occurs at 4096.
The 2048 run averages 13.319 ms GPU and reuses about 96% of its 225 resident
pages. These are scene-specific measurements, not a cross-device guarantee.

The final cleaned-binary repeat produces a byte-identical framebuffer and
identical page counters, but measures **14.088 ms GPU mean** (2.9% below the
baseline), **15.625 ms GPU p95** and **30.841 ms CPU mean**. Timing variation
therefore precludes claiming a uniform 8.1% gain, a CPU improvement, or a
tail-latency improvement. `clean-verification.json` records the final executable
hash, all targeted regression results and the repeated Native/PerPixel checks.

A 30-frame renderer capture sequence verifies camera-relative motion without
world-shadow jumps or mirrored-page bands. Native lighting with the stored
shadow preference On/Off yields byte-identical framebuffers and zero shadow
passes/atlas allocation. Structural diagnostics report zero failed passes,
missing reads and Vulkan/NRI validation errors. Cache-versus-full-redraw at
2048 differs on 178 of 3,686,400 pixels, maximum 9/255; the conservative rigid
pose quantization is therefore not claimed to be byte-exact.
CPU regressions cover fixed skinned inputs, world-page retention, uncommitted
updates and near/far coverage under physical exhaustion. Actual F1 widget smoke
passes 4,676 assertions. Private evidence:
`I:/_triaevum_work/virtual-shadow-maps/accepted-verification.json`.
Earlier captures preceding `corrected-still` are diagnostic iterations, not
visual acceptance evidence; the old `baseline-orbit` was stationary.

### Captured Grass Lighting and Shadow Precision (2026-09-24)

Grass now derives its model/world transform and native light directions from
the view matrix captured with the draw, including camera roll, rather than
reconstructing a world-up basis from the live eye/at singleton.
`PicaResolvedDrawStream` schema 4 and `PicaSceneFrame` schema 11 retain a
validated GPU slice for the draw's effective surface-lighting uniforms.
The shared surface-lighting atlas consumes that slice and the structurally
recovered surface program in Per-pixel/PBR modes. Native continues to replay
the canonical vertex lighting. That change removed the native-only Grass
lighting path under an authored Environment profile without adding Grass VSM
receivers, changing caster admission, or moving composition anchors. Grid-based
Grass reception is described below.

PCF comparisons now evaluate receiver-plane depth at each gathered texel.
Derivatives are taken in view space and transformed by the linear
view-to-light matrix before normalized-depth translation; differentiating
values near 0.5 introduced a visible precision grid on close geometry.
A two-ULP D32/quarter-virtual-texel minimum allowance supplements the stored
bias, including when the user bias is zero. The hair comparison retains
world shadows without the previous dark striping or the diagnostic iteration's
precision grid.

PICA draw receivers (2026-10-09) no longer pass a receiver-plane slope; the
grid-based Grass receiver keeps its own. The derivatives of a flat triangle are
constant across it and change at its edges. On smooth-shaded low-poly actors
the filtered shadows therefore showed the triangles they fell on, as facets
with hard edges along the mesh. The depth bias, its two-ULP minimum and the
normal bias now cover the filter footprint. Two other corrections, tried and
rejected on Link's savestate, did not remove the facets:
- moving the lookup point onto the surface implied by the vertex normals
  (Hanika's shadow terminator offset, Phong tessellation);
- drawing the casters' back faces.
In interleaved same-pose 8-bit captures, the slope-free filter ranked best, tied
with Phong plus slope-free. It was chosen because it needs no
fragment-barycentric support. The game-camera ratio map against shadows off
shows no new acne on the platform, tree, cliffs or fences.

CPU caster bounds resolve packed attribute streams once per draw and compose
each used palette matrix once, retaining native post-skin homogeneous W even
when weights do not sum to one. Grass selection emits directly during stable
tree traversal and stops at the budget; bounded stable occupancy counting
replaces repeated range sorting without changing roots, density or topology.
Surface/PBR shaders skip native direct-light work when its contribution is
zero, and skip the native light loop when both native contributions are zero.

Verification uses the newly saved live checkpoint, original quality settings,
4096 VSM, PCF 2 and strength 4. All 295 foundation, 40 shader-pipeline,
18 scene-frame and 18 shared-capability tests pass, alongside cluster
invariants, native frontend/presentation checks and the 82-file boundary audit.
Native with Grass disabled is byte-identical to the previous executable and
allocates no VSM pages. Native Grass and Per-pixel/PBR replay successfully;
Vulkan validation reports no errors. Thirty-six framebuffer captures over
600 orbit frames retain all requested surface-lighting sources and report
no unmapped/evicted VSM pages, failed graph passes or missing resources.
An Environment blackout also extinguishes Grass while leaving UI unaffected.

Performance gains are **modest**, not a substantial FPS improvement.
Two balanced before/after moving-camera pairs at AboveNormal benchmark-process
priority average 40.625 -> 41.137 native throughput fps (+1.3%); Grass CPU
preparation averages 2.876 -> 2.685 ms (-6.7%). Total GPU time is
13.778 -> 13.887 ms, not an improvement. Whole-process CPU time, including
startup and warmup, rises 24.016 -> 26.344 seconds.
Normal-priority paired runs regress from 38.984 -> 35.572 fps static and
24.950 -> 24.515 fps moving; earlier measurements were much faster.
Three concurrent Node processes each occupied one logical CPU, and about
9.2 GiB of GPU memory remained occupied with the game closed. These observations
do not establish exclusive causation or justify discarding slower runs.
No large FPS gain, 60-fps guarantee, or overall CPU reduction is claimed.
Original30, VSync and user quality settings are restored for the live session;
AboveNormal priority is confined to the explicitly labelled benchmark runs.
Private evidence: `I:/_triaevum_work/vsm-stability/accepted-verification.json`.

### Grass VSM Reception on the Texture-Colour Grid

Grass reuses the existing 4x4 texture-colour stencil: the same two nearest nodes
and blend weight, not a second bilinear grid. Spatial identity includes the
source triangle and unwrapped UV occurrence within each transformed placement.
Repeats, mirrored tiles and overlapping UV islands therefore cannot share an
unrelated world-space receiver. Nodes outside the triangle project onto its UV
boundary; degenerate UV triangles use their real geometric centroid. Sampling
uses the native surface plane, not the cosmetic Grass normal offset.

Immutable points and root references belong to the placement cache and its
memory accounting. A private compute pass evaluates VSM visibility once per
shared spatial node, using the same page lookup, coarse fallback, PCF, receiver
plane bias and distance fade as native receivers. Strength and clamping apply
at the nodes before interpolation, preserving the colour-grid gradients even
above strength one. Roots consume two visibility values and their existing
blend weight; they do not sample the VSM atlas. Static GPU data uploads only
when placement membership changes; per-invocation uniforms remain dynamic.

The optional surface-lighting atlas retains appearance, unclamped lighting and
the selected directional coefficient. Grass attenuates only that coefficient,
then applies the original absolute value, tint and clamp rules before native
vertex interpolation. Ambient, point lights and Environment fill are not
multiplied by shadow visibility. The captured view transform resolves native
light directions; an authored key is matched in the same world-space contract.

The compiled graph declares the history read at the existing BeforeTransparent
Grass anchor. The OOT3D adapter supplies the coherent target-scoped snapshot;
private buffers and descriptor sets are isolated by in-flight frame and graph
invocation. Atlas transitions remain graph-owned. No caster admission, native
ordering, canonical native shader or UI composition changes are introduced.
Off, zero strength, Native lighting, disabled Grass ReceiveLighting and absent
history do not execute the grid path. Existing settings retain their owners;
there is no additional F1 control.

At 2560x1440 on the frozen Kokiri checkpoint, 7,590 shared samples serve the
170,401-blade Grass draw counters, with 28 draws unchanged. Steady static uploads
are zero; dynamic uploads add one 2,848-byte receiver block. Deterministic
comparisons retain byte-identical Off, zero-strength, Native, unlit and
missing-history framebuffers. The enabled comparison changes 37,883 pixels,
all darker, confined to Grass in the inspected difference image.

Verification passes 293 foundation, 47 effect-graph, 42 shader-pipeline and
19 scene-frame tests, plus 24 existing and 10 coefficient/recomposition GPU
cases. The 64 fixed pass artifacts include the grid compute and surface-lighting
variants; canonical, temporal, toon and pass artifact checks pass. The corrected
2048-atlas orbit records 180 active camera calls with coherent Grass history;
all acceptance captures report zero Vulkan/NRI errors, failed graph passes,
missing reads or native composition mismatches. The earlier `orbit-2048` probe
had free camera disabled and is not moving-camera evidence.
Both the retained baseline and the candidate report 11 Vulkan warnings and no
NRI warnings. At 2048, physical budget pressure leaves up to 146 fine pages
unmapped and evicts up to 35 pages; the shared receiver retains native coarse
fallback rather than claiming full fine-page residency.

A same-executable ABBA comparison isolates Grass reception while global VSM
remains active: RTX 3060, 2560x1440, atlas 4096, PCF 2, strength 4, normal process
priority, validation and Grass logging disabled, 60 warmup plus 360 timed native
frames per run (358 valid GPU timestamp samples). GPU mean is **8.377 -> 8.879 ms**
(+0.502 ms), with Grass **1.431 -> 1.854 ms** (+0.423 ms). GPU p95 is
8.953/9.002 ms in the controls and 9.518/9.496 ms with reception. Uncapped native
throughput is **43.061 -> 39.858 fps (-7.44%)**; individual controls range from
41.794 to 44.407 fps. This is a measured cost, not a free effect or a claim of
unchanged CPU throughput. The private control was removed after measurement.
Results and exact invocations: `I:/_triaevum_work/grass-vsm-grid/performance.json`.
The rebuilt binary without that control produces a byte-identical enabled
framebuffer; its executable hash is recorded in the verification receipt.

The base colour grid intentionally trades per-root shadow edges for spatially
coarse detail. The bounded actor-local refinement below improves nearby detail
without changing that base grid. Private evidence for the original coarse path:
`I:/_triaevum_work/grass-vsm-grid/verification.json`.

### Static VSM Cache and Local Actor Grass Refinement

Static and final page signatures are independent. Actor animation or movement
can invalidate final pages without re-rasterizing unchanged world geometry.
Eviction, genuine light/layout changes and failed recording retain conservative
invalidation; dirty pages become reusable only after successful recording.
Native actor ownership comes from the title adapter's nested Actor_Draw scope
and survives deferred model queues. The reusable renderer consumes typed actor
identity and captured geometry/pose bounds, not title addresses, texture names,
collision-cylinder shadow proxies or per-scene exceptions.

Grass keeps its existing two-nearest-node colour/shadow grid. Immutable native
triangles and their real world-space planes additionally support local grids
around Link and the nearest native Epona. Child Epona and the adult Epona
variant are identified in the adapter; other horse variants do not acquire
that role. Current target-scoped caster bounds project along the actual light
direction onto admitted native receiver triangles.

Local nodes use a world-fixed four-unit lattice with an eight-unit transition
back to the coarse result. Selection is bounded to 128 native-surface patches
per actor and 4,096 additional shared samples per graph invocation; spacing
coarsens by powers of two when necessary. Contained rider/horse grids share
sample storage without merging their independent transition footprints.
The planner traverses retained native surfaces, not individual Grass roots.
GPU sampling clamps to the actual triangle boundary, including sloped and
stacked surfaces, rather than extrapolating an invented ground plane.

The compute pass applies the existing page lookup, PCF, bias and strength rules
once per shared node. Grass vertices interpolate local visibility and blend
back to the coarse result; they do not read VSM depth per root or apply a second
shadow multiplier. Stale history does not supply current-pose refinement.
Buffers and descriptor sets remain invocation/fence scoped. Native draw order,
caster admission, ambient/point-light terms, UI composition and settings
ownership are unchanged. There is no additional F1 control.

On the deterministic 2560x1440 Kokiri capture, cache separation alone preserves
the baseline framebuffer byte-for-byte. Local refinement changes 15,851 of
3,686,400 pixels, confined to the inspected Grass region around Link.
At frame 60 it adds 361 samples to the existing 7,590-node grid for 170,401
visible blades. Halving density to 512 produces 85,916 visible blades while
retaining the same 361 local samples. A native-input walk changes 12,115 pixels
against its matching baseline, confined to Link's final local Grass region;
the old location and the rest of the framebuffer remain identical.
Off remains byte-identical and allocates zero VSM atlas bytes.
These captures report zero Vulkan/NRI validation errors and no failed graph
passes or missing resources. The nested rider/horse regression reproduces
646 redundant samples before sharing and 525 afterward.

A private native Ranch fixture additionally verifies child Epona in view with
Link and Malon, including native movement/camera input and animated poses.
At frame 60, six local patches use 1,104 additional samples for 44,601 visible
blades. Against the retained baseline, the framed comparison changes only 660
pixels on the inspected blade beneath Epona's rear hoof; the rest of the
framebuffer is byte-identical. Fixture setup uses native story state and
coherent saved player positions, not runtime scene corrections. Its two
native Grass source selections remain private; user settings are untouched.
This is child-Epona runtime evidence, not a mounted adult playthrough.

Verification passes 296 foundation, 47 graph, 19 scene-frame and 42 shader-cache
tests, native composition and visual-savestate checks, 34 lighting/shadow GPU
cases, and all four shader artifact families. The F1 atlas labels now include
both depth images; the actual-widget smoke passes 6,648 assertions.
The final formatted/rebuilt executable preserves the approved enabled Kokiri
framebuffer byte-for-byte. Test receipts are retained alongside the captures.

A retained-executable ABBA comparison measures the complete change on RTX 3060:
2560x1440, 4096 atlas, PCF 2, strength 4, density 1024, normal priority,
validation/Grass logging disabled, 60 warmup plus 360 timed native frames
(358 completed GPU timestamps per run). Mean GPU time is **8.710 -> 8.971 ms**
(+0.261 ms); Grass is **1.730 -> 1.806 ms** (+0.076 ms). GPU p95 is
9.693/9.683 ms in the baselines and 9.923/9.757 ms in the candidate runs.
Average uncapped throughput is **36.156 -> 37.468 fps**, but the baselines span
32.499–39.813 fps versus 37.232–37.704 fps for the candidate. That spread does
not establish a reliable FPS gain, and neither GPU nor tail latency improves.
The measured host-frame means are 27.944 and 26.690 ms, respectively.
Steady candidate samples re-use all 371 static pages with no static refresh;
up to 48 final pages are recomposed. The explicit memory tradeoff is
**64 -> 128 MiB** of atlas depth at 4096 per target.
Exact invocations, executable hashes and per-run timing distributions:
`I:/_triaevum_work/actor-shadow-refinement/performance.json`.

Private deterministic captures, exact invocations and structural counters:
`I:/_triaevum_work/actor-shadow-refinement/verification.json`.

### Current-Sample Shadow Casters (2026-10-01)

Receivers drawn before the old `AfterOpaque` update sampled the previous
sample's map, so moving actors were shadowed by their previous pose (patches
under Link's cap while walking). The map is now complete before the first
opaque receiver of its own sample.

- **Preparation.** After publishing a presentation sequence, the scheduler
  hands the backend the draws it selects from the published metadata
  (`WantsPicaSequenceDrawPreparation`: opaque-world scene draws while
  directional shadows are enabled), then executes the sequence in native
  order. Preparing never rasterizes, consumes the sequence or replaces the
  ordered submission; native-memory execution never prepares.
- **Prepared casters.** Preparation applies the ordered submission's caster
  admission, places the geometry with the same helper as the ordered draw
  (per-identity buffer or transient arena; a transient caster that does not
  fit stays out of the map instead of exhausting the arena), uploads the vertex
  uniforms and records the caster in its own `PicaSceneFrame`. Only what the
  map consumes is recorded: canonical programs stand in for the effective
  ones; no fragment state, texture or resolved raster/target value is
  invented. The ordered submission reuses the prepared bounds instead of
  transforming skinned vertices twice.
- **Graph and anchors.** The graph imports the result as
  `PreparedShadowCasters` (external, semantic). `DirectionalShadowMap` runs at
  `BeforeOpaque` and reads it instead of the scene frame; `BeforeUi` remains
  its late anchor. A fallback producer may start at the opaque boundary
  because it reads only imported data. The composition schedule publishes
  `BeforeOpaque` for every target with opaque world draws and known layers:
  the first opaque draw needs no contiguity, so sequences where Navi's
  transparent draws interleave the opaque world keep the early update while
  the later stage anchors stay withheld.
- **Alpha test.** The native runtime now declares the PICA alpha test
  (register `0x104`) in the draw view. Distance-field geometry excludes
  alpha-cutout draws; before, the flag was never set and cutouts entered both
  the map and the field as solid quads. Shadow casters keep their cutout
  through the alpha-tested caster program below. With MSAA the existing
  alpha-to-coverage policy now applies to those draws; at 1x nothing changes.
  Pipeline manifest keys include the flag, so a manifest recorded earlier misses
  alpha-tested pipelines once.
- **Diagnostics.** `nri_directional_shadow_prepared_draw_count`,
  `_caster_count`, `_transient_count`, `_reuse_count`, `_map_count` and
  `_map_before_opaque_count`; the scheduler reports `prepared_draws` and
  `draw_preparation_seconds`.
- **Verification.** Reference Kokiri walk (GI off, the user's shadow
  settings, interpolated presentation, RTX 3060 at 2560x1440, 420 frames with
  Navi in view): all 417 map updates run at `BeforeOpaque`; every eligible
  receiver binds a map (23,344; the previous build left 45 unbound); 44.1
  casters per frame are prepared and all are reused by their ordered
  submission, none from the transient arena. The cap patches are gone: at
  frame 210 the cap is uniformly lit (7,509 brighter and 129 darker pixels in
  its region). Excluding cutouts lowers the queued casters from 53.1 to 44.1
  per frame; their rectangular shadows (for example on a hedge under its
  grass ledge) disappear. A repeated run is byte-identical (12/12 captures).
  Vulkan and NRI validation report no error, and no pass fails or misses a
  read. Preparation takes 0.33 s over the run (about 0.8 ms per frame for 53.7
  handed-over draws, including the bounds the ordered draw no longer
  computes); GPU frame time shows no change beyond run-to-run noise.
  Receipts: `I:/_triaevum_work/perf-levers/qualify/shadow-prepass/`.

### Alpha-Tested Shadow Casters (2026-10-01)

An alpha-tested caster (foliage, grass tufts, fences, hedges) casts the shape
its native draw shows: the map runs the draw's own canonical fragment program,
with the same textures and fragment uniforms, up to the native alpha test.

- **Admission.** A depth- and colour-writing draw whose alpha test can discard
  (compare other than "Always") is a caster when the map can run that test:
  the frontend published its fragment hooks (`AfterNativeTev`, `BeforeDepth`),
  the packed fragment uniforms are complete and every texture is sampled
  directly (no packed Shadow2D image, no GPU-produced copy, which may not exist
  yet when the map runs). A test that never passes casts nothing. "Always"
  remains a depth-only caster.
- **Preparation.** The scheduler hands an alpha-tested draw over with its
  fragment uniforms and textures; other draws still carry the vertex side
  only. The backend resolves the textures and the native lighting LUT through
  the ordered draw's cache (custom texture replacements included), uploads the
  fragment uniforms next to the vertex uniforms (both blocks reserved before
  either upload, since growth replaces the arena) and records them with the
  caster. A texture that cannot be resolved leaves the draw out of the map.
- **Records.** `PicaSceneShaderProgram` retains the fragment program's
  published hook offsets (`FragmentHooks`), and the resolved draw record a
  `LightingLut` binding (R32Uint, bound only when the program samples it).
  The ordered scene frame publishes both too. Schemas: resolved draw stream 7,
  scene frame 12.
- **Caster program.** `BuildPicaShadowCasterAlphaFragmentShader` keeps the
  canonical program up to its `BeforeDepth` hook, which follows the TEV and
  the alpha test, and closes `main` there: no depth, fog or colour is written,
  only the rasterized light depth. The map has no colour attachment, so the
  native colour output (published at location 0) is declared as a private
  variable instead of an output. Combined samplers are split to the NRI
  contract. The caster transform moved outside the canonical contract (to
  27, then to 30 when point shadows took 27/28), so one layout serves both caster kinds: depth-only casters
  write the vertex uniforms and the transform; alpha-tested casters also the
  textures, samplers (7-9), fragment uniforms (4) and the LUT (13-14). The
  sampler mirrors the ordered draw's: native filters, PICA wrap modes and LOD
  clamp within the uploaded chain. A caster whose program cannot be built is
  dropped from the map instead of failing it, and a discarding alpha test is
  never drawn as a solid caster.
- **Pages.** The cutout is part of the caster's content: its version includes
  the TEV stages and constants, the alpha test, the texture LOD bias, the
  lighting and procedural-texture programs and the sampled contents. Light
  and fog values do not invalidate cached pages.
- **Diagnostics.** `nri_directional_shadow_prepared_alpha_caster_count`,
  `_prepared_alpha_rejected_count` and `nri_directional_shadow_alpha_caster_draw_count`.
- **Verification.** Reference Kokiri walk (420 frames, GI off): 8.95 alpha-tested
  casters prepared per frame, none rejected; queued casters return from 44.1 to
  53.1 per frame and 31.4 caster draws per frame run the alpha test. Static page
  reuse holds (4.54 -> 4.63 static pages refreshed per frame).
  With strength 1 and PCF 0 the new shadows show the cutouts: blade spikes of a
  grass tuft on a hedge face, fence posts and rails, grass fringes. At the
  user's strength 4 and PCF 2 the filtered partial coverage saturates, so a
  dense tuft reads as an almost solid patch. The real generator family
  compiles the caster program (`build_fragment_artifacts --check`); canonical
  artifacts are unchanged. ABBA timing (360 frames): GPU frame median
  8.28-8.39 ms in all four runs; map recording 0.76-0.77 -> 0.83-0.87 ms CPU
  median; preparation 0.45 -> 0.50 s per run. Alpha caster programs compile
  once per fragment program and persist in the pass shader cache. Vulkan and
  NRI validation report no error and only the 11 startup warnings of the
  baseline (an exported but unattached colour output had added ten), and the
  validated run is byte-identical to an unvalidated one (12/12 captures).
  Receipts: `I:/_triaevum_work/perf-levers/qualify/shadow-prepass/`
  (`alpha-*`, `crisp-*`, `t-alpha-*`, `crops-alpha/`).

### Point-Light Shadow Cube Maps (2026-10-01)

Dynamic point lights (Navi, fairies, native local point lights) cast shadows
from the same prepared casters as the directional map, and only within their
own reach: a light's far plane is its lighting radius times the shadow reach.

- **Setting.** `Graphics.Effects.PointShadows` (`fast/oot3d/point_shadows.h`):
  Mode `Off | CubeMaps` (default Off), face resolution 256/512/1024 (default
  512), shadowed lights 1-8 (default 4), reach 0.25-1.0 of the light radius
  (default 1.0) and strength 0-1. One owner and one control surface: F1 >
  Environment > Point light shadows. Gated like the directional map: the NRI
  shadow pass capability, Per-pixel or Per-pixel + PBR lighting, forced Off by
  the native fidelity profile, Native lighting and F2, preference preserved.
  Persisted as `PointShadows`; a missing object loads the defaults.
- **Graph.** External `PointShadowHistory` and written `PointShadowMap`
  (ShadowDepth, dispatch DepthAttachment, completion ShaderRead, exported);
  passes `PointShadowMap` (auxiliary output, BeforeOpaque with BeforeUi
  fallback, reads PreparedShadowCasters) and `PointShadowLighting` (lighting
  contributor, reads the history). `PicaAttachmentFeatureRequests.PointShadows`
  requests them; Off compiles neither pass, so no map is scheduled and no
  receiver requests the feature. The owned-draw layout always carries 27/28
  (fallback texture when Off) and surface lighting always calls the stub,
  which returns exactly 1.
- **Lights.** Prepared and ordered draw records carry the lights their
  lighting sees (`ScenePoints`, `NativeLightSources`; resolved draw stream 8,
  scene frame 13). Scene points use the group's enable, intensity and radius
  scale; native point sources are skipped when the scene environment removes
  native local light, and a native source that is also a scene point counts
  once. Selection keeps at most N lights, nearest first by the camera's
  distance to the light's reach, then brighter first; a light that stays
  selected keeps its atlas row.
- **Atlas.** One 2D D32 atlas per target (namespace + colour address): a row
  per light, six face tiles per row. A tile is the face plus a two-texel
  border rendered with the same projection (tangent half-extent = tile/face),
  so the receiver's 3x3 gather never leaves its tile. Face basis, matrices and
  receiver share one table (`renderer3ds/point_shadow_map.h`). Depth is
  perspective, Vulkan 0..1, near plane = far/64. A row renders again only when
  its light, reach or a caster in reach changed (dynamic or unversioned casters
  always re-render it); its six tiles are cleared first.
- **Casters.** The directional caster programs plus a point vertex variant
  whose face transform is a 64-byte root constant; alpha-tested casters keep
  their cutout. Culling: sphere(light, far) and the face frustum. Cull mode
  none, slope bias 2. The caster transform block moved to binding 30, outside
  the PICA contract (0-28). The per-frame caster descriptor pool covers both
  the directional and the point bindings.
- **Receivers.** NRI-owned scene draws with full scene lighting and native
  per-pixel lighting request `PointShadowLighting`. Rows of the published
  history are matched to the draw's lights by source identity (surface light
  `Diffuse.w`, scene point `Color.w`; negative = none), with
  `point_shadow` (reach, strength, face texels, border) and
  `point_shadow_atlas` (1/width, 1/height, near fraction, enabled) appended to
  the surface-lighting block (1488 bytes). `pica_point_shadow` selects the
  face, applies a 1.5-texel receiver offset, filters 3x3 (as VSM PCF 1) and
  fades out over the last tenth of the reach. The atlas is bound at 27/28
  (`PointShadowSampler` NRI extension); draws without the feature bind the
  fallback texture and read a stub that returns 1. Native PBR fragment-light
  units and grass are not shadowed by point lights.
- **Diagnostics.** `nri_point_shadow_map_attempt_count`, `_map_count`,
  `_candidate_light_count`, `_light_count`, `_rendered_light_count`,
  `_caster_draw_count`, `_alpha_caster_draw_count`, `_receiver_draw_count`,
  `_receiver_bound_draw_count`.
- **Verification.** Reference Kokiri walk (420 frames, GI off, VSM at the
  user's settings, scene points enabled, native local light 0). Off: 12/12
  captures byte-identical to the build before this change. On (512, 4 lights,
  reach 1, strength 1): 3.97 lights per frame from 5.68 candidates (Navi and
  the Kokiri fairies), 3.97 rows rendered per frame (moving lights rarely reuse
  a row), 315 caster draws per frame of which 99 run the alpha test, 76 of 94
  requesting receivers bound. Link and the Kokiri cast soft shadows on the
  ground from Navi and their fairies; the differences on the bodies follow the
  geometry, without acne. Vulkan and NRI validation: no error, only the 11
  startup warnings of the baseline; validated and plain runs are
  byte-identical (12/12). ABBA timing (360 frames, medians): GPU frame
  6.54/6.41 -> 6.97/6.96 ms; shadow anchor CPU 0.41/0.44 -> 0.64/0.68 ms.
  Receipts: `I:/_triaevum_work/perf-levers/qualify/shadow-prepass/`
  (`pt-*`, `t-pt-*`, `crops-pt/`).

## Per-pixel actor lighting and native UI boundaries (2026-09-24)

- Native projected actor shadows are a typed composition role, not a texture,
  blend-state or scene heuristic. The OOT3D adapter identifies the native blob
  and both foot-model producers at model queue submission. Presentation omits
  that role only in Per-pixel and Per-pixel + PBR. Native, Authentic, F2 and
  native-memory execution retain their canonical draws. The player's separate
  mirrored-model route is not classified as a blob shadow.
- Navi and fairy lights come from immutable native-frame snapshots, with stable
  source identities and a maximum of 32 records. The adapter rebuilds ownership
  from the live actor lists and native LightNodes after restore; stale or recycled
  nodes cannot inherit a fairy role. Renderer code contains no title addresses.
  Active native light records retain their position, color and radius.
  NPC fairies with dormant native records instead use the live `En_Elf` glow's
  world position, inner color and opacity, subject to its native visibility gates.
  Their title-owned default reach is 100 world units, scaled by Environment.
- Both per-pixel models remove the corresponding selected native vertex-light
  direct terms even when a point group is Off, preserving the independent native
  ambient term. They evaluate the replacement points per fragment in
  world-distance units; PBR uses its GGX response. Group controls do not depend
  on enabling authored ambient/directional lighting. Replay V19 retains the
  snapshots, projected-shadow role and native actor ownership; V18, V17 and V16
  remain readable. Visual sampling interpolates matching source identities
  without inventing emitter lifetimes.
- Surface normals transform each authored normal by each bone's inverse
  transpose before applying native weights, then transform to view space.
  Inverting the blended bone matrix caused singularities during animation.
  An unavailable surface direction stays zero rather than reading a stale
  native temporary. Positions, UVs, alpha and authored hard edges are unchanged.
- A restored shader may first enter the cache through legacy source inference.
  A later valid typed frontend contract supersedes that inference after checking
  the native output interface. Its new identity rebuilds derived variants while
  retaining variants referenced by recorded draws. Without this promotion,
  per-pixel requests could silently keep the native vertex-lighting shader.
- A mixed opaque/transparent scene can still have a truthful completion boundary:
  a scene-only prefix followed exclusively by native UI on the same target.
  The schedule publishes that `BeforeUi` anchor without fabricating an
  `AfterOpaque` anchor or changing native order. Interleaved UI followed by more
  scene draws has no such boundary. Once UI is consumed, the target is no longer
  an eligible unresolved scene input.
- The VSM producer uses that graph-declared `BeforeUi` boundary when
  `AfterOpaque` is absent. This prevents Navi's interleaved transparent draws
  from stopping shadow updates. Only imported caster/view/history data and
  exported shadow atlases participate; no UI or scene-color input is added.
  The 2026-09-26 runtime and regression evidence is recorded in
  [`TRIAEVUM_F2_NATIVE_PRESENTATION.md`](TRIAEVUM_F2_NATIVE_PRESENTATION.md#lighting-and-mixed-scene-shadow-correction--2026-09-26).
- At that anchor, the compiled scene graph resolves into its declared output.
  The native UI consumer commits it by an exact, initialized, same-extent NRI
  image copy. Offscreen composition retains storage orientation; physical
  scanout alone applies display-transfer flips. Native UI writes preserve the
  resolved-scene provenance and its auxiliary bindings, so presentation does
  not run AO again over the HUD.
- Native PICA descriptor storage grows in reusable per-frame pool blocks.
  Growth preserves previously recorded sets rather than failing at a fixed
  draw count. A 431-draw native capture with 64-set initial blocks completed
  without binding errors and matched the 512-set control framebuffer exactly.

Verification uses the user's static fairy checkpoint without gameplay input.
The final shadow trace identifies five projected-shadow draws; their removal
is visible in both per-pixel modes, while Native remains byte-identical.
With authored/native illumination off, the actors are dark; Navi-only,
fairy-only and combined settings produce distinct local illumination.
GPU regressions pass nine animated-normal cases, seven point-light cases and
the native-RGB/particle-coverage checks. Both AO providers execute one NRI-owned
AO pass per active frame with no failed or unresolved graph passes. Zero AO
strength is byte-identical to Off, including the offscreen copy and native UI.
The actual F1 widgets pass 5,022 assertions; a 180-frame run preserves the user's
combined settings and completes without graph failures. These are bounded
correctness checks, not a new performance benchmark or a claim that every
optional pass is admissible on a non-contiguous opaque target.
Private evidence is under `I:/_triaevum_work/surface-lighting/`; no save, capture
or extracted title data belongs in a public package.

## Scene-wide native environment lighting (2026-09-24)

The two native environment directionals are explicit scene sources in Per-pixel
and Per-pixel + PBR, not multipliers on whichever lights an actor selected.
The EUR adapter derives their ownership from `Gameplay_Init` at
`00449218..00449228` and `Environment_Init` at `0044FF38`: the environment is
`PlayState + 3190`, with `LightInfo` records at `+2C` and `+44`.
Only records still linked through that PlayState's `LightContext` are accepted.
Directions are signed bytes pointing toward the source; native CMB assignment
negates and divides by 127 at `0035BDF8..0035BEE4`. Colors come from the live
records, including native environment updates, never static scene-file guesses.

Immutable directional snapshots are independent of moving fairy snapshots.
The captured world/view transform accompanies geometry, replay and interpolation.
Replay V19 retains the snapshot deduplication introduced in V18; sampling pairs
source identities rather than selected slot numbers. Resolved draw-stream
schema 6 retains scene directions and native actor ownership for shadows.

Both native directionals illuminate eligible world and actor surfaces. Matching
selected native direct terms are removed by source identity, without discarding
the shared native ambient term. Other native lights retain their per-draw
assignment and native lighting-enable contract. Ambient-only materials use their
authored ambient reflectance for new direct illumination, as with authored key/fill
and fairy points. Native, Authentic, F2 and native-memory rendering remain canonical.

F1 **Native scene direct contribution** controls these two native directions;
**Native local contribution** controls the remaining native per-draw direct
terms. Added key/fill, ambient and the two fairy groups keep their existing
independent controls. With no authored override, native scene/local gains are one.
An active authored key owns the single shadow map; otherwise the brightest valid
native environment source is selected without averaging opposing directions or
weighting by actor geometry. Existing graph-anchor restrictions still apply:
this does not manufacture an `AfterOpaque` boundary in a mixed target.

Static framebuffer probes at the private fairy checkpoint verify changes on
terrain, tree, Link and Saria in both per-pixel modes. Added key, Navi and other
fairies remain independently observable; 311 opaque heart pixels are unchanged.
Native direct-on and direct-off captures both match the pre-change framebuffer
SHA-256 `e568d7e93b126720171e13202f1aec9b805b97adf2a3e3ef37fc235db074dfe0`.
Fifteen GPU lighting cases cover opposite directions, simultaneous scene/local
gains, source deduplication, preserved ambient and stale slots on unlit materials.
The actual F1 widgets pass 5,077 assertions, including version 1/2 migration
without rewriting files. Native capture, replay, interpolation, 19 scene-frame,
41 shader-cache, 292 foundation and 47 graph tests pass. No graph pass failed or
remained unresolved in the bounded captures. Evidence is private under
`I:/_triaevum_work/native-scene-lighting/`; no gameplay input was injected.

### Screen-Space Contact Shadows (2026-10-09)

The directional shadow light can add Godot 4.8's screen-space contact shadows
(pull request 118045, after Bend Studio's screen-space shadows): small-scale
shadows the paged map misses, combined with it by `min()` as Godot does. They
are off by default and exist only with virtual shadow maps.

- **Controls.** `DirectionalShadowSettings.ContactShadows`, persisted as the
  optional `ContactShadows` object and edited under F1 Shadows: enable, length
  (Godot's Short/Normal/Long, 2-4 reads of 64 samples), surface thickness
  (0.001-0.1, default 0.01), opacity and blur (Godot's per-light values, here
  of the one shadow light) and half resolution (default on; see cost).
- **Camera depth.** Godot traces its depth prepass. This renderer has no
  completed scene depth before opaque shading, so the shadow pass draws its
  prepared casters once more with the native camera: reverse-Z depth
  `near / w` (near 1), both faces, no bias, into a private D32 at the target's
  extent (half width and height by default). Only eligible shadow casters are in
  it, so other surfaces neither receive nor occlude contact shadows.
- **Rays.** `contact_shadow_shader.cpp` is Godot's compute shader, with the
  depth texture and sampler split (bindings 0 and 2), the R8 visibility at 1 and
  a far-plane write of 1 where Godot clears the image white.
  `ResolveContactShadowDispatch` is Godot's light-coordinate and group-bound
  math (converging, diverging and parallel rays). The noise phase is the
  temporal jitter sequence, 0 without temporal accumulation.
- **Graph and receivers.** `EffectResource::ContactShadowMap` is written by the
  directional shadow pass and exported as `ContactShadowHistory`; the camera depth
  is private to the pass, like the atlas pages. Receivers bind the visibility at
  set 0 bindings 29/30 and project their view position with
  `contact_projection`. They read it only in the frame that produced it, scaled
  by the light's shadow strength; otherwise, and outside the screen, they read 1.
  Contact shadows run for one shadow target per frame, within the caster arena's
  reserve. Missing R8 storage support or a failed contact resource leaves the
  shadow map working alone.
- **Cost.** In the Kokiri lab (2160x3840 target, Normal length, three
  interleaved rounds of 600 frames), full resolution costs 0.64 ms of camera
  depth and 1.42 ms of compute; half resolution costs 0.20 + 0.31 ms. GPU busy
  time rises by 0.8 ms at half resolution and 2.4 ms at full. At half resolution
  a one-pixel halo can follow strong depth edges.
- **Evidence.** Unit tests cover the reverse-Z projection, the dispatch cases,
  parameter clamps and the three kernels' compilation. The kernels are in the
  built-in pass catalogue (85 artifacts). A depth-debug capture matched the
  geometry, and the contact ratio maps show the fence posts', grass blades' and
  Link's feet shadows. Evidence is private under `I:/_triaevum_work/contact-shadows/`.

### Selected-tree commit qualification

The lighting and canonical-copy closure was also exported from the Git index and
built independently, excluding the unrelated AO, grass, audio and AOT memory-region
changes. This selected Windows tree passes 294 foundation, 47 graph, 41 shader-cache,
19 scene-frame and 8 pipeline-manifest tests, plus the native host/memory,
composition, submission, semantic-trace, bridge, plan, visual-savestate,
savestate and Native-policy executables. The real F1 widgets pass 4,807 assertions.
The product-only configuration lacks `oot3d_native_runtime_host.lib` required by
the aggregate native-game test target; its affected scene-view tests pass through
a focused runner linked to the same built objects. No global-suite pass is claimed.
The rebuilt synthetic title DLL passes all eight ABI checks, including compiled
deferred reads and repeated same-value writes. The staged GPU probes pass 15
lighting cases, nine normal cases and 61,440 copy pixels; all 57 selected built-in
shader artifacts match their sources.

Independent grass changes produce different placement counts, so the cross-tree
framebuffer comparison disables grass only in private verification profiles.
Native output is byte-identical between the verified full-tree executable and
the selected-tree executable, and between scene-direct Off and On:
`f356543d2893028f68cfc8bd1eb24159643ba516799bf205c1e00d75dffe0859`.
Both per-pixel models change terrain, tree, Link and Saria while preserving the
sampled opaque heart interiors. All seven bounded runs complete without failed,
unresolved or missing-read graph work. Receipts, invocations, hashes and captures
remain private under `native-scene-lighting/staged-*-proof`; user profiles,
checkpoints and public packages are unchanged.

The private native-copy checkpoint also passes 180 fixed-delta frames in Native,
Per-pixel and Per-pixel+PBR with Toon/outline retained. Each run records 108
deferred copies, 216 GPU bindings, one final materialization and zero prefix
waits. Vulkan and NRI validation are enabled: zero errors, zero NRI warnings,
11 unsuppressed Vulkan warnings. This smoke first exposed missing scanout guide
transitions and descriptor mutation across repeated composite invocations;
the graph-declared reads now always receive their barriers, and each composite
invocation retains its own descriptor set until the frame slot can be reused.
Guest memory content, memory state and process fingerprints match across all
three models (`12296319316116986855`, `539994612170039003`,
`3145733879543920988`). Evidence is in `staged-copy*-proof` and
`copy-memory-proof.json`; the failed reproduction remains in `staged-copy-before`.

## Multiscale GTAO and reflection performance

The selector retains both providers. Multiscale GTAO uses shared kernels in
`fast/renderer/gtao_shader.{h,cpp}` and the OOT3D/NRI adapter in
`fast/oot3d/ambient_occlusion_pass.{h,cpp}`. Original FidelityFX CACAO uses
`fast/oot3d/cacao_pass.{h,cpp}` and upstream revision
`0ddca95e6714727a252ead345591ca8f2598f261`, with its MIT notice retained.
The following estimator and performance details describe GTAO:

- The graph declares native depth, the coverage-bearing normal guide and the
  native scene view. It owns the published R32F AO image, scheduling and
  external resource transitions. The provider owns only its half-resolution
  RG16F scratch and its internal transitions.
- Horizon integration operates at half extent in each axis, using original
  FP32 depth rather than a half-precision depth reconstruction. Low uses
  12 depth samples and Medium 24 per horizon pixel. Both use native normals,
  the canonical negative-Z camera projection and explicit W-buffer handling.
- Fine and broad occlusion share the samples. The broad control weights the
  incremental broad contribution, not a second copy of fine occlusion.
  A tiled bilateral resolve reconstructs the output. There is no AO history,
  frame-index rotation or per-frame uniform-buffer ring.
- Native RGB8 normal decoding has finite angular precision. A bound derived
  from the input format rejects coplanar false horizons. The quantized-normal
  GPU probe reproduced 0.0213 false occlusion on an empty sloped plane before
  this correction and zero afterwards; the permanent GPU regression uses
  actual RGBA8 UNORM input.
- Coverage is valid when alpha is nonzero, including Grass's coverage marker.
  No draw, asset, scene, Grass density, distance or topology is excluded to
  obtain the measured speedup. Composition retains the existing scene-coverage
  weighting of scene RGB before UI; it is not an ambient-light-only decomposition.
- Native overlay coverage follows the native **RGB destination blend factor**,
  not an unconditional `ONE_MINUS_SRC_ALPHA`. The latter erased AO across
  additive particle quads, even where black texels contributed no native RGB.
  This defect also appeared with CACAO. The GPU regression fails before the
  correction and passes afterwards for additive, alpha-over and opaque
  replacement, while checking actual native RGB. Matching saved-scene frames
  384–386 no longer contain the rectangular AO holes; frame 385 restores
  77,027 previously erased AO pixels. Validation reports zero errors.
  This is a reproducible coverage defect, not proof that every intermittent
  artifact in every scene has been eliminated.
  Auxiliary coverage remains scalar, not an exact per-channel decomposition of
  every native RGB blend equation.

F1 **Lighting** owns **Off / Multiscale GTAO / FidelityFX CACAO** and
**Low / Medium** quality. GTAO retains independent **Fine radius / Fine strength**
and **Broad radius / Broad strength**, with fine radius no larger than broad.
CACAO exposes its original radius, strength, detail strength, horizon-angle
threshold and blur-pass controls. Power, clamp, fade and sharpness remain
available with the selected provider's semantics.
Schema 14 preserves saved `CACAO`, `GTAO` and `Off` choices and their numeric
controls. Older profiles without a fine radius acquire one quarter of the
broad radius; CACAO is not forcibly converted to GTAO. The two estimators do
not promise pixel equivalence. Both consume the same corrected particle
coverage and complete before native UI composition.

Hi-Z reflection filtering now shares a 12-by-12 tile for each 8-by-8 workgroup,
retaining the original 5-by-5 filter. Inactive groups and zero-contribution
rough surfaces avoid unnecessary work; mip selection uses integer operations.
The covered reflection-on framebuffer is byte-identical before/after, including
508 pixels with an observable reflection contribution. The covered AO/SSR-off
framebuffers are also byte-identical.

### Measured cost and limits

RTX 3060, 2560-by-1440 output, original saved scene, PBR, 4096 VSM/PCF 2 and
unchanged dense Grass. Four balanced A/B/B/A runs use 600 fixed-1/30 frames each,
discard 60 warmup frames, and disable VSync, capture and validation. AboveNormal
priority applies only to these benchmark processes. The final-binary repeat
below has 0.25% drift between the two baseline GPU-frame medians.

| GPU scope, pooled median | CACAO baseline | GTAO runtime | Change |
| --- | ---: | ---: | ---: |
| AO, selected Low quality | 0.943 ms | 0.801 ms | -15.0% |
| Hi-Z reflections | 1.451 ms | 1.294 ms | -10.8% |
| Complete GPU frame | 11.949 ms | 11.594 ms | -3.0% |

Mean AO cost is 0.972 to 0.824 ms. **No overall FPS improvement is established**:
the final series measures 43.79 to 42.32 fps, with the two unchanged baseline
runs themselves ranging from 45.17 to 42.50 fps. An earlier series measured
40.43 to 41.49 fps. Pre-backend wall time remains around 12–13 ms.
There is no general CPU-improvement or 60-fps-headroom claim.
All runs and outliers remain in the private reports.

An earlier final-binary repeat was **non-stationary**: the unchanged baseline moves from
37.94 to 23.51 fps and from 12.47 to 32.26 ms mean GPU time within that A/B/B/A
series. The two candidate runs reach 23.65 and 23.77 fps. A subsequent idle
measurement reports 10,006 of 12,288 MiB allocated without the game running.
Other user processes were not stopped. Consequently that series is retained,
but its pooled total-frame/FPS deltas are not accepted as an optimization result.
AO's pooled median remains 0.944 to 0.798 ms (-15.5%) in this final-binary check.

Verification includes the native RGBA8 GPU regression, independent bands,
zero identity, coverage, W-buffer, odd extents and identical-input repeatability;
64 native lighting configurations; three 4,096-case TEV comparisons; and
61,440 texture-copy pixels. The 59 built-in pass artifacts regenerate and pass
`--check`. CPU regressions pass 292 foundation, 47 graph, six diagnostics and
40 shader-cache tests. The actual F1 widgets pass 4,805 assertions.
The corrected runtime also covers 40 native captures along a 400-frame camera
orbit, plus the consecutive particle-defect frames 384–386. Live validation
covers six Low/Medium/Off transitions with Hi-Z/TAA (769 frames), SSSR
(336 frames, zero fallback) and Off (273 frames, zero AO/normal-guide passes).
All three validation runs report zero Vulkan/NRI errors. Separate native
captures distinguish fine/broad controls and prove zero-gain/Off identity.

Current bounded runtime tooling is
`python -m tools.triaevum_release.validate_ambient_occlusion_run`; use a
disposable launch profile as well as disposable config/save paths.
Diagnostics use `ambient_occlusion_*`, GPU timing `ambient_occlusion_ms`,
and developer switches `OOT3D_GRAPHICS_AO*`. The live diagnostic sequence is
`TRIAEVUM_AO_DIAGNOSTIC_SEQUENCE`. Legacy CACAO-named diagnostics and control
scripts are replaced by these provider-neutral names. CACAO itself, its
`THREE_DS_RECOMP_ENABLE_CACAO` build option and its shader generator remain
supported alongside GTAO.
Private captures, checkpoints, profiles and measurement reports remain under
`I:/_triaevum_work/ao-stability/` and must never enter a public package.

### iMMERSE MXAO

MXAO from Pascal Gilcher's iMMERSE is a separate module under its own licence,
`runtime/three_ds_recomp/src/fast/immerse/` (see `LICENSE.md` there): it is not
covered by TriAevum's GPL, and it is compiled only with
`THREE_DS_RECOMP_ENABLE_IMMERSE`, into its own shared library
(`triaevum_immerse`) beside the runtime. Its graph contract is GTAO's: native
FP32 depth and the coverage-bearing normal guide in, the published R32F
visibility out, composited before UI. TriAevum reaches it through
`fast/oot3d/mxao_pass.h`, whose implementation loads the library through the
MIT module interface `module_interface/triaevum/immerse_module.h` and serves
it the renderer's NRI device, textures, programs and barriers (the pass's
transitions are recorded as before). Without the library MXAO is unavailable,
and `GraphicsSettingsRuntime::SnapshotForRendering` renders a configuration
that selects it, like an unavailable CACAO, with AO Off. The port, its
options, the temporal mode and their validation are documented in the
module's [README](../runtime/three_ds_recomp/src/fast/immerse/README.md).

### Native AO normals

GTAO and CACAO read their normals from the scene's normal guide, as MXAO does
with its Renderer normal source (its default reconstructs them from depth, see
[MXAO original options](../runtime/three_ds_recomp/src/fast/immerse/README.md#mxao-original-options-and-debug-views)). The guide is fed from native data rather than rebuilt from depth or from
the final image. A port of iMMERSE LAUNCHPAD's normal smoothing and textured
normals (cd7cfe5, b87c0b5) was removed in 21bf907: it reconstructed in screen
space what the draws already carry, for 3–4 ms, and its smoothing changed the AO
of 0.1–0.3% of the pixels.

- **Authored normals in every lighting mode.** OoT3D's CMB vertex program exposes
  its transform, normal input and skinning to the typed frontend. Whenever a
  guide reader (AO, outlines, reflections, GI) requests the normal guide, the
  vertex stage also emits the authored normal (`pica_surface_normal`, the same
  transform and skinning the per-pixel lighting uses), and the guide reads it.
  With Native lighting, vertex-lit programs have no normal quaternion; the guide
  previously fell back to the flat normal of the view vector's screen
  derivatives, a per-triangle reconstruction from depth. That fallback now
  remains only for programs without a typed normal, and for a degenerate
  authored normal.
- **No mesh smoothing.** A survey of the Kokiri and castle-town geometry (every
  normal-guide draw's vertex and index data, temporary instrumentation) found
  the authored normals already shared across 99% of the welded vertices; the
  remaining splits are authored hard edges, which also match the faceted depth
  the AO estimators integrate against.
- **AO texture relief** (F1 Lighting, *AO texture relief* Off / Subtle / Medium /
  Strong, `AO.TextureRelief`, no schema change). The draw's colour texture is the
  one texture unit its TEV passes to the output colour
  (`PicaTevMaterialTextureMask`, excluding the bump and integer shadow units);
  a draw with several colour textures gets no relief rather than a guessed
  layer. That texture is sampled at the LOD and wrap of the native sample, one
  texel of that LOD apart on each axis, and the slope of its log2 luminance bends
  the guide normal along the surface's texture axes (tangent frame from screen
  derivatives of position and texture coordinates). Taps and derivatives are
  taken at the start of the fragment program, before the native alpha test can
  end an invocation of the quad; the bend follows the outline geometry guide,
  which keeps the unbent normal. PBR materials keep their authored normal maps.
  The relief is written into the shared guide, so reflections and GI see it
  while AO is enabled.
- Off, and with AO disabled, the programs are unchanged: Kokiri captures are
  byte-identical to the previous build with AO off (relief set to Strong) and with
  PerPixelPBR GTAO, whose per-pixel lighting already supplied the authored normals.

Measured on the Kokiri reference walk and the castle-town drawbridge
checkpoint, 2560x1440, RTX 3060 (single validated runs; timing medians of
480 frames):

- Native lighting: all 127 normal-guide draws per frame read the authored normal
  (`typed_normal_guide_draw_count`). The frame changes on 1.2% of its pixels with
  GTAO and 0.15% with MXAO, mostly on Link, darker in creases the flat facets
  had hidden. The world pass costs the same (1.86 vs 1.88 ms).
- PerPixelPBR: 96 of 122 guide draws take the new path (draws outside the
  per-pixel relighting); the captures are byte-identical to the previous build.
- Texture relief: 80 of 83 depth-writing draws in Kokiri and 55 of 65 in castle
  town have one colour texture; the others have several and get none. With the
  AO faded far enough to reach the walls (MXAO radius 20, fade to 4000), Medium
  darkens brick mortar and cobblestone joints on 12.8% of the castle-town frame.
  A coarser LOD (one level up) softened the joints without adding depth and was
  not kept. Strong costs 0.33 ms in the world pass and 0.63 ms per frame
  (A/B/B/A: 10.42 → 11.05 ms); the AO passes are unchanged. LAUNCHPAD's
  texture detail cost 3.0–3.5 ms.
- Validated runs report zero Vulkan/NRI errors (the 11 pre-existing warnings).
  Tests: pipeline-cache cases for the typed guide in Native lighting and for the
  relief variants and exclusions (both compiled with shaderc), settings
  validation and persistence, and the F1 actual-widget smoke (7,308 assertions).
- Motion vectors already cover these draws: with forced motion, every
  depth-writing scene draw has its exact previous-frame vertex program except in
  the frame it first appears (Kokiri walk 20 of 240 frames with a new draw;
  castle town none). Grass blades had no motion of their own (the camera's
  motion, and the reactive mask rejected their history); see *Grass motion
  vectors*.

### Grass motion vectors

Grass blades write exact motion into the rigid-motion guide, like the native
draws with their previous vertex uniforms, so temporal consumers (TAA, the
temporal upscalers, GI and the planned temporal AO) keep their history on grass.

- **Previous frame per placement.** The pass keeps what each placement drew with
  (world-to-clip, jitter, camera position, wind time), keyed by the world
  placement and render target, and uses it only in the next frame, as the native
  draws do. The environment record carries it (`PreviousPositionToClip`,
  `PreviousCameraTime`, `MotionState`; the record and the push constants now live
  in `grass_draw_state.h`).
- **Same code, both frames.** The vertex shader evaluates the blade point with
  `grass_blade_position` twice: with the current camera (single-plane blades
  face it, far tufts spread with its distance), wind time and interaction bend,
  and with the previous ones. The fragment writes previous minus current UV with
  the valid flag and clears the reactive mask. Without a previous frame the blade
  keeps the former contract: camera motion from depth and a reactive pixel.
- **Exact interaction bend.** The instance compactor keeps, per static anchor,
  the last bend with the frame stamp that wrote it and the bend it replaced
  (16 bytes per anchor), and writes each instance's previous bend. A frame gap, a
  temporal reset (`ResetTemporalState`) or a new static set clears it; a second
  grass invocation in the same frame reads the stored pair. The CPU fallback keeps
  no history and reuses the current bend.
- With temporal motion off the programs take the former path. Kokiri captures are
  byte-identical to the previous build with motion off and with forced motion
  (which has no consumer there, see below), as are the field captures with grass.
- `grass_motion_gpu_tests` renders the shipped SPIR-V: the second of two frames
  (camera, jitter and wind time changed, an actor sweeping through the blades)
  reprojects its blade pixels with their motion onto the first frame's blades at
  the same height (grey encodes height) for 99.9% of them, mean height error
  0.0025; camera-only motion lands 50.5% and no motion 40.6%, for both fixed and
  camera-facing planes. The bend history is checked for consecutive frames, a
  repeated invocation, a gap, a reset and a new static set.
- Kokiri walk with forced motion: up to 198,673 blades per frame carry exact
  motion, in 354 of 360 frames (the first frames have no previous frame). The
  Kokiri reference walk has no camera for its presented view, so TAA, the
  upscalers and the motion pass do not run there in either build. In Hyrule
  Field with the Kokiri grass settings TAA and the motion pass run (357 of 360
  frames, up to 9,984 blades with exact motion), but its TAA history was reset in
  every frame in both builds (see the next section), so those captures were
  identical.
- Cost (Kokiri walk, ~199k blades, 2560x1440, RTX 3060, A/B/B/A medians of 480
  frames): with temporal motion the grass pass takes 3.45 instead of 3.26 ms
  (+0.19 ms, frame +0.21 ms); without it the grass pass and the frame are
  unchanged (1.97–2.04 vs 1.99–2.01 ms).
- The built-in pass artifacts are regenerated; the committed catalogue was also
  stale for the grass shadow grid, surface-lighting and reflection passes.
  `build_pass_artifacts --check` passes with 64 artifacts.

### Temporal history across display-graph invocations

Two defects kept TAA from working in Hyrule Field (both predate the grass
motion work above):

- **Transient-arena thrash.** A frame there runs three display-graph invocations:
  the native boundary (AO and its composite colour), the presentation (motion
  vectors and reactive mask for TAA) and the interpolated presentation. The
  transient image arena held one configuration, so the first two replaced each
  other in every frame; each replacement completed the recorded work and released
  every effect provider together with the temporal history, and TAA started from
  a first frame each time. The arena now keeps one configuration per recurring
  plan (`EffectTransientConfigurationCache`, four, least recently used); only
  replacing one releases the providers. Field TAA: two allocations per run
  instead of two per frame, history valid in 354 of 360 frames instead of none,
  and the GPU frame takes 3.02 instead of 11.36 ms (A/B/B/A medians of 480
  frames, 1600x960, RTX 3060); the field with AO alone is unchanged (2.55 vs
  2.58 ms).
- **TAA extent.** OoT3D renders its top screen twice as wide as it presents
  (480x400 physical, 1920x1600 here) and the native display transfer halves it.
  TAA resolves the presented scene colour (960x1600) but was sized for the target,
  so it read past the colour's width and the scanout showed the scene squeezed
  into half of the screen. TAA now takes its colour's extent and reads motion,
  which stays at the target extent, at the same normalized position (the same
  texel when the extents match).
- Captures are byte-identical to the previous build where neither applies
  (Kokiri walk, field with AO only); field TAA and grass TAA now resolve the full
  frame. Zero Vulkan/NRI errors.
- The two defects left open here are resolved below:
  [upscaler render resolution](#upscaler-render-resolution-and-guide-extent) and
  [presentation stages under native UI](#presentation-stages-under-native-ui).

### Upscaler render resolution and guide extent

The temporal upscalers were configured from the scene target's extent while they
read the presented colour, which the native display transfer had halved: in the
field FSR got 640x1067 colour for a 1920x1601 output planned from the 1280x1067
target, and NRI rejected every dispatch (`currentResolution is below the minimum
render resolution`, 357 of 360 frames; the scene was black). DLSS failed alike.

- An upscaler's render resolution is now the extent of the colour it declares:
  the display image (`SceneColor`), the working colour, or a composition on the
  scene target. The working colour converts the scene colour at that colour's
  extent unless a scene stage traced on the guide target (SSSR) reads it.
- Depth, motion and reactive guides keep the target's extent. When it differs,
  `UpscalerGuidePass` resamples them into private images at the colour's extent
  inside the upscaler pass: each colour texel covers its footprint of guide
  texels (`ResolveUpscalerGuideSpan`) and takes the depth and motion of the
  nearest one and the largest reactive value. FSR (NRI), DLSS (NRI) and the D3D12
  DLSS bridge consume them; the jitter, applied in target pixels, is passed in
  input pixels.
- Field FSR now dispatches 357/357 frames at 640x1067 -> 960x1601 with zero NRI
  errors (63,903 before), resampling the 1280x1067 guides every frame; field DLSS
  dispatches 357/357. On the Kokiri walk FSR reads the working colour at the
  display extent (960x1707 -> 1440x2561) instead of the target's 1920 width.
  The field FSR frame takes 2.35 ms, the upscaler scope (with the resampling)
  0.74 ms. Diagnostics: `upscaler_guide_resample_count`,
  `upscaler_guide_width/height`.

### Upscaler motion (2026-10-12)

DLSS shook with a still camera: in Hyrule Field the presented frame moved 0.44
pixels from one frame to the next (AA off 0.003, TAA 0.09), following the
jitter phases; with the jitter off it held still. The vectors it read were
zero. The motion vectors are a transient of the graph execution that computes
them, and their slot is taken by the plan's later passes once their last reader
ran (`EffectTransientAllocationPlan`); the presentation that dispatches DLSS
reused the vectors of the native boundary executed before it in the frame, so
it read the slot's later contents (no motion), and the jitter step the colour
carried was taken for motion.

- Every graph execution that reads motion computes it (a compute pass at the
  render resolution); none reads another execution's motion.
- The upscalers take motion without the projection jitter: `UpscalerGuidePass`
  now prepares the guides for every dispatch, resampled or not, and adds the
  jitter step back to each texel that has motion (`UpscalerMotionWithoutJitter`);
  texels without motion (the sky, backdrops drawn without depth) stay still.
  FSR, DLSS and the D3D12 DLSS bridge no longer declare jittered motion.
- Scene draws whose vertex program does not add the jitter (no temporal
  variant) take the same sub-pixel step through their viewport, so the whole
  scene the upscaler reads is jittered alike.
- Field, still camera, DLSS Quality 1280x720: 0.002 pixels frame to frame
  upright and stored (was 0.44), temporal flicker 0.99 (was 2.46; TAA 1.08).
  Mid-pan the image is sharper (edge energy 10.94 against 8.70) with no trails.
- Only an execution that runs a reader of motion (AO that reads it, SSSR,
  GI, TAA, a temporal upscaler) computes it. A frame without native UI (the
  Temple of Time cutscene between text boxes) presents the same plan twice;
  the second execution reuses the first's AO, Hi-Z and reflection, and used
  to write this frame's motion again into the slot the reflection shared with
  it, so the scanout composed the motion as reflection. With temporal MXAO
  (which reads motion) and the water reflections on, additive surfaces, where
  the reactive mask is 1, turned black: the light from the windows, Navi's
  glow. Frames with a text box compose at the boundary before UI and were not
  affected. That second execution now runs no reader, so it computes no
  motion. Where motion is computed anyway (GI runs in every execution), an
  output of an earlier execution that shares its slot (reflection, working
  colour, composite, SMAA, upscaled colour) is computed again, not reused.
- Water reflections with Reflections Off trace Hi-Z for water only: a pixel
  off the title's water bodies and the catalog's water planes writes zero. A
  frame whose scene draws published neither (`mReflectableWaterFrame`) plans
  no reflection, Hi-Z included; its image is unchanged. Temple of Time
  cutscene, medians: frames with a text box 18.4 -> 16.6 ms on the GPU, frames
  without 21.4 -> 15.7 ms (reflection 0.44 / 1.67 ms -> 0).

### Lights that follow their surfaces (2026-10-12)

The architecture's lights (Forge's scene light catalog) shone whenever their
room was loaded. In Hyrule Castle the lit windows of the town below (glow
cards) lit the hill by day, and Kakariko's windows lit the village by day,
although the game showed no light there: it draws those glow cards in every
frame and fades them with a combiner constant's alpha (0 by day, 1 at night;
Kakariko's window shafts 0.5 at night).

- `PicaSurfaceEmissionRegistry` (shared renderer, `pica_surface_emission.h`)
  follows the surfaces a title names (base-level texture content hashes) and
  measures how strongly each frame draws them: the combiner's output for white
  textures and vertex colour (`EvaluatePicaTevProgram`, the CPU mirror of
  `pica_evaluate_tev_resolved`), weighed by the blend's source factor
  (`PicaUnitSurfaceEmission`); the strongest draw of a frame counts. Strengths
  are read one frame late. A surface not drawn in a frame keeps its last
  strength; one never drawn reads full.
- The OoT3D adapter follows each catalog light's `surfaces` and scales the
  light by the strongest of them before light selection; a light whose
  surfaces were drawn dark is left out, so it takes no slot and no shadow.
  Forge's generator names the surfaces (merged lights name both) and no longer
  takes the opaque panes of a room that draws lit windows as additive glow for
  lights.
- Lamplight through a window now casts point shadows, like doorways: it sits
  off the facade.

### Torch stands are not lights (2026-10-12)

The scene light catalog placed a light above every torch stand drawn in a
room mesh, whether a flame burned on it or not. By day, Hyrule Field's
drawbridge gate was lit, with shadows, by two torches that burn only at night;
unlit stands in the Spirit Temple and Ganon's Castle lit their rooms too.

- The flame on a stand is always an actor's: En_Light or Obj_Syokudai in the
  dungeons (already lit as flames, group 2, while they burn), and at the
  drawbridge Bg_Spot00_Hanebasi itself, which draws both flames at a scale
  that grows from 0 as the bridge rises at night. The adapter now lights
  those two flames from that scale (`oot3d_native_a32_fire_emitters.cpp`).
- Forge's generator no longer turns torch stands into lights (catalog
  revision 4: 127 fewer); the runtime refuses a catalog that still lists the
  `torch` category, so an older one is regenerated, and no longer needs to
  drop a stand's light where a flame actor already burns.

### Grass coarse grid without actor shadows (2026-10-12)

Link's and the actors' shadows reached the grass twice: through the local
grids around Link and Epona, blade by blade, and through the coarse 4x4
texture-colour grid, whose nodes sampled the composed shadow map and spread an
actor's shadow over whole grid cells.

- The directional shadow pass already keeps each layout's static casters'
  depth (`DirectionalShadowStaticCache`), copied into the composed map before
  the actors are drawn. That copy is now its intermediate phase (TransferRead),
  and the pass completes with the displayed layouts' static atlases readable
  (ShaderRead). The history snapshot names them (`StaticViews`: the newest
  layout, then the older one while a dissolve runs).
- The Grass pass declares a read of `DirectionalShadowStaticHistory` with the
  composed history. Its grid compute runs twice over one pipeline: the coarse
  nodes with the static atlases, the local nodes with the composed map. Each
  layout of the static depth is its own one-region atlas, so the receiver
  shader can read layout 1 from its own binding (`PICA_VSM_LAYOUT_ATLASES`).
- Static shadows on the grass are unchanged (a pillar's shade: 69 against 70
  of 255 in the same low-sun frame). With the local grids disabled, Link's
  long shadow leaves the blades lit and stays on the ground only.
- Actors without a local grid (all but Link and Epona) no longer shadow the
  grass.
- The geometry-provider plan's read mask accepted only the first 32 resources;
  it now covers the graph's 64.

### Presentation stages under native UI

On the Kokiri walk the HUD reaches the scene target before its display transfer,
so the transfer sees no unresolved scene (`SceneResolved` false) and, without a
scene effect, nothing recorded the scene's completion either: presentation had
no world surface, no depth association and no camera, and TAA, SMAA and the
upscalers never ran (motion and TAA passes: 0 of 360 frames).

- `AntiAliasingFramePolicy::PresentationWorldSurface` names the presentation
  stages that read the world surface (TAA, SMAA, NIS/FSR/DLSS, forced motion).
  When one is configured, the target's `BeforeUi` boundary records the scene's
  completion even if no scene stage composes there: the target's content at that
  boundary already is the scene, before any UI. Native UI preserves that record,
  so the display transfer resolves the world surface and camera exactly as after
  an AO composition.
- Kokiri walk: TAA 358/360 frames (history valid 354), SMAA 358, FSR 358, from 0.
  Captures without those stages (effects off, MXAO without AA, field AO and TAA)
  are byte-identical to the previous build.
- Same-extent anti-aliasing now resolves before that UI and the upscalers
  reconstruct the scene before it; see below.

#### Anti-aliasing before native UI

TAA and SMAA keep the scene's extent, so they are scene stages (declared at
`AfterTransparent`): the boundary that completes a target's scene before its
native UI (`DisplayEffectPlanInput::BeforeNativeUi`) resolves them into the
composition it commits, and the HUD is drawn over the result. At presentation
they used to process the display image, HUD included: TAA reprojected the HUD
with the world's motion and blended its history (the Kokiri walk showed doubled
"Attack" button text), and SMAA filtered the HUD's edges.

- Only that boundary runs them (`ExecuteNativePicaDisplayGraph(...,
  beforeNativeUi)`); a raw-copy boundary does not, so one scene version is
  anti-aliased once. A display whose target had no such boundary (no UI on it,
  or UI interleaved with scene draws) still anti-aliases at presentation, and a
  presentation of a scene the boundary resolved scans it out as it is (the
  declared domain projection drops the resolved stages).
- The boundary composes when TAA or SMAA is configured
  (`AntiAliasingFramePolicy::SceneAntiAliasing`); its exit now follows TAA and
  SMAA, whose outcomes join the declared-stage failure check. One scene surface
  per frame owns the AA history and output: a second boundary before UI of
  another surface in the same frame composes its other stages without them
  (`before_ui_anti_aliasing_unavailable_count`; 0 in the covered runs).
- TAA works at the target's extent, where the jitter is defined, and reads
  motion without resampling; SMAA also runs on the target before the transfer
  halves it.
- Kokiri walk (2560x1440): TAA at the boundary in 357 of 360 frames (history
  valid 354), SMAA 357; field TAA 356; zero Vulkan/NRI errors. Pixels of the
  HUD that stay constant over the walk (an AA-off mask) equal the AA-off capture
  in 99.9% (TAA) and about 100% (SMAA) of cases, against 90.8% (mean difference
  3.1, maximum 99) and 99.6% before. Captures without TAA/SMAA (effects off,
  field AO, MXAO, temporal MXAO, FSR) are byte-identical to the previous build.
- Cost (A/B/B/A medians, 600 frames): the AA scope doubles with the pixel
  count (TAA 0.37 -> 0.63 ms, SMAA 0.47 -> 0.90 ms) and the boundary composition
  adds 0.17-0.18 ms; the Kokiri frame stays within run-to-run variation (TAA
  11.2 -> 10.6 ms, SMAA 8.3 -> 8.5 ms).
#### Upscalers and native UI

The upscalers change the extent, so they stay at presentation; they used to
receive the display with its HUD, and FSR/DLSS reprojected and jittered the HUD
with the scene (the Kokiri "Attack" text ghosted and broke up while walking).

- With an upscaler configured (`AntiAliasingFramePolicy::PresentationUpscaler`)
  the boundary before native UI keeps the scene it completed: one copy of the
  target (`SceneBeforeUiSerial`, carried by native UI writes like the resolved
  serial). The display transfer then transfers it exactly as the display
  (`mNativePicaSceneBeforeUiDisplays`), so where the UI drew nothing the two
  images are bit-identical.
- `EffectResource::SceneBeforeUi` declares that scene. With a resolved scene
  (`DisplayEffectPlanInput::SceneBeforeUi`) every presentation read of the
  display's scene colour reads it instead (the working colour converts it, or
  the upscaler reads it), and FSR, DLSS and NIS also read `SceneColor` and
  `SceneBeforeUi` to lay the UI over their output (`NativeUiOverlay`).
- `NativeUiOverlayPass` (in the upscaler pass, `oot3d_native_ui_overlay.comp`)
  takes, for each output texel, the display texels of its bilinear footprint
  that the UI changed and blends them in with their coverage, decoded to the
  upscaled colour's encoding; elsewhere the upscaled scene is kept exactly. The
  UI keeps the display's resolution (bilinear), and translucent UI shows the
  display's scene behind it.
- Kokiri walk: FSR, DLSS and NIS overlay the UI in 358 of 360 frames (357 scenes
  kept and transferred), field FSR and DLSS in 357; zero Vulkan/NRI errors. The
  constant HUD pixels equal the AA-off capture in 94% of cases (FSR/DLSS; the
  rest is the display's lower resolution, maximum difference 33) against 90.6%
  and a maximum of 140 before. Captures without an upscaler are byte-identical.
- Cost (Kokiri FSR, A/B/B/A medians of 600 frames): the frame goes from 8.20 to
  8.34 ms for the kept scene, its transfer and the overlay.

Fresh combined source adoption, deterministic framebuffer/state comparisons and
the remaining realtime validation observation are recorded in
[`TRIAEVUM_RUNTIME_PROGRESS_QUALIFICATION.md`](TRIAEVUM_RUNTIME_PROGRESS_QUALIFICATION.md).
The measurements above remain historical, not a new performance qualification.

## Acceptance Gates

- Extensions-off output matches the covered Azahar corpus, including structural entrance scenarios.
- Covered draws have no unknown PICA fallback and produce no Vulkan/NRI validation errors.
- Enabling an observer or auxiliary-output feature does not change native color or depth.
- Shadow maps can use caster/receiver geometry and contribute at the generic
  `NativeLighting` stage through a title-owned PICA hook; grass can use
  `GeometryProvider`; a future path tracer can use `WorldPassReplacement`. None
  requires a generic final-frame filter.
- The native path remains comfortably above 60 FPS on the target machine, with deterministic pipeline caches and no per-frame bulk asset copies.
- Fixed x2 preserves the 30-Hz simulation while sustaining 60 presentations per
  second. Fixed x3 must preserve the same simulation while sustaining 90 with
  sufficient stereo headroom before it can be used as a VR presentation mode.

### Key light direction from the scene lights (2026-10-02)

The authored key light (the one shadow map) can take its direction from the
native environment lights instead of its own azimuth/elevation. Scene profile
version 5 adds `keyLightDirection`:

- `authored`: its own azimuth/elevation (versions 1-4 load as this).
- `scene_main`: the scene's main light, the brightest native environment
  directional (the one the native directional shadows follow without an
  authored key), turned by `keyLightAzimuthOffset` (-180-180 degrees) and
  `keyLightElevationOffset` (-90-90 degrees) around the profile's axes.
- `scene_ambient`: the axis of the other environment directional, its sense
  taken from the main light (positive dot product). The light itself is never
  modified; only its direction serves as the reference.

The renderer resolves it per draw from that draw's scene snapshot
(`ResolvePicaEnvironmentKeyDirection`), in the surface lighting and in the
shadow map, so it follows the sun without republishing the profile (which
resets temporal history). While a snapshot has no emitting environment light,
the authored direction applies. The main-light rule is one owner
(`FindPicaSceneMainDirectional`), shared with the native shadow selection.
The title publishes its axes (`WorldUp` +Y, `WorldAzimuthZero` +Z).

Why not the native shadow direction: the engine's per-pixel Shadow2D receive
path exists (per-model render context `+0x1A8` shadow map, `+0x1B5` enable;
material compile `003F9B5C` binds texture unit 0 as Shadow2D in `003FA198` and
takes texture coordinate 0's 4x4 light projection from the shadow map object
in `003F9F68`), but OOT3D never creates or fills that object: its constructor
(`00347258`) zeroes both fields and only the context copy (`00310F7C`) writes
them; lighting defaults (`00410C68`) disable the shadow factor and every
light's shadow; no CMB material carries per-light shadow flags. There is no
native shadow direction to recover.

Kokiri Enhanced (DLSS, grass): `scene_main` against the authored key changes
47% of pixels; `scene_main` against `scene_ambient` is 12/12 identical (the
outdoor environment pair: the second light, turned to the main light's side,
coincides with it). Authored cold against warm cache differs by 1.2% (known
hybrid noise). F1 smoke: 7,988 assertions. Receipts:
`I:/_triaevum_work/perf-levers/qualify/shadow-prepass/key-*`.

Since 2026-10-02 (animated lighting, docs/TRIAEVUM_ANIMATED_LIGHTING_STRATEGY.md)
a key that follows the scene (`scene_main`, `scene_ambient`) also takes its main
light's colour and brightness (`ResolvePicaEnvironmentKeyColor`): the authored
colour tints it and the intensity scales it, so white and 1 give the title's
own sun or moon, including sunrise colours and night. The main light is the
strongest environment light above the horizon (the published shadow caster).
An authored (`Manual`) key keeps its own colour.

## Isolated lighting: the title logo (2026-10-06)

`PicaCompositionAttribution::IsolatedLighting` marks a draw the title lights
as an object of its own. In the Modern (full per-pixel) model such a draw keeps
per-pixel + PBR shading by its own native lights and receives none of the
scene's lighting: the scene environment profile is replaced by the native
environment (profile disabled), it is neither a directional nor a point shadow
receiver, its GI source weights are zero, and its shader publishes no ambient
occlusion response (`AmbientPath::None`, ambient guide `vec4(0,0,0,1)`) and a
zero diffuse albedo guide (no GI received or bounced). The Native model and
F2 are unaffected.

The OOT3D adapter sets it from typed actor ownership, not from assets: draws
under `Actor_Draw` of `ACTOR_EN_MAG` (0x0171, the title logo composited over
the opening; `kOot3dTitleLogoActorId`, `IsTitleLogoActor` in
`oot3d_native_pica_composition.h`). The semantic trace reports
`isolated_lighting`.

Evidence (developer build, intro from boot, the user's configuration and scene
profiles, 3840x2160): at guest frame 899 21 draws are isolated, all of actor
0x0171. Before, the scene profile turned the logo almost black in Modern; now
it matches the Native look with per-pixel shading. Between Modern before and
after, every changed pixel lies inside the logo's box at presentation frames
1200, 1500 and 1800; Native frames are byte-identical to the previous build.
Foundation 337/337, pipeline cache 52/52, composition, visual savestate and
Vulkan bridge tests pass.

## The title logo as a layer over the resolved scene (2026-10-20)

In the intro's river shot the logo (actor 0x0171, `IsolatedLighting`) was
mirrored, deformed, in the water: its 23 draws (17 opaque with depth write, 6
transparent) are the last scene draws on the top target, right before its
display transfer, so the Hi-Z reflection read a scene colour and depth that
already held the logo. In the extension profile the logo is now a layer of
its own over the completed scene, exactly as native UI is:

- `PicaCompositionSchedule` treats a draw as an overlay when its domain is
  `Ui` or when it is a scene draw with `IsolatedLighting`. A tail of overlay
  draws ends the scene: the `SceneResolved`/`BeforeUi` anchors lie before its
  first draw, and isolated draws are no world geometry for the stage analysis
  (the logo's opaque draws after the atmosphere no longer make the intro's
  opaque world non-contiguous). A world draw after an overlay draw withholds
  the boundary as before.
- `ExecuteNativePicaBeforeDrawAnchors` runs the pre-UI boundary for the
  first overlay draw (`NativeSceneOverlayDraw`): the scene stages (AO, GI,
  reflections, outline, same-extent TAA/SMAA) are composed and committed to
  the target, or the scene is kept beside the display for an upscaler.
- A logo draw after that boundary (`sceneOverlayDraw`: the target's
  `SceneEffectsResolvedSerial` is current) is drawn like UI: no projection
  jitter, no motion vectors, not a reflection-environment sample, and its
  write keeps the resolved-scene and scene-before-UI serials, so the display
  transfer carries the scene effects as resolved and the upscaler lays the
  logo over its output through the native UI overlay. Its depth test against
  the world is unchanged (the target's depth is not touched by the boundary).
  Without a boundary (no scene stage enabled and no upscaler, or world draws
  after the logo) it is a scene draw as before. Diagnostics count
  `scene_overlay_draw_count`.
- An isolated draw never casts a directional shadow nor feeds the scene
  distance field, in any frame: it is an object of its own.

The Native profile and F2 are unaffected. What the layer gives up: the
logo is not temporally anti-aliased (it is drawn after the same-extent TAA
resolve, like the HUD); SMAA/FSR/DLSS display stages still see it.

## Sky identity under visual interpolation (2026-10-06)

In the title intro the sky and its clouds held still for two presentations and
then jumped, every three to four guest frames, with Interpolated 2x. The guest
delivered its frames evenly (one top transfer every two refreshes); the
transitions with the stutter were exactly those where the three sky draws of
`SceneRenderer_SubmitConfiguredModelPass@0x002D9664` were unmatched, so the
sampler drew them from the current frame at alpha 0 and 0.5 alike. Their
attribution packed `r2 | r3 << 16` into the native value, and `r3` is the sky
blend, which the game steps every few frames as it rewrites the sky's vertex
colours (the call takes four core arguments and the eye position in `s0-s2`).
A draw parameter inside the identity made each step a new draw for the strict,
structural and pipeline tiers, which all compare the attribution.

The adapter now publishes only the sky selector (`r2`). Intro from boot with
the play configuration, 720p, every presentation from 300 to 1499: the sky
region held still while the scene moved in 99 presentations before and in none
after; authoritative samples equal the native-30 frame of the previous source
exactly in motion. At a blend step the sky colour still changes one sample
early, because unsigned-byte vertex attributes are not interpolated; the step
itself is native (native 30 shows it too). Composition tests cover the selector
staying constant while the blend steps.

## Four-light vertex programs and native fragment-lit receivers (2026-10-08)

The frontend recognizes a second vertex-lighting shape besides CMB's
three-light block (`DecodeCmbSurface`): under a boolean uniform, MOV material
diffuse and ambient, then four DP3 of light-direction uniforms with the
view-space normal into the components of one temporary, each light enabled by
a component of a uniform vector (MM3D's `GzwShader`,
`DecodeFourLightSurface`). The surface rows hold three vertex lights, so that
layout is published with `VertexLightsReconstructed = false`: it supplies the
normal, material and tint; a draw that lights per vertex
(`LightingBooleanUniform` set) keeps its native shader
(`BuildPicaSurfaceLightingUniforms` returns no uniforms).

Draws lit by native fragment lights (and not per vertex) keep their native
per-pixel result. A scene directional light with the view-space direction of
one of the draw's native directional fragment lights
(`ExcludePicaSceneDirectionalsLitNatively`, within ~5 degrees for the
half-float light vector) is not added again; the shadow receivers list the
native fragment lights (slots 0-7, as the PBR native loop does), and
`PicaSurfaceLightingBody`, without per-vertex lighting, removes the shadowed
diffuse share `diffuse_i * max(n . l_i, 0) * (1 - shadow_i)` of a selected
native light from the native primary fragment color. Lights the shadow does
not select read 1 and leave the result unchanged. A draw whose fragment
lights match no scene light keeps the previous behaviour.

Verified on MM3D (developer launch, Clock Town attract demo, deterministic):
with directional shadows and PerPixelPBR, the 96 opaque scene draws bind as
matched receivers and the frames equal the effects-off frames byte for byte
(the demo's light is horizontal, so no shadow); with the guest's directional
light raised in guest memory (a local experiment, not committed), the shadow
of the building and of the characters darkens the native per-pixel light
(18% of the pixels of frame 2390 against the same light without shadows).
Tests: frontend (four-light surface program), pipeline cache 53/53 (layout,
per-vertex fallback, exclusion), foundation 340/340, effect graph 59/59,
scene frame 20/20. Not re-run: an OoT3D capture; OoT3D changes only where a
fragment-lit material's native light matches a scene light.
