# Native runtime of the OoT3D executables: the application host and audio libraries, the A32 runtime and its
# whole-function, mass and true AOT variants, typed gameplay, the A32 process, and their tests.
# Included from the root CMakeLists.txt, in its scope.

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
oot3d_target_defaults(oot3d_native_application_host NO_WARNING_OVERRIDE FOLDER Tools)

add_library(oot3d_native_audio STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_cseq.cpp
    tools/oot3d/native_game_runtime/oot3d_native_audio.cpp
    tools/oot3d/native_game_runtime/oot3d_native_audio_profiles.cpp
    tools/oot3d/native_game_runtime/oot3d_native_audio_mixer.cpp
    tools/oot3d/native_game_runtime/oot3d_native_audio_dsp.cpp
    tools/oot3d/native_game_runtime/oot3d_native_audio_resampler.cpp
    tools/oot3d/native_game_runtime/oot3d_native_audio_service.cpp
)
target_include_directories(oot3d_native_audio PUBLIC
    tools/oot3d/native_game_runtime
)
oot3d_target_defaults(oot3d_native_audio FOLDER Tools)

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
    tools/oot3d/native_a32_runtime/a32_thumb.cpp
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
    oot3d_target_defaults(oot3d_native_a32_aot FOLDER Tools)
endif()

add_executable(oot3d_native_a32_runtime_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_a32_runtime/oot3d_native_a32_runtime_tests.cpp
)
target_link_libraries(oot3d_native_a32_runtime_tests PRIVATE
    oot3d_native_a32_runtime)
oot3d_target_defaults(oot3d_native_a32_runtime_tests FOLDER Tests)

add_executable(oot3d_native_a32_thumb_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_a32_runtime/a32_thumb_tests.cpp
)
target_link_libraries(oot3d_native_a32_thumb_tests PRIVATE
    oot3d_native_a32_runtime)
oot3d_target_defaults(oot3d_native_a32_thumb_tests FOLDER Tests)

add_executable(oot3d_native_a32_media_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_a32_runtime/a32_media_tests.cpp
)
target_link_libraries(oot3d_native_a32_media_tests PRIVATE
    oot3d_native_a32_runtime)
oot3d_target_defaults(oot3d_native_a32_media_tests FOLDER Tests)

add_executable(oot3d_native_a32_vfp_fast_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_a32_runtime/oot3d_native_a32_vfp_fast_tests.cpp
)
target_link_libraries(oot3d_native_a32_vfp_fast_tests PRIVATE
    oot3d_native_a32_runtime)
target_compile_options(oot3d_native_a32_vfp_fast_tests PRIVATE "${WARNING_OVERRIDE}"
    $<IF:$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>,/fp:strict,-ffp-model=strict>)
oot3d_target_defaults(oot3d_native_a32_vfp_fast_tests NO_WARNING_OVERRIDE FOLDER Tests)

add_executable(pica_gi_geometry_tests EXCLUDE_FROM_ALL
    runtime/three_ds_recomp/tests/pica_gi_geometry_tests.cpp
    runtime/three_ds_recomp/src/fast/renderer3ds/pica_gi_geometry.cpp
)
target_include_directories(pica_gi_geometry_tests PRIVATE
    runtime/three_ds_recomp/include)
target_compile_features(pica_gi_geometry_tests PRIVATE cxx_std_20)
oot3d_target_defaults(pica_gi_geometry_tests NO_WARNING_OVERRIDE FOLDER Tests)

add_executable(pica_fragment_lighting_memo_tests EXCLUDE_FROM_ALL
    runtime/three_ds_recomp/tests/pica_fragment_lighting_memo_tests.cpp
    runtime/three_ds_recomp/src/fast/oot3d/pica_fragment_lighting.cpp
)
target_include_directories(pica_fragment_lighting_memo_tests PRIVATE
    runtime/three_ds_recomp/include)
target_compile_features(pica_fragment_lighting_memo_tests PRIVATE cxx_std_20)
oot3d_target_defaults(pica_fragment_lighting_memo_tests NO_WARNING_OVERRIDE FOLDER Tests)

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
oot3d_target_defaults(oot3d_typed_gameplay FOLDER Tools)

add_executable(oot3d_gameplay_time_tests EXCLUDE_FROM_ALL
    tools/oot3d/typed_gameplay/oot3d_gameplay_time_tests.cpp
)
target_link_libraries(oot3d_gameplay_time_tests PRIVATE
    oot3d_typed_gameplay)
target_compile_options(oot3d_gameplay_time_tests PRIVATE
    "${WARNING_OVERRIDE}")
oot3d_target_defaults(oot3d_gameplay_time_tests NO_WARNING_OVERRIDE FOLDER Tests)

add_library(oot3d_native_a32_process STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_a32_memory.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_process.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_registry.cpp
    tools/oot3d/native_game_runtime/oot3d_native_owner_call_adapter.cpp
    tools/oot3d/native_game_runtime/oot3d_native_compiled_functions.cpp
    tools/oot3d/native_game_runtime/oot3d_native_mass_aot.cpp
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
        oot3d_native_a32_runtime)
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
    oot3d_target_defaults(oot3d_typed_gameplay_tests NO_WARNING_OVERRIDE FOLDER Tests)

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
    oot3d_target_defaults(oot3d_typed_player_gameplay_tests NO_WARNING_OVERRIDE FOLDER Tests)
endif()
target_compile_options(oot3d_native_a32_process PRIVATE "${WARNING_OVERRIDE}")
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_options(oot3d_native_a32_process PRIVATE
        $<$<CONFIG:RelWithDebInfo>:/Ob2>)
    target_compile_definitions(oot3d_native_a32_process PRIVATE NOMINMAX)
    set_property(TARGET oot3d_native_a32_process PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET oot3d_native_a32_process PROPERTY FOLDER Tools)

# The mass-AOT tests compile against the generated A32 AOT header, so they exist
# only in a tree configured with those generated inputs.
if (TARGET oot3d_native_a32_aot)
    add_executable(oot3d_native_mass_aot_tests EXCLUDE_FROM_ALL
        tools/oot3d/native_game_runtime/oot3d_native_mass_aot_tests.cpp)
    target_link_libraries(oot3d_native_mass_aot_tests PRIVATE
        oot3d_native_a32_process
        oot3d_native_a32_aot)
    target_include_directories(oot3d_native_mass_aot_tests PRIVATE
        ${OOT3D_A32_AOT_GENERATED_DIR})
    if (TARGET oot3d_native_mass_aot)
        target_include_directories(oot3d_native_mass_aot_tests PRIVATE
            ${OOT3D_MASS_AOT_GENERATED_DIR}
            tools/oot3d/native_a32_runtime/mass_cpp)
    endif()
    target_compile_options(oot3d_native_mass_aot_tests PRIVATE
        "${WARNING_OVERRIDE}")
    oot3d_target_defaults(oot3d_native_mass_aot_tests NO_WARNING_OVERRIDE FOLDER Tests)
endif()
