import tempfile
import unittest
from pathlib import Path

from common import atomic_write_json, sha256_file
from plan_windows_toolchain import create_plan


class ToolchainPlanTests(unittest.TestCase):
    def test_selects_dependencies_without_compiler_or_debugger(self):
        payload = {"url": "https://download.visualstudio.microsoft.com/fixture",
                   "sha256": "a" * 64, "size": 1}
        def package(identifier, names):
            return {"id": identifier, "version": "test", "payloads": [
                {**payload, "fileName": name} for name in names]}
        with tempfile.TemporaryDirectory() as temporary:
            catalog = Path(temporary) / "catalog.json"
            atomic_write_json(catalog, {"packages": [
                package("Microsoft.VC.14.44.17.14.CRT.Headers.base", ["headers.vsix"]),
                package("Microsoft.VC.14.44.17.14.CRT.x64.Desktop.base", ["libs.vsix"]),
                package("Win11SDK_10.0.26100", [
                    "Installers\\Universal CRT Headers Libraries and Sources-x86_en-us.msi",
                    "Installers\\Windows SDK Desktop Libs x64-x86_en-us.msi",
                    "Installers\\Windows SDK for Windows Store Apps Headers-x86_en-us.msi",
                    "Installers\\Windows SDK for Windows Store Apps Libs-x86_en-us.msi",
                    "Installers\\Windows SDK Desktop Headers arm64-x86_en-us.msi",
                    "Installers\\debugger.msi", "Installers\\tools.cab", "setup.exe"]),
            ]})
            plan = create_plan(catalog, sha256_file(catalog), "14.44.17.14", "26100")
            self.assertEqual(len(plan["payloads"]), 6)
            self.assertEqual(list(plan["sdk_cabinets"]), ["tools.cab"])
            self.assertTrue(plan["license_acceptance_required"])
            with self.assertRaisesRegex(ValueError, "pinned SHA"):
                create_plan(catalog, "b" * 64, "14.44.17.14", "26100")
