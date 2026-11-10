# Cross toolchain for the Windows x86_64 product built with llvm-mingw
# (UCRT, libc++, lld) on a Linux build machine. Additive: the clang-cl/MSVC
# route (Oot3dLlvmClToolchain.cmake, vcpkg x64-windows-static) is unchanged.
#
# The pin is the llvm-mingw RELEASE identity, not a bare compiler version:
# release tag, archive sha256 and the clang version it carries. The archive
# hash is recorded by whoever unpacked it in <root>/TRIAEVUM_LLVM_MINGW_SHA256
# (a one-line file next to bin/); configure refuses any other release.
#
# Set TRIAEVUM_LLVM_MINGW_ROOT (cache or environment) to the unpacked
# llvm-mingw-<tag>-ucrt-<host> directory.

set(TRIAEVUM_LLVM_MINGW_RELEASE "20260519")
set(TRIAEVUM_LLVM_MINGW_SHA256
    "a48f8c2801508272ccde64d87a26747ecc5306623d9a080a42ed80dc61f79fa2")
set(TRIAEVUM_LLVM_MINGW_CLANG_VERSION "22.1.6")

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_SYSTEM_VERSION 10.0)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TRIAEVUM_LLVM_MINGW_ROOT)

if(NOT TRIAEVUM_LLVM_MINGW_ROOT AND DEFINED ENV{TRIAEVUM_LLVM_MINGW_ROOT})
    set(TRIAEVUM_LLVM_MINGW_ROOT "$ENV{TRIAEVUM_LLVM_MINGW_ROOT}")
endif()
set(TRIAEVUM_LLVM_MINGW_ROOT "${TRIAEVUM_LLVM_MINGW_ROOT}" CACHE PATH
    "Unpacked llvm-mingw ${TRIAEVUM_LLVM_MINGW_RELEASE} (UCRT) root")
if(NOT TRIAEVUM_LLVM_MINGW_ROOT)
    message(FATAL_ERROR "Set TRIAEVUM_LLVM_MINGW_ROOT to the unpacked llvm-mingw "
        "${TRIAEVUM_LLVM_MINGW_RELEASE} UCRT release")
endif()

get_filename_component(_tri_mingw_dir "${TRIAEVUM_LLVM_MINGW_ROOT}" NAME)
if(NOT _tri_mingw_dir MATCHES "^llvm-mingw-${TRIAEVUM_LLVM_MINGW_RELEASE}-ucrt-")
    message(FATAL_ERROR "llvm-mingw root '${_tri_mingw_dir}' is not the pinned "
        "release ${TRIAEVUM_LLVM_MINGW_RELEASE} (UCRT)")
endif()
set(_tri_mingw_hash_file "${TRIAEVUM_LLVM_MINGW_ROOT}/TRIAEVUM_LLVM_MINGW_SHA256")
if(NOT EXISTS "${_tri_mingw_hash_file}")
    message(FATAL_ERROR "Missing ${_tri_mingw_hash_file} (the verified archive sha256)")
endif()
file(STRINGS "${_tri_mingw_hash_file}" _tri_mingw_hash LIMIT_COUNT 1)
string(STRIP "${_tri_mingw_hash}" _tri_mingw_hash)
if(NOT _tri_mingw_hash STREQUAL TRIAEVUM_LLVM_MINGW_SHA256)
    message(FATAL_ERROR "llvm-mingw archive sha256 '${_tri_mingw_hash}' is not the pinned "
        "${TRIAEVUM_LLVM_MINGW_SHA256}")
endif()

set(_tri_mingw_bin "${TRIAEVUM_LLVM_MINGW_ROOT}/bin")
set(CMAKE_C_COMPILER "${_tri_mingw_bin}/x86_64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${_tri_mingw_bin}/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER "${_tri_mingw_bin}/x86_64-w64-mingw32-windres")
set(CMAKE_AR "${_tri_mingw_bin}/llvm-ar" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${_tri_mingw_bin}/llvm-ranlib" CACHE FILEPATH "" FORCE)
set(CMAKE_LINKER_TYPE LLD)

execute_process(COMMAND "${CMAKE_C_COMPILER}" --version
    OUTPUT_VARIABLE _tri_mingw_version COMMAND_ERROR_IS_FATAL ANY)
if(NOT _tri_mingw_version MATCHES "clang version ${TRIAEVUM_LLVM_MINGW_CLANG_VERSION}[ \n]")
    message(FATAL_ERROR "llvm-mingw clang is not ${TRIAEVUM_LLVM_MINGW_CLANG_VERSION}: ${_tri_mingw_version}")
endif()

# The Windows SDK defaults to the Windows 10 API set; mingw-w64 defaults to
# Windows 7 (0x601). Match the MSVC build's target (Windows 10).
set(CMAKE_C_FLAGS_INIT "-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00")
set(CMAKE_CXX_FLAGS_INIT "-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00")
# TRIAEVUM_BUILD_ROOT (environment): the build machine's work root. Every
# compile under it records /build instead (__FILE__, assertions), also in
# vcpkg ports, whose triplet chainloads this file and so never sees
# VCPKG_C_FLAGS (CMake/vcpkg-triplets/x64-mingw-llvm.cmake passes the variable).
if(DEFINED ENV{TRIAEVUM_BUILD_ROOT} AND NOT "$ENV{TRIAEVUM_BUILD_ROOT}" STREQUAL "")
    string(APPEND CMAKE_C_FLAGS_INIT " -ffile-prefix-map=$ENV{TRIAEVUM_BUILD_ROOT}=/build")
    string(APPEND CMAKE_CXX_FLAGS_INIT " -ffile-prefix-map=$ENV{TRIAEVUM_BUILD_ROOT}=/build")
endif()
# Released PE files carry no DWARF: the prebuilt static libraries (the
# toolchain's mingw-w64 CRT, vcpkg ports) would otherwise embed their build
# machines' paths, which the release audit refuses. MSVC keeps debug data in
# PDBs that are not shipped; this is the llvm-mingw equivalent.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--strip-debug")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-Wl,--strip-debug")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-Wl,--strip-debug")

set(CMAKE_FIND_ROOT_PATH "${TRIAEVUM_LLVM_MINGW_ROOT}/x86_64-w64-mingw32" ${CMAKE_FIND_ROOT_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# One shared libc++/libunwind for every C++ image of the product (runtime,
# title plugin, iMMERSE module, helpers): the package ships libc++.dll and
# libunwind.dll from x86_64-w64-mingw32/bin. Build-time Windows tools run
# through the emulator (wine) when one is given.
set(TRIAEVUM_LLVM_MINGW_RUNTIME_DIR "${TRIAEVUM_LLVM_MINGW_ROOT}/x86_64-w64-mingw32/bin"
    CACHE INTERNAL "")
if(DEFINED ENV{TRIAEVUM_WINE} AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    set(CMAKE_CROSSCOMPILING_EMULATOR "$ENV{TRIAEVUM_WINE}")
endif()

unset(_tri_mingw_dir)
unset(_tri_mingw_hash_file)
unset(_tri_mingw_hash)
unset(_tri_mingw_version)
