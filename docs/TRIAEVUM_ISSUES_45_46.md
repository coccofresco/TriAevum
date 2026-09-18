# Issues 45 and 46: Native UI and Cutscene Verification

## Scope

Issue 46 must recover the native Game Over options through the TopScreen
adapter. It must not introduce a host replacement menu or composite the entire
lower framebuffer. Issue 45 concerns Sheik's identity-reveal cutscene, not the
Sheikah Stone movie player.

## Game Over

The native controller had created its choices, but the product dispatcher did
not execute the existing renderer-table reconciliation source port. Its delay
and fade counters remained zero. Simply redirecting a framebuffer did not fix
this; that prototype was discarded.

Evidence, read-only:

- TopScreen 2.1.1 `005CF75C`: gameplay mode 3, variant 2, Game Over phases 3..6,
  with native transition and transient scene-latch exclusions.
- `005CFB38`: 60 draw calls of delay, 24 fade steps; visible native queue entries
  at controller `+AF8..EF4` reconcile into corresponding slots minus `400`;
  the terminal renderer at `+6FC` is hidden. Existing conflicting owners are
  preserved. Phase 7 is outside the reconciliation interval.
- Compositor `005D3D30`: viewport `(0,40,480,320)`, auxiliary native pause draw
  `005CA3DC` (native `0041EC50` with six child gates temporarily suppressed),
  then native upper controller draw `00427A3C`; restore `(0,0,480,400)`.
- Original `00426894` creates the choices, `00426E6C` consumes their input,
  and `00458AC0` handles the native Game Over result. These remain unchanged.

Implementation boundaries:

- `tools/oot3d/ui_topscreen/oot3d_top_screen_mod_profile.{h,cpp}` owns phase
  eligibility, delay/fade and table reconciliation.
- `ExecuteProductTopScreenHook` in
  `tools/oot3d/native_game_runtime/oot3d_native_a32_window.cpp` runs the native
  draw sequence at its observable function entry. Native choice objects, text,
  selection, saves and return-to-game/title logic remain native.
- A restored savestate can already contain migrated choices while host fade
  counters are zero. The adapter recognizes hidden terminal plus a visible
  reconciled choice and resumes presentation immediately. It does not replay
  the fade and draw those choices temporarily with the wrong viewport.
- The viewport precedes the auxiliary draw, not just the final choice draw.
  This matters for the first visible frame.

### Windows Verification

Private evidence root: `J:/TriAevum-diagnostics/issues45-46/`.
The original Kokiri checkpoint is untouched; `zero-health.oot3dsav` is a copy
with only health `0058799C` set to zero. It is a diagnostic setup, not a claim
to have tested every cause of player death.

| Run | Outcome |
| --- | --- |
| `gameover-before` | GAME OVER, no choices; native controller state 4, Game Over phase 5 |
| `gameover-native-draw` | Choices became visible, but composition was incomplete; not accepted as final |
| `gameover-native-composition` | Centered native save question and buttons |
| `gameover-composition-continue` | Don't save, Continue prompt, then gameplay with Game Over phase 0 and health `0030` |
| `gameover-composition-quit` | Quit leaves Game Over and enters the title sequence |
| `gameover-realtime` | Restored choice screen centered in framebuffer capture 1; Continue path exercised with real-time pacing |

Repeatable input timelines: `topscreen_gameover_continue.json` and
`topscreen_gameover_quit.json` under `tools/oot3d/native_game_runtime/input_timelines/`.
They begin at an already-open native save question, use directional/A input,
and write only the isolated diagnostic save directory.

Unit tests cover phase boundaries, fade delay, ownership conflicts, exclusion
by runtime scene latch, and immediate reconstruction from migrated native
tables on savestate restore. The TopScreen contract executable passes.

### Audio Qualification

Do not judge audio latency using accelerated fixed-delta capture runs.
`gameover-composition-continue` accumulated 453,655 buffered samples at
32,728 Hz, about 13.86 seconds. The same input path in `gameover-realtime`,
with `--frames 0`, `--max-seconds 38`, no forced delta, and normal presentation
pacing, peaked at 4,950 samples (0.151 seconds). PCM is captured privately.
Framebuffer captures still produce brief starvation events; they are not
evidence of a native music timer bug. No audio/gameplay timer was shortened.

## Sheik Reveal

There is no reporter save or platform information. Native entrance handling
at `0044F0E4` checks adult age, quest bits `08` and `10` (Spirit/Shadow), event
`C4` clear, and entrance-table scene `43`. It then sets C4 and selects entrance
`0053`, cutscene `FFF8`. Literals were verified in the native image, not
inferred only from N64.

`Flags_GetEventChkInf` reads SaveContext `00587958 + EEC + 2*(flag>>4)`.
For C4 the word is `0058885C`, bit `10`.

All available complete adult saves have C4 set. Diagnostic setup:

1. Copy private `pr13-native-save-fixtures/adult-temple-inventory.oot3dsav`.
2. Change only the C4 word from `82FB` to `82EB` in the copy.
3. Use the existing native entrance recipe `tokinoma_info_entry_0057`.
4. Let the native entrance routine select the cutscene; do not write a
   cutscene selector into an already-loaded scene.
5. Advance dialogue with `native_cutscene_dialogue_advance.json`.

`sheik-native-trigger` reached Sheik's speech and the Triforce story sequence;
the checkpoint has native entrance `00A0`, cutscene `FFF4`, and C4 set again
by the game. `sheik-reveal` continues that checkpoint. Merely reaching this
sequence does not close issue 45: the reveal, subsequent dialogue/reward and
return to control must be checked, or a precise stall isolated.

### Reproduced Stall and Missing Transfer

`sheik-reveal` completes the visible Zelda reveal, then stalls at the next
transition. Captures 2500, 3000, 3500 and 4000 have identical SHA-256
`417a97c5949c964952586a33116e7b7ff7b37b618c56a17102302289ae01f875`.
No guest memory fault is reported. Its frame-4100 checkpoint retains the stall.

`sheik-wait-diagnostic` repeats the sequence with `--extended-diagnostics`,
captures at 2300/2500 and a checkpoint at 2300 (before the stall). The final
GSP trace identifies a command accepted without implementation:

```text
id=4 (TextureCopy), control=01000104
source=1F2447C0 destination=1F3E3300 size=000BB800
input_width_gap=000003C0 output_width_gap=004003C0 flags=0000000C
```

The native list at `1400006C` contains 34 requests, 34 issued and only 33
completed. Main thread waits in `0030B398`, LR `004884C4`, from
`nngxWaitCmdlistDone` / `00310264`; native global `005A6F30+11` remains busy.
All threads are waiting, no host GPU completions remain, and the GSP relay is
empty without overflow. Sending more dialogue input cannot repair this wait.

Code evidence:

- `native_pica_frontend/oot3d_native_pica_frontend.h` declares TextureCopy=4.
- `Oot3dNativePicaFrontend::SubmitGspCommand` accepts it but neither submits a
  copy to its packet sink nor reports it unsupported.
- `native_game_runtime/oot3d_native_a32_ctr_host.cpp` handles completion for
  DMA, memory fill and display transfer, but not TextureCopy.
- Local Azahar `src/video_core/gpu.cpp` handles TextureCopy through the
  display-transfer engine; `renderer_software/sw_blitter.cpp::TextureCopy`
  documents the 16-byte units and independent source/destination row gaps.

Required correction belongs to the shared PICA transfer/submission/backend
boundary: typed raw texture-copy command, ordered execution against current
GPU-owned source data, destination gap preservation/cache invalidation, and
exactly one PPF completion after the copy. Here the source rows are 15,360
bytes and output rows have a 1,024-byte gap. Do not specialize for these
addresses or sizes. Do not simply signal PPF, copy stale guest framebuffer
bytes, or map this to an ordinary scaling/color-converting display transfer.
No production TextureCopy workaround has been installed yet.

### Raw Copy Contract Implemented (Not Yet Connected to GPU Execution)

`oot3d_native_pica_transfer.{h,cpp}` now provides
`BuildOot3dPicaTextureCopyPlan` and `ExecuteOot3dPicaTextureCopy`.
The planner consumes the GSP packet, aligns length down to 16 bytes, decodes
independent width/gap fields, treats zero-gap sides as contiguous even with
zero width, and rejects zero strided widths and address-space overflow.
Its explicit byte spans are independent of texture formats and scenes.
The portable executor validates every span before mutation and preserves
destination gaps. It requires coherent CPU memory: it is NOT a substitute
for flushing GPU-owned surfaces and does not send a completion interrupt.
Overlapping source/destination storage is snapshotted by this executor;
hardware overlap behavior remains unqualified.

Windows contract test `oot3d_native_pica_transfer_tests` passes, including
the captured #45 packet (50 spans, 768,000 copied bytes), partial lengths,
unequal gaps, unmapped later spans without partial writes, overflow, and
3,600 width/gap/length combinations checked against an independent byte-index
oracle. Incremental test build takes about two seconds on this machine.

This is a tested transfer primitive, not an in-game fix. No new replay or
Linux validation has been performed for it. Next: add the typed operation
through the frontend sink, submission queue, ordered presentation scheduler
and GPU surface ownership path; only then deliver PPF and replay
`sheik-wait-diagnostic/checkpoint.oot3dsav`. Keep this separate from ordinary
display transfers, which perform color conversion and may invoke scene
effects/presentation. Neither behavior belongs to a raw byte copy.

### Tiled Image Mapping and Coherence Findings

`runtime/three_ds_recomp/include/fast/renderer3ds/pica_texture_copy_plan.h`
adds a backend-independent mapping from physical byte spans to native tiled
image rectangles. Surface dimensions and formats are explicit inputs, not
inferred from this scene. Full Morton tiles and adjacent equal translations
coalesce; partial tiles retain exact texel coordinates. Format reinterpretation,
partial texels, out-of-range accesses and overlapping destination spans are
rejected rather than approximated. Coordinates remain in native image axes;
the backend must apply its orientation and resolution mapping separately.

The captured packet maps to one 480x400 rectangle when supplied a 480x400
source and 512-wide destination. The test uses a 512x512 destination fixture;
this is not a runtime assumption about the consumer's declared height.
The existing 3,600 parameter combinations now additionally check all valid
image mappings against independent Morton encoding and raw byte writes,
including untouched padding. Windows test passes; no full game build or
Linux test was run for this isolated header-only planner.

Important integration finding: `CaptureTextureResources` in
`oot3d_native_pica_submission.cpp` creates immutable CPU-memory texture
snapshots, and `GetOrCreateNativePicaTexture` in `gfx_vulkan_pica.cpp` caches
ordinary textures from those bytes. Only the Shadow2D path currently has an
explicit live render-target alias in that function. Therefore a GPU copy
alone is insufficient: subsequent samplers must resolve the copied GPU
version, or coherent writeback must precede their snapshot capture. Do not
announce completion while allowing the consumer to sample stale CPU data.
The source version must also be captured at the copy's native command position,
before later draws can overwrite it.

The existing `CapturePicaColorTargets` requires an idle frame boundary and
captures all targets. It cannot safely be called in the middle of recording
as a shortcut. Ordinary display transfer also runs scene-effect hooks and
stores scanout images, not ordinary sampled textures; reusing it unchanged
would introduce both composition and ownership bugs.

Issue 45 remains open. Neither planner is connected to production GPU
execution yet; no new successful cutscene replay is claimed.

### Operational TextureCopy and Cutscene Completion (2026-09-18)

The preceding entries describe earlier milestones, not the current state.
TextureCopy is now connected through the frontend, submission queue, Vulkan
bridge and shared backend. `TextureCopyBytes` distinguishes raw width/gap
copies from pixel display transfers. The isolated backend implementation is
`gfx_vulkan_pica_texture_copy.cpp`; `pica_framebuffer_encode.h` restores native
tiling, channels and resolution from the authoritative GPU color surface.
Guest destination gaps remain untouched and PPF is delivered after writeback.
No title logic, cutscene state, AOT module or scene-specific address was patched.

The first connected test still froze: the guest waited for TextureCopy before
swapping, while the presentation scheduler waited for a swap before executing
the copy. `TakeDependencyWork()` and `DependencyFlush` now execute the ordered
offscreen prefix without inventing a scanout or presenting unfinished work.
The previously completed scanout remains available during that dependency.
Immutable sampled-texture snapshots are refreshed only when both their old
bytes and current guest memory establish that the completed copy owns them.
Visual replay format V13 records raw copies; readers retain V1-V12 support.

Windows framebuffer evidence is under private diagnostics
`issues45-46/sheik-texture-copy-final`: frame 7200 shows normal Temple gameplay
after the full revelation/flashback/Light Arrow sequence. The run completed
7,534 host frames, 688 dependency flushes and 675,919 draw executions, with zero
duplicate-draw attempts and zero memory faults. Its loaded checkpoint is the
same pre-stall checkpoint used by the failing baseline. Four focused suites
cover transfers, submission, visual serialization and bridge scheduling;
the transfer suite includes 3,600 width/gap/length combinations.

This is a conservative synchronous GPU readback/writeback implementation,
not a performance optimization: the complete run spent about 25.65 seconds
in transfer submission, predominantly across the animated copies. GPU-only
copy/resolution is a future optimization. Ambiguous source ownership and
destinations aliasing active attachments fail explicitly, rather than silently
claiming unsupported completion. Off-base source ownership and general chained
copy coherence still need broader qualification.

The first cache-retirement implementation checked texture start addresses,
missing a sampled texture containing the written subregion. The diagnostic
run `sheik-copy-save-diagnostic` established aggregate decoded cache bytes
exceeding the existing 512 MiB limit. Retirement now checks overlapping
storage intervals after queue synchronization, conservatively bounding native
mip storage by RGBA8 (extra invalidations are safe uploads, not lost content).
`sheik-copy-overlap-save` successfully saves during the flashback at native
frame 14207: 292,740,633 bytes, without raising any limit. Error messages now
include texture dimensions, address and aggregate bytes for future diagnosis.

Attempting to load that native-30 state as interpolated-60 was rejected by
the existing savestate timing-mode contract, before rendering. This is NOT a
2x renderer result and the compatibility check was not bypassed.

The same-mode reload (`sheik-copy-reload`) decoded/restored the new save,
including 298 sampled textures / 133,749,504 decoded bytes, but produced
zero new draws or transfers in 600 run frames. Therefore writing the save
is verified; resuming mid-flashback is NOT. Investigate outstanding GPU
completion/dependency ownership at save/restore rather than treating a
successful file decode as successful gameplay. The uninterrupted full-sequence
result above was obtained from the existing pre-stall checkpoint, not this new
mid-flashback state.

## Remaining Qualification

- The reproduced #45 stall is fixed and the full sequence returns to gameplay
  on Windows and Linux/native-30 presentation. Qualify interpolation 2x and
  pending dependency restoration in mid-TextureCopy savestates before closing the
  wider issue.
- Validate save-yes and mouse/touch Game Over choices in addition to the
  tested directional/A paths.
- Linux synchronization and qualification are recorded below; earlier SSH
  unavailability no longer applies.
- No issue has been closed, no release has been published, and no ROM,
  reference mod, save, capture or translated donor payload is added here.

## Linux Alignment (2026-09-18)

Source baseline: `0bcd22d`, including the preceding #46 changes. Compared all
archived tracked files with `/home/xander/triaevum-linux`, normalizing line
endings only: 32 missing/different files were updated, then a second comparison
reported zero differences. Existing overwritten files are backed up privately
under `/home/xander/triaevum-before-0bcd22d`. Untracked parallel AOT experiments
were not imported, and the independently dirty mirror was not reset.

Both existing build paths compile successfully with three jobs:

- Native Clang: `/home/xander/triaevum-linux-build/TriAevum`.
- Steam Runtime SDK: `/home/xander/triaevum-steamrt4-build/runtime/TriAevum`
  and `oot3d_game_module.so`, using `scripts/run-in-steamrt4.sh` with
  `/home/xander/triaevum-steamrt4-qualified-sdk`.

The five focused Linux suites pass: PICA transfer, submission, visual replay
serialization, Vulkan bridge/scheduler and TopScreen mod profile (including
item dispatch, ocarina and D-pad contracts). No platform-specific source fix
was needed. The movie-capable title module was not recompiled or replaced:
`/home/xander/triaevum-ui-movie-title-build/triaevum_title_aot.so`.

Binary SHA-256 identities:

| Artifact | SHA-256 |
| --- | --- |
| Native runtime | `0ff43bf73a1de6941283ece011a3a970c6d1c060eb9142e64ce8d336e968f490` |
| SDK runtime | `351323a7b58741b2fa7a7386256bda19fce8e76cc2da99ad2396e60f2ab34f42` |
| SDK game module | `ac1957954ebaa5dcbbe839adf6433bc252b32fb7a188458db613eb9c0bae3a66` |

The existing **TriAevum - Updated Development Build** desktop entry points
directly to the updated SDK runtime. No AppImage or public release has been
repackaged; the separately installed published AppImage remains unchanged.
No actual Steam Deck hardware qualification is claimed.

The native Linux Vulkan/Wayland replay uses the unchanged Windows pre-stall
checkpoint and the same dialogue input timeline. It completes all 8,000 host
frames with exit 0, no pending draws/transfers, and returns to Temple gameplay
(framebuffer capture at frame 7,200). Private evidence:
`/home/xander/triaevum-issue45-20260918/{runtime.json,run.log,frame_007200.bmp}`.
It is a functional test, not a performance comparison: a concurrent SDK build,
framebuffer captures and platform profile differences invalidate timing or
pixel-identity claims. The mid-flashback savestate limitation remains open.

Native run totals: 688 dependency flushes, 714,805 executed draws, zero duplicate
draw attempts and zero guest memory faults. The SDK-built runtime independently
passes a 1,500-frame replay of the transfer-heavy flashback: exit 0, 664
dependency flushes, 90,474 draws, zero duplicates and zero memory faults.
Evidence: `/home/xander/triaevum-issue45-sdk-20260918/`. Captures show the
monochrome framebuffer-copy effect progressing. Widescreen side regions and
optional grass remain visible outside that native image; this qualification
establishes forward progress, not complete visual equivalence to the 3DS.
