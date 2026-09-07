from __future__ import annotations

import ctypes
import hashlib
import json
import _ctypes
import tempfile
import unittest
from pathlib import Path

from direct_aot_backend import (
    DirectAotToolchain,
    build_direct_aot_plugin,
    load_direct_program,
)


LLVM_ROOT = Path("I:/oot3dre_tools/llvm-22.1.6")


class DirectAotBackendTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.code = self.root / "code.bin"
        self.program = self.root / "program.json"
        self.selection = self.root / "selection.json"

        code = (0xE3A00001).to_bytes(4, "little")
        code += (0xE12FFF1E).to_bytes(4, "little")
        self.code.write_bytes(code)
        self.program.write_text(
            json.dumps(
                {
                    "format": "oot3d_whole_aot_program_v1",
                    "base": 0x1000,
                    "code_sha256": hashlib.sha256(code).hexdigest(),
                    "blocks": [
                        {"id": 0, "pc": 0x1000, "end_pc": 0x1008}
                    ],
                    "functions": [
                        {
                            "entry": 0x1000,
                            "name": "fixture_entry",
                            "closed_static_cfg": True,
                            "blocks": [0],
                            "direct_calls": [],
                        }
                    ],
                },
                separators=(",", ":"),
            ),
            encoding="utf-8",
        )
        self.selection.write_text(
            json.dumps(
                {
                    "format": "oot3d_whole_aot_function_selection_v1",
                    "functions": [{"entry": 0x1000}],
                    "external_functions": [],
                },
                separators=(",", ":"),
            ),
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_loads_and_lowers_exact_program(self) -> None:
        functions, dispatch_pcs, owners = load_direct_program(
            self.program, self.selection, self.code
        )
        self.assertEqual(len(functions), 1)
        self.assertEqual(dispatch_pcs, (0x1000,))
        self.assertEqual(owners, (0,))
        self.assertEqual(len(functions[0].blocks[0].operations), 2)

    @unittest.skipUnless(
        (LLVM_ROOT / "bin/llc.exe").is_file()
        and (LLVM_ROOT / "bin/lld-link.exe").is_file(),
        "LLVM direct-AOT toolchain is unavailable",
    )
    def test_builds_loadable_cached_plugin(self) -> None:
        toolchain = DirectAotToolchain(
            LLVM_ROOT / "bin/llc.exe", LLVM_ROOT / "bin/lld-link.exe"
        )
        built = build_direct_aot_plugin(
            program_path=self.program,
            selection_path=self.selection,
            code_path=self.code,
            cache_root=self.root / "cache",
            toolchain=toolchain,
            shard_count=1,
            jobs=1,
        )
        self.assertEqual(built["status"], "built")
        plugin = ctypes.WinDLL(built["plugin"])
        query = plugin.triaevum_title_aot_query
        query.argtypes = [ctypes.c_uint32]
        query.restype = ctypes.c_void_p
        self.assertTrue(query(1))
        self.assertFalse(query(2))

        reused = build_direct_aot_plugin(
            program_path=self.program,
            selection_path=self.selection,
            code_path=self.code,
            cache_root=self.root / "cache",
            toolchain=toolchain,
            shard_count=1,
            jobs=1,
        )
        self.assertEqual(reused["status"], "reused")
        self.assertEqual(reused["plugin_sha256"], built["plugin_sha256"])
        _ctypes.FreeLibrary(plugin._handle)


if __name__ == "__main__":
    unittest.main()
