# LLVM/Clang-cl toolchain for isolated OOT3D AOT experiments on Windows.
# Keep the validated MSVC build tree separate from builds using this file.

set(_oot3d_llvm_default_root "I:/oot3dre_tools/llvm-22.1.6")
if(DEFINED ENV{OOT3D_LLVM_ROOT} AND NOT "$ENV{OOT3D_LLVM_ROOT}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{OOT3D_LLVM_ROOT}" _oot3d_llvm_default_root)
endif()

set(OOT3D_LLVM_ROOT "${_oot3d_llvm_default_root}" CACHE PATH
    "Root of the pinned OOT3D LLVM toolchain")

set(_oot3d_clang_cl "${OOT3D_LLVM_ROOT}/bin/clang-cl.exe")
set(_oot3d_lld_link "${OOT3D_LLVM_ROOT}/bin/lld-link.exe")
set(_oot3d_llvm_lib "${OOT3D_LLVM_ROOT}/bin/llvm-lib.exe")

foreach(_oot3d_tool IN ITEMS
        "${_oot3d_clang_cl}"
        "${_oot3d_lld_link}"
        "${_oot3d_llvm_lib}")
    if(NOT EXISTS "${_oot3d_tool}")
        message(FATAL_ERROR "Pinned OOT3D LLVM tool is missing: ${_oot3d_tool}")
    endif()
endforeach()

set(CMAKE_C_COMPILER "${_oot3d_clang_cl}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${_oot3d_clang_cl}" CACHE FILEPATH "" FORCE)
set(CMAKE_LINKER "${_oot3d_lld_link}" CACHE FILEPATH "" FORCE)
set(CMAKE_AR "${_oot3d_llvm_lib}" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER_TARGET "x86_64-pc-windows-msvc" CACHE STRING "" FORCE)
set(CMAKE_CXX_COMPILER_TARGET "x86_64-pc-windows-msvc" CACHE STRING "" FORCE)
set(CMAKE_LINKER_TYPE "LLD" CACHE STRING "" FORCE)
set(CMAKE_TRY_COMPILE_CONFIGURATION "RelWithDebInfo")

unset(_oot3d_clang_cl)
unset(_oot3d_lld_link)
unset(_oot3d_llvm_lib)
unset(_oot3d_llvm_default_root)
unset(_oot3d_tool)
