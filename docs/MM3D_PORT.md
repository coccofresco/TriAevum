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

The code is build 1.1.0 (`joker-1_1_0` source paths in rodata). The USA
v1.1 card (`CTR-P-AJRE`, program `0004000000125500`, partition-0 SHA-256
`9c756279e3ec00bb9009f92ca2e7b9ec8b1fcdfcd9de057658b209caa24323da`, the dump
dekui requires) carries a byte-identical code.bin; the two ExHeaders differ
only in the program ID and access descriptor. Region lives in the RomFS:
EUR has `layout/EU_<Language>/` and `message/eu/eu?.gmsg` (English, French,
German, Italian, Spanish; Dutch layouts are present but empty), USA has
`US_*` and `message/us/` (English, French, Spanish). The title adapter is
therefore keyed by the code hash (`mm3d`), and the region by program ID and
RomFS. MM3D's code contains Thumb (223 functions, about 21 KB, mostly in the
SDK runtime at `0x00100000-0x00103600` and `0x002FF000-0x00303000`) entered
from the process entry by `BLX`; OoT3D's contains none.

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

## Single-screen experience (TopScreen equivalent)

`tgacquin/mm3d-dekui` (GPL-2.0, documentation at
`a49554f0c3c797f165539e3170ff671bb463569c`; source archive of release
v1.0.0-beta.1, SHA-256 `E39E374B…3E6281`, in `W/external/dekui-source`) is
the reference for MM3D's single-screen mode, as the TopScreen mod was for
OoT3D: gear, masks, items and map, the Bomber's Notebook, song and give-item
menus, file select and options on one screen; minimap; console-like HUD and
text boxes; widescreen and camera distance. It is an Azahar fork
(`src/mm3d`, about 15,800 lines) over bundled Project Restoration
(`leoetlino/project-restoration` at `8bb2a7b8`, source in
`resources/mm3d/project-restoration`), whose `source/game/*.h` describes
MM3D's actor, context, camera, item, message and input structures.

dekui accepts only the USA v1.1 card and runs Project Restoration's `v110`
build (its `code.bps` names source CRC32 `ffdf1ee8`, the CRC32 of the code.bin
above). Because EUR and USA v1.1 share that code.bin, their code addresses,
function knowledge and structure layouts hold for the EUR title this port
runs (maintainer's choice, for its languages including Italian). Their
RomFS-side work does not transfer as is: dekui overrides USA paths
(`mods/0004000000125500/romfs/layout/...`), while EUR's localized layouts and
messages live under `EU_<Language>` and `message/eu`. Behaviours are
re-implemented as typed source contracts in the MM3D adapter, never by
applying their binary patches.

## Pipeline (reproducible, outside the repository)

Work directory `I:/_triaevum_work/mm3d-port` (`W` below).

1. Extract with Forge's title-neutral extractor (`tools/triaevum_release/ctr_rom.py`):
   ExHeader, decompressed code.bin, RomFS image into `W/extract`.
2. Process manifest: `tools/oot3d/native_a32_runtime/build_process_manifest.py
   --exheader --code-bin --romfs-image --output W/manifest/process-manifest.json`.
3. Function inventory: `tools/ctr_title/ghidra_inventory.py` (headless Ghidra,
   code-set layout from the manifest, `ghidra_scripts/ImportCtrCodeSet.java` and
   `ExportFunctionInventory.java`), then `tools/ctr_title/normalize_inventory.py`
   (contiguous disjoint word-aligned intervals in text). A first pass found
   12,652 functions, 80.9% of text.
   `normalize_inventory.py --thumb-ranges` drops the functions inside Ghidra's
   Thumb ranges (`ghidra_scripts/ExportThumbRanges.java`) and cuts A32
   intervals at them: the generators decode only A32, the runtime executes
   Thumb itself. The C++ static constructors are reached only through a
   self-relative table that Ghidra cannot follow: the Thumb initializer loop at
   `0x001009E0` walks `0x0064AAE8-0x0064AD28` (144 entries, entry = word +
   its address). `tools/ctr_title/init_array_entries.py` lists them and
   `ghidra_scripts/CreateFunctionsAt.java` seeds them before analysis, which
   then follows their callees: 13,827 functions, 13,601 kept (223 Thumb
   functions, 21.3 KB), 87.3% of text.
4. A32 registry: `generate_aot.py` with `--code --exheader --inventory
   --boundary-audit --supplemental-entries --true-aot-blocks` (the last three
   from `tools/mm3d/a32/`, empty for now). 1,143,835 of 1,170,061 slots reachable;
   6 unsupported operations left, all data words decoded as code.
5. Whole-AOT program (from the first inventory; not yet regenerated):
   `generate_whole_aot.py` with the same title inputs: 17,753 functions, 237,599 blocks, 1,245,442 instructions, none unclaimed.
6. Selection: `select_profile_whole_aot.py --all-lowerable --code` from the
   empty profile/selection in `tools/mm3d/a32/`: 16,522 functions (1,063
   lowering rejections, 905 ownership conflicts).
7. C++ shards: `tools/ctr_title/generate_whole_aot_cpp.py` (256 shards).

The generator wrappers keep OoT3D's inputs as defaults, so the OoT3D pipeline
is unchanged.

## Runtime title seam

`triaevum_title_adapter.h` resolves the adapter from the process image's
code.bin hash: `oot3d_eur`, `mm3d` or `generic`. Only OoT3D EUR gets the
OoT3D adapter: guest block hooks, composition and UI-lifecycle hooks,
hand-written compiled functions, typed gameplay, mass/true-AOT blocks and the
TopScreen profile (refused for other titles). MM3D's adapter so far only
selects its RomFS language layout (`DetectGameLanguages`, also reachable as
`TriAevum --game-language-info ROMFS CODE_SHA256`). Other titles run on the
CTR host, the A32 runtime with their own whole AOT, and the canonical
renderer. Stderr reports `TRIAEVUM_TITLE_ADAPTER`.

## Bring-up status

A product-mode build stops at the first instruction: `0x00100000` (the CRT
entry) was rejected by whole-AOT lowering because `0x00100010` is `BLX` into
Thumb (`0x001009E0`), and product mode has no residual path. Bring-up
therefore uses a development build (no product mode, no title module): the
MM3D A32 registry, the A32 core and the Thumb interpreter.

What MM3D needed beyond OoT3D, all title-neutral:

- Thumb (`tools/oot3d/native_a32_runtime/a32_thumb.cpp`): ARMv6K Thumb-1 with
  the BL/BLX pairs, executed from guest memory. Thumb state is an odd PC, the
  interworking form BX/BLX/POP/LDR already produce; the A32 runtime runs
  `BLX #imm` and sends odd PCs to the Thumb interpreter. OoT3D has no Thumb
  code.
- ARMv6 SIMD media instructions in the A32 core: parallel add/subtract with
  the GE flags, SEL, SSAT16/USAT16, SXTAB16/UXTAB16 and SMLAD/SMLSD/SMUAD/
  SMUSD (540 such operations in MM3D's reachable code; OoT3D only used
  UQSUB8), and MRS/MSR APSR classified as core operations.
- CTR kernel: `QueryMemory` over typed kernel memory states,
  `GetThreadId` (nonzero kernel IDs), `WaitSynchronization1` (zero and
  infinite timeouts), VBlanks before the GSP relay queue is registered, and
  `SleepThread(0)` as a yield that passes the CPU to the next ready thread of
  the same priority (MM3D has 20 explicit yields, OoT3D none; without it the
  main thread starved an equal-priority worker).
- Services: `APT:A` (MM3D's ExHeader grants APT:A, not APT:U),
  `APT:GlanceParameter`, `APT:Set/GetScreenCapPostPermission`,
  `ndm:u` Suspend/ResumeDaemons and Override/Reset/GetDefaultDaemons,
  `fs:USER` InitializeWithSdkVersion and Set/GetPriority, `dsp::DSP`
  Flush/InvalidateDataCache.
- A use-after-free in GSP `RegisterInterruptRelayQueue`: the event's handle
  record was read after `CreateHandle` could reallocate the handle table.

Current state (developer launch, development build, 1800 frames): eight
guest threads; APT, NDM, FS, GSP (hardware registers, command queue), HID,
CFG and DSP (component loaded, audio frames processed) initialize; the first
RomFS archives load; 2 PICA draws and a display transfer complete; the LCD
stays force-black. The main thread then loops once per VBlank (GSP
interrupt thread, worker thread, address arbiter) while the byte at
`0x007B2711` stays set: the next step is finding which subsystem should clear
it. With `--extended-diagnostics` the host keeps the full SVC history and
runtime.json lists the last 4096 events; `TRIAEVUM_DIAGNOSE_GUEST_WAIT`
reports every thread's priority, status and registers when the guest does
not reach a wait.

## Next steps

1. Continue the boot to the title screen, one missing kernel or service
   behaviour at a time, each tested at the CTR host boundary.
2. Thumb lowering in whole AOT and the MM3D title module from the current
   inventory, for a zero-residual product.
3. Function discovery from data pointers (vtables, callbacks), as
   `fearkov/3dsrecomp` does, before indirect calls reach unseen code.
4. MM3D adapter: camera/projection and composition metadata for the
   extension effects, frame pacing, HUD; then the single-screen mode from
   dekui and Project Restoration.
