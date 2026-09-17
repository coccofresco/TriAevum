# Closed-family AOT experiment

2026-09-17. Follow-up to `TRIAEVUM_CPU_GEOMETRY_KERNEL_EXPERIMENTS.md`.
Developer-only, no production/release registration or NRI changes. This specializes
whole-AOT call families; it is not a new semantic decompilation of 21 functions.

## Structural change

`build_aot_family_probe.py` walks direct calls and tail calls from a root, rejects
recursion, indirect/open control flow, system/memory observers and existing native
external owners, and emits the complete closed family in one translation unit.
No string rewriting of generated code or production-generator modifications are
used. The current generator's explicit callback-free mode supplies the bodies.

The candidate validates the complete family's observer ranges **once at entry**.
If any active callback could apply, it delegates to the frozen original before
executing the family. Otherwise callback checks are absent throughout that call
graph. Block budgets, arithmetic, branches, faults and architectural state remain
in the generated code. The previously verified triangle kernel is composed at its
native call boundary, with architectural state and block accounting preserved.
Its original 192-byte ARM body must match the qualified SHA-256; an address match
alone never authorizes the replacement for a different program/revision.

The control is generated from the same input, compiler and family layout without
callback-free bodies or the native triangle substitution. Both versions now reject
interior observers before entering the private family. A separate frozen oracle
is required when using a family module as the baseline, to prevent self-recursion.

This does **not** eliminate all memory checks or soft VFP operations. It removes
repeated observer handling across an entire closed call graph, rather than merely
optimizing the observer helper. Existing math kernels and native matrix owners are
not duplicated.

## Families and results

| Root | Static functions | Result |
| --- | ---: | --- |
| `002BBF74`, floor-polygon traversal | 6 | Correct replay; no convincing benefit against regenerated control |
| `00324758`, `BgCheck_LineTestImpl` | 21 | Repeatable local improvement, including original-module comparison |
| `0032DB24`, complete floor raycast | 19 including a native boundary | Not built: crosses the existing `0036C174` native matrix owner |

The two built families contain **23 distinct functions**, not 27. The line-test
family includes vector/triangle tests and their scalar math callees. It is not a
replacement of collision rules or scene-specific geometry.

All measurements below are isolated real-input ABBA replay, **not game FPS**.
The entire first quartet is excluded consistently from warm statistics.

| Comparison | Input occurrence | Blocks | Warm A median | Warm B median | Warm quartet elapsed B/A |
| --- | ---: | ---: | ---: | ---: | ---: |
| Floor regenerated control / candidate | 64 | 158 | 8.45 us | 8.05 us | 0.958 |
| Line regenerated control / candidate | 64 | 4,237 | 70.55 us | 40.65 us | 0.624 |
| Line frozen original / candidate | 128 | 4,236 | 71.15 us | 48.70 us | 0.759 |
| Line regenerated control / candidate, repeat | 128 | 4,236 | 68.60 us | 38.95 us | 0.582 |

Corresponding warm thread-cycle ratios for the three line comparisons are
0.626, 0.761 and 0.592. These include counter overhead; they are not CPU milliseconds.
The floor-control quartet ratios ranged 0.600..1.475, so its small median gain is
not promoted. The original frozen-vs-floor-candidate run alone was insufficient:
the regenerated control exposed the weakness of that initial comparison.

The two floor captures each report 104 native triangle hits across eight candidate
trials. **The line captures report zero native triangle hits.** Their benefit is
therefore associated with the broader callback-free family specialization, not
with the preceding triangle micro-kernel. Compiler layout/register allocation can
contribute to that structural benefit; no exclusive hardware-stall attribution is
claimed.

## Verification

- Five real-input runs, 16 replicas each: **80 matching replicas**, 40 executing
  candidate families. GPR/VFP, flags, memory content/generation, exit and consumed
  blocks coincide with their reference.
- Eight family-path hits are required in each capture; native leaf hits are
  reported separately, preventing fallback from masquerading as acceleration.
- All five unchanged-game runs retain memory-content fingerprint
  `14004724930905175187`, memory-state `16777719094724974201`, and process-state
  `11508416489629890540`.
- Six closure-construction unit tests were added; the AOT diagnostic Python suite
  has **29 passing tests**.
- Build-only time: six-function modules about 1.3..1.8 seconds; 21-function modules
  about 2.4..3.0 seconds. No whole-title build, host replacement or object-cache
  mutation was needed.

Remaining qualification: budget-boundary/fault/observer mutation tests, wider
real-input coverage, platform testing, and actual-call-chain deployment. Private
replay runs the candidate on copied game state while live gameplay still uses the
original. Final fingerprints demonstrate noninterference, not candidate gameplay
qualification. No framebuffer comparison was performed and no total speedup is
claimed. The reported FPS of these diagnostic runs is unsuitable for comparison.

## Overall budget and next boundary

Earlier intrusive stack observations contain 127 samples under the floor root and
52 under the line-test root: 179/2,214 = **8.1% of AOT observations**, deduplicated.
All-caller coverage of their static closures is not credited to these activations.
Samples are not exact CPU cost; these results do not establish a 20% pre-NRI gain.

The next useful extension is a declared, verified bridge to the already-native
matrix owner, retaining its implementation, to test the 19-function floor-raycast
family. Mesh/material traversal additionally needs observer/indirect-call ownership
handled explicitly. Do not suppress those boundaries simply to enlarge a cohort.
Only after actual-call-chain integration should a nonintrusive whole-game ABBA
decide whether to promote the specialization.

## Reproduce

Build using `tools/renderer/tev_program/build_aot_family_probe.py` with `--program`,
`--code`, `--compiler`, `--support`, `--include`, `--root`, `--output`. For the line
family use `--root 00324758 --max-functions 32`; add `--optimized` for the candidate.
Inputs and generated title code remain outside the public repository. Each build
records input, generator, generated source, wrapper, kernel and DLL hashes.

Replay using `run_aot_invocation_probe.py`. For matched-family A/B, set `--baseline`
to the control DLL, `--candidate` to the optimized DLL, and `--oracle` to the frozen
whole-AOT DLL. For original-module comparison, set `--baseline` to that frozen DLL
and omit `--oracle`. Use the established field invocation and occurrences 64/128.

Private evidence: `C:/Users/xander/triaevum-aot-causal-analysis/`, directories
`floor-family-real-ab`, `floor-family-controlled-ab`, `line-family-controlled-ab`,
`line-family-frozen-ab`, `line-family-controlled-repeat`; build directories
`floor-family-*-v2`, `line-family-*`. Provenance identifies the exact snapshots;
the later wrapper also preserves unsupported/SVC exit reporting explicitly.
External decomp, original code, the frozen game executable and release files were
not modified. Pre-existing generator/memory-region work remains separate.
