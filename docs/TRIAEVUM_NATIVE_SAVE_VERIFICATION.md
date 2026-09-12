# Native Save Verification

2026-09-10. Developer fixtures, not release content. Original OOT3D persistent
saves are read through the normal filesystem service and file-select flow.
They are not N64 saves, emulator snapshots, inventory patches or scene injection.

## Sources And Files

Downloaded [100% Save File](https://gamebanana.com/mods/670122), submitted by
MegaMarble, who credits the
[GBATemp USA completion pack](https://gbatemp.net/threads/3ds-100-completion-usa-savegames-pack-v1-1.514491/).
Archive: `ocarinaoftime_3ds_100.7z`, GameBanana download `1678647`.
Archive SHA256: `61dce5d6b499c638fc34ff1ffaaa993df4439d1847fb693d84fdef17e433ce5b`.

| Native file | Bytes | Purpose | Qualification |
| --- | ---: | --- | --- |
| `save00.bin` | 5340 | Regular quest, populated adult inventory | Loaded through original menu into Temple of Time |
| `save03.bin` | 5340 | Master Quest slot, as described by submitter | Staged; gameplay boot not tested |
| `system.dat` | 34 | Native system/save-selection data | Copied unchanged with the slot files |

Slot signatures match `ZELDAZ`; equipment/inventory offsets agree with the
[CloudModding format documentation](https://cloudmodding.com/zelda/oot3dsave).
No checksum, entrance, inventory, age or region bytes were patched. The guest's
own loader accepted the regular save and displayed adult Link, twenty hearts,
magic, 500 rupees, equipped boots/lens and populated Items-page graphics. This
USA-labelled save worked with the EUR title module. It does not prove universal
compatibility of every region, save revision or Master Quest configuration.

Also identified [erinexplosives' 12 practice saves](https://www.speedrun.com/oot3d/resources/xiknl).
The download received a Cloudflare challenge; that collection is **not acquired
or qualified**. No exploit saves or executable utilities were used.

## Private Locations

- Windows: `I:/oot3dre_work/pr13-native-save-fixtures/`.
- Untouched extracted originals: `complete/` under that directory.
- `provenance.json`: source, author credit, archive/file hashes and qualifications.
- `adult-temple-inventory.oot3dsav`: runtime checkpoint created after the original
  save loaded, not a downloaded emulator state.
- Linux: `/home/xander/triaevum-pipeline-live-proof/native-save-fixtures/`.
- Live evidence: sibling directories `native-save-boot`, `native-save-items`,
  `native-save-items-even`, `native-save-items-odd`.
- Linux checkpoint: `native-save-boot/checkpoint.oot3dsav`.

All payloads, framebuffer captures and checkpoints stay outside Git and releases.
Existing user saves/configurations were not overwritten. Native `.bin` files
remain the portable fixture; `.oot3dsav` is a convenience snapshot tied to the
runtime/title contracts and must not replace native-save compatibility checks.

## Reproduction

The shared Windows/Linux developer probe accepts a seed directory and hashes
the copied files in `OUTPUT/save-data-seed.json`. The guest can write only its
private copy. Links/special files, reusing a destination and nesting output in
the seed are rejected. Inherited checkpoint output paths are removed. Config,
TopScreen config, save data, cache and diagnostics remain isolated.

```sh
python3 tools/triaevum_release/probe_renderer.py INSTALL RUNTIME NEW_OUTPUT \
  --profile TriAevum.linux.launch.json --native-fidelity \
  --frames 1500 --seconds 120 --capture-interval 200 \
  --save-data-seed PRIVATE_ORIGINAL_FILES \
  --input-timeline tools/oot3d/native_game_runtime/input_timelines/title_to_existing_save.json \
  --save-state-frame 1400
```

Use the matching launcher profile on Windows. The checked-in input sequence
selects the first existing regular slot; it is not a general file-menu robot.
The observed Linux run exited normally after 1500 presentations and saved at
run frame 1400 / guest frame 2801. Checkpoint creation took about 1.07 seconds.
Inspect the framebuffer before accepting a new fixture or changing the profile.

For subsequent short gameplay checks, use `--load-state CHECKPOINT` together
with `--save-data-seed PRIVATE_ORIGINAL_FILES` and an explicit input timeline.
The probe removes inherited load-state/input automation unless explicitly
requested. The seed and checkpoint are never modified.

## Findings From The New Fixture

- Native boot and populated inventory rendering pass on Linux NRI Vulkan.
- A 600-presentation item probe from the new checkpoint exits normally but
  reports zero assignment begins/completions. Having items does not by itself
  make the remaining TopScreen assignment path work.
- A paired 400-presentation experiment offsets otherwise identical ZR/ZL/Start
  presses by **one guest refresh**, using the same checkpoint. Getter true
  counts `(I pressed, II pressed, I held, II held)` are `2/0/2/0` for the even
  sequence and `0/0/50/0` for the odd sequence. Both open the populated Items
  page; neither assigns an item through the new continuation hook.
- These counts prove phase-dependent behavior in this experiment, not an
  exactly-once contract or the cause of every missing input. The input owner
  currently recomputes edges at guest-refresh rate while the game consumes
  actions at its update cadence. Assignment observation may have a separate
  issue; it must be traced at its native call/return sites, not inferred fixed
  from getter counters.
- Retain both phase cases as regression fixtures. The next input change should
  establish the actual consumer lifetime, preserve native held/suppression
  semantics and verify the equipped slot changes plus subsequent item use.
  Do not silently extend every press for an arbitrary number of refreshes.

No Windows or Android gameplay qualification, Master Quest playthrough or FPS
claim is made by these runs. The four probe unit tests pass on Windows and Linux; the
Linux runs exercise actual product code and renderer.
