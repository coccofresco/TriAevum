# Windows llvm-mingw build (x86_64-w64-windows-gnu)

The Windows product can be cross-built on Linux with the pinned llvm-mingw toolchain
(`CMake/TriAevumLlvmMingwToolchain.cmake`, vcpkg overlay triplet `CMake/vcpkg-triplets/x64-mingw-llvm.cmake`)
and packaged with `tools/triaevum_release/prepare_release.py --target x86_64-w64-windows-gnu`.
It is its own release platform: its modules and title caches never mix with the MSVC-ABI product's.

- Layout: `libc++.dll`, `libunwind.dll` and `shaderc_shared.dll` sit once beside the root executables, so the
  installed game executable and every module it loads share one C++ runtime. The `forge/` shader helpers link
  libc++ statically; the Rust Forge links libunwind statically (`+crt-static`).
- Build paths: set `TRIAEVUM_BUILD_ROOT` to the work root; the toolchain maps it to `/build` in every compile,
  including vcpkg ports, and released PE files carry no DWARF (`--strip-debug`, Rust `strip=debuginfo`).
  Our own build-machine paths are therefore absent.
- Known vendor paths: llvm-mingw's prebuilt `libc++.dll`, `libunwind.dll` and the static libc++abi linked into
  `shaderc_shared.dll` and the `forge/` shader helpers contain assertion file names under the toolchain
  vendor's public CI path `/home/runner/work/llvm-mingw/`; these are accepted as third-party strings, not
  build-machine paths of this project.
- Licences: `LICENSES/LLVM-Apache-2.0-WITH-LLVM-exception.txt` and `LICENSES/mingw-w64.txt`.
- The NVIDIA DLSS module is built separately as an MSVC-ABI DLL (xwin sysroot); `nvngx_dlss.dll` is verified on the
  Linux host by `tools/triaevum_release/dlss_runtime_signature.py` (osslsigncode, pinned trust bundle).
