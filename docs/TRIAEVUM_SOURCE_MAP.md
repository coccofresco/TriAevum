# TriAevum source map

Where the large components live after the October 2026 reorganization, how
they are split, and how to read older documents that cite the files as they
were before. The reorganization moved code without changing it: every step was
gated by deterministic captures, the structural `runtime.json` comparison, the
host test suite and a development-mode compile (see each commit message).

## Build files

| File | Contents |
| --- | --- |
| `CMakeLists.txt` | Project setup, options, the `triaevum_module` and `three_ds_recomp` runtime subdirectories, then the area files below, in this order. |
| `CMake/oot3d/native_runtime.cmake` | Application host and audio libraries, the A32 runtime and its whole-function, mass and true AOT variants, typed gameplay, the A32 process, their tests. |
| `CMake/oot3d/pica.cmake` | Process image, native PICA frontend, service, transfer, submission, composition, Vulkan plan and bridge, semantic trace, Azahar capture, shader packs, AOT shader compiler, PICA renderer and module host. |
| `CMake/oot3d/ui_input.cmake` | UI contract, TopScreen, N64 UI, lifecycle bridge, UI textures and fonts, input and control configuration. |
| `CMake/oot3d/host_services.cmake` | The full and PICA-only CTR service hosts, the PICA client bridge, the title AOT stub and game module, savestate, boot probe. |
| `CMake/oot3d/game.cmake` | Game runtime, the `oot3d_native_game` executable (`TriAevum.exe`) and its link options, crash diagnostics, CPU phase probe, Vulkan runtime deployment. |
| `CMake/oot3d_target_defaults.cmake` | `oot3d_target_defaults(<target> [NO_WARNING_OVERRIDE] [FOLDER <f>])`: warning override, NOMINMAX and the static MSVC runtime on Windows, IDE folder. |

The area files are `include()`d, so they run in the root's scope and resolve
relative paths from the repository root. The runtime's backends
(`runtime/three_ds_recomp/src/fast/backends/*.cpp`) are globbed; a new backend
file needs no CMake edit.

## Native A32 window (the game loop)

`RunOot3dNativeA32Window` creates a `NativeA32WindowSession` and calls `Run()`:
`Setup()`, then `RunFrame()` until the window closes or the guest terminates,
then `Finish()`. The members are the former locals of the function, in their
original order and with their names. An object built with arguments lives in
`std::optional<T> <name>Storage` and is emplaced where the local was declared;
the window and the rendering API are `<name>Pointer`. Methods bind `<name>` on
entry.

| File (`tools/oot3d/native_game_runtime/`) | Contents |
| --- | --- |
| `oot3d_native_a32_window_session.h` | The session class, the helpers its files share, the A32 block trace writer. |
| `oot3d_native_a32_window.cpp` | `Run()`, `Finish()`, the guest-run helpers, `RunOot3dNativeA32Window`. |
| `oot3d_native_a32_window_setup.cpp` | `Setup()`: process image, host services, renderer, UI, initial state; small setup methods. |
| `oot3d_native_a32_window_frame.cpp` | `RunFrame()`: input, pacing and the guest refreshes, each `SampleGuestRefreshInput`, `UpdateGuestServicesForRefresh` (TopScreen routing, scenario, guest clock, HID, DSP audio), `RunGuestRefresh` (guest run, PICA drain) and, on the last one, `PresentVisualFrame`; then UI and present. Also the renderer-side methods these schedule. |
| `oot3d_native_a32_window_savestate.cpp` | `SaveState()`, `LoadState()` and the restore steps. |
| `oot3d_native_a32_window_summary.{h,cpp}` | The `runtime.json` document written at the end of a run. |
| `oot3d_native_a32_window_shared.h`, `_stats.h` | Title constants, window helper declarations, run statistics. |
| `oot3d_native_a32_candidate_dispatch.cpp`, `_projection_policy.cpp`, `_window_input.cpp` | Native candidate dispatch, widescreen projection policy, input polling. |

## Vulkan backend (`runtime/three_ds_recomp/src/fast/backends/`)

| File | Contents |
| --- | --- |
| `gfx_vulkan.cpp` | The Fast3D rendering API: shaders, textures, samplers, state, draws, framebuffers, ImGui. |
| `gfx_vulkan_lifecycle.cpp` | Construction, `Init`, `Shutdown`, the recorded DLSS feature. |
| `gfx_vulkan_frame.cpp` | Presentation settings, `StartFrame`, `EndFrame`, `FinishRender`. |
| `gfx_vulkan_device.cpp` | Instance, devices, pipeline cache, presentation worker, command submission, swapchain. |
| `gfx_vulkan_shaders.cpp` | Shader programs, SPIR-V compilation, native PICA AOT and uber programs, shader modules. |
| `gfx_vulkan_resources.cpp` | Frame resources, Shadow2D targets, buffers, texture images, descriptor sets, pipelines. |
| `gfx_vulkan_pica.cpp` | Native PICA: shader and geometry resources, render passes and targets, display images, memory fills, descriptor sets. |
| `gfx_vulkan_pica_textures.cpp` | Native PICA texture records, storage, uploads, materials. |
| `gfx_vulkan_pica_pipelines.cpp` | Native PICA shader modules, programs, pipelines, prewarm. |
| `gfx_vulkan_pica_draw.cpp` | Draw submission, composition anchors, interactive grass, shadow maps. |
| `gfx_vulkan_pica_display.cpp` | Display transfers, the display-effect graph, scene composition for copies, clears, overlays. |
| `gfx_vulkan_pica_state.cpp` | Savestate capture and restore of textures, targets and presentation state; resets. |
| `gfx_vulkan_pica_texture_copy.cpp`, `_attachments.cpp` | Texture copies, attachments (unchanged). |

`gfx_vulkan_internal.h` and `gfx_vulkan_pica_internal.h` are private to their
units: the includes and the helpers the files share, in an anonymous namespace
so each unit keeps its own copy as when they lived in one file.

## Native audio (`tools/oot3d/native_game_runtime/`)

| File | Contents |
| --- | --- |
| `oot3d_native_audio.cpp` | BCWAV, BCSTM and BCSAR decoding and sequence resolution (with the Citra/Azahar DSP-ADPCM notice). |
| `oot3d_native_audio_profiles.cpp` | Envelope, modulation, mix, pan and filter profiles read from code.bin. |
| `oot3d_native_audio_mixer.cpp` | The sequence mixer (`NativeAudioMixer`). |

## CTR host services (`tools/oot3d/native_game_runtime/`)

`NativeA32CtrHostServices::HandleSvc` dispatches SVCs; `SendSyncRequest`
calls one `Handle<Service>Request` method per client session (srv: stays
inline).

| File | Contents |
| --- | --- |
| `oot3d_native_a32_ctr_host.cpp` | SVC dispatch, handles and kernel objects, GSP framebuffers and interrupts, memory, timing. |
| `_ctr_host_system.cpp` | ndm:u, cfg:u, y2r:u, APT:U. |
| `_ctr_host_hid.cpp` | hid:USER and the HID shared-memory samples. |
| `_ctr_host_gsp.cpp` | gsp::Gpu. |
| `_ctr_host_dsp.cpp` | dsp::DSP. |
| `_ctr_host_fs.cpp` | fs:USER and RomFS files. |
| `_ctr_host_state.cpp` | Savestate capture and restore of the service state. |
| `_ctr_host_internal.h` | Service constants, shared helpers, the RomFS backing file. |

## TopScreen mod profile (`tools/oot3d/ui_topscreen/`)

| File | Contents |
| --- | --- |
| `oot3d_top_screen_mod_profile.cpp` | UI profiles, verified layout and runtime geometry, control-flow and float-load contracts, title logo and camera scalars. |
| `_mod_profile_items.cpp` | Item lanes, queries and selection. |
| `_mod_profile_hud.cpp` | HUD and touch geometry and presentation. |
| `_mod_profile_quest.cpp` | Quest and system menu geometry, model transforms, pause projection. |
| `_mod_profile_pause.cpp` | Pause inputs, routes and redraws, renderer visibility and fades, pause controller, alternate renderers, child suppression. |
| `_mod_profile_internal.h` | Verified layout tables and shared helpers. |

## iMMERSE module (`runtime/three_ds_recomp/src/fast/immerse/`)

MXAO from Pascal Gilcher's iMMERSE is a module under its own licence, not the
GPL: all rights remain with the author, the original iMMERSE licence applies,
and TriAevum uses it under the author's explicit permission, granted
exclusively to the project (`LICENSE.md` in the directory, `LICENSE_SCOPE.md`).
Everything derived from MXAO lives in that directory and nowhere else.

| File | Contents |
| --- | --- |
| `mxao_shader.{h,cpp}`, `mxao_module.cpp` | The ported programs, their NRI execution and the texts of the F1 controls. |
| `mxao_pass.spvpack`, `mxao_pass_binaries.{h,cpp}` | The precompiled programs. |
| `immerse.cmake`, `tests/gpu_tests.cmake` | The module library `triaevum_immerse`, GPU tests and `build_immerse_pass_artifacts`. |
| `README.md`, `LICENSE.md`, `LICENSE-iMMERSE.txt` | The port's documentation, the exception, the original licence. |

The module is a separate shared library (`triaevum_immerse.dll`,
`libtriaevum_immerse.so`) beside the runtime. TriAevum reaches it only through
the MIT interface `runtime/three_ds_recomp/module_interface/triaevum/immerse_module.h`:
`fast/oot3d/immerse_module_loader.cpp` loads it, `fast/oot3d/mxao_pass.cpp`
serves it the renderer's resources and `fast/oot3d/mxao_settings_panel.cpp`
draws its controls. It is built with `THREE_DS_RECOMP_ENABLE_IMMERSE` (on with
NRI); without the library MXAO is unavailable and a configuration selecting it
renders AO Off.

## Reading older documents

Documents written before October 2026 cite `file.cpp:line` positions and file
names of the code before it was split. Find the function by name; it moved
whole and unchanged:

| Cited file | Now |
| --- | --- |
| `oot3d_native_a32_window.cpp` | the window files above (setup, frame, savestate, summary, input, projection policy, candidate dispatch) |
| `gfx_vulkan_pica.cpp` | `gfx_vulkan_pica*.cpp` |
| `gfx_vulkan.cpp` | `gfx_vulkan*.cpp` (not `_pica`) |
| `oot3d_native_a32_ctr_host.cpp` | `oot3d_native_a32_ctr_host*.cpp`; the service blocks of `HandleSvc` are `Handle<Service>Request` |
| `oot3d_top_screen_mod_profile.cpp` | `oot3d_top_screen_mod_profile*.cpp` |
| `oot3d_native_audio.cpp` | `oot3d_native_audio.cpp`, `_profiles.cpp`, `_mixer.cpp` |
| root `CMakeLists.txt` target blocks | `CMake/oot3d/*.cmake` |
| `fast/oot3d/mxao_pass.cpp` (before 2026-10-04), `include/fast/immerse/*`, `tev_program/mxao_gpu_tests.cpp`, `DrawMxaoSettings` in the lighting panel | `runtime/three_ds_recomp/src/fast/immerse/` |
| `fast/immerse/mxao_pass.cpp`, `fast/immerse/mxao_settings_panel.cpp`, `fast/oot3d/immerse_unavailable.cpp` (before 2026-10-12) | `fast/immerse/mxao_module.cpp` (the module library); TriAevum's side in `fast/oot3d/mxao_pass.cpp`, `mxao_settings_panel.cpp`, `immerse_module_loader.cpp` |
| `build_pass_artifacts --immerse` | `build_immerse_pass_artifacts` |
| "iMMERSE MXAO" sections of `OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md` | `runtime/three_ds_recomp/src/fast/immerse/README.md` |

The source-native study, the legacy sandbox and asset-tool group A were removed
in the same reorganization (phase 2); documents describing them are history.
