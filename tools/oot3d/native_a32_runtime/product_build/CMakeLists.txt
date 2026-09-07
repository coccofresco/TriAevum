cmake_minimum_required(VERSION 3.26 FATAL_ERROR)

project(Oot3dWholeAotProduct LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

foreach(required_variable IN ITEMS
        OOT3D_REPO_ROOT
        OOT3D_WHOLE_AOT_GENERATED_DIR
        OOT3D_NLOHMANN_INCLUDE_DIR)
    if(NOT DEFINED ${required_variable} OR
       NOT EXISTS "${${required_variable}}")
        message(FATAL_ERROR
            "${required_variable} must name an existing product input")
    endif()
endforeach()

file(GLOB whole_aot_shards CONFIGURE_DEPENDS
    "${OOT3D_WHOLE_AOT_GENERATED_DIR}/oot3d_whole_aot_shard_*.cpp")
list(LENGTH whole_aot_shards whole_aot_shard_count)
if(NOT whole_aot_shard_count EQUAL 256)
    message(FATAL_ERROR
        "Expected 256 whole-AOT shards, found ${whole_aot_shard_count}")
endif()

add_library(oot3d_native_whole_aot STATIC
    "${OOT3D_WHOLE_AOT_GENERATED_DIR}/oot3d_whole_aot_generated.cpp"
    ${whole_aot_shards})

target_include_directories(oot3d_native_whole_aot PRIVATE
    "${OOT3D_WHOLE_AOT_GENERATED_DIR}"
    "${OOT3D_REPO_ROOT}/tools/oot3d/native_game_runtime"
    "${OOT3D_REPO_ROOT}/tools/oot3d/native_a32_runtime"
    "${OOT3D_REPO_ROOT}/tools/oot3d/native_a32_runtime/upstream"
    "${OOT3D_NLOHMANN_INCLUDE_DIR}")

target_compile_definitions(oot3d_native_whole_aot PRIVATE
    NOMINMAX
    OOT3D_NATIVE_GENERATED_WHOLE_AOT=1)

if(MSVC)
    target_compile_options(oot3d_native_whole_aot PRIVATE
        /O2
        /Ob2
        /bigobj
        /fp:strict
        /w
        /Z7
        /Zc:preprocessor)
    set_property(TARGET oot3d_native_whole_aot PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    message(FATAL_ERROR
        "The pinned whole-AOT product archive currently requires clang-cl")
endif()

set_target_properties(oot3d_native_whole_aot PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    OUTPUT_NAME "oot3d_native_whole_aot")
