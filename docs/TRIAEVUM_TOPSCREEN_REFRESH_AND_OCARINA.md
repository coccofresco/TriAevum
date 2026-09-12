# TopScreen Refresh Fix And Ocarina Investigation

2026-09-10. Follow-up to `7617c8a` and the selective integration of
[999sian's PR #13](https://github.com/coccofresco/TriAevum/pull/13).
This separates verified input/assignment repairs from the still incomplete
ocarina presenter. It does not claim full mod parity.

## Refresh And Assignment: Implemented

There are three clocks: host polling, guest display refresh, and the native game
update. Host polling already preserves brief presses in `PendingPressedActions`.
The remaining error was replacing TopScreen's pressed snapshot at every refresh,
including refreshes before the game consumed it. A six-refresh timeout would
hide that scheduling error and could carry an action into a different UI state.

- `tools/three_ds/input/three_ds_digital_accumulator.h` owns a reusable sampled
  held/pressed accumulator. Multiple edges before an update coalesce; it is not
  a command queue. It has no title addresses or platform dependencies.
- `tools/oot3d/ui_topscreen/oot3d_top_screen_input_cadence.h` collects resolved
  refresh inputs, then publishes one immutable-for-the-update snapshot at
  `0x0041E988`, the compiled return from native `PauseInput_UpdateTouchState`.
  Queries do not consume the snapshot; a subsequent update clears an unrenewed
  press. Held state stays separate. Quickload clears temporal state.
- The old redundant refresh-rate ZR/ZL edge fields were removed. Existing host
  polling, native HID, Start handling and gameplay timing are unchanged.
- The Items hook formerly targeted `0x00433B68`, a BL **inside** a compiled
  basic block. Registering it did not make that instruction observable. It now
  intercepts the actual callee `0x002EC3E4`, only for LR `0x00433B6C`.
- Assignment reproduces the mod's separate held-edge sampler and keeps brief
  update presses. Pending work prevents re-entry before native execution.
  The native update runs once, followed by the selection/cursor continuation.
- The continuation resumes `0x00433B6C`, not `PC+4`: `0x00433B70` is another
  internal instruction, not a valid compiled entry. The shared registry now
  feeds all three relevant observation/dispatch filters.

The title module, renderer, platform backends, persistent configuration and save
format are unchanged. This is an OOT3D input/dispatch adaptation, not a patch to
the compiled title or a second implementation of item gameplay.

## Product Evidence

Linux NRI/Vulkan, same precompiled title, native timing without interpolation,
400 presentations per run. The populated adult native-save fixture is described
in [native-save verification](TRIAEVUM_NATIVE_SAVE_VERIFICATION.md).
Two timelines differ by exactly one **guest refresh**, not one presentation.

| Result | Before, even / odd | Repaired, both phases |
| --- | --- | --- |
| True item queries: I press, II press, I held, II held | `2/0/2/0` / `0/0/50/0` | `1/0/1/0` |
| Native input updates | not reported | 400 |
| ZR / ZL update presses | not reported | 3 / 2 |
| Items owner updates | not reported | 100 |
| Assignment begins / completions | 0 / 0 | 2 / 2 |

Framebuffers show Din's Fire assigned to ZR and boots assigned to ZL after
closing Items. All runs terminate normally. The duplicate host accumulator
was removed before qualification; all twelve corresponding captured frames
remain byte-identical to the intermediate repaired build. Final cleaned-source
qualification repeats both phases in `native-save-cadence-qualified-{even,odd}`
with the same counters and 12/12 matching framebuffers. A separate 900-presentation
title control (`topscreen-cadence-title`) matches all six `pr13-title` baseline
framebuffers byte for byte and exits normally.

**Follow-up (2026-09-10):** actual bow use (50 -> 49 arrows) and longshot aiming
are now verified in Hyrule Field using native menu assignment and native scene
transition. See [issues 5/17 product evidence](TRIAEVUM_ISSUES_5_12_17_19_20.md).
The following paragraph records the earlier Temple-only validation limit.

**Original limits:** a query returning true is not proof of using an item. The Temple
fixture has native suppression flags (`0x03800000` at its original checkpoint).
The follow-up movement/use run did not demonstrate Din's Fire being cast or a
magic decrease. That action and held-use items still need an eligible gameplay
fixture. Do not bypass native suppression to make a test pass. Physical
controller timing, focus changes, Android gameplay and a new Windows executable
are not qualified by these Linux scripted runs.

The profile suite covers short refresh taps, skipped refreshes, stable/repeated
queries, release/repress, held input, reset, compatibility fields, native
suppression, both assignment destinations, native rejection, caller/return
ownership and atomic destination validation. Four Python regression tests cover
the paired-probe checker, including equal-but-inactive reports and malformed
counters.

Run the existing `probe_renderer.py` twice with the same checkpoint/settings and
two guest-origin timelines shifted by one refresh. Keep saves/settings/output
directories isolated. Then:

```sh
python3 tools/triaevum_release/verify_topscreen_item_probes.py \
  EVEN/runtime.json ODD/runtime.json \
  --updates 400 --zr-presses 3 --zl-presses 2 --assignments 2
```

The checker deliberately does not certify visuals or item effects. Private
evidence lives under `/home/xander/triaevum-pipeline-live-proof/`:
`native-save-items-{even,odd}` (before), `native-save-cadence-final-{even,odd}`,
and `native-save-cadence-use-v2`. Fixture descriptions/timelines and the ocarina
checkpoint are mirrored in `I:/oot3dre_work/pr13-native-save-fixtures/`.
No save, mod payload or framebuffer is included in the public source.

## Official Mod Evidence

Reference: TopScreen 2.1.1 EUR, archive SHA-256
`e0c143c872ccf4ad72033caab768753b147a4033d260a701204a63ee6bc16df4`, IPS SHA-256
`c834030be2fbbfd93db26180333f112e1abcfd85050b09252a8449f06bf90400`.
Local root: `I:/oot3dre_work/topscreen-2.1.1-reference/`.
Use `topscreen_v2_1_1_raw_analysis.json`, `ghidra_export/decompiled/`, and focused
ARM disassembly of `topscreen_v2_1_1_payload.bin` (base `0x005C7000`). Existing
exports plus targeted Capstone disassembly were sufficient; no full-game
decompilation or external-repo modification was required.

- IPS `0x00433B68` -> wrapper `0x005DF460`: pre-handler `0x005CCCDC`, native
  update `0x002EC3E4`, post-handler `0x005CCD74`.
- Pre-handler samples held state at `0x005E3E44` against `0x005E22A4`, requires
  page state 2, sets action bit `0x400` and destination 5 (ZR) or `0x17` (ZL).
  Post-handler clears the bit and only rewrites a native selection result `0xB`.
- The mod's central sampler is `0x005CE554`, after the patched Quest draw at
  `0x003004DC` through `0x005DF638`; it writes held/pressed words
  `0x005E3E44`/`0x005E3E40`, guarded by `0x005E4454`.
  Our native-update boundary is a deliberate cadence adaptation, **not** a
  claim that the mod samples at `0x0041E988`.
- The compiled title source confirms blocks `0x002EC3E4`, `0x00433B6C` and
  `0x0041E988`; it does not expose `0x00433B68` or `0x00433B70` as blocks.
  Relevant existing shards are 052 and 099 in the private title source.

## Ocarina: Concrete Remaining Gap

**Follow-up:** the owner/guide repair is now implemented and verified in game;
see [ocarina owner, PR assessment and remaining scope](TRIAEVUM_TOPSCREEN_OCARINA_OWNER.md).
The section below records the diagnosis before that repair, not current status.

[PR #18](https://github.com/coccofresco/TriAevum/pull/18), by 999sian, head
`3655e285f4c53733b9e486a9434c073dba2d5954`, adds useful live evidence for the
missing performance UI. Review only its two ocarina commits (`e3eca08` and
`3655e28`): its branch also contains unrelated stacked PRs. No code from those
two commits is adopted yet.

The proposed condition change must not replace the verified mod suppression
predicate. Official `0x005C9ED8` tests result `!= 0xFF`, the scene extension flag
`0x01000000`, or action `> 1`; callers use it to **suppress** other content.
It is not a general instruction to promote the lower framebuffer. Preserve it
and give ocarina presentation its own owner/visibility contract.

Native checkpoint comparison now establishes:

| Native field | Ordinary gameplay | Ocarina free play |
| --- | --- | --- |
| scene `+0x2B82`, result | 0 | 255 |
| scene `+0x2B80`, action | 0 | 1 |
| `0x005093F8`, page state | 0 | 12 |
| `0x0050941C`, transition | 0 | 0 |
| `0x00509428`, local enabled | 1 | 1 |
| `0x0050AF68`, pause state | 2 | 0 |

The framebuffer confirms Link plays the ocarina but no performance UI appears.
The current `ReadTopScreenWorldMapGeometry` reads that same page's arrays from
`0x005093E4`, including song/note tables, yet requires nonzero pause state.
`BuildTopScreenPresentation` calls it only as `UiSubsystem::Map`. Therefore
existing geometry cannot appear during free play. The inherited `PauseWorldMap`
names in the semantic inventory must not be treated as authoritative ownership.

Official `0x005D2078` identifies the source arrays unambiguously:
positions `0x0050944C`, sizes `0x005097AC`, UV extents `0x00509B0C`, origins
`0x00509E6C`, texture resource slot 5 (`OcarinaPage`). Its six band quads use
source indices **0, 1, 2, 35, 36, 37**, not the first six entries as in the current
reader. Literal pools confirm X offset 40, staff Y offset 168 and banner offset
**-182**; the PR's -196 is not that literal. Subsequent loops use the selected
song and its native note/learned-song tables; these are not world-map markers.

Next implementation, at the existing title UI adapter:

1. Extract a correctly named ocarina owner/geometry module, preserving actual
   scene/result/page/transition gates and the mod's free-play/browser distinction.
2. Reuse native resource slot 5 and typed UI primitives for the verified bands,
   played/selected notes and text. Publish at the UI composition boundary.
3. Correct the mistaken Map-only lifecycle association and source indices;
   keep unrelated map rendering and HUD visibility unchanged.
4. For song browsing, read native cursor and learned-song tables. Do not use
   repeated synthetic taps to guess twelve possible tiles as proposed in PR #18.
5. Verify idle, free play, learned-song browsing, cancellation and completed-song
   transitions from the original-save fixture; inspect framebuffer plus state.

Do not merge PR #18's raw replay filter as-is: texture dimensions/index counts
are not sufficient UI ownership, its vertex reads need bounds/format checks,
and copying lower-screen draw batches is unnecessary where the native producer
and resource identity are already known. No PICA/NRI special case is needed.
