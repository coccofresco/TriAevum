# TAS shader collection

The resulting corpus is now connected to private Windows Forge and its launch
profile: see [activation and measured reuse](TRIAEVUM_TAS_CORPUS_ACTIVATION.md).

**2026-09-12: complete corrected-movie acquisition accepted for shader evidence.**
All eight boss milestones and credits were inspected in native framebuffer
captures. This is route-level verification, not a frame-exact match against the
publication video. No public package or active Forge catalog was changed.

## Purpose and acceptance

Replay recorded native inputs to collect dynamic rendering states without human
gameplay. This supplements the location-injection corpus; it does not replace
it or establish coverage of all OOT3D scenes. Movie EOF alone is not validation:
require expected route milestones, no input-order desynchronization, and an
intact transferable cache after orderly emulator shutdown.

## Selected recording

- Author: **benstephens56**, [All Dungeons, TASVideos movie 6281](https://tasvideos.org/6281M).
- [Submission and synchronization requirements](https://tasvideos.org/9153S):
  USA initial revision, English, CPU 100%, HLE audio; Citra Nightly 1652-1815.
- Power-on recording, 1:02:23, 223,948 nominal display frames. The CTM contains
  **875,858 pad events**, 875,856 touch events, 389,195 accelerometer events and
  377,968 gyroscope events. Pad polls are not display frames.
- CTM SHA256: `e6df3721c407690dda89fe7903bf19fc6f23f60c1b4ff4d9781c3626c29616b8`.
- The route exploits wrong warps and skips content. Its eight-dungeon objective
  does not imply every dungeon room, cinematic, or final-boss state is visited.

## Synchronization decision

Modern instrumented Azahar was tested first. It reports mismatched pad/touch/
motion event ordering almost immediately; that acquisition was stopped and is
**rejected**, even though its input counter advanced. Do not silence the error,
resample the movie, or substitute visual completion for synchronization.

The compatibility fallback is **Citra Nightly 1796, b05b5b3**, within the author's
supported range. The Windows archive is preserved privately with SHA1
`728396c1b86f3229c9a0fb5bd8a389955f0a57af`; provenance:
[archived distribution](https://archive.org/details/3-ds-emulator-citra-nightly-1796).
The SDL frontend accepts `--movie-play`; its Qt frontend does not accept the
same movie argument. Use an isolated, initially empty portable `user` directory,
the user's USA initial-revision image, accurate multiplication, hardware shaders,
1x resolution, CPU 100%, English and HLE audio. The SDL frontend hardcodes
`SDL_GL_SetSwapInterval(1)` in `Present`, independently of its config. The second
acquisition disables that host wait with `wglSwapIntervalEXT(0)` on its current
presentation context. The emulated clock is not changed.

### Recording defect, not a timing workaround

The unmodified recording stopped at about 30% with `expected Touch, found Pad`.
An exhaustive structural check found exactly one out-of-range pad sample and
one malformed pair at **byte 5,313,081**. Both TASVideos submission and publication
downloads have the same SHA256 and defect. The pair is a one-byte left rotation
of the identical valid pairs on either side:

```text
observed: 00 00 6d 00 6d 00 01 00 00 00 00 00 00 00
repaired: 00 00 00 6d 00 6d 00 01 00 00 00 00 00 00
```

This produces invalid circle-pad values and mislabels Touch as another Pad.
The exact legacy HID source always calls Pad then Touch in the same callback.
`repair_ctm_repeated_pair.py` repairs only this class of one-byte rotation when
both surrounding complete pairs are identical and valid; ambiguous cases fail.
It writes a **new file** and an audit, never edits the original. This is an
evidence-supported reconstruction, not an author-verified replacement movie;
successful replay and expected route milestones still have to verify it.

Repaired copy SHA256:
`35f39c4962dc57ef9690b16777f8b92a9991262ef3617c5bdc9b38b8c22404db`.
Its header pad count is recalculated to **875,857**, matching 875,857 touch events.
No duration, subsequent record, gyro/accelerometer sample or valid pad value is
resampled. Five focused tests cover exact recovery and rejection of ambiguity.

The legacy video dumper failed/crashed during initialization. Collection uses
native transferable caching plus a private OpenGL framebuffer readback instead.
The readback runs at `SwapBuffers`, preserves GL read framebuffer, read buffer,
pixel-pack buffer/alignment/row/skip state. The first acquisition captured every
ten wall seconds; the repaired, host-uncapped acquisition samples every three.
These are native framebuffer samples, not Windows desktop screenshots and not
a frame-exact comparison video. The diagnostic DLL uses MinHook 1.3.4 (Tsuda
Kageyu, BSD-2-Clause; bundled disassembler notices retained privately).

## Reproducible local artifacts

Private workspace: `I:/TriAevum-private-tas/` (never release inputs).

- `all-dungeons.ctm`: unchanged author recording.
- `nightly-mingw/`: verified historical executable and isolated portable data.
- `run-legacy.py`, `observe.py`: bounded launcher, orderly owned-process close,
  desync detection, low-disk stop and read-only input-counter observations.
- `capture.c`, `build-capture.cmd`, `capture.dll`: framebuffer-only diagnostic.
- `citra-1796-capture/`: rejected incomplete original-movie acquisition, including
  `user-evidence/` and its native cache. The emulator shut down cleanly.
- `citra-1796-repaired/`: corrected-movie collection, progress, timestamped input
  observations, native PPM milestones, `result.json` and `qualification.json`.
  The latter preserves all evidence and explicitly records the acceptance scope.
- `all-dungeons-repaired.ctm`, `movie-repair.json`: derived recording and exact
  byte-level repair/provenance, separate from the author recording.
- `cache-tool/`, `build-cache.cmd`: small developer-only LLVM build of the
  **unchanged current** cache parser, PICA frontend and NRI shader generator.
- `prior-cache-current-inventory.json`: historical Citra evidence regenerated
  with that same frontend, so comparisons do not mix generator revisions.

The private counter adapter is deliberately executable-version-specific:
`Movie::s_instance` RVA `0x11366a0`, `current_input` offset `0x70`, confirmed from
b05b5b3 source and symbols. It only reads emulator host state. These addresses
must not enter a reusable renderer or be applied to a different executable.

Public helper `tools/oot3d/native_a32_runtime/collect_tas_shader_corpus.py`
validates CTM structure/counts and runs the instrumented **Azahar** path. It is
not a claim that this movie synchronizes on Azahar. Its six focused tests cover
device counts, malformed streams and incremental log reading.

Tool implementation checkpoint: **`8d93699`**. The separate Azahar diagnostic
branch `diagnostic/tas-shader-corpus-20260912` preserves its instrumentation at
**`00f9a770a`**. No game/runtime/AOT rebuild was needed. The private cache utility
build compiled only 16 steps from current frontend sources with two jobs.

## Completed run

- Native `MovieFinished` state **3**, **875,857** corrected pad events consumed;
  `Playback finished` logged at **629.753402 seconds**.
- **Zero input-order desynchronization errors**, orderly process exit **0**.
- **804.766 seconds** wall time including post-roll and shutdown, approximately
  13m25s. This is the successful acquisition, not total investigation time.
- **261 native framebuffer captures**. The original result's last input count
  becomes zero during shutdown; `native-eof.json` and `timeline.jsonl` preserve
  the completed counter before the emulator clears its Movie object.
- Final cache: **393 records**, comprising **392 fragment** and **1 vertex**;
  zero fragment decoding failures, **678** deduplicated fragment/NRI modules.
- Comparison against the actual retained **887-module** pack: **150 new modules
  (75 fragment + 75 NRI fragment)**. The union would contain **1,037** modules.
  Against the older Citra-only inventory, the increase is 200, not 150; do not
  interchange those baselines.
- Existing Forge shader compiler successfully produced `tas-additions.o3ps`:
  **152 modules**, comprising the 150 additions plus its two builtin scanout
  modules. These two are not newly discovered TAS shaders. The complete prior
  pack was neither replaced nor silently rebound in a user's installation.

Native frame references, all under `citra-1796-repaired/frame-NNNN.ppm`:

| Milestone | Samples reviewed |
| --- | --- |
| Gohma and blue warp | 0067-0069 |
| Barinade and aftermath | 0082-0085 |
| Phantom Ganon and warp | 0104, 0107-0109 |
| Morpha and warp | 0150, 0151, 0153 |
| Bongo Bongo and aftermath | 0155-0159 |
| King Dodongo | 0164, 0166-0169 |
| Twinrova and warp | 0185, 0189, 0191, 0195, 0197, 0200 |
| Volvagia and final Farore's Wind | 0203, 0204, 0206-0209 |
| Credits | 0211, 0212, 0214, 0215, 0219, 0221 |

`qualification.json` includes per-artifact and per-milestone SHA256, executable,
ROM and diagnostic-module identities, repair provenance and acceptance limits.
Final transferable cache SHA256:
`d09ada10d93a3f024892a4a707b037828ec92645f0af641d572524958a2ebe2d`.
Addon pack SHA256:
`623747597fa72d5642f256e3af6ad4c8b6d836653288161c12faaee9874df68d`.

Verification: **11 Python tests passed** (six collector, five repair); existing
C++ cache parser tests passed, including **3,100 truncated-input boundaries**.
The real replay and shader compilation are separate from those unit-test counts.

## Cache integration boundary

Preserve `user/shaders/opengl/transferable/0004000000033500.bin`, not driver
precompiled binaries. Use `oot3d_native_pica_cache_inventory --dialect
citra-legacy-v1`; regenerate canonical fragment/NRI source with the current
frontend, compare source IDs, then compile accepted sources through the existing
portable pack / Forge destination-GPU preparation path. Do not add a separate
runtime cache or reuse a foreign OpenGL driver binary in Vulkan.

The present importer intentionally reports vertex/geometry records as **not
imported**. It also cannot recover pipeline pairings from a transferable cache.
Retaining those records is useful evidence, not proof of complete import. Keep
the original cache and its checksum even when only fragment modules are used.

Next integration step: merge the retained inventory/pack with the 150 additions
through the existing Forge preparation contract, preserving learned runtime
misses. Qualify destination-GPU pipelines separately: the TAS cache supplies
shader evidence, not new observed pipeline pairings. The one vertex record is
retained but not imported; `complete_import=false` is intentional and must not
be relabeled as complete just because fragment compilation succeeded.

ROM, CTM payload, native cache, screenshots and derived shader packs stay private.
No automatic change to public packages, user's active configuration, or release
allowlist is implied by this collection. This task does not alter gameplay,
NRI rendering, TopScreen or the platform-specific cache activation contract.
