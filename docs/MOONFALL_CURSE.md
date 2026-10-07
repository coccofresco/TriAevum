# TriAevum — Moonfall Curse

TriAevum — Moonfall Curse is the Majora's Mask 3D project of TriAevum
(repository `I:/TriAevum-MoonfallCurse`, branch `moonfall-curse/main`). The
runtime shows that name for this title (`TitleProductName` in
`triaevum_title_adapter.h`; the log file name keeps ASCII,
`TriAevum - Moonfall Curse.log`). Code and documents keep `mm3d` for the
title itself: the adapter, the work directory and the inventory profile.

It is a fork of the OoT3D TriAevum line (`qual/windows-next` at `3eb16f1`,
remote `oot3d`, push disabled) that brings The Legend of Zelda: Majora's
Mask 3D to the same runtime, reusing the title-neutral layers: CTR host services, A32
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
   Virtual and callback tables name functions Ghidra never reaches either.
   `tools/ctr_title/data_pointer_entries.py` scans code.bin for words naming a
   word-aligned text address outside the inventory, keeps those that decode
   as a function (valid A32 up to a return or tail branch) and sit at a
   function boundary (after a return, padding or a literal pool), and writes
   seeds for `CreateFunctionsAt.java`; repeated until it finds nothing new.
   Two passes (3,497 then 37 seeds, 3,479 created) give 17,335 functions,
   17,108 kept, 92.5% of text. With `--interior-entries` it also lists the
   data pointers naming an address inside a function (a second entry, such as
   a `this`-adjusting virtual method falling into the next): 448, passed to
   the A32 generator as `--supplemental-entries`.
4. A32 registry: `generate_aot.py` with `--code --exheader --inventory
   --boundary-audit --supplemental-entries --true-aot-blocks` (boundary audit
   and true-AOT blocks from `tools/mm3d/a32/`, empty for now; supplemental
   entries are the interior entries above). 17,556 callable entries,
   1,213,315 of 1,240,404 slots reachable; 6 unsupported operations left, all
   data words decoded as code.
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
  `fs:USER` InitializeWithSdkVersion and Set/GetPriority, file sessions'
  OpenLinkFile and Set/GetPriority, `dsp::DSP` Flush/InvalidateDataCache.
- A use-after-free in GSP `RegisterInterruptRelayQueue`: the event's handle
  record was read after `CreateHandle` could reallocate the handle table.
- PICA command buffer jumps (`GPUREG_CMDBUF_SIZE/ADDR/JUMP`, registers
  `0x238`-`0x23D`): MM3D's GX command lists are short heads that set channel 0
  and 1 and jump, so the draws and the closing IRQ request live in the jumped
  buffers. The PICA frontend now continues in the jumped buffer (the rest of
  the jumping buffer is not executed), read through the PICA physical memory
  view by the window and by the module PICA host. OoT3D never jumps.
- Resolution: the top-screen target was recognized only at OoT3D's exact
  480x400; MM3D pads it to 512 (480-sample viewport), so it stayed at native
  size and was only magnified. A top-screen target is now 400 lines by 480 to
  512 samples, its padding scaled at the visible samples' density. A title
  whose adapter does not widen the projection (all but OoT3D, so far) keeps
  the native 400:240 view fitted inside the output (letterboxed) instead of
  being stretched. At the presented density only the line axis collapses the
  guest's 2:1 filter; MM3D's 2x2 reduced copies of the frame halve the other
  axis.
- A device lost at any internal scale above native (also with the original
  code): the memory fill's shadow clear was recorded inside the active
  swapchain/overlay render pass (invalid; on NVIDIA it wrote past another
  image once targets were scaled). It now ends that pass first, as the
  display transfer already did. Found with the Vulkan SDK's crash diagnostic
  layer.
- Startup shader preparation: the startup program family (OoT3D's vertex
  programs and effect supersets) is prepared only for the OoT3D adapter.
  Another title's draws never match it, so preparing it only held MM3D's
  guest for 83 s at startup; MM3D's draws prepare their own programs.

Current state (developer launch, development build, 5400 frames): eight
guest threads; APT, NDM, FS, GSP, HID, CFG and DSP initialize, RomFS archives
load, and the attract sequence plays: the intro (Majora's Mask, the Happy
Mask Salesman), then the Clock Town demo scenes. 333,842 PICA draws, 4,854
display transfers, no fault. The renderer runs MM3D's draws on the
canonical path.

F1 and the optional effects: the window is **Moonfall Curse Graphics**. The
title frontend sets two title-owned capabilities from the adapter:
`TitleSceneView` (the title publishes its frustum, camera and scene) and
`TitleSceneFov` (the title applies the global scene FOV). MM3D publishes no
scene view yet, so F1 says that grass, shadows, ambient occlusion,
reflections and GI stay inactive (their settings are kept); OoT3D's
per-scene lights are not installed for MM3D. The effects were already inert: with the maintainer's play configuration (Modern
lighting, virtual and cube shadows, MXAO, grass, FOV 1.10) the MM3D frames
are bit-identical to the same run with every effect off, at frames 1800 and
2400 (`--fixed-delta-seconds 1/60`, an empty `--input-timeline`; without
them MM3D's runs are not reproducible). Frame interpolation (Interpolated
2x/3x) is the renderer's own and runs for MM3D. Actual-widget smoke: 8,444
assertions, including the title without scene hooks.

Widescreen and scene FOV: the adapter hooks MM3D's perspective frustum
builder, `0x00122FE8` (s0..s5 = left, right, bottom, top, near, far; the
engine routine of OoT3D's `0x002FDE9C` without its rotation argument, found
by its instruction pattern; its first word is checked at startup). The same
presentation policy as OoT3D's widens each frustum to the output aspect
(horizontally for wider outputs, vertically for narrower ones) and applies
**Global scene FOV**; the top-screen target then fills the output. Its
frusta are not published as the scene view, since they also serve views
other than the scene camera. At 1280x720 the native part of the frame is
unchanged (PSNR 88 dB against the letterboxed run over the same columns)
and the sides show more of the scene; 1600x1200 extends vertically; FOV
1.25 and 1.5 widen the view. Not yet checked: 2D drawn on the top screen
during gameplay (the HUD), which has no composition metadata and so is
scaled with the scene.

Earlier stalls, in order. First the main thread waited for byte
`0x007B2711` (the busy flag of the GX command scheduler at `0x007B2700`)
because of the missing jumps above: the scheduler counts one completion per
command (GSP handler table at
`0x0011BB48`: PSC0, PSC1, VBlank0, VBlank1, PPF, P3D, DMA) and two never
arrived. Then `File:OpenLinkFile` and `File:SetPriority` on the RomFS file,
then calls through vtables into functions (and second entries) the
inventory lacked, fixed by the data-pointer discovery above.

Diagnostics: with `--extended-diagnostics` the host keeps the full SVC
history, runtime.json lists the last 4096 events and GSP command lists of up
to 16 words show their words. `TRIAEVUM_PROBE_GUEST_RANGES=ADDR:SIZE[,...]`
(hex) records guest words at exit in runtime.json `guest_memory_probe`;
`TRIAEVUM_DIAGNOSE_GUEST_WAIT` reports every thread's priority, status and
registers when the guest does not reach a wait.

## F1 effects: native composition (2026-10-07)

The optional effects run on MM3D once the renderer knows which draws are the
opaque world, translucency, atmosphere and UI, and the scene's projection.

How MM3D draws a frame (recovered by tracing one frame's function entries
against the command-list cursor and the PICA draws, and read against
`Play_Draw` in the N64 decompilation, `zeldaret/mm` src/code/z_play.c):

- Commands go through the cursor at `0x006A3A88` (limit `0x006A3A8C`) into
  the bound command list; `0x001369F4` binds a list by id.
- `0x001109F8` draws the frame: it binds list 9 and draws the sky
  (`0x005B51D0`, Skybox_Draw's place), binds list 7 and draws the scene:
  `0x0011EF14` visits the opaque render list (scene+4) and the translucent
  one (scene+4+0x408) with `0x005EF054`; `0x0011EF60` visits both again with
  another visitor (blended, no depth writes); `0x0011EFAC` draws the 2D
  scene. Layouts are drawn by `0x001262E0` (from `0x001267E8`).
- Prebuilt model buffers are called from the scene list through
  `GPUREG_CMDBUF_JUMP` and jump back just past the call.

`mm3d_pica_composition.{h,cpp}` records the command bytes each pass writes
(its entry hook with the return address, then the return) and offers them
with every submission (the GPU runs a list one guest frame after it is
built; ranges expire after two scene frames or when rewritten). Title-
neutral changes: the frontend keeps the stack of buffer calls made through
jumps, and a draw takes the span of the innermost of its own place and its
calling jumps; spans may lie outside the submitted list; and a memory fill
over a target's whole color buffer restarts that target's composition
(`PicaCompositionTargetReset`), since MM3D draws a layout into the top
buffer and clears it before the scene.

Projection and camera: the hooked frustum builder makes the stereo
camera's constant base frustum (object `0x006B35D4`), not the scene's field
of view. The scene camera is the current View (N64 View layout): the game's
getter `0x001DD0C4` returns the pointer at `0x00656B5C` (set by
`0x00170514`; the play state's View on the heap during gameplay) or, while
it is null, the static View at `0x0072F5D0`, which View_Init
(`0x001393E0`) leaves at fovy 60, near 5, far 12800 and a placeholder eye
(60000, ...). The scene view is published from the current View's fovy,
clip planes, eye and target, at the aspect and widening of the hooked
frustum; the default View publishes no camera. Found by diffing guest
memory between savestates for (at, eye, up) triples whose up is
perpendicular to the view direction. In the attract demo the camera moves
with the cutscene (fovy 45, then 35 indoors).

The draws' own projection (GzwShader `uProjection`, c0..c3) does not follow
the View's fovy (1.22 x 2.17 in both demo scenes, with FOV x1.1); the
renderer applies it to the published view on frames that draw.

Scene lighting. MM3D's vertex shader is `rom:/shaders/GzwShader.shbin`
(RomFS), not OoT3D's `CmbVShader`. Its symbol table: uProjection c0..3,
uModelView c4..7, MatDiffuseColor c8, MatAmbientColor c9, TexMtx0..2
c10..19, uMatrixPallet c20..49, uInvView c70..73, GlobalAmbient c74,
LightEnabled c75 (one component per light), LightType c76, then four
lights of LightDir/LightDiffuseColor/LightAmbientColor at c77, c80, c83,
c86; TexCoordSlot/ShaderMode c89, attribute scales c90..91,
TexMappingMethod c92; booleans b0..b10 (HasTangent, IsTexture,
IsSkinning, IsSmoothSkinning, IsOrthoStereo, HasColor, HasTexcoord0..2,
IsVertexLighting, IsFragmentLighting). Its outputs include the
normal quaternion (o5) and the view vector (o6) for PICA fragment
lighting. Every opaque scene draw of the demo lights per pixel with two
directional fragment lights, the environment's two lights (N64 MM's
envCtx.dirLight1/dirLight2): the LightInfo records at play + 0x86D0 and
play + 0x86E8, linked in the LightContext list at play + 0xA88 (Play_Init,
`0x0012AA40`, calls View_Init on play + 0x1F8). The MM3D composition
tracker attaches to the world passes' spans the current View's
world-to-view (View + 0x17C) and those lights. Verified against the GPU:
the fragment light registers are these directions in view space and these
colors. In the attract demo the light is horizontal ((1, 0, 0)), so by the
horizon rule no shadow is cast; with the world-to-view the temporal
history is now valid (it reset every frame before) and the directional
shadow pass renders its map from the native light.

Shadow receivers: the frontend now recognizes GzwShader's four-light block
(`DecodeFourLightSurface`; see "Four-light vertex programs and native
fragment-lit receivers" in OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md).
The scene draws, lit by native fragment lights, keep their native per-pixel
lighting; the environment lights they already apply are not added again,
and the directional shadow darkens the matching native light. No MM3D draw
of the demo lights per vertex (b9 never set); one that did would stay
native.

Result (developer launch, Clock Town attract demo): 96 opaque, 8
translucent and 4 sky draws classified; ambient occlusion composes on the
scene (without the streaks the stereo frustum gave), directional shadows
prepare their 96 casters, and the 96 opaque draws receive the shadow
(none is cast in the demo: its light is horizontal). Not yet: grass sources
for MM3D's textures, the HUD in gameplay, a daytime gameplay scene to see
the shadows at native light.

Bring-up tooling: `TRIAEVUM_A32_CALL_TRACE=PATH,FIRST,LAST[,WORD[,ENTRIES]]`
writes a record per observed block entry (pc, lr, sp, r0-r3, thread, a
guest word such as the cursor); with an ENTRIES file of hex function
entries the compiled code reports them as hooks. `tools/ctr_title/
match_functions.py` pairs functions of two titles by normalized body and
call graph (OoT3D to MM3D: few pairs; the engines diverged).

## Next steps

1. Continue past the intro to the title screen and file select, one missing
   kernel, service or PICA behaviour at a time, each tested at its boundary.
2. Thumb lowering in whole AOT and the MM3D title module from the current
   inventory, for a zero-residual product.
3. Function discovery from code pointers built at run time (ADR/ADD-PC
   sequences), the remaining 7.5% of text, before indirect calls reach it.
4. MM3D adapter: camera/projection and composition metadata for the
   extension effects, frame pacing, HUD; then the single-screen mode from
   dekui and Project Restoration.
