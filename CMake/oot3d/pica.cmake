# PICA: the process image, the native PICA frontend, service, transfer, submission, composition, Vulkan plan
# and bridge, semantic trace, Azahar capture, shader packs and the AOT shader compiler, the PICA renderer and
# module host, and their tests and inventory tools.
# Included from the root CMakeLists.txt, in its scope.

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
oot3d_target_defaults(oot3d_native_a32_process_image FOLDER Tools)

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
    ThreeDsRecomp::TevProgram
    oot3d_renderer_fragment_lighting
    spdlog::spdlog
    nlohmann_json::nlohmann_json)
oot3d_target_defaults(oot3d_native_pica_frontend FOLDER Tools)

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
oot3d_target_defaults(triaevum_oot3d_pica_service NO_WARNING_OVERRIDE FOLDER Tools)

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
oot3d_target_defaults(oot3d_native_pica_frontend_tests FOLDER Tests)

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
include(runtime/three_ds_recomp/cmake/spirv_pack.cmake)
triaevum_add_spirv_pack(oot3d_native_pica_capture_inventory
    tools/oot3d/native_pica_frontend/oot3d_native_vertex_binaries.cpp
    tools/oot3d/native_pica_frontend/oot3d_native_vertex.spvpack)
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
    oot3d_native_a32_process
    oot3d_renderer_contracts)
oot3d_target_defaults(oot3d_native_pica_transfer FOLDER Tools)

add_executable(oot3d_native_pica_transfer_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_transfer_tests.cpp
)
target_link_libraries(oot3d_native_pica_transfer_tests PRIVATE
    oot3d_native_pica_transfer)
target_include_directories(oot3d_native_pica_transfer_tests PRIVATE
    runtime/three_ds_recomp/include)
oot3d_target_defaults(oot3d_native_pica_transfer_tests FOLDER Tests)

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
    oot3d_native_pica_transfer
    oot3d_native_pica_frontend)
oot3d_target_defaults(oot3d_native_pica_submission FOLDER Tools)

add_executable(oot3d_native_pica_submission_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_submission_tests.cpp
)
target_link_libraries(oot3d_native_pica_submission_tests PRIVATE
    oot3d_native_pica_submission)
oot3d_target_defaults(oot3d_native_pica_submission_tests FOLDER Tests)

add_library(oot3d_native_pica_composition STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_composition.cpp
    tools/oot3d/native_game_runtime/mm3d_pica_composition.cpp
    tools/oot3d/native_game_runtime/mm3d_native_a32_lighting.cpp
    tools/oot3d/native_game_runtime/oot3d_native_a32_lighting.cpp
)
set_source_files_properties(
    tools/oot3d/native_game_runtime/oot3d_native_a32_lighting.cpp
    tools/oot3d/native_game_runtime/oot3d_native_pica_composition_tests.cpp
    PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/tools/oot3d/decomp_support/include")
target_include_directories(oot3d_native_pica_composition PUBLIC
    tools/oot3d/native_game_runtime
    tools/oot3d/native_pica_frontend
)
target_link_libraries(oot3d_native_pica_composition PUBLIC
    oot3d_native_a32_process
    oot3d_native_pica_frontend)
target_compile_options(oot3d_native_pica_composition PRIVATE
    "${WARNING_OVERRIDE}")
oot3d_target_defaults(oot3d_native_pica_composition NO_WARNING_OVERRIDE FOLDER Tools)

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
oot3d_target_defaults(oot3d_native_pica_vulkan_plan FOLDER Tools)

add_executable(oot3d_native_pica_vulkan_plan_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_vulkan_plan_tests.cpp
)
target_link_libraries(oot3d_native_pica_vulkan_plan_tests PRIVATE
    oot3d_native_pica_vulkan_plan)
oot3d_target_defaults(oot3d_native_pica_vulkan_plan_tests FOLDER Tests)

add_library(oot3d_native_pica_semantic_trace STATIC EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/oot3d_native_pica_semantic_trace.cpp
)
target_include_directories(oot3d_native_pica_semantic_trace PUBLIC
    tools/oot3d/native_game_runtime
    tools/oot3d/native_pica_frontend
)
target_link_libraries(oot3d_native_pica_semantic_trace PUBLIC
    oot3d_native_pica_vulkan_plan
    oot3d_renderer_texture_decode
    nlohmann_json::nlohmann_json)
target_compile_options(oot3d_native_pica_semantic_trace PRIVATE
    "${WARNING_OVERRIDE}")
oot3d_target_defaults(oot3d_native_pica_semantic_trace NO_WARNING_OVERRIDE FOLDER Tools)

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
oot3d_target_defaults(oot3d_native_pica_trace_inventory NO_WARNING_OVERRIDE FOLDER Tools)

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
oot3d_target_defaults(oot3d_native_pica_azahar_capture NO_WARNING_OVERRIDE FOLDER Tools)

add_executable(oot3d_native_pica_azahar_capture_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_pica_frontend/oot3d_native_pica_azahar_capture_tests.cpp
)
target_link_libraries(oot3d_native_pica_azahar_capture_tests PRIVATE
    oot3d_native_pica_azahar_capture)
target_compile_options(oot3d_native_pica_azahar_capture_tests PRIVATE
    "${WARNING_OVERRIDE}")
oot3d_target_defaults(oot3d_native_pica_azahar_capture_tests NO_WARNING_OVERRIDE FOLDER Tests)

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
oot3d_target_defaults(oot3d_native_pica_azahar_corpus_validator NO_WARNING_OVERRIDE FOLDER Tools)

if(OOT3D_ENABLE_VULKAN_RENDERER AND TARGET three_ds_recomp_shaderc)
    add_executable(oot3d_native_pica_aot_compiler EXCLUDE_FROM_ALL
        tools/oot3d/native_pica_frontend/oot3d_native_pica_aot_compiler.cpp
        runtime/three_ds_recomp/src/fast/oot3d/pica_aot_shader_pack.cpp
    )
    include(runtime/three_ds_recomp/cmake/pass_shader_sources.cmake)
    triaevum_add_pass_shader_sources(oot3d_native_pica_aot_compiler)
    target_include_directories(oot3d_native_pica_aot_compiler PRIVATE
        runtime/three_ds_recomp/include)
    target_link_libraries(oot3d_native_pica_aot_compiler PRIVATE
        nlohmann_json::nlohmann_json
        three_ds_recomp_shaderc ${CMAKE_DL_LIBS})
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
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_property(TARGET oot3d_native_pica_aot_compiler APPEND PROPERTY BUILD_RPATH "$ORIGIN")
        set_property(TARGET oot3d_native_pica_aot_compiler PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()

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
# The translated title vertex family (title-derived, not title-neutral).
include(runtime/three_ds_recomp/cmake/spirv_pack.cmake)
triaevum_add_spirv_pack(oot3d_native_pica_vulkan_bridge
    tools/oot3d/native_pica_frontend/oot3d_native_vertex_binaries.cpp
    tools/oot3d/native_pica_frontend/oot3d_native_vertex.spvpack)
target_include_directories(oot3d_native_pica_vulkan_bridge PUBLIC
    tools/oot3d/native_game_runtime
)
target_link_libraries(oot3d_native_pica_vulkan_bridge PUBLIC
    oot3d_native_pica_vulkan_plan
    oot3d_renderer_contracts)
oot3d_target_defaults(oot3d_native_pica_vulkan_bridge FOLDER Tools)

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
oot3d_target_defaults(triaevum_oot3d_pica_renderer NO_WARNING_OVERRIDE FOLDER Tools)

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
oot3d_target_defaults(triaevum_oot3d_pica_batch_tests NO_WARNING_OVERRIDE FOLDER Tests)

add_executable(triaevum_oot3d_pica_renderer_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/triaevum_oot3d_pica_renderer_tests.cpp
)
target_link_libraries(triaevum_oot3d_pica_renderer_tests PRIVATE
    triaevum_oot3d_pica_renderer)
target_compile_options(triaevum_oot3d_pica_renderer_tests PRIVATE
    "${WARNING_OVERRIDE}")
oot3d_target_defaults(triaevum_oot3d_pica_renderer_tests NO_WARNING_OVERRIDE FOLDER Tests)

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
oot3d_target_defaults(triaevum_runtime_layout_tests NO_WARNING_OVERRIDE FOLDER Tests)

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

add_executable(mm3d_native_lighting_tests EXCLUDE_FROM_ALL
    tools/oot3d/native_game_runtime/mm3d_native_lighting_tests.cpp)
target_link_libraries(mm3d_native_lighting_tests PRIVATE oot3d_native_pica_composition)
oot3d_target_defaults(mm3d_native_lighting_tests FOLDER Tests)
