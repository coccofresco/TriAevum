from __future__ import annotations

import argparse
import hashlib
import json
import re
import zipfile
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any

from .actor_qdb_audit import is_qdb_file
from .binary import BinaryView, ParseError
from .zar import ZarArchive
from .zsi_cutscene_audit import parse_oot3d_native_cutscene_block


FORMAT = "oot3d_qdb_catalog_v1"
CATALOG_ARCHIVE_PATH = "oot3d/catalog/oot3d_qdb_catalog.json"


def _safe_component(value: str) -> str:
    component = re.sub(r"[^A-Za-z0-9._-]+", "_", value).strip("._")
    return component or "unnamed"


def _resource_path(domain: str, archive_relative: str, embedded_index: int, member: str) -> str:
    archive_path = PurePosixPath(archive_relative)
    archive_parts = [_safe_component(part) for part in archive_path.parts]
    member_stem = _safe_component(Path(member.replace("\\", "/")).stem)
    return str(PurePosixPath("oot3d/native/qdb", _safe_component(domain), *archive_parts,
                             f"{embedded_index:04d}_{member_stem}.qdb"))


def _native_asset_id(domain: str, archive_relative: str, member: str) -> str:
    container = str(PurePosixPath(domain, archive_relative))
    return f"qdb:{container}!{member}"


def _trailer_status(payload: bytes, decoded_size: int) -> tuple[str, int]:
    trailer = payload[decoded_size:]
    if len(trailer) < 4 or trailer[:4] != b"\xff\xff\xff\xff":
        return "missing_native_terminator", len(trailer)
    if any(trailer[4:]):
        return "nonzero_alignment_bytes", len(trailer)
    return "native_terminator_and_alignment", len(trailer)


def _command_record(command: dict[str, object]) -> dict[str, object]:
    return {
        "index": int(command["index"]),
        "offset": int(command["offset"]),
        "command_id": int(command["command_id"]),
        "command_id_hex": str(command["command_id_hex"]),
        "name": str(command["name"]),
        "category": str(command["category"]),
        "entry_count": command.get("entry_count"),
        "camera_point_count": int(command.get("camera_point_count", 0)),
        "blob_size": command.get("blob_size"),
        "packed_stride": int(command.get("packed_stride", 0)),
        "payload_size": int(command["payload_size"]),
        "total_size": int(command["total_size"]),
    }


def build_qdb_catalog(
    roots: dict[str, Path], *, now: datetime | None = None
) -> dict[str, Any]:
    if not roots:
        raise ValueError("QDB catalog requires at least one DOMAIN=PATH root")
    now = now or datetime.now(timezone.utc)
    records: list[dict[str, Any]] = []
    issues: list[dict[str, Any]] = []
    archive_errors: list[dict[str, str]] = []
    archive_count = 0

    for domain, root in sorted(roots.items()):
        if not domain or not root.is_dir():
            raise ValueError(f"{domain or '<empty>'}={root}: expected an extracted OOT3D directory")
        for archive_path in sorted(root.rglob("*.zar")):
            archive_relative = archive_path.relative_to(root).as_posix()
            try:
                archive = ZarArchive.from_path(archive_path)
            except ParseError as exc:
                archive_errors.append({
                    "domain": domain,
                    "archive": archive_relative,
                    "error": str(exc),
                })
                continue
            archive_count += 1
            for file in archive.files:
                if not is_qdb_file(file.name, file.type_name):
                    continue
                payload = archive.read_file(file)
                asset_id = _native_asset_id(domain, archive_relative, file.name)
                try:
                    decoded = parse_oot3d_native_cutscene_block(BinaryView(payload, asset_id), 0)
                    trailer_status, trailer_size = _trailer_status(payload, int(decoded["decoded_size"]))
                    if trailer_status != "native_terminator_and_alignment":
                        issues.append({"asset_id": asset_id, "reason": trailer_status})
                    commands = [_command_record(command) for command in decoded["commands"]]
                    status = "decoded_native_qdb"
                    error = ""
                except (ParseError, KeyError, TypeError, ValueError) as exc:
                    decoded = {}
                    commands = []
                    trailer_status = "not_checked"
                    trailer_size = 0
                    status = "decode_error"
                    error = str(exc)
                    issues.append({"asset_id": asset_id, "reason": "decode_error", "error": error})
                records.append({
                    "asset_id": asset_id,
                    "source_domain": domain,
                    "source_archive": str(PurePosixPath(domain, archive_relative)),
                    "source_archive_relative": archive_relative,
                    "source_archive_path": str(archive_path),
                    "source_member": file.name,
                    "embedded_index": file.index,
                    "embedded_type": file.type_name,
                    "canonical_resource": _resource_path(domain, archive_relative, file.index, file.name),
                    "byte_length": len(payload),
                    "sha256": hashlib.sha256(payload).hexdigest(),
                    "status": status,
                    "decode_error": error,
                    "header": {
                        "magic": decoded.get("magic_hex"),
                        "version_or_flags": decoded.get("version_or_flags_hex"),
                        "command_count": decoded.get("command_count"),
                        "end_frame": decoded.get("end_frame"),
                        "command_stream_offset": decoded.get("command_stream_offset"),
                        "decoded_size": decoded.get("decoded_size"),
                        "trailer_size": trailer_size,
                        "trailer_status": trailer_status,
                    },
                    "commands": commands,
                })

    records.sort(key=lambda record: str(record["asset_id"]))
    ids = [str(record["asset_id"]) for record in records]
    resources = [str(record["canonical_resource"]) for record in records]
    duplicate_asset_ids = sorted(key for key, count in Counter(ids).items() if count > 1)
    duplicate_resources = sorted(key for key, count in Counter(resources).items() if count > 1)
    if archive_errors:
        status = "invalid_archive_errors"
    elif issues:
        status = "invalid_qdb_payloads"
    elif duplicate_asset_ids or duplicate_resources:
        status = "invalid_duplicate_identities"
    else:
        status = "complete"
    category_counts = Counter(
        str(command["category"])
        for record in records
        for command in record["commands"]
    )
    return {
        "format": FORMAT,
        "status": status,
        "generated_utc": now.isoformat(),
        "source_roots": {domain: str(path) for domain, path in sorted(roots.items())},
        "summary": {
            "parsed_archive_count": archive_count,
            "qdb_count": len(records),
            "decoded_qdb_count": sum(record["status"] == "decoded_native_qdb" for record in records),
            "command_count": sum(len(record["commands"]) for record in records),
            "command_category_counts": dict(sorted(category_counts.items())),
            "byte_length": sum(int(record["byte_length"]) for record in records),
            "issue_count": len(issues),
            "archive_error_count": len(archive_errors),
        },
        "archive_errors": archive_errors,
        "issues": issues,
        "duplicate_asset_ids": duplicate_asset_ids,
        "duplicate_resources": duplicate_resources,
        "records": records,
        "notes": [
            "QDB payloads are preserved byte-for-byte from native OOT3D ZAR members.",
            "Decoded command spans are indices over the native stream, not converted N64 timelines.",
            "The emulator is not an input to this catalog.",
        ],
    }


def _zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    return info


def write_qdb_archive(path: Path, catalog: dict[str, Any]) -> None:
    if catalog.get("format") != FORMAT or catalog.get("status") != "complete":
        raise ValueError("QDB archive requires a complete oot3d_qdb_catalog_v1")
    manifest = {
        "name": "OOT3D Native QDB Timelines",
        "author": "local",
        "version": "0.1.0",
        "description": "Native OOT3D QDB payloads and source identity catalog.",
        "license": "Generated from user-provided local assets; do not redistribute without rights.",
        "code_version": 1,
    }
    archive_cache: dict[str, ZarArchive] = {}
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        output.writestr(_zip_info("manifest.json"), json.dumps(manifest, indent=2) + "\n")
        output.writestr(_zip_info(CATALOG_ARCHIVE_PATH), json.dumps(catalog, indent=2) + "\n")
        for record in catalog["records"]:
            source_path = str(record["source_archive_path"])
            archive = archive_cache.get(source_path)
            if archive is None:
                archive = ZarArchive.from_path(Path(source_path))
                archive_cache[source_path] = archive
            index = int(record["embedded_index"])
            if index < 0 or index >= len(archive.files):
                raise ValueError(f"{record['asset_id']}: embedded index no longer exists")
            file = archive.files[index]
            if file.name != record["source_member"]:
                raise ValueError(f"{record['asset_id']}: embedded member identity changed")
            payload = archive.read_file(file)
            if hashlib.sha256(payload).hexdigest() != record["sha256"]:
                raise ValueError(f"{record['asset_id']}: source payload changed after catalog generation")
            output.writestr(_zip_info(str(record["canonical_resource"])), payload)


def render_qdb_catalog_markdown(catalog: dict[str, Any]) -> str:
    summary = catalog.get("summary", {})
    lines = [
        "# OOT3D Native QDB Catalog",
        "",
        f"- Status: `{catalog.get('status', 'unknown')}`",
        f"- Native QDB payloads: `{summary.get('qdb_count', 0)}`",
        f"- Decoded payloads: `{summary.get('decoded_qdb_count', 0)}`",
        f"- Native commands: `{summary.get('command_count', 0)}`",
        f"- Source bytes: `{summary.get('byte_length', 0)}`",
        "",
        "## Command Categories",
        "",
        "| Category | Commands |",
        "| --- | ---: |",
    ]
    for category, count in summary.get("command_category_counts", {}).items():
        lines.append(f"| `{category}` | {count} |")
    lines.extend([
        "",
        "## Runtime Policy",
        "",
        "- OOT3D QDB bytes, frame spans and commands remain authoritative.",
        "- Ship/N64 may request a semantic cutscene but cannot replace its native timeline.",
        "- HUD, menus and their connected logic remain on the N64/Ship path for now.",
        "",
    ])
    return "\n".join(lines)


def _parse_root(value: str) -> tuple[str, Path]:
    domain, separator, raw_path = value.partition("=")
    if not separator or not domain.strip() or not raw_path.strip():
        raise argparse.ArgumentTypeError("--root must use DOMAIN=PATH")
    return domain.strip(), Path(raw_path.strip())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Catalog and package native OOT3D QDB timelines.")
    parser.add_argument("--root", action="append", type=_parse_root, required=True)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-md", type=Path, required=True)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args(argv)
    roots = dict(args.root)
    if len(roots) != len(args.root):
        parser.error("duplicate QDB source domain")
    result = build_qdb_catalog(roots)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_md.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8", newline="\n")
    args.output_md.write_text(render_qdb_catalog_markdown(result), encoding="utf-8", newline="\n")
    if args.archive is not None and result["status"] == "complete":
        write_qdb_archive(args.archive, result)
    print(args.output_json)
    print(args.output_md)
    if args.archive is not None and args.archive.is_file():
        print(args.archive)
    return 1 if args.verify and result["status"] != "complete" else 0


if __name__ == "__main__":
    raise SystemExit(main())
