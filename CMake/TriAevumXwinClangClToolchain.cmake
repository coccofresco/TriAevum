# MSVC-ABI cross toolchain (clang-cl + lld-link) over an xwin MSVC CRT /
# Windows SDK sysroot, for ONE purpose: the isolated DLSS module DLL
# (src/fast/dlss, plain-C boundary) of the llvm-mingw Windows product. NVIDIA's
# NGX SDK ships only MSVC static libraries, which MinGW cannot link.
#
# The compiler is the pinned llvm-mingw release's own clang (LLVM 22.1.6),
# driven in cl mode: llvm-mingw has no clang-cl name, so the caller points
# TRIAEVUM_CLANG_CL at a `clang-cl` link to that clang (the driver mode follows
# the program name). TRIAEVUM_XWIN_SYSROOT is the `xwin splat` output.
# The static CRT (/MT) is used: no vc-redist DLL ships.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_SYSTEM_VERSION 10.0)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TRIAEVUM_CLANG_CL TRIAEVUM_XWIN_SYSROOT)

foreach(_tri_var TRIAEVUM_CLANG_CL TRIAEVUM_XWIN_SYSROOT)
    if(NOT ${_tri_var} AND DEFINED ENV{${_tri_var}})
        set(${_tri_var} "$ENV{${_tri_var}}")
    endif()
    if(NOT ${_tri_var})
        message(FATAL_ERROR "Set ${_tri_var}")
    endif()
endforeach()

get_filename_component(_tri_bin "${TRIAEVUM_CLANG_CL}" DIRECTORY)
set(CMAKE_C_COMPILER "${TRIAEVUM_CLANG_CL}")
set(CMAKE_CXX_COMPILER "${TRIAEVUM_CLANG_CL}")
set(CMAKE_LINKER "${_tri_bin}/lld-link" CACHE FILEPATH "" FORCE)
set(CMAKE_AR "${_tri_bin}/llvm-lib" CACHE FILEPATH "" FORCE)
set(CMAKE_MT "${_tri_bin}/llvm-mt" CACHE FILEPATH "" FORCE)
set(CMAKE_RC_COMPILER "${_tri_bin}/llvm-rc" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_LINKER_TYPE LLD)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# xwin splats release CRT libraries only (no debug CRT).
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

set(_tri_x "${TRIAEVUM_XWIN_SYSROOT}")
file(GLOB _tri_sdk_versions LIST_DIRECTORIES true "${_tri_x}/sdk/include/10.*")
list(LENGTH _tri_sdk_versions _tri_sdk_count)
if(_tri_sdk_count EQUAL 1)
    set(_tri_sdk_inc "${_tri_sdk_versions}")
else()
    set(_tri_sdk_inc "${_tri_x}/sdk/include")
endif()
set(_tri_flags "/imsvc \"${_tri_x}/crt/include\"")
foreach(_tri_part ucrt um shared winrt)
    string(APPEND _tri_flags " /imsvc \"${_tri_sdk_inc}/${_tri_part}\" /imsvc \"${_tri_x}/sdk/include/${_tri_part}\"")
endforeach()
set(CMAKE_C_FLAGS_INIT "${_tri_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_tri_flags}")
set(_tri_libs "/libpath:\"${_tri_x}/crt/lib/x86_64\" /libpath:\"${_tri_x}/sdk/lib/um/x86_64\" /libpath:\"${_tri_x}/sdk/lib/ucrt/x86_64\"")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_tri_libs}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_tri_libs}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_tri_libs}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
unset(_tri_bin)
unset(_tri_x)
