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
include(CMake/oot3d_target_defaults.cmake)

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
option(TRIAEVUM_DEVELOPER_TOOLS
    "Developer build: F1 Dev Tools (warp screen and warp points); never public" OFF)
if(TRIAEVUM_DEVELOPER_TOOLS)
    # Renderer diagnostics for the lighting lab (pica_surface_debug.h).
    add_compile_definitions(TRIAEVUM_RENDERER_DEVELOPER_TOOLS=1)
endif()
option(OOT3D_ENABLE_VULKAN_RENDERER
    "Build the Vulkan renderer used by native OoT3D executables" ON)
set(THREE_DS_RECOMP_ENABLE_VULKAN ${OOT3D_ENABLE_VULKAN_RENDERER} CACHE BOOL
    "Build the native 3DS Vulkan backend" FORCE)
# Before the sub-projects, so the build tree's root test file includes theirs.
enable_testing()
add_subdirectory(runtime/triaevum_module
    ${CMAKE_BINARY_DIR}/triaevum_module)
add_subdirectory(runtime/three_ds_recomp
    ${CMAKE_BINARY_DIR}/three_ds_recomp_runtime)
target_compile_options(three_ds_recomp_runtime PRIVATE "${WARNING_OVERRIDE}")

include(CMake/oot3d/native_runtime.cmake)

include(CMake/oot3d/pica.cmake)

include(CMake/oot3d/ui_input.cmake)

add_subdirectory(tools/triaevum_release/tests)

include(CMake/oot3d/host_services.cmake)

include(CMake/oot3d/game.cmake)

include(CMake/oot3d/tests.cmake)

