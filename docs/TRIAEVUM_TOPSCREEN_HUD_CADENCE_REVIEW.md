# TopScreen HUD Cadence Review

2026-09-11. Baseline `5c47c67`. Scope: inconsistent HUD visibility, native
update versus host presentation cadence, and original-mod visibility gates.
This is not certification that all reported flashing has been reproduced.

## Confirmed Defects

### Callback-dependent Visibility

`BuildTopScreenPresentation(GameplayHud)` required the gameplay subsystem to
have been observed in the current observation batch. `BeginObservationFrameIfNeeded`
clears that batch when the first UI callback occurs in a new host frame.
An unrelated callback can therefore remove permission to present the gameplay
HUD, even while `ReadTopScreenHudCompositorGate` still says Draw. Frames with
no callback retain the previous batch; the result depends on callback order,
not just game state. Repeated/interpolated host presentations expose this
inconsistent ownership policy.

Regression: seed a valid native HUD, observe GameplayHud, then begin the next
host frame and observe FileSelect without activating its native owner. The
old implementation fails with `unrelated callback hid HUD despite the live
native compositor gate`. The corrected implementation passes.

TopScreen presentation now uses its own live native gate, not N64 shadow-UI
lifecycle observation flags. Those flags remain intact for routing and the
legacy shadow presenter. No lower-screen promotion or retained-frame timeout
is introduced. Live suppression still removes the HUD immediately.

### Mixed-age Presentation Data

Health/magic content previously came from `mLatestState`, captured at the first
UI entry in the host frame, whereas controls, icons, counters, gate and native
vertex streams were read after guest execution. A native update between these
points could mix pre-update health/fades with current controls.

The TopScreen HUD builder now takes a read-only semantic capture at its
presentation boundary, alongside the live streams. It does not call
`CaptureState()`: that lifecycle method also advances Items-hint state and
must not run again merely to render an interpolated frame. No input, timer,
native animation or gameplay update is advanced by the new read.

Regression changes health capacity after the observed native entry and checks
that presentation immediately has four hearts, not the earlier three. Additional
tests retain HUD on presentation-only frames and honor native suppression.

### Missing Native Player Gate

Original TopScreen 2.1.1 EUR function `005CE554` checks the player referenced
by `Play+20AC`: HUD is allowed only if that pointer is absent or its signed
halfword at `+224E` is zero. Our gate omitted this condition.

Evidence: existing read-only export
`I:/oot3dre_work/topscreen-2.1.1-reference/ghidra_export/decompiled/00129_005ce554_FUN_005ce554.c`,
lines around 280-299. The test setting that field nonzero fails on the old
implementation and passes after correction; clearing it restores permission.
The existing no-player fixture still draws. An unreadable player field reports
an error rather than inventing availability. This is native mod policy, not a
new scene-specific suppression heuristic.

## Architecture And Verification

Changes remain in the OOT3D UI adapter and its tests. Shared PICA/NRI scheduling,
shader behavior, gameplay translation, save format and native input are unchanged.
No debounce, multi-frame hold, hardcoded scene exception, alpha smoothing or
unconditional visibility is used to conceal flashing.

- Regression tests for lifecycle and TopScreen profile pass on Windows and
  Linux after reproducing the two visibility failures on the previous code.
- Both product runtimes rebuild incrementally without rebuilding title AOT.
- Windows `hud-review-final-x2`: 160 presentations from the compatible native
  interpolated field checkpoint, 40 consecutive framebuffers at 120-159.
  Heart-region red pixels remain present in every frame (5,913-6,180 pixels
  using R>170, R>1.5G and R>1.5B in rectangle 65,12 to 360,65). This is a
  fixture-specific visual check, not a general HUD classifier or proof about
  every icon. Animated pulse changes are intentionally allowed.
- Windows `hud-review-final-x3`: same 160-presentation test at configured
  90 Hz; all 40 consecutive captures contain the hearts (5,910-6,177 red
  pixels under the same criterion). Exit 0. This tests visibility at 3x, not
  the correctness or distinctness of every interpolated world sample.
- Windows `hud-review-pause-x2`: 600 presentations, two Items open/close cycles
  using repository `topscreen_hud_pause_roundtrip.json`; Items visible at frame
  120 and gameplay HUD restored at 570. Exit 0.
- Linux `hud-cadence-fixed-native`: 540-presentation partial-progress ocarina
  regression, exit 0. Final `hud-review-final-storms`: 1,100 presentations,
  native teaching completion and all five before/after progress checks pass.

Private Windows evidence: `J:/TriAevum-verify-20260910/`.
Private Linux evidence: `/home/xander/triaevum-linux-parity-20260911/`.
Capture runs deliberately stall on synchronous GPU readback; not FPS tests.

## Remaining Investigation

The user has now narrowed the report to Navi notifications and mounted HUD
behavior. A reproducible checkpoint and platform/settings are still needed.
These fixes remove reproduced causes of incorrect visibility, but do not
establish that every report has the same cause. Further focused qualification:

- Individual native alpha streams during item restrictions, targeting,
  damage, cutscenes, riding, pause transitions and quickload.
- Original-mod alternate HUD modes and remaining owner predicates in
  `005CE554`, including companion helper `005DF1EC`, require separate evidence
  before changing their treatment. Do not infer parity from the gate repaired
  here or add broad overrides from an incomplete decompiler expression.
- Texture/resource lifetime and native UI pass ordering when a report concerns
  a specific icon instead of disappearance of the whole host-composed HUD.
- Paired original-mod visual traces, Android and installed Flatpak binaries.

TopScreen is an independent reimplementation of rlgcarrot's mod; attribution
and original reference hashes remain in the existing TopScreen documents.

## Contextual Copies: Navi / Mounted Follow-Up

The previous idle field and menu runs did not qualify either reported case.
In particular, the earlier stamina test only exercised the on-foot hidden lane.

The original 2.1.1 payload has the same ten-copy transform table as the older
implementation, but its current address is `005E1734`, consumed inside
`FUN_005D4A8C`. The former `005C9940` owner annotation is obsolete for 2.1.1.
Do not use an old mod function address against a new payload.

Two missing visibility predicates are now implemented at the title UI adapter:

- `005D6C50..005D6C88`: clear alpha of copies of source quads 34/35 when
  `PauseTouchButton+34` is 7..9 or greater than 11. Source alpha alone is not
  authoritative while the owner is changing modes.
- `005D6890..005D6910` and `005D6990..005D69B4`: source quad 27 requires
  Play type/subtype 3/2, scene 3..16 inclusive, and a nonnegative signed byte
  at `Save+D4+u16(Save+1592)`. Preserve this recovered predicate without
  guessing a semantic identity for the byte.

Literal pointers were decoded from the original payload, and branch conditions
were checked against ARM disassembly, not only Ghidra's C output. The consumer
does not modify native positions, alpha, save flags, or gameplay to apply these
filters. No new smoothing or unconditional visibility override was introduced.

Regression tests cover nine owner-state transitions with deliberately stale
nonzero source alpha, sixteen scene/signed-byte boundary cases, and four
alternating hidden/visible stamina rows with six independent positions.
These are synthetic contract tests, **not mounted gameplay qualification**.
Profile and lifecycle tests pass on Windows and Linux; both developer runtimes
were relinked incrementally without rebuilding AOT. Installed packages are not
updated by these developer builds.

Still open: reproduce the actual Navi and riding artifacts in game, verify
native versus copied draw ordering and alpha ownership, and audit remaining
2.1.1 copy predicates. In particular, the mod's additional first-two-copy
suppression via payload state `005E3D88` is computed at the start of
`005D4A8C` from the previous `005E3D8C` state, touch mode 11, and native
alternate renderer `004FC648+40`. `005E3D8C` is later updated from the player
owner at `+12B8` or cleared on several paths. Its update timing must be
preserved before porting it; do not replace it with a guessed host flag.
`ReadTopScreenTouchDynamicState` currently
uses quad 34 as shared alpha: its equivalence to each original producer's alpha
must be qualified in these contexts, not assumed from an idle capture.

The first Windows framebuffer regression attempt stopped on disk exhaustion
on J:, not a rendering assertion. Its log is retained under
`J:/TriAevum-verify-20260910/hud-context-gates-x2`; incomplete generated images
were removed. The replacement run uses
`C:/Users/xander/triaevum-verify-20260911/hud-context-gates-x2`.
That replacement run completed with exit 0, 160 presentations and 40 consecutive
framebuffer captures (120..159). Frame 159 was inspected: gameplay and the
on-foot HUD are present. This is a regression run, not a reproduction of the
reported mounted/Navi defects.

## Mounted Reproduction: A/B and Map Marker

The user supplied an actual mounted checkpoint on September 11:
`C:/Users/xander/triaevum-verify-20260911/epona-user/epona-hud-reference.oot3dsav`.
It is private evidence, not a distributable fixture. It was copied from the
user's F5 save without editing guest state.

`epona-before/framebuffer_000124.bmp` reproduces native A/Down and B over the
right minimap, a red map marker still on the left, and six carrots split into
two groups. All paths below are relative to the private verification directory.

Root causes and corrections:

1. `ReadTopScreenQuestGeometryContext` incorrectly ORed the alternate gameplay
   HUD's `NativeQuestGate` into the open-page bypass. On horseback this skipped
   the *whole* native geometry relocation, including the left map marker and
   right A/B group. The original 2.1.1 `FUN_005C7F48` checks page predicates
   separately, then uses player `+12B8` to select the contextual geometry rule;
   it does not bypass relocation just because that owner is present. Keep the
   four real pause-page gates, but do not combine them with the alternate HUD
   flag. The existing special-state rule moves A and hides the redundant B.
2. `ApplyTopScreenHudScale` chose a left/right pivot independently per carrot.
   A row crossing X=200 therefore split at scales below 1. The original
   `005CE554` copies 28..33 use one center/bottom pivot: literals `005CF558=200`
   and `005CF55C=240`. The HorseStamina role now shares that horizontal pivot
   instead of inheriting independent edge margins.

`epona-context-fixed/framebuffer_000124.bmp` was captured from the **same**
checkpoint, same 2x profile, after 125 presentations (exit 0). It shows A/Down
in the upper command group, no duplicate B on the map, the red marker on the
right minimap, and a contiguous six-carrot row. No checkpoint repair was
needed. An initial hypothesis about missing saved projection tracking did
not explain these persistent artifacts: live diagnostics showed the native
map offset being refreshed before relocation. Do not treat that hypothesis
as an established cause or add a save migration for this fix.

Tests now cover mounted map/A/B relocation while the alternate HUD flag is
set, real page bypass retention, and a scaled stamina row crossing X=200.
Both profile and lifecycle tests pass on Windows and Linux; developer binaries
were relinked without AOT changes. Windows `epona-fix-onfoot-regression` also
completed 125 presentations, exit 0. Linux runtime was rebuilt, but the mounted
visual comparison here was performed on Windows.

At that checkpoint, mounted ammunition still had adjacent counters
near the upper command group (corrected below). Navi notification transitions and full mounted
input/alpha parity remain separate investigations; these captures do not close
those issues or prove complete TopScreen parity.

## Mounted Counter Destination and Restored Grass (September 11)

The two adjacent `50` labels came from copying the source counter's positions
along with its style. Original TopScreen 2.1.1 `FUN_005CD254` instead constructs
and scales the destination geometry (`005C7C78`, `005CC5F4`), then copies UVs,
colors and per-digit translation. `AppendTopScreenNativeCounters` now retains
the existing destination slot anchor/scale in the styled-copy path. The source
position stream is not needed. A regression test supplies deliberately different
source positions and checks both destination digit anchors and dimensions.

Two counts can legitimately remain: the mounted B bow and the bow assigned to
ZR share ammunition. They must appear under their respective commands, not
side by side under B. `epona-ammo-grass/framebuffer_000150.bmp` verifies that
layout in the real checkpoint; do not hide either count based on equal values.

That probe also isolated missing Grass: three eligible configured surfaces,
but zero ready masks and `texture masks unavailable`. The savestate texture
cache lazy upload returned before the ordinary decode observer, so GPU textures
were restored without repopulating `GrassTextureSourceCache`. The restored-upload
path in `GetOrCreateNativePicaTexture` now publishes native base-level pixels
before upload. Replacement images are never used as native masks: when restoring
a custom image, decode its native source base level for the observer. This work
runs once at lazy restoration, not on every cached draw. No save format changes,
scene-specific rules, or preset changes are involved.

Windows before/after probes used the same immutable checkpoint, profile and
180-presentation limit, with framebuffer captures at 120 and 150; both exited 0.
`epona-grass-restored` reports three masks, three placements and about 87,000
visible Grass elements in nine draws, versus zero before. Its frame 150 visibly
contains Grass. Cold placement construction still costs about eight seconds in
this diagnostic run; restoring masks does not claim to eliminate that separate
startup cost. These capture runs are not throughput measurements. Profile tests
pass; these latest two changes have not yet received Linux visual validation.
