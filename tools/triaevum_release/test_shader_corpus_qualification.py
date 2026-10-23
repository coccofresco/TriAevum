import hashlib
import unittest

from shader_corpus_qualification import (INPUTS_FORMAT, changed_inputs, closure_failures, inputs_digest,
                                         qualification_record, validate_qualification)


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _inputs(files: dict[str, bytes]) -> dict:
    manifest = {path: _sha(data) for path, data in files.items()}
    return {"format": INPUTS_FORMAT, "targets": ["t"], "sha256": inputs_digest(manifest), "files": manifest}


class QualificationTests(unittest.TestCase):
    def setUp(self):
        self.sources = {"gen/shader.cpp": b"generator", "runtime/draw.cpp": b"policy"}
        self.record = qualification_record(
            generator=_inputs({"gen/shader.cpp": b"generator"}),
            policies=[_inputs({"runtime/draw.cpp": b"policy"})],
            pack_sha256="a" * 64, manifest_sha256=["c" * 64, "b" * 64], modules=3, requests=9)

    def validate(self, **overrides):
        arguments = dict(pack_sha256="a" * 64, manifest_sha256=["b" * 64, "c" * 64],
                         read=lambda path: self.sources.get(path))
        arguments.update(overrides)
        validate_qualification(self.record, **arguments)

    def test_current_sources_qualify(self):
        self.validate()

    def test_changed_generator_input_requires_regeneration(self):
        self.sources["gen/shader.cpp"] = b"generator v2"
        with self.assertRaisesRegex(ValueError, "Stale shader corpus.*gen/shader.cpp"):
            self.validate()

    def test_changed_runtime_input_requires_new_recordings(self):
        del self.sources["runtime/draw.cpp"]
        with self.assertRaisesRegex(ValueError, "Stale variant recordings.*runtime/draw.cpp"):
            self.validate()

    def test_other_artifacts_are_refused(self):
        with self.assertRaisesRegex(ValueError, "differs from its qualification"):
            self.validate(pack_sha256="d" * 64)
        with self.assertRaisesRegex(ValueError, "Pipeline recipes differ"):
            self.validate(manifest_sha256=["b" * 64])
        with self.assertRaisesRegex(ValueError, "no qualification record"):
            validate_qualification({}, pack_sha256="a" * 64, manifest_sha256=[], read=lambda path: None)

    def test_tampered_input_record_is_refused(self):
        self.record["generator_inputs"]["files"]["gen/other.cpp"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "Invalid generator input record"):
            self.validate()

    def test_corpus_without_recorded_sessions_is_not_qualified(self):
        with self.assertRaisesRegex(ValueError, "recorded variant sessions"):
            qualification_record(generator=_inputs({"a": b"a"}), policies=[], pack_sha256="a" * 64,
                                 manifest_sha256=[], modules=1, requests=0)

    def test_changed_inputs_reports_missing_and_modified(self):
        manifest = {"a": _sha(b"a"), "b": _sha(b"b"), "c": _sha(b"c")}
        self.assertEqual(changed_inputs(manifest, {"a": b"a", "b": b"B"}.get), ["b", "c"])

    def test_closure_counts_missing_modules(self):
        inventory = {"shaders": [{"stage": "nri_fragment", "source_id": "0x1", "secondary_hash": "0x2",
                                  "source_size": 3}]}
        source = {"source_id": "0x4", "secondary_hash": "0x5", "source_size": 6}
        manifest = {"pipelines": [{"vertex_source": source, "fragment_source": source,
                                   "nri_fragment_available": True, "nri_fragment_source": source}]}
        self.assertEqual(closure_failures({(3, 1, 2, 3), (1, 4, 5, 6)}, [inventory], [manifest]),
                         {"missing_modules": 0, "missing_recipe_modules": 2})


if __name__ == "__main__":
    unittest.main()
