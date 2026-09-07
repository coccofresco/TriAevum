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
- proprietary SDKs, redistributable libraries or model files whose own terms
  do not permit inclusion.

The adopted precompiled release model explicitly permits catalogued translated
title modules and their corresponding translated source in the package. This
does not grant original-game rights or relicense them under TriAevum's GPL.
ROMs, extracted assets, keys, unapproved mod payloads and SDKs remain excluded.
Local ROM-derived Forge output is private user data, not runtime-licensed data.

## Release review

The technical separation and automated audit reduce accidental inclusion; they
do not determine whether a particular user's extraction, translation or use is
lawful. The maintainer's legal-review approval is recorded in
`docs/TRIAEVUM_RELEASE_APPROVAL_RECORD.md`. Every remaining technical and
compliance gate in `tools/triaevum_release/release_readiness.json` must still be
complete before public packaging is enabled.
