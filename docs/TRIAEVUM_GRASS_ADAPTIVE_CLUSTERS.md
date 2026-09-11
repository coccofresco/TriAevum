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
  interior and it fits `MidrangeAdaptiveCapacity` (128 by default, configurable
  from 50 to 10000 under `Graphics.Grass.Performance`).
- Otherwise subdivide in two octree levels down to the configured base extent.
- Uncertified base cells retain the former 50-root capacity. Dense certified
  base cells may use the configured capacity; excessive occupancy is split
  without widening.
- Restore stable randomized-ID order inside each completed group so distant
  nested prefixes do not preferentially retain one spatial corner.

These capacities are not a substitute for mask validity.
Cells never grow beyond the maximum extent to fill a quota. Different supports
or floors never merge; no new geometry connects the roots. Future surface
adjacency work could relax exact-triangle separation, but is not implemented.

`grass_cluster_limits.h` is the shared CPU/indexed-topology capacity contract.
The GPU executes the actual occupied prefix, not 10000 blades' worth of children
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
topology tests now cover all 10000 possible children. The production index
stream and draw-batch member count are 32-bit: two-plane/two-segment groups
can exceed 65535 vertices, and member counts must not truncate at 255.
The standalone test additionally exercises 12000 roots with capacities 128,
256, 5000 and 10000, exact membership and GPU descriptor coverage, and an
interior mask hole forcing the original 50-root boundary partition.

Framebuffer sidecars expose `prepared_draw_clusters` and `large_draw_clusters`
to distinguish actual adaptive preparation from a merely enabled setting.
They also preserve selected-root/group counts and CPU-stage diagnostics.
`maximum_cluster_members` reports actual prepared occupancy and
`capacity_limited_ranges` counts certified candidate ranges subdivided or
split because they exceeded the configured capacity. The latter is a build
decision count across tree levels, not a count of final saturated groups.
Both are cached statistics, without an additional per-frame group traversal.
Capacity participates in asynchronous and world-placement cache keys only
when adaptive mode is active; disabled/fixed-cluster behavior remains inert.

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

## Capacity Expansion and Coverage Review (2026-09-11)

The user permits up to 5000/10000 roots where mask boundaries remain respected.
The implementation supports that limit without changing root acceptance.
Private evidence: `grass-capacity` and `grass-capacity-extent`, using the same
protocol above. All rows below use the earlier, aggressive distance policy.

| Capacity / base extent | Prepared groups | Actual maximum roots | Capacity-limited ranges | Host ms | Selected GPU ms | Grass GPU ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 128 / 22 | 183182 | 128 | 10766 | 11.472 | 3.189 | 0.742 |
| 256 / 22 | 152687 | 256 | 1559 | 10.435 | 3.148 | 0.718 |
| 5000 / 22 | 146858 | 363 | 0 | 10.728 | 3.284 | 0.759 |
| 10000 / 22 | 146858 | 363 | 0 | 12.951 | 3.267 | 0.785 |
| 10000 / 44 | 74257 | 1196 | 0 | 11.788 | 2.964 | 0.704 |
| 10000 / 88 | 59309 | 3558 | 0 | 12.431 | 3.039 | 0.759 |

At base extent 22, capacity stops being the constraint at 363. The 5000 and
10000 variants have identical membership, selected-root counts and all seven
framebuffers. Their differing host times demonstrate run variability, not an
intrinsic performance difference. Enlarging cells lowers group count but does
not establish a total-performance win. Do not promote the largest variant
solely because it groups more roots.

The 128-capacity run matches all seven previous adaptive-128 framebuffers
exactly after the 32-bit index migration. Mean absolute differences over the
four wide captures, relative to 128, are 0.0932/255 for 256 and 0.1074/255 for
5000/10000 (lower two-thirds: 0.1396 and 0.1609). These are spatial image
metrics, not acceptance of distant coverage or motion.

**User rejection overrides the small pixel-difference metric:** the tested
distance policy makes grass disappear too early. Its LOD reference is 1200
and start fraction 0.12 (density reduction starts at 144), versus the live
preset's reference 1668 and start fraction 1. The extra cluster child schedule
also retains only 25% at 1200. Group density, child retention and fragment
fade compound; the hard draw distance remains 50000 and is not the early
cutoff. None of these experimental configurations is an approved default.

`grass-coverage-plan.json` restores reference 1668, start fraction 1 and
segment transition 501-1000, raises far child retention to 0.5, and compares
128/22 against 10000/44 at identical density policy. Density fade remains
0.3 for this experiment, rather than the live preset's 1.0. This is an explicit
coverage-first candidate, not a silent rewrite of the user's saved preset.
Evaluate the distant terrain directly; unchanged sky/UI must not dilute the
acceptance metric. New results cannot be compared as equal-quality speedups
against the rejected low-coverage policy.

The user confirmed that the problem is distant coverage, not the whole pass
disappearing. Completed `grass-coverage` and reversed timing-only
`grass-coverage-repeat-coverage-large` / `grass-coverage-repeat-coverage-control`:

| Pair / mode | Host ms per native step | Selected total GPU ms | Grass GPU ms |
| --- | ---: | ---: | ---: |
| First / coverage control 128/22 | 11.944 | 3.865 | 1.505 |
| First / larger clusters 10000/44 | 10.772 | 3.501 | 1.379 |
| Reverse / larger clusters 10000/44 | 13.615 | 3.675 | 1.424 |
| Reverse / coverage control 128/22 | 15.203 | 3.922 | 1.484 |

Larger clusters reduce host step time by 9.8-10.4% against this equal-policy
control and selected total GPU time by 6.3-9.4%. This is an intermediate
comparison, NOT the original -33% versus historical acceptance test.
At frames 840/1020/1200 the candidate retains 103441/106485/101265 roots,
versus 35112/34355/32891 with the rejected aggressive policy at the same cell
extent. Larger clusters versus the new 128 control have wide-view mean MAE
0.0301/255, lower-two-thirds MAE 0.0439, and at most 0.607% changed pixels.
Framebuffer inspection shows a more continuous distant cover; user visual
acceptance is still required. The saved live/product preset is unchanged.

### Original Acceptance Target Remains Binding

The target is at least 33% less total render time than the historical renderer
and its historical preset, on the wide intro views, while retaining acceptable
distant cover. A low Grass-only time, fewer clusters, or an improvement versus
another experimental variant is not completion. Previous historical selected
GPU measurements vary (5.409, 5.739, 5.946 ms; see the terrain-continuity
strategy). They cannot be cherry-picked as a denominator for today's runs.
The next acceptance comparison must pair the historical executable/preset and
the coverage-first candidate, same native frames/windows/output, warmup and
uncapped/no-interpolation timing, repeated in reverse order. Record both total
GPU and total host step cost. Do not claim the -33% target until that fresh
pair and the distant-coverage review pass. All current automated runs completed
and closed; no build or game process was left running.
