# CPU geometry kernel experiments

2026-09-17. Follow-up to `TRIAEVUM_NATIVE_PROJECTION_KERNEL_EXPERIMENT.md`
and baseline commit `18a0b56`. Developer-only Windows/x64 experiments, **not
registered in the production runtime or release**. These title-derived algorithms
are not title-neutral renderer code. External decomp was read only.

Larger-family follow-up: `TRIAEVUM_AOT_CLOSED_FAMILY_EXPERIMENT.md` covers
6- and 21-function groups and distinguishes their controlled results.

## Implementation

`tools/oot3d/native_a32_runtime/oot3d_cpu_geometry_kernel_probe.cpp` contains
two independently routed kernels; the corresponding `_tests.cpp` exercises both.

| Native entry | Complete operation | State contract |
| --- | --- | --- |
| `0040C744..0040C9CC` | Build local matrices for an entire skeleton pose | Original angle reduction, trig table, evaluation order, VFP/GPR, FPSCR/CPSR, stack/matrix writes, block count |
| `002BFCB4..002BFD74` | Decode three indexed collision vertices | Nine exact signed-int16 to binary32 conversions; flags masked from the first two indices only; register/write results |

The skeleton reads constants at `0040C9CC` and the table address at `0040C9DC`.
Neither trigonometry nor matrix scale is replaced with guessed values or host
`sin/cos`. Arithmetic is ARM CPU binary32, **not PICA arithmetic**. The already
decompiled PICA vertex shader remains unchanged.

Validation and output staging finish before any guest write. Generic overlap,
unmapped/protected memory, unsupported FP modes, exceptional arithmetic, interior
hooks, insufficient block budget and non-ARM-aligned returns retain the baseline
path. The original write sequence and generation count are preserved. Allocation
is included in timings. Build with `/fp:strict /clang:-ffp-contract=off`; preserve
host MXCSR and the original non-fused arithmetic, including VNMLS.

## Evidence and tests

Read-only sources:

- `I:/oot3decomp/src/runtime/owner_runtime/z_owner_closeout_long_tail_11.c`,
  `0040C744`, corroborated against all 162 original ARM instructions.
- `I:/oot3decomp/src/genuine/z_genuine_cohort_13.c`, `002BBF74` and `002BFCB4`;
  the latter has 48 ARM instructions.
- `I:/oot3decomp/src/runtime/owner_runtime/z_owner_register_abi_deep_07.c`,
  material/mesh traversal reviewed for the next ownership boundary.
- The frozen AOT program and original code from the projection experiment.

The skeleton decomp contains pointer/integer casts where ARM uses bit-preserving
VMOV and numeric comparisons where ARM compares float bit patterns. The prototype
implements the verified instruction meaning, not those literal C casts. This was
an evidence audit, not a new Ghidra run or decomp synchronization.

1,024 seeded synthetic invocations compared against the frozen whole-AOT module:

- Skeleton: 512 cases, **456 fast / 56 fallback**, 1..32 bones, positive/negative
  angles, large-angle reduction, signed zeros, subnormal rejection, FZ/DN modes
  and unsupported rounding-mode rejection; host MXCSR restored.
- Triangles: 512 cases, **485 fast / 27 fallback**, varying signed coordinates,
  flagged first/second indices and a high third index that must not be masked.
- Matching GPR/VFP, flags, exit, consumed blocks, memory content, write generation
  and **write-trace fingerprints**, including the original write order.
- Existing AOT diagnostic Python suite: 23 tests passed.

Fault/alias/budget fallback boundaries and every numeric corner are not fully
covered. No Linux/ARM64 implementation or exhaustive correctness proof is claimed.

## Real-input replay

Each run captures an invocation in the unchanged adult-Link field scenario and
executes 16 private ABBA replicas. Eight candidate fast-path hits are required;
matching fallback alone is not a pass. Larger-skeleton selection is diagnostic
only and does not modify the live game's inputs.

| Input | Warm baseline median | Warm candidate median | Warm quartet elapsed B/A |
| --- | ---: | ---: | ---: |
| 25-bone pose, qualifying occurrence 64 | 22.15 us | 9.65 us | 0.429 |
| 25-bone pose, qualifying occurrence 128 | 23.35 us | 10.35 us | 0.436 |
| Triangle, occurrence 64 | 3.00 us | 1.45 us | 0.500 |
| Triangle, occurrence 128 | 3.25 us | 1.45 us | 0.443 |

Warm excludes the **entire first ABBA quartet** consistently. Skeleton warm cycle
ratios: 0.450/0.459; triangle: 0.579/0.561. Counters include query overhead, not CPU
milliseconds. All 64 replicas match. An earlier two-bone capture matched another
16 replicas, but its noisy timing is not evidence of benefit for small skeletons.

All five unchanged-game runs retained final memory-content fingerprint
`14004724930905175187`, memory-state `16777719094724974201`, and process-state
`11508416489629890540`. These establish experiment noninterference, **not**
qualification of an entire game using the candidates. No framebuffer comparison
or whole-game speedup is claimed. Do not use diagnostic-run FPS as a benchmark.

## Scope and remaining candidates

The two new kernels plus projection account for 85/2,214 = **3.84%** of previous
AOT leaf observations (17 skeleton, 26 triangle, 42 projection). These are not
exact CPU times. Even treating them as representative, local reductions imply
roughly 1-2% pre-NRI potential, **not 20%**. Do not justify a full title rebuild or
claim the requested overall target from this result.

| Family reviewed | Decision |
| --- | --- |
| `0036C174`, `00372224`: matrix multiply/copy | Already native external owners; do not duplicate |
| `00307BD8`: PICA register-range writer | Already native external owner |
| `00452934`: mesh transform traversal | Still AOT; crosses matrix helpers and packet writes. Needs a full owner-boundary implementation, not another matrix micro-kernel |
| `00452B40`: material transform traversal | Includes initialization, animated materials and emission; preserve all side effects, no static-material shortcut |
| `002DD5D4`, `002DD628`: bone palette | Reuse matrix owners; avoid duplicating them or hiding callback costs |
| `003084E8`: rotation sampling | Includes native-table tangent behavior; skeleton trig is not a drop-in replacement |
| `002BBF74`: floor collision traversal | New triangle decoder is only one callee; full traversal/nearest-hit semantics remain authoritative |

Promotion still requires routing in the **actual call chain**, preserving existing
budget/hook/exit contracts, followed by nonintrusive whole-game ABBA. This remains
unfinished. Larger gains require a larger measured owner closure, not blind
expansion of similar-looking arithmetic. No NRI, UI, effects, shaders or native
draw-order changes were made.

## Reproduction

Compile the probe and its tests separately with clang-cl, `/MT /O2 /EHsc
/std:c++20 /DNOMINMAX /fp:strict /clang:-ffp-contract=off`, including native-game,
A32, upstream and the existing nlohmann directories. Link the existing
`J:/TriAevum-verify-20260910/runtime/triaevum_title_whole_aot_support.lib` with lld,
`/threads:2`, and `/LD` for the candidate only. No full-AOT build is needed.

Test arguments: frozen baseline DLL, original private `code.bin`. For real replay
use `run_aot_invocation_probe.py` as described in
`TRIAEVUM_AOT_REAL_INVOCATION_REPLAY.md`, passing the candidate and entries above.
Set `TRIAEVUM_SKELETON_MIN_BONES=16` for larger skeletons: occurrence then means
qualifying occurrence. Unset it for unfiltered runs. Selection is in provenance.

Private artifacts: `C:/Users/xander/triaevum-aot-causal-analysis/`, directories
`skeleton-real-ab`, `skeleton-large-ab`, `skeleton-large-ab-repeat`,
`triangle-real-ab`, `triangle-real-ab-repeat`. Each records module hashes,
invocation CSV and final runtime state. Binaries, inputs and captures stay private.
