# AO, Grass, audio, pacing and AOT source qualification

## Scope — 2026-09-26

This records adoption of the remaining useful source changes after material
commit `c9138cb`. It does not replace the historical measurements in the owning
architecture documents, promote the AOT experiment, or claim a new performance
win. Runtime verification used an isolated Windows/NRI build, an RTX 3060,
1280×720 output, private Kokiri checkpoints and the compatible private title
module from `<work>/deferred-copy/`.

The installed game, user profiles/packs and running user sessions were not
replaced. No ROM-derived assets, checkpoints, captures, SDKs or title binaries
are part of these commits or a public package.

## Adopted behavior

- **AO:** Off, FidelityFX CACAO and multiscale GTAO remain distinct choices.
  CACAO retains its provider, build option, shader generator and original F1
  controls; saved CACAO settings are not converted to GTAO. GTAO has independent
  broad/detail radii and strengths, Low/Medium quality, declared graph resources,
  format-derived normal-precision handling and provider-neutral diagnostics.
  Scene coverage follows native RGB blending, including additive preservation.
  AMD and Intel donor notices are retained. The graph/coordinate standalone
  fixtures are now registered with CTest instead of remaining orphan sources.
- **Grass:** immutable tree-order traversal avoids the candidate-vector
  allocation and stops at the visible budget. Stable bounded occupancy counting
  preserves selected-root order. Lighting uses the captured camera basis,
  including roll, rather than live camera state. Density, distances, geometry
  and product presets are not reduced by this increment.
- **Audio:** the DSP resolver receives the complete encoded PCM8, PCM16 or ADPCM
  extent after native DMA alignment. Its synchronous view observes deferred
  bytes across that extent, without exporting a persistent pointer. Partial
  final ADPCM blocks and stereo extents have regression coverage.
- **Pacing:** the native scheduler is the single software limiter during
  interactive gameplay; VSync is not disabled. Original30 and Interpolated2x
  restore the same 30 Hz simulation clock. Enhanced60 remains incompatible and
  rejection does not mutate the clock. This is not a promise to eliminate
  startup stutter or sustain 60 FPS.
- **AOT:** the memory-region pilot is connected to the explicit developer CLI,
  generator, manifest and object-cache contract. Default generated C++ remains
  unchanged. Unobserved views cannot bypass deferred memory, observation hooks
  or tracing. The experiment remains opt-in and is not installed or promoted.

## Fresh verification

The full isolated runtime and owning fixtures built successfully with
clang-cl 22.1.6/MSVC 14.44. Results:

| Boundary | Result |
| --- | --- |
| Graphics foundation | 292 passing tests |
| Shader pipeline / scene / graph / diagnostics | 42 / 19 / 47 / 6 passing tests |
| F1 actual widgets | 6,648 assertions; Off → GTAO → Medium → CACAO → Off, original CACAO controls, capability loss and F2 |
| AO/coverage GPU | RGB8 planes, independent scales, W-buffer, odd extents, repeatability; additive, alpha-transmission and opaque coverage |
| AO guide / surface coordinates | Registered CTest fixtures pass; diagnostic reads and UI isolation, transfer/UV round trips |
| Grass | Midrange-cluster, stable ordering, partition and spacing fixtures pass |
| Audio | 7,776 differential resampler calls plus codec, playback, snapshot and deferred PCM8/PCM16/ADPCM regressions |
| Clock / presentation | Clock/restore, presentation-policy and pacer fixtures pass |
| Built-in shader artifacts | All 59 match generator output |
| AOT | 62 whole-AOT Python tests, 13 release cache/backend tests, two ABBA summarizer tests; 6,912 compiled differential cases plus deferred-memory fallback |

The AOT probe links a freshly built, matching
`triaevum_title_whole_aot_support` library. An old September 10 support library
cannot link the new deferred-memory regression and is not the final evidence.
The CLI smoke also proves default C++/header identity, opt-in generation of the
three selected functions, affected-shard invalidation when the helper contract
changes, and exact restoration when opt-in is removed.

### Deterministic runtime captures

Thirteen bounded runs cover the previous material runtime with AO Off, current
Off, GTAO Low/Medium/zero strengths, CACAO Low/Medium, Grass with Native and
Per-pixel lighting, Grass with each AO provider, x2, and restore back to
Original30. All thirteen have zero Vulkan/NRI errors and zero missing,
unresolved or failed graph resources/passes. The existing 11 Vulkan warnings
remain visible; this is not a warning-free claim.

- Previous material runtime Off versus current Off: **zero differing RGB
  pixels**. GTAO with both strengths zero versus Off: also **zero**.
- Each AO provider and each quality setting produces an observable response.
  This establishes independent operation, not visual equivalence between
  algorithms. Framebuffers were inspected directly, not as Windows screenshots.
- Saved GTAO, CACAO and Grass runs have **zero changed guest-memory bytes** and
  exactly equal native PICA frontend/submission state against Off.
- Every deterministic native run processes 218 audio frames with nonzero PCM.
  Original30's 32 presentations and x2's 64 presentations both execute 32 game
  updates and end at simulation tick 113062, with the same audio counters.
  The x2 checkpoint also restores into Original30 and continues successfully.

### Realtime result and unresolved validation observation

The first 15-second x2 interactive run confirms the native pacer is enabled and
SDL's limiter is disabled, but records **one Vulkan lifetime error**:
`VUID-vkDestroyImageView-imageView-01026`, at diagnostic frame 1. An image view
was destroyed while still referenced by a submitted descriptor set. Its source
has not been attributed; this qualification does not mark that failure fixed.

A subsequent matched pair, both loading the saved x2 checkpoint, completes with
zero Vulkan/NRI errors: the previous material runtime has both software
limiters enabled, while the candidate has only the native pacer enabled.
Both retain a 30 Hz simulation. That pair does not erase the earlier failure
or establish that it predates these changes. WASAPI re-priming/startup warnings
also remain; no audio-underrun or long-session stability claim is made.

## Historical measurements, not fresh speed claims

The Grass document retains the small, workload-dependent aggregate improvement
and normal-priority regressions. The AOT ABBA evidence was re-summarized, not
rerun: its paired pre-backend ratio remains **1.0174**, not a speedup. The second
historical AOT comparison was intentionally stopped by the user, not a crash.
No new FPS claim is inferred from short capture or validation runs.

## Evidence and reproduction boundaries

Private evidence root:
`<work>/remaining-progress-qualification/`.

- `source-selection.json`, build products, suite logs and `test-receipts.json`.
- Per-case `invocation.json`, `runtime.json`, `gpu.json`, console logs and
  framebuffer captures; successful bounded cases also have `receipt.json`.
- `structural-qualification.json`, `runtime-qualification.json` and
  `realtime-comparison.json`; the latter is separate from the first failing
  interactive probe.
- `aot-generator-qualification.json`, `aot-historical-summary.json`, generated
  probe products and the matching support library.

Owning references:
[renderer architecture](OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md),
[F1 review](TRIAEVUM_F1_MENU_REVIEW.md),
[Grass selection](TRIAEVUM_GRASS_MIDRANGE_CLUSTERS.md),
[audio/copy and pacing history](TRIAEVUM_GPU_COPY_PATH_PLAN.md), and
[AOT memory regions](TRIAEVUM_AOT_MEMORY_REGIONS.md).

Public installation still consumes catalogued precompiled title logic. These
are developer verification/build commands and do not add compilation, SDK
acquisition or a fallback path to Forge.
