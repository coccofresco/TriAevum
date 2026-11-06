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
