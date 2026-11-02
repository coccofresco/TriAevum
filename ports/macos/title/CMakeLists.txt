cmake_minimum_required(VERSION 3.30)
# Publisher-side macOS (Apple Silicon) title module and its service tests.
# Same translated title sources and flags as the Android arm64 title
# (ports/android/native): only the Mach-O link differs.
project(TriAevumMacTitle LANGUAGES C CXX)
if(NOT APPLE OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "Configure on macOS with -DCMAKE_OSX_ARCHITECTURES=arm64")
endif()
set(TRIAEVUM_TITLES "oot3d" CACHE STRING "Titles in the public macOS build" FORCE)
include("${CMAKE_CURRENT_LIST_DIR}/../PathRemapping.cmake")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
get_filename_component(root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory("${root}/runtime/triaevum_module" module)
add_subdirectory("${root}/tools/triaevum_release/native_probe" abi)
add_executable(triaevum_macos_abi_probe "${root}/tools/triaevum_release/whole_aot_abi_probe.cpp")
target_link_libraries(triaevum_macos_abi_probe PRIVATE triaevum_title_whole_aot_support)

set(TRIAEVUM_TRANSLATED_TITLE_DIR "" CACHE PATH "Verified translated title sources (publisher only)")
if(TRIAEVUM_TRANSLATED_TITLE_DIR)
    set(TRIAEVUM_MACOS_TITLE_OPTIMIZATION "1" CACHE STRING "Publisher ARM64 optimization level (0, 1, 2)")
    if(NOT TRIAEVUM_MACOS_TITLE_OPTIMIZATION MATCHES "^[012]$")
        message(FATAL_ERROR "TRIAEVUM_MACOS_TITLE_OPTIMIZATION must be 0, 1 or 2")
    endif()
    include("${root}/tools/triaevum_release/cmake/TranslatedTitleSources.cmake")
    triaevum_translated_title_sources("${TRIAEVUM_TRANSLATED_TITLE_DIR}" title_sources)
    add_library(triaevum_macos_title SHARED EXCLUDE_FROM_ALL ${title_sources}
        "${root}/tools/oot3d/native_game_runtime/triaevum_title_whole_aot_plugin.cpp")
    target_compile_definitions(triaevum_macos_title PRIVATE OOT3D_NATIVE_GENERATED_WHOLE_AOT=1)
    target_include_directories(triaevum_macos_title PRIVATE "${TRIAEVUM_TRANSLATED_TITLE_DIR}")
    # Strict IEEE semantics: Apple clang contracts FMAs on arm64 by default.
    target_compile_options(triaevum_macos_title PRIVATE
        -O${TRIAEVUM_MACOS_TITLE_OPTIMIZATION} -ffp-model=strict -ffp-contract=off -g0)
    target_compile_options(triaevum_title_whole_aot_support PRIVATE -O2 -ffp-model=strict -ffp-contract=off)
    target_link_libraries(triaevum_macos_title PRIVATE triaevum_title_whole_aot_support)
    # Mach-O rejects undefined symbols and uses 16 KiB segments by default.
    set_target_properties(triaevum_macos_title PROPERTIES PREFIX "" OUTPUT_NAME triaevum_title_aot
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
endif()
