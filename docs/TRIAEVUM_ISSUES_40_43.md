# Issues 40-43: implementation and verification

## Workspace interruption (2026-09-17)

The user confirmed that another instance was restoring this worktree and has
stopped it. Recovery restores #40/#42/#43 from the recorded patches and source
evidence, including regression tests. #41 remains investigation only: neither
the rim experiment nor the unverified classifier change is reinstated.
Fresh validation is required; earlier mixed-worktree build results do not count.

Recovered implementations are committed in `3f84692`. The fresh replay and
test results below supersede the pre-recovery validation where stated.

At the end of the investigation, `git status` showed ALL tracked changes
removed without HEAD moving from a946f34, including unrelated pre-existing
AOT changes. The issue implementations below were subsequently recovered
for #40/#42/#43; #41 remains an unimplemented hypothesis. No command to
discard all tracked changes was intentionally issued by this session.
Build results spanning this change cannot establish which experimental code
was executed. Preserve this distinction; do not close any issue on this basis.
The user checkpoint and private captures remain available. Resolve concurrent
workspace mutation, or use an isolated worktree, before resuming changes.

## Menu baseline

Commit `a946f34` restores the consolidated ImGui F1 menu. The deferred
F1/F12 split is preserved on `feature/deferred-f1-f12-menus` at `a144833`.
Actual-widget smoke: 3896 assertions passed. Unrelated AOT experiments are
not part of this change set.

## #40: horse stamina and timer overlap

The TopScreen 2.1.1 Quest geometry tail, `005C86F8..005C8850`, relocates
both clock quad 5 and the digit renderer bound at `004FC674`. For lower
quads (center Y > 205), X decreases by 9 and Y by
`18 + (HudScale - 0.6) * 30`. Upper timer positions remain native.

Implemented in `RelocateTopScreenTimerQuad`, `ApplyTopScreenTimerCounterLayout`
and the Quest materialized hook. Tests cover scales 0.6/0.8/1.0, consistent
digit/clock displacement and the upper-position boundary.
Live race-timer verification remains pending; a manually seeded timer did
not become visible and is not evidence of a successful in-game fix.
This earlier gap was subsequently covered by the user-created race checkpoint
and replay documented below; the synthetic timer attempt remains invalid evidence.
The added guest-memory fixture verifies the real counter/renderer indirection,
two active digit quads out of three allocated quads, preserved Z, empty
counters, absent bindings and rejection of invalid counts.

## #42: mounted B counter

The original draw guard at `005E3D1C` is set by `005C8918` and cleared by
the update at `005D4A8C` (literal references `005C9050` and `005D4F0C`).
Our `QuestDrawModelAdjusted` flag previously stayed set across updates.
Rearm it at the native Quest submit/refresh boundary, not host presentation
frames. Repeated draws within the same refresh remain protected.

Tests cover the refresh boundary and rebuilt counter geometry. Mounted
framebuffer replay before recovery placed the native B ammunition beneath B at the
intended scale; the separate ZR count remains legitimate and visible.
Live diagnostics: 360 Quest hooks, zero failures, 240 transformations.
The recovered executable reproduced these counts in `recovery-mounted`
(240 presentation frames). Framebuffer 120 confirms separated B and ZR
ammunition counts in the mounted scene.

## #43: boot hotkey repeat

The mapped D-pad consumer already uses pressed edges, but legacy item-slot
compatibility also turned held D-pad directions into native item presses.
`GameplayDpadActionsOwned` now suppresses that legacy path only when the
mapped consumer owns the input. Ocarina D-pad and ZR/ZL semantics remain.

Before recovery, dispatch regression tests passed. A 100-frame held-left gameplay run recorded
one equipment change and one player refresh. The exact underwater scenario
has not yet been replayed.
Fresh `recovery-boots` replay (220 frames, including sustained input) also
records exactly one equipment change and one player refresh, with zero
Quest hook failures.

## #41: advanced toon rim on black backgrounds

User reports black with effects bypassed and a light gradient with the advanced
pipeline active, both here and during loading. The private checkpoint is saved.
The user explicitly reconfirmed that the initial black loading screen with the
Triforce exhibits the same #41 defect. Keep cold boot as a primary reproduction;
the isolated Off/toon probes below do not invalidate that report or cover the
full advanced pipeline.
IMPORTANT: the initial automated A/B runs were INVALID: Preset=Authentic
normalized the requested Toon mode back to Off. Their framebuffer SHA256 is
identical (820B5E504ECDDBFD33B7FC714A8B8CA98702E449F0A5FE20DECF0CCAE114C4DA).
Visual assessments claiming those captures differed were incorrect.
The probe now selects Custom and diagnostics confirm toon_mode=2 and eight
instrumented programs. The background sample (100,360) is still RGB 66/66/66
both with Off and Toon. This does not establish the user's reported cause.

Candidate boundary correction: the toon classifier lacked depth comparison and treated
raster canvas initialization as a lit surface. The existing outline contract
already excludes `DepthCompare::Always` from geometric surface guides.
Pass that native state into `PicaToonDrawInfo` and apply the same boundary to
toon eligibility. The pipeline cache already includes depth comparison in
its key. Normal depth-tested surfaces remain eligible.

The initial post-TEV rim-modulation experiment did not fix the replay and was
removed, together with its generated binaries. The classifier experiment was
also lost in the reset and is intentionally not restored without evidence.
No scene IDs, asset names, color thresholds or black-pixel masking are used.

This has NOT resolved or validated issue #41. A controlled live F2 comparison
is still required. The lost classifier test covered both toon modes and
restoring an ordinary depth comparison. The separate existing grass topology
GPU test reports Vulkan error -13; the full GPU suite is therefore not green.

### Fresh isolation after recovery

- `recovered-sages-on-native` and `recovered-sages-off-native` each completed
  150 frames using the checkpoint's actual `native30_no_interpolation` timing.
  The background remains RGB 66/66/66 in both. Diagnostics confirm eight
  instrumented programs with toon and zero with Off; the preset is Custom.
- Read-only inspection of `sages-user/issue41.oot3dsav` finds black in the
  stored top color target and both stored top display images. The gray is
  therefore not simply a gray image baked into the checkpoint. Trace current
  native draws, restored state and scanout/composition before blaming toon.
- `recovery-boot-off` and `recovery-boot-on` each completed 35 cold-boot frames.
  Framebuffer 9 shows the loading triforce on black in both. This does not
  validate every advanced-effect combination reported by the user.
- The `DepthCompare::Always` toon exclusion was temporarily rebuilt and tested,
  but did not fix the Sages replay. It was removed rather than shipped as a
  speculative scene repair. Always comparison alone is not proof of a canvas.
- The shader pipeline regression test now checks the existing uniform-backed
  toon contract instead of expecting obsolete per-style literal shader source.
  Style updates must preserve shader source/key, while raster eligibility
  changes must still be reflected in cached variants.

Next verification boundaries: live Lon Lon race for #40; compare restored
native target output against final scanout for #41. Neither issue is closed.
The #40 live boundary was subsequently verified below; #41 remains unresolved.
Final regression run after removing the classifier experiment: shader pipeline
suite 40/40 passed; TopScreen profile, item, ocarina and mapped D-pad tests
passed. The consolidated F1 widget smoke remains at 3896 passing assertions.

## User race checkpoint verification (2026-09-17)

The user started the actual Lon Lon Ranch obstacle race and saved it using F5.
Private checkpoint: `C:/Users/xander/triaevum-issues-40-43/ranch-user/savedata/quick.oot3dsav`.
Replay: `race-user-verification`, 150 frames, `native30_no_interpolation`,
TopScreen HUD scale 0.8, current executable after commit `71a654e`.
Framebuffer captures at frames 30 and 90 show the clock and timer reading
00:01 and 00:03 above the six stamina carrots, with no overlap. Both digit and
clock positioning stay stable while the timer advances. Diagnostics report
450 Quest hook calls, 300 transformations and zero failures; process exit 0.
This validates the #40 correction in the actual race at the user's HUD scale.
Other scales retain unit coverage, not equivalent live race coverage. No
timer injection, gameplay patch or scene-specific rendering exception was used.

## Verified loading-background correction (2026-09-17)

The earlier cold-boot comparison used PicaMaterial, not PostProcessPreview.
Repeating it with PostProcessPreview reproduces the reported full-screen
brown/gray gradient behind the loading triforce. Do not generalize the earlier
negative result to all toon modes.

The shared fragment instrumentation now receives the native projection class.
An orthographic, destination-replacing color pass with depth test/write enabled,
Always comparison and ordinary fragment operations is not a toon lighting
surface. It retains the canonical fragment shader. This is deliberately narrower
than the rejected Always-only exclusion; perspective draws remain eligible.
The shader pipeline key already contains projection and depth comparison.

Verification with the rebuilt executable:

- Shader pipeline tests: 40/40 passed, including both toon modes, canonical
  source/key preservation for canvas initialization and perspective eligibility.
- Cold boot, frame 9: before the fix, 5,934 of 5,940 background samples are
  nonblack; after the fix, all 5,940 are RGB 0/0/0, matching the Off reference.
  The loading triforce remains visible. Sampling excludes its screen region.
- Private evidence: `issue41-preview-boot` and `issue41-fixed-preview-boot`.
- Sages checkpoint replay: `issue41-fixed-preview-sages`, 150 frames, exit 0.
  The captured gray rectangle also occurs in the earlier Off replay. It was
  initially classified as an unresolved defect; the subsequent live user
  verification below supersedes that interpretation. Native initialization is
  not suppressed by the TopScreen frontend backdrop rule.

No color thresholds, scene IDs, texture names or black-pixel masking are used.

## Issue #41 live user acceptance (2026-09-17)

Fix: `ebd6ba6` (native raster initialization excluded from toon lighting).
The user replayed `sages-user/issue41.oot3dsav` and confirmed that the reported
background defect no longer appears. The user identified the subsequent native
fade to white as the likely source of the gray-frame misinterpretation.
The same checkpoint was then relaunched explicitly with PostProcessPreview
toon enabled, preserving the remaining session settings. The user confirmed
correct behavior again. Logs: `sages-user/toon-user-{stdout,stderr}.log`.

Issue #41 is resolved within the verified Windows Vulkan configuration, together
with the deterministic loading-background check above. No extra correction to
the native fade is warranted. This does not claim a new Linux verification or
an exhaustive test of every optional-effect combination. Earlier unresolved
status statements in this chronological investigation are superseded here.

## Private evidence (not distributed)

`C:/Users/xander/triaevum-issues-40-43/` contains bounded run arguments,
framebuffer captures and runtime diagnostics. `sages-user/issue41.oot3dsav`
is the user-created reproduction. `sages-user-{off,toon,no-rim}` isolate the
effect. `mounted-refresh` verifies the mounted counter, `boots-held` covers
the sustained input. ROM data and these private fixtures stay outside Git.
