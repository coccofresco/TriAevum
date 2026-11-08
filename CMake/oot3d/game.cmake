# The game: the native game runtime, the oot3d_native_game executable (TriAevum) and its link options,
# crash diagnostics, the CPU phase probe option and the Vulkan runtime deployment.
# Included from the root CMakeLists.txt, in its scope.

add_library(oot3d_native_game_runtime STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_scene_view.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_timing_probe.cpp
    tools/oot3d/native_game_runtime/oot3d_native_scenario_bootstrap.cpp
    tools/oot3d/native_game_runtime/oot3d_native_frame_rate.cpp
    tools/oot3d/native_game_runtime/oot3d_native_presentation_pacer.cpp
    tools/oot3d/native_game_runtime/oot3d_native_input_latency_trace.cpp
    tools/oot3d/native_game_runtime/oot3d_native_input_admission.cpp
    tools/oot3d/native_game_runtime/oot3d_native_performance_hint.cpp
    tools/oot3d/native_game_runtime/oot3d_native_temporal_event_ledger.cpp
    titles/oot3d/gameplay/oot3d_native_player_temporal_bridge.cpp
    titles/oot3d/gameplay/oot3d_native_root_motion.cpp
    titles/oot3d/gameplay/oot3d_native_skel_anime.cpp
    titles/oot3d/gameplay/oot3d_rumble_events.cpp
)
target_include_directories(oot3d_native_game_runtime PUBLIC
    tools/oot3d/decomp_support/include
    tools/oot3d/native_demo_host
    tools/oot3d/native_game_runtime
    titles/oot3d/topscreen
    titles/oot3d/gameplay
    titles/oot3d/environment
)
target_compile_options(oot3d_native_game_runtime PRIVATE "${WARNING_OVERRIDE}")
target_link_libraries(oot3d_native_game_runtime PUBLIC
    oot3d_native_audio
    oot3d_native_a32_process
    three_ds_recomp_input
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
    titles/oot3d/gameplay/oot3d_native_player_temporal_bridge_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_scene_view_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_timing_probe_tests.cpp
    titles/oot3d/gameplay/oot3d_native_root_motion_tests.cpp
    titles/oot3d/gameplay/oot3d_native_skel_anime_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_input_latency_trace_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_input_admission_tests.cpp
    titles/oot3d/gameplay/oot3d_rumble_events_tests.cpp
)
target_link_libraries(oot3d_native_game_runtime_tests PRIVATE oot3d_native_game_runtime)
oot3d_target_defaults(oot3d_native_game_runtime_tests FOLDER Tests)

if(TRIAEVUM_TITLE_MM3D)
    add_executable(mm3d_presentation_settings_tests EXCLUDE_FROM_ALL
        titles/mm3d/adapter/mm3d_presentation_settings_tests.cpp)
    target_include_directories(mm3d_presentation_settings_tests PRIVATE tools/oot3d/native_game_runtime)
    target_link_libraries(mm3d_presentation_settings_tests PRIVATE oot3d_mm3d_single_screen)
    oot3d_target_defaults(mm3d_presentation_settings_tests FOLDER Tests)

    add_executable(mm3d_photo_camera_tests EXCLUDE_FROM_ALL
        titles/mm3d/adapter/mm3d_photo_camera_tests.cpp)
    target_include_directories(mm3d_photo_camera_tests PRIVATE tools/oot3d/native_game_runtime)
    oot3d_target_defaults(mm3d_photo_camera_tests FOLDER Tests)

    add_executable(mm3d_foliage_distance_tests EXCLUDE_FROM_ALL
        titles/mm3d/gameplay/mm3d_foliage_distance_tests.cpp)
    target_include_directories(mm3d_foliage_distance_tests PRIVATE tools/oot3d/native_game_runtime)
    oot3d_target_defaults(mm3d_foliage_distance_tests FOLDER Tests)

    add_executable(mm3d_actor_visibility_tests EXCLUDE_FROM_ALL
        titles/mm3d/gameplay/mm3d_actor_visibility_tests.cpp)
    target_include_directories(mm3d_actor_visibility_tests PRIVATE tools/oot3d/native_game_runtime)
    oot3d_target_defaults(mm3d_actor_visibility_tests FOLDER Tests)

    add_executable(mm3d_frame_pacing_tests EXCLUDE_FROM_ALL
        titles/mm3d/adapter/mm3d_frame_pacing_tests.cpp)
    target_include_directories(mm3d_frame_pacing_tests PRIVATE tools/oot3d/native_game_runtime)
    target_link_libraries(mm3d_frame_pacing_tests PRIVATE oot3d_native_game_runtime)
    oot3d_target_defaults(mm3d_frame_pacing_tests FOLDER Tests)

    add_executable(mm3d_timing_observer_tests EXCLUDE_FROM_ALL
        titles/mm3d/adapter/mm3d_timing_observer_tests.cpp)
    oot3d_target_defaults(mm3d_timing_observer_tests FOLDER Tests)

    add_executable(mm3d_light_arena_tests EXCLUDE_FROM_ALL
        titles/mm3d/gameplay/mm3d_light_arena_tests.cpp)
    oot3d_target_defaults(mm3d_light_arena_tests FOLDER Tests)

    add_executable(mm3d_gameplay_options_tests EXCLUDE_FROM_ALL
        titles/mm3d/gameplay/mm3d_gameplay_options_tests.cpp)
    target_link_libraries(mm3d_gameplay_options_tests PRIVATE oot3d_mm3d_guest_patches)
    oot3d_target_defaults(mm3d_gameplay_options_tests FOLDER Tests)
    add_executable(mm3d_rumble_donor_sites_tests EXCLUDE_FROM_ALL
        titles/mm3d/gameplay/mm3d_rumble_donor_sites_tests.cpp)
    oot3d_target_defaults(mm3d_rumble_donor_sites_tests FOLDER Tests)
    add_executable(mm3d_rumble_events_tests EXCLUDE_FROM_ALL
        titles/mm3d/gameplay/mm3d_rumble_events_tests.cpp)
    target_include_directories(mm3d_rumble_events_tests PRIVATE tools/three_ds/input)
    oot3d_target_defaults(mm3d_rumble_events_tests FOLDER Tests)

    # MM3D adapter read/lifetime guards; no process, renderer or ROM needed.
    add_executable(mm3d_scene_environment_tests EXCLUDE_FROM_ALL
        titles/mm3d/adapter/mm3d_scene_environment_tests.cpp)
    target_include_directories(mm3d_scene_environment_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/three_ds_recomp/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_game_runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/topscreen"
        "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/gameplay"
        "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/environment"
        "${CMAKE_CURRENT_SOURCE_DIR}/titles/mm3d/adapter")
    oot3d_target_defaults(mm3d_scene_environment_tests FOLDER Tests)
endif()

# Clock verification must also work without the optional legacy cutscene host.
add_executable(triaevum_presentation_clock_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_frame_rate_tests.cpp
    tools/triaevum_release/tests/presentation_clock_main.cpp)
target_link_libraries(triaevum_presentation_clock_tests PRIVATE oot3d_native_game_runtime)
oot3d_target_defaults(triaevum_presentation_clock_tests NO_WARNING_OVERRIDE FOLDER Tests)

# Which titles start the renderer with prepared shaders (title session capability).
add_executable(triaevum_title_shader_preparation_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_title_shader_preparation_tests.cpp)
target_include_directories(triaevum_title_shader_preparation_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/three_ds_recomp/include")
# TitleSession also exposes native button overlays (menu glyph/UI contracts).
target_link_libraries(triaevum_title_shader_preparation_tests PRIVATE
    oot3d_native_game_runtime oot3d_ui_menu oot3d_native_control_config)
oot3d_target_defaults(triaevum_title_shader_preparation_tests FOLDER Tests)

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
    tools/oot3d/native_game_runtime/triaevum_game_session_lock.cpp
    tools/oot3d/native_game_runtime/triaevum_fatal_report.cpp
    tools/oot3d/native_game_runtime/triaevum_console.cpp
    tools/oot3d/native_game_runtime/triaevum_data_routing.cpp
    tools/oot3d/native_game_runtime/triaevum_early_log.cpp
)
if(WIN32)
    target_sources(oot3d_native_game PRIVATE
        tools/oot3d/native_game_runtime/triaevum_windows.manifest
        # The executable's icon (tools/triaevum_release/branding).
        tools/oot3d/native_game_runtime/triaevum_windows.rc)
    # Owner decision 12 (UX plan WP-03): players get no console window; an
    # error that ends the game shows a dialog (triaevum_fatal_report.h) and
    # developers ask for a console with --console (triaevum_console.h).
    # Redirected output (scripts, Forge, release probes) is inherited as
    # before. main() stays the entry point.
    set_target_properties(oot3d_native_game PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(oot3d_native_game PRIVATE /ENTRY:mainCRTStartup)
    endif()
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
    tools/oot3d/native_game_runtime
    titles/oot3d/topscreen
    titles/oot3d/gameplay
    titles/oot3d/environment)
target_link_libraries(oot3d_native_game_launch_profile_tests PRIVATE
    nlohmann_json::nlohmann_json)
# ${runtime_dir} comes from SDL_GetBasePath; the renderer directory's SDL
# import is not visible here.
if (NOT TARGET SDL2::SDL2)
    find_package(SDL2 CONFIG QUIET)
endif()
if (TARGET SDL2::SDL2)
    target_link_libraries(oot3d_native_game_launch_profile_tests PRIVATE SDL2::SDL2)
endif()
target_compile_options(oot3d_native_game_launch_profile_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_game_launch_profile_tests PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_native_game_launch_profile_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(triaevum_game_session_lock_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_game_session_lock.cpp
    tools/oot3d/native_game_runtime/triaevum_game_session_lock_tests.cpp
)
oot3d_target_defaults(triaevum_game_session_lock_tests FOLDER Tests)

# The managed data tree's runtime side (UX plan WP-05C): routing options,
# the save-state move and its record, the early log spool. No window.
add_executable(triaevum_data_routing_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_data_routing.cpp
    tools/oot3d/native_game_runtime/triaevum_early_log.cpp
    tools/oot3d/native_game_runtime/oot3d_savestate_migration.cpp
    tools/oot3d/native_game_runtime/oot3d_savestate_library.cpp
    tools/oot3d/native_game_runtime/triaevum_data_routing_tests.cpp
)
target_link_libraries(triaevum_data_routing_tests PRIVATE nlohmann_json::nlohmann_json oot3d_renderer_contracts)
oot3d_target_defaults(triaevum_data_routing_tests FOLDER Tests)

# The error reports and display-recovery record (UX plan WP-03); no window.
add_executable(triaevum_fatal_report_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_fatal_report.cpp
    tools/oot3d/native_game_runtime/triaevum_console.cpp
    runtime/three_ds_recomp/src/fast/extension/display_recovery.cpp
    tools/oot3d/native_game_runtime/triaevum_fatal_report_tests.cpp
)
target_include_directories(triaevum_fatal_report_tests PRIVATE runtime/three_ds_recomp/include)
target_link_libraries(triaevum_fatal_report_tests PRIVATE nlohmann_json::nlohmann_json)
oot3d_target_defaults(triaevum_fatal_report_tests FOLDER Tests)
set_property(TARGET oot3d_native_game_launch_profile_tests PROPERTY
    FOLDER Tests)

add_executable(oot3d_graphics_settings_schedule_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_graphics_settings_schedule.cpp
    tools/oot3d/native_game_runtime/oot3d_graphics_settings_schedule_tests.cpp
)
target_include_directories(oot3d_graphics_settings_schedule_tests PRIVATE
    tools/oot3d/native_game_runtime
    titles/oot3d/topscreen
    titles/oot3d/gameplay
    titles/oot3d/environment)
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
    titles/oot3d/gameplay/oot3d_actor_visibility_policy.cpp
    titles/oot3d/gameplay/oot3d_actor_visibility_policy_tests.cpp
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
    titles/oot3d/gameplay/oot3d_actor_visibility_policy.cpp
    titles/oot3d/gameplay/oot3d_native_a32_actor_visibility.cpp
    titles/oot3d/gameplay/oot3d_native_a32_actor_visibility_tests.cpp
    runtime/three_ds_recomp/src/fast/extension/perspective_fov_policy.cpp
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

# SingleScreen 3.0 object bank extension (19 -> 39 object slots).
add_executable(oot3d_object_bank_extension_tests EXCLUDE_FROM_ALL
    titles/oot3d/gameplay/oot3d_object_bank_extension.cpp
    titles/oot3d/gameplay/oot3d_object_bank_extension_tests.cpp
)
target_link_libraries(oot3d_object_bank_extension_tests PRIVATE
    oot3d_native_a32_process)
target_compile_options(oot3d_object_bank_extension_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_property(TARGET oot3d_object_bank_extension_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_object_bank_extension_tests PROPERTY FOLDER Tests)

# SingleScreen gameplay options: native-call sequences at their block entries.
add_executable(oot3d_top_screen_gameplay_runtime_tests EXCLUDE_FROM_ALL
    titles/oot3d/topscreen/oot3d_top_screen_gameplay_runtime.cpp
    titles/oot3d/topscreen/oot3d_top_screen_link_ext.cpp
    titles/oot3d/topscreen/oot3d_top_screen_gameplay_runtime_tests.cpp
)
target_link_libraries(oot3d_top_screen_gameplay_runtime_tests PRIVATE
    oot3d_native_a32_process oot3d_ui_topscreen)
target_compile_options(oot3d_top_screen_gameplay_runtime_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_property(TARGET oot3d_top_screen_gameplay_runtime_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_top_screen_gameplay_runtime_tests PROPERTY FOLDER Tests)

# SingleScreen 3.0 rain: title policy (runtime) and its whole-AOT adapter.
# The policy math must not contract into FMAs (3.0 parity goldens).
if (MSVC)
    set_property(SOURCE
        titles/oot3d/topscreen/oot3d_top_screen_rain_runtime.cpp
        APPEND PROPERTY COMPILE_OPTIONS /fp:strict)
else()
    set_property(SOURCE
        titles/oot3d/topscreen/oot3d_top_screen_rain_runtime.cpp
        APPEND PROPERTY COMPILE_OPTIONS -ffp-contract=off)
endif()
add_executable(oot3d_top_screen_rain_runtime_tests EXCLUDE_FROM_ALL
    titles/oot3d/topscreen/oot3d_top_screen_rain_runtime.cpp
    titles/oot3d/topscreen/oot3d_top_screen_rain_runtime_tests.cpp
)
add_executable(oot3d_top_screen_rain_adapter_tests EXCLUDE_FROM_ALL
    titles/oot3d/topscreen/oot3d_top_screen_rain_runtime.cpp
    titles/oot3d/topscreen/oot3d_top_screen_rain_adapter.cpp
    titles/oot3d/topscreen/oot3d_top_screen_rain_adapter_tests.cpp
)
target_link_libraries(oot3d_top_screen_rain_adapter_tests PRIVATE
    oot3d_native_a32_process)
foreach(rainTests oot3d_top_screen_rain_runtime_tests oot3d_top_screen_rain_adapter_tests)
    target_compile_options(${rainTests} PRIVATE "${WARNING_OVERRIDE}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set_property(TARGET ${rainTests} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(TARGET ${rainTests} PROPERTY FOLDER Tests)
endforeach()

add_library(oot3d_native_crash_diagnostics STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_crash_diagnostics.cpp
)
target_include_directories(oot3d_native_crash_diagnostics PUBLIC
    tools/oot3d/native_game_runtime
    titles/oot3d/topscreen
    titles/oot3d/gameplay
    titles/oot3d/environment
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
    titles/oot3d/topscreen
    titles/oot3d/gameplay
    titles/oot3d/environment
    tools/oot3d/ui_topscreen
)
target_link_libraries(oot3d_native_game PRIVATE
    oot3d_native_application_host
    oot3d_native_game_runtime
    oot3d_native_a32_savestate
    oot3d_native_control_config
    oot3d_ui_topscreen
    oot3d_ui_menu)
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
        tools/oot3d/native_game_runtime/triaevum_title_sessions.cpp
        titles/oot3d/topscreen/oot3d_top_screen_ocarina_text_runtime.cpp
        titles/oot3d/topscreen/oot3d_top_screen_pause_transaction.cpp
        titles/oot3d/topscreen/oot3d_top_screen_item_description_runtime.cpp
        titles/oot3d/topscreen/oot3d_top_screen_wide_items_runtime.cpp
        titles/oot3d/topscreen/oot3d_top_screen_visions_runtime.cpp
        titles/oot3d/topscreen/oot3d_top_screen_reframed_file_select.cpp
        titles/oot3d/topscreen/oot3d_top_screen_gameplay_runtime.cpp
        titles/oot3d/topscreen/oot3d_top_screen_link_ext.cpp
        titles/oot3d/gameplay/oot3d_object_bank_extension.cpp
        titles/oot3d/topscreen/oot3d_top_screen_rain_runtime.cpp
        titles/oot3d/topscreen/oot3d_top_screen_rain_adapter.cpp
        titles/oot3d/gameplay/oot3d_native_actor_interactions.cpp
        titles/oot3d/gameplay/oot3d_actor_visibility_policy.cpp
        titles/oot3d/gameplay/oot3d_native_a32_actor_visibility.cpp
        tools/oot3d/native_game_runtime/oot3d_native_pcm_diagnostics.cpp
        tools/oot3d/native_game_runtime/oot3d_native_controls_settings_panel.cpp
        tools/oot3d/native_game_runtime/oot3d_game_language_panel.cpp
        tools/oot3d/native_game_runtime/oot3d_savestate_panel.cpp
        tools/oot3d/native_game_runtime/oot3d_savestate_settings.cpp
        titles/oot3d/environment/oot3d_scene_environment.cpp
        titles/oot3d/environment/oot3d_scene_environment_blend.cpp
        titles/oot3d/environment/oot3d_light_gizmo.cpp
        titles/oot3d/environment/oot3d_scene_environment_codec.cpp
        titles/oot3d/environment/oot3d_scene_environment_panel.cpp
        titles/oot3d/topscreen/oot3d_top_screen_settings_panel.cpp
        tools/oot3d/native_game_runtime/oot3d_photo_mode_panel.cpp
        tools/oot3d/native_game_runtime/oot3d_in_game_menu.cpp
        tools/oot3d/native_game_runtime/oot3d_hud_button_labels.cpp
        tools/oot3d/native_game_runtime/oot3d_graphics_settings_schedule.cpp)
    set_source_files_properties(titles/oot3d/gameplay/oot3d_native_actor_interactions.cpp
        PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/decomp_support/include")
    # The title sessions this build contains (triaevum_title_session.h).
    if(TRIAEVUM_TITLE_MM3D)
        target_sources(oot3d_native_game PRIVATE
            titles/mm3d/adapter/mm3d_title_session.cpp)
        target_compile_definitions(oot3d_native_game PRIVATE TRIAEVUM_TITLE_MM3D=1)
        target_link_libraries(oot3d_native_game PRIVATE
            oot3d_mm3d_guest_patches
            oot3d_mm3d_single_screen
            oot3d_mm3d_pica_composition)
    endif()
    # Developer tools (F1 > Dev Tools: SoH's Debug Warp Screen and Warp
    # Points). Never in a public build: --product-info reports the capability
    # and the release workflow refuses a runtime that has it.
    if(TRIAEVUM_DEVELOPER_TOOLS)
        target_sources(oot3d_native_game PRIVATE
            tools/oot3d/native_game_runtime/oot3d_dev_warp.cpp
            tools/oot3d/native_game_runtime/oot3d_dev_tools_panel.cpp
            tools/oot3d/native_game_runtime/oot3d_dev_warp_process.cpp
            titles/oot3d/environment/oot3d_dev_emitter_inventory.cpp
            tools/oot3d/native_game_runtime/oot3d_dev_lab.cpp
            tools/oot3d/native_game_runtime/native_a32_read_probe.cpp)
        set_source_files_properties(tools/oot3d/native_game_runtime/oot3d_dev_warp.cpp
            titles/oot3d/environment/oot3d_dev_emitter_inventory.cpp
            PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/decomp_support/include")
        target_compile_definitions(oot3d_native_game PRIVATE TRIAEVUM_DEVELOPER_TOOLS=1)
        add_executable(oot3d_dev_warp_tests EXCLUDE_FROM_ALL
            tools/oot3d/native_game_runtime/oot3d_dev_warp.cpp
            tools/oot3d/native_game_runtime/oot3d_dev_warp_tests.cpp)
        target_include_directories(oot3d_dev_warp_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_game_runtime"
            "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/topscreen"
            "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/gameplay"
            "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/environment"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/decomp_support/include")
        target_link_libraries(oot3d_dev_warp_tests PRIVATE oot3d_native_game_runtime)
        oot3d_target_defaults(oot3d_dev_warp_tests FOLDER Tests)
        if(TRIAEVUM_TITLE_MM3D)
            add_executable(mm3d_dev_tools_tests EXCLUDE_FROM_ALL
                tools/oot3d/native_game_runtime/oot3d_dev_warp.cpp
                titles/mm3d/adapter/mm3d_dev_tools_tests.cpp)
            target_include_directories(mm3d_dev_tools_tests PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_game_runtime"
                "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/topscreen"
                "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/gameplay"
                "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/environment"
                "${CMAKE_CURRENT_SOURCE_DIR}/titles/mm3d/adapter"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/decomp_support/include")
            target_compile_definitions(mm3d_dev_tools_tests PRIVATE TRIAEVUM_DEVELOPER_TOOLS=1)
            target_link_libraries(mm3d_dev_tools_tests PRIVATE oot3d_native_game_runtime)
            oot3d_target_defaults(mm3d_dev_tools_tests FOLDER Tests)
        endif()
        add_executable(native_a32_read_probe_tests EXCLUDE_FROM_ALL
            tools/oot3d/native_game_runtime/native_a32_read_probe.cpp
            tools/oot3d/native_game_runtime/native_a32_read_probe_tests.cpp)
        target_link_libraries(native_a32_read_probe_tests PRIVATE oot3d_native_game_runtime)
        oot3d_target_defaults(native_a32_read_probe_tests FOLDER Tests)
        # Scene environment profiles for agents and scripts (validate, format,
        # explain), without starting the game.
        add_executable(triaevum_environment_tool EXCLUDE_FROM_ALL
            titles/oot3d/environment/oot3d_environment_tool.cpp
            titles/oot3d/environment/oot3d_interior_sun_prebuild.cpp
            titles/oot3d/environment/oot3d_scene_environment_codec.cpp)
        target_include_directories(triaevum_environment_tool PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_game_runtime"
            "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/topscreen"
            "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/gameplay"
            "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/environment")
        target_link_libraries(triaevum_environment_tool PRIVATE three_ds_recomp_runtime oot3d_native_pica_composition)
        oot3d_target_defaults(triaevum_environment_tool FOLDER Tools)
        # The scene environment runtime's developer hot reload.
        add_executable(triaevum_environment_tests EXCLUDE_FROM_ALL
            titles/oot3d/environment/oot3d_scene_environment_tests.cpp
            titles/oot3d/environment/oot3d_scene_environment.cpp
            titles/oot3d/environment/oot3d_scene_environment_blend.cpp
            titles/oot3d/environment/oot3d_scene_environment_codec.cpp
            titles/oot3d/environment/oot3d_light_gizmo.cpp)
        target_include_directories(triaevum_environment_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_game_runtime"
            "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/topscreen"
            "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/gameplay"
            "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/environment")
        target_compile_definitions(triaevum_environment_tests PRIVATE TRIAEVUM_DEVELOPER_TOOLS=1)
        target_link_libraries(triaevum_environment_tests PRIVATE three_ds_recomp_runtime)
        oot3d_target_defaults(triaevum_environment_tests FOLDER Tests)
        if(TRIAEVUM_TITLE_MM3D)
            # MM3D's base scene environment per group on native fragment lights.
            add_executable(mm3d_scene_environment_profile_tests EXCLUDE_FROM_ALL
                titles/mm3d/adapter/mm3d_scene_environment_profile_tests.cpp
                titles/oot3d/environment/oot3d_scene_environment.cpp
                titles/oot3d/environment/oot3d_scene_environment_blend.cpp
                titles/oot3d/environment/oot3d_scene_environment_codec.cpp
                titles/oot3d/environment/oot3d_light_gizmo.cpp)
            target_include_directories(mm3d_scene_environment_profile_tests PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_game_runtime"
                "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/topscreen"
                "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/gameplay"
                "${CMAKE_CURRENT_SOURCE_DIR}/titles/oot3d/environment"
                "${CMAKE_CURRENT_SOURCE_DIR}/titles/mm3d/adapter")
            target_compile_definitions(mm3d_scene_environment_profile_tests PRIVATE TRIAEVUM_DEVELOPER_TOOLS=1)
            target_link_libraries(mm3d_scene_environment_profile_tests PRIVATE three_ds_recomp_runtime)
            oot3d_target_defaults(mm3d_scene_environment_profile_tests FOLDER Tests)
        endif()
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
        oot3d_native_pica_submission
        oot3d_native_pica_semantic_trace
        oot3d_native_pica_vulkan_bridge)
    if (TARGET oot3d_native_a32_aot)
        target_link_libraries(oot3d_native_game PRIVATE
            oot3d_native_a32_aot)
    endif()
    if (OOT3D_DIRECT_AOT_PLUGIN)
        # macOS resolves the title module at run time through dlopen only; the
        # stub dylib would need an install name and rpath to link against.
        if (NOT APPLE)
            target_link_libraries(oot3d_native_game PRIVATE
                triaevum_title_aot_stub)
        endif()
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
    if (MSVC AND CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT OOT3D_FAST_DEV_LINK)
        set(_oot3d_whole_aot_symbol_dir "${CMAKE_CURRENT_BINARY_DIR}/symbols")
        file(MAKE_DIRECTORY "${_oot3d_whole_aot_symbol_dir}")
        # /DEBUG keeps unreferenced functions and data unless /OPT:REF is explicit. Identical-code folding
        # (/OPT:ICF) stays off: it can merge functions whose addresses the code compares.
        target_link_options(oot3d_native_game PRIVATE
            "/DEBUG:FULL"
            "/OPT:REF"
            "/PDB:${_oot3d_whole_aot_symbol_dir}/oot3d_native_game.pdb"
            "/MAP:${_oot3d_whole_aot_symbol_dir}/oot3d_native_game.map")
    elseif(MSVC AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_link_options(oot3d_native_game PRIVATE
            "/DEBUG:NONE"
            "/INCREMENTAL:NO"
            "/OPT:NOREF"
            "/OPT:NOICF")
    endif()
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND OOT3D_ENABLE_VULKAN_RENDERER AND TARGET three_ds_recomp_shaderc)
    # The Windows package keeps shaderc beside the other DLLs in runtime/; it
    # loads after main registers that directory (UseRuntimeLibraryDirectory).
    target_link_options(oot3d_native_game PRIVATE "/DELAYLOAD:shaderc_shared.dll")
    target_link_libraries(oot3d_native_game PRIVATE delayimp)
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
