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

The SDL sensor-axis conversion already matches Azahar. Two faults are downstream:
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

### Second Root Cause: Truncated VFP Double Transfers

The HID repair alone did **not** restore aiming. Paired gameplay probes received
raw gyro changes, but the native reader's three double-precision conversion
coefficients were zero, including after a cold boot. The service's sensitivity
coefficient and factory calibration replies already matched Azahar.

`whole_aot_cpp.py::_emit_vfp` recognized both scalar VLDR/VSTR encodings but
always emitted a 32-bit transfer. A double register uses **two** aliased S lanes.
For example, native gyro reset `0x004230C8` loads its double literal at
`0x00436714` and stores double coefficients at `0x004367A8`, `0x004367B0` and
`0x004367C8`. The generated code discarded the high word on every transfer.
The reference interpreter `upstream/recomp/a32_vfp_transport.cpp` was already
correct; the defect was specific to the optimized C++ emitter.

The emitter now distinguishes 32/64-bit memory width, preserves both lanes,
and rejects unmodelled D16-D31 and PC stores. No gyro-specific constants,
camera adaptation or original game data are changed. A full regeneration of
the same 12,419-function program changes only **6 of 256 shards**, correcting
135 generated loads and 39 stores. All other generated files remain identical.

Verification:

- Red/green emitter regressions cover all 16 double registers, signed offsets,
  literals, conditionals, invalid registers and unchanged odd single lanes.
- `test_whole_aot_vfp_memory_execution.py` compiles and runs the emitted C++:
  192 transfer variants, each with success, fault, skipped and skipped-fault
  cases; both words, adjacent lanes, guards and four-byte alignment are checked.
- 45 generator/optimization/true-AOT/executable tests pass on Windows.
- Incremental Linux title build recompiles only the six affected shards.
- Cold boot from a copied native save produces coefficients
  `1.0041044776119403` on all three axes instead of zero, derived by the native
  SDK from its calibration replies.
- Paired Hyrule Field bow-aiming runs use the same new checkpoint and inputs.
  Zero motion stays level; +30 degrees/second on X changes the actual camera
  and native SDK orientation; -30 on X moves it in the opposite direction,
  while +30 on Y produces horizontal aiming. Z-only rotation does not move
  this two-axis aim view in this posture. Before this emitter fix the matching positive
  and zero-input framebuffers were identical. Evidence:
  `issues-20260910-vfp64-{coldboot,field,aim-control,aim-gyro,aim-negative,aim-y}/`.

**Packaging:** this requires a regenerated/rebuilt precompiled title module as
well as the runtime HID fix. Updating the runtime alone cannot repair already
compiled transfers. The normal developer translator identity invalidates its
artifact cache; Forge must still activate a shipped module, never compile for
the user. Local verification uses a diagnostic manifest, not a published release.
Ordinary save files remain compatible. A savestate captured before the repair
can retain the old zero calibration in memory: qualify from cold boot/native
save, not from such an old checkpoint. Physical Switch Pro sensor transport
still needs confirmation on the reporter's device.

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
but a physical Xbox/Switch Pro qualification remains separate. A second product
run with `free_camera_enabled=true` also fires one arrow (50 -> 49) and enters
longshot aiming, with both actions visibly confirmed in framebuffer captures:
`issues-20260910-vfp64-free-camera-items/`. These runs exercise logical native
inputs, not a physical controller's remapped SDL bindings.

Diagnostics are opt-in under `--extended-diagnostics`: a bounded 128-record
item-query trace captures native caller/result/suppression and the resolved
input without reading guest state twice. Normal gameplay allocates no trace.
The input timeline now preserves explicit `zr`/`zl` when legacy boolean keys
are absent and accepts finite three-axis `gyroscope_dps`/`accelerometer_g`
vectors. These are probe repairs, not a change to physical controller mapping.
`probe_renderer.py` forwards an explicitly paired scenario/catalog and strips
inherited automation, retaining isolated saves/settings and bounded execution.

## Windows Product Qualification

The corrected runtime, title DLL and frozen Forge were rebuilt and exercised
on **Windows 11 Pro for Workstations 10.0.26200**, RTX 3060, driver
`32.0.16.1074`, NRI/Vulkan. This is actual native Windows execution, not an
inference from the Linux results. Build source: `c7f96869a9bf07c4dada763b067102d94f24d9bd`.
No additional product-code repair was needed for the five reported issue paths.

Private evidence root: `J:/TriAevum-verify-20260910/`. It contains:

- `runtime/`: fresh current-source Windows build, not a legacy worktree binary;
  CMake's `triaevum-runtime-targets-Release.json` records its source identity.
- `title-build.json`: regenerated 12,419-function title DLL build receipt.
- `package/`: allowlist-built, audited private candidate, subsequently installed
  by the actual frozen Forge GUI using the user's ROM. Not a published release.
- `win-*/`: input invocation, runtime counters, direct framebuffer BMPs and
  private checkpoints. Original persistent saves were copied for every probe.
- `verification.json`: assertions across **19 completed gameplay probes**,
  native SDK calibration/orientation, item counts and file-deletion results.
- `forge-install.json`, `forge-play-result.json`: real GUI installation and
  subsequent frozen-launcher/native-child outcomes.

### Results

| Path | Windows evidence |
| --- | --- |
| #5 / #17, native 30 FPS | Cold boot, native Items assignment of bow to ZR and longshot to ZL, native transition to Hyrule Field, actual bow shot **50 -> 49** arrows, longshot aiming laser visible. |
| #5 / #17, free camera | Same checkpoint and actions with `free_camera_enabled=true`: one arrow consumed and longshot aiming still works. |
| #20, native 30 FPS | All three SDK gains are `1.0041044776119403`; zero gyro leaves aim unchanged, positive and negative X produce opposite pitch, Y changes horizontal aim. Framebuffers and SDK state both change. |
| Default interpolated 2x | A separate cold-boot/checkpoint chain repeats assignment, transition, bow/longshot use and gyro aiming with the installed default graphics configuration. Simulation stays **30 Hz**, presentation is **60 Hz**, and real interpolated frame lists are submitted. Item-use probe: 560 presentations, 280 completed native visual frames, 278 interpolated frame lists, arrow **50 -> 49**. |
| #19 | Native Erase menu and confirmations finish with **File deleted**. `DeleteFile:wchar:/save03.bin` is handled; only that copied file disappears. `save00.bin` and `system.dat` retain their original SHA-256 hashes. |
| #12, installation | Real frozen Forge GUI imports `I:/Zelda3drecomp/oot3d.cci`, prepares TopScreen and enables Play in **23.58 seconds**. No compiler is invoked by the installation. |
| #12, launch | `TriAevumForge.exe --play` launches its installed runtime with `PATH=C:/Windows/System32;C:/Windows`. The title renders with TopScreen/2x enabled; a normal window-close message after a 25-second play interval returns **0** from both game and Forge, without forced termination. |

All 19 gameplay probes reached their requested frame counts with exit 0 and
**zero whole-AOT memory faults**. Native-fidelity tests disable interpolation;
the six 2x probes use new checkpoints created in that timing mode. The existing
loader explicitly rejects a non-interpolated checkpoint in interpolated mode;
that guard was not bypassed and checkpoint contents were not patched.

The same-frame positive/negative gyro checkpoint pitch values are
`-0.045241184532642365` / `+0.045241184532642365`; the zero-motion control is
zero. These are observations of the native SDK, not values supplied to the game.

Windows test suites also pass:

- Four C++ executables: `oot3d_native_a32_input_tests`,
  `oot3d_native_a32_ctr_host_tests`, `oot3d_top_screen_mod_profile_tests`,
  `triaevum_filesystem_service_tests`.
- 45 AOT generator/optimization/executable tests.
- 47 Forge/process/probe/shader-preparation tests: 45 passed, two
  platform-specific skips. Combined Python count: **90 passed, two skipped**.
- Frozen Forge real-widget smoke: all controls mapped, none clipped, Prepare
  correctly enabled/disabled according to ROM input.

### Build And Reproduction

The private scripts `build-windows.ps1`, `build-title.py`,
`package-candidate.py`, `run-probe.py`, `read-state.py`, `verify-results.py` and
`forge-play.ps1` retain the exact commands and assertions. `run-probe.py` uses
the repository's shared `tools/triaevum_release/probe_renderer.py`; it does not
replace input, filesystem or gameplay consumers. Use fresh output directories
for another qualification, never reuse mutable user saves.

Windows runtime build: Ninja, clang-cl 22.1.6, Release, three compile jobs,
current NRI/Vulkan desktop features, static SDL2 2.32.10 and explicit SDK paths.
Existing donor sources were reused, not legacy runtime object files. The
current title support library was rebuilt separately.

The title emitter still changes only six source shards for the VFP fix.
This Windows build nevertheless compiled **257 objects** (256 shards plus
registry), taking **593.65 seconds including generation/linking**: the available
September 5 object cache recorded bundled nlohmann headers under
`repo/forge/include/...`, whereas the current developer build records the same
files under the external include role. Toolchain and normalized dependency
content hashes match, but their cache identities differ. No identity checks
were disabled to force reuse. The fresh cache remains on J: for incremental
development; end-user Forge still installs the precompiled DLL.

SHA-256 of tested artifacts:

- Runtime: `29365168792bdf275e49c7985cdc98b9630daa2599dbab2c06261582977a89b8`.
- Title DLL: `42abdf0846be2f778e9432ce47c7cc21a5a3815dc6ec99057ec1a4c3a259018b`.
- Frozen Forge: `25d883dc1aa916f4bf0f0d57c375b7181981fc9c61a4e9402f587e5530be1273`.

### Limits And Separate Finding

This verifies logical shoulder/sensor inputs through the real Windows game,
not the reporter's physical Switch Pro/Xbox device, remapping or Windows
installation. #12 remains a demonstrated local loader-isolation fix, not proof
of the particular missing export on the reporter's machine. A reduced PATH
does not turn this development PC into a clean Windows VM.

The candidate's minimal release layout does not include the expanded private
shader seed/pipeline inventory. Initial playback consequently still compiles
unseeded shaders; these runs are **not** qualification of complete Forge shader
prewarming or performance benchmarks. Do not publish this diagnostic candidate
as a replacement for the ongoing cache/release work.

An apparent rectangular outline was investigated in the default graphics
profile during letterboxed weapon aiming.
Same checkpoint, input and 2x timing, changing only
`Graphics.Effects.Toon.OutlineEnabled=false`, removes that boundary.
Compare frame 360 in `win-item-use-x2/` and
`win-item-use-x2-outline-disabled/`. This additional A/B run exits 0; it is not
counted among the 19 issue probes. This prompted a separate renderer
investigation under the existing fidelity-extension architecture.
No outline masking or per-scene workaround was introduced here, and the user's
defaults were not changed to hide it.

September 12 disposition: **not an active regression**. The user does not
observe a problem in gameplay and explicitly requested that it be excluded.
Linux replay of the same checkpoint also shows the contour, but the existing
geometry-guide diagnostic (mode 4, frame 360) places it on actual wall/bridge
boundaries. Mode 7 shows it without mixed extension depth. The effective draw
trace at frame 359 finds only the initial raster clear as a non-perspective
guide writer; no observed NoOp guide draw explains an overlay. These results
do not establish an erroneous HUD/camera rectangle. No renderer correction was
made. Temporary diagnostic source edits were removed and the clean SDK runtime
rebuilt successfully. Private evidence: `/home/xander/triaevum-outline-parity/`
(`projection-trace`, `geometry-guide-4`, `diagnostic-7`). Each bounded replay
completed with exit 0; these are diagnostic runs, not performance benchmarks.
