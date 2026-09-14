# Temporal Shader Preparation and Frame-Time Tails

## Scope

Follow-up to `cc04157` and `TRIAEVUM_NATIVE_PATH_BENCHMARK_20260914.md`.
This closes runtime compilation of the **temporal-only native fragment family**,
not all remaining compilation or all causes of stuttering. No shader collection,
ROM input, Forge compilation or material-ID exception was added.

## Implemented

- `tools/renderer/tev_program/build_fragment_artifacts.cpp --temporal` reuses
  `BuildPicaFragmentInstrumentationVariant` and its typed frontend hooks.
  Native blend behavior reduces to four reactive coverage equations: none,
  source alpha, source color and full coverage. Combined with the existing
  sampler/LUT/shadow interfaces and canonical/NRI descriptor layouts this
  produces 40 distinct offline programs; shadow-write variants deduplicate
  because their reactive coverage is disabled by the native classifier.
- `runtime/three_ds_recomp/include/fast/oot3d/pica_temporal_fragment_binaries.h`
  owns the generated temporal catalog, separate from the 16 canonical programs.
  The canonical header remains byte-for-byte reproducible and unchanged.
- `GfxRenderingAPIVulkan::ResolveNativePicaShaderSpirv` resolves both catalogs
  before any shader-pack/cache/compiler fallback. Lookup checks exact source
  identity, size, descriptor interface and SPIR-V validity as before.
- A fragment artifact miss reports stage and source identity once per resolution,
  not per draw. The resolver also serves the compatibility combiner, so the
  comparison harness classifies misses against the actual native draw inventory.
  It rejects observed native fragment fallbacks, without whitelisting asset IDs.
- `fast/renderer/frame_time_distribution.h` is a reusable, fixed-memory host-frame
  histogram. It allocates nothing per sample and writes nothing per frame.
  It exposes conservative quantile upper bounds at 0.125 ms resolution,
  maximum duration, invalid/overflow counts and 60/30 Hz budget overruns.
  Durations above 1024 ms are not hidden or clipped: overflow quantiles use
  the actual maximum. These are host-frame durations, not GPU timestamps.
- The host enables this distribution only for throughput benchmarks, using the
  same completed-frame interval and warmup exclusion as aggregate FPS.
  `benchmark_native_paths.py` checks sample count and summed duration against
  the original window and offers `--taa` without enabling frame interpolation.

The runtime still constructs source variants for exact identity/contract
resolution. This change removes their runtime **compilation**, not all source
construction. It does not change pass scheduling, draw order, targets or output
equations. Other combinations of optional effects remain outside this catalog.

## Verification on Windows

- All 11 standalone CTest suites pass, including temporal artifact generation,
  native GPU equations, uniform layouts and histogram boundary/overflow tests.
- Canonical generation also passes `--check-exact` against the existing header.
- Field with TAA: one canonical and three instrumented program owners; eight
  fragment artifact hits and four vertex artifact hits. Fixed passes compile zero
  programs. With a fresh application cache, total compiler calls fall from eight
  to two: the six temporal fragment compilations have been removed. The remaining
  pair belongs to `BuildVertexShaderSource` / `BuildFragmentShaderSource` in the
  compatibility combiner. In the benchmark its compiler time is 205.342 ms.
- The three parametric framebuffer captures are byte-identical to the prior
  `cc04157`-era TAA captures. Exact comparison against specialized rendering still
  FAILS at frame 150 by one pixel (red difference 8/255); frames 120/180 match.
  This pre-existing discrepancy remains open, not waived.
- Final Field test with effects Off: all three framebuffer captures match
  specialized rendering exactly, with no observed native fragment fallback.

Nine-run TAA benchmark, three repeats per arm in rotated order. Same Field state,
assets, title DLL and configuration. Native30, no interpolation, no VSync or
limiter, 900 frames per run, 180 excluded as warmup. No screenshots or shader
inventory during timing. Driver/OS caches were not cleared.

| Path | Median native fps | Range | Median run p95 upper | Median run p99 upper |
| --- | ---: | ---: | ---: | ---: |
| Historical | 102.11 | 97.69-107.73 | Not instrumented | Not instrumented |
| Current specialized | 93.35 | 91.50-93.95 | 13.000 ms | 14.375 ms |
| Current parametric | 99.59 | 99.14-100.49 | 11.750 ms | 13.875 ms |

Parametric: 2160 measured frames, maximum 26.9425 ms, zero durations over
33.333 ms. Each run still executes 85,500 draws and 900 native state updates.
This demonstrates steady throughput, NOT hitch-free scene transitions or cold
driver pipeline creation. Do not compare these TAA numbers directly to the
previous non-TAA benchmark as a before/after speedup.

Measured executable SHA256:
`598c928ea4a6aa18c39977f92507e27ab099e02c1a0d15ad5865b330e74f3869`.
Subsequent edits rename a diagnostic label and refine test classification;
they do not change shader binaries or rendering behavior.

Private evidence under `C:/Users/xander/AppData/Local/Temp/`, never package:
- `TriAevum-temporal-offline-benchmark-20260914`: nine-run timings and counters.
- `TriAevum-temporal-offline-parity-confirm-20260914`: TAA framebuffer comparisons.
- `TriAevum-temporal-offline-canonical-verified-20260914`: final canonical comparison.
- `TriAevum-temporal-offline-taa-verified-20260914`: final TAA coverage check.
- `TriAevum-tev-static-operands-taa-parity-20260914`: previous TAA reference.

Developer commands:

```text
build_fragment_artifacts --temporal runtime/three_ds_recomp/include/fast/oot3d/pica_temporal_fragment_binaries.h
build_fragment_artifacts --temporal --check
ctest --test-dir <standalone-build> --output-on-failure
python tools/renderer/tev_program/benchmark_native_paths.py --invocation <private-invocation.json> --baseline <historical-exe> --output <new-directory> --taa
```

## Remaining Structural Work

1. Replace the remaining compatibility combiner compilation with a maintained
   parametric/offline family, or migrate its callers to existing typed passes.
   Do not embed the two observed shader IDs as a supposed general solution.
2. Convert other effect combinations to bounded offline programs; parameters
   such as explicit reflection material values must become data rather than
   source variants. Keep each effect's ownership and canonical-Off invariants.
3. Remove eager duplicate pipeline preparation safely. In
   `gfx_vulkan_pica.cpp`, `GetOrCreateNativePicaPipeline` currently creates a Vulkan
   pipeline and then its NRI counterpart. `NriPicaPipelineBridge::CreateOwnedPipeline`
   uses the real fallback handle as its key. A clean fix needs an independent
   typed pipeline identity plus lazy compatibility ownership, not fake Vulkan
   handles or suppressed draws. Then prepare finite pipeline requirements before
   consumption; avoid using the old captured manifest/pack as a dependency.
4. Add per-frame correlation of compiler/pipeline creation, uploads and waits,
   and exercise automatic transitions/first-use scenarios. The histogram alone
   does not attribute a spike, and warmup excludes initial preparation.
5. Repeat correctness and tail measurements on Linux/Android. No such platform
   validation is claimed for this change.
