# CPU/AOT levers: implementation and measurement

Branch `perf/aot-levers`, based on `807b66e`. Developer work only: the
release catalogue, installed modules and Forge are unchanged.

## Measurement protocol

- Field checkpoint `<verify-20260910>/win-field`, neutral input,
  `native30_no_interpolation`, `--throughput-benchmark`, 900 updates per run,
  last 720 measured, alternating ABBA pairs (`measure_prebackend.py`,
  `summarize_aot_abba.py`).
- `--no-present` (commit "add a no-present developer throughput mode"). The
  test display is limited to 30 Hz, and earlier runs on this branch were paced
  by it through the frame fence, not by game pacing, VSync or GPU clocks.
  Results measured before this mode are not throughput results.
- GPU clocks locked at 1800-2100 MHz for the measured campaigns.
- The host carried unrelated background load (about 4-6 busy logical cores,
  mostly idle agent sessions). A/A spread under that load is about +/-5% per
  pair; single-pair differences below that are not claimed.
- Control module: current generator, 256 affinity shards, no instrumentation.
  Every comparison keeps the same host executable in both arms unless stated.

## Results

| Lever | Change | Pre-NRI ratio (median of 4 pairs) | Guest ratio | Fingerprint | Decision |
| --- | --- | ---: | ---: | --- | --- |
| Host helper fast memory | exact single-page `ReadBytes`/`WriteBytes` forms, product skips overlay stub | 0.999 (0.94-1.04) | - | identical | kept: exact, no measurable gain |
| PGO | instrumentation profile from 7 non-Field workloads, `-fprofile-instr-use` | **0.864** (0.836-0.871) | **0.822** | identical | **candidate for promotion** |
| Inline host-float VFP | exact SSE fast paths for add/sub/mul/multiply-accumulate/int-to-float, software fallback | **0.924** (0.89-1.00) | **0.946** | identical | **kept** |
| Inline host-float VFP + its own PGO profile, vs PGO | same training set | **0.916** (0.88-0.95) | **0.926** (0.91-0.93, all pairs) | identical | **candidate for promotion**, 204 MB module |
| Host: draw-plan vector capacity, allocation-free vertex layout | accumulator reserves the previous frame's draw count; `BuildPicaNriVertexInputLayout` drops per-draw `std::set`s and regrowth | **0.934** (0.87-0.95) | 0.989 | identical | **kept**; whole frame **0.919** (all pairs <= 0.93), same module both arms |
| Host: incremental register-file ID | FNV-1a fold state kept per 32-word block, resumed from the first changed block | 0.983 (0.96-0.99) | 0.985 | identical | **kept**; whole frame 0.980 |
| Host: fragment lighting decode reuse | per-thread memo on the registers the decoder reads | **0.955** (0.92-1.00) | 0.976 | identical | **kept**; whole frame 0.960 |
| Host: snapshot reserve, per-slot sampler memo | resource vector reserved once; last sampler per texture slot | 0.984 (0.97-1.00) | 0.981 | identical | **kept**; whole frame 0.992 |
| Host: SVC event names without streams | hex helper + `std::to_string` for ArbitrateAddress/WaitSynchronizationN | 0.982 (0.96-0.99) | 0.976 | identical | **kept**; runtime.json SVC text identical |
| IR-level PGO instead of front-end PGO | `-fprofile-generate`, same training set | 1.016 (0.97-1.05) | 1.015 | identical | rejected: no gain over front-end PGO |
| Context-sensitive PGO on the IR profile | IR profile + CS counters (2,404 functions) merged, `/lto-cs-profile-file` | 1.004 (A/A) | - | identical | not applied: module byte-identical to IR PGO; cause not investigated |
| Context-sensitive PGO on the front-end profile | `/lto-cs-profile-generate`, merged profile at compile and link | - | - | - | inapplicable: CS-PGO needs an IR-level profile; the module was byte-identical to PGO (its ABBA, 0.966 pre-NRI, is an A/A reference) |
| Local registers for every function + PGO | generator `--local-state`, same training set | 1.000 (0.96-1.05, 3 clean pairs) | 1.020 | identical | rejected: no gain, 318 MB module |
| Audio kernels, memory-region lowering | `--memory-region` for `00466E2C`, `004A022C`, `004A0338` (views, local registers, local budget) | 0.996 (0.98-1.03) | 0.997 | identical | rejected: no gain; hand-written natives of the same shape are not pursued |

PGO whole-frame ratio 0.901 (about 110 to 122 fps in this configuration).

Cumulative, 2026-09-29: control host and module against the current host
(fca3e07) with `mod-vfp-pgo`, one ABBA: pre-NRI 6.84 -> 4.92 ms (0.726),
guest 5.20 -> 3.76 ms (0.731), whole frame 9.39 -> 7.08 ms (0.760, about
107 to 141 fps); fingerprint identical, all pairs 0.71-0.77.

### PGO qualification

| Check | Result |
| --- | --- |
| Field, 360 updates, captures at 120/240 | both framebuffers byte-identical to the control; memory content/state, process state, 34,200 draws, 720 transfers, empty queues and PCM FNV identical |
| Sheik raw copy, 400 updates, captures at 120/240/360 | all three framebuffers byte-identical; memory/process state, 41,671 draws, 1,118 transfers, empty queues and PCM identical |
| Cross-check with history | memory fingerprints equal the 2026-09-27 ABI-3 qualification (`6672342775686176555`, `17615798149623760801`) |
| Untrained scene (`native-child-field`, ABBA, no-present) | pre-NRI **0.872** (0.868-0.880), guest **0.834**, whole frame 0.908; fingerprint identical |
| Startup | process CPU including startup 10.55-10.94 s (PGO) versus 11.36-13.42 s (control) per run; no load-time penalty observed |

The PGO module is 197,840,896 bytes versus 87,777,792 for the control; the
maintainer accepted this size. The release builder now takes the profile
(`build-title --profile-data`), see `TRIAEVUM_PRECOMPILED_RELEASE.md`.

### PGO training set

`practice-windmill`, `practice-meadow`, `temple-sword`, `ocarina-final-abi`,
`win-coldboot`, `practice-tomb-forward` (all `<verify-20260910>`)
and Sheik raw copy with `native_cutscene_dialogue_advance.json`, 900 updates
each. `win-items` was excluded: the checkpoint no longer restores on this host
("display transfer source has no Vulkan render target"). The measured Field
scene is deliberately not in the training set. Merged profile SHA-256
`7804e25b3d311100fd8b72525cd2c7820b482705e782e67bf75616366eac28ce`.

### Where the guest thread spends time

User-mode RIP sampling of the PGO module on Field (`measure_prebackend.py
--sample-map`, 5,462 samples of the guest thread, cold renderer cache) put 29%
of samples in the title module. Within the module, 22% were the software
binary32 helpers (`RoundFinite` 10%, `F32Mul` 3.8%, `F32Add` 2.8%, other
`VfpBinary32*` 2%); translated bodies with their inlined memory helpers were
75%; block-entry helpers 1%. Front-end PGO counters are pre-inlining call
counts and do not measure cost: `Oot3dAotFlagLive` has the highest count but
folds to nothing.

This motivated exact inline host-float VFP fast paths
(`oot3d_native_a32_vfp_fast.h`, differential test
`oot3d_native_a32_vfp_fast_tests`).

### Host submission costs

Call-stack sampling (`sample_aot_stacks_windows.cpp`, warm renderer cache,
no-present, 125 fps) of the main thread after inline VFP: guest 46% (title
code 36%, SVC/GSP into the PICA frontend 10%), draw-plan submission 29%
(`SubmitPicaDraw` 23%), draw-plan construction 9.5%, heap 9-10%. Two
allocation hot spots were exact to remove: the visual-frame accumulator
restarted its multi-kilobyte draw-plan vector from zero capacity every frame
(3% in plan moves alone), and the per-draw vertex input layout built two
`std::set`s and grew three vectors. Remaining submission costs by inclusive
share: `BindOwnedDraw` 6.8%, pipeline identity construction and map lookup
2.6%, reflection-environment shader-source substring search 1%, per-draw
`getenv` 0.7%.

### Inline VFP qualification

`mod-vfp-pgo` (inline VFP with its own profile) was captured with the same
probe settings as the PGO qualification and compared with the control hashes
recorded there: Field (captures at 120/240) and Sheik raw copy (120/240/360)
framebuffers byte-identical; memory content/state, process state, draw and
transfer counts, empty queues and PCM identical.
The host allocation changes, and each later host step (register-file ID,
lighting reuse, sampler memo, SVC names), were qualified the same way (new host with
`mod-vfp-pgo`): all captures and contracts identical to the control.

## Reproduction

```powershell
# instrumented module, profile, profiled module
build_aot_header_experiment.py --generated <gen> --output <gen-module> ... --pgo-generate
collect_aot_profile.py --invocation <template> --executable <host> --plugin <gen-module dll> \
  --workload <checkpoint>[::<timeline>] ... --llvm-root <llvm> --output <profile-dir>
build_aot_header_experiment.py --generated <gen> --output <use-module> ... --pgo-use <profile-dir>/title.profdata
measure_prebackend.py --invocation <template> --executable <host> --baseline-plugin <control> \
  --comparison-plugin <candidate> --runs 8 --frames 900 --no-present
```

Private evidence: `<work>/perf-levers/` (runs, profiles, modules).
Its `DISPOSABLES.md` lists every artifact and its removal condition.

## Parallel presentation (product mode)

Parallel presentation presents frame f on a render thread while the guest
simulates f+1 (design, qualification and measurements in
`TRIAEVUM_PARALLEL_PRESENTATION.md`). Interpolated2x, unpaced, no-present,
against host6 (fca3e07) with `mod-vfp-pgo`: Field 0.616 (5.61 -> 3.46 ms per
presented frame), Sheik copy 0.768 (12.79 -> 9.83 ms). On by default;
`--serial-presentation` disables it.

## Not yet done

- Done 2026-10-05: `prepare_release --title-profile` ships the profile with the
  translated title source and the release audit requires it for a profiled
  module (`TRIAEVUM_PRECOMPILED_RELEASE.md`, "Profile-guided title builds").
- PICA plan/queue off the guest critical path; block-entry contract changes;
  further call-free leaf functions.
