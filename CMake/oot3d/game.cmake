# The game: the native game runtime, the oot3d_native_game executable (TriAevum) and its link options,
# crash diagnostics, the CPU phase probe option and the Vulkan runtime deployment.
# Included from the root CMakeLists.txt, in its scope.

add_library(oot3d_native_game_runtime STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_scene_view.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_timing_probe.cpp
    tools/oot3d/native_game_runtime/oot3d_native_scenario_bootstrap.cpp
    tools/oot3d/native_game_runtime/oot3d_native_frame_rate.cpp
    tools/oot3d/native_game_runtime/oot3d_native_presentation_pacer.cpp
    tools/oot3d/native_game_runtime/oot3d_native_temporal_event_ledger.cpp
    tools/oot3d/native_game_runtime/oot3d_native_player_temporal_bridge.cpp
    tools/oot3d/native_game_runtime/oot3d_native_root_motion.cpp
    tools/oot3d/native_game_runtime/oot3d_native_skel_anime.cpp
)
target_include_directories(oot3d_native_game_runtime PUBLIC
    tools/oot3d/decomp_support/include
    tools/oot3d/native_demo_host
    tools/oot3d/native_game_runtime
)
target_compile_options(oot3d_native_game_runtime PRIVATE "${WARNING_OVERRIDE}")
target_link_libraries(oot3d_native_game_runtime PUBLIC
    oot3d_native_audio
    oot3d_native_a32_process
)
if (TARGET oot3d_native_a32_aot)
    target_link_libraries(oot3d_native_game_runtime PUBLIC oot3d_native_a32_aot)
    target_compile_definitions(oot3d_native_game_runtime PRIVATE
        OOT3D_NATIVE_A32_AOT_AVAILABLE=1)
endif()
oot3d_target_defaults(oot3d_native_game_runtime NO_WARNING_OVERRIDE FOLDER Tools)

add_executable(oot3d_native_game_runtime_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_frame_rate_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_temporal_event_ledger_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_player_temporal_bridge_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_scene_view_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_timing_probe_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_root_motion_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_skel_anime_tests.cpp
)
target_link_libraries(oot3d_native_game_runtime_tests PRIVATE oot3d_native_game_runtime)
oot3d_target_defaults(oot3d_native_game_runtime_tests FOLDER Tests)

# MM3D adapter read/lifetime guards; no process, renderer or ROM needed.
add_executable(mm3d_scene_environment_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/mm3d_scene_environment_tests.cpp)
target_include_directories(mm3d_scene_environment_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_game_runtime")
oot3d_target_defaults(mm3d_scene_environment_tests FOLDER Tests)

# Clock verification must also work without the optional legacy cutscene host.
add_executable(triaevum_presentation_clock_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_frame_rate_tests.cpp
    tools/triaevum_release/tests/presentation_clock_main.cpp)
target_link_libraries(triaevum_presentation_clock_tests PRIVATE oot3d_native_game_runtime)
oot3d_target_defaults(triaevum_presentation_clock_tests NO_WARNING_OVERRIDE FOLDER Tests)

add_executable(oot3d_native_audio_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_audio_tests.cpp
)
target_link_libraries(oot3d_native_audio_tests PRIVATE oot3d_native_audio)
oot3d_target_defaults(oot3d_native_audio_tests FOLDER Tests)

if(ANDROID)
    add_library(oot3d_native_game SHARED EXCLUDE_FROM_ALL)
else()
    add_executable(oot3d_native_game EXCLUDE_FROM_ALL)
endif()
target_sources(oot3d_native_game PRIVATE
    tools/oot3d/native_game_runtime/oot3d_native_game.cpp
    tools/oot3d/native_game_runtime/triaevum_product_info.cpp
    tools/oot3d/native_game_runtime/oot3d_native_game_bootstrap.cpp
    tools/oot3d/native_game_runtime/oot3d_native_game_launch_profile.cpp
)
if(WIN32)
    target_sources(oot3d_native_game PRIVATE
        tools/oot3d/native_game_runtime/triaevum_windows.manifest)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Relocated packages must resolve the bootstrap library beside the runtime,
    # never accidentally fall back to the publisher's build directory.
    set_target_properties(oot3d_native_game PROPERTIES
        BUILD_RPATH_USE_ORIGIN TRUE
        BUILD_RPATH "$ORIGIN;$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN;$ORIGIN/lib")
endif()
set(TRIAEVUM_PRODUCT_PLUGIN 0)
if(OOT3D_DIRECT_AOT_PLUGIN)
    set(TRIAEVUM_PRODUCT_PLUGIN 1)
    set_property(TARGET oot3d_native_game PROPERTY OUTPUT_NAME TriAevum)
    add_custom_target(triaevum_public_runtime
        DEPENDS oot3d_native_game triaevum_title_aot_stub)
    # The separately licensed iMMERSE module, a library of its own beside the
    # runtime (runtime/three_ds_recomp/src/fast/immerse/LICENSE.md).
    set(_triaevum_immerse_target "")
    if(TARGET triaevum_immerse)
        add_dependencies(triaevum_public_runtime triaevum_immerse)
        set(_triaevum_immerse_target ",\n  \"immerse\": \"$<TARGET_FILE:triaevum_immerse>\"")
    endif()
    # The separately licensed DLSS module with NVIDIA's DLSS runtime and the
    # NGX SDK licence (runtime/three_ds_recomp/src/fast/dlss/LICENSE.md), and
    # FidelityFX's FSR runtime (MIT), each beside the runtime.
    if(TARGET triaevum_dlss AND THREE_DS_RECOMP_DLSS_VK_DLL)
        add_dependencies(triaevum_public_runtime triaevum_dlss)
        string(APPEND _triaevum_immerse_target
            ",\n  \"dlss\": \"$<TARGET_FILE:triaevum_dlss>\""
            ",\n  \"dlss_runtime\": \"${THREE_DS_RECOMP_DLSS_VK_DLL}\""
            ",\n  \"dlss_license\": \"${THREE_DS_RECOMP_NGX_SOURCE_DIR}/LICENSE.txt\"")
    endif()
    if(THREE_DS_RECOMP_FFX_VK_DLL)
        string(APPEND _triaevum_immerse_target
            ",\n  \"fidelityfx_runtime\": \"${THREE_DS_RECOMP_FFX_VK_DLL}\"")
    endif()
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/triaevum-runtime-targets-$<CONFIG>.json"
        CONTENT "{\n  \"format\": \"triaevum_runtime_targets_v1\",\n  \"source_commit\": \"${GIT_COMMIT_HASH}\",\n  \"runtime\": \"$<TARGET_FILE:oot3d_native_game>\",\n  \"stub\": \"$<TARGET_FILE:triaevum_title_aot_stub>\"${_triaevum_immerse_target}\n}\n")
endif()
set(TRIAEVUM_PRODUCT_NRI 0)
if(THREE_DS_RECOMP_ENABLE_NRI AND OOT3D_ENABLE_VULKAN_RENDERER)
    set(TRIAEVUM_PRODUCT_NRI 1)
endif()
# Reported so the release workflow can refuse a runtime that offers the
# experimental global illumination modes.
set(TRIAEVUM_PRODUCT_EXPERIMENTAL_GI 0)
if(THREE_DS_RECOMP_OFFER_EXPERIMENTAL_GI)
    set(TRIAEVUM_PRODUCT_EXPERIMENTAL_GI 1)
endif()
# Reported so the release workflow can refuse a runtime with developer tools.
set(TRIAEVUM_PRODUCT_DEVELOPER_TOOLS 0)
if(TRIAEVUM_DEVELOPER_TOOLS)
    set(TRIAEVUM_PRODUCT_DEVELOPER_TOOLS 1)
endif()
configure_file(
    tools/oot3d/native_game_runtime/triaevum_product_build.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/triaevum-product/triaevum_product_build.h" @ONLY)
target_include_directories(oot3d_native_game PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/triaevum-product")
get_filename_component(OOT3D_NATIVE_GAME_WORKSPACE_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
configure_file(
    config/oot3d_native_game.launch.json.in
    "${CMAKE_CURRENT_BINARY_DIR}/oot3d_native_game.launch.json"
    @ONLY)
unset(OOT3D_NATIVE_GAME_WORKSPACE_ROOT)

add_executable(oot3d_native_game_launch_profile_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_game_launch_profile.cpp
    tools/oot3d/native_game_runtime/oot3d_native_game_launch_profile_tests.cpp
)
target_include_directories(oot3d_native_game_launch_profile_tests PRIVATE
    tools/oot3d/native_game_runtime)
target_link_libraries(oot3d_native_game_launch_profile_tests PRIVATE
    nlohmann_json::nlohmann_json)
target_compile_options(oot3d_native_game_launch_profile_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_game_launch_profile_tests PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_native_game_launch_profile_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_game_launch_profile_tests PROPERTY
    FOLDER Tests)

add_executable(oot3d_graphics_settings_schedule_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_graphics_settings_schedule.cpp
    tools/oot3d/native_game_runtime/oot3d_graphics_settings_schedule_tests.cpp
)
target_include_directories(oot3d_graphics_settings_schedule_tests PRIVATE
    tools/oot3d/native_game_runtime)
target_link_libraries(oot3d_graphics_settings_schedule_tests PRIVATE
    nlohmann_json::nlohmann_json)
target_compile_options(oot3d_graphics_settings_schedule_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_property(TARGET oot3d_graphics_settings_schedule_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_graphics_settings_schedule_tests PROPERTY
    FOLDER Tests)

add_executable(oot3d_actor_visibility_policy_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_actor_visibility_policy.cpp
    tools/oot3d/native_game_runtime/oot3d_actor_visibility_policy_tests.cpp
)
target_compile_options(oot3d_actor_visibility_policy_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_property(TARGET oot3d_actor_visibility_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_actor_visibility_policy_tests PROPERTY
    FOLDER Tests)

add_executable(oot3d_native_a32_actor_visibility_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_actor_visibility_policy.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_actor_visibility.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_actor_visibility_tests.cpp
    runtime/three_ds_recomp/src/fast/oot3d/perspective_fov_policy.cpp
)
target_include_directories(oot3d_native_a32_actor_visibility_tests PRIVATE
    runtime/three_ds_recomp/include)
target_link_libraries(oot3d_native_a32_actor_visibility_tests PRIVATE
    oot3d_native_a32_process)
target_compile_options(oot3d_native_a32_actor_visibility_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_property(TARGET oot3d_native_a32_actor_visibility_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_actor_visibility_tests PROPERTY FOLDER Tests)

add_library(oot3d_native_crash_diagnostics STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_crash_diagnostics.cpp
)
target_include_directories(oot3d_native_crash_diagnostics PUBLIC
    tools/oot3d/native_game_runtime
)
target_compile_options(oot3d_native_crash_diagnostics PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_crash_diagnostics PRIVATE NOMINMAX)
    target_link_libraries(oot3d_native_crash_diagnostics PUBLIC dbghelp.lib)
    set_property(TARGET oot3d_native_crash_diagnostics PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_crash_diagnostics PROPERTY FOLDER Tools)

add_executable(oot3d_native_crash_diagnostics_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_crash_diagnostics_tests.cpp
)
target_link_libraries(oot3d_native_crash_diagnostics_tests PRIVATE
    oot3d_native_crash_diagnostics)
target_compile_options(oot3d_native_crash_diagnostics_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_crash_diagnostics_tests PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_native_crash_diagnostics_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_crash_diagnostics_tests PROPERTY FOLDER Tools)
target_include_directories(oot3d_native_game PRIVATE
    tools/oot3d/native_demo_host
    tools/oot3d/native_game_runtime
    tools/oot3d/ui_topscreen
)
target_link_libraries(oot3d_native_game PRIVATE
    oot3d_native_application_host
    oot3d_native_game_runtime
    oot3d_native_a32_savestate
    oot3d_native_control_config
    oot3d_ui_topscreen)
target_sources(oot3d_native_game PRIVATE tools/oot3d/native_game_runtime/oot3d_game_language.cpp)
if (TARGET oot3d_native_a32_aot OR TARGET oot3d_native_whole_aot OR
    OOT3D_DIRECT_AOT_PLUGIN)
    target_sources(oot3d_native_game PRIVATE
        tools/oot3d/native_game_runtime/oot3d_native_a32_window.cpp
        tools/oot3d/native_game_runtime/oot3d_native_a32_window_setup.cpp
        tools/oot3d/native_game_runtime/oot3d_native_a32_window_frame.cpp
        tools/oot3d/native_game_runtime/oot3d_native_a32_window_savestate.cpp
        tools/oot3d/native_game_runtime/oot3d_native_a32_candidate_dispatch.cpp
        tools/oot3d/native_game_runtime/oot3d_native_a32_projection_policy.cpp
        tools/oot3d/native_game_runtime/oot3d_native_a32_window_input.cpp
        tools/oot3d/native_game_runtime/oot3d_native_a32_window_summary.cpp
        tools/oot3d/native_game_runtime/oot3d_native_preparation_screen.cpp
        tools/oot3d/native_game_runtime/oot3d_native_render_command_stream.cpp
        tools/oot3d/native_game_runtime/oot3d_top_screen_ocarina_text_runtime.cpp
        tools/oot3d/native_game_runtime/oot3d_top_screen_pause_transaction.cpp
        tools/oot3d/native_game_runtime/oot3d_top_screen_item_description_runtime.cpp
        tools/oot3d/native_game_runtime/oot3d_top_screen_wide_items_runtime.cpp
        tools/oot3d/native_game_runtime/oot3d_native_actor_interactions.cpp
        tools/oot3d/native_game_runtime/oot3d_actor_visibility_policy.cpp
        tools/oot3d/native_game_runtime/oot3d_native_a32_actor_visibility.cpp
        tools/oot3d/native_game_runtime/oot3d_native_pcm_diagnostics.cpp
        tools/oot3d/native_game_runtime/oot3d_native_controls_settings_panel.cpp
        tools/oot3d/native_game_runtime/oot3d_game_language_panel.cpp
        tools/oot3d/native_game_runtime/oot3d_savestate_panel.cpp
        tools/oot3d/native_game_runtime/oot3d_savestate_settings.cpp
        tools/oot3d/native_game_runtime/oot3d_scene_environment.cpp
        tools/oot3d/native_game_runtime/oot3d_scene_environment_blend.cpp
        tools/oot3d/native_game_runtime/oot3d_light_gizmo.cpp
        tools/oot3d/native_game_runtime/oot3d_scene_environment_codec.cpp
        tools/oot3d/native_game_runtime/oot3d_scene_environment_panel.cpp
        tools/oot3d/native_game_runtime/oot3d_top_screen_settings_panel.cpp
        tools/oot3d/native_game_runtime/oot3d_graphics_settings_schedule.cpp)
    set_source_files_properties(tools/oot3d/native_game_runtime/oot3d_native_actor_interactions.cpp
        PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/decomp_support/include")
    # Developer tools (F1 > Dev Tools: SoH's Debug Warp Screen and Warp
    # Points). Never in a public build: --product-info reports the capability
    # and the release workflow refuses a runtime that has it.
    if(TRIAEVUM_DEVELOPER_TOOLS)
        target_sources(oot3d_native_game PRIVATE
            tools/oot3d/native_game_runtime/oot3d_dev_warp.cpp
            tools/oot3d/native_game_runtime/oot3d_dev_tools_panel.cpp
            tools/oot3d/native_game_runtime/oot3d_dev_warp_process.cpp
            tools/oot3d/native_game_runtime/oot3d_dev_emitter_inventory.cpp
            tools/oot3d/native_game_runtime/oot3d_dev_lab.cpp
            tools/oot3d/native_game_runtime/native_a32_read_probe.cpp)
        set_source_files_properties(tools/oot3d/native_game_runtime/oot3d_dev_warp.cpp
            tools/oot3d/native_game_runtime/oot3d_dev_emitter_inventory.cpp
            PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/decomp_support/include")
        target_compile_definitions(oot3d_native_game PRIVATE TRIAEVUM_DEVELOPER_TOOLS=1)
        add_executable(oot3d_dev_warp_tests EXCLUDE_FROM_ALL
            tools/oot3d/native_game_runtime/oot3d_dev_warp.cpp
            tools/oot3d/native_game_runtime/oot3d_dev_warp_tests.cpp)
        target_include_directories(oot3d_dev_warp_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_game_runtime"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/decomp_support/include")
        target_link_libraries(oot3d_dev_warp_tests PRIVATE oot3d_native_game_runtime)
        oot3d_target_defaults(oot3d_dev_warp_tests FOLDER Tests)
        add_executable(mm3d_dev_tools_tests EXCLUDE_FROM_ALL
            tools/oot3d/native_game_runtime/oot3d_dev_warp.cpp
            tools/oot3d/native_game_runtime/mm3d_dev_tools_tests.cpp)
        target_include_directories(mm3d_dev_tools_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_game_runtime"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/decomp_support/include")
        target_compile_definitions(mm3d_dev_tools_tests PRIVATE TRIAEVUM_DEVELOPER_TOOLS=1)
        target_link_libraries(mm3d_dev_tools_tests PRIVATE oot3d_native_game_runtime)
        oot3d_target_defaults(mm3d_dev_tools_tests FOLDER Tests)
        add_executable(native_a32_read_probe_tests EXCLUDE_FROM_ALL
            tools/oot3d/native_game_runtime/native_a32_read_probe.cpp
            tools/oot3d/native_game_runtime/native_a32_read_probe_tests.cpp)
        target_link_libraries(native_a32_read_probe_tests PRIVATE oot3d_native_game_runtime)
        oot3d_target_defaults(native_a32_read_probe_tests FOLDER Tests)
        # Scene environment profiles for agents and scripts (validate, format,
        # explain), without starting the game.
        add_executable(triaevum_environment_tool EXCLUDE_FROM_ALL
            tools/oot3d/native_game_runtime/oot3d_environment_tool.cpp
            tools/oot3d/native_game_runtime/oot3d_interior_sun_prebuild.cpp
            tools/oot3d/native_game_runtime/oot3d_scene_environment_codec.cpp)
        target_include_directories(triaevum_environment_tool PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_game_runtime")
        target_link_libraries(triaevum_environment_tool PRIVATE three_ds_recomp_runtime oot3d_native_pica_composition)
        oot3d_target_defaults(triaevum_environment_tool FOLDER Tools)
        # The scene environment runtime's developer hot reload.
        add_executable(triaevum_environment_tests EXCLUDE_FROM_ALL
            tools/oot3d/native_game_runtime/oot3d_scene_environment_tests.cpp
            tools/oot3d/native_game_runtime/oot3d_scene_environment.cpp
            tools/oot3d/native_game_runtime/oot3d_scene_environment_blend.cpp
            tools/oot3d/native_game_runtime/oot3d_scene_environment_codec.cpp
            tools/oot3d/native_game_runtime/oot3d_light_gizmo.cpp)
        target_include_directories(triaevum_environment_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_game_runtime")
        target_compile_definitions(triaevum_environment_tests PRIVATE TRIAEVUM_DEVELOPER_TOOLS=1)
        target_link_libraries(triaevum_environment_tests PRIVATE three_ds_recomp_runtime)
        oot3d_target_defaults(triaevum_environment_tests FOLDER Tests)
    endif()
    target_link_libraries(oot3d_native_game PRIVATE
        oot3d_native_a32_input
        oot3d_native_a32_process_image
        oot3d_native_a32_ctr_host
        oot3d_native_ui_lifecycle_bridge
        oot3d_native_ui_texture_provider
        oot3d_ui_legacy_adapters
        oot3d_top_screen_texture_runtime
        oot3d_top_screen_runtime_consumer
        oot3d_native_pica_composition
        oot3d_mm3d_guest_patches
        oot3d_mm3d_single_screen
        oot3d_native_pica_submission
        oot3d_native_pica_semantic_trace
        oot3d_native_pica_vulkan_bridge)
    if (TARGET oot3d_native_a32_aot)
        target_link_libraries(oot3d_native_game PRIVATE
            oot3d_native_a32_aot)
    endif()
    if (OOT3D_DIRECT_AOT_PLUGIN)
        target_link_libraries(oot3d_native_game PRIVATE
            triaevum_title_aot_stub)
        target_compile_definitions(oot3d_native_game PRIVATE
            OOT3D_REQUIRE_WHOLE_AOT_PLUGIN_V2=1
            OOT3D_NATIVE_DIRECT_AOT_PLUGIN=1)
    endif()
    target_compile_definitions(oot3d_native_game PRIVATE
        OOT3D_NATIVE_A32_WINDOW_AVAILABLE=1)
endif()
target_compile_options(oot3d_native_game PRIVATE "${WARNING_OVERRIDE}")
if (OOT3D_WHOLE_AOT_PRODUCT_MODE)
    target_link_libraries(oot3d_native_game PRIVATE
        oot3d_native_crash_diagnostics)
    target_compile_definitions(oot3d_native_game PRIVATE
        OOT3D_WHOLE_AOT_PRODUCT_MODE=1)
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT OOT3D_FAST_DEV_LINK)
        set(_oot3d_whole_aot_symbol_dir "${CMAKE_CURRENT_BINARY_DIR}/symbols")
        file(MAKE_DIRECTORY "${_oot3d_whole_aot_symbol_dir}")
        # /DEBUG keeps unreferenced functions and data unless /OPT:REF is explicit. Identical-code folding
        # (/OPT:ICF) stays off: it can merge functions whose addresses the code compares.
        target_link_options(oot3d_native_game PRIVATE
            "/DEBUG:FULL"
            "/OPT:REF"
            "/PDB:${_oot3d_whole_aot_symbol_dir}/oot3d_native_game.pdb"
            "/MAP:${_oot3d_whole_aot_symbol_dir}/oot3d_native_game.map")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_link_options(oot3d_native_game PRIVATE
            "/DEBUG:NONE"
            "/INCREMENTAL:NO"
            "/OPT:NOREF"
            "/OPT:NOICF")
    endif()
endif()
option(TRIAEVUM_ENABLE_CPU_PHASE_PROBE
    "Developer-only Windows main-thread CPU/wall accounting (not clean performance timing)" OFF)
if(TRIAEVUM_ENABLE_CPU_PHASE_PROBE)
    foreach(_cpu_probe_target IN ITEMS oot3d_native_a32_process
            oot3d_native_a32_ctr_host oot3d_native_pica_submission
            oot3d_native_pica_vulkan_plan oot3d_native_game)
        target_compile_definitions(${_cpu_probe_target} PRIVATE TRIAEVUM_ENABLE_CPU_PHASE_PROBE=1)
    endforeach()
    unset(_cpu_probe_target)
endif()

set(_oot3d_vulkan_hosts
    triaevum_oot3d_module_host
    oot3d_native_game)
if(OOT3D_ENABLE_VULKAN_RENDERER AND TARGET three_ds_recomp_shaderc)
    if(TARGET oot3d_native_pica_default_pack)
        add_dependencies(oot3d_native_game
            oot3d_native_pica_default_pack)
    endif()
    foreach(oot3d_vulkan_host IN LISTS _oot3d_vulkan_hosts)
        add_custom_command(TARGET ${oot3d_vulkan_host} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:three_ds_recomp_shaderc>
                    $<TARGET_FILE_DIR:${oot3d_vulkan_host}>
            COMMENT "Deploying shaderc for ${oot3d_vulkan_host}"
        )
    endforeach()
endif()
if(COMMAND three_ds_recomp_deploy_nri_runtime)
    foreach(oot3d_vulkan_host IN LISTS _oot3d_vulkan_hosts)
        three_ds_recomp_deploy_nri_runtime(${oot3d_vulkan_host})
    endforeach()
endif()
unset(_oot3d_vulkan_hosts)
oot3d_target_defaults(oot3d_native_game NO_WARNING_OVERRIDE)
if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    nx_generate_nacp(oot3d_native_game.nacp
        NAME "The Legend of Zelda: Ocarina of Time 3D Recompiled"
        AUTHOR "OoT3D Recompiled contributors"
        VERSION "0.1.0")
    nx_create_nro(oot3d_native_game
        NACP oot3d_native_game.nacp)
endif()
set_property(TARGET oot3d_native_game PROPERTY FOLDER Tools)
