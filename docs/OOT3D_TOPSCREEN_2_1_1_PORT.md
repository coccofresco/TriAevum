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
| Pause behavior | Configurable Select action and configurable Items-to-Save B behavior |
| Items hint | Native selected-model bounds, 90-update delay, 12-update fade, 90-update pulse and exact atlas geometry |
| Assets | Original-size localized CTXB replacements plus named 2.1.1 menu/font profile assets in O3TU v2 |
| Compatibility | O3TU v1 remains readable; default OoT3D profile and original saves remain unchanged |

Equipment and direct-item routes deliberately call or mutate the same native
OoT3D contracts recovered from the payload. They are gameplay intents handled
by the application adapter, not renderer substitutions.

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
