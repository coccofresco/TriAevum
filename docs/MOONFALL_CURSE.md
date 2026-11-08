# TriAevum — Moonfall Curse

TriAevum — Moonfall Curse is the Majora's Mask 3D project of TriAevum
(repository `<local>/TriAevum-MoonfallCurse`, branch `moonfall-curse/main`). The
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
build. Its `code.bps` accepts the code.bin above exactly (source CRC32
`ffdf1ee8`) but does not patch it: Restoration is built once on v1.0, and its
v1.1 patch rewrites the v1.1 code into that v1.0-derived image (7,835,648
bytes, SHA-256 `dbb46a8b…2ef9`, with Restoration's code appended at
`0x007CF000`; its ExHeader grows text and BSS). That image keeps every
language layout (`EU_Italian` included). dekui's addresses are therefore
addresses of the v1.0 code plus Restoration: of its 143 patch entries only
4 match this port's code (CameraLive projection, Difficulty damage and
spawn), the other 139 read different words in v1.1 or lie in Restoration's
code.

The two images differ by shifts, as the maintainer expected: v1.1's code is
the reference code moved by a constant within long contiguous stretches (47
stretches hold 11,606 of the 11,655 matched functions; about 25 distinct
shifts between -0x230 and +0x228), and the data segment and BSS keep their
addresses (every global the donors use). `titles/mm3d/a32/v110_address_map.py`
turns the instruction correspondence (1,314,324 matched words, branch
displacements, PC-relative loads and literal pools masked) into
`titles/mm3d/dekui/v110_address.h`: 363 stretches of equal shift and
`V110(address)` / `V110Branch(word, address)` (a B/BL re-encoded for its
translated target). With it every one of dekui's 126 native patch entries
finds its intercepted word in v1.1 (103 identical words, 19 re-encoded
branches, 4 already v1.1); the other 17 lie in Restoration's appended code
and follow Restoration's source port. Test: `oot3d_mm3d_v110_address_tests`.

The port stays on the vanilla v1.1 code.bin (maintainer's decision,
2026-10-08). Project Restoration is ported as source onto v1.1, and every
dekui and Restoration address (patch sites, guard words, functions, globals)
is translated from the v1.0-derived image to v1.1 through a map built from
both images. Behaviours are typed source contracts in the MM3D adapter; no
binary patch is applied and their RomFS payloads stay outside the
repository and packages (EUR needs its own `EU_<Language>` equivalents).

## Pipeline (reproducible, outside the repository)

The local work directory (not part of this repository) is called `W` below.

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
   and true-AOT blocks from `titles/mm3d/a32/`, empty for now; supplemental
   entries are the interior entries above plus every guest patch boundary:
   `oot3d_mm3d_single_screen_tests --block-boundaries` lists them and
   `titles/mm3d/a32/guest_patch_entries.py --merge` adds them to the interior
   entries; regenerate whenever a site is added). 17,556 callable entries,
   1,213,315 of 1,240,404 slots reachable; 6 unsupported operations left, all
   data words decoded as code.
5. Whole-AOT program (from the first inventory; not yet regenerated):
   `generate_whole_aot.py` with the same title inputs: 17,753 functions, 237,599 blocks, 1,245,442 instructions, none unclaimed.
6. Selection: `select_profile_whole_aot.py --all-lowerable --code` from the
   empty profile/selection in `titles/mm3d/a32/`: 16,522 functions (1,063
   lowering rejections, 905 ownership conflicts).
7. C++ shards: `tools/ctr_title/generate_whole_aot_cpp.py` (256 shards).

The generator wrappers keep OoT3D's inputs as defaults, so the OoT3D pipeline
is unchanged.

## Runtime title seam

`triaevum_title_adapter.h` resolves the adapter from the process image's
code.bin hash: `oot3d_eur`, `mm3d` or `generic`. Only OoT3D EUR gets the
OoT3D adapter: guest block hooks, composition and UI-lifecycle hooks,
hand-written compiled functions, typed gameplay, mass/true-AOT blocks and the
TopScreen profile (refused for other titles). MM3D's adapter is a title
session (`triaevum_title_session.h`, implemented in
`titles/mm3d/adapter/mm3d_title_session.cpp`): its hooks and guest patches
(dekui, Project Restoration, lighting), the image checks, the scene view,
camera and environment it publishes at the opaque world pass, its interface
frame, its developer warp and its `runtime.json` statistics. The window
session calls these at fixed points and holds no MM3D code; every member
defaults to the session's own behaviour. The adapter also selects its RomFS
language layout (`DetectGameLanguages`, also reachable as
`TriAevum --game-language-info ROMFS CODE_SHA256`). Other titles run on the
CTR host, the A32 runtime with their own whole AOT, and the canonical
renderer. Stderr reports `TRIAEVUM_TITLE_ADAPTER`.

The CMake cache variable `TRIAEVUM_TITLES` (default `oot3d;mm3d`) lists the
titles whose adapters a build contains. `-DTRIAEVUM_TITLES=oot3d` leaves out
every `titles/mm3d` source, library and test; such a build refuses an MM3D
image at start instead of running it as a generic title. OoT3D's adapter is
still part of the window session, so `oot3d` is required.

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
1.25 and 1.5 widen the view.

Interface at a fixed 16:9: as OoT3D's interface keeps its own aspect
whatever the output's, MM3D's keeps the 16:9 frame mm3d-dekui lays it out
for. The composition tracker attributes the top screen's 2D to the UI
domain: the owners and finishers of the 2D layout queues (`0x001267E8`'s
two loops, `0x00126294` and `0x001262E0`) and the frame draw's sprite
queues 4 and 5 (`0x0012C6A8`, after the 2D scene pass; dekui moves the map
and radar quads there). The adapter marks draws whose viewport is the top
screen with a UI frame 16/15 of the screen along native Y; the shared
renderer, for a draw that stays UI after frame-content resolution, fits
that frame centred in the output (`ResolvePicaRasterCanvas`) and narrows
the draw's clip Y by 15/16 (a vertex-uniform delta, zero for every other
draw), so the 400:240 screen keeps its proportions at the frame's centre
and dekui's margins stay drawable, as dekui's renderer does on Azahar.
Partial scissors map through the screen's own proportions
(`PicaUiFrameContentCanvas`). Fades and copies of the rendered frame keep
the scene's mapping. Verified on the developer launch (`save100_items.json`,
Restoration registration skipped for the run) at 1280x720, 1680x720 and
960x720: the pause menu (items page, whole grid and tabs) and the gameplay
HUD (clock, bars, A button, map) stay in a centred 16:9 frame, the scene
fills the output. The A button overlaps the map until dekui's HUD-button
and Restoration's large-screen sites are live.

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
`TRIAEVUM_A32_PC_DUMP=PC,FRAME,DIR,ADDR:SIZE[;...]` (hex PC, ranges) writes
the registers and the listed guest ranges once, at the first entry of block
PC from host frame FRAME on: memory at a native draw boundary inside a frame,
which a savestate at a frame boundary cannot show.
`TRIAEVUM_IR_NO_EXTRA_HID=1` presents an Old 3DS without a Circle Pad Pro,
so the title's accessory prompts can be captured.

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
native. In the unified tree this is the adapter capability
`SetNativePicaFragmentLitShadowReceivers` (MM3D only; OoT3D's shader
sources are unchanged by it).

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

## Loading a save: ir:USER, ptm:u (2026-10-08)

Standard MM3D saves load: a 100% EUR save published online (`MJR` header,
CRC-16/ARC at `0x1A88`) copied into the save-data directory appears in file
select and starts in South Clock Town. Getting from "Start playing on this
file?" to gameplay needed four host services:

- After the file is chosen, the file-select object (`0x602AC0`) runs the
  Circle Pad Pro dialog on the lower screen (messages `0x5741` "This game
  supports the Circle Pad Pro...", `0x5742`/`0x5743` "Set Circle Pad Pro to
  Don't Use", `0x5744` "Use"). Every 64 frames it starts the accessory
  library (`0x1B424C`), whose connect loop (`0x1D55F0`) issues
  `RequireConnection(1)` and waits on the connection-status event with a
  14 ms timeout, retrying four times. The title never asks whether it runs on
  a New 3DS.
- That needed `svcCreateMemoryBlock` (the receive buffer), finite
  `WaitSynchronization1` timeouts and event release on the last
  `CloseHandle` (commit c98e981).
- With no accessory the dialog never ends on our host, and the top screen
  stays black. The host now behaves as a New 3DS, whose ir module presents
  the built-in C-stick and ZL/ZR as a connected Circle Pad Pro (as Citra and
  Azahar do). `RequireConnection(1)` sets the connection fields of the shared
  header and signals the event. The Extra HID device answers
  `ReadCalibrationData` and `ConfigureHIDPolling`. Afterwards it puts one
  6-byte input report per requested period into the receive ring, as
  `0xA5`-framed packets with CRC-8. The report carries the 12-bit C-stick,
  ZL, ZR and a full battery. The window host projects
  `ThreeDsRecomp::Input::ProjectExtraHid` (New 3DS profile, absolute stick
  samples only) into it every frame; the module ABI has no C-stick yet.
  `SetIrExtraHidAttached(false)` gives the Old 3DS behaviour.
- Once connected, the title opens `ptm:u` (battery level, shell and adapter
  state, `GetStepHistory`). The host answers as a charged, open console with
  an empty pedometer.

With these the developer launch reaches gameplay deterministically:
`inputs/save100_play.json` plus `SAVE_SEED=saves/eur-100` (both in the work
directory) loads the save, enters the Clock Tower and walks. Tests:
`TestIrUserCirclePadPro`, `TestPtmUser`, `TestEventResourceRelease` and the
finite-timeout case of `TestWaitSynchronization1`.

## Naming a new file: the software keyboard (2026-10-08)

Choosing an empty file starts the system software keyboard (library applet
0x401). On this host no applet runs: the APT service answers the
application as the keyboard would, in the order MM3D uses:

- `ReplySleepQuery` and `ReplySleepNotificationComplete` succeed.
- `PrepareToStartLibraryApplet(0x401)` prepares the keyboard (other applets
  stay unanswered) and `IsRegistered` reports it.
- GSP: `SaveVramSysArea`, `ImportDisplayCaptureInfo` (the current
  framebuffers), `ReleaseRight`, later `AcquireRight` and
  `RestoreVramSysArea`.
- `SendParameter(Request)` asks for the memory the application copies its
  screens into (the first word of the request is its size): the keyboard
  answers with a `Response` parameter carrying a new memory block, which the
  application maps (`MapMemoryBlock`, also with explicit permissions for
  others), fills and unmaps (`UnmapMemoryBlock`, which now removes the
  mapped region). `SendCaptureBufferInfo` succeeds.
- `StartLibraryApplet(0x401)` with the 0x400-byte configuration and a block
  over the caller's memory: the host writes its text (UTF-16, terminated)
  at the start of that block, sets `text_offset` 0, `text_length`, and the
  return code of the confirming button (the only one, the right of two, the
  right of three), and returns the configuration in a `WakeupByExit`
  parameter that the application's APT thread receives.

The text is "Link" by default; `TRIAEVUM_SWKBD_TEXT` (development, ASCII)
changes it. Verified in the developer launch with
`inputs/save100_new_file.json` (W): an empty slot, the keyboard, "Save
completed.", the new `save1.bin` with the name at the file's name field, and
the new game's opening text. Test: `TestSoftwareKeyboardApplet`.

## Guest patch sites (2026-10-08)

dekui and Restoration change the game by replacing instructions at fixed
addresses (dekui's emulator substitutes the fetched word, mostly with an SVC
trap whose handler runs host code, when the surrounding words still match).
The MM3D adapter expresses each one as a typed site
(`mm3d_guest_patches.h`): its PC, the guard words it was written against,
and a handler that runs on the guest thread's registers, VFP state and
memory in place of that instruction and returns the PC to continue at (or
declines, leaving the original instruction to run). After the process image
is mounted, every site whose guard matches is enabled and the A32 runtime's
native-function callback dispatches it; the guest image is never modified.
A site must start a registry block, so its PC and the next instruction are
given to the A32 generator as supplemental entries (a supplemental entry
splits the block that contains it). runtime.json reports each site under
`widescreen_projection.mm3d_guest_patches` (enabled, first mismatching
guard word, replacements, declines). Product mode has no native-function
dispatch yet and refuses sites. A site is also enabled only if the registry
has a block at its PC (otherwise it would run only when branched to).
Tests: `oot3d_mm3d_guest_patches_tests`, `oot3d_mm3d_single_screen_tests`.

Besides SVC traps dekui substitutes plain instructions (MOV/CMP/TST/ADD/SUB
immediates, B, BX lr, LDMIA sp!); `mm3d_a32_substitute.h` executes exactly
those forms, condition included, and refuses any other. A site can prepare
itself once on the mounted image (to derive its replacement from the code
there). Source ports that call the game's own functions (Project
Restoration calls dozens) use `mm3d_guest_calls.h`: a site starts a C++20
coroutine on the guest thread's state; `co_await context.Call(entry, args)`
branches to the function with the return address at a host sentinel
(`0x000FFFF0`, below the image, dispatched without a registry block), the
runtime runs it as ordinary guest code, and the return resumes the
coroutine; at the end the guest continues with the site's registers as the
body left them. Bodies are kept per guest thread, nested calls stacked.
Tests: `oot3d_mm3d_a32_substitute_tests`, `oot3d_mm3d_guest_calls_tests`.

### dekui's guest side

`titles/mm3d/dekui/` is a faithful port of dekui's guest side (56 of its
`src/mm3d` headers, ported by Codex, GPL-2.0-or-later notices kept, every
address translated to v1.1 with its reference in a comment) and of its
emulator glue as `Mm3dDekui::Core`: `Instruction` is the ordered
substitution chain of dekui's `MemoryReadCode` (every `CodeMatches` kept),
`CallSvc` its SVC dispatch with all hook objects in their order (the shared
NativeInventory pass/begin/end and DialogueScale before/after calls reach
about twenty modules, with scratch-root stack frames unwound in reverse).
`Mm3dSingleScreen` (`mm3d_single_screen.cpp`) runs it as dekui's emulator
does: each of the 122 candidate PCs is a guest patch site; once the image is
mounted the chain gives the word dekui would fetch there, and where that
differs the site runs it: an SVC with its condition checked, r15 = pc + 4
beforehand and the guest continuing at r15 (a CPSR Thumb bit becomes an odd
PC); a plain instruction through the executor above. dekui's settings are
its module atomics; its clock is the deterministic presentation clock (60
Hz). The core stays behind a pointer so its headers never meet `windows.h`.

On vanilla v1.1 all 122 sites apply with the Restoration port below. 42 of
them also check Project Restoration's appended code (39 Prices through
SingleScreenText's message getter, 3 FormCamera); dekui's Memory adapter
answers those checks with the port's verified sites instead
(`RestorationMessagePort`, `RestorationCameraPort`; every native guard is
still checked), and its minimap reads the HUD's heart widget from the port
(`RestorationHeartWidget`). The 17 entries inside Restoration's appended
code are excluded from dekui's candidates and implemented in the port.
Without the port, 80 apply. dekui's two text tables (original US English
messages and their edits) are withheld from the repository as game material
(`titles/mm3d/dekui/PORT_NOTES.md`). `TRIAEVUM_MM3D_DAMAGE_MULTIPLIER` (development) sets dekui's
damage multiplier until the F1 menu owns its settings.

Verified on the developer launch (100% save, `inputs/save100_dekui_song_list.json`
in W, touches standing in for the controls Restoration adds): the native
minimap is drawn on the top screen; playing the ocarina shows dekui's
prompt ("START Song list", "B Back"); the song list opens on the top screen
(Song of Time panel and staff) and B returns to play. The bomb sequence
still multiplies damage through the core. Tests:
`oot3d_mm3d_dekui_core_tests` (with code.bin: 80 replacements, 20,818
corrupted-guard checks), `oot3d_mm3d_single_screen_tests`.

### Project Restoration

Restoration supports v1.1 cards only by rewriting their code into its v1.0
build (`make_release.sh`: the v101/v110 patches go from each version's
code.bin to the one `patched_code.bin` built on v1.0, with the v1.0
ExHeader). Its port onto v1.1 (`titles/mm3d/restoration/`, GPL notices kept,
glue in `mm3d_restoration.cpp`) turns each `hooks/*.hks` record into a guest
patch site at the translated v1.1 address, guarded by the v1.1 word.
Magikoopa's softbranch saves r0-r12/lr, calls the function and then runs
the displaced instruction, so its site runs the function and declines; a
branch to a trampoline becomes what the trampoline leaves in registers or
flags; a code patch is executed in place of the guest's word (branches
re-encoded with `V110Branch`); a data patch is written after the image is
mounted. Restoration reads the game's structures in place; the port uses
their offsets, computed by compiling Restoration's headers for a 32-bit
target (`usable_btns` at CommonData + 0x13740, ScreenContext's active and
new screens at 0x72F5C4 and 0x72F5C8).

Every PC a handler continues at must start a registry block, since the A32
runtime does not interpret code outside the registry (a branch into the
middle of a function faulted as a missing block): sites declare such
targets (`Mm3dGuestPatches::AddTargets`) and they join the registry
boundaries: dekui's sites, Restoration's sites and the native entries and
continuations its calls reach (`Mm3dRestorationPort::RegistryBoundaries`),
520 in all (`oot3d_mm3d_single_screen_tests --block-boundaries`,
`a32_generated_v8`). Every site's guard is checked before any site prepares,
so dekui's preparations see which Restoration sites are enabled.

First slice, the controls that replace the lower-screen buttons
(`rst/input.cpp`): with ZR held, A plays the ocarina and X/Y use the I/II
items; ZL is the Pictograph Box; on the ocarina screen ZL/ZR also play L/R;
X/Y assign the selected item directly; the native ZL/ZR checks are skipped
(`decouple_trigger_btns`) and Start/Select are decoupled (data patch at
0x6B083C). Verified on the developer launch: ZR+A takes out the ocarina and
dekui's prompt appears (`inputs/save100_rst_ocarina.json`, W). Test:
`oot3d_mm3d_restoration_tests`.

Second slice, `rst::Calc` (softbranch at the game state's update,
`0x106740`): the first game state plays Restoration's initialisation jingle;
in play, Start or Select leaves the Gear screen, and on the ocarina screen
closes the current layout (the ocarina's, which opens the song list, or the
song list's) and plays the decide effect. It calls guest functions through
`Mm3dGuestCalls` and resumes the guest at the hook itself, whose own
instruction then runs once (`Mm3dGuestPatches::SkipOnce`).

The rest of the port (Codex; `titles/mm3d/restoration/PORT_NOTES.md`) covers
all 107 `hooks/*.hks` records: 108 instruction sites and 15 guarded data
patches, each v1.1 position proved by instruction context
(`W/analysis/dekui/RESTORATION_HOOK_PROOFS.md`): schedule and menu
shortcuts (with dekui's MenuShortcuts and MinimapShortcut), the large-screen
HUD (Hud2, with dekui's HudButtons), the message getter, camera and form
camera (dekui's calls reach the same Core through `SetDekuiBridge`), Link
(Zora swim, fast transformation, Goron roll, arrows), inverted time, the
Song of Time save, NPC and actor fixes and the four bosses. The data patches
are written before the sites are checked, as dekui runs on the image
Restoration patched. Restoration's own RomFS files (`layout/Joker.Main.New.gar`
with Hud2, its `Joker.Main.Play.gar` and the `Joker.mfpj` project naming the
new package and layouts) are not part of the user's game: they are laid over
it with the RomFS layers below. Without them the port builds no layout or
package (the native lookups return 0xFFFF, which the native index would read
past) and Hud2 stays closed. Message edits (prices, single-screen texts) are
caller-owned, from the user's own messages; none are supplied yet.

Data addresses: Codex's address map treats the data and BSS segments as
unchanged between v1.0 and v1.1, but its own cross-references show shifts
(+4 at 0x6B0AA8-0x6B1DF4, +8 at 0x6B1E40-0x6BB364 and 0x6CB298-0x6F8178).
Two port globals lay there: `ui::GetCommonLayouts()` (v1.0 0x6B7B8C, v1.1
0x6B7B94) and the LayoutMgr instance guard (0x6B0AFC, v1.1 0x6B0B00), each
confirmed by the literal v1.1's own code loads; with the v1.0 values Hud2's
initialisation swapped the wrong field and the native touch panel was drawn
on the top screen. dekui's own data references were already translated (its
guards check them).

### RomFS layers

Installed RomFS mods ship through OoT3D's O3RO overlay
(`oot3d_native_a32_romfs_overlay.{h,cpp}`, Forge-built over the user's image,
`NativeA32CtrHostConfig::RomFsOverlay`). The development path is separate:
`oot3d_native_a32_romfs_layers.{h,cpp}` (title-neutral) serves the title's
RomFS archive as its level 3 rebuilt with host files laid over it, as
LayeredFS does: each layer is a directory mirroring RomFS paths, a file
replaces or adds the one at its path, later layers win. The directory and
file tables and their hash tables are rebuilt in the image's own order (a
directory's children get consecutive entries before each child's subtree;
files in depth-first order), so an empty overlay reproduces the image's
tables byte for byte; file data is read from the image and the layer files
on demand, never copied. The host serves it in place of the image
(`NativeA32CtrHostConfig::RomFsLayers`); a state saved over another RomFS
is refused, since the title caches the tables it read. Enabled with
`TRIAEVUM_ROMFS_OVERLAY=<dir>[;<dir>...]`, never together with installed
RomFS mods (the host refuses the combination). Layer files are the user's
and are never packaged.

Verified on the developer launch with Restoration's three RomFS files as the
layer (1280x720, `save100_items.json`): Hud2 on the top screen in the 16:9
frame (hearts and magic top left, D-pad items below, B/A and the X/Y/I/II
items top right, rupees bottom left, clock centred, the map bottom right),
the pause items page unchanged.

dekui's setup layer: `titles/mm3d/dekui/setup/prepare_romfs_layer.py` (ported
from dekui's `build_release_assets.py`, GAR code in `dekui_gar.py`) prepares
the layer dekui's first launch prepares, from the user's own files: the
layouts it edits read from the user's RomFS (EUR's `layout/Joker.Main.gar`,
`Joker.Main.Play.gar`, `Joker.Main.Play.SubMenu.gar` and `Joker.gar` are the
USA files byte for byte), Restoration's three files from the user's
Restoration folder. It applies dekui's edits (Hud2 groups moved into the
16:9 margins, Restoration's A/B prompt tracks restored from the native
archive, the map's slide tracks held, notebook rows, submenu title and
telop poses) and checks every input and output against dekui's published
fingerprints (`setup-fingerprints.json`): all 11 match on EUR. dekui's
re-edit sources (`*.mm3d-*-source`) are written beside the layer, not in it.
In W: `python titles/mm3d/dekui/setup/prepare_romfs_layer.py --romfs
extract/romfs.bin --restoration <restoration> --layer mods/dekui-romfs
--sources mods/dekui-sources`, then
`TRIAEVUM_ROMFS_OVERLAY=<W>/mods/dekui-romfs`.

dekui's single-screen wording: `titles/mm3d/dekui/setup/prepare_message_edits.py`
(from dekui's `single_screen_text.py`, reviewed English in
`single-screen.json`) compiles dekui's edits against the user's own English
messages (EUR `message/eu/eue.gmsg`, language bit 4) with dekui's checks
(allocation, unchanged dialogue flow, control round trip) and writes guarded
edits for the port's message getter; a message whose English differs is
skipped, no wording is invented. On EUR English 11 of dekui's 12 messages
apply (0x0E9B "Progress saved." differs); other languages keep their native
text. The edits hold the user's text: they stay on the user's machine.
Development: `TRIAEVUM_MM3D_MESSAGE_EDITS=<json>`
(`Mm3dRestorationPort::ReadMessageEdits`, then `SetMessageEdits`). Loaded
live (11 messages, no faults); a scene showing one of them is not yet
captured. dekui's doubled prices (option, off by default) are not compiled
yet.

Screen sweep (developer launch, with the layer; `W/analysis/dekui/sweep`):
gear, masks, items, notebook (Event Notes on the top screen), Song of Time
(dekui's song prompt, staff, the dawn confirmation on the top screen),
Double Time, shortcuts and file select run without faults. The map
timeline from the archived branch drives the old lower-screen controls and
does not open the map. Tests: `oot3d_native_a32_romfs_layers_tests` (layers, replacement,
nested directories, re-layering, malformed base; with
`OOT3D_ROMFS_OVERLAY_IMAGE` the image's tables are reproduced and its file
data served).

Verified on the developer launch (`save100_items.json`, 1280x720): all 122
dekui and 113 Restoration sites enabled, 55 fired, no faults; dekui's A and
B buttons on the top screen, the pause items page with the X/Y/I/II slots.
Tests: `oot3d_mm3d_restoration_tests` (with code.bin: 108 sites and 15 data
guards match, 648 corrupted guards rejected, missing-layout lifecycle),
`oot3d_mm3d_guest_patches_tests` (guards before preparations),
`oot3d_mm3d_dekui_core_tests`.

### dekui's settings

`titles/mm3d/dekui/settings.{h,cpp}` (ported by Codex from dekui's
`settings.cpp` and the application in `overlay.cpp`, nlohmann::json instead
of Qt) load dekui's `presentation.json` and apply it to the modules as dekui
does at startup. The runtime applies dekui's shipped defaults
(`titles/mm3d/dekui/defaults/presentation.json`, dekui-authored values, no
game material), or a file named by `TRIAEVUM_MM3D_PRESENTATION`
(development), until the F1 menu owns and persists them. The shipped
defaults keep dekui's choices, e.g. `song_reset_top=false` and damage x1.

Verified on the developer launch with Restoration's controls only
(`inputs/save100_rst_song_list.json`, W): ZR+A takes out the ocarina, Start
opens the song list in dekui's layout (the songs as a grid of three columns
on the left, the selected song's staff below), B returns to play. dekui's
prompt icons come from its renderer side, not yet integrated. Tests:
`oot3d_mm3d_dekui_settings_tests` (shipped file and a scratch directory),
`oot3d_mm3d_restoration_tests`, `oot3d_mm3d_guest_calls_tests`.

## Opening post effects: GPU-produced surfaces (2026-10-09)

The opening's spinning mask leaves a motion-blur trail and glows; the
Happy Mask Salesman has a halo. MM3D draws both with native feedback: a
DisplayTransfer into texture memory (2x2 box downscale), a bloom ping-pong
between small render targets that sample each other, and the previous frame
blended back with constant alpha 200/255. Our renderer sampled stale guest
memory for all of them. Tiled transfer results and sampled colour targets are
now GPU producers in the shared renderer (TRIAEVUM_GPU_COPY_PATH_PLAN.md §9).
Validated on the developer launch against the Azahar oracle
(`W/tools/run_azahar_oracle.sh`, an isolated portable instrumented Azahar).

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
