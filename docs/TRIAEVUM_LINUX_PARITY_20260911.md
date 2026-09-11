# Linux Alignment: September 10-11

Source baseline: `94cb1c1`, branch `port/linux-nri`. Physical CachyOS/KDE
Wayland, NVIDIA RTX 4060. This is development qualification, not an Android,
actual Steam Deck or installed Flatpak qualification.

## Coverage

| Changes | Linux results |
| --- | --- |
| Controls/capture/motion: d941550, 79d05c1, 23e44c9 | Shared 3DS and native input tests pass; real F1 binding widgets pass. Physical controller/mouse ergonomics not automated. |
| Display: 421ec8e | 400-frame live resize sequence: scales 1.01/0.73, 720p, 4:3, 4K, borderless, confirmation, return to windowed. Telemetry confirms all three output extents. Exit 0. |
| Language: ac77f6c | Native detector, actual Forge combo and F1 persistence pass. Cold boot reaches Italian native file/name menu on the sky. |
| Shader preparation: fa86170 | Updated compiler: 22 modules compiled cold in 3.074 s; repeat has 22 hits, zero compilations/failures. Release/cache contracts pass. Not all-game coverage. |
| TopScreen: 131076d, 3c937dd, a9b9506, a52fcf4 | Contract and native text lifecycle tests pass. Live partial-progress replay shows the repaired unknown marker and completes learned/unknown browsing. Restricted-item opacity remains contract-tested, not exhaustive gameplay coverage. |
| Learning: 94cb1c1 | Windows checkpoint loads on Linux. Native L,R,A,L,R,A repetition succeeds. Quest 109DD620 -> 109FD620; gate event 0600 -> 0E00; completion event 8000 -> 8020. All five learning checks pass. |
| Earlier issue fixes, including c7f9686 | Retain current Linux title module, previously rebuilt with VFP repair. No whole-AOT generation or title rebuild during this alignment. |

Vulkan validation layers are absent on the host. The explicit validation probe
correctly refused to start; a separate functional display probe ran without
requesting layers. Do not call that zero Vulkan validation errors.

## Builds

- Backup of previous mirror including local changes:
  `/home/xander/triaevum-linux-before-94cb1c1.tar.gz`. Its old Git HEAD was not
  its source identity. Archive files were compared byte-for-byte and only
  changed files replaced, retaining incremental build outputs.
- Host: `/home/xander/triaevum-linux-build/TriAevum`, native Clang, three jobs.
- Steam Runtime SDK: `/home/xander/triaevum-steamrt4-build/runtime/TriAevum`.
  Runtime and game module require at most GLIBC 2.38. SDK dependency/ABI proof
  passes and rejects the empty title stub. The SDK runtime also completes the
  Wayland partial-progress replay, using the current host title module.
- Five native test programs pass: shared input, native input, TopScreen
  profile/actions/ocarina, native text lifecycle, actual F1 widgets.
  F1: 3,819 assertions including repeated UI-frame invariants.
- Forge/release: 346 tests, 11 skips, no failures; native detector and Tk
  widget enabled. Three new bundle tests also pass on Windows.

Developer Python: `/home/xander/triaevum-pr6-probe-build/venv`, with repository
root in PYTHONPATH. System Python initially lacked capstone/msgpack and Tk
discovery. Use the native Tk library directory, not an old frozen Forge's
entire `_internal` on LD_LIBRARY_PATH: the latter injects incompatible
libcrypto into system Python. No system upgrade or sudo was required.

## Forge Repair

PyInstaller found external Tk binaries but omitted `_tk_data`; the resulting
binary failed even `doctor --inventory`. The builder now honors explicit
publisher `TK_LIBRARY` / `TCL_LIBRARY`, validates their marker scripts before
building, and bundles data at the runtime hook's expected locations. Without
overrides, standard discovery is unchanged.

Host frozen Forge passes inventory and actual GUI smoke: language combo
present, no clipped/unmapped widgets, prepare button tracks input. Output:
`/home/xander/triaevum-linux-parity-20260911/forge-dist/TriAevumForge/`.
It uses host Python/GLIBC and is **not a portable Flatpak artifact**. The Steam
SDK lacks Python ensurepip; portable Forge provisioning/build remains pending.
Do not substitute the host Forge in the existing installed Flatpak.

## Runtime Evidence

Private root: `/home/xander/triaevum-linux-parity-20260911/`. `run.py`, isolated
invocations/configs, logs, GPU telemetry and runtime framebuffer captures.
No game data, saves or captures are committed.

| Probe | Frames | Result |
| --- | ---: | --- |
| storms | 1100 | First-learning completion, checkpoint 1050, exit 0. |
| partial | 540 | Learned/unknown guide replay, exit 0. |
| italian | 900 | Native Italian frontend, exit 0. |
| display-functional | 400 | Resize/mode/scale sequence, exit 0. |
| steamrt4-partial | 540 | SDK-built runtime, same fixture, exit 0. |
| pacing | 900 | No captures/checkpoint writes; one >100 ms CPU-frame outlier after frame 99. Publisher work overlapped: not a clean baseline. |
| pacing-x2 | 1800 | No captures; 11 >100 ms post-startup outliers while publisher work overlapped. Effects also enabled: not an interpolation-only comparison. |
| pacing-x2-warm | 1800 | Warm, no captures/checkpoint writes. No >100 ms CPU frames after frame 99; maximum 39.955 ms. Not an unlocked FPS benchmark. |

Warm effects still cause a **14.072 s startup CPU frame**, including 13.979 s
in `grass_render_ms`. Cache reports 20/20 pass hits and 140/140 SPIR-V hits,
zero compilations. The known Grass startup stall is real, not screenshot
capture or first-use shader compilation. Do not describe it as resolved.

## Remaining

- Build portable Forge, audit/assemble/install the new Flatpak. Existing
  installed Flatpak has not changed; development alignment is separate.
- Actual Steam Deck and physical-input checks; original-mod paired visuals.
- Validation-layer resize run and exhaustive restricted-item contexts.
- Resolve Grass startup cost separately, without counting interpolation or
  synchronous captures as simulation throughput.
