# TopScreen Practice Save Fixtures

## Inputs And Isolation

User-provided archives, received 2026-09-10. Originals remain unchanged in
`C:/Users/xander/Downloads/`; extracted copies and all captures are private,
outside the repository and release inputs.

| Archive | SHA-256 |
| --- | --- |
| `SavepracticeAD_d5065f7b.zip` | `0be202f08b58921e5c289630d796877ed4a6a4d0f433a12a4c9a86b4dce885eb` |
| `100_Practice_Saves_xiknl.zip` | `df7c1a1bd3139d7a7272c55b118459b7c11a6eea5e7b4cbb33739f243deec5ae` |

Resource attribution: [All dungeon practice saves, stevste](https://www.speedrun.com/oot3d/resources/qk1e3)
and [100% Practice, erinexplosives](https://www.speedrun.com/oot3d/resources/xiknl).
No permission to redistribute these archives is inferred.

Private root: `J:/TriAevum-verify-20260910/`.
Extracted roots: `fixtures/practiceAD-original/` and
`fixtures/practice100-original/`. There are 20 `save*.bin` entries, each
5,340 bytes: eight in AD and twelve in 100% Practice. This is an entry count,
not a claim of twenty distinct playable checkpoints. AD has no `system.dat`;
the four 100% Practice groups each include one. Import complete groups into
isolated save directories, never over the player's active save directory.

## Fixture Selection

Inspect the save itself rather than relying on its folder name: AD Adult's
`save03.bin` actually contains child state. Read-only inspection uses entrance
at +0, age at +4 (0 adult, 1 child), quest flags at +0xBC and ocarina at +0x93.
These fields are for selecting tests, not constructing fictional progress.

| Group / file | Age | Entrance | Quest flags | Ocarina |
| --- | --- | --- | --- | --- |
| AD practice Child / save00.bin | Child | 00BB | 00001000 | 07 |
| AD practice Child / save01.bin | Child | 010E | 00141000 | 07 |
| AD practice Child / save02.bin | Child | 02CA | 00141821 | 07 |
| 100% Practice (Adult 1-1) / save00.bin | Adult | 05F4 | 109DD620 | 08 |
| 100% Practice (Adult 1-1) / save01.bin | Adult | 04BE | 209FD7E0 | 08 |

AD Child/save00 is the preferred first-time learning candidate: it owns the
ocarina but only Zelda's Lullaby among the song flags. This avoids using the
completion fixture, which cannot qualify first-time learning.

## Actual Runtime Results

Tests use `tools/triaevum_release/probe_renderer.py`, isolated save seeds,
native 30 Hz with interpolation disabled, bounded runs and framebuffer
captures. Each output directory contains `invocation.json`, configuration,
logs and the exact input paths required to reproduce it.

| Private output directory | Result |
| --- | --- |
| `practice-child-boot` | Original AD Child group accepted by the native file menu; 1,500-frame run completed, checkpoint at 1,400. Link visible in his house. |
| `practice-tomb` | Native transition from that checkpoint to `hakaana_ouke_info_entry_002f`; 650-frame run completed, checkpoint at 620. |
| `practice-tomb-forward` | Ordinary movement reaches the barred door in the first tomb room, not the song tablet. |
| `practice-tomb-turn` | Reverse movement exits into the graveyard; not a learning test. |
| `practice-tomb-cutscene` | Rejected setup experiment: changing the cutscene selector in a cloned checkpoint before transition faults at 00371788. Do not reuse or count as coverage. |
| `practice-meadow` | Native entrance 00FC reached, 650 frames completed, checkpoint at 620; starts at the normal meadow entrance with a barred path, not beside Saria. |
| `practice-ocarina-opacity` | Rebuilt runtime, 700 frames completed from the meadow checkpoint using `topscreen_ocarina_recognition.json`; guide and native note staff visible. Frame 160 confirms Zelda's Lullaby text after the transitional empty frame 120. Not first-time learning qualification. |
| `ocarina-partial-progress` | 540 frames completed; learned title/notes return after browsing an unknown song. Exposes wrong texture binding for the unknown marker. |
| `ocarina-partial-progress-fixed` | 540 frames completed; unknown marker now visible. Four unknown-state captures differ only over the marker; four checked learned/gameplay captures match the pre-fix run exactly. |
| `tomb-native-transition-setup1` | Rejected: selector FFF1 applied at accepted transition still faults at 00371788. Experimental consumer removed. |

At this stage no first-time learning sequence had been qualified; the
successful windmill follow-up below supersedes that coverage status.
The failed setup experiments are not justification to suppress a native fault
or skip game logic. Moving the selector write to the native transition
boundary did not resolve the fault; additional preconditions remain unknown.

The isolated Azahar reference preparation did not produce a usable movie or
capture: bounded launches timed out and their processes were terminated.
Thus these results are native-runtime fixture qualification, not paired
Azahar visual parity. Matching original TopScreen captures remain pending.

## First-Time Song of Storms Qualification (2026-09-11)

Windows Vulkan/NRI runtime at commit `a52fcf4` completed genuine native
teaching, player repetition and reward processing. No production change was
needed for this sequence. This qualifies one learning interaction, not all
ocarina states or cross-platform visual parity.

Start with the complete original `100% Practice (Adult 1-1)` save group and
select file 0 using `title_to_existing_save.json`. `practice-adult-boot`
completed 1,450 frames, checkpoint 1,400. `practice-windmill` then used the
existing native entrance recipe `hakasitarelay_info_entry_0453` (scene 72,
local entrance 1), completing 650 frames, checkpoint 620. The private
`windmill-entrance-probe.json` retains the native transition contract; it does
not inject a cutscene selector, inventory or event flags.

**Setup exception, not gameplay coverage:** normal approach probes reached a
pillar or free play instead of the musician. In a private checkpoint clone,
only Link's world position was moved to `(3110,-127,34)`, 75 units in front of
the native EnFu position `(3185,-127,34)`. The native distance gate is 100.
For this exact snapshot, player `098F4010` has position at `+28`; use
`patch_native_a32_savestate_memory.py` with write
`0x98f4038:006042450000fec200000842`. Do not reuse that pointer in another
snapshot. The clone is `fixtures/windmill-front-position.oot3dsav`.
The original save, inventory and quest/event progress were not edited.

Reproduction stages, under private root `J:/TriAevum-verify-20260910/`:

| Output / input | Result |
| --- | --- |
| `windmill-lesson-start`, `windmill-lesson-start.json` | Down at 45; A at 160/260/360, each held four frames. 750 frames, checkpoint 700. NPC starts native lesson dialogue. |
| `windmill-lesson-demo`, `windmill-lesson-dialogue.json` | From previous checkpoint, A at 30/130/230/330. 900 frames, checkpoint 850. Frame 870 shows native repetition staff with L,R,A,L,R,A. |
| `windmill-lesson-complete`, repository `topscreen_storms_first_learning.json` | From previous checkpoint, native note inputs followed by dialogue advances. 1,100 frames, checkpoint 1,050, exit 0. Storm effect and subsequent NPC dialogue visible. |

All stages use native 30 Hz, interpolation disabled, isolated saves, and
runtime framebuffer captures, not desktop captures. Invocation manifests
record the executable hash, renderer options, shader pack and input paths.
The timeline starts at player repetition, not the beginning of the lesson.

`verify_storms_learning.py BEFORE AFTER` compares the `practice-windmill` and
`windmill-lesson-complete` checkpoints without writing either:

| EUR baseline field | Before | After |
| --- | --- | --- |
| Quest flags `00587A14` | `109DD620` | `109FD620` |
| Lesson gate event `0058884E` | `0600` | `0E00` |
| Completion event `00588850` | `8000` | `8020` |

All five checks pass: song initially absent, subsequently present, only the
expected quest bit changed, and completion event changed from clear to set.
The verifier is deliberately fixture-specific, not a generic save validator.
Addresses refer to the EUR baseline only; checksum verification is skipped
for these locally generated trusted checkpoints, as in other state analyses.

Read-only native evidence: `EnFu_WaitAdult` at `001426F8` starts message 5035
when the player offers the ocarina; `EnFu_TeachSong` at `0015F6C8` and
`EnFu_WaitForPlayback` at `0015FB80` lead to action `00104720`.
That action awards `Item_Give(0x65)` and sets event `00588758+F8` bit 20.
The native song table gives Storms quest bit `00020000` and notes
`0,1,4,0,1,4`, displayed as L,R,A,L,R,A. These are checks of native outcomes,
not replacement implementations of the lesson or reward logic.
