# vcpkg overlay triplet of the llvm-mingw Windows build (XBUILD; x86_64-w64-windows-gnu).
# Use: vcpkg install --overlay-triplets=<repo>/CMake/vcpkg-triplets --triplet x64-mingw-llvm --host-triplet x64-linux
# with TRIAEVUM_LLVM_MINGW_ROOT (the pinned llvm-mingw) and TRIAEVUM_BUILD_ROOT (the work root that
# contains vcpkg; mapped to /build in __FILE__) in the environment. Static libraries, UCRT.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME MinGW)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../TriAevumLlvmMingwToolchain.cmake")
set(VCPKG_ENV_PASSTHROUGH TRIAEVUM_LLVM_MINGW_ROOT TRIAEVUM_BUILD_ROOT PATH)
