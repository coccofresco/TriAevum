from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path

from oot3d_asset_tool.playable_coverage import (
    FORMAT,
    INPUT_SPECS,
    build_playable_coverage_baseline,
    render_markdown,
)


def _write_inputs(root: Path) -> None:
    values = {
        "romfs_inventory": {"model_counts": {"parsed": 20, "rigid_export_candidate": 18}},
        "scene_metadata": {"room_count_total": 10, "scene_stem_count": 8, "collision_candidate_total": 6},
        "material_inventory": {
            "material_count": 100,
            "raw_texture_stage_export_gap_material_count": 20,
            "raw_texture_stage_export_classification_counts": {
                "covered_by_current_export": 79,
                "covered_by_baked_extra_texture_stage": 1,
            },
        },
        "ctxb_audit": {"ctxb_count": 12, "parsed_ctxb_count": 12},
        "ctxb_export": {"exported_count": 12},
        "actor_inventory": {
            "cmb_counts": {"parsed": 7},
            "animation_file_counts": {"csab": 9, "cmab": 4},
        },
        "static_actor_package": {"converted": 5},
        "skinned_bind_pose": {"counts": {"exported": 2}},
        "skinned_animation": {"exported": 8},
        "collision_package": {"accepted_scene_count": 5},
        "kankyo_audit": {},
        "kankyo_package": {
            "source_audit_summary": {"cmb_counts": {"parsed": 6}},
            "converted": 6,
        },
        "audio_audit": {"file_count": 3},
        "route_manifest": {"resource_target_kind_counts": {"room_mesh": 2, "scene_collision": 1}},
        "route_static_actors": {"visual_actor_entry_count": 6, "ready_static_actor_entry_count": 4},
        "route_kankyo": {"ready_environment_record_count": 3},
        "route_audio": {
            "playback_status": "native_bcsar_bcstm_runtime",
            "ready_runtime_record_count": 7,
        },
        "link_child": {
            "csab_animation_count": 3,
            "cmab_count": 2,
            "native_bind_pose_resource_count": 10,
        },
    }
    for spec in INPUT_SPECS:
        path = root / spec.relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(values[spec.key]), encoding="utf-8")


def test_coverage_tiers_do_not_promote_exports_to_playable(tmp_path: Path) -> None:
    _write_inputs(tmp_path)
    report = build_playable_coverage_baseline(
        tmp_path, now=datetime(2026, 7, 12, tzinfo=timezone.utc), max_age_days=99999
    )
    assert report["format"] == FORMAT
    assert report["status"] == "complete"
    families = {family["family_id"]: family for family in report["families"]}
    assert families["ctxb_textures"]["tier_counts"] == {
        "inventoried": 12,
        "decoded": 12,
        "packaged": 12,
        "runtime_candidate": 0,
        "playable": 0,
        "parity_validated": 0,
    }
    assert families["audio_containers"]["tier_counts"]["runtime_candidate"] == 3
    assert families["scene_rooms"]["tier_counts"]["runtime_candidate"] == 2
    assert families["scene_rooms"]["tier_counts"]["playable"] == 0
    assert report["opportunities"][0]["opportunity_id"] == "global_asset_catalog"
    assert report["opportunities"][0]["priority_score"] >= report["opportunities"][-1]["priority_score"]
    markdown = render_markdown(report)
    assert "Highest-Yield Opportunities" in markdown
    assert "`scene_rooms`" in markdown


def test_coverage_reports_missing_required_inputs(tmp_path: Path) -> None:
    report = build_playable_coverage_baseline(tmp_path)
    assert report["status"] == "blocked_missing_required_inputs"
    assert len(report["missing_required_inputs"]) == len(INPUT_SPECS)
    assert report["families"] == []
