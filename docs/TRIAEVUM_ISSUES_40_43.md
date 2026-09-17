# Issues 40-43: implementation and verification

## Workspace interruption (2026-09-17)

The user confirmed that another instance was restoring this worktree and has
stopped it. Recovery restores #40/#42/#43 from the recorded patches and source
evidence, including regression tests. #41 remains investigation only: neither
the rim experiment nor the unverified classifier change is reinstated.
Fresh validation is required; earlier mixed-worktree build results do not count.

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

## #43: boot hotkey repeat

The mapped D-pad consumer already uses pressed edges, but legacy item-slot
compatibility also turned held D-pad directions into native item presses.
`GameplayDpadActionsOwned` now suppresses that legacy path only when the
mapped consumer owns the input. Ocarina D-pad and ZR/ZL semantics remain.

Before recovery, dispatch regression tests passed. A 100-frame held-left gameplay run recorded
one equipment change and one player refresh. The exact underwater scenario
has not yet been replayed.

## #41: advanced toon rim on black backgrounds

User reports black with effects bypassed and a light gradient with the advanced
pipeline active, both here and during loading. The private checkpoint is saved.
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

## Private evidence (not distributed)

`C:/Users/xander/triaevum-issues-40-43/` contains bounded run arguments,
framebuffer captures and runtime diagnostics. `sages-user/issue41.oot3dsav`
is the user-created reproduction. `sages-user-{off,toon,no-rim}` isolate the
effect. `mounted-refresh` verifies the mounted counter, `boots-held` covers
the sustained input. ROM data and these private fixtures stay outside Git.
