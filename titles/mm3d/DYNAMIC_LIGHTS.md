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
