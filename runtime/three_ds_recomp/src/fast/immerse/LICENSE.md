# iMMERSE module: licence exception

This directory, `runtime/three_ds_recomp/src/fast/immerse/`, is the iMMERSE
module of TriAevum. It is an explicit exception to TriAevum's licensing: the
GNU General Public License that covers TriAevum's own code (`LICENSE`,
`LICENSE_SCOPE.md`) does not apply to anything in this directory.

## Origin

The module ports MXAO from `Shaders/MartysMods_MXAO.fx` of
[iMMERSE](https://github.com/martymcmodding/iMMERSE), revision
`f57d3afa1ebe5d1fd6152d4f6fb9a2e75bd1d1cb`, by Pascal Gilcher
([Marty's Mods](https://martysmods.com)).

Copyright (c) Pascal Gilcher. All rights reserved.

## Terms

- All rights in MXAO and in this module remain with its original author,
  Pascal Gilcher.
- The module stays under the original iMMERSE licence, reproduced unchanged in
  `LICENSE-iMMERSE.txt`. Nothing in TriAevum relicenses it. The changes made to
  integrate it into TriAevum are part of the module, under the same terms, and
  grant no further right.
- TriAevum integrates and modifies MXAO under the explicit permission of its
  author, granted exclusively to the TriAevum project. The permission is not
  transferable and extends to no one else: receiving TriAevum, its source, or a
  copy or fork of either gives no right to use, modify or redistribute this
  module. Anyone else needs the author's own permission.
- Credit: iMMERSE MXAO by Pascal Gilcher (martysmods.com). F1 Lighting >
  Ambient Occlusion closes with it, the reserved rights and this exception in
  brief, and `THIRD_PARTY_NOTICES.md` records it for source and binary
  distributions.
- The permission, as the maintainer reports it (2026-10-12): the author
  expressly permits the distribution and modification of MXAO for the
  exclusive use of this repository of the TriAevum project, on the condition
  that all rights in it, and the licence for any further use, remain his
  (`docs/TRIAEVUM_RELEASE_APPROVAL_RECORD.md`).

## Boundary

Everything in this directory, and nothing outside it, is under these terms.

| File | Contents |
| --- | --- |
| `mxao_shader.{h,cpp}` | The ported MXAO programs (GLSL) and their constants. |
| `mxao_module.cpp` | The module library: MXAO's NRI execution (resources, descriptor sets, dispatches, the original's parameter mapping) and the texts of its F1 controls (the original's options, presets and descriptions). |
| `mxao_pass.spvpack`, `mxao_pass_binaries.{h,cpp}` | The precompiled programs and their embedding. |
| `immerse.cmake` | Builds the module library `triaevum_immerse`. |
| `tests/` | GPU tests and `build_immerse_pass_artifacts`, the generator of `mxao_pass.spvpack`. |
| `README.md` | How the port works and how it was validated. |
| `LICENSE.md`, `LICENSE-iMMERSE.txt` | This exception and the original licence. |

The module is a separate shared library, `triaevum_immerse.dll` on Windows and
`libtriaevum_immerse.so` on Linux, installed beside the runtime. It is built
from this directory alone: its include path holds only this directory, the
module interface `module_interface/triaevum/immerse_module.h`, and the NRI and
Vulkan headers, and it links nothing of TriAevum. The interface is a small C
table of plain values, Vulkan handles and opaque NRI objects; TriAevum's
maintainer licenses it under MIT so that a module under another licence can
implement it.

TriAevum's GPL code reaches the module only through that interface, loaded by
`fast/oot3d/immerse_module_loader.cpp`. Its side of the boundary names MXAO
only to select and serve it: `fast/oot3d/mxao_pass.cpp` hands the module the
renderer's NRI device, textures, programs and barriers;
`fast/oot3d/mxao_settings_panel.cpp` draws generic F1 controls labelled with
the module's texts; the provider choice `AmbientOcclusionMode::Mxao` has its
settings, persistence and capability; the effect graph wiring is in the
Vulkan backend. The module's programs include two GLSL libraries the host
passes it (the geometry guide's decoding and the debug views' normal
encoding).

`THREE_DS_RECOMP_ENABLE_IMMERSE`, on by default with NRI, builds the library.
Without it, or when the library is not installed, TriAevum runs without MXAO:
the provider stays unselectable, a configuration that selects it renders AO
Off and keeps the preference, and every other feature builds, runs and is
tested. The directory can then be removed. A fork or redistribution without
the author's permission must be built that way, or ship without the library,
and must not include this directory.

Binary packages that include the library ship it as a separately licensed
component beside the runtime, with this file as `LICENSES/iMMERSE-MXAO.md` and
the original licence as `LICENSES/iMMERSE-LICENSE.txt`.
