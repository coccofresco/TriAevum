# Repeatable Midrange Grass Clusters

The subsequent [adaptive grouping implementation](TRIAEVUM_GRASS_ADAPTIVE_CLUSTERS.md)
uses mask-certified spatial subdivision and a separate 128-root GPU capacity;
50-root figures below describe the fixed-cluster implementation and its tests.

Status: experimental GPU consumer and cluster-owned selection implemented,
2026-09-11. The original equivalent-geometry prototype and its measurements
are retained below as history. The cluster-owned successor is described in
the final section; it remains opt-in, not a promoted product preset.
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

The section above records the initial CPU tranche. The GPU consumer and its
in-game validation are described below. No F1 toggle or product preset
advertises this experimental path.

## GPU Consumer and Measured Limitation

Opt in through `Graphics.Grass.Performance.MidrangeClustersEnabled`; the
optional `MidrangeClusterCellExtent` defaults to 22 world units. Default is
off. Enabling it suppresses the old far cutout tufts for an unambiguous test.
The async placement cache retains group membership and an inverse root map.
`PackGrassMidrangeDraws` groups selected roots in the one/two-segment bins.
The actual GPU topology is the extension in `grass_indexed_topology.h`,
repeating the canonical blade indices for up to 50 children and one/two
planes. `BuildGrassMidrangeTopology` above remains a prototype helper, not
the topology used by this consumer.

`interactive_grass_pass.cpp` batches groups by occupancy, issuing instanced
draws with only the occupied index prefix, not a draw per group. The vertex
shader reads exact prepared roots and group descriptors from storage buffers.
Root positions, color, live native lighting, wind and actor interaction stay
on the existing Grass path. Compaction barriers cover vertex shader reads.
Native PICA, UI composition and the product preset are unchanged.

IMPORTANT: individual-root thinning still precedes grouping. In the wide
intro frames 300/840/1020/1200, respectively 32195/74352/75409/73370 clustered
blades occupy 11051/35219/36353/36009 group instances: only about 2-3 surviving
blades per group. This is not yet the intended 50-blade amortization. Existing
LOD-bin splits and cell/support boundaries also constrain occupancy.

### Verification

The full Windows runtime builds successfully. Standalone cluster invariants
pass, including canonical GPU topology parity and selected-root packing.
Three comparison-harness tests pass; the full foundation suite was not run.
Framebuffer sidecars now record successful Grass draws and cluster counts.
The harness rejects absent Grass GPU timings or inactive experimental draws.
An initial shader-compilation failure was corrected and its timings discarded.

Private evidence root: `C:/Users/xander/triaevum-verify-20260911/`.
Protocol: boot through 1320 fixed native 30 Hz steps, 180 warmup steps,
1280x720, no interpolation, VSync or pacing in throughput runs. Captures run
separately. GPU means use windows [270,330), [810,870), [990,1050),
[1170,1230); host time covers the entire post-warmup run.

`grass-cluster-render-occupancy` compares identical density/segment settings
with old tufts disabled. The cluster-equivalent variant has identical captured
pixels at all four wide frames; frame 480 differs by two pixels. The earlier
segment reduction variant has mean wide-view MAE 0.7165/255 relative to this
control, but does not improve host cost. This control is NOT the live preset.

Separate paired timing repeats, using the final occupancy consumer:

| Run directory | Selected total GPU ms | Grass GPU ms | Host ms/step |
| --- | ---: | ---: | ---: |
| grass-cluster-control-repeat | 3.925 | 1.129 | 14.944 |
| grass-cluster-equivalent-repeat | 3.739 | 1.009 | 18.535 |

GPU differences vary between runs (including native PICA timing), so no
stable GPU speedup is established. Host time is about 24% worse in this pair;
the preceding series was also slower. Per-root selection/sorting remains a
cost, and fewer draw instances alone do not demonstrate an optimization.

`grass-cluster-default-regression` matches `grass-intro-wide-reference`
exactly in all seven captured BMPs: experimental-off preserves the existing
1024 preset in these samples. BMP parity is capture-format parity, not proof
of floating-point identity. Linux, Android and interpolated modes are not
validated by this tranche. No running game or changed live preset is left.

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

Retain individual curved blades nearby. In the middle, select compact clusters
representing up to about 50 accepted blades BEFORE thinning individual roots.
Farther away, represent the same bounded footprint with fewer silhouettes and
vertices, then thin whole clusters with a gradual fade. This supersedes the
old recommendation to keep an independent far-tuft system. Counts and shapes
are experimental rendering choices, not native game data. Fewer silhouettes
must preserve apparent coverage without widening across invalid mask regions.
Avoid sorting every surviving root each frame; reuse immutable cluster
membership and select/budget clusters as units.

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

## Cluster-Owned Selection (Successor)

`grass_cluster_selection.h::SelectGrassDrawableClusters` now selects ownership
and density before emitting children. A grouped draw retains or rejects the
whole cluster, rather than grouping individually thinned survivors. The shader
uses the same representative root's position and visibility seed for all
children, preventing a second independent per-blade fade. Color, lighting,
wind and interaction still use each child's actual root. Near geometry remains
individual; topology ownership switches per group so representations cannot
overlap. Near topology selection is consequently not pixel-identical to the
old independent per-root selection at the transition.

The immutable placement stores a stackless spatial hierarchy over groups.
Bounds include accepted-root extent and maximum blade extent; traversal skips
whole invisible regions. Cache accounting includes the index. Its fixed
traversal order also defines stable budget priority without a per-frame sort.
The prepared root stream is already group-contiguous, so draw packing skips
the old individual-index sort. Occupancy-bucketed draws remain in use.

`Graphics.Grass.Performance.MidrangeFarBladeFraction` is an experimental
manual configuration parameter (default 0.25; 1 disables child reduction).
Between `SegmentLodEndDistance` and `LodReferenceDistance` (bounded by draw
distance), smooth progression selects a nested prefix of the existing stable
members. A full 50-root group reaches 13 roots at fraction 0.25. Sparse groups
remain sparse, at least one root survives, and no root is moved or invented.
Whole-cluster density and final draw-distance fade remain independent stages.
The initial use of maximum draw distance as the reduction endpoint was too
remote to exercise the feature meaningfully in the sampled intro views; that
is corrected to the established LOD reference distance.

This is a reduced geometric cluster, not yet a coverage-compensated far
template: it does not widen blades or guarantee the original silhouette.
Child-count changes are discrete within a smooth distance schedule. Their
temporal visibility and the near/group handover still require a moving-view
quality pass before promotion. No new F1 controls or live/default changes are
made by this tranche.

Tests now cover whole-group retention despite individually rejected children,
indivisible budgets, distance rejection, stable nested far subsets, sparse
masks, spatial leaves/bounds/escape links, and sort-free packing. The standalone
test links the actual `grass_visibility.cpp`, not a duplicate LOD model.
Framebuffer sidecars additionally expose CPU preparation/selection/upload
times and upload bytes for diagnosis; those capture-run samples are not the
throughput benchmark.

Intermediate private evidence: `grass-cluster-owned-selection` (linear group
scan) and `grass-cluster-hierarchy` (indexed scan, before removing the candidate
sort and correcting the far endpoint). At frame 840 the full cluster path
draws 75281 roots in 7248 group instances, rather than the previous prototype's
74352 roots in 35219 instances. The linear scan examined 443069 groups;
the index reduced this to 121341 candidate groups without changing that
frame's selected roots. Fewer instances alone still did not establish a total
speedup: the intermediate full-cluster host average was 19.948 ms, versus
14.183 ms for its individual control. Do not present these intermediate runs
as a completed performance optimization.

### Final Validation and Remaining Target

The index also stores a conservative minimum representative visibility seed.
Where an entire node is guaranteed to use grouped topology, its nearest-point
density upper bound can reject the node before visiting its groups. This is
not applied to nodes that could still contain individually selected near
blades. At frame 840 candidate groups fall from 121341 to 43015. All seven
captures are pixel-identical before/after this optimization. Removing the
candidate sort likewise preserved all seven full-cluster captures.

Final evidence: `grass-cluster-density-bounds`, with reversed-order timing-only
repeats `grass-cluster-repeat-cluster-far` and
`grass-cluster-repeat-individual-control`. Same 1320/180 frame protocol as
above; no compilation or screenshot capture during these final timing runs.

| Pair / variant | Host ms/step | Selected total GPU ms | Selected Grass GPU ms |
| --- | ---: | ---: | ---: |
| First / individual control | 13.106 | 4.137 | 1.170 |
| First / reduced far clusters | 12.373 | 3.972 | 0.921 |
| Reverse repeat / individual control | 17.042 | 4.169 | 1.156 |
| Reverse repeat / reduced far clusters | 16.098 | 3.915 | 0.869 |

Absolute host timings vary substantially between pairs; the within-pair
improvement is about 5.5-5.6%. Grass GPU time falls 21.2-24.8%, while total
selected GPU time falls only 4.0-6.1%. This does NOT meet the overall -33%
target, and is not a comparison against a fresh historical executable or the
unchanged live preset. Do not report the Grass-only reduction as total gain.

At frame 840 the full cluster variant renders 79804 roots, reduced-far 32585,
including near individuals. Compared with full clusters, far reduction changes
0.26% of pixels in that frame. Across the four wide views its mean MAE is
0.0615/255; relative to the individual test control it is 0.3538/255. Relative
to the original unchanged 1024 image reference it is 0.8549/255 (lower-two-thirds
MAE 1.2797/255). These are different references, not interchangeable claims of
parity. Rare local differences can be much larger than full-image averages.

The current executable was built and run successfully; standalone invariants
and the three Python harness tests pass. Captures confirm Grass is drawn in
all seven sampled views and native-frame interpolation is disabled. The live
configuration and product defaults remain unchanged. No Linux/Android,
interpolated-mode, first-frame, or prolonged moving-view acceptance is claimed.

The next performance work should use selection costs rather than just draw
instance counts: frame-840 diagnostic selection drops to 4.881 ms, but is
still substantial; the current implementation still traverses candidates and
emits/compacts individual child records on CPU/GPU each frame. Reusing compact
cluster selections and reducing this per-child preparation is the remaining
structural opportunity. Validate temporal handover and far silhouette before
promoting the experimental path or adding user-facing controls.
