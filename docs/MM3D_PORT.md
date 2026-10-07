# Majora's Mask 3D port

Fork of the OoT3D TriAevum line (`qual/windows-next` at `3eb16f1`, remote
`oot3d`, push disabled) to bring The Legend of Zelda: Majora's Mask 3D to the
same runtime, reusing the title-neutral layers: CTR host services, A32
runtime and whole-AOT pipeline, PICA frontend, NRI renderer and extension
graph, F1, packaging and Forge. Improvements to those layers are shared with
the OoT3D repository by cherry-pick. The same rules apply as in `AGENTS.md`:
no ROM, code.bin, RomFS, savestates or generated title code in Git.

## Title

EUR cartridge image, decrypted (NoCrypto): product `CTR-P-AJRP`, program
`0004000000125600`, NCCH version 2, ExHeader name `Joker`, compressed code.
Decompressed code.bin SHA-256
`6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d`; text
`0x00100000` (0x51CFB4 bytes), rodata `0x0061D000`, data `0x0064B000`, BSS to
`0x007CF000`. Same Grezzo engine generation as OoT3D.

## N64 evidence

As OoT N64 was for OoT3D ([offline semantic source](OOT3D_OFFLINE_SEMANTIC_SOURCE.md),
[rehosting plan](OOT3D_SEMANTIC_REHOSTING_PLAN.md)), Majora's Mask N64 is
read-only, offline evidence: the US ROM (SHA-1
`d6133ace5afaa0882cf214cf88daba39e266c078`, the baserom `zeldaret/mm`
expects), the `zeldaret/mm` decompilation (`56fa21dd0031a17cfc9e355f609542617598a265`)
and the 2Ship2Harkinian PC port (`e8757c14a0fc8701461b0458c9ed72c118bcfc67`),
downloaded at those commits outside the repository (`W/external`, with
`provenance.txt`). They name and explain MM3D's
code (actor, scene, save-context and message structures, game states, the
three-day clock, mask transformations, event flags) and describe the intent
of events, entrances and progression. They never supply MM3D values: setup
and actor lists, parameters, transforms, camera, timing, assets and rendering
come from MM3D's code.bin and RomFS, which win every conflict. No N64 source
or asset is copied into the repository or the runtime.

## Pipeline (reproducible, outside the repository)

Work directory `I:/_triaevum_work/mm3d-port` (`W` below).

1. Extract with Forge's title-neutral extractor (`tools/triaevum_release/ctr_rom.py`):
   ExHeader, decompressed code.bin, RomFS image into `W/extract`.
2. Process manifest: `tools/oot3d/native_a32_runtime/build_process_manifest.py
   --exheader --code-bin --romfs-image --output W/manifest/process-manifest.json`.
3. Function inventory: `tools/ctr_title/ghidra_inventory.py` (headless Ghidra,
   code-set layout from the manifest, `ghidra_scripts/ImportCtrCodeSet.java` and
   `ExportFunctionInventory.java`), then `tools/ctr_title/normalize_inventory.py`
   (contiguous disjoint word-aligned intervals in text). 12,652 functions found,
   12,631 kept (18 Thumb entries and 3 outside text dropped), 80.9% of text.
4. A32 registry: `generate_aot.py` with `--code --exheader --inventory
   --boundary-audit --supplemental-entries --true-aot-blocks` (the last three
   from `tools/mm3d/a32/`, empty for now). 1,058,521 of 1,084,865 slots reachable.
5. Whole-AOT program: `generate_whole_aot.py` with the same title inputs:
   17,753 functions, 237,599 blocks, 1,245,442 instructions, none unclaimed.
6. Selection: `select_profile_whole_aot.py --all-lowerable --code` from the
   empty profile/selection in `tools/mm3d/a32/`: 16,522 functions (1,063
   lowering rejections, 905 ownership conflicts).
7. C++ shards: `tools/ctr_title/generate_whole_aot_cpp.py` (256 shards).

The generator wrappers keep OoT3D's inputs as defaults, so the OoT3D pipeline
is unchanged.

## Runtime title seam

`triaevum_title_adapter.h` resolves the adapter from the process image's
code.bin hash. Only OoT3D EUR gets the OoT3D adapter: guest block hooks,
composition and UI-lifecycle hooks, hand-written compiled functions, typed
gameplay, mass/true-AOT blocks and the TopScreen profile (refused for other
titles). Any other title runs on the CTR host, the A32 runtime with its own
whole AOT, and the canonical renderer. Stderr reports `TRIAEVUM_TITLE_ADAPTER`.

## Next steps

1. MM3D host build in its own directory with `OOT3D_A32_AOT_GENERATED_DIR`
   on the MM3D registry (the executable links the A32 registry statically),
   and the MM3D title module from the C++ shards
   (`tools/renderer/tev_program/build_aot_header_experiment.py` with the
   build's `triaevum_title_whole_aot_support.lib`).
2. First boot with `--ui-profile oot3d` and the Authentic preset: record the
   CTR services and SVCs MM3D needs beyond OoT3D's, runtime-discovered entries
   (fed back through `tools/mm3d/a32/runtime_discovered_entries.csv`), and
   remaining per-frame reads of OoT3D addresses outside the hooks.
3. Title screen, then the MM3D adapter: camera/projection and composition
   metadata for the extension effects, frame pacing, HUD.
