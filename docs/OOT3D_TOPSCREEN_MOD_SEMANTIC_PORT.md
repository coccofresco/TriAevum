# TopScreenMod 1.2 semantic port

> Historical baseline: current TopScreen 2.1.1 work and its architectural
> contract are documented in `OOT3D_TOPSCREEN_2_1_1_PORT.md`. Keep this file
> for the 1.2 evidence and inherited behavior; do not use its configuration
> schema or module layout as the current implementation contract.

## Scope

This document defines the source port of official TopScreenMod 1.2. The
archive is an offline evidence input, not a runtime dependency: the
application never loads its IPS, exheader, or injected ARM payload. Only
behavior identified from that evidence and expressed as typed C++ is enabled.
The mod's native-resolution replacement CTXB files are imported as optional
TopScreen-profile assets; they are never applied to the default profile.

The port is an optional runtime UI profile. The default profile remains the unmodified OoT3D presentation. Selecting either profile must not alter `SaveContext`, save serialization, or save-file compatibility. Under the TopScreen profile, file select, name entry, and other menus retain native mechanics and assets but use the mod's single-screen presentation semantics.

## Provenance

| Input | SHA-256 |
| --- | --- |
| Official `topscreenmod_v1.2.zip` | `f724c8f0c3e971feb1c11447de18d505323acdc77f2a8c5dc7dc17be16e37f33` |
| EUR emulator `code.ips` | `744941412ca22088ce6e6f4b6b61f9df7ea6698f45ca048365269c9f0c66cd79` |
| EUR original `code.bin` | `16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220` |

The official 1.2 archive is the reconstruction baseline. Its size is
113,391,809 bytes and its MD5 is `5a291097f0bdf1f26536bd761c825a14`.
The previous local archive remains historical comparison evidence only. The
1.2 delta supplies HUD scaling, Normal/Restoration layouts, revised projectile
selection, Quest model hooks, title-demo correction, minimap alignment, and
Ocarina overlap handling. The original mod stores user preferences in save
data and mutates them through gameplay chords; this port deliberately moves
all persistent preferences to an external host JSON contract so saves remain
compatible and gameplay input remains available to the game.

The camera payload also has a matching public source reference:
`tristangnl/OoT3D_Standalone_Free_Cam_CPP` at commit
`fb752e64a1b9fc35f057c4cf124539da3a341984`. Its `Camera_FreeCamEnabled`
control flow, Camera field offsets, blocked-setting set, masks `0x20938230`
and `0x82E3`, deadzone, and orbit constants match payload functions
`0x005D0AF8` and `0x005D0C60`. The maintained port still treats the IPS
payload as the version-specific binary oracle; the public source supplies
names and types only where that correspondence is confirmed.

### External 1.2 configuration

`--topscreen-config <path.json>` is accepted only together with
`--ui-profile topscreen`. The schema is `oot3d_topscreen_ui_v1`:

```json
{
  "schema": "oot3d_topscreen_ui_v1",
  "hud_layout": "normal",
  "hud_scale": 0.9,
  "minimap_visible": true,
  "n64_camera_zoom": false,
  "free_camera_enabled": false,
  "free_camera_speed": 6,
  "free_camera_invert_x": false,
  "free_camera_invert_y": false
}
```

`free_camera_enabled` defaults to `false`; in that mode TopScreen never owns
`Camera_Update`, so mouse motion cannot alter the native OOT3D camera.
`hud_scale` accepts the exact 1.2 lattice `0.60..1.25` in `0.05` steps.
`free_camera_speed` accepts `2`, `3`, `4`, `6`, `8`, `12`, or `16`.
Unknown fields, invalid values, invalid schemas, missing files, and use under
the default `oot3d` profile are rejected before gameplay. The resolved values
are loaded into one typed runtime store, reapplied after lifecycle/savestate
resets and whenever that store advances, reported under `topscreen_settings`,
and never written to guest memory or save serialization. Reference files are
`config/topscreen_ui.example.json` and
`config/topscreen_ui.restoration.example.json`.

The renderer configurator exposes that same store in its `TopScreen 1.2` tab.
`Apply and save` atomically replaces the selected external JSON before
publishing a new runtime revision; `Reload JSON` imports manual edits. HUD
layout, HUD scale, minimap visibility and the free-camera enable, speed and
axis-inversion preferences
therefore have one persistence authority. No controller chord or guest save
field can mutate them. When no `--topscreen-config` path is selected, the tab
shows the built-in defaults as read-only.

`analyze_topscreen_mod.py` parses the IPS, validates these inputs externally, maps records to the maintained code inventory, decodes scalar values, and resolves direct ARM hooks. Its output is analysis material and is not linked into the game.

`build_topscreen_mod_coverage.py` joins that raw inventory with the maintained
semantic dispositions. It rejects duplicate, missing, or out-of-range record
assignments and writes
`tools/oot3d/decomp_support/analysis/topscreen_mod_coverage.json`. Regenerate it
with:

```powershell
python tools/oot3d/decomp_support/scripts/build_topscreen_mod_coverage.py --raw-analysis I:\oot3dre_work\native_game\topscreen_v1_2_raw_analysis.json --semantic-port tools/oot3d/decomp_support/analysis/topscreen_mod_semantic_port.json --output tools/oot3d/decomp_support/analysis/topscreen_mod_coverage.json
```

The record-level matrix is authoritative for closing each IPS patch site, but
it is not by itself proof that every portable behavior inside the injected
the injected payload record has been reconstructed. A broad disposition does not imply
implementation: only an explicit resolution or a deliberately closed
disposition closes the patch site, while the separate functional matrix below
tracks payload-owned controls and presentation behavior. At the current
checkpoint the gameplay compositor in record 11 is source-ported, including
the native minimap lane and its associated indicators. Records 26 and 36 are
source-ported camera contracts with deterministic Vulkan evidence.

`build_topscreen_texture_override_pack.py` imports only the mod's original-size
CTXB payloads. It accepts either the source archive through `--archive` or its
extracted `TopScreenMod/romfs` through `--mod-romfs`, validates the unchanged
0x48-byte CTXB header and payload size against the original ROMFS, and rejects
an original payload that maps to conflicting replacements. Identical mappings
across languages are deduplicated. The current archive produces eight unique
entries (1,638,572 bytes, SHA-256
`d93a393d74de1f9029158af9c28ee07d49dcc0a62e3ae96c8dfb205ffe57d92e`)
from the eighteen localized `menu_top_parts00.ctxb` and
`menu_cursor00.ctxb` files.

The runtime accepts that offline artifact only with
`--ui-profile topscreen --topscreen-texture-overrides <pack.o3tu>`. The
project launcher supplies the tracked original-size pack automatically for the
TopScreen profile; an explicit path still overrides that default. The native
PICA submission path binds loaded pause-shared CTXB identities to physical
surfaces and replaces only matching draw snapshots; the host UI provider
applies the same hash-gated replacement before decoding. Guest memory and save
state are not mutated, and the default profile cannot accept the option. A
deterministic Vulkan run from `hudtest_after120` recorded 16 native target
bindings, 112 matching PICA draw replacements, and one host-provider
replacement in ten frames. Its authoritative framebuffer is
`native_game/topscreen_native_ctxb_final_20260722/2.png`; archive- and
ROMFS-built packs produced byte-identical framebuffers.

The PICA target registry is refreshed once at the host-frame boundary, before
guest draws are consumed. This mirrors a ROMFS replacement being available
from the first draw and prevents transparent atlas edits from exposing pixels
drawn before `PauseUi_Update`; it also avoids duplicate registry scans at the
guest-update seam. Per-target diagnostics retain each native semantic,
guest/physical surface identity, dimensions, format, last payload hash and
replacement result.

Record 14 is source-ported. Producer `0x005CE57C`
builds sixteen world-map quads from the four native 108-entry `PauseWorldMap`
vec2 arrays, destination tables, save flags, controller state, and provider
slot 5 (`OcarinaPage`). The source presenter reproduces its destination color,
six base panels, eight conditional markers, two navigation arrows, alpha rules,
and PICA UV orientation. It also reconstructs `0x005C9040/0x005C887C`'s two
side-navigation arrows from the native directional input frame and provider
slot 2 (`PauseTopPage`), including active offset, brightness, alpha, and
mirrored left UV. The shared tail was audited call by call: it reuses the
already ported pause route, touch suppression, viewport/native redraw,
controller, and edge contracts, so the presenter does not duplicate them.

The pause-page redraw tail at `0x005CD1E0` is also source-ported as an
independent presentation owner. Its two 32x240 edge quads use pause-shared
builtin texture slot 3 (`ItemPage`, native `menu_item_parts00.ctxb`, 256x256),
not `PauseTopPage`; their source rectangles are `(192,0,64,240)` and
`(256,0,-64,240)`. The host presenter samples those already decoded top-down
rows directly, while the payload's `FUN_005C7230` V inversion remains confined
to the guest PICA upload boundary.

The 1.2 injected region has also been imported as a raw ARMv7 little-endian
image at its real base `0x005C7000` in a separate Ghidra project.
Reproducible entry points are listed in
`tools/oot3d/decomp_support/analysis/topscreen_v1_2_payload_selected_entries.txt`.
Exporting those entries with payload callees enabled produces a transitively
closed set of 160 functions. The tracked evidence is under
`tools/oot3d/decomp_support/analysis/topscreen_v1_2_payload_ghidra_export`;
generated output is analysis evidence and never a build input. The original
`code.bin` entries
that own the gameplay minimap are listed separately in
`tools/oot3d/decomp_support/analysis/topscreen_minimap_native_entries.txt`.

Earlier exports treated selected semantic entries as if they were call-graph
closure. The maintained 1.2 export follows every payload-local `FUN_` and
`func_0x` callee recursively. The validator finds 439 unique local
source/target edges and zero targets outside
`functions_selected.csv`. The added functions cover front-end and renderer
helpers, camera trigonometry/distance/effect code, Items-page selection,
numeric formatting, payload allocation, CTR IPC/SVC wrappers, and bounded
string utilities. Portable behavior is assigned to existing typed front-end,
free-camera, Items, visibility, and notice/font owners; platform mechanisms
are explicitly closed as host replacements. None is executed as injected ARM.

Azahar validation must not autoload an older unmodified savestate without
additional preparation. Such a state restores both the original code bytes
and the old virtual-memory map, erasing the IPS and its payload allocation.
The diagnostic emulator can re-create `0x005C7000..0x005D5000`, reapply the IPS,
and invalidate translated code after load. This is sufficient to validate
closed scalar/control-flow changes (for example the top-screen A button and
minimap relocation), but it does not reconstruct payload-owned renderer
objects that are normally created during a modded boot. Full dynamic HUD
oracle captures therefore require a save reached after a clean modded boot.

## Findings

The 1.2 IPS has 117 records and 64,009 replacement bytes. One 63,165-byte
record is an injected ARM region. The mod combines five independent concerns:

1. In-game HUD and pause geometry. It relocates native OoT3D quads, health/magic geometry, pause pages, render targets, and viewport/scissor state onto the top screen.
2. Input semantics. Four native item-button queries are wrapped to synthesize Item I/II from ZL/ZR, while the central payload adds pause routing, boot compatibility, and an aiming projectile selector. Pause and item-selection calls receive smaller wrappers.
3. Optional camera controls. `Camera_Update` and `Camera_Normal1` are extended with a free-camera path, persistent sensitivity/inversion settings, and transient option notices rendered with an embedded 6x10 font.
4. 3DS process support. Startup, IR service, home-menu, stereo, and renderer-service hooks make the binary mod work on original hardware. Their hardware behavior must not be ported; portable behavior reached by the same callbacks is split into typed source contracts.
5. Title-logo behavior. Three instructions in `EnMag_Update` hold the title and effect at the completed third fade stage instead of advancing to the copyright stage and native animation-state call.

The 1.2-specific hook delta includes
`PauseQuestPage_Draw` at `0x0042B9F4`,
`Graph_ThreadEntry` at `0x00416350`, and
`PauseRenderBuffer_SetVec2` at `0x002E70A8`, plus revised Quest-submit,
Pause-icon-refresh, and `Camera_Normal1` continuations. Their portable effects
are represented by typed Quest model transforms, layout-aware icon geometry,
title-demo gating, Ocarina-overlap suppression, camera configuration, and
single-target minimap alignment. The original native callees and render
streams remain owners wherever the mod only transforms their inputs.

The recovered HUD scale word is at payload address `0x005D6690`: default
`0.90`, increment `0.05`, minimum `0.60`, maximum `1.25`. These are format
evidence for validation, not runtime storage. The source port keeps the value
in host configuration.

The replacement CTXB files preserve the original native dimensions and PICA RGBA4444 format. Pixel comparison shows selective edits rather than replacement art: the cursor atlas changes 502 pixels in `x=64..97, y=32..48`; the English top-parts atlas changes 7,619 pixels in `x=127..510, y=16..254`. They primarily remove touch-only elements and add ZL/ZR labels. The source port imports these original-size CTXB files into a profile-owned asset pack; loading the TopScreen profile selects them by native resource identity, while the default profile continues to use the unmodified OoT3D textures. The ten optional 4K emulator PNG replacements added by 1.2 are not runtime inputs; all 1.2 native CTXB files are byte-identical to the preceding archive.

## Functional completion matrix

This matrix, rather than the 117/117 patch-site count, defines completion of
the selectable TopScreen profile.

| Portable behavior | Status | Evidence / remaining work |
| --- | --- | --- |
| ZL/ZR Item II/I, slot compatibility, D-pad boots | Source-ported | Typed item-query and slot resolvers; live ZR edge plus contract tests. |
| START open Items and START close pause | Source-ported | Deterministic native-page open/close transaction. |
| Pause B close-to-save and L/R tab switching | Source-ported / native retained, verified | The source port reproduces payload `0x005C9940`'s five-tick B transaction into the native System Menu; unchanged native L/R routing switches Items, Gear, and Map. |
| Top-screen minimap visibility | Source-ported | External `minimap_visible` selects visibility; Select no longer mutates a persistent option. Native owner and all four indicator streams retain their original draw path. |
| Free camera and notices | Source-ported | Typed staged camera bridge and payload-font notices; speed and X/Y inversion are supplied by external configuration rather than L+R gameplay chords. |
| N64-style camera zoom | Source-ported | External `n64_camera_zoom` controls the recovered `Camera_Normal1` continuation. A Vulkan probe reaches 79 calls with zoom enabled. |
| Custom HUD sizing | Source-ported | External `hud_scale` accepts the exact recovered `0.60..1.25`, step-`0.05` contract, defaulting to `0.90`. |
| Normal / Restoration layout | Source-ported | External `hud_layout` selects the two recovered presentation and input layouts without guest-save preferences. |
| D-pad Up Navi/view and D-pad Down ocarina | Source-ported | Payload scene/pause/chord precedence is reproduced; Down reuses the original OoT3D eligibility function. Vulkan hudtest proves one Navi/view activation and one rejected native ocarina query, while the default profile remains inactive. |
| Aiming projectile switching | Source-ported | Left/Right cycle the native selector in Normal layout; Restoration additionally uses ZL. Native availability tables, prompt state, and sound remain authoritative. |
| Desktop controller input | Source-ported | SDL gamepads feed the native OoT3D HID contract directly: face buttons, shoulders, Start/Select, D-pad, circle pad, right stick, and ZL/ZR triggers. Axis thresholds reuse three_ds_recomp_runtime's persisted dead-zone settings; no controller state enters saves. |
| Project Restoration item chords | Source-ported | ZR+X and ZR+Y feed Item I/II only under the Restoration layout; Normal retains ZR/ZL Item I/II. |
| Complete HUD/menu visual parity | Source-ported / verified | Deterministic Vulkan framebuffer validation covers File Select, Name Entry, Items, Gear, Map, and Save/Quit. Name Entry is reached through the original `FileSelect_Update -> NameEntry_Activate` transition, not a forced UI state. |
| Quest models, title demo, Ocarina overlap, minimap indicators | Source-ported / verified | The three 1.2 hook additions are represented as typed lifecycle/model contracts; single-target host composition retains native indicator draws without duplicating the payload's 3DS target replay. |

The EUR and USA 1.2 images remain region-relocated variants of the same
portable feature set. Persistent payload state is not recreated: layout,
scale, minimap visibility, camera zoom, speed, and inversion are host
configuration. Gameplay actions remain gameplay input and use the recovered
1.2 Normal/Restoration mappings.

Payload `0x005C9940` also transforms native PauseTouchButtons quad 28. Native
renderer memory and the original `menu_top_parts00` UV rectangle
`(128, 0, 48, 48)` identify this quad as the Navi/view eye button. Its two
vertical layouts are source-ported directly; it is not a minimap transform.

The Gear, Map, and Items keyboard shortcuts were also exercised manually
against the native pause controller after the source-port changes. `G`, `M`,
and `I` each reached the corresponding native page; no synthetic controller
state was used.

The record-level closure is supplemented by
`tools/oot3d/decomp_support/analysis/topscreen_payload_function_port.json`.
It assigns every one of the 160 call-closed functions inside the injected payload
to a typed source owner, a source-ported result whose ARM mechanism is
discarded, or a 3DS hardware service replaced by the host. The corresponding
`build_topscreen_payload_function_coverage.py` validator rejects missing,
duplicate, or extra functions, nonexistent owners/tests, empty semantic roles,
unresolved payload-local callees, and C++ owners that do not contain their
declared implementation symbols.
Item I/II and boot-slot wrappers are a separate ownership group from generic
UI rendering. This prevents the single large IPS payload blob from hiding
unreviewed internal behavior behind the 117-record coverage count or a merely
nominal source-file assignment.

The complete acceptance gate is defined by
`tools/oot3d/decomp_support/analysis/topscreen_mod_completion_contract.json`
and executed by `verify_topscreen_mod_completion.py`. It combines the two
static closures, the focused C++ tests, paired TopScreen/default runtime
evidence, and authoritative framebuffer artifacts:

```powershell
python tools/oot3d/decomp_support/scripts/verify_topscreen_mod_completion.py --contract tools/oot3d/decomp_support/analysis/topscreen_mod_completion_contract.json --repo-root . --artifact-root I:\oot3dre_work\native_game --test-bin-dir I:\oot3dre_work\build-hud-baseline-a30 --output I:\oot3dre_work\native_game\topscreen_mod_completion_result_v1_2_20260723.json
```

The current result passes 2 static evidence groups, 23 runtime evidence
groups, 10 internal framebuffer files, 3 C++ executables, and the manual
native-page check for `G`, `M`, and `I`. The result file is an external build
artifact; the maintained contract and verifier are the reproducible sources.
BMP evidence is decoded by the verifier and must contain at least 100,000
non-black pixels; file size alone is not accepted. The 1.2 profile captures
use a delayed single framebuffer read at presentation frame 170 so startup
black frames cannot satisfy the visual gate.

## Port decisions

| Evidence | Decision |
| --- | --- |
| HUD/pause callsites and scalar tables | Source-port by semantic owner: HUD/pause layout, lifecycle, model geometry, and top-screen composition. Only understood transformations are eligible. |
| Item-query and slot wrappers | Source-port as typed resolvers fed by host semantic input. Do not emulate payload globals. |
| `Camera_Normal1` and `Camera_Update` hooks | Source-port as profile-only camera contracts; persistent options come from external JSON and world queries remain in original OoT3D helpers. |
| Startup, service, IR, stereo, and hardware-renderer hooks | Reject the 3DS mechanism. Source-port only portable behavior reached through those callbacks. |
| Injected payload record | Never execute or copy as a blob. Its transitively closed functions are evidence split among typed owners. |
| Quest submit/draw and icon-refresh hooks added by 1.2 | Source-port the recovered model, projection, and icon geometry around unchanged native consumers. |
| Renderer call suppressions | Source-port the exact callsite suppression contract; do not infer that a single-screen backend makes guest side effects irrelevant. |
| `EnMag_Update` title behavior | Source-port the recovered fade-hold block as profile-only typed semantics. |

Record numbers are intentionally not repeated here because adding an IPS
record renumbers later records. The authoritative 117-entry mapping is
`topscreen_mod_semantic_port.json`, joined against
`topscreen_v1_2_raw_analysis.json` by the coverage generator.

## Recovered contracts

The contracts below retain callsite descriptions accumulated during the
earlier binary audit. Any `Record N` label in this section is a historical
ordinal, not a 1.2 record identifier. For 1.2, use the address/disposition
mapping in `topscreen_mod_semantic_port.json` and the tracked 1.2 Ghidra
export.

Records 104-106 replace three `RendererSubmissionGroup_PushPair` calls in
`HintMovie_UpdateRendererTransitions` with `mov r0,r0`. Records 107-110 do the
same for command-list binding, uniform blocks 0/4, and command statistics in
`PicaMaterialState_SubmitTransformUniforms`. The source port intercepts those
seven original callsites only under the TopScreen profile and continues at
`callsite+4` without touching LR or argument registers. This is the exact ARM
`NOP` contract and remains correct independently of how the host backend
implements single-screen presentation.

Records 111-113 patch one fallthrough block in `EnMag_Update`. Original block
`0x001DADCC` loads the animation owner through literal `0x001DAD08` and global
`0x0050BB50`, clamps actor fields `+0x1D0/+0x1DC`, advances fade substate
`+0x1C4` from 3 to 4, and calls `0x00347FBC(owner, 0)`. The mod instead keeps
substate 3, writes timer `+0x1C6 = 1`, omits that call, and reaches the unchanged
branch target `0x001DAFF8` with `r0=owner` and `r1=1`. The source port makes
`0x001DADCC` an observable whole-AOT exit and performs this complete block only
for the TopScreen profile; it neither patches `code.bin` nor introduces title
state outside the native `EnMag` actor.

The title hold is validated end to end from the same boot checkpoint. A
1,000-frame Vulkan TopScreen run reaches the title and records 1,039
`topscreen_title_logo_fade_hold_calls`; the matching `oot3d` profile control
records zero. The authoritative framebuffer confirms that the title remains
in the completed third fade stage. The older `enmag_trace` callback does not
observe this whole-AOT route and is therefore not used as its acceptance gate.

The four item-button wrappers at `0x005C9180`, `0x005C91E0`, `0x005C9240`, and `0x005C92A0` replace, respectively, original entries `0x00349504`, `0x003494F4`, `0x002C3960`, and `0x002C3950`. Those entries read `GlobalActionState` fields `+0x3C`, `+0x40`, `+0x44`, and `+0x48`. Item I uses ZR and suppression bit `0x04000000`; Item II uses ZL and suppression bit `0x02000000`. The input producer at `0x005CC1A8` combines both mod input sources, stores current ZL/ZR state at `0x005D413C`, and computes press edges at `0x005D4138` as `current & ~previous`. The wrapper data references therefore prove that fields `+0x3C/+0x40` are pressed lanes and `+0x44/+0x48` are held lanes.

The wrappers retain the native result unless the matching extended-input lane or compatibility override is active. They synthesize availability only when the corresponding suppression bit is clear. The compatibility producer is recovered: held D-pad Left enables both Item I lanes only when native slot byte `0x005879FC` is item `0x45`, while held D-pad Right enables both Item II lanes only when `0x005879FD` is item `0x46`. The source port reads those native identities directly instead of reproducing payload globals `0x005D4124/0x005D4120`. Exhaustive literal-reference analysis proves that `0x005D4128..0x005D4134` are read by the four wrappers and cleared by the central routine but are never set by this payload, directly or through a nearby base pointer; they are dead zero-valued lanes and are not represented as host-provided gameplay state. Host semantic input replaces the excluded 3DS IR polling routine at `0x005D1460`.

The fifth wrapper at `0x005C9300` replaces `GlobalActionState_GetSlotItemId` at `0x002C3970`. A targeted Ghidra comparison confirms that native slot handling, special state `+0x5C75`, inventory checks, and fallback item IDs are retained. Its ordering is significant: slots above five are rejected first, then any nonzero signed special-state byte returns the original special result, and only the ordinary state can receive the compatibility delta. D-pad Item I maps slot 3 to item ID `0x45`, while D-pad Item II maps slot 4 to item ID `0x46`. ZR/ZL still traverse the wrapped native slot queries and do not produce these IDs directly. The typed guest bridge reproduces this ordering, applies the overlay only when its native identity gate is satisfied, and declines every other case so the complete original resolver executes unchanged.

The aiming projectile cycle is a separate delta in the central payload entry;
it is not produced by the renderer helper. In 1.2, fresh HID Left/Right edges
are accepted only when native aim state `0x004FDABC` equals one
and selector mode `0x00506CE8` equals two. The selector at `0x00506CE0` moves
within ranges `0..4` and `5..7`. Transitions in the first range are gated by
the original availability tables at `0x0055B634` and `0x0055B63C`, then update
the native prompt field at `0x004FDAA4`; focused `code.bin` export proves that
callee `0x002E666C` performs exactly that write. Every accepted transition
emits original sound `0x0100048E` through `Audio_PlaySoundGeneral`. The source
port applies this contract directly to guest state and invokes the native
audio function; it does not open or duplicate the pause Items arrow popup.

The same central entry owns the non-aiming D-pad actions. Both require the
native gameplay scene owner to report mode `3`, variant `2`; an `L+R` chord
suppresses this route. A fresh Up edge without R first evaluates pause state
`0x0050AF68`, the scene special owner, action blocker `0x005043EC`, pause flags
and Navi mode. When accepted it copies player Navi text `scene+0x2DD4` plus one
to `0x0050AF6C`, emits the mode-one signal at `0x00565660` where required, and
sets state `0x0050AF68` to one. This write occurs before Down is considered, so
simultaneous Up/Down edges deliberately give Navi/view priority. A remaining
Down edge invokes unchanged native function `0x002EFFF8`; only return value one
sets ocarina request `0x0050AF94` and state `0x0050AF68` to one. The source port
uses a synchronous original-function call with an isolated register copy, so
the complex OoT3D eligibility logic remains owned by the game.

The apparent modulo-12 input machine in `0x005CF4A0` belongs to the native
world-map controller. Its global base is `gPauseWorldMapState` at
`0x005093E4`; the tested field `0x005093F8` is controller state `+0x14`, and
the twelve values are world-map destinations. It is therefore retained under
the already source-ported map/compositor contract, not classified as an item
or projectile selector.

The live native-candidate path now applies that overlay only while the host
Item I/ZR or Item II/ZL lane is active and the native special-state byte is
zero. A matching slot returns `0x45` or `0x46`; inactive lanes, special state,
and every other slot decline the candidate so the full original OoT3D function
executes unchanged. Keyboard `1` and `2` currently provide the two portable
semantic lanes without aliasing native L/R actions. Runtime diagnostics expose
per-lane query/result counts plus slot attempts, compatibility overrides and
native fallbacks, allowing deterministic input runs to validate the live path.
The five original entries are also explicit observable exits from whole-AOT;
registering them only as native candidates was insufficient because direct
calls inside a compiled region bypassed the dispatcher. A deterministic
`hudtest_after120` run using `topscreen_item_validation.json` now reaches the
live pressed getter 59 times and produces exactly one true result on the ZR
press edge. The checkpoint does not own items that exercise the remaining
three getters or slot resolver, so their complete matrix remains covered by
the guest-memory contract tests rather than being inferred from this run.

`0x005C8198` first rebuilds twenty native health quads, clears auxiliary quad slots `0x3F..0x52`, and advances the native 0..16 pulse phase. `0x005C8244` rebuilds hold-progress/magic geometry. `0x005C7924` transforms quest-page geometry before using the native model upload path. These must remain native-content transformations, not substitute HUD art.

The health builder at `0x005C7E88` is now represented independently from rendering. It derives visible heart count from capacity in sixteenth-heart units, places up to twenty native quads in two rows, selects one of five original atlas rows from remaining health, and reproduces the original 17-phase pulse expansion. The following `0x005C8198` cleanup of auxiliary slots remains a separate consumer operation; no replacement heart texture is introduced.

The health builder now has a host presentation consumer. Only `--ui-profile topscreen` asks the lifecycle bridge to emit these heart primitives. It reads health through the typed OoT3D UI snapshot, reads the original pulse phase from the owning global action state, resolves the existing native `pause_top_page` CTXB identity, and submits normalized native atlas UVs through the common overlay renderer. The default `oot3d` profile retains its prior guest-only presentation path.

Record 11's gameplay compositor is source-ported. In addition to
the health, contextual, touch and Item I/II producers, it now copies the first
five live native item/action quads, reconstructs the rupee, dungeon-key and four
ammo `PauseCounter` instances, and emits group-39 quads 34 through 38. The
latter retain the exact three native layout variants and derive the payload's
former `0x005D4118` flag directly from `PlayState + 0x20AC + 0x12B8`, pause
state 11, and the alternate HUD renderer field at `0x004FC648 + 0x40`. No
payload BSS is recreated. The compositor gate reads the original scene mode,
health, player transition/readiness, runtime mode and competing pause-page
owners.

A direct scalar audit of the reconstructed IPS closes two earlier transcription
errors in the six-quad touch producer. The four 32-pixel action circles are
centered with the payload's `16.0` half extent, and quad 5 samples the native
directional-pad rectangle `(432,218,30,30)` rather than the unrelated heart
column. The resulting Vulkan capture is
`native_game/topscreen_gameplay_payloadfix_vk_20260722_000020.bmp`. Gameplay HUD
primitives are clipped to the native 400x240 top surface before that surface is
centered in the 16:9 host canvas. This preserves the payload's `+400` off-screen
translations as visibility state instead of leaking them into the wider host
surface.

The six native touch-renderer source quads `86..91` are the Epona stamina row.
They are copied with their original position, UV, color and alpha streams and
are classified as `HorseStamina` primitives. Their guest `+400` X translation
remains the authoritative hidden state while Link is on foot; it is evaluated
only after the common 400x240 canvas clip. Runtime diagnostics therefore
distinguish alpha-visible source quads from primitives that remain visible
after clipping. A `hudtest_after120` run reads all six quads but reports zero
visible stamina primitives, matching the on-foot state without adding a
host-side horse predicate.

Every runtime report includes `topscreen_presentations`, indexed by native UI
subsystem, plus the compatibility alias `topscreen_gameplay_primitives` for the
last gameplay frame. Each record carries its
semantic role, original owner and descriptor, source quad, native texture
identity, destination, UV, color, layer and post-canvas visibility. This makes
contextual HUD families auditable from renderer data instead of relying on a
Windows screenshot. It also preserves the architectural split: the native
top-screen A action and minimap draws remain guest PICA output, while only the
payload-owned copies and producers appear in this host presentation list.

The focused native export in
`tools/oot3d/decomp_support/analysis/topscreen_counter_ghidra_export` verifies
the remaining counter layout instead of tuning it from screenshots:
`PauseCounter_UpdateDigitPositions` places slots right-to-left at 10-pixel
intervals with native widths and style heights, while the payload applies its
recovered origin and scale afterward. The apparent spacing between the rupee
icon and a one-digit count is therefore retained as native behavior.

Targeted Vulkan draw diagnostics identify the minimap independently of screen
captures: it is the indexed six-vertex draw using the native 128x128 format-13
texture. Its loader-0 stream is copied from `0x090FA700`, the materialized
position field at `+0x10` of the one-quad renderer at `0x08009250`. The fixed
global `0x004FDA84` points to that renderer. Original `code.bin` references
lead to `0x00418A58`, which constructs the `smenu_top_map00.ctxb` resource and
its quad renderer, plus its update and draw owners at `0x0042C0C8`,
`0x004416E4`, `0x00441AA8`, `0x002F233C`, and `0x002FBC50`. Payload
`0x005C9940` reads `0x004FDA84` as the native minimap lane gate, while
`0x005CBD8C` applies its extent-derived right-edge offset immediately before
the unchanged `0x002FBC50` submit. The source port observes that stable target
entry only when LR is `0x0041EB2C`; this remains effective inside whole-AOT,
where the patched call instruction is not itself a materialized block entry.
The selector corresponding to the payload minimap word starts at its native
initialized value `1`, leaving the map visible. In this port its persistent
value comes from external `minimap_visible`; Select is not consumed for a
preference toggle. A deterministic Vulkan run from `hudtest_after120` records five
successful applications with no failures and moves the live minimap renderer,
including its marker, from X `2..130` to X `306..434`. The framebuffer artifact
is `native_game/topscreen_minimap_visible_default_20260722/2.png`.

`0x005C8244` contains the fully recovered four-quad magic meter. Its source contract preserves the native atlas coordinates, mirrored atlas span, health-capacity-dependent row, normal/double-magic variants, signed current-magic input, and exact fill-width equation. Unacquired or unknown magic state produces no geometry.

The focused original-code export in `tools/oot3d/decomp_support/analysis/topscreen_magic_meter_original_ghidra_export` resolves the former ambiguous `PauseTouchButtons_UpdateHoldProgressGeometry` name. Original `0x0044483C` reads `SaveContext +0x4E`, `+0x50`, and `+0x47`: the accepted OoT3D fields `magic_acquired`, `double_magic_acquired`, and current `magic`. The lifecycle bridge now consumes those fields through `UiHudMagicContent`, not absolute memory reads, and emits `MagicFrame`/`MagicFill` primitives only under the TopScreen profile.

The native Gear, Map, and Items tabs remain touch-owned. Their recovered centers are `(96,221)`, `(160,221)`, and `(224,221)` in the original 320x240 touch domain. Interactive pause sessions map the host pointer through the contained 400x240 top-screen presentation and then invert the payload's horizontal `400/320` projection into that unchanged touch domain. The renderer owns containment and letterbox rejection, so clicks remain aligned at arbitrary output aspect ratios. Desktop shortcuts `G`, `M`, and `I` synthesize those same native touches, allowing direct page entry without relying on the TopScreen `START` remap; unchanged OoT3D `START` would open the save page.

Record 26 branches from `Camera_Normal1` at `0x0023A930` to payload entry
`0x005D001C`. The typed source contract preserves the incoming scalar in the
native camera field at `+0x124`, multiplies only the continuation value by the
recovered factor `1.3` while the extended camera owns the update, materializes
the original stack slot at `sp+0x60`, and preserves the native continuation at
`0x0023A938..0x0023A948` through its real AOT successors `0x0023A94C` and
`0x0023A974`. The default profile and the inactive extended-camera path retain
the original value.

The free-camera reconstruction has a standalone typed camera core. It
matches the payload's persistent pitch/yaw/distance state, right-stick
deadzone, game/cutscene/main-camera/player gates, complete blocked-setting
set, Master Quest horizontal inversion, pitch clamp, Link-age head height,
distance curve, binary-angle trigonometry, and orbit interpolation. The A32
consumer intercepts the record-36 `Camera_Update` site at `0x002D84C8` and
executes that core through a staged guest bridge. Water, interface, background
collision, quake, and camera-data queries are delegated to their unchanged
native OoT3D helpers; only the recovered mod delta is implemented in host C++.
Inactive ownership resumes the original instruction stream. Right-stick input
is supplied by the portable host input layer. Speed and inversion are
initialized from external JSON and are not mutated by gameplay chords.
Runtime counters distinguish update calls from frames where the free camera
actually owns the update.

Record 39 reaches the payload's central per-frame ticker at `0x005D048C`;
record 40 reaches presenter `0x005D063C -> 0x005D024C` and then resumes the
original `0x002E25F0` continuation.
The source port does not reproduce its 3DS service initialization, shared
memory access, HID polling, or framebuffer writes. The recovered option
mutator and its independent 30-tick timer remain covered as offline semantic
evidence, but the live runtime does not call them because persistent options
come from external configuration. Presenter `0x005D024C` is represented by typed
`GameplayHud` primitives for the four recovered inversion labels and seven
speed labels. When both notices are active they use native Y positions 10 and
20; one notice alone uses Y 10. The texture provider builds a 128x256 atlas
from the exact required rows of the payload's 6x10 bitmap font. It never
executes the ARM blob, reads a lower-screen framebuffer, or imports the
optional 4K emulator texture pack.

The historical deterministic timeline
`tools/oot3d/native_game_runtime/input_timelines/topscreen_camera_option_feedback.json`
pressed native `L+R+Up` on the first `hudtest_after120` guest frame while
validating the recovered predecessor behavior. Eight-frame
Vulkan capture `native_game/topscreen_camera_option_feedback_20260723.bmp`
shows the recovered `...|...` notice. Runtime diagnostics report zero lower
framebuffer overlays, four provider resolves (three native CTXB surfaces plus
the generated font atlas), zero texture failures, and 224 drawn primitives.
The same timeline under profile `oot3d` performs zero TopScreen camera calls,
texture resolves, and host primitive draws.

The camera gate is now reported as a typed blocker mask rather than inferred
from a missing active count. On `hudtest_after120`, the recovered owner is the
main camera with status 7, setting 1 and no blocker. A no-stick ten-frame run
reaches `Camera_Normal1` 19 times and leaves ownership inactive; the matching
right-stick run owns all 19 `Camera_Update` calls. Direct Vulkan present
captures at frames 2, 4, 6 and 8 in
`native_game/topscreen_camera_vk_sequence_20260722` show the resulting orbit
progressing continuously. The same validation exposed an unrelated lifetime
bug: the Ship context was destroyed before TopScreen locals that referenced
it. Context teardown now occurs only after the A32 window returns, eliminating
the former `0xC0000005` on bounded TopScreen runs.

The Link-age branches follow the native `SaveContext.linkAge` encoding exactly:
zero is adult and one is child. Consequently the recovered camera uses a
50-unit head offset and 200-unit base distance for adult Link, and 38/150 for
child Link. Earlier host-only values had these two branches reversed.

The direct pause hook at `0x0042B86C` owns `PauseQuestPage_SubmitModels`. Its payload routine transforms existing OoT3D model quads before retaining the native submit path. The source port now reproduces its three exact geometry operations: move selected quads outside the 400x240 canvas, apply the runtime left-region offset, or scale selected right-region quads by `0.95` around their centroid and recenter them at `(288, 24)`. Z coordinates and untouched regions are preserved.

A selective native decompile confirms the render-buffer ABI used by that submit path: quad count at `+0x00`, base positions at `+0x0C`, materialized positions at `+0x10`, and XY offsets at `+0x1C`, with a `0x30`-byte quad stride. A transactional guest-memory adapter applies the transformation only to the eight materialized Quest quads. The projection producer is also source-ported: external `minimap_visible` supplies its persistent alternate-page state, X derives from the two native extent values and selected layout, Y follows the native 3000-unit wrap, and the Quest gate derives from the scene owner and native renderer state. These values live in typed host profile state; the consumer does not recreate payload preference storage.

Direct ARM disassembly of payload entries `0x005C7924` and `0x005CBD8C` identifies those producers. Pause state is the word at `0x0050AF34 + 0x34`; the special branch follows the native scene-owner pointer at `0x005043D4 + 0x0C`, requires mode bytes `3/2` at `+0x100/+0x101`, and tests the nested owner reached through `+0x20AC` and `+0x12B8`. The source port decodes this state with checked guest-memory reads and retains dynamic offsets in host state rather than recreating payload globals.

The four leading bypass predicates are native page-owner flags: Gear at `0x0050446C+0x18`, Items at `0x005066F8+0x34`, Dungeon Map at `0x00506CB0+0x38`, and System Menu at `0x0050A508+0x28`. When any is active the payload skips Quest relocation. The TopScreen runtime now observes `0x0042B884`, after native XY-offset materialization and before position upload, transforms that native stream only when all four gates are clear, and then leaves the original upload and draw path intact.

`0x005D00A4` changes the pause render-target contract to a 400x240 top target and a 320-pixel-high viewport/scissor path where applicable. `0x005CFEF0` retains the native pause draw order while temporarily suppressing selected lower-screen child renderers. This is the intended seam for pause composition.

More precisely, `0x005D00A4` is only a stateful dispatcher. Its selected implementation at `0x005C7834` handles native renderer commands `0x400` and `0x401` in the rotated 3DS framebuffer convention confirmed by `glViewport`: `0x400` binds renderer field `+0x3C` with viewport `(0,0,240,320)`; `0x401` binds field `+0x38` at Y `40`, width `240` or `480` from the native half-height flag, and height `320`. Other commands retain the native stored command without forcing a target. This deterministic command translation is now a typed source contract; the dispatcher's 900-tick mod state is not imported as game state.

The host integration observes the unchanged native target routine at `0x00300588`, holds the resulting plan only across its immediate nested `glBindFramebuffer` and `glViewport` calls, and rewrites those call arguments. The original routine still owns its enabled check and stored command. The transient host plan is cleared after viewport submission and exists only under `--ui-profile topscreen`.

The target rewrite is additionally gated by the recovered `0x005CCC48`
route, exactly like wrapper `0x005D00A4`. This is required: applying the
`0x400`/`0x401` target mapping merely because the TopScreen profile is enabled
promotes the complete lower framebuffer during file-select and unrelated UI
flows. Outside the route the dispatcher is now wholly native.

The pause-draw wrapper is reached only from callsite `0x0041982C` and returns to `0x00419830`. Its suppression branch snapshots and clears Gear `0x00504484`, Items `0x0050672C`, Dungeon Map `0x00506CE8`, System Menu `0x0050A530`, and pause-state `0x0050AF68` around the unchanged `PauseUi_Draw`, then restores every word. The native routing predicates, transition latch, and 180-command exclusion window have now been recovered and source-ported. The live TopScreen path applies the transactional suppression only for this verified call/return pair and resets transient host state after savestate loads.

Record 42 at `0x00419850` is source-ported as a three-stage native call transaction. When the recovered alternate-path predicate is active, it selects viewport `(0,40,480,320)`, calls the unchanged renderer at `0x0041D1A8`, and restores `(0,0,480,400)`. Otherwise it performs only the original call. The transaction state is host-transient and is reset on savestate load.

Record 44 at `0x00300538` uses the same predicate. It snapshots byte `+0x6C` from the four native renderer instances referenced by `0x005C4CD4..0x005C4CE0`, clears those bytes around the unchanged update at `0x0042A278`, and restores the snapshot afterward. The source port performs the same checked transaction and keeps no renderer state in the save file.

Record 47 at `0x00300550` is the scene-mode-5 viewport wrapper. Its source transaction selects `(0,-80,240,480)`, calls the unchanged renderer at `0x0042CBCC`, and restores `(0,0,240,320)`. Because scene mode 5 itself makes the payload's preceding routing helper succeed, no payload-owned latch is needed for this predicate.

Record 45 replaces the native display-ready predicate at `0x003102A0` with
payload entry `0x005CF44C`. The replacement first mirrors the native display
float and disabled byte into payload-private storage, then evaluates the same
native condition: disabled byte clear and display float greater than zero. Its
only additional gate is BSS word `0x005D40F8`. Exhaustive references in the
injected image show two reads and no writer for that word, while its zero BSS
initial value is never changed. The mirror is consumed only by the injected
payload and has no native-game side effect. The typed port therefore closes
record 45 by retaining the original function unchanged; adding a host hook or
persistent state would introduce behavior absent from the mod.

Payload helper `0x005CCC48` is represented as a typed transient route machine
rather than an opaque gate. It remembers the previous runtime mode, starts a
900-call window on transitions from modes 1, 2, or 4 to zero, advances through
the native scene-variant handshake, and otherwise selects scene mode 5 or the
native alternate path. Record 35 uses that exact result to retain or suppress
the unchanged draw at `0x0041C500`. Record 43 independently suppresses the
unchanged draw at `0x004228E4` only for scene mode 7. Both source blocks are
profile-only and reset their transient routing state after savestate loads.

Records 30 and 32 wrap the two native `0x00300240` command submissions. The
payload's final command transform is exact and deliberately narrow: while the
same route machine is active, `0x400` and `0x401` exchange places; inactive
routes and every other command pass through unchanged. The source port applies
that transform to `r1` at the two original callsites and delegates submission
to the untouched native function.

Record 28 at `0x0041E984` now retains the unchanged native
`PauseTouchInput_Update` call at `0x004289DC` and source-ports its complete
post-call delta. Payload helper `0x005CCDFC` disables native touch hit testing
for the shared transition route, scene mode 3 with runtime modes 1/2, scene
variant 2 sequences 3..6 outside the runtime latch, or an active pause-camera
secondary layer. The typed adapter reproduces the payload's transient
mode-3-preserving latch and writes the exact six native touch-state halfwords
at `0x0050BB3A..0x0050BB48`: two `0x7FFF` coordinate sentinels and four zero
fields. It is reset on savestate load and no payload storage is recreated.

Record 41 at `0x00300544` now source-ports wrapper `0x005D0114` and payload
controller `0x005CCEF4` around the unchanged draw at `0x00427A3C`. The typed
route preserves the native scene-mode predicates, the exact 60-call delay and
24-step alpha fade, updates both renderer alpha bytes and native float color
blocks, reconciles the paired renderer/owner tables for sequences 4 through 7,
and clears the terminal renderer visibility byte. The three payload globals
used for delay, fade, and ownership conflict are transient host state and reset
after savestate loads; the source port does not execute or persist payload
memory.

The outer portion of record 33 is source-ported at callsite `0x00419838`.
When the recovered route is active it selects viewport `(0,40,480,320)`, runs
the unchanged `PauseOverlay_DrawAllGroups` at `0x0041F308`, and restores
`(0,0,480,400)` for scene mode 5 exactly as the wrapper does. Its subsequent
payload-owned producer at `0x005CD1E0` is now an independent typed contract:
it builds the exact two mirrored edge quads, including the native 256x256
`item_page` texture selected through `RendererTextureDescriptor_GetBuiltin(3)`,
transition retraction, scene-byte fade, and RGB modulation. Direct disassembly
of the patched payload proves each source quad
is `32x240`, not `32x32`. The payload authors it in the promoted 320x240
domain; the record-33 viewport expands X by `400/320`, yielding the two
`40x240` top-screen strips. The host presenter applies that projection and
centers the 400x240 surface in its 16:9 canvas. Reference captures independently
place the strip boundaries at native X `40` and `360`. Record 33's source path
invokes the fully determined fixed form only after the mode-5 restore; it is
therefore not forced into ordinary scene-mode-3 pause frames. The same general
producer remains available to the separate record-34 controller and persists
between host HUD observations while that controller owns it; it is not
conflated with health or touch HUD geometry and does not recreate payload
renderer objects.

Record 34 at `0x0041988C` now source-ports the deterministic visual-routing
core of controller `0x005CD918` around the unchanged draw at `0x0041AFAC`.
It derives the payload latch from native renderer phase 8, retains native draw
for phases 1 and 2, suppresses the routed and scene-mode-7 cases, and applies
the exact 20-step `step * 0.05` fade to all 14 renderer fields and both native
color blocks. Its alternate transition reuses the typed edge producer after
the native draw. The two native auxiliary renderers are now repositioned by
their recovered `(45,-6)` and `(-25,-6)` offsets without cumulative drift and
drawn through their unchanged renderer entry inside the recovered 480x320
viewport transaction. Focused Ghidra export proves that producer
`0x005CD64C` reads the texture resource embedded at
`0x00504000 + 0xFA0` (`0x00504FA0`), configures it through native
`RendererTextureStage_ConfigureTextureUnit` (`0x00348A64`), and emits a
two-quad file-select strip. It is not an audio sequence. Original loader
`0x00481390` writes the localized CTXB at that exact address during
`oot3d_init_sys_message_font`; disposer `0x002EE468` destroys it through the
native allocator and clears the same field. The source presenter consumes this
unchanged native lifecycle directly, so record 34 requires neither payload
storage nor a duplicate host loader and is source-ported in full.

The six `PauseIconGroup_Refresh` calls patched by records 18-23 all use payload wrapper `0x005D014C`. Its complete delta is a single selector transform before the unchanged native icon builder: selector `0xC5` becomes arguments `(0xE2, 0x0B, 0x1C, 0x1C)`, while every other selector passes through. The source port applies this contract at the native builder only when LR identifies one of those six callsites.

Four closed flow patches are also implemented as profile-only source blocks before AOT dispatch: record 16 suppresses the lower-screen touch callback only for native pause states 12 through 19; record 24 reproduces `tst r0,#0` without patching code; record 25 skips the identified lower-screen-only block; and record 27 returns immediately from dungeon-map touch cursor input. These blocks are registered as native candidates only for `--ui-profile topscreen`, so the default profile still executes the original instructions.

Record 17 is implemented as a two-phase source transaction around the
unchanged `PauseItemsPage_Update` call. A ZR/ZL press in native Items state 2
seeds the same temporary action bit and selection IDs (`5`/`0x17`) as the
payload. After the native update returns, state `0x0B` restores the three
cursor markers with the recovered `7`/`0xBF` coordinates. Other states execute
the original call without interception.

Record 12 uses the same native pause-state interval to gate `PauseUi_Update`'s call to `0x0042DDA8`. The source block now either performs the original BL with its exact return address or advances past it for states 12 through 19; it does not duplicate the native callee.

The understood scalar layout changes are encoded in `oot3d_top_screen_mod_profile.cpp` with expected original values. Applying them performs a complete preflight before any write, so an unsupported `code.bin` cannot receive a partial layout. No address is inside `SaveContext`.

The scalar coverage now also includes the native Items, Gear, Dungeon Map,
shared Map-page, and System-page vectors from records 51-75 and 81-87. Record
51 is an exact no-op and is intentionally omitted. The seven records at
`0x005074C8..0x00507530` belong to `PauseMapPage_Init`'s 30-quad stream inside
`gPauseDungeonItemPanelState`, not to Quest Status. These vectors are
initialized inside the corresponding native pause-page state blocks; the
source port keeps their original storage and consumers and changes only the
verified initial geometry when TopScreen is selected. Records 76-80 and 88-89 cover the related
native extent/projection constants. Records 50, 90, and 91 are now typed
control-flow contracts: they skip the identified lower-screen-only Quest,
pause-update, and gameplay-composition paths through native-candidate dispatch,
without modifying guest instructions. Record 49 is the remaining System-page
layout inset and is included in the same atomic scalar preflight. Record 76 is
a pair of float literals consumed by `PauseQuestPage_InitAuxiliaryRenderers`.
The source port now intercepts the two original `vldr` entries and supplies
their typed `s0=33` and `s1=110` results before continuing normally. The guest
code page remains unchanged and non-writable.

The gameplay-composition contract records the exact conditional branch patched
by the IPS, `0x002E27C0`, and its target `0x002E2A04`. Because that instruction
is internal to the AOT block beginning at `0x002E278C`, the whole-AOT observable
exit occurs at the block boundary and the source consumer executes the original
`0x002E278C..0x002E27BC` register, VFP, stack, literal-load, and flag semantics
before applying the unconditional branch. Guest code remains immutable and no
native side effect preceding the IPS patch site is omitted.

## Runtime architecture

`--ui-profile oot3d` is the default and performs no TopScreenMod work. `--ui-profile topscreen` enables only source-ported features. The selection lives in host launch state and bootstrap diagnostics, never in game save data.

The launcher exposes the external profile directly:

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeGame.ps1 -SkipBuild -UiProfile topscreen -TopScreenConfig .\config\topscreen_ui.example.json -Renderer vulkan
```

Omitting `-TopScreenConfig` under the TopScreen profile uses the same documented
host defaults; it never falls back to guest save preferences. Passing a
TopScreen configuration while `-UiProfile oot3d` is an error, which prevents
an apparently inactive profile from influencing the baseline.

TopScreen gameplay retains whole-AOT and uses explicit observable exits for
the gameplay-composition delta. While the recovered native pause route is
active, the dispatcher selects block-granular mass-AOT because several IPS
records patch internal pause callsites rather than function/basic-block
entries. The route uses the native scene/runtime/transition contract, not the
unrelated page-state word at `0x0050AF68`. This is a semantic routing decision,
not a permanent global TopScreen performance gate; gameplay no longer loses
whole-AOT coverage.

Single-screen promotion of the native lower-screen frontend belongs only to
the unchanged `oot3d` profile. It requires an active native File Select or
Name Entry controller together with the persistent non-gameplay
`SaveContext.game_mode`. The `topscreen` profile owns its source-relocated
frontend directly and never clones or overlays the lower framebuffer.
Callback order cannot claim ownership because those updates share a global
chain with gameplay. A stale nonzero controller state therefore cannot keep
the lower framebuffer promoted after game mode returns to gameplay.

The corrected whole-AOT path now preserves frontend rendering as well as the
interpreter path. The previous black output was not a framebuffer ownership
or transparency failure: register-specified ARM shifts were lowered through
the immediate-shift helpers, so `LSR Rs` with a zero shift incorrectly behaved
as immediate `LSR #32`. During PICA output-map construction this dropped the
first `0x03020100` POSITION map and collapsed menu geometry. Commit `50c243ad9`
introduces the register-shift helper and generator/runtime regressions. With
that correction, `topscreen_file_select_shiftfix_20260723_000040.bmp` shows
the native File Select over the top-screen sky, and
`topscreen_{gear,map,items}_shiftfix_20260723_000080.bmp` validate the three
touch-owned pause pages from the same gameplay checkpoint. The default
`oot3d` profile promotes the active native lower-screen frontend on the
single-screen host; TopScreen does not use that promotion path.

Name Entry now has equivalent native-path evidence. The reusable savestate
memory patcher validates the native container, MsgPack document, region
ownership, payload length, and FNV-1a checksum before changing diagnostic
guest data. Starting from the frame-574 File Select checkpoint, the fixture
clears both the persistent three-slot validity table at `0x0055BEA8` and the
already materialized File Select cache at `0x0055B53C`; it does not alter a UI
controller state. The deterministic two-A input timeline then follows
`FileSelect_Update` into original `NameEntry_Activate` at `0x0043E220`.
`topscreen_name_entry_coherent_20260723.json` records 207 active Name Entry
frames and final native controller state 4. Framebuffer captures at frames 20
through 200 show the native keyboard, editable name, animated cursor,
Cancel/OK controls, and top-screen sky without a lower-framebuffer overlay.
The focused `FileSelect_Update` evidence is preserved under
`topscreen_name_entry_flow_ghidra_export`; its entry list is kept separately
in `topscreen_name_entry_native_entries.txt` so unrelated focused exports are
not overwritten.

The diagnostic checkpoint is reproduced from the unmodified frame-574 state
with:

```powershell
python tools/oot3d/native_game_runtime/patch_native_a32_savestate_memory.py --input I:\oot3dre_work\native_game\checkpoints\file_select_good_no_whole_frame574_20260723.oot3dsav --output I:\oot3dre_work\native_game\checkpoints\file_select_empty_slots_coherent_20260723.oot3dsav --write 0x0055BEA8:000000000000000000000000 --write 0x0055B53C:000000000000000000000000
```

The native transition is then driven by
`input_timelines/file_select_empty_slot_to_name_entry.json`; no checkpoint
containing a fabricated Name Entry controller is required.

The additional TopScreen gameplay presenter is gated by the native pause state
at `0x0050AF68`. It emits no health, touch-control, or hold-progress copies
while any pause/menu state is active; the unchanged native pause renderer owns
that frame. This prevents the reconstructed gameplay-only quads from being
drawn a second time around the native pause UI.

TopScreen `START` routing is now source-owned at the desktop HID boundary.
While gameplay owns the UI, a `START` sample is removed from the native button
word and converted to the original Items tab touch target `(224,221)`. This
uses the same native page-opening path as the mod instead of opening the
system Save/Quit menu. Touch ownership and this routing decision use the four
native page gates for Gear, Items, Dungeon Map, and System Menu; the broad
OoT3D `pause.open` observation also covers the ordinary gameplay touch HUD and
must not block the remap. A held opening gesture remains suppressed until it
is released, so it cannot become a close edge after Items has opened. The
remap is additionally gated by an observed `GameplayHud` lifecycle and the
decoded `SaveContext.game_mode` gameplay values `0/1`. Title, File Select, and
Name Entry (`game_mode >= 2`) therefore retain their original `START`
semantics; the TopScreen Items gesture cannot consume frontend input before a
gameplay owner exists. Once a new `START` gesture arrives while a native pause page owns
the UI, the source
dispatcher consumes the `START` edge and reproduces payload root
`0x005C9940`: it selects the active page in Items/Gear/Dungeon Map/System
priority, runs that page's original OoT3D reset function, writes pause state
`2` and scene control `0`, then calls native `PauseInput_SetTouchEnabled(0,1)`.
The original functions run through the generated A32 registry and the normal
CTR host-service handler; no injected payload code is executed. The routing
applies equally to physical and deterministic timeline input, is disabled
completely by `--ui-profile oot3d`, and does not affect the diagnostic `G`,
`M`, and `I` page shortcuts.

Physical gamepads use the same boundary. The desktop SDL layer maps face
buttons, shoulders, Start/Select, D-pad, left stick, right stick, and trigger
axes to `NativeA32InputFrame`; native gameplay and the TopScreen source port
consume one shared state. Trigger axes are the direct ZL/ZR producers used by
the recovered item-lane wrappers, while the right stick feeds the recovered
free-camera path. Keyboard and controller samples merge by button union and
greatest axis magnitude, and deterministic timelines remain authoritative in
automated runs.

The adjacent `B` route is also source-owned. A fresh `B` edge on Items, Gear,
or Dungeon Map arms the exact five-tick payload transition instead of treating
the native page close as the final action. During that window the dispatcher
observes the original page state and accepts the payload's terminal values:
`3/14` for Items, `3/10` for Gear, and `3/8/10` for Dungeon Map. It then calls
the matching original page reset, writes native scene control `1`, calls
`PauseInput_SetTouchEnabled(1,0)`, writes pause state `0`, and invokes original
`PauseSystemMenu_Open` at `0x0043ACA8`. The five-tick counter is transient host
state and is cleared on savestate load; no payload or save memory is retained.

The deterministic `topscreen_start_items.json` run in
`topscreen_enter_items_gatefix_20260723.json` reaches native `items_state=2`,
keeps the page gate active, and records zero close transactions. The
open/close sequence in `topscreen_enter_items_close_gatefix_20260723.json`
records exactly one Items close transaction and finishes with
`items_state=0`, no active page gate, and no lower-screen overlay. Replaying
the opening input with `--ui-profile oot3d` in
`oot3d_enter_default_control_20260723.json` records zero TopScreen close
transactions and preserves native `START`.
The `topscreen_start_items_b.json` run in
`topscreen_b_system_open_20260723.json` records one source transition from
Items, ends with native `system_state=2`, and shows the original Save/Quit
menu in the Vulkan framebuffer. The same timeline under `--ui-profile oot3d`
in `oot3d_b_system_control_20260723.json` records zero source transitions.

Pause-page scalar tables are copied into heap-backed render streams once by
the native constructors. Consequently, selecting TopScreen after loading an
ordinary OoT3D savestate cannot be implemented by changing only those scalar
tables: the old positions and UVs already exist in the restored heap. The
source port now rebinds the initialized Items (37 quads), Gear (41 quads),
Dungeon Map (50 quads), shared Map page (30 quads), and System Menu (78 quads)
streams from their verified owner tables by reproducing
the native `PauseRenderBuffer_SetQuadRects` and
`PauseRenderBuffer_SetQuadTexcoords` semantics. The operation is staged before
its first write, preserves Z and all per-frame offset streams, and is a no-op
when a page constructor has not run yet. The Gear framebuffer capture in
`topscreen_gear_rebind_20260723_000060.bmp` confirms that the five zero-extent
TopScreen quads no longer leave the original `B BACK` presentation behind.
Items, Dungeon/World Map, and System Menu use the same typed mechanism rather
than draw filters. The shared Map descriptor is derived from native
`PauseMapPage_Init` at `0x00480BD8`; the System descriptor is derived from
native `PauseSystemMenu_Init` at `0x004676A4`.

Implementation order:

1. Apply verified scalar HUD/pause layout contracts atomically after mounting the original process image.
2. Replace health, magic, quest, and pause composition hooks with typed C++ one function at a time, retaining native guest mechanics until each replacement is complete.
3. Feed the typed Item I/II resolver from host semantic ZL/ZR and verified D-pad compatibility inputs; keep 3DS hardware polling out of the runtime. (Complete.)
4. Import the mod's native-resolution CTXB atlases through the validated,
   profile-owned override pack; never use its optional 4K emulator PNG pack or
   mutate guest/save memory.
5. Validate the optional camera bridge from a deterministic gameplay checkpoint and add its transient option notices without coupling it to the default profile. (Complete.)

Every tranche needs a default-profile regression test, a profile-specific semantic test, and a framebuffer capture only when visual output is involved. A successful IPS application is not a test oracle for the source port.
