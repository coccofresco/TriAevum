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

Azahar's license text and source availability requirements apply to combined
distributions that consume these files.

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

## NVIDIA NRI

The primary renderer uses
[NVIDIA Rendering Interface](https://github.com/NVIDIA-RTX/NRI), pinned by the
current integration to revision `4b485316463969f182db15e67aad2aec2f40a3d7`.
NRI is licensed under the MIT License, copyright 2021 NVIDIA Corporation.
TriAevum must ship that notice with runtime binaries that contain NRI.

NRI is distinct from proprietary NGX/DLSS SDK components. No proprietary SDK,
model or DLL is part of the clean public release baseline.

## TopScreen single-screen modifications

The required title-owned UI contract is published in
`tools/oot3d/ui_contract`, imported from Zelda3drecomp revision
`2cec5ef08305fbe1e4fe00efaf3f2467ff5228b1` with the existing TriAevum
`HorseStamina` extension. Its source manifest records the 62 imported files and
their provenance. It includes reconstructed UI semantics and resource-layout
tables, not UI textures. It must not be described as title-neutral code; the
original-game rights exclusions in `LICENSE_SCOPE.md` still apply.

TriAevum contains an independently maintained typed reimplementation of
behavior observed in the TopScreen modifications for OoT3D. The official 2.1.1
archive is identified by SHA-256
`e0c143c872ccf4ad72033caab768753b147a4033d260a701204a63ee6bc16df4`.
Its IPS, injected ARM payload and CTXB assets are not redistributed by TriAevum.
Forge imports the modified native textures locally from the official archive,
downloaded or supplied by the user, without applying its executable patches.

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
