import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from common import atomic_write_json, load_json_object, sha256_file
from import_local_vs_plan import import_plan


class LocalMetadataTests(unittest.TestCase):
    def test_preserves_metadata_provenance_and_uses_normal_planner(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "_package.json"
            package = {"id": "fixture", "version": "test"}
            atomic_write_json(path, package)
            def planner(catalog, digest, version, sdk):
                self.assertEqual(load_json_object(catalog), {"packages": [package]})
                self.assertEqual(sha256_file(catalog), digest)
                self.assertEqual((version, sdk), ("14.44.17.14", "26100"))
                return {"catalog_sha256": digest}
            with patch("import_local_vs_plan.create_plan", side_effect=planner):
                result = import_plan([path], msvc_package_version="14.44.17.14", sdk_build="26100")
            self.assertEqual(result["metadata_source"], "installed_visual_studio_private_bootstrap")
            self.assertEqual(result["installed_metadata"],
                             [{"path": str(path.resolve()), "sha256": sha256_file(path)}])
