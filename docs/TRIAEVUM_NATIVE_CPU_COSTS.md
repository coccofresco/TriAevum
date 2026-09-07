# Native CPU Costs: Measured Reduction

2026-09-07. Follow-up to `TRIAEVUM_F2_LIFETIME_AND_PERFORMANCE.md`.
Baseline: project `d60f03196`, renderer `71a575b3`. Renderer optimization:
`485dcc5c`; producer changes, expanded tests and this report are in the enclosing
project commit.
This work addresses baseline rendering cost, independently of the F2 lifetime fix
and of Grass or other optional effects. No game-code translation, timing change,
decompilation synchronization, asset substitution or quality reduction.

## Evidence Before Editing

The clean baseline was CPU-limited: 68.54 complete native frames/s at 2560x1440,
68.53 at 640x360. This excludes interpolation work, not only double-counted frames.

Windows ETW profiling was unavailable under the machine's profiling policy
(`wpr` 0xc5585011). User-mode sampling with Very Sleepy 0.91 succeeded without
elevation, security-policy changes or recompilation. A 15-second steady-state
capture, filtered to stacks rooted at `TriAevum!main`, attributed:

| Sampled surface | Main-thread exclusive time share |
| --- | ---: |
| Precompiled title module | 42.80% |
| Canonical PICA draw identity construction | 6.02% |
| `memcmp`, all callers | 4.60% |
| Scene uniform content hashing | 4.27% |
| Environment-variable lookup | 2.80% |
| Texture content identity resolution | 2.66% |

Shader catalog resolution was 3.32% inclusive, overlapping `memcmp`. Environment
lookup stacks led specifically to the memory-fill smoke-test switch, queried on
every native draw even when disabled. Scene recording was 9.53% inclusive.
These statistical categories overlap where stated; they are not additive phase
timers or benchmark results. Without the DLL's matching private symbols, samples
inside it resolve to its nearest exported symbol, `triaevum_title_whole_aot_query`:
that name does **not** mean the query function itself consumed 42.80%.

## Changes and Ownership

1. **Immutable texture versions at the producer.**
   `tools/oot3d/native_game_runtime/oot3d_native_pica_submission.{h,cpp}` computes
   both whole-payload and base-mip FNV hashes when creating the immutable resolved
   texture snapshot, after any texture payload transform. The draw planner consumes
   these identities without hashing mipmapped texture bytes again on every draw.
   A changed source, transform invalidation or savestate restore rebuilds the
   snapshot and its derived identities. Legacy/unversioned inputs retain the full
   validation/hash path in `oot3d_native_pica_vulkan_plan.cpp`.
2. **Shader catalog consumes the existing source-identity contract.**
   `fast/oot3d/pica_scene_frame.{h,cpp}` accepts the generator's dual-hash/size
   identity, validates it on first admission and compares identities on repeated
   use. The NRI adapter forwards canonical and effective identities separately.
   Callers without identities retain exact source comparison. Key conflicts,
   invalid sizes and hook-metadata conflicts still reject publication. No raw
   pointer lifetime shortcut or per-scene cache.
3. **Fast, shared hashing for ephemeral content versions.**
   `include/fast/renderer/content_hash.h` factors out the existing portable
   CityHash64 implementation from the Azahar texture-pack module. Uniform/fog
   content versions use it through `HashPicaUniformBytes`; all bytes still
   participate. Azahar's existing wrapper and known-answer tests remain unchanged.
   These versions are transient scene-publication tokens, not serialized state.
   Canonical PICA, shader-pack and native asset FNV identifiers are unchanged.
4. **Cold diagnostic configuration stays off the hot path.**
   `BuildNativePicaMemoryFillSmoke` reads its startup environment switch once,
   consistently with other backend diagnostic switches. Normal settings still
   update live through the owned settings system.

The scene remains fully published with effects off. No consumer was disabled to
obtain the gain, and no native color/depth/blend/order/pass contract was changed.

## A/B/A/B Results

Same Kokiri `hudtest` checkpoint, title DLL, assets and private copies of settings
and saves. Each run: 1,200 presentations, 1,199 actual game updates; first 120
presentations excluded, 1,080 measured. 2560x1440, effects/AA off, VSync/pacer off,
visual interpolation explicitly off, zero interpolated or repeated lists.
No framebuffer capture, validation, sampling profiler or concurrent build in the
timed runs. Bounded renderer diagnostics are identically enabled in both builds.

| Order | Executable | Native frames/s |
| --- | --- | ---: |
| A1 | Before | 68.710 |
| B1 | After | 79.547 |
| A2 | Before | 68.574 |
| B2 | After | 78.390 |

Mean: **68.642 -> 78.968, +15.04%**. Per complete native frame:
**14.57 -> 12.66 ms**, approximately **13.1% less elapsed work**.
Warm backend draw CPU timing: **2.905 -> 1.409 ms, -51.5%**. Its command/publication
stage falls from 2.337 to 0.865 ms. Planning totals, including warmup, fall from
about 1.75 to 1.36 ms/frame. Whole-run phase counters include startup and may
overlap; do not add them or compare their cold costs as steady-state measurements.

Each measured frame still has 107 native draws, 107 published lighting states,
20 skeleton states, zero rejected scene records and the same geometry upload
counts. This is uncapped processing capacity with native tick semantics, **not**
ordinary-speed gameplay FPS and not synthetic x2/x3 presentation FPS. Do not
generalize the percentage to every scene or GPU configuration.

## Verification

- 352 renderer/foundation/GPU tests across 110 suites pass, including the expanded
  scene-publication and Azahar texture-pack suites.
- Native submission and Vulkan draw-plan executables pass: cache reuse, transformed
  payloads, mip identity, changed base mip, restore and legacy input paths.
- Four framebuffer pairs at native frames 120/240/360/480, 640x360, have **zero
  differing channel bytes** before/after. Vulkan validation was enabled for these
  separate functional runs, with zero scene-record rejections.
- Enhanced rendering/F1/F2: 1,000 presentations, configured/off/restored/off/restored;
  zero GPU-validation or effect-graph errors. Interpolation was active in this
  separate functional test (665 updates, 662 interpolated and 335 repeated lists),
  not in the timed A/B measurements. Framebuffer inspection confirms Grass, Toon
  and HUD after restoration. The enhanced Kokiri path still rejects four scene
  metadata records per enhanced frame, exactly as the pre-change baseline did
  (`f2-general-performance-after-20260907`); this pre-existing publication gap is
  not claimed fixed. The native baseline rejects none.

## Reproduction and Artifacts

Active worktree: `I:/oot3dre_work/triaevum-release`.
Runtime: `I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe`.
Before binary: same directory, `TriAevum.before-native-costs.exe`; original PDB
preserved under `I:/oot3dre_work/native-performance-20260907-before/`.
The catalogued user release has not been rebuilt or overwritten by these tests.

```powershell
node tools/triaevum_release/tests/benchmark_gameplay.mjs `
  exe=I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe `
  profile=I:/TriAevum-0.6.0-candidate-r1/TriAevum.launch.json `
  state=I:/oot3dre_work/native_game/checkpoints/hudtest.oot3dsav `
  output=I:/oot3dre_work/UNIQUE_NATIVE_BENCHMARK_DIRECTORY `
  overrides=tools/triaevum_release/tests/renderer_native_performance.json `
  frames=1200 warmup=120 diagnostics=true interpolation=false
```

Paired evidence: `I:/oot3dre_work/native-costs-20260907-{a1,b1,a2,b2}/`.
Framebuffers: `.../native-costs-20260907-visual-{a,b}/`.
Enhanced test: `.../native-costs-20260907-effects-validation/`.
Original sampler capture and machine-readable summary:
`I:/oot3dre_work/native-performance-20260907-sample/{cpu.sleepy,cpu-summary.json}`.

User-mode profiler: `I:/oot3dre_tools/verysleepy/app/sleepy.exe`, unpacked from
the official [Very Sleepy 0.91 release](https://github.com/VerySleepy/verysleepy/releases/tag/v0.91).
Attach only after benchmark startup: `/a PID /t 15 /o ABSOLUTE_CAPTURE.sleepy /q`.
Use an owned, bounded benchmark process and close any remaining profiler window.
The runtime's embedded PDB path is `.../symbols/oot3d_native_game.pdb`; preserve
the matching binary/PDB pair before subsequent linking. Summarize with
`tools/triaevum_release/tests/summarize_cpu_sample.ps1 -Capture PATH -Output JSON`.

## Next Highest-Value Work

Follow-up implemented and measured in
[title and audio CPU costs](TRIAEVUM_TITLE_AND_AUDIO_CPU_COSTS.md): matching title
symbols, allocation-free audio consumption, differential playback/state checks,
and a narrowed list of common title helper costs. The renderer work above remains
the independently measured +15% baseline tranche.

Re-profile the reduced renderer before pursuing smaller descriptor/binding
optimizations. The title execution interval remains dominant and unchanged:
obtain matching function-level symbols/mapping for the existing working module
to distinguish actual game work from memory/dispatch ABI overhead. Do not label
all its sampled addresses as the exported query function. Canonical identity
construction and allocation churn (including the DSP sample queue) remain
identified candidates, not implemented fixes. Preserve fast runtime-only builds,
native no-interpolation A/B measurements and framebuffer equivalence checks.
