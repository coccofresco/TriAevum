cmake_minimum_required(VERSION 3.30)
# macOS (Apple Silicon) runtime: the neutral product runtime over Vulkan/NRI
# with MoltenVK as the ICD. Mirrors ports/android/runtime: the whole-AOT title
# is the separately published triaevum_title_aot.dylib (ports/macos/title).
project(TriAevumMacRuntime LANGUAGES C CXX)
if(NOT APPLE OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "Configure on macOS with -DCMAKE_OSX_ARCHITECTURES=arm64")
endif()
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Apple libc++ kept std::stop_token/std::jthread behind -fexperimental-library
# in older toolchains; probe the configured compiler first.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-std=c++20")
check_cxx_source_compiles("#include <stop_token>\nint main(){std::stop_source s; return s.get_token().stop_requested();}"
    TRIAEVUM_MACOS_HAS_STOP_TOKEN)
if(NOT TRIAEVUM_MACOS_HAS_STOP_TOKEN)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-fexperimental-library>)
endif()
unset(CMAKE_REQUIRED_FLAGS)

set(OOT3D_WHOLE_AOT_PRODUCT_MODE ON CACHE BOOL "" FORCE)
set(OOT3D_DIRECT_AOT_PLUGIN ON CACHE BOOL "" FORCE)
set(OOT3D_ENABLE_VULKAN_RENDERER ON CACHE BOOL "" FORCE)
set(THREE_DS_RECOMP_ENABLE_VULKAN ON CACHE BOOL "" FORCE)
set(THREE_DS_RECOMP_ENABLE_NRI ON CACHE BOOL "" FORCE)
# Desktop vendor SDK paths are Windows/Linux only: no DLSS/NGX, SSSR, CACAO,
# experimental GI or the separately licensed iMMERSE module on macOS.
set(THREE_DS_RECOMP_ENABLE_NRI_UPSCALER_SDKS OFF CACHE BOOL "" FORCE)
# NIS is compiled from HLSL by ShaderMake/DXC, which has no macOS host build
# here; the spatial upscaler stays off until its SPIR-V ships prebuilt.
set(THREE_DS_RECOMP_ENABLE_NRI_NIS OFF CACHE BOOL "" FORCE)
set(THREE_DS_RECOMP_ENABLE_DLSS_MODULE OFF CACHE BOOL "" FORCE)
set(THREE_DS_RECOMP_ENABLE_CACAO OFF CACHE BOOL "" FORCE)
set(THREE_DS_RECOMP_ENABLE_SSSR OFF CACHE BOOL "" FORCE)
set(THREE_DS_RECOMP_ENABLE_IMMERSE OFF CACHE BOOL "" FORCE)
set(THREE_DS_RECOMP_OFFER_EXPERIMENTAL_GI OFF CACHE BOOL "" FORCE)
set(ENABLE_SCRIPTING OFF CACHE BOOL "" FORCE)
set(INCLUDE_MPQ_SUPPORT OFF CACHE BOOL "" FORCE)
set(TRIAEVUM_MACOS_DEPENDENCY_SOURCE_CACHE "" CACHE PATH "Existing FetchContent source trees (never share binary directories)")
if(TRIAEVUM_MACOS_DEPENDENCY_SOURCE_CACHE)
    file(GLOB cached_sources LIST_DIRECTORIES TRUE "${TRIAEVUM_MACOS_DEPENDENCY_SOURCE_CACHE}/*-src")
    foreach(cached_source IN LISTS cached_sources)
        get_filename_component(dependency "${cached_source}" NAME)
        string(REGEX REPLACE "-src$" "" dependency "${dependency}")
        string(TOUPPER "${dependency}" dependency)
        set("FETCHCONTENT_SOURCE_DIR_${dependency}" "${cached_source}" CACHE PATH "Cached dependency source" FORCE)
    endforeach()
endif()
get_filename_component(root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_subdirectory("${root}" product)
