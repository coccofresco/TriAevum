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

## Remaining Qualification

- Implement and test the missing TextureCopy transfer, then replay the
  pre-stall checkpoint and complete the cutscene. Issue 45 remains open.
- Validate save-yes and mouse/touch Game Over choices in addition to the
  tested directional/A paths.
- Linux mirror is not updated yet: SSH timed out, including after Wake-on-LAN.
- No issue has been closed, no release has been published, and no ROM,
  reference mod, save, capture or translated donor payload is added here.
