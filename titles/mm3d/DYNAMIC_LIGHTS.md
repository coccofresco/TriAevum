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
  native draw history; lens-off cannot reuse yesterday's visibility. Fire-arrow
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
