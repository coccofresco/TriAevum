# MobiClip decoder qualification

## Scope

Developer-only qualification for the remaining Sheikah Stone movie path in
issue #39. No change to production playback, UI ownership or the renderer.
The external decomp repository is read-only. All source snapshots, packets and
decoded frames belong in a private diagnostic directory, never a release.

## Reproduction

Run `tools/oot3d/codec_validation/validate_mobiclip.py` with:

```text
--decomp-root <external checkout>
--ffmpeg-dir <directory containing modern ffmpeg and ffprobe>
--cc <GCC executable>
--output <new private directory outside the checkout>
--frames 64
<user-extracted hint000.moflex> <user-extracted hint006.moflex>
```

Use `--repair-planar` to apply the diagnosed correction to the copied source
only. The patch requires an exact unique expression; it does not silently
overwrite newer upstream work. Reports include original/compiled source hashes,
decomp commit, compiler, probe hash, movie hashes and bytewise YUV comparisons.
For a full movie, choose a frame limit greater than its packet count.

The packet-copy command needs `-copyinkf`: the first packet is not marked as a
keyframe. Omitting it skips data and invalidates packet boundaries. Do not use
the legacy 2013 FFmpeg executable found in the local Python Scripts directory.

Harness tests:

```text
python -m unittest discover -s tools/oot3d/codec_validation -v
```

They reject empty, truncated, unequal-length and short-success results.

## Verified defect

External snapshot `e0b6c5f7`, `src/middleware/mobiclip.c`,
`Oot3dMobiclip_PredictPlanar`:

```c
/* Original */
const s32 shift = size == 16 ? 3 : 2;
/* Corrected on the diagnostic copy */
const s32 shift = Oot3dMobiclip_AdjustPlanar((s32)size, size) == 8 ? 3 : 2;
```

Both 16x16 and 8x8 planar prediction require shift 3; 4x4 requires 2.
The original expression incorrectly uses 2 for 8x8. This is a general block
prediction rule, not a movie-specific correction.

Reference: [FFmpeg n8.0.1 MobiClip decoder](https://raw.githubusercontent.com/FFmpeg/FFmpeg/n8.0.1/libavcodec/mobiclip.c),
`adjust` and `predict_intra`. Inspect the actual downloaded tag source: the web
extraction returned a different 4x4 scan-table order. The direct tag download
agrees with the decomp scan table. A scan-table experiment was rejected; no such
change is included. Prediction reset also agrees with the reference's
`setup_qtables`, so it was not changed.

The external file already attributes Florian Nouwt, Adib Surani and Paul B Mahol
and the FFmpeg LGPL-2.1-or-later cross-check. Preserve attribution and perform the
normal release/source review before importing a production decoder.

## Results (2026-09-15)

Windows GCC, FFmpeg 8.0.1, 400x240 planar YUV420:

| Movie | Frames | Original differing bytes | Corrected differing bytes |
| --- | ---: | ---: | ---: |
| hint000 | 64 | 6,873,377 | 0 |
| hint006 | 64 | 6,878,285 | 0 |
| hint001 | 64 | not measured | 0 |

Original hint000 and hint006 first differ at frame index 2, despite successful
decoder return codes. Corrected output matches all 27,648,000 compared bytes.
Private baseline report: `%TEMP%/triaevum-mobiclip-real-002/report.json`.
The baseline snapshot was subsequently used for labelled experiments; its report
records the ORIGINAL hash. For an untouched corrected run use
`%TEMP%/triaevum-mobiclip-planar-003/report.json` and its snapshot/hash pair.

Full-movie follow-up: all **482 frames of hint000** also match byte-for-byte
(69,408,000 bytes, zero differences). Report:
`%TEMP%/triaevum-mobiclip-planar-full-004/report.json`.
This covers the entire packet stream, not only the opening frame sequence.

## Remaining integration boundary

1. Validate whole movies and malformed/truncated packet handling; preserve the
   bytewise reference test as a regression check.
2. Map a complete packet/frame entry, guest buffers, six reference frames and
   save/restore lifetime explicitly. Host C pointers are not guest ABI structs.
   Do not replace isolated non-AAPCS register-based internal fragments.
3. Verify Y2R output, native timing, typed Hint UI composition and return to game
   in the real runtime on Windows and Linux. Offline pixel parity is not issue
   closure or proof of working gameplay.

No private movie, decoded pixels, external decoder source or packet data is
tracked by this diagnostic tool. Do not propagate the patch into the external
checkout without an explicit synchronization request.
