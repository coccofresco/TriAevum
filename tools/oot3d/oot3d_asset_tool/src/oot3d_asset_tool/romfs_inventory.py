from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

from .binary import ParseError
from .cmb import CmbModel
from .zar import ZarArchive
from .zsi import ZsiFile


def inventory_romfs(
    romfs_root: Path,
    output_path: Path | None = None,
    *,
    deep: bool = True,
    include_records: bool = True,
) -> dict[str, object]:
    if not romfs_root.is_dir():
        raise ParseError(f"{romfs_root}: expected an extracted OOT3D RomFS directory")

    files = sorted(path for path in romfs_root.rglob("*") if path.is_file())
    top_level_counts: Counter[str] = Counter()
    extension_counts: Counter[str] = Counter()
    file_kind_counts: Counter[str] = Counter()
    total_bytes = 0
    records: list[dict[str, object]] = []
    model_records: list[dict[str, object]] = []
    parse_errors: list[dict[str, object]] = []
    model_counts: Counter[str] = Counter()

    for path in files:
        rel = path.relative_to(romfs_root)
        rel_text = rel.as_posix()
        suffix = normalized_suffix(path)
        size = path.stat().st_size
        total_bytes += size
        top_level_counts[top_level_key(rel)] += 1
        extension_counts[suffix] += 1

        record = {
            "path": rel_text,
            "top_level": top_level_key(rel),
            "extension": suffix,
            "size": size,
            "kind": file_kind(path),
            "support_status": support_status_for_file(path),
        }
        file_kind_counts[str(record["kind"])] += 1

        if deep and suffix == ".cmb":
            scan_cmb_file(path, romfs_root, record, model_records, parse_errors, model_counts)
        elif deep and suffix == ".zar":
            scan_zar_file(path, romfs_root, record, model_records, parse_errors, model_counts)
        elif deep and suffix == ".zsi":
            scan_zsi_file(path, romfs_root, record, model_records, parse_errors, model_counts)

        if include_records:
            records.append(record)

    parsed_models = model_counts["static_candidate"] + model_counts["nonstatic_candidate"]
    model_counts["parsed"] = parsed_models
    model_counts["parse_errors"] = len(
        [error for error in parse_errors if error.get("kind") == "cmb_model"]
    )

    inventory = {
        "schema_version": 1,
        "romfs_root": str(romfs_root),
        "deep": deep,
        "file_count": len(files),
        "total_bytes": total_bytes,
        "top_level_file_counts": sorted_counter(top_level_counts),
        "extension_counts": sorted_counter(extension_counts),
        "file_kind_counts": sorted_counter(file_kind_counts),
        "container_counts": {
            "cmb_files": extension_counts[".cmb"],
            "zar_archives": extension_counts[".zar"],
            "zsi_files": extension_counts[".zsi"],
            "ctxb_files": extension_counts[".ctxb"],
            "moflex_files": extension_counts[".moflex"],
            "bcstm_files": extension_counts[".bcstm"],
            "bcsar_files": extension_counts[".bcsar"],
        },
        "model_counts": {
            "discovered": model_counts["discovered"],
            "parsed": model_counts["parsed"],
            "static_candidate": model_counts["static_candidate"],
            "nonstatic_candidate": model_counts["nonstatic_candidate"],
            "rigid_export_candidate": model_counts["rigid_export_candidate"],
            "rigid_multibone_candidate": model_counts["rigid_multibone_candidate"],
            "parse_errors": model_counts["parse_errors"],
        },
        "conversion_readiness": conversion_readiness(extension_counts, model_counts),
        "parse_error_count": len(parse_errors),
        "parse_errors": parse_errors,
        "model_records": model_records if include_records else [],
        "records": records if include_records else [],
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8", newline="\n")
    return inventory


def scan_cmb_file(
    path: Path,
    romfs_root: Path,
    record: dict[str, object],
    model_records: list[dict[str, object]],
    parse_errors: list[dict[str, object]],
    model_counts: Counter[str],
) -> None:
    model_counts["discovered"] += 1
    try:
        model = CmbModel.from_path(path)
    except Exception as exc:
        add_parse_error(parse_errors, path, romfs_root, "cmb_model", str(exc))
        record["support_status"] = "parse_error"
        return

    record["embedded_cmb_count"] = 1
    add_model_record(model_records, model_counts, path, romfs_root, "cmb", model)


def scan_zar_file(
    path: Path,
    romfs_root: Path,
    record: dict[str, object],
    model_records: list[dict[str, object]],
    parse_errors: list[dict[str, object]],
    model_counts: Counter[str],
) -> None:
    try:
        archive = ZarArchive.from_path(path)
    except Exception as exc:
        add_parse_error(parse_errors, path, romfs_root, "zar_archive", str(exc))
        record["support_status"] = "parse_error"
        return

    type_counts: Counter[str] = Counter(file.type_name for file in archive.files)
    cmb_files = archive.cmb_files()
    record["embedded_file_count"] = len(archive.files)
    record["embedded_type_counts"] = sorted_counter(type_counts)
    record["embedded_cmb_count"] = len(cmb_files)

    for file in cmb_files:
        model_counts["discovered"] += 1
        try:
            model = CmbModel.parse(archive.read_file(file), f"{path}!{file.name}")
        except Exception as exc:
            add_parse_error(
                parse_errors,
                path,
                romfs_root,
                "cmb_model",
                str(exc),
                embedded_name=file.name,
            )
            continue
        add_model_record(model_records, model_counts, path, romfs_root, "zar", model, embedded_name=file.name)


def scan_zsi_file(
    path: Path,
    romfs_root: Path,
    record: dict[str, object],
    model_records: list[dict[str, object]],
    parse_errors: list[dict[str, object]],
    model_counts: Counter[str],
) -> None:
    try:
        zsi = ZsiFile.from_path(path)
        cmbs = zsi.embedded_cmbs()
        scene_setups = zsi.scene_setups()
    except Exception as exc:
        add_parse_error(parse_errors, path, romfs_root, "zsi_file", str(exc))
        record["support_status"] = "parse_error"
        return

    record["zsi_role"] = zsi_role(path)
    record["embedded_cmb_count"] = len(cmbs)
    record["scene_setup_count"] = len(scene_setups)
    for cmb in cmbs:
        model_counts["discovered"] += 1
        add_model_record(
            model_records,
            model_counts,
            path,
            romfs_root,
            "zsi",
            cmb.model,
            embedded_index=cmb.index,
            embedded_offset=cmb.offset,
            embedded_size=cmb.size,
        )


def add_model_record(
    model_records: list[dict[str, object]],
    model_counts: Counter[str],
    path: Path,
    romfs_root: Path,
    container_type: str,
    model: CmbModel,
    *,
    embedded_name: str | None = None,
    embedded_index: int | None = None,
    embedded_offset: int | None = None,
    embedded_size: int | None = None,
) -> None:
    static_candidate = model.is_static_candidate()
    rigid_export_candidate = model.is_rigid_export_candidate()
    model_counts["static_candidate" if static_candidate else "nonstatic_candidate"] += 1
    if rigid_export_candidate:
        model_counts["rigid_export_candidate"] += 1
    if rigid_export_candidate and not static_candidate:
        model_counts["rigid_multibone_candidate"] += 1
    summary = compact_model_summary(model)
    model_records.append(
        {
            "container_path": path.relative_to(romfs_root).as_posix(),
            "container_type": container_type,
            "embedded_name": embedded_name,
            "embedded_index": embedded_index,
            "embedded_offset": embedded_offset,
            "embedded_size": embedded_size,
            "model_name": model.name,
            "static_candidate": static_candidate,
            "rigid_export_candidate": rigid_export_candidate,
            "support_status": (
                "static_cmb_export_supported"
                if static_candidate
                else "rigid_multibone_export_supported"
                if rigid_export_candidate
                else "needs_skeleton_skinning_or_animation_support"
            ),
            **summary,
        }
    )


def compact_model_summary(model: CmbModel) -> dict[str, object]:
    primitive_count = 0
    triangle_count = 0
    vertex_count = 0
    for shape in model.shapes:
        primitive_count += len(shape.primitives)
        triangle_count += sum(len(primitive.indices) // 3 for primitive in shape.primitives)
        vertex_count += len(shape.positions)

    texture_formats: Counter[str] = Counter(
        f"0x{texture.texture_format:x}:0x{texture.data_type:x}" for texture in model.textures
    )
    return {
        "bone_count": model.bone_count,
        "texture_count": len(model.textures),
        "texture_format_counts": sorted_counter(texture_formats),
        "material_count": len(model.materials),
        "mesh_count": len(model.meshes),
        "shape_count": len(model.shapes),
        "primitive_count": primitive_count,
        "triangle_count": triangle_count,
        "vertex_count": vertex_count,
    }


def conversion_readiness(
    extension_counts: Counter[str],
    model_counts: Counter[str],
) -> list[dict[str, object]]:
    return [
        {
            "category": "static_cmb_models",
            "count": model_counts["static_candidate"],
            "status": "supported_by_current_rigid_exporter",
            "next_milestone": "batch static object and scene coverage",
        },
        {
            "category": "rigid_multibone_cmb_models",
            "count": model_counts["rigid_multibone_candidate"],
            "status": "supported_by_current_rigid_exporter",
            "next_milestone": "broader actor/object batch coverage",
        },
        {
            "category": "skinned_or_animated_cmb_models",
            "count": model_counts["nonstatic_candidate"] - model_counts["rigid_multibone_candidate"],
            "status": "not_yet_supported",
            "next_milestone": "skeleton, skinning, and animation conversion",
        },
        {
            "category": "ctxb_external_textures",
            "count": extension_counts[".ctxb"],
            "status": "not_yet_supported",
            "next_milestone": "CTXB parser and texture binding audit",
        },
        {
            "category": "moflex_movies",
            "count": extension_counts[".moflex"],
            "status": "not_yet_supported",
            "next_milestone": "movie/cutscene compatibility path",
        },
        {
            "category": "streamed_audio",
            "count": extension_counts[".bcstm"],
            "status": "not_yet_supported",
            "next_milestone": "audio handoff or fallback policy",
        },
    ]


def add_parse_error(
    parse_errors: list[dict[str, object]],
    path: Path,
    romfs_root: Path,
    kind: str,
    error: str,
    *,
    embedded_name: str | None = None,
) -> None:
    parse_errors.append(
        {
            "path": path.relative_to(romfs_root).as_posix(),
            "kind": kind,
            "embedded_name": embedded_name,
            "error": error,
        }
    )


def file_kind(path: Path) -> str:
    suffix = normalized_suffix(path)
    if suffix == ".cmb":
        return "standalone_cmb_model"
    if suffix == ".zar":
        return "zar_archive"
    if suffix == ".zsi":
        return "zsi_scene_or_room"
    if suffix == ".ctxb":
        return "ctxb_external_texture"
    if suffix == ".moflex":
        return "moflex_movie"
    if suffix in {".bcstm", ".bcsar"}:
        return "audio"
    if suffix == ".shbin":
        return "shader_binary"
    if suffix.startswith(".q"):
        return "q_archive_or_metadata"
    return "other"


def support_status_for_file(path: Path) -> str:
    suffix = normalized_suffix(path)
    if suffix in {".cmb", ".zar", ".zsi"}:
        return "container_supported_for_inventory"
    if suffix == ".ctxb":
        return "needs_ctxb_parser"
    if suffix == ".moflex":
        return "later_movie_cutscene_milestone"
    if suffix in {".bcstm", ".bcsar"}:
        return "audio_fallback_or_future_audio_milestone"
    if suffix == ".shbin":
        return "shader_binary_not_current_asset_port_target"
    if suffix.startswith(".q"):
        return "unknown_q_format_needs_inventory"
    return "not_current_asset_port_target"


def zsi_role(path: Path) -> str:
    name = path.name.lower()
    if re.match(r"^.+_\d+_info\.zsi$", name):
        return "room"
    if re.match(r"^.+_info\.zsi$", name):
        return "scene"
    return "other"


def top_level_key(relative_path: Path) -> str:
    return relative_path.parts[0] if len(relative_path.parts) > 1 else "<root>"


def normalized_suffix(path: Path) -> str:
    return path.suffix.lower() or "<none>"


def sorted_counter(counter: Counter[str]) -> dict[str, int]:
    return {key: counter[key] for key in sorted(counter)}
