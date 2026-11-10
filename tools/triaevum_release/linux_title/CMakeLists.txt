cmake_minimum_required(VERSION 3.26)
project(TriAevumLinuxTitle LANGUAGES CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    message(FATAL_ERROR "This publisher target currently supports Linux x86_64 only")
endif()
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Use Clang for the strict floating-point title build")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(TRIAEVUM_TRANSLATED_TITLE_DIR "" CACHE PATH "Extracted published translated title source")
include("${root}/tools/triaevum_release/cmake/TranslatedTitleSources.cmake")
triaevum_translated_title_sources("${TRIAEVUM_TRANSLATED_TITLE_DIR}" sources)
find_package(nlohmann_json REQUIRED)
include("${root}/tools/triaevum_release/cmake/TitleSupport.cmake")
triaevum_add_title_support("${root}" "${root}/tools/oot3d/native_a32_runtime/upstream")
target_compile_options(triaevum_title_whole_aot_support PRIVATE -O2 -ffp-model=strict)
add_library(triaevum_linux_title SHARED
    ${sources}
    "${root}/tools/oot3d/native_game_runtime/triaevum_title_whole_aot_plugin.cpp")
target_include_directories(triaevum_linux_title PRIVATE "${TRIAEVUM_TRANSLATED_TITLE_DIR}")
target_compile_definitions(triaevum_linux_title PRIVATE OOT3D_NATIVE_GENERATED_WHOLE_AOT=1)
target_compile_options(triaevum_linux_title PRIVATE -O2 -ffp-model=strict)
target_link_libraries(triaevum_linux_title PRIVATE triaevum_title_whole_aot_support)
target_link_options(triaevum_linux_title PRIVATE -Wl,--no-undefined)
set_target_properties(triaevum_linux_title PROPERTIES PREFIX "" OUTPUT_NAME "triaevum_title_aot")

# Profile-guided optimization of the title module (off by default; release
# builds adopt it only with a qualified profile, docs/TRIAEVUM_TITLE_PGO.md).
#   GENERATE: an instrumented module for tools/renderer/tev_program/
#             collect_aot_profile.py; it writes triaevum_title_aot.pgo.json
#             beside itself (the title source and compiler it was built from).
#   USE:      optimizes with TRIAEVUM_TITLE_PGO_RECEIPT (the collection's
#             receipt.json; the merged title.profdata lies beside it). The
#             profile must come from this title source and this compiler
#             version; anything else stops the configure. The compiler's
#             profile runtime (libclang_rt.profile) must match the compiler.
set(TRIAEVUM_TITLE_PGO "OFF" CACHE STRING "Title module PGO: OFF, GENERATE or USE")
set_property(CACHE TRIAEVUM_TITLE_PGO PROPERTY STRINGS OFF GENERATE USE)
set(TRIAEVUM_TITLE_PGO_RECEIPT "" CACHE FILEPATH "receipt.json of the profile collection (PGO USE)")
file(SHA256 "${TRIAEVUM_TRANSLATED_TITLE_DIR}/TITLE_SOURCE_MANIFEST.json" title_source_manifest_sha256)
if(TRIAEVUM_TITLE_PGO STREQUAL "GENERATE")
    target_compile_options(triaevum_linux_title PRIVATE -fprofile-instr-generate)
    target_compile_options(triaevum_title_whole_aot_support PRIVATE -fprofile-instr-generate)
    target_link_options(triaevum_linux_title PRIVATE -fprofile-instr-generate)
    file(WRITE "${CMAKE_BINARY_DIR}/triaevum_title_aot.pgo.json"
        "{\n  \"format\": \"triaevum_title_pgo_build_v1\",\n"
        "  \"title_source_manifest_sha256\": \"${title_source_manifest_sha256}\",\n"
        "  \"compiler_id\": \"${CMAKE_CXX_COMPILER_ID}\",\n"
        "  \"compiler_version\": \"${CMAKE_CXX_COMPILER_VERSION}\"\n}\n")
elseif(TRIAEVUM_TITLE_PGO STREQUAL "USE")
    if(NOT EXISTS "${TRIAEVUM_TITLE_PGO_RECEIPT}")
        message(FATAL_ERROR "PGO USE needs TRIAEVUM_TITLE_PGO_RECEIPT (a profile collection receipt.json)")
    endif()
    file(READ "${TRIAEVUM_TITLE_PGO_RECEIPT}" receipt)
    string(JSON receipt_format GET "${receipt}" format)
    string(JSON built_format GET "${receipt}" instrumented_build format)
    string(JSON built_source GET "${receipt}" instrumented_build title_source_manifest_sha256)
    string(JSON built_compiler GET "${receipt}" instrumented_build compiler_version)
    string(JSON profdata_sha256 GET "${receipt}" profdata_sha256)
    if(NOT receipt_format STREQUAL "triaevum_aot_profile_collection_v1" OR
       NOT built_format STREQUAL "triaevum_title_pgo_build_v1")
        message(FATAL_ERROR "PGO USE: not a title profile collection receipt")
    endif()
    if(NOT built_source STREQUAL title_source_manifest_sha256)
        message(FATAL_ERROR "PGO USE: stale profile: it was collected from another title source "
            "(${built_source}, this build ${title_source_manifest_sha256}); collect it again")
    endif()
    if(NOT built_compiler STREQUAL CMAKE_CXX_COMPILER_VERSION)
        message(FATAL_ERROR "PGO USE: the profile was collected with Clang ${built_compiler}, "
            "this build uses ${CMAKE_CXX_COMPILER_VERSION}; collect it again with this compiler")
    endif()
    get_filename_component(receipt_dir "${TRIAEVUM_TITLE_PGO_RECEIPT}" DIRECTORY)
    set(profdata "${receipt_dir}/title.profdata")
    file(SHA256 "${profdata}" actual_profdata_sha256)
    if(NOT actual_profdata_sha256 STREQUAL profdata_sha256)
        message(FATAL_ERROR "PGO USE: title.profdata does not match its receipt")
    endif()
    # A function whose code changed since the collection keeps no profile
    # (Clang matches each function's control-flow hash); it is compiled as
    # without PGO, never with another function's counts.
    foreach(target triaevum_linux_title triaevum_title_whole_aot_support)
        target_compile_options(${target} PRIVATE "-fprofile-instr-use=${profdata}"
            -Wno-profile-instr-unprofiled)
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${TRIAEVUM_TITLE_PGO_RECEIPT}" "${profdata}")
elseif(NOT TRIAEVUM_TITLE_PGO STREQUAL "OFF")
    message(FATAL_ERROR "TRIAEVUM_TITLE_PGO must be OFF, GENERATE or USE")
endif()
