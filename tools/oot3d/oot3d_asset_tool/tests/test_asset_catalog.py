from __future__ import annotations

import json
from pathlib import Path

from oot3d_asset_tool.asset_catalog import MANIFESTS, build_asset_catalog


def _write(root: Path, key: str, data: dict) -> None:
    path = root / MANIFESTS[key]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data), encoding="utf-8")


def test_catalog_uses_native_identities_and_keeps_route_bindings_separate(tmp_path: Path) -> None:
    _write(tmp_path, "ctxb", {"records": [{"status": "exported", "source_kind": "loose",
        "path": "menu/icon.ctxb", "resource_path": "textures/oot3d/icon", "width": 16,
        "height": 16, "format_pair": "x"}]})
    _write(tmp_path, "static_actor", {"records": [{"status": "converted", "source":
        r"C:\romfs\actor\test.zar!Model/test.cmb", "symbol": "gTest", "resource_root": "objects/test",
        "resource_count": 1, "resources": [{"kind": "DisplayList", "path": "objects/test/gTest"}]}]})
    _write(tmp_path, "skinned_model", {"records": []})
    _write(tmp_path, "csab", {"records": []})
    _write(tmp_path, "collision", {"records": []})
    _write(tmp_path, "kankyo", {"records": []})
    _write(tmp_path, "route", {"route_id": "test", "resource_targets": [{"kind": "room_mesh",
        "oot3d_source": "scene/test_0_info.zsi", "fallback": "n64"}]})

    catalog = build_asset_catalog(tmp_path)
    assert catalog["status"] == "complete"
    assert catalog["duplicate_asset_ids"] == []
    records = {record["asset_id"]: record for record in catalog["records"]}
    assert "ctxb:menu/icon.ctxb" in records
    assert "cmb:actor/test.zar!Model/test.cmb" in records
    assert records["cmb:actor/test.zar!Model/test.cmb"]["required_engine_capabilities"] == [
        "native_cmb_geometry", "native_ctxb_texture", "native_pica_material"]
    route = next(record for record in catalog["records"] if record["family"] == "route_binding")
    assert route["support_tier"] == 3
    assert "n64" not in route["asset_id"].lower()


def test_catalog_blocks_when_manifests_are_missing(tmp_path: Path) -> None:
    catalog = build_asset_catalog(tmp_path)
    assert catalog["status"] == "blocked_missing_manifests"
    assert set(catalog["missing_manifests"]) == set(MANIFESTS)


def test_catalog_rejects_unresolved_csab_model_dependency(tmp_path: Path) -> None:
    for key in MANIFESTS:
        _write(tmp_path, key, {"records": []})
    _write(tmp_path, "csab", {"records": [{"status": "exported", "archive_path": "test.zar",
        "csab_name": "Anim/idle.csab", "target_cmb_name": "Model/missing.cmb",
        "track_export": "idle.json", "target_resolution_status": "resolved"}]})

    catalog = build_asset_catalog(tmp_path)
    assert catalog["status"] == "invalid_unresolved_dependencies"
    assert catalog["unresolved_dependencies"] == ["cmb:actor/test.zar!Model/missing.cmb"]


def test_catalog_imports_packaged_native_qdb_timelines(tmp_path: Path) -> None:
    for key in MANIFESTS:
        _write(tmp_path, key, {"records": []})
    qdb_path = tmp_path / "oot3d_qdb_catalog.json"
    qdb_path.write_text(json.dumps({
        "format": "oot3d_qdb_catalog_v1",
        "status": "complete",
        "records": [{
            "asset_id": r"qdb:scene/spot00.zar!..\..\scene\Spot00\demo\opening.qdb",
            "source_domain": "scene",
            "source_archive": "scene/spot00.zar",
            "source_archive_relative": "spot00.zar",
            "source_member": r"..\..\scene\Spot00\demo\opening.qdb",
            "embedded_index": 2,
            "canonical_resource": "oot3d/native/qdb/scene/spot00.zar/0002_opening.qdb",
            "byte_length": 64,
            "sha256": "0" * 64,
            "status": "decoded_native_qdb",
            "header": {"command_count": 3, "end_frame": 120},
            "commands": [{"category": "camera_list"}, {"category": "fixed16"}],
        }],
    }), encoding="utf-8")

    catalog = build_asset_catalog(tmp_path, qdb_catalog_path=qdb_path)

    record = next(row for row in catalog["records"] if row["family"] == "cutscene_timeline")
    assert record["canonical_resources"] == [
        "oot3d/native/qdb/scene/spot00.zar/0002_opening.qdb"
    ]
    assert record["metadata"]["end_frame"] == 120
    assert record["metadata"]["command_categories"] == ["camera_list", "fixed16"]
    assert record["required_engine_capabilities"] == [
        "native_qdb_command_stream", "native_qdb_source"
    ]
