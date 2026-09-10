# Issues 5, 12, 17, 19 and 20

2026-09-10. Scope: shared services, input and Forge; no renderer workarounds,
save-format changes or bundled game data. Private evidence is under
`I:/oot3dre_work/issue-fixes-20260910/` and Linux
`/home/xander/triaevum-pipeline-live-proof/issues-20260910-*`.

## #19: Erase Never Completes

`FS:DeleteFile` (request `0x08040142`) had no handler and entered the generic
service wait path. The native file-select worker never received its reply.
The CTR host now decodes the archive/path descriptor and replies synchronously
with the native response `0x08040040`, including failure results.

The shared filesystem ABI gains an additive `REMOVE_FILE` operation, routed
through the existing client/adapter/rooted backend and CTR bridge. It permits
only individual regular save files, rejects traversal, content, directories
and symlinks, and does not recursively remove anything. Existing operations
and save formats are unchanged.

Verification:

- `triaevum_filesystem_service_tests`: real service/client round trip, missing
  file, forbidden roots/paths and symlink protection.
- `oot3d_native_a32_ctr_host_tests`: actual IPC, invalid archive/descriptor,
  success and missing-file results; response resumes instead of waiting.
- Linux NRI product, copied native completion save, native Erase menu and both
  confirmations: framebuffer says **File deleted**. The captured service event
  is `SendSyncRequest:fs:USER:DeleteFile:wchar:/save03.bin`; that private file
  is removed. `save00.bin` and `system.dat` retain their original SHA-256 hashes.
  This run erased the selected Master Quest file, not a fabricated slot state.

Sources: [FS IPC](https://www.3dbrew.org/wiki/FS:DeleteFile),
[Azahar FS handler](https://github.com/azahar-emu/azahar/blob/master/src/core/hle/service/fs/fs_user.cpp).

## #20: Gyroscope Seen By Calibration, Not By Game

The SDL sensor-axis conversion already matches Azahar. The fault is downstream:
the gyro shared-memory section started at `0x154`, four bytes before its native
`0x158` location. The index, timestamps, raw vector and ring were all shifted;
timestamp writes also overlapped the last accelerometer record. Host calibration
does not read this shared memory, so could appear correct.

The producer now derives the gyro section from the end of all eight 6-byte
accelerometer records: index `0x168`, raw `0x170`, samples `0x178`.
No axis swaps, sensitivity multipliers or controller-specific exceptions.

The independent consumer regression uses the locations in
[libctru hidScanInput](https://github.com/devkitPro/libctru/blob/master/libctru/source/services/hid.c)
(word 86 section, word 94 samples), agreeing with
[Azahar SharedMem](https://github.com/azahar-emu/azahar/blob/master/src/core/hle/service/hid/hid.h).
It fails against the old producer and passes after correction, including all
32 gyro entries and all 8 accel entries after wrap. Physical Switch Pro aiming
still requires user/device confirmation. The #5 attachment additionally selects
`controller_accelerometer`, which intentionally supplies no angular velocity;
this is distinct from the gyro memory-layout defect.

## #12: Runtime Preflight Loader Error

The screenshot's decimal `3221225785` is **0xC0000139**, missing DLL export,
not a bad ROM or unsupported CPU instruction. The screenshot alone does not
identify the DLL or Windows version.

Forge sanitized Linux's loader environment but not Windows' inherited
`SetDllDirectoryW` from PyInstaller. `native_process.py` now owns native-child
launches: reset that directory only around process creation, restore Forge's
directory even on failure, and exclude only frozen-bundle PATH entries.
Runtime preflight, gameplay launch, portable shader preparation, GPU pipeline
preparation and portal helpers use the same boundary. Frozen Forge workers
keep their own Python environment. The loader diagnostic names known NTSTATUS
failures and asks for the Windows version instead of suggesting another ROM.

Verification: 43 focused Forge/process/preparation tests, 2 platform skips,
including real Windows DLL-search reset/restore, child execution, failure,
timeout cleanup, PATH boundaries and signed/unsigned 0xC0000139 reporting.
This closes a demonstrated loader-isolation gap, but does not prove the cause
on the reporter's machine; request its Windows version/dependency details if
a rebuilt complete package still fails.

Source: [PyInstaller external-program guidance](https://pyinstaller.org/en/stable/common-issues-and-pitfalls.html#launching-external-programs-from-the-frozen-application).

## #5 / #17: Shoulder Item Use

The native-update cadence and real compiled assignment boundary were repaired
in `3ebef38`, following `7617c8a`. The follow-up now verifies actual item use,
not just HUD assignment or a query returning true:

- Linux NRI/Vulkan, copied original completed save, native Items menu: bow
  assigned to ZR and longshot to ZL.
- The existing structural scenario loader invokes the native transition to
  Hyrule Field (`spot00_info_entry_00cd`). It does not patch inventory, actor
  actions or suppression. Start from an established gameplay checkpoint;
  requesting this scenario during file-select boot can target an earlier state.
- Hold/release ZR: Link uses the bow and the native arrow count decreases
  **50 -> 49**. Hold ZL: Link holds the longshot with its aiming laser visible.
- Evidence: `issues-20260910-field-use/`, framebuffers 120 and 180, runtime
  counters and checkpoint. Native item getter calls come from the actual Link
  action selector, with one ZR and one ZL pressed update.

The Temple of Time legitimately suppresses ordinary weapons. That explained
the earlier negative use probe; its rules were not bypassed. All 24 shoulder
mapping permutations have unit coverage (288 held/pressed/released checks),
but a physical Xbox/Switch Pro qualification remains separate. The current
product run uses logical native input and free camera disabled; it does not
alone close the free-camera-specific variant of #17.

Diagnostics are opt-in under `--extended-diagnostics`: a bounded 128-record
item-query trace captures native caller/result/suppression and the resolved
input without reading guest state twice. Normal gameplay allocates no trace.
The input timeline now preserves explicit `zr`/`zl` when legacy boolean keys
are absent and accepts finite three-axis `gyroscope_dps`/`accelerometer_g`
vectors. These are probe repairs, not a change to physical controller mapping.
`probe_renderer.py` forwards an explicitly paired scenario/catalog and strips
inherited automation, retaining isolated saves/settings and bounded execution.
