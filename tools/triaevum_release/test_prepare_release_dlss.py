import tempfile
import unittest
from pathlib import Path

from prepare_release import dlss_layout


class DlssLayoutTests(unittest.TestCase):
    """The DLSS module, NVIDIA's DLSS runtime and NVIDIA's SDK licence are
    published together or not at all."""

    def test_publishes_module_runtime_and_licence_together(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            targets = {}
            for key, name in (("dlss", "triaevum_dlss.dll"), ("dlss_runtime", "nvngx_dlss.dll"),
                              ("dlss_license", "LICENSE.txt")):
                (root / name).write_bytes(b"fixture")
                targets[key] = str(root / name)
            layout = dlss_layout(targets)
            self.assertEqual({item["path"]: item["role"] for item in layout}, {
                "triaevum_dlss.dll": "separately_licensed_module",
                "nvngx_dlss.dll": "separately_licensed_module",
                "LICENSES/NVIDIA-DLSS-SDK-LICENSE.txt": "license",
            })

    def test_without_the_module_nothing_is_published(self):
        self.assertEqual(dlss_layout({"runtime": "TriAevum.exe"}), [])

    def test_refuses_a_partial_set(self):
        with tempfile.TemporaryDirectory() as temporary:
            module = Path(temporary) / "triaevum_dlss.dll"
            module.write_bytes(b"fixture")
            with self.assertRaisesRegex(ValueError, "published together"):
                dlss_layout({"dlss": str(module), "dlss_runtime": str(module)})


if __name__ == "__main__":
    unittest.main()
