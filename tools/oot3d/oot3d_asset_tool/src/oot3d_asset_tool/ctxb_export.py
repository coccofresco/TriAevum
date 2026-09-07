from __future__ import annotations

import json
from collections import Counter
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from .binary import BinaryView, ParseError
from .ctxb import CtxbTexture, parse_ctxb
from .media_asset_audit import size_summary
from .romfs_inventory import sorted_counter
from .legacy_fast_resource import (
    FAST_RESOURCE_TEXTURE,
    FAST_TEXTURE_RGBA16,
    ConvertedResource,
    normalize_resource_root,
    write_texture_resource,
)
from .zar import ZarArchive

CTXB_TEXTURE_EXPORT_MANIFEST = "ctxb_texture_export_manifest.json"
TEXTURE_RESOURCE_HEADER_SIZE = 0x5C


@dataclass(frozen=True)
class CtxbPayloadSource:
    data: bytes
    source: str
    source_kind: str
    rel_path: str
    container_path: str | None = None
    embedded_name: str | None = None


def export_ctxb_textures(
    romfs_root: Path,
    output_dir: Path,
    *,
    resource_prefix: str = "textures/oot3d/ctxb",
    sample_limit: int = 100,
    include_records: bool = True,
) -> dict[str, object]:
    if not romfs_root.is_dir():
        raise ParseError(f"{romfs_root}: expected an extracted OOT3D RomFS directory")

    output_dir.mkdir(parents=True, exist_ok=True)
    resource_output_dir = output_dir / "resources"
    normalized_prefix = normalize_resource_root(resource_prefix)

    records: list[dict[str, object]] = []
    sample_records: list[dict[str, object]] = []
    resources: list[ConvertedResource] = []
    parse_errors: list[dict[str, object]] = []
    export_errors: list[dict[str, object]] = []
    zar_parse_errors: list[dict[str, object]] = []

    source_kind_counts: Counter[str] = Counter()
    top_level_counts: Counter[str] = Counter()
    format_pair_counts: Counter[str] = Counter()
    format_name_counts: Counter[str] = Counter()
    dimension_counts: Counter[str] = Counter()
    flags_counts: Counter[str] = Counter()
    status_counts: Counter[str] = Counter()
    resource_issue_counts: Counter[str] = Counter()
    generated_resource_sizes: list[int] = []
    decoded_rgba16_sizes: list[int] = []

    ctxb_count = 0
    parsed_count = 0
    exported_count = 0
    resource_audit_checked_count = 0
    resource_audit_issue_record_count = 0

    def add_sample(record: dict[str, object]) -> None:
        if len(sample_records) < sample_limit:
            sample_records.append(record)

    for source in iter_ctxb_payload_sources(romfs_root, zar_parse_errors):
        ctxb_count += 1
        source_kind_counts[source.source_kind] += 1
        parts = PurePosixPath(source.rel_path).parts
        top_level_counts[parts[0] if parts else "<root>"] += 1

        try:
            ctxb = parse_ctxb(
                source.data,
                source.source,
                name=PurePosixPath(source.rel_path.rsplit("!", 1)[-1]).stem,
            )
        except Exception as exc:
            error_record = source_error_record(source, "parse_error", str(exc))
            parse_errors.append(error_record)
            status_counts["parse_error"] += 1
            add_sample(error_record)
            if include_records:
                records.append(error_record)
            continue

        parsed_count += 1
        format_pair_counts[ctxb.format_pair] += 1
        format_name_counts[ctxb.format_name] += 1
        dimension_counts[f"{ctxb.width}x{ctxb.height}"] += 1
        flags_counts[f"0x{ctxb.flags:08x}"] += 1

        resource_path = ctxb_resource_path(normalized_prefix, source)
        resource_file = resource_output_dir / resource_path
        record: dict[str, object] = {
            "status": "pending",
            "source": source.source,
            "source_kind": source.source_kind,
            "path": source.rel_path,
            "container_path": source.container_path,
            "embedded_name": source.embedded_name,
            "resource_path": resource_path,
            "resource_file": str(resource_file),
            "width": ctxb.width,
            "height": ctxb.height,
            "payload_size": ctxb.payload_size,
            "format_pair": ctxb.format_pair,
            "format_name": ctxb.format_name,
            "data_type_name": ctxb.data_type_name,
            "flags": f"0x{ctxb.flags:08x}",
        }

        try:
            decoded_size = ctxb.width * ctxb.height * 2
            write_texture_resource(resource_file, ctxb.to_texture())
            resource_audit = audit_exported_texture_resource(resource_file, ctxb)
        except Exception as exc:
            record["status"] = "export_error"
            record["reason"] = str(exc)
            export_errors.append(record)
            status_counts["export_error"] += 1
            add_sample(record)
            if include_records:
                records.append(record)
            continue

        resource_audit_checked_count += 1
        if resource_audit["issue_count"]:
            resource_audit_issue_record_count += 1
            for issue in resource_audit["issues"]:
                resource_issue_counts[str(issue)] += 1
        else:
            resource_issue_counts["none"] += 1

        resource_size = int(resource_audit["file_size"])
        generated_resource_sizes.append(resource_size)
        decoded_rgba16_sizes.append(decoded_size)
        resources.append(ConvertedResource("Texture", resource_path, str(resource_file)))
        exported_count += 1
        status_counts["exported"] += 1
        record.update(
            {
                "status": "exported",
                "decoded_rgba16_size": decoded_size,
                "resource_size": resource_size,
                "resource_audit": resource_audit,
            }
        )
        add_sample(record)
        if include_records:
            records.append(record)

    resource_audit_summary = {
        "checked_resource_count": resource_audit_checked_count,
        "issue_record_count": resource_audit_issue_record_count,
        "issue_counts": sorted_counter(resource_issue_counts),
        "generated_resource_size_summary": size_summary(generated_resource_sizes),
    }
    manifest: dict[str, object] = {
        "format": "oot3d_ctxb_texture_export_manifest_v1",
        "romfs_root": str(romfs_root),
        "output_dir": str(output_dir),
        "resource_output_dir": str(resource_output_dir),
        "resource_prefix": normalized_prefix,
        "ctxb_count": ctxb_count,
        "parsed_count": parsed_count,
        "exported_count": exported_count,
        "parse_error_count": len(parse_errors),
        "export_error_count": len(export_errors),
        "zar_parse_error_count": len(zar_parse_errors),
        "resource_count": len(resources),
        "source_kind_counts": sorted_counter(source_kind_counts),
        "top_level_counts": sorted_counter(top_level_counts),
        "format_pair_counts": sorted_counter(format_pair_counts),
        "format_name_counts": sorted_counter(format_name_counts),
        "dimension_counts": sorted_counter(dimension_counts),
        "flags_counts": sorted_counter(flags_counts),
        "status_counts": sorted_counter(status_counts),
        "decoded_rgba16_size_summary": size_summary(decoded_rgba16_sizes),
        "resource_audit_summary": resource_audit_summary,
        "parse_errors": parse_errors,
        "export_errors": export_errors,
        "zar_parse_errors": zar_parse_errors,
        "sample_records": sample_records,
        "resources": [resource.__dict__ for resource in resources],
        "records": records if include_records else [],
        "notes": [
            "Generated Texture resources are derived from local OOT3D CTXB payloads and must stay outside git.",
            "This export does not route Shipwright runtime lookups to these textures by itself.",
        ],
    }

    manifest_path = output_dir / CTXB_TEXTURE_EXPORT_MANIFEST
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return manifest


def iter_ctxb_payload_sources(
    romfs_root: Path,
    zar_parse_errors: list[dict[str, object]],
) -> list[CtxbPayloadSource]:
    sources: list[CtxbPayloadSource] = []
    for path in sorted(romfs_root.rglob("*.ctxb")):
        rel_path = path.relative_to(romfs_root).as_posix()
        sources.append(
            CtxbPayloadSource(
                data=path.read_bytes(),
                source=str(path),
                source_kind="loose",
                rel_path=rel_path,
            )
        )

    for path in sorted(romfs_root.rglob("*.zar")):
        rel_path = path.relative_to(romfs_root).as_posix()
        try:
            archive = ZarArchive.from_path(path)
        except Exception as exc:
            zar_parse_errors.append({"path": rel_path, "reason": str(exc)})
            continue

        for file in archive.files:
            if file.type_name != "ctxb" and not file.name.lower().endswith(".ctxb"):
                continue
            sources.append(
                CtxbPayloadSource(
                    data=archive.read_file(file),
                    source=f"{path}!{file.name}",
                    source_kind="embedded_zar",
                    rel_path=f"{rel_path}!{file.name}",
                    container_path=rel_path,
                    embedded_name=file.name,
                )
            )
    return sources


def audit_exported_texture_resource(path: Path, ctxb: CtxbTexture) -> dict[str, object]:
    issues: list[str] = []
    if not path.exists():
        return {
            "file_size": 0,
            "issues": ["missing_resource_file"],
            "issue_count": 1,
        }
    data = path.read_bytes()
    file_size = len(data)
    if file_size < TEXTURE_RESOURCE_HEADER_SIZE:
        return {
            "file_size": file_size,
            "issues": ["truncated_texture_resource"],
            "issue_count": 1,
        }

    view = BinaryView(data, str(path))
    if view.u32(0x04) != FAST_RESOURCE_TEXTURE:
        issues.append("invalid_fast_resource_magic")
    if view.u32(0x40) != FAST_TEXTURE_RGBA16:
        issues.append("invalid_fast_texture_type")
    if view.u32(0x44) != ctxb.width:
        issues.append("width_mismatch")
    if view.u32(0x48) != ctxb.height:
        issues.append("height_mismatch")

    declared_image_size = view.u32(0x58)
    actual_image_size = file_size - TEXTURE_RESOURCE_HEADER_SIZE
    expected_image_size = ctxb.width * ctxb.height * 2
    if declared_image_size != actual_image_size:
        issues.append("declared_image_size_mismatch")
    if actual_image_size != expected_image_size:
        issues.append("image_size_mismatch")

    return {
        "file_size": file_size,
        "fast_resource_magic": f"0x{view.u32(0x04):08x}",
        "texture_type": f"0x{view.u32(0x40):08x}",
        "width": view.u32(0x44),
        "height": view.u32(0x48),
        "declared_image_size": declared_image_size,
        "actual_image_size": actual_image_size,
        "expected_image_size": expected_image_size,
        "issues": issues,
        "issue_count": len(issues),
    }


def source_error_record(
    source: CtxbPayloadSource,
    status: str,
    reason: str,
) -> dict[str, object]:
    return {
        "status": status,
        "source": source.source,
        "source_kind": source.source_kind,
        "path": source.rel_path,
        "container_path": source.container_path,
        "embedded_name": source.embedded_name,
        "reason": reason,
    }


def ctxb_resource_path(resource_prefix: str, source: CtxbPayloadSource) -> str:
    if source.source_kind == "loose":
        suffix_parts = ["loose", *sanitized_texture_path_parts(source.rel_path)]
    else:
        container_parts = sanitized_container_path_parts(source.container_path or "unknown.zar")
        embedded_parts = sanitized_texture_path_parts(source.embedded_name or source.rel_path)
        suffix_parts = ["embedded_zar", *container_parts, *embedded_parts]
    suffix = "/".join(part for part in suffix_parts if part)
    return f"{resource_prefix}/{suffix}" if resource_prefix else suffix


def sanitized_texture_path_parts(path: str) -> list[str]:
    pure = PurePosixPath(path.replace("\\", "/"))
    parent_parts = [sanitize_resource_part(part) for part in pure.parent.parts if part != "."]
    return [*parent_parts, f"{sanitize_resource_part(pure.stem)}.rgba16"]


def sanitized_container_path_parts(path: str) -> list[str]:
    pure = PurePosixPath(path.replace("\\", "/"))
    parent_parts = [sanitize_resource_part(part) for part in pure.parent.parts if part != "."]
    return [*parent_parts, sanitize_resource_part(pure.stem)]


def sanitize_resource_part(value: str) -> str:
    cleaned = []
    for char in value:
        if char.isalnum() or char in {"_", "-"}:
            cleaned.append(char)
        else:
            cleaned.append("_")
    result = "".join(cleaned).strip("_")
    return result or "unnamed"
