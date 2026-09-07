# Reviewed storage cleanup, 2026-09-07

User-authorized cleanup of obsolete project files on C: and I:. No gameplay,
renderer, presets, source, Git branches, or registered worktrees were changed.
Code baseline: project `899366255`, renderer `485dcc5c`.

## Recovered space

| Volume / operation | Accounted bytes | Approximate space |
| --- | ---: | ---: |
| I: obsolete generated files deleted | 18,411,890,590 | 17.15 GiB |
| I: diagnostic NTFS compression, content hash verified | 2,432,307,092 | 2.27 GiB |
| C: obsolete project residues deleted | 367,160,716 | 350.15 MiB |

Deletion covered 40,179 files on I: and 65 on C:. Compression verified 280
diagnostic files with SHA-256 before/after. Their paths and bytes remain usable
without extraction. Only files were compressed, not directory inheritance;
active binaries, build inputs, shader caches and saves were not compressed.
The larger compression candidate list was not fully executed: continuing the
low-yield per-file work was stopped to avoid prolonged disk contention.

I: free space went from 61,771,776 to 20,894,281,728 bytes. C: started full and
had about 250 MB free at the final check. Other running applications consumed
space during the operation; free-space deltas are not deletion accounting.
C: remains critically tight. Windows files, pagefile, other applications and
unrelated caches were deliberately not deleted.

## Removed

- Superseded `triaevum-{acquired-crt,assembled,private}-sysroot`,
  `triaevum-full-components`, `triaevum-forge-aot-object-cache`, and generated
  sysroot/components in `triaevum-toolchain-generation-proof`.
- Old `.obj`/`.lib` products in `oot3d-whole-aot-product-cache`; generated
  C++/IR, manifests, reports and build definitions remain.
- Executables, libraries and toolchain copies from 0.5.x release packages,
  `triaevum-release-artifacts` through `-r8`, and old source-validation builds.
  Those test installations are no longer runnable. Their source offers,
  attribution/licenses, manifests, logs, configurations and game data remain.
- Project-specific compiler crash reproducers, project crash dumps, the obsolete
  C: Forge `direct-aot` translation cache, a regenerable native-title profiler
  symbol index, and four July audit object files.

## Still required: do not delete by directory name

All paths below are under `I:/oot3dre_work/` unless stated otherwise:

- `triaevum-release`: active source worktree.
- `triaevum-direct-module-build`: current runtime, incremental build, symbols
  and before/after performance executables.
- `whole-aot-product-consumer/_deps`: **active** FetchContent dependencies.
- `triaevum-full-title-proof`: exact title generation/object archive, compiler
  and support library still required for incremental title work.
- `triaevum-fully-acquired-sysroot`: current usable SDK/sysroot.
- `native-game-costs-20260907-symbols-b`: latest diagnostic DLL, PDB/map and cache.
- `native_game/checkpoints`, recent performance evidence and original captures.
- `I:/TriAevum-0.6.0-candidate-r1`: current release, plugin/source offer, assets,
  savedata, presets and TopScreen data. `I:/TriAevumReleaseFinalData` also remains
  referenced by the global active-title configuration.
- Original ROMs, `code.bin`, extracted assets, decomp repositories and tools.
- C: installed SDKs/toolchains, TriAevum config/module cache, and
  `%APPDATA%/oot3d_native_vulkan`: the old-named shader cache is still active.

## Verification and audit

- Checked 14 required launch/build paths: none missing. Both active Git trees
  were clean before this report. CMake/Ninja dry-run found the build directory
  and scheduled regeneration; no compilation was run or claimed validated.
- Current runtime booted into the title intro with a private 1280x720,
  effects-off, non-interpolated test configuration: 120 presentations, exit 0,
  framebuffer captured and visually checked. This is not a performance result.
- The first test with the full saved preset failed with Vulkan
  `vmaAllocateMemory`, result `-2` (device memory allocation). That configuration
  is **not** certified by this cleanup. User settings were not changed; all
  test processes were closed.
- Detailed plans, deletion receipts, compression hashes, protected-path check
  and launch evidence: `J:/TriAevum-diagnostics/cleanup-20260907/`.
  `deleted-*.jsonl` records completed removals; `compressed-diagnostics.jsonl`
  records verified files. Plans are inventories, not proof of execution.

Removal used explicit resolved absolute paths and native PowerShell, with
protected paths, worktree, original/save-file and reparse-point checks. No
recursive cleanup was applied to a Git worktree or external decomp repository.
