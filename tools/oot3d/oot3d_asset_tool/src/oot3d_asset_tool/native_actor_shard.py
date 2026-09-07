from __future__ import annotations

import argparse
import hashlib
import json
import zipfile
from pathlib import Path, PurePosixPath
from typing import Any

from .native_abi_catalog import load_native_abi_catalog
from .enko_native_runtime_contract import (
    RESOURCE as ENKO_RUNTIME_CONTRACT_RESOURCE,
    build_enko_native_runtime_contract,
)
from .ensa_native_runtime_contract import (
    RESOURCE as ENSA_RUNTIME_CONTRACT_RESOURCE,
    build_ensa_native_runtime_contract,
)
from .enmd_native_runtime_contract import (
    RESOURCE as ENMD_RUNTIME_CONTRACT_RESOURCE,
    build_enmd_native_runtime_contract,
)
from .enkusa_native_runtime_contract import (
    RESOURCE as ENKUSA_RUNTIME_CONTRACT_RESOURCE,
    build_enkusa_native_runtime_contract,
)
from .enholl_native_runtime_contract import (
    RESOURCE as ENHOLL_RUNTIME_CONTRACT_RESOURCE,
    build_enholl_native_runtime_contract,
)
from .objhana_native_runtime_contract import (
    RESOURCE as OBJHANA_RUNTIME_CONTRACT_RESOURCE,
    build_objhana_native_runtime_contract,
)
from .rigid_actor_native_runtime_contract import (
    RESOURCE as RIGID_ACTOR_RUNTIME_CONTRACT_RESOURCE,
    build_rigid_actor_native_runtime_contract,
)
from .item_actor_native_runtime_contract import (
    RESOURCE as ITEM_ACTOR_RUNTIME_CONTRACT_RESOURCE,
    build_item_actor_native_runtime_contract,
)
from .zar import ZarArchive


FORMAT = "oot3d_native_actor_shard_v1"
MANIFEST_RESOURCE = "oot3d/catalog/shards/oot3d-actors-native.json"
RESOURCE_PREFIX = "oot3d/native/actor"


def room_compilation_object_sources(
    unit_paths: list[Path], actor_source_root: Path
) -> tuple[list[Path], list[dict[str, Any]]]:
    sources: list[Path] = []
    bindings: list[dict[str, Any]] = []
    seen_sources: set[Path] = set()
    seen_bindings: set[tuple[str, int]] = set()
    for unit_path in unit_paths:
        unit = json.loads(unit_path.read_text(encoding="utf-8-sig"))
        if unit.get("format") != "oot3d_room_compilation_unit_v1":
            raise ValueError(f"{unit_path}: unsupported room compilation unit format")
        identity = unit.get("identity")
        if not isinstance(identity, dict) or not isinstance(identity.get("unit_id"), str):
            raise ValueError(f"{unit_path}: room compilation unit has no unit_id")
        unit_id = identity["unit_id"]
        dependencies = unit.get("object_dependencies")
        if not isinstance(dependencies, list):
            raise ValueError(f"{unit_path}: room compilation unit has no object dependencies")
        for dependency in dependencies:
            if not isinstance(dependency, dict) or not isinstance(dependency.get("object_id"), int):
                raise ValueError(f"{unit_path}: invalid object dependency")
            object_id = dependency["object_id"]
            binding_key = (unit_id, object_id)
            if binding_key in seen_bindings:
                raise ValueError(f"{unit_path}: duplicate object dependency {object_id}")
            seen_bindings.add(binding_key)
            payload_status = dependency.get("payload_status")
            if payload_status not in {
                "native_zar_materialized",
                "native_unmaterialized_object_bank_reference",
            }:
                raise ValueError(
                    f"{unit_path}: unsupported object dependency payload status {payload_status!r}"
                )
            logical_path = ""
            for owner in (dependency.get("native_path_record"), dependency.get("source")):
                if isinstance(owner, dict) and isinstance(owner.get("logical_path"), str):
                    logical_path = owner["logical_path"]
                    if logical_path:
                        break
            resource: str | None = None
            if payload_status == "native_zar_materialized":
                logical = PurePosixPath(logical_path)
                if not logical.parts or logical.parts[0] != "actor" or ".." in logical.parts:
                    raise ValueError(
                        f"{unit_path}: object {object_id} has invalid actor path {logical_path!r}"
                    )
                source = actor_source_root.joinpath(*logical.parts[1:]).resolve()
                if not source.is_file():
                    raise FileNotFoundError(source)
                resource = f"{RESOURCE_PREFIX}/{source.name}"
                if source not in seen_sources:
                    seen_sources.add(source)
                    sources.append(source)
            bindings.append({
                "unit_id": unit_id,
                "object_id": object_id,
                "payload_status": payload_status,
                "source_container": logical_path,
                "resource": resource,
                "required_by": dependency.get("required_by", []),
            })
    return sources, bindings


def build_manifest(
    sources: list[Path], object_bank_bindings: list[dict[str, Any]] | None = None
) -> dict[str, object]:
    records: list[dict[str, object]] = []
    for source in sources:
        data = source.read_bytes()
        archive = ZarArchive.parse(data, str(source))
        type_counts: dict[str, int] = {}
        files = []
        for entry in archive.files:
            type_local_index = type_counts.get(entry.type_name, 0)
            type_counts[entry.type_name] = type_local_index + 1
            files.append({"index": entry.index, "name": entry.name,
                          "type": entry.type_name, "type_local_index": type_local_index,
                          "offset": entry.offset, "size": entry.size})
        records.append({
            "archive_name": source.name,
            "source_container": f"actor/{source.name}",
            "resource": f"{RESOURCE_PREFIX}/{source.name}",
            "source_path": str(source),
            "byte_length": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "file_count": len(files),
            "type_counts": type_counts,
            "files": files,
        })
    return {"format": FORMAT, "status": "complete", "archive_count": len(records),
            "records": records, "object_bank_bindings": object_bank_bindings or []}


def write_archive(path: Path, manifest: dict[str, object],
                  extra_resources: dict[str, object] | None = None) -> None:
    package = {"name": "OOT3D Native Actors", "author": "local", "version": "0.1.0",
               "description": "Unmodified native OOT3D actor ZAR sources.", "code_version": 1}
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for name, value in (("manifest.json", package), (MANIFEST_RESOURCE, manifest)):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(info, json.dumps(value, indent=2) + "\n")
        for name, value in (extra_resources or {}).items():
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(info, json.dumps(value, indent=2) + "\n")
        for record in manifest["records"]:
            info = zipfile.ZipInfo(record["resource"], date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(info, Path(record["source_path"]).read_bytes())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Package unmodified native OOT3D actor ZAR sources.")
    parser.add_argument("--source", type=Path, action="append", default=[])
    parser.add_argument("--room-compilation-unit", type=Path, action="append", default=[])
    parser.add_argument("--actor-source-root", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest-output", type=Path, required=True)
    parser.add_argument("--code-bin", type=Path)
    parser.add_argument("--native-abi-catalog", type=Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args(argv)
    if args.room_compilation_unit and args.actor_source_root is None:
        parser.error("--actor-source-root is required with --room-compilation-unit")
    for unit_path in args.room_compilation_unit:
        if not unit_path.is_file():
            parser.error(f"room compilation unit not found: {unit_path}")
    rcu_sources, object_bank_bindings = room_compilation_object_sources(
        args.room_compilation_unit, args.actor_source_root
    ) if args.room_compilation_unit else ([], [])
    sources: list[Path] = []
    seen_sources: set[Path] = set()
    for source in [*args.source, *rcu_sources]:
        source = source.resolve()
        if source in seen_sources:
            continue
        seen_sources.add(source)
        sources.append(source)
    if not sources:
        parser.error("no native actor sources selected")
    for source in sources:
        if not source.is_file():
            parser.error(f"native actor source not found: {source}")
    if args.code_bin is not None and not args.code_bin.is_file():
        parser.error(f"OOT3D code.bin not found: {args.code_bin}")
    if args.native_abi_catalog is not None and args.code_bin is None:
        parser.error("--native-abi-catalog requires --code-bin")
    native_abi_catalog = None
    if args.native_abi_catalog is not None:
        if not args.native_abi_catalog.is_file():
            parser.error(
                f"OOT3D native ABI catalog not found: {args.native_abi_catalog}"
            )
        try:
            native_abi_catalog = load_native_abi_catalog(args.native_abi_catalog)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            parser.error(str(error))
    manifest = build_manifest(sources, object_bank_bindings)
    extra_resources: dict[str, object] = {}
    if args.code_bin is not None:
        enko_contract = build_enko_native_runtime_contract(
            args.code_bin, sources, native_abi_catalog
        )
        ensa_contract = build_ensa_native_runtime_contract(args.code_bin, sources)
        enmd_contract = build_enmd_native_runtime_contract(args.code_bin, sources)
        enkusa_contract = build_enkusa_native_runtime_contract(args.code_bin, sources)
        enholl_contract = build_enholl_native_runtime_contract(args.code_bin)
        objhana_contract = build_objhana_native_runtime_contract(args.code_bin, sources)
        rigid_actor_contract = build_rigid_actor_native_runtime_contract(
            args.code_bin, sources
        )
        item_actor_contract = build_item_actor_native_runtime_contract(
            args.code_bin, sources
        )
        manifest["runtime_contracts"] = [
            {
                "format": enko_contract["format"],
                "resource": ENKO_RUNTIME_CONTRACT_RESOURCE,
            },
            {
                "format": ensa_contract["format"],
                "resource": ENSA_RUNTIME_CONTRACT_RESOURCE,
            },
            {
                "format": enmd_contract["format"],
                "resource": ENMD_RUNTIME_CONTRACT_RESOURCE,
            },
            {
                "format": enkusa_contract["format"],
                "resource": ENKUSA_RUNTIME_CONTRACT_RESOURCE,
            },
            {
                "format": enholl_contract["format"],
                "resource": ENHOLL_RUNTIME_CONTRACT_RESOURCE,
            },
            {
                "format": objhana_contract["format"],
                "resource": OBJHANA_RUNTIME_CONTRACT_RESOURCE,
            },
            {
                "format": rigid_actor_contract["format"],
                "resource": RIGID_ACTOR_RUNTIME_CONTRACT_RESOURCE,
            },
            {
                "format": item_actor_contract["format"],
                "resource": ITEM_ACTOR_RUNTIME_CONTRACT_RESOURCE,
            },
        ]
        extra_resources[ENKO_RUNTIME_CONTRACT_RESOURCE] = enko_contract
        extra_resources[ENSA_RUNTIME_CONTRACT_RESOURCE] = ensa_contract
        extra_resources[ENMD_RUNTIME_CONTRACT_RESOURCE] = enmd_contract
        extra_resources[ENKUSA_RUNTIME_CONTRACT_RESOURCE] = enkusa_contract
        extra_resources[ENHOLL_RUNTIME_CONTRACT_RESOURCE] = enholl_contract
        extra_resources[OBJHANA_RUNTIME_CONTRACT_RESOURCE] = objhana_contract
        extra_resources[RIGID_ACTOR_RUNTIME_CONTRACT_RESOURCE] = rigid_actor_contract
        extra_resources[ITEM_ACTOR_RUNTIME_CONTRACT_RESOURCE] = item_actor_contract
    args.manifest_output.parent.mkdir(parents=True, exist_ok=True)
    args.manifest_output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    write_archive(args.output, manifest, extra_resources)
    if args.verify:
        with zipfile.ZipFile(args.output) as archive:
            for record in manifest["records"]:
                payload = archive.read(record["resource"])
                if hashlib.sha256(payload).hexdigest() != record["sha256"]:
                    raise RuntimeError(f"archive payload hash mismatch: {record['archive_name']}")
            for resource, value in extra_resources.items():
                if json.loads(archive.read(resource)) != value:
                    raise RuntimeError(f"archive runtime contract mismatch: {resource}")
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
