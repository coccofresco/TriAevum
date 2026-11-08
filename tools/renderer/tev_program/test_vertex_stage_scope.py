"""Offline publisher stage-scope gates; no ROM or GPU is required."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get("TRIAEVUM_VERTEX_ARTIFACT_BUILDER"), "publisher tool not configured")
class VertexStageScopeTests(unittest.TestCase):
    def run_stage(self, stage, *flags):
        # One empty DVLP and one DVLE. Stage admission must precede translation.
        data = bytearray(84)
        struct.pack_into("<4sII", data, 0, b"DVLB", 1, 36)
        struct.pack_into("<4s", data, 12, b"DVLP")
        struct.pack_into("<4s", data, 36, b"DVLE")
        data[42] = stage
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / "stage.shbin", root / "result.spvpack"
            source.write_bytes(data)
            result = subprocess.run([os.environ["TRIAEVUM_VERTEX_ARTIFACT_BUILDER"],
                                     str(output), *flags, str(source)],
                                    capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())
            return result.stdout + result.stderr

    def test_geometry_still_rejected_by_default(self):
        self.assertIn("not a vertex DVLE", self.run_stage(1))

    def test_scoping_does_not_publish_an_empty_vertex_family(self):
        text = self.run_stage(1, "--vertex-only")
        self.assertIn("untranslated_geometry_dvle", text)
        self.assertIn("no vertex DVLE was compiled", text)

    def test_unknown_stage_is_never_silently_skipped(self):
        self.assertIn("unknown DVLE stage", self.run_stage(2, "--vertex-only"))

    def test_scoped_check_requires_explicit_publisher_pack(self):
        env = dict(os.environ)
        env.pop("TRIAEVUM_TITLE_PROGRAM_PACK", None)
        result = subprocess.run([os.environ["TRIAEVUM_VERTEX_ARTIFACT_BUILDER"],
                                 "--check", "--vertex-only"], env=env,
                                capture_output=True, text=True, timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires TRIAEVUM_TITLE_PROGRAM_PACK", result.stderr)


if __name__ == "__main__":
    unittest.main()
