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
endif()
set(runtime "${root}/runtime/three_ds_recomp")
add_library(triaevum_pipeline_preparation STATIC
    "${runtime}/src/fast/renderer/pipeline_preparation_job.cpp"
    "${runtime}/src/fast/renderer3ds/vulkan_pipeline_cache_store.cpp"
    "${runtime}/src/fast/renderer3ds/nri_pica_pipeline_bridge.cpp"
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
add_executable(triaevum_nri_pipeline_prepare main.cpp)
target_link_libraries(triaevum_nri_pipeline_prepare PRIVATE triaevum_pipeline_preparation)
add_executable(triaevum_pipeline_preparation_tests tests.cpp)
target_link_libraries(triaevum_pipeline_preparation_tests PRIVATE triaevum_pipeline_preparation)
enable_testing()
add_test(NAME pipeline_preparation_contract COMMAND triaevum_pipeline_preparation_tests)
