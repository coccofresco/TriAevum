# MM3D lighting/material performance qualification

LIGHTPERF, 2026-11-07. Baseline `cde29681`, branch `mm3d/parity-lightperf`.

## Shared host-uniform fix

`gfx_vulkan_pica_draw.cpp` previously reserved the plain surface payload even
when uploading a balanced native-fragment payload:

| Payload | Bytes | Reservation at 256-byte alignment |
|---|---:|---:|
| Plain surface | 2,992 | 3,072 |
| Balanced surface | 3,152 | 3,328 |

Reservation and descriptor range now use the same size helper. The balanced
path also has a separate last-upload cache, equivalent to the existing plain
cache. It compares every host byte, never mapped GPU memory; resets on both
frame recycling and arena replacement; accepts offset zero; and does not
publish a failed upload. Shader layouts/equations, plain payloads and profiles
are unchanged. This fixes the renderer arena, not a title's guest light arena.

`pica_surface_uniform_upload_tests` passes 9,472 checks including exhaustive
single-byte invalidation, exact bytes, aligned arena boundaries, frame/arena
reset and throwing-upload recovery. `pica_fragment_lighting_memo_tests` now
optionally accepts a local binary corpus of consecutive 768-word register
snapshots, comparing cached and repeated results with fresh-thread decodes.
No ROM-derived data is included in the repository.

## Bounded qualification

Exactly one targeted game run and one OoT smoke/O2 gate, through the approved
safe runner with crash dialogs disabled. Private patched NRI and JOBS=4;
iMMERSE/DLSS modules present. No shader edit, so no pack regeneration or new O5
run. No unreleased branch integration.

- **MM3D night Clock Town**, Enhanced Original30, 300 frames: exit 0.
  Frame 270 exactly matches the existing DL night/on BMP reference:
  `dbe098f9e0bfb3f9d1a1185cc58a60a61fccd50c917681ea51a3dcfefbd84075`.
- The trace contains **106 balanced uploads and zero reuse hits** in one
  active scene execution. Baseline would also upload all 106 blocks. Thus the
  reuse path is enabled/tested but **no byte saving or speedup was demonstrated**.
  Per-draw water time/material/balance inputs remain exact; they were not
  quantized or discarded to obtain hits.
- For 90 active scene executions in presentation frames [120,300), nearest-rank
  counter p50/p95: VSM reused pages **594/675**, updated pages **16/16**;
  prepared-caster reuse **91/91**; point candidates **14/14**, rendered lights
  **4/4**, bound receiver draws **104/106**; transient configuration reuse
  **1/1**, new slots **0/0**. Alternating idle Original30 records are retained
  separately. These are attribution counters, **not throughput timings**.
- Offline noon/night/Inn corpora: **103/117/121** draw snapshots, **zero**
  differential decode mismatches. Adjacent equal decode keys **86/102**,
  **94/116**, **97/120** are opportunities, not observed runtime hits.
- **OoT Kokiri**, 400 frames: exit 0; all three frame 100/200/300 hashes match
  `I:/_triaevum_work/retuned/cap-p13.sha256`.
- Existing foundation **463**, shader-pipeline cache **71**, point shadow **7**
  tests pass, as do composition, MM native lighting, environment, appearance,
  fragment memo and private NRI patch suites.

## Limits and handoff

The full inventory is not a blanket MM3D performance certification. Missing
MM `TemporalIdentity` still safely disables fixed caster anchors; daytime
camera/model phase and the empty stationary catalog are not solved by enlarging
tolerances or copying OoT actor IDs. Interior-sun producers, complete room/water
catalogs, MM shader corpus and guest-light-arena facts remain prerequisites.
GRASS/GP integration and tuning belong to their owners. No mobile, VR or GI
qualification, long-transition/low-VRAM stress, or installed-material matrix
was performed.

Unreleased `gpu-fill`, `vsm-receiver` and `frame-redundancy` stay separate.
Any future record/GPU-producer key must preserve native fragment configuration,
LUT resource revisions, light ownership/replacement ledger, balance and point
assignment. Reach culling must not suppress native light without its replacement;
pose-once must preserve Gzw skinning and bounds. Owning-branch failures must be
resolved before adopting those paths.

Local reproducibility/evidence directory:
`I:/_triaevum_work/mm3d-port/analysis/parity/LIGHTPERF/`.
`RESULTS.md` maps all AUDIT O01–O30/G01–G12 items to evidence or explicit gaps;
`INVENTORY.md` records provenance and unreleased prerequisites; `PROGRESS.md`
records commits. Recipes, binary/config/input receipts, counter JSONL, tests,
image hashes and reduced previews are retained there. Raw captures/diagnostics
were reduced; no copyrighted corpus is committed.

## Wave 2 (partial, separate evidence)

See [the title-owned wave-2 note](../titles/mm3d/docs/LIGHTPERF_WAVE2.md).
Native-fragment guide selection and cheap writer attribution were added; legacy
packs and OoT O2 remain identical. The MM targeted attempt failed before loading
because of a stale OoT-only build selection and was not retried. G09 is partial;
G10/G13 remain open, with new offline EUR allocator/camera evidence. This does
not replace the successful first-wave results above or imply a wave-2 MM speedup.
