# Title module PGO (Linux)

Profile-guided optimization of the per-title whole-AOT module (`triaevum_title_aot.so`). It is off by default. A release
build turns it on only with a profile collected for exactly the title source and compiler it builds with. Echoes of Time
builds are unchanged unless they set the option themselves.

Measured on the Linux box (Moonfall Curse, East Clock Town, unpaced, admission on; PERF/LINUX_r7 round 3):

| title module | fps | guest ms per presented frame |
|---|---|---|
| no PGO (option-2 tree) | 70.95 | 5.66 |
| PGO | 100.69 | 3.27 |

The no-PGO run was the first after a pipeline re-preparation. The profile used for the PGO run included East Clock Town.

## Build options (`tools/triaevum_release/linux_title`)

- `-DTRIAEVUM_TITLE_PGO=OFF` (default): no instrumentation, no profile.
- `-DTRIAEVUM_TITLE_PGO=GENERATE`: an instrumented module (`-fprofile-instr-generate`). The configure writes
  `triaevum_title_aot.pgo.json` beside the module: the SHA-256 of the title source's `TITLE_SOURCE_MANIFEST.json` and the
  compiler version. The compiler's profile runtime (`libclang_rt.profile`, the one matching that Clang) must be installed,
  or linking fails. The steamrt4 SDK does not ship it; supply the matching runtime of the same compiler release.
- `-DTRIAEVUM_TITLE_PGO=USE -DTRIAEVUM_TITLE_PGO_RECEIPT=<collection>/receipt.json`: optimizes with the merged
  `title.profdata` beside the receipt. The configure stops when:
  - the receipt is not a title profile collection;
  - it was collected from another title source (stale profile: collect again);
  - it was collected with another compiler version;
  - `title.profdata` differs from the SHA-256 in the receipt.

  Inside a matching profile, a function whose code changed since the collection (for example after a runtime header
  change) loses its counts. Clang matches each function by its control-flow hash and compiles that function as without PGO.
  A profile can therefore cost speed but never changes what the code does.

## Collecting a profile

1. Build the instrumented module (`GENERATE`) from the same translated title source and compiler as the release.
2. Run the training workloads with `tools/renderer/tev_program/collect_aot_profile.py`. Pass the product runtime, the
   instrumented module, and the installed launch profile as the invocation template. Each workload is a savestate, plus an
   input timeline if needed, run for `--frames` frames with `LLVM_PROFILE_FILE` set. The script merges the raw counters with
   `llvm-profdata merge -sparse` (`--llvm-root` is the LLVM prefix whose `bin` holds `llvm-profdata`) into `title.profdata`.
   It writes `receipt.json` with the module's build record, the per-run results and the profile's SHA-256.
3. Keep the collection directory (`receipt.json` + `title.profdata`) as a private release input, next to the title's
   shader corpus, named by the title source key. It is derived from game execution and is never committed or published.

Moonfall Curse training set (savestates of the MM3D test set; walking timeline: stick in four directions, 120 frames each):
North Clock Town day 1, South Clock Town noon, South Clock Town night, Stock Pot Inn (`sct_day1_idle`). Keep the scene
used to judge the result (East Clock Town) out of a release collection. The first box collection included it, and also a
boot run without a savestate, which `collect_aot_profile.py` does not support.

## Other platforms

The Linux receipt pins the Linux compiler version, so a Linux profile is never used by another build.

- **Windows** builds the module with clang-cl from the pinned LLVM 22.1.6 through `whole_aot_object_cache.py`. Its own PGO
  modes (`generate`, `generate-ir`, `use`) put the profile's content hash into the object-cache identity.
- **Android** builds with the NDK's clang for aarch64.

The `.profdata` format is backward-compatible (a newer `llvm-profdata` and Clang read an older indexed profile). The
counts are per-source function, so a Linux profile would partly apply. Target-conditional code, the Windows ABI and the
Android target change function bodies and hashes, though, and each compiler should profile what it compiles. Collect one
profile per platform with that platform's instrumented build: Windows on the Windows runtime, Android on a device.
