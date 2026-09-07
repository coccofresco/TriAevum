# Title and Audio CPU Costs

2026-09-07. Continuation of [native CPU costs](TRIAEVUM_NATIVE_CPU_COSTS.md).
Baseline project `f263b6d35`, renderer `485dcc5c`. The renderer, installed title
module, game timing, assets and user settings are unchanged by this tranche.

## Implemented

The Azahar-derived DSP decoded samples into MSVC `std::deque`: each block holds
only four stereo PCM16 samples. Decoding a voice allocated many tiny blocks;
resampling also inserted two history samples and erased the consumed prefix.

- `audio_core/audio_types.h`: decoder output is now contiguous `std::vector`.
- `audio_core/sample_queue.h`: an owning buffer plus an unread cursor. Consuming
  samples performs no allocation and moves no remaining samples. Copies contain
  only the unread range; moves leave a valid empty source.
- `audio_core/interpolate.cpp`: the two history samples are a virtual prefix.
  Fixed-point stepping, saturation, history, fractional position, predelay and
  source rate are unchanged. Both history values are read before either is changed.
- `audio_core/hle/source.{h,cpp}`: explicit consumption, unchanged JSON/MessagePack
  array representation, and an adapter preserving the donor Boost deque archive
  representation. The host cursor is never serialized.
- Initialize `backup_frame`, previously indeterminate before the first sleep.
  This was exposed by the sleep/wakeup state test, not by an audio-math mismatch.

Paths above are under `tools/oot3d/third_party/azahar_audio/`. This is a host
storage change, not a replacement DSP or a change to audio/game speed. One decoded
buffer is retained per source until replaced; consumed prefixes are not freed
incrementally. There is no growing audio-history cache. Donor licenses remain.

## Verification

`tools/oot3d/native_game_runtime/oot3d_native_audio_storage_tests.cpp`:

- 7,776 differential resampler calls against the previous deque algorithm:
  None/Linear, random signed samples, saturation, rate changes, empty/short/odd
  buffers, nonzero history/fractional positions, partial/full output frames.
- PCM8/PCM16 mono/stereo and ADPCM samples, odd lengths and feedback history.
- Cursor bounds, zero-copy consumption, copy/move invariants and normalization.
- Source playback, JSON/MessagePack restore and sleep/wakeup equivalence.
- At real Kokiri frame 480, **all 24 DSP sources and mixers match exactly** between
  old and new executables: current frames, unread samples, histories, filters,
  queued buffers and serialized state. This is not a claim to have recorded and
  compared the entire soundtrack sample stream.

`oot3d_native_a32_ctr_host_tests` and `oot3d_native_a32_savestate_tests` pass.
Four native framebuffers at 120/240/360/480, 640x360, are byte-identical before/after.
Both Vulkan validation runs have zero validation errors and zero rejected scene
records; both retain 18 existing validation warnings. Draw publication remains
107/107. No capture, savestate or validation run is used as an FPS benchmark.
An additional start-from-boot run with the existing enhanced profile and audio
completed 600 presentations / 287 updates, with visual interpolation active.
Evidence: `J:/TriAevum-diagnostics/audio-costs-20260907-intro/`. Its repeated and
synthetic presentations are explicitly not counted as native benchmark frames.

Reproduce the audio test, optionally comparing two real checkpoints:

```powershell
& I:/oot3dre_work/triaevum-direct-module-build/oot3d_native_audio_storage_tests.exe `
  J:/TriAevum-diagnostics/audio-costs-20260907-visual-a/checkpoint.oot3dsav `
  J:/TriAevum-diagnostics/audio-costs-20260907-visual-b/checkpoint.oot3dsav
```

## Measurements and Limits

Same Kokiri `hudtest.oot3dsav`, same installed title DLL, 1,200 presentations /
1,199 real updates. Throughput mode, interpolation/AA/effects/VSync/limiter off.
The FPS window excludes 120 warmup frames; phase totals below include warmup.
No sampler, build, capture or save operation ran during these timed runs.

| Run | Complete native FPS, warmup excluded | DSP milliseconds / presentation, whole run |
| --- | ---: | ---: |
| Before A1 | 64.294 | 0.8482 |
| After B1 | 49.654 | 0.4957 |
| Before A2 | 57.356 | 0.9799 |
| After B2 | 64.307 | 0.3818 |

Mean DSP cost fell **0.9141 -> 0.4388 ms, about 52%** in these runs. This is a
component measurement, not a 52% FPS gain. Overall throughput is too variable to
claim an improvement: unchanged guest work ranged from 11.91 to 15.96 seconds.
A separate process sample observed two FPilot processes each using about one CPU
core plus substantial remoting/other activity. None were stopped or modified.
Do not compare these absolute FPS to the previous tranche's quieter +15% result.
Re-measure overall FPS under stable host load before attributing a total gain.

Artifacts: `I:/oot3dre_work/audio-costs-20260907-{a1,b1,a2,b2}/`.
Functional framebuffer/checkpoint evidence: `J:/TriAevum-diagnostics/audio-costs-20260907-visual-{a,b}/`.
Before executable: `I:/oot3dre_work/triaevum-direct-module-build/TriAevum.before-audio-costs.exe`.
Current executable: same directory, `TriAevum.exe`.

The initial functional save runs on I: failed because free space fell to about
60 MB (C: also had only about 80 MB). The B run was stopped; neither is successful
save evidence. The successful pair is on J:. The benchmark harness now preflights
space for requested checkpoints and requires the final checkpoint to exist;
a zero process exit code alone no longer passes a failed save verification.

## Title Symbols and Sampling

`tools/triaevum_release/tests/relink_title_for_profiling.ps1` creates a diagnostic
DLL/map/PDB from the **exact existing title object archive and paired source**.
It compiles only the small ABI wrapper, does not regenerate game IR or compile
the 256 generated C++ shards, and does not overwrite a catalogued module.
The link still performs ThinLTO machine-code generation: this cold diagnostic
link took 392 seconds with at most three workers, not an instantaneous relink.
It retains a ThinLTO cache and records inputs, SHA256, flags and elapsed time.
Pass `-LtoCache` with that existing cache directory for subsequent isolated links;
the default new-output cache is cold. The optional reuse parameter is syntax
checked; no warm-cache timing is claimed for this tranche.

Exact local inputs (do not search for another compiler or regenerate the title):

- Source archive: `I:/TriAevum-0.6.0-candidate-r1/source/titles/oot3d-eur-project-baseline-16a6b0aa-build.zip`.
- Generated headers: `I:/oot3dre_work/triaevum-full-title-proof/data/translator-cache/whole-aot-v2/generated/b8a9abc888cbc74ed180d4cef6356a183b2945858f611f49be9920cc50cf1cee/fade3a4fcc298ab8f4568e0384103012f03dc9aa1d055666cd47525a2eda4cf2`.
- Object archive: `I:/oot3dre_work/triaevum-full-title-proof/data/translator-cache/whole-aot-v2/native-objects/archives/4a1c0a10d757ac674346241b10bbda5b6e9f03c13d268faa11b4ba10643b4226.lib`.
- Support/compiler: `I:/oot3dre_work/triaevum-full-title-proof/forge/{triaevum_title_whole_aot_support.lib,clang-cl.exe}`.
- Sysroot: `I:/oot3dre_work/triaevum-fully-acquired-sysroot`.
- Diagnostic result: `I:/oot3dre_work/native-game-costs-20260907-symbols-b/`, including `relink.json`, `title.map`, `title.pdb` and the DLL.

The benchmark harness accepts `plugin=PATH` only for an isolated run and records
the actual plugin hash. The sample was taken **from the diagnostic DLL itself**,
not by assuming its offsets match an older DLL. It ran with the matching runtime
PDB before the audio rebuild. Very Sleepy `/a PID /t 15 /o CAPTURE /q` was attached
after startup; stacks are filtered to `TriAevum!main`. Profiling FPS is not a benchmark.

Capture and summary: `I:/oot3dre_work/native-game-costs-20260907-profile/{cpu.sleepy,cpu-summary.json}`.
Summarizer: `tools/triaevum_release/tests/summarize_cpu_sample.ps1`.

| Sampled surface | Main-thread exclusive share |
| --- | ---: |
| Title DLL, all work | 49.67% |
| Fast memory reads/writes, runtime + title | 14.16% |
| Block-entry helpers | 7.47% |
| Scalar VFP helpers, title | 10.48% |
| Canonical PICA identity construction, runtime | 7.03% |

Rows overlap with the title total. Identical-code folding can give a shared block
helper the name of one generated function: the 7.47% is **not** evidence that
`FUN_002c26b4` alone is expensive. Unresolved OS/driver names can still denote a
nearby export. Title samples are no longer mislabeled as the exported query function.

## Next Implementation Boundary

Follow-up: [canonical hash CPU costs](TRIAEVUM_CANONICAL_HASH_CPU_COSTS.md)
implements the allocation-free arithmetic optimization for item 4 below and
records native A/B/A/B results. The proposed identity cache was evaluated and
removed; persistent FNV IDs and the installed title module remain unchanged.

Prioritize measured common helper costs over another graphics-quality reduction:

1. Inspect the emitted machine code and call frequency of `NativeA32Memory::ReadFast`
   and `WriteFast`. Separate protected/cross-page/tracing paths from the common
   access path, preserving fault addresses, write generations and exclusive access.
   These templates are embedded in the title: a runtime-only build cannot silently
   update their implementation. Plan the paired module rebuild explicitly.
2. Reduce block-entry scaffolding without dropping instruction budgets, external
   exits or sparse callbacks. Profile folded helpers as a class, not a single PC.
3. Scalar VFP normal-value fast paths require differential checks for FPSCR flags,
   NaNs, denormals, saturation and rounding; replacing them with unchecked host
   floating-point expressions is not an optimization with equivalent behavior.
4. Canonical identity hashing is still a real host cost. Preserve persistent IDs,
   unknown-register coverage and source verification; do not disable metadata.

No step above is claimed implemented by the audio storage change. Preserve the
working module as the baseline, build only changed owners where possible, and
require real-state/framebuffer equivalence plus un-interpolated A/B measurements.
