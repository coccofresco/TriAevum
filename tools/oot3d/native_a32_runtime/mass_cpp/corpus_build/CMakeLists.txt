cmake_minimum_required(VERSION 3.24)

project(oot3d_mass_aot_corpus LANGUAGES CXX)

set(OOT3D_MASS_AOT_GENERATED_DIR "" CACHE PATH
    "Directory containing dispatch.cpp, oot3d_mass_generated.h and functions")
set(OOT3D_MASS_AOT_RUNTIME_ROOT "" CACHE PATH
    "Runtime include root containing recomp/a32_direct_runtime.h")
option(OOT3D_MASS_AOT_ENABLE_THINLTO
    "Retain LLVM IR in the archive for final-link ThinLTO" OFF)

if(NOT EXISTS "${OOT3D_MASS_AOT_GENERATED_DIR}/dispatch.cpp" OR
   NOT EXISTS "${OOT3D_MASS_AOT_GENERATED_DIR}/oot3d_mass_generated.h")
    message(FATAL_ERROR "OOT3D_MASS_AOT_GENERATED_DIR is incomplete")
endif()
if(NOT EXISTS "${OOT3D_MASS_AOT_RUNTIME_ROOT}/recomp/a32_direct_runtime.h")
    message(FATAL_ERROR "OOT3D_MASS_AOT_RUNTIME_ROOT is incomplete")
endif()

file(GLOB _oot3d_mass_aot_shards CONFIGURE_DEPENDS
    "${OOT3D_MASS_AOT_GENERATED_DIR}/functions/shard_*.cpp")
if(NOT _oot3d_mass_aot_shards)
    message(FATAL_ERROR "No generated mass-AOT shards were found")
endif()

add_library(oot3d_mass_aot_corpus STATIC
    "${OOT3D_MASS_AOT_GENERATED_DIR}/oot3d_mass_generated.h"
    "${OOT3D_MASS_AOT_GENERATED_DIR}/dispatch.cpp"
    ${_oot3d_mass_aot_shards})
target_compile_features(oot3d_mass_aot_corpus PUBLIC cxx_std_17)
target_include_directories(oot3d_mass_aot_corpus PRIVATE
    "${OOT3D_MASS_AOT_GENERATED_DIR}"
    "${OOT3D_MASS_AOT_RUNTIME_ROOT}"
    "${OOT3D_MASS_AOT_RUNTIME_ROOT}/../upstream/recomp")

if(MSVC)
    set_property(TARGET oot3d_mass_aot_corpus PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_compile_options(oot3d_mass_aot_corpus PRIVATE /O2 /Ob2 /w)
else()
    target_compile_options(oot3d_mass_aot_corpus PRIVATE -O2 -w)
endif()

if(OOT3D_MASS_AOT_ENABLE_THINLTO)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "ThinLTO requires Clang")
    endif()
    target_compile_options(oot3d_mass_aot_corpus PRIVATE -flto=thin)
endif()

set_target_properties(oot3d_mass_aot_corpus PROPERTIES
    OUTPUT_NAME "oot3d-a32-mass-aot")
