import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import regenerate_shader_corpus as regeneration
from merge_shader_packs import encode
from shader_corpus_qualification import INPUTS_FORMAT, inputs_digest

SPIRV = b"\x03\x02\x23\x07" + b"\0" * 12
VERTEX = {"source_id": "0x11", "secondary_hash": "0x12", "source_size": 13}
FRAGMENT = {"source_id": "0x21", "secondary_hash": "0x22", "source_size": 23}


def _inputs(files: dict[str, str]) -> dict:
    return {"format": INPUTS_FORMAT, "targets": ["TriAevum"], "sha256": inputs_digest(files), "files": files}


def _session(directory: Path, *, pipelines: bool = True) -> Path:
    directory.mkdir(parents=True)
    (directory / regeneration.RAW_DRAWS).write_text("raw\n", encoding="utf-8")
    (directory / regeneration.VARIANT_REQUESTS).write_text("requests\n", encoding="utf-8")
    (directory / regeneration.POLICY_INPUTS).write_text(json.dumps(_inputs({"runtime.cpp": "0" * 64})),
                                                        encoding="utf-8")
    if pipelines:
        (directory / regeneration.SESSION_PIPELINES).write_text("{}", encoding="utf-8")
    return directory


def _option(command: list[str], name: str) -> str:
    return command[command.index(name) + 1]


class VariantRecordTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_sessions_are_found_recursively(self):
        _session(self.root / "records" / "kokiri")
        _session(self.root / "records" / "boss" / "barinade")
        sessions = regeneration.variant_sessions([self.root / "records"])
        self.assertEqual([session["directory"].name for session in sessions], ["barinade", "kokiri"])

    def test_incomplete_or_missing_sessions_are_refused(self):
        _session(self.root / "partial", pipelines=False)
        with self.assertRaisesRegex(RuntimeError, "missing session-pipelines.json"):
            regeneration.variant_sessions([self.root / "partial"])
        (self.root / "text").mkdir()
        (self.root / "text" / "old-shaders.json").write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "no recorded session"):
            regeneration.variant_sessions([self.root / "text"])

    def _regenerate(self, pack_modules, **options):
        session = _session(self.root / "records" / "kokiri")
        output = self.root / "out"
        commands = []

        def fake_run(command, log, accept=(0,)):
            commands.append(command)
            name = Path(command[0]).name
            if name.startswith("oot3d_native_pica_variant_inventory"):
                Path(_option(command, "--report")).write_text(json.dumps(
                    {"sessions": [{"requests": 191}], "witness_mismatches": 0,
                     "recipes": {"input": 1, "written": 1}}), encoding="utf-8")
                Path(_option(command, "--shader-inventory")).write_text(json.dumps(
                    {"shaders": [{"stage": "fragment", **FRAGMENT}]}), encoding="utf-8")
                Path(_option(command, "--pipeline-manifest")).write_text(json.dumps(
                    {"pipeline_count": 1, "pipelines": [{"vertex_source": VERTEX, "fragment_source": FRAGMENT,
                                                         "nri_fragment_available": False}]}), encoding="utf-8")
            elif name.startswith("oot3d_native_pica_aot_compiler"):
                Path(_option(command, "--pack")).write_bytes(encode(3, {key: SPIRV for key in pack_modules}))
                Path(_option(command, "--manifest")).write_text(json.dumps(
                    {"shader_count": len(pack_modules), "descriptor_schema_version": 3}), encoding="utf-8")
            return 0.0, 0

        generator = _inputs({"generator.cpp": "1" * 64})
        with patch.object(regeneration, "_run", fake_run), \
                patch.object(regeneration, "inputs_record", lambda *arguments: generator):
            receipt = regeneration.regenerate(self.root / "tools", [], [], output, [self.root / "records"],
                                              **options)
        return session, output, commands, receipt

    def test_replay_requires_witness_and_feeds_a_qualified_union(self):
        both = [(1, 0x11, 0x12, 13), (2, 0x21, 0x22, 23)]
        session, output, commands, receipt = self._regenerate(both)
        replay = commands[0]
        self.assertIn("--require-witness", replay)
        self.assertEqual(replay[replay.index("--session") + 1:replay.index("--session") + 3],
                         [str(session / regeneration.RAW_DRAWS), str(session / regeneration.VARIANT_REQUESTS)])
        self.assertIn(str(output / "variant-shaders.json"), commands[1])
        step = receipt["steps"][0]
        self.assertEqual((step["requests"], step["witness_mismatches"]), (191, 0))
        self.assertEqual(len(step["variant_sessions"][0]["policy_inputs_sha256"]), 64)
        self.assertEqual(receipt["closure"], {"missing_modules": 0, "missing_recipe_modules": 0})
        qualification = json.loads((output / receipt["qualification"]).read_text(encoding="utf-8"))
        self.assertEqual(qualification["variant_requests"], 191)
        self.assertEqual(qualification["generator_inputs"]["files"], {"generator.cpp": "1" * 64})
        self.assertEqual(qualification["policy_inputs"][0]["files"], {"runtime.cpp": "0" * 64})
        self.assertNotIn(str(self.root), json.dumps(qualification))

    def test_compile_cache_and_jobs_reach_the_compiler(self):
        both = [(1, 0x11, 0x12, 13), (2, 0x21, 0x22, 23)]
        _, _, commands, _ = self._regenerate(both, compile_cache=self.root / "spirv", jobs=4)
        self.assertEqual(_option(commands[1], "--compile-cache"), str(self.root / "spirv"))
        self.assertEqual(_option(commands[1], "--jobs"), "4")

    def test_pack_that_misses_a_recipe_module_is_refused(self):
        with self.assertRaisesRegex(RuntimeError, "does not close"):
            self._regenerate([(2, 0x21, 0x22, 23)])


if __name__ == "__main__":
    unittest.main()
