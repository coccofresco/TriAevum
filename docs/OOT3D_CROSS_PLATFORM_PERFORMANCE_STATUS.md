# OOT3D cross-platform performance status

## Scope and evidence level

This note records the shared performance work present on the Linux/Switch port
branch as of 2026-08-28 and defines how to carry it back to Windows. It replaces
the current-status portions of the older
[AOT performance handover](OOT3D_AOT_PERFORMANCE_HANDOVER.md), which remains a
historical record of the earlier interpreter-capable Windows runtime.

Three evidence levels are kept separate:

- **verified** means exercised by the named build, test or benchmark;
- **shared candidate** means the code is in a platform-neutral path and should
  compile on Windows, but has not yet produced a controlled Windows A/B;
- **platform-specific** means it must not be used to claim a gain on another
  backend or device.

Linux OpenGL results are a useful CPU and compatibility-renderer proxy. They do
not predict Windows Vulkan/NRI throughput or Nintendo Switch hardware speed.
Eden receipts prove boot and runtime progress only; emulator FPS is not a
performance measurement for the homebrew target.

## Current whole-AOT product contract

The current product contains 12,419 generated native functions, three audited
host boundaries and zero residual A32 entries. Its 256 shards use the
`affinity` strategy. Product mode links generated gameplay plus the exact
architectural-state/VFP support ABI; it excludes the decoded A32 runtime,
interpreter, packed-op fallback and source-native study.

The canonical selection is the repository-normalized LF file
`tools/oot3d/native_a32_runtime/whole_aot_functions.json`, SHA-256
`e396db3f1d0785906502a6e2dfa871b33bb3bd250218cb40050ef74995a33be6`.
An older Windows cache used semantically equivalent CRLF JSON, SHA-256
`975ead42a936ad6b8a598a26051a33bf6fe7abf8b5e22fcfa618836e37250ff6`,
and incremental shard placement. That cache is historical, not a compatible
product input.

The generated function ABI and runtime context changed in this work. In
particular, direct continuation, stop-PC handling and block-entry filtering now
cross the generated/runtime boundary. The previous Windows `.lib` must be
regenerated and recompiled; relinking it against the new small registry is not
sufficient.

## Shared candidates for Windows

| Area | Current change | Cost intended to be removed | Windows status |
| --- | --- | --- | --- |
| Whole-AOT state | Promoted GPRs and lazy flags are shared by reference across direct guest calls | caller/callee register-file copies | Shared candidate; regenerate archive |
| Guest dispatch | Continue `Branch`/`Returned` flows directly until stop PC or budget; use 4 KiB page buckets for registry lookup | repeated outer dispatch and full-table search | Shared candidate |
| Block hooks | A 4,096-bit no-false-negative prefilter precedes the exact sorted lookup | binary search on nearly every guest block | Shared candidate; exact lookup remains authoritative |
| Host boundaries | Flush/reload only the audited GPR/flag state used by the three external calls | unconditional architectural-state traffic | Shared candidate; exact masks have a focused contract test |
| Shard layout | Direct-call affinity co-locates related generated functions | lost compiler inlining/locality across arbitrary shards | Shared candidate; compiler-dependent, not a guaranteed gain |
| PICA snapshots | Shader program and swizzle arrays use immutable copy-on-write backing | copying two 4,096-word arrays per draw | Shared candidate; copy-then-mutate and structural invalidation are tested |
| PICA identity | Cache the exact structural vertex state and mutation identity; reuse single-mip texture payload hashes | repeated hashing of unchanged shader and texture payloads | Shared candidate; canonical identity is unchanged |
| PICA queue | Construct large draw submissions directly from the source packet and reserve transient storage | redundant zero-fill, copy and allocation work | Shared candidate |
| Diagnostics | Return before JSON/event construction when semantic tracing is disabled | hidden instrumentation cost in normal runs | Shared candidate |
| CTR services | Keep bounded SVC summaries by default and make full history/profiling opt-in | unbounded event storage and name aggregation | Shared candidate |
| RomFS | Keep one host stream per RomFS kernel object and clear stream state before each seek | reopening the same image for every read | Shared candidate; tests prove one open across multiple reads |
| Savestates | Rebind only serialized RomFS objects to the configured immutable image | unusable cross-host absolute paths without broad path trust | Shared correctness support for identical A/B checkpoints |

The bounded SVC policy retains recent events plus bounded filesystem and IPC
summaries, the last event and per-thread state. It still exposes counters needed
for profiling. `--throughput-benchmark` disables synchronization and pacing but
does not silently disable audio; an A/B must state whether audio was enabled.

## Backend-specific work

The nested runtime now has a portable PICA OpenGL path used by Linux and
Switch. Stable geometry carrying an authoritative identity and content version
is resolved through the same entry-bounded `PicaGeometryRegistry` used by the
Vulkan backend. OpenGL retains one buffer per geometry identity and skips the
upload while both content version and structural signature remain unchanged.
Draws without a cacheable identity retain the original streaming path.

The backend still uses `glBufferData` for the first upload of a geometry
version, non-cacheable geometry and the two per-draw uniform buffers. Removal
of the global completion `glFinish()`, shadow-only image barriers, cached PICA
VAO state and shader-source work only on cache misses remain in effect. A
persistent-mapped or ring-buffer path for uniforms and dynamic geometry remains
future work. These changes may benefit a Windows OpenGL diagnostic build, but
the Windows Vulkan renderer already had its own persistent geometry consumer;
the Linux result is not evidence of a Windows Vulkan/NRI uplift.

The OpenGL PICA texture and lighting-LUT maps currently have no eviction, and
unmatched pending fills can survive until a compatible target appears. Short
smokes do not bound their RAM/VRAM growth. A long-running soak with memory and
cache-cardinality telemetry is required before this backend is considered
stable on memory-constrained hardware; the likely follow-up is bounded
replacement by guest address/content generation plus expiry of stale fills.

The 30 Hz presentation contract, Horizon block budget, libnx filesystem/SDL
integration, Joy-Con mapping, SD receipt and NXVK probes are Switch-specific.
They must not be carried into the Windows performance result.

## Measurements completed

The current Linux product builds and the focused whole-AOT, CTR host,
savestate, semantic-trace and game-runtime tests pass. The product audit reports
12,419 compiled functions, three host boundaries and zero residual A32.

The reproducible Linux OpenGL proxy uses the Kokiri Forest checkpoint at guest
frame 10,600, 1280x720, 1,200 presentation frames including 300 warmup frames,
with audio disabled and VSync, SDL limiting and application pacing asserted
off. A six-run alternating A/B used the same executable and changed only the
OpenGL geometry-cache switch:

| Geometry cache | Clean FPS runs | Median FPS | Geometry upload calls | Geometry upload bytes |
| --- | --- | ---: | ---: | ---: |
| Disabled | 93.643774, 91.158468, 90.905952 | 91.158468 | 747,789 | 464,913,200 |
| Enabled | 96.372095, 94.811548, 94.536146 | 94.811548 | 3,958 | 11,691,452 |

The shared geometry registry raised median throughput by 4.01%. Geometry upload
calls fell by 99.47% and uploaded geometry bytes by 97.49%. Enabled runs
submitted 143,482 persistent draws, with 139,524 registry hits, 3,958 misses,
3,794 in-place updates, 164 resident entries and zero evictions. All six runs
retained identical process, memory-state and memory-content fingerprints,
143,482 submitted draws and 1,200 presentations. Uniform traffic was unchanged
at 286,964 uploads and 534,900,896 bytes.

This audio-disabled A/B is a Linux OpenGL CPU/render proxy, not a shipping or
physical-Switch result. It does not predict Windows Vulkan/NRI throughput. A
shipping comparison must repeat the gate with normal audio.

A pre-geometry-cache sampled profile of the same path attributed 8.521 of
10.447 host seconds to guest dispatch. SVC handling accounted for 1.175
seconds, including
1.080 seconds in IPC immediate `0x32`; GSP command-queue processing accounted
for 1.056 seconds. These values prioritize shared whole-AOT/IPC work, but they
do not attribute a Windows speedup to any individual change.

A separate single-pass dispatch-budget sweep used 600 presentations, including
150 warmup and 450 measured frames:

| Whole-AOT block budget | Measured FPS | Block-limit exits |
| ---: | ---: | ---: |
| 64 | 61.402434 | 1,989,424 |
| 128 | 63.815255 | 996,253 |
| 256 | 63.432734 | 494,886 |
| 512 | 63.889031 | 245,448 |
| 1,024 | 63.348825 | 119,650 |
| 1,000,000 | 63.480216 | 0 |

Budget 128 improved this one Linux sample by 3.93% over 64 while approximately
halving block-limit exits. Differences above 128 were small and 512 was
nominally fastest, so 128 is the conservative Switch stack/performance
compromise rather than a broadly proven optimum. The sweep retained the same
memory-content fingerprint, 96,619 draws and 600 presentations. This runtime
block-entry budget is unrelated to the product's 256 affinity shards. Windows
keeps its 1,000,000 default and needs a separate A/B.

The Switch NRO passed a fresh 40-second Eden 0.2.1 clang-PGO smoke and reached
`stage=runtime_running` with the complete 12,419/3/0 contract, native 30 Hz
gameplay, the requested 30 Hz presentation contract, interpolation disabled,
native controller input and TopScreen 2.1.1 assets. This is a correctness result
only.

## Rejected or non-comparable results

- A dense approximately 4 MiB flat dispatch table was reverted. Against the
  104.199688 FPS boot/title baseline it produced 94.862336, 100.284371 and
  94.371615 FPS (median 94.862336), approximately 8.96% lower.
- Page buckets plus disabled-trace guards produced a 102.750 FPS median in the
  same startup workload versus 104.200 FPS. The implementation remains for its
  bounded lookup and disabled-work contracts, not because that experiment
  proved a speedup.
- The approximately 104 FPS number is a boot/title baseline, not Kokiri
  gameplay. A cold 73.510 FPS Kokiri sample is an outlier and is not a baseline.
- Instrumented profiles collected across multiple evolving builds cannot be
  compared as feature-level A/B data.
- Eden guest-frame counts and emulator FPS are excluded from throughput claims.

## Required Windows adoption and A/B

Use a separate cache so the historical archive remains inspectable. The
content-addressed product path is authoritative:

```powershell
.\scripts\oot3d\Build-Oot3dWholeAotProduct.ps1 -Parallel 4
.\scripts\oot3d\Configure-Oot3dWholeAotProduct.ps1 -Force
```

Both scripts resolve the legally extracted inputs outside Git. Optional
`-CodeBin` and `-ExHeader` overrides on the configure step make those audit
inputs explicit. Before benchmarking, require the generated manifest to report
`affinity` and the canonical LF selection hash, and the runtime receipt to
report 12,419/3/0. Do not copy the old incremental `.lib` into the new cache.

`Configure-Oot3dLlvmBuild.ps1` is a separate non-product development path. It
now requires `-RebuildWholeAot` and refuses the historical unaudited prebuilt,
because its generated/runtime ABI is incompatible with this branch. Use the
content-addressed Product scripts above when preparing a Windows performance
candidate.

For a defensible comparison:

1. Build parent and candidate with the same pinned clang-cl/LLD, configuration,
   dependencies and renderer settings.
2. Restore the same Kokiri checkpoint and use 1280x720, 1,200 frames including
   300 warmup frames, native 30 Hz gameplay and no visual interpolation.
3. Assert VSync, SDL limiting and application pacing off. Run at least three
   alternating parent/candidate pairs, not three consecutive runs of each.
4. Keep profiling disabled during FPS collection. Collect a separate profile
   only after the clean result is stable.
5. Run one CPU-isolation gate with the declared audio setting and a second
   shipping Vulkan/NRI correctness/performance gate with normal audio.
6. Record the commit, product cache key, archive/generated-manifest hashes,
   configuration hash, process/memory fingerprint, draw counts, shader misses,
   deterministic framebuffer evidence and PCM hash when audio is enabled.

No Windows uplift should be claimed until this protocol has been run. The
shared changes above are implementation candidates, not substituted evidence.

## Next shared priorities

The current Linux profile points first to guest dispatch, IPC immediate `0x32`
and GSP queue handling. Any next optimization should retain the exact
12,419/3/0 closure and be measured independently. Before treating the current
archive as release-ready, retain the focused external-state-mask and shader
copy-on-write/invalidation contract tests. Add an end-to-end boundary-clobber
differential beyond those unit contracts.
A block-limit exit only unwinds the generated call chain before the wrapper
immediately resumes it; it is not an SDL event-pump or guest-scheduling yield.
The stable-geometry path is now cached. Remaining OpenGL work includes the
unchanged per-draw uniform uploads, a byte-bounded geometry policy, and bounded
texture/LUT/pending-fill lifetimes validated by a memory-telemetry soak.
Closure counts duplicated in receipts and launch assertions should eventually
come from generated manifest metadata so a future upstream realignment cannot
leave stale accepted values.

Related platform notes:

- [Linux whole-AOT port](OOT3D_LINUX_WHOLE_AOT_PORT.md)
- [Switch whole-AOT port](OOT3D_SWITCH_WHOLE_AOT_PORT.md)
- [Citra/Azahar donor audit](OOT3D_SWITCH_CITRA_AZAHAR_DONOR_AUDIT.md)
