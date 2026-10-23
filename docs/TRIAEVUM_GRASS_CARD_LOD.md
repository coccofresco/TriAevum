# Grass card LOD: geometric substitutes beyond the blades

The interactive grass draws every visible blade as its own instance. Dense
fields at 4K therefore paid for blades that covered a texel or less, and the
previous attempt to extend the field with thinner blades (the "distant field"
of beta.1.2, reverted in 4963cc1) only moved that cost. Games with thick grass
stop drawing blades at a short distance and stand in geometric substitutes for
them: cut-out cards that carry a tile of blades as texture. This document
describes TriAevum's four-tier version of that scheme, its contracts and how
to validate it. The setting is off by default until the user approves it.

Code: `grass_card_lod.*` (policy, records, card build), `grass_card_atlas.*`
(tile generator), `grass_card_renderer.*` (Vulkan resources and draws),
`grass_shader_sources.cpp` (`BuildGrassCardVertexShader`,
`BuildGrassCardFragmentShader`), `grass_pipeline_factory.*` (the pipeline
recipe shared by blades and cards), `interactive_grass_pass.cpp` (ownership and
scheduling).

## Tiers

| Tier | Substitute | Range (Enhanced defaults, 1 m = 100 units) | Source |
| --- | --- | --- | --- |
| 0 | Blades | 0 – T1 = 1000 (10 m) | the existing blade pipeline |
| 1 | Clump card | T1 – T2 = 4000 (40 m) | one card per culling cluster |
| 2 | Group card | T2 – T3 = 15000 (150 m) | one card per 4×4 clusters |
| 3 | Patch card | T3 – ∞ | one card per 16×16 clusters |

Tier 3 never ends: the patch cards are drawn at any distance the native
geometry is visible. `GrassCardLevelWeight` returns 1 for level 2 beyond T3
and the unit tests pin that down.

Each tier change happens over a band of `CardLodBlendFraction` × the tier
distance. The entering tier dithers in over the first half of the band and the
leaving tier dithers out over the second half, so at every distance one tier is
whole (`OneTierIsWholeAtEveryDistance`). With cards enabled the blades' draw
distance becomes T1 and their final fade fraction half the blend fraction; the
`DrawDistance` slider no longer bounds the blades, it bounds the placement
residency store (anchors within `DrawDistance` + margin of the eye).

## Cards derive from the accepted roots

`BuildGrassCards(placement, clusterSize)` builds the three card levels of a
world placement:

- The **representative root** is the accepted root nearest the mean of the
  group. The card stands on the native surface at that root and reads that
  root's surface reference (lighting texel) and shadow grid points.
- The **footprint radius** is the largest horizontal distance of a member root
  from the representative plus that root's half width; the **height** is the
  tallest member blade; the **count** is the member roots.
- The **normal** is the normalised sum of the member normals, the **colour**
  the mean surface colour of the members that have one, and the **seed** a
  hash of the representative's stable id (picks the atlas variant).
- Levels 1 and 2 bucket clusters into world cells of `clusterSize` × 4 and
  × 16 by the cluster centre, in key order, so the result is deterministic and
  independent of the thread count. A cell whose clusters occupy less than
  half of its slots splits into its quadrants, down to the cluster size, so a
  card never spans a gap the extractor rejected (a path, a pond): sparse
  cells become several small cards instead of one wide one
  (`SparseCellsSplitIntoSmallerCards`).

Each level's cards lie in Morton order of their roots and are cut into
chunks of 256 consecutive cards with a bounding sphere of the roots, the
tallest blade and the widest footprint (`GrassCardChunk`). Every frame the
pass selects, per placement and level, the runs of consecutive chunks that
intersect the frustum (sphere grown by `GrassCardReach`) and the level's
distance band (`SelectGrassCardChunks`) and draws one instanced call per
run; the rest of a field's cards are never instanced.

No tier adds surface: every card is pinned to a root the extractor accepted,
and the cards of a placement are rebuilt only when the placement identity,
content version or culling cluster size change (`EnsureCardRecords`).
`AppendGrassCards` concatenates the sets of the active placements with the
shadow points rebased to the pass's concatenated point array, or marks the
card without shadow when the placement has no grid.

## Atlas

`BuildGrassCardAtlas(shape)` rasterises, on the CPU, 3 sets × 4 elevations
(0°, 30°, 60°, 90°) × 8 variants of 128×128 tiles into an RGBA8 image: R is
coverage, G the height factor along the blade (0 root, 1 tip), B the covered
blade's offset toward the viewer from the tile plane (0.5 on the plane, in
`GrassCardTileDepthScale` units of the tile radius), A unused. The sets differ
by the tallest blade over the footprint radius (`HeightOverRadius`, 1, 1/4 and
1/16 by default): a card picks the set nearest its own height/radius ratio in
log space (`GrassCardAtlasSetFor`, mirrored in the vertex shader) and scales
the tile so its blade height matches, which keeps a tile's texture, extent and
depth consistent for every card that samples it. The blades of a tile use the
blades' own shape function (`grass_blade_shape` ported to C++: variation,
curvature, sway, droop, twist) so the cards keep the blades' silhouette. Each
mip is resolved from the supersampled raster of the base tile with a
coverage-preserving alpha scale, so an alpha-tested card does not thin away
with distance. The atlas depends only on the blade shape, blade sizes and
cluster size; the key changes with those and the renderer rebuilds the image
on its next frame (`CardAtlasRebuilt` in the telemetry).

Colour, lighting, toon bands, fog and the auxiliary guides are not baked: the
card shaders apply the blade functions per card at draw time.

## Draw

The card vertex shader first drops the cards that cannot reach the clip
volume (every point of a card lies within twice its depth scale of the root;
the projection's rows bound how far that offset moves each clip term) and
the cards whose tier weight is zero, before any shading: a field of hundreds
of thousands of cards costs its vertex shader only for the cards in view.
It then expands one record into a camera-facing quad pivoted at the root:
`toward` is the root-to-eye direction, `side` its horizontal perpendicular,
`up` their cross product. The card's tile radius is its height
over the chosen set's ratio, and the quad's vertical extent at the view
elevation comes from `GrassCardTileExtentAt(ratio, tileRadius, elevation)`
(above = r (ratio cos e + sin e), below = r sin e), the same function the
generator used, so the texture is never stretched against its tile. The two
nearest elevation tiles are sampled and mixed, each with its own vertical
mapping. The top edge shears with the blade wind evaluation at the card's
phase. The previous-frame clip position (motion vectors, temporal effects)
is the same expansion evaluated with the previous eye and wind time, so a
card's motion is exact rather than a translation of the quad. Lighting uses
the record's shadow grid points and surface reference through the same
`grass_local_shadow` and `grass_apply_native_lighting` as the blades; the
root and tip colours are toon-banded per vertex and the fragment mixes them
by the height factor.

The fragment shader alpha-tests the coverage, applies the tier weight as a
per-card stipple (`grass_card_weight`, the GLSL mirror of
`GrassCardLevelWeight`), moves the fragment's world position toward the
viewer by the tile's depth channel and writes that blade position's depth
(not the quad plane's), so terrain, water and actors intersect the card where
the blades stand. It ends in the shared `grass_write_fragment`, which writes
the colour, the guide outputs, PICA depth and fog exactly as a blade fragment
does. Writing the depth would forfeit the early depth test, so the vertex
shader draws each corner on its own eye ray pulled toward the eye by the
largest blade offset the tile encodes (the projection is unchanged, only the
plane moves along the rays) and the fragment never places a blade ahead of
the drawn fragment (`card_shift`): the written depth is never nearer than
the rasterized one. Batches whose PICA depth mapping is the raster depth
(`GrassDepthIsRasterDepth`) therefore take the conservative-depth programs
(`layout(depth_greater) out float gl_FragDepth`) and keep early depth; any
other mapping takes the explicit-depth programs. The cards are drawn by the
grass pass after the blades,
in the same render pass and with the same pipeline recipe
(`CreateGrassPipelineSet`), one `vkCmdDraw(6, count)` per level per
placement. The record buffer is immutable per revision: a rebuild uploads a
new buffer and retires the previous one after the frames in flight, so every
recorded invocation keeps the buffer it was recorded with.

## Ownership and settings

`InteractiveGrassSettings` owns `CardLodEnabled`, `CardLodBladeDistance`,
`CardLodClumpDistance`, `CardLodGroupDistance` and `CardLodBlendFraction`
(validated in order and then ordered: clump ≥ blade, group ≥ clump;
persisted in the `Performance` block with defaults for older files). F1 >
Grass > Performance > "Distant cards" is the control surface (distances in
metres); the in-game menu's Graphics tab has the toggle only. The grass pass
owns all card resources through `GrassCardRenderer`, created lazily on the
first frame that enables the cards; when the card programs cannot be created
the cards are unavailable, the pass logs why once and the blades keep their
configured draw distance. A failure while preparing the cards (atlas or
record upload, out of memory) disables the cards for the session with one
`[grass] distant cards disabled:` line and never costs the blades of that
frame. With cards enabled, a residency store with no anchor near the eye is a
cards-only frame: the blade compaction and GPU selection are skipped, not
disabled.

Disabled, the feature is inert: no card resources exist, the blade policy,
shaders, pipelines and descriptors are the ones of 4963cc1.

## Diagnostics

- `TRIAEVUM_GRASS_PROFILE=1` adds `gpu_cards_ms`, `card_batches` and
  `card_instances` to the per-frame grass line (`GrassStage::Cards`, GPU
  scope `grass_cards`).
- The F1 runtime status line prints "distant cards: N (M draws)".
- `[grass-cards]` log lines report every record rebuild with the per-level
  counts.

## Validation

- Unit: `oot3d_grass_card_lod_tests.cpp` (policy order and the endless last
  tier, card build on a synthetic grid placement, the occupancy split, shadow
  point rebasing, atlas set choice, tile extent, coverage, gradient, depth
  channel and mips, key stability), persistence round trip,
  `renderer_spirv_pack_tests` (97 built-in pass programs), the F1 smoke test
  (metre conversion, ordering, toggle).
- Scene: deterministic 4K captures from the third-person camera on the user's
  F5 states (Kokiri Forest, Hyrule Field) with cards on and off, ABBA GPU/CPU
  with the profile line. The user approves the look before the default moves.
