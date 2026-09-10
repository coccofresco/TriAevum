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
