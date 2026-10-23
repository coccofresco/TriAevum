# License inventory

This directory is the release-facing index for third-party licenses. Until
dependency vendoring is normalized, authoritative copies also remain beside
their imported source.

| Component | License | Authoritative repository copy |
| --- | --- | --- |
| Original TriAevum contributions | GPL-3.0-or-later | Root `LICENSE` and `LICENSE_SCOPE.md` |
| Azahar PICA subset | GPL-2.0-or-later | File headers and `tools/oot3d/third_party/azahar_audio/LICENSE.txt` |
| Azahar audio subset | GPL-2.0-or-later | `tools/oot3d/third_party/azahar_audio/LICENSE.txt` |
| nihstro subset | BSD-3-Clause | `tools/oot3d/third_party/azahar_pica/NIHSTRO_LICENSE.txt` |
| libultraship | MIT | Upstream submodule `runtime/three_ds_recomp/LICENSE` |
| NVIDIA NRI | MIT | `LICENSES/NRI-MIT.txt` and pinned upstream source |
| TriAevum DLSS module (separately licensed) | MIT for its integration code; NVIDIA's NGX SDK library linked into it under NVIDIA's terms | `runtime/three_ds_recomp/src/fast/dlss/LICENSE.md` |
| NVIDIA DLSS SDK (NGX library, `nvngx_dlss` runtime) | NVIDIA RTX SDKs licence | `LICENSES/NVIDIA-DLSS-SDK-LICENSE.txt`, copied by the packager from the SDK |
| AMD FidelityFX SDK FSR runtime (`amd_fidelityfx_vk.dll`) | MIT | `LICENSES/FidelityFX-SDK-MIT.txt` |
| Godot Engine contact shadows port | MIT | `LICENSES/Godot-MIT.txt` and the ported file headers |
| SDL2 2.32.10 (statically linked) | zlib | `LICENSES/SDL2-zlib.txt` |
| SDL_GameControllerDB mappings | zlib | `LICENSES/SDL-GameControllerDB-zlib.txt` |
| Zstandard 1.5.6 (statically linked) | BSD-3-Clause (upstream is dual BSD/GPL-2.0; used under BSD) | `LICENSES/zstd-BSD-3-Clause.txt` |
| Microsoft Visual C++ runtime for shaderc | Microsoft runtime redistribution terms | `LICENSES/Microsoft-Visual-Cpp-Runtime.md` |
| NXVK probe | GPL-2.0-or-later | `ports/switch_vulkan_probe/COPYING` |
| shaderc shared runtime | Apache-2.0 | `LICENSES/shaderc-Apache-2.0.txt` |
| CPython 3.13 runtime embedded by Forge | Python Software Foundation License | `LICENSES/Python-3.13.txt` |
| Capstone 5.0.7 embedded by Forge | BSD-style | `LICENSES/Capstone-BSD.txt` |
| PyInstaller 6.16.0 bootloader | GPL-2.0-or-later with bootloader exception | `LICENSES/PyInstaller-GPL-2.0-or-later-with-bootloader-exception.txt` |
| LLVM 22 Forge tools (`llc`, `lld-link`) | Apache-2.0 WITH LLVM-exception | `LICENSES/shaderc-Apache-2.0.txt` plus `LICENSES/LLVM-exception.txt` |

The public packager must include the root `LICENSE` and copy applicable donor
texts into its own `LICENSES` directory. This index does not replace any
license text.
