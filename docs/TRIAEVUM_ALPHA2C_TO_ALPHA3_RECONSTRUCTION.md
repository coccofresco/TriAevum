# TriAevum: reconstruction from alpha.2c to the withdrawn alpha.3, and regression qualification

Date: 2026-09-22. Scope: the 100 commits between `v0.6.0-alpha.2c` and HEAD
`a9b4477` on `port/linux-nri`. This record separates what in that range is
useful product work from what must stay out, and qualifies the regressions that
withdrew alpha.3 and alpha.3b. It is a record of executed checks, not a release
approval: no package is published by this document.

## 1. Method and identities

- Source: `I:/TriAevum-public/source-repository-clean`, branch `port/linux-nri`,
  HEAD `a9b4477` (withdrawal commit). Range start `e419028` (alpha.2c
  publication record).
- Build tree: `J:/TriAevum-verify-20260910/runtime`, configured against this
  checkout (`CMAKE_HOME_DIRECTORY` = this repository), Ninja + clang-cl 22.1.6,
  Vulkan SDK 1.4.350.0, vcpkg `x64-windows-static`, whole-AOT product mode.
  Test executables land in `I:/TriAevum-public/ui-dependencies/bin`.
- Product runtime built from this checkout:
  `I:/TriAevum-public/ui-dependencies/bin/TriAevum.exe`, SHA-256
  `804f39bf75d5ceafccce265492ad86a27ce4c883bb8f92c4e28a1219ea952c10`.
- Pre-fix comparison binary: the frozen alpha.3b package runtime
  `I:/TriAevum-public/releases/v0.6.0-alpha.3b/qualification-windows/TriAevum.exe`,
  SHA-256 `0f76c822145dea1aea11d256a938f107a28e0b5ae509cd7644e473b9e3b698b2`.
- Private playable installation used for the live checks:
  `I:/TriAevum-public/releases/v0.6.0-alpha.3b/qualification-windows` and the
  private checkpoint evidence under `J:/TriAevum-diagnostics/issues45-46/`.
  Both are read-only inputs; the probe copies configuration and save data into
  private output directories and never writes the installation.
- `docs/TRIAEVUM_POST_ALPHA2C_REGRESSION_REVIEW.md` already records an
  inventory of the first 81 commits. This document covers the complete 100 and
  adds the 19 later commits, the executed checks and the corrected regressions.

## 2. What the range contains

The range is not one feature: it is 100 commits in four kinds of work.

| Kind | Evidence | Product value |
| --- | --- | --- |
| Renderer/pipeline infrastructure | parametric TEV, native fragment/vertex families, NRI pipeline ownership, toon/temporal precompilation, pacing and stall work | Useful. Embedded shader artifacts cover most of the previously runtime-compiled families; runtime misses still occur (see the residual limits in `TRIAEVUM_POST_ALPHA2C_REGRESSION_REVIEW.md`). |
| UI/TopScreen/UI composition | centered composition, minimap layout, native HUD aspect, movie host services, HD fonts, Game Over choices, title/menu draw domains | Useful. Directly answers issues 39, 40, 42, 44, 46, 48. |
| Input | shared SDL routing, physical device identity, motion aiming, mouse ownership | Useful. |
| Issue 45 ordered TextureCopy progress | `3b7e8a1`, `b230513`, `0bcd22d` | Partly useful; the runtime half carried the regression corrected in section 3.3. |
| Diagnostics and rejected experiments | direct-cohort AOT (`ecea4ca`), cold-read split (`b0c4aff`), call-free region pilot (`5207181`), native kernels and cohort replays | Keep as opt-in diagnostics. `TRIAEVUM_AOT_RENDICONTO_2026-09-17.md` measured no repeatable gain for the pilot; do not activate them in a release. |
| Deferred/reverted | split F1/F12 frontend (`0b30a6e`, `9892379`, `23961d8`, preserved on `feature/deferred-f1-f12-menus`) | Not active. `a946f34` restored the consolidated F1 surface, which is the current product surface. |

Package and release records: `7b88099`, `ddf85a3`, `89dc2b2`, `f72b161`,
`6863881`, `2df81d3`, `a9b4477`. The last three are withdrawn-release records,
not features.

Android donor work (`WINDROID-EMU/RECOMP-ZELDA-3DS-ANDROID`) is evaluation only
and is not part of this range.

## 3. Regressions found and corrected

### 3.1 Forge cannot write a private module inside a deep installation root (issue 53)

`package_private_module` reported
`cannot write private game module: [Errno 2] No such file or directory` and
named a `.tmp` path. The reporting host installation root placed the module
destination at 248 characters; `atomic_write_bytes` created its temporary file
as `.<destination name>.<random>.tmp`, which put the temporary path at **262
characters**, past the 260-character legacy Win32 limit. On a host without long
path support the create fails with `ERROR_PATH_NOT_FOUND`, which Python reports
as `ENOENT` — hence the misleading "no such file or directory" for a directory
that exists.

Correction (general, at the owning helper): `tools/triaevum_release/common.py`
now creates its temporary file with a short fixed name (`.<random>.tmp`), so the
temporary path never exceeds the destination path. Every Forge atomic write
(state, configuration, module, shader pack) is covered by the single change; no
content-addressed name changed, so existing caches stay valid.

Evidence: `tools/triaevum_release/test_common.py`
(`AtomicWritePathCapacityTests`) shapes a destination like a content-addressed
module inside a padded installation root and requires the temporary path to stay
inside the legacy limit. It fails against the previous implementation and passes
with the correction. The full release Python suite passes: 411 tests, 14
skipped (410 + the new test).

### 3.2 F1 opens the settings window only when held (issue 51)

The consolidated settings window was toggled by a **per-frame sample** of the F1
key state (`Fast3dGui::DrawMenu` compared `IsKeyDown(F1)` against a latch).
The shipped loop pumps SDL events and samples in the same iteration, so a tap
whose press and release land in one iteration is invisible while a hold always
spans a sample — exactly the reported "I had to hold F1". The same file already
drives F2 from the key event with an explicit comment about short taps.

The whole chain was byte-identical to `v0.6.0-alpha.2c`, so the defect is latent;
`a946f34` reintroduced it in-range after `0b30a6e`, `9892379` and `23961d8` had
made it event-driven, which is why the range is where it resurfaced.

Correction: F1 now toggles the window from the key event
(`Fast3dGui::HandleWindowEvents`, `SDL_KEYDOWN` with `repeat == 0`), through the
existing `ToggleOot3dGraphicsWindow` path; the sampled latch
(`mOot3dGraphicsToggleKeyWasDown`) is removed. `DrawMenu` keeps only the
per-frame host-UI visibility and mouse-capture sync. No debounce and no
input-specific special case: this is the implementation the branch already had.

Evidence, live A/B on the packaged private installation with the deterministic
capture profile (`--native-fidelity`, captures every 60 frames):

| Run | Frames before tap | First tap | Second tap |
| --- | --- | --- | --- |
| Control (two runs) | byte-identical | — | — |
| alpha.3b package runtime (pre-fix) | byte-identical | **0 changed pixels** | — |
| Corrected build | byte-identical | 366,446 changed pixels (39.76 %) | frame returns byte-identical to control |

Control determinism was verified first: two independent runs produced five
byte-identical captures each. The actual-widget smoke test also passes:
`triaevum_f1_settings_smoke`, 3,978 assertions.

### 3.3 Restored raw TextureCopy loses its guest completion obligation (issues 45/49)

`0bcd22d` introduced an ordered GPU raw-copy path whose completion obligation
(`Oot3dPicaInterruptId::Ppf`) was tracked in **window-local state**
(`pendingTextureCopyCompletions`) while the copy itself lives in the persisted
ordered-replay accumulator. Restoring a state that still contains a pending raw
copy clears the window-local set and restores the accumulator, so the gate that
authorises the only dependency flush stays closed: the copy never executes, PPF
never arrives, the guest spins and the last presented frame (the revelation's
white flash) stays on screen. This is the failure the release notes described as
"restoring a savestate made during the revelation's flashback remains
unreliable", and it reproduces deterministically.

Correction (invariant: one accepted transfer command yields exactly one guest
completion, owned by the persisted ordered-replay state):

- `Oot3dPicaVisualFrameAccumulator::HasPendingRawCopyCompletion()` and
  `Oot3dPicaPresentationScheduler::HasPendingRawCopyCompletion()` derive the
  obligation from the accumulator, which the savestate codec already serializes.
- The dependency flush is gated on that query instead of the window-local set.
- The window-local set, its clears on restore, its insert on capture and its
  erase on commit are removed.
- The commit callback no longer silently reports success for a completion it
  does not own.

The four hard failures for a raw copy that the GPU path cannot service
(unavailable/unmapped source, ambiguous ownership, destination aliasing an
attachment) are unchanged: they remain explicit errors, not silent fallbacks.

Evidence, live A/B on the private checkpoint saved during the revelation
flashback (`sheik-copy-overlap-save/checkpoint.oot3dsav`, 600 bounded frames,
native 30 Hz timing):

| Runtime | Guest draws | Display transfers | Memory faults |
| --- | ---: | ---: | ---: |
| alpha.3b package runtime (pre-fix) | 0 | 0 | 0 |
| Corrected build | 38,933 | 961 | 0 |

The pre-fix run reproduces the recorded softlock exactly; the corrected build
returns to gameplay. `oot3d_native_pica_visual_savestate_tests` additionally
round-trips a pending raw copy and requires the restored scheduler to report the
obligation and discharge it exactly once at the dependency boundary.

### 3.4 The default build was red: title-specific code inside the shared renderer boundary

Found while completing the raw-copy work, not reported by a user. The default
build runs `verify_renderer_3ds_cross_title_boundary`
(`runtime/three_ds_recomp/tests/CMakeLists.txt:348`, a dependency of the canary
and capture-conformance targets), and it failed on the tree as committed:
`include/fast/renderer3ds/pica_lighting_program.h` and
`include/fast/renderer3ds/pica_ui_canvas.h` both contained the forbidden token
`oot3d`. The lighting file is committed (`a6ccd1c`, `d61c2f1`, pre-alpha.2c); the
canvas file already carried the token at HEAD and the composition-domain change
of this session added a second one. So the gate had been failing before this
session, and no product build noticed because the product target does not depend
on it.

Both files put title-side code in the title-neutral renderer:

- `BuildPicaLightingProgram(const Oot3d::PicaFragmentLightingState&)` packed the
  decoded native lighting state into the shared std140 program, and the header
  included the title's decoder.
- `ResolvePicaRasterCanvas` applied this title's LCD rotation/sampling
  convention through `FitUiPresentation`/`FillUiPresentation`.

Correction, moving the title-specific halves to their owning adapter and leaving
the reusable halves shared:

- `PicaLightingProgram` (the std140 struct and the GLSL snippets) stays in
  `renderer3ds/pica_lighting_program.h`; the builder moved to
  `include/fast/oot3d/pica_fragment_lighting.h` as
  `Fast::Oot3d::BuildPicaLightingProgram`, with its two call sites updated.
- `PicaRasterCanvas` and `ClipToPicaCanvas` stay in
  `renderer3ds/pica_ui_canvas.h`; the resolver moved to
  `include/oot3d/renderer/ui_presentation_layout.h` as
  `Oot3d::Renderer::ResolvePicaRasterCanvas`, with its three call sites updated.
  The backend reaches it as `::Oot3d::Renderer` because `Fast::Oot3d` is a
  different, title-side namespace.

The gate now reports 67 files title-neutral, and the relocation is
behaviour-preserving: flashback 12/12, boot 5/5 and file-select 6/6 captures are
byte-identical to the qualified baselines, `pica_ui_canvas_tests` passes, the
lighting GPU test compiles against the new location, and 292 graphics-foundation
tests stay green.

Two further build-integrity defects surfaced while verifying this, both
pre-existing and both fixed here:

- `oot3d_pica_scene_frame_tests` had never linked: it calls
  `Oot3d::Renderer::AzaharCityHash64`, which `fast/oot3d/*.cpp` compiles into
  `ThreeDsRecomp::Runtime`, a library the target did not link. With the library
  linked the suite builds, and one stale setup in it failed
  (`UniformBuffer.Size` did not cover the declared uniform slices, so `Record`
  correctly returned `InvalidUniformRange`); with the declared buffer covering
  both slices all 18 tests pass. Until now this suite - the direct test of the
  frame evidence the composition-domain fix relies on - had never executed.
- `tools/triaevum_release/tests/run_f1_settings_smoke.ps1` linked library copies
  left in the build tree by an older configure instead of the configured archive
  directory, so it failed with undefined input/motion symbols. It now reads
  `CMAKE_ARCHIVE_OUTPUT_DIRECTORY` from the build cache; the actual-widget smoke
  passes 3,978 assertions again.

Environmental build gaps remain, unchanged and unrelated to these fixes: the
vendored googletest `gmock` target fails under `-Werror,-Wcharacter-conversion`
and has no consumer in this project; `oot3d_native_mass_aot_tests` needs the
generated `oot3d_a32_generated.h`; the two `native_demo_host` executables need
`ship/Context.h` and `oot3d/scene_cutscene_frame_runtime.h`, which this tree does
not carry; `oot3d_native_game_runtime_tests` needs `oot3d_native_runtime_host.lib`.

## 4. Checks executed in this session

All C++ suites below were built from this checkout in the tree named in
section 1 and run from `I:/TriAevum-public/ui-dependencies/bin`.

| Check | Result |
| --- | --- |
| Windows release/Forge Python suite (`python -m unittest discover tools/triaevum_release`) | 415 tests, 14 skipped, no failures |
| Release runtime-matrix/display Python suites (`tools/triaevum_release/tests`) | pass, 7 skipped |
| AOT runtime Python suite (`tools/oot3d/native_a32_runtime`, excluding the untracked concurrent `test_whole_aot_memory_regions.py`) | 105 tests, no failures |
| Renderer/AOT tool Python suite (`tools/renderer/tev_program`) | 51 tests, no failures |
| Other Python suites (`tools/oot3d/native_game_runtime`, `native_pica_frontend`, `codec_validation`, `renderer_integration`, `runtime/three_ds_recomp/tools`, `tools/android`) | pass (native_pica_frontend skips 5) |
| C++ test executables | 49 of 54 pass |
| Actual F1 widget smoke (`triaevum_f1_settings_smoke`) | 3,978 assertions passed |
| SDL controller/input suites after the Steam virtual-pad opt-in | pass (`three_ds_sdl_controller_tests`, `oot3d_native_a32_input_tests`, `three_ds_recomp_input_tests`); the new opt-in assertion fails without the fix |
| Bounded product boot after the same change | exit 0, 300 frames, 18,098 draws, 579 transfers, 0 faults |
| Revelation flashback reproduction and correction (packaged install, native-30) | defect reproduced deterministically (desaturated region at columns `[40,1240)` of 1280x720 with live colour margins, frames 3120/3420) and then corrected to full-width desaturation; boot and file-select byte-identical; see section 7 |
| F1 key-event A/B, live runtime | corrected (section 3.2) |
| Raw-copy savestate restore A/B, live runtime | corrected (section 3.3) |
| Control-run capture determinism | 5 of 5 captures byte-identical across runs |
| Raw-copy cost attribution and readback fix (section 7.4) | 513 copies attributed per cause (`TRIAEVUM_RAW_COPY`): readback was 94% of copy time. In-band readback into cached staging takes 33.6 -> 4.36 ms per copy and the copy window from 6.95 to 1.83 ms/frame (two paired runs, 1.832/1.825), `execute` 12.50 -> 7.84 ms/frame; 10 PICA suites green, 12 flashback captures byte-identical, boot 5/5 hash-exact, file-select 6/6 identical to the agreeing qualified baselines, gameplay run unchanged with zero raw copies |
| Raw-copy GPU encode (section 7.4) | Differential against the CPU oracle on real colour targets: 125,952,000 bytes, 0 mismatches over 164 copies; 12/12 captures byte-identical; readback 7.37 MB -> 768 KB per copy, window transfer time 1.83-1.92 -> 0.730 ms/frame, per-copy cost 4.36 -> 1.14 ms |
| Raw-copy destination host-access watch (section 7.4) | `NativeA32Memory` range watch traps the translated destination on the checked path; `oot3d_native_a32_ctr_host_tests` proves identical values and per-origin counters. Revelation window: zero guest typed/byte accesses, 104,141 renderer pointer handouts; short run from `sheik-near-copy` reproduces it with 8,200 suppressed writeback spans as the live-trap control |
| Title-neutral renderer boundary (section 3.4) | Gate reports 67 files title-neutral (was failing on two files at HEAD). After the relocation: flashback 12/12, boot 5/5, file-select 6/6 captures byte-identical to the qualified baselines; `pica_ui_canvas_tests` passes; `pica_lighting_gpu_tests` compiles against the moved builder; `oot3d_graphics_foundation_tests` 292 tests pass |
| `oot3d_pica_scene_frame_tests` (section 3.4) | Built and executed for the first time (18 tests, all pass, including the frame-evidence test the composition-domain fix relies on) |
| Actual F1 widget smoke after the script fix | 3,978 assertions passed (`run_f1_settings_smoke.ps1`, real renderer/Controls/TopScreen widgets) |
| Input suites after the Steam virtual-pad opt-in | `three_ds_sdl_controller_tests`, `oot3d_native_a32_input_tests`, `three_ds_recomp_input_tests` pass |

Four Python suites could not be exercised here and are not source findings:
`tools/oot3d/oot3d_asset_tool/tests` and `tools/oot3d/source_native_runtime`
fail to import their modules under per-directory discovery;
`tools/oot3d/source_integration` requires external decompilation checkouts
(`git rev-parse` of donor commits); `tools/triaevum_release/flatpak` requires
PyGObject, and Flatpak is a historical packaging path.

Five C++ executables fail, none of them because of this session's changes and
none reproduced as an alpha.3 regression:

| Executable | Cause |
| --- | --- |
| `oot3d_native_a32_execution_tests` | Tree configured without `oot3d_native_a32_aot`: `OOT3D_NATIVE_A32_AOT_AVAILABLE` is undefined, so `NativeA32ExecutionRuntime::Available()` is false. Environmental. |
| `oot3d_native_actor_runtime_tests` | Same missing A32 registry; `EvaluateNativeRoomMaterialTevAlphaOperation` cannot evaluate. Environmental. |
| `oot3d_native_audio_tests` | Requires ROM-derived fixtures (`QueenSound.bcsar`, `code.bin`, …). Not runnable here. |
| `oot3d_native_a32_process_tests` | `TestNativeFunctionAtDispatchEntry` asserts the interpreter dispatcher's native-function registry, which whole-AOT product mode does not consume. `Run()` at `v0.6.0-alpha.2c` already ignored `mNativeFunctionCallback`, and no product code calls `SetNativeFunctionCallback`; pre-existing configuration-dependent test. |
| `oot3d_toon_outline_gpu_tests` | `Oot3dGrassLodGpu.IndexedVertexDecoderMatchesExpandedStreamForEveryTopology` fails with `Vulkan failure: -13` in the shared compute fixture. Test, fixture and indexed-topology code are byte-identical to alpha.2c. Pre-existing on this host. |

Combined with the standalone TEV/PICA CPU and GPU suites recorded in
`TRIAEVUM_POST_ALPHA2C_REGRESSION_REVIEW.md` (19/19), and the graphics
foundation suite (291 tests) and shader pipeline suite (40 tests) which pass
here, this qualifies the exercised scenarios only.

## 5. Why alpha.3 and alpha.3b were withdrawn

The withdrawal commit itself records no reason; the qualifying evidence is the
sequence of user reports filed against the published packages:

- `#48` (title Start crash) was found, fixed in `16be838` and shipped as
  alpha.3b. It is a coverage gap: the release was qualified for unattended
  playback, not for pressing Start.
- `#45` (softlock at the Temple of Time) was addressed by `0bcd22d`, whose
  ordered-copy runtime path is the source of the defect corrected in section 3.3.
- `#49` (freeze on a white screen right after the Adult Zelda reveal) matches
  the softlock in section 3.3, which is now reproduced deterministically and
  corrected.
- `#51` (F1 only works when held) is the sampled-poll defect corrected in
  section 3.2.
- `#53` (Forge "cannot write private game module") is the path-capacity defect
  corrected in section 3.1.

Alpha.3 and alpha.3b therefore failed qualification for a reproducible reason
rather than an unknown one: the release tested the happy path of a new ordered
GPU copy mechanism and a sampled input toggle, and both broke on ordinary user
actions (loading a checkpoint, tapping a key, installing into a deep directory).
Three of the five defects above are corrected in this work; `#45` and `#48`
remain as recorded in their own documents.

## 6. Linux AppImage report on alpha.3 (Forge entry, persistence, controllers)

The AppImage report says: the Forge only started from a command line; after
preparing, the game launched but with no controller support; and once the Forge
was closed, launching the AppImage again showed an error about the prepared
title. One of the three is a proven defect with a corrected root cause, one is a
proven entry defect with a corrected trigger, and one could not be reproduced on
a reachable host — each is stated as such.

### 6.1 User data written inside the AppImage mount (persistence)

`host_layout.for_package` decided "this is an AppImage" from the `APPDIR`
environment variable only. When `APPDIR` is absent or not absolute — a payload
binary started directly, a wrapper that sanitizes the environment, a
Steam/desktop launch that re-execs through a shell — the activation root fell
back to the package directory itself, which is the read-only or ephemeral
squashfs mount. Imported content, the private module, saves and caches were then
written into a mount that disappears, which is exactly "something isn't
persistent in the installation". Measured before the correction:

| `for_package(<mount>/usr/lib/triaevum, platform="linux")` | activation root |
| --- | --- |
| `APPDIR=<mount>` | `$HOME/.local/share/TriAevum` |
| no `APPDIR`, or a relative one | `<mount>/usr/lib/triaevum` (inside the mount) |

Correction: the payload layout is authoritative and the environment is only
corroboration (`host_layout.appimage_root`). A frozen package that lives at
`<root>/usr/lib/triaevum` always resolves user data to
`$XDG_DATA_HOME/TriAevum` or `$HOME/.local/share/TriAevum`, whatever started it,
while Flatpak and portable installs keep their existing behaviour. The
`sanitized-environment`, `relative APPDIR`, "mount relocation" and
"similar directory name" cases are covered in `test_host_layout.py`; the new
cases fail against the previous implementation.

### 6.2 A desktop launch that passes the opened file did nothing

`desktop_launcher.main` accepted only `[]` or `["--forge"]`; anything else
printed `Usage: triaevum [--forge]` to stderr and returned 2. Desktop
integrations and file managers routinely pass the file they opened (a `%U`
expansion, or the AppImage path itself), so a normally opened AppImage exited
immediately with no window and no visible error — the reported "the Forge
launches only by command". Correction: an argument that is an existing file is
treated as the opened file, reported once on stderr, and ignored; unknown
options are still rejected with the usage error. `AppRun` also now reports a
missing or non-executable bundled Forge instead of exiting silently.

The remaining part of that report is not a code defect: an AppImage only appears
in desktop menus when an integration daemon (or Steam, for a non-Steam shortcut)
registers it. That is documented in `TRIAEVUM_APPIMAGE.md`; no packaging change
can install a menu entry from inside the file.

### 6.3 Controllers: Steam Input's virtual gamepad was discarded by SDL

Root cause (source + binary evidence, corrected):

- On Linux and SteamOS, Steam Input presents the pad to the game as its own
  uinput *Steam Virtual Gamepad* (Valve `28DE:11FF`). While Steam owns the
  physical device the real pad is not enumerable, so that virtual device is the
  only controller the process can see.
- SDL discards that device unless the application opts in through
  `SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD`. The bundled SDL 2.32.10
  implements the opt-in: the hint name sits in the library next to
  `uinput-fpc` and `STEAM_COMPAT_PROTON` (the Linux Steam-Input path), and the
  library's built-in mapping table already maps
  `03000000de280000ff11000000007701,Steam Virtual Gamepad`. The runtime never
  set the hint, so `ConnectedPhysicalDeviceManager` counted zero joysticks and
  the product reported no controller. Windows is unaffected because Steam Input
  there presents an XInput device that SDL does not classify as a Steam virtual
  pad, which is why the same runtime has controllers on one platform and not on
  the other.
- Correction: `ConfigureSDLControllerCapabilities()` now opts in through the
  *process environment* (`SDL_setenv("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1", 0)`)
  before `SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)`. The environment, not a
  hint, is required: SDL3 tests the variable directly and bypasses its hint
  table, and the AppImage's library is sdl2-compat over SDL3 — a hint-only fix
  would have been silently ignored by exactly the library that ships in the
  package. SDL2 reads the environment before its hint table, so the single call
  covers both, `overwrite=0` keeps an explicit Steam or user value
  authoritative, and no new input backend, per-device exception, udev rule or
  Steam-account change was added.

Verified here: `three_ds_sdl_controller_tests`, `oot3d_native_a32_input_tests`
and `three_ds_recomp_input_tests` pass, and a bounded product boot after the
change runs 300 frames / 18,098 draws / 579 transfers with zero faults and exit
zero, so the controller initialisation path still executes and the product is
unaffected on the platform that has no Steam virtual pad.

Not verified: the filtering itself. It happens in SDL's Linux uinput driver, so
a synthetic `SDL_JoystickAttachVirtualEx` device with the same Valve identifiers
is *not* filtered on Windows (measured), and no Linux host answered in this
session. The confirmation belongs to the Steam Deck run in section 8.1.

## 7. Revelation/movie frame composition (corrected) and the CPU copy path (open)

Reported: the black-and-white revelation video plays at original resolution in
the centre while the frame margins show the same content at output resolution
and in colour. Reproduced deterministically on this host (packaged install,
`sheik-native-trigger/checkpoint.oot3dsav` + `native_cutscene_dialogue_advance.json`,
native-30): sequence frames 3120 and 3420 have a fully desaturated region at
columns `[40,1240)` of 1280x720 — exactly the native 400x240 aspect fitted to
the output height — with live colour content in the two 40 px margins.

### 7.1 What the measurements and the trace establish

- Two writers share one swapchain image. The swapchain render pass uses
  `loadOp=LOAD` and is never cleared, so anything a smaller writer does not
  cover stays visible. One writer is the title's presented frame (native-aspect
  fit: `[40,1240)` x `[0,720)`); the other is our re-composition, which
  rasterises scene-domain draws over the whole output extent.
- The measured rectangle is the native-aspect fit of the title's frame, not the
  in-target UI canvas geometry: for a 480x400 target `ResolvePicaRasterCanvas`
  yields 864x720, while `FitUiPresentation(1280,720,400,240)` yields exactly the
  measured 1200x720 at x=40.
- The draw trace (`--pica-semantic-trace`) over the same sequence shows the
  top-screen 2D frame content attributed `ui/ui/native_ui_lifecycle` with
  full-framebuffer viewport coverage (5,301 draws), while every placed UI
  element on the lower screen uses a partial viewport (30,248 draws). Full-screen
  two-dimensional content is therefore currently classified as a placed UI
  element, which is the classification the handover warns about ("a fullscreen
  quad is not automatically UI").
- A counter check rules out the presentation scheduler as the cause of the
  *content* difference: interpolation contributes no second frame in this run
  (`cross_frame_history_enabled=false`, `interpolated_draws=0`), and the margins
  are live across consecutive captures, not stale.

### 7.2 The copy that feeds the black-and-white frame runs on the CPU

`SubmitNativePicaTextureCopy`
(`runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_texture_copy.cpp`)
executes the raw `TextureCopy` the flashback's effect depends on as a
GPU->CPU->guest-memory->GPU round trip:

1. `WaitForAllPresents()` then `vkQueueWaitIdle(mGraphicsQueue)` — full GPU sync
   (lines 96-103).
2. `CaptureNativePicaImageBytes(...)` — readback of the whole colour target
   (line 104; implementation at `gfx_vulkan_pica.cpp:1865`).
3. `Renderer3ds::EncodePicaFramebuffer(...)` — CPU tiling/channel conversion.
4. `mPhysicalMemoryCommit(...)` (line 157) — CPU write into guest memory.

Where the readback sits, verified by enclosing function so that the plan does
not remove materialisation that has to stay:

| Readback site | Enclosing function | Kind |
| --- | --- | --- |
| `gfx_vulkan_pica_texture_copy.cpp:104` | `SubmitNativePicaTextureCopy` | ordered-copy hot path (the one to move to the GPU) |
| `gfx_vulkan_pica.cpp:8384` | `CapturePicaTextureCache` (`:8348`) | idle snapshot / savestate / capture — explicit materialisation, must be preserved |
| `gfx_vulkan_pica.cpp:8568` | `CapturePicaColorTargets` (`:8555`) | idle snapshot / savestate / capture — explicit materialisation, must be preserved |

The live sampling seam is elsewhere and is where the copied content must be
resolved: `ResolveNativePicaCopiedTexture` (`gfx_vulkan_pica_texture_copy.cpp:8`,
called at `gfx_vulkan_pica.cpp:4148`) and `GetOrCreateNativePicaTexture`
(`gfx_vulkan_pica.cpp:2553`), with `CaptureTextureResources`
(`oot3d_native_pica_submission.cpp:388`) on the submission side. Coherence is
reestablished there from the writeback bookkeeping rather than from a fresh
readback. So the effect reaches our composition through CPU copies of GPU
content, while the title's own presented frame is a GPU-side snapshot: that split
is exactly why the centre showed the finished black-and-white frame while our
re-composition still drew the pre-effect colour content in the margins, and it is
why the effect "passes through a CPU path" instead of staying in NRI. The issue
record already implies the ceiling: one 7,534-frame run spent about 25.65 seconds
in transfer submission, and the implementation is described there as a
conservative synchronous readback.

### 7.3 Correction: screen-space content is frame content

The fitted box was drawn by our own composition, not by the presented display
image. Proved by measurement: forcing the in-target UI raster canvas from
`FitUiPresentation` to cover made the defect frame fully desaturated across all
1280 columns with zero colour in the margins, while changing the display-image
scanout extent changed nothing at all.

That experiment also showed why a blanket cover is wrong: it would stretch every
placed UI element. The correction therefore introduces the missing composition
case instead:

- `PicaCompositionDomain::Screen` — two-dimensional content the title composites
  over the whole native screen: movie frames, full-frame effect re-draws,
  screen-space post-process. It is frame content, so it is rasterised over the
  presentation extent without distortion (`FillUiPresentation`: native
  proportions preserved, excess centred) and it is exempt from the UI canvas
  clip. `Ui` keeps the uniform native fit and the clip that HUD, menu layout and
  pointer mapping depend on; `Scene` keeps its field-of-view-compensated stretch.
- The classification uses frame evidence, not an asset or scene identity: a
  UI-attributed draw is resolved as `Screen` when it renders into a colour target
  that **this frame's scene draws** used (`PicaSceneFrame::HasSceneDrawOnColorTarget`)
  **and** its viewport covers that whole target. Both conditions are required,
  and both were measured: the target test alone also reclassified the title's own
  full-screen menus, which are drawn on the same surface (the file-select screen
  changed by half its pixels), while the coverage test alone cannot tell a
  full-screen menu surface from full-screen effect content. Together they leave
  placed elements - HUD, menus, file select, lower-screen UI - on the uniform
  native canvas that their layout and pointer mapping depend on.
- The rule lives at the single owner of the raster canvas
  (`ResolvePicaRasterCanvas`, applied per draw in `gfx_vulkan_pica.cpp`), so the
  resolved domain is also what the frame record and diagnostics carry.

Measured on the packaged installation, native-30, revelation flashback
(desaturated columns of 1280, colour in the margins):

| Frame | Before | After |
| --- | --- | --- |
| 2820 | 40..1239 (1200 cols), black frame, no colour | 0..1279 (1280 cols) |
| 3120 | 40..1239, margins 0.619 / 0.712 saturation | 0..1279, margins 0.000 / 0.000 |
| 3420 | 40..1239, margins 0.536 / 0.237 saturation | 0..1279, margins 0.000 / 0.000 |

Inertness, measured against pre-change captures of the same runs: boot
(5 captures, frames 120-360) and the title-to-file-select sequence (6 captures,
frames 120-1120) are **byte-identical**; the flashback frames are the only ones
that change, and they change into a single full-frame image. Reviewed by eye as
well: the native Game Over save question stays centred at native proportions with
its side bars, and Temple gameplay with dialogue and HUD is unchanged, so the
placed-UI canvas policy is intact on the frames that depend on it.

Unit coverage: `Oot3dPicaRasterCanvas.ScreenSpaceContentCoversAndPlacedUiKeepsNativeFit`
(canvas per domain) and
`Oot3dPicaSceneFrame.ReportsTheColourTargetsTheFrameSceneDrewInto` (the frame
evidence the classification rests on). The foundation suite is 292 tests, all
passing, and the frontend, transfer, bridge and visual-savestate suites stay
green.

**Correction (2026-10-05): the evidence was too broad.** The two conditions
above also hold for placed UI in gameplay. The TopScreen button cluster and
minimap, and a dialogue's text box and glyphs, are UI-attributed draws with a
whole-target viewport on the colour target the scene drew into (gameplay trace:
4 such draws per frame; Zelda's dialogue: 3). They were therefore composed as
`Screen`: rasterised with the cover canvas and exempt from the UI clip, so
they grew, moved towards the real output edges and were cut at 4:3 and 21:9
(the A button disappeared at 21:9). The checks of this section never looked at
gameplay HUD or dialogue at another aspect ratio.

What frame content has and placed UI lacks is coverage: the flashback's
consumer (one texture an ordered raw copy of the rendered frame owns, 50 copy
references in the trace), the fades (untextured quads) and the Sheikah Stone
movie frames each cover the whole native screen, while every placed element
covers part of it. `ResolvePicaUiFrameContent` (`renderer3ds/pica_composition.h`)
now also requires geometry covering the viewport, proven by projecting the
vertex positions through the frontend's typed model-view-projection layout
(`renderer3ds/pica_draw_coverage.h`). Covering content that redraws rendered
copies or washes the frame takes the scene's mapping: the cover canvas of this
section had zoomed the flashback by 1.07 at 16:9, 1.25 at 4:3 and 1.43 at 21:9
against the scene it copies. A picture of its own (a movie frame) keeps the
cover canvas, so the grey fill drawn behind it never shows. The rule uses PICA
provenance and geometry only, no address or asset. The per-frame diagnostics
report `whole_target_ui_draw_count` and `screen_content_draw_count`.
Verification is in `TRIAEVUM_WINDOWS_RELEASE_QUALIFICATION.md` (E1-E3, I1, I5).

**Movie frames fitted with black bars (2026-10-05, user decision).** An opaque
picture of its own is no longer covered: it is shown whole on the native
canvas of placed UI, and before it is drawn the renderer paints the target
outside that canvas opaque black, on the native colour attachment only
(`SurroundPicaCanvas` in `renderer3ds/pica_ui_canvas.h`; nothing is painted at
the native aspect ratio). The fill drawn behind the movie never shows: the bars
replace it. Opaque means that no sampled texture stores alpha per texel
(`PicaTextureFormatCarriesAlpha`; the movie frames are RGB8). A covering picture
with alpha is an overlay laid on the frame - the revelation draws an IA8 mask
over its first 72 flashback frames under Original30 - and keeps the cover
canvas, so a mask over a visible scene is never cut by bars. Diagnostics:
`own_picture_draw_count`, `picture_bar_rect_count`,
`picture_overlay_draw_count`. The Sheikah Stone movie has one picture draw per
frame (237 frames in the verification run), and no pixel outside the picture
is lit at 4:3 (bars above and below), 16:9 or 21:9.

**The copied frame is upright (2026-10-05, I5).** The flashback was the
left-right mirror of the scene it copies. The raw-copy path read attachment row
y as PICA memory row y, but attachments hold window rows bottom-up (the viewport
maps NDC y = -1 to row 0) while memory stores the top window row first:
Azahar's framebuffer writes window row y at memory row height - 1 - y, and its
texture unit reads coordinate t from row height - 1 - t. On the top screen,
whose framebuffer is rotated, a row reversal is a left-right mirror.
`EncodePicaFramebuffer`, its GPU form and the GPU copy derivation now reverse
rows between attachment and memory order; a base decoded from native bytes is
already in memory order. In the interpolated revelation every flashback capture
(frames 5060-5580, one every 20) is now the exact mirror of the previous
build's (luminance correlation 1.000 mirrored, -0.04 to 0.77 as is), while the
dialogue boxes drawn over it keep their orientation. Link now stands in the
left part of the frame looking right, towards the castle Impa rides out of,
as the user remembers the original.

### 7.4 Confirmed: the flashback copy and its consumer, measured

The implementation plan for the GPU path is `TRIAEVUM_GPU_COPY_PATH_PLAN.md`
(produced with the architect-builder agent: phases A-D with the
ownership/version contract, acceptance criteria and the verification matrix).
Its D1 step was executed here, and it answers questions the plan had left as
experiments:

- The transfer trace now records `texture_copy_bytes`
  (`oot3d_native_pica_semantic_trace.cpp`, `RecordDisplayTransfer`), so a run can
  be asked whether it contained a raw copy at all; the trace test asserts both
  the display-transfer zero case and the nonzero raw-copy case.
- A revelation run from `sheik-native-trigger` plus the dialogue timeline
  (3,500 frames) contains **514 raw copies**, every one `TextureCopyBytes` of
  768,000, one every second presented frame from presentation frame 2,473 to
  3,499, destination `0x183E3300..0x184AAF00` (800 rows of 15,360 bytes plus the
  1,024-byte gap, 12.5 MiB), `SignalInterrupt` true, CompletionIds increasing by
  seven.
- **The consumer is a sampler binding, one per copy, in the same frame**: a
  `ui/ui` draw (list-level UI-lifecycle attribution, `source_pc` zero) samples a
  1 MiB texture at `0x183AB300` whose extent overlaps the copy destination. That
  is the title's own full-frame two-dimensional effect - the same draw class the
  correction in section 7.3 composes as screen-space frame content.
- What this does not answer, and the plan keeps as a required experiment
  (phases D3/D4), is whether the translated consumer also reaches those bytes
  through a host pointer. The measurement proves the GPU sampler is a consumer;
  it does not prove it is the only one.
- Cost, measured in the copy window (3,500 frames, 513 flushes, no captures):
  the transfer path spends 6.95 ms per frame against 0.068 ms per frame of plain
  gameplay in the same scene - about 40 ms per raw copy. The first Phase A
  increment (the copy submission waits on its own fence instead of idling the
  graphics queue, keeping present ordering) brings that to 5.90 ms per frame and
  43.7 s of total execution from 46.7 s, with captures byte-identical to the
  previous run.
- **Attribution, then the fix.** The copy now orders its own costs per cause
  (`TRIAEVUM_RAW_COPY`, one shutdown receipt; `RawCopyTiming` in `gfx_vulkan.h`).
  Over those 513 copies the synchronous readback was 16.2 s of 17.2 s of copy
  time (**94%**); PICA encode 0.57 s, texture retirement 0.29 s, span building
  0.13 s, guest commit 0.04 s. Two defects, both measured rather than assumed:
  the readback opened its own immediate command (a second submit and a second
  full queue idle per copy), and its staging was created with
  `HOST_VISIBLE | HOST_COHERENT`, which on this machine resolves to
  write-combined memory - CPU reads from it run at ~289 MB/s. The copy now reads
  the source inside the submission it already waits on (barrier,
  `vkCmdCopyImageToBuffer`, return barrier) through a persistent mapped staging
  buffer chosen by `FindReadbackMemoryType` (prefers `HOST_CACHED`, invalidates
  explicitly when the chosen type is not coherent). Bytes handed to the encoder
  are unchanged. Per copy: 33.6 ms -> **4.36 ms**; the copy window drops to
  **1.83 ms per frame** of transfer time and 7.84 ms per frame of total
  execution (two paired runs: 1.832 and 1.825 ms/frame), against 6.95 and 13.4
  before. Note that plain gameplay in this scene performs no raw copy at all
  (`dependency_flushes` 0, `copies=0`), so its 0.068 ms/frame is the ordinary
  display-transfer snapshot cost and is not a copy-path baseline; the honest
  comparison is the copy window against itself. Verification after the change:
  captures byte-identical on 12 frames, boot 5/5 hash-exact, file-select 6/6
  identical to the three agreeing qualified baselines, 10 PICA suites green.
- **D3/D4, executed.** The open question in section 7.5 step 3 - does the
  translated title read the copy destination through host memory - is now
  measured. `NativeA32Memory` gained an opt-in range watch: watched pages lose
  their fast entry, so typed accesses from the title module (inlined against
  that header, so also from the precompiled module) fall into the checked path
  where they are counted, with identical bytes read and written and the fast
  path untouched when no range is watched. Covered by
  `oot3d_native_a32_ctr_host_tests`. First attempt returned a false zero because
  the transfer names *physical* addresses while the memory object is indexed by
  *guest* addresses; the watch now translates the span first. Revelation window
  (3,500 frames, 513 copies): zero typed reads, zero typed writes, zero byte
  reads, zero byte writes, and 104,141 live-pointer handouts, all from our own
  PICA memory view - the sampler path phase B replaces. A 400-frame run from the
  near-phase checkpoint reproduces it (164 copies, `pointer_reads` 33,291) and
  adds `suppressed_byte_writes` 8,200, exactly the copy's writeback spans. The
  instrument itself is controlled rather than assumed: counting sits in the
  checked path (`ReadSized`/`WriteSized`), because counting only the inline
  fallbacks would have missed the precompiled module entirely, and a gameplay run
  watching one MiB of guest memory reports 80,844 typed reads with a plausible
  first address while the copy destination reports none. Two defects surfaced
  while building that control: the watch was lost on every savestate restore
  (the memory image is replaced wholesale) and the receipt printed shifted
  numbers until its argument list was fixed. The generated title code never
  asks for a pointer at all (311,191 `ReadFast`, 189,552 `WriteFast`, zero
  `ReadBytes`/`GetReadPointer`/`GetWritePointer` in the product cache), so the
  only untrappable case is a pointer handed out to the guest through a service.
- **The encode moved to the GPU, measured.** The native packing of a raw copy
  (Morton order, formats 0-4, half-texel rule) is now done by a compute shader
  writing R32_UINT words into a storage image, copied to host staging inside the
  submission the copy already waits on; the CPU oracle runs only under
  `TRIAEVUM_RAW_COPY_ENCODE_VERIFY=1`, where it compares byte for byte with what
  the GPU produced (`ENCODE_DIFF bytes=125952000 mismatches=0` over 164 copies).
  Captures stay byte-identical to the CPU-path baseline (12 of 12), the readback
  per copy falls from 7.37 MB to 768 KB, the copy window's transfer time from
  1.83-1.92 to 0.730 ms per frame and the per-copy cost from 4.36 to 1.14 ms.
  The host refuses what the shader cannot check (non compute-capable queue
  family, missing R32_UINT storage or transfer-source support, extents or
  dispatch beyond device limits, geometry the oracle would reject, a half-texel
  numerator that could wrap). This is an increment of phase A, not phase A: the
  ordered readback (smaller), present wait, guest writeback, texture
  invalidation and CPU upload are all still there.
- **What the sampler pays because of the copy, measured.** With the GPU encode in
  place the dominant cost moved off the copy itself: over 400 frames and 164
  copies, `TRIAEVUM_RAW_COPY_SAMPLER` reports 164 resolutions, 164 texture
  creations, 171,966,464 bytes pulled through the coherence path and 1,685 ms
  spent resolving them (about 10 ms per resolution, 4.2 ms per frame), against
  307 ms for the copies themselves. Every copy invalidates the overlapping
  decoded texture and the sampler rebuilds it from guest bytes; that rebuild, not
  the copy, is what phase B has to remove by writing the copied region into the
  texture that already exists.
- **The GPU texture decoder is proven before anything switches to it.** With
  `TRIAEVUM_RAW_COPY_GPU_DECODE_VERIFY=1` the new compute decoder is compared
  pixel by pixel with the CPU oracle on the game's own textures:
  `DECODE_DIFF pixels=86026368 mismatches=0` over the copy window and
  `pixels=536960 mismatches=0` in gameplay. Making it faithful surfaced three
  defects worth recording: CPU-written staging consumed by the GPU needs
  `vkFlushMappedMemoryRanges`; the descriptor kept pointing at the previous view
  after the byte image grew; and reading the R32_UINT storage image through a
  `usampler2D` returned zeros, so the decoder reads it with `imageLoad` and needs
  no sampler. The runtime compiler (shaderc) also rejected a construct
  glslangValidator accepted, which is why the live differential, not the offline
  compile, is the gate that counts.
- **The sampler stopped rebuilding copied textures from guest bytes.** A texture
  a raw copy writes into arrives without a content hash, which is exactly the
  case the CPU path answers with a 1 MiB hash, a CPU decode and an RGBA8 upload.
  It now takes a version identity (`SurfaceVersion` in the texture key) and is
  decoded on the GPU from its native bytes, with the previous version of the same
  surface retired immediately so versions stay bounded. Everything else keeps the
  hash identity and the CPU path, and `TRIAEVUM_RAW_COPY_GPU_DECODE=0` restores
  the old behaviour for an A/B. Verified: decoder differential zero mismatches
  over 86,026,368 pixels plus 536,960 in gameplay; 12 of 12 captures
  byte-identical to the CPU baseline in the shipping configuration (3,500 frames,
  513 copies, all 513 textures GPU-created); direct A/B 6/6 identical on the copy
  scene with resolve time 1,306 -> 833 ms, and 6/6 identical on a scene without
  copies; PICA, memory and graphics suites green.
- **Phase C's first increment, and where it stops.** With the GPU-owned decode
  the pre-copy bytes (`PicaPhysicalMemoryWrite::Before`) are no longer needed: the
  copy has already written them, and the resolver serving the GPU does not need
  to know whether the CPU snapshot was old. Under the GPU path that read is gone
  (768 KB of guest reads and 768 KB of copying per copy, plus one heuristic fewer
  in the resolver). Verified: 12 of 12 captures byte-identical, suites green, and
  a save/restore taken mid-phase running 300 copies without faults or stalls. The
  saving does not rise above run-to-run noise on this window, so the reason to
  keep it is the removed read and the removed heuristic, not a wall-clock number.
  What is refused, with reasons: deferring the guest writeback past PPF. Guest
  memory is observable state, the plan itself requires materializing
  host-observable intervals at the latest before PPF, and a pointer handed to the
  guest is unobservable after it is handed out - the watch records that delivery
  and shows none in the measured scenario, which tracks the case without
  licensing a deferral.
- **Where the floor is.** The residual ~4.4 ms per copy is the CPU-encode model
  itself: ~2.0 ms reading back roughly 7.4 MB of source per copy, ~1.1 ms of
  PICA encode, ~0.9 ms retiring overlapped textures. No further Phase A/C
  increment removes it; only the GPU-owned resource of Phase B can, because
  until then the sampler consumes guest bytes rather than a versioned GPU
  surface.

### 7.5 Remaining deep work: the copy behind the effect is still CPU-side

The effect now reaches the whole frame, but it still travels
GPU -> CPU -> guest memory -> GPU before our composition samples it (§7.2), and
the same readback feeds ordinary texture capture. That remains the deep fix the
report asks for, in this order:

1. Keep the raw copy on the GPU: copy the source colour target into a
   GPU-owned native-format image keyed by physical address and layout, and
   publish it as a versioned resource so later draws sample the post-copy
   content — the `mRawTextureCopyWritebacks`/`ResolveNativePicaCopiedTexture`
   seam is the right one, re-pointed at a GPU resource instead of a CPU
   writeback.
2. Stop reading GPU-owned surfaces back for sampling (the live render-target
   alias pattern Shadow2D already uses), instead of
   `CaptureNativePicaImageBytes`. The ordered copy no longer calls that helper:
   its readback is in-band on its own submission, into cached staging. The
   helper is still the correct mechanism for explicit snapshots
   (savestate/capture/debug), and ordinary texture capture still uses it.
3. Keep guest-visible writeback only where the guest actually reads those bytes,
   deferred and batched, and signal PPF on the ordered dependency boundary
   rather than after a full queue idle. Whether the translated title reads the
   copy destination through host memory is the AOT-level question: if the
   lowered code consumes it as a host pointer, the copy cannot become GPU-owned
   without changing that materialisation.
4. Verification: transfer/submission/bridge/savestate suites and the
   byte-exact tiling oracles stay green; the flashback captures must not regress;
   transfer submission time must drop from the recorded tens of seconds.
   The host-read question that gates step 3 is now measured rather than assumed
   (section 7.4 and `TRIAEVUM_GPU_COPY_PATH_PLAN.md` section 6): with an opt-in
   range watch that traps the copy destination on the checked path, the
   revelation window shows zero typed, byte or write accesses from the guest and
   only our own sampler traffic reading it (`pointer_reads`), with the copy's own
   writeback spans as the positive control that the trap was live. What remains
   unobservable is the moment a live pointer is handed out, which is why that
   event is what the counter records.
   Current state after the readback fix: ten PICA suites green
   (`transfer`, `vulkan_bridge`, `visual_savestate`, `semantic_trace`, `texture`,
   `composition`, `submission`, `vulkan_plan`, `azahar_capture`,
   `graphics_foundation`), 12 flashback captures byte-identical, boot 5/5
   hash-exact, file-select 6/6 identical to the qualified baselines, and
   transfer submission time at 1.83 ms/frame from 6.95.


## 8. Residual limits

- The four hard-failure branches of the raw-copy path are unchanged. A copy
  whose source is not a registered render target, or whose destination aliases
  an active attachment, still ends the run explicitly rather than falling back
  to the CPU executor. If a user reports a crash (process exit, stderr line
  `oot3d_native_game: native PICA …`) rather than a frozen window, that path is
  the candidate and the discriminating artifact is their `runtime-data.json`
  plus stderr.
- The A32 native-execution suites were not exercised: this tree has no A32 AOT
  generated inputs. `tools/oot3d/build_fast_dev.ps1` regenerates them and
  reconfigures; the two suites should be re-run in a complete tree.
- Three tests in the untracked, concurrently authored
  `tools/oot3d/native_a32_runtime/test_whole_aot_memory_regions.py` fail because
  the file calls `_render_region_function` with an argument the tracked generator
  no longer takes. That module is not part of this repository's history; it is
  the parallel AOT work the handover asks not to absorb. Resolve its owner
  before folding it in.
- `oot3d_toon_outline_gpu_tests` fails on this host with an unchanged test and
  unchanged target code; it needs a second host (or the standalone
  `run_grass_outline_tests.ps1` path) before it is called a source defect.
- Linux and Steam Deck were not exercised in this session. The corrections are
  platform-neutral C++ and one Python helper, and the release Python suite runs
  on the same code on either host, but that is not a Linux qualification.
- No frame-rate, stutter or memory claim is made. The live runs above are
  bounded functional checks with synchronous captures.
- The F1 correction changes when the mouse-capture side effect is applied: it
  follows the next `DrawMenu` frame after the key event instead of the same
  frame. No behavioural difference was observed in the captures; it is stated
  because it is a real ordering change.

### 8.1 Checks that need the Linux host

The Linux host (`<linux-host>`) did not answer in this session, so no
AppImage was mounted, launched or re-packaged here. The corrected Python path is
platform-neutral and is covered by the suite, but that is not a Linux
qualification. Ordered checks for the next Linux session:

1. Confirm the persistent root: run the AppImage once, then inspect
   `$XDG_DATA_HOME/TriAevum` (or `~/.local/share/TriAevum`) for
   `active-title.json`, `data/titles`, the private module and `savedata`, and
   confirm nothing was written under `usr/lib/triaevum` inside an extracted or
   mounted AppDir.
2. Confirm the entry: launch the AppImage with no arguments (desktop or shell),
   with the AppImage path as an extra argument, and with `--forge`; each must
   reach the Forge or the game, never exit with the usage error.
3. Controllers: the correction now opts in to Steam Input's virtual gamepad, so
   confirm the pad appears and drives the game under a real Steam session
   (Gaming Mode or Desktop with Steam Input enabled):
   - in game, F1, Controls, Devices must list the pad; the previous behaviour
     was an empty list;
   - if it is still empty, distinguish the remaining causes in this order:
     `ls /dev/input/js*` and `grep -i "Steam Deck\|Virtual" /proc/bus/input/devices`
     (does the kernel see any pad?), `LD_DEBUG=libs ./TriAevum-*.AppImage 2>&1 |
     grep -i "udev\|SDL"` plus a listing of `usr/lib/triaevum/lib` (does a
     bundled `libudev` shadow the host copy through `AppRun`'s
     `LD_LIBRARY_PATH`?), and `strings usr/lib/triaevum/lib/libSDL2*.so* |
     grep -i udev` (was SDL built with its Linux udev backend at all?);
   - check the runtime log for
     `not recognized as gamepad ... gamecontrollerdb.txt`, which separates
     "enumerated but unmapped" from "not enumerated";
   - if the pad is listed but inert, verify the Steam controller layout for the
     non-Steam shortcut is a Gamepad template, not a keyboard/mouse one, and
     check F1, Controls, Bindings.
4. Re-run one bounded Windows/Linux parity capture (boot, advance Kokiri) to
   confirm the corrections did not change the native composition.

## 9. Files changed by this work

| File | Change |
| --- | --- |
| `tools/triaevum_release/common.py` | Short fixed temporary name in `atomic_write_bytes`. |
| `tools/triaevum_release/test_common.py` | New: legacy path-capacity regression test. |
| `runtime/three_ds_recomp/src/fast/Fast3dGui.cpp` | F1 toggled from the key event; sampled poll removed. |
| `runtime/three_ds_recomp/include/fast/Fast3dGui.h` | `ToggleOot3dGraphicsWindow` declared; latch member removed. |
| `tools/oot3d/native_game_runtime/oot3d_native_pica_visual_frame.{h,cpp}` | `HasPendingRawCopyCompletion`. |
| `tools/oot3d/native_game_runtime/oot3d_native_pica_presentation_scheduler.{h,cpp}` | Delegating query for the dependency gate. |
| `tools/oot3d/native_game_runtime/oot3d_native_a32_window.cpp` | Gate derived from persisted state; window-local set removed. |
| `tools/oot3d/native_game_runtime/oot3d_native_pica_visual_savestate_tests.cpp` | Pending raw copy round-trip and single discharge. |
| `runtime/three_ds_recomp/include/ship/controller/physicaldevice/SDLControllerSetup.h` | Opts in to Steam Input's virtual gamepad (Linux/SteamOS controller support). |
| `tools/three_ds/input/three_ds_sdl_controller_tests.cpp` | Guards the opt-in and that an explicit user value survives it. |
| `tools/triaevum_release/host_layout.py` | AppImage layout detected from the payload path, not only `APPDIR`. |
| `tools/triaevum_release/desktop_launcher.py` | Opened-file arguments ignored; unknown options still rejected. |
| `tools/triaevum_release/appimage/AppRun` | Reports a missing or non-executable bundled Forge. |
| `tools/triaevum_release/test_host_layout.py` | New: AppImage layout without `APPDIR`, similar-name package, opened-file argument, unknown option. |
| `runtime/three_ds_recomp/include/fast/renderer3ds/pica_composition.h` | New `Screen` composition domain (screen-space frame content). |
| `runtime/three_ds_recomp/include/fast/renderer3ds/pica_ui_canvas.h` | `Screen` covers the presentation extent; placed UI keeps the native fit. |
| `runtime/three_ds_recomp/include/oot3d/renderer/ui_presentation_layout.h` | `FillUiPresentation` (cover without distortion) beside the existing fit. Since 2026-10-05 it serves only overlays with their own alpha; movie frames take the fit with black bars (§7.3). |
| `runtime/three_ds_recomp/include/fast/oot3d/pica_scene_frame.h` | `HasSceneDrawOnColorTarget` and the screen-domain statistic. |
| `runtime/three_ds_recomp/src/fast/oot3d/pica_scene_frame.cpp` | Frame evidence query and domain counting. |
| `runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica.cpp` | Resolves the per-draw composition domain from the frame's scene target. |
| `runtime/three_ds_recomp/tests/oot3d_graphics_foundation_tests.cpp` | Canvas policy per composition domain. |
| `runtime/three_ds_recomp/tests/oot3d_pica_scene_frame_tests.cpp` | Frame scene-target evidence. |
| `runtime/three_ds_recomp/include/fast/backends/gfx_vulkan.h` | `RawCopyTiming` attribution counters, persistent mapped readback staging and its coherence flag, `FindReadbackMemoryType`/`CreateReadbackBuffer`. |
| `runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_texture_copy.cpp` | Readback moved in-band onto the copy's own submission (barriers, image-to-buffer, cached staging, explicit invalidate when not coherent); per-phase timing. |
| `runtime/three_ds_recomp/src/fast/backends/gfx_vulkan.cpp` | Cached readback memory-type selection and `TRIAEVUM_RAW_COPY` shutdown receipt; staging destroyed with the copy fence. |
| `runtime/three_ds_recomp/include/fast/renderer3ds/pica_lighting_program.h` | Title-neutral: keeps the std140 program and GLSL; the builder and the title decoder include moved out. |
| `runtime/three_ds_recomp/include/fast/oot3d/pica_fragment_lighting.h` | `Fast::Oot3d::BuildPicaLightingProgram` packs the decoded native state into the shared program. |
| `runtime/three_ds_recomp/include/fast/renderer3ds/pica_ui_canvas.h` | Title-neutral: keeps `PicaRasterCanvas` and clipping; the domain resolver moved out. |
| `runtime/three_ds_recomp/include/oot3d/renderer/ui_presentation_layout.h` | `Oot3d::Renderer::ResolvePicaRasterCanvas` applies this title's presentation convention. |
| `runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica.cpp` | Resolver call through `::Oot3d::Renderer`. |
| `runtime/three_ds_recomp/tests/CMakeLists.txt` | `oot3d_pica_scene_frame_tests` links `ThreeDsRecomp::Runtime` so the suite can build. |
| `runtime/three_ds_recomp/tests/oot3d_pica_scene_frame_tests.cpp` | Declared uniform buffer covers the declared slices. |
| `tools/renderer/tev_program/lighting_gpu_tests.cpp` | Explicit title-adapter include for the moved builder. |
| `tools/triaevum_release/tests/run_f1_settings_smoke.ps1` | Links the configured archive directory from the build cache instead of stale build-tree copies. |
| `runtime/three_ds_recomp/include/fast/renderer3ds/pica_framebuffer_encode_gpu.h` | Compute shader reproducing the native packing on R32_UINT words, title-neutral, with a test that compiles it offline. |
| `runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_texture_copy.cpp` | GPU encode resources, dispatch and staging copy replace the CPU encode in the copy path; the oracle stays behind the differential switch. |
| `runtime/three_ds_recomp/include/fast/backends/gfx_vulkan.h` | Encode resource members and the capability/limit validation entry points. |
| `tools/oot3d/native_game_runtime/oot3d_native_a32_memory.{h,cpp}` | Opt-in range watch: trapped pages lose their fast entry, per-origin counters, suppression for the renderer's own materialization. Inert when unused. |
| `tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_host_tests.cpp` | Proves the watch keeps values identical and counts typed, byte, pointer and suppressed traffic. |
| `tools/oot3d/native_game_runtime/oot3d_native_a32_window.cpp` | Watches the translated raw-copy destination under extended diagnostics and prints `TRIAEVUM_RAW_COPY_HOST_ACCESS`. |
| `tools/oot3d/operational_inputs.json` | Adds the paths found in this pass (generated translated code, savestates, structural scenario catalog, release corresponding source) as 22 entries and 10 variables, plus two probe commands and the known absence of a regenerated tree in the build; validated with `Test-Oot3dOperationalInputs.ps1`, no new failures. |
| `docs/OOT3D_WORKSPACE_PATHS.md` | Narrative map of those paths, with the recipe for moving between situations (including the quiescent anchor the structural scenarios require) and the measured memory-access shape of the generated title code. |
