"""Synthetic-only integration checks for the compiler shipped with Forge."""

import json
import os
import subprocess
import struct
import tempfile
import unittest
from pathlib import Path


def inventory_source(source):
    data = source.encode()
    def digest(seed):
        for byte in data:
            seed = ((seed ^ byte) * 1099511628211) & ((1 << 64) - 1)
        return f"0x{seed or 1:016x}"
    return {"stage": "vertex", "source_id": digest(1469598103934665603),
            "secondary_hash": digest(1099511628211 ^ len(data)),
            "source_size": len(data), "source": source}


@unittest.skipUnless(os.environ.get("TRIAEVUM_TEST_SHADER_COMPILER"), "requires built shader compiler")
class ShaderCompilerTests(unittest.TestCase):
    def test_renderer_cache_cold_warm_macros_and_corruption_repair(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache = root / "cache"
            manifest = root / "prepared.json"
            def prepare():
                subprocess.run([os.environ["TRIAEVUM_TEST_SHADER_COMPILER"],
                    "--prepare-renderer-cache", str(cache), "--manifest", str(manifest)],
                    check=True, capture_output=True, timeout=60)
                return json.loads(manifest.read_text())
            cold = prepare()
            self.assertGreaterEqual(cold["modules"], 22)
            self.assertEqual(cold["compiled"], cold["modules"])
            self.assertEqual(cold["writes"], cold["modules"])
            self.assertEqual(cold["write_failed"], 0)
            self.assertFalse(cold["game_booted"])
            warm = prepare()
            self.assertEqual(warm["hits"], cold["modules"])
            self.assertEqual(warm["compiled"], 0)
            self.assertEqual(warm["writes"], 0)
            # Same fragment source, two macro configurations, two distinct keys
            # and SPIR-V payloads. Never merge canonical/instrumented grass.
            grass = []
            files = sorted((cache / "spirv-v2").glob("*.spvc"))
            for path in files:
                data = path.read_bytes()
                stage, contract_size, source_size = struct.unpack_from("<3I", data, 8)
                contract = data[40:40 + contract_size]
                if b"GRASS_AUXILIARY_OUTPUTS" in contract:
                    start = 40 + contract_size
                    grass.append((stage, contract, data[start:start + source_size], data[start + source_size:]))
            self.assertEqual(len(grass), 2)
            self.assertEqual(grass[0][0], 2)
            self.assertEqual(grass[0][2], grass[1][2])
            self.assertNotEqual(grass[0][1], grass[1][1])
            self.assertNotEqual(grass[0][3], grass[1][3])
            files[0].write_bytes(b"damaged")
            repaired = prepare()
            self.assertEqual(repaired["compiled"], 1)
            self.assertEqual(repaired["hits"], cold["modules"] - 1)
            self.assertEqual(repaired["writes"], 1)

    def test_renderer_sources_included_deduplicated_and_diagnostic_independent(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inventory = root / "input.json"
            inventory.write_text(json.dumps({"format": "oot3d_pica_effective_shader_inventory_v1",
                "descriptor_schema_version": 3, "shaders": [inventory_source(
                    "#version 450\nvoid main(){gl_Position=vec4(0,0,0,1);}\n")]}))
            def compile(name, inputs, diagnostic):
                env = dict(os.environ, TRIAEVUM_GUIDE_DIAGNOSTIC_VIEW=str(diagnostic))
                pack = root / (name + ".o3ps")
                manifest = root / (name + ".json")
                merged = root / (name + "-merged.json")
                command = [os.environ["TRIAEVUM_TEST_SHADER_COMPILER"], "--pack", str(pack),
                           "--manifest", str(manifest), "--merged-inventory", str(merged)]
                for path in inputs:
                    command.extend(("--inventory", str(path)))
                subprocess.run(command, check=True, capture_output=True, timeout=60, env=env)
                return pack.read_bytes(), json.loads(manifest.read_text()), merged
            first, report, merged = compile("first", [inventory], 0)
            self.assertEqual(report["shader_count"], 3)
            self.assertEqual(report["renderer_sources_added"], 2)
            sources = json.loads(merged.read_text())["shaders"]
            scanout = [value for value in sources if "physical_scanout" in value["source"]]
            self.assertEqual(len(scanout), 1)
            self.assertIn("oot3d_guide_diagnostic_mode = 0;", scanout[0]["source"])
            second, _, _ = compile("diagnostic-env", [inventory], 7)
            self.assertEqual(first, second)
            reused, report, _ = compile("merged", [inventory, merged, merged], 4)
            self.assertEqual(first, reused)
            self.assertEqual(report["renderer_sources_added"], 0)


if __name__ == "__main__":
    unittest.main()
