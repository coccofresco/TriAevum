from __future__ import annotations

import hashlib
import tempfile
import unittest
from pathlib import Path

from whole_aot_source_backend import (
    SourceImage,
    WholeAotFrontend,
    WholeAotSourceError,
    prepare_whole_aot_ir,
    whole_aot_ir_cache_key,
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class AuditResult:
    ok = True
    errors: tuple[str, ...] = ()
    summary = {
        "functions": 12_422,
        "selected_functions": 12_419,
        "residual_a32_entries": 0,
    }


class WholeAotSourceBackendTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.code = self.root / "one" / "code.bin"
        self.exheader = self.root / "one" / "exheader.bin"
        self.code.parent.mkdir()
        self.code.write_bytes(b"verified-code-image")
        exheader = bytearray(0x20)
        exheader[0x10:0x14] = (0x00100000).to_bytes(4, "little")
        exheader[0x14:0x18] = (3).to_bytes(4, "little")
        self.exheader.write_bytes(exheader)
        self.identity = self.root / "frontend.py"
        self.identity.write_bytes(b"frontend-v1")
        self.inventory = self.root / "inventory.csv"
        self.supplemental = self.root / "supplemental.csv"
        self.boundary = self.root / "boundary.csv"
        self.manifest = self.root / "product.json"
        self.selection = self.root / "selection.json"
        for path in (
            self.inventory,
            self.supplemental,
            self.boundary,
            self.manifest,
            self.selection,
        ):
            path.write_bytes(path.name.encode("ascii"))
        self.extract_calls: list[list[str]] = []
        self.audit_calls: list[tuple[Path, Path, dict[str, object]]] = []

        def add_entries(
            inventory: Path,
            supplemental: Path,
            output: Path,
            entrypoint: int,
        ) -> Path:
            self.assertEqual(inventory, self.inventory)
            self.assertEqual(supplemental, self.supplemental)
            self.assertEqual(entrypoint, 0x00100000)
            output.write_bytes(b"generated-inventory")
            return output

        def extract(arguments=None) -> int:
            values = list(arguments or ())
            self.extract_calls.append(values)
            output = Path(values[values.index("--output") + 1])
            output.write_bytes(b"structural-program")
            return 0

        def audit(manifest: Path, program: Path, **kwargs):
            self.audit_calls.append((manifest, program, kwargs))
            return AuditResult()

        self.frontend = WholeAotFrontend(
            inventory=self.inventory,
            supplemental_entries=self.supplemental,
            boundary_audit=self.boundary,
            product_manifest=self.manifest,
            selection=self.selection,
            default_base=0x00100000,
            identity_files=(("frontend.py", self.identity),),
            add_local_entry_intervals=add_entries,
            extract_program=extract,
            audit_product=audit,
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def source(self, path: Path) -> SourceImage:
        return SourceImage(path, path.stat().st_size, digest(path))

    def test_prepares_explicit_ir_once_and_reuses_cache(self) -> None:
        result = prepare_whole_aot_ir(
            recipe="fixture",
            code=self.source(self.code),
            exheader=self.source(self.exheader),
            cache_root=self.root / "cache",
            frontend=self.frontend,
        )
        self.assertEqual(result["status"], "prepared")
        self.assertEqual(
            Path(result["program"]).read_bytes(), b"structural-program"
        )
        self.assertEqual(len(self.extract_calls), 1)
        arguments = self.extract_calls[0]
        self.assertEqual(Path(arguments[arguments.index("--code") + 1]), self.code)
        self.assertEqual(
            arguments[arguments.index("--executable-size") + 1], "0x3000"
        )
        self.assertEqual(len(self.audit_calls), 1)
        audit_kwargs = self.audit_calls[0][2]
        self.assertEqual(audit_kwargs["code_path"], self.code)
        self.assertEqual(audit_kwargs["exheader_path"], self.exheader)
        self.assertEqual(audit_kwargs["selection_path"], self.selection)

        reused = prepare_whole_aot_ir(
            recipe="fixture",
            code=self.source(self.code),
            exheader=self.source(self.exheader),
            cache_root=self.root / "cache",
            frontend=self.frontend,
        )
        self.assertEqual(reused["status"], "reused")
        self.assertEqual(len(self.extract_calls), 1)
        self.assertEqual(len(self.audit_calls), 1)

    def test_cache_key_does_not_depend_on_source_paths(self) -> None:
        other = self.root / "another-location"
        other.mkdir()
        other_code = other / "renamed-code.bin"
        other_exheader = other / "renamed-exheader.bin"
        other_code.write_bytes(self.code.read_bytes())
        other_exheader.write_bytes(self.exheader.read_bytes())
        first = whole_aot_ir_cache_key(
            "fixture",
            self.source(self.code),
            self.source(self.exheader),
            self.frontend,
        )
        second = whole_aot_ir_cache_key(
            "fixture",
            self.source(other_code),
            self.source(other_exheader),
            self.frontend,
        )
        self.assertEqual(first, second)

    def test_audits_seed_instead_of_trusting_it(self) -> None:
        seed = self.root / "seed.json"
        seed.write_bytes(b"previous-structural-program")
        result = prepare_whole_aot_ir(
            recipe="fixture",
            code=self.source(self.code),
            exheader=self.source(self.exheader),
            cache_root=self.root / "seed-cache",
            frontend=self.frontend,
            seed_program=seed,
        )
        self.assertEqual(result["status"], "prepared")
        self.assertEqual(
            Path(result["program"]).read_bytes(), b"previous-structural-program"
        )
        self.assertEqual(self.extract_calls, [])
        self.assertEqual(len(self.audit_calls), 2)

    def test_rejects_concurrent_preparation_for_same_key(self) -> None:
        key = whole_aot_ir_cache_key(
            "fixture",
            self.source(self.code),
            self.source(self.exheader),
            self.frontend,
        )
        cache = self.root / "locked-cache"
        cache.mkdir()
        (cache / f".{key}.lock").write_text("pid=1\n", encoding="ascii")
        with self.assertRaisesRegex(WholeAotSourceError, "already active"):
            prepare_whole_aot_ir(
                recipe="fixture",
                code=self.source(self.code),
                exheader=self.source(self.exheader),
                cache_root=cache,
                frontend=self.frontend,
            )


if __name__ == "__main__":
    unittest.main()
