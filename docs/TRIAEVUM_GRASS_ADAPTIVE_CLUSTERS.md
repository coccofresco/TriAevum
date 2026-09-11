# Adaptive Grass Clusters

Experimental successor to [fixed clusters](TRIAEVUM_GRASS_MIDRANGE_CLUSTERS.md).
Enable both `Graphics.Grass.Performance.MidrangeClustersEnabled` and
`MidrangeAdaptiveEnabled` in a private configuration. Product defaults and the
live preset remain unchanged; no additional F1 control is advertised yet.

## Ownership and Mask Contract

The extractor still owns acceptance of every root using the original selected
texture channel, mask levels, inversion, response curve, distribution, slope
and spacing. Adaptive grouping never adds or relocates roots. Lighting, color,
wind, interactions, native depth/fog and the effect graph are unchanged.

`grass_mask_interior.h` builds a conservative interior certificate from the
actual scalar mask and `EvaluateGrassMaskLevel`. Masks up to 512 pixels per
axis are indexed at exact texel resolution. Larger masks use conservative
tiles, bounding the index to approximately 1 MiB. Each tile is marked
non-interior if ANY texel evaluates to zero. A summed-area table answers
rectangle queries in constant time. The 256 possible byte values are evaluated
once, not through the nonlinear mask function for every texel or every frame.
Small holes cannot disappear in an average or a downsample. Coarse tiles can
prevent an otherwise valid merge, never certify a hole as filled.

The world-placement producer queries the UV bounds of the accepted roots in a
candidate group. It uses the existing canonical wrap function; ranges crossing
a repeat/mirror boundary conservatively cover the full texture axis. This can
over-subdivide, but cannot omit a seam or a hole from the certificate. The
certificate is temporary build data; the resulting groups/index are cached.
Mask data, rule and adaptive mode participate in invalidation.

## Spatial Partition

`BuildGrassMidrangeClusters` groups separately per placement/mask owner and
exact supporting triangle. With adaptive mode:

- Start with cells four times `MidrangeClusterCellExtent` along each axis.
- Merge a candidate only if its accepted-root UV rectangle is certified
  interior and it fits the 128-root technical capacity.
- Otherwise subdivide in two octree levels down to the configured base extent.
- Uncertified base cells retain the former 50-root capacity. Dense certified
  base cells may use 128; excessive occupancy is split without widening.
- Restore stable randomized-ID order inside each completed group so distant
  nested prefixes do not preferentially retain one spatial corner.

The numbers 50/128 are draw capacities, not a substitute for mask validity.
Cells never grow beyond the maximum extent to fill a quota. Different supports
or floors never merge; no new geometry connects the roots. Future surface
adjacency work could relax exact-triangle separation, but is not implemented.

`grass_cluster_limits.h` is the shared CPU/indexed-topology capacity contract.
The GPU executes the actual occupied prefix, not 128 vertices' worth of children
for every sparse group. Existing per-cluster density, far-detail selection,
spatial culling and density bounds remain in use. This is still indexed
instancing and storage buffers, without mesh/geometry-shader requirements.

## Validation

The standalone cluster test exercises actual LOD code plus the adaptive
partition: a uniform support with 120 roots becomes one group; a single black
texel inside its UV bounds forces subdivision; every large group must pass
the interior query. Accepted grayscale values retain their already-generated
probability distribution: they are not incorrectly classified as holes.
All roots occur once, and reversed input traversal gives
the same stable membership. Existing support separation, capacity, topology,
budget, far-prefix and visibility-bound tests remain applicable. Indexed
topology tests now cover all 128 possible children.

Framebuffer sidecars expose `prepared_draw_clusters` and `large_draw_clusters`
to distinguish actual adaptive preparation from a merely enabled setting.
They also preserve selected-root/group counts and CPU-stage diagnostics.

The comparison protocol remains fixed native 30 Hz simulation, 1320 steps,
180 warmup steps, VSync/pacing/interpolation off, with separate framebuffer
capture runs. Selected GPU windows are [270,330), [810,870), [990,1050),
[1170,1230). No ROM, captures, savestates or mask payloads belong in this repo.

## Promotion Requirements

Do not infer performance from a lower group count. Compare full host cost,
GPU Grass and total GPU time with the fixed-cluster control on the same build.
Larger groups alter density correlations and near/far handover; evaluate
coverage and camera-motion transitions before changing the live preset.
Neither the -33% overall target nor cross-platform validation follows from
passing the structural tests. Linux/Android and interpolated modes need their
own execution checks before promotion.

## Measured Windows Result (2026-09-11)

Private evidence under `C:/Users/xander/triaevum-verify-20260911/`:
`grass-adaptive-certified` and reversed-order timing-only runs
`grass-adaptive-repeat-adaptive-clusters` / `grass-adaptive-repeat-fixed-clusters`.
The control is the previous experimental fixed-cluster configuration, not the
live product preset. Both variants use base extent 22, far fraction 0.25,
segment transition 250-500, and LOD reference distance 1200.

| Pair / mode | Host ms per native step | Selected total GPU ms | Grass GPU ms |
| --- | ---: | ---: | ---: |
| First / fixed | 15.191 | 3.925 | 0.904 |
| First / adaptive | 10.955 | 3.402 | 0.781 |
| Reverse repeat / fixed | 12.359 | 3.885 | 0.888 |
| Reverse repeat / adaptive | 10.733 | 3.338 | 0.771 |

Host improvement ranges from 13.2% to 27.9%; report the conservative repeated
13% result rather than promising 28%. Native PICA GPU timing also changed
between runs, so the total GPU improvement cannot be attributed solely to
Grass geometry. No new historical-executable comparison was made: this does
not establish the overall -33% target relative to that historical baseline.

Prepared groups: 443069 fixed versus 183182 adaptive, including 41821 groups
above 50 roots. At frame 840, drawn group instances fall from 7248 to 3200;
visible roots remain 32585 versus 32506. At frame 1200, instances fall from
6872 to 2906, with 32919 versus 32424 visible roots. The reduction primarily
comes from grouping and selection, not lowering the configured density.

Across the four wide framebuffer comparisons, adaptive-versus-fixed mean MAE
is 0.2454/255 (lower two-thirds: 0.3670/255). Changed-pixel fractions range
from 1.76% to 3.43%. This is not pixel identity, and isolated differences are
larger than those means. The fixed path matches all seven captures of the
previous fixed-cluster build exactly despite extending GPU capacity to 128.
Grass is present in every sampled frame; first-frame availability and long
camera-motion quality were not separately validated.

The first `grass-adaptive` series did not exercise adaptation and is excluded
from adaptive performance claims. Diagnostics in `grass-adaptive-mask-diagnostic`
showed why: the meadow mask had 44987 partially admitted texels and only 11
excluded texels, but a white-only certificate classified all coarse tiles as
boundaries. The corrected certificate tests exclusion (zero), preserving
grayscale through existing root acceptance, and indexes these small masks at
exact texel resolution. The comparison manifest now explicitly requires
nonzero large-group preparation, rejecting a silently inactive adaptive case.

Full runtime build, standalone cluster invariants and comparison-harness tests
pass. No game is left running. The preset remains unchanged and the feature
remains opt-in pending user review and cross-platform validation.
