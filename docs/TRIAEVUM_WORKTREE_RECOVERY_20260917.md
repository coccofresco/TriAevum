# Worktree recovery and cleanup

## Protected working set

- Repository: `I:/TriAevum-public/source-repository-clean`.
- Active host build: `J:/TriAevum-verify-20260910/runtime`.
- Runtime outputs: `I:/TriAevum-public/ui-dependencies`.
- Compiler: `I:/oot3dre_tools/llvm-22.1.6`.
- Shared dependencies: `I:/oot3dre_work/whole-aot-product-consumer/_deps`.
- Working title module and provenance: `I:/TriAevum-aot-lab-evidence/baseline`.
- ROM-derived runtime data: `I:/oot3dre_work/triaevum-full-title-proof`.
- Issue reproduction, including the user's checkpoint:
  `C:/Users/xander/triaevum-issues-40-43`.
- Mounted checkpoint: `C:/Users/xander/triaevum-verify-20260911/epona-user`.
- External decompilation, ROMs, tools, releases and source worktrees are preserved.

## Cleanup status

The recursive deletion request was rejected by the tool security policy before
execution. No space recovery is claimed. At inventory time I: had only about
37 MiB free, C: 4.4 GiB and J: 2.2 GiB.

The following explicit directories contain regenerable diagnostic object caches,
not source, saves or the active runtime. Their plugin DLL/map siblings are kept:

- `I:/TriAevum-aot-lab-debug/native-objects` (the whole debug directory is 1.64 GiB;
  its obsolete archive alone is 644,214,700 bytes).
- `I:/TriAevum-aot-lab-o3/native-objects`.

Older `*.bmp` captures under `C:/Users/xander/triaevum-verify-20260911`
total 204 files / approximately 0.52 GiB. Do not delete that entire directory:
it also contains useful saves, presets and diagnostic reports. Keep reference
captures supporting still-open issues. Older test captures under
`J:/TriAevum-verify-20260910` are also candidates; the active runtime, inputs,
checkpoints and shader preparation data are not.

## Recovery policy

The user confirmed and stopped another instance that reset tracked changes
without moving HEAD. Reconstruct only evidenced changes. Do not guess lost AOT
emitter semantics or promote abandoned renderer experiments as fixes.

- Consolidated F1 is already safe in `a946f34`; deferred F1/F12 remains on
  `feature/deferred-f1-f12-menus`.
- Restore the TopScreen #40/#42/#43 changes and their tests; track #41 separately
  in `TRIAEVUM_ISSUES_40_43.md`.
- The untracked AOT region helper, recognizer, differential harness, pilot
  builder, summaries and documentation survived. Keep them. Tracked generator
  and cache integration changes were lost; do not report that pilot as restored
  until differential verification passes again. Default AOT remains unchanged.
- The old AOT region comparison did not establish a performance improvement.
  Preserve its negative/incomplete result rather than enabling it in the game.

Use separate worktrees for concurrent code changes, with private build/output
directories per worktree. Commit coherent verified changes before another
instance can reset or replace the shared checkout.

## Recovery validation

The selected TopScreen/profile and UI lifecycle test build was interrupted
after seven of 37 compilation steps, with approximately 37 MiB free on I:.
No fresh executable test result is claimed. This is a source recovery checkpoint,
not a verified issue-resolution commit. Rerun both test targets and their
executables after freeing space, then reproduce the affected gameplay states.
The surviving untracked AOT files remain in place, untouched by this checkpoint.
