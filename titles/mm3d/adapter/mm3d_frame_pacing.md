# MM3D EUR v1.1 native30 presentation limiter

`mm3d_frame_pacing.h` supplies title facts to the shared `NativeFrameRatePolicy`.
It uses the three already-registered limiter PCs from `mm3d_timing_observer.h`;
no generated registry or guest simulation/math hooks change.

## Native proof

- `1D6F7C` reads the limiter owner through `6A3A48 + 4`.
- Constructor `108FF4..1090D4`: r5=2; manager C8/CC = 16/2;
  counter `6A39F8+10` = 0, native interval `6A39F8+1C` = 2.
- `108F5C..108F68`: deadline D0 = counter + native interval.
- `115A08..115A10`: counter increments with plain ARM ADD/STR. Unlike OoT,
  it wraps at 2^32, not modulo 0x7fffffff. The existing signed BGT at
  `124F78` is preserved, including its signed-boundary behavior.
- `124F6C` runs **after** the mandatory wait at `124F68`. Once per real begin,
  shared pacing rebases D0 to current counter + (2 - 1). Subsequent loop
  decisions never move the deadline again.
- At `124F7C`, shared pacing writes C8/CC = 2/2. Native code then computes
  CC + (counter - deadline) + nativeInterval and clamps to C8 at `124F98`.
  We must bound C8 too: writing CC alone would not bound the native result.

All three sites require their four-word guards and registered blocks. Additional
constructor/getter/counter/clamp words and address literals are checked. Runtime
admission requires aligned non-overflowing state, current manager identity,
native interval exactly 2, maximum 16 or 2, interval <=16 and the expected r6
at decision/commit. Unsupported or unreadable admission invalidates the epoch
without writes. Shared policy read/write failure stops guest execution rather
than continuing a partial write. CPU registers are never modified.

## Lifecycle and receipts

Restore/scene/session reset clears the active begin. A restore directly into
an old wait/commit is left native until the next observed real begin, avoiding
an invented or repeated pacing decision. The policy is re-admitted against
current guest ownership/rate at each site. Guards survive temporal resets.

`frame_pacing` reports shared begin/decision/commit/write/failure counts and
rejections/unpaired sites. `output_samples` checks C8/CC at the next begin after
a committed frame; `output_violations` counts maximum !=2 or interval >2.
These are post-native-commit observations, unlike the read-only observer's
`commit_input_intervals`. Tests constrain all guest writes to C8/CC/D0.

## Scope and qualification

No writes to global `6A3A14`, VSync, GameState, player, three-day clock, animation
or physics. This adapter never invokes OoT's simulation-rate update method.
The native30 contract is independent of host presentation/interpolation mode.
This is not true60 simulation or proof of player/forms/three-day-clock parity.
The shared host scheduler's existing bounded debt/batch policy is unchanged.

Offline tests cover every guard word/missing block, original EUR image,
owner/rate/field/register rejection, repeated decision, long-stall native
clamp arithmetic, both counter boundaries, reset-mid-loop and fail-stop memory
errors. Shared tests compare default OoT guest bytes against its old formulas.
Live acceptance and the still-required OoT O2 capture gate are tracked in the
HOOKS lane's external RESULTS.md; unit byte comparisons do not replace O2.
