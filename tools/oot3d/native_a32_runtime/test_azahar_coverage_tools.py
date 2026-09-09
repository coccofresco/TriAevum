#!/usr/bin/env python3
"""Focused tests for the repeatable Azahar coverage corpus."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools.oot3d.native_a32_runtime.summarize_azahar_shader_coverage import (
    IDENTITY_FORMAT,
    OUTPUT_FORMAT,
    summarize,
)
from tools.oot3d.native_a32_runtime.select_azahar_pipeline_cover import select_cover


ROOT = Path(__file__).resolve().parent
CATALOG_PATH = ROOT / "oot3d_azahar_coverage_scenarios.json"


def shader_identity(fragment_hash: str, *, geometry: bool = False) -> dict[str, object]:
    return {
        "format": IDENTITY_FORMAT,
        "vertex": {
            "program_hash": "0x1111",
            "swizzle_hash": "0x2222",
            "entry_point": 3,
            "program_words": 64,
            "swizzle_words": 8,
        },
        "geometry": {
            "enabled": geometry,
            "program_hash": "0x3333",
            "swizzle_hash": "0x4444",
            "entry_point": 5,
            "program_words": 32,
            "swizzle_words": 4,
        },
        "fragment_config_hash": fragment_hash,
    }


class AzaharCoverageCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))

    def test_catalog_is_unique_and_covers_every_decomp_scene(self) -> None:
        catalog = self.catalog
        scenarios = catalog["scenarios"]
        scenario_ids = [scenario["id"] for scenario in scenarios]
        scene_ids = {int(scenario["scene_id"]) for scenario in scenarios}

        self.assertEqual(catalog["format"], "oot3d_azahar_coverage_catalog_v1")
        self.assertEqual(len(scenario_ids), len(set(scenario_ids)))
        self.assertEqual(len(scenarios), catalog["summary"]["total_scenario_count"])
        self.assertEqual(scene_ids, set(range(catalog["summary"]["scene_source_count"])))
        self.assertEqual(catalog["summary"]["covered_scene_count"], 111)

    def test_selection_tags_have_expected_structural_surface(self) -> None:
        scenarios = self.catalog["scenarios"]

        def count(tag: str) -> int:
            return sum(tag in scenario["selection_tags"] for scenario in scenarios)

        self.assertEqual(count("smoke"), 3)
        self.assertEqual(count("scene_representative"), 111)
        self.assertEqual(count("local_entrance_representative"), 400)
        self.assertEqual(count("setup_variant"), 110)
        self.assertEqual(count("exhaustive"), 1582)
        representatives = [
            scenario
            for scenario in scenarios
            if "scene_representative" in scenario["selection_tags"]
        ]
        self.assertEqual(
            sum(scenario["runtime_availability"]["launchable"] for scenario in representatives),
            102,
        )
        self.assertEqual(self.catalog["summary"]["catalog_only_scene_count"], 9)

    def test_runtime_contract_uses_proven_native_addresses(self) -> None:
        runtime = self.catalog["runtime_contract"]
        self.assertEqual(runtime["player_update_function"], 0x001E1B54)
        self.assertEqual(runtime["game_state_update_function"], 0x00417014)
        self.assertEqual(runtime["play_init_function"], 0x00449440)
        self.assertEqual(runtime["play_main_function"], 0x004523AC)
        self.assertEqual(runtime["request_transition_function"], 0x003348E8)
        self.assertEqual(runtime["prepare_transition_effect_function"], 0x0035DA3C)
        self.assertEqual(runtime["entrance_table_address"], 0x00543BB8)
        self.assertEqual(runtime["transition_trigger"], 0x14)


class AzaharShaderCoverageTests(unittest.TestCase):
    def write_capture(self, directory: Path, *, complete: bool = True) -> Path:
        trace_path = directory / "frame.jsonl"
        events = [
            {
                "event": "draw_begin",
                "mode": "arrays",
                "triangle_topology": 1,
                "shader_identity": shader_identity("0xAAAA"),
                "textures": [{"enabled": 1, "format": 4, "type": 2}],
                "shadow_summary": {"lighting_enable_shadow": 0},
            },
            {
                "event": "draw_begin",
                "mode": "arrays",
                "triangle_topology": 1,
                "shader_identity": shader_identity("0xBBBB", geometry=True),
                "textures": [{"enabled": 1, "format": 4, "type": 2}],
                "shadow_summary": {"lighting_enable_shadow": 1},
            },
        ]
        if complete:
            events.append({"event": "capture_end"})
        trace_path.write_text(
            "".join(json.dumps(event) + "\n" for event in events), encoding="utf-8"
        )
        summary_path = directory / "coverage_summary.json"
        summary_path.write_text(
            json.dumps(
                {
                    "format": "oot3d_azahar_coverage_capture_v1",
                    "scenario": {"id": "synthetic", "scene_id": 7, "entrance_index": 9},
                    "backend": "vulkan",
                    "pica_frames": [str(trace_path)],
                }
            ),
            encoding="utf-8",
        )
        return summary_path

    def test_summarizer_deduplicates_native_shader_and_pipeline_identities(self) -> None:
        with tempfile.TemporaryDirectory() as raw_directory:
            document = summarize(self.write_capture(Path(raw_directory)))

        self.assertEqual(document["format"], OUTPUT_FORMAT)
        self.assertEqual(document["counts"]["frames"], 1)
        self.assertEqual(document["counts"]["draws"], 2)
        self.assertEqual(document["counts"]["unique_vertex_programs"], 1)
        self.assertEqual(document["counts"]["unique_geometry_programs"], 1)
        self.assertEqual(document["counts"]["unique_fragment_configs"], 2)
        self.assertEqual(document["counts"]["unique_pipelines"], 2)
        self.assertEqual(document["counts"]["unique_enabled_texture_format_types"], 1)
        self.assertEqual(document["counts"]["unique_shadow_states"], 2)

    def test_summarizer_rejects_an_incomplete_capture(self) -> None:
        with tempfile.TemporaryDirectory() as raw_directory:
            summary_path = self.write_capture(Path(raw_directory), complete=False)
            with self.assertRaisesRegex(ValueError, "lacks capture_end"):
                summarize(summary_path)

    def test_shader_seed_requires_actual_resources_not_only_the_launcher_flag(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = self.write_capture(Path(temporary))
            summary = json.loads(path.read_text())
            summary["shader_seed_capture"] = True
            path.write_text(json.dumps(summary))
            with self.assertRaisesRegex(ValueError, "lacks resources"):
                summarize(path)
            frame = Path(summary["pica_frames"][0])
            events = [json.loads(line) for line in frame.read_text().splitlines()]
            for event in events:
                if event["event"] == "draw_begin":
                    event["shader_seed_resources"] = True
            frame.write_text("".join(json.dumps(e) + "\n" for e in events))
            with self.assertRaisesRegex(ValueError, "lacks resources"):
                summarize(path)
            events = [{"event": "shader_seed_program"}, {"event": "shader_seed_luts"}, *events]
            frame.write_text("".join(json.dumps(e) + "\n" for e in events))
            report = summarize(path)
            self.assertEqual(report["counts"]["shader_seed_draws"], 2)
            self.assertEqual(report["counts"]["program_payloads"], 1)
            self.assertEqual(report["counts"]["lut_snapshots"], 1)


class AzaharPipelineCoverTests(unittest.TestCase):
    def test_greedy_cover_is_deterministic_and_preserves_failed_scenarios(self) -> None:
        matrix = {
            "format": "oot3d_azahar_coverage_matrix_v1",
            "results": [
                {
                    "scenario_id": "first",
                    "status": "passed",
                    "scene_id": 1,
                    "entrance_index": 10,
                    "pipeline_ids": ["p1", "p2"],
                    "texture_format_ids": ["t1"],
                    "shadow_state_ids": ["s1"],
                },
                {
                    "scenario_id": "second",
                    "status": "passed",
                    "scene_id": 2,
                    "entrance_index": 20,
                    "pipeline_ids": ["p2", "p3"],
                    "texture_format_ids": ["t1"],
                    "shadow_state_ids": ["s1"],
                },
                {
                    "scenario_id": "failed",
                    "status": "failed",
                    "scene_id": 3,
                    "entrance_index": 30,
                    "pipeline_ids": [],
                    "texture_format_ids": [],
                    "shadow_state_ids": [],
                },
                {
                    "scenario_id": "unavailable",
                    "status": "unavailable",
                    "scene_id": 4,
                    "entrance_index": 40,
                    "pipeline_ids": [],
                    "texture_format_ids": [],
                    "shadow_state_ids": [],
                },
            ],
        }
        document = select_cover(matrix, ("pipeline", "texture", "shadow"))

        self.assertEqual(document["scenario_ids"], ["first", "second"])
        self.assertEqual(document["cover_counts"]["selected_scenarios"], 2)
        self.assertEqual(document["cover_counts"]["uncovered_identity_tokens"], 0)
        self.assertEqual(document["failed_scenario_ids"], ["failed"])
        self.assertEqual(document["unavailable_scenario_ids"], ["unavailable"])

    def test_cover_uses_the_latest_result_for_a_retried_scenario(self) -> None:
        base = {
            "scenario_id": "retried",
            "scene_id": 4,
            "entrance_index": 40,
            "texture_format_ids": [],
            "shadow_state_ids": [],
        }
        matrix = {
            "format": "oot3d_azahar_coverage_matrix_v1",
            "results": [
                {**base, "status": "failed", "pipeline_ids": []},
                {**base, "status": "passed", "pipeline_ids": ["p4"]},
            ],
        }
        document = select_cover(matrix, ("pipeline",))

        self.assertEqual(document["scenario_ids"], ["retried"])
        self.assertEqual(document["failed_scenario_ids"], [])


if __name__ == "__main__":
    unittest.main()
