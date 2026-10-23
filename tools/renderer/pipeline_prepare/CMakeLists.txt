cmake_minimum_required(VERSION 3.30)
project(TriAevumPipelinePreparation LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
get_filename_component(root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
find_package(Vulkan REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
if(NOT TARGET NRI)
    set(TRIAEVUM_NRI_SOURCE_DIR "" CACHE PATH "Pinned NRI source checkout (developer build only)")
    if(NOT EXISTS "${TRIAEVUM_NRI_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "Set TRIAEVUM_NRI_SOURCE_DIR to the project's pinned NRI checkout")
    endif()
    set(NRI_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
    foreach(feature D3D11_SUPPORT D3D12_SUPPORT NVTX_SUPPORT XLIB_SUPPORT WAYLAND_SUPPORT NGX_SDK FFX_SDK XESS_SDK)
        set(NRI_ENABLE_${feature} OFF CACHE BOOL "" FORCE)
    endforeach()
    add_subdirectory("${TRIAEVUM_NRI_SOURCE_DIR}" nri)
    if(MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND TARGET NRI_Shared)
        # As the runtime build: NRI enables /WX for clang-cl after its Clang
        # suppressions; warnings in the pinned SDK must not block the helper.
        target_compile_options(NRI_Shared PUBLIC /WX-)
    endif()
endif()
set(runtime "${root}/runtime/three_ds_recomp")
add_library(triaevum_pipeline_preparation STATIC
    "${runtime}/src/fast/renderer/cache_file.cpp"
    "${runtime}/src/fast/renderer/pipeline_preparation_job.cpp"
    "${runtime}/src/fast/renderer3ds/vulkan_pipeline_cache_store.cpp"
    "${runtime}/src/fast/renderer3ds/nri_pica_pipeline_bridge.cpp"
    "${runtime}/src/fast/renderer3ds/pica_preparation_queue.cpp"
    "${runtime}/src/fast/renderer3ds/nri_pica_upload_arena.cpp"
    "${runtime}/src/fast/renderer3ds/pica_nri_upload.cpp"
    "${runtime}/src/fast/renderer3ds/pica_nri_shader_contract.cpp"
    "${runtime}/src/fast/oot3d/pica_nri_pipeline_state.cpp"
    "${runtime}/src/fast/oot3d/pica_pipeline_preparation.cpp"
    "${runtime}/src/fast/oot3d/pica_nri_vertex_input.cpp"
    "${runtime}/src/fast/oot3d/pica_aot_shader_pack.cpp"
    "${runtime}/src/fast/oot3d/pica_pipeline_manifest.cpp")
target_include_directories(triaevum_pipeline_preparation PUBLIC "${runtime}/include")
target_compile_definitions(triaevum_pipeline_preparation PUBLIC ENABLE_RENDERER3DS_VULKAN ENABLE_RENDERER3DS_NRI)
target_link_libraries(triaevum_pipeline_preparation PUBLIC Vulkan::Vulkan NRI nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_definitions(triaevum_pipeline_preparation PUBLIC NOMINMAX)
    target_compile_options(triaevum_pipeline_preparation PRIVATE /utf-8)
endif()
# The built-in fragment families resolve before the portable pack, as in the
# runtime (pica_builtin_fragment_families.h).
add_executable(triaevum_nri_pipeline_prepare main.cpp "${runtime}/src/fast/oot3d/pica_builtin_fragment_families.cpp")
target_link_libraries(triaevum_nri_pipeline_prepare PRIVATE triaevum_pipeline_preparation)
# --startup-programs embeds the translated title vertex family, as the runtime
# executable does: this helper is the title's Forge tool, not title-neutral.
target_include_directories(triaevum_nri_pipeline_prepare PRIVATE "${root}/tools/oot3d/native_pica_frontend")
include("${runtime}/cmake/spirv_pack.cmake")
triaevum_add_spirv_pack(triaevum_nri_pipeline_prepare "${root}/tools/oot3d/native_pica_frontend/oot3d_native_vertex_binaries.cpp"
    "${root}/tools/oot3d/native_pica_frontend/oot3d_native_vertex.spvpack")
foreach(_pack renderer3ds/pica_native_fragment oot3d/pica_temporal_fragment oot3d/pica_toon_fragment
              oot3d/pica_uber_fragment)
    triaevum_add_spirv_pack(triaevum_nri_pipeline_prepare "${runtime}/src/fast/${_pack}_binaries.cpp"
        "${runtime}/src/fast/${_pack}.spvpack")
endforeach()
add_executable(triaevum_pipeline_preparation_tests tests.cpp)
target_link_libraries(triaevum_pipeline_preparation_tests PRIVATE triaevum_pipeline_preparation)
enable_testing()
add_test(NAME pipeline_preparation_contract COMMAND triaevum_pipeline_preparation_tests)
