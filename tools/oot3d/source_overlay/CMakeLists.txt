cmake_minimum_required(VERSION 3.26)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(oot3d_source_overlay LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

set(_oot3d_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(_oot3d_cutscene_frame_import
    "${_oot3d_repo_root}/tools/oot3d/source_imports/cutscene_update_frame_d5c2927")
set(_oot3d_cutscene_commands_import
    "${_oot3d_repo_root}/tools/oot3d/source_imports/cutscene_process_commands_d5c2927")
set(_oot3d_csab_curve_import
    "${_oot3d_repo_root}/tools/oot3d/source_imports/csab_curve_eval_d5c2927")

add_library(oot3d_source_overlay_loader STATIC
    oot3d_source_overlay_loader.cpp)
target_include_directories(oot3d_source_overlay_loader PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(oot3d_source_overlay_smoke SHARED
    "${_oot3d_cutscene_frame_import}/src/code/z_cutscene_update_frame_owner.cpp"
    "${_oot3d_cutscene_commands_import}/src/code/z_cutscene_process_commands_owner.cpp"
    "${_oot3d_repo_root}/tools/oot3d/source_imports/csab_curve_eval_d5c2927/src/code/z_csab_curve_eval.cpp"
    "${_oot3d_repo_root}/tools/oot3d/native_game_runtime/oot3d_source_csab_curve_core.cpp"
    "${_oot3d_repo_root}/tools/oot3d/native_a32_runtime/oot3d_native_a32_vfp_ops.cpp"
    "${_oot3d_repo_root}/tools/oot3d/native_a32_runtime/upstream/recomp/a32_vfp_binary64.cpp"
    oot3d_source_overlay_cutscene_commands.cpp
    oot3d_source_overlay_cutscene_frame.cpp
    oot3d_source_overlay_boot_leaves.cpp
    oot3d_source_overlay_csab_curves.cpp
    oot3d_source_overlay_game_state.cpp
    oot3d_source_overlay_pause_state.cpp
    oot3d_source_overlay_smoke.cpp)
target_include_directories(oot3d_source_overlay_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${_oot3d_cutscene_frame_import}/include"
    "${_oot3d_cutscene_commands_import}/include"
    "${_oot3d_csab_curve_import}/include"
    "${_oot3d_repo_root}/tools/oot3d/native_game_runtime"
    "${_oot3d_repo_root}/tools/oot3d/native_a32_runtime"
    "${_oot3d_repo_root}/tools/oot3d/native_a32_runtime/upstream")
target_compile_definitions(oot3d_source_overlay_smoke PRIVATE
    OOT3D_SOURCE_OVERLAY_BUILD_PLUGIN=1)
set_target_properties(oot3d_source_overlay_smoke PROPERTIES
    OUTPUT_NAME "oot3d_source_overlay")

option(OOT3D_SOURCE_OVERLAY_BUILD_DECOMP_OWNER
    "Compile the decompiled source owner into the overlay DLL" ON)

if (OOT3D_SOURCE_OVERLAY_BUILD_DECOMP_OWNER)
set(OOT3D_SOURCE_OVERLAY_CLANG "" CACHE FILEPATH
    "Pinned clang executable used for decompiled owner bitcode")
set(OOT3D_SOURCE_OVERLAY_OPT "" CACHE FILEPATH
    "Pinned opt executable used for decompiled owner bitcode")
set(OOT3D_SOURCE_OVERLAY_GUEST_MEMORY_LOWERING "" CACHE FILEPATH
    "Standalone guest-memory lowering executable")

set(_oot3d_memory_owner_root
    "${CMAKE_CURRENT_SOURCE_DIR}/decomp_owner/runtime_memory")
set(_oot3d_memory_owner_outputs
    "${CMAKE_CURRENT_BINARY_DIR}/decomp_owner/runtime_memory")
set(_oot3d_memory_owner_raw
    "${_oot3d_memory_owner_outputs}/runtime_memory.raw.bc")
set(_oot3d_memory_owner_ssa
    "${_oot3d_memory_owner_outputs}/runtime_memory.ssa.bc")
set(_oot3d_memory_owner_lowered
    "${_oot3d_memory_owner_outputs}/runtime_memory.lowered.bc")
set(_oot3d_memory_owner_object
    "${_oot3d_memory_owner_outputs}/runtime_memory.obj")
set(_oot3d_game_state_owner_root
    "${CMAKE_CURRENT_SOURCE_DIR}/decomp_owner/game_state_update")
set(_oot3d_game_state_owner_outputs
    "${CMAKE_CURRENT_BINARY_DIR}/decomp_owner/game_state_update")
set(_oot3d_game_state_owner_raw
    "${_oot3d_game_state_owner_outputs}/game_state_update.raw.bc")
set(_oot3d_game_state_owner_ssa
    "${_oot3d_game_state_owner_outputs}/game_state_update.ssa.bc")
set(_oot3d_game_state_owner_lowered
    "${_oot3d_game_state_owner_outputs}/game_state_update.lowered.bc")
set(_oot3d_game_state_owner_object
    "${_oot3d_game_state_owner_outputs}/game_state_update.obj")

foreach(required_tool
        OOT3D_SOURCE_OVERLAY_CLANG
        OOT3D_SOURCE_OVERLAY_OPT
        OOT3D_SOURCE_OVERLAY_GUEST_MEMORY_LOWERING)
    if (NOT EXISTS "${${required_tool}}")
        message(FATAL_ERROR
            "Source-overlay owner compiler input is missing: ${required_tool}='${${required_tool}}'")
    endif()
endforeach()

add_custom_command(
    OUTPUT "${_oot3d_memory_owner_object}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${_oot3d_memory_owner_outputs}"
    COMMAND "${OOT3D_SOURCE_OVERLAY_CLANG}"
            -target x86_64-pc-windows-msvc
            -emit-llvm -c -O0 -Xclang -disable-O0-optnone
            -I "${_oot3d_memory_owner_root}/include"
            "${_oot3d_memory_owner_root}/runtime_memory.c"
            -o "${_oot3d_memory_owner_raw}"
    COMMAND "${OOT3D_SOURCE_OVERLAY_OPT}"
            -passes=mem2reg
            "${_oot3d_memory_owner_raw}"
            -o "${_oot3d_memory_owner_ssa}"
    COMMAND "${OOT3D_SOURCE_OVERLAY_GUEST_MEMORY_LOWERING}"
            "${_oot3d_memory_owner_ssa}"
            -o "${_oot3d_memory_owner_lowered}"
            --strict
            --guest-pointer=Lib_MemSet:0
            --guest-pointer=Oot3d_ClearTenBytes:0
            --guest-pointer=oot3d_memclear:0
            --guest-pointer=oot3d_memclear_counted_8byte_entries:0
            --guest-pointer=oot3d_memcpy_aligned_end:0
            --guest-pointer=oot3d_memcpy_aligned_end:1
            --guest-pointer=oot3d_memcpy_end:0
            --guest-pointer=oot3d_memcpy_end:1
            --guest-return=oot3d_memclear
            --guest-return=oot3d_memclear_counted_8byte_entries
            --guest-return=oot3d_memcpy_aligned_end
            --guest-return=oot3d_memcpy_end
    COMMAND "${OOT3D_SOURCE_OVERLAY_CLANG}"
            -target x86_64-pc-windows-msvc
            -c -O2 "${_oot3d_memory_owner_lowered}"
            -o "${_oot3d_memory_owner_object}"
    DEPENDS
        "${_oot3d_memory_owner_root}/runtime_memory.c"
        "${_oot3d_memory_owner_root}/include/oot3d/memory.h"
        "${_oot3d_memory_owner_root}/include/oot3d/types.h"
        "${OOT3D_SOURCE_OVERLAY_GUEST_MEMORY_LOWERING}"
    COMMENT "Lowering decompiled runtime-memory owner to checked guest-memory ABI"
    VERBATIM)

add_custom_command(
    OUTPUT "${_oot3d_game_state_owner_object}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${_oot3d_game_state_owner_outputs}"
    COMMAND "${OOT3D_SOURCE_OVERLAY_CLANG}"
            -target x86_64-pc-windows-msvc
            -emit-llvm -c -O0 -Xclang -disable-O0-optnone
            -I "${_oot3d_game_state_owner_root}/include"
            "${_oot3d_game_state_owner_root}/game_state_update.c"
            -o "${_oot3d_game_state_owner_raw}"
    COMMAND "${OOT3D_SOURCE_OVERLAY_OPT}"
            -passes=mem2reg
            "${_oot3d_game_state_owner_raw}"
            -o "${_oot3d_game_state_owner_ssa}"
    COMMAND "${OOT3D_SOURCE_OVERLAY_GUEST_MEMORY_LOWERING}"
            "${_oot3d_game_state_owner_ssa}"
            -o "${_oot3d_game_state_owner_lowered}"
            --strict
            --guest-pointer=oot3d_game_state_read_main:0
            --guest-pointer=oot3d_game_state_increment_frames:0
    COMMAND "${OOT3D_SOURCE_OVERLAY_CLANG}"
            -target x86_64-pc-windows-msvc
            -c -O2 "${_oot3d_game_state_owner_lowered}"
            -o "${_oot3d_game_state_owner_object}"
    DEPENDS
        "${_oot3d_game_state_owner_root}/game_state_update.c"
        "${_oot3d_game_state_owner_root}/include/oot3d/game_state_update.h"
        "${OOT3D_SOURCE_OVERLAY_GUEST_MEMORY_LOWERING}"
    COMMENT "Lowering decompiled GameState_Update owner data operations"
    VERBATIM)

set_source_files_properties(
    "${_oot3d_memory_owner_object}"
    "${_oot3d_game_state_owner_object}"
    PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
target_sources(oot3d_source_overlay_smoke PRIVATE
    "${_oot3d_memory_owner_object}"
    "${_oot3d_game_state_owner_object}")
endif()

add_executable(oot3d_source_overlay_loader_tests EXCLUDE_FROM_ALL
    oot3d_source_overlay_loader_tests.cpp)
target_include_directories(oot3d_source_overlay_loader_tests PRIVATE
    "${_oot3d_repo_root}/tools/oot3d/source_imports/csab_curve_eval_d5c2927/include")
target_link_libraries(oot3d_source_overlay_loader_tests PRIVATE
    oot3d_source_overlay_loader)

if (WIN32)
    foreach(target oot3d_source_overlay_loader
                   oot3d_source_overlay_smoke
                   oot3d_source_overlay_loader_tests)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()
elseif (CMAKE_DL_LIBS)
    target_link_libraries(oot3d_source_overlay_loader PUBLIC ${CMAKE_DL_LIBS})
endif()

set_property(TARGET oot3d_source_overlay_loader
                    oot3d_source_overlay_smoke PROPERTY FOLDER Tools)
set_property(TARGET oot3d_source_overlay_loader_tests PROPERTY FOLDER Tests)

option(OOT3D_SOURCE_OVERLAY_BUILD_HOST_PATCH
    "Build the small whole-AOT host bridge object set" OFF)
set(OOT3D_SOURCE_OVERLAY_A32_GENERATED_DIR "" CACHE PATH
    "Existing generated A32 metadata used by the host bridge")
set(OOT3D_SOURCE_OVERLAY_WHOLE_AOT_GENERATED_DIR "" CACHE PATH
    "Existing generated whole-AOT registry used by the host bridge")
set(OOT3D_SOURCE_OVERLAY_VCPKG_INCLUDE_DIR "" CACHE PATH
    "Existing vcpkg include directory used by the host bridge")

if (OOT3D_SOURCE_OVERLAY_BUILD_HOST_PATCH)
    foreach(required_path
            "${OOT3D_SOURCE_OVERLAY_A32_GENERATED_DIR}/oot3d_a32_generated.h"
            "${OOT3D_SOURCE_OVERLAY_WHOLE_AOT_GENERATED_DIR}/oot3d_whole_aot_generated.h"
            "${OOT3D_SOURCE_OVERLAY_VCPKG_INCLUDE_DIR}/nlohmann/json.hpp")
        if (NOT EXISTS "${required_path}")
            message(FATAL_ERROR
                "Source-overlay host bridge input is missing: ${required_path}")
        endif()
    endforeach()
    add_library(oot3d_source_overlay_host_patch_objects OBJECT EXCLUDE_FROM_ALL
        "${_oot3d_repo_root}/tools/oot3d/native_game_runtime/oot3d_native_compiled_functions.cpp"
        "${_oot3d_repo_root}/tools/oot3d/native_game_runtime/oot3d_native_source_overlay.cpp"
        "${OOT3D_SOURCE_OVERLAY_WHOLE_AOT_GENERATED_DIR}/oot3d_whole_aot_generated.cpp"
        oot3d_source_overlay_loader.cpp)
    target_include_directories(oot3d_source_overlay_host_patch_objects PRIVATE
        "${_oot3d_repo_root}/tools/oot3d/native_game_runtime"
        "${_oot3d_repo_root}/tools/oot3d/native_a32_runtime"
        "${_oot3d_repo_root}/tools/oot3d/native_a32_runtime/upstream"
        "${_oot3d_repo_root}/tools/oot3d/native_a32_runtime/upstream/recomp"
        "${_oot3d_repo_root}/tools/oot3d/native_a32_runtime/mass_cpp"
        "${_oot3d_repo_root}/tools/oot3d/typed_gameplay"
        "${OOT3D_SOURCE_OVERLAY_A32_GENERATED_DIR}"
        "${OOT3D_SOURCE_OVERLAY_WHOLE_AOT_GENERATED_DIR}"
        "${OOT3D_SOURCE_OVERLAY_VCPKG_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(oot3d_source_overlay_host_patch_objects PRIVATE
        CONTROLLERBUTTONS_T=uint32_t
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        OOT3D_NATIVE_GENERATED_TRUE_AOT=1
        OOT3D_NATIVE_GENERATED_WHOLE_AOT=1)
    if (MSVC)
        target_compile_options(oot3d_source_overlay_host_patch_objects PRIVATE
            /w /Ob2)
        set_property(TARGET oot3d_source_overlay_host_patch_objects PROPERTY
            MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_property(TARGET oot3d_source_overlay_host_patch_objects PROPERTY
        FOLDER Tools)
endif()
