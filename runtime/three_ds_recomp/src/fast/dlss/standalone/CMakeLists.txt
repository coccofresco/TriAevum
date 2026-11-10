# Standalone build of the DLSS module (triaevum_dlss.dll) alone, for a Windows
# product whose runtime is NOT built with an MSVC-ABI toolchain (llvm-mingw).
# NVIDIA's NGX SDK is MSVC static libraries only, so this one DLL is built with
# clang-cl/lld-link over an MSVC CRT + Windows SDK sysroot
# (CMake/TriAevumXwinClangClToolchain.cmake) and the static CRT. Only the
# plain-C interface of module_interface/triaevum/dlss_module.h crosses into the
# runtime. The MSVC-built product keeps building the module in-tree as before.
#
#   cmake -S runtime/three_ds_recomp/src/fast/dlss/standalone -B <build> -G Ninja
#     -DCMAKE_TOOLCHAIN_FILE=<repo>/CMake/TriAevumXwinClangClToolchain.cmake
#     -DTHREE_DS_RECOMP_NGX_SOURCE_DIR=<NGX SDK> -DTHREE_DS_RECOMP_NGX_VERSION=<x.y.z>
#     -DTRIAEVUM_VULKAN_HEADERS_DIR=<Vulkan-Headers/include>
cmake_minimum_required(VERSION 3.26)
project(triaevum_dlss_standalone LANGUAGES CXX)

if(NOT MSVC OR NOT WIN32)
    message(FATAL_ERROR "The standalone DLSS module needs an MSVC-ABI Windows toolchain")
endif()
set(THREE_DS_RECOMP_NGX_SOURCE_DIR "" CACHE PATH "NVIDIA NGX/DLSS SDK root")
set(THREE_DS_RECOMP_NGX_VERSION "310.7.0" CACHE STRING "NGX SDK version of that root")
set(TRIAEVUM_VULKAN_HEADERS_DIR "" CACHE PATH "Vulkan-Headers include directory")
if(NOT EXISTS "${THREE_DS_RECOMP_NGX_SOURCE_DIR}/include" OR
   NOT EXISTS "${TRIAEVUM_VULKAN_HEADERS_DIR}/vulkan/vulkan.h")
    message(FATAL_ERROR "Set THREE_DS_RECOMP_NGX_SOURCE_DIR and TRIAEVUM_VULKAN_HEADERS_DIR")
endif()
set(THREE_DS_RECOMP_NGX_INCLUDE_DIR "${THREE_DS_RECOMP_NGX_SOURCE_DIR}/include")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# dlss.cmake takes the Vulkan headers from Vulkan::Vulkan; the module calls
# Vulkan only through the host's GetInstanceProcAddr, so headers suffice.
add_library(Vulkan::Vulkan INTERFACE IMPORTED)
set_target_properties(Vulkan::Vulkan PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${TRIAEVUM_VULKAN_HEADERS_DIR}")

include("${CMAKE_CURRENT_LIST_DIR}/../dlss.cmake")
