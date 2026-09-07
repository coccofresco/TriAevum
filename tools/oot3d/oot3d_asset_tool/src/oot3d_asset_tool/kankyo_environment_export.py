from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from .binary import ParseError
from .cmb import CmbModel
from .kankyo_environment_audit import (
    audit_kankyo_environment_assets,
    environment_model_group,
)
from .romfs_inventory import sorted_counter
from .legacy_fast_resource import LegacyFastResourceOptions, export_static_model
from .static_batch_audit import audit_static_batch
from .zar import ZarArchive


KANKYO_ENVIRONMENT_EXPORT_MANIFEST = "kankyo_environment_export_manifest.json"
KANKYO_ENVIRONMENT_RESOURCE_AUDIT = "kankyo_environment_export_resource_audit.json"


def export_kankyo_environment_assets(
    kankyo_root: Path,
    output_dir: Path,
    *,
    resource_prefix: str = "environments/oot3d/kankyo",
    include_textures: bool = True,
    sample_limit: int = 100,
) -> dict[str, object]:
    if not kankyo_root.is_dir():
        raise ParseError(f"{kankyo_root}: expected an extracted OOT3D kankyo directory")

    output_dir.mkdir(parents=True, exist_ok=True)
    resource_output = output_dir / "resources"
    normalized_prefix = normalize_resource_prefix(resource_prefix)

    source_audit = audit_kankyo_environment_assets(
        kankyo_root,
        None,
        sample_limit=sample_limit,
        include_records=False,
    )

    records: list[dict[str, object]] = []
    archive_records: list[dict[str, object]] = []
    resource_kind_counts: Counter[str] = Counter()
    environment_group_counts: Counter[str] = Counter()
    status_counts: Counter[str] = Counter()

    archive_count = 0
    considered = 0
    converted = 0
    skipped = 0
    failed = 0
    parse_failed = 0

    for archive_path in sorted(kankyo_root.glob("*.zar")):
        archive_count += 1
        archive_rel = archive_path.relative_to(kankyo_root).as_posix()
        archive_record = {
            "archive_path": archive_rel,
            "converted": 0,
            "skipped": 0,
            "failed": 0,
            "parse_failed": 0,
        }
        archive_records.append(archive_record)

        try:
            archive = ZarArchive.from_path(archive_path)
        except Exception as exc:
            parse_failed += 1
            status_counts["parse_failed"] += 1
            archive_record["parse_failed"] = int(archive_record["parse_failed"]) + 1
            records.append(
                {
                    "status": "parse_failed",
                    "archive_path": archive_rel,
                    "asset_id": unique_asset_id(archive_path.stem, records),
                    "source": str(archive_path),
                    "reason": str(exc),
                }
            )
            continue

        for file in archive.files:
            if file.type_name != "cmb":
                continue
            considered += 1
            embedded_id = sanitize_path_part(Path(file.name).with_suffix("").as_posix())
            asset_id = unique_asset_id(
                sanitize_path_part(f"{archive_path.stem}_{embedded_id}"),
                records,
            )
            resource_root = f"{normalized_prefix}/{asset_id}"
            symbol = f"gOot3dKankyo{to_pascal_case(asset_id)}"
            record_base: dict[str, object] = {
                "asset_id": asset_id,
                "archive_path": archive_rel,
                "embedded_name": file.name,
                "resource_root": resource_root,
                "symbol": symbol,
            }

            try:
                data = archive.read_file(file)
                model = CmbModel.parse(data, f"{archive_path}!{file.name}")
            except Exception as exc:
                parse_failed += 1
                status_counts["parse_failed"] += 1
                archive_record["parse_failed"] = int(archive_record["parse_failed"]) + 1
                records.append(
                    {
                        "status": "parse_failed",
                        "reason": str(exc),
                        **record_base,
                    }
                )
                continue

            group = environment_model_group(file.name)
            summary = model.summary()
            record_base.update(
                {
                    "source": model.source,
                    "model_name": model.name,
                    "environment_group": group,
                    "summary": summary,
                }
            )

            if not model.is_rigid_export_candidate():
                skipped += 1
                status_counts["skipped"] += 1
                archive_record["skipped"] = int(archive_record["skipped"]) + 1
                records.append(
                    {
                        "status": "skipped",
                        "reason": "not_rigid_export_candidate",
                        **record_base,
                    }
                )
                continue

            try:
                result = export_static_model(
                    model,
                    LegacyFastResourceOptions(
                        output_dir=resource_output / asset_id,
                        resource_root=resource_root,
                        symbol=symbol,
                        include_textures=include_textures,
                        allow_nonstatic=False,
                    ),
                )
            except Exception as exc:
                failed += 1
                status_counts["failed"] += 1
                archive_record["failed"] = int(archive_record["failed"]) + 1
                records.append(
                    {
                        "status": "failed",
                        "reason": str(exc),
                        **record_base,
                    }
                )
                continue

            converted += 1
            status_counts["converted"] += 1
            archive_record["converted"] = int(archive_record["converted"]) + 1
            environment_group_counts[group] += 1
            for resource in result.resources:
                resource_kind_counts[resource.kind] += 1
            records.append(
                {
                    "status": "converted",
                    "manifest": str(result.manifest_path),
                    "resource_count": len(result.resources),
                    "resources": [resource.__dict__ for resource in result.resources],
                    **record_base,
                }
            )

    manifest_path = output_dir / KANKYO_ENVIRONMENT_EXPORT_MANIFEST
    resource_audit_path = output_dir / KANKYO_ENVIRONMENT_RESOURCE_AUDIT
    manifest: dict[str, object] = {
        "format": "oot3d_kankyo_environment_export_manifest_v1",
        "kankyo_root": str(kankyo_root),
        "output": str(output_dir),
        "resource_output": str(resource_output),
        "resource_prefix": normalized_prefix,
        "include_textures": include_textures,
        "source_audit_summary": {
            "archive_count": source_audit["archive_count"],
            "archive_file_count_total": source_audit["archive_file_count_total"],
            "embedded_type_counts": source_audit["embedded_type_counts"],
            "cmb_counts": source_audit["cmb_counts"],
            "cmab_count": source_audit["cmab_count"],
            "ctxb_count": source_audit["ctxb_count"],
            "tbd_count": source_audit["tbd_count"],
        },
        "archive_count": archive_count,
        "considered": considered,
        "converted": converted,
        "skipped": skipped,
        "failed": failed,
        "parse_failed": parse_failed,
        "status_counts": sorted_counter(status_counts),
        "environment_model_group_counts": sorted_counter(environment_group_counts),
        "resource_kind_counts": sorted_counter(resource_kind_counts),
        "archive_records": archive_records,
        "records": records,
        "resource_audit": str(resource_audit_path),
        "fallback_policy": [
            "Generated kankyo resources are export-readiness artifacts only.",
            "Shipwright/N64 environment rendering remains fallback until explicit runtime routing is implemented.",
            "CMAB playback, CTXB binding, and TBD parameter semantics are not generated by this batch.",
        ],
    }
    write_json(manifest_path, manifest)

    resource_audit = audit_static_batch(manifest_path, resource_audit_path)
    manifest["resource_audit_summary"] = {
        "converted_record_count": resource_audit["converted_record_count"],
        "resource_count": resource_audit["resource_count"],
        "material_display_list_count": resource_audit["material_display_list_count"],
        "mesh_display_list_count": resource_audit["mesh_display_list_count"],
        "vertex_resource_count": resource_audit["vertex_resource_count"],
        "set_texture_image_count": resource_audit["set_texture_image_count"],
        "load_texture_block_count": resource_audit["load_texture_block_count"],
        "negative_st_count": resource_audit["negative_st_count"],
        "min_st": resource_audit["min_st"],
        "max_st": resource_audit["max_st"],
        "issue_counts": resource_audit["issue_counts"],
    }
    write_json(manifest_path, manifest)
    return manifest


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")


def normalize_resource_prefix(value: str) -> str:
    return value.replace("\\", "/").strip("/")


def sanitize_path_part(value: str) -> str:
    cleaned = []
    for char in value:
        if char.isalnum() or char in {"_", "-"}:
            cleaned.append(char)
        else:
            cleaned.append("_")
    result = "".join(cleaned).strip("_")
    return result or "unnamed"


def to_pascal_case(value: str) -> str:
    parts = [part for part in sanitize_path_part(value).split("_") if part]
    return "".join(part[:1].upper() + part[1:] for part in parts) or "Asset"


def unique_asset_id(asset_id: str, records: list[dict[str, object]]) -> str:
    existing = {str(record.get("asset_id")) for record in records if "asset_id" in record}
    if asset_id not in existing:
        return asset_id
    index = 2
    while f"{asset_id}_{index}" in existing:
        index += 1
    return f"{asset_id}_{index}"
