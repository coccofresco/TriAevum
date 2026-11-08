#!/usr/bin/env python3
"""Unit tests for check_registry_boundaries.py on synthetic site tables and registries."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

CHECK = Path(__file__).resolve().parent / "check_registry_boundaries.py"
SITES = '{0x00134de4U,0xe5940028U,0xeffffa40U,"button_emission",false},\n'
BOTH = ('{0x00134DE4U, 0x00134FD8U, "mm3d_guest_patch_00134DE4"},\n'
        '{0x00134DE8U, 0x00134FD8U, "mm3d_guest_patch_00134DE8"},\n')


def run(sites: str, registry: str) -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory() as tmp:
        site_path, registry_path = Path(tmp, "sites.h"), Path(tmp, "registry.cpp")
        site_path.write_text(sites)
        registry_path.write_text(registry)
        return subprocess.run([sys.executable, str(CHECK), "--sites", str(site_path),
                               "--registry", str(registry_path)],
                              capture_output=True, text=True)


class RegistryBoundaryTest(unittest.TestCase):
    def test_accepts_registry_with_both_boundaries(self):
        self.assertEqual(run(SITES, BOTH).returncode, 0)

    def test_refuses_registry_missing_the_successor(self):
        result = run(SITES, BOTH.splitlines()[0] + "\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("0x00134DE8", result.stderr)

    def test_refuses_empty_site_table(self):
        self.assertEqual(run("", BOTH).returncode, 1)


if __name__ == "__main__":
    unittest.main()
