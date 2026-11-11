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


LIGHTING = ("Mm3dLightingHookSite{0x00110D88U, {0xE320F000U}},\n"
            "Mm3dLightingHookSite{0x0011EF3CU, {0xE5940004U}},\n")
SHARD = ("a32::Block kShard0000Blocks[] = {\n    {0x00110D88U, kShard0000Ops + 0U, 1U},\n"
         "    {0x0011EF3CU, kShard0000Ops + 1U, 1U},\n};\n")


def run(sites: str, registry: str, lighting=None, shard: str = "") -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory() as tmp:
        site_path, registry_path = Path(tmp, "sites.h"), Path(tmp, "registry.cpp")
        site_path.write_text(sites)
        registry_path.write_text(registry)
        Path(tmp, "shard_0000.cpp").write_text(shard)
        extra = []
        if lighting is not None:
            Path(tmp, "lighting.h").write_text(lighting)
            extra = ["--lighting-sites", str(Path(tmp, "lighting.h"))]
        return subprocess.run([sys.executable, str(CHECK), "--sites", str(site_path),
                               "--registry", str(registry_path), *extra],
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

    def test_accepts_lighting_hooks_that_start_blocks(self):
        result = run(SITES, BOTH, LIGHTING, SHARD)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("all 2 MM3D lighting hook sites", result.stdout)

    def test_refuses_registry_without_a_lighting_hook_block(self):
        result = run(SITES, BOTH, LIGHTING, SHARD.replace("0x0011EF3CU", "0x0011EF40U"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("0x0011EF3C", result.stderr)

    def test_refuses_registry_without_shards(self):
        self.assertEqual(run(SITES, BOTH, LIGHTING, "").returncode, 1)


if __name__ == "__main__":
    unittest.main()
