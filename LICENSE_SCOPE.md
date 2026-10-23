# License scope

TriAevum is a mixed-provenance work. This file describes scope; it is not a
license for Nintendo software, third-party mods or any other material whose
copyright is owned by somebody else.

## Repository code

Original TriAevum contributions are licensed under the GNU General Public
License, version 3 or (at your option) any later version
(`GPL-3.0-or-later`). The complete text is in `LICENSE`.

Existing component licenses and source-file notices remain in force. Adding a
root license does not relicense donor code. Executable product paths combine
the original contributions with GPL-2.0-or-later Azahar-derived code; that
code's "or later" option permits the combined product to be conveyed under
GPL-3.0-or-later. Permissively licensed components retain their MIT or BSD
notices and permissions.

Attribution is preserved through copyright headers, `THIRD_PARTY_NOTICES.md`
and the component license texts. These notices must remain with source and
binary distributions as required by their applicable licenses. This is not a
separate restriction on the freedoms granted by the GPL.

## Excluded material

No permission is granted here for:

- Nintendo ROMs, executable code, assets, firmware, keys, trademarks or other
  proprietary material;
- generated or translated title modules and decompiled title source;
- patches, textures or payloads from the TopScreen mod or another mod unless
  their respective author has granted redistribution permission;
- the iMMERSE module, `runtime/three_ds_recomp/src/fast/immerse/` (MXAO by
  Pascal Gilcher); see [the exception below](#exception-the-immerse-module);
- the DLSS module, `runtime/three_ds_recomp/src/fast/dlss/`, and NVIDIA's DLSS
  SDK components it is built and distributed with; see
  [the exception below](#exception-the-dlss-module);
- proprietary SDKs, redistributable libraries or model files whose own terms
  do not permit inclusion.

The adopted precompiled release model explicitly permits catalogued translated
title modules and their corresponding translated source in the package. This
does not grant original-game rights or relicense them under TriAevum's GPL.
ROMs, extracted assets, keys, unapproved mod payloads and SDKs remain excluded.
Local ROM-derived Forge output is private user data, not runtime-licensed data.

## Exception: the iMMERSE module

The directory `runtime/three_ds_recomp/src/fast/immerse/` is excluded from the
GPL grant above. It ports MXAO from Pascal Gilcher's iMMERSE, and:

- all rights in it remain with its original author, Pascal Gilcher;
- it stays under the original iMMERSE licence, reproduced unchanged in that
  directory as `LICENSE-iMMERSE.txt`, including the changes that integrate it
  into TriAevum;
- TriAevum integrates it under the author's explicit permission, granted
  exclusively to the TriAevum project. The permission does not pass to
  recipients, forks or other projects.

The directory's `LICENSE.md` states these terms and the boundary. The module
is built as a separate shared library (`triaevum_immerse.dll`,
`libtriaevum_immerse.so`) that contains only the module and links nothing of
TriAevum; packages ship it beside the runtime as a separately licensed
component. TriAevum's GPL code reaches it only through the module interface
`runtime/three_ds_recomp/module_interface/triaevum/immerse_module.h`, which the
maintainer licenses under MIT so that the module may implement it. The GPL
runtime is complete without the library: building with
`THREE_DS_RECOMP_ENABLE_IMMERSE=OFF`, or leaving the library out, gives every
feature except MXAO. Anyone else distributing TriAevum must do so and omit the
directory, unless the author has given them permission.

## Exception: the DLSS module

The directory `runtime/three_ds_recomp/src/fast/dlss/` is excluded from the
GPL grant above. It is the only part of TriAevum that links NVIDIA's NGX SDK,
and its `LICENSE.md` states the terms and the boundary:

- TriAevum's integration code in it is licensed under the MIT License;
- NVIDIA's NGX SDK library linked into it, and NVIDIA's DLSS runtime
  (`nvngx_dlss.dll`, `libnvidia-ngx-dlss.so.<version>`) distributed beside it,
  remain NVIDIA's, under the NVIDIA RTX SDKs licence that packages reproduce
  as `LICENSES/NVIDIA-DLSS-SDK-LICENSE.txt`. Neither this file nor the GPL
  grants any right in them.

The module is a separate shared library (`triaevum_dlss.dll`,
`libtriaevum_dlss.so`) that links nothing of TriAevum. The GPL runtime links
nothing of NGX and reaches the module only through
`runtime/three_ds_recomp/module_interface/triaevum/dlss_module.h`, which the
maintainer licenses under MIT so that the module may implement it. The runtime
is complete without the module: building with
`THREE_DS_RECOMP_ENABLE_DLSS_MODULE=OFF`, or leaving the module or NVIDIA's
runtime out, gives every feature except DLSS. The release audit rejects any
other packaged binary that contains NGX code.

## Release review

The technical separation and automated audit reduce accidental inclusion; they
do not determine whether a particular user's extraction, translation or use is
lawful. The maintainer's legal-review approval is recorded in
`docs/TRIAEVUM_RELEASE_APPROVAL_RECORD.md`. Every remaining technical and
compliance gate in `tools/triaevum_release/release_readiness.json` must still be
complete before public packaging is enabled.
