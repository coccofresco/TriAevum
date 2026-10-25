# Azahar audio provenance

## Identity

- Upstream: `https://github.com/azahar-emu/azahar.git`
- Upstream revision: `beb5681ee7f85586501b16b083a961b707092cd7`
- TriAevum import commit: `c3313fb20796522f43c177fbd071b069384bc2aa`
- Upstream license for imported files: GPL-2.0-or-later

The local Azahar checkout used during development identifies that revision as
its HEAD immediately before the TriAevum import. Git object comparison between
the two repositories proves that 15 of the 19 imported `audio_core` and
`common` files are byte-identical blobs at the stated revisions.

## Adapted files

Four imported paths intentionally do not have identical blob IDs:

- `audio_core/hle/source.cpp` and `source.h` replace Azahar's concrete
  `MemorySystem` dependency with a bounded physical-memory resolver callback;
- `common/assert.h` and `common/logging/log.h` are reduced compatibility shims
  for the isolated audio build.

The five tiny `boost/serialization` headers are local include shims and are not
represented as copied Azahar implementation. Later TriAevum commits add
save-state validation and runtime integration; Git history preserves those
changes separately from the donor baseline.

Beta 1.3 ports a bit-exact mixing optimization from the TriAevum Switch fork
(GustavoHenriqueRocha/TriAevum-Switch `8ee3f632`, GPL like the donor files). It
modifies `audio_core/codec.{h,cpp}`, `interpolate.cpp`, `sample_queue.h`,
`hle/mixers.{h,cpp}` and `hle/source.cpp`: reused decoded-sample storage,
memoized ADPCM loop decoding, clamp-free ADPCM frames with a fallback, a
branch-free None/Linear interpolation loop (NEON on AArch64) and skipped
zero-gain mixes. `oot3d_native_audio_dsp_mix_tests` checks that the whole mix,
including savestates, digests identically to the code before the change.

## Reproduction

With the Azahar repository available locally, compare the trees with:

```text
git -C <azahar> ls-tree -r beb5681e src/audio_core src/common
git ls-tree -r c3313fb20 tools/oot3d/third_party/azahar_audio
```

Matching Git blob IDs establish exact file identity independently of checkout
line endings. The four adapted paths must instead be reviewed as diffs against
the stated upstream revision.
