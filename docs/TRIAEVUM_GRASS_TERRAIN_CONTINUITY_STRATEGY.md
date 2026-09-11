# Grass: Terrain Continuity and Bounded Cost

Date: 2026-09-11. Reviewed implementation: `0079a43`.
Status: research and code audit, not an implemented performance improvement.

## Decision

Make Grass extend the terrain's local appearance, then spend geometry only on
visible silhouettes. Do not compensate for a color/lighting mismatch with more
blades, larger caches, or a longer draw distance. Retain the existing NRI effect
boundary and portable Vulkan path. This is an optional extension, not a change
to native OOT3D rendering or an attempt to reproduce BotW gameplay.

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
triangle+barycentric identity. Sample local RGB with the native mapper/wrap and
the selected texture policy instead of recomputing an average for every root.
Cache static samples per patch/material generation, not per presentation.

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
