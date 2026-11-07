# Dynamic-light parity work

The binding owner decision is to convert local lights and analogous emissive
sources through the same renderer mechanisms as OoT3D, without changing the
MM3D base profile. Title facts belong here; mechanisms belong in the renderer.

Baseline investigation and proof ledger (local evidence, not shipped assets):
`I:/_triaevum_work/mm3d-port/analysis/parity/DL/INVENTORY.md`.
Running log: `DL/PROGRESS.md` alongside it.

Native fragment lights must be replaced by exact source identity, never added
again on top. The proven chain is LightInfo+4 -> accepted Lights record ->
upload context -> hardware slot, with code guards and current-frame ownership.
Unproved/ambiguous/stale cases retain native lighting. A fullbright combiner
alone is not proof of an emissive material (UI, sky and decals also use it).

MM3D actor ids do not all match N64 ids. In particular the proved MM3D torch
actor is 0x2E, whereas N64 Obj_Syokudai is 0x39 and N64 0x2E is unset. Semantic
references are read-only; EUR addresses/fields require their own proof.

Status at inventory completion: no dynamic-light product implementation or
new game captures. Remaining fire/glow analogues and the room/material catalog
are explicitly unproved in the ledger; native lamp replacement is only one
slice, not complete parity.

## Core implementation qualification (partial; not ready to land)

- `783446ed`: exact-source renderer replacement ledger.
- `13259e09`: captured pre-clamp Local recomposition using existing scene-point
  Lambert/cube-shadow evaluation; unqualified installed GGX stays native.
- `b41a0f6c`: guarded native source table for MM3D lamps/torches/fairies,
  default publication, native colour/radius/flicker, no base-profile change.
- Owning CPU tests and five new numeric production GPU cases pass. Regenerated
  uber/pass packs are byte-identical; all five legacy fragment-family checks pass.
- One night Clock Town run passes: 43 traced draws replace exact native Local
  hardware slots, with two published actor sources and no identity guessing;
  two point maps and 96 bound receiver draws observed. This is structural
  no-doubling evidence, not the requested Off/On visual comparison.
- One OoT3D Kokiri gate: 3/3 SHA-256 identical to `retuned/cap-p13.sha256`.
- Product build succeeds (`developer_tools=false`), but the single Authentic
  smoke fails before its first frame: savestate read `bad allocation`.
  Severe system memory pressure was observed; its sole causality is not proved.
  No retry or owner capture batch follows that failed gate.

Still missing: remaining fire/glow analogues; shared selector reuse; MM3D
room/material emitter catalog and S14 follow set; installed native-fragment
GGX conversion; complete Off/On qualification and the owner JPEG batch.
See the append-only local progress log for exact binaries, run receipts,
checksums, E:-backed build junctions, failure evidence and resume boundary.

## Continuation: shared selection and room material facts (still partial)

The selector and room-emitter catalog/TEV-strength capture now have neutral
renderer owners (`pica_scene_light_selection.h`, `pica_emitter_catalog.*`).
OoT3D retains compatibility adapters and its original arithmetic/defaults.
MM3D uses that selector for its proved native sources and room emitters.

The guarded native table now contains 15 source records across 12 EUR actor
ids. Added insertion/store and matching destructor/removal evidence covers
warp/light-ray/Poe/secondary-torch records and two keep-archive families.
An unidentified keep family is **not** thereby called a proved fire-arrow
analogue. Live callback, linked node and exact LightInfo identity remain
mandatory; native RGB, signed radius and flicker remain the inputs.

`tools/scene_emitters.py` generates an explicitly `mm3d`-namespaced catalog
from the user's EUR code and RomFS. It reads CMB v10 geometry/material names,
uses encoded texture hashes and reuses placement/clustering policies without
pixel statistics. Torch stands, unknown/fullbright materials, water and shadows
are excluded. The room reader normalises the native ZSI relocation fields;
offline Clock Town/Inn/Woodfall ROM/native/catalog identities agree. The local
EUR survey currently finds 145 emitters in 310 rooms, no rejected rooms, but
only four populated categories: windows, lamps, shafts and fire/lava. Magic
and doorway predicates are not evidence of populated/qualified MM3D examples.

The shared native-fragment ledger now accepts separately proved independent
emission as well as exact native replacements. Neither default supplemental
points nor contradictory proof flags are sufficient. A native identity clash
or untracked native point fails closed. Catalog material roles are not a PBR
emissive-map installer and do not change the native surface appearance.

**Not complete:** native-gated synthetic stick/arrow/Keese/projectile/wall/spout
sources, dormant-but-visible fairy variants, complete six-category material
proof, installed GGX receivers, installer/Forge catalog routing, previous-room
transition coverage and the final owner Off/On batch. The MM3D base profile,
including ambient 0.6, is unchanged. Do not land this as full parity.

Continuation gates: pipeline-cache69, foundation463+1 skipped, owning MM3D/native
composition/environment tests, F1 widgets12491/2720 and Python catalogs7 MM+13
OoT pass. At1a48f652 one targeted night run passes:43 exact native replacement
draws plus12 catalog identities reach native-fragment receivers (14 points;
4 point lights rendered,96 bound draws); trace ambient remains0.6. One rebuilt
OoT3D Kokiri run is3/3 SHA-identical to cap-p13. The subsequent64725c3f
same-frame scene invalidation fix is built/unit-tested, not game-rerun.
No new product smoke or owner Off/On batch is claimed. Exact receipts and the
still-pending capture index are in the local DL evidence directory.

## Remaining runtime paths (qualification is recorded separately)

- `mm3d_synthetic_emitters.h` adds independently guarded EUR fire Keese,
  fire-arrow child, two wall forms, spout controller/child and wizard trail
  readers. Native phase, scale/opacity, colour and draw gates own publication.
  The invisible Keese additionally requires the current EUR lens field and
  native draw history; lens-off cannot reuse the previous frame's visibility. Fire-arrow
  Charge/Fly/Hit callbacks are proved independently; their lighting policy is
  the OoT3D policy. Active registered points suppress synthetic duplicates.
- The player/carried-fire LightInfo is now an exact native replacement, not a
  guessed second stick light. Fairy callback variants are registered. A
  visible, positively proved dormant fairy glow uses independent emission,
  retaining its native identity so an unexpected native slot fails closed.
- Installed native-fragment GGX consumes the same hardware-slot balance as
  Lambert, retaining native ambient, applying direct/specular gains once and
  reusing point-cube visibility. Unsupported/fallback shaders omit replacement
  points rather than double native light. Black native diffuse stays black.
- PBR distinguishes native TEV-independent self-lit colour from reflective
  native-fragment colour by the final RGB dependency graph, including alpha,
  buffer history, overwrites and DOT3_RGBA. This preserves self-lit appearance;
  **it does not classify fullbright surfaces as emitters**. Artist-authored PBR
  emissive maps remain a separate surface channel. Catalog S14 follows authored
  native draw strength; it is not an emissive-map generator.

The six semantic catalog policies remain bounded, with four populated EUR
room categories. Native fairy/warp/ray sources cover positively proved actor
forms of magical glow; no unnamed room material is relabelled to fill an empty
category. Unknown materials retain native behaviour. Neither CPU fixtures nor
shader arithmetic probes establish every actor variant's in-game appearance.

New gates: pipeline-cache71; foundation463+1 skipped; owning MM3D/native and
F1 tests; 9 production GGX +9 native-unlit PBR GPU cases and the existing point
GPU suite; regenerated byte-identical uber/pass packs and legacy family checks.
The owner batch uses one implementation/binary, serial launches and **no location
retries**; its exact results, labelled JPEGs and remaining qualification limits
belong to `DL/captures/INDEX.md`, not to an assumed full-parity verdict. Product
smoke failure, automatic installer routing and previous-room ABI qualification
are not silently declared resolved. Base-profile values remain frozen.

## Owner batch delivered at implementation `117687b8`

The final implementation consists of shared renderer commit `67622f32` and
MM3D source commit `117687b8`. Developer binary SHA-256:
`fa8db2ec25d035410706d35b72273a5b6bc5f1f917a2ead8a0013039e6a306ff`.
All ten serial launches succeeded, **zero retries**: Off/On night Clock Town
lamps, Stock Pot Inn, Woodfall torches, the semantic window policy and visible
Tatl. A labelled native warp-glow crop derives from the Tatl pair with no extra
launch. JPEGs, checksums, profile overlays and exact receipts are indexed at:
`I:/_triaevum_work/mm3d-port/analysis/parity/DL/captures/INDEX.md`.
No implementation/binary change followed the batch; this status update is docs only.

The Inn actually installed the neutral-map PBR fixture:152 GGX trace records
per mode;86 balanced Off /152 On,3 rendered point lights and77 bound receiver
draws. Off renders zero point lights in all five views. Its flat/rough/black-map,
zero-specular-weight fixture qualifies routing, not a glossy or positive
emissive-map appearance. Nine GGX and nine native-unlit PBR numeric GPU cases
pass; native-unlit PBR was not encountered live in that Inn fixture. Six source
families/18 native records have EUR/CPU proof, not exhaustive actor look review.

One new OoT3D runtime gate is3/3 p13 SHA-identical. Final audit discovered its
nominal OoT-only build directory still enabled both titles; the gate used that
dual-title binary. The cache was then explicitly corrected to OoT3D-only and
built successfully, without another game launch. Do not mislabel either receipt.
Fresh product game/iMMERSE/DLSS builds also pass; the product-info tool reports
`developer_tools=false`. No product runtime smoke was repeated.

This delivers the requested runtime/capture slice, **not full parity or release
approval**. The four-populated-category room catalog, unknown material roles,
uncaptured variants, installer/previous-room integration and the prior failed
product smoke remain honestly bounded in the evidence index. Base-profile
values, including ambient0.6, remain unchanged; the look verdict is the owner's.

## Native contribution conservation correction (`f04d2376`)

Owner review rejected the old On torch pools as too dark. The shared conversion
had discarded exact native fragment-slot diffuse/specular response and rebuilt
it from a separately faded supplemental colour, generic material/normal and
different falloff. Exact identity proved ownership, not energy conservation.

Matched slots now retain their native per-pixel response and attenuation LUT;
user intensity/tint/radius controls and the existing cube visibility apply once.
The exact matching point row is excluded from supplemental evaluation. Installed
GGX uses the same native radiance/attenuation and cube routing. Ambient and alpha
are retained. Unmatched independent emitters and OoT3D's legacy path are unchanged;
there is no per-scene tuning, base-profile edit, new uniform ABI or old pack change.

A real Clock Town floor-triangle sample traced to source `99069c4` gives native
unshadowed RGB `(0.228650,0.158109,0)` versus the old approximation
`(0.189191,0.131659,0)`. The corrected unit-control, unoccluded response is native.
Another sample had almost no deficit: do not claim that selector fade explains
all the image darkening. Reconstruction is not a live pixel/visibility readback.
New36 production GPU cases preserve contribution at the70-unit reference distance
and across/beyond native reach, with shadow/control/ambient/specular/alpha checks.
Pipeline72, foundation463+1skip,54 existing point cases and12 GGX+12 native-unlit
PBR cases pass. Regenerated uber/pass packs remain byte-identical.

Exactly four new serialized launches, zero retries: diagnostic Clock Town Off,
diagnostic Woodfall Off, then corrected On for each at this implementation.
Off used `cde29681` plus trace-only instrumentation and is BMP-identical to the
previous Off reference; both On runs used one clean `f04d2376` binary. Details:
`DL/captures/conversion/INDEX.md`. No other scene/game was rerun.

The new comparisons show retained warm response, but darker regions remain
around torch bases and parts of the altar. Whether every remaining region is an
intended cube shadow or a residual artifact is not isolated by these captures.
**Contribution conservation is tested; complete visual resolution and appearance
approval remain unclaimed.** The four-launch budget is exhausted. Further live
isolation needs new authorization; all earlier product/full-parity gaps remain.

## Wave 2: reference routing, loaded rooms, lifetime and census

The additional authorized shadow-Off diagnostic did not remove most Woodfall
Off/On darkening. Trace routing identified a separate reference error: local-only
native-fragment draws with points Off retained two supplemental Scene directionals
in the legacy vertex-colour fallback; converted On draws used native-fragment
recomposition. Vertex colour is an authored TEV factor, not another scene-light
receiver. The shared capability-gated uniform fix removes that supplemental Scene
array for eligible native-fragment draws. Native vertex-lit titles, water's separate
contract, explicit profile keys and native response conservation are unchanged.
The numeric production GPU fixture includes the old erroneous .25→(.45,.35,.25)
vertex-factor witness and corrected (.25,.25,.25) reference. This is not a profile
brightness adjustment or proof of every remaining dark pixel.

EUR room capture now reads current C260 and previous C2C4 (record stride64 hex,
segment+10), independently re-found from request/copy/draw/unload code and ten
instruction guards. Pending request C330 excludes the incompletely installed
current record; previous remains eligible. Shared catalog capture deduplicates
identical room records and rejects conflicting fingerprints. S14 removes strength
history immediately on unfollow, rejects old in-flight observations on reload,
and retains last strength for continuously followed but temporarily unseen surfaces.
Scene/frame/day rollback resets MM continuity and follow lifetime. Regression
fixtures cover coexistence, pending install, unload, every corrupt guard, duplicate
and conflicting records, fade0/reload, never-seen1 and same-frame scene reuse.
Three real offline saves have empty previous records: **no live room transition
or actual two-loaded-room save was captured**. Synthetic coexistence is labelled.

The title-owned `tools/emitter_census.py` inventories all used material slots and
alternate headers without pixels or new light recipes. Stock EUR results:310 rooms,
308 models,5247 materials,6721 slots;72 primary candidates,47 secondary candidates,
232 explicitly excluded roles,6370 unknowns,0 parse rejects. Four categories remain
populated in the145-source catalog. Zero magic/doorway name matches are not proof
that these semantic classes are absent. All but one room have invariant setup mesh
pointers; the variant has no admitted emitters. Header hashes still do not attest
arbitrary modified payloads. Runtime mismatches/failed guards/conflicting records
omit catalog lights; modified-content catalog validation remains CATALOGS-owned.

`DL/wave2/ANALOGUES.md` explicitly disposes all26 OoT actor and8 material rows as
proved equivalents or remaining unknowns (none silently declared absent). Unproved
breath/particle/lantern variants and atlas/body/secondary/doorway roles stay native.
G05 inventory enumeration is complete; universal source/material parity is not.

A1025-sample offline full clock cycle exercises both horizons, wrapping clock,
changing colour, caster elevation and continuity. Double-Time direction cut,
Song-of-Time day rollback, frame rollback/save restore, fixed-time entry and scene
reuse are covered. Day rollback now invalidates the snapshot even when the scene
and clock match. This is adapter evidence, not a live song/transition capture or
new GPU shadow-page/keyframe-budget measurement. G06/G08/G21 retain those limits.
Final run receipts and precise qualification are in `DL/wave2/` and `DL/PROGRESS.md`.
