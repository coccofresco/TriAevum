# MM3D room-emitter facts

Generate local metadata from the user's own EUR v1.1 assets:

```text
python titles/mm3d/tools/scene_emitters.py --code-bin <code.bin> --romfs <romfs.bin> --out <mm3d.scene-lights.json>
```

Pass it to the existing `--scene-light-catalog` runtime option. Automatic
installer/Forge generation and packaging are **not wired yet**. No game pixels
or catalog payloads are shipped here.

The code reader guards the native scene resource table and derives its ids;
there is no hand-maintained per-scene lighting list. The model reader supplies
MM3D v10 layout facts to shared rigid geometry/placement mechanisms. Compressed
ZSI inputs are boundedly decoded. Scene + room + canonical native ZSI header
identify the loaded room: ActorList/Mesh pointer relocations and flags are
normalised; bytes after End are not hashed.

Material roles come from authored texture semantics, never image statistics or
fullbright alone. A torch stand is excluded because its flame is actor-owned.
`kamado` (hearth) is not `mado` (window). Named room sources carry the separate
`independentEmission` assertion; matching/unknown native hardware identities
still fail closed. S14 observes only the catalog's encoded base-level hashes
and follows the native TEV/blend strength; it does not install PBR emission maps.

Current local EUR survey: 310 rooms, 145 emitters, zero rejected rooms. Four
categories have examples. Magic-glow and doorway role predicates are present,
but no populated/qualified examples have been established. This is a bounded
semantic catalog, **not a complete MM3D emission inventory or full parity**.

Tests: `python -m unittest discover -s titles/mm3d/tools -p 'test_*emit*.py'`.

Audit all used material texture slots (including rejected/unknown secondary roles)
without changing the catalog:

```text
python titles/mm3d/tools/emitter_census.py --code-bin <code.bin> --romfs <romfs.bin> --out <room-census.json>
```

The audit records encoded identity, semantic reason, additive state, resource SHA
and base/alternate mesh pointers. It does not infer atlas coverage from a texture
name or classify pixels. No matching magic/doorway name means **unproved**, not
semantic absence. A stable header fingerprint is not a payload attestation for
modified assets; those catalogs need regeneration/validation. Keep generated
asset inventories local; none are distributed in this checkout.
