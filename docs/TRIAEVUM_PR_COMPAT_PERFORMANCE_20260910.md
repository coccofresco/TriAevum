# PR Follow-Up: Compatibility And Performance

2026-09-10; development baseline `cb21d7f`, branch `port/linux-nri`.
The user redirected work away from TopScreen UI toward portability and performance.
The live GitHub API lists seven open PRs: #6, #11, #13, #14, #15, #16, #18.
The compatibility/performance heads are unchanged from the previous review.
This is selective local integration, not a GitHub merge or a new release.

## Decisions

| PR | Pinned head | Action |
| --- | --- | --- |
| [#11 shader cache](https://github.com/coccofresco/TriAevum/pull/11) | `28fa5fdd60dc79c9145dc299f4440a90a8f1d026` | Integrate missing outline pipeline identity, recording and preparation through the existing NRI/Forge owners. Fix the reported Linux Clang test-option problem. Full per-user recording cache remains separate. |
| [#16 macOS](https://github.com/coccofresco/TriAevum/pull/16) | `49fbfcd7149526d1fc0474055279af9a556b77e6` | Adapt full-drawable readback into a portable tested module. Do not replace the current platform/release pipeline. |
| [#15 guest worker](https://github.com/coccofresco/TriAevum/pull/15) | `99454b757805c5580a2cada2ac214870fcd52693` | Not enabled: unsafe frame-local capture lifetime on presentation exceptions; contributor measurements do not establish a stable throughput gain. |

#6's useful infrastructure, #14's error feedback and #13's product input
hooks were already integrated selectively. #18's UI work is paused here.
See [earlier findings](TRIAEVUM_PR_REVIEW_20260910.md) for the rejected parts;
unchanged PR heads do not justify repeating the same investigation.

## #11: A Missing Pipeline, Not A Different Shader

Live rendering already distinguishes the transparent outline-occlusion pass
from the ordinary color pass. They use the same shader but different raster
state: the coverage pass does not write scene color/depth and writes only
coverage alpha to its declared guide. The previous manifest omitted that
distinction, and the extra pass explicitly disabled inventory recording.

Adopted from 999sian's proposal:

- `OutlineOcclusionOnly` participates in structural equivalence and JSON.
- Missing field means false; false does not change the existing structural ID.
- Live inventory records the extra pass, and runtime prewarm forwards the flag.

Additional integration into current architecture:

- `ResolvePicaPipelinePreparationItem` forwards the same flag into the shared
  `BuildPicaNriPipelineState` factory used by the actual renderer. Thus Forge's
  NRI preparation cannot accidentally construct a color-writing substitute.
- Reject a coverage recipe claiming the canonical domain, lacking transparent
  coverage output, or lacking its rigid-motion-guide attachment.
- Preserve strict/native-fidelity profile isolation and ordinary manifest IDs.
- Tests use a JSON parser, rather than removing arbitrary text lines from JSON.
- Gate GoogleTest's `/WX-` on the MSVC frontend; GNU-style Linux Clang must not
  receive that Windows option. The separately reported Vulkan test dependency
  is already present and is not added twice.

No new queue or cache owner was added to the Vulkan class. This does **not**
implement the entire proposed local SPIR-V store, shutdown cache dump or
blocking overlay. Source/generator identity validation and atomic incremental
persistence remain required before adopting those pieces.

## #16: Portable Full-Drawable Readback

The old Vulkan readback copied `min(requested, drawable)` pixels: high-density
windows were cropped to the top-left; larger requested outputs were padded.
Pablo Souza's full-drawable resampling fix is adapted into
`fast/renderer/framebuffer_readback.h`, independent of SDL, Vulkan, PICA and
title state. The Vulkan/NRI host supplies the drawable bytes and format.

The helper validates extents and storage before writing, handles RGBA/BGRA,
and preserves existing RGBA5551 conversion and equal-size pixels. It does not
change scene projection, on-screen filtering, gamma or gameplay resolution.
This corrects readback compatibility, not rendering FPS.

Not imported from the broader macOS patch:

- Logical/drawable swapchain policy: already fixed in the current host.
- `std::make_unsigned` specialization repair: already supplied by the Android
  work as `nihstro::BitFieldUnsigned` (`5035004`/`87603fa` history).
- Mac-only async Grass and global pacing changes: not generic portability fixes.
- A parallel AppKit/Forge/package pipeline or disabling NRI/CACAO/SSSR globally.

No Mac device was available. This contribution does not establish macOS support.

## Verification

- Linux runtime builds incrementally, without rebuilding title code.
- Readback tests pass with both GCC and Clang, `-Wall -Wextra -Werror`:
  equal-size RGBA/BGRA, nonuniform up/down scaling, alpha and invalid extents.
- Eight pipeline-manifest tests pass, including old-field omission, distinct
  coverage identities, roundtrip, malformed flags and canonical-domain rejection.
- Pipeline preparation contract passes on Linux Clang and Windows MSVC. It
  exercises manifest-to-NRI lowering using synthetic modules without calling
  the GPU; it is not an actual shader compilation or a gameplay scenario.
- The same helper and contract test compile/link for Android ARM64 with NDK r29.
- Actual Linux RTX 4060 NRI preparation still accepts the existing 500-recipe
  native manifest and creates 500/500 pipelines, no failures. About 1.00 s in
  this run, with a fresh application cache but an already warm driver cache:
  **not a cold-driver benchmark or a claimed performance improvement**.
- A bounded 900-presentation native-fidelity intro run exits normally. All six
  framebuffer captures match `ocarina-text-regression-title` byte-for-byte.
- A separate 900-presentation run with the current effects profile records
  88 distinct pipeline recipes, including **27 outline-occlusion recipes**
  observed **11,008 times**, and 77 effective shader modules. This verifies
  that enabling recording on the actual auxiliary draw populates the new flag;
  adding a JSON field alone would have left these recipes absent.
- All 77 modules compile into a private portable pack. The actual NRI helper
  creates **88/88 device pipelines**, including all 27 coverage variants, with
  zero failures on Linux RTX 4060 (3.498 s elapsed, 3.012 s pipeline creation).
  Validation layers were not requested; driver acceptance is not a claim of
  validation-layer qualification or complete game coverage.
- A further bounded live run using that manifest and pack exits normally:
  `PREWARM created=88 reused=0 skipped=0 profiles=1`. The live native-PICA
  shader lookup counters remain `hits=0 misses=0` because the prewarm has
  already populated those shader objects. This is not a count of unrelated
  extension shaders or total driver work. Its framebuffer capture is nonblank;
  the interpolated/effects runs are not byte-identical or a deterministic
  visual-equivalence test. The native-fidelity comparison above is separate.

Private evidence: `/home/xander/triaevum-pipeline-live-proof/` directories
`pr-compat-native-title`, `pr-review-cache`, `pr-review-cache-report.json`,
`pr11-effects` and `pr11-effects-prewarm`. The latter two contain inventory,
compilation/preparation reports, launch counters and framebuffer captures.
Windows helper/test build: `I:/oot3dre_work/nri-pipeline-preparation-windows`.
No ROM, captures, shader packs or driver caches are committed or distributed.

Limits: no real Retina display test, Windows gameplay rebuild, Android device
run, full CMake renderer-test-suite run, or FPS improvement is claimed.
The legacy Shadow2D probe did not build from the current Linux configuration
because its old demo host does not propagate its include dependencies; it is
not counted as GPU readback qualification. The portable conversion tests are
the direct coverage of mismatched readback extents.

## Next Priority

Follow-up: [persistent SPIR-V cache](TRIAEVUM_PERSISTENT_SPIRV_CACHE.md) replaces
the unsafe legacy store with shared, bounded, compiler/source-validated atomic
storage. Native live cold/warm tests give 51 -> 0 compilations with identical
framebuffers. Automatic persistence and prewarm of pipeline discoveries remain
separate from this completed shader-module storage work.

1. Finish #11's per-user discoveries behind the existing shared cache store:
   bind to the generator/compiler contract and exact effective source identity,
   checkpoint atomically during the session, isolate strict mode, and reuse the
   bounded preparation job with cancellation/profile invalidation. Measure
   actual first-use compilation and hitch reduction, not just cache-file presence.
2. For #15, first extract exception-safe, owned work packets and correct the
   disjoint timing spans. Require injected presentation failures, quickload,
   resize and shutdown tests before a worker experiment. Compare repeatable
   real simulation throughput and latency; interpolated FPS is not evidence.
3. Remaining macOS work should register capabilities/loaders with the common
   platform pipeline, qualified with the contributor's hardware, not duplicate
   Forge or weaken NRI/release requirements for the supported platforms.
