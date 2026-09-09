import json
from pathlib import Path
import tempfile
import unittest

from tools.oot3d.native_a32_runtime.recover_pica_capture_corpus import compact_frame, recover


class RecoveryTests(unittest.TestCase):
    def test_compacts_resources_without_changing_order(self):
        events = [{"event": name} for name in
                  ("capture_begin", "shader_seed_program", "shader_seed_luts", "draw_begin",
                   "output_vertex", "register_write", "capture_end")]
        events[3]["shader_seed_resources"] = True
        with tempfile.TemporaryDirectory() as temporary:
            source, output = Path(temporary) / "source", Path(temporary) / "output"
            source.write_text("".join(json.dumps(e) + "\n" for e in events))
            before = source.read_bytes()
            info = compact_frame(source, output)
            self.assertEqual(info["draws"], 1)
            self.assertEqual(info["seeded_draws"], 1)
            self.assertEqual(info["program_payloads"], 1)
            self.assertEqual(info["lut_snapshots"], 1)
            self.assertEqual(source.read_bytes(), before)
            actual = [json.loads(line) for line in output.read_text().splitlines()]
            self.assertEqual(actual, events[:4] + events[-1:])

    def test_rejects_incomplete_and_empty_capture(self):
        with tempfile.TemporaryDirectory() as temporary:
            source, output = Path(temporary) / "source", Path(temporary) / "output"
            for names in ([], ["draw_begin"], ["capture_begin", "draw_begin"],
                          ["capture_begin", "capture_end"],
                          ["capture_begin", "draw_begin", "capture_end", "draw_begin"]):
                source.write_text("".join(json.dumps({"event": n}) + "\n" for n in names))
                with self.subTest(names=names), self.assertRaises(ValueError):
                    compact_frame(source, output)

    def test_recovery_is_relocatable_and_repeatable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            frame = root / "capture.jsonl"
            frame.write_text("".join(json.dumps({"event": n}) + "\n" for n in
                                     ("capture_begin", "draw_begin", "capture_end")))
            summary = root / "summary.json"
            summary.write_text(json.dumps({"scenario_status": {"status": "ready", "scenario_id": "test"},
                                           "pica_frames": [str(frame)]}))
            first = recover([], [summary, summary], root / "corpus")
            second = recover([], [summary], root / "corpus")
            self.assertEqual(first, second)
            self.assertEqual(first["counts"]["frames"], 1)
            self.assertEqual(first["counts"]["seeded_draws"], 0)
            self.assertFalse(Path(first["frames"][0]["path"]).is_absolute())

    def test_empty_frame_is_skipped_but_truncated_frame_is_an_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            empty, truncated = root / "empty.jsonl", root / "truncated.jsonl"
            empty.write_text('{"event":"capture_begin"}\n{"event":"capture_end"}\n')
            truncated.write_text('{"event":"capture_begin"}\n')
            summary = root / "summary.json"
            summary.write_text(json.dumps({"scenario_status": {"status": "ready", "scenario_id": "test"},
                                           "pica_frames": [str(empty), str(truncated)]}))
            result = recover([], [summary], root / "corpus")
            self.assertEqual(result["counts"]["frames"], 0)
            self.assertEqual(len(result["skipped_frames"]), 1)
            self.assertEqual(len(result["errors"]), 1)


if __name__ == "__main__":
    unittest.main()
