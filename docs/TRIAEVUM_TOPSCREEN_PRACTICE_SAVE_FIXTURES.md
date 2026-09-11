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

No first-time learning sequence has yet been qualified with these fixtures.
The failed setup experiments are not justification to suppress a native fault
or skip game logic. Moving the selector write to the native transition
boundary did not resolve the fault; additional preconditions remain unknown.

The isolated Azahar reference preparation did not produce a usable movie or
capture: bounded launches timed out and their processes were terminated.
Thus these results are native-runtime fixture qualification, not paired
Azahar visual parity. Next: reach a genuine learning interaction from an
unmodified practice save and capture matching original TopScreen behavior.
