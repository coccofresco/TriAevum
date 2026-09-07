from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from .binary import ParseError
from .cmb import CmbModel, Material, MaterialLightingBlock
from .zar import ZarArchive


LINK_DIRECT_ARCHIVE_STEMS = frozenset(
    {
        "zelda_link_boy_new",
        "zelda_link_boy_ultra",
        "zelda_link_child_new",
        "zelda_link_child_ultra",
        "zelda_link_opening",
    }
)


def audit_actor_material_lighting(
    actor_root: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
    include_records: bool = True,
) -> dict[str, object]:
    records: list[dict[str, object]] = []
    samples: dict[str, list[dict[str, object]]] = {
        "incomplete_materials": [],
        "bump_active_materials": [],
        "flag3_active_materials": [],
        "parse_errors": [],
    }
    scope_counts = {
        "all_actor_materials": new_scope_counts(),
        "link_direct_materials": new_scope_counts(),
    }

    archive_count = 0
    archive_with_cmb_count = 0
    model_count = 0
    material_count = 0
    parse_error_count = 0
    issue_counts: Counter[str] = Counter()

    for archive_path in iter_actor_archives(actor_root):
        archive_count += 1
        archive_scope = archive_scope_name(archive_path)
        try:
            archive = ZarArchive.from_path(archive_path)
        except (OSError, ParseError) as exc:
            parse_error_count += 1
            append_sample(
                samples["parse_errors"],
                {"archive": display_path(archive_path, actor_root), "reason": str(exc)},
                sample_limit,
            )
            continue

        cmb_files = archive.cmb_files()
        if cmb_files:
            archive_with_cmb_count += 1
        for cmb_file in cmb_files:
            source = f"{display_path(archive_path, actor_root)}!{cmb_file.name}"
            try:
                model = CmbModel.parse(archive.read_file(cmb_file), source)
            except (OSError, ParseError) as exc:
                parse_error_count += 1
                append_sample(
                    samples["parse_errors"],
                    {"archive": display_path(archive_path, actor_root), "cmb": cmb_file.name, "reason": str(exc)},
                    sample_limit,
                )
                continue

            model_count += 1
            for material in model.materials:
                material_count += 1
                issues = material_lighting_issues(model, material)
                issue_counts.update(issues)
                record = material_record(
                    actor_root,
                    archive_path,
                    cmb_file.name,
                    model,
                    material,
                    archive_scope,
                    issues,
                )
                accumulate_scope_counts(scope_counts["all_actor_materials"], record)
                if archive_scope == "link_direct":
                    accumulate_scope_counts(scope_counts["link_direct_materials"], record)
                if include_records:
                    records.append(record)
                if issues:
                    append_sample(samples["incomplete_materials"], compact_sample(record), sample_limit)
                if record["pica_bump_mode_active"]:
                    append_sample(samples["bump_active_materials"], compact_sample(record), sample_limit)
                if record["flag3_active"]:
                    append_sample(samples["flag3_active_materials"], compact_sample(record), sample_limit)

    public_scope_counts = {
        scope: public_counts(counts) for scope, counts in scope_counts.items()
    }

    audit = {
        "format": "oot3d_actor_material_lighting_audit_v1",
        "source": "oot3d_actor_zar_embedded_cmb_material_0xcc_blocks",
        "actor_root": str(actor_root),
        "scope_definitions": {
            "all_actor_materials": "all CMB materials embedded in actor/*.zar",
            "link_direct_materials": sorted(LINK_DIRECT_ARCHIVE_STEMS),
        },
        "archive_count": archive_count,
        "archive_with_cmb_count": archive_with_cmb_count,
        "model_count": model_count,
        "material_count": material_count,
        "parse_error_count": parse_error_count,
        "scope_counts": public_scope_counts,
        "issue_counts": dict(sorted(issue_counts.items())),
        "samples": samples,
        "records": records if include_records else [],
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8", newline="\n")
    return audit


def iter_actor_archives(actor_root: Path) -> list[Path]:
    if actor_root.is_file():
        return [actor_root]
    return sorted(actor_root.glob("*.zar"))


def archive_scope_name(archive_path: Path) -> str:
    return "link_direct" if archive_path.stem.lower() in LINK_DIRECT_ARCHIVE_STEMS else "actor"


def material_lighting_issues(model: CmbModel, material: Material) -> list[str]:
    block = material.lighting_block
    if block is None:
        return ["cmb_material_lighting_block_not_decoded"]

    issues: list[str] = []
    if not block.pica_bump_texture_unit_recognized:
        issues.append("pica_bump_texture_unit_unrecognized")
    if not block.pica_bump_mode_recognized:
        issues.append("pica_bump_mode_unrecognized")
    if not block.pica_lighting_config_recognized:
        issues.append("pica_lighting_config_unrecognized")
    if not block.pica_lut_input_abs_d0_selector_recognized:
        issues.append("pica_lut_input_abs_d0_unrecognized")
    if not block.pica_lut_input_recognized:
        issues.append("pica_lut_input_rb_unrecognized")
    if not block.pica_lut_scale_recognized:
        issues.append("pica_lut_scale_rb_unrecognized")
    if block.pica_bump_mode_recognized and block.pica_bump_mode == 1:
        if not material_bump_texture_resolved(model, material, block):
            issues.append("pica_bump_normal_map_texture_unit_unresolved")
    elif block.pica_bump_mode_recognized and block.pica_bump_mode != 0:
        issues.append("pica_bump_mode_backend_pending")
    return issues


def material_bump_texture_resolved(
    model: CmbModel,
    material: Material,
    block: MaterialLightingBlock,
) -> bool:
    if not block.pica_bump_texture_unit_recognized:
        return False
    unit = block.pica_bump_texture_unit
    if unit >= len(material.texture_indices):
        return False
    texture_index = material.texture_indices[unit]
    return 0 <= texture_index < len(model.textures)


def material_record(
    actor_root: Path,
    archive_path: Path,
    cmb_name: str,
    model: CmbModel,
    material: Material,
    archive_scope: str,
    issues: list[str],
) -> dict[str, object]:
    block = material.lighting_block
    bump_normal_map_supported = bool(
        block
        and block.pica_bump_mode_recognized
        and block.pica_bump_mode == 1
        and material_bump_texture_resolved(model, material, block)
    )
    bump_backend_pending = bool(
        block
        and block.pica_bump_mode_recognized
        and block.pica_bump_mode != 0
        and not bump_normal_map_supported
    )
    texture_names = [
        model.textures[index].name if 0 <= index < len(model.textures) else None
        for index in material.texture_indices
    ]
    return {
        "archive": display_path(archive_path, actor_root),
        "archive_scope": archive_scope,
        "cmb": cmb_name,
        "model_name": model.name,
        "material_index": material.index,
        "texture_indices": list(material.texture_indices),
        "texture_names": texture_names,
        "fragment_lighting_enabled": material.fragment_lighting_enabled,
        "vertex_lighting_enabled": material.vertex_lighting_enabled,
        "hemisphere_lighting_enabled": material.hemisphere_lighting_enabled,
        "hemisphere_occlusion_enabled": material.hemisphere_occlusion_enabled,
        "lighting_block_decoded": block is not None,
        "pica_bump_texture_unit_raw": hex_field(block.pica_bump_texture_unit_raw if block else None),
        "pica_bump_texture_unit": block.pica_bump_texture_unit if block else None,
        "pica_bump_texture_unit_recognized": bool(block and block.pica_bump_texture_unit_recognized),
        "pica_bump_mode_raw": hex_field(block.pica_bump_mode_raw if block else None),
        "pica_bump_mode": block.pica_bump_mode if block else None,
        "pica_bump_mode_recognized": bool(block and block.pica_bump_mode_recognized),
        "pica_bump_mode_active": bool(block and block.pica_bump_mode_recognized and block.pica_bump_mode != 0),
        "pica_bump_mode_backend_pending": bump_backend_pending,
        "pica_bump_normal_map_backend_supported": bump_normal_map_supported,
        "pica_lighting_config_raw": hex_field(block.pica_lighting_config_raw if block else None),
        "pica_lighting_config": block.pica_lighting_config if block else None,
        "pica_lighting_config_recognized": bool(block and block.pica_lighting_config_recognized),
        "pica_lut_input_abs_d0_recognized": bool(block and block.pica_lut_input_abs_d0_selector_recognized),
        "pica_lut_input_rb_recognized": bool(block and block.pica_lut_input_recognized),
        "pica_lut_scale_rb_recognized": bool(block and block.pica_lut_scale_recognized),
        "flag3_raw": hex_field(block.flag3_raw if block else None),
        "flag3_active": bool(block and block.flag3),
        "flag3_material_lighting_backend_pending": False,
        "flag3_status": (
            "copied_to_runtime_lane_but_no_material_lighting_consumer_proven"
            if block and block.flag3
            else "disabled"
        ),
        "material_lighting_complete": not issues,
        "material_lighting_incomplete_reasons": issues,
    }


def new_scope_counts() -> dict[str, int]:
    return {
        "model_count": 0,
        "material_count": 0,
        "lighting_block_decoded_material_count": 0,
        "material_lighting_complete_count": 0,
        "material_lighting_incomplete_count": 0,
        "pica_bump_mode_available_count": 0,
        "pica_bump_mode_recognized_count": 0,
        "pica_bump_mode_active_count": 0,
        "pica_bump_normal_map_backend_supported_count": 0,
        "pica_bump_mode_backend_pending_count": 0,
        "flag3_available_count": 0,
        "flag3_active_count": 0,
        "flag3_material_lighting_backend_pending_count": 0,
    }


def accumulate_scope_counts(counts: dict[str, int], record: dict[str, object]) -> None:
    model_key = f"{record['archive']}!{record['cmb']}"
    seen_key = "_seen_models"
    seen = counts.setdefault(seen_key, set())  # type: ignore[assignment]
    if isinstance(seen, set) and model_key not in seen:
        seen.add(model_key)
        counts["model_count"] += 1
    counts["material_count"] += 1
    if record["lighting_block_decoded"]:
        counts["lighting_block_decoded_material_count"] += 1
        counts["pica_bump_mode_available_count"] += 1
        counts["flag3_available_count"] += 1
    if record["material_lighting_complete"]:
        counts["material_lighting_complete_count"] += 1
    else:
        counts["material_lighting_incomplete_count"] += 1
    if record["pica_bump_mode_recognized"]:
        counts["pica_bump_mode_recognized_count"] += 1
    if record["pica_bump_mode_active"]:
        counts["pica_bump_mode_active_count"] += 1
    if record["pica_bump_normal_map_backend_supported"]:
        counts["pica_bump_normal_map_backend_supported_count"] += 1
    if record["pica_bump_mode_backend_pending"]:
        counts["pica_bump_mode_backend_pending_count"] += 1
    if record["flag3_active"]:
        counts["flag3_active_count"] += 1
    if record["flag3_material_lighting_backend_pending"]:
        counts["flag3_material_lighting_backend_pending_count"] += 1


def public_counts(counts: dict[str, object]) -> dict[str, int]:
    return {
        key: value
        for key, value in counts.items()
        if not key.startswith("_") and isinstance(value, int)
    }


def compact_sample(record: dict[str, object]) -> dict[str, object]:
    keys = (
        "archive",
        "cmb",
        "model_name",
        "material_index",
        "texture_indices",
        "texture_names",
        "fragment_lighting_enabled",
        "vertex_lighting_enabled",
        "hemisphere_lighting_enabled",
        "pica_bump_texture_unit",
        "pica_bump_mode",
        "pica_lighting_config",
        "flag3_active",
        "material_lighting_incomplete_reasons",
    )
    return {key: record[key] for key in keys}


def append_sample(samples: list[dict[str, object]], record: dict[str, object], sample_limit: int) -> None:
    if sample_limit <= 0 or len(samples) < sample_limit:
        samples.append(record)


def display_path(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def hex_field(value: int | None) -> str | None:
    return None if value is None else f"0x{value:04x}"
