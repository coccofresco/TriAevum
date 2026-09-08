# Alpha 1c component provenance

This release updates Forge and the input recipes/catalog only. Runtime and
title binaries are unchanged from alpha 1b, preserving its controller fix,
graphics defaults, TopScreen, audio and existing save formats.

- Runtime build source: `7e810b37e52e8d538b0d6387f0384f4660556ae5`.
- Forge implementation: `01cf75c` (the alpha 1c source tag also includes release documentation).
- The package's existing corresponding-source archive remains paired with
  the unchanged runtime. Title build and translated sources remain unchanged.
- Complete updated Forge/project source is supplied as the additional release
  asset `TriAevum-v0.6.0-alpha.1c-source.zip` and at the alpha 1c Git tag.
- `release-manifest.json`'s `source_commit` describes the unchanged runtime
  build, not a claim that Forge was rebuilt from that older commit.

Source: https://github.com/coccofresco/TriAevum/releases/tag/v0.6.0-alpha.1c

Publisher qualification used exact audited inputs to generate the two content
families; those input files are private and not distributed. See
`TRIAEVUM_CONTENT_FAMILY_IMPORT.md` for the reproducible publisher command.
