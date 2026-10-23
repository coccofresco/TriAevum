import hashlib
import unittest

from shader_corpus_qualification import (INPUTS_FORMAT, POLICY_SELECTION, changed_inputs, closure_failures,
                                         inputs_digest, is_policy_input, policy_inputs, qualification_record,
                                         validate_qualification)


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _inputs(files: dict[str, bytes]) -> dict:
    manifest = {path: _sha(data) for path, data in files.items()}
    return {"format": INPUTS_FORMAT, "targets": ["t"], "sha256": inputs_digest(manifest), "files": manifest}


class QualificationTests(unittest.TestCase):
    def setUp(self):
        self.sources = {"gen/shader.cpp": b"generator", "runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_draw.cpp": b"policy"}
        self.record = qualification_record(
            generator=_inputs({"gen/shader.cpp": b"generator"}),
            policies=[_inputs({"runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_draw.cpp": b"policy"})],
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
        del self.sources["runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_draw.cpp"]
        with self.assertRaisesRegex(ValueError, "Stale variant recordings.*runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_draw.cpp"):
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

    def test_policy_inputs_keep_what_decides_a_variant_request(self):
        generator = _inputs({"runtime/three_ds_recomp/include/fast/oot3d/graphics_settings.h": b"settings"})
        recorded = _inputs({
            "runtime/three_ds_recomp/include/fast/oot3d/graphics_settings.h": b"settings",
            "runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_draw.cpp": b"request",
            "runtime/three_ds_recomp/src/fast/oot3d/pica_scene_semantics.cpp": b"semantics",
            "runtime/three_ds_recomp/src/fast/renderer3ds/vulkan_memory_budget.cpp": b"budget",
            "runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_device.cpp": b"device",
            "runtime/three_ds_recomp/src/ship/window/Window.cpp": b"window",
            "tools/oot3d/native_game_runtime/oot3d_native_audio.cpp": b"audio",
        })
        selected = policy_inputs(recorded, generator)
        self.assertEqual(sorted(selected["files"]), [
            "runtime/three_ds_recomp/include/fast/oot3d/graphics_settings.h",
            "runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_draw.cpp",
            "runtime/three_ds_recomp/src/fast/oot3d/pica_scene_semantics.cpp"])
        self.assertEqual((selected["selection"], selected["recorded_files"], selected["sha256"]),
                         (POLICY_SELECTION, 7, inputs_digest(selected["files"])))
        self.assertFalse(is_policy_input("runtime/three_ds_recomp/include/fast/renderer3ds/vulkan_commands.h", set()))
        self.assertTrue(is_policy_input("tools/oot3d/native_pica_frontend/oot3d_native_pica_shader_gen.cpp", set()))

    def test_qualification_keeps_the_policy_subset_of_each_recording(self):
        record = qualification_record(
            generator=_inputs({"gen/shader.cpp": b"generator"}),
            policies=[_inputs({"runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_draw.cpp": b"request",
                               "runtime/three_ds_recomp/src/fast/renderer3ds/vulkan_memory_budget.cpp": b"budget"})],
            pack_sha256="a" * 64, manifest_sha256=[], modules=1, requests=1)
        self.assertEqual(list(record["policy_inputs"][0]["files"]),
                         ["runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_draw.cpp"])
        sources = {"gen/shader.cpp": b"generator",
                   "runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_draw.cpp": b"request"}
        # A changed file outside the policy set does not stale the recordings.
        validate_qualification(record, pack_sha256="a" * 64, manifest_sha256=[], read=sources.get)
        sources["runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica_draw.cpp"] = b"request v2"
        with self.assertRaisesRegex(ValueError, "Stale variant recordings.*gfx_vulkan_pica_draw"):
            validate_qualification(record, pack_sha256="a" * 64, manifest_sha256=[], read=sources.get)
        with self.assertRaisesRegex(ValueError, "no decision-policy file"):
            qualification_record(generator=_inputs({"gen/shader.cpp": b"generator"}),
                                 policies=[_inputs({"runtime/three_ds_recomp/src/ship/window/Window.cpp": b"w"})],
                                 pack_sha256="a" * 64, manifest_sha256=[], modules=1, requests=1)

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
