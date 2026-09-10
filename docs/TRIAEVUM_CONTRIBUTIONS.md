# Community Contributions

External contributions retain their authorship even when integrated selectively,
rebased, or substantially adapted. This record complements Git history and
[third-party notices](../THIRD_PARTY_NOTICES.md); it does not replace licenses.

## PR #6: Linux x64 Infrastructure

- Contributor: [999sian](https://github.com/999sian).
- Pull request: [Linux x64 port of runtime loader, Forge and packaging](https://github.com/coccofresco/TriAevum/pull/6).
- Original commit: [`505d7b8c974a2eadd5a362933a5ffdc19bd28214`](https://github.com/999sian/TriAevum/commit/505d7b8c974a2eadd5a362933a5ffdc19bd28214).
- Integration: selective adaptation on `port/linux-nri`, not a wholesale merge.
- First integration commit: [`f1c05a8`](https://github.com/coccofresco/TriAevum/commit/f1c05a8).

Retained contribution: Linux object-cache/plugin build support, ELF visibility
and link policy, the POSIX synthetic ABI probe, the actual title-loader test,
compiler portability changes, and the fixed-width C++ module status enum.
The target-aware precompiled-title promotion is also adapted into the existing
common publisher, as is portable installation migration, without introducing
a second Linux release policy.
Relevant owners are `tools/triaevum_release/whole_aot_*`,
`validate_whole_aot_toolchain.py`, `toolchain_probe.py`,
`tools/oot3d/native_game_runtime/oot3d_native_direct_aot_tests.cpp`, and
`runtime/triaevum_module/include/triaevum/module_abi.h`.

Integration changes by TriAevum: use the existing `release_platform.py` registry;
explicitly retain C++ driver mode after resolving compiler symlinks; reject
unresolved ELF link symbols; propagate the target/profile to object builds;
isolate loader CI from graphics/UI dependencies; test empty-title rejection.
The existing public UI contract, Linux loader, portable Forge bundle, Steam SDK
work and common platform release policy are preserved instead of replaced.
The existing Linux shaderc-name correction is also retained.

The contribution's infrastructure tests do not by themselves establish native
game boot, feature parity or Steam Deck performance. Those remain separate
release qualification records. See [platform architecture](TRIAEVUM_PLATFORM_RELEASE_ARCHITECTURE.md).

## PR #14: Display Failure Feedback

- Contributor: [999sian](https://github.com/999sian), Git author `sian <sian@localhost>`.
- Pull request: [Show why a display change was reverted](https://github.com/coccofresco/TriAevum/pull/14).
- Original commit: `a359f7063a512e7626eed4e5d849dd46aab8fc79`.
- Integration commit: `dd7539e`.
- Integration: selective adaptation on `port/linux-nri`; the integration commit
  records the PR URL, original SHA and a `Co-authored-by` trailer.

Retained contribution: forward the backend failure reason to the graphics
settings runtime and expose it in the shared F1 display status, outside tabs.
TriAevum adaptation: ignore obsolete/idle failures; preserve the message through
recovery; clear it only when a new candidate actually applies. Actual-widget
tests cover the full lifecycle. No renderer feature or performance improvement
is attributed to this UI fix. Validation and the other pending community PRs
are recorded in [the 2026-09-10 review](TRIAEVUM_PR_REVIEW_20260910.md).

## PR #13: Product TopScreen Item Hooks

- Contributor: [999sian](https://github.com/999sian), Git author `sian <sian@localhost>`.
- Pull request: [Fix TopScreen ZL/ZR item slots](https://github.com/coccofresco/TriAevum/pull/13).
- Original head: `800345379958fc7d9c03555e8ecb28c1f8e44dea`.
- Integration: selective, with original SHA/PR and coauthor recorded in Git.

Retained contribution: product-mode observable item getter/slot and assignment
call/return hooks, eligibility-based exits and handled-or-resume-once routing.
TriAevum adaptation: move the pre-existing item execution into a separate tested
adapter; use one registry for observation and dispatch; validate writes before
partial assignment changes; add repeatable gameplay probing and diagnostics.
The PR's six-refresh input latch is not adopted. The follow-up now fixes native
update cadence and moves the assignment hook from an unobservable instruction
to the real compiled function entry. Paired refresh phases produce identical
counters and two completed assignments; framebuffers confirm the assigned
objects. Item-use and device coverage are not implied by those results.
See [scope and results](TRIAEVUM_TOPSCREEN_REFRESH_AND_OCARINA.md).

## PR #18: Ocarina Evidence And Selective Adaptation

Contributor: [999sian](https://github.com/999sian), Git author `sian <sian@localhost>`.
[PR #18](https://github.com/coccofresco/TriAevum/pull/18), head
`3655e285f4c53733b9e486a9434c073dba2d5954`, contributed the report and live
investigation of missing ocarina UI. Its ocarina commits `e3eca08`/`3655e28`
are reviewed separately from the stacked branch.

The song-guide and D-pad ownership intent is now adapted in a dedicated title
UI module, verified against original-mod producers and in-game framebuffers.
Texture-size heuristics, raw replay copying and synthetic tile browsing were
not adopted. This is a replacement implementation informed by the PR's
evidence, not a wholesale merge or attribution of the new module to its author.
Localized song text is subsequently integrated in `cb21d7f`; performed-note
feedback remains separate. See
[scope, evidence and verification](TRIAEVUM_TOPSCREEN_OCARINA_OWNER.md).

## PR #11: Outline Pipeline Cache Coverage

Contributor: [999sian](https://github.com/999sian), Git author `sian <sian@localhost>`.
[PR #11](https://github.com/coccofresco/TriAevum/pull/11), head
`28fa5fdd60dc79c9145dc299f4440a90a8f1d026`.

Selectively adopted: outline-occlusion manifest flag, backward-compatible
identity and live recording/prewarm propagation. The current integration also
forwards the flag through Forge's shared NRI preparation, validates the declared
pass domain/attachment, tests the complete contract and fixes the contributor's
reported Linux Clang GoogleTest option issue. The PR's local store, queue and
thread behavior are not attributed as merged. See
[scope and verification](TRIAEVUM_PR_COMPAT_PERFORMANCE_20260910.md).

The next [persistent-cache integration](TRIAEVUM_PERSISTENT_SPIRV_CACHE.md)
addresses the same PR's reuse objective by replacing the existing generic
SPIR-V store with a shared, bounded, compiler-validated atomic implementation.
This is newly implemented storage informed by the PR review, not attribution
of its unmerged local-pack/queue code as part of the runtime.

## PR #16: Full-Drawable Readback

Contributor: [Pablo Souza](https://github.com/PabloVSouza), Git author
`Pablo Souza <30412188+PabloVSouza@users.noreply.github.com>`.
[PR #16](https://github.com/coccofresco/TriAevum/pull/16), head
`49fbfcd7149526d1fc0474055279af9a556b77e6`.

Selectively adopted: capture the entire drawable and resample to the requested
output extent. TriAevum adaptation extracts a backend-independent tested helper
with bounds validation, RGBA/BGRA handling and unchanged equal-size conversion.
The broader Mac launcher, package, pacing and Grass changes are not merged;
this is not a macOS release or a claim of platform parity. The integration
commit preserves both PR references and original-author coauthor trailers.

## Integration Rules

1. Preserve original author metadata when merging or cherry-picking unchanged
   commits. For squash/selective integration, use accurate `Co-authored-by`
   trailers with the contributor's existing public Git identity.
2. Record the PR URL, original commit, adopted scope and significant integration
   changes. The integration commit references the PR and original SHA so history
   remains traceable without inventing authorship for unrelated work.
3. Keep source copyright/license headers and required donor notices. Do not
   describe external code as exclusively maintainer-written or AI-generated.
4. Include this record in release documentation and corresponding source.
5. Credit partial contributions accurately; do not close a PR as fully merged
   when substantial proposed work was intentionally not integrated.
