cmake_minimum_required(VERSION 3.26 FATAL_ERROR)

project(Oot3dSwitchGlProbe LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    message(FATAL_ERROR "The OpenGL probe requires the libnx toolchain")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

find_package(SDL2 REQUIRED)
find_library(GLAD_LIBRARY glad
    HINTS "${DEVKITPRO}/portlibs/switch/lib" REQUIRED)
find_library(EGL_LIBRARY EGL
    HINTS "${DEVKITPRO}/portlibs/switch/lib" REQUIRED)
find_library(GLAPI_LIBRARY glapi
    HINTS "${DEVKITPRO}/portlibs/switch/lib" REQUIRED)
find_library(DRM_NOUVEAU_LIBRARY drm_nouveau
    HINTS "${DEVKITPRO}/portlibs/switch/lib" REQUIRED)

add_executable(oot3d_switch_gl_probe switch_gl_probe.cpp)
target_include_directories(oot3d_switch_gl_probe PRIVATE
    "${DEVKITPRO}/portlibs/switch/include")
target_compile_options(oot3d_switch_gl_probe PRIVATE -O2 -g0)
target_link_libraries(oot3d_switch_gl_probe PRIVATE
    SDL2::SDL2
    ${GLAD_LIBRARY}
    ${EGL_LIBRARY}
    ${GLAPI_LIBRARY}
    ${DRM_NOUVEAU_LIBRARY})

nx_generate_nacp(oot3d_switch_gl_probe.nacp
    NAME "OoT3D Switch OpenGL probe"
    AUTHOR "OoT3D Recompiled contributors"
    VERSION "0.1.0")
nx_create_nro(oot3d_switch_gl_probe
    NACP oot3d_switch_gl_probe.nacp)
