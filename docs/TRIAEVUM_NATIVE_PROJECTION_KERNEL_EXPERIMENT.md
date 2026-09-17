# Native CPU projection experiment

2026-09-17. Developer-only prototype, not enabled in the shipped game and not a
20% total pre-NRI performance result.

## Boundary and evidence

The translated GPU vertex family is already present in
`tools/oot3d/native_pica_frontend/oot3d_translated_vertex_programs.h`. Its provenance
records `CmbVShader.shbin` and two `profile.shbin` entries, produced using the
Citra/Azahar decompiler. `oot3d_native_pica_shader_gen.cpp` selects matching
translated bodies and rejects runtime translation in the required-native path.
This experiment neither edits nor replaces that renderer path or its cache policy.

The CPU still prepares and projects scene data before NRI. The decompiled
`I:/oot3decomp/src/genuine/z_genuine_cohort_20.c`, function `0033B11C`, describes
a complete matrix/point projection. Its 42 original ARM instructions and emitted
whole-AOT body establish load order, the non-fused VMLA arithmetic, output-Z sign,
literal operand, final VFP lanes and stores. The external decomp remains read-only.

Important distinction: **ARM binary32 is not PICA arithmetic.** The vertex shader
helps explain the graphics boundary, not substitute GPU rounding for CPU math.

## Replacement

`tools/oot3d/native_a32_runtime/oot3d_projection_kernel_probe.cpp` is explicitly
title-derived diagnostic code. It computes four rows together with SSE, using
separate multiply and add operations. It replaces the full projection algorithm,
not individual ARM instructions with another helper-call sequence.

- Validate whole input/output ranges before taking the fast path; otherwise
  delegate to the original module without modifying guest state.
- Read the fourth coordinate from the original literal; never assume it is one.
- Load all operands before stores and retain native output ordering for aliases.
- Preserve native final GPR/VFP state, cumulative exception flags and write count.
- Retain the original path for NaNs, subnormal inputs/intermediates, overflow,
  underflow, unsupported FPSCR modes and observer/entry conditions.
- Save/restore the host floating-point environment once per projection.
- Require precise exception-aware compilation and prohibit FMA contraction.

The first numerical test caught matching values but a missing underflow flag.
Exception-aware compilation (`/fp:strict`, no FP contraction) resolved the tested
discrepancy; source pragmas also declare precise exception semantics. This is why
visual matching or ordinary `float` expressions alone are not sufficient.

## Validation and measurements

`oot3d_projection_kernel_probe_tests.cpp` checks 40,000 inputs against the existing
software ARM VFP implementation: **32,175 fast-path acceptances agree on values
and flags; 7,825 inputs are rejected to retain the original path**. Host MXCSR
restoration is checked on both acceptance and rejection. These are prototype
tests, not exhaustive proof of every input, alias, fault or platform.

The real-input proxy captured occurrences 64 and 128 of `0033B11C` in separate
360-update field-checkpoint runs. Each runs 16 ABBA replicas, with eight native
acceptances confirmed by the exported counter, and exact final replica state.

| Real invocation | Warm baseline median | Warm candidate median | Median quartet B/A wall ratio |
| --- | ---: | ---: | ---: |
| Occurrence 64 | 3.75 us | 1.15 us | 0.298 |
| Occurrence 128 | 4.15 us | 1.25 us | 0.278 |

Warm medians exclude the first quartet; the last column includes all quartets.
Excluding the first quartet gives a median ratio of 0.310 in both runs.
Thread-cycle ratios also favor the candidate (all-quartet medians 0.400/0.375),
but contain counter-call overhead and must not be converted to CPU milliseconds.

Both full runs preserve the control's process/memory fingerprints, PCM,
34,200 draws and 720 display transfers. **The running game still executes the
baseline; only private replicas execute the candidate.** Consequently these
fingerprints establish noninterference of the experiment, not qualification of
a game running entirely with the replacement. No candidate whole-game speedup
is claimed. All test processes exited.

## Scope decision

This is a repeatable **approximately 70% local elapsed-time reduction** for two
captured inputs. The function accounts for only 42/2,214 (1.9%) earlier AOT samples.
Even assuming that share were representative CPU time, the implied whole pre-NRI
saving is roughly 0.8%, not 20%. This isolated result cannot justify promotion as
a significant overall optimization or an expensive full title rebuild.

The useful finding is that algorithm-level batching can remove most translation
cost in a graphics calculation while preserving observed ARM state. Extending it
to a larger matrix/skeleton/visibility family still needs a measured cost budget,
equivalence tests and execution in the real call chain. Do not apply PICA shader
arithmetic to these CPU calculations or infer correctness from the shared subject
matter. The 20% objective remains open.

## Reproduction

Compile the test and candidate sources separately using the existing clang-cl,
`/MT /O2 /EHsc /std:c++20 /DNOMINMAX /fp:strict /clang:-ffp-contract=off`, with
the runtime, A32, upstream and existing nlohmann include directories. Link the
existing `triaevum_title_whole_aot_support.lib` with lld; `/LD` for the candidate.
No full-AOT build is involved. The scalar oracle remains the existing support lib.

Use `run_aot_invocation_probe.py` as described in
`TRIAEVUM_AOT_REAL_INVOCATION_REPLAY.md`, adding `--candidate` for this DLL,
`--entry 0033B11C` and `--occurrence 64` or `128`. The proxy records actual native
acceptance through the optional `triaevum_invocation_candidate_hits` export.

Private artifacts: `C:/Users/xander/triaevum-aot-causal-analysis/`, files
`projection-test.exe`, `projection-candidate.dll`, directories `projection-real-ab`
and `projection-real-ab-repeat`. Run provenance records the module hashes.
This Windows/x64 SSE prototype has no production build or release registration.
Future portable implementations need the same numeric contract and tests; no
new platform dependency was added to the runtime or renderer.
