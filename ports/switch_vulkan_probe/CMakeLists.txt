cmake_minimum_required(VERSION 3.26 FATAL_ERROR)

project(Oot3dSwitchVulkanProbe LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    message(FATAL_ERROR "The Vulkan probe requires the libnx toolchain")
endif()

set(OOT3D_NXVK_ROOT "" CACHE PATH
    "Explicit path to an NXVK source tree or installed package prefix")
if(NOT OOT3D_NXVK_ROOT)
    message(FATAL_ERROR
        "OOT3D_NXVK_ROOT is required. Pass the NXVK source tree after `make` "
        "or an installed package prefix; the probe never discovers or fetches "
        "a Vulkan driver implicitly.")
endif()

cmake_path(ABSOLUTE_PATH OOT3D_NXVK_ROOT NORMALIZE
    OUTPUT_VARIABLE OOT3D_NXVK_ROOT_ABSOLUTE)

set(_nxvk_include "${OOT3D_NXVK_ROOT_ABSOLUTE}/include")
if(EXISTS "${OOT3D_NXVK_ROOT_ABSOLUTE}/lib/libnvk.a")
    set(_nxvk_library_dir "${OOT3D_NXVK_ROOT_ABSOLUTE}/lib")
elseif(EXISTS
       "${OOT3D_NXVK_ROOT_ABSOLUTE}/switch/build/pkg/lib/libnvk.a")
    set(_nxvk_library_dir
        "${OOT3D_NXVK_ROOT_ABSOLUTE}/switch/build/pkg/lib")
else()
    message(FATAL_ERROR
        "${OOT3D_NXVK_ROOT_ABSOLUTE} has no packaged libnvk.a. Build/stage "
        "NXVK first; expected lib/libnvk.a or "
        "switch/build/pkg/lib/libnvk.a.")
endif()

foreach(_required_file IN ITEMS
        "${_nxvk_include}/vulkan/vulkan.h"
        "${_nxvk_include}/vulkan/vulkan_vi.h"
        "${_nxvk_library_dir}/libnvk.a"
        "${_nxvk_library_dir}/libnvk_support.a")
    if(NOT EXISTS "${_required_file}")
        message(FATAL_ERROR "NXVK dependency is incomplete: ${_required_file}")
    endif()
endforeach()

find_library(Z_LIBRARY z
    HINTS "${DEVKITPRO}/portlibs/switch/lib" REQUIRED)
find_library(EXPAT_LIBRARY expat
    HINTS "${DEVKITPRO}/portlibs/switch/lib" REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

add_executable(oot3d_switch_vulkan_probe
    nxvk_newlib_compat.cpp
    switch_vulkan_probe.cpp)
target_include_directories(oot3d_switch_vulkan_probe PRIVATE
    "${_nxvk_include}"
    "${DEVKITPRO}/libnx/include")
target_compile_definitions(oot3d_switch_vulkan_probe PRIVATE
    VK_NO_PROTOTYPES
    VK_USE_PLATFORM_VI_NN)
target_compile_options(oot3d_switch_vulkan_probe PRIVATE
    -O2
    -g0
    -ffunction-sections
    -fdata-sections)

# NXVK is a static ICD. It must be retained as a whole archive and reached
# through vk_icdGetInstanceProcAddr; the support archive can still be garbage
# collected. Keep this dependency confined to the standalone probe target.
target_link_options(oot3d_switch_vulkan_probe PRIVATE
    "-Wl,-u,vk_icdGetInstanceProcAddr"
    "-Wl,--gc-sections"
    "-Wl,--whole-archive"
    "${_nxvk_library_dir}/libnvk.a"
    "-Wl,--no-whole-archive"
    "-Wl,--start-group"
    "${_nxvk_library_dir}/libnvk_support.a"
    "${Z_LIBRARY}"
    "${EXPAT_LIBRARY}"
    "-lnx"
    "-lc"
    "-lm"
    "-lstdc++"
    "-pthread"
    "-Wl,--end-group")

message(WARNING
    "This target statically links the separately supplied NXVK dependency. "
    "The audited NXVK Switch fork is GPL-2.0-or-later; assess distribution "
    "obligations before sharing the resulting NRO. This does not enable NXVK "
    "in the OoT3D product.")

nx_generate_nacp(oot3d_switch_vulkan_probe.nacp
    NAME "OoT3D Switch Vulkan probe"
    AUTHOR "OoT3D Recompiled contributors"
    VERSION "0.1.0")
nx_create_nro(oot3d_switch_vulkan_probe
    NACP oot3d_switch_vulkan_probe.nacp)
