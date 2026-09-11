# Repeatable Midrange Grass Clusters

Status: CPU grouping and shared index topology implemented, 2026-09-11.
The new 3D cluster draw/LOD consumer is not connected yet.
The current 1024 default is unchanged. See
[terrain continuity](TRIAEVUM_GRASS_TERRAIN_CONTINUITY_STRATEGY.md) for the
fixed-time wide-intro measurement protocol and historical references.

## First Implementation: Maximum 50 Accepted Roots

`grass_midrange_clusters.h` groups existing accepted roots into explicit
world-space cells, with a hard limit of 50 members per group. Cells include
height and exact triangle support, keeping floors and native triangle seams
separate. Construction is scoped to one placement/mask owner. Root order is
not modified: the result is an index table plus group bounds. Sorting by
cell/support/stable ID makes membership independent of input traversal order
when IDs are unique. No camera input, new positions, mask filling or duplicate
root/color payload is introduced. Sparse groups stay sparse; the footprint
does not grow to reach 50. Invalid extent/coordinates fail explicitly.

`GrassWorldPlacementRequest::MidrangeCellExtent` opts into this producer after
the existing world transformation and mask extraction. Zero keeps it disabled
without changing the existing cache content hash. Enabling/changing extent
invalidates the affected placement. Exact triangle indices are separated from
packed barycentric weights, so different points on one triangle can group
without mixing unrelated supports. `GrassWorldPlacement::Midrange` retains
the immutable result; no per-frame build is added.

`BuildGrassMidrangeTopology` supplies one shared 50-child indexed topology
for either one or two segments. Child indices never cross-connect; partial
groups draw a prefix. The initial representation preserves every admitted
root, with 100/200 triangles for a full group, rather than inventing unmasked
positions from a generic template. This is repeated topology with different
root references, not yet a compressed set of recurring geometric patterns.
The radius covers roots only: a draw consumer must expand it for blade shape,
wind and actor bending, as the current Grass path already does.

The dependency-free `oot3d_grass_midrange_clusters_tests` target covers capacity,
one-to-one membership, stable order, bounds, supports, stacked/negative cells,
empty/sparse masks, invalid input and topology prefixes. GCC compiles/runs it
separately from the game; the changed placement consumer also passes a C++20
syntax check. These are CPU/contract checks, NOT an in-game validation.

Remaining: upload/bind the member/group tables in the Grass pass, select group
LOD alongside individual roots without double coverage, resolve each member's
native lighting/color and interactions, then measure the new path. No F1
toggle or product preset advertises this incomplete path; previous tuft tests
must not be presented as measurements of these 50-root groups.

## What Already Exists

- `grass_surface_extractor.cpp::ClusterWeight` is seeded world-space noise
  modulating placement acceptance. It does not instance shared clump meshes;
  increasing it can create bare patches rather than save equivalent geometry.
- `grass_world_placement_cache.h::GrassWorldCluster` groups existing roots for
  visibility. It is a culling structure, not a shared drawable shape.
- `GrassStaticPlacementCache` keeps camera-independent roots; each full root
  is 48 bytes, with a separate 16-byte culling record. Its worker cache is
  byte-bounded. Reusing this ownership avoids another scene-scale cache.
- `grass_distant_tuft.h` and `grass_shader_sources.cpp` already replace a
  distant root with one cutout quad representing several silhouettes.
  Retention accounts for blade count and transition growth, not spread as
  occupied area. A wider quad can increase discarded fragments without
  increasing actual blade area. Nine silhouettes also cost more per fragment
  than five in the current cutout loop; fewer instances alone is not proof
  of lower total GPU cost.
- `GrassSelectionCache` reuses selection only. Wind, actor interactions,
  native lighting and fog must still refresh every presentation.

## Recommended Representation

Retain individual curved blades nearby, use small instanced 3D clumps in the
middle, and retain compact cutout tufts far away. A midrange clump should
represent several blades with one/two-segment geometry, not several complete
copies of the near mesh. Prototype a small template set (for example eight
templates with five blades); these counts are experimental, not native data.

Templates share immutable vertex/index buffers. Each blade has a fixed local
root, bend, width and phase. A stable hash of surface identity, cell and seed
selects template, rotation and modest scale variation. No frame/time/camera
value may choose a new root pattern. Space anchors by deterministic jittered
cells to reduce random holes; reuse the existing placement's native surface
identity and mask acceptance. Do not simply repeat a visible rectangular grid.

Build eligibility once with static placement: project child roots onto the
actual triangle, reject invalid mask samples, and split/fall back near material
seams, steep normal changes or triangle boundaries. Never move children onto
unrelated surfaces or draw them across paths because the central root passed
the mask. A compact cluster may use a small child-validity mask; partially
valid clusters must not distort the instance budget or coverage estimate.

Keep static placement and selection hierarchies distinct. Reuse hierarchy
bounds for coarse culling, including blade height, bend and interaction bounds.
Issue instanced batches by template/material/LOD, not a draw per cluster. A
plain indexed-instancing path avoids introducing geometry/mesh shader
requirements for Android and Steam Deck. Count actual selected triangles,
vertices, cutout fragments, transfer bytes and selection time as well as roots.

Shade through the existing terrain-color and PICA surface-lighting inputs.
Cluster-local UV/root offsets still select the established two-nearest 4x4
color interpolation; do not bake live native lighting into the static cache.
The group can share environment evaluation, but color/normal discontinuities
require subdivision or fallback, not unconditional centroid averaging.
Keep fog, toon, actor bending, native composition and HUD isolation unchanged.

## Transition and Ownership

Use stable parent/child identities so individual blades become their matching
cluster without appearing twice. Make the LOD decision conservative across
the cluster bounds, with complementary stable fade weights and no random
resampling on camera cuts. World-distance controls remain owned by Grass;
projected extent can impose a quality safeguard for FOV/resolution changes.
Screen coverage is a budget/quality input, never a placement seed.

Implement template data and validation in an isolated Grass cluster module;
placement owns eligibility and bounds, visibility owns LOD/budgets, the Grass
pass owns buffers/batches. Do not modify canonical PICA, UI composition or
title-specific gameplay. Do not overload the existing distribution-noise
controls with template-selection semantics.

## Acceptance Before Promotion

1. Unit-test stable generation, bounded spacing, mask-zero exclusion, slope
   and seam fallback, parent/child identity and conservative culling bounds.
2. Measure an A/B at fixed 30 Hz, VSync/pacing/interpolation off; no captures
   during timing. Re-run the historical control in the same measurement block.
3. Compare the existing four wide-intro views, especially the bright hill
   silhouette at frame 840. Add grazing-angle and near/mid transition captures;
   full-image MAE alone hides holes behind the unchanged sky and logo.
4. Measure camera-motion selection cost and first-frame availability, not
   only static GPU draw time. Re-enter a frustum and cross the LOD boundary:
   no swimming, pop-in, persistent holes or density doubling.
5. Keep a candidate opt-in until measured coverage and total cost improve.
   Shared architecture does not replace later Linux/Android validation.
