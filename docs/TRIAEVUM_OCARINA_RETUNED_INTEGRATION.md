# Ocarina ReTuned integration record

Ocarina ReTuned (<https://github.com/OTPR26/Ocarina-ReTuned>, OTPR26) is a fork
of TriAevum pinned to `17218b0` (beta.1.1). It ships Windows, macOS (Apple
silicon), Linux, Android, iOS and tvOS previews of OoT3D with the Ocarina
ReFramed single-screen HUD. The fork keeps no modified copy of the runtime: its
CI checks out TriAevum at the pinned revision and applies Python patch scripts
(`tools/native/apply_*.py`, `tools/reframed/apply_*.py`,
`tools/frontend/apply_*.py`) before building. This record lists which of those
changes TriAevum adapted back on 2026-10-23, how they were verified, and which
were deliberately left out.

Provenance: the adapted hunks are modifications of a GPL-3.0 program and are
distributed under this repository's license; two of the fork's own files carry
`SPDX-License-Identifier: GPL-2.0-or-later`. Credit is recorded in
`THIRD_PARTY_NOTICES.md` and `docs/TRIAEVUM_CONTRIBUTIONS.md`. The fork's
repository has no top-level LICENSE file of its own.

## Adopted

Every item was rewritten against the current tree (the fork's contexts were
written for `17218b0`), guarded where TriAevum's product behaviour must not
change, and verified at its owning boundary.

### Standard library (libc++ on Android and Apple, MinGW)

| Change | Files | Why |
| --- | --- | --- |
| Float `std::from_chars` only when `__cpp_lib_to_chars` is available; otherwise a classic-locale `istringstream` | `pica_shader_instrumentation.cpp` | libc++ lacks floating-point `from_chars` on the Android NDK and older Apple SDKs |
| `std::chrono::clock_cast` only when `__cpp_lib_chrono >= 201907`; otherwise offset the file time onto the system clock | `oot3d_savestate_panel.cpp` | libc++ has no `clock_cast` |
| File-time stamp cast to `long long` before `std::to_string` | `oot3d_scene_environment.cpp` | libc++'s `file_time_type::rep` is `__int128` |
| Explicit `<cstdlib>`/`<cstdio>` includes | `StringHelper.cpp`, `DisplayList.cpp` | Transitive includes differ between standard libraries |
| `spirv_pack.cmake` resolves its helper through `CMAKE_CURRENT_FUNCTION_LIST_DIR` | `runtime/three_ds_recomp/cmake/spirv_pack.cmake` | A variable set at include time is not visible when the function is called from another directory scope |

### Windows with the GNU driver (llvm-mingw)

| Change | Files |
| --- | --- |
| MSVC linker switches (`/DEBUG`, `/OPT`, `/PDB`, `/MAP`) only when `MSVC` is true | `CMake/oot3d/game.cmake` |
| OpenGL loader header selects GLEW on every Windows driver | `gfx_opengl.h` |
| `SDL.h` included for the `SDL_main` rename on Windows without `_MSC_VER` (MSVC and clang-cl unchanged) | `oot3d_native_game.cpp` |
| `ksuser` linked for MinGW ImGui/SDL audio | `cmake/dependencies/windows.cmake` |
| vcpkg triplet `x64-mingw-static` when `MINGW` | `CMakeLists.txt` |
| Title adapter ABI prototypes carry `TRIAEVUM_TITLE_AOT_EXPORT` when the macro is defined before inclusion | `triaevum_title_*_abi.h`, `triaevum_title_whole_aot_plugin.cpp` |

### macOS (not verified on Apple hardware in this tree)

Compiled in the fork's `macos-15` CI against `17218b0`; the adaptation compiles
on Windows and Linux because every hunk is under `__APPLE__` or `APPLE`.

| Change | Files |
| --- | --- |
| `VK_KHR_portability_enumeration` on the instance and `VK_KHR_portability_subset` on the device when present (MoltenVK) | `gfx_vulkan_device.cpp` |
| Title module `triaevum_title_aot.dylib` beside the executable, located with `_NSGetExecutablePath`; `dlopen` path enabled for Apple | `oot3d_native_direct_aot.cpp` |
| The stub title dylib is not linked on Apple (no install name/rpath; the module is always `dlopen`ed) | `game.cmake`, `host_services.cmake` |
| Default audio backend SDL on Apple | `Audio.cpp` |
| Metal shader table namespace fix | `gfx_metal_shader.cpp` |
| Render into drawable pixels on HiDPI (SDL reports points) | `oot3d_native_a32_window_frame.cpp` |
| `enable_language(OBJC OBJCXX)` | `runtime/three_ds_recomp/CMakeLists.txt` |

### Android (not verified on a device in this tree)

| Change | Files |
| --- | --- |
| The Activity surface is always fullscreen: `SetFullscreenImpl` forces on, window flags request fullscreen | `gfx_sdl2.cpp` |
| Presentation settings on Android are window mode 1 at the surface size | `gfx_vulkan_frame.cpp` |
| Test registration rooted at `CMAKE_CURRENT_SOURCE_DIR` so the engine can be embedded as a subdirectory | `CMake/oot3d/tests.cmake` |

### Renderer (shared)

| Change | Files | Verification |
| --- | --- | --- |
| PICA exclude scissor (mode 1): the visible region becomes up to four disjoint rectangles and the draw is submitted once per rectangle; `fullTargetRaster` is false for such draws | `pica_exclude_scissor.h` (new), `gfx_vulkan_pica_draw.cpp` | `pica_exclude_scissor_tests` (exact coverage, no overlap); OoT3D never emits mode 1 (the previous code threw), so desktop captures are unchanged |
| Native vertex formats the GPU cannot bind no longer abort pipeline creation while the NRI path repacks them; the legacy Vulkan pipeline still rejects them when it is actually needed | `gfx_vulkan_pica_pipelines.cpp` | Desktop GPUs support every format, so no behaviour change there |

The OpenGL PICA backend keeps throwing on mode 1; it is not a product path.

### CTR kernel and services

| Change | Files | Verification |
| --- | --- | --- |
| `svc QueryMemory` (0x02): the fork's version was adapted first; the unified tree keeps the Moonfall Curse implementation (`HandleQueryMemory`, per-region kernel memory states, merged touching mappings), which the MM3D title exercises | `oot3d_native_a32_memory.{h,cpp}`, `oot3d_native_a32_ctr_host.cpp` | `TestQueryMemory` |
| `svc OutputDebugString` (0x3D) records up to 1024 bytes in the SVC event log; unmapped bytes fault | `oot3d_native_a32_ctr_host.cpp` | `TestOutputDebugString` |
| New 3DS `ir:rst` service (GetHandles, Initialize, Shutdown; shared-memory ring of eight C-stick/ZL/ZR samples) behind `NativeA32CtrHostConfig::IrstService`, off by default; `WriteIrstSample` publishes samples | `oot3d_native_a32_ctr_host*.{h,cpp}` | `TestIrstService`, `TestIrstServiceAbsentByDefault` |

Nothing publishes ir:rst samples yet: a title adapter that wants the native
C-stick (MM3D camera, modded OoT3D) opts in and writes them from its frame.

### ARM translation

| Change | Files | Verification |
| --- | --- | --- |
| `VCMP{E}.F64 Dd, #0.0`, `VCMP{E}.F32 Sd, #0.0`, `VCVT.F64.U32` in the translator and the software binary64 runtime | `whole_aot_cpp.py`, `upstream/recomp/a32_vfp_binary64.cpp` | `oot3d_native_a32_vfp_binary64_tests` (new), `test_whole_aot_cpp.py`; the OoT3D product whole-AOT cache contains none of these encodings, so its generated code is unchanged |

## Not adopted

| Fork change | Reason |
| --- | --- |
| On-device / first-run ROM preparation that translates and compiles the title on the user's machine (`rom_setup.py`, Android `RomPreparationService`, bundled llvm-mingw toolchain) | Contradicts the public release invariant: Forge's installation path must not generate IR, compile or link; TriAevum ships catalogued precompiled title modules (`docs/TRIAEVUM_PRECOMPILED_RELEASE.md`) |
| Removal of the built-in shader packs ("locally generated shaders") | TriAevum's packs are a product decision documented in the precompiled-release model and the shader corpus work |
| ReTuned startup/settings interface, blue style, interface fonts, ReTuned Dialogue fonts, texture-alias loading | Product UI; TopScreen follows SingleScreen 3.1 only |
| `Reframed` UI profile, writable HID shared memory, `svc 0x7D/0x7E` frontend hooks, Reframed patch translation | TriAevum reimplements the Reframed file selection as typed host code and does not run the Reframed binary patch |
| HD-font linear sampling override | TopScreen owns font presentation |
| SDL audio ring buffer with fades and underrun counter, per-profile refresh limit | Candidate for a separate change on the SDL (Linux) backend after an audio qualification on the Linux AMD reference machine; not part of this record |
| Mobile texture-memory budgets | TriAevum's VRAM residency work (#98) owns texture budgets |
| iCloud save sync, Apple touch controls, Siri Remote navigation, Android touch overlay | Platform products TriAevum does not ship |
| GitHub Actions workflows | TriAevum releases are built and qualified locally; the workflows remain a reference for a macOS or Android CI |

## Packaging references kept outside the tree

The fork's packaging scripts were not copied; they are the reference when those
platforms are qualified:

- macOS bundle: `tools/native/package_macos.py` (dylibbundler, MoltenVK ICD
  under `Resources/vulkan/icd.d`, `vtool` deployment check per Mach-O,
  ad-hoc `codesign`, `ditto` archive) and `macos_launcher.cpp` (re-exec setting
  `VK_ICD_FILENAMES` and `DYLD_LIBRARY_PATH`).
- Linux audio: `stage_desktop_host.py` bundles the PipeWire client library,
  its modules and SPA plugins beside the executable and the launcher exports
  `SDL_AUDIODRIVER=pipewire` when the user session exposes `pipewire-0`.
  TriAevum's AppImage and Flatpak rely on the system audio stack instead.
- Portable Windows toolchain: `stage_windows_toolchain.py` pins llvm-mingw,
  CMake and Ninja release archives by SHA-256 and `windows-toolchain.cmake`
  maps source prefixes; `fix_windows_fmt.py` backports the fmt 11.0.2
  format-checker fix for Clang.
- Android: `stage_android_host.py` checks AArch64, 16 KiB load alignment and
  the NEEDED closure of every packaged library; `prepare_android_app.py`
  adjusts `ports/android`.
