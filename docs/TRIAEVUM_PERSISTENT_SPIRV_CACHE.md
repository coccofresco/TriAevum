# Persistent SPIR-V Cache

The [Forge shader handoff](TRIAEVUM_FORGE_SHADER_HANDOFF.md) now connects this
cache to an installation-owned directory shared with offline NRI preparation.
Real incomplete-pack testing verifies that newly compiled shaders survive into
the next launch. No automatic gameplay-time preparation queue was introduced.

Measurement correction: the counters below cover calls routed through this
cache, not all renderer passes. The subsequent independent shaderc audit found
20 direct NRI/effect calls even with native presentation. See the handoff above;
a zero cache compilation count is not a whole-renderer zero-compilation claim.

2026-09-10. Continuation of PR #11 review after `09f6202`, on `port/linux-nri`.
This changes the common renderer, not TopScreen, gameplay or title compilation.

## Decision And Scope

The renderer **already had** a source-keyed disk cache. Its problems were
unbounded allocation from a file-supplied word count, no payload integrity check,
no compiler-build identity and direct truncating writes. It was also embedded
inside `gfx_vulkan.cpp`. Do not describe this work as inventing persistent caching
or claim that previous warm launches necessarily recompiled every shader.

Replace that implementation with one reusable cache, retaining the same live
compiler options and source generation. Do not add PR #11's second local pack,
backend-owned prewarm queue or blocking UI. The PR's discovery/reuse motivation
informs this integration, but its storage implementation is not merged wholesale.
Contributor: [999sian](https://github.com/999sian),
[PR #11](https://github.com/coccofresco/TriAevum/pull/11), reviewed head
`28fa5fdd60dc79c9145dc299f4440a90a8f1d026`.

## Ownership

All paths below are relative to `runtime/three_ds_recomp/`:

- `include/fast/renderer/spirv_cache.h` and `src/fast/renderer/spirv_cache.cpp`:
  bounded portable container, exact source/contract verification, resolve policy,
  immediate persistence, and counters. No GPU objects, PICA or title semantics.
- `src/fast/renderer/cache_file.cpp`: shared atomic file publication, also used
  by `renderer3ds/vulkan_pipeline_cache_store.cpp`. Same-directory temporary,
  close/check, then replacement; failure leaves the previous cache intact.
- `src/fast/renderer/shaderc_compiler.cpp`: unchanged Vulkan 1.1/performance/main
  compiler options and process-local compiler fingerprint. Renderer/compiler
  code lives here, not in the generic byte cache or title adapter.
- `src/fast/backends/gfx_vulkan.cpp`: configure once, route compilation through
  the cache, report metrics once, and create GPU objects as before.

No worker was introduced. Each instance belongs to its rendering thread;
separate processes can share storage. No full directory scan or shutdown-only
pack rebuild is needed. Shader-source generation and in-memory GPU caches keep
their existing owners and canonical/instrumented separation.

## Validity And Failures

Cache keys include stage, effective source and compiler/options contract.
The complete source and contract are also verified from the stored payload;
hashes are indices, not substitutes for that comparison. A generator change
that changes the shader text naturally invalidates the entry. A profile change
producing identical text may safely reuse identical SPIR-V.

The compiler contract fingerprints the loaded shaderc module and loaded
glslang/SPIRV-tool library files, rather than trusting an outer descriptor
schema or a manually entered SDK version. Static compiler builds fingerprint
the owning module. Android APK-mapped libraries fingerprint the APK container
once, conservatively invalidating on package updates. Failure to establish the
identity disables disk reuse without preventing normal compilation. Live
replacement of loaded compiler libraries during a process is not supported.

Containers have explicit little-endian fields, a 16 MiB entry bound, exact
length checks, payload checksum and basic SPIR-V instruction-boundary checks.
This detects accidental corruption, not malicious code signed by an attacker;
the user-local cache is not an authenticated distribution format.

Missing, incompatible, busy or corrupt entries are cache misses. Failed writes
are reported and do not stop rendering. Compilation failures still propagate;
they are never converted into an empty shader. Windows may temporarily deny
replacement while another reader holds the file: skip that write, never wait
in a retry loop or delete the previous valid entry. Interrupted temporary files
are ignored. Atomic publication protects process interruptions, not a guarantee
of storage durability across power loss.

The explicit AOT shader pack keeps priority. Its strict mode still rejects a
missing module **before** consulting the local cache. Device-specific Vulkan/NRI
pipeline caches remain separate and retain GPU/driver/UUID validation.

## User Storage

The existing `TRIAEVUM_RENDERER_CACHE_DIR`/platform preference directory is used,
with entries under `spirv-v2/*.spvc`. Old root-level `.spv` entries lack the new
contract and are ignored, not silently imported or deleted. The first launch
after this change may therefore compile again unless a supplied pack covers it.
No new F1 setting or manual setup is required. All data remains local; nothing
from these captures or caches is added to the public release payload.

## Verification

Linux RTX 4060, actual NRI runtime, two bounded 900-presentation boot/title runs,
native fidelity and native 30 Hz timing, no shader pack, same isolated cache:

| Counter | Empty application cache | Second launch |
| --- | ---: | ---: |
| Resolve requests | 98 | 98 |
| Cache hits | 47 | 98 |
| Shader compilations | 51 | 0 |
| Compile failures / rejected entries | 0 / 0 | 0 / 0 |
| Successful writes / failed writes | 51 / 0 | 0 / 0 |
| Total measured shaderc time | 8,403.991 ms | 0 ms |
| Cache-read time | 10.718 ms | 13.480 ms |
| Cache-write time | 9.560 ms | 0 ms |

Both runs exit normally. Six framebuffer captures per run match each other
and the pre-change `pr-compat-native-title` reference byte-for-byte. The 47
first-run hits include repeated module requests within that same process.

These are compiler/cache measurements across the selected boot/title interval,
not uncapped FPS, gameplay-only frame-time statistics or an improvement over
the old warm cache. The global driver cache was not reset. Interpolation and
extensions are disabled for this comparison; no interpolation multiplier is
counted as throughput. First-use hitch percentiles remain unmeasured.

A separate negative live test supplies the incomplete 77-module effects pack
under strict/native-fidelity mode, with the populated local cache still present.
It rejects the missing native fragment module as expected: pack hits 13,
misses 1, strict=1; no shader compilation. The local cache does not mask the
pack's coverage failure. The final summary is emitted only once per renderer.

Focused tests pass with Linux Clang 22, GCC 16 and Windows MSVC 19.44. They cover
all truncation offsets of a synthetic entry, corrupt payload, exact-source
mismatch despite a recomputed checksum, hostile counts/oversize/trailing bytes,
compiler/stage/source invalidation, immediate reuse by a separate instance,
interrupted writes, unavailable storage and compilation failures. Concurrent
publication never exposes an accepted partial entry; transient Windows open/
replacement failures are permitted misses. Actual shaderc cold/warm SPIR-V is
identical. The shared pipeline-cache preparation contract still passes on
Windows and Linux after extracting its writer.

Android NDK r29/ARM64 cache/test compilation and shaderc adapter compilation
pass. **No Android device run, macOS qualification, Windows gameplay rebuild or
full renderer test-suite run is claimed.** No title/AOT rebuild was needed.

## Reproduce And Continue

```text
cmake -S tools/renderer/shader_cache -B BUILD
cmake --build BUILD --config Release --parallel 3
ctest --test-dir BUILD -C Release --output-on-failure
```

The standalone project needs only C++20/Threads and shaderc for its real-compiler
test; `-DTEST_SHADERC_COMPILER=OFF` runs the independent container tests without
the SDK. Runtime tests also register `spirv_cache_tests`. The existing renderer
probe accepts `--cache-directory` to share an isolated cache across runs.

Private evidence: `/home/xander/triaevum-pipeline-live-proof/` directories
`pr11-cache-cold`, `pr11-cache-warm`, `pr11-cache-strict`, and
`pr11-shared-spirv-cache`. Unit builds: `/home/xander/triaevum-shader-cache-tests`,
`/home/xander/triaevum-shader-cache-gcc-tests`,
`/home/xander/triaevum-shader-cache-android-tests`, and
`I:/oot3dre_work/shader-cache-windows`.

The former automatic next-launch-prewarm proposal is superseded by the
[Forge-first handoff](TRIAEVUM_FORGE_SHADER_HANDOFF.md): prepare known work in
Forge, compile and persist only uncovered work in the game. Do not introduce a
gameplay-time preparation queue or import #15's unsafe worker. Incremental
recipe export may feed a future Forge update, but is not a prerequisite for
reusing the already persistent shader and driver caches. Measure first-use
hitches separately from steady-state CPU/GPU rendering.
