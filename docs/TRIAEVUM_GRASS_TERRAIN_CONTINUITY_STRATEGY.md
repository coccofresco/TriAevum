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
