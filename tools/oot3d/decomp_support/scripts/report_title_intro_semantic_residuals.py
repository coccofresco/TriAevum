#!/usr/bin/env python3
"""Report actionable native material, animation, and OpenGL residuals."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any, Iterable


MODEL_PENDING_COUNTERS = (
    "native_material_combiner_pending_batch_count",
    "native_material_texture_env_shader_pending_batch_count",
    "native_material_pica_lut_input_evaluation_pending_batch_count",
    "native_material_lighting_incomplete_batch_count",
    "native_pica_vertex_hemisphere_vector_pending_batch_count",
    "native_pica_unlit_texture_env_route_missing_native_color_batch_count",
)

BACKEND_FAILURE_COUNTERS = (
    "missing_texture_batch_count",
    "missing_texture_draw_call_count",
    "missing_secondary_texture_binding_count",
    "missing_tertiary_texture_binding_count",
    "effect_primitive_missing_texture_count",
    "native_kankyo_primitive_missing_texture_submit_count",
    "native_pica_texture2_backend_unsupported_batch_count",
    "native_pica_alpha_test_backend_unsupported_batch_count",
    "native_blend_state_backend_unsupported_batch_count",
    "shader_input_layout_mismatch_count",
)

BACKEND_COVERAGE_COUNTERS = (
    "secondary_texture_binding_count",
    "secondary_texture_coord_batch_count",
    "tertiary_texture_binding_count",
    "tertiary_texture_coord_batch_count",
)


def int_value(value: Any) -> int:
    return int(value) if isinstance(value, (bool, int, float)) else 0


def iter_models(scene: dict[str, Any]) -> Iterable[tuple[str, dict[str, Any]]]:
    room = scene.get("room")
    if isinstance(room, dict):
        yield "room", room
    for collection, label in (
        ("native_actor_visuals", "actor"),
        ("environment_models", "environment"),
        ("moon_models", "moon"),
    ):
        models = scene.get(collection, [])
        if not isinstance(models, list):
            continue
        for index, model in enumerate(models):
            if isinstance(model, dict):
                yield f"{label}[{index}]", model


def add_residual(
    residuals: list[dict[str, Any]],
    scope: str,
    model_name: str,
    kind: str,
    value: Any,
    material_index: int | None = None,
    detail: Any = None,
) -> None:
    item: dict[str, Any] = {
        "scope": scope,
        "model": model_name,
        "kind": kind,
        "value": value,
    }
    if material_index is not None:
        item["material_index"] = material_index
    if detail not in (None, "", []):
        item["detail"] = detail
    residuals.append(item)


def analyze(checkpoint: dict[str, Any], source: Path) -> dict[str, Any]:
    scene = checkpoint.get("engine_render_scene", {})
    if not isinstance(scene, dict):
        raise ValueError("checkpoint has no engine_render_scene object")

    residuals: list[dict[str, Any]] = []
    coverage = {
        "model_count": 0,
        "material_count": 0,
        "supported_texture_env_program_count": 0,
        "applied_texture_env_program_count": 0,
        "native_animation_morph_active_model_count": 0,
        "native_animation_morph_applied_model_count": 0,
    }
    program_coverage: dict[str, int] = {}
    complex_routes: list[dict[str, Any]] = []
    complex_route_keys: set[tuple[Any, ...]] = set()

    for scope, model in iter_models(scene):
        coverage["model_count"] += 1
        model_name = str(model.get("name", ""))
        model_source = str(model.get("source", ""))
        if "native_skelanime_morph_active=true" in model_source:
            coverage["native_animation_morph_active_model_count"] += 1
            if "native_skelanime_morph_applied=true" in model_source:
                coverage["native_animation_morph_applied_model_count"] += 1
            else:
                add_residual(
                    residuals,
                    scope,
                    model_name,
                    "native_animation_morph_not_applied",
                    True,
                )
        for counter in MODEL_PENDING_COUNTERS:
            count = int_value(model.get(counter))
            if count > 0:
                add_residual(residuals, scope, model_name, counter, count)

        materials = model.get("native_materials", [])
        if not isinstance(materials, list):
            continue
        coverage["material_count"] += len(materials)
        for material in materials:
            if not isinstance(material, dict):
                continue
            material_index = int_value(material.get("material_index", -1))
            program = material.get("texture_env_program", {})
            if not isinstance(program, dict):
                program = {}
            supported = bool(program.get("color_shader_path_supported", False))
            applied = bool(program.get("color_shader_path_applied", False))
            if supported:
                coverage["supported_texture_env_program_count"] += 1
                path = str(program.get("color_shader_path", ""))
                program_coverage[path] = program_coverage.get(path, 0) + 1
            if applied:
                coverage["applied_texture_env_program_count"] += 1

            secondary_texture = int_value(material.get("secondary_texture_index", -1))
            tertiary_texture = int_value(material.get("tertiary_texture_index", -1))
            lut_culled = bool(
                material.get("native_pica_material_lut_input_evaluation_culled_as_unused", False)
            )
            if secondary_texture >= 0 or tertiary_texture >= 0 or lut_culled:
                route_key = (
                    scope,
                    model_name,
                    material_index,
                    program.get("color_shader_path", ""),
                    secondary_texture,
                    tertiary_texture,
                    lut_culled,
                )
                if route_key not in complex_route_keys:
                    complex_route_keys.add(route_key)
                    complex_routes.append(
                        {
                            "scope": scope,
                            "model": model_name,
                            "material_index": material_index,
                            "program": program.get("color_shader_path", ""),
                            "secondary_texture_index": secondary_texture,
                            "tertiary_texture_index": tertiary_texture,
                            "fragment_lut_evaluation_culled_as_unused": lut_culled,
                        }
                    )

            if bool(material.get("native_material_combiner_requires_decoder", False)):
                add_residual(
                    residuals,
                    scope,
                    model_name,
                    "texture_env_combiner_requires_decoder",
                    True,
                    material_index,
                    material.get("native_material_lighting_incomplete_reasons"),
                )
            if material.get("native_material_lighting_complete") is False:
                add_residual(
                    residuals,
                    scope,
                    model_name,
                    "native_material_lighting_incomplete",
                    True,
                    material_index,
                    material.get("native_material_lighting_incomplete_reasons"),
                )
            if bool(material.get("textured", False)) and supported and not applied:
                add_residual(
                    residuals,
                    scope,
                    model_name,
                    "texture_env_shader_route_not_applied",
                    program.get("color_shader_path", ""),
                    material_index,
                )
            for field in (
                "native_pica_material_lut_input_evaluation_pending",
                "native_pica_fragment_lighting_config_runtime_override_pending",
                "native_pica_bump_mode_backend_pending",
                "native_pica_vertex_hemisphere_vector_pending",
            ):
                if bool(material.get(field, False)):
                    add_residual(
                        residuals,
                        scope,
                        model_name,
                        field,
                        True,
                        material_index,
                    )

    backend = checkpoint.get("fast3d_adapter_lifetime", {})
    if not isinstance(backend, dict):
        backend = {}
    backend_failures = {key: int_value(backend.get(key)) for key in BACKEND_FAILURE_COUNTERS}
    backend_coverage = {key: int_value(backend.get(key)) for key in BACKEND_COVERAGE_COUNTERS}
    for key, count in backend_failures.items():
        if count > 0:
            add_residual(residuals, "backend", "OpenGL Fast3D", key, count)

    title_runtime = checkpoint.get("title_intro_runtime", {})
    opening_runtime = (
        title_runtime.get("opening_frame_runtime", {})
        if isinstance(title_runtime, dict)
        else {}
    )
    step = opening_runtime.get("step", {}) if isinstance(opening_runtime, dict) else {}
    actor_motion = step.get("actor_motion", {}) if isinstance(step, dict) else {}
    paired_animation = (
        actor_motion.get("paired_mount_animation", {})
        if isinstance(actor_motion, dict)
        else {}
    )
    if not isinstance(paired_animation, dict):
        paired_animation = {}
    animation_coverage = {
        "paired_mount_animation_present": bool(paired_animation),
        "paired_mount_animation_valid": bool(paired_animation.get("valid", False)),
        "native_morph_active": bool(paired_animation.get("morph_active", False)),
        "native_morph_source_animation_index": int_value(
            paired_animation.get("morph_source_animation_index", -1)
        ),
        "native_morph_source_csab_name": str(
            paired_animation.get("morph_source_csab_name", "")
        ),
        "native_morph_source_frame": paired_animation.get("morph_source_frame", 0.0),
        "native_morph_weight": paired_animation.get("morph_weight", 0.0),
        "native_morph_rate": paired_animation.get("morph_rate", 0.0),
        "native_morph_frames": paired_animation.get("morph_frames", 0.0),
    }
    if animation_coverage["native_morph_active"]:
        morph_weight = float(animation_coverage["native_morph_weight"])
        morph_rate = float(animation_coverage["native_morph_rate"])
        morph_frames = float(animation_coverage["native_morph_frames"])
        if not animation_coverage["native_morph_source_csab_name"]:
            add_residual(
                residuals,
                "animation",
                "opening_epona",
                "native_animation_morph_source_csab_missing",
                True,
            )
        if not (0.0 < morph_weight <= 1.0):
            add_residual(
                residuals,
                "animation",
                "opening_epona",
                "native_animation_morph_weight_out_of_range",
                morph_weight,
            )
        if morph_frames >= 0.0 or not math.isclose(
            morph_rate, 1.0 / -morph_frames, rel_tol=1.0e-6, abs_tol=1.0e-6
        ):
            add_residual(
                residuals,
                "animation",
                "opening_epona",
                "native_animation_morph_rate_mismatch",
                morph_rate,
                detail={"morph_frames": morph_frames},
            )
        if coverage["native_animation_morph_applied_model_count"] == 0:
            add_residual(
                residuals,
                "animation",
                "opening_epona",
                "native_animation_morph_missing_from_render_scene",
                True,
            )

    return {
        "format": "oot3d_title_intro_semantic_residual_report_v1",
        "source_checkpoint": str(source),
        "status": "complete" if not residuals else "residuals_present",
        "residual_count": len(residuals),
        "coverage": coverage,
        "program_coverage": dict(sorted(program_coverage.items())),
        "complex_routes": complex_routes,
        "backend_failures": backend_failures,
        "backend_coverage": backend_coverage,
        "animation_coverage": animation_coverage,
        "residuals": residuals,
        "scope_notes": [
            "The report evaluates native CMB material, animation, and OpenGL backend execution state, not screenshot similarity.",
            "Inactive/default Shadow2D state is not classified as a residual for this fixture.",
            "Synthetic runtime environment primitives are covered only by backend failure counters because they have no CMB material records.",
        ],
    }


def markdown(report: dict[str, Any]) -> str:
    coverage = report["coverage"]
    backend_failures = report["backend_failures"]
    backend_coverage = report["backend_coverage"]
    lines = [
        "# OoT3D title intro semantic residuals",
        "",
        f"- Source checkpoint: `{report['source_checkpoint']}`",
        f"- Status: `{report['status']}`",
        f"- Residual count: `{report['residual_count']}`",
        f"- Models inspected: `{coverage['model_count']}`",
        f"- Native material records inspected: `{coverage['material_count']}`",
        f"- Supported TextureEnv routes: `{coverage['supported_texture_env_program_count']}`",
        f"- Applied TextureEnv routes: `{coverage['applied_texture_env_program_count']}`",
        f"- Active native animation morph models: `{coverage['native_animation_morph_active_model_count']}`",
        f"- Applied native animation morph models: `{coverage['native_animation_morph_applied_model_count']}`",
        "",
        "## OpenGL backend",
        "",
    ]
    lines.extend(f"- `{key}`: `{value}`" for key, value in backend_failures.items())
    lines.extend(f"- `{key}`: `{value}`" for key, value in backend_coverage.items())
    lines.extend(["", "## Native animation", ""])
    lines.extend(
        f"- `{key}`: `{value}`" for key, value in report["animation_coverage"].items()
    )
    lines.extend(["", "## Native TextureEnv coverage", ""])
    lines.extend(
        f"- `{program}`: `{count}` material records"
        for program, count in report["program_coverage"].items()
    )
    lines.extend(["", "## Complex resolved routes", ""])
    for route in report["complex_routes"]:
        lines.append(
            f"- `{route['scope']}` `{route['model']}` material `{route['material_index']}`: "
            f"`{route['program']}`; texture1 `{route['secondary_texture_index']}`; "
            f"texture2 `{route['tertiary_texture_index']}`; dead fragment LUT "
            f"`{route['fragment_lut_evaluation_culled_as_unused']}`"
        )
    lines.extend(["", "## Actionable residuals", ""])
    residuals = report["residuals"]
    if not residuals:
        lines.append(
            "No actionable native material, animation, or OpenGL backend residuals were found in this checkpoint."
        )
    else:
        for item in residuals:
            material = (
                f" material `{item['material_index']}`" if "material_index" in item else ""
            )
            detail = f"; detail: `{item['detail']}`" if "detail" in item else ""
            lines.append(
                f"- `{item['scope']}` `{item['model']}`{material}: "
                f"`{item['kind']}` = `{item['value']}`{detail}"
            )
    lines.extend(["", "## Scope", ""])
    lines.extend(f"- {note}" for note in report["scope_notes"])
    lines.append("")
    return "\n".join(lines)


def aggregate_reports(reports: list[dict[str, Any]]) -> dict[str, Any]:
    residuals: list[dict[str, Any]] = []
    checkpoints: list[dict[str, Any]] = []
    for report in reports:
        for item in report["residuals"]:
            residuals.append({"checkpoint": report["source_checkpoint"], **item})
        checkpoints.append(
            {
                "source_checkpoint": report["source_checkpoint"],
                "status": report["status"],
                "residual_count": report["residual_count"],
                "coverage": report["coverage"],
                "backend_failures": report["backend_failures"],
                "backend_coverage": report["backend_coverage"],
                "animation_coverage": report["animation_coverage"],
                "program_coverage": report["program_coverage"],
            }
        )
    return {
        "format": "oot3d_title_intro_semantic_residual_sweep_report_v1",
        "status": "complete" if not residuals else "residuals_present",
        "checkpoint_count": len(checkpoints),
        "residual_count": len(residuals),
        "checkpoints": checkpoints,
        "residuals": residuals,
        "scope_notes": reports[0]["scope_notes"] if reports else [],
        "native_animation_morph_checkpoint_count": sum(
            1 for report in reports if report["animation_coverage"]["native_morph_active"]
        ),
    }


def sweep_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# OoT3D title intro semantic residual sweep",
        "",
        f"- Status: `{report['status']}`",
        f"- Checkpoints inspected: `{report['checkpoint_count']}`",
        f"- Residual count: `{report['residual_count']}`",
        f"- Checkpoints with active native animation morph: `{report['native_animation_morph_checkpoint_count']}`",
        "",
        "## Checkpoints",
        "",
        "| checkpoint | status | models | materials | shader routes | morph models | residuals |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for checkpoint in report["checkpoints"]:
        coverage = checkpoint["coverage"]
        lines.append(
            f"| `{checkpoint['source_checkpoint']}` | `{checkpoint['status']}` | "
            f"{coverage['model_count']} | {coverage['material_count']} | "
            f"{coverage['applied_texture_env_program_count']} | "
            f"{coverage['native_animation_morph_active_model_count']}/"
            f"{coverage['native_animation_morph_applied_model_count']} | "
            f"{checkpoint['residual_count']} |"
        )
    lines.extend(["", "## Actionable residuals", ""])
    if not report["residuals"]:
        lines.append(
            "No actionable native material, animation, or OpenGL backend residuals were found in the sweep."
        )
    else:
        for item in report["residuals"]:
            material = (
                f" material `{item['material_index']}`" if "material_index" in item else ""
            )
            detail = f"; detail: `{item['detail']}`" if "detail" in item else ""
            lines.append(
                f"- `{item['checkpoint']}`: `{item['scope']}` `{item['model']}`{material}: "
                f"`{item['kind']}` = `{item['value']}`{detail}"
            )
    lines.extend(["", "## Scope", ""])
    lines.extend(f"- {note}" for note in report["scope_notes"])
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkpoint", nargs="+", type=Path)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--markdown-out", type=Path)
    parser.add_argument("--require-native-animation-morph", action="store_true")
    args = parser.parse_args()

    reports = [
        analyze(json.loads(path.read_text(encoding="utf-8")), path)
        for path in args.checkpoint
    ]
    multiple = len(reports) > 1
    report = aggregate_reports(reports) if multiple else reports[0]
    active_morph_count = (
        report["native_animation_morph_checkpoint_count"]
        if multiple
        else int(report["animation_coverage"]["native_morph_active"])
    )
    if args.require_native_animation_morph and active_morph_count == 0:
        missing = {
            "scope": "animation",
            "model": "opening_epona",
            "kind": "native_animation_morph_coverage_missing",
            "value": True,
        }
        report["residuals"].append(missing)
        report["residual_count"] = len(report["residuals"])
        report["status"] = "residuals_present"
    if multiple:
        default_json_out = args.checkpoint[0].parent / "title_intro.semantic_residual_sweep.json"
        default_markdown_out = args.checkpoint[0].parent / "title_intro.semantic_residual_sweep.md"
    else:
        default_json_out = args.checkpoint[0].with_suffix(".semantic_residuals.json")
        default_markdown_out = args.checkpoint[0].with_suffix(".semantic_residuals.md")
    json_out = args.json_out or default_json_out
    markdown_out = args.markdown_out or default_markdown_out
    json_out.parent.mkdir(parents=True, exist_ok=True)
    markdown_out.parent.mkdir(parents=True, exist_ok=True)
    json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    markdown_out.write_text(sweep_markdown(report) if multiple else markdown(report), encoding="utf-8")
    print(json.dumps({"status": report["status"], "residual_count": report["residual_count"]}))
    return 0 if not report["residuals"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
