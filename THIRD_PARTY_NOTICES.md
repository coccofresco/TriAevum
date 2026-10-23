# Third-party notices

TriAevum contains or derives from the projects below. This record describes
the current known scope; source-file notices and copied license texts remain
authoritative. Renaming a module never removes its copyright, license or
history.

Community patches and their authors are recorded in
[Community Contributions](docs/TRIAEVUM_CONTRIBUTIONS.md), including the
Linux infrastructure contribution by [999sian](https://github.com/999sian)
in [PR #6](https://github.com/coccofresco/TriAevum/pull/6). Partial/adapted
integrations retain their original provenance and Git coauthor credit.

## Shipwright / Ship of Harkinian

This repository originated as a fork of
[HarbourMasters/Shipwright](https://github.com/HarbourMasters/Shipwright).
Retained history includes platform integration, resource-system and Fast3D
compatibility work. The N64 game implementation and its extraction toolchain
are not current TriAevum product dependencies. Shipwright also provides the
technical precedent of preparing assets locally from a user-provided game
copy; that precedent is not legal advice or permission for TriAevum content.

The upstream project is commonly named Ship of Harkinian / Shipwright. That is
the donor referred to in project discussions as "Ship of Arkanian."

## Dusklight / Aurora controller architecture

The shared controller identity and routing work references
[TwilitRealm/Dusklight](https://github.com/TwilitRealm/dusklight) at
`97d46baec1c9b1af11b9c9d52314ef1d234911be` (CC0-1.0) and its pinned
[encounter/Aurora](https://github.com/encounter/aurora) at
`7f2801cd0133c9333eadb4e2e6b24100c328d328` (MIT, Luke Street).
Serial normalization in `tools/three_ds/input/three_ds_input.cpp` is adapted
from Aurora's `lib/input.cpp`; the MIT notice is retained in
`LICENSES/Aurora-MIT.txt`. TriAevum keeps its own SDL lifecycle, 3DS mappings
and consolidated ImGui controls rather than importing Aurora's PAD frontend
or Dusklight's game-specific controls. See
`docs/TRIAEVUM_DUSKLIGHT_CONTROL_ALIGNMENT.md` for scope and verification.

## libultraship

`runtime/three_ds_recomp` descends from
[Kenix3/libultraship](https://github.com/Kenix3/libultraship), retained as the
`three_ds_recomp_runtime` submodule. libultraship is licensed under the MIT
License, copyright 2022 kenix3. Its upstream license must be included in source
and binary distributions that contain this component.

## Azahar

Selected audio implementation and PICA shader-decompiler behavior comes from
or was adapted against [Azahar Emulator](https://github.com/azahar-emu/azahar).

- `tools/oot3d/third_party/azahar_pica` imports the GLSL shader decompiler from
  Azahar revision `beb5681ee7f85586501b16b083a961b707092cd7`. Those files are
  GPL-2.0-or-later and retain their headers.
- The same directory imports nihstro definitions under the BSD 3-Clause
  License; its text is in `NIHSTRO_LICENSE.txt`.
- `tools/ctr_services/y2r_service.h` adapts the Y2R fixed-point conversion,
  rotation, tiled output and service contracts from Azahar's
  `src/core/hw/y2r.cpp` and `src/core/hle/service/cam/y2r_u.cpp` at revision
  `beb5681ee7f85586501b16b083a961b707092cd7`, under GPL-2.0-or-later.
  The portable service has its own memory/IPC boundary and regression tests;
  source-file attribution is retained.
- `tools/oot3d/third_party/azahar_audio` contains an adapted audio subset under
  GPL-2.0-or-later. The applicable text is in its `LICENSE.txt`. It entered this
  repository in commit `c3313fb20796522f43c177fbd071b069384bc2aa` from Azahar
  revision `beb5681ee7f85586501b16b083a961b707092cd7`. Fifteen imported source
  files have identical Git blob identities at that revision; the remaining
  source and compatibility-shim differences are recorded in
  `docs/AZAHAR_AUDIO_PROVENANCE.md`.
- `tools/triaevum_release/ctr_rom.py` adapts the bounds and backwards-LZSS
  semantics of Azahar's NCSD/NCCH loader at the same revision for Forge's
  decrypted cartridge-image importer. It is GPL-2.0-or-later and retains an
  explicit source-file notice.
- `tools/three_ds/input/three_ds_input.cpp`, `three_ds_sdl_controller.h` and
  `runtime/three_ds_recomp/include/ship/controller/physicaldevice/SDLControllerSetup.h`
  adapt the SDL motion coordinate/unit mapping, normalized touchpad input and
  extended PlayStation report setup from `src/input_common/sdl/sdl_impl.cpp`
  at Azahar revision `c2237de04d8c08cb5ad0ba3fb98e5a9640203257`
  (Citra/Azahar contributors, GPL-2.0-or-later). Device selection and event
  ownership remain in TriAevum's shared input layer; no donor event thread
  or renderer is imported.

Azahar's license text and source availability requirements apply to combined
distributions that consume these files.

The read-only transferable shader-cache importer follows Citra's legacy
`gl_shader_disk_cache` format and Azahar's successor (GPL-2.0-or-later,
including yuzu donor notices). It preserves the two explicit stage-enum
dialects instead of interpreting driver binaries. The imported shaders are
regenerated by TriAevum's PICA frontend; user cache payloads are not included
in this repository. See [shader preparation](docs/TRIAEVUM_FORGE_SHADER_PREPARATION.md).

The Android port also imports Azahar/Citra's actual touchscreen overlay in
`ports/android/controls` under GPL-2.0-or-later: four Kotlin drawing/input classes,
overlay preferences, button identifiers and their original artwork/layouts.
`donor_manifest.json` records the snapshot and per-file hashes; `license.txt`
retains the donor license. TriAevum changes the host boundary and namespace,
not the controller artwork or gesture algorithms. The isolated input adapter
reproduces Azahar JNI's axis normalization. See the module README for tested
scope and the remaining in-game integration; this is not an Android release.

## SDL2 and controller mappings

The Windows runtime links SDL2 statically for window, audio and controller
services; no separate `SDL2.dll` is required by this build. SDL2 2.32.10 is
licensed under zlib; its notice is in `LICENSES/SDL2-zlib.txt`.

`runtime/triaevum_resources/gamecontrollerdb.txt` is an unmodified copy from
[SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) at commit
`28a856f2b92da8891b161acd0abd64fbf4445d97`, SHA-256
`f6cb9252c3c3790c3513c7d279e5b4b37b0d75d739f6a573f3a55471914fbde6`.
The release ships it as `resources/gamecontrollerdb.txt` together with its
zlib notice in `LICENSES/SDL-GameControllerDB-zlib.txt`. These community
mappings supplement SDL's built-in controller database.

## Zstandard

The runtime links [Zstandard](https://github.com/facebook/zstd) 1.5.6 (release
archive SHA-256
`8c29e06cf42aacc1eafc4077ae2ec6c6fcb96a626157e0593d5e82a34fd403c1`) statically
to compress save states. Zstandard is dual-licensed upstream under BSD and
GPL-2.0; TriAevum uses it under the BSD license, copyright Meta Platforms, Inc.
and affiliates. Its notice is in `LICENSES/zstd-BSD-3-Clause.txt`.

## NVIDIA NRI

The primary renderer uses
[NVIDIA Rendering Interface](https://github.com/NVIDIA-RTX/NRI), pinned by the
current integration to revision `4b485316463969f182db15e67aad2aec2f40a3d7`.
NRI is licensed under the MIT License, copyright 2021 NVIDIA Corporation.
TriAevum must ship that notice with runtime binaries that contain NRI.

NRI is distinct from proprietary NGX/DLSS SDK components. The runtime is built
with NRI's NGX support disabled; NGX is only in the DLSS module below.

## NVIDIA DLSS (separately licensed DLSS module)

Upscaling by NVIDIA DLSS. NVIDIA and DLSS are trademarks and/or registered
trademarks of NVIDIA Corporation. TriAevum is not sponsored or endorsed by
NVIDIA.

The DLSS module (`triaevum_dlss.dll`, `libtriaevum_dlss.so`; source in
`runtime/three_ds_recomp/src/fast/dlss/`) links the NGX SDK library from
NVIDIA's public [DLSS SDK](https://github.com/NVIDIA/DLSS) and is distributed
beside NVIDIA's DLSS runtime (`nvngx_dlss.dll`,
`libnvidia-ngx-dlss.so.<version>`), both unmodified and from the same SDK
release. They are NVIDIA's property and are distributed under the NVIDIA RTX
SDKs licence reproduced in `LICENSES/NVIDIA-DLSS-SDK-LICENSE.txt`, not under
TriAevum's GPL; the module's own integration code is MIT. See the module's
`LICENSE.md` and `LICENSE_SCOPE.md`. Without the module, DLSS is not offered.

## AMD FidelityFX SDK (FSR runtime)

On Windows, FSR runs through AMD's signed FidelityFX runtime
`amd_fidelityfx_vk.dll` from the [FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK)
(`PrebuiltSignedDLL/`), which NRI loads by name. The FidelityFX SDK is
licensed under the MIT License, copyright Advanced Micro Devices, Inc.; its
notice is in `LICENSES/FidelityFX-SDK-MIT.txt`.

## AMD FidelityFX CACAO

The optional original CACAO provider integrates
[AMD FidelityFX CACAO](https://github.com/GPUOpen-Effects/FidelityFX-CACAO)
at revision `0ddca95e6714727a252ead345591ca8f2598f261`.
Its upstream MIT license is reproduced below and must accompany binaries
containing this provider.

Copyright (c) 2020-2021 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

## Intel XeGTAO

The multiscale GTAO shader in
`runtime/three_ds_recomp/src/fast/renderer/gtao_shader.cpp` adapts the
normal-relative radius falloff from [Intel XeGTAO](https://github.com/GameTechDev/XeGTAO).
This is not an unmodified integration of the complete XeGTAO implementation.
The upstream [MIT license](https://github.com/GameTechDev/XeGTAO/blob/master/LICENSE)
is retained in source and reproduced here for binary distributions.

Copyright (C) 2016-2021, Intel Corporation

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Godot Engine screen-space contact shadows

The optional contact shadows of the directional shadow light,
`runtime/three_ds_recomp/src/fast/oot3d/contact_shadow_shader.cpp` and
`runtime/three_ds_recomp/src/fast/oot3d/contact_shadows.cpp`, port
`servers/rendering/renderer_rd/shaders/effects/screen_space_contact_shadows.glsl`
and `SSEffects::screen_space_contact_shadows` from
[Godot Engine](https://github.com/godotengine/godot) 4.8 (pull request 118045).
Godot's implementation follows the screen-space shadows technique Bend Studio
presented for Days Gone; no Bend Studio code is included here. The changes (the
depth and sampler bindings, the camera depth of the shadow casters, the
far-plane write) are listed in the files. The upstream
[MIT license](https://github.com/godotengine/godot/blob/master/LICENSE.txt) is
retained in source, in `LICENSES/Godot-MIT.txt` and here for binary distributions.

Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md).
Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## iMMERSE (Pascal Gilcher)

The optional MXAO ambient-occlusion provider is the iMMERSE module,
`runtime/three_ds_recomp/src/fast/immerse/`. It is ported from
`Shaders/MartysMods_MXAO.fx` of [iMMERSE](https://github.com/martymcmodding/iMMERSE)
at revision `f57d3afa1ebe5d1fd6152d4f6fb9a2e75bd1d1cb`, by Pascal Gilcher
([Marty's Mods](https://martysmods.com)). Its changes are listed in the headers of
its files and in its `README.md`.

iMMERSE is not under an open-source license: "Copyright (c) Pascal Gilcher. All
rights reserved." Its original license is reproduced unchanged in the module as
`LICENSE-iMMERSE.txt` and remains the license of the module, including the changes
that integrate it. Its terms forbid public redistribution, public modified versions,
use of its parts in another project and monetization unless the author gives
explicit permission, and require clear credit of the project, its author and the
parts used.

All rights remain with the author. TriAevum integrates and modifies MXAO under the
author's explicit permission, granted exclusively to the TriAevum project; the
maintainer reports it for MXAO, LAUNCHPAD and SOLARIS (2026-09-30), and only MXAO
is integrated. The permission does not extend to recipients, forks or other
projects. The module's `LICENSE.md` records this exception and its boundary to
TriAevum's GPL code, and `LICENSE_SCOPE.md` excludes the module from the GPL grant.

This material is not an original TriAevum contribution. It is compiled only with
`THREE_DS_RECOMP_ENABLE_IMMERSE`, into its own shared library
(`triaevum_immerse.dll`, `libtriaevum_immerse.so`) that contains nothing of
TriAevum and is loaded by the runtime through the MIT module interface
`runtime/three_ds_recomp/module_interface/triaevum/immerse_module.h`; without
the library TriAevum runs without MXAO. Binary packages that include the
library ship it beside the runtime as a separately licensed component, with
the module's `LICENSE.md` and the original license under `LICENSES/`.

The permission, as the maintainer reports it on 2026-10-12, expressly allows
the distribution and modification of MXAO for the exclusive use of this
repository of the TriAevum project, on the condition that all rights in it,
and the license for any further use, remain the author's
(`docs/TRIAEVUM_RELEASE_APPROVAL_RECORD.md`).

## TopScreen single-screen modifications

The required title-owned UI contract is published in
`tools/oot3d/ui_contract`, imported from Zelda3drecomp revision
`2cec5ef08305fbe1e4fe00efaf3f2467ff5228b1` with the existing TriAevum
`HorseStamina` extension. Its source manifest records the 62 imported files and
their provenance. It includes reconstructed UI semantics and resource-layout
tables, not UI textures. It must not be described as title-neutral code; the
original-game rights exclusions in `LICENSE_SCOPE.md` still apply.

TriAevum contains an independently maintained typed reimplementation of
behavior observed in the TopScreen modifications for OoT3D (2.1.1, then
SingleScreen 3.0). Their IPS patches and injected ARM payloads are not
redistributed and never applied.

SingleScreen 3.0 is integrated into TriAevum with its author's permission: the
package ships its low-resolution UI atlas `custom_ui01.ctxb` and its Link
animations `zelda_link_ext.zar`, unchanged, from the official archive
`singlescreen_30_25aeb.zip` (SHA-256
`5668640fec8dc2b47b4516fd0b871c4d7d90ca1c2203acc2a22adb59aaab5993`;
SingleScreen 3.1's `singlescreen_31_989c3.zip` carries them unchanged), and
SingleScreen 3.1's HD Latin font `ltn16.qbf`, unchanged. They remain the
author's; see `LICENSES/SingleScreen-3.0.md`. The HD UI atlas (by chubbycubby)
is not distributed; Forge takes it from chubbycubby's HD UI texture pack when
the user obtains it (see below). TopScreen 2.1.1 is no longer used.

The reverse-engineering method and implementation boundaries are documented in
`docs/OOT3D_TOPSCREEN_MOD_SEMANTIC_PORT.md` and
`docs/OOT3D_TOPSCREEN_2_1_1_PORT.md`. Credit is due to the original TopScreen
mod author **M-1**, credited for reverse engineering, programming and testing on
the [official mod page](https://gamebanana.com/mods/695893). That page also credits
RobertoNessy for free-camera work, HenrikoMagnifico for HD textures, and Nintendo
and Grezzo for the original game. The optional 4K texture pack is not part of
the base native-texture import. These credits are not a redistribution license.

Public source used only as corroboration includes:

- [tristangnl/OoT3D_Standalone_Free_Cam_CPP](https://github.com/tristangnl/OoT3D_Standalone_Free_Cam_CPP),
  at commit `fb752e64` as recorded in the semantic-port document;
- [Roberto-Nessy/OoT3D_Standalone_Free_Cam](https://github.com/Roberto-Nessy/OoT3D_Standalone_Free_Cam),
  credited by the former as the original standalone free-camera work;
- [OTPR26/OOT3DHud](https://github.com/OTPR26/OOT3DHud), also known as Ocarina
  Reframed, whose own GPL-3.0-or-later and inherited notices apply to any code
  actually copied from it.

These references do not grant permission to redistribute the original mod
archive. Project-authored reverse engineering and typed source changes remain
traceable through this repository's Git history.

## mm3d-dekui and Project Restoration (Majora's Mask 3D)

Moonfall Curse integrates the single-screen mode of
[tgacquin/mm3d-dekui](https://github.com/tgacquin/mm3d-dekui) (release
v1.0.0-beta.1, source archive SHA-256 `E39E374B…3E6281`), whose `src/mm3d`
files are GPL-2.0-or-later. Code ported from it lives in `tools/mm3d/dekui/`
with its SPDX notice and a note of the dekui file it derives from; addresses
are translated to the vanilla v1.1 code.bin and the hooks are expressed as
typed guest patch sites (`docs/MOONFALL_CURSE.md`). dekui builds on
[leoetlino/project-restoration](https://github.com/leoetlino/project-restoration)
(GPL-2.0, commit `8bb2a7b8`), credited to leoetlino and contributors; code
ported from it lives in `tools/mm3d/restoration/` with its SPDX notice, a
note of the Restoration file it derives from and the GPL text (`COPYING`).
Neither project's binary
patches (`code.bps`, `exheader.bin`), RomFS layouts, text payloads or
settings files are redistributed, and none is applied: Nintendo and Grezzo
material inside them stays under the exclusions of `LICENSE_SCOPE.md`.
## Vanilla+ - Link Remodel And HD Item Models (optional mod)

Forge's "Optional mods" section can apply **Vanilla+ - Link Remodel And HD
Item Models** 1.9 by **HugAMuffin** (3D art), tested by **Fado** (QA lead),
**M-1** and **seiunsky_1084** (hardware testing), made with **Jameriquiah**'s
CMB-Exporter (<https://gamebanana.com/mods/705890>). It is licensed CC BY-NC-ND
4.0 (<https://creativecommons.org/licenses/by-nc-nd/4.0/>) and is not part of
TriAevum: packages contain none of its files. Forge downloads the author's
original archive on the user's request, verifies it and applies it to the
user's local game data only. See `LICENSES/Vanilla-Plus-Link-Remodel.md` and
`docs/TRIAEVUM_MOD_VANILLA_PLUS_LINK.md`.

## OoT3D Vanilla UI/HUD (optional HD UI texture pack)

Forge's "HD UI textures" section can install **OoT3D Vanilla UI/HUD** 3.2 by
**chubbycubby**, crediting **HenrikoMagnifico** and **M-1**
(<https://gamebanana.com/mods/701929>), HD textures for the HUD, the menus and
TopScreen in each game language. It is licensed CC BY-NC-ND 4.0
(<https://creativecommons.org/licenses/by-nc-nd/4.0/>) and is not part of
TriAevum: packages contain none of its files. Forge downloads the author's
original archive on the user's request, verifies it and keeps its textures in
the user's private data folder only. See `LICENSES/OoT3D-Vanilla-UI-HUD.md` and
`docs/TRIAEVUM_UI_TEXTURE_PACK.md`.

## Ocarina Reframed (TopScreen file selection design)

TopScreen's file selection (Link's pedestal on the right, the file interface on
the left, Back in the corner) follows the single-screen presentation of
**Ocarina Reframed** 2.0.2 by **OTPR26** (<https://github.com/OTPR26/OOT3DHud>,
GPL-3.0-or-later). TriAevum reimplements that behavior as its own typed host
code (`tools/oot3d/native_game_runtime/oot3d_top_screen_reframed_file_select.*`);
it contains no Ocarina Reframed source, patch or artwork.

## Ocarina ReTuned (portability fixes)

**Ocarina ReTuned** by **OTPR26** (<https://github.com/OTPR26/Ocarina-ReTuned>)
is a fork of TriAevum that builds the runtime for macOS, Android, iOS and tvOS
and with a non-MSVC Windows toolchain. TriAevum adapted a set of its generic
portability fixes back into the shared runtime in October 2026: standard-library
compatibility for libc++, Windows GNU-driver build conditions, MoltenVK
portability enumeration, the macOS title-module loader, Android fullscreen
presentation, the Vulkan exclude-scissor partition, the deferred vertex-format
rejection, three VFP encodings, and the QueryMemory, OutputDebugString and
ir:rst kernel services. As modifications of a GPL-3.0 program they are covered
by this repository's GPL-3.0-or-later license; files that carry OTPR26's own
SPDX tag are GPL-2.0-or-later. The adapted code is marked in place and listed in
`docs/TRIAEVUM_OCARINA_RETUNED_INTEGRATION.md`. No ReTuned interface, font,
artwork, ROM-preparation or Reframed-profile code was taken.

## Fira Sans Condensed (in-game menu font)

The in-game settings menu (Esc / R3) is drawn by TriAevum's own UI renderer
from glyph atlases baked from
[Fira Sans Condensed](https://github.com/mozilla/Fira) Regular and Bold,
copyright (c) 2012-2015 The Mozilla Foundation and Telefonica S.A., under the
SIL Open Font License, Version 1.1. The font files, the license and the atlas
generator are in `tools/oot3d/ui_menu/font`; the baked atlases
(`tools/oot3d/ui_menu/oot3d_menu_font_atlas_data.cpp`) are compiled into the
runtime, so the license is also kept at `LICENSES/FiraSans-OFL.txt` for binary
distributions. The reserved font name "Fira Sans" is not used for any derived
font. The menu's layout follows Dusklight's settings menu (CC0-1.0, see the
controller section above) as a design reference; no Dusklight code or assets
are included.

## Forge and runtime binary dependencies

Windows `TriAevumForge.exe` embeds CPython 3.13; the initial Linux Forge build
embeds CPython 3.12. Both include Capstone 5.0.7 and the PyInstaller
6.16.0 bootloader. Their license texts, including PyInstaller's bootloader
exception, are distributed under `LICENSES/`. PyInstaller is a packaging tool;
its presence does not change the license selected for TriAevum source.

Developer-only Forge build commands can invoke LLVM 22 tools `clang-cl.exe`,
`llvm-lib.exe` and `lld-link.exe`. The current precompiled player release does
not ship or invoke a compiler. LLVM is licensed under Apache-2.0 WITH
LLVM-exception; the Apache 2.0 text and LLVM exception are distributed under
`LICENSES/`. These tools, their compiler-rt builtins, the whole-AOT support
archive and the public `forge/oot3d_game_module.dll` are title-neutral and
contain no game code or content.

Portable Forge builds include [certifi](https://github.com/certifi/python-certifi)
2026.7.22 and its Mozilla root certificate collection to verify HTTPS downloads
independently of the build host's OpenSSL CA paths. Certifi is MPL-2.0 licensed;
its distribution metadata and license are included in the frozen bundle.
Certificate and hostname verification remain enabled.

Forge embeds the minimal nlohmann/json forward-declaration headers needed by
the generated source interface. nlohmann/json is MIT licensed, copyright
2013-2025 Niels Lohmann; its notice is distributed as
`LICENSES/nlohmann-json-MIT.txt`.

The Windows runtime package includes `shaderc_shared.dll` from the pinned
Vulkan SDK used for the release build. shaderc is licensed under Apache-2.0;
the applicable license is distributed as `LICENSES/shaderc-Apache-2.0.txt`.
Its Visual C++ runtime dependencies (`msvcp140.dll`, `vcruntime140.dll`,
`vcruntime140_1.dll`) are deployed beside the executable from Visual Studio's
redistributable directory; see `LICENSES/Microsoft-Visual-Cpp-Runtime.md`.
The system Vulkan loader and display driver are machine prerequisites and are
not redistributed by TriAevum.

## NXVK Switch compatibility adapter

`ports/switch_vulkan_probe/nxvk_newlib_compat.cpp` is adapted from
[`PalindromicBreadLoaf/nxvk`](https://github.com/PalindromicBreadLoaf/nxvk),
file `switch/smoke/nvk_compat.c` at commit
`69ec283dbda64e65347a36274efb349122e85363`. Its GPL-2.0-or-later notice is
retained in source and the license is copied at
`ports/switch_vulkan_probe/COPYING`. The probe is isolated from current product
targets.

## Other dependencies

Dependencies obtained through CMake, vcpkg or platform SDKs retain their own
licenses. Release construction must generate a resolved dependency inventory
and include all required notices. A dependency being downloadable at build
time does not imply that its binaries may be redistributed.
