# TopScreen 2.1.1 source port

## Objective

Port the behavior of the official TopScreen 2.1.1 release into typed host
code without executing its IPS or injected ARM payload. The profile remains
selectable at runtime, the default OoT3D presentation remains unchanged, and
save data stays binary-compatible with the original game.

This document supersedes `OOT3D_TOPSCREEN_MOD_SEMANTIC_PORT.md` for current
work. That document remains the historical record for the 1.2 baseline and
for behavior inherited unchanged by 2.1.1.

## Binary evidence

| Evidence | Digest |
| --- | --- |
| Official `topscreen211.zip` | SHA-256 `e0c143c872ccf4ad72033caab768753b147a4033d260a701204a63ee6bc16df4` |
| Official `topscreen211.zip` | MD5 `2457743e14ffe02bfe64adc083f900a1` |
| EUR `code.ips` | SHA-256 `c834030be2fbbfd93db26180333f112e1abcfd85050b09252a8449f06bf90400` |
| Reconstructed EUR payload at `0x005c7000` | SHA-256 `e25bacdae7331714cb8be89044e07f973c3f9701a30bc84e6e282a15aaf89ade` |

The reconstructed payload is 111,157 bytes. Its maintained Ghidra export has
266 functions. `OOT3DHud` public source is useful corroboration for names and
intent, but it is not binary-identical and is never authoritative over the
official archive.

`tools/oot3d/decomp_support/scripts/analyze_topscreen_mod.py` reproduces the
IPS inventory and payload extraction. The archive, Ghidra project, export and
payload are offline evidence; none is a runtime dependency.

## Architecture

```text
official IPS/CTXB evidence (offline only)
    -> typed TopScreen policy and geometry       ui_topscreen
    -> OoT3D guest-state/action adapters         runtime consumer
    -> UI lifecycle and application input        native runtime
    -> generic UiPrimitive + texture identity    UI presentation contract
    -> ordinary native frame composition         NRI renderer
```

The boundaries are mandatory:

- `tools/oot3d/ui_topscreen` owns configuration, pure control/action policy,
  camera math, temporal hint state and geometry. New policy code does not know
  Vulkan, NRI, framebuffers or renderer internals.
- `oot3d_top_screen_runtime_consumer` is the isolated application adapter. It
  owns new 2.1.1 guest addresses, native state reads/writes and calls to
  original OoT3D actions.
- `Oot3dNativeUiLifecycleBridge` owns observation and temporal UI state. It
  requests typed state from the consumer and emits generic UI primitives.
- `Oot3dNativeA32UiTextureProvider` resolves profile texture identities. It
  does not alter draw ordering or backend state.
- NRI/Vulkan remains title-profile agnostic. No TopScreen condition, address,
  texture name or layout rule may be added to the renderer.
- Preferences live only in external JSON and the F1 host settings panel. They
  are not stored in `SaveContext`, guest configuration bytes or save files.
- TopScreen never promotes or copies the lower framebuffer. It reconstructs
  only the required native elements on the top-screen UI canvas.

This follows `OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md`: gameplay, input
and UI policy remain outside the renderer while final composition consumes
stable, generic contracts.

## Implemented surface

| Area | Current source port |
| --- | --- |
| Configuration | Typed `oot3d_topscreen_ui_v2`, strict ranges, atomic external persistence and v1 migration |
| HUD | Normal/Restoration layouts, scale, margins, magic-bar offset, HUD/D-pad visibility and minimap session visibility |
| D-pad | Exact 14-action ABI, separate child/adult mappings and official defaults |
| Native actions | View, Ocarina, minimap, boots, sword, all-boots cycle, tunic, shield, Boomerang and Slingshot routes |
| Item input | ZR/ZL item lanes, native item-query compatibility and direct child-item lifetime handling |
| Camera | Zoom percentage, FOV percentage, free-camera speed/inversion and C-stick aiming speed/inversion |
| Filtering | Five official smoothing levels, exact coefficient table, half-unit deadband, update only on guest refresh |
| Pause behavior | Configurable Select action and configurable Items-to-Save B behavior; START reopens the last page (0x005E21C8); on an open Items or Gear page SELECT shows the native item description over the page (0x005E3D3C, 0x005D3344..0x005D3410) |
| Items hint | Native selected-model bounds, 90-update delay, 12-update fade, 90-update pulse and exact atlas geometry |
| Assets | Original-size localized CTXB replacements plus named 2.1.1 menu/font profile assets in O3TU v2 |
| Version choice | The title's normal / Master Quest choice on the upper screen with the frontend menus, native input unchanged (`TRIAEVUM_TOPSCREEN_FRONTEND_SKY.md`, #28) |
| Compatibility | O3TU v1 remains readable; default OoT3D profile and original saves remain unchanged |

Equipment and direct-item routes deliberately call or mutate the same native
OoT3D contracts recovered from the payload. They are gameplay intents handled
by the application adapter, not renderer substitutions.

### Product hooks

Product builds (`OOT3D_WHOLE_AOT_PRODUCT_MODE`) execute the precompiled
whole-AOT graph through pause routes. `ExecuteProductTopScreenHook` accepts the
sorted list `ProductTopScreenBlocks` (every TopScreen source-port block), and
those blocks are observable exits. The graph reports only the starts of its
basic blocks, and blocks end after branches and calls, so a site in the middle
of a block would never be reached.

`tools/oot3d/native_a32_runtime/whole_aot_observed_sites.json` lists the hook
and resume pcs that are not natural block starts (touch-button draw predicate
0x0042F5CC, the page draw calls 0x00419838/50/74/8C, 0x0041952C, 0x004198B0,
0x0041E984, 0x0041EB28, the scene sites 0x00300520..0x0030055C, the FOV scalar
0x00478EB8 and its resume, the control-flow and float-load contracts, and the
planned layout sites). `whole_aot_program.py` starts a block at each of them,
so a title module generated from it reaches all of them. The product runtime
checks the title module's entry points at startup and refuses one that lacks
any TopScreen site, instead of silently skipping it.

The developer interpreter runs the block policy at every dispatcher entry
before the hook; `ExecuteProductTopScreenHook` does the same, except for the
entry that its own observable exit left, whose policy already ran in compiled
code. A hook that branches to another hooked entry (0x0041E984 resumes at the
input boundary 0x0041E988) therefore still runs that entry's policy.

The camera update entry 0x002D84C4 is observed whenever the TopScreen profile
is active, because it resolves the FOV owner. 2.1.1 stores the play view's
FOV address (`camera->play + 0x198`, its hook at 0x002D92B4) and 005DF388
scales and clamps the value that 0x00478EB8 loads from that address, at every
percentage.

One 2.1.1 patch is deliberately not ported. 2.1.1 branches 0x0041EB40 to
0x0041EC10 and so skips the pause scene dim (a translucent grey submitted
through `EnvironmentRenderer_SubmitViewportConfig`). At 400x240 the page covers
the scene; with wider output the scene shows beside the page, so the port keeps
the native dim and those strips stay dark during the pause. The site stays in
the observed-sites catalogue; an extra block start is harmless.

### Widened Items page

While Items is active (word 0x0050672C; the payload's 0x005E21F4 is a constant
1), 2.1.1 draws `PauseUi_Draw` in the full 400-wide viewport (0x005D32A4) with
the projection offset by 0.8 instead of the centred (0, 40, 480, 320) one.
`TopScreenWideItemsRuntime` (native_game_runtime) ports it on native calls:

- `WidenTopScreenPauseTargetForPage` gives the page target plan the full
  viewport. The plan rewrites the next `glViewport`, so a viewport call of its
  own would be overwritten.
- 0x0041EE1C (005DF428) writes -0.005 and 0.8 into the projection in r1 and
  lets the native upload run. No other call may run there: 0x0041EE08 has
  bound the material's PicaCommandWriter at the command cursor (0x0054CC4C),
  and a GL call in between writes over the same commands. Doing so turned the
  page background white in every other command buffer.
- 0x0041EE74 (005D19F8) moves the page's background columns by -40, its grid
  and icon groups by -17 and its assignment origins to -17 on the selected
  model streams, which the native update rewrites every frame. It then uploads
  the projection with 0.6, calls `PauseItemsPage_Draw` and restores 0.8.
- The cursor subdraws 0x00434CDC (005D1CBC) and 0x00434D48 (005D1BE8) use 0.685
  for a cursor over the grid. The payload's record of the selected cursor's
  bounds (0x005E40A8..BC) is read only by payload code and is not ported.
- 0x005D0494 gives the right pause edge quad no width while Items is active,
  because the page covers that side; the left edge stays.

Dungeon Map shares the full viewport and page projection: Map state
0x00506CE8 must be nonzero and shared map mode 0x004FDABC must equal 1.
World Map stays centred. Verified scalar contracts widen both the dungeon
panel and title to 272 pixels and move the side columns; six native icon
coordinate hooks set R2 to -30 while retaining the ownership branches.
Dungeon Map omits both extra pause edges but keeps the tab hints.

Scene mode 5 (the file select and the other frontend screens) has no pause
edges: 2.1.1 0x005D2078 leaves through 0x005D18F0 (scene owner byte +0x100
== 5) before its edge producer 0x005D0374. The record-33 site 0x00419838
keeps the mode-5 viewport but no longer adds the 1.2 payload's edges; they
showed as stone strips beside the file select once 29d0375 made the site
reachable in product builds. Azahar captures of 2.1.1 show the file select's
sky to the screen edges.

The Save (System) page overlays gameplay on the upper screen. 2.1.1 replaces
the queue-5 draw at 0x0030055C: after the original 0x004228D4 it calls its
compositor before the top transfer. While System is active (0x0050A530) the
compositor sets viewport (0, 0, 480, 400), saves the pause root's
backdrop skip (0x005043F4) and touch (0x0050AF68), writes 1 and 0, calls
`PauseUi_Draw` on the gameplay target and restores both values and the
viewport. `TopScreenSystemPageRuntime` (pause transaction unit) runs the same
native calls and resumes at 0x00300560. System joins the shared wide
predicate, so 0x0041EE1C applies the same projection. The page keeps the
native framebuffer plan rather than PageRedraw's lower-target swap, draws no
pause edges or tab hints, and its later lower-tail draw uses the Tail
suppression set. The payload's Mod Options affordance (0x005DCA80,
0x005DF178) is not drawn; mod settings live in F1.

Exits that run guest continuations are conditional: 0x0041E988 leaves the
graph only while the pause transaction or song text has work, and the page
draw return 0x00419830 only while the item description is drawing or may
start. The page-child restore at 0x00419830 runs before that exit, because a
resumed graph skips the entry it left from.

The item description is the native message 0x005C1878, which the native page
draws under the relocated page. `TopScreenItemDescriptionRuntime` redraws it
after the page with the payload's native calls (material bind, the payload's
two matrices from guest stack scratch, viewport X 0 or 300 by cursor row).
While a page is open SELECT belongs to the description; it reaches the
minimap and Save routes only in the gameplay pause state.

## Deliberate host adaptations

- The mod's in-game settings overlay and preference chords are represented by
  the F1 panel and external JSON. This keeps gameplay inputs available and
  avoids mod-private guest save/config bytes.
- The official 320x240 Items-hint coordinates are promoted through the shared
  400x240 pause presentation transform before becoming generic primitives.
- `custom_menu00.ctxb` supplies the Items-hint atlas. `custom_font00.ctxb` is
  retained as a named profile asset, but host F1 text uses the host UI font.
- Preferences changed for the current session, such as minimap visibility,
  update typed host state only unless the user explicitly saves the JSON.

## Validation state

The following is proven at this checkpoint:

- policy, geometry, configuration and input unit tests pass;
- native Items-owner memory traversal is exercised with a typed fixture;
- the complete game target links with the isolated runtime consumer;
- whole-AOT closure remains 12,419 compiled functions, three host boundaries
  and zero residual A32 entries;
- no NRI, Vulkan or renderer source is changed by the 2.1.1 port.

This does not yet claim full visual parity. Runtime acceptance still requires
a controlled game comparison for HUD layouts, the Items hint, each configured
D-pad action, camera/FOV transitions and localized CTXB selection. Any defect
found there must be corrected in policy, consumer or lifecycle code, not with
a renderer special case.

## Focused verification

```powershell
C:\devkitPro\msys2\usr\bin\cmake.exe --build /i/oot3dre_work/whole-aot-product-consumer --target oot3d_top_screen_items_hint_tests oot3d_native_ui_lifecycle_bridge_tests oot3d_top_screen_mod_profile_tests oot3d_native_a32_input_tests -j 6
I:\oot3dre_work\whole-aot-product-consumer\oot3d_top_screen_items_hint_tests.exe
I:\oot3dre_work\whole-aot-product-consumer\oot3d_native_ui_lifecycle_bridge_tests.exe
I:\oot3dre_work\whole-aot-product-consumer\oot3d_top_screen_mod_profile_tests.exe
I:\oot3dre_work\whole-aot-product-consumer\oot3d_native_a32_input_tests.exe
```

The product gate is:

```powershell
C:\devkitPro\msys2\usr\bin\cmake.exe --build /i/oot3dre_work/whole-aot-product-consumer --target oot3d_native_game -j 6
```
