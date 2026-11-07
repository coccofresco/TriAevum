# Native UI at output density

The reduced scene must be reconstructed **before native UI rasterization**. A
changed-pixel overlay can preserve already-rasterized pixels, but cannot recover
glyph edges lost by rendering UI into a reduced attachment.

## Contract

`ExecuteNativePicaBeforeDrawAnchors` promotes a target only at a schedule-proved
`BeforeUi` anchor, for an explicitly `PreserveNativeUiSampling` overlay, a native
top-screen target, enabled presentation upscaling and single-sample rendering.
Native-memory work is excluded by the existing anchor admission. Unknown or
interleaved ownership cannot manufacture an anchor. Default-false plan inputs
leave ordinary boundaries and existing OoT producers unchanged.

1. Run the scene effect graph and NIS/temporal reconstruction on the reduced
   scene, exporting `SceneComposition` at the native output-density extent.
2. Commit that scene into a separate native target with the same guest identity
   but scale 1.0. Transfer depth/stencil with nearest sampling; UI retains native
   alpha, blending, coverage, viewport/scissor and depth/stencil behavior.
3. Route subsequent owned overlay draws to this companion, not the reduced
   scene. No changed-pixel overlay is needed on this path.
4. Display/capture/raw-copy source resolution chooses the companion only while
   its source serial exactly matches the reduced scene and its resolved receipt
   matches its own content. World/clear/fill writes invalidate that association;
   reset, restore, target teardown and resolution/sample changes destroy it.
5. A presentation-resolved receipt suppresses a second reconstruction or scene
   effect application at scanout. The guest address, memory extent and scene
   resolution policy do not change.

The promoted composition uses a separate canvas: resizing the existing raw-copy
canvas while the same command buffer still references it would be unsafe.
Scene and companion attachments have **separate surface registries**, including
lazy guide allocation and retirement. Their identical guest keys must not cause
promotion to retire scene/NRI bindings referenced by the still-recording command
buffer. A regression tests simultaneous publication and independent retirement.
Companions are retained between frames and are not serialized into guest state.
`TRIAEVUM_NATIVE_UI_GRID_AUDIT=1` emits bounded `TRIAEVUM_NATIVE_UI_RASTER` receipts
with reduced and output-density extents.

## Qualification boundaries

Graph tests cover NIS/FSR/DLSS promotion, resolved/unresolved scenes, missing
opt-in, missing before-UI proof, unavailable upscaler and native-memory rejection,
and single scanout after promotion. Foundation tests cover exact serial lifetime
and independent reduced-scene/output-density extents. These are not visual
qualification of FSR/DLSS or all MM3D inventory, map, pause, transition and text
producers. Live acceptance and open F03/F18 items are recorded in the COMPOSITION
lane's `PROGRESS.md` and `RESULTS.md`; do not infer feature closure from these
mechanism tests.

No shader producer or packed SPIR-V is changed by this implementation.
