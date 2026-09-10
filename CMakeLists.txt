cmake_minimum_required(VERSION 3.26.0 FATAL_ERROR)

if(CMAKE_HOST_WIN32 AND NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_SYSTEM_VERSION 10.0 CACHE STRING "" FORCE)
endif()
set(CMAKE_CXX_STANDARD 20 CACHE STRING "The C++ standard to use")
set(CMAKE_C_STANDARD 23 CACHE STRING "The C standard to use")

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")

project(Oot3dRecomp VERSION 0.1.0 LANGUAGES C CXX)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
include(CMake/runtime-cvars.cmake)
set(SPDLOG_LEVEL_TRACE 0)
set(SPDLOG_LEVEL_OFF 6)
set(SPDLOG_MIN_CUTOFF SPDLOG_LEVEL_TRACE CACHE STRING "cutoff at trace")

option(SUPPRESS_WARNINGS "Suppress warnings in the runtime and decompiled sources" ON)
if(SUPPRESS_WARNINGS)
    message(STATUS "Suppressing warnings in runtime and decompiled sources")
    if(MSVC)
        set(WARNING_OVERRIDE /w)
    else()
        set(WARNING_OVERRIDE -w)
    endif()
else()
    MESSAGE("Skipping warning suppression")
endif()

set(GIT_BRANCH "source-archive")
set(GIT_COMMIT_HASH "unknown")
set(GIT_COMMIT_TAG "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" branch --show-current
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE GIT_BRANCH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE GIT_COMMIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0 --exact-match HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE GIT_COMMIT_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endif()
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/SOURCE_ARCHIVE_MANIFEST.json")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/SOURCE_ARCHIVE_MANIFEST.json"
         SOURCE_ARCHIVE_MANIFEST_JSON)
    string(REGEX MATCH
        "\"source_commit\"[ \t\r\n]*:[ \t\r\n]*\"([0-9a-fA-F]+)\""
        SOURCE_ARCHIVE_COMMIT_MATCH "${SOURCE_ARCHIVE_MANIFEST_JSON}")
    if(CMAKE_MATCH_1)
        set(GIT_COMMIT_HASH "${CMAKE_MATCH_1}")
    endif()
endif()

if(NOT GIT_BRANCH)
    set(GIT_BRANCH "detached")
endif()
string(LENGTH "${GIT_COMMIT_HASH}" GIT_COMMIT_HASH_LENGTH)
if(GIT_COMMIT_HASH_LENGTH GREATER_EQUAL 7)
    string(SUBSTRING "${GIT_COMMIT_HASH}" 0 7 SHORT_COMMIT_HASH)
else()
    set(SHORT_COMMIT_HASH "unknown")
endif()

set(CMAKE_PROJECT_GIT_BRANCH "${GIT_BRANCH}" CACHE STRING "Git branch" FORCE)
set(CMAKE_PROJECT_GIT_COMMIT_HASH "${SHORT_COMMIT_HASH}" CACHE STRING "Git commit hash" FORCE)
set(GIT_COMMIT_TAG "${GIT_COMMIT_TAG}" CACHE STRING "Git commit tag" FORCE)

set(CMAKE_PROJECT_GIT_COMMIT_TAG "${GIT_COMMIT_TAG}" CACHE STRING "Git commit tag" FORCE)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT oot3d_native_game)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/MP>)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(NOT CMAKE_VS_PLATFORM_NAME)
        set(CMAKE_VS_PLATFORM_NAME "x64")
    endif()

    if("${CMAKE_VS_PLATFORM_NAME}" MATCHES "^[Aa][Rr][Mm]64$")
        set(OOT3D_WINDOWS_ARM64 TRUE)
    else()
        set(OOT3D_WINDOWS_ARM64 FALSE)
    endif()

    include(CMake/automate-vcpkg.cmake)

    set(VCPKG_TRIPLET x64-windows-static)
    set(VCPKG_TARGET_TRIPLET x64-windows-static)
    if(OOT3D_WINDOWS_ARM64)
        set(VCPKG_TRIPLET arm64-windows-static)
        set(VCPKG_TARGET_TRIPLET arm64-windows-static)
    endif()

    vcpkg_bootstrap()
    vcpkg_install_packages(zlib bzip2 libzip libpng sdl2 glew glfw3 nlohmann-json tinyxml2 spdlog libogg libvorbis opus opusfile)
    if (CMAKE_C_COMPILER_LAUNCHER MATCHES "ccache|sccache")
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
    endif()
endif()

################################################################################
# Set target arch type if empty. Visual studio solution generator provides it.
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(NOT CMAKE_VS_PLATFORM_NAME)
        set(CMAKE_VS_PLATFORM_NAME "x64")
    endif()
    message("${CMAKE_VS_PLATFORM_NAME} architecture in use")

    if(NOT ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64"
        OR "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32"
        OR OOT3D_WINDOWS_ARM64))
        message(FATAL_ERROR "${CMAKE_VS_PLATFORM_NAME} arch is not supported!")
    endif()
endif()

################################################################################
# Global configuration types
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
set(CMAKE_C_FLAGS_DEBUG "-g -ffast-math -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -ffast-math -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -ffast-math -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -ffast-math -DNDEBUG")
else()
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_OBJCXX_FLAGS_RELEASE "-O2 -DNDEBUG")
endif()
# IEEE 754 compliant floating-point rounding on arm64 macOS
if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_compile_options(-Xarch_arm64 -ffp-contract=off)
endif()

if(NOT CMAKE_BUILD_TYPE )
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
endif()

################################################################################
# Common utils
################################################################################
include(CMake/Utils.cmake)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    get_linux_lsb_release_information()
    message(STATUS "Linux ${LSB_RELEASE_ID_SHORT} ${LSB_RELEASE_VERSION_SHORT} ${LSB_RELEASE_CODENAME_SHORT}")
else()
    message(STATUS ${CMAKE_SYSTEM_NAME})
endif()

################################################################################
# Additional Global Settings(add specific info there)
################################################################################
include(CMake/GlobalSettingsInclude.cmake OPTIONAL)

################################################################################
# Use solution folders feature
################################################################################
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

################################################################################
# Configure the retained Fast3D compatibility layer
################################################################################

# The temporary Fast3D compatibility adapter still consumes F3DEX_GBI_2.
set(GBI_UCODE F3DEX_GBI_2)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

################################################################################
# Set CONTROLLERBUTTONS_T
################################################################################
add_compile_definitions(CONTROLLERBUTTONS_T=uint32_t)

################################################################################
# Sub-projects
################################################################################
option(OOT3D_FAST_DEV_LINK
    "Disable large linker debug artifacts during iterative native builds" OFF)
option(OOT3D_ENABLE_VULKAN_RENDERER
    "Build the Vulkan renderer used by native OoT3D executables" ON)
set(THREE_DS_RECOMP_ENABLE_VULKAN ${OOT3D_ENABLE_VULKAN_RENDERER} CACHE BOOL
    "Build the native 3DS Vulkan backend" FORCE)
add_subdirectory(runtime/triaevum_module
    ${CMAKE_BINARY_DIR}/triaevum_module)
add_subdirectory(runtime/three_ds_recomp
    ${CMAKE_BINARY_DIR}/three_ds_recomp_runtime)
if (NOT OOT3D_WHOLE_AOT_PRODUCT_MODE)
    add_subdirectory(tools/oot3d/source_overlay
        ${CMAKE_BINARY_DIR}/oot3d_source_overlay)
endif()
target_compile_options(three_ds_recomp_runtime PRIVATE "${WARNING_OVERRIDE}")
add_library(oot3d_room_compilation_runtime STATIC
    tools/oot3d/runtime_common/oot3d_native_abi_catalog.cpp
    tools/oot3d/runtime_common/oot3d_room_compilation_unit.cpp
)
target_include_directories(oot3d_room_compilation_runtime PRIVATE
    tools/oot3d/runtime_common
)
get_target_property(OOT3D_ROOM_RUNTIME_INTERFACE_INCLUDES
    oot3d_room_compilation_runtime INTERFACE_INCLUDE_DIRECTORIES)
if ("${OOT3D_ROOM_RUNTIME_INTERFACE_INCLUDES}" MATCHES
    "tools[/\\\\]oot3d[/\\\\]runtime_common")
    message(FATAL_ERROR
        "oot3d_room_compilation_runtime must not publish runtime_common includes; "
        "doing so invalidates unrelated runtime translation units")
endif()
unset(OOT3D_ROOM_RUNTIME_INTERFACE_INCLUDES)
target_link_libraries(oot3d_room_compilation_runtime PUBLIC
    nlohmann_json::nlohmann_json)
target_compile_options(oot3d_room_compilation_runtime PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_room_compilation_runtime PRIVATE NOMINMAX)
    set_property(TARGET oot3d_room_compilation_runtime PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_room_compilation_runtime PROPERTY FOLDER Tools)
add_executable(oot3d_room_compilation_runtime_tests EXCLUDE_FROM_ALL
    tools/oot3d/runtime_common/oot3d_room_compilation_unit_tests.cpp
)
target_link_libraries(oot3d_room_compilation_runtime_tests PRIVATE oot3d_room_compilation_runtime)
target_include_directories(oot3d_room_compilation_runtime_tests PRIVATE
    tools/oot3d/runtime_common
)
target_compile_options(oot3d_room_compilation_runtime_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_room_compilation_runtime_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_room_compilation_runtime_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_room_compilation_runtime_tests PROPERTY FOLDER Tests)
set(OOT3D_DECOMP_DEVELOPMENT_SOURCE_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/decomp_support/src/code")
if (EXISTS "${OOT3D_DECOMP_DEVELOPMENT_SOURCE_ROOT}/z_oot3d_math.c" AND
    EXISTS "${OOT3D_DECOMP_DEVELOPMENT_SOURCE_ROOT}/z_scene_cutscene_frame_runtime.c")
add_library(oot3d_native_math STATIC EXCLUDE_FROM_ALL
    tools/oot3d/decomp_support/src/code/z_oot3d_math.c
)
target_include_directories(oot3d_native_math PUBLIC
    tools/oot3d/decomp_support/include
)
target_compile_options(oot3d_native_math PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_property(TARGET oot3d_native_math PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_math PROPERTY FOLDER Tools)

add_library(oot3d_native_cutscene_host STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_demo_host/oot3d_demo_host_camera_controls.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_context.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_diagnostics.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_cli.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_io.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_input.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_link_controls.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_playback_frame.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_render_frame.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_screenshot.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_self_test.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_self_test_camera.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_self_test_link.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_self_test_playback.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_summary.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_title_trace.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_view_projection.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_window_demo.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_window_summary.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_window_state.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_window_timing.cpp
    tools/oot3d/native_demo_host/oot3d_link_collision.cpp
    tools/oot3d/native_demo_host/oot3d_link_instance.cpp
    tools/oot3d/native_demo_host/oot3d_link_animation.cpp
    tools/oot3d/native_demo_host/oot3d_link_locomotion_config.cpp
    tools/oot3d/native_demo_host/oot3d_link_movement.cpp
    tools/oot3d/native_demo_host/oot3d_link_render.cpp
    tools/oot3d/native_demo_host/oot3d_link_surface_state.cpp
    tools/oot3d/native_demo_host/oot3d_native_bg_camera.cpp
    tools/oot3d/native_demo_host/oot3d_native_camera_collision.cpp
    tools/oot3d/native_demo_host/oot3d_native_camera_config.cpp
    tools/oot3d/native_demo_host/oot3d_native_camera_controller.cpp
    tools/oot3d/native_demo_host/oot3d_native_camera_normal.cpp
    tools/oot3d/native_demo_host/oot3d_native_camera_runtime.cpp
    tools/oot3d/native_demo_host/oot3d_native_camera_scene.cpp
    tools/oot3d/native_demo_host/oot3d_native_collision_math.cpp
    tools/oot3d/native_demo_host/oot3d_intro_cutscene_playback.cpp
    tools/oot3d/native_demo_host/oot3d_intro_cutscene_status_names.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_actor_assets.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_actor_diagnostics.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_actor_render.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_effects.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_camera.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_cue_sampling.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_diagnostics.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_logo_render.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_logo_runtime.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_opening_frame_runtime.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_opening_resolver.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_playback_init.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_playback_render.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_render_binding_diagnostics.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_render_guard.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_render_scene.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_runtime_environment.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_status_names.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_trace.cpp
    tools/oot3d/native_demo_host/oot3d_title_intro_visibility_diagnostics.cpp
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_camera_blob_table.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_camera_cmad_apply.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_camera_cmad_table.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_camera_curve_eval.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_camera_keyframe_table.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_camera_runtime.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_frame_runtime.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_direct_player_event_table.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_intro_camera_timeline.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_intro_runtime.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_lighting_table.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_misc_action_table.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_set_time_table.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_transition_handoff_table.c
    tools/oot3d/decomp_support/src/code/z_scene_global_entrance_table.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_native_source_table.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_player_action_table.c
    tools/oot3d/decomp_support/src/code/z_scene_cutscene_source_table.c
    tools/oot3d/decomp_support/src/code/z_title_intro_source_table.c
    tools/oot3d/decomp_support/src/code/z_title_intro_opening_orchestration.c
    tools/oot3d/decomp_support/src/code/z_title_intro_opening_resolver_runtime.c
    tools/oot3d/decomp_support/src/code/z_title_intro_opening_player_motion.c
    tools/oot3d/decomp_support/src/code/z_title_intro_opening_actor_runtime.c
    tools/oot3d/decomp_support/src/code/z_title_intro_opening_camera_runtime.c
    tools/oot3d/decomp_support/src/code/z_title_intro_opening_logo_runtime.c
    tools/oot3d/decomp_support/src/code/z_title_intro_runtime_tables.c
    tools/oot3d/decomp_support/src/code/z_title_intro_player_resource_visibility.c
    tools/oot3d/decomp_support/src/code/z_title_intro_record_c_runtime.c
    tools/oot3d/decomp_support/src/code/z_title_intro_playback_runtime.c
    tools/oot3d/decomp_support/src/code/z_title_intro_source_selector_runtime.c
    tools/oot3d/decomp_support/src/code/z_title_intro_source_selector_feed_runtime.c
    tools/oot3d/decomp_support/src/code/z_title_intro_cutscene_runtime.c
    tools/oot3d/decomp_support/src/code/z_title_intro_opening_frame_runtime.c
)
target_link_libraries(oot3d_native_cutscene_host PUBLIC three_ds_recomp_runtime oot3d_native_math)
target_include_directories(oot3d_native_cutscene_host PUBLIC tools/oot3d/decomp_support/include)
target_compile_options(oot3d_native_cutscene_host PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_cutscene_host PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_cutscene_host PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_cutscene_host PROPERTY FOLDER Tools)
add_library(oot3d_native_runtime_host ALIAS oot3d_native_cutscene_host)
endif()

add_library(oot3d_native_application_host STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_demo_host/oot3d_demo_host_context.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_io.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_screenshot.cpp
    tools/oot3d/native_demo_host/oot3d_demo_host_window_timing.cpp
)
target_include_directories(oot3d_native_application_host PUBLIC
    tools/oot3d/native_demo_host
    tools/oot3d/decomp_support/include
)
target_link_libraries(oot3d_native_application_host PUBLIC three_ds_recomp_runtime)
target_compile_options(oot3d_native_application_host PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_application_host PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_application_host PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_application_host PROPERTY FOLDER Tools)

add_library(oot3d_native_audio STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_cseq.cpp
    tools/oot3d/native_game_runtime/oot3d_native_audio.cpp
    tools/oot3d/native_game_runtime/oot3d_native_audio_dsp.cpp
    tools/oot3d/native_game_runtime/oot3d_native_audio_resampler.cpp
    tools/oot3d/native_game_runtime/oot3d_native_audio_service.cpp
)
target_include_directories(oot3d_native_audio PUBLIC
    tools/oot3d/native_game_runtime
)
target_compile_options(oot3d_native_audio PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_audio PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_audio PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_audio PROPERTY FOLDER Tools)

set(OOT3D_PINNED_A32_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_a32_runtime/upstream")
set(OOT3D_A32_AOT_GENERATED_DIR
    "${CMAKE_BINARY_DIR}/oot3d_a32_generated"
    CACHE PATH "Generated revision-pinned OoT3D A32 AOT shards")
set(OOT3D_WHOLE_AOT_GENERATED_DIR
    "${CMAKE_BINARY_DIR}/oot3d_whole_aot_cpp"
    CACHE PATH "Generated whole-function OoT3D A32 C++ AOT shards")
set(OOT3D_WHOLE_AOT_PROGRAM
    "${CMAKE_BINARY_DIR}/oot3d_whole_aot/aot_program.json"
    CACHE FILEPATH "Audited structural program consumed by whole-AOT codegen")
set(OOT3D_WHOLE_AOT_SELECTION
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_a32_runtime/whole_aot_functions.json"
    CACHE FILEPATH "Byte-exact whole-AOT function selection")
set(OOT3D_WHOLE_AOT_CODE_BIN "" CACHE FILEPATH
    "Legally extracted code.bin used by the whole-AOT product audit")
set(OOT3D_WHOLE_AOT_EXHEADER "" CACHE FILEPATH
    "Legally extracted ExHeader used by the whole-AOT product audit")
set(OOT3D_WHOLE_AOT_PRODUCT_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_a32_runtime/whole_aot_product_manifest.json"
    CACHE FILEPATH "Immutable whole-AOT product closure contract")
set(OOT3D_WHOLE_AOT_PREBUILT_LIBRARY
    "${CMAKE_BINARY_DIR}/oot3d_native_whole_aot.lib"
    CACHE FILEPATH "Prebuilt whole-function OoT3D A32 AOT archive")
set(OOT3D_WHOLE_AOT_THINLTO_CACHE_DIR "" CACHE PATH
    "Persistent lld ThinLTO cache for whole-AOT consumer links")
option(OOT3D_REBUILD_WHOLE_AOT
    "Compile all generated whole-function AOT shards (expensive)" OFF)
option(OOT3D_REGENERATE_WHOLE_AOT
    "Regenerate whole-AOT C++ before compiling the shards" ON)
option(OOT3D_REQUIRE_WHOLE_AOT
    "Fail configuration when the whole-function AOT backend is unavailable" OFF)
option(OOT3D_WHOLE_AOT_PRODUCT_MODE
    "Require audited zero-residual whole-AOT execution" OFF)
option(OOT3D_DIRECT_AOT_PLUGIN
    "Consume title code from the stable TriAevum direct-AOT plugin ABI" OFF)
set(OOT3D_WHOLE_AOT_MAX_PARALLEL_COMPILES "2" CACHE STRING
    "Maximum concurrent whole-AOT shard compilations")
set(OOT3D_WHOLE_AOT_SHARD_COUNT "256" CACHE STRING
    "Generated whole-AOT C++ shard count")
option(OOT3D_WHOLE_AOT_THINLTO
    "Compile and link the whole-AOT archive with LLVM ThinLTO" OFF)
set(OOT3D_WHOLE_AOT_PROFILE_MODE "none" CACHE STRING
    "LLVM whole-AOT profile mode: none, generate, use, or sample-use")
set_property(CACHE OOT3D_WHOLE_AOT_PROFILE_MODE PROPERTY STRINGS
    none generate use sample-use)
set(OOT3D_WHOLE_AOT_PROFILE_FILE "" CACHE FILEPATH
    "LLVM indexed or sample profile consumed by whole-AOT profile-use")
set(OOT3D_WHOLE_AOT_ORDER_FILE "" CACHE FILEPATH
    "Optional linker symbol order file for the final whole-AOT consumer")
if (OOT3D_WHOLE_AOT_PRODUCT_MODE AND NOT OOT3D_DIRECT_AOT_PLUGIN)
    set(OOT3D_REQUIRE_WHOLE_AOT ON CACHE BOOL
        "Fail configuration when the whole-function AOT backend is unavailable"
        FORCE)
endif()
set(OOT3D_MASS_AOT_GENERATED_DIR "" CACHE PATH
    "Generated mass-direct OoT3D C++ metadata directory")
set(OOT3D_MASS_AOT_PREBUILT_LIBRARY "" CACHE FILEPATH
    "Prebuilt mass-direct OoT3D C++ ThinLTO archive")
add_library(oot3d_native_a32_runtime STATIC EXCLUDE_FROM_ALL
    ${OOT3D_PINNED_A32_ROOT}/recomp/a32_core.cpp
    ${OOT3D_PINNED_A32_ROOT}/recomp/a32_core_alu.cpp
    ${OOT3D_PINNED_A32_ROOT}/recomp/a32_core_memory.cpp
    ${OOT3D_PINNED_A32_ROOT}/recomp/a32_runtime.cpp
    ${OOT3D_PINNED_A32_ROOT}/recomp/a32_vfp_binary64.cpp
    tools/oot3d/native_a32_runtime/mass_cpp/recomp/a32_direct_runtime.cpp
    tools/oot3d/native_a32_runtime/oot3d_native_a32_vfp_ops.cpp
    ${OOT3D_PINNED_A32_ROOT}/recomp/a32_vfp_transport.cpp
)
target_include_directories(oot3d_native_a32_runtime PUBLIC
    ${OOT3D_PINNED_A32_ROOT}
    ${OOT3D_PINNED_A32_ROOT}/recomp
    tools/oot3d/native_a32_runtime
    tools/oot3d/native_a32_runtime/mass_cpp
)
target_compile_options(oot3d_native_a32_runtime PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_options(oot3d_native_a32_runtime PRIVATE
        $<$<CONFIG:RelWithDebInfo>:/Ob2>)
    target_compile_definitions(oot3d_native_a32_runtime PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_a32_runtime PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_runtime PROPERTY FOLDER Tools)

if(OOT3D_WHOLE_AOT_PRODUCT_MODE)
    # The generated product still uses the guest-state and exact VFP ABI as
    # data contracts.  Keep those helpers separate so the decoded A32 core,
    # direct runtime, and interpreter cannot enter a whole-AOT product link.
    add_library(oot3d_whole_aot_architecture_support STATIC EXCLUDE_FROM_ALL
        ${OOT3D_PINNED_A32_ROOT}/recomp/a32_vfp_binary64.cpp
        tools/oot3d/native_a32_runtime/oot3d_native_a32_vfp_ops.cpp
        ${OOT3D_PINNED_A32_ROOT}/recomp/a32_vfp_transport.cpp)
    target_include_directories(oot3d_whole_aot_architecture_support PUBLIC
        ${OOT3D_PINNED_A32_ROOT}
        ${OOT3D_PINNED_A32_ROOT}/recomp
        tools/oot3d/native_a32_runtime
        tools/oot3d/native_a32_runtime/mass_cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(oot3d_whole_aot_architecture_support PRIVATE
            -O2 -fno-fast-math -ffp-contract=off -fno-rtti)
    endif()
    set_property(TARGET oot3d_whole_aot_architecture_support PROPERTY
        FOLDER Tools)

    # Title-neutral support linked into the private whole-AOT plugin produced
    # by Forge.  Keep this boundary narrow: generated title code needs only
    # the exact guest-memory and VFP semantics, never the host/game runtime.
    include(tools/triaevum_release/cmake/TitleSupport.cmake)
    triaevum_add_title_support("${CMAKE_CURRENT_SOURCE_DIR}" "${OOT3D_PINNED_A32_ROOT}")
    target_compile_options(triaevum_title_whole_aot_support PRIVATE
        "${WARNING_OVERRIDE}")
endif()

set(_oot3d_a32_generated_header
    "${OOT3D_A32_AOT_GENERATED_DIR}/oot3d_a32_generated.h")
set(_oot3d_a32_generated_registry
    "${OOT3D_A32_AOT_GENERATED_DIR}/registry.cpp")
set(_oot3d_a32_generated_true_aot_header
    "${OOT3D_A32_AOT_GENERATED_DIR}/oot3d_a32_true_aot_generated.h")
set(_oot3d_a32_generated_true_aot_source
    "${OOT3D_A32_AOT_GENERATED_DIR}/oot3d_a32_true_aot_generated.cpp")
if (EXISTS "${_oot3d_a32_generated_header}" AND
    EXISTS "${_oot3d_a32_generated_registry}" AND
    EXISTS "${_oot3d_a32_generated_true_aot_header}" AND
    EXISTS "${_oot3d_a32_generated_true_aot_source}")
    file(GLOB _oot3d_a32_generated_shards CONFIGURE_DEPENDS
        "${OOT3D_A32_AOT_GENERATED_DIR}/shard_*.cpp")
    add_library(oot3d_native_a32_aot STATIC EXCLUDE_FROM_ALL
        ${_oot3d_a32_generated_header}
        ${_oot3d_a32_generated_registry}
        ${_oot3d_a32_generated_true_aot_header}
        ${_oot3d_a32_generated_true_aot_source}
        ${_oot3d_a32_generated_shards}
    )
    target_include_directories(oot3d_native_a32_aot PUBLIC
        ${OOT3D_PINNED_A32_ROOT}
        ${OOT3D_A32_AOT_GENERATED_DIR}
    )
    target_link_libraries(oot3d_native_a32_aot PUBLIC
        oot3d_native_a32_runtime)
    target_compile_options(oot3d_native_a32_aot PRIVATE "${WARNING_OVERRIDE}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(oot3d_native_a32_aot PRIVATE NOMINMAX)
        set_property(TARGET oot3d_native_a32_aot PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(TARGET oot3d_native_a32_aot PROPERTY FOLDER Tools)
endif()

add_executable(oot3d_native_a32_runtime_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_a32_runtime/oot3d_native_a32_runtime_tests.cpp
)
target_link_libraries(oot3d_native_a32_runtime_tests PRIVATE
    oot3d_native_a32_runtime)
target_compile_options(oot3d_native_a32_runtime_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_a32_runtime_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_a32_runtime_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_runtime_tests PROPERTY FOLDER Tests)

if (NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json REQUIRED)
endif()

add_library(oot3d_typed_gameplay STATIC EXCLUDE_FROM_ALL
    tools/oot3d/typed_gameplay/oot3d_gameplay_actor.cpp
    tools/oot3d/typed_gameplay/oot3d_gameplay_animation.cpp
    tools/oot3d/typed_gameplay/oot3d_gameplay_camera.cpp
    tools/oot3d/typed_gameplay/oot3d_gameplay_camera_animation.cpp
    tools/oot3d/typed_gameplay/oot3d_gameplay_cutscene.cpp
    tools/oot3d/typed_gameplay/oot3d_gameplay_math.cpp
    tools/oot3d/typed_gameplay/oot3d_gameplay_player.cpp
    tools/oot3d/typed_gameplay/oot3d_gameplay_quake.cpp
    tools/oot3d/typed_gameplay/oot3d_gameplay_skel_anime.cpp
    tools/oot3d/typed_gameplay/oot3d_gameplay_time.cpp
)
target_include_directories(oot3d_typed_gameplay PUBLIC
    tools/oot3d/typed_gameplay
)
target_compile_options(oot3d_typed_gameplay PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_typed_gameplay PRIVATE NOMINMAX)
    set_property(TARGET oot3d_typed_gameplay PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_typed_gameplay PROPERTY FOLDER Tools)

add_executable(oot3d_gameplay_time_tests EXCLUDE_FROM_ALL
    tools/oot3d/typed_gameplay/oot3d_gameplay_time_tests.cpp
)
target_link_libraries(oot3d_gameplay_time_tests PRIVATE
    oot3d_typed_gameplay)
target_compile_options(oot3d_gameplay_time_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_gameplay_time_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_gameplay_time_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_gameplay_time_tests PROPERTY FOLDER Tests)

set(OOT3D_ACTOR_UPDATE_ALL_SOURCE_IMPORT
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/source_imports/actor_update_all_449e219e")
add_library(oot3d_source_actor_update_all_oracle OBJECT EXCLUDE_FROM_ALL
    "${OOT3D_ACTOR_UPDATE_ALL_SOURCE_IMPORT}/src/code/z_owner_actor_runtime.c"
)
target_include_directories(oot3d_source_actor_update_all_oracle PUBLIC
    "${OOT3D_ACTOR_UPDATE_ALL_SOURCE_IMPORT}/include"
)
target_compile_definitions(oot3d_source_actor_update_all_oracle PRIVATE
    OOT3D_HOST_OWNER_ACTOR_GLOBAL_RESOLVER=1
    OOT3D_HOST_OWNER_ACTOR_REGISTRY_CALLBACK_RESOLVER=1
    OOT3D_HOST_ACTOR_SPAWN_PTR32_RESOLVER=1
)
target_compile_options(oot3d_source_actor_update_all_oracle PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_source_actor_update_all_oracle PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_source_actor_update_all_oracle PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_source_actor_update_all_oracle PROPERTY FOLDER Tools)

set(OOT3D_ACTOR_INIT_CONTEXT_SOURCE_IMPORT
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/source_imports/actor_init_context_d5c2927")
add_library(oot3d_source_actor_init_context_owner OBJECT EXCLUDE_FROM_ALL
    "${OOT3D_ACTOR_INIT_CONTEXT_SOURCE_IMPORT}/src/code/z_actor_init_context_owner.cpp"
)
target_include_directories(oot3d_source_actor_init_context_owner PUBLIC
    "${OOT3D_ACTOR_INIT_CONTEXT_SOURCE_IMPORT}/include"
)
target_compile_options(oot3d_source_actor_init_context_owner PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_source_actor_init_context_owner PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_source_actor_init_context_owner PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_source_actor_init_context_owner PROPERTY FOLDER Tools)

set(OOT3D_CUTSCENE_UPDATE_FRAME_SOURCE_IMPORT
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/source_imports/cutscene_update_frame_d5c2927")
add_library(oot3d_source_cutscene_update_frame_owner OBJECT EXCLUDE_FROM_ALL
    "${OOT3D_CUTSCENE_UPDATE_FRAME_SOURCE_IMPORT}/src/code/z_cutscene_update_frame_owner.cpp"
)
target_include_directories(oot3d_source_cutscene_update_frame_owner PUBLIC
    "${OOT3D_CUTSCENE_UPDATE_FRAME_SOURCE_IMPORT}/include"
)
target_compile_options(oot3d_source_cutscene_update_frame_owner PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_cutscene_update_frame_owner PRIVATE NOMINMAX)
    set_property(TARGET oot3d_source_cutscene_update_frame_owner PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_source_cutscene_update_frame_owner PROPERTY FOLDER Tools)

set(OOT3D_CUTSCENE_PROCESS_COMMANDS_SOURCE_IMPORT
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/source_imports/cutscene_process_commands_d5c2927")
add_library(oot3d_source_cutscene_process_commands_owner OBJECT EXCLUDE_FROM_ALL
    "${OOT3D_CUTSCENE_PROCESS_COMMANDS_SOURCE_IMPORT}/src/code/z_cutscene_process_commands_owner.cpp"
)
target_include_directories(oot3d_source_cutscene_process_commands_owner PUBLIC
    "${OOT3D_CUTSCENE_PROCESS_COMMANDS_SOURCE_IMPORT}/include"
)
target_compile_options(oot3d_source_cutscene_process_commands_owner PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_cutscene_process_commands_owner PRIVATE NOMINMAX)
    set_property(TARGET oot3d_source_cutscene_process_commands_owner PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_source_cutscene_process_commands_owner PROPERTY FOLDER Tools)

add_executable(
    oot3d_source_cutscene_process_commands_guest_pointer_tests
    EXCLUDE_FROM_ALL
    "${OOT3D_CUTSCENE_PROCESS_COMMANDS_SOURCE_IMPORT}/tests/guest_pointer_tests.cpp"
)
target_include_directories(
    oot3d_source_cutscene_process_commands_guest_pointer_tests PRIVATE
    "${OOT3D_CUTSCENE_PROCESS_COMMANDS_SOURCE_IMPORT}/include"
)
target_compile_options(
    oot3d_source_cutscene_process_commands_guest_pointer_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_cutscene_process_commands_guest_pointer_tests PRIVATE
        NOMINMAX)
    set_property(
        TARGET oot3d_source_cutscene_process_commands_guest_pointer_tests
        PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(
    TARGET oot3d_source_cutscene_process_commands_guest_pointer_tests
    PROPERTY FOLDER Tests)

set(OOT3D_CAMERA_UPDATE_SOURCE_IMPORT
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/source_imports/camera_update_d5c2927")
add_library(oot3d_source_camera_update_owner OBJECT EXCLUDE_FROM_ALL
    "${OOT3D_CAMERA_UPDATE_SOURCE_IMPORT}/src/code/z_camera_update_owner.cpp"
)
target_include_directories(oot3d_source_camera_update_owner PUBLIC
    "${OOT3D_CAMERA_UPDATE_SOURCE_IMPORT}/include"
)
target_compile_options(oot3d_source_camera_update_owner PRIVATE
    "${WARNING_OVERRIDE}")
if (MSVC)
    target_compile_options(
        oot3d_source_camera_update_owner PRIVATE /fp:strict)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        oot3d_source_camera_update_owner PRIVATE -frounding-math)
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_camera_update_owner PRIVATE NOMINMAX)
    set_property(TARGET oot3d_source_camera_update_owner PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_source_camera_update_owner PROPERTY FOLDER Tools)

add_executable(
    oot3d_source_camera_update_guest_pointer_tests
    EXCLUDE_FROM_ALL
    "${OOT3D_CAMERA_UPDATE_SOURCE_IMPORT}/tests/guest_pointer_tests.cpp"
)
target_include_directories(
    oot3d_source_camera_update_guest_pointer_tests PRIVATE
    "${OOT3D_CAMERA_UPDATE_SOURCE_IMPORT}/include"
)
target_compile_options(
    oot3d_source_camera_update_guest_pointer_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_camera_update_guest_pointer_tests PRIVATE NOMINMAX)
    set_property(
        TARGET oot3d_source_camera_update_guest_pointer_tests PROPERTY
        MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(
    TARGET oot3d_source_camera_update_guest_pointer_tests
    PROPERTY FOLDER Tests)

set(OOT3D_PLAYER_UPDATE_SOURCE_IMPORT
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/source_imports/player_update_d5c2927")
add_library(oot3d_source_player_update_owner OBJECT EXCLUDE_FROM_ALL
    "${OOT3D_PLAYER_UPDATE_SOURCE_IMPORT}/src/code/z_player_update_owner.cpp"
)
target_include_directories(oot3d_source_player_update_owner PUBLIC
    "${OOT3D_PLAYER_UPDATE_SOURCE_IMPORT}/include"
)
target_compile_options(oot3d_source_player_update_owner PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_player_update_owner PRIVATE NOMINMAX)
    set_property(TARGET oot3d_source_player_update_owner PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_source_player_update_owner PROPERTY FOLDER Tools)

add_executable(
    oot3d_source_player_update_guest_pointer_tests
    EXCLUDE_FROM_ALL
    "${OOT3D_PLAYER_UPDATE_SOURCE_IMPORT}/tests/guest_pointer_tests.cpp"
)
target_include_directories(
    oot3d_source_player_update_guest_pointer_tests PRIVATE
    "${OOT3D_PLAYER_UPDATE_SOURCE_IMPORT}/include"
)
target_compile_options(
    oot3d_source_player_update_guest_pointer_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_player_update_guest_pointer_tests PRIVATE NOMINMAX)
    set_property(
        TARGET oot3d_source_player_update_guest_pointer_tests
        PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(
    TARGET oot3d_source_player_update_guest_pointer_tests
    PROPERTY FOLDER Tests)

set(OOT3D_PLAYER_UPDATE_COMMON_SOURCE_IMPORT
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/source_imports/player_update_common_d5c2927")
add_library(
    oot3d_source_player_update_common_owner OBJECT EXCLUDE_FROM_ALL
    "${OOT3D_PLAYER_UPDATE_COMMON_SOURCE_IMPORT}/src/code/z_player_update_common_owner.cpp"
)
target_include_directories(
    oot3d_source_player_update_common_owner PUBLIC
    "${OOT3D_PLAYER_UPDATE_COMMON_SOURCE_IMPORT}/include"
)
target_compile_options(
    oot3d_source_player_update_common_owner PRIVATE
    "${WARNING_OVERRIDE}")
if (MSVC)
    target_compile_options(
        oot3d_source_player_update_common_owner PRIVATE /fp:strict)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        oot3d_source_player_update_common_owner PRIVATE -frounding-math)
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_player_update_common_owner PRIVATE NOMINMAX)
    set_property(
        TARGET oot3d_source_player_update_common_owner PROPERTY
        MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(
    TARGET oot3d_source_player_update_common_owner PROPERTY FOLDER Tools)

add_executable(
    oot3d_source_player_update_common_guest_pointer_tests
    EXCLUDE_FROM_ALL
    "${OOT3D_PLAYER_UPDATE_COMMON_SOURCE_IMPORT}/tests/guest_pointer_tests.cpp"
)
target_include_directories(
    oot3d_source_player_update_common_guest_pointer_tests PRIVATE
    "${OOT3D_PLAYER_UPDATE_COMMON_SOURCE_IMPORT}/include"
)
target_compile_options(
    oot3d_source_player_update_common_guest_pointer_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_player_update_common_guest_pointer_tests PRIVATE NOMINMAX)
    set_property(
        TARGET oot3d_source_player_update_common_guest_pointer_tests
        PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(
    TARGET oot3d_source_player_update_common_guest_pointer_tests
    PROPERTY FOLDER Tests)

set(OOT3D_CSAB_CURVE_SOURCE_IMPORT
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/source_imports/csab_curve_eval_d5c2927")
add_library(
    oot3d_source_csab_curve_eval OBJECT EXCLUDE_FROM_ALL
    "${OOT3D_CSAB_CURVE_SOURCE_IMPORT}/src/code/z_csab_curve_eval.cpp"
)
target_include_directories(
    oot3d_source_csab_curve_eval PUBLIC
    "${OOT3D_CSAB_CURVE_SOURCE_IMPORT}/include"
)
target_compile_options(
    oot3d_source_csab_curve_eval PRIVATE
    "${WARNING_OVERRIDE}")
if (MSVC)
    target_compile_options(
        oot3d_source_csab_curve_eval PRIVATE /fp:strict)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        oot3d_source_csab_curve_eval PRIVATE -frounding-math)
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_csab_curve_eval PRIVATE NOMINMAX)
    set_property(
        TARGET oot3d_source_csab_curve_eval PROPERTY
        MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(
    TARGET oot3d_source_csab_curve_eval PROPERTY FOLDER Tools)

add_executable(
    oot3d_source_gameplay_profile_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_source_gameplay_profile_tests.cpp
)
target_include_directories(
    oot3d_source_gameplay_profile_tests PRIVATE
    tools/oot3d/native_game_runtime
)
target_compile_options(
    oot3d_source_gameplay_profile_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_gameplay_profile_tests PRIVATE NOMINMAX)
    set_property(
        TARGET oot3d_source_gameplay_profile_tests
        PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(
    TARGET oot3d_source_gameplay_profile_tests PROPERTY FOLDER Tests)

set(OOT3D_OBJ_HANA_INIT_SOURCE_IMPORT
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/source_imports/obj_hana_init_d5c2927")
add_library(oot3d_source_obj_hana_init_callback OBJECT EXCLUDE_FROM_ALL
    "${OOT3D_OBJ_HANA_INIT_SOURCE_IMPORT}/src/code/z_obj_hana_init_owner.cpp"
)
target_include_directories(oot3d_source_obj_hana_init_callback PUBLIC
    "${OOT3D_OBJ_HANA_INIT_SOURCE_IMPORT}/include"
)
target_compile_options(oot3d_source_obj_hana_init_callback PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_source_obj_hana_init_callback PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_source_obj_hana_init_callback PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_source_obj_hana_init_callback PROPERTY FOLDER Tools)

add_library(oot3d_native_a32_process STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_memory.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_process.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_registry.cpp
    tools/oot3d/native_game_runtime/oot3d_native_owner_call_adapter.cpp
    tools/oot3d/native_game_runtime/oot3d_native_compiled_functions.cpp
    tools/oot3d/native_game_runtime/oot3d_native_mass_aot.cpp
    tools/oot3d/native_game_runtime/oot3d_native_source_overlay.cpp
    tools/oot3d/native_game_runtime/oot3d_typed_actor_lifecycle.cpp
    tools/oot3d/native_game_runtime/oot3d_typed_actor_update_records.cpp
    tools/oot3d/native_game_runtime/oot3d_typed_audio_request_callback.cpp
    tools/oot3d/native_game_runtime/oot3d_typed_dyna_interaction_reset.cpp
    tools/oot3d/native_game_runtime/oot3d_typed_game_state.cpp
    tools/oot3d/native_game_runtime/oot3d_typed_pause_ui_alpha.cpp
    tools/oot3d/native_game_runtime/oot3d_typed_player_lock_on.cpp
    tools/oot3d/native_game_runtime/oot3d_typed_record_initializer.cpp
    tools/oot3d/native_game_runtime/oot3d_typed_gameplay_bridge.cpp
    tools/oot3d/native_game_runtime/oot3d_native_true_aot_blocks.cpp
    tools/oot3d/native_game_runtime/oot3d_native_whole_aot_runtime.cpp
)
target_include_directories(oot3d_native_a32_process PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(oot3d_native_a32_process PUBLIC
    oot3d_typed_gameplay
    nlohmann_json::nlohmann_json)
if(OOT3D_WHOLE_AOT_PRODUCT_MODE)
    if(OOT3D_DIRECT_AOT_PLUGIN)
        target_sources(oot3d_native_a32_process PRIVATE
            tools/oot3d/native_game_runtime/oot3d_native_direct_aot.cpp)
        target_link_libraries(oot3d_native_a32_process PUBLIC
            oot3d_native_a32_runtime ${CMAKE_DL_LIBS})
        target_compile_definitions(oot3d_native_a32_process PRIVATE
            OOT3D_NATIVE_DIRECT_AOT_PLUGIN=1
            OOT3D_NATIVE_GENERATED_WHOLE_AOT=1
            OOT3D_WHOLE_AOT_PRODUCT_MODE=1)
    else()
        target_sources(oot3d_native_a32_process PRIVATE
            tools/oot3d/native_game_runtime/oot3d_memory_bus_abi.cpp)
        target_link_libraries(oot3d_native_a32_process PUBLIC
            oot3d_whole_aot_architecture_support)
    endif()
else()
    target_link_libraries(oot3d_native_a32_process PUBLIC
        oot3d_native_a32_runtime
        oot3d_source_overlay_loader)
endif()
if (TARGET oot3d_native_a32_aot)
    target_include_directories(oot3d_native_a32_process PRIVATE
        ${OOT3D_A32_AOT_GENERATED_DIR})
    target_compile_definitions(oot3d_native_a32_process PRIVATE
        OOT3D_NATIVE_GENERATED_TRUE_AOT=1)
endif()
set(_oot3d_whole_aot_generated_header
    "${OOT3D_WHOLE_AOT_GENERATED_DIR}/oot3d_whole_aot_generated.h")
set(_oot3d_whole_aot_generated_source
    "${OOT3D_WHOLE_AOT_GENERATED_DIR}/oot3d_whole_aot_generated.cpp")
set(_oot3d_whole_aot_generated_manifest
    "${OOT3D_WHOLE_AOT_GENERATED_DIR}/whole_aot_cpp_manifest.json")
if (OOT3D_WHOLE_AOT_PRODUCT_MODE AND NOT OOT3D_DIRECT_AOT_PLUGIN)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    foreach(_oot3d_product_input IN ITEMS
            "${OOT3D_WHOLE_AOT_PROGRAM}"
            "${OOT3D_WHOLE_AOT_SELECTION}"
            "${OOT3D_WHOLE_AOT_CODE_BIN}"
            "${OOT3D_WHOLE_AOT_EXHEADER}"
            "${OOT3D_WHOLE_AOT_PRODUCT_MANIFEST}"
            "${_oot3d_whole_aot_generated_manifest}")
        if (NOT EXISTS "${_oot3d_product_input}")
            message(FATAL_ERROR
                "Whole-AOT product input does not exist: ${_oot3d_product_input}. "
                "Run prepare_whole_aot_product.py before configuring.")
        endif()
    endforeach()
    execute_process(
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_a32_runtime/audit_whole_aot_product.py"
            --manifest "${OOT3D_WHOLE_AOT_PRODUCT_MANIFEST}"
            --program "${OOT3D_WHOLE_AOT_PROGRAM}"
            --selection "${OOT3D_WHOLE_AOT_SELECTION}"
            --code "${OOT3D_WHOLE_AOT_CODE_BIN}"
            --exheader "${OOT3D_WHOLE_AOT_EXHEADER}"
            --generated-manifest "${_oot3d_whole_aot_generated_manifest}"
            --skip-generated-file-hashes
        RESULT_VARIABLE _oot3d_product_audit_result
        OUTPUT_VARIABLE _oot3d_product_audit_output
        ERROR_VARIABLE _oot3d_product_audit_error)
    if (NOT _oot3d_product_audit_result EQUAL 0)
        message(FATAL_ERROR
            "Whole-AOT product audit failed during configure:\n"
            "${_oot3d_product_audit_output}${_oot3d_product_audit_error}")
    endif()
endif()
if (EXISTS "${_oot3d_whole_aot_generated_header}" AND
    EXISTS "${_oot3d_whole_aot_generated_source}")
    if (OOT3D_REBUILD_WHOLE_AOT)
        file(GLOB _oot3d_whole_aot_generated_shards CONFIGURE_DEPENDS
            "${OOT3D_WHOLE_AOT_GENERATED_DIR}/oot3d_whole_aot_shard_*.cpp")
        add_library(oot3d_native_whole_aot STATIC EXCLUDE_FROM_ALL
            ${_oot3d_whole_aot_generated_header}
            ${_oot3d_whole_aot_generated_source}
            ${_oot3d_whole_aot_generated_shards})
        target_include_directories(oot3d_native_whole_aot PRIVATE
            ${OOT3D_WHOLE_AOT_GENERATED_DIR}
            tools/oot3d/native_game_runtime
            ${OOT3D_PINNED_A32_ROOT}
            tools/oot3d/native_a32_runtime)
        if (OOT3D_WHOLE_AOT_PRODUCT_MODE)
            # Product builds consume only generated native functions plus the
            # architectural data/VFP ABI.  The decoded A32 runtime is a legacy
            # development dependency and must not enter the product closure.
            target_link_libraries(oot3d_native_whole_aot PUBLIC
                oot3d_whole_aot_architecture_support
                nlohmann_json::nlohmann_json)
        else()
            target_link_libraries(oot3d_native_whole_aot PUBLIC
                oot3d_native_a32_runtime
                nlohmann_json::nlohmann_json)
        endif()
        target_compile_options(oot3d_native_whole_aot PRIVATE
            "${WARNING_OVERRIDE}")
        if (NOT MSVC)
            # Affinity-sharded guest callees are deliberately colocated so
            # normal release builds can remove their translated call ABI.
            target_compile_options(oot3d_native_whole_aot PRIVATE
                -finline-functions)
        endif()
        if (OOT3D_WHOLE_AOT_THINLTO OR
            NOT OOT3D_WHOLE_AOT_PROFILE_MODE STREQUAL "none")
            if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                message(FATAL_ERROR
                    "Whole-AOT ThinLTO/profile modes require clang-cl/LLVM")
            endif()
        endif()
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            execute_process(
                COMMAND "${CMAKE_CXX_COMPILER}" /clang:--print-resource-dir
                OUTPUT_VARIABLE _oot3d_clang_resource_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE
                COMMAND_ERROR_IS_FATAL ANY)
            set(_oot3d_clang_builtins
                "${_oot3d_clang_resource_dir}/lib/windows/clang_rt.builtins-x86_64.lib")
            if (NOT EXISTS "${_oot3d_clang_builtins}")
                message(FATAL_ERROR
                    "LLVM builtins library does not exist: ${_oot3d_clang_builtins}")
            endif()
            target_link_libraries(oot3d_native_whole_aot INTERFACE
                "${_oot3d_clang_builtins}")
        endif()
        if (OOT3D_WHOLE_AOT_THINLTO)
            target_compile_options(oot3d_native_whole_aot PRIVATE
                -flto=thin)
        endif()
        if (OOT3D_WHOLE_AOT_PROFILE_MODE STREQUAL "generate")
            target_compile_options(oot3d_native_whole_aot PRIVATE
                -fprofile-instr-generate)
            set(_oot3d_clang_profile_runtime
                "${_oot3d_clang_resource_dir}/lib/windows/clang_rt.profile-x86_64.lib")
            if (NOT EXISTS "${_oot3d_clang_profile_runtime}")
                message(FATAL_ERROR
                    "LLVM profile runtime does not exist: "
                    "${_oot3d_clang_profile_runtime}")
            endif()
            target_link_libraries(oot3d_native_whole_aot INTERFACE
                "${_oot3d_clang_profile_runtime}")
        elseif (OOT3D_WHOLE_AOT_PROFILE_MODE STREQUAL "use" OR
                OOT3D_WHOLE_AOT_PROFILE_MODE STREQUAL "sample-use")
            if (NOT EXISTS "${OOT3D_WHOLE_AOT_PROFILE_FILE}")
                message(FATAL_ERROR
                    "OOT3D_WHOLE_AOT_PROFILE_FILE does not exist")
            endif()
            if (OOT3D_WHOLE_AOT_PROFILE_MODE STREQUAL "use")
                set(_oot3d_whole_aot_profile_option
                    "-fprofile-instr-use=${OOT3D_WHOLE_AOT_PROFILE_FILE}")
            else()
                set(_oot3d_whole_aot_profile_option
                    "-fprofile-sample-use=${OOT3D_WHOLE_AOT_PROFILE_FILE}")
            endif()
            target_compile_options(oot3d_native_whole_aot PRIVATE
                "${_oot3d_whole_aot_profile_option}")
        elseif (NOT OOT3D_WHOLE_AOT_PROFILE_MODE STREQUAL "none")
            message(FATAL_ERROR
                "Invalid OOT3D_WHOLE_AOT_PROFILE_MODE: "
                "${OOT3D_WHOLE_AOT_PROFILE_MODE}")
        endif()
        if (OOT3D_WHOLE_AOT_ORDER_FILE)
            if (NOT EXISTS "${OOT3D_WHOLE_AOT_ORDER_FILE}")
                message(FATAL_ERROR
                    "OOT3D_WHOLE_AOT_ORDER_FILE does not exist")
            endif()
            target_link_options(oot3d_native_whole_aot INTERFACE
                "/ORDER:@${OOT3D_WHOLE_AOT_ORDER_FILE}")
        endif()
        set_property(GLOBAL APPEND PROPERTY JOB_POOLS
            oot3d_whole_aot_compile=${OOT3D_WHOLE_AOT_MAX_PARALLEL_COMPILES})
        set_property(TARGET oot3d_native_whole_aot PROPERTY
            JOB_POOL_COMPILE oot3d_whole_aot_compile)
        if (MSVC)
            # Generated shards do not share a compiler PDB. /Zi plus /FS
            # serialized otherwise independent translation units.
            target_compile_options(oot3d_native_whole_aot PRIVATE /Z7)
            set_property(TARGET oot3d_native_whole_aot PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
        if (OOT3D_REGENERATE_WHOLE_AOT)
            find_package(Python3 REQUIRED COMPONENTS Interpreter)
            set(_oot3d_whole_aot_code_arguments)
            set(_oot3d_whole_aot_code_dependencies)
            if (OOT3D_WHOLE_AOT_CODE_BIN)
                list(APPEND _oot3d_whole_aot_code_arguments
                    --code "${OOT3D_WHOLE_AOT_CODE_BIN}")
                list(APPEND _oot3d_whole_aot_code_dependencies
                    "${OOT3D_WHOLE_AOT_CODE_BIN}")
            endif()
            set(_oot3d_whole_aot_generation_stamp
                "${OOT3D_WHOLE_AOT_GENERATED_DIR}/generation.stamp")
            add_custom_command(
                OUTPUT "${_oot3d_whole_aot_generation_stamp}"
                COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_a32_runtime/generate_whole_aot_cpp.py"
                    --program "${OOT3D_WHOLE_AOT_PROGRAM}"
                    --selection "${OOT3D_WHOLE_AOT_SELECTION}"
                    ${_oot3d_whole_aot_code_arguments}
                    --output "${OOT3D_WHOLE_AOT_GENERATED_DIR}"
                    --shards "${OOT3D_WHOLE_AOT_SHARD_COUNT}"
                    --shard-strategy affinity
                COMMAND ${CMAKE_COMMAND} -E touch
                    "${_oot3d_whole_aot_generation_stamp}"
                DEPENDS
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_a32_runtime/generate_whole_aot_cpp.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_a32_runtime/generate_whole_aot.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_a32_runtime/generate_aot.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_a32_runtime/whole_aot_cpp.py"
                    "${OOT3D_WHOLE_AOT_SELECTION}"
                    "${OOT3D_WHOLE_AOT_PROGRAM}"
                    ${_oot3d_whole_aot_code_dependencies}
                COMMENT "Regenerating affinity whole-AOT C++ shards"
                VERBATIM)
            add_custom_target(oot3d_generate_whole_aot_cpp
                DEPENDS "${_oot3d_whole_aot_generation_stamp}")
            add_dependencies(oot3d_native_whole_aot
                oot3d_generate_whole_aot_cpp)
        endif()
        set_property(TARGET oot3d_native_whole_aot PROPERTY FOLDER Tools)
    elseif (EXISTS "${OOT3D_WHOLE_AOT_PREBUILT_LIBRARY}")
        add_library(oot3d_native_whole_aot STATIC IMPORTED GLOBAL)
        set_target_properties(oot3d_native_whole_aot PROPERTIES
            IMPORTED_LOCATION "${OOT3D_WHOLE_AOT_PREBUILT_LIBRARY}")
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            execute_process(
                COMMAND "${CMAKE_CXX_COMPILER}" /clang:--print-resource-dir
                OUTPUT_VARIABLE _oot3d_clang_resource_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE
                COMMAND_ERROR_IS_FATAL ANY)
            set(_oot3d_clang_builtins
                "${_oot3d_clang_resource_dir}/lib/windows/clang_rt.builtins-x86_64.lib")
            if (NOT EXISTS "${_oot3d_clang_builtins}")
                message(FATAL_ERROR
                    "LLVM builtins library does not exist: ${_oot3d_clang_builtins}")
            endif()
            target_link_libraries(oot3d_native_whole_aot INTERFACE
                "${_oot3d_clang_builtins}")
        endif()
        # Compile the small registry in the consumer so runtime dispatch
        # contracts can evolve without rebuilding every prebuilt AOT shard.
        # The archive registry member is then left unselected by the linker.
        target_sources(oot3d_native_a32_process PRIVATE
            "${_oot3d_whole_aot_generated_source}")
    endif()
    if (TARGET oot3d_native_whole_aot)
        if (OOT3D_WHOLE_AOT_PRODUCT_MODE)
            add_custom_target(oot3d_audit_whole_aot_product
                COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_a32_runtime/audit_whole_aot_product.py"
                    --manifest "${OOT3D_WHOLE_AOT_PRODUCT_MANIFEST}"
                    --program "${OOT3D_WHOLE_AOT_PROGRAM}"
                    --selection "${OOT3D_WHOLE_AOT_SELECTION}"
                    --code "${OOT3D_WHOLE_AOT_CODE_BIN}"
                    --exheader "${OOT3D_WHOLE_AOT_EXHEADER}"
                    --generated-manifest
                    "${_oot3d_whole_aot_generated_manifest}"
                DEPENDS
                    "${OOT3D_WHOLE_AOT_PROGRAM}"
                    "${OOT3D_WHOLE_AOT_SELECTION}"
                    "${OOT3D_WHOLE_AOT_CODE_BIN}"
                    "${OOT3D_WHOLE_AOT_EXHEADER}"
                    "${OOT3D_WHOLE_AOT_PRODUCT_MANIFEST}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_a32_runtime/whole_aot_functions.json"
                    "${_oot3d_whole_aot_generated_manifest}"
                COMMENT "Auditing zero-residual whole-AOT product closure"
                VERBATIM)
            if (TARGET oot3d_generate_whole_aot_cpp)
                add_dependencies(oot3d_audit_whole_aot_product
                    oot3d_generate_whole_aot_cpp)
            endif()
            add_dependencies(oot3d_native_a32_process
                oot3d_audit_whole_aot_product)
            target_compile_definitions(oot3d_native_a32_process PRIVATE
                OOT3D_WHOLE_AOT_PRODUCT_MODE=1)
        endif()
        if (OOT3D_WHOLE_AOT_THINLTO_CACHE_DIR)
            file(MAKE_DIRECTORY "${OOT3D_WHOLE_AOT_THINLTO_CACHE_DIR}")
            target_link_options(oot3d_native_whole_aot INTERFACE
                "/lldltocache:${OOT3D_WHOLE_AOT_THINLTO_CACHE_DIR}")
        endif()
        target_link_libraries(oot3d_native_a32_process PUBLIC
            oot3d_native_whole_aot)
        target_include_directories(oot3d_native_a32_process PRIVATE
            ${OOT3D_WHOLE_AOT_GENERATED_DIR})
        target_compile_definitions(oot3d_native_a32_process PRIVATE
            OOT3D_NATIVE_GENERATED_WHOLE_AOT=1)
        if (OOT3D_REBUILD_WHOLE_AOT)
            target_compile_definitions(oot3d_native_whole_aot PRIVATE
                OOT3D_NATIVE_GENERATED_WHOLE_AOT=1)
        endif()
    endif()
endif()
if (OOT3D_REQUIRE_WHOLE_AOT AND NOT TARGET oot3d_native_whole_aot AND
    NOT OOT3D_DIRECT_AOT_PLUGIN)
    message(FATAL_ERROR
        "OOT3D_REQUIRE_WHOLE_AOT is ON, but no whole-AOT backend was "
        "configured. Generated metadata must exist in "
        "${OOT3D_WHOLE_AOT_GENERATED_DIR} and either "
        "OOT3D_REBUILD_WHOLE_AOT must be ON or "
        "OOT3D_WHOLE_AOT_PREBUILT_LIBRARY must name an existing archive "
        "(configured value: ${OOT3D_WHOLE_AOT_PREBUILT_LIBRARY}).")
endif()
if (EXISTS "${OOT3D_MASS_AOT_GENERATED_DIR}/oot3d_mass_generated.h" AND
    EXISTS "${OOT3D_MASS_AOT_GENERATED_DIR}/oot3d_mass_function_boundaries.h" AND
    EXISTS "${OOT3D_MASS_AOT_PREBUILT_LIBRARY}")
    add_library(oot3d_native_mass_aot STATIC IMPORTED GLOBAL)
    set_target_properties(oot3d_native_mass_aot PROPERTIES
        IMPORTED_LOCATION "${OOT3D_MASS_AOT_PREBUILT_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OOT3D_MASS_AOT_GENERATED_DIR}")
    target_link_libraries(oot3d_native_a32_process PUBLIC
        oot3d_native_mass_aot)
    target_include_directories(oot3d_native_a32_process PRIVATE
        ${OOT3D_MASS_AOT_GENERATED_DIR}
        tools/oot3d/native_a32_runtime/mass_cpp)
    target_compile_definitions(oot3d_native_a32_process PRIVATE
        OOT3D_NATIVE_GENERATED_MASS_AOT=1)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" /clang:--print-resource-dir
            OUTPUT_VARIABLE _oot3d_mass_clang_resource_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY)
        set(_oot3d_mass_clang_builtins
            "${_oot3d_mass_clang_resource_dir}/lib/windows/clang_rt.builtins-x86_64.lib")
        if (NOT EXISTS "${_oot3d_mass_clang_builtins}")
            message(FATAL_ERROR
                "LLVM builtins library does not exist: ${_oot3d_mass_clang_builtins}")
        endif()
        target_link_libraries(oot3d_native_mass_aot INTERFACE
            "${_oot3d_mass_clang_builtins}")
    endif()
endif()

if (TARGET oot3d_native_a32_aot)
    add_executable(oot3d_typed_gameplay_tests EXCLUDE_FROM_ALL
        tools/oot3d/native_game_runtime/oot3d_typed_gameplay_tests.cpp
    )
    target_include_directories(oot3d_typed_gameplay_tests PRIVATE
        ${OOT3D_A32_AOT_GENERATED_DIR}
    )
    target_link_libraries(oot3d_typed_gameplay_tests PRIVATE
        oot3d_native_a32_process
        oot3d_native_a32_aot
    )
    target_compile_options(oot3d_typed_gameplay_tests PRIVATE
        "${WARNING_OVERRIDE}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(oot3d_typed_gameplay_tests PRIVATE NOMINMAX)
        set_property(TARGET oot3d_typed_gameplay_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(TARGET oot3d_typed_gameplay_tests PROPERTY FOLDER Tests)

    add_executable(oot3d_typed_player_gameplay_tests EXCLUDE_FROM_ALL
        tools/oot3d/native_game_runtime/oot3d_typed_player_gameplay_tests.cpp
    )
    target_include_directories(oot3d_typed_player_gameplay_tests PRIVATE
        ${OOT3D_A32_AOT_GENERATED_DIR}
    )
    target_link_libraries(oot3d_typed_player_gameplay_tests PRIVATE
        oot3d_native_a32_process
        oot3d_native_a32_aot
    )
    target_compile_options(oot3d_typed_player_gameplay_tests PRIVATE
        "${WARNING_OVERRIDE}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(oot3d_typed_player_gameplay_tests PRIVATE NOMINMAX)
        set_property(TARGET oot3d_typed_player_gameplay_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(TARGET oot3d_typed_player_gameplay_tests PROPERTY FOLDER Tests)
endif()
target_compile_options(oot3d_native_a32_process PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_options(oot3d_native_a32_process PRIVATE
        $<$<CONFIG:RelWithDebInfo>:/Ob2>)
    target_compile_definitions(oot3d_native_a32_process PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_a32_process PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_process PROPERTY FOLDER Tools)

add_library(oot3d_source_actor_callback_runtime STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_source_actor_callback_runtime.cpp
    $<TARGET_OBJECTS:oot3d_source_obj_hana_init_callback>
)
target_include_directories(oot3d_source_actor_callback_runtime PUBLIC
    tools/oot3d/native_game_runtime
)
target_include_directories(oot3d_source_actor_callback_runtime PRIVATE
    "${OOT3D_OBJ_HANA_INIT_SOURCE_IMPORT}/include"
)
target_compile_options(oot3d_source_actor_callback_runtime PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_source_actor_callback_runtime PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_source_actor_callback_runtime PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_source_actor_callback_runtime PROPERTY FOLDER Tools)

add_library(oot3d_source_actor_update_all_runtime STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_source_actor_update_all_runtime.cpp
    $<TARGET_OBJECTS:oot3d_source_actor_update_all_oracle>
)
target_include_directories(oot3d_source_actor_update_all_runtime PUBLIC
    tools/oot3d/native_game_runtime
)
target_include_directories(oot3d_source_actor_update_all_runtime PRIVATE
    "${OOT3D_ACTOR_UPDATE_ALL_SOURCE_IMPORT}/include"
)
target_link_libraries(oot3d_source_actor_update_all_runtime PUBLIC
    oot3d_native_a32_process
    oot3d_source_actor_callback_runtime
)
target_compile_options(oot3d_source_actor_update_all_runtime PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_source_actor_update_all_runtime PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_source_actor_update_all_runtime PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_source_actor_update_all_runtime PROPERTY FOLDER Tools)

add_library(oot3d_source_actor_init_context_runtime STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_source_actor_init_context_runtime.cpp
    $<TARGET_OBJECTS:oot3d_source_actor_init_context_owner>
)
target_include_directories(oot3d_source_actor_init_context_runtime PUBLIC
    tools/oot3d/native_game_runtime
)
target_include_directories(oot3d_source_actor_init_context_runtime PRIVATE
    "${OOT3D_ACTOR_INIT_CONTEXT_SOURCE_IMPORT}/include"
)
target_link_libraries(oot3d_source_actor_init_context_runtime PUBLIC
    oot3d_native_a32_process
)
target_compile_options(oot3d_source_actor_init_context_runtime PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_source_actor_init_context_runtime PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_source_actor_init_context_runtime PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_source_actor_init_context_runtime PROPERTY FOLDER Tools)

add_library(oot3d_source_cutscene_update_frame_runtime STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_source_cutscene_update_frame_runtime.cpp
    $<TARGET_OBJECTS:oot3d_source_cutscene_update_frame_owner>
)
target_include_directories(oot3d_source_cutscene_update_frame_runtime PUBLIC
    tools/oot3d/native_game_runtime
)
target_include_directories(oot3d_source_cutscene_update_frame_runtime PRIVATE
    "${OOT3D_CUTSCENE_UPDATE_FRAME_SOURCE_IMPORT}/include"
)
target_link_libraries(oot3d_source_cutscene_update_frame_runtime PUBLIC
    oot3d_native_a32_process
)
target_compile_options(oot3d_source_cutscene_update_frame_runtime PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_cutscene_update_frame_runtime PRIVATE NOMINMAX)
    set_property(TARGET oot3d_source_cutscene_update_frame_runtime PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_source_cutscene_update_frame_runtime PROPERTY FOLDER Tools)

add_library(oot3d_source_cutscene_process_commands_runtime STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_source_cutscene_process_commands_runtime.cpp
    $<TARGET_OBJECTS:oot3d_source_cutscene_process_commands_owner>
)
target_include_directories(
    oot3d_source_cutscene_process_commands_runtime PUBLIC
    tools/oot3d/native_game_runtime
)
target_include_directories(
    oot3d_source_cutscene_process_commands_runtime PRIVATE
    "${OOT3D_CUTSCENE_PROCESS_COMMANDS_SOURCE_IMPORT}/include"
)
target_link_libraries(
    oot3d_source_cutscene_process_commands_runtime PUBLIC
    oot3d_native_a32_process
)
target_compile_options(
    oot3d_source_cutscene_process_commands_runtime PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_cutscene_process_commands_runtime PRIVATE NOMINMAX)
    set_property(
        TARGET oot3d_source_cutscene_process_commands_runtime PROPERTY
        MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(
    TARGET oot3d_source_cutscene_process_commands_runtime PROPERTY
    FOLDER Tools)

add_library(oot3d_source_camera_update_runtime STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_source_camera_update_runtime.cpp
    $<TARGET_OBJECTS:oot3d_source_camera_update_owner>
)
target_include_directories(
    oot3d_source_camera_update_runtime PUBLIC
    tools/oot3d/native_game_runtime
)
target_include_directories(
    oot3d_source_camera_update_runtime PRIVATE
    "${OOT3D_CAMERA_UPDATE_SOURCE_IMPORT}/include"
)
target_link_libraries(
    oot3d_source_camera_update_runtime PUBLIC
    oot3d_native_a32_process
)
target_compile_options(
    oot3d_source_camera_update_runtime PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_camera_update_runtime PRIVATE NOMINMAX)
    set_property(
        TARGET oot3d_source_camera_update_runtime PROPERTY
        MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(
    TARGET oot3d_source_camera_update_runtime PROPERTY FOLDER Tools)

add_library(oot3d_source_player_update_runtime STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_source_player_update_runtime.cpp
    $<TARGET_OBJECTS:oot3d_source_player_update_owner>
)
target_include_directories(
    oot3d_source_player_update_runtime PUBLIC
    tools/oot3d/native_game_runtime
)
target_include_directories(
    oot3d_source_player_update_runtime PRIVATE
    "${OOT3D_PLAYER_UPDATE_SOURCE_IMPORT}/include"
)
target_link_libraries(
    oot3d_source_player_update_runtime PUBLIC
    oot3d_native_a32_process
)
target_compile_options(
    oot3d_source_player_update_runtime PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_player_update_runtime PRIVATE NOMINMAX)
    set_property(
        TARGET oot3d_source_player_update_runtime PROPERTY
        MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(
    TARGET oot3d_source_player_update_runtime PROPERTY FOLDER Tools)

add_library(
    oot3d_source_player_update_common_runtime STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_source_player_update_common_runtime.cpp
    $<TARGET_OBJECTS:oot3d_source_player_update_common_owner>
)
target_include_directories(
    oot3d_source_player_update_common_runtime PUBLIC
    tools/oot3d/native_game_runtime
)
target_include_directories(
    oot3d_source_player_update_common_runtime PRIVATE
    "${OOT3D_PLAYER_UPDATE_COMMON_SOURCE_IMPORT}/include"
)
target_link_libraries(
    oot3d_source_player_update_common_runtime PUBLIC
    oot3d_native_a32_process
)
target_compile_options(
    oot3d_source_player_update_common_runtime PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_player_update_common_runtime PRIVATE NOMINMAX)
    set_property(
        TARGET oot3d_source_player_update_common_runtime PROPERTY
        MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(
    TARGET oot3d_source_player_update_common_runtime PROPERTY FOLDER Tools)

add_library(
    oot3d_source_csab_curve_runtime STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_source_csab_curve_runtime.cpp
    tools/oot3d/native_game_runtime/oot3d_source_csab_curve_core.cpp
    $<TARGET_OBJECTS:oot3d_source_csab_curve_eval>
)
target_include_directories(
    oot3d_source_csab_curve_runtime PUBLIC
    tools/oot3d/native_game_runtime
)
target_include_directories(
    oot3d_source_csab_curve_runtime PRIVATE
    "${OOT3D_CSAB_CURVE_SOURCE_IMPORT}/include"
)
target_link_libraries(
    oot3d_source_csab_curve_runtime PUBLIC
    oot3d_native_a32_process
)
target_compile_options(
    oot3d_source_csab_curve_runtime PRIVATE
    "${WARNING_OVERRIDE}")
if (MSVC)
    target_compile_options(
        oot3d_source_csab_curve_runtime PRIVATE /fp:strict)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        oot3d_source_csab_curve_runtime PRIVATE -frounding-math)
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(
        oot3d_source_csab_curve_runtime PRIVATE NOMINMAX)
    set_property(
        TARGET oot3d_source_csab_curve_runtime PROPERTY
        MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(
    TARGET oot3d_source_csab_curve_runtime PROPERTY FOLDER Tools)

if (TARGET oot3d_native_a32_aot)
    add_executable(
        oot3d_source_csab_curve_runtime_tests EXCLUDE_FROM_ALL
        tools/oot3d/native_game_runtime/oot3d_source_csab_curve_runtime_tests.cpp
    )
    target_include_directories(
        oot3d_source_csab_curve_runtime_tests PRIVATE
        "${OOT3D_CSAB_CURVE_SOURCE_IMPORT}/include"
        ${OOT3D_A32_AOT_GENERATED_DIR}
    )
    target_link_libraries(
        oot3d_source_csab_curve_runtime_tests PRIVATE
        oot3d_source_csab_curve_runtime
        oot3d_native_a32_aot
    )
    target_compile_options(
        oot3d_source_csab_curve_runtime_tests PRIVATE
        "${WARNING_OVERRIDE}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(
            oot3d_source_csab_curve_runtime_tests PRIVATE NOMINMAX)
        set_property(
            TARGET oot3d_source_csab_curve_runtime_tests PROPERTY
            MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(
        TARGET oot3d_source_csab_curve_runtime_tests PROPERTY FOLDER Tests)
endif()

add_executable(oot3d_native_mass_aot_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_mass_aot_tests.cpp)
target_link_libraries(oot3d_native_mass_aot_tests PRIVATE
    oot3d_native_a32_process)
if (TARGET oot3d_native_a32_aot)
    target_link_libraries(oot3d_native_mass_aot_tests PRIVATE
        oot3d_native_a32_aot)
    target_include_directories(oot3d_native_mass_aot_tests PRIVATE
        ${OOT3D_A32_AOT_GENERATED_DIR})
endif()
if (TARGET oot3d_native_mass_aot)
    target_include_directories(oot3d_native_mass_aot_tests PRIVATE
        ${OOT3D_MASS_AOT_GENERATED_DIR}
        tools/oot3d/native_a32_runtime/mass_cpp)
endif()
target_compile_options(oot3d_native_mass_aot_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_mass_aot_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_mass_aot_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_mass_aot_tests PROPERTY FOLDER Tests)

if (NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json REQUIRED)
endif()
add_library(oot3d_native_a32_process_image STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_process_image.cpp
)
target_include_directories(oot3d_native_a32_process_image PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(oot3d_native_a32_process_image PUBLIC
    oot3d_native_a32_process
    nlohmann_json::nlohmann_json
)
target_compile_options(oot3d_native_a32_process_image PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_a32_process_image PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_a32_process_image PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_process_image PROPERTY FOLDER Tools)

find_package(spdlog REQUIRED)

add_library(oot3d_native_pica_frontend STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_draw_state.cpp
    tools/oot3d/native_pica_frontend/oot3d_native_pica_fragment_lighting_gen.cpp
    tools/oot3d/native_pica_frontend/oot3d_native_pica_fragment_shader_gen.cpp
    tools/oot3d/native_pica_frontend/oot3d_native_pica_frontend.cpp
    tools/oot3d/native_pica_frontend/oot3d_native_pica_proctex.cpp
    tools/oot3d/native_pica_frontend/oot3d_native_pica_program_descriptor.cpp
    tools/oot3d/native_pica_frontend/oot3d_native_pica_shader_gen.cpp
    tools/oot3d/third_party/azahar_pica/video_core/shader/generator/glsl_shader_decompiler.cpp
)
target_include_directories(oot3d_native_pica_frontend PUBLIC
    tools/oot3d/native_pica_frontend
    tools/oot3d/third_party/azahar_pica
    runtime/three_ds_recomp/include
)
target_link_libraries(oot3d_native_pica_frontend PUBLIC
    oot3d_renderer_fragment_lighting
    spdlog::spdlog
    nlohmann_json::nlohmann_json)
target_compile_options(oot3d_native_pica_frontend PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_frontend PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_frontend PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_frontend PROPERTY FOLDER Tools)

add_library(triaevum_oot3d_pica_service STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_oot3d_pica_host.cpp
    tools/oot3d/native_game_runtime/triaevum_oot3d_pica_memory.cpp
    tools/oot3d/native_game_runtime/triaevum_oot3d_pica_service.cpp
)
target_include_directories(triaevum_oot3d_pica_service PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(triaevum_oot3d_pica_service PUBLIC
    triaevum_module
    oot3d_native_pica_submission
)
target_compile_options(triaevum_oot3d_pica_service PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(triaevum_oot3d_pica_service PRIVATE NOMINMAX)
    set_property(TARGET triaevum_oot3d_pica_service PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET triaevum_oot3d_pica_service PROPERTY FOLDER Tools)

add_executable(triaevum_oot3d_pica_service_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_oot3d_pica_service_tests.cpp
)
target_link_libraries(triaevum_oot3d_pica_service_tests PRIVATE
    triaevum_oot3d_pica_service)
target_compile_options(triaevum_oot3d_pica_service_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(triaevum_oot3d_pica_service_tests PRIVATE
        NOMINMAX)
    set_property(TARGET triaevum_oot3d_pica_service_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET triaevum_oot3d_pica_service_tests PROPERTY FOLDER Tests)

add_executable(oot3d_native_pica_frontend_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_frontend_tests.cpp
)
target_link_libraries(oot3d_native_pica_frontend_tests PRIVATE
    oot3d_native_pica_frontend)
target_compile_options(oot3d_native_pica_frontend_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_frontend_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_frontend_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_frontend_tests PROPERTY FOLDER Tests)

add_executable(oot3d_native_pica_program_descriptor_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_program_descriptor_tests.cpp
)
target_link_libraries(oot3d_native_pica_program_descriptor_tests PRIVATE
    oot3d_native_pica_frontend)
target_compile_options(oot3d_native_pica_program_descriptor_tests PRIVATE
    "${WARNING_OVERRIDE}")
add_executable(oot3d_native_pica_transferable_cache_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_transferable_cache.cpp
    tools/oot3d/native_pica_frontend/oot3d_native_pica_transferable_cache_tests.cpp)
add_executable(oot3d_native_pica_cache_inventory EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_transferable_cache.cpp
    tools/oot3d/native_pica_frontend/oot3d_native_pica_cache_inventory.cpp
    runtime/three_ds_recomp/src/fast/renderer3ds/pica_nri_shader_contract.cpp)
target_link_libraries(oot3d_native_pica_cache_inventory PRIVATE oot3d_native_pica_frontend)
add_executable(oot3d_native_pica_capture_inventory EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_capture_inventory.cpp
    tools/oot3d/native_game_runtime/oot3d_native_pica_vulkan_plan.cpp
    tools/oot3d/native_game_runtime/oot3d_native_pica_vulkan_bridge.cpp
    runtime/three_ds_recomp/src/fast/oot3d/pica_pipeline_manifest.cpp
    runtime/three_ds_recomp/src/fast/oot3d/pica_aot_shader_pack.cpp
    runtime/three_ds_recomp/src/fast/renderer3ds/pica_nri_shader_contract.cpp)
target_include_directories(oot3d_native_pica_capture_inventory PRIVATE tools/oot3d/native_game_runtime)
target_link_libraries(oot3d_native_pica_capture_inventory PRIVATE
    oot3d_native_pica_frontend oot3d_native_pica_azahar_capture)
foreach(_cache_target oot3d_native_pica_transferable_cache_tests oot3d_native_pica_cache_inventory oot3d_native_pica_capture_inventory)
    target_compile_options(${_cache_target} PRIVATE "${WARNING_OVERRIDE}")
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(${_cache_target} PRIVATE NOMINMAX)
        set_property(TARGET ${_cache_target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(TARGET ${_cache_target} PROPERTY FOLDER Tools)
endforeach()

if(OOT3D_ENABLE_VULKAN_RENDERER AND TARGET three_ds_recomp_shaderc)
    target_link_libraries(oot3d_native_pica_program_descriptor_tests PRIVATE
        three_ds_recomp_shaderc)
    target_compile_definitions(oot3d_native_pica_program_descriptor_tests PRIVATE
        OOT3D_NATIVE_PICA_TEST_SHADERC=1)
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        add_custom_command(TARGET oot3d_native_pica_program_descriptor_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:three_ds_recomp_shaderc>
                    $<TARGET_FILE_DIR:oot3d_native_pica_program_descriptor_tests>
            COMMENT "Deploying shaderc for native PICA descriptor tests")
    endif()
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_program_descriptor_tests PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_native_pica_program_descriptor_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_program_descriptor_tests PROPERTY
    FOLDER Tests)

add_executable(oot3d_native_pica_aot_shader_pack_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_aot_shader_pack_tests.cpp
    runtime/three_ds_recomp/src/fast/oot3d/pica_aot_shader_pack.cpp
)
target_include_directories(oot3d_native_pica_aot_shader_pack_tests PRIVATE
    runtime/three_ds_recomp/include)
target_link_libraries(oot3d_native_pica_aot_shader_pack_tests PRIVATE
    nlohmann_json::nlohmann_json)
target_compile_options(oot3d_native_pica_aot_shader_pack_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_aot_shader_pack_tests PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_native_pica_aot_shader_pack_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_aot_shader_pack_tests PROPERTY
    FOLDER Tests)

add_executable(oot3d_native_pica_pipeline_manifest EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_pipeline_manifest.cpp
    runtime/three_ds_recomp/src/fast/oot3d/pica_aot_shader_pack.cpp
    runtime/three_ds_recomp/src/fast/oot3d/pica_pipeline_manifest.cpp
)
target_include_directories(oot3d_native_pica_pipeline_manifest PRIVATE
    runtime/three_ds_recomp/include)
target_link_libraries(oot3d_native_pica_pipeline_manifest PRIVATE
    nlohmann_json::nlohmann_json)
target_compile_options(oot3d_native_pica_pipeline_manifest PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_pipeline_manifest PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_native_pica_pipeline_manifest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_pipeline_manifest PROPERTY FOLDER Tools)

add_library(oot3d_native_pica_transfer STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_transfer.cpp
)
target_include_directories(oot3d_native_pica_transfer PUBLIC
    tools/oot3d/native_pica_frontend
)
target_link_libraries(oot3d_native_pica_transfer PUBLIC
    oot3d_native_a32_process)
target_compile_options(oot3d_native_pica_transfer PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_transfer PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_transfer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_transfer PROPERTY FOLDER Tools)

add_executable(oot3d_native_pica_transfer_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_transfer_tests.cpp
)
target_link_libraries(oot3d_native_pica_transfer_tests PRIVATE
    oot3d_native_pica_transfer)
target_compile_options(oot3d_native_pica_transfer_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_transfer_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_transfer_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_transfer_tests PROPERTY FOLDER Tests)

add_library(oot3d_native_pica_submission STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_submission.cpp
    tools/oot3d/native_game_runtime/oot3d_native_pica_a32_memory_view.cpp
)
target_include_directories(oot3d_native_pica_submission PUBLIC
    tools/oot3d/native_game_runtime
    tools/oot3d/native_pica_frontend
)
target_link_libraries(oot3d_native_pica_submission PUBLIC
    oot3d_native_a32_process
    oot3d_native_pica_frontend)
target_compile_options(oot3d_native_pica_submission PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_submission PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_submission PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_submission PROPERTY FOLDER Tools)

add_executable(oot3d_native_pica_submission_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_submission_tests.cpp
)
target_link_libraries(oot3d_native_pica_submission_tests PRIVATE
    oot3d_native_pica_submission)
target_compile_options(oot3d_native_pica_submission_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_submission_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_submission_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_submission_tests PROPERTY FOLDER Tests)

add_library(oot3d_native_pica_composition STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_composition.cpp
)
target_include_directories(oot3d_native_pica_composition PUBLIC
    tools/oot3d/native_game_runtime
    tools/oot3d/native_pica_frontend
)
target_link_libraries(oot3d_native_pica_composition PUBLIC
    oot3d_native_a32_process
    oot3d_native_pica_frontend)
target_compile_options(oot3d_native_pica_composition PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_composition PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_composition PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_composition PROPERTY FOLDER Tools)

add_executable(oot3d_native_pica_composition_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_composition_tests.cpp
)
target_link_libraries(oot3d_native_pica_composition_tests PRIVATE
    oot3d_native_pica_composition)
target_compile_options(oot3d_native_pica_composition_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_composition_tests PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_native_pica_composition_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_composition_tests PROPERTY FOLDER Tests)

add_library(oot3d_native_pica_vulkan_plan STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_vulkan_plan.cpp
    tools/oot3d/native_game_runtime/oot3d_native_pica_visual_frame.cpp
)
target_include_directories(oot3d_native_pica_vulkan_plan PUBLIC
    tools/oot3d/native_game_runtime
    tools/oot3d/native_pica_frontend
)
target_link_libraries(oot3d_native_pica_vulkan_plan PUBLIC
    oot3d_native_pica_submission)
target_compile_options(oot3d_native_pica_vulkan_plan PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_vulkan_plan PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_vulkan_plan PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_vulkan_plan PROPERTY FOLDER Tools)

add_executable(oot3d_native_pica_vulkan_plan_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_vulkan_plan_tests.cpp
)
target_link_libraries(oot3d_native_pica_vulkan_plan_tests PRIVATE
    oot3d_native_pica_vulkan_plan)
target_compile_options(oot3d_native_pica_vulkan_plan_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_vulkan_plan_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_vulkan_plan_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_vulkan_plan_tests PROPERTY FOLDER Tests)

add_library(oot3d_native_pica_semantic_trace STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_semantic_trace.cpp
)
target_include_directories(oot3d_native_pica_semantic_trace PUBLIC
    tools/oot3d/native_game_runtime
    tools/oot3d/native_pica_frontend
)
target_link_libraries(oot3d_native_pica_semantic_trace PUBLIC
    oot3d_native_pica_vulkan_plan
    nlohmann_json::nlohmann_json)
target_compile_options(oot3d_native_pica_semantic_trace PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_semantic_trace PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_semantic_trace PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_semantic_trace PROPERTY FOLDER Tools)

add_executable(oot3d_native_pica_semantic_trace_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_semantic_trace_tests.cpp
)
target_link_libraries(oot3d_native_pica_semantic_trace_tests PRIVATE
    oot3d_native_pica_semantic_trace)
target_compile_options(oot3d_native_pica_semantic_trace_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_semantic_trace_tests PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_native_pica_semantic_trace_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_semantic_trace_tests PROPERTY FOLDER Tests)

add_executable(oot3d_native_pica_trace_inventory EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_trace_inventory.cpp
)
target_link_libraries(oot3d_native_pica_trace_inventory PRIVATE
    nlohmann_json::nlohmann_json)
target_compile_options(oot3d_native_pica_trace_inventory PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_trace_inventory PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_trace_inventory PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_trace_inventory PROPERTY FOLDER Tools)

add_library(oot3d_native_pica_azahar_capture STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_azahar_capture.cpp
)
target_include_directories(oot3d_native_pica_azahar_capture PUBLIC
    tools/oot3d/native_pica_frontend
)
target_link_libraries(oot3d_native_pica_azahar_capture PUBLIC
    oot3d_native_pica_frontend
    nlohmann_json::nlohmann_json)
target_compile_options(oot3d_native_pica_azahar_capture PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_azahar_capture PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_azahar_capture PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_azahar_capture PROPERTY FOLDER Tools)

add_executable(oot3d_native_pica_azahar_capture_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_azahar_capture_tests.cpp
)
target_link_libraries(oot3d_native_pica_azahar_capture_tests PRIVATE
    oot3d_native_pica_azahar_capture)
target_compile_options(oot3d_native_pica_azahar_capture_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_azahar_capture_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_azahar_capture_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_azahar_capture_tests PROPERTY FOLDER Tests)

add_executable(oot3d_native_pica_azahar_corpus_validator EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_azahar_corpus_validator.cpp
)
target_link_libraries(oot3d_native_pica_azahar_corpus_validator PRIVATE
    oot3d_native_pica_azahar_capture)
target_compile_options(oot3d_native_pica_azahar_corpus_validator PRIVATE
    "${WARNING_OVERRIDE}")
if(OOT3D_ENABLE_VULKAN_RENDERER AND TARGET three_ds_recomp_shaderc)
    target_link_libraries(oot3d_native_pica_azahar_corpus_validator PRIVATE
        three_ds_recomp_shaderc)
    target_compile_definitions(oot3d_native_pica_azahar_corpus_validator PRIVATE
        OOT3D_NATIVE_PICA_CORPUS_SHADERC=1)
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        add_custom_command(TARGET oot3d_native_pica_azahar_corpus_validator POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:three_ds_recomp_shaderc>
                    $<TARGET_FILE_DIR:oot3d_native_pica_azahar_corpus_validator>
            COMMENT "Deploying shaderc for Azahar PICA corpus validation")
    endif()
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_azahar_corpus_validator PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_azahar_corpus_validator PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_azahar_corpus_validator PROPERTY FOLDER Tools)

if(OOT3D_ENABLE_VULKAN_RENDERER AND TARGET three_ds_recomp_shaderc)
    add_executable(oot3d_native_pica_aot_compiler EXCLUDE_FROM_ALL
        tools/oot3d/native_pica_frontend/oot3d_native_pica_aot_compiler.cpp
        runtime/three_ds_recomp/src/fast/oot3d/pica_aot_shader_pack.cpp
    )
    target_include_directories(oot3d_native_pica_aot_compiler PRIVATE
        runtime/three_ds_recomp/include)
    target_link_libraries(oot3d_native_pica_aot_compiler PRIVATE
        nlohmann_json::nlohmann_json
        three_ds_recomp_shaderc)
    target_compile_options(oot3d_native_pica_aot_compiler PRIVATE
        "${WARNING_OVERRIDE}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(oot3d_native_pica_aot_compiler PRIVATE
            NOMINMAX)
        set_property(TARGET oot3d_native_pica_aot_compiler PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        add_custom_command(TARGET oot3d_native_pica_aot_compiler POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:three_ds_recomp_shaderc>
                    $<TARGET_FILE_DIR:oot3d_native_pica_aot_compiler>
            COMMENT "Deploying shaderc for PICA AOT compiler")
    endif()
    set_property(TARGET oot3d_native_pica_aot_compiler PROPERTY FOLDER Tools)

    set(OOT3D_PICA_HOST_AOT_COMPILER "" CACHE FILEPATH
        "Host-native PICA shader compiler for cross-compilation")
    if(NOT CMAKE_CROSSCOMPILING OR OOT3D_PICA_HOST_AOT_COMPILER)
    if(CMAKE_CROSSCOMPILING)
        set(_oot3d_shader_compiler "${OOT3D_PICA_HOST_AOT_COMPILER}")
    else()
        set(_oot3d_shader_compiler oot3d_native_pica_aot_compiler)
    endif()
    set(OOT3D_PICA_DEFAULT_SHADER_INVENTORY
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/native_pica_frontend/profiles/oot3d_pica_boot_title_kokiri_pipeline.json")
    set(OOT3D_PICA_DEFAULT_SHADER_PACK
        "${CMAKE_CURRENT_BINARY_DIR}/oot3d_pica_default.o3ps")
    set(OOT3D_PICA_DEFAULT_SHADER_MANIFEST
        "${CMAKE_CURRENT_BINARY_DIR}/oot3d_pica_default_manifest.json")
    add_custom_command(
        OUTPUT
            "${OOT3D_PICA_DEFAULT_SHADER_PACK}"
            "${OOT3D_PICA_DEFAULT_SHADER_MANIFEST}"
        COMMAND ${_oot3d_shader_compiler}
            --inventory "${OOT3D_PICA_DEFAULT_SHADER_INVENTORY}"
            --pack "${OOT3D_PICA_DEFAULT_SHADER_PACK}"
            --manifest "${OOT3D_PICA_DEFAULT_SHADER_MANIFEST}"
        DEPENDS
            ${_oot3d_shader_compiler}
            "${OOT3D_PICA_DEFAULT_SHADER_INVENTORY}"
        COMMENT "Compiling the baseline OOT3D PICA shader corpus AOT"
        VERBATIM)
    add_custom_target(oot3d_native_pica_default_pack
        DEPENDS
            "${OOT3D_PICA_DEFAULT_SHADER_PACK}"
            "${OOT3D_PICA_DEFAULT_SHADER_MANIFEST}")
    set_property(TARGET oot3d_native_pica_default_pack PROPERTY FOLDER Tools)
    unset(_oot3d_shader_compiler)
    else()
        message(STATUS "Cross-build: using runtime shaderc; set OOT3D_PICA_HOST_AOT_COMPILER for a precompiled shader pack")
    endif()
endif()

add_library(oot3d_native_pica_vulkan_bridge STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_presentation_scheduler.cpp
    tools/oot3d/native_game_runtime/oot3d_native_pica_visual_savestate.cpp
    tools/oot3d/native_game_runtime/oot3d_native_pica_vulkan_bridge.cpp
)
target_include_directories(oot3d_native_pica_vulkan_bridge PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(oot3d_native_pica_vulkan_bridge PUBLIC
    oot3d_native_pica_vulkan_plan
    oot3d_renderer_contracts)
target_compile_options(oot3d_native_pica_vulkan_bridge PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_vulkan_bridge PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_vulkan_bridge PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_vulkan_bridge PROPERTY FOLDER Tools)

add_library(triaevum_oot3d_pica_renderer STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_oot3d_pica_batch.cpp
    tools/oot3d/native_game_runtime/triaevum_oot3d_pica_renderer.cpp
)
target_include_directories(triaevum_oot3d_pica_renderer PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(triaevum_oot3d_pica_renderer PUBLIC
    triaevum_oot3d_pica_service
    oot3d_native_pica_vulkan_bridge)
target_compile_options(triaevum_oot3d_pica_renderer PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(triaevum_oot3d_pica_renderer PRIVATE NOMINMAX)
    set_property(TARGET triaevum_oot3d_pica_renderer PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET triaevum_oot3d_pica_renderer PROPERTY FOLDER Tools)

add_library(triaevum_forge_content_filesystem STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_forge_content_filesystem.cpp)
target_include_directories(triaevum_forge_content_filesystem PUBLIC
    tools/oot3d/native_game_runtime)
target_link_libraries(triaevum_forge_content_filesystem PUBLIC
    triaevum_module
    nlohmann_json::nlohmann_json)
target_compile_options(triaevum_forge_content_filesystem PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(triaevum_forge_content_filesystem PRIVATE
        NOMINMAX)
    set_property(TARGET triaevum_forge_content_filesystem PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET triaevum_forge_content_filesystem PROPERTY FOLDER Tools)

add_executable(triaevum_oot3d_pica_batch_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_oot3d_pica_batch_tests.cpp
)
target_link_libraries(triaevum_oot3d_pica_batch_tests PRIVATE
    triaevum_oot3d_pica_renderer)
target_compile_options(triaevum_oot3d_pica_batch_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(triaevum_oot3d_pica_batch_tests PRIVATE NOMINMAX)
    set_property(TARGET triaevum_oot3d_pica_batch_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET triaevum_oot3d_pica_batch_tests PROPERTY FOLDER Tests)

add_executable(triaevum_oot3d_pica_renderer_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_oot3d_pica_renderer_tests.cpp
)
target_link_libraries(triaevum_oot3d_pica_renderer_tests PRIVATE
    triaevum_oot3d_pica_renderer)
target_compile_options(triaevum_oot3d_pica_renderer_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(triaevum_oot3d_pica_renderer_tests PRIVATE NOMINMAX)
    set_property(TARGET triaevum_oot3d_pica_renderer_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET triaevum_oot3d_pica_renderer_tests PROPERTY FOLDER Tests)

# Developer vertical for a private Forge-produced title module. The host is
# deliberately independent of oot3d_game_module: public runtime builds do not
# link any user-derived title image and load it only through the stable ABI.
add_executable(triaevum_oot3d_module_host EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_audio_player_backend.cpp
    tools/oot3d/native_game_runtime/triaevum_oot3d_input_backend.cpp
    tools/oot3d/native_game_runtime/triaevum_runtime_layout.cpp
    tools/oot3d/native_game_runtime/triaevum_oot3d_module_host.cpp)
target_link_libraries(triaevum_oot3d_module_host PRIVATE
    oot3d_native_application_host
    oot3d_native_a32_input
    triaevum_oot3d_pica_renderer
    triaevum_module
    triaevum_forge_content_filesystem)
target_compile_options(triaevum_oot3d_module_host PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(triaevum_oot3d_module_host PRIVATE NOMINMAX)
    set_property(TARGET triaevum_oot3d_module_host PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    find_package(SDL2 CONFIG REQUIRED)
    if (TARGET SDL2::SDL2)
        get_target_property(_triaevum_sdl2_type SDL2::SDL2 TYPE)
        if (_triaevum_sdl2_type STREQUAL "SHARED_LIBRARY")
            add_custom_command(TARGET triaevum_oot3d_module_host POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:SDL2::SDL2>
                        $<TARGET_FILE_DIR:triaevum_oot3d_module_host>
                COMMENT "Deploying SDL2 for triaevum_oot3d_module_host")
        endif()
        unset(_triaevum_sdl2_type)
    endif()
endif()
set_property(TARGET triaevum_oot3d_module_host PROPERTY FOLDER Tools)
set_property(TARGET triaevum_oot3d_module_host PROPERTY OUTPUT_NAME TriAevumModuleDiagnostic)

add_executable(triaevum_runtime_layout_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_runtime_layout.cpp
    tools/oot3d/native_game_runtime/triaevum_runtime_layout_tests.cpp)
target_include_directories(triaevum_runtime_layout_tests PRIVATE
    tools/oot3d/native_game_runtime)
target_link_libraries(triaevum_runtime_layout_tests PRIVATE
    nlohmann_json::nlohmann_json)
target_compile_options(triaevum_runtime_layout_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(triaevum_runtime_layout_tests PRIVATE NOMINMAX)
    set_property(TARGET triaevum_runtime_layout_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET triaevum_runtime_layout_tests PROPERTY FOLDER Tests)

add_executable(oot3d_native_pica_vulkan_bridge_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_vulkan_bridge_tests.cpp
)

add_executable(oot3d_native_pica_visual_savestate_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_visual_savestate_tests.cpp
)
target_link_libraries(oot3d_native_pica_visual_savestate_tests PRIVATE
    oot3d_native_pica_vulkan_bridge)
target_compile_options(oot3d_native_pica_visual_savestate_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_property(TARGET oot3d_native_pica_visual_savestate_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_visual_savestate_tests PROPERTY
    FOLDER Tests)
target_link_libraries(oot3d_native_pica_vulkan_bridge_tests PRIVATE
    oot3d_native_pica_vulkan_bridge)
target_compile_options(oot3d_native_pica_vulkan_bridge_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_vulkan_bridge_tests PRIVATE
        NOMINMAX)
    set_property(TARGET oot3d_native_pica_vulkan_bridge_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_vulkan_bridge_tests PROPERTY FOLDER Tests)

add_executable(oot3d_native_pica_texture_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_texture_tests.cpp
)
target_link_libraries(oot3d_native_pica_texture_tests PRIVATE three_ds_recomp_runtime)
target_compile_options(oot3d_native_pica_texture_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_pica_texture_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_pica_texture_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_pica_texture_tests PROPERTY FOLDER Tests)

add_subdirectory(tools/oot3d/ui_contract EXCLUDE_FROM_ALL)

add_library(oot3d_ui_topscreen STATIC EXCLUDE_FROM_ALL
    tools/oot3d/ui_topscreen/oot3d_top_screen_config.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_controls.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_gameplay_actions.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_items_hint.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_camera.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_camera_guest.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_item_guest.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_item_dispatch.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_mod_profile.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_texture_overrides.cpp
)
target_include_directories(oot3d_ui_topscreen PUBLIC
    tools/oot3d/native_game_runtime
    tools/oot3d/ui_topscreen
)
target_link_libraries(oot3d_ui_topscreen PUBLIC
    oot3d_native_a32_process
    oot3d_native_ui_contract)
target_compile_options(oot3d_ui_topscreen PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_ui_topscreen PRIVATE NOMINMAX)
    set_property(TARGET oot3d_ui_topscreen PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_ui_topscreen PROPERTY FOLDER Tools)

add_executable(oot3d_top_screen_mod_profile_tests EXCLUDE_FROM_ALL
    tools/oot3d/ui_topscreen/oot3d_top_screen_mod_profile_tests.cpp
    tools/oot3d/ui_topscreen/oot3d_top_screen_item_dispatch_tests.cpp
)
target_link_libraries(oot3d_top_screen_mod_profile_tests PRIVATE
    oot3d_ui_topscreen)
target_compile_options(oot3d_top_screen_mod_profile_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_top_screen_mod_profile_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_top_screen_mod_profile_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_top_screen_mod_profile_tests PROPERTY FOLDER Tests)

add_executable(oot3d_top_screen_items_hint_tests EXCLUDE_FROM_ALL
    tools/oot3d/ui_topscreen/oot3d_top_screen_items_hint_tests.cpp
)
target_link_libraries(oot3d_top_screen_items_hint_tests PRIVATE
    oot3d_ui_topscreen)
target_compile_options(oot3d_top_screen_items_hint_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_top_screen_items_hint_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_top_screen_items_hint_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_top_screen_items_hint_tests PROPERTY FOLDER Tests)

add_executable(oot3d_native_ui_contract_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_ui_contract_tests.cpp
)
target_link_libraries(oot3d_native_ui_contract_tests PRIVATE
    oot3d_native_ui_contract)
target_compile_options(oot3d_native_ui_contract_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_ui_contract_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_ui_contract_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_ui_contract_tests PROPERTY FOLDER Tests)

add_library(oot3d_ui_n64 STATIC EXCLUDE_FROM_ALL
    tools/oot3d/ui_n64/oot3d_native_hud_atlas_regions.h
    tools/oot3d/ui_n64/oot3d_n64_gameplay_hud.cpp
    tools/oot3d/ui_n64/oot3d_n64_integrated_ui_runtime.cpp
)
target_include_directories(oot3d_ui_n64 PUBLIC
    tools/oot3d/ui_n64
)
target_link_libraries(oot3d_ui_n64 PUBLIC
    oot3d_native_ui_contract)
target_compile_options(oot3d_ui_n64 PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_ui_n64 PRIVATE NOMINMAX)
    set_property(TARGET oot3d_ui_n64 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_ui_n64 PROPERTY FOLDER Tools)

add_library(oot3d_ui_n64_renderer STATIC EXCLUDE_FROM_ALL
    tools/oot3d/ui_n64/oot3d_n64_ui_renderer.cpp
)
target_include_directories(oot3d_ui_n64_renderer PUBLIC
    tools/oot3d/ui_n64
)
target_link_libraries(oot3d_ui_n64_renderer PUBLIC
    oot3d_ui_n64
    oot3d_renderer_contracts)
target_compile_options(oot3d_ui_n64_renderer PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_ui_n64_renderer PRIVATE NOMINMAX)
    set_property(TARGET oot3d_ui_n64_renderer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_ui_n64_renderer PROPERTY FOLDER Tools)

add_library(oot3d_ui_legacy_adapters STATIC EXCLUDE_FROM_ALL
    tools/oot3d/ui_n64/oot3d_legacy_ui_adapters.cpp
)
target_include_directories(oot3d_ui_legacy_adapters PUBLIC
    tools/oot3d/ui_n64
)
target_link_libraries(oot3d_ui_legacy_adapters PUBLIC
    oot3d_ui_n64_renderer
    oot3d_renderer_contracts
    PRIVATE ImGui)
target_compile_options(oot3d_ui_legacy_adapters PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_ui_legacy_adapters PRIVATE NOMINMAX)
    set_property(TARGET oot3d_ui_legacy_adapters PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_ui_legacy_adapters PROPERTY FOLDER Tools)

add_executable(oot3d_n64_integrated_ui_runtime_tests EXCLUDE_FROM_ALL
    tools/oot3d/ui_n64/oot3d_n64_integrated_ui_runtime_tests.cpp
)
target_link_libraries(oot3d_n64_integrated_ui_runtime_tests PRIVATE
    oot3d_ui_n64)
target_compile_options(oot3d_n64_integrated_ui_runtime_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_n64_integrated_ui_runtime_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_n64_integrated_ui_runtime_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_n64_integrated_ui_runtime_tests PROPERTY FOLDER Tests)

add_executable(oot3d_n64_gameplay_hud_tests EXCLUDE_FROM_ALL
    tools/oot3d/ui_n64/oot3d_n64_gameplay_hud_tests.cpp
)
target_link_libraries(oot3d_n64_gameplay_hud_tests PRIVATE
    oot3d_ui_n64)
target_compile_options(oot3d_n64_gameplay_hud_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_n64_gameplay_hud_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_n64_gameplay_hud_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_n64_gameplay_hud_tests PROPERTY FOLDER Tests)

add_executable(oot3d_n64_ui_renderer_tests EXCLUDE_FROM_ALL
    tools/oot3d/ui_n64/oot3d_n64_ui_renderer_tests.cpp
)
target_link_libraries(oot3d_n64_ui_renderer_tests PRIVATE
    oot3d_ui_n64_renderer)
if (DEFINED _oot3d_mass_clang_builtins)
    target_link_libraries(oot3d_n64_ui_renderer_tests PRIVATE
        "${_oot3d_mass_clang_builtins}")
endif()
target_compile_options(oot3d_n64_ui_renderer_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_n64_ui_renderer_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_n64_ui_renderer_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_n64_ui_renderer_tests PROPERTY FOLDER Tests)

add_library(oot3d_top_screen_runtime_consumer STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_top_screen_gameplay_action_consumer.cpp
    tools/oot3d/native_game_runtime/oot3d_top_screen_items_hint_consumer.cpp
)
target_include_directories(oot3d_top_screen_runtime_consumer PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(oot3d_top_screen_runtime_consumer PUBLIC
    oot3d_native_a32_process
    oot3d_ui_topscreen)
target_compile_options(oot3d_top_screen_runtime_consumer PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_top_screen_runtime_consumer PRIVATE NOMINMAX)
    set_property(TARGET oot3d_top_screen_runtime_consumer PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_top_screen_runtime_consumer PROPERTY FOLDER Tools)

add_library(oot3d_native_ui_lifecycle_bridge STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_ui_lifecycle_bridge.cpp
)
target_include_directories(oot3d_native_ui_lifecycle_bridge PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(oot3d_native_ui_lifecycle_bridge PUBLIC
    oot3d_native_a32_process
    oot3d_ui_n64
    oot3d_ui_topscreen
    oot3d_top_screen_runtime_consumer)
target_compile_options(oot3d_native_ui_lifecycle_bridge PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_ui_lifecycle_bridge PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_ui_lifecycle_bridge PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_ui_lifecycle_bridge PROPERTY FOLDER Tools)

add_executable(oot3d_native_ui_lifecycle_bridge_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_ui_lifecycle_bridge_tests.cpp
)
target_link_libraries(oot3d_native_ui_lifecycle_bridge_tests PRIVATE
    oot3d_native_ui_lifecycle_bridge)
target_compile_options(oot3d_native_ui_lifecycle_bridge_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_ui_lifecycle_bridge_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_ui_lifecycle_bridge_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_ui_lifecycle_bridge_tests PROPERTY FOLDER Tests)

add_library(oot3d_native_ui_texture_provider STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_ui_texture_provider.cpp
)
target_include_directories(oot3d_native_ui_texture_provider PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(oot3d_native_ui_texture_provider PUBLIC
    oot3d_native_pica_submission
    oot3d_ui_n64_renderer
    oot3d_ui_topscreen
    oot3d_renderer_texture_decode)
target_compile_options(oot3d_native_ui_texture_provider PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_ui_texture_provider PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_ui_texture_provider PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_ui_texture_provider PROPERTY FOLDER Tools)

add_executable(oot3d_native_ui_texture_provider_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_ui_texture_provider_tests.cpp
)
target_link_libraries(oot3d_native_ui_texture_provider_tests PRIVATE
    oot3d_native_ui_texture_provider)
target_compile_options(oot3d_native_ui_texture_provider_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_ui_texture_provider_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_ui_texture_provider_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_ui_texture_provider_tests PROPERTY FOLDER Tests)

add_library(oot3d_top_screen_texture_runtime STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_top_screen_texture_runtime.cpp
)
target_include_directories(oot3d_top_screen_texture_runtime PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(oot3d_top_screen_texture_runtime PUBLIC
    oot3d_native_ui_lifecycle_bridge
    oot3d_native_ui_texture_provider)
target_compile_options(oot3d_top_screen_texture_runtime PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_top_screen_texture_runtime PRIVATE NOMINMAX)
    set_property(TARGET oot3d_top_screen_texture_runtime PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_top_screen_texture_runtime PROPERTY FOLDER Tools)

add_executable(oot3d_top_screen_texture_runtime_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_top_screen_texture_runtime_tests.cpp
)
target_link_libraries(oot3d_top_screen_texture_runtime_tests PRIVATE
    oot3d_top_screen_texture_runtime)
target_compile_options(oot3d_top_screen_texture_runtime_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_top_screen_texture_runtime_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_top_screen_texture_runtime_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_top_screen_texture_runtime_tests PROPERTY FOLDER Tests)

add_library(three_ds_recomp_input STATIC EXCLUDE_FROM_ALL
    tools/three_ds/input/three_ds_input.cpp
)
target_include_directories(three_ds_recomp_input PUBLIC
    tools/three_ds/input
)
target_compile_options(three_ds_recomp_input PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(three_ds_recomp_input PRIVATE NOMINMAX)
    set_property(TARGET three_ds_recomp_input PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET three_ds_recomp_input PROPERTY FOLDER Tools)

add_executable(three_ds_recomp_input_tests EXCLUDE_FROM_ALL
    tools/three_ds/input/three_ds_input_tests.cpp
)
target_link_libraries(three_ds_recomp_input_tests PRIVATE
    three_ds_recomp_input)
target_compile_options(three_ds_recomp_input_tests PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(three_ds_recomp_input_tests PRIVATE NOMINMAX)
    set_property(TARGET three_ds_recomp_input_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET three_ds_recomp_input_tests PROPERTY FOLDER Tests)

add_library(oot3d_native_control_config STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_control_config.cpp
)
target_include_directories(oot3d_native_control_config PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(oot3d_native_control_config PUBLIC
    nlohmann_json::nlohmann_json
    three_ds_recomp_input)
target_compile_options(oot3d_native_control_config PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_control_config PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_control_config PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_control_config PROPERTY FOLDER Tools)

add_library(oot3d_native_a32_input STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_input.cpp
)
target_include_directories(oot3d_native_a32_input PUBLIC
    tools/oot3d/native_game_runtime
)
target_include_directories(oot3d_native_a32_input PRIVATE
    runtime/three_ds_recomp/include
)
target_link_libraries(oot3d_native_a32_input PUBLIC
    nlohmann_json::nlohmann_json
    oot3d_native_control_config)
target_compile_options(oot3d_native_a32_input PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_a32_input PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_a32_input PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_input PROPERTY FOLDER Tools)

# The UI lifecycle bridge consumes the guest-input routing contract. Declare
# the dependency after both targets exist so the shared 3DS input API and its
# include directory propagate deterministically.
target_link_libraries(oot3d_native_ui_lifecycle_bridge PUBLIC
    oot3d_native_a32_input)

add_executable(oot3d_native_a32_input_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_input_tests.cpp
)
target_link_libraries(oot3d_native_a32_input_tests PRIVATE
    oot3d_native_a32_input)
target_compile_options(oot3d_native_a32_input_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_a32_input_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_a32_input_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_input_tests PROPERTY FOLDER Tests)

add_subdirectory(tools/triaevum_release/tests)

set(_oot3d_native_a32_ctr_host_sources
    tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_host.cpp
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
target_compile_options(oot3d_native_a32_ctr_host PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_a32_ctr_host PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_a32_ctr_host PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_ctr_host PROPERTY FOLDER Tools)

add_executable(oot3d_native_audio_storage_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_audio_storage_tests.cpp)
target_include_directories(oot3d_native_audio_storage_tests PRIVATE
    tools/oot3d/third_party/azahar_audio)
target_link_libraries(oot3d_native_audio_storage_tests PRIVATE
    oot3d_native_a32_ctr_host)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_audio_storage_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_audio_storage_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_audio_storage_tests PROPERTY FOLDER Tests)

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
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(oot3d_game_module PRIVATE NOMINMAX)
        set_property(TARGET oot3d_game_module PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(TARGET oot3d_game_module PROPERTY FOLDER Tools)

    add_executable(oot3d_game_module_smoke EXCLUDE_FROM_ALL
        tools/oot3d/native_game_module/oot3d_game_module_smoke.cpp)
    target_link_libraries(oot3d_game_module_smoke PRIVATE
        triaevum_module
        triaevum_forge_content_filesystem)
    add_dependencies(oot3d_game_module_smoke oot3d_game_module)
    target_compile_options(oot3d_game_module_smoke PRIVATE
        "${WARNING_OVERRIDE}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(oot3d_game_module_smoke PRIVATE NOMINMAX)
        set_property(TARGET oot3d_game_module_smoke PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(TARGET oot3d_game_module_smoke PROPERTY FOLDER Tests)
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
target_compile_options(oot3d_native_a32_process_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_a32_process_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_a32_process_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_process_tests PROPERTY FOLDER Tests)

if (TARGET oot3d_native_a32_aot)
    add_executable(oot3d_source_actor_update_all_tests EXCLUDE_FROM_ALL
        tools/oot3d/native_game_runtime/oot3d_source_actor_update_all_tests.cpp
    )
    target_include_directories(oot3d_source_actor_update_all_tests PRIVATE
        "${OOT3D_ACTOR_UPDATE_ALL_SOURCE_IMPORT}/include")
    target_link_libraries(oot3d_source_actor_update_all_tests PRIVATE
        oot3d_source_actor_update_all_runtime
        oot3d_native_a32_process
        oot3d_native_a32_aot)
    target_compile_options(oot3d_source_actor_update_all_tests PRIVATE
        "${WARNING_OVERRIDE}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(oot3d_source_actor_update_all_tests PRIVATE
            NOMINMAX)
        set_property(TARGET oot3d_source_actor_update_all_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(TARGET oot3d_source_actor_update_all_tests PROPERTY
        FOLDER Tests)

    add_executable(oot3d_source_actor_init_context_tests EXCLUDE_FROM_ALL
        tools/oot3d/native_game_runtime/oot3d_source_actor_init_context_tests.cpp
    )
    target_link_libraries(oot3d_source_actor_init_context_tests PRIVATE
        oot3d_source_actor_init_context_runtime
        oot3d_native_a32_process
        oot3d_native_a32_aot)
    target_compile_options(oot3d_source_actor_init_context_tests PRIVATE
        "${WARNING_OVERRIDE}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(
            oot3d_source_actor_init_context_tests PRIVATE NOMINMAX)
        set_property(TARGET oot3d_source_actor_init_context_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(TARGET oot3d_source_actor_init_context_tests PROPERTY
        FOLDER Tests)

    add_executable(oot3d_source_cutscene_update_frame_tests EXCLUDE_FROM_ALL
        tools/oot3d/native_game_runtime/oot3d_source_cutscene_update_frame_tests.cpp
    )
    target_link_libraries(oot3d_source_cutscene_update_frame_tests PRIVATE
        oot3d_source_cutscene_update_frame_runtime
        oot3d_native_a32_process
        oot3d_native_a32_aot)
    target_compile_options(
        oot3d_source_cutscene_update_frame_tests PRIVATE
        "${WARNING_OVERRIDE}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(
            oot3d_source_cutscene_update_frame_tests PRIVATE NOMINMAX)
        set_property(TARGET oot3d_source_cutscene_update_frame_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(TARGET oot3d_source_cutscene_update_frame_tests PROPERTY
        FOLDER Tests)

    add_executable(
        oot3d_source_cutscene_process_commands_tests EXCLUDE_FROM_ALL
        tools/oot3d/native_game_runtime/oot3d_source_cutscene_process_commands_tests.cpp
    )
    target_link_libraries(
        oot3d_source_cutscene_process_commands_tests PRIVATE
        oot3d_source_cutscene_process_commands_runtime
        oot3d_native_a32_process
        oot3d_native_a32_aot)
    target_compile_options(
        oot3d_source_cutscene_process_commands_tests PRIVATE
        "${WARNING_OVERRIDE}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(
            oot3d_source_cutscene_process_commands_tests PRIVATE NOMINMAX)
        set_property(
            TARGET oot3d_source_cutscene_process_commands_tests PROPERTY
            MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(
        TARGET oot3d_source_cutscene_process_commands_tests PROPERTY
        FOLDER Tests)

    add_executable(
        oot3d_source_camera_update_tests EXCLUDE_FROM_ALL
        tools/oot3d/native_game_runtime/oot3d_source_camera_update_tests.cpp
    )
    target_link_libraries(
        oot3d_source_camera_update_tests PRIVATE
        oot3d_source_camera_update_runtime
        oot3d_native_a32_process
        oot3d_native_a32_aot)
    target_compile_options(
        oot3d_source_camera_update_tests PRIVATE
        "${WARNING_OVERRIDE}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(
            oot3d_source_camera_update_tests PRIVATE NOMINMAX)
        set_property(
            TARGET oot3d_source_camera_update_tests PROPERTY
            MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(
        TARGET oot3d_source_camera_update_tests PROPERTY FOLDER Tests)

    add_executable(
        oot3d_source_player_update_tests EXCLUDE_FROM_ALL
        tools/oot3d/native_game_runtime/oot3d_source_player_update_tests.cpp
    )
    target_link_libraries(
        oot3d_source_player_update_tests PRIVATE
        oot3d_source_player_update_runtime
        oot3d_native_a32_process
        oot3d_native_a32_aot)
    target_compile_options(
        oot3d_source_player_update_tests PRIVATE
        "${WARNING_OVERRIDE}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(
            oot3d_source_player_update_tests PRIVATE NOMINMAX)
        set_property(
            TARGET oot3d_source_player_update_tests PROPERTY
            MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(
        TARGET oot3d_source_player_update_tests PROPERTY FOLDER Tests)
endif()

add_executable(oot3d_native_a32_ctr_host_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_ctr_host_tests.cpp
)
target_link_libraries(oot3d_native_a32_ctr_host_tests PRIVATE
    oot3d_native_a32_ctr_host)
target_compile_options(oot3d_native_a32_ctr_host_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_a32_ctr_host_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_a32_ctr_host_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_ctr_host_tests PROPERTY FOLDER Tests)

add_library(oot3d_native_a32_savestate STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_savestate.cpp
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
target_compile_options(oot3d_native_a32_savestate PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_a32_savestate PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_a32_savestate PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_savestate PROPERTY FOLDER Tools)

add_executable(oot3d_native_a32_savestate_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_savestate_tests.cpp
)
target_link_libraries(oot3d_native_a32_savestate_tests PRIVATE
    oot3d_native_a32_savestate)
target_compile_options(oot3d_native_a32_savestate_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_a32_savestate_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_a32_savestate_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_savestate_tests PROPERTY FOLDER Tests)

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
    target_compile_options(oot3d_native_a32_boot_probe PRIVATE "${WARNING_OVERRIDE}")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(oot3d_native_a32_boot_probe PRIVATE NOMINMAX)
        set_property(TARGET oot3d_native_a32_boot_probe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(TARGET oot3d_native_a32_boot_probe PROPERTY FOLDER Tools)
endif()

# Documentation and analysis workspace for the historical source-native study.
# It is not part of the product graph; configure that directory directly only
# when regenerating its documentary evidence.

add_library(oot3d_native_game_runtime STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_execution.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_scene_view.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_timing_probe.cpp
    tools/oot3d/native_game_runtime/oot3d_native_scenario_bootstrap.cpp
    tools/oot3d/native_game_runtime/oot3d_native_frame_rate.cpp
    tools/oot3d/native_game_runtime/oot3d_native_presentation_pacer.cpp
    tools/oot3d/native_game_runtime/oot3d_native_temporal_event_ledger.cpp
    tools/oot3d/native_game_runtime/oot3d_native_player_temporal_bridge.cpp
    tools/oot3d/native_game_runtime/oot3d_native_actor_core_contract.cpp
    tools/oot3d/native_game_runtime/oot3d_native_actor_runtime.cpp
    tools/oot3d/native_game_runtime/oot3d_native_closure_catalog.cpp
    tools/oot3d/native_game_runtime/oot3d_native_limb_callback.cpp
    tools/oot3d/native_game_runtime/oot3d_native_npc_tracking.cpp
    tools/oot3d/native_game_runtime/oot3d_native_object_bank_runtime.cpp
    tools/oot3d/native_game_runtime/oot3d_native_room_runtime.cpp
    tools/oot3d/native_game_runtime/oot3d_native_room_render_runtime.cpp
    tools/oot3d/native_game_runtime/oot3d_native_transition_actor_runtime.cpp
    tools/oot3d/native_game_runtime/oot3d_native_player_action_runtime.cpp
    tools/oot3d/native_game_runtime/oot3d_native_player_collision_action_contract.cpp
    tools/oot3d/native_game_runtime/oot3d_native_player_clip_runtime.cpp
    tools/oot3d/native_game_runtime/oot3d_native_player_animation_catalog.cpp
    tools/oot3d/native_game_runtime/oot3d_native_root_motion.cpp
    tools/oot3d/native_game_runtime/oot3d_native_skel_anime.cpp
)
target_include_directories(oot3d_native_game_runtime PUBLIC
    tools/oot3d/decomp_support/include
    tools/oot3d/native_demo_host
    tools/oot3d/native_game_runtime
    tools/oot3d/runtime_common
)
target_compile_options(oot3d_native_game_runtime PRIVATE "${WARNING_OVERRIDE}")
target_link_libraries(oot3d_native_game_runtime PUBLIC
    oot3d_native_asset_runtime
    oot3d_room_compilation_runtime
    oot3d_native_audio
    oot3d_native_a32_process
)
if(TARGET oot3d_native_math)
    target_link_libraries(oot3d_native_game_runtime PUBLIC oot3d_native_math)
endif()
if (TARGET oot3d_native_a32_aot)
    target_link_libraries(oot3d_native_game_runtime PUBLIC oot3d_native_a32_aot)
    target_compile_definitions(oot3d_native_game_runtime PRIVATE
        OOT3D_NATIVE_A32_AOT_AVAILABLE=1)
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_game_runtime PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_game_runtime PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_game_runtime PROPERTY FOLDER Tools)

add_executable(oot3d_native_game_runtime_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_frame_rate_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_temporal_event_ledger_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_player_temporal_bridge_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_scene_view_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_timing_probe_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_player_clip_runtime_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_player_action_runtime_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_player_animation_catalog_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_player_collision_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_root_motion_tests.cpp
    tools/oot3d/native_game_runtime/oot3d_native_skel_anime_tests.cpp
)
target_link_libraries(oot3d_native_game_runtime_tests PRIVATE
    oot3d_native_game_runtime
    oot3d_native_runtime_host
)
target_compile_options(oot3d_native_game_runtime_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_game_runtime_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_game_runtime_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_game_runtime_tests PROPERTY FOLDER Tests)

add_executable(oot3d_native_actor_core_contract_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_actor_core_contract_tests.cpp
)
target_link_libraries(oot3d_native_actor_core_contract_tests PRIVATE oot3d_native_game_runtime)
target_compile_options(oot3d_native_actor_core_contract_tests PRIVATE "${WARNING_OVERRIDE}")

add_executable(oot3d_native_actor_runtime_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_actor_runtime_tests.cpp
)
target_link_libraries(oot3d_native_actor_runtime_tests PRIVATE oot3d_native_game_runtime)
target_compile_options(oot3d_native_actor_runtime_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_actor_core_contract_tests PRIVATE NOMINMAX)
    target_compile_definitions(oot3d_native_actor_runtime_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_actor_core_contract_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET oot3d_native_actor_runtime_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_actor_core_contract_tests PROPERTY FOLDER Tests)
set_property(TARGET oot3d_native_actor_runtime_tests PROPERTY FOLDER Tests)

add_executable(oot3d_native_a32_execution_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_execution_tests.cpp
)
target_link_libraries(oot3d_native_a32_execution_tests PRIVATE
    oot3d_native_game_runtime)
target_compile_options(oot3d_native_a32_execution_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_a32_execution_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_a32_execution_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_execution_tests PROPERTY FOLDER Tests)

add_executable(oot3d_native_audio_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_audio_tests.cpp
)
target_link_libraries(oot3d_native_audio_tests PRIVATE oot3d_native_audio)
target_compile_options(oot3d_native_audio_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_audio_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_audio_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_audio_tests PROPERTY FOLDER Tests)

add_executable(oot3d_native_closure_catalog_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_closure_catalog_tests.cpp
)
target_link_libraries(oot3d_native_closure_catalog_tests PRIVATE oot3d_native_game_runtime)
target_compile_options(oot3d_native_closure_catalog_tests PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_closure_catalog_tests PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_closure_catalog_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_closure_catalog_tests PROPERTY FOLDER Tests)

add_executable(oot3d_native_fast3d_demo EXCLUDE_FROM_ALL
    tools/oot3d/native_demo_host/oot3d_native_fast3d_demo.cpp
)
target_link_libraries(oot3d_native_fast3d_demo PRIVATE oot3d_native_cutscene_host)
target_compile_options(oot3d_native_fast3d_demo PRIVATE "${WARNING_OVERRIDE}")

add_executable(oot3d_native_cutscene_player EXCLUDE_FROM_ALL
    tools/oot3d/native_demo_host/oot3d_native_cutscene_player.cpp
    tools/oot3d/native_demo_host/oot3d_cutscene_player_cli.cpp
)
target_link_libraries(oot3d_native_cutscene_player PRIVATE oot3d_native_cutscene_host)
target_compile_options(oot3d_native_cutscene_player PRIVATE "${WARNING_OVERRIDE}")

add_executable(oot3d_shadow2d_backend_probe EXCLUDE_FROM_ALL
    tools/oot3d/native_demo_host/oot3d_shadow2d_backend_probe.cpp
)
target_link_libraries(oot3d_shadow2d_backend_probe PRIVATE oot3d_native_cutscene_host)
target_compile_options(oot3d_shadow2d_backend_probe PRIVATE "${WARNING_OVERRIDE}")

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
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/triaevum-runtime-targets-$<CONFIG>.json"
        CONTENT "{\n  \"format\": \"triaevum_runtime_targets_v1\",\n  \"source_commit\": \"${GIT_COMMIT_HASH}\",\n  \"runtime\": \"$<TARGET_FILE:oot3d_native_game>\",\n  \"stub\": \"$<TARGET_FILE:triaevum_title_aot_stub>\"\n}\n")
endif()
set(TRIAEVUM_PRODUCT_NRI 0)
if(THREE_DS_RECOMP_ENABLE_NRI AND OOT3D_ENABLE_VULKAN_RENDERER)
    set(TRIAEVUM_PRODUCT_NRI 1)
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
if (TARGET oot3d_native_a32_aot OR TARGET oot3d_native_whole_aot OR
    OOT3D_DIRECT_AOT_PLUGIN)
    target_sources(oot3d_native_game PRIVATE
        tools/oot3d/native_game_runtime/oot3d_native_a32_window.cpp
        tools/oot3d/native_game_runtime/oot3d_native_actor_interactions.cpp
        tools/oot3d/native_game_runtime/oot3d_native_pcm_diagnostics.cpp
        tools/oot3d/native_game_runtime/oot3d_native_controls_settings_panel.cpp
        tools/oot3d/native_game_runtime/oot3d_top_screen_settings_panel.cpp)
    set_source_files_properties(tools/oot3d/native_game_runtime/oot3d_native_actor_interactions.cpp
        PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/source_imports/actor_update_all_449e219e/include")
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
    if (NOT OOT3D_WHOLE_AOT_PRODUCT_MODE)
        target_link_libraries(oot3d_native_game PRIVATE
            oot3d_source_actor_init_context_runtime
            oot3d_source_actor_update_all_runtime
            oot3d_source_camera_update_runtime
            oot3d_source_player_update_runtime
            oot3d_source_player_update_common_runtime
            oot3d_source_csab_curve_runtime
            oot3d_source_cutscene_process_commands_runtime
            oot3d_source_cutscene_update_frame_runtime)
    endif()
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
        target_link_options(oot3d_native_game PRIVATE
            "/DEBUG:FULL"
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
set(_oot3d_vulkan_hosts
    triaevum_oot3d_module_host
    oot3d_native_fast3d_demo
    oot3d_native_cutscene_player
    oot3d_shadow2d_backend_probe
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
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_fast3d_demo PRIVATE NOMINMAX)
    target_compile_definitions(oot3d_native_cutscene_player PRIVATE NOMINMAX)
    target_compile_definitions(oot3d_shadow2d_backend_probe PRIVATE NOMINMAX)
    target_compile_definitions(oot3d_native_game PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_fast3d_demo PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET oot3d_native_cutscene_player PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET oot3d_shadow2d_backend_probe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET oot3d_native_game PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    nx_generate_nacp(oot3d_native_game.nacp
        NAME "The Legend of Zelda: Ocarina of Time 3D Recompiled"
        AUTHOR "OoT3D Recompiled contributors"
        VERSION "0.1.0")
    nx_create_nro(oot3d_native_game
        NACP oot3d_native_game.nacp)
endif()
set_property(TARGET oot3d_native_fast3d_demo PROPERTY FOLDER Tools)
set_property(TARGET oot3d_native_cutscene_player PROPERTY FOLDER Tools)
set_property(TARGET oot3d_shadow2d_backend_probe PROPERTY FOLDER Tools)
set_property(TARGET oot3d_native_game PROPERTY FOLDER Tools)

add_executable(oot3d_native_game_legacy_sandbox EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_game_legacy_sandbox.cpp
    tools/oot3d/native_game_runtime/oot3d_native_game_bootstrap.cpp
    tools/oot3d/native_game_runtime/oot3d_native_player_controller.cpp
)
target_include_directories(oot3d_native_game_legacy_sandbox PRIVATE
    tools/oot3d/native_demo_host
    tools/oot3d/native_game_runtime
    tools/oot3d/ui_topscreen
)
target_link_libraries(oot3d_native_game_legacy_sandbox PRIVATE
    oot3d_native_cutscene_host
    oot3d_native_game_runtime
    oot3d_native_control_config
    oot3d_ui_topscreen
)
target_compile_options(oot3d_native_game_legacy_sandbox PRIVATE
    "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(oot3d_native_game_legacy_sandbox PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_game_legacy_sandbox PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_game_legacy_sandbox PROPERTY FOLDER Tools)
if(OOT3D_ENABLE_VULKAN_RENDERER AND TARGET three_ds_recomp_shaderc)
    add_custom_command(TARGET oot3d_native_game_legacy_sandbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:three_ds_recomp_shaderc>
                $<TARGET_FILE_DIR:oot3d_native_game_legacy_sandbox>
        COMMENT "Deploying shaderc for oot3d_native_game_legacy_sandbox")
endif()
