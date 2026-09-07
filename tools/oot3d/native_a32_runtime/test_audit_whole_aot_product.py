from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from audit_whole_aot_product import audit_product


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class WholeAotProductAuditTests(unittest.TestCase):
    def test_accepts_complete_closure_and_rejects_open_cfg(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = b"\x00" * 16
            exheader = bytearray(2048)
            exheader[0x10:0x14] = (0x1000).to_bytes(4, "little")
            exheader[0x14:0x18] = (1).to_bytes(4, "little")
            code_path = root / "code.bin"
            exheader_path = root / "exheader.bin"
            selection_path = root / "selection.json"
            program_path = root / "program.json"
            manifest_path = root / "manifest.json"
            code_path.write_bytes(code)
            exheader_path.write_bytes(exheader)
            selection = {
                "format": "selection-v1",
                "selection_mode": "all_lowerable",
                "functions": [{"entry": 0x1000, "name": "entry"}],
                "external_functions": [],
                "performance_exclusions": [],
                "selection_conflicts": [],
            }
            selection_bytes = (json.dumps(selection) + "\n").encode()
            selection_path.write_bytes(selection_bytes)
            program = {
                "format": "program-v1",
                "code_sha256": sha256(code),
                "base": 0x1000,
                "executable_size": 0x1000,
                "inputs": {
                    "inventory_sha256": "inventory",
                    "boundary_audit_sha256": "boundaries",
                },
                "counts": {"instructions": 1},
                "blocks": [{"id": 0, "pc": 0x1000, "instruction_count": 1}],
                "functions": [
                    {
                        "entry": 0x1000,
                        "name": "entry",
                        "blocks": [0],
                        "closed_static_cfg": True,
                        "direct_calls": [],
                        "unresolved_static_edges": [],
                    }
                ],
                "unclaimed_blocks": [],
            }
            program_path.write_text(json.dumps(program), encoding="utf-8")
            manifest = {
                "format": "oot3d_whole_aot_product_contract_v1",
                "source_image": {
                    "code": {"operational_id": "unused", "bytes": 16, "sha256": sha256(code)},
                    "exheader": {
                        "operational_id": "unused",
                        "bytes": 2048,
                        "sha256": sha256(exheader),
                    },
                    "base": 0x1000,
                    "entrypoint": 0x1000,
                    "executable_size": 0x1000,
                },
                "frontend": {
                    "program_format": "program-v1",
                    "inventory_sha256": "inventory",
                    "boundary_audit_sha256": "boundaries",
                },
                "selection": {
                    "path": str(selection_path),
                    "format": "selection-v1",
                    "mode": "all_lowerable",
                    "sha256": sha256(selection_bytes),
                },
                "closure": {
                    "counts": {
                        "functions": 1,
                        "selected_functions": 1,
                        "host_boundaries": 0,
                        "blocks": 1,
                        "unique_instructions": 1,
                        "emitted_instructions": 1,
                        "dispatch_aliases": 0,
                        "dispatcher_entries": 1,
                        "atomic_host_blocks": 0,
                    },
                    "host_boundaries": [],
                    "residual_a32_entries": 0,
                },
                "codegen": {
                    "format": "cpp-v1",
                    "shard_count": 1,
                    "shard_strategy": "stable",
                },
            }
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            valid = audit_product(
                manifest_path,
                program_path,
                code_path=code_path,
                exheader_path=exheader_path,
                selection_path=selection_path,
            )
            self.assertTrue(valid.ok, valid.errors)

            program["functions"][0]["closed_static_cfg"] = False
            program_path.write_text(json.dumps(program), encoding="utf-8")
            invalid = audit_product(
                manifest_path,
                program_path,
                code_path=code_path,
                exheader_path=exheader_path,
                selection_path=selection_path,
            )
            self.assertFalse(invalid.ok)
            self.assertIn("one or more function CFGs remain open", invalid.errors)


if __name__ == "__main__":
    unittest.main()
