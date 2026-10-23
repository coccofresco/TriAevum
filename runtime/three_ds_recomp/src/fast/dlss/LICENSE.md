# DLSS module: licence boundary

This directory, `runtime/three_ds_recomp/src/fast/dlss/`, is the DLSS module
of TriAevum: DLSS Super Resolution through NVIDIA's NGX SDK. It is an
explicit exception to TriAevum's licensing. The GNU General Public License
that covers TriAevum's own code (`LICENSE`, `LICENSE_SCOPE.md`) does not apply
to this directory or to the module binary built from it.

## What the module contains

- `dlss_module.cpp`: TriAevum's integration code, licensed under the MIT
  License (the notice in `module_interface/triaevum/dlss_module.h`). Its NGX
  sequence follows NVIDIA's NRI (MIT, copyright NVIDIA Corporation,
  `LICENSES/NRI-MIT.txt`).
- NVIDIA's NGX SDK static library (`nvsdk_ngx_*.lib` on Windows,
  `libnvsdk_ngx.a` on Linux), linked into the module binary.

The module binary, `triaevum_dlss.dll` (Windows) or `libtriaevum_dlss.so`
(Linux), is distributed beside NVIDIA's DLSS runtime, `nvngx_dlss.dll` or
`libnvidia-ngx-dlss.so.<version>`, which NVIDIA builds and signs.

## Source

Both NVIDIA components come unmodified from NVIDIA's public DLSS SDK,
[github.com/NVIDIA/DLSS](https://github.com/NVIDIA/DLSS), at the version in
`THREE_DS_RECOMP_NGX_VERSION` (`runtime/three_ds_recomp/CMakeLists.txt`). The
build checks the Windows runtime's NVIDIA Authenticode signature and version
(`cmake/ValidateNvidiaDlssRuntime.cmake`).

## Terms

- The NVIDIA components are NVIDIA's property, distributed under the NVIDIA
  RTX SDKs licence that accompanies the SDK, reproduced unchanged in packages
  as `LICENSES/NVIDIA-DLSS-SDK-LICENSE.txt`. Recipients receive them under
  those terms, not under the GPL and not under the MIT License; in particular
  they may not be reverse engineered or redistributed on their own.
- The module is distributed as part of TriAevum, an application with material
  functionality of its own, and is used only on systems with NVIDIA GPUs:
  the runtime offers DLSS on NVIDIA GPUs only, when NGX confirms it.
- DLSS is credited where it is offered: F1 > Display > Antialiasing and
  upscaling shows the module's attribution under the DLSS controls, and
  `THIRD_PARTY_NOTICES.md` records it. TriAevum is not sponsored or endorsed
  by NVIDIA.
- TriAevum is not a commercial release. NVIDIA asks to be notified before a
  commercial release of an application using the DLSS SDK
  (<https://developer.nvidia.com/sw-notification>).

## Boundary

The runtime (GPL) links nothing of NGX. It loads the module when the library
is installed beside the executable (`fast/oot3d/dlss_module_loader.cpp`), and
the two meet only through `module_interface/triaevum/dlss_module.h` (MIT):
plain C values and Vulkan handles, with the host owning every image, view,
command buffer and barrier. Without the module, or without NVIDIA's runtime,
DLSS is not offered and every other feature, NIS and FSR included, works.

The release audit (`tools/triaevum_release/audit_release.py`,
`proprietary_sdk_markers` in `release_policy.json`) rejects a package whose
runtime, Forge, title module or other GPL binary contains NGX code.
