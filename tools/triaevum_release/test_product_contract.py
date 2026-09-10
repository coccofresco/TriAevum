from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from product_contract import ensure_runtime_config, query_product, validate_product_info


def fixture_product():
    return {
        "format": "triaevum_product_info_v1",
        "runtime": "oot3d_native_game",
        "source_commit": "a" * 40,
        "whole_aot_plugin_abi": 2,
        "private_title_loaded": False,
        "capabilities": {"nri": True, "f1": True, "topscreen": True},
        "default_config": {"Graphics": {
            "Preset": "Custom", "FrameRate": {"Mode": "Interpolated2x"},
        }},
    }


class ProductContractTests(unittest.TestCase):
    def test_rejects_old_host_missing_features_and_wrong_commit(self):
        for key, value in (("runtime", "triaevum_oot3d_module_host"),
                           ("whole_aot_plugin_abi", 1), ("capabilities", {})):
            info = fixture_product()
            info[key] = value
            with self.assertRaises(ValueError):
                validate_product_info(info)
        with self.assertRaisesRegex(ValueError, "commit"):
            validate_product_info(fixture_product(), "b" * 40)

    def test_initializes_x2_only_for_new_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            self.assertTrue(ensure_runtime_config(path, fixture_product()))
            self.assertEqual(json.loads(path.read_text())["Graphics"]["FrameRate"]["Mode"],
                             "Interpolated2x")
            custom = b'{"Graphics":{"Preset":"Authentic"},"Other":"keep"}'
            path.write_bytes(custom)
            self.assertFalse(ensure_runtime_config(path, fixture_product()))
            self.assertEqual(path.read_bytes(), custom)
            path.write_bytes(b"invalid")
            with self.assertRaises(ValueError):
                ensure_runtime_config(path, fixture_product())
            self.assertEqual(path.read_bytes(), b"invalid")

    def test_query_is_bounded_and_hashes_the_queried_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "TriAevum.exe"
            exe.write_bytes(b"fixture")
            result = SimpleNamespace(returncode=0, stdout=json.dumps(fixture_product()))
            with patch("product_contract.run_native", return_value=result) as run:
                receipt = query_product(exe, "a" * 40)
            self.assertEqual(run.call_args.kwargs["timeout"], 15)
            self.assertEqual(run.call_args.args[0], [str(exe), "--product-info"])
            self.assertEqual(len(receipt["runtime_sha256"]), 64)

    def test_selected_plugin_query_requires_native_abi_success(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "TriAevum.exe"
            plugin = Path(directory) / "private.dll"
            exe.write_bytes(b"host")
            plugin.write_bytes(b"plugin")
            info = fixture_product()
            result = SimpleNamespace(returncode=0, stdout=json.dumps(info))
            with patch("product_contract.run_native", return_value=result) as run:
                with self.assertRaisesRegex(ValueError, "native ABI"):
                    query_product(exe, plugin=plugin)
                info["private_title_loaded"] = True
                result.stdout = json.dumps(info)
                query_product(exe, plugin=plugin)
                self.assertEqual(run.call_args.args[0],
                                 [str(exe), "--verify-title-plugin", str(plugin)])

    def test_windows_loader_failure_names_dependency_not_rom(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "TriAevum.exe"
            exe.write_bytes(b"host")
            for code in (3221225785, -1073741511):
                result = SimpleNamespace(returncode=code, stdout="", stderr="")
                with patch("product_contract.run_native", return_value=result):
                    with self.assertRaisesRegex(ValueError, "0xC0000139.*DLL export.*before ROM"):
                        query_product(exe)
