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

# Private title modules retain the exact CTR/SVC implementation but may only
# reach graphics through the stable host-service ABI. This variant therefore
# has no direct renderer dependency or callable PICA frontend path.
add_library(oot3d_native_a32_ctr_service_host STATIC EXCLUDE_FROM_ALL
    ${_oot3d_native_a32_ctr_host_sources})
target_include_directories(oot3d_native_a32_ctr_service_host PUBLIC
    tools/oot3d/native_game_runtime)
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
        tools/oot3d/native_game_runtime)
    target_link_libraries(oot3d_game_module PRIVATE
        oot3d_native_a32_process_image
        oot3d_native_a32_ctr_service_host
        oot3d_ui_topscreen
        oot3d_triaevum_pica_client_bridge
        triaevum_module
        nlohmann_json::nlohmann_json)
    if (OOT3D_DIRECT_AOT_PLUGIN)
        target_link_libraries(oot3d_game_module PRIVATE
            triaevum_title_aot_stub)
    endif()
    target_compile_options(oot3d_game_module PRIVATE "${WARNING_OVERRIDE}")
    set_target_properties(oot3d_game_module PROPERTIES
        PREFIX ""
        OUTPUT_NAME "oot3d_game_module")
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

add_library(oot3d_mm3d_guest_patches STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/mm3d_guest_patches.cpp
)
target_include_directories(oot3d_mm3d_guest_patches PUBLIC
    tools/oot3d/native_game_runtime)
target_link_libraries(oot3d_mm3d_guest_patches PUBLIC
    oot3d_native_a32_process)
oot3d_target_defaults(oot3d_mm3d_guest_patches FOLDER Tools)

add_executable(oot3d_mm3d_guest_patches_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/mm3d_guest_patches_tests.cpp
)
target_link_libraries(oot3d_mm3d_guest_patches_tests PRIVATE
    oot3d_mm3d_guest_patches)
oot3d_target_defaults(oot3d_mm3d_guest_patches_tests FOLDER Tests)

add_executable(oot3d_native_a32_ctr_host_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_host_tests.cpp
)
target_link_libraries(oot3d_native_a32_ctr_host_tests PRIVATE
    oot3d_native_a32_ctr_host)
oot3d_target_defaults(oot3d_native_a32_ctr_host_tests FOLDER Tests)

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
)
target_include_directories(oot3d_native_a32_savestate PUBLIC
    tools/oot3d/native_game_runtime
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
