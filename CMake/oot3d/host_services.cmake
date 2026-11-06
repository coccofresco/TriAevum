# CTR host services: the full and PICA-only service hosts, the PICA client bridge, the title AOT stub and
# game module, the A32 process, savestate and boot probe, and their tests.
# Included from the root CMakeLists.txt, in its scope.

set(_oot3d_native_a32_ctr_host_sources
    tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_host.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_host_system.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_host_hid.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_host_ir.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_host_gsp.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_host_dsp.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_host_fs.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_host_state.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_host_time.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_romfs_overlay.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_romfs_layers.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_dsp_hle.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_dsp_memory_adapter.cpp
    tools/oot3d/third_party/azahar_audio/audio_core/codec.cpp
    tools/oot3d/third_party/azahar_audio/audio_core/interpolate.cpp
    tools/oot3d/third_party/azahar_audio/audio_core/hle/filter.cpp
    tools/oot3d/third_party/azahar_audio/audio_core/hle/mixers.cpp
    tools/oot3d/third_party/azahar_audio/audio_core/hle/source.cpp
)
add_library(oot3d_native_a32_ctr_host STATIC EXCLUDE_FROM_ALL
    ${_oot3d_native_a32_ctr_host_sources})
target_include_directories(oot3d_native_a32_ctr_host PUBLIC
    tools/oot3d/native_game_runtime
    titles/oot3d/topscreen
    titles/oot3d/gameplay
    titles/oot3d/environment
)
target_include_directories(oot3d_native_a32_ctr_host PRIVATE
    tools/oot3d/third_party/azahar_audio
)
target_link_libraries(oot3d_native_a32_ctr_host PUBLIC
    nlohmann_json::nlohmann_json
    three_ds_recomp_input
    oot3d_native_a32_process
    oot3d_native_pica_frontend
    oot3d_native_pica_transfer)
oot3d_target_defaults(oot3d_native_a32_ctr_host FOLDER Tools)

add_executable(oot3d_native_audio_storage_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_audio_storage_tests.cpp)
target_include_directories(oot3d_native_audio_storage_tests PRIVATE
    tools/oot3d/third_party/azahar_audio)
target_link_libraries(oot3d_native_audio_storage_tests PRIVATE
    oot3d_native_a32_ctr_host)
oot3d_target_defaults(oot3d_native_audio_storage_tests NO_WARNING_OVERRIDE FOLDER Tests)

# Whole DSP mix (24 sources, mixers, aux, savestates) bit-identical to the
# pre-optimization code; --bench times an OoT3D-like frame.
add_executable(oot3d_native_audio_dsp_mix_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_audio_dsp_mix_tests.cpp)
target_include_directories(oot3d_native_audio_dsp_mix_tests PRIVATE
    tools/oot3d/third_party/azahar_audio)
target_link_libraries(oot3d_native_audio_dsp_mix_tests PRIVATE
    oot3d_native_a32_ctr_host)
oot3d_target_defaults(oot3d_native_audio_dsp_mix_tests NO_WARNING_OVERRIDE FOLDER Tests)

# Private title modules retain the exact CTR/SVC implementation but may only
# reach graphics through the stable host-service ABI. This variant therefore
# has no direct renderer dependency or callable PICA frontend path.
add_library(oot3d_native_a32_ctr_service_host STATIC EXCLUDE_FROM_ALL
    ${_oot3d_native_a32_ctr_host_sources})
target_include_directories(oot3d_native_a32_ctr_service_host PUBLIC
    tools/oot3d/native_game_runtime
    titles/oot3d/topscreen
    titles/oot3d/gameplay
    titles/oot3d/environment)
target_include_directories(oot3d_native_a32_ctr_service_host PRIVATE
    tools/oot3d/third_party/azahar_audio)
target_link_libraries(oot3d_native_a32_ctr_service_host PUBLIC
    nlohmann_json::nlohmann_json
    three_ds_recomp_input
    oot3d_native_a32_process
    oot3d_native_pica_transfer)
target_compile_definitions(oot3d_native_a32_ctr_service_host PRIVATE
    OOT3D_CTR_HOST_SERVICE_PICA_ONLY=1)
target_compile_options(oot3d_native_a32_ctr_service_host PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_a32_ctr_service_host PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_native_a32_ctr_service_host PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_ctr_service_host PROPERTY FOLDER Tools)

add_library(oot3d_triaevum_pica_client_bridge STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_oot3d_pica_client_bridge.cpp
)
target_include_directories(oot3d_triaevum_pica_client_bridge PUBLIC
    tools/oot3d/native_game_runtime
    titles/oot3d/topscreen
    titles/oot3d/gameplay
    titles/oot3d/environment
)
target_link_libraries(oot3d_triaevum_pica_client_bridge PUBLIC
    triaevum_module)
target_compile_options(oot3d_triaevum_pica_client_bridge PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_triaevum_pica_client_bridge PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_triaevum_pica_client_bridge PROPERTY
        MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_triaevum_pica_client_bridge PROPERTY FOLDER Tools)

if (OOT3D_DIRECT_AOT_PLUGIN)
    add_library(triaevum_title_aot_stub SHARED EXCLUDE_FROM_ALL
        tools/oot3d/native_game_runtime/triaevum_title_aot_stub.cpp)
    target_include_directories(triaevum_title_aot_stub PRIVATE
        tools/oot3d/native_game_runtime
        titles/oot3d/topscreen
        titles/oot3d/gameplay
        titles/oot3d/environment
        tools/oot3d/native_a32_runtime
        ${OOT3D_PINNED_A32_ROOT})
    target_compile_options(triaevum_title_aot_stub PRIVATE
        "${WARNING_OVERRIDE}")
    set_target_properties(triaevum_title_aot_stub PROPERTIES
        PREFIX ""
        OUTPUT_NAME "triaevum_title_aot")
    if(ANDROID)
        set_target_properties(triaevum_title_aot_stub PROPERTIES
            PREFIX "lib" OUTPUT_NAME "triaevum_title_bootstrap")
    endif()
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(triaevum_title_aot_stub PRIVATE NOMINMAX)
        set_property(TARGET triaevum_title_aot_stub PROPERTY
            MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(TARGET triaevum_title_aot_stub PROPERTY FOLDER Tools)
endif()

if (OOT3D_WHOLE_AOT_PRODUCT_MODE AND
    (TARGET oot3d_native_whole_aot OR OOT3D_DIRECT_AOT_PLUGIN))
    add_library(oot3d_game_module SHARED EXCLUDE_FROM_ALL
        tools/oot3d/native_game_module/oot3d_game_module_runtime.cpp
        tools/oot3d/native_game_module/oot3d_game_module_ui_profile.cpp
        tools/oot3d/native_game_module/oot3d_game_module_exports.cpp
        tools/oot3d/native_game_runtime/triaevum_ctr_filesystem_client_bridge.cpp)
    target_include_directories(oot3d_game_module PRIVATE
        tools/oot3d/native_game_module
        tools/oot3d/native_game_runtime
        titles/oot3d/topscreen
        titles/oot3d/gameplay
        titles/oot3d/environment)
    target_link_libraries(oot3d_game_module PRIVATE
        oot3d_native_a32_process_image
        oot3d_native_a32_ctr_service_host
        oot3d_ui_topscreen
        oot3d_triaevum_pica_client_bridge
        triaevum_module
        nlohmann_json::nlohmann_json)
    if (OOT3D_DIRECT_AOT_PLUGIN AND NOT APPLE)
        target_link_libraries(oot3d_game_module PRIVATE
            triaevum_title_aot_stub)
    endif()
    target_compile_options(oot3d_game_module PRIVATE "${WARNING_OVERRIDE}")
    set_target_properties(oot3d_game_module PROPERTIES
        PREFIX ""
        OUTPUT_NAME "oot3d_game_module")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Packaged as forge/oot3d_game_module.so beside the package root's
        # triaevum_title_aot.so: never record the publisher's build
        # directory as a library search path.
        set_target_properties(oot3d_game_module PROPERTIES
            BUILD_RPATH_USE_ORIGIN TRUE
            BUILD_RPATH "$ORIGIN;$ORIGIN/..;$ORIGIN/../lib"
            INSTALL_RPATH "$ORIGIN;$ORIGIN/..;$ORIGIN/../lib")
    endif()
    oot3d_target_defaults(oot3d_game_module NO_WARNING_OVERRIDE FOLDER Tools)

    add_executable(oot3d_game_module_smoke EXCLUDE_FROM_ALL
        tools/oot3d/native_game_module/oot3d_game_module_smoke.cpp)
    target_link_libraries(oot3d_game_module_smoke PRIVATE
        triaevum_module
        triaevum_forge_content_filesystem)
    add_dependencies(oot3d_game_module_smoke oot3d_game_module)
    target_compile_options(oot3d_game_module_smoke PRIVATE
        "${WARNING_OVERRIDE}")
    oot3d_target_defaults(oot3d_game_module_smoke NO_WARNING_OVERRIDE FOLDER Tests)
endif()

add_executable(oot3d_triaevum_pica_client_bridge_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_oot3d_pica_client_bridge_tests.cpp
)
target_link_libraries(oot3d_triaevum_pica_client_bridge_tests PRIVATE
    oot3d_triaevum_pica_client_bridge)
target_compile_options(oot3d_triaevum_pica_client_bridge_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_triaevum_pica_client_bridge_tests PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_triaevum_pica_client_bridge_tests PROPERTY
        MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_triaevum_pica_client_bridge_tests PROPERTY
    FOLDER Tests)

add_executable(oot3d_native_a32_process_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_process_tests.cpp
)
target_link_libraries(oot3d_native_a32_process_tests PRIVATE
    oot3d_native_a32_process)
if (TARGET oot3d_native_a32_aot)
    target_link_libraries(oot3d_native_a32_process_tests PRIVATE
        oot3d_native_a32_aot)
    target_compile_definitions(oot3d_native_a32_process_tests PRIVATE
        OOT3D_NATIVE_A32_AOT_TESTS=1)
endif()
if (TARGET oot3d_native_whole_aot)
    target_compile_definitions(oot3d_native_a32_process_tests PRIVATE
        OOT3D_NATIVE_WHOLE_AOT_TESTS=1)
endif()
if (OOT3D_WHOLE_AOT_PRODUCT_MODE)
    # The process library dispatches whole-AOT product blocks, which have no
    # interpreter native-function entries (NativeA32Process::Run).
    target_compile_definitions(oot3d_native_a32_process_tests PRIVATE
        OOT3D_NATIVE_A32_PRODUCT_DISPATCH_TESTS=1)
endif()
oot3d_target_defaults(oot3d_native_a32_process_tests FOLDER Tests)

# Majora's Mask 3D's adapter: guest patches, mm3d-dekui, Project Restoration.
if(TRIAEVUM_TITLE_MM3D)
    add_library(oot3d_mm3d_guest_patches STATIC EXCLUDE_FROM_ALL
        titles/mm3d/adapter/mm3d_guest_patches.cpp
        titles/mm3d/adapter/mm3d_a32_substitute.cpp
        titles/mm3d/adapter/mm3d_guest_calls.cpp
    )
    target_include_directories(oot3d_mm3d_guest_patches PUBLIC
        tools/oot3d/native_game_runtime
        titles/oot3d/topscreen
        titles/oot3d/gameplay
        titles/oot3d/environment
        titles/mm3d/adapter)
    target_link_libraries(oot3d_mm3d_guest_patches PUBLIC
        oot3d_native_a32_process)
    oot3d_target_defaults(oot3d_mm3d_guest_patches FOLDER Tools)

    add_library(oot3d_mm3d_single_screen STATIC EXCLUDE_FROM_ALL
        titles/mm3d/adapter/mm3d_single_screen.cpp
        titles/mm3d/adapter/mm3d_restoration.cpp
        titles/mm3d/dekui/core.cpp
        titles/mm3d/dekui/settings.cpp
    )
    target_include_directories(oot3d_mm3d_single_screen PUBLIC
        tools/oot3d/native_game_runtime
        titles/oot3d/topscreen
        titles/oot3d/gameplay
        titles/oot3d/environment
        titles/mm3d/adapter
        titles/mm3d/dekui
        titles/mm3d/restoration)
    target_link_libraries(oot3d_mm3d_single_screen PUBLIC
        oot3d_mm3d_guest_patches
        nlohmann_json::nlohmann_json)
    oot3d_target_defaults(oot3d_mm3d_single_screen FOLDER Tools)

    add_executable(oot3d_mm3d_v110_address_tests EXCLUDE_FROM_ALL
        titles/mm3d/dekui/v110_address_tests.cpp
    )
    target_include_directories(oot3d_mm3d_v110_address_tests PRIVATE titles/mm3d/dekui)
    oot3d_target_defaults(oot3d_mm3d_v110_address_tests FOLDER Tests)

    add_executable(oot3d_mm3d_dekui_core_tests EXCLUDE_FROM_ALL
        titles/mm3d/dekui/core_tests.cpp
    )
    target_link_libraries(oot3d_mm3d_dekui_core_tests PRIVATE
        oot3d_mm3d_single_screen)
    oot3d_target_defaults(oot3d_mm3d_dekui_core_tests FOLDER Tests)

    add_executable(oot3d_mm3d_restoration_tests EXCLUDE_FROM_ALL
        titles/mm3d/adapter/mm3d_restoration_tests.cpp
    )
    target_link_libraries(oot3d_mm3d_restoration_tests PRIVATE
        oot3d_mm3d_single_screen)
    oot3d_target_defaults(oot3d_mm3d_restoration_tests FOLDER Tests)

    add_executable(oot3d_mm3d_dekui_settings_tests EXCLUDE_FROM_ALL
        titles/mm3d/dekui/settings_tests.cpp
    )
    target_link_libraries(oot3d_mm3d_dekui_settings_tests PRIVATE
        oot3d_mm3d_single_screen)
    oot3d_target_defaults(oot3d_mm3d_dekui_settings_tests FOLDER Tests)

    # Replay fixtures of the dekui 6e36dfb4 gameplay hooks (titles/mm3d/dekui).
    add_executable(oot3d_mm3d_dekui_update_tests EXCLUDE_FROM_ALL
        titles/mm3d/dekui/update_tests.cpp
    )
    target_link_libraries(oot3d_mm3d_dekui_update_tests PRIVATE
        oot3d_mm3d_single_screen)
    oot3d_target_defaults(oot3d_mm3d_dekui_update_tests FOLDER Tests)

    # Tip 4f2692ed HUD fixtures (dekui S2): mirrored item-button backgrounds and the
    # minimap's pending-quad discard. Header-only modules, no arguments.
    add_executable(oot3d_mm3d_dekui_hud_tests EXCLUDE_FROM_ALL
        titles/mm3d/dekui/hud_tests.cpp
    )
    target_include_directories(oot3d_mm3d_dekui_hud_tests PRIVATE titles/mm3d/dekui)
    oot3d_target_defaults(oot3d_mm3d_dekui_hud_tests FOLDER Tests)

    # Tip 4f2692ed dungeon map (dekui S3): sites 5/6/7, routing, arrows, marker style.
    # Optional argument: the user's code.bin, to compare the guard words with original EUR v1.1.
    add_executable(oot3d_mm3d_dekui_dungeon_map_tests EXCLUDE_FROM_ALL
        titles/mm3d/dekui/dungeon_map_tests.cpp
    )
    target_link_libraries(oot3d_mm3d_dekui_dungeon_map_tests PRIVATE
        oot3d_mm3d_single_screen)
    target_include_directories(oot3d_mm3d_dekui_dungeon_map_tests PRIVATE titles/mm3d/dekui)
    oot3d_target_defaults(oot3d_mm3d_dekui_dungeon_map_tests FOLDER Tests)

    # Message-archive language contract and prompt consumers (dekui S1). Optional argument:
    # the user's code.bin, to compare the guard words with original EUR v1.1.
    add_executable(oot3d_mm3d_dekui_language_tests EXCLUDE_FROM_ALL
        titles/mm3d/dekui/language_tests.cpp
    )
    target_include_directories(oot3d_mm3d_dekui_language_tests PRIVATE titles/mm3d/dekui)
    oot3d_target_defaults(oot3d_mm3d_dekui_language_tests FOLDER Tests)

    # No integer language code may cross PromptText: `Language(m) == 2` and
    # `Translate(2, label)` must fail to compile, the normalized form must compile.
    foreach(_mm3d_language_case POSITIVE_CONTROL NEGATIVE_COMPARE NEGATIVE_TRANSLATE)
        try_compile(_mm3d_language_${_mm3d_language_case}
            "${CMAKE_CURRENT_BINARY_DIR}/mm3d_language_compile/${_mm3d_language_case}"
            "${CMAKE_CURRENT_SOURCE_DIR}/titles/mm3d/dekui/language_negative_compile.cpp"
            CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${CMAKE_CURRENT_SOURCE_DIR}/titles/mm3d/dekui"
            COMPILE_DEFINITIONS -DTRIAEVUM_${_mm3d_language_case}
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            OUTPUT_VARIABLE _mm3d_language_output_${_mm3d_language_case})
    endforeach()
    if(NOT _mm3d_language_POSITIVE_CONTROL)
        message(FATAL_ERROR "MM3D PromptText positive compile control failed:\n${_mm3d_language_output_POSITIVE_CONTROL}")
    endif()
    if(_mm3d_language_NEGATIVE_COMPARE OR _mm3d_language_NEGATIVE_TRANSLATE)
        message(FATAL_ERROR "MM3D PromptText accepted an integer language code (negative compile test)")
    endif()
    message(STATUS "MM3D PromptText negative compile test: integer language codes rejected")

    add_executable(oot3d_mm3d_single_screen_tests EXCLUDE_FROM_ALL
        titles/mm3d/adapter/mm3d_single_screen_tests.cpp
    )
    target_link_libraries(oot3d_mm3d_single_screen_tests PRIVATE
        oot3d_mm3d_single_screen)
    oot3d_target_defaults(oot3d_mm3d_single_screen_tests FOLDER Tests)

    add_executable(oot3d_mm3d_a32_substitute_tests EXCLUDE_FROM_ALL
        titles/mm3d/adapter/mm3d_a32_substitute_tests.cpp
    )
    target_link_libraries(oot3d_mm3d_a32_substitute_tests PRIVATE
        oot3d_mm3d_guest_patches)
    oot3d_target_defaults(oot3d_mm3d_a32_substitute_tests FOLDER Tests)

    add_executable(oot3d_mm3d_guest_calls_tests EXCLUDE_FROM_ALL
        titles/mm3d/adapter/mm3d_guest_calls_tests.cpp
    )
    target_link_libraries(oot3d_mm3d_guest_calls_tests PRIVATE
        oot3d_mm3d_guest_patches)
    oot3d_target_defaults(oot3d_mm3d_guest_calls_tests FOLDER Tests)

    add_executable(oot3d_mm3d_guest_patches_tests EXCLUDE_FROM_ALL
        titles/mm3d/adapter/mm3d_guest_patches_tests.cpp
    )
    target_link_libraries(oot3d_mm3d_guest_patches_tests PRIVATE
        oot3d_mm3d_guest_patches)
    oot3d_target_defaults(oot3d_mm3d_guest_patches_tests FOLDER Tests)
endif()

add_executable(oot3d_native_a32_ctr_host_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_host_tests.cpp
)
target_link_libraries(oot3d_native_a32_ctr_host_tests PRIVATE
    oot3d_native_a32_ctr_host)
oot3d_target_defaults(oot3d_native_a32_ctr_host_tests FOLDER Tests)

add_executable(oot3d_native_a32_romfs_overlay_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_romfs_overlay_tests.cpp
)
target_link_libraries(oot3d_native_a32_romfs_overlay_tests PRIVATE
    oot3d_native_a32_ctr_host)
oot3d_target_defaults(oot3d_native_a32_romfs_overlay_tests FOLDER Tests)

add_executable(oot3d_native_a32_romfs_layers_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_romfs_layers_tests.cpp
)
target_link_libraries(oot3d_native_a32_romfs_layers_tests PRIVATE
    oot3d_native_a32_ctr_host)
oot3d_target_defaults(oot3d_native_a32_romfs_layers_tests FOLDER Tests)

# Exercise FS IPC through the same service-only host used by the product module.
add_executable(oot3d_native_a32_ctr_fs_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_fs_tests.cpp
    tools/oot3d/native_game_runtime/triaevum_ctr_filesystem_client_bridge.cpp)
target_link_libraries(oot3d_native_a32_ctr_fs_tests PRIVATE
    oot3d_native_a32_ctr_service_host triaevum_module)
oot3d_target_defaults(oot3d_native_a32_ctr_fs_tests FOLDER Tests)

add_executable(oot3d_native_render_command_stream_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_render_command_stream_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_render_command_stream.cpp
)
target_compile_features(oot3d_native_render_command_stream_tests PRIVATE cxx_std_20)
oot3d_target_defaults(oot3d_native_render_command_stream_tests NO_WARNING_OVERRIDE FOLDER Tests)

add_executable(oot3d_native_preparation_screen_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_preparation_screen_tests.cpp
)
target_compile_features(oot3d_native_preparation_screen_tests PRIVATE cxx_std_20)
target_include_directories(oot3d_native_preparation_screen_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/three_ds_recomp/include)
oot3d_target_defaults(oot3d_native_preparation_screen_tests NO_WARNING_OVERRIDE FOLDER Tests)

add_executable(ctr_y2r_service_tests EXCLUDE_FROM_ALL
    tools/ctr_services/y2r_service_tests.cpp
)
target_compile_features(ctr_y2r_service_tests PRIVATE cxx_std_20)
set_property(TARGET ctr_y2r_service_tests PROPERTY FOLDER Tests)

# Zstandard (BSD) compresses save states, as Azahar does. Static, library only;
# offline builds provide FETCHCONTENT_SOURCE_DIR_ZSTD like the other packages.
if(NOT TARGET libzstd_static)
    include(FetchContent)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
    set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(zstd
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG v1.5.6
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR build/cmake)
    FetchContent_MakeAvailable(zstd)
endif()
if(NOT TARGET triaevum_zstd)
    add_library(triaevum_zstd INTERFACE)
    target_link_libraries(triaevum_zstd INTERFACE libzstd_static)
    target_include_directories(triaevum_zstd INTERFACE "${zstd_SOURCE_DIR}/lib")
endif()
target_link_libraries(oot3d_native_audio_storage_tests PRIVATE triaevum_zstd)

add_library(oot3d_native_a32_savestate STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_savestate.cpp
    tools/oot3d/native_game_runtime/oot3d_savestate_library.cpp
    tools/oot3d/native_game_runtime/oot3d_savestate_migration.cpp
)
target_include_directories(oot3d_native_a32_savestate PUBLIC
    tools/oot3d/native_game_runtime
    titles/oot3d/topscreen
    titles/oot3d/gameplay
    titles/oot3d/environment
)
target_link_libraries(oot3d_native_a32_savestate PUBLIC
    nlohmann_json::nlohmann_json
    oot3d_renderer_contracts
    oot3d_native_a32_ctr_host
    oot3d_native_pica_submission
)
target_link_libraries(oot3d_native_a32_savestate PRIVATE triaevum_zstd)
oot3d_target_defaults(oot3d_native_a32_savestate FOLDER Tools)

add_executable(oot3d_native_a32_savestate_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_savestate_tests.cpp
)
target_link_libraries(oot3d_native_a32_savestate_tests PRIVATE
    oot3d_native_a32_savestate triaevum_zstd)
oot3d_target_defaults(oot3d_native_a32_savestate_tests FOLDER Tests)

# Developer tool: what differs between two states of one game moment.
add_executable(oot3d_native_a32_savestate_diff EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_savestate_diff.cpp
)
target_link_libraries(oot3d_native_a32_savestate_diff PRIVATE
    oot3d_native_a32_savestate)
oot3d_target_defaults(oot3d_native_a32_savestate_diff FOLDER Tools)

if (TARGET oot3d_native_a32_aot)
    add_executable(oot3d_native_a32_boot_probe EXCLUDE_FROM_ALL
        tools/oot3d/native_game_runtime/oot3d_native_a32_boot_probe.cpp
    )
    target_link_libraries(oot3d_native_a32_boot_probe PRIVATE
        oot3d_native_a32_process_image
        oot3d_native_a32_ctr_host
        oot3d_native_pica_submission
        oot3d_native_pica_vulkan_plan
        oot3d_native_a32_aot
    )
    if(OOT3D_ENABLE_VULKAN_RENDERER AND TARGET three_ds_recomp_shaderc)
        target_link_libraries(oot3d_native_a32_boot_probe PRIVATE
            three_ds_recomp_shaderc)
        target_compile_definitions(oot3d_native_a32_boot_probe PRIVATE
            OOT3D_NATIVE_HAS_SHADERC=1)
        add_custom_command(TARGET oot3d_native_a32_boot_probe POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:three_ds_recomp_shaderc>
                    $<TARGET_FILE_DIR:oot3d_native_a32_boot_probe>
            COMMENT "Deploying shaderc for oot3d_native_a32_boot_probe")
    endif()
    oot3d_target_defaults(oot3d_native_a32_boot_probe FOLDER Tools)
endif()
