# Grass: Terrain Continuity and Bounded Cost

Date: 2026-09-11. Reviewed implementation: `0079a43`.
Status: first color-grid implementation verified on Windows; full lighting
continuity and bounded-patch generation remain pending.

## Decision

Make Grass extend the terrain's local appearance, then spend geometry only on
visible silhouettes. Do not compensate for a color/lighting mismatch with more
blades, larger caches, or a longer draw distance. Retain the existing NRI effect
boundary and portable Vulkan path. This is an optional extension, not a change
to native OOT3D rendering or an attempt to reproduce BotW gameplay.

## First Implementation: 4x4 Grid, Two Nearest Points

Per the user's refinement, cache 16 RGB reference points per source texture.
Each point is an alpha-weighted regional average at a cell center. Grid creation
runs once per observed texture; workers share the immutable 192-byte RGB grid.
For each retained root UV, choose only the two nearest points, using Euclidean
distance in normalized UV space. Weights are `d1/(d0+d1)` and `d0/(d0+d1)`;
an exact match takes that point's color. Only four neighboring candidate distances
are needed on the regular grid, and only the selected two colors are read.
This is deliberately **not** a 16-point blend or four-point bilinear blend.
Ties use stable grid indices. Two-nearest selection can have changes at pair
boundaries; do not silently replace the requested method with bilinear smoothing.

The resulting RGB is stored once per world anchor with a validity byte, including
valid black. The vertex shader uses it through the existing texture influence
and root/tip brightness controls; missing color retains the prior average
fallback. Native mask resolution and semantics are unchanged. No full texture
sampling or nearest-point search occurs in the frame's Grass shader.

`grass_texture_source_cache` owns the grid and shared source lifetime;
`grass_world_placement_cache` samples it; `grass_instance_layout.h` owns the common
CPU/compute/render instance layout. CPU fallback and compute expansion both copy
the packed color. Compute output uses integer words to preserve packed bits,
and shader strides derive from C++ layout sizes. Reused source observations no
longer construct a redundant texture-byte copy before `try_emplace`.

This costs four additional bytes per retained world anchor (36 -> 40) and visible
instance (56 -> 60), about 17.8 MiB extra for the old 4.68-million-anchor fixture.
It is **not** the planned memory/performance reduction. Dynamic terrain lighting,
vertex colors/TEV, distance contrast and patch-bounded populations are unchanged.

Validation: 77 Grass tests pass on Windows, including two-nearest weighting,
exact point, black, repeat seam, mirror/clamp, shared grid/cache reset, 1x1 source
and unchanged world placement. The previously disabled foundation test target
was missing source dependencies for existing static placement/instrumentation
tests; its CMake list was repaired so these tests can actually link.

Private Vulkan/NRI probe: `epona-grass-grid4-two`, same mounted checkpoint and
profile, 180 presentations, captures at 120/150, exit 0. Grass remains visible;
at frame 172 the counts remain 87,315 instances and nine draws. The profile has
22% texture-color influence and root/tip brightness 2.87/4.0, so this is not a
neutral-lighting or full-influence comparison. No FPS improvement is claimed.
Linux/Android validation and lower-density appearance qualification are pending.

## What Is Actually Known About BotW

- The reverse-engineered `grass.extm` format contains local height and RGB data.
  This supports spatially varying grass color, rather than one average per
  texture. It does **not** establish direct runtime sampling of the terrain's
  diffuse texture. The page inconsistently mentions a 64x64 grid and a 256x256
  example loop: do not adopt either dimension without binary verification.
  [ZeldaMods: grass.extm](https://zeldamods.org/wiki/Grass.extm).
- Terrain metadata is organized into tiles with terrain LODs and optional grass
  data. This demonstrates a spatial terrain/grass data relationship; it does not
  establish the exact grass LOD or GPU culling algorithm.
  [ZeldaMods: TSCB](https://zeldamods.org/wiki/TSCB).
- This investigation did not establish BotW's precise grass lighting, shadow,
  density reduction, or draw scheduling equations. The user-observed visual
  continuity is a useful target, not proof of those mechanisms. Public Unity
  tutorials explicitly recreate its *look*; their tessellation/geometry shaders
  must not be attributed to Nintendo's implementation.
  [Daniel Ilett's recreation](https://github.com/daniel-ilett/shaders-botw-grass).

Separate, first-party implementation references inform our design:

- Ubisoft documents GPU grass placement using terrain material, height, color
  and normal data, with indirect rendering. This is Far Cry 5, **not BotW**.
  [Terrain Rendering on Zeta, GDC 2018](https://media.gdcvault.com/gdc2018/presentations/TerrainRenderingFarCry5.pdf).
- Sucker Punch describes GPU-generated individual blades within memory and
  performance constraints. This establishes a production alternative to storing
  all detail on the CPU, not an identical Nintendo technique.
  [Procedural Grass in Ghost of Tsushima, GDC 2021](https://gdcvault.com/play/1027033/).
- AMD demonstrates patch generation, distance-dependent blade counts, fractional
  transitions and width compensation. Borrow the bounded-patch/coverage idea,
  not a mandatory mesh-shader dependency. Large cards can trade vertex savings
  for fragment overdraw, so compensation needs limits and measurement.
  [AMD GPUOpen: Procedural grass rendering](https://gpuopen.com/learn/mesh_shaders/mesh_shaders-procedural_grass_rendering/).

## Findings In Our Renderer

All paths below are relative to `runtime/three_ds_recomp/`.

| Finding | Code boundary | Consequence |
| --- | --- | --- |
| One alpha-weighted average RGB per source texture | `src/fast/oot3d/grass_texture_source_cache.cpp`, `AcquireAverageColor`; `interactive_grass_pass.cpp`, `BuildGrassEnvironmentRecord` | Local dark/light/color variations underneath a root are lost. Multiple masks do not solve this. |
| Root/tip colors mix with that average, then separate illumination | `src/fast/oot3d/grass_shader_sources.cpp`, `evaluate_blade_color`, `evaluate_shading` | Matching the average terrain color does not ensure matching the terrain's final appearance. |
| Lighting bridge carries light directions, diffuse/ambient and fog, but not full resolved terrain color/TEV/vertex-color contribution | `grass_shading_environment.cpp`, `grass_shading_environment.h`, `grass_scene_bridge.h` | With no active lights, the shader returns unit illumination. That is not evidence that the actual terrain is unshaded. |
| Root UV exists locally but is dropped from the packed world anchor | `grass_surface_extractor.h` (`GrassAnchor::Uv`), `grass_world_placement_cache.cpp` (`BuildPlacement`) | The draw cannot sample local terrain appearance through that anchor as currently packed. |
| Whole-surface anchor population precedes view selection | `interactive_grass_pass.cpp` constructs placement requests without enabling `PlacementView`; `grass_async_placement_builder.cpp` | Cached invisible detail consumes memory and cold construction time. Simply enabling view-dependent full-mesh rebuilds would reintroduce camera stalls. |
| Visibility/LOD selection still iterates anchors on CPU before the GPU compaction stage | `interactive_grass_pass.cpp`, `processCluster`; `grass_shader_sources.cpp`, visible-index compute entry | The GPU stage does not remove CPU per-blade selection costs. |
| Far tufts reduce geometry but use procedural cutouts | `grass_distant_tuft.h`, `grass_tuft_covered` | Fewer triangles is not automatically fewer fragment operations. Large overlapping tufts require a separate GPU cost check. |

Keep what is already useful: source mask controls, native UV decoding, stable
seeds, shared surface normal, native fog LUT/depth, world anchoring, hierarchy
culling, LOD transitions, actor interaction, indexed geometry, and effect/HUD
separation. Do not claim these features are absent or replace them wholesale.
The existing use of terrain normals is not the missing feature by itself.

## Quantitative Evidence, Not An FPS Claim

Private evidence: `C:/Users/xander/triaevum-verify-20260911/epona-grass-restored/`.
The mounted Hyrule Field probe at frame 172 reports:

| Measurement | Value |
| --- | ---: |
| Ready masks / placements | 3 / 3 |
| Retained world anchors | 4,677,577 |
| Stored clusters | 320,108 |
| Candidate clusters | 14,512 |
| CPU-evaluated anchors | 93,806 |
| Visible instances, including tuft instances | 87,315 |
| Draws / culling workers | 9 / 9 |
| CPU selection / total Grass CPU telemetry | 2.892 / 3.494 ms |
| Recorded upload bytes | 1,404,844 |
| Accumulated asynchronous build time | 7,926.716 ms |

Only about 1.9% of retained anchors were visible in that view. This does not
mean the other 98.1% were drawn. The two world-anchor arrays alone use about
232 MiB (36 + 16 bytes per anchor), excluding clusters, spare capacity, local
construction data and GPU copies. These are known storage costs, not an estimate
of total process memory.

The asynchronous time is the **sum of completed job durations**
(`Statistics.BuildMilliseconds += elapsed`), not measured wall-clock startup
latency. Earlier shorthand calling it an eight-second startup delay was too
strong. The run used synchronous captures and 2x interpolation: its timings
identify work, but must not be published as representative FPS or GPU duration.
No BotW-versus-TriAevum hardware benchmark was performed.

## Implementation Order

### 1. Local Surface Appearance

Introduce a versioned surface-appearance input at the existing scene/extension
boundary, owned by the PICA producer and consumed by Grass. Retain root UV or
triangle+barycentric identity. Use the 4x4 color grid and the two-nearest weighting
specified above with the native mapper/wrap and selected texture policy.
Cache static root colors per patch/material generation, not per presentation.

Local RGB alone is an intermediate improvement, not the finished lighting fix.
Publish/evaluate the terrain's relevant native vertex-color, material/TEV and
lighting contributions at the root, in the correct color domain and **before
fog**. Reuse the canonical PICA evaluator/contracts rather than creating a
second collection of material-specific approximations inside Grass. Dynamic
lighting/material changes invalidate appearance, not placement. Native textures
remain placement-mask authority; replacement texture generation must explicitly
invalidate any optional replacement-based appearance samples.

Make root appearance converge to this terrain response. Allow stronger blade
detail at the tips/near camera, then reduce that contrast with distance before
reducing coverage to zero. Apply native fog exactly once. Do not multiply an
already lit color by lighting a second time.

Do not sample arbitrary final-screen pixels under a blade: actors, overlapping
geometry, HUD and effects can contaminate that color. If a surface-cache pass is
needed, it must be keyed by surface identity and declare its graph resources.
Avoid a new full-resolution G-buffer or replay of every terrain draw by default.
First qualify supported canonical material paths and expose unsupported ones.

### Shared Toon Response Implemented (2026-09-11)

`toon_surface_response.h` now owns the common GLSL response and its 128-byte
std430 parameter payload. `pica_toon_shader.cpp` specializes that response with
the existing native toon settings; Grass consumes the same response with the
frame's effective settings supplied by its caller. No second F1 settings owner,
new preset, scene exception or dependency from canonical rendering to Grass.

Shared operations: automatic/custom bands, band softness, saturation, shadow
tint/strength and rim tint/strength/width. Zero softness now uses an explicit
step, avoiding undefined GLSL smoothstep with coincident edges. The native toon
variant key carries a new implementation revision to avoid old cached variants.

Grass passes lighting, normal and view direction to its fragment stage and
applies the response before native fog, without altering alpha or outline
policy. Off returns its incoming color unchanged. Settings are per-draw data,
not Grass shader variants or placement-cache dependencies. The parameter payload
adds 128 bytes per environment record, not per blade; no new full-screen pass,
texture sampling or CPU readback is introduced. This is not a measured speedup.

Important boundary: this implements shared **style**, not yet canonical terrain
lighting inheritance. Grass still receives the existing decoded-light response.
Its lighting-available flag must not pretend that native primary RGB has already
been captured. The native material-lighting path retains its pre-TEV placement;
Grass currently follows the vertex-lit surface-style path. The next section's
native output publication remains necessary for full terrain continuity.

Verification: Windows NRI/Vulkan runtime built incrementally (no title AOT
rebuild); 94 Grass/PICA-toon tests passed, including shared-source identity,
parameter packing, Off, alpha/fog order and outline independence. Three bounded
180-presentation Hyrule Field probes exited 0, using the same read-only mounted
checkpoint: `grass-shared-toon-default`, `grass-shared-toon-off`, and
`grass-shared-toon-hard-bands` under
`C:/Users/xander/triaevum-verify-20260911/`. Native framebuffer frame 150 inspected
in each. The diagnostic two-band profile visibly darkens both terrain and Grass;
the default rim visibly brightens grazing-angle Grass. User presets were not
modified. No shader compilation failures in the default run. The final default
sample retains 4,677,577 anchors, 87,315 blades and nine draws, with no pending
placement builds. Captures are not throughput benchmarks; Linux/Android and
live F1 interaction were not tested in this step.

### Vertex Lighting Reuse Investigation (2026-09-11)

Scope: source-path verification only. No new lighting implementation, runtime
capture or performance claim is included in this investigation.

Verified boundaries:

- `grass_geometry_registry.h`, `GrassGeometryRequest`: extracts position and
  texture coordinates, but no native vertex color or normal attribute stream.
- `grass_surface_extractor.h`, `GrassSourceVertex`: stores position, normal and
  UV, but no color. `grass_surface_extractor.cpp` already computes barycentric
  weights for roots; normals currently come from the triangle face. Triangle
  subdivision also needs to preserve any newly introduced shading attributes.
- `gfx_vulkan_pica.cpp`, Grass publication immediately before
  `ForgetNativePicaEffectNriTextures`: publishes geometry and a separately
  decoded `GrassShadingEnvironment`, not the native vertex shader's output.
- `grass_shading_environment.cpp` and `grass_shader_sources.cpp`: Grass uses
  ambient plus absolute N.L, independently of the terrain shader. It does not
  reproduce the terrain's primary color, full TEV or fragment-lighting result.
- `tools/oot3d/native_pica_frontend/oot3d_native_pica_shader_gen.cpp` exports
  `pica_primary_color` from native output semantics 8..11, after native
  `min(abs(raw), 1)` conversion. This is already calculated on the GPU.
- `oot3d_native_pica_fragment_shader_gen.cpp` consumes the interpolated primary
  color with byte rounding before TEV. It is not necessarily pure illumination:
  vertex tint/material factors and TEV consumption must be respected.

Decision: first qualify a native terrain material path in which primary RGB
provides the multiplicative surface appearance response. Reuse its resolved
vertex output, then interpolate over the source triangle at each root. Three
barycentric vertex weights are appropriate here; this is separate from, and
does not change, the user's two-nearest-point rule for the texture color grid.
Native smooth perspective-correct interpolation corresponds to surface
barycentric interpolation at the same geometric point; preserve native rounding
at the appropriate consumption boundary rather than quantizing prematurely.

Do not read raw input vertex colors and label them final lighting. Do not divide
final pixel color by albedo to invent an illumination factor. Primary RGB may
already contain tint, and fragment lighting/TEV can prevent this factorization.
Unsupported material paths need an explicit contract, not a claimed exact match.

The missing piece is a declared, versioned GPU output resource for surface
shading, not a new CPU lighting grid. Prefer a supported producer/output hook
sharing the canonical vertex program over CPU readback or evaluating the whole
shader once per blade. The existing `MainBodyEnd` hook is a potential producer
insertion boundary, not an already implemented surface-output cache. Validate
indexed/shared-vertex writes, resource barriers and platform costs before
choosing the concrete producer. No shader string patching or mandatory replay
of every terrain draw.

Keep topology/placement versions separate from shading versions: changed light,
uniforms or shader output must refresh appearance without rebuilding millions
of roots. Key shading by surface instance and relevant native shader/material
state, never just texture hash. A bounded per-patch 4x4 lighting cache remains
an alternative only if profiling makes it cheaper; allocating one per texture
repeat throughout the room is not justified by this inspection.

Next acceptance step: on one existing terrain fixture, capture canonical
primary RGB and TEV consumption alongside the proposed surface response;
verify shaded/unshaded regions and two light states, no double lighting and
no placement rebuild. This must precede claims of visual parity or savings.

### 2. Bounded Patch Generation

Replace full-surface blade populations with small deterministic surface patches.
Cache the cheap triangle/UV/normal/appearance recipe, not millions of roots at
maximum density. Use patch bounds for frustum/distance rejection **before**
expanding roots. Expand visible patches plus a small bounded guard region.

Use `(surface, patch, seed, local index)` identity so motion, LOD and visibility
changes preserve roots. Evicted patches can regenerate deterministically. Avoid
pure screen-space random placement and avoid a single world-XZ height map:
OOT3D meshes can overlap vertically or contain slopes and overhangs.

Retain the current rendering/interaction path for an initial patch-based pilot;
then move selected-patch expansion and visibility to compute + indirect draws
where measured beneficial. Cache coarse patch topology for immediate low-cost
coverage on a camera cut, and refine nearby detail without waiting for the whole
room. Put a byte budget and work budget on generation, not just a final instance
cap. Persist only versioned derived patch data when useful; no new Forge
requirement or game-derived content in the public package.

### 3. Screen-Space Coverage Budget

Choose detail by projected size, FOV and internal resolution, not distance alone.
Near: articulated blades. Mid: fewer segments and controlled width compensation.
Far: bounded low-overdraw tufts or no geometry when local color/lighting already
matches the terrain. At the final transition, coverage must reach zero smoothly;
a permanently nonzero far-density floor is not the objective.

Use stable nested subsets and gradual transitions. Cap compensation to avoid
oversized silhouettes or costly cards. Do not use TAA blur to conceal popping.
Preserve mask boundaries, including true zero density, and existing Grass
occlusion/outline policy. Keep Windows/Linux/Android on one capability-safe
compute/instancing implementation; mesh shaders are optional future work.

## Verification And Acceptance

- Reuse the mounted checkpoint, Kokiri and title camera cuts. No manual gameplay
  is required for the first before/after captures. Leave the user's live session
  and presets untouched during investigation.
- Check local color patches, shade boundaries, day/night changes and fog. Compare
  Grass on/off only, with other effects fixed; F2 toggles too much for this test.
- Sweep density through 100%, 50%, 25% and zero at identical views. The test is
  whether terrain remains visually continuous, not whether enough blades hide it.
- Check camera movement/cuts, reload, replacement texture changes, 4:3/16:9, FOV,
  and 1x/2x/3x. Inspect actual framebuffer captures, not Windows screenshots.
- Separate cold-build wall time, resident bytes, CPU selection, GPU generation,
  GPU Grass draw cost, upload bytes and p50/p95 frame durations. Use GPU timestamps.
- Measure after warmup without VSync/frame caps or synchronous captures; report
  native guest updates separately from presented/interpolated frames. Confirm the
  scene actually advances and exclude loading. Repeat on Linux and Android.
- Initial engineering targets, not promises: at least halve Grass CPU selection
  cost and reduce retained blade data by an order of magnitude on the mounted
  fixture, with a substantially lower-density presentation accepted visually.

The first deliverable should be local appearance continuity with a density
comparison. The next is patch-bounded generation. Another increase in cache
capacity, worker count or draw distance is not the strategy.

## Native Vertex Lighting Consumer (2026-09-11)

Implemented after `9b7c3b3` (shared toon response), without changing native
geometry, PICA lighting equations, user presets or gameplay/AOT:

- `renderer3ds/pica_surface_lighting_pass.{h,cpp}` is a shared 3DS Vulkan
  producer, called inside the authorized Grass geometry-provider preparation.
  It borrows the resolved native vertex program, packed GPU geometry and current
  uniform buffer. One point draw per eligible surface writes native primary RGB
  to an atlas. It does not replay the scene or read framebuffer pixels/CPU data.
- `renderer3ds/pica_surface_lighting_shader.h` uses typed shader hooks, preserving
  the canonical vertex computation and replacing only the final position with
  an atlas texel coordinate. The canonical draw/shader is not modified.
- Submission ID plus render-target namespace identifies the current draw.
  `grass_scene_bridge.cpp` refreshes the submission even when geometry is reused.
- `grass_surface_reference.h` retains three original vertex indices and two
  normalized barycentric weights in eight bytes. Surface subdivision propagates
  the original triangle weights; placement caches do not bake lighting values.
- CPU fallback and GPU compaction preserve the same reference. The Grass vertex
  shader interpolates three native primary RGB texels at the root, replacing
  its independent ambient/directional estimate. Shared toon response is applied
  next, then native fog. Texture color remains the separate two-nearest-samples
  4x4 grid; the large source texture is not sampled per blade.

### Cost, Ownership And Limits

The producer owns private frame-slot atlas images, descriptors and pipelines;
the existing Grass provider invocation controls its execution before transparent
geometry. It records color-write to vertex-read barriers and relies on the
renderer frame-slot fence before reuse/shutdown. It does not export an image
to postprocessing or change scene/HUD ownership. A future second consumer must
promote this private dependency to an explicit shared graph resource rather
than borrow the Grass-owned lifetime.

Atlas allocation is lazy: two 256x1024 RGBA32F images (8 MiB total), with a
capability fallback to RGBA16F. At most 262144 vertex texels and 256 descriptor
sets per slot/frame; unavailable sources retain the prior Grass response.
There is no cross-frame lighting cache yet. Persistent roots and draw instances
each grow by eight bytes (48 and 68 bytes respectively). Lighting currently
requires three atlas fetches per Grass vertex, not one compute evaluation per
blade. No performance improvement is claimed for this lighting addition.

This implements **native vertex primary RGB**, not complete material/TEV parity:
fragment-lit materials are excluded; combiner multipliers, secondary color and
material-specific fragment effects are not reconstructed by this producer.
Barycentric weights have 8-bit precision. The separate ambient guide used by
other effects is unchanged. Do not describe this as complete native material
lighting or bit-identical final terrain color.

### Verification

- Windows Clang-cl incremental runtime build; no title whole-AOT rebuild.
- 98 Grass/toon tests pass, including original-triangle references after
  subdivision, packing normalization and typed shader-hook preservation.
- Broader foundation suite: 285/286 pass. The settings persistence test
  `GraphicsSettingsRuntimeTest.UsesInjectedPersistencePort` fails because initial
  normalization adds one Store call; this unrelated settings behavior was not
  changed to make the lighting tests pass.
- Kokiri `hudtest`: 180 presentations, native lighting available for 2/2 selected
  surfaces. Mounted Hyrule Field: 180 presentations, available for 3/3 surfaces.
  Both exit normally; actual framebuffer captures were inspected.
- Vulkan/NRI validation enabled in both probes: zero errors. Vulkan warnings
  remain and are not being reported as a clean warning-free validation run.
- Evidence (local, not distributed):
  `C:/Users/xander/triaevum-verify-20260911/grass-native-light-validation` and
  `C:/Users/xander/triaevum-verify-20260911/grass-native-light-field`.
  These bounded capture runs are functional checks, not FPS benchmarks.
- Linux/Android execution, day/night comparison and density-sweep visual
  acceptance remain to be verified. Neither whole-game parity nor a speedup is
  established by the two fixtures.

## Color And TEV Correction After Visual Feedback (2026-09-11)

The user correctly reported that increasing texture blend did not establish
terrain continuity. The previous vertex-lighting probe was not evidence of
complete material response. Investigation separated three issues:

1. The live appearance profile had texture root/tip gains 2.87/4.0. Blend=1
   therefore selected an amplified texture, not a neutral texture response.
   These controls are independent and have not silently been reinterpreted.
2. At gains 1/1 the framebuffer still showed an underlit result. Exporting the
   actual selected canonical shader proved a missing operation: texture0 times
   rounded primary RGB, byte-round/clamp, then TEV RGB scale 2, followed by five
   pass-through stages. This is material state, not an artistic correction.
3. Placement color caches hashed only the presence of a color grid, not its
   values. Two nonempty grids could reuse stale colors. They now fingerprint
   the 16 RGB reference points; equal content still reuses placement. The color
   consumer also uses the observed texture identity used by the mask/selector,
   avoiding the decoded/observed alias mismatch.

### Implementation

`renderer3ds/pica_surface_color_response.h` decodes an explicit capability from
the six native TEV register groups: initial texture0/primary RGB modulation in
either input order, then RGB pass-through stages, each with native 1x/2x/4x
scale. It rejects unsupported sources, modifiers, operations and scale codes.
The canonical frontend passes this typed capability through the Vulkan bridge
and resolved material state to the surface-lighting consumer. No shader-string
recognition, texture hashes or room-specific gains drive rendering.

`grass_shader_sources.cpp` now executes primary rounding, per-stage byte
rounding/clamping and decoded scales in native order, before the shared toon
response and native fog. Simply removing the previous clamp would have been
incorrect: the missing scale, not rounding itself, was the material discrepancy.
More general TEV expressions, fragment lighting and secondary-color combinations
remain outside this capability; unavailable is not reported as full parity.

The explicit opt-in `TRIAEVUM_SURFACE_LIGHTING_DUMP=<directory>` exports only
selected surface canonical shaders and decoded response metadata once per
fragment program per run. These are local game-derived diagnostics, not public
package contents. Normal runs perform no export.

### Reproduction

Local evidence lives under
`C:/Users/xander/triaevum-verify-20260911/grass-color-audit/`:

- `boosted-run`: blend 1, existing gains 2.87/4, before the TEV correction.
- `neutral-run`: same settings and checkpoint, gains 1/1, before correction.
- `materials`: actual canonical shader exposing the missing scale.
- `tev-fixed`: same neutral settings, corrected TEV response; metadata reports
  `available=1 packed_scales=1` (stage0 2x, remaining stages 1x).
- `field-fixed`: independent mounted Hyrule Field fixture.

The user's config and saved Grass preset were not overwritten. `neutral.json`
is a private comparison configuration: blend 1 and gains 1/1. A request to
test terrain inheritance should use these neutral gains, not mistake the old
amplified preset for a neutral reference. Root/tip styling can then be restored
deliberately. Identical root/tip gains naturally remove the artistic gradient
along a blade; this is distinct from spatial terrain lighting.

Windows runtime builds and 100 Grass/toon tests pass, including 1x/2x/4x decode,
swapped modulation operands, invalid expressions, and stale-grid regression.
Kokiri corrected framebuffer probe exits normally with Vulkan/NRI validation
enabled, zero errors and 11 Vulkan warnings. No Linux/Android execution or
performance improvement is claimed by this correction.

## Grazing-Angle Brightening (2026-09-11)

The full toon surface response introduced an additional Grass rim highlight.
It used the terrain normal and a per-blade view direction; as the view became
grazing, `pow(1 - abs(N dot V), width)` approached one. With the tested rim
strength 0.34 this produced a conspicuous bright distant band, independent of
the inherited diffuse terrain response. This was not a reason to disable fog
or compensate distant LOD colors.

Grass now calls the shared `oot3d_toon_diffuse_response`: light bands,
saturation and shadow tint remain, but the diffuse interface intentionally has
no view-direction/normal inputs and does not add rim light. Individual blades
and distant tufts use the same call. Removed the unused view-direction varying
and its vertex calculation. Native PICA toon and its rim remain unchanged;
normal guides used by scene effects are retained. No settings or presets were
changed. The earlier statement that Grass inherits the complete toon response,
including rim, is superseded by this explicit terrain-cover policy.

Verification: 101 Grass/toon tests pass, including a regression that checks the
diffuse consumer cannot invoke the grazing rim while the canonical full response
still contains it. Windows NRI/Vulkan mounted Field captures at presentation 150
use the same checkpoint, camera and settings before/after. Local evidence:
`C:/Users/xander/triaevum-verify-20260911/grass-grazing-before` and
`grass-grazing-after`. A distant Grass rectangle (x100..499, y310..379) changes
mean encoded RGB from (143.85,98.89,44.45) to (102.21,58.33,13.69); this is an
image-domain comparison, not radiometric luminance. The checked hearts and
castle rectangles are pixel-identical. The corrected 180-presentation run exits
normally with zero Vulkan/NRI validation errors (11 Vulkan warnings remain).
No Linux/Android execution or whole-game visual parity is claimed.

## Configurable Nearby Rim (2026-09-11)

User-selected refinement of the preceding policy: retain the shared toon rim
near the camera, fading it out by root distance. Grass still consumes the shared
diffuse response independently; only the additional rim receives
`1 - smoothstep(start, end, rootDistance)`. Fog is applied afterwards.
Native PICA materials, terrain lighting, density and placement are unchanged.

F1 > Grass > Appearance exposes Enable nearby rim, Rim fade start (m) and
Rim fade end (m). Defaults are enabled, full strength through 2 m, zero beyond
8 m. Color, exponent and strength come from the existing global toon controls.
The three Appearance keys ToonRimEnabled, ToonRimFadeStart and ToonRimFadeEnd
persist in configurations and saved Grass presets; distances are stored in
world units (100 per displayed meter). Validation orders the smoothstep edges
and rejects non-finite values. The weight is flat per root, shared by all blade
vertices and LOD representations, so wind cannot modulate the fade. This stays
inside the existing Grass provider without extra passes or CPU root generation.

Verification: 118 focused Grass/toon/persistence tests pass. The unrelated known
GraphicsSettingsRuntimeTest.UsesInjectedPersistencePort store-count failure is
excluded. Actual F1 widget smoke covers editing both distances, unit conversion
and toggling the rim. Windows NRI/Vulkan runs 180 presentations successfully,
with zero validation errors and 11 existing Vulkan warnings. Framebuffer evidence:
`C:/Users/xander/triaevum-verify-20260911/grass-rim-distance`.
Against the preceding rim-off capture, distant pixels x100..499/y240..299,
the checked hearts and castle are identical; closer grass regains the highlight.
This is a bounded visual test, not a performance benchmark or cross-platform test.

## User Grass Default Snapshot (2026-09-11)

Promoted the complete Grass configuration saved after the user's Kokiri session
to the title-owned `triaevum_product_graphics.inc` default, and to the local
GrassSavedPreset. This supersedes the initial nearby-rim defaults for new title
configurations: rim disabled, retained fade range 0..9.01 m. Texture blend is
0.75, root/tip brightness 1.01/1.74, density 1123.9 per square meter, far tuft
density 0.1 with three blades and spread 4. All six texture rules and remaining
Grass values are copied together. Existing unrelated profiles, global toon,
TopScreen and presentation defaults are not changed. Product-default tests
check the snapshot and its compiled export, including GrassSavedPreset equality.

## Preset Cost Audit and Lossless-Intent Optimization (2026-09-11)

The next user-saved preset differs in two fields: density 1862.9000244140625
and TuftTransitionFraction 0.3400000035762787. These are now the product default.
Every current-renderer measurement below used these values; no density, shape,
mask, distance, fog or lighting controls were reduced to manufacture a speedup.
Local Grass and GrassSavedPreset match. Other graphics settings are unchanged.

### Method and Measurement Corrections

The renderer/frontend/bridge surface preceding the 4x4 color work was rebuilt
from `4bd045f`, using the same build configuration and title module. Its old
product Grass preset was tested separately from the current preset. Historical
sources were temporary measurement inputs and have all been restored. The
optimized code remains the development build, not the historical executable.

`probe_renderer.py --throughput --no-captures --frames 900 --warmup-frames 240`
now preserves effects unless `--native-fidelity` is explicitly requested, while
forcing native30_no_interpolation and Original30. It also writes
Graphics.Presentation.VSync=false in the **private probe config**. Previously
the host reported VSync off but the persisted renderer setting re-enabled it;
GPU diagnostics confirmed presentation_vsync=true. Those initial capped runs
are not throughput evidence. The corrected runs confirm false at both layers,
no application limiter, no pacing and no interpolated frames. Shader caches
are warm; the first 240 presentations and the final pending GPU-query slots
are excluded from GPU means. Tests never modify user saves/configuration.

Evidence root: `C:/Users/xander/triaevum-verify-20260911/`. Each probe contains
its executable SHA256, arguments, private config, runtime counters and GPU log.
Windows RTX 3060, output 1280x720, Kokiri legacy hudtest checkpoint:

| Run | Grass GPU mean ms | Total GPU mean ms | Measured native steps/s |
| --- | ---: | ---: | ---: |
| grass-perf-before-free | 2.733 | 4.307 | 85.7 |
| grass-perf-optimized-a | 2.338 | 3.870 | 86.5 |
| grass-perf-before-b | 6.341 | 9.988 | 80.5 |
| grass-perf-optimized-c | 5.379 | 8.836 | 88.5 |

The two before/after comparisons reduce Grass GPU time by about 14-15% and
total GPU time by 10-12%. Absolute GPU times vary substantially: the device
changes power/clock states (a spot check observed P3, 780 MHz), and host work
remains significant. Do not advertise these as fixed game FPS improvements.
An additional optimized run, grass-perf-optimized-b, measured 2.385 ms Grass.
The clean historical renderer + old-preset run grass-perf-historical-old-b
measured 1.783 ms Grass, 3.632 ms total GPU, 87.0 native steps/s. The current
implementation has **not demonstrated lower cost than that historical setup**.
The earlier historical-old-free run overlapped a build and is excluded from
host-performance comparisons. Initial capped historical-new measurements are
also not a basis for an uncapped improvement claim.

### Implemented Changes

- Collapse the validated RGB pass-through TEV scale chain to one CPU-computed
  power-of-two gain per environment, retaining the first byte round and final
  saturation. After byte quantization, the remaining integer gains preserve
  the byte lattice. This is restricted to the decoder's existing capability,
  not an approximation for arbitrary PICA expressions. The canonical native
  material renderer and decoded register representation remain unchanged.
- Evaluate the shared, **unclamped** toon band/saturation/tint transform in the
  Grass vertex shader. The lighting input is constant for every vertex of a
  root, so this color-linear transform commutes with perspective interpolation.
  Clamp still happens in the fragment shader, before rim and fog. Remove the
  unused lighting varying. Native terrain lighting, shadows and toon controls
  remain active; no lighting term is dropped to reduce cost.

Validation: 119 focused Grass/toon/persistence tests pass, including all
729 valid scale combinations times 256 byte values. Eight probe tests pass.
The known unrelated UsesInjectedPersistencePort store-count test remains
excluded. A Vulkan validation run has zero errors, 11 existing warnings.
No F1 controls were changed.

Framebuffers use the same preset, checkpoint and explicit 1/60 fixed delta
(capture profiles are intentionally separate from throughput profiles):
grass-opt-fixed-before/after differ by 1 pixel at presentation 120 and 2 pixels
at 150 out of 921600, maximum encoded-channel delta 8. In
grass-intro-before/optimized, presentations 120, 300, 660, 840 are identical;
480 differs by one pixel, maximum delta 8. This is effectively unchanged for
these samples, not a claim of bit-exact output for every scene or toon profile.
Earlier non-fixed captures had different animation phases and are discarded
as pixel-comparison evidence.

### Remaining Work, Ordered by Expected Benefit

1. Move root-constant native lighting fetches and material endpoint evaluation
   into the existing GPU instance-preparation stage, once per visible root
   instead of repeatedly for each strip vertex. This needs an explicit
   environment/atlas-range input contract and the existing CPU fallback; do
   not add a second scene renderer or read the atlas back to the CPU.
2. Measure cold placement generation, moving-frustum selection and static
   uploads separately. The 4x4 two-nearest color is already cached in roots;
   it is not a full texture lookup performed on the CPU every frame.
3. Only consider geometry/LOD/coverage reductions after the above, using
   fixed-time framebuffers and quantified silhouettes/coverage. Keep the
   user's chosen preset intact unless a visual tradeoff is approved.

The mounted Field checkpoint contains a native30_interpolated clock and rejects
a native30_no_interpolation benchmark. The three grass-field-* probes correctly
failed before rendering; they are not performance measurements. Obtain a
compatible fixture through the runtime's supported save path before claiming
Field throughput. Do not patch out the timing-contract check. Linux/Android and
whole-game performance remain untested in this tranche.

### User-Approved Density 1024

The user then explicitly requested 1024 instead of 1862.9. Updated active
Grass, GrassSavedPreset and the title product snapshot to 1024; every other
Grass setting is unchanged, including TuftTransitionFraction 0.34. The product
snapshot tests now require this density. The following consecutive runs use
the same optimized executable and methodology, no builds running concurrently:

| Run | Density | Grass GPU mean ms | Total GPU mean ms | Native steps/s |
| --- | ---: | ---: | ---: | ---: |
| grass-perf-1024-a | 1024 | 3.098 | 6.613 | 94.2 |
| grass-perf-1863-control | 1862.9 | 5.412 | 8.913 | 90.6 |
| grass-perf-1024-b | 1024 | 3.088 | 6.610 | 92.9 |
| grass-perf-historical-old-c | historical preset | 3.373 | 6.904 | 91.1 |

At 1024 the Grass GPU cost falls about 43% versus the current renderer at
1862.9, with total GPU time about 26% lower. In this consecutive run block it
is also about 8% below the historical renderer/old-preset Grass time. This is
the measured Kokiri case, not proof that every scene is faster than history.
Host throughput improves much less, consistent with remaining non-Grass work.
The earlier absolute timings at different device power states must not be
mixed with this block to invent a larger improvement.

`grass-image-1024` contains the fixed-time framebuffer check. Coverage remains
dense but the placement is deliberately different because the user changed
density. Do not describe that change as pixel-identical; the 1-2 pixel figures
above concern shader optimization alone at unchanged density.
