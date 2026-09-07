from __future__ import annotations

import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path

from .binary import ParseError


MESH_DISPLAY_LIST_RE = re.compile(r"_mesh_\d+$")


def audit_static_batch(
    batch_manifest_path: Path,
    output_path: Path | None = None,
) -> dict[str, object]:
    if not batch_manifest_path.is_file():
        raise ParseError(f"{batch_manifest_path}: batch manifest not found")

    manifest = json.loads(batch_manifest_path.read_text(encoding="utf-8"))
    records = [record for record in manifest.get("records", []) if record.get("status") == "converted"]
    resources = [
        resource
        for record in records
        for resource in record.get("resources", [])
    ]
    resource_paths = {normalize_resource_path(resource["path"]) for resource in resources}

    resource_results = [
        audit_resource(batch_manifest_path.parent, resource, resource_paths)
        for resource in resources
    ]
    material_results = [
        result for result in resource_results if result["role"] == "material_display_list"
    ]
    mesh_results = [
        result for result in resource_results if result["role"] == "mesh_display_list"
    ]
    vertex_results = [
        result for result in resource_results if result["kind"] == "Vertex"
    ]

    issue_counts = count_issues(resource_results)
    vertex_min_values = [
        result["min_st"]
        for result in vertex_results
        if result.get("min_st") is not None
    ]
    vertex_max_values = [
        result["max_st"]
        for result in vertex_results
        if result.get("max_st") is not None
    ]
    audit = {
        "batch_manifest": str(batch_manifest_path),
        "input": manifest.get("input"),
        "resource_prefix": manifest.get("resource_prefix"),
        "converted_record_count": len(records),
        "resource_count": len(resources),
        "material_display_list_count": len(material_results),
        "mesh_display_list_count": len(mesh_results),
        "vertex_resource_count": len(vertex_results),
        "set_texture_image_count": sum(len(result.get("texture_paths", [])) for result in material_results),
        "load_texture_block_count": sum(result.get("load_texture_block_count", 0) for result in resource_results),
        "negative_st_count": sum(result.get("negative_st_count", 0) for result in vertex_results),
        "min_st": min(vertex_min_values) if vertex_min_values else None,
        "max_st": max(vertex_max_values) if vertex_max_values else None,
        "issue_counts": issue_counts,
        "resource_results": resource_results,
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8", newline="\n")
    return audit


def audit_resource(
    manifest_dir: Path,
    resource: dict[str, object],
    resource_paths: set[str],
) -> dict[str, object]:
    resource_path = normalize_resource_path(resource["path"])
    file_path = resolve_resource_file(manifest_dir, resource["file"])
    kind = str(resource.get("kind"))
    role = classify_resource(resource_path, kind)
    issues: list[str] = []
    texture_paths: list[str] = []
    material_call_paths: list[str] = []
    min_st = None
    max_st = None
    negative_st_count = 0
    load_texture_block_count = 0

    if not file_path.is_file():
        issues.append("missing_resource_file")
    elif kind in {"DisplayList", "Vertex"}:
        try:
            text = file_path.read_text(encoding="utf-8")
            load_texture_block_count = text.count("LoadTextureBlock")
            root = ET.fromstring(text)
        except (OSError, UnicodeDecodeError, ET.ParseError) as exc:
            issues.append("invalid_resource_xml")
            error = str(exc)
        else:
            error = None
            if load_texture_block_count:
                issues.append("load_texture_block_used")
            if role == "material_display_list":
                texture_paths = [
                    normalize_resource_path(element.attrib["Path"])
                    for element in root.iter("SetTextureImage")
                    if "Path" in element.attrib
                ]
                if any(path not in resource_paths for path in texture_paths):
                    issues.append("missing_texture_reference")
            elif role == "mesh_display_list":
                material_call_paths = [
                    normalize_resource_path(element.attrib["Path"])
                    for element in root.iter("CallDisplayList")
                    if "Path" in element.attrib and "_mat_" in element.attrib["Path"]
                ]
                if not material_call_paths:
                    issues.append("missing_mesh_material_call")
                elif any(path not in resource_paths for path in material_call_paths):
                    issues.append("missing_mesh_material_reference")
            elif kind == "Vertex":
                st_values = [
                    int(element.attrib[name])
                    for element in root.iter("Vtx")
                    for name in ("S", "T")
                    if name in element.attrib
                ]
                if st_values:
                    min_st = min(st_values)
                    max_st = max(st_values)
                    negative_st_count = sum(1 for value in st_values if value < 0)
                if negative_st_count:
                    issues.append("negative_vertex_st")

    result = {
        "kind": kind,
        "role": role,
        "path": resource_path,
        "file": str(file_path),
        "exists": file_path.is_file(),
        "texture_paths": texture_paths,
        "material_call_paths": material_call_paths,
        "load_texture_block_count": load_texture_block_count,
        "min_st": min_st,
        "max_st": max_st,
        "negative_st_count": negative_st_count,
        "issues": issues,
    }
    if "error" in locals() and error:
        result["error"] = error
    return result


def classify_resource(resource_path: str, kind: str) -> str:
    if kind == "Texture":
        return "texture"
    if kind == "Vertex":
        return "vertex"
    if kind != "DisplayList":
        return "other"
    if "_mat_" in resource_path:
        return "material_display_list"
    if MESH_DISPLAY_LIST_RE.search(resource_path):
        return "mesh_display_list"
    if "_tri" in resource_path:
        return "triangle_display_list"
    return "top_display_list"


def count_issues(results: list[dict[str, object]]) -> dict[str, int]:
    known_issues = (
        "missing_resource_file",
        "invalid_resource_xml",
        "load_texture_block_used",
        "missing_texture_reference",
        "missing_mesh_material_call",
        "missing_mesh_material_reference",
        "negative_vertex_st",
    )
    counts = {issue: 0 for issue in known_issues}
    for result in results:
        for issue in result.get("issues", []):
            counts[issue] = counts.get(issue, 0) + 1
    counts["total"] = sum(counts.values())
    return counts


def resolve_resource_file(manifest_dir: Path, value: object) -> Path:
    path = Path(str(value))
    if path.exists() or path.is_absolute():
        return path
    return manifest_dir / path


def normalize_resource_path(value: object) -> str:
    return str(value).replace("\\", "/").strip("/")
