# OOT3D CSAB Source Overlay Owner

## Scope

This owner promotes the two native CSAB curve evaluators from whole-AOT into
the hot source overlay while retaining whole-AOT fallback for every entry not
yet promoted. It is a runtime composition change, not an asset conversion.

| Entry | Native owner | Source import |
| --- | --- | --- |
| `0x003084E8` | `Oot3d_EvalAnimationCurveS16` | `csab_curve_eval_d5c2927` |
| `0x003087A4` | `Oot3d_EvalAnimationCurveF32` | `csab_curve_eval_d5c2927` |

The dispatch boundary is `tools/oot3d/source_overlay/oot3d_source_overlay_csab_curves.cpp`. It copies curve bytes through the host memory API, executes the shared core in `tools/oot3d/native_game_runtime/oot3d_source_csab_curve_core.cpp`, and writes back only the target-observable guest state and stack scratch. No N64 asset format or runtime asset adapter is involved.

## Native evidence preserved

The source import is pinned to `d5c292708f7f25f921a066449db3cea43fad73c4` and the original `code.bin` at `E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin`. The S16 Hermite path calls `Math_TanF@0x003555D8`; it is not a `sinf` call and is not delegated to host `tanf`.

The shared core reproduces the observed ARM/VFP contract:

- exact target `Math_TanF` range reduction, constants and binary32 polynomial order;
- negative-tiny classifier return ABI and nested tangent stack writes;
- target FPSCR rounding, flush-to-zero, default-NaN and exception flags;
- VFP comparison flags and the final `CPSR` ownership of the last `Math_TanF` call;
- S16/F32 caller registers, return PC, malformed-input rejection and stack writes.

## Verification

Fast build and unit test:

```powershell
.\scripts\oot3d\Build-Oot3dSourceOverlay.ps1 -Parallel 2 -RunTests
```

Opt-in whole-AOT differential probe:

```powershell
$env:OOT3D_SOURCE_OVERLAY_CSAB_COMPARE='1'
.\scripts\oot3d\Invoke-Oot3dSourceOverlayGame.ps1 -LoadState 'I:\oot3dre_work\native_game\source_overlay_baseline_frame4.oot3dsav' -Frames 2 -MaxSeconds 15 -DisableAudio -SkipOverlayBuild -Output 'I:\oot3dre_work\native_game\csab_probe.json'
```

The validated probe from the promoted build reported `1003/1003` handled
calls, `s16=45`, `f32=958`, `fallbacks=0` and
`differential_mismatches=0`. The overlay registry contains 15 active entries;
whole-AOT fallback remains enabled for the other entries.

## Maintenance boundary

Do not copy the CSAB implementation into the gameplay runtime or add another
curve evaluator. New source owners must use the same host-memory boundary,
bounded differential switch and loader test pattern. If the decomp producer
changes the pinned CSAB source, update the import manifest hashes and rerun
the loader test plus the bounded differential probe before changing the
promotion status.
