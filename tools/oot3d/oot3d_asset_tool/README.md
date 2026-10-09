# OoT3D Asset Tool

`oot3d-asset-tool` parses, audits, indexes, and exports native Ocarina of Time
3D assets outside the game runtime. It consumes files extracted locally from a
user-provided RomFS and never includes copyrighted game data in this repository.

## Install

```powershell
python -m pip install -e tools\oot3d\oot3d_asset_tool
oot3d-assets --help
```

The native paths cover CMB models, CSAB animation, CMAB material animation,
ZSI scenes, QDB cutscenes, CTXB textures, collision, environment data, and Room
Compilation Units.

Some older subcommands can still emit Fast resource XML or `.o2r` archives for
regression comparison. Those are isolated legacy export adapters, not runtime
dependencies and not the preferred input path for the native renderer.

## Tests

```powershell
python -m unittest discover -s tools\oot3d\oot3d_asset_tool\tests
```

Generated assets and extracted game files must be written outside the repo.

## Offline scene facts and collision rays

These commands read the user's extracted **RomFS `scene/` directory and
decompressed ExeFS `code.bin`**. They do not run the game, load graphics settings,
or resolve moving lights. All outputs are private ROM-derived evidence and must
stay outside the repository. No extra Python dependencies are needed.

```powershell
# From the repository root (or install the package as above):
$env:PYTHONPATH = "$PWD/tools/oot3d/oot3d_asset_tool/src"
$env:OOT3D_EXTRACT_ROOT = '<extract-root>'
python -B -m oot3d_asset_tool scene-facts --scene 0x55 --scene 0x52 `
  --out '<out-dir>'
python -B -m oot3d_asset_tool scene-facts --scene 0x03 --setup 0 --eye-height 60 `
  --out '<out-dir>'
python -B -m oot3d_asset_tool scene-facts --extract-root '<extract-root>' --all `
  --out '<out-dir>'
python -B -m oot3d_asset_tool collision-ray --scene 0x55 --setup 0 `
  --x 0 --z 0 --y-origin 2000
```

Both commands require `--extract-root` or `OOT3D_EXTRACT_ROOT`; the CLI flag takes
precedence. There is no machine-specific default. `--scene-root` and `--code-bin`
can override the `romfs/scene` and `exefs/code.bin` paths below that root.
`--actor-names` optionally supplies the local `Oot3dActorId` enum header; in a
checkout the existing decomp-support header is discovered automatically.
The six pilot IDs are `0x55`, `0x52`, `0x34`, `0x03`, `0x04`, `0x05`; pass six
`--scene` arguments to export them together. `--all` requests all 111 native
table entries and skips missing scene files (retail extractions may omit debug/test
scenes). It writes `all-scenes-summary.json` with `writtenCount`, `skippedCount`,
`written: [{"scene", "file"}]` and `skipped: [{"scene", "reason"}]`; scene values are
native numeric IDs. Exit status is 0 when at least one scene was written, or 1 if
all scenes were missing. Explicit `--scene` requests still fail on missing inputs;
missing room files and malformed input remain errors. Counts describe this run,
not pre-existing files in the output directory.

Each `scene-XXXX.facts.json` uses `schema: "triaevum.scene-facts", version: 1`:

- Identity: native scene ID, resource stem (not an invented display name), normal
  variant, code-table record offset, and SHA-256/size/path for every consumed
  scene, room, code image, and optional actor-name header. Output has no timestamp
  and is deterministic for identical inputs, paths, options, and tool version.
- `setups[]`: native header ordinals, spawns, local entrances with matching global
  entrance indices, scene actors, rooms and their actor lists. Room ID is the
  room-list ordinal. A room header is paired only with the same setup ordinal;
  unavailable bindings remain unknown rather than silently selecting setup zero.
- `rooms[].visualBounds`: world-space AABB of referenced rigid CMB vertices after
  parent/child scale, rotation and translation. All embedded models and visibility
  groups are included. Bounds exclude actors, skeletal animation and prerendered
  backgrounds; unsupported primitives make the result `partial`.
- `environment.records[]`: original 28-byte records and existing audit decodes.
  All interpretations are marked diagnostic. The native consumer interpretation
  (`native_env_consumer_0045dd50`) is distinct from the legacy-prefix and actor
  packet hypotheses. RGB bytes are **not** claimed to be renderer linear colours;
  fog/environment colour meaning and time/weather interpolation remain uncertain.
  `sky` preserves command `0x11`'s skybox ID and indoor flag; room time/sky controls
  and explicit room-light records are preserved as diagnostics.
- `poseCandidates[]`: forward/back views at each entrance spawn plus eye height
  (default 60 world units), and an overview derived from each room's visual AABB.
  Y is up; yaw zero faces +Z and +90 faces +X; positive pitch looks up. Every pose
  is `unverified`: geometry clearance, culling and visual usefulness require the
  runtime pilot. These are not gameplay-camera replays.
- `unknowns[]`: limitations and scene-specific missing/partial decoding. Offline
  setup ordinals do not resolve age, time of day, quest flags or cutscene state.

Actor entries carry the following stable `sourceKey` (all IDs/indices are integers;
absent scope components and unresolved emitter slots are JSON `null`):

```json
{
  "sceneId": 85,
  "sceneSetup": 0,
  "roomId": 0,
  "roomSetup": 0,
  "sourceKind": "roomActor",
  "sourceIndex": 0,
  "emitterSlot": null
}
```

`sourceKind` is `spawn`, `sceneActor`, or `roomActor`; `sourceIndex` is the original
command-list entry index, never an index in the filtered emitter list. Actor ID,
name if known, unsigned 16-bit params, position and raw binary rotations remain
separate fields. Keys identify source entries, not live actor instances. File
hashes identify the extraction revision. Room transitions and runtime-spawned
actors are not enumerated by this static actor-list export.

Emitter candidates are deliberately a small allowlist. The IDs/names come from
the existing OoT3D actor enum; each named N64 reference initializer explicitly
calls `LightContext_InsertLight`. This is **medium-confidence candidate evidence**,
not proof of OoT3D live emission or a light-to-actor binding:

| ID | Actor | Reason / reference initializer |
| --- | --- | --- |
| `0x0008` | `ACTOR_EN_LIGHT` | Decorative flames, `EnLight_Init` |
| `0x000D` | `ACTOR_EN_POH` | Moving Poe lantern, `EnPoh_Init` |
| `0x0018` | `ACTOR_EN_ELF` | Fairy lights, `EnElf_Init` |
| `0x005E` | `ACTOR_OBJ_SYOKUDAI` | Torch stand, `ObjSyokudai_Init` |
| `0x0091` | `ACTOR_EN_PO_SISTERS` | State-dependent lantern/flame, `EnPoSisters_Init` |

Spawn position is not light position; no reference offsets, radii, colours or
slot counts are transplanted into OoT3D facts. `emitterSlot` and `active` stay
unknown until runtime observation. Purely visual fire, light shafts, lamps in
geometry, and dynamically spawned effects are not inferred from actor names.

### Collision API and interpretation

```python
from pathlib import Path
from oot3d_asset_tool.scene_facts import SceneFactsSource, query_scene_collision

source = SceneFactsSource(Path("<extract-root>/romfs/scene"),
                          Path("<extract-root>/exefs/code.bin"))
result = query_scene_collision(source, 0x55, x=0, z=0, y_origin=2000, setup=0)
```

`scene_geometry.collision_ray(meshes, x, z, y_origin=None)` also accepts pure
Python `CollisionMesh`/`RayPolygon` values. The scene API selects the collision
header referenced by the requested setup, reusing the existing ZSI recovery
parser; facts expose its offsets, counts and recovery evidence. If no origin is
given, the ray starts one world unit above the highest decoded collision vertex.

Hits contain position, distance, a unit normal from triangle winding, original
polygon index, surface-type index/raw words, native normal and collision mesh
identity. All downward intersections are sorted nearest first, including stacked
floors, ceilings and backfaces. Coincident hits from distinct polygons are kept.
Vertical walls and degenerate XZ triangles do not intersect this ray. This is a
geometric query, not the guest's collision filter: dynamic actors, surface flags,
and walkability are not applied. `no-hit` means a decoded mesh was queried;
missing/undecodable collision is an error, never a fabricated no-hit.

The code-image layout uses the audited native addresses in
`decomp_support/scripts/build_scene_resource_table.py` (normal first 14 scenes,
static tail, 0x8C-byte records) and the developer warp's global entrance table.
All 111 bounded scene paths must validate before the layout is accepted. Other
regions/revisions and Master Quest replacement tables are not claimed supported;
use the recorded code hash when joining facts to runtime observations.

Synthetic checks (no title inputs required):

```powershell
python -B -m unittest discover -s tools/oot3d/oot3d_asset_tool/tests -p test_scene_facts.py -v
```

They cover transformed visual bounds, hierarchy validation, full room lists,
stacked floors, slopes, backfaces, no-hit, native collision decoding/shared headers,
yaw/pose conventions, stable source keys, deterministic output and missing inputs.
