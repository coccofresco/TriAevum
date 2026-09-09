"""Opt-in native importer tests. No game-derived fixture is distributed.

Set TRIAEVUM_CAPTURE_IMPORTER and TRIAEVUM_CAPTURE_CORPUS to a built tool and
the private corpus.json produced by recover_pica_capture_corpus.py.
"""

import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get("TRIAEVUM_CAPTURE_IMPORTER") and
                     os.environ.get("TRIAEVUM_CAPTURE_CORPUS"), "private native capture test not configured")
class CaptureInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.importer = os.environ["TRIAEVUM_CAPTURE_IMPORTER"]
        manifest = Path(os.environ["TRIAEVUM_CAPTURE_CORPUS"])
        corpus = json.loads(manifest.read_text())
        for frame in corpus["frames"]:
            resources = []
            with (manifest.parent / frame["path"]).open() as source:
                for line in source:
                    event = json.loads(line)
                    if event["event"] in ("shader_seed_program", "shader_seed_luts"):
                        resources.append(event)
                    if event["event"] == "draw_begin" and event.get("shader_seed_resources"):
                        cls.resources, cls.draw = resources, event
                        return
        raise AssertionError("private corpus has no resource-complete draw")

    def run_import(self, bodies):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            frames = []
            original = {}
            for index, body in enumerate(bodies):
                path = root / f"frame-{index}.jsonl"
                events = [{"event": "capture_begin"}, *body, {"event": "capture_end"}]
                path.write_text("".join(json.dumps(event) + "\n" for event in events))
                original[path] = path.read_bytes()
                frames.append({"path": path.name, "scenario": f"test-{index}"})
            manifest = root / "corpus.json"
            manifest.write_text(json.dumps({"format": "oot3d_pica_capture_corpus_v1", "frames": frames}))
            output = root / "inventory.json"
            result = subprocess.run([self.importer, "--manifest", str(manifest), "--output", str(output)],
                                    capture_output=True, text=True, timeout=30)
            for path, data in original.items():
                self.assertEqual(path.read_bytes(), data)
            return result, json.loads(output.read_text()) if output.exists() else None

    def test_complete_capture_uses_production_generators(self):
        result, inventory = self.run_import([[*self.resources, self.draw]])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(inventory["capture_import"]["complete_import"])
        self.assertEqual({s["stage"] for s in inventory["shaders"]}, {"vertex", "fragment", "nri_fragment"})
        self.assertEqual(len(inventory["native_pipeline_recipes"]), 1)

    def test_program_bank_recovers_earlier_hash_only_draw(self):
        old = copy.deepcopy(self.draw)
        old.pop("shader_seed_resources")
        result, inventory = self.run_import([[old], [*self.resources, self.draw]])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(inventory["capture_import"]["complete_draws"], 2)
        self.assertEqual(inventory["capture_import"]["draws_with_complete_resource_snapshots"], 1)
        self.assertTrue(inventory["native_pipeline_recipes"][0]["complete_capture_resources"])

    def test_missing_program_is_explicitly_partial(self):
        old = copy.deepcopy(self.draw)
        old.pop("shader_seed_resources")
        result, inventory = self.run_import([[old]])
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertEqual(inventory["capture_import"]["missing_vertex_draws"], 1)
        self.assertFalse(inventory["capture_import"]["complete_import"])

    def test_dynamic_luts_never_leak_across_frames(self):
        result, inventory = self.run_import([[*self.resources, self.draw], [self.draw]])
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertEqual(inventory["capture_import"]["complete_draws"], 1)
        self.assertEqual(inventory["capture_import"]["failures"], 1)
        self.assertIn("LUT snapshot", inventory["capture_import"]["frames"][1]["errors"][0]["error"])

    def test_conflicting_program_payload_is_rejected(self):
        program = copy.deepcopy(next(r for r in self.resources if r["event"] == "shader_seed_program"))
        word = program["program"][0]
        program["program"][0] = (int(word, 0) if isinstance(word, str) else word) ^ 1
        result, inventory = self.run_import([[*self.resources, self.draw], [program, self.draw]])
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("identity collision", result.stderr)
        self.assertIsNone(inventory)


if __name__ == "__main__":
    unittest.main()
