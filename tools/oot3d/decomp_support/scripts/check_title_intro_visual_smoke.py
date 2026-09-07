#!/usr/bin/env python3
"""Compare a title-intro demo capture against a known visible baseline."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path
from typing import Any


FIELD_PATHS = {
    "title_frame": ("title_intro_runtime", "frame"),
    "frame_count": ("frame_count",),
    "environment_model_count": ("engine_render_scene", "environment_model_count"),
    "native_actor_visual_count": ("engine_render_scene", "native_actor_visual_count"),
    "camera_fov_degrees": ("engine_render_scene", "camera", "fov_degrees"),
    "camera_position": ("engine_render_scene", "camera", "position"),
    "camera_target": ("engine_render_scene", "camera", "target"),
    "initial_scene_camera_status": ("engine_render_scene", "initial_scene_camera_status"),
    "camera_blob_segment_source_index": ("engine_render_scene", "camera_blob_segment_source_index"),
    "camera_segment_start": (
        "engine_render_scene",
        "opening_frame_runtime",
        "step",
        "camera_segment_start_frame",
    ),
    "camera_segment_end": (
        "engine_render_scene",
        "opening_frame_runtime",
        "step",
        "camera_segment_end_frame",
    ),
    "kankyo_current_profile": (
        "engine_render_scene",
        "environment_background",
        "native_kankyo_current_profile_index",
    ),
    "kankyo_next_profile": (
        "engine_render_scene",
        "environment_background",
        "native_kankyo_next_profile_index",
    ),
    "kankyo_schedule_entry": (
        "engine_render_scene",
        "environment_background",
        "native_kankyo_schedule_entry_index",
    ),
    "kankyo_blend_alpha": (
        "engine_render_scene",
        "environment_background",
        "native_kankyo_blend_alpha",
    ),
    "kankyo_material_frame": (
        "engine_render_scene",
        "environment_background",
        "native_kankyo_material_animation_frame",
    ),
    "render_submission_status": ("last_engine_renderer_submission", "status"),
    "render_model_count": ("last_engine_renderer_submission", "model_count"),
    "render_draw_call_count": ("last_engine_renderer_submission", "draw_call_count"),
    "render_texture_upload_count": ("last_engine_renderer_submission", "texture_upload_count"),
    "render_missing_texture_draw_call_count": (
        "last_engine_renderer_submission",
        "missing_texture_draw_call_count",
    ),
    "backend_draw_call_count": ("last_fast3d_adapter", "backend_draw_call_count"),
    "backend_batch_draw_count": ("last_fast3d_adapter", "batch_draw_count"),
    "lifetime_backend_draw_call_count": (
        "fast3d_adapter_lifetime",
        "backend_draw_call_count",
    ),
    "lifetime_backend_batch_draw_count": (
        "fast3d_adapter_lifetime",
        "batch_draw_count",
    ),
    "lifetime_texture_upload_count": (
        "fast3d_adapter_lifetime",
        "texture_upload_count",
    ),
    "backend_shader_input_layout_mismatch_count": (
        "last_fast3d_adapter",
        "shader_input_layout_mismatch_count",
    ),
    "lifetime_missing_texture_batch_count": (
        "fast3d_adapter_lifetime",
        "missing_texture_batch_count",
    ),
    "lifetime_missing_texture_batch_model_counts": (
        "fast3d_adapter_lifetime",
        "missing_texture_batch_model_counts",
    ),
    "lifetime_missing_secondary_texture_binding_count": (
        "fast3d_adapter_lifetime",
        "missing_secondary_texture_binding_count",
    ),
    "lifetime_missing_tertiary_texture_binding_count": (
        "fast3d_adapter_lifetime",
        "missing_tertiary_texture_binding_count",
    ),
    "native_pica_fog_enabled": ("last_fast3d_adapter", "native_pica_fog_enabled"),
    "native_pica_fog_used_for_render": ("last_fast3d_adapter", "native_pica_fog_used_for_render"),
    "native_pica_fog_applied_batch_count": (
        "last_fast3d_adapter",
        "native_pica_fog_applied_batch_count",
    ),
}


def read_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValueError(f"{path} does not contain a JSON object")
    return value


def get_path(value: dict[str, Any], path: tuple[str, ...]) -> Any:
    current: Any = value
    for key in path:
        if not isinstance(current, dict) or key not in current:
            return None
        current = current[key]
    return current


def extract_fields(value: dict[str, Any]) -> dict[str, Any]:
    fields = {name: get_path(value, path) for name, path in FIELD_PATHS.items()}
    fallback_paths = {
        "camera_fov_degrees": (("camera", "fov_degrees"),),
        "camera_position": (("camera", "position"),),
        "camera_target": (("camera", "target"),),
        "initial_scene_camera_status": (
            ("title_intro_runtime", "initial_scene_camera_status"),
            ("title_intro_runtime", "opening_frame_runtime", "render_binding_status"),
        ),
        "camera_blob_segment_source_index": (
            ("title_intro_runtime", "camera_blob_segment_source_index"),
            ("title_intro_runtime", "opening_frame_runtime", "step", "camera", "camera_blob_segment_source_index"),
        ),
        "camera_segment_start": (
            ("title_intro_runtime", "camera_segment", "start_frame"),
            ("title_intro_runtime", "opening_frame_runtime", "camera_segment_start_frame"),
            ("title_intro_runtime", "opening_frame_runtime", "step", "camera_segment_start_frame"),
        ),
        "camera_segment_end": (
            ("title_intro_runtime", "camera_segment", "end_frame"),
            ("title_intro_runtime", "opening_frame_runtime", "camera_segment_end_frame"),
            ("title_intro_runtime", "opening_frame_runtime", "step", "camera_segment_end_frame"),
        ),
    }
    for name, paths in fallback_paths.items():
        if fields.get(name) is not None:
            continue
        for path in paths:
            value_at_path = get_path(value, path)
            if value_at_path is not None:
                fields[name] = value_at_path
                break
    return fields


def read_bmp_stats(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError(f"{path} is not a BMP file")

    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40:
        raise ValueError(f"{path} uses an unsupported BMP DIB header")

    width, height, planes, bits_per_pixel, compression = struct.unpack_from("<iiHHI", data, 18)
    if planes != 1:
        raise ValueError(f"{path} has invalid BMP plane count {planes}")
    if bits_per_pixel not in (24, 32):
        raise ValueError(f"{path} has unsupported BMP depth {bits_per_pixel}")
    if compression != 0:
        raise ValueError(f"{path} uses unsupported BMP compression {compression}")
    if width <= 0 or height == 0:
        raise ValueError(f"{path} has invalid BMP dimensions {width}x{height}")

    row_count = abs(height)
    top_down = height < 0
    pixel_size = bits_per_pixel // 8
    row_stride = ((width * bits_per_pixel + 31) // 32) * 4

    channel_min = [255, 255, 255]
    channel_max = [0, 0, 0]
    channel_sum = [0, 0, 0]
    unique: set[tuple[int, int, int]] = set()
    unique_over_cap = False
    unique_cap = 4096

    for out_row in range(row_count):
        src_row = out_row if top_down else row_count - 1 - out_row
        row_offset = pixel_offset + src_row * row_stride
        row_end = row_offset + width * pixel_size
        if row_end > len(data):
            raise ValueError(f"{path} is truncated before all pixels could be read")
        for x in range(width):
            offset = row_offset + x * pixel_size
            blue, green, red = data[offset], data[offset + 1], data[offset + 2]
            rgb = (red, green, blue)
            for channel, component in enumerate(rgb):
                channel_min[channel] = min(channel_min[channel], component)
                channel_max[channel] = max(channel_max[channel], component)
                channel_sum[channel] += component
            if not unique_over_cap:
                unique.add(rgb)
                if len(unique) > unique_cap:
                    unique_over_cap = True
                    unique.clear()

    pixel_count = width * row_count
    mean = [round(total / pixel_count, 6) for total in channel_sum]
    uniform = all(channel_min[index] == channel_max[index] for index in range(3))
    return {
        "path": str(path),
        "width": width,
        "height": row_count,
        "bits_per_pixel": bits_per_pixel,
        "mean_rgb": mean,
        "min_rgb": channel_min,
        "max_rgb": channel_max,
        "uniform": uniform,
        "unique_color_count_capped": None if unique_over_cap else len(unique),
        "unique_color_count_over_cap": unique_over_cap,
    }


def classify_capture(
    fields: dict[str, Any], image: dict[str, Any] | None, expect_uniform_black: bool = False
) -> str:
    if image is None:
        return "fail_missing_screenshot"
    draw_calls = fields.get("render_draw_call_count")
    if draw_calls is None:
        draw_calls = fields.get("lifetime_backend_draw_call_count")
    textures = fields.get("render_texture_upload_count")
    if textures is None:
        textures = fields.get("lifetime_texture_upload_count")
    models = fields.get("render_model_count")
    if models is None:
        environment_models = fields.get("environment_model_count") or 0
        actor_models = fields.get("native_actor_visual_count") or 0
        models = environment_models + actor_models
    if (fields.get("lifetime_missing_texture_batch_count") or 0) > 0:
        return "fail_missing_texture_batches"
    if (fields.get("lifetime_missing_secondary_texture_binding_count") or 0) > 0:
        return "fail_missing_secondary_texture_bindings"
    if (fields.get("lifetime_missing_tertiary_texture_binding_count") or 0) > 0:
        return "fail_missing_tertiary_texture_bindings"
    if expect_uniform_black:
        if image["uniform"] and image["min_rgb"] == [0, 0, 0] and image["max_rgb"] == [0, 0, 0]:
            return "pass_expected_terminal_black"
        return "fail_expected_terminal_black"
    if image["uniform"] and (draw_calls or 0) > 0 and (textures or 0) > 0 and models > 0:
        return "fail_uniform_framebuffer_with_valid_render_submission"
    if image["uniform"]:
        return "fail_uniform_framebuffer"
    if draw_calls is not None and draw_calls <= 0:
        return "fail_missing_render_submission"
    if models <= 0:
        return "fail_missing_render_submission"
    return "pass_nonuniform_framebuffer"


def diff_fields(current: dict[str, Any], reference: dict[str, Any] | None) -> dict[str, Any]:
    if reference is None:
        return {}
    diff: dict[str, Any] = {}
    for key, current_value in current.items():
        reference_value = reference.get(key)
        if current_value != reference_value:
            diff[key] = {
                "reference": reference_value,
                "current": current_value,
            }
    return diff


def write_markdown(path: Path, report: dict[str, Any]) -> None:
    current = report["current"]
    reference = report.get("reference")
    diff = report.get("field_differences", {})
    lines = [
        "# Title intro visual regression smoke",
        "",
        f"- Status: `{report['status']}`",
        f"- Current image uniform: `{current['image']['uniform'] if current.get('image') else None}`",
        f"- Current image mean RGB: `{current['image']['mean_rgb'] if current.get('image') else None}`",
    ]
    if reference:
        lines.extend(
            [
                f"- Reference status: `{reference['status']}`",
                f"- Reference image uniform: `{reference['image']['uniform'] if reference.get('image') else None}`",
                f"- Reference image mean RGB: `{reference['image']['mean_rgb'] if reference.get('image') else None}`",
            ]
        )
    lines.extend(["", "## Key fields", ""])
    key_fields = [
        "title_frame",
        "frame_count",
        "environment_model_count",
        "native_actor_visual_count",
        "camera_fov_degrees",
        "camera_segment_start",
        "camera_segment_end",
        "kankyo_current_profile",
        "kankyo_next_profile",
        "kankyo_schedule_entry",
        "kankyo_blend_alpha",
        "kankyo_material_frame",
        "render_submission_status",
        "render_model_count",
        "render_draw_call_count",
        "render_texture_upload_count",
        "backend_draw_call_count",
        "lifetime_backend_draw_call_count",
        "lifetime_backend_batch_draw_count",
        "lifetime_texture_upload_count",
        "backend_shader_input_layout_mismatch_count",
        "lifetime_missing_texture_batch_count",
        "lifetime_missing_texture_batch_model_counts",
        "lifetime_missing_secondary_texture_binding_count",
        "lifetime_missing_tertiary_texture_binding_count",
        "native_pica_fog_applied_batch_count",
    ]
    lines.append("| Field | Reference | Current |")
    lines.append("| --- | --- | --- |")
    for key in key_fields:
        ref_value = reference["fields"].get(key) if reference else None
        cur_value = current["fields"].get(key)
        lines.append(f"| `{key}` | `{ref_value}` | `{cur_value}` |")
    lines.extend(["", "## Divergence", ""])
    if diff:
        for key, values in diff.items():
            lines.append(f"- `{key}`: reference `{values['reference']}`, current `{values['current']}`")
    else:
        lines.append("- No extracted field differences.")
    lines.extend(["", "## Diagnosis", ""])
    for item in report["diagnosis"]:
        lines.append(f"- {item}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_capture(
    json_path: Path, bmp_path: Path | None, expect_uniform_black: bool = False
) -> dict[str, Any]:
    fields = extract_fields(read_json(json_path))
    image = read_bmp_stats(bmp_path) if bmp_path is not None else None
    return {
        "json_path": str(json_path),
        "bmp_path": str(bmp_path) if bmp_path is not None else None,
        "fields": fields,
        "image": image,
        "status": classify_capture(fields, image, expect_uniform_black),
    }


def build_diagnosis(current: dict[str, Any], reference: dict[str, Any] | None) -> list[str]:
    diagnosis: list[str] = []
    current_image = current.get("image")
    current_fields = current["fields"]
    if current["status"] == "fail_uniform_framebuffer_with_valid_render_submission":
        diagnosis.append(
            "The current framebuffer is uniform even though render submission, texture uploads, "
            "and backend draw calls are present; this points at runtime phase or render-state "
            "collapse, not an asset discovery failure."
        )
    if current["status"] == "fail_expected_terminal_black":
        diagnosis.append(
            "The checkpoint is beyond the source-backed native cutscene end, but the framebuffer "
            "is not uniformly black. Inspect terminal cutscene phase handling."
        )
    if current["status"] == "fail_missing_texture_batches":
        diagnosis.append(
            "The backend skipped textured batches after asset submission; inspect texture record "
            "decoding and resident texture cache identity before evaluating framebuffer pixels."
        )
        missing_models = current_fields.get("lifetime_missing_texture_batch_model_counts") or {}
        if missing_models:
            diagnosis.append(f"Affected model batch counts: `{missing_models}`.")
    if reference:
        ref_image = reference.get("image")
        if ref_image and current_image and not ref_image["uniform"] and current_image["uniform"]:
            diagnosis.append("A previously visible baseline regressed to a uniform framebuffer.")
        for key in (
            "title_frame",
            "camera_segment_start",
            "camera_segment_end",
            "kankyo_current_profile",
            "kankyo_next_profile",
            "kankyo_blend_alpha",
            "kankyo_material_frame",
        ):
            ref_value = reference["fields"].get(key)
            cur_value = current_fields.get(key)
            if ref_value != cur_value:
                diagnosis.append(f"`{key}` differs: baseline `{ref_value}`, current `{cur_value}`.")
    return diagnosis


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--current-json", required=True, type=Path)
    parser.add_argument("--current-bmp", required=True, type=Path)
    parser.add_argument("--reference-json", type=Path)
    parser.add_argument("--reference-bmp", type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    parser.add_argument("--expect-uniform-black", action="store_true")
    args = parser.parse_args()

    current = build_capture(args.current_json, args.current_bmp, args.expect_uniform_black)
    reference = None
    if args.reference_json is not None:
        if args.reference_bmp is None:
            raise SystemExit("--reference-bmp is required when --reference-json is provided")
        reference = build_capture(args.reference_json, args.reference_bmp)

    report = {
        "status": current["status"],
        "current": current,
        "reference": reference,
        "field_differences": diff_fields(current["fields"], reference["fields"] if reference else None),
        "diagnosis": build_diagnosis(current, reference),
    }
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_md.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_markdown(args.output_md, report)
    print(report["status"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
