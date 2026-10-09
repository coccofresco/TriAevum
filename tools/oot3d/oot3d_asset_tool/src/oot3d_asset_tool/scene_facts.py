"""Private, reproducible scene facts from extracted OoT3D files.

No N64 geometry substitutions, runtime emitter claims, or ROM data embedded in
this module. Addresses below describe the supported native code-image layout.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import re
import tempfile

from .binary import ParseError
from .kokiri_runtime_route_audit import actor_entry_from_payload, load_semantic_names
from .scene_geometry import (CollisionMesh, RayPolygon, collision_ray, entrance_poses,
                             overview_pose, visual_bounds, yaw_degrees)
from .zsi import ZsiFile
from .zsi_scene_index import (decode_light_settings_records, decode_room_index_command,
                              native_entrance_candidate, resolve_actor_object_semantics)

SCENE_COUNT = 0x6F
CODE_BASE = 0x100000
# Evidence: decomp_support/scripts/build_scene_resource_table.py, native loader
# 0x002EAFB4; dev-warp's native global entrance table (1586 four-byte rows).
NORMAL_SCENES = 0x4DC400
TAIL_SCENES = 0x545C2C
GLOBAL_ENTRANCES = 0x543BB8
GLOBAL_ENTRANCE_COUNT = 1586
RECORD_SIZE = 0x8C

# Names/IDs are labels from oot3d/actor_object_semantics.h. Evidence is the
# named reference actor's explicit LightContext_InsertLight calls, NOT proof
# that a placed OoT3D instance is active, lit, or has the same emitter slots.
EMITTER_RULES = {
    0x08: ("ACTOR_EN_LIGHT", "flame / decorative light", "EnLight_Init"),
    0x0D: ("ACTOR_EN_POH", "Poe lantern (moving)", "EnPoh_Init"),
    0x18: ("ACTOR_EN_ELF", "fairy (moving)", "EnElf_Init"),
    0x5E: ("ACTOR_OBJ_SYOKUDAI", "torch stand", "ObjSyokudai_Init"),
    0x91: ("ACTOR_EN_PO_SISTERS", "Poe Sister flame / lantern (state dependent)", "EnPoSisters_Init"),
}


def source_key(scene: int, setup: int, room: int | None, room_setup: int | None,
               kind: str, index: int, slot: int | None = None) -> dict:
    return {"sceneId": scene, "sceneSetup": setup, "roomId": room,
            "roomSetup": room_setup, "sourceKind": kind, "sourceIndex": index,
            "emitterSlot": slot}


def fingerprint(path: Path, data: bytes | None = None) -> dict:
    data = path.read_bytes() if data is None else data
    return {"path": str(path.resolve()), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def room_paths(zsi: ZsiFile, command) -> list[str]:
    """Native command 0x04: count records of 0x44-byte bounded path strings.

    The older audit helper searches only a 512-byte sample window, which omits
    rooms in large dungeons. Never use a sampled search for this manifest.
    """
    if command is None:
        return []
    offset = command.argument + 16
    if offset < 16 or offset + command.parameter * 0x44 > len(zsi.data):
        raise ParseError(f"{zsi.path}: room list outside file")
    result = []
    for index in range(command.parameter):
        raw = zsi.data[offset+index*0x44:offset+(index+1)*0x44]
        if b"\0" not in raw:
            raise ParseError(f"{zsi.path}: unterminated room path {index}")
        name = raw.split(b"\0", 1)[0].decode("ascii", errors="replace")
        if not re.fullmatch(r"rom:/scene/[A-Za-z0-9_]+\.zsi", name):
            raise ParseError(f"{zsi.path}: invalid native room path {index}: {name!r}")
        result.append(name.rsplit("/", 1)[1])
    return result


def scene_table(code: bytes) -> dict[int, dict]:
    """Read native IDs, never alphabetical/Better-menu indices.

    Fail closed if the known layout's complete table does not contain bounded
    native scene paths. No claim of support for other code-image revisions.
    """
    result = {}
    for scene in range(SCENE_COUNT):
        address = NORMAL_SCENES + scene * RECORD_SIZE if scene < 14 else TAIL_SCENES + (scene-14) * RECORD_SIZE
        offset = address - CODE_BASE
        raw = code[offset:offset+0x44]
        if len(raw) != 0x44 or b"\0" not in raw:
            raise ParseError(f"unsupported code.bin scene table at 0x{address:X}")
        try:
            name = raw.split(b"\0", 1)[0].decode("ascii")
        except UnicodeDecodeError as exc:
            raise ParseError(f"unsupported code.bin scene table at 0x{address:X}") from exc
        if not re.fullmatch(r"rom:/scene/[A-Za-z0-9_]+_info\.zsi", name):
            raise ParseError(f"unsupported code.bin scene table at 0x{address:X}: {name!r}")
        result[scene] = {"path": name.rsplit("/", 1)[1], "recordAddress": address, "fileOffset": offset}
    return result


class SceneFactsSource:
    def __init__(self, scene_root: Path, code_bin: Path, actor_names: Path | None = None):
        self.scene_root, self.code_bin = scene_root.resolve(), code_bin.resolve()
        if not self.scene_root.is_dir():
            raise ParseError(f"missing extracted RomFS scene directory: {self.scene_root}")
        if not self.code_bin.is_file():
            raise ParseError(f"missing extracted/decompressed code.bin: {self.code_bin}")
        self.code = self.code_bin.read_bytes()
        self.scenes = scene_table(self.code)
        self.code_input = fingerprint(self.code_bin, self.code)
        self.names_path = resolve_actor_object_semantics(actor_names)
        if actor_names is not None and not actor_names.is_file():
            raise ParseError(f"missing actor-name header: {actor_names}")
        self.names = load_semantic_names(self.names_path)
        self.names_input = fingerprint(self.names_path) if self.names_path else None

    def load_scene(self, scene: int) -> ZsiFile:
        if scene not in self.scenes:
            raise ParseError(f"scene 0x{scene:X} is outside the native scene table")
        path = self.scene_root / self.scenes[scene]["path"]
        if not path.is_file():
            raise ParseError(f"missing scene ZSI: {path}")
        return ZsiFile.from_path(path)

    def global_entrances(self, scene: int, local: int) -> list[int]:
        start = GLOBAL_ENTRANCES - CODE_BASE
        if start + GLOBAL_ENTRANCE_COUNT * 4 > len(self.code):
            raise ParseError("code.bin global entrance table is truncated")
        return [i for i in range(GLOBAL_ENTRANCE_COUNT)
                if self.code[start+i*4:start+i*4+2] == bytes((scene, local))]


def actor_records(zsi: ZsiFile, setup, scene: int, scene_setup: int, names: dict,
                  command_id: int, kind: str, room: int | None = None) -> list[dict]:
    command = setup.first_command(command_id)
    if command is None or command.parameter == 0:
        return []
    offset = command.argument + 16
    if offset < 16 or offset + command.parameter * 16 > len(zsi.data):
        raise ParseError(f"{zsi.path}: setup {setup.index} command 0x{command_id:02X} actor list outside file")
    result = []
    for index in range(command.parameter):
        actor = actor_entry_from_payload(zsi.data, offset+16*index, index, names)
        actor_id = actor["actor_id"] & 0xFFFF
        rule = EMITTER_RULES.get(actor_id)
        name = names["actor"].get(actor_id) or (rule[0] if rule else None)
        record = {"actorId": actor_id, "name": name, "params": actor["params"] & 0xFFFF,
                  "position": actor["pos"], "rotationBinaryAngles": actor["rot"],
                  "yawDegrees": yaw_degrees(actor["rot"][1]),
                  "sourceKey": source_key(scene, scene_setup, room, setup.index if room is not None else None, kind, index),
                  "sourceFile": zsi.path.name, "sourceOffset": actor["offset"],
                  "attributionMethod": "static ZSI command entry", "confidence": "high"}
        if rule and command_id != 0:
            record["emitterCandidate"] = {"role": rule[1], "confidence": "medium",
                "attributionMethod": "actor ID allowlist; reference actor registers a point light",
                "evidence": f"N64 reference {rule[2]}: LightContext_InsertLight; OoT3D ID label {rule[0]}",
                "active": None, "positionIs": "actor spawn, not resolved light position",
                "emitterSlot": None}
        result.append(record)
    return result


def selected_collision(zsi: ZsiFile, setup):
    command = setup.first_command(3)
    if command is None:
        return None
    # Shared headers occur in many setups; the existing parser deduplicates by
    # address, so match the command address rather than its first setup index.
    expected = command.argument + 16
    return next((c for c in zsi.collision_header_candidates() if c.offset == expected), None)


def emitter_candidates(actors: list[dict]) -> list[dict]:
    # Source-row attribution is high-confidence; the emitter claim is not.
    return [{**actor, "confidence": actor["emitterCandidate"]["confidence"],
             "attributionMethod": actor["emitterCandidate"]["attributionMethod"]}
            for actor in actors if "emitterCandidate" in actor]


def collision_mesh(candidate, source: str) -> CollisionMesh:
    mesh_id = f"{source}@0x{candidate.offset:X}"
    polygons = []
    for index, polygon in enumerate(candidate.polygons):
        surface = (candidate.effective_surface_types[polygon.type]
                   if polygon.type < len(candidate.effective_surface_types) else None)
        polygons.append(RayPolygon(polygon.vertex_indices, polygon.type,
            (surface.data1, surface.data2) if surface else None, index, polygon.normal))
    return CollisionMesh(mesh_id, tuple((v.x, v.y, v.z) for v in candidate.vertices), tuple(polygons))


def collision_summary(candidate, source: str) -> dict:
    return {"mesh": f"{source}@0x{candidate.offset:X}", "sourceFile": source,
            "headerOffset": candidate.offset, "vertexOffset": candidate.effective_vertex_offset,
            "polygonOffset": candidate.effective_polygon_offset,
            "vertexCount": len(candidate.vertices), "polygonCount": len(candidate.polygons),
            "declaredPolygonCount": candidate.raw_polygon_count,
            "surfaceTypeCount": len(candidate.effective_surface_types),
            "bounds": {"kind": "collision header", "min": candidate.bounds_min, "max": candidate.bounds_max},
            "status": "decoded_candidate" if len(candidate.polygons) == candidate.raw_polygon_count else "partial",
            "method": "existing ZSI layout recovery; static scene collision only",
            "evidence": list(candidate.evidence)}


def query_scene_collision(source: SceneFactsSource, scene: int, x: float, z: float,
                          y_origin: float | None = None, setup: int = 0) -> dict:
    zsi = source.load_scene(scene)
    setups = zsi.scene_setups()
    if not 0 <= setup < len(setups):
        raise ParseError(f"scene 0x{scene:04X}: setup {setup} unavailable")
    candidate = selected_collision(zsi, setups[setup])
    if candidate is None:
        # An unavailable mesh must not masquerade as a valid no-hit query.
        raise ParseError(f"scene 0x{scene:04X} setup {setup}: no decoded native collision header")
    result = collision_ray([collision_mesh(candidate, zsi.path.name)], x, z, y_origin)
    return {"schema": "triaevum.collision-ray", "version": 1, "sceneId": scene,
            "sceneSetup": setup, "units": "world", "provenance": [source.code_input, fingerprint(zsi.path, zsi.data)],
            "collision": collision_summary(candidate, zsi.path.name), **result}


def build_scene_facts(source: SceneFactsSource, scene: int, setup_index: int | None = None,
                      eye_height: float = 60.0) -> dict:
    if not math.isfinite(eye_height) or eye_height < 0:
        raise ValueError("eye height must be finite and nonnegative")
    zsi = source.load_scene(scene)
    setups = zsi.scene_setups()
    if not setups or (setup_index is not None and not 0 <= setup_index < len(setups)):
        raise ParseError(f"scene 0x{scene:04X}: requested setup unavailable")
    inputs = {str(zsi.path): fingerprint(zsi.path, zsi.data)}
    room_cache = {}
    # Decode each collision header once, even for scenes with many setups.
    collisions = {c.offset: c for c in zsi.collision_header_candidates()}
    unknowns = [
        "Setup indices are native header ordinals; age/time/story-state selection is not resolved offline.",
        "Emitter candidates are static actor hints, not observed lights. Active state, radius, colour, light offset and emitterSlot require runtime observation; the allowlist is not exhaustive.",
        "Collision recovery uses the existing parser's layout heuristics; moving actor collision and live surface filtering are absent.",
        "Room visual bounds combine every embedded CMB visibility group; setup-specific visibility, prerendered backgrounds, actors and animation are not resolved.",
        "Environment time interpolation and weather selection are unknown; diagnostic RGB bytes are not renderer linear colours.",
        "Only the normal scene resource table is selected; Master Quest replacements are not included.",
    ]
    facts = {"schema": "triaevum.scene-facts", "version": 1, "sceneId": scene,
             "sceneIdHex": f"0x{scene:04X}", "name": zsi.path.name.removesuffix("_info.zsi"),
             "nameKind": "native resource stem", "variant": "normal", "units": "world",
             "coordinates": {"up": "+Y", "yawZero": "+Z", "yawPositive90": "+X", "angles": "degrees"},
             "eyeHeight": eye_height, "setups": [], "poseCandidates": [], "unknowns": unknowns}
    for setup in setups:
        if setup_index is not None and setup.index != setup_index:
            continue
        scope = f"setup {setup.index}"
        spawns = actor_records(zsi, setup, scene, setup.index, source.names, 0, "spawn")
        actors = actor_records(zsi, setup, scene, setup.index, source.names, 1, "sceneActor")
        record = {"sceneSetup": setup.index, "sourceOffset": setup.offset,
                  "spawns": spawns, "entrances": [], "actors": actors, "rooms": [],
                  "emitterCandidates": emitter_candidates(actors)}
        entrance_command = setup.first_command(6)
        entrances = native_entrance_candidate(zsi.data, entrance_command) if entrance_command and entrance_command.parameter else None
        if entrance_command and entrance_command.parameter and entrances is None:
            raise ParseError(f"{zsi.path}: {scope} entrance list outside file")
        for entrance in (entrances or {}).get("entries", []):
            spawn = spawns[entrance["spawn"]] if entrance["spawn"] < len(spawns) else None
            entry = {"index": entrance["index"], "spawnIndex": entrance["spawn"], "roomId": entrance["room"],
                     "sourceOffset": entrance["offset"],
                     "globalEntranceIndices": source.global_entrances(scene, entrance["index"]),
                     "position": spawn["position"] if spawn else None,
                     "yawDegrees": spawn["yawDegrees"] if spawn else None}
            if spawn is None:
                unknowns.append(f"{scope} entrance {entry['index']}: spawn {entry['spawnIndex']} unavailable")
            record["entrances"].append(entry)
            facts["poseCandidates"].extend(entrance_poses(entry, setup.index, eye_height))
        for spawn in spawns:
            spawn["roomIds"] = sorted({e["roomId"] for e in record["entrances"] if e["spawnIndex"] == spawn["sourceKey"]["sourceIndex"]})
        room_command = setup.first_command(4)
        refs = room_paths(zsi, room_command)
        count = room_command.parameter if room_command else 0
        if len(refs) < count:
            raise ParseError(f"{zsi.path}: {scope} expects {count} room paths, decoded {len(refs)}")
        for room_id, name in enumerate(refs[:count]):
            # The room table ordinal is authoritative, not the filename suffix.
            if not re.fullmatch(r"[A-Za-z0-9_]+\.zsi", name):
                raise ParseError(f"{zsi.path}: invalid room filename {name!r}")
            if name not in room_cache:
                path = source.scene_root / name
                if not path.is_file():
                    raise ParseError(f"missing room ZSI: {path}")
                room_zsi = ZsiFile.from_path(path)
                inputs[str(path)] = fingerprint(path, room_zsi.data)
                try:
                    bounds = visual_bounds(c.model for c in room_zsi.embedded_cmbs())
                except (ParseError, IndexError, ValueError, RecursionError) as exc:
                    bounds = {"kind": "visual", "min": None, "max": None, "status": "unavailable", "error": str(exc)}
                room_cache[name] = (room_zsi, room_zsi.scene_setups(), bounds)
            room_zsi, room_setups, bounds = room_cache[name]
            selected = room_setups[setup.index] if setup.index < len(room_setups) else None
            room = {"roomId": room_id, "sourceFile": name, "visualBounds": bounds,
                    "roomSetup": selected.index if selected else None, "availableRoomSetups": [s.index for s in room_setups],
                    "setupBinding": "matching native header ordinal" if selected else "unresolved",
                    "actors": [], "emitterCandidates": [], "roomSettings": []}
            if selected is None:
                unknowns.append(f"{scope} room {room_id}: matching room header unavailable; no fallback guessed")
            else:
                room["actors"] = actor_records(room_zsi, selected, scene, setup.index, source.names, 1, "roomActor", room_id)
                room["emitterCandidates"] = emitter_candidates(room["actors"])
                for command in selected.commands:
                    if command.command_id in (0x08, 0x0C, 0x10, 0x12):
                        decoded = decode_room_index_command(room_zsi.data, command, selected.commands, source.names,
                                                            sample_limit=255, full_entries=True)["decoded"]
                        room["roomSettings"].append({"commandId": command.command_id, "status": "diagnostic", "decoded": decoded})
            if bounds["status"] != "decoded":
                unknowns.append(f"{scope} room {room_id}: visual bounds {bounds['status']}")
            record["rooms"].append(room)
            pose = overview_pose(bounds, setup.index, room_id)
            if pose:
                facts["poseCandidates"].append(pose)
        sky = setup.first_command(0x11)
        record["sky"] = ({"skyboxId": sky.argument & 255, "indoorFlagRaw": (sky.argument >> 16) & 255,
                          "indoors": bool((sky.argument >> 16) & 255), "weatherByteDiagnostic": (sky.argument >> 8) & 255,
                          "source": "ZSI command 0x11", "sourceOffset": sky.offset}
                         if sky else {"skyboxId": None, "indoors": None})
        if sky is None:
            unknowns.append(f"{scope}: sky/indoor command absent")
        light = setup.first_command(0x0F)
        environment = decode_light_settings_records(zsi.data, light.argument+16, light.parameter,
                      sample_limit=255, full_entries=True) if light and light.parameter else None
        record["environment"] = {"status": "diagnostic", "recordLayout": "native 0x1C-byte records",
            "records": (environment or {}).get("records", []),
            "decodeStatus": environment["status"] if environment else "absent",
            "interpretation": "native_env_consumer_0045dd50 is the audited candidate; legacy prefix and actor packet candidates are diagnostics, not interchangeable"}
        if environment and environment["status"] != "decoded_light_settings_list":
            unknowns.append(f"{scope}: {environment['status']}")
        elif environment is None:
            unknowns.append(f"{scope}: environment records absent")
        collision_command = setup.first_command(3)
        candidate = collisions.get(collision_command.argument+16) if collision_command else None
        record["collision"] = collision_summary(candidate, zsi.path.name) if candidate else None
        if candidate is None:
            unknowns.append(f"{scope}: static collision unavailable")
        elif len(candidate.polygons) != candidate.raw_polygon_count:
            unknowns.append(f"{scope}: collision recovery is partial; inspect declared and decoded polygon counts")
        facts["setups"].append(record)
    facts["provenance"] = {"sceneRoot": str(source.scene_root), "sceneTable": source.scenes[scene],
        "layout": "native code-image addresses; structurally validated table; see README for revision limits",
        "inputs": [source.code_input] + ([source.names_input] if source.names_input else []) + [inputs[k] for k in sorted(inputs)],
        "tool": "oot3d_asset_tool.scene_facts v1"}
    return facts


def register_commands(subparsers) -> None:
    for name, help_text in (("scene-facts", "Export private offline scene facts."),
                            ("collision-ray", "Query all static collision hits below X/Z.")):
        parser = subparsers.add_parser(name, help=help_text)
        parser.add_argument("--extract-root", type=Path, help="Extraction root containing romfs/ and exefs/; required unless OOT3D_EXTRACT_ROOT is set.")
        parser.add_argument("--scene-root", type=Path, help="Override <extract-root>/romfs/scene.")
        parser.add_argument("--code-bin", type=Path, help="Override <extract-root>/exefs/code.bin.")
        parser.add_argument("--actor-names", type=Path, help="Optional Oot3dActorId enum header.")
        parser.add_argument("--setup", type=int, default=None if name == "scene-facts" else 0,
                            help="Native setup ordinal; facts default to all, ray defaults to 0.")
        if name == "scene-facts":
            selection = parser.add_mutually_exclusive_group(required=True)
            selection.add_argument("--scene", type=lambda x: int(x, 0), action="append")
            selection.add_argument("--all", action="store_true")
            parser.add_argument("--out", type=Path, required=True)
            parser.add_argument("--eye-height", type=float, default=60.0)
        else:
            parser.add_argument("--scene", type=lambda x: int(x, 0), required=True)
            parser.add_argument("--x", type=float, required=True)
            parser.add_argument("--z", type=float, required=True)
            parser.add_argument("--y-origin", type=float)


def write_json(path: Path, document: dict) -> None:
    canonical = json.dumps(document, indent=2, allow_nan=False) + "\n"
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, prefix=f".{path.name}.",
                                         suffix=".tmp", encoding="utf-8", newline="\n", delete=False) as output:
            temporary = Path(output.name)
            output.write(canonical)
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def run_command(args) -> int:
    configured_root = args.extract_root or os.environ.get("OOT3D_EXTRACT_ROOT", "").strip()
    if not configured_root:
        raise ValueError("provide --extract-root or set OOT3D_EXTRACT_ROOT to the directory containing romfs/ and exefs/")
    extract = Path(configured_root)
    source = SceneFactsSource(args.scene_root or extract / "romfs/scene",
                              args.code_bin or extract / "exefs/code.bin", args.actor_names)
    if args.command == "collision-ray":
        print(json.dumps(query_scene_collision(source, args.scene, args.x, args.z, args.y_origin, args.setup),
                         indent=2, allow_nan=False))
        return 0
    # Reject repo outputs: these documents contain private, ROM-derived data.
    destination = args.out.resolve()
    repo = next((p for p in Path(__file__).resolve().parents
                 if (p / "tools/oot3d/oot3d_asset_tool").is_dir()), None)
    if repo is not None and destination.is_relative_to(repo):
        raise ValueError("scene facts contain ROM-derived data; --out must be outside the repository")
    selected = sorted(source.scenes) if args.all else sorted(set(args.scene))
    # Preflight scene and room inputs before writing any outputs.
    missing, available, skipped = [], [], []
    for scene in selected:
        if scene in source.scenes and not (source.scene_root / source.scenes[scene]["path"]).is_file():
            reason = f"missing scene ZSI: {source.scene_root / source.scenes[scene]['path']}"
            if args.all:
                skipped.append({"scene": scene, "reason": reason})
            else:
                missing.append(f"scene 0x{scene:04X}: {reason}")
            continue
        zsi = source.load_scene(scene)
        available.append(scene)
        for setup in zsi.scene_setups():
            if args.setup is not None and setup.index != args.setup:
                continue
            command = setup.first_command(4)
            if command:
                for name in room_paths(zsi, command):
                    if not (source.scene_root / name).is_file():
                        missing.append(f"scene 0x{scene:04X}, room: {source.scene_root / name}")
    if missing:
        raise ParseError("missing extracted ZSI inputs (no outputs written):\n" + "\n".join(sorted(set(missing))))
    destination.mkdir(parents=True, exist_ok=True)
    written = []
    for scene in available:
        facts = build_scene_facts(source, scene, args.setup, args.eye_height)
        path = destination / f"scene-{scene:04X}.facts.json"
        write_json(path, facts)
        written.append({"scene": scene, "file": path.name})
        print(json.dumps({"sceneId": scene, "file": str(path), "setups": len(facts["setups"]),
                          "poses": len(facts["poseCandidates"]), "unknowns": len(facts["unknowns"])}), flush=True)
    if args.all:
        summary_path = destination / "all-scenes-summary.json"
        write_json(summary_path, {"schema": "triaevum.all-scenes-summary", "version": 1,
            "writtenCount": len(written), "skippedCount": len(skipped), "written": written, "skipped": skipped})
        print(json.dumps({"summary": str(summary_path), "written": len(written), "skipped": len(skipped)}), flush=True)
    return 0 if written else 1
