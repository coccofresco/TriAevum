# TopScreen HUD ownership and aspect preservation

## Scope and status

Checkpoint: 2026-09-15. This is a verified implementation increment, not closure
of all three reports. Issues: [25](https://github.com/coccofresco/TriAevum/issues/25),
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
- Sheikah Stone reaches the native Visions screen after the Y2R/TLS/scheduling
  fixes below. Movie playback qualification is still pending; do not close #39.

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

## Next validation

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
- Current structural program: 12,385 functions, 12,382 compiled functions,
  161,912 blocks, 921,823 unique instructions; product audit passes. Translator
  suites pass 60 tests, including 16 recovery tests and execution of generated
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
