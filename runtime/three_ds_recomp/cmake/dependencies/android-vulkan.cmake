# Cross-built compiler libraries, never host SDK binaries. Versions below come
# from shaderc v2026.2's DEPS; test-only dependencies are intentionally omitted.
include(FetchContent)
FetchContent_Declare(android_vulkan_headers
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.354.tar.gz
    SOURCE_SUBDIR _download_only)
FetchContent_Declare(android_spirv_headers
    URL https://github.com/KhronosGroup/SPIRV-Headers/archive/ad9184e76a66b1001c29db9b0a3e87f646c64de0.tar.gz
    SOURCE_SUBDIR _download_only)
FetchContent_Declare(android_spirv_tools
    URL https://github.com/KhronosGroup/SPIRV-Tools/archive/c1cb30bb04e2bf911755a40df1242cc6e3d83e26.tar.gz
    SOURCE_SUBDIR _download_only)
FetchContent_Declare(android_glslang
    URL https://github.com/KhronosGroup/glslang/archive/5ed4003a18a10a9d1bd7e43aaf1664499abffa83.tar.gz
    SOURCE_SUBDIR _download_only)
FetchContent_MakeAvailable(android_vulkan_headers android_spirv_headers android_spirv_tools android_glslang)
set(Vulkan_INCLUDE_DIR "${android_vulkan_headers_SOURCE_DIR}/include" CACHE PATH "Android Vulkan headers" FORCE)
find_library(Vulkan_LIBRARY vulkan REQUIRED)
set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE BOOL "" FORCE)
# Keep dependency export sets consistent. This target never invokes installation.
set(SHADERC_SKIP_INSTALL OFF CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(SHADERC_SPIRV_HEADERS_DIR "${android_spirv_headers_SOURCE_DIR}" CACHE STRING "" FORCE)
set(SPIRV-Headers_SOURCE_DIR "${android_spirv_headers_SOURCE_DIR}" CACHE STRING "" FORCE)
set(SHADERC_SPIRV_TOOLS_DIR "${android_spirv_tools_SOURCE_DIR}" CACHE STRING "" FORCE)
set(SHADERC_GLSLANG_DIR "${android_glslang_SOURCE_DIR}" CACHE STRING "" FORCE)
FetchContent_Declare(android_shaderc
    URL https://github.com/google/shaderc/archive/d5f08ae5c5a9a45165578445cbd0f9adf0223448.tar.gz)
FetchContent_MakeAvailable(android_shaderc)
add_library(three_ds_recomp_shaderc ALIAS shaderc)
