cmake_minimum_required(VERSION 3.26)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(oot3d_source_native_runtime LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

set(OOT3D_DECOMP_ROOT "" CACHE PATH "Pinned OOT3D decomp repository")
set(OOT3D_SOURCE_SNAPSHOT_ROOT "" CACHE PATH
    "Materialized revision-pinned OOT3D source snapshot")
set(OOT3D_SOURCE_LOWERED_ROOT "" CACHE PATH
    "Generated snapshot with 32-bit guest-word globals lowered")
set(OOT3D_SOURCE_PROCESS_PROFILE_ROOT "" CACHE PATH
    "Generated source-native process image profile")
set(OOT3D_SOURCE_LEGALIZED_ARCHIVE "" CACHE FILEPATH
    "Strict legalized source archive used by the nnMain link probe")
set(OOT3D_LLVM_CLANG "" CACHE FILEPATH
    "Clang executable used for the isolated native startup closure")
set(OOT3D_LLVM_AR "" CACHE FILEPATH
    "llvm-ar executable used for the isolated native startup closure")
set(OOT3D_LLVM_NM "" CACHE FILEPATH
    "llvm-nm executable used for the isolated native startup closure")
set(OOT3D_SOURCE_GUEST_STORAGE_LEGALIZER "" CACHE FILEPATH
    "Oot3dGuestStorageLegalizer executable used by the startup closure")
set(OOT3D_SOURCE_STARTUP_CLOSURE_ROOT
    "${CMAKE_CURRENT_BINARY_DIR}/startup-closure" CACHE PATH
    "Build directory for the isolated native startup closure")
set(OOT3D_VCPKG_ROOT "" CACHE PATH "vcpkg root used for header-only dependencies")

find_package(Python3 QUIET COMPONENTS Interpreter)

set(OOT3D_SOURCE_EXECUTION_STACK_SOURCES
    oot3d_source_execution_stack.cpp)
if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    if (MSVC)
        enable_language(ASM_MASM)
        list(APPEND OOT3D_SOURCE_EXECUTION_STACK_SOURCES
            oot3d_source_execution_stack_win64.asm)
    else()
        enable_language(ASM)
        list(APPEND OOT3D_SOURCE_EXECUTION_STACK_SOURCES
            oot3d_source_execution_stack_win64.S)
    endif()
endif()

find_path(OOT3D_NLOHMANN_INCLUDE_DIR nlohmann/json.hpp
    HINTS "${OOT3D_VCPKG_ROOT}"
    PATH_SUFFIXES installed/x64-windows-static/include
                  installed/x64-windows/include)
if (NOT OOT3D_NLOHMANN_INCLUDE_DIR)
    message(FATAL_ERROR "nlohmann/json.hpp is required for the native PICA frontend")
endif()

set(OOT3D_SOURCE_PROFILE_JSON "${CMAKE_CURRENT_SOURCE_DIR}/decomp_profile.json")
file(READ "${OOT3D_SOURCE_PROFILE_JSON}" OOT3D_SOURCE_PROFILE_CONTENT)
string(JSON OOT3D_DECOMP_PROFILE_ID GET "${OOT3D_SOURCE_PROFILE_CONTENT}" profile_id)
string(JSON OOT3D_DECOMP_REVISION GET "${OOT3D_SOURCE_PROFILE_CONTENT}" decomp_revision)
string(JSON OOT3D_DECOMP_SOURCE_LANE GET "${OOT3D_SOURCE_PROFILE_CONTENT}" canonical_source_lane)
string(JSON OOT3D_DECOMP_SOURCE_COUNT GET "${OOT3D_SOURCE_PROFILE_CONTENT}" canonical_source_count)
string(JSON OOT3D_DECOMP_TARGET_ENTRY_COUNT GET "${OOT3D_SOURCE_PROFILE_CONTENT}" target_entry_count)
string(JSON OOT3D_DECOMP_TITLE_ID GET "${OOT3D_SOURCE_PROFILE_CONTENT}" title_id)
string(JSON OOT3D_DECOMP_PRODUCT_CODE GET "${OOT3D_SOURCE_PROFILE_CONTENT}" product_code)
string(JSON OOT3D_DECOMP_CODE_BIN_SHA256 GET "${OOT3D_SOURCE_PROFILE_CONTENT}" code_bin_sha256)
string(JSON OOT3D_DECOMP_CODE_LOAD_ADDRESS GET "${OOT3D_SOURCE_PROFILE_CONTENT}" code_load_address)

set(OOT3D_SOURCE_GENERATED_INCLUDE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${OOT3D_SOURCE_GENERATED_INCLUDE_DIR}")
configure_file(oot3d_decomp_profile.h.in
    "${OOT3D_SOURCE_GENERATED_INCLUDE_DIR}/oot3d_decomp_profile.h" @ONLY)

add_library(oot3d_source_runtime_core STATIC EXCLUDE_FROM_ALL
    ${OOT3D_SOURCE_EXECUTION_STACK_SOURCES}
    oot3d_ctr_apt_service.cpp
    oot3d_ctr_config_service.cpp
    oot3d_ctr_dsp_service.cpp
    oot3d_ctr_fs_service.cpp
    oot3d_ctr_hid_service.cpp
    oot3d_ctr_hid_producer.cpp
    oot3d_ctr_gsp_service.cpp
    oot3d_ctr_pica_backend.cpp
    oot3d_ctr_host_services.cpp
    oot3d_ctr_ipc_router.cpp
    oot3d_ctr_ndm_service.cpp
    oot3d_ctr_srv_service.cpp
    oot3d_guest_address_space.cpp
    oot3d_source_data_bindings.cpp
    oot3d_source_c_runtime_shims.cpp
    oot3d_source_function_registry.cpp
    oot3d_source_host_pump.cpp
    oot3d_source_process_image.cpp)
add_library(oot3d_source_dsp_backend STATIC EXCLUDE_FROM_ALL
    ../native_game_runtime/oot3d_native_a32_dsp_hle.cpp
    ../third_party/azahar_audio/audio_core/codec.cpp
    ../third_party/azahar_audio/audio_core/interpolate.cpp
    ../third_party/azahar_audio/audio_core/hle/filter.cpp
    ../third_party/azahar_audio/audio_core/hle/mixers.cpp
    ../third_party/azahar_audio/audio_core/hle/source.cpp)
target_include_directories(oot3d_source_dsp_backend PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../native_game_runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/azahar_audio"
    "${OOT3D_NLOHMANN_INCLUDE_DIR}")

add_library(oot3d_source_pica_backend STATIC EXCLUDE_FROM_ALL
    ../native_pica_frontend/oot3d_native_pica_draw_state.cpp
    ../native_pica_frontend/oot3d_native_pica_fragment_shader_gen.cpp
    ../native_pica_frontend/oot3d_native_pica_frontend.cpp
    ../native_pica_frontend/oot3d_native_pica_shader_gen.cpp
    ../third_party/azahar_pica/video_core/shader/generator/glsl_shader_decompiler.cpp)
target_include_directories(oot3d_source_pica_backend PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../native_pica_frontend"
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/azahar_pica"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../three_ds_recomp/legacy/include"
    "${OOT3D_NLOHMANN_INCLUDE_DIR}")

target_sources(oot3d_source_runtime_core PRIVATE
    oot3d_source_dsp_mixer.cpp
    oot3d_source_pica_submission.cpp
    ../native_game_runtime/oot3d_native_pica_submission.cpp)
target_sources(oot3d_source_runtime_core PRIVATE
    oot3d_source_ctr_runtime.cpp)
target_include_directories(oot3d_source_runtime_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../native_game_runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/../native_pica_frontend"
    "${OOT3D_NLOHMANN_INCLUDE_DIR}"
    "${OOT3D_SOURCE_GENERATED_INCLUDE_DIR}")
target_link_libraries(oot3d_source_runtime_core PUBLIC
    oot3d_source_dsp_backend oot3d_source_pica_backend)

add_executable(oot3d_source_game EXCLUDE_FROM_ALL
    oot3d_source_game.cpp
    oot3d_source_game_session.cpp)
target_link_libraries(oot3d_source_game PRIVATE oot3d_source_runtime_core)

if (OOT3D_SOURCE_LEGALIZED_ARCHIVE AND
    EXISTS "${OOT3D_SOURCE_LEGALIZED_ARCHIVE}")
    foreach(required OOT3D_LLVM_CLANG OOT3D_LLVM_AR OOT3D_LLVM_NM
                     OOT3D_SOURCE_GUEST_STORAGE_LEGALIZER)
        if (NOT ${required} OR NOT EXISTS "${${required}}")
            message(FATAL_ERROR
                "${required} is required for the native process-entry closure")
        endif()
    endforeach()
    if (NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR
            "Python 3 is required for the native process-entry closure")
    endif()

    set(OOT3D_SOURCE_STARTUP_IMPORT_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/source_imports/startup_8f00bb64")
    set(OOT3D_SOURCE_STARTUP_ARCHIVE
        "${OOT3D_SOURCE_STARTUP_CLOSURE_ROOT}/liboot3d_source_startup_closure.a")
    file(GLOB_RECURSE OOT3D_SOURCE_STARTUP_IMPORT_FILES CONFIGURE_DEPENDS
        "${OOT3D_SOURCE_STARTUP_IMPORT_ROOT}/*")
    file(GLOB_RECURSE OOT3D_SOURCE_BOOT_OVERLAY_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/source_overlays/boot_contracts/*")
    add_custom_command(
        OUTPUT "${OOT3D_SOURCE_STARTUP_ARCHIVE}"
        BYPRODUCTS
            "${OOT3D_SOURCE_STARTUP_CLOSURE_ROOT}/startup_closure_build.json"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/build_startup_closure.py"
            --clang "${OOT3D_LLVM_CLANG}"
            --llvm-ar "${OOT3D_LLVM_AR}"
            --llvm-nm "${OOT3D_LLVM_NM}"
            --legalizer "${OOT3D_SOURCE_GUEST_STORAGE_LEGALIZER}"
            --base-archive "${OOT3D_SOURCE_LEGALIZED_ARCHIVE}"
            --source-root "${OOT3D_SOURCE_STARTUP_IMPORT_ROOT}"
            --output-root "${OOT3D_SOURCE_STARTUP_CLOSURE_ROOT}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/build_startup_closure.py"
            "${OOT3D_SOURCE_LEGALIZED_ARCHIVE}"
            "${OOT3D_SOURCE_GUEST_STORAGE_LEGALIZER}"
            ${OOT3D_SOURCE_STARTUP_IMPORT_FILES}
            ${OOT3D_SOURCE_BOOT_OVERLAY_FILES}
        COMMENT "Building revision-pinned OOT3D native startup closure"
        VERBATIM)
    add_custom_target(oot3d_source_startup_closure
        DEPENDS "${OOT3D_SOURCE_STARTUP_ARCHIVE}")
    add_dependencies(oot3d_source_game oot3d_source_startup_closure)

    add_executable(oot3d_source_link_probe EXCLUDE_FROM_ALL
        oot3d_source_link_probe.cpp)
    target_link_libraries(oot3d_source_link_probe PRIVATE
        "$<LINK_GROUP:RESCAN,oot3d_source_runtime_core,${OOT3D_SOURCE_LEGALIZED_ARCHIVE}>")
    target_link_libraries(oot3d_source_game PRIVATE
        "$<LINK_GROUP:RESCAN,oot3d_source_runtime_core,${OOT3D_SOURCE_LEGALIZED_ARCHIVE},${OOT3D_SOURCE_STARTUP_ARCHIVE}>")
    target_compile_definitions(oot3d_source_game PRIVATE
        OOT3D_SOURCE_EXECUTABLE_CLOSURE=1
        OOT3D_SOURCE_PROCESS_ENTRY_CLOSURE=1)
    if (MINGW)
        target_link_options(oot3d_source_link_probe PRIVATE
            "-Wl,--unresolved-symbols=ignore-all")
        target_link_options(oot3d_source_game PRIVATE
            "-Wl,--wrap=memset"
            "-Wl,--defsym,__bss_start=0x0055A220"
            "-Wl,--defsym,__bss_end=0x005C6608")
    endif()
    set_property(TARGET oot3d_source_link_probe PROPERTY FOLDER Tests)
endif()

if (OOT3D_SOURCE_SNAPSHOT_ROOT AND
    EXISTS "${OOT3D_SOURCE_SNAPSHOT_ROOT}/oot3d_source_snapshot_profile.h")
    target_include_directories(oot3d_source_game PRIVATE
        "${OOT3D_SOURCE_SNAPSHOT_ROOT}")
    target_compile_definitions(oot3d_source_game PRIVATE
        OOT3D_SOURCE_SNAPSHOT_AVAILABLE=1)
endif()

if (OOT3D_SOURCE_LOWERED_ROOT AND
    EXISTS "${OOT3D_SOURCE_LOWERED_ROOT}/lowered_sources.cmake")
    enable_language(ASM)
    include("${OOT3D_SOURCE_LOWERED_ROOT}/lowered_sources.cmake")
    add_library(oot3d_source_lowered_surface OBJECT EXCLUDE_FROM_ALL
        ${OOT3D_DECOMP_LOWERED_SOURCES})
    target_include_directories(oot3d_source_lowered_surface PRIVATE
        "${OOT3D_SOURCE_LOWERED_ROOT}/include")
    target_compile_features(oot3d_source_lowered_surface PRIVATE c_std_11)
    target_compile_definitions(oot3d_source_lowered_surface PRIVATE
        OOT3D_HOST_CTR_CONNECT_TO_PORT=1)
    if (MSVC)
        target_compile_options(oot3d_source_lowered_surface PRIVATE /w)
    else()
        target_compile_options(oot3d_source_lowered_surface PRIVATE
            $<$<C_COMPILER_ID:Clang>:--target=x86_64-w64-windows-gnu>
            -std=gnu11
            -mcmodel=large
            -Wno-error=incompatible-pointer-types
            -Wno-error=int-conversion
            -w)
    endif()
    set_property(TARGET oot3d_source_lowered_surface PROPERTY FOLDER Tools)

    add_library(oot3d_source_lowered_surface_archive STATIC EXCLUDE_FROM_ALL
        $<TARGET_OBJECTS:oot3d_source_lowered_surface>)
    set_target_properties(oot3d_source_lowered_surface_archive PROPERTIES
        PREFIX "lib" SUFFIX ".a")
    set_property(TARGET oot3d_source_lowered_surface_archive PROPERTY FOLDER Tools)
endif()

if (OOT3D_SOURCE_PROCESS_PROFILE_ROOT AND
    EXISTS "${OOT3D_SOURCE_PROCESS_PROFILE_ROOT}/oot3d_source_process_profile.h")
    target_include_directories(oot3d_source_game PRIVATE
        "${OOT3D_SOURCE_PROCESS_PROFILE_ROOT}")
    target_compile_definitions(oot3d_source_game PRIVATE
        OOT3D_SOURCE_PROCESS_PROFILE_AVAILABLE=1)

    if (MINGW)
        add_executable(oot3d_source_absolute_data_pilot EXCLUDE_FROM_ALL
            oot3d_source_absolute_data_pilot.c
            oot3d_source_absolute_data_pilot_main.cpp)
        target_include_directories(oot3d_source_absolute_data_pilot PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${OOT3D_SOURCE_PROCESS_PROFILE_ROOT}")
        target_link_libraries(oot3d_source_absolute_data_pilot PRIVATE
            oot3d_source_runtime_core)
        set_source_files_properties(oot3d_source_absolute_data_pilot.c
            PROPERTIES COMPILE_OPTIONS "-mcmodel=large")
        target_link_options(oot3d_source_absolute_data_pilot PRIVATE
            -static-libgcc
            -static-libstdc++
            "-Wl,--defsym,DAT_00314094=0x00314094")
        set_property(TARGET oot3d_source_absolute_data_pilot PROPERTY FOLDER Tests)
    endif()
endif()

if (OOT3D_SOURCE_SNAPSHOT_ROOT AND
    EXISTS "${OOT3D_SOURCE_SNAPSHOT_ROOT}/canonical_sources.cmake")
    include("${OOT3D_SOURCE_SNAPSHOT_ROOT}/canonical_sources.cmake")
    add_library(oot3d_source_surface OBJECT EXCLUDE_FROM_ALL
        ${OOT3D_DECOMP_CANONICAL_SOURCES})
    target_include_directories(oot3d_source_surface PRIVATE
        "${OOT3D_SOURCE_SNAPSHOT_ROOT}/include")
    target_compile_features(oot3d_source_surface PRIVATE c_std_11)
    target_compile_definitions(oot3d_source_surface PRIVATE
        OOT3D_HOST_CTR_CONNECT_TO_PORT=1)
    if (MSVC)
        target_compile_options(oot3d_source_surface PRIVATE /w)
    else()
        target_compile_options(oot3d_source_surface PRIVATE
            -std=gnu11
            -Wno-error=incompatible-pointer-types
            -Wno-error=int-conversion
            -w)
    endif()
    set_property(TARGET oot3d_source_surface PROPERTY FOLDER Tools)

    add_library(oot3d_source_surface_archive STATIC EXCLUDE_FROM_ALL
        $<TARGET_OBJECTS:oot3d_source_surface>)
    set_target_properties(oot3d_source_surface_archive PROPERTIES
        PREFIX "lib" SUFFIX ".a")
    set_property(TARGET oot3d_source_surface_archive PROPERTY FOLDER Tools)
endif()

add_executable(oot3d_source_runtime_tests EXCLUDE_FROM_ALL
    oot3d_source_runtime_tests.cpp)
target_link_libraries(oot3d_source_runtime_tests PRIVATE
    oot3d_source_runtime_core)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    foreach(target oot3d_source_runtime_core oot3d_source_game
                   oot3d_source_runtime_tests)
        target_compile_definitions(${target} PRIVATE NOMINMAX)
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()
endif()
if (MINGW)
    target_link_options(oot3d_source_game PRIVATE -static-libgcc -static-libstdc++)
    target_link_options(oot3d_source_runtime_tests PRIVATE
        -static-libgcc -static-libstdc++)
endif()

set_property(TARGET oot3d_source_runtime_core oot3d_source_game
             PROPERTY FOLDER Tools)
set_property(TARGET oot3d_source_runtime_tests PROPERTY FOLDER Tests)

if (Python3_Interpreter_FOUND AND OOT3D_DECOMP_ROOT)
    if (NOT OOT3D_SOURCE_SNAPSHOT_ROOT)
        set(OOT3D_SOURCE_SNAPSHOT_ROOT
            "${CMAKE_CURRENT_BINARY_DIR}/decomp-snapshot")
    endif()
    add_custom_target(oot3d_source_snapshot
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/sync_decomp_snapshot.py"
            --decomp-root "${OOT3D_DECOMP_ROOT}"
            --profile "${OOT3D_SOURCE_PROFILE_JSON}"
            --output-root "${OOT3D_SOURCE_SNAPSHOT_ROOT}"
            --replace
        COMMENT "Materializing pinned OOT3D decomp source snapshot"
        VERBATIM)
    set_property(TARGET oot3d_source_snapshot PROPERTY FOLDER Tools)
endif()
