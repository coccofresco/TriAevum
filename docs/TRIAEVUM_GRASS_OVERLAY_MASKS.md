# Native room overlays and grass placement

## Round 36: mask identification and overrides

F1 > Grass > **Masks**, beside Sources, lists current observed overlaps and saved
rules. Each texture has a thumbnail, exact catalog hash/dimensions, source hashes,
mode, summed overlap area and estimated coverage. The Sources and Masks catalogs
show **MASK auto**, **MASK custom**, or **MASK off**. Measurements arrive
asynchronously; a saved rule without current overlap is marked as such.

- **Automatic** evaluates the native draw's alpha coverage. Missing rules and
  unknown mode strings select Automatic.
- **Custom** reuses the source editor's channel, inversion, input/output levels,
  response exponent, wrapping, active rectangles and interactive preview, with
  an additional strength. White suppresses grass; black lets it pass. Coverage
  is `clamp(EvaluateGrassMaskLevel(sample, rule, wrappedUV) * strength, 0, 1)`.
  The preview includes strength. Custom ignores native TEV/primary alpha and can
  override an unsupported native alpha/blend path when static geometry and UVs
  are available.
- **Off** gives zero coverage. It remains selectable when renderer capabilities
  disappear. It does not turn the overlay into a source.
- **Add selected texture mask** creates/selects a Custom rule from the catalog.
  **Remove mask rule** removes its persisted override; an observed overlay then
  returns to Automatic. **Reset mask to automatic** removes the override and
  its custom tuning. Eligible configured source rules retain priority.

`Graphics.Grass.Masks` and `Graphics.GrassSavedPreset.Masks` are optional arrays.
Each entry uses the source rule's `Target` (`Rgba8Hash`, Width, Height) and coverage
fields, plus `Mode` and `Strength`. The shared codec accepts earlier files without
Masks. Active rules are normalized, bounded to 256, and duplicate exact targets
are removed. Global graphics presets preserve Masks alongside Sources; the
explicit grass saved preset includes both. Persistence failures use the existing
visible **Settings applied, but could not be saved** notice and retry action.

Mask identity deliberately does **not** use approximate texture twins. Similar
RGB dirt/path images can contain materially different alpha or cutout locations;
source twin inheritance still works independently. A manual rule applies only to
eligible static room draws, never actors, skinned meshes or projected shadows.

The mode and every effective Custom control participate in the overlay version.
The existing async/static/world-placement caches therefore rebuild on edits.
No special texture hashes, scene IDs or hand-authored path shapes are in the
implementation.

### Highlight boundary and measurement cost

**Highlight mask in scene** draws amber edges of the actual clipped overlap. It
is ephemeral and cleared on tab changes, F1 closure, F2, room reset and stale
frames; a 500 ms lease also prevents a lost UI owner from leaving it active.
Geometry is scoped to the source's current frame, target namespace and color
framebuffer. It is capped at 4,096 clipped triangles per footprint and the
existing 16,384 debug-line limit. Targets without a proven BeforeUi boundary
are not highlighted.

The existing world-debug-line draw now occurs at the presentation BeforeUi
anchor **after scene effects and the clean pre-UI upscaler snapshot**, before
native UI. Mask edges and light-editor gizmos share that one debug draw. They
write neither depth nor effect guides and never enter the scene-effect input or
clean upscaler image. Native UI covers them normally; an upscaler carries them
with the presentation decoration. With no selection/gizmos, the path returns
before beginning a render pass, creating a pipeline or uploading vertices.
Native draw shaders and ordering are unchanged. Live UI/upscaler acceptance
still belongs to the lab.

Area/histogram work runs in **one asynchronous CPU job**, using immutable
snapshots and rejecting results whose source version/room epoch is stale.
Placement preparation does not wait for it. Histograms use 64 deterministic
barycentric samples per clipped triangle. Area is a sum of source/overlay
triangle intersections, not a union across overlapping draws; placement itself
still unions triangles within a draw and composes separate draws by alpha-over.
The wireframe and statistics are editor data, separate from placement payloads.

On the private Graveyard fixture, Release build, no diagnostic printing:

| Measurement | Round 36 |
| --- | ---: |
| Cold placement preparation, including job submission | 0.575 ms |
| Warm overlay lookup | 0.688 us |
| Republish 19 prepared meshes + lookup | 3.554 us |
| Empty-overlay lookup | 0.023 us |
| Roots before / after (same assumed-alpha fixture) | 542,260 / 490,943 |

The roughly 30 ms histogram workload moved off the render thread. These are
local CPU measurements, not live frame-time guarantees.

### Graveyard evidence and the outstanding native comparison

The supplied `round35_graveyard_trace.jsonl` identifies room draw 18 as
`s2_tsuchi_02` (`c018f0023fa65419`, 64x128), draw 19 as `s2_tsuchi_01`
(`78795509fbe0ffeb`, 256x128), both alpha-over with alpha testing disabled.
**Draw 20, `dd5aab0feaa345b3`, has `actor=1`; it is excluded before mask
capture.** It cannot be the third grass mask under the round-35/36 producer.

Native base-level texture alpha, decoded from the installed room CMB:

| Texture | Pixels | Alpha zero | Alpha one | Mean alpha |
| --- | ---: | ---: | ---: | ---: |
| s2_tsuchi_01, full atlas | 32,768 | 7,132 (21.765%) | 17,377 (53.030%) | 0.637948 |
| s2_tsuchi_02, full atlas | 8,192 | 945 (11.536%) | 5,055 (61.707%) | 0.736898 |
| s2_tsuchi_01, rightmost 64 columns | 8,192 | 938 (11.450%) | 5,540 (67.627%) | 0.783276 |

Full texel histograms, in byte-alpha order
`0,17,34,51,68,85,102,119,136,153,170,187,204,221,238,255`:

- s2_tsuchi_01: `7132,1118,853,741,666,589,565,531,466,486,427,464,425,422,506,17377`
- s2_tsuchi_02: `945,246,212,188,166,161,151,140,153,134,127,115,145,125,129,5055`

The green between slabs is **not uniformly transparent**. In the displayed
atlas (top-left origin after the decoder's vertical display flip), samples
`(218,9)=(95,103,62,255)`, `(226,40)=(132,140,91,255)` and
`(243,48)=(69,86,53,255)` are green/brown opaque pixels. A soft sample is
`(226,95)=(94,94,26,102)`. Green visible in a screenshot therefore does not by
itself establish that the ground texture is showing through. Custom Green or
Luminance controls can express a different artistic coverage rule.

Using the real room triangles/UVs, clipping to the ground source and native
bilinear base-level filtering, the **offline texture-alpha** histogram over
covered area is:

| Alpha bin | s2_tsuchi_01 area % | s2_tsuchi_02 area % |
| --- | ---: | ---: |
| exactly 0 | 4.2212 | 3.5115 |
| (0,1/14) | 3.2428 | 3.4129 |
| [1/14,2/14) | 2.5988 | 2.6187 |
| [2/14,3/14) | 2.5843 | 3.3479 |
| [3/14,4/14) | 2.4218 | 2.7514 |
| [4/14,5/14) | 2.3386 | 2.4359 |
| [5/14,6/14) | 2.0617 | 3.1819 |
| [6/14,7/14) | 2.0522 | 2.8251 |
| [7/14,8/14) | 2.1253 | 3.1046 |
| [8/14,9/14) | 1.7716 | 1.9126 |
| [9/14,10/14) | 2.2065 | 1.8179 |
| [10/14,11/14) | 2.0876 | 2.7779 |
| [11/14,12/14) | 2.2405 | 2.9285 |
| [12/14,13/14) | 2.7301 | 2.7321 |
| [13/14,1) | 4.6169 | 3.5045 |
| exactly 1 | 60.7001 | 57.1366 |

Clipped areas are 66.122757 m2 and 1.469483 m2 respectively. With the existing
fixture's **assumed** `texture alpha * primary alpha 1` TEV, mean coverage is
0.787003 and 0.766186. Those are not measured live native-output alpha values.

The trace does not record alpha constants/operands/vertex alpha. The supplied
savestate's `runtime.native_pica_visual_replay_state.available` is false and its
replay payload is empty. Thus the real native primary/constant/TEV output and its
agreement with the displayed blend **remain unverified**. No universal
native-alpha correction or claim that automatic coverage is fixed is justified
by this evidence. The scalar supported-path operations were checked against
`pica_tev_program.cpp`; no arithmetic discrepancy was established. The new
producer retains real UVs for constant-alpha textured overlays, needed by Custom
rules and atlas diagnostics, and retains unsupported alpha paths as identifiable
zero-coverage Automatic candidates rather than silently making them opaque.

### Lab continuation

Enable `TRIAEVUM_GRASS_MASK_DIAGNOSTICS=1`, then load
`state-2026-10-11_16-21-37.oot3dsav` (scene 0x53, room 1, child night).

1. Capture the `program` and six `tev` records for draws 18/19: native/catalog
   identities, primary-alpha min/max, buffer RGBA, constants, TEV words, alpha
   test and support status. The asynchronous `coverage` lines report clipped
   area, effective mean, texture histogram and native-coverage histogram.
   Bins are exactly zero, fourteen equal-width interior bins, exactly one.
   For opaque draws, native coverage means replacement/alpha-test coverage,
   not raw shader alpha. `pair` records retain configured source rule IDs.
2. Compare texture/native coverage at visible soft edges and specific slab gaps
   with grass Off/On at the same camera. If native output differs from the CPU
   evaluation, fix that generic dependency/sampling/primary path; do not add
   texture-specific data. Keep replacement-pack alpha/mip limitations in mind.
3. In F1 > Grass > Masks, identify both thumbnails and source relationships;
   inspect any additional room masks. Verify draw 20 is absent. Toggle each
   mask Off, Automatic and Custom, then use Green/Luminance, inversion, levels,
   strength and rectangles to let the desired green regions pass. Confirm live
   rebuilds settle, removal/reset work, and settings survive restart.
4. Highlight, change tabs, close F1, toggle F2 and re-enter the room. Check the
   wireframe follows only the selected surface and never the HUD. Exercise
   native presentation, MSAA/TAA and FSR/DLSS modes; inspect the clean scene
   snapshot/effect inputs to confirm there is no highlight contamination.
5. Compare warm capture and placement costs with diagnostics off, including a
   room without overlapping masks. Confirm Grass Off bypasses mask capture.

### Round 36 validation

Built `TriAevum.exe`, `oot3d_graphics_foundation_tests` and
`triaevum_f1_settings_smoke`. Full foundation run: **371 passed**, one optional
fixture skipped; the private fixture passed separately. Focused actual-widget
smoke: **489 assertions passed**, including visible save failure and retry.
The full F1 smoke still stops at the documented unrelated material-editor
Azahar-identity assertion. Tests cover every Custom control, all modes/channels,
codec defaults/round trip, exact target identity, cache version changes,
clipped overlap, stale/in-flight room results, scoped/inert highlighting and
F1/tab/F2 cleanup. No game or git commands were run.

## Round 35 baseline

Round 35, 2026-10-11. Native decals previously failed grass-source admission,
but had no way to suppress placements from the surface underneath them. This
produced grass through the Graveyard stone path.

## Capture and coverage

Grass now observes room overlays during the existing composition-sequence
preparation pass. This is necessary because grass can be inserted at the opaque
boundary before native order reaches the translucent decals. Preparation only
captures immutable CPU data; it does not submit or reorder native draws.

`grass_scene_bridge` keeps overlays separate from its grass-source index. A
source can use only later overlays in the same frame, composition sequence,
render-target namespace and color framebuffer. Configured, eligible grass
sources do not become masks. Actor geometry, projected shadows, stencil draws,
skinned geometry and unsupported UV programs are excluded. Non-alpha-over
blends have no Automatic coverage; Custom may override their coverage.

`grass_overlay_mask` prepares world triangles once per cached overlay set. For
each source triangle it requires overlapping bounds, normals within one degree,
and all overlay vertices within **one world unit (1 cm)** of the source plane.
Distance is measured along the source normal, so this also works on slopes.
Roots are tested with triangle barycentrics, not the overlay's bounding rectangle.
Surfaces further above the plane and crossing walls do not mask the source.

The mask evaluates native TEV alpha, including texture-zero RGBA operands,
primary alpha, per-stage constants, previous/buffer alpha, operands, scales and
8-bit stage quantization. Native alpha-test comparisons use the resulting byte.
Opaque overlays cover fully where their alpha test passes; opaque overlays
without an alpha test cover fully without requiring a texture. Alpha-over
overlays use their output alpha after the alpha test. Unsupported dependencies
(fragment lighting, other texture units/procedural sources, previous RGB or
DOT3_RGBA) decline the mask instead of assuming opaque coverage.

Primary alpha is recovered from the typed native CMB surface program and its
captured uniforms/vertex input. The translated CMB instructions 77–115 in
`tools/oot3d/native_pica_frontend/oot3d_translated_vertex_programs.h` establish
the contract: lit alpha is material diffuse alpha times the sum of enabled light
diffuse alphas, without the normal dot product; lit vertex tint multiplies it;
unlit vertex tint replaces material alpha. The registry applies native vertex
`min(abs(alpha), 1)` before interpolation and fragment quantization.

Coverage scales the existing acceptance probability by `1 - alpha`. Separate
draws multiply their survival probabilities; triangles within one draw form a
union, avoiding double attenuation at shared edges. Alpha zero leaves candidate
placement unchanged, alpha one removes the root, and soft edges thin the grass.
The existing random sequence is retained. Blades can still lean or spread across
an edge from an uncovered root.

Texture sampling uses immutable **native base-level pixels**, the native UV
transform/wrap and nearest/bilinear filtering. It deliberately remains independent
of camera-dependent mip selection. Replacement-pack alpha and screen-space
occlusion/scissor coverage are not reconstructed. These limits should be checked
if a future overlay depends on them; native draw appearance is never modified.

## Lifetime and cost

Geometry and primary alpha are cached in `grass_geometry_registry`. Overlay-set
versions include source and overlay geometry/content, world transforms and alpha
material/texture state. Native order controls set membership. The version feeds
the existing async/static and world-placement keys, including removal, so stale
placements cannot survive a changed overlay set. An unchanged set shares its
prepared triangles and does not rerun extraction. No overlay returns the original
source version and uses the existing extractor path without per-root mask work.

Actors are excluded up front. Changes to a room overlay's UV transform, vertex
positions or rigid world transform mark that instance animated and remove its
mask until room removal/reset. Small reconstruction roundoff is tolerated for
the world transform. The first observed pose can mask until motion is detected.
Animation history is bounded to 4,096 instances; prepared sets to 256 sources.
Material/texture-alpha changes invalidate and rebuild static placement.

Grass Off bypasses capture and masking. With editor highlighting disabled,
the masking feature adds **zero GPU passes, draws, texture uploads or readbacks**. The GPU receives the existing grass data
with fewer roots. Exact GPU savings and total live CPU frame cost require a lab
run; CPU capture still has a cost even when the resulting overlays do not
intersect a source. No claim of zero total-frame overhead is made.

### Offline Graveyard measurement

The private fixture was decoded from `scene/spot02_1_info.zsi`; no ROM-derived
payload is checked in. The optional test `Oot3dGrassOverlayBenchmark.OfflineRoomFixture`
reads the path in `TRIAEVUM_GRASS_MASK_FIXTURE`. It uses the actual room triangles,
UVs, native texture channels and trace order/blend categories. It uses the user's
density (1048.8/m²), spacing (0.6), first rule's green levels and an eight-million
candidate cap. Primary alpha is assumed one and alpha-test reference 128; this
is a CPU workload measurement, **not a replay of captured shader uniforms or the
runtime's inherited rule selection**.

Five extraction repetitions, median, local Release build:

| Measurement | Result |
| --- | ---: |
| Source triangles | 991 |
| Broad-phase overlay triangles | 2,541 |
| Source triangles with coplanar candidates | 241 |
| Source/overlay triangle candidate pairs | 1,282 |
| Roots before / after | 542,260 / 490,943 |
| Cold overlay preparation | 0.548 ms |
| Extraction before / after | 70.032 / 67.409 ms |
| Warm set lookup | 0.544 µs |
| Republish 19 prepared meshes + lookup | 3.148 µs |
| Empty-overlay lookup | 0.018 µs |

Reduced spacing/output work offsets mask evaluation in this fixture. The warm
republish measurement excludes native uniform decoding and geometry-registry
lookup; use the runtime capture diagnostic for that cost. These numbers do not
establish a frame-rate improvement or a no-overlay frame-time guarantee.

## Graveyard identification and lab checklist

Offline texture inspection and triangle counts identify these room draws in
`round35_graveyard_trace.jsonl` (frame 2983):

| Trace ordinal | Native texture ID | Room texture | Triangles |
| --- | --- | --- | ---: |
| 9 | `c09d75caf61cc19c` | `s2_yuka_01`, grass ground | 991 |
| 18 | `c018f0023fa65419` | `s2_tsuchi_02`, dirt overlay | 10 |
| 19 | `78795509fbe0ffeb` | `s2_tsuchi_01`, dirt/stone-path atlas | 205 |

Draw 20 (`dd5aab0feaa345b3`) was not identified in this room CMB. Round 36
confirmed its trace actor flag, which excludes it from masks; this is not hardcoded. None of these asset names,
IDs or scene IDs are used by the feature.

Enable `TRIAEVUM_GRASS_MASK_DIAGNOSTICS=1` for the lab. A `pair` line gives native
ordinals, submissions, geometry identities, source native/observed texture hashes,
**configured rule ID and original configured Rgba8Hash**, and the mask's native
and observed hashes. This also resolves sources admitted through texture twins.
`prepare` lines give cold triangle count/version/time; `frame` lines report the
previous frame's capture attempts, captures and CPU time. `excluded` lines explain
unsupported alpha/geometry/UV paths or detected animation. Pair counts are broad
phase; the extractor still applies the plane and barycentric tests.

1. In Claude's lab, load scene 0x53 room 1, child Link at night, from
   `state-2026-10-11_16-21-37.oot3dsav`, with the supplied Custom grass settings.
2. Confirm the diagnostic pairs source draw 9 with path draw 19 and records the
   actual configured rule ID/hash. Check for exclusions on these two draws.
3. Capture grass On/Off at the same camera. Slab interiors should have no roots;
   alpha-zero gaps and the grass beside the atlas should remain populated; soft
   edges should thin. Confirm the native pavement appearance is identical.
4. Move/rotate the camera and cross the path. Check slopes, intersections and
   room re-entry for stale masks or rebuilding. With `OOT3D_GRASS_DIAGNOSTICS=1`,
   `[grass-build]` should settle after static placement is cached.
5. Change/remove a source rule and toggle grass Off/On. Check that the overlay
   never becomes a source and Off has no mask-capture work.
6. Compare warm capture CPU and grass CPU/GPU timings against baseline, including
   a scene without ground overlays. Collect medians/tails with diagnostics off
   for final frame-time comparisons. Do not use the offline numbers as frame times.

## Validation

Built `TriAevum.exe` and `oot3d_graphics_foundation_tests`. The grass-related suite
plus overlay fixture passed 107 tests; its optional private benchmark was skipped
in the generic run and passed when explicitly supplied the local fixture.
Coverage includes opaque/alpha-edged masks, native TEV/primary/constant alpha,
alpha testing, bilinear/wrapped UVs, slopes, walls, plane tolerance, ordering,
scope/sequence isolation, animation exclusion, geometry alpha capture, and
static/world-cache invalidation/restoration. Standalone `grass_spacing_tests`
(36 brute-force reference cases) and `texture_source_cache_tests` also passed.
No game execution or visual acceptance was performed by Codex.


## Round 37: draw memo and final placement persistence

Static native draws now retain their resolved source, overlay, or neither result
in a renderer-owned 4,096-entry LRU memo. The key includes native geometry
identity/content version; command-list location and render-target scope; native
base texture identity, dimensions, format/type, sampler and immutable payload
extent; index/attribute/binding layout; eligibility flags; shader identities and
typed UV/primary-alpha hooks; UV uniforms, TEV constants/buffer/program and only
the alpha-relevant vertex-light scalars; source/mask rule versions and texture
signature generation. Camera matrices and RGB light directions are excluded.
Unversioned or copy-produced textures/geometry bypass the memo. Transient invalid
camera/world transforms are retried instead of becoming permanent rejections.

A hit refreshes exact PICA model-to-clip, draw order, sequence and liveness.
Source lighting/fog/depth are still refreshed: a stationary camera does not imply
stationary lighting. A rigid draw must agree with its stored world anchor when
reconstructed through the current captured camera, within the existing bridge's
0.05-unit translation / 1e-4 linear **world-space** tolerance. The camera inverse
is memoized separately (eight exact camera states); the per-draw matrix check
remains necessary to detect animation. Actual within-tolerance transforms still
reach the bridge so accumulated motion cannot disappear after a rule edit.
A mismatch takes the cold path;
an animated overlay remains subject to the existing exclusion guard. There is
no camera quantization or tolerance in **selection** reuse. Neither hits do not
publish a mesh. The native publisher alone marks its observed texture identity
as already resolved; external scene publishers retain the original resolution
contract.

F1 source channel/levels/rects/wrap/twin mapping/slope/offset, mask
Automatic/Custom/Off/strength/coverage, rule add/remove, quality and generation
changes invalidate capture at the next immutable frame-settings snapshot.
Placement independently keys generation, effective rule, geometry, transforms,
cluster policy, blade height scale and shadow support. New/cleared texture
signatures invalidate memo decisions, including earlier neither/twin results.
Native geometry/texture edits require their producer content identity to change.
Renderer reset clears the memo. Base-only identity deliberately ignores mip-only
changes. Replacement texture packs still do **not** supply mask alpha or placement
RGB: these use the original native base-level contract established in round 36.
Reloading a pack alone therefore cannot leave an obsolete pack-derived mask;
there is no such mask input. Native content or F1 edits take the normal invalidation
paths above.

Final per-source placements, including their later room overlays, are stored in
`<renderer cache directory>/grass_placements_v1/*.grass`. This is local
ROM/config-derived data, never release content. The renderer directory follows
`TRIAEVUM_RENDERER_CACHE_DIR`, or the existing SDL renderer preference directory.
The two-part content/settings key hashes actual source vertices/indices, overlay
world triangles/UV/primary alpha and material version, native texture identities
(or explicit decoded mask/color payloads), effective source/twin rules,
generation/spacing/slope/normal offset, transforms, candidate budget, cluster
settings, height scale, shadow support and algorithm version. Transient content
counters, frame/sequence IDs and process-local placement IDs are not persisted.
The original geometry identity **is** included because the pre-existing random
seed uses it; a changed guest geometry address is a safe miss, not a new seed.
Exact world-transform differences also miss rather than approximate placement.

Files contain final packed world anchors **with RGB and surface references**,
culling anchors, clusters/visibility hierarchy, midrange membership/hierarchy,
and shadow-grid geometry/references. No texture sampling or extraction is needed
to load these streams. Source channel/color conversion and overlay texture decode
are deferred to worker misses. Classification of a first-ever eligible texture
may need a native twin signature; that signature is now persisted for the next
scene entry. Native scene rendering still owns its ordinary texture uploads.
The F1 area/histogram worker is requested only while its observations are being
read, or with mask diagnostics enabled, so a closed editor cannot force a decode
on a placement hit.

The whole room batch is queued before waiting, as before. A hit loads on workers;
a miss bakes/writes on workers. The batch is admitted together: there is no
asynchronous grass pop-in. Initial disk I/O, native draw discovery, membership
checks and budget measurement still cost time; this is not a pre-scene room
manifest. The existing 32-entry / 768 MiB resident budget remains, with disk
limits of **128 files / 1 GiB total / 512 MiB per file**, oldest access first.
Temporary writes never expose partial final files. Missing/unwritable cache
storage falls back to normal construction. Magic, algorithm version, endian/ABI
sizes, total byte count, per-vector counts, checksum, finite values and tree/index
bounds are checked before use; rejected files are rebuilt and replaced. The
checksum is a local, non-cryptographic word mixer. Increase
`kGrassPlacementDiskVersion` whenever bake/packing/overlay semantics change.

Blade shaders sample neither source nor mask textures. Baked anchor RGB feeds
color; the native surface-lighting atlas remains a separate dynamic lighting
input. Exact unchanged selections now also reuse each fenced GPU slot's visible
index upload. Full compaction is reused only with an unchanged static/selection
revision, no actors, an exactly resting field, and no motion-vector consumer.
Normal gameplay with Link/interactions retains its dispatch and bend history.
Wind, lighting, fog, shadows and blade draws continue each presentation.

See [grass cost and measurement commands](TRIAEVUM_GRASS_PRESET_RENDER_COST.md#round-37-mask-and-placement-cache)
for telemetry, numbers, A/B switches and live acceptance. The cache tests cover
memo inputs/camera separation, native base changes, F1 edit-to-rebuild,
roundtrip/version/corruption/bounds/eviction, and a cache hit with no source pixels
left available. The Vulkan test compares cached and freshly compacted instance
bytes and color/material/motion captures exactly (zero differing values), while
retaining the previous motion-history tests.

## Translucent overlays reach preparation with their fragment state

Masks are captured while the sequence is prepared, before the grass insertion
point. The OOT3D bridge used to hand prepared draws their fragment uniforms and
textures only when they ran an alpha test (the shadow-caster need), so every
translucent decal arrived without a texture: its alpha was "mask TEV
unavailable" and it failed open. In the Graveyard both s2_tsuchi decals
(orders 22 and 23) were never masks, and a per-texture rule could not find
them (identity 0). The renderer backend now declares
`PicaSequenceDrawPreparationNeedsFragmentState()`; the Vulkan backend asks for
it while the grass provider is active, so prepared draws carry their fragment
state and the decals resolve to their native identity (path atlas
78795509fbe0ffeb). Grass Off keeps the vertex-only preparation.

Lab, user configuration with the ground as an explicit source, grass minus
grass Off on the slabs: Automatic 17.3, the path mask Off 33.6 (lawn 17.6 in
both).

## Placements persist only once settled

A built placement is written to `grass_placements_v1` only after it has stayed
in use for the settle delay (2 s): every step of an F1 slider drag is a new
placement that is replaced before it settles, so none of them reaches the disk
(before, one drag wrote about 1 GB). The in-memory rebuild stays immediate;
the write runs on a placement worker.

## Dusk in a time-flowing scene: placements never stall the frame

Hyrule Field's dusk and dawn drew heavy stutter. Lab: Kokiri savestate, warp to
Hyrule Field by day, clock just before 0xC001, 2,400 frames through the ramp
and into night (`TRIAEVUM_SLOW_STEP_MS=12`). Three independent causes:

- Mist layers that scroll over the field appear at dusk. Their texture
  transform changes every frame, so each frame was a new overlay content; the
  scroll lives in the uniforms, not in the vertex coordinates, and their draw
  identity does not stay in the animated-overlay exclusion. Every frame the
  overlay set changed and all four field sources rebuilt synchronously: 122
  placement rebuilds of about 1.2 s inside the frame. Those layers are
  translucent on an alpha path the evaluator does not support: in Automatic
  their coverage is zero everywhere.
- A covering overlay (an opaque decal for a frame or two) or the key light's
  shadows starting to reach the grass are legitimate placement changes, but
  each still rebuilt the field inside the frame (1-2 s).
- Pipeline creation for the dusk and night programs (one 65 KB fragment
  program) took 4.6 s on the render thread because the developer play harness
  never kept its renderer cache, and the persisted NRI cache of the play
  profile predated a driver update (rejected, as it must be). With a kept
  cache a session creates the same 283 pipelines in 0.7 s instead of 58.6 s.
  An installed game keeps its cache; `play_run.sh` now copies it back.

Fixes, generic at their owners:

- `GrassOverlaySet` keeps two versions. `Version` covers every later
  coplanar draw (what the mask editor lists and measures, so an unsupported
  layer can still be set to Custom). `PlacementVersion` covers the source and
  only the overlays that can remove grass (`GrassOverlayCanCover`: Custom, or
  Automatic on a supported native alpha path; Off never). The placement key
  and the persistent disk key use only those overlays.
- A covering overlay change applies at once; another within
  `kGrassOverlaySettleFrames` (20) waits until it holds that long. An animated
  covering mask keeps its last settled placement instead of rebuilding every
  frame. Source changes are never held back.
- `GrassStaticPlacementCache` keeps showing a source's previous placement
  while a new one builds when only its overlays or its shadow grid changed.
  The first admission of a source and any other setting change (density,
  height, rules) still wait, as before. A shown placement without the shadow
  grid is drawn unshadowed until the one with it is ready, rather than the
  pass refusing it.

Result, same lab path with a kept cache: worst frame of the ramp 5,287 ms ->
119 ms, night 4,682 ms -> 54 ms; placement rebuilds inside a frame during the
ramp 122 -> 0. `OOT3D_GRASS_DIAGNOSTICS` reports `[grass-wait]` when a
pending placement cannot fall back to the shown one, and
`TRIAEVUM_GRASS_MASK_DIAGNOSTICS` now prints each overlay's draw content,
geometry content, primary alpha scale and texture transform.
