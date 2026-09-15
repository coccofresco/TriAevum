# TopScreen HUD ownership and aspect preservation

## Scope and status

Checkpoint: 2026-09-15. The reported pause/HUD aspect, minimap, independent item
input and Sheikah Stone playback/exit paths have now passed local Windows and
Linux runtime checks. This is not a published release or exhaustive coverage of
every UI page/item/movie. Issues: [25](https://github.com/coccofresco/TriAevum/issues/25),
[32](https://github.com/coccofresco/TriAevum/issues/32),
[39](https://github.com/coccofresco/TriAevum/issues/39).

- Native gameplay HUD now uses a centered, uniformly scaled native canvas,
  matching the existing host-composed HUD and inverse pointer mapping.
- Minimap marker geometry no longer receives the independent button/HUD scale.
- ZR bow use is demonstrated in a real checkpoint, including ammunition
  decreasing from 50 to 49. This does not establish every item/context works.
- Pause/map/inventory and message backdrops now share the centered canvas and
  are clipped to it. Independent ZL hookshot use was captured from gameplay.
- Jabu-Jabu with the native map/compass flags shows its marker on the map.
  A second defect on savestate reload is fixed: original map/indicator positions
  now persist with the already-transformed guest buffers. Fresh entry and reload
  framebuffer captures both retain the marker. Older states without these
  originals need a native scene re-entry; do not infer lost coordinates.
- Sheikah Stone plays a native movie, completes or cancels it, and returns to
  controllable Kokiri gameplay on both platforms with the matching rebuilt title
  module. See the final qualification section below; older pending notes are
  retained as investigation history, not current playback status.
- Visions title/panel/model ownership is now corrected and framebuffer-verified
  on Windows in ultrawide and 4:3. The latest adapter change still needs deployment
  and smoke verification on Linux: the remote machine is currently unreachable.
  Earlier cross-platform movie/input/minimap results remain separate evidence.

## Evidence and ownership

[CmbVShader.vsh](https://gist.github.com/M-1-RLG/c0a3ef277241781297ddeb4763dfbc88)
projects positions through c0..c3 after model/view transformation. Its board
branch and stereo flag do not establish UI ownership or preserve a host window's
aspect ratio. Comments are supporting evidence, not classification rules.
No shader equations were replaced by scene-specific corrections.

The copied `ui_native_workflow_closure.cpp` identifies these exact draw calls:

| Owner | Entry | Caller return |
| --- | --- | --- |
| PauseQuestPage_Draw | 0042B9F4 | 003004E0 |
| PauseUi_Draw | 0041EC50 | 00419830 |
| PauseOverlay_DrawAllGroups | 0041F308 | 0041983C |
| PauseWorldMap_Draw | 00425930 | 0041EB14 |
| Renderer-local orthographic backdrop | 002FFF7C | 00419824 |
| Orthographic deferred model queue 6 | 0041AFAC | 00419890 |
| Screen-space overlay queue 4 | 004228E4 | 00300524 |
| Hint upper UI callback | 00471F60 | 0030051C |
| Hint lower UI callback | 00477E30 | 0041984C |
| Message backdrop/choice | 0042CB54 | 00300530 |
| Auxiliary message glyphs | 0042A278 | 0030053C |
| Contextual message glyphs | 00427A3C | 00300548 |
| Main message glyphs | 0042CBCC | 00300554 |

The OOT3D composition adapter tracks command cursor 0054CC4C across these calls.
It publishes exact UI command spans, including models emitted inside the UI.
The frontend propagates their domain independently of adjacent scene draws.
The enclosing 00300328 DrawViewPass is deliberately NOT a UI owner: it also
executes world callbacks. A framebuffer experiment using that broad scope lost
grass/world attribution and was rejected. A regression test excludes it.

Shared renderer `pica_ui_canvas.h` fits UI viewport/scissor coordinates uniformly
and centers them; Scene/Unknown retain their existing projection. Physical
framebuffer dimensions include native rotated/doubled sampling: fit the native
480x400 canvas in that space, equivalent to logical 400x240 after scanout.
Do not substitute a logical 240x400 framebuffer in this calculation.

TopScreen 2.1.1 reference function 005C7F48 translates the minimap marker region
but does not apply the general button scale to it. The port did both. The fix in
`TransformTopScreenQuestGeometry` preserves the map-owner translation and omits
the second scale/margin transform for those quads. Tests cover scales 0.6/0.8/1.2.
Reference archive identities and provenance remain in
`OOT3D_TOPSCREEN_2_1_1_PORT.md`; private exports are not distributed.

## Verification

- Renderer suite: 19/19 passing, including native/host canvas and pointer inverse
  parity over five aspect ratios and three render scales.
- Native frontend and composition tests: passing; mixed Scene/UI command lists
  retain neighboring scene domains, UI spans are consumed once, reset clears scope.
- TopScreen profile and native UI lifecycle tests: passing.
- Actual NRI framebuffer captures: 1720x720 ultrawide and 960x720 4:3, adult Link
  outside Castle Town, default scene effects present. HUD retains native proportions;
  A/B, item slots, minimap and host HUD share the centered canvas.
- Ultrawide capture: 302 UI entries/exits/spans, no overlap rejection.
- Input probe: ZR activates the bow and consumes one arrow on release. ZL was
  subsequently pressed while already aiming; this is not independent ZL proof.

Private reproducibility artifacts:
`%TEMP%/TriAevum-ui-canvas-ultrawide/leaf-runtime.json`,
`leaf-framebuffer_000150.bmp`, `four-three-framebuffer_000150.bmp`,
`items-runtime.json`, `items-framebuffer_000130.bmp` and `_000180.bmp`.
Checkpoint: `J:/TriAevum-verify-20260910/win-field-x2/checkpoint.oot3dsav`.
Runtime: `J:/TriAevum-verify-20260910/runtime/TriAevum.exe`.
These are correctness captures, not performance measurements. Windows desktop
screenshots were not used. Linux was not rerun in this increment.

## Original validation checklist (historical)

1. Test pause inventory/map in ultrawide and 4:3, including inverse pointer hits.
2. Reproduce the dungeon minimap screenshot from #32 and counters from #39.
3. Independently activate ZL and ZR items from neutral gameplay, including the
   post-Forest-Temple state. Preserve native update cadence; icon animation and
   successful query counts alone are insufficient evidence of item activation.
4. Reproduce Sheikah Stone entry/exit with a suitable checkpoint and trace native
   UI ownership/lifecycle before changing routing. No speculative crash fix.
5. Run Linux smoke verification before publishing or marking reports resolved.

## Extended qualification (2026-09-15)

- `0b2d848`: initial centered native canvas and marker scale correction.
- `pages-four-three-framebuffer_000200.bmp` / `_000560.bmp`: actual NRI 960x720
  map/inventory; logical UI canvas is 960x576 centered vertically, with native
  proportions. Ultrawide map/backdrop and message captures also pass inspection.
- `qualification-framebuffer_000170.bmp`: independent ZL hookshot chain.
  ZR bow use previously consumed one arrow (50 -> 49).
- `jabu-persist.oot3dsav` and `jabu-reload-framebuffer_000150.bmp`: real scene
  entry, map/compass marker, then reload with original projection state preserved.
  The fixture changes only the private save inventory flags before scene entry.
- Windows: composition, frontend, TopScreen profile, UI lifecycle, CTR host,
  TLS-only process and full savestate round-trip tests pass. TLS restoration
  accepts the unallocated initial state as well as multiple allocated pages.
- The full synthetic process suite is not runnable against the frozen product
  registry; use `oot3d_native_a32_process_tests --tls-only` for this boundary.
  This limitation does not replace the actual movie/scene verification.

### Sheikah Stone runtime dependencies

The failures were observed in actual guest execution, not inferred from HUD
appearance. Keep the fixes in their generic owners:

1. `tools/ctr_services/y2r_service.h`: synchronous YUV conversion with native
   fixed-point coefficients, DMA strides/gaps, rotation, Morton output and
   conversion completion. `oot3d_native_a32_ctr_host.cpp` owns IPC/event handles
   and persistence. Azahar provenance is in `THIRD_PARTY_NOTICES.md`.
2. `NativeA32Process::CreateThread`: allocate another TLS page when eight slots
   are exhausted. The movie creates a ninth thread. Existing addresses remain
   stable, and multi-page TLS restores correctly.
3. `OnlyBackgroundThreadsReady` / `RunUntilGuestWait`: yield to host event
   delivery when the foreground waits and only lower-priority poll workers are
   ready. The movie worker otherwise prevented timers and input from advancing.
4. AOT recovery: an internal switch case is executable even when it is not a
   public callable ABI root. Base-relative jump tables also differ from
   self-relative pointer tables; recognize their ADR/LDR/ADD-pc structure.
   The translator tests cover this independently of title addresses. The product
   selection/manifest must match the newly generated program before release.

Y2R dithering flags are stored but not applied, matching the inspected donor;
this is not a claim of complete physical Y2R hardware emulation. The first new
decoder build reached an additional base-relative dispatch gap, so reaching
Visions alone is explicitly NOT proof of working playback or exit.

Private decoder diagnostics are under `J:/TriAevum-diagnostics/y2r-codec-relative`;
UI captures/checkpoints are under `%TEMP%/TriAevum-ui-canvas-ultrawide`.
Never package these artifacts. Keep original game saves separate from diagnostic
states. No issue has been marked resolved solely on unit tests.

### Continued qualification

- `1f5ef90` commits the UI composition, minimap projection persistence and
  portable movie host services described above.
- `jabu-motion-framebuffer_000150.bmp` separates the moving yellow player arrow
  from the red entry marker, both positioned on the minimap after movement.
- `linux-jabu-compass-framebuffer.bmp` verifies the same native compass owner
  on Linux/Wayland at 1720x720. Linux composition, TopScreen profile, CTR host
  and savestate tests pass. This supersedes the earlier Linux-not-rerun note
  for those boundaries, not for movie decoding.
- Windows physical-binding tests also pass all 24 shoulder/trigger permutations:
  held/released ZL/ZR survive configuration serialization and do not inject X/Y.
  This is boundary coverage, not a claim to reproduce the reporter's exact save.
- The movie decoder exposed a base-relative jump table with index arithmetic
  including an ARM shift alias. The decoder labels that shift
  `data_processing_extended`, so accepting ordinary arithmetic alone was
  insufficient. Tests now cover actual program recovery through that pattern,
  as well as rejecting an overwritten base. No title-address exception is used.
- Current structural program: 12,442 functions, 12,439 compiled functions,
  162,719 blocks, 924,535 unique instructions; product audit passes. Translator
  suites pass 61 tests, including 17 recovery tests and execution of generated
  C++ computed-call continuations. These counts
  describe static coverage, NOT successful movie playback.

Movie qualification must use the matching newly generated title module as well
as the runtime. Reusing an older packaged module preserves its missing dispatch
targets even when the host services and HUD are corrected. No release recipe is
promoted until playback and return to gameplay have actually passed.

The local-extension provenance now records the modified relative-table recovery
source. Its validation also exposed two stale working-file hashes already present
in public snapshot `034820c` (`recomp/a32_runtime.cpp` and `recomp/a32_core.cpp`).
Those metadata records were reconciled with the unchanged tracked files; their
original donor hashes remain intact. `validate_local_extensions` now passes.

### Decoder continuation audit

The first movie probes exposed more than a missing IPC service. The offline
translator did not preserve several handwritten ARM control-flow idioms:

- `ADR lr,continuation; B/Bcc callee` requires a resume entry even though the
  branch instruction does not set LR.
- The continuation can differ from the instruction immediately after the
  branch, and can be constructed with several immediate additions.
- Relative jump-table loads can be separated from the PC write by unrelated
  arithmetic, shifted operands, or LR restoration.

Recovery lives in the pinned frontend's documented local extension, with
structured resume edges in `whole_aot_program.py`; conditional lowering keeps
the condition-failed fallthrough distinct from the successful return path.
No decoder-address whitelist or runtime instruction fallback was added.
An all-reachable-code preflight found 101 provable explicit continuations and
zero missing block entries in program
`a652c6178d73b099845046c7688691ab6eab3e7b44975754d27b6cc0bf7229d4`.
This is static evidence, not proof that all dynamic movie paths are covered.

On September 15 the reporter of #39 commented that the latest version seems
to fix the issue. This supports the independent item/HUD tests but does not
explicitly confirm Sheikah Stone playback. Keep playback and exit pending until
verified with the matching new module on the real runtime.

The actual Windows movie probe of program `a652c617...` passed the previously
missing continuations but stopped at `004B319C` (missing offline dispatch entry).
The next investigation found **sparse relative tables**: reserved zero selectors
were being interpreted as the end of a table. Recovery now preserves internal
zero slots as data, never as executable destinations, and trims trailing padding.
Program `c5df1b2c227fecd86bff4a0ba883932c37e2977266c7254db5ee4182b2cd1df5`
has 138 recognized dispatch sites, 72 distinct relative tables and 648 distinct
targets, with none of those targets missing from its block registry. This last
program initially passed structural selection/audit and tests only. It has now
been built and qualified as recorded below. Do not confuse it with the previous
Windows/Linux modules that still lack those entries.

### Read-only decompilation evidence and next validation boundary

Inspected external snapshot: `I:/oot3decomp`, commit `e0b6c5f7`.
No external sources or metadata were modified.

- `src/middleware/mobiclip.c`: structured portable decoder; bit reader,
  quantization, inverse transforms, intra prediction, motion compensation,
  frame/packet decoding. It explicitly attributes FFmpeg codec cross-checks.
- `include/oot3d/mobiclip.h`: explicit decoder, plane, frame, motion and status
  contracts; its host pointer-based structures are NOT guest-memory ABI layouts.
- `tests/mobiclip_test.c`: 11 test groups including complete synthetic frame
  decoding. Compiled with the existing Windows GCC and executed successfully;
  outputs stayed in the private temporary directory. This does not establish
  correctness on the actual Sheikah Stone movie packets.
- `metadata/mobiclip_native_bindings.csv`: clone/role mapping and explicit
  `non-aapcs` internal entries.
- `docs/HANDOFF.md`, sections 7CD/7CE: nine bit-reader internal entries and six
  continuations/trampolines must not become independent host C call boundaries;
  they carry live state through multiple registers and internal fallthroughs.
- `tools/audit_mobiclip_register_bitreader_contracts.py` and
  `tools/audit_mobiclip_internal_jump_contracts.py`: existing binary-evidence
  checks to consult or copy, not execute in place if they write metadata.

Use this evidence to review the complete movie path and compare decoded planes
on real packets. If adopting the portable core, do so at a complete packet/frame
boundary with explicit guest-memory/state conversion and save/restore contracts.
Do not substitute individual non-AAPCS fragments. UI queue ownership, Y2R,
native timing and return-to-game handling remain separate responsibilities.

The real-packet test now identifies a concrete defect in that portable core:
8x8 planar prediction used shift 2 instead of 3. A guarded correction on a
private copy produces byte-exact FFmpeg output for 64 frames each of three real
movies (192 frames total). The unmodified core diverges from the third frame
on both measured baseline movies. See
[decoder qualification](TRIAEVUM_MOBICLIP_DECODER_VALIDATION.md) for the precise
expression, repeatable harness and evidence. This correction has NOT yet been
connected to the game. Runtime movie playback was subsequently repaired through
the existing translated decoder, as recorded below, not by importing this core.

## Completed movie qualification (2026-09-15)

The sparse-table translator fix removed the real `004B319C` dispatch failure.
The remaining delay was a scheduling problem: the low-priority movie worker
polls its mutex/status while the foreground waits for events. A foreground-sized
one-million-block dispatch budget let this loop monopolize host execution.
The diagnostic trace recorded about 1.8 billion block entries and only 135
presentation frames before the 90-second run limit. This instrumented trace is
causal evidence, not a comparable performance benchmark.

`NativeA32Process::DispatchBlockBudget` now bounds a dispatch to 10,000 blocks
only when the primary thread is waiting and no ready/running worker has equal
or higher priority. It preserves smaller caller budgets. PC/registers survive
the yield, host events are delivered, and execution resumes normally. Guest
clock rates, thread priorities and foreground budgets are unchanged. The quantum
is a host scheduling policy, not a recovered game constant or decoder-address
exception. Both ordinary and product dispatch use the same policy.

Owning files: `tools/oot3d/native_game_runtime/oot3d_native_a32_process.{h,cpp}`
and `oot3d_native_a32_process_tests.cpp`. Boundary tests cover the bounded
background quantum, preservation of smaller budgets and the full foreground
budget. `--tls-only` passes on Windows and Linux.

Actual NRI framebuffer verification, default dispatch budget (no CLI override):

| Platform | Route | Frames | Observed result |
| --- | --- | ---: | --- |
| Windows | Play to completion, exit, move | 1501 | Movie at 230; menu after completion; Kokiri at 1310/1430 |
| Linux Wayland | Same route | 1501 | Movie at 230; Kokiri at 1310, matching Windows |
| Windows | Play, cancel with B, exit, move | 901 | Gameplay restored by 590 and retained afterward |
| Linux Wayland | Same cancellation route | 901 | Gameplay restored by 590 |

All four processes exited successfully. Both completion reports have 9,967
total guest refresh frames; the Windows cancellation report has 8,767. These
counts include the restored state's frame origin. Runs use native timing,
fixed delta 1/30 and deterministic input/capture, not interpolated FPS claims.
The tested movie is hint000 (Link carrying Ruto); not every hint was played.

Private artifacts (never package these):

- Windows: `%TEMP%/TriAevum-ui-canvas-ultrawide/stone-complete-runtime.json`,
  `stone-cancel-runtime.json` and corresponding `*-framebuffer_*.bmp`.
- Linux: `/home/xander/triaevum-linux-parity-20260911/ui-issues-20260915/movie/`,
  `complete-runtime.json`, `cancel-runtime.json`, framebuffer sequences and logs.
- Inputs: `%TEMP%/stone-complete-input.json` (A at 60/180, B at 900/1050/1200,
  movement at 1350), `stone-cancel-input.json` (A at 60/180, B at 240/360/480,
  movement at 650); button holds last six presentation frames.
- Linux runner: private `triaevum_linux_stone_complete.py`, optional `--cancel`.

The earlier Linux `stone-facing` fixture did not enter the Stone and is NOT a
passing movie test. The final Linux run uses the Windows guest/kernel state with
only the optional derived `native_pica_visual_replay_state` omitted: its host
replay ABI was incompatible. Existing cold-replay restore handles the omission;
the fixture checksum was regenerated. No production compatibility checks were
disabled and no guest memory or game logic was changed for that conversion.

Required title program:
`c5df1b2c227fecd86bff4a0ba883932c37e2977266c7254db5ee4182b2cd1df5`.
Qualified module SHA-256:

- Windows DLL: `304dae27dde2d287f0d911efd2d66e194359ad9403f5bd9083b05fc16f5ffe6b`.
- Linux SO: `fbf5291cc268d5162c58162319e625bba56877b5700ef0d26c937535592b4b69`.

The runtime alone cannot fix an older title module's missing dispatch targets.
Before release, rebuild/package the matching module through the normal
allowlisted release process and rerun the package audit. No binaries, fixtures,
movies, extracted data or private decoder copies are added to public source.

## Shared implementation alignment and Visions closure

There is one composition adapter for Windows/Linux, one shared raster-canvas
calculation and one existing inverse-pointer transform. No platform-specific
UI scaling, second movie decoder, texture-name rule or duplicate TopScreen
layout correction was introduced. The title module remains program `c5df1b2c...`;
these last changes require only a runtime relink, not title recompilation.

Read-only binary/decomp evidence identifies the missing owners:

- `HintMovie_Init` at `00457518` installs callbacks `00471F60`/`00477E30`.
  Literals `004577B4`/`004577B8` confirm their addresses; they populate
  GraphRenderer+7440/+7444. The invocation continuations are `0030051C` and
  `0041984C`. Their state dispatch draws UI, unlike unrelated renderer callbacks.
- `0041AFAC` establishes an orthographic projection and consumes model queue 6.
- `004228E4` consumes screen-space overlay queue 4, including the Hint header.
- `0047087C` and `00483D10` enqueue UI work; classifying only those producers
  does not cover later drawing. No ineffective producer hook was retained.
- The shared primitive writer `003FB5EC` is also used by UI. An exact outer UI
  owner now takes precedence over its environment classification, including
  packets written into a deferred buffer. Scene calls retain their classification.

Tests cover these exact entry/return pairs, reject unrelated callers and the
mixed `DrawViewPass`, and preserve UI attribution through a deferred primitive
writer without overlapping spans. No world-container blanket classification.

Windows qualification after the complete change:

- Composition, frontend, TopScreen profile, UI lifecycle and input suites pass.
- `stone-final-framebuffer_000110.bmp`: 1720x720 Visions, header and panel centered
  with uniform scale. `stone-fourthree-framebuffer_000110.bmp`: actual 960x720
  framebuffer, UI fitted to 960x576 at y=72. Use `config-4x3`: the persisted
  output size overrides bare CLI width; a CLI-only run was rejected as evidence.
- `stone-qualified-runtime.json`: 1501 presentation frames, 9967 total guest
  refreshes, successful exit. Movie at 230; gameplay at 1310 and movement after.
  Zero composition overlap, partial-submission or read failures. The 143 empty/
  invalid primitive ranges are unchanged from the baseline, not a new regression.
- Private structural trace: `stone-final-pica.jsonl`, same temporary directory
  as earlier Windows artifacts. All screenshots are framebuffer captures.

Remaining deployment checkpoint: transfer only the two composition source/test
changes to `/home/xander/triaevum-linux` with `git apply --check`, preserving its
unrelated work; build `oot3d_native_game` and `oot3d_native_pica_composition_tests`,
run the test and repeat the existing Stone fixture. SSH to `192.168.1.190`
timed out during this increment. Do not label that machine updated until the
transfer/build and framebuffer verification actually complete. Do not publish
or close issues based on a stale packaged title module.
