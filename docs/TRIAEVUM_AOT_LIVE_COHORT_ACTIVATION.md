# Live cohort activation experiment

2026-09-17. Follow-up to [the 303-function cohort](TRIAEVUM_AOT_300_FUNCTION_COHORT.md).

## Decision

The qualified groups now execute on **live game state**, not only private replays.
The reversible ABI adapter is retained as a developer validation tool, **not a
production optimization**: its measured overhead outweighs the local savings.
The normal executable, shipped AOT module, renderer and release settings remain
unchanged. Do not enable this adapter by default or claim a frame-rate improvement.

## Implementation

`aot_live_cohort_probe.cpp` is a small proxy DLL around the frozen original and the
private cohort DLL. It accepts only roots supplied from successful replay results.
`triaevum_family_eligible` exposes the same root/observer/return-address guard used
by the candidate; no independent, weaker copy of that policy is introduced.

For an eligible call:

1. The original's block callback takes an observable exit **before** the first
   instruction of the group. Its own snapshot/exception machinery preserves the
   architectural state across the original DLL's stack unwind.
2. The proxy refunds the one block charged by `EnterBlock` before notifying. It
   then executes the candidate against real memory, with only the remaining budget.
3. It resumes at the caller's return PC in the frozen original. The PC must exist
   in its dispatch table; no guessed continuation, global budget reset or hidden
   guest-time advancement is used.

Direct entry at an eligible root avoids the initial unwind. Unfiltered tracing,
active observers inside a group, and unregistered returns remain on the original.
Faults and budget exits propagate; no partially executed candidate is retried from
its beginning. The proxy forwards existing native callbacks rather than swallowing
them. The candidate families exclude external native-owner calls as before.

The original's V2 table contains 162,710 dispatch entries. All 88 recorded return
PCs from the successful batch are registered, making this experiment possible
without rebuilding the full title. This is an OOT3D developer adapter, not a new
portable public ABI or a renderer extension.

## Results

Private evidence under `C:/Users/xander/triaevum-aot-causal-analysis/`:

| Run | Activated scope | Calls | Candidate blocks | Pre-NRI baseline | Pre-NRI candidate |
| --- | --- | ---: | ---: | ---: | ---: |
| `cohort-live-line-ab` | `003723C0` line-test group | 540 | 2,288,880 | 6.644 ms | 7.210 ms |
| `cohort-live-88-abba/candidate-retry` | Allowlist of 88 matching roots | 1,388,250 | 27,527,765 | 6.952 ms | 19.926 ms |

Counters cover the complete 360-frame run. Timing covers only the final 180 frames
after warmup, not process startup, asset loading or private replay work. Vsync,
pacing, frame limiter and visual interpolation are disabled and checked by the
existing measurement harness. The aggregate counter does not claim that every
allowed root is independently entered: groups may call shared members internally.

Both completed candidate runs preserve the exact reference memory-content,
memory-state and process-state fingerprints:
`14004724930905175187`, `16777719094724974201`, `11508416489629890540`.
Thus actual call-chain activation is established for this scenario. This is not
complete gameplay, per-frame framebuffer equivalence or Linux qualification.

The attempted four-run ABBA stopped at the first candidate's startup timeout,
before runtime output. The harness killed that process. Reusing the same adapter
without rebuilding then completed the full cohort run. Consequently the table is
**not a completed/repeated ABBA estimate**; the large observed regression is enough
to reject default promotion, not to assign an exact exclusive cost to exceptions.
Do not interpret those startup stalls as AOT execution measurements.

All intercepted candidate entries in these runs used an unwind and one refunded
block. The full allowlist causes about 3,856 such handoffs per frame across the
complete run. Unwinding, architectural-state round trips, hook-list assembly and
dispatcher re-entry are extra work not present in the local replay measurement.
This explains why local kernel savings cannot simply be added up as game savings.
No attempted micro-optimization of the proxy is presented as a solution.

## Tests and reproduction

- `aot_live_cohort_tests.cpp`: 128 modeled routing cases across budgets 1..8,
  absent/filtered/unfiltered/root observers, skip-first behavior and memory faults.
  Register state, exit/detail and consumed block count match uninterrupted execution.
- `test_aot_live_cohort.py`: allowlist restriction, canonicalization, alignment,
  range checks and empty-selection rejection.
- The family DLL adds only an eligibility export; its 303-function generated
  computation remains the previously built cohort. New build: `cohort303-live-ready`.

Use `run_aot_live_cohort_probe.py` with compiler/support/include, frozen baseline,
candidate DLL, successful `batch.json` as `--qualification`, executable, invocation
and a new private output directory. Optional repeated `--root` narrows the allowlist;
`--abba` requests baseline/candidate/candidate/baseline. Each run has copied user
configuration/save data, provenance, bounded child-process cleanup and counters.
An incomplete run is never accepted as successful activation.

The expanded run was retried using its existing `live.dll`, the same recorded
allowlist/environment and `measure_prebackend.py`, outputting to `candidate-retry`.
Its counter and runtime JSON files are authoritative; the interrupted ABBA's
`results.json` contains only its completed baseline. No unfinished game process
was deliberately left running.

## Next implementation boundary

Follow-up implemented and tested in
[the direct-link pilot](TRIAEVUM_AOT_DIRECT_COHORT.md). It removes the ABI handoff,
but has not demonstrated repeatable total savings and remains developer-only.

To obtain speed rather than only qualification, integrate the cohort at the
**generated direct-call boundary inside the AOT module**:

1. Keep the original observed implementation and the root-specific eligibility
   guard. When eligible, call the specialized group directly using the existing
   `Oot3dWholeAotFrame`, context and promoted architectural state.
2. Share the original block budget and return convention, including declared tail
   calls. Do not translate the handoff into an `ObservableExit`, an exception, or a
   trip through the host runtime. Existing external native owners stay unchanged.
3. Build the cohort as a separate reusable translation unit and regenerate only
   affected caller/root shards, reusing verified unchanged objects. The small
   cohort itself builds in seconds; fast relinking must be established before a
   full-title experiment. This direct-link path is **not implemented by this proxy**.
4. Repeat live-state verification and a complete non-instrumented ABBA. Keep the
   original default until an actual, repeatable total pre-NRI benefit is measured.

This preserves the successful correctness work without shipping a validation
mechanism whose runtime cost destroys the intended performance benefit.
