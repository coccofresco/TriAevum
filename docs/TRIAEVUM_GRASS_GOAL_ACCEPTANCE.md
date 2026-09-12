# Grass: Historical -33% Acceptance

## Binding Target

Reduce total time by at least 33% versus the historical renderer/preset, retain
1024 roots per square metre near the camera, and improve apparent average
coverage versus the user's unchanged 1024 preset. Earlier disappearance is
not an acceptable optimization. Grass-only time and comparisons against
other experimental presets do not establish completion.

Status: **paired performance and coverage target passed**. The accepted
configuration is promoted to product defaults and the local saved Grass preset;
the rebuilt-product smoke check also passed.

## Frozen References

Private evidence root: `C:/Users/xander/triaevum-verify-20260911/`.

- Historical renderer/frontend/bridge: commit `4bd045f`, rebuilt with the
  same host/toolchain/title module. Executable in `grass-historical-runtime/`,
  SHA256 `10b6580e9892c8e44b53a21574f553f402fa3b3dd3b537a056966dc9995f92c3`.
  Profile `grass-historical-native.launch.json` points at the unchanged
  `grass-intro-historical-repeat/config.json`. Historical density is 1048.8;
  do not replace that historical preset with the newer visual reference.
- Visual reference: unchanged `grass-intro-wide-reference/config.json`,
  density 1024. Recaptured in `grass-goal-reference-current/` with the bucketed
  renderer. Six of seven captures match the older reference byte-for-byte;
  frame 1200 differs at exactly one pixel (one RGB component by 9/255).
  Final coverage is also checked directly against the original captures,
  producing the same scores, rather than substituting the recapture.
- Grass-off background: `grass-goal-coverage-off/`, same scene/configuration
  with only Grass disabled. Never use this for performance acceptance.

## Protocol

1320 native fixed 1/30 steps, first 180 excluded; 1280x720; no interpolation,
VSync, frame limiter or pacing. Host total covers all 1140 measured steps.
Selected total GPU windows: [270,330), [810,870), [990,1050), [1170,1230).
Exclude unresolved trailing GPU samples. Keep framebuffer capture runs separate
from throughput runs. Do not compile or run other benchmarks during acceptance
timing. Repeat historical/candidate in ABBA order and retain every result.

Capture native frames 120,300,480,660,840,1020,1200. Require an active effect,
GPU compaction, real adaptive groups and stable root counts. Inspect terrain,
not just global MAE diluted by unchanged sky/UI. `measure_grass_coverage.py`
compares each on-image to the same off-image in predefined near/far terrain
regions (`grass-goal-terrain-regions.json`). It measures apparent pixel
contribution, not exact geometric occupancy; color, outlines and quantization
can affect it. Report lost reference pixels and empty tiles as well as the mean.
Visual inspection and temporal continuity remain separate checks.

## Implemented Cost Reductions

- `GrassClusterDrawCapacity`: bucket occupancies into powers of two, capped at
  10000. A shader early-out clips only padding children, before root loads.
  Actual root counts and positions are unchanged. This replaces hundreds of
  exact-count draw batches with about 50-60 in the tested wide views.
- `SelectGrassDrawableClusters`: optional range emission for grouped roots.
  Existing per-root consumers remain supported. Near roots retain individual
  LOD decisions. Budget limits still reject whole distant groups, never tear
  them or admit new mask samples.
- `RebaseGrassMidrangeDraws`: consume the selected range boundaries directly,
  validating their partition, instead of rescanning every selected root's
  owner. Draw ordering remains occupancy then prepared offset.
- Cluster metadata stores the immutable representative position/visibility
  and is published in BVH traversal order. This removes repeated dependent
  reads through scattered member/root arrays. Member indices are unchanged;
  ownership is remapped once at construction. No disk-cache schema changes.
- Selection-cache key includes far child retention, so an F1 change while
  stationary cannot retain the previous selection.
- Comparison harness accepts per-variant executable/profile and timing-only
  repetitions. Output records executable hashes. Captures publish draw count.

## Rejected Experiment

Visible-root preparation of native triangle lighting was implemented, built,
and tested in `grass-root-lighting/`. All seven framebuffers were identical,
but selected GPU time was 3.484 ms versus the preceding bucketed 3.391 ms,
and host time 10.478 versus 10.266 ms. These isolated measurements do not
demonstrate a benefit. The extra descriptors, barriers, 84-byte root layout
and compute sampling were removed. The existing 68-byte layout and native
vertex-atlas sampling remain. Do not present this experiment as an optimization.

## Coverage Recovery

`grass-far-recovery/` tested 75% retention at cell extents 44/88 and 100% at 88.
These are exploratory timings, not the final acceptance series. The 100%/88
candidate retains the original density start/reference (1 / 1668), segment
transition 501-1000, far density 0.62 and draw distance 50000. It disables old
tufts, uses adaptive capacity 10000 and density fade 0.3. Other appearance,
mask, light and color settings are unchanged.

Far-region coverage gain at frames 300/840/1020/1200 is respectively
-0.0091 / +5.5987 / +1.8740 / +0.0215 percentage points. The four near regions
are unchanged. Mean across all eight regions is +0.9356 points; the mean alone
does not prove absence of local or temporal regressions.

`grass-range/` tests the range-emission implementation on that same candidate:
all seven framebuffers are identical to its pre-optimization captures. Host
time is 10.2707 ms, selected total GPU 3.4528 ms, Grass GPU 1.5021 ms.
Frames 840/1020/1200 contain 163731/173129/158408 roots, not the ~33k of the
earlier rejected thinning policy. This is encouraging, not final acceptance.

The intermediate `grass-finalist-abba/` did not pass uniformly: host reduction
was 31.9% / 33.9%, selected GPU 32.4% / 35.0%. This motivated the metadata-locality
change, not a further reduction of coverage.

## Accepted Paired Measurements

`grass-locality-abba/`, after all builds/tests terminated, uses the exact
protocol above. Candidate executable SHA256:
`74e2573ccbdeaea7cf621e2fed8e355dd9d4ff9fcfd2df3049954097d76d9497`.

| ABBA run | Host total ms/native step | Selected total GPU ms |
| --- | ---: | ---: |
| Historical A | 15.489965 | 5.246660 |
| Candidate A | 9.850099 | 3.263428 |
| Candidate B | 10.130257 | 3.431396 |
| Historical B | 16.547169 | 5.556790 |

Means: 16.018567 -> 9.990178 ms host (**-37.63%**) and
5.401725 -> 3.347412 ms selected GPU (**-38.03%**). Even pairing the slowest
candidate with the fastest historical run yields **-34.60% for both metrics**.
These are measured results on this PC/workload, not a guarantee for every
scene, resolution, GPU or operating system.

All seven candidate captures remain identical to the pre-locality 100%/88
candidate. `coverage-original.json` compares directly to the original 1024
reference: +0.9356 percentage points averaged across all eight terrain regions,
or +1.8713 points across the four far regions; all four near regions unchanged.
One far region loses 2 affected pixels (-0.0091 points), while the others improve.
This establishes better average apparent cover, not pixelwise dominance.
The wider views were also inspected directly from framebuffer images.

As a check against region-selection bias, `grass-product-accepted/coverage-full-frame.json`
measures all pixels in all seven original-reference frames. Apparent Grass
contribution increases in six and is unchanged in one; mean gain is +0.1948
percentage points of the whole image. Gains by frame are +0.1882, +0.1911,
+0.0087, 0, +0.4301, +0.4623, +0.0833 points. Empty-tile fractions never
increase. Unchanged sky/UI dilute these whole-frame values, hence the separate
terrain-region report. Local pixel losses remain possible and are reported;
better average coverage is not a claim that every old grass pixel is retained.

The approved preset has `InstancesPerSquareMeter=1024`, no added far-child
thinning (`MidrangeFarBladeFraction=1`), capacity 10000, extent 88, old tufts off,
and density fade 0.3. Draw distance remains 50000 and native lighting, masks,
color, geometry shape, interaction and the user's other effects are unchanged.
The earlier saved config is retained privately as
`epona-user/config.before-grass-goal-accepted.json`. Existing users' independently
saved profiles are not globally migrated or overwritten by the product default.

Final product build changes only the embedded default preset, not rendering
code. `grass-product-accepted/` checks the rebuilt executable using the updated
normal launch configuration, with probe-only native30 timing/capture overrides.
It passed with seven identical framebuffers, active GPU compaction and the same
root/group counts. Host total: 9.5144 ms; selected total GPU: 3.1948 ms.
Final executable SHA256:
`2021d9d882d63b897e48782dd2927bbaf21b8a3783b6bb44601d50218e9b6b27`.
Product/live/saved Grass configurations match the measured candidate at runtime
float32 precision (JSON 0.62 and 0.6200000047683716 encode the same float).
All non-Grass live configuration is exactly unchanged. All automated processes
terminated successfully; no benchmark/game remains running.

## Verification and Boundaries

Standalone `oot3d_grass_midrange_clusters_tests.cpp` covers exact index
topology, boundary-mask limits, capacities up to 10000, stable nested membership,
range/per-root output parity, range partition validation, and cache invalidation.
Python Grass tests cover comparison statistics and apparent-coverage metrics.
Runtime captures, not unit tests alone, establish raster parity.

Changes are isolated to the optional Grass extension and its test tooling.
No native PICA behavior, composition anchor, UI ordering or title code changes.
No ROM, captures, generated shader cache or save data belongs in the repository.
