import json
import tempfile
import unittest
from pathlib import Path

from bind_private_shader_corpus import bind
from common import atomic_write_json
from precompiled_title_layout import artifact
from precompiled_titles import CATALOG, checked_file
from shader_preparation import FORMAT as SEED_FORMAT
from device_pipeline_preparation import FORMAT as DEVICE_FORMAT


class PrivateCorpusTests(unittest.TestCase):
    def fixture(self, root):
        package, evidence = root / "package", root / "evidence"
        inputs = evidence / "preparation-inputs"
        inputs.mkdir(parents=True)
        def record(name):
            path = inputs / name
            path.write_bytes(name.encode())
            return artifact(path, name)
        title = {"shader_preparation": {"format": SEED_FORMAT,
                 "mode": "source_inventories", "compiler": record("compiler"),
                 "inventories": [record("inventory.json")], "descriptor_schema_version": 3},
                 "device_pipeline_preparation": {"format": DEVICE_FORMAT,
                 "helper": record("helper"), "manifests": [record("pipelines.json")]}}
        atomic_write_json(evidence / "title-record.json", title)
        atomic_write_json(package / CATALOG, {"titles": [
            {"recipe": "eur"}, {"recipe": "usa"}]})
        return package, evidence

    def test_binding_is_persistent_revision_specific_and_idempotent(self):
        with tempfile.TemporaryDirectory() as tmp:
            package, evidence = self.fixture(Path(tmp))
            first = bind(package, evidence, "eur")
            self.assertEqual(first, bind(package, evidence, "eur"))
            catalog = json.loads((package / CATALOG).read_text())
            title, other = catalog["titles"]
            self.assertEqual(other, {"recipe": "usa"})
            self.assertFalse(first["public_distribution"])
            for item in [title["shader_preparation"]["compiler"],
                         *title["shader_preparation"]["inventories"],
                         title["device_pipeline_preparation"]["helper"],
                         *title["device_pipeline_preparation"]["manifests"]]:
                checked_file(package, item)

    def test_current_packaged_compiler_is_used(self):
        with tempfile.TemporaryDirectory() as tmp:
            package, evidence = self.fixture(Path(tmp))
            compiler = package / "compiler-current"
            compiler.write_bytes(b"current compiler")
            catalog = json.loads((package / CATALOG).read_text())
            renderer = {"compiler": artifact(compiler, compiler.name), "dependencies": []}
            catalog["titles"][0]["renderer_shader_preparation"] = renderer
            atomic_write_json(package / CATALOG, catalog)
            bind(package, evidence, "eur")
            title = json.loads((package / CATALOG).read_text())["titles"][0]
            self.assertEqual(title["renderer_shader_preparation"], renderer)
            self.assertEqual(title["shader_preparation"]["compiler"], renderer["compiler"])

    def test_corrupt_evidence_does_not_change_catalog(self):
        with tempfile.TemporaryDirectory() as tmp:
            package, evidence = self.fixture(Path(tmp))
            before = (package / CATALOG).read_bytes()
            (evidence / "preparation-inputs/inventory.json").write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "integrity"):
                bind(package, evidence, "eur")
            self.assertEqual(before, (package / CATALOG).read_bytes())

    def test_unknown_revision_does_not_change_catalog(self):
        with tempfile.TemporaryDirectory() as tmp:
            package, evidence = self.fixture(Path(tmp))
            before = (package / CATALOG).read_bytes()
            with self.assertRaisesRegex(ValueError, "recipe"):
                bind(package, evidence, "unknown")
            self.assertEqual(before, (package / CATALOG).read_bytes())

    def test_portable_pack_uses_the_same_forge_seed_service(self):
        from shader_preparation import prepare_shader_seed
        from test_shader_preparation import synthetic_pack
        with tempfile.TemporaryDirectory() as tmp:
            package, evidence = self.fixture(Path(tmp))
            pack = evidence / "preparation-inputs/portable.o3ps"
            pack.write_bytes(synthetic_pack())
            record = json.loads((evidence / "title-record.json").read_text())
            record["shader_preparation"] = {"format": SEED_FORMAT,
                "mode": "portable_pack", "descriptor_schema_version": 3,
                "pack": artifact(pack, pack.name)}
            atomic_write_json(evidence / "title-record.json", record)
            bind(package, evidence, "eur")
            title = json.loads((package / CATALOG).read_text())["titles"][0]
            prepared = prepare_shader_seed(root=package, data_root=package / "data", title=title)
            self.assertEqual(prepared.read_bytes(), pack.read_bytes())
            stamp = prepared.stat().st_mtime_ns
            self.assertEqual(prepared, prepare_shader_seed(root=package, data_root=package / "data", title=title))
            self.assertEqual(stamp, prepared.stat().st_mtime_ns)


if __name__ == "__main__":
    unittest.main()
