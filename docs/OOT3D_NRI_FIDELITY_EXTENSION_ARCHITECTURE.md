# OOT3D NRI Fidelity and Extension Architecture

## Goal

Build an NRI renderer that reproduces the native OOT3D/PICA output faithfully while exposing enough structured scene and pipeline data for future advanced rendering. Extensions must integrate at the correct rendering stage; they must not be limited to generic post-processing.

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
- **NRI Native Render Core** performs the faithful world render and remains usable without any extension system.
- **PicaSceneFrame** is a passive, low-level, read-only view of resolved draws and GPU resources. It is never the rendering source of truth.
- **NativeSceneView** joins stable draw IDs with camera, lights, transforms, current/previous skeleton state, geometry, materials, textures and object identity when those semantics are available.
- **Extension Render Graph** owns declared dependencies, resource lifetimes, barriers and pass ordering.
- **Native Frame Composer** preserves the original ordering of world, atmosphere, transparent content, display transfer and UI.

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
- Ledger integration exposed two declaration/runtime mismatches. FSR now declares and depends on its real HiZ depth preparation rather than claiming native-depth input, and non-world display surfaces cannot request world effects. Duplicate presentations of the same world surface now reuse the already generated guide, motion and TAA outputs instead of silently scanning out an unprocessed image.
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
- The enhanced receiver policy is deliberately additive to the original presentation: dynamic skinned geometry casts, rigid ambient/baked world geometry receives, and native actor lighting is not re-shadowed. This preserves original vertex/material self-lighting while adding projected actor shadows to the level. The policy is based on decoded skeleton and lighting semantics, never scene, actor, shader or texture IDs.
- Deterministic Kokiri validation over 180 frames executes 89 maps. A representative frame contains 19 skinned caster draws, 38 native light candidates in two world-space clusters, and 29 rigid baked receivers. The extensions-off/on RMSE is `0.00701319`; the difference is confined to ground projections beneath Link and the Kokiri rather than actor self-darkening or global level darkening. Vulkan/NRI validation remains clean. Artifacts use the `nativeview_v6` suffix under `I:/oot3dre_work/visual-parity/directional-shadow-rework-20260825/`.
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
