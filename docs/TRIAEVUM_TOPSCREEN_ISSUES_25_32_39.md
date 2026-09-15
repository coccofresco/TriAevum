# TopScreen HUD ownership and aspect preservation

## Scope and status

Checkpoint: 2026-09-15. This is a verified implementation increment, not closure
of all three reports. Issues: [25](https://github.com/coccofresco/TriAevum/issues/25),
[32](https://github.com/coccofresco/TriAevum/issues/32),
[39](https://github.com/coccofresco/TriAevum/issues/39).

- Native gameplay HUD now uses a centered, uniformly scaled native canvas,
  matching the existing host-composed HUD and inverse pointer mapping.
- Minimap marker geometry no longer receives the independent button/HUD scale.
- ZR bow use is demonstrated in a real checkpoint, including ammunition
  decreasing from 50 to 49. This does not establish every item/context works.
- Pause/map screens, the reported dungeon marker case, independent ZL item use
  and the Sheikah Stone crash still need scenario-specific verification.
  Do not close the issues based on this checkpoint alone.

## Evidence and ownership

[CmbVShader.vsh](https://gist.github.com/M-1-RLG/c0a3ef277241781297ddeb4763dfbc88)
projects positions through c0..c3 after model/view transformation. Its board
branch and stereo flag do not establish UI ownership or preserve a host window's
aspect ratio. Comments are supporting evidence, not classification rules.
No shader equations were replaced by scene-specific corrections.

The copied `ui_native_workflow_closure.cpp` identifies these exact draw calls:

| Owner | Entry | Caller return |
| --- | --- | --- |
| PauseQuestPage_Draw | 0042B9F4 | 003004E0 |
| PauseUi_Draw | 0041EC50 | 00419830 |

The OOT3D composition adapter tracks command cursor 0054CC4C across these calls.
It publishes exact UI command spans, including models emitted inside the UI.
The frontend propagates their domain independently of adjacent scene draws.
The enclosing 00300328 DrawViewPass is deliberately NOT a UI owner: it also
executes world callbacks. A framebuffer experiment using that broad scope lost
grass/world attribution and was rejected. A regression test excludes it.

Shared renderer `pica_ui_canvas.h` fits UI viewport/scissor coordinates uniformly
and centers them; Scene/Unknown retain their existing projection. Physical
framebuffer dimensions include native rotated/doubled sampling: fit the native
480x400 canvas in that space, equivalent to logical 400x240 after scanout.
Do not substitute a logical 240x400 framebuffer in this calculation.

TopScreen 2.1.1 reference function 005C7F48 translates the minimap marker region
but does not apply the general button scale to it. The port did both. The fix in
`TransformTopScreenQuestGeometry` preserves the map-owner translation and omits
the second scale/margin transform for those quads. Tests cover scales 0.6/0.8/1.2.
Reference archive identities and provenance remain in
`OOT3D_TOPSCREEN_2_1_1_PORT.md`; private exports are not distributed.

## Verification

- Renderer suite: 19/19 passing, including native/host canvas and pointer inverse
  parity over five aspect ratios and three render scales.
- Native frontend and composition tests: passing; mixed Scene/UI command lists
  retain neighboring scene domains, UI spans are consumed once, reset clears scope.
- TopScreen profile and native UI lifecycle tests: passing.
- Actual NRI framebuffer captures: 1720x720 ultrawide and 960x720 4:3, adult Link
  outside Castle Town, default scene effects present. HUD retains native proportions;
  A/B, item slots, minimap and host HUD share the centered canvas.
- Ultrawide capture: 302 UI entries/exits/spans, no overlap rejection.
- Input probe: ZR activates the bow and consumes one arrow on release. ZL was
  subsequently pressed while already aiming; this is not independent ZL proof.

Private reproducibility artifacts:
`%TEMP%/TriAevum-ui-canvas-ultrawide/leaf-runtime.json`,
`leaf-framebuffer_000150.bmp`, `four-three-framebuffer_000150.bmp`,
`items-runtime.json`, `items-framebuffer_000130.bmp` and `_000180.bmp`.
Checkpoint: `J:/TriAevum-verify-20260910/win-field-x2/checkpoint.oot3dsav`.
Runtime: `J:/TriAevum-verify-20260910/runtime/TriAevum.exe`.
These are correctness captures, not performance measurements. Windows desktop
screenshots were not used. Linux was not rerun in this increment.

## Next validation

1. Test pause inventory/map in ultrawide and 4:3, including inverse pointer hits.
2. Reproduce the dungeon minimap screenshot from #32 and counters from #39.
3. Independently activate ZL and ZR items from neutral gameplay, including the
   post-Forest-Temple state. Preserve native update cadence; icon animation and
   successful query counts alone are insufficient evidence of item activation.
4. Reproduce Sheikah Stone entry/exit with a suitable checkpoint and trace native
   UI ownership/lifecycle before changing routing. No speculative crash fix.
5. Run Linux smoke verification before publishing or marking reports resolved.
