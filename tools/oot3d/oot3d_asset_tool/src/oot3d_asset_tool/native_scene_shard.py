from __future__ import annotations

import argparse
import hashlib
import json
import re
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .zsi import ZsiFile
from .zar import ZarArchive


FORMAT = "oot3d_native_scene_shard_v1"
ARCHIVE_PREFIX = "oot3d/native/scene"
ROOM_CMAB_MEMBER = re.compile(r"^room([0-9]+)/(.*\.cmab)$", re.IGNORECASE)


def shard_manifest_resource(shard_name: str) -> str:
    return f"oot3d/catalog/shards/{shard_name}.json"


def _scene_sidecar_archive(scene_root: Path, scene_stem: str) -> dict[str, Any] | None:
    name = f"{scene_stem}.zar"
    path = scene_root / name
    if not path.is_file():
        return None

    archive = ZarArchive.from_path(path)
    room_members: dict[int, list[dict[str, Any]]] = {}
    type_counts: dict[str, int] = {}
    for file in archive.files:
        type_counts[file.type_name] = type_counts.get(file.type_name, 0) + 1
        normalized_name = file.name.replace("\\", "/")
        match = ROOM_CMAB_MEMBER.fullmatch(normalized_name)
        if match is None or file.type_name.lower() != "cmab":
            continue
        room_index = int(match.group(1))
        room_members.setdefault(room_index, []).append({
            "index": file.index,
            "type_local_index": file.type_local_index,
            "name": file.name,
            "normalized_name": normalized_name,
            "size": file.size,
        })

    payload = path.read_bytes()
    resource = f"{ARCHIVE_PREFIX}/{name}"
    return {
        "kind": "scene_sidecar_zar",
        "name": name,
        "path": str(path),
        "resource": resource,
        "byte_length": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "file_count": len(archive.files),
        "type_counts": type_counts,
        "room_material_animations": [
            {
                "room_index": room_index,
                "archive_resource": resource,
                "archive_byte_length": len(payload),
                "members": members,
            }
            for room_index, members in sorted(room_members.items())
        ],
    }


def build_native_scene_shard(scene_index: dict[str, Any], scene_root: Path, scene_stems: list[str],
                             shard_name: str, *, now: datetime | None = None) -> dict[str, Any]:
    now = now or datetime.now(timezone.utc)
    selected = [record for record in scene_index.get("records", [])
                if str(record.get("scene_stem")) in set(scene_stems)]
    found = {str(record["scene_stem"]) for record in selected}
    missing = sorted(set(scene_stems) - found)
    records = []
    source_names: set[str] = set()
    sidecar_sources: list[dict[str, Any]] = []
    for scene in selected:
        scene_path = str(scene["scene_path"])
        source_names.add(scene_path)
        room_paths = sorted({str(room["room_path"]) for room in scene.get("rooms", [])})
        source_names.update(room_paths)
        environment_setups = []
        for setup in scene.get("setups", []):
            commands = []
            for command in setup.get("commands", []):
                if command.get("command_name") not in {"light_settings_list", "skybox_settings", "misc_settings"}:
                    continue
                commands.append({"command_name": command["command_name"],
                                 "command_id": command["command_id"],
                                 "offset": command["offset"],
                                 "decoded": command.get("decoded", {})})
            environment_setups.append({"setup_index": int(setup["index"]),
                                       "setup_role": setup.get("setup_role"), "commands": commands})
        sidecar = _scene_sidecar_archive(scene_root, str(scene["scene_stem"]))
        if sidecar is not None:
            sidecar_sources.append(sidecar)
        records.append({
            "scene_stem": scene["scene_stem"],
            "scene_path": scene_path,
            "scene_resource": f"{ARCHIVE_PREFIX}/{scene_path}",
            "setup_indices": [int(setup["index"]) for setup in scene.get("setups", [])],
            "room_paths": room_paths,
            "room_resources": [f"{ARCHIVE_PREFIX}/{path}" for path in room_paths],
            "sidecar_archives": ([{
                "name": sidecar["name"],
                "resource": sidecar["resource"],
                "byte_length": sidecar["byte_length"],
                "sha256": sidecar["sha256"],
                "file_count": sidecar["file_count"],
                "type_counts": sidecar["type_counts"],
            }] if sidecar is not None else []),
            "room_material_animations": (
                sidecar["room_material_animations"] if sidecar is not None else []
            ),
            "environment_setups": environment_setups,
        })
    sources = []
    for name in sorted(source_names):
        path = scene_root / name
        if not path.is_file():
            raise FileNotFoundError(path)
        embedded = ZsiFile.from_path(path).embedded_cmbs()
        sources.append({"kind": "zsi", "name": name, "path": str(path), "resource": f"{ARCHIVE_PREFIX}/{name}",
                        "byte_length": path.stat().st_size,
                        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        "embedded_cmb_count": len(embedded),
                        "embedded_cmbs": [{"index": cmb.index, "offset": cmb.offset,
                                           "name": cmb.model.name,
                                           "material_count": len(cmb.model.materials),
                                           "texture_count": len(cmb.model.textures),
                                           "mesh_count": len(cmb.model.meshes)} for cmb in embedded]})
    sources.extend(sidecar_sources)
    return {"format": FORMAT, "generated_utc": now.isoformat(), "shard_name": shard_name,
            "status": "complete" if not missing else "blocked_missing_scenes",
            "requested_scene_stems": sorted(set(scene_stems)), "missing_scene_stems": missing,
            "scene_count": len(records), "source_count": len(sources), "records": records, "sources": sources}


def write_native_scene_archive(path: Path, manifest: dict[str, Any]) -> None:
    package_manifest = {"name": manifest["shard_name"], "author": "local", "version": "0.1.0",
                        "description": "Native OOT3D ZSI scene, room and sidecar ZAR sources.",
                        "license": "Generated from user-provided local assets; do not redistribute without rights.",
                        "code_version": 1}
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, payload in (("manifest.json", package_manifest),
                              (shard_manifest_resource(str(manifest["shard_name"])), manifest)):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, json.dumps(payload, indent=2) + "\n")
        for source in manifest["sources"]:
            info = zipfile.ZipInfo(source["resource"], date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, Path(source["path"]).read_bytes())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Package native OOT3D scene ZSI sources into a shard.")
    parser.add_argument("--scene-index", type=Path, required=True)
    parser.add_argument("--scene-root", type=Path, required=True)
    parser.add_argument("--scene", action="append", required=True)
    parser.add_argument("--shard-name", required=True)
    parser.add_argument("--output-manifest", type=Path, required=True)
    parser.add_argument("--output-archive", type=Path, required=True)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args(argv)
    scene_index = json.loads(args.scene_index.read_text(encoding="utf-8-sig"))
    manifest = build_native_scene_shard(scene_index, args.scene_root, args.scene, args.shard_name)
    args.output_manifest.parent.mkdir(parents=True, exist_ok=True)
    args.output_manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    if manifest["status"] == "complete":
        write_native_scene_archive(args.output_archive, manifest)
    print(args.output_manifest)
    print(args.output_archive)
    return 1 if args.verify and manifest["status"] != "complete" else 0


if __name__ == "__main__":
    raise SystemExit(main())
