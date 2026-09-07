from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import whole_aot_plugin_backend as backend


class WholeAotPluginBackendTests(unittest.TestCase):
    def test_builds_and_reuses_content_addressed_plugin(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo = root / "repo"
            a32 = repo / "tools/oot3d/native_a32_runtime"
            runtime = repo / "tools/oot3d/native_game_runtime"
            a32.mkdir(parents=True)
            runtime.mkdir(parents=True)
            for path in (
                runtime / "triaevum_title_whole_aot_plugin.cpp",
                runtime / "triaevum_title_aot_abi.h",
                runtime / "triaevum_title_whole_aot_abi.h",
                runtime / "oot3d_native_whole_aot_runtime.h",
                a32 / "oot3d_aot_architectural_state.h",
                a32 / "oot3d_native_a32_vfp_ops.h",
                a32 / "whole_aot_cpp.py",
                a32 / "whole_aot_optimization_ir.py",
            ):
                path.write_text(path.name, encoding="utf-8")

            program = root / "program.json"
            selection = root / "selection.json"
            code = root / "code.bin"
            compiler = root / "clang-cl.exe"
            archiver = root / "llvm-lib.exe"
            (root / "lld-link.exe").write_bytes(b"linker")
            support = root / "support.lib"
            nlohmann = root / "include"
            nlohmann.mkdir()
            for path in (program, selection, code, compiler, archiver, support):
                path.write_bytes(path.name.encode("ascii"))
            archive = root / "whole-aot.lib"
            archive.write_bytes(b"archive")

            def generate(
                _program: Path,
                _selection: Path,
                _code: Path,
                output: Path,
                **_kwargs: object,
            ) -> dict[str, object]:
                output.mkdir(parents=True, exist_ok=True)
                (output / "oot3d_whole_aot_generated.h").write_text(
                    "generated", encoding="utf-8"
                )
                return {"functions": [{"entry": 1}, {"entry": 2}]}

            archive_result = {
                "archive": str(archive),
                "archive_sha256": backend.sha256_file(archive),
                "archive_cache_key": "a" * 64,
                "objects_compiled": 3,
                "objects_reused": 0,
            }
            commands: list[tuple[str, ...]] = []

            def run(arguments: tuple[str, ...], _cwd: Path, _label: str) -> None:
                commands.append(arguments)
                for argument in arguments:
                    if argument.startswith("/Fo"):
                        Path(argument[3:]).write_bytes(b"wrapper")
                    elif argument.startswith("/Fe"):
                        Path(argument[3:]).write_bytes(b"MZ plugin")

            with (
                patch.object(backend, "REPO_ROOT", repo),
                patch.object(backend, "A32_ROOT", a32),
                patch.object(backend, "RUNTIME_ROOT", runtime),
                patch.object(
                    backend,
                    "_load_generator",
                    return_value=(
                        SimpleNamespace(generate=generate),
                        a32 / "whole_aot_cpp.py",
                        a32 / "whole_aot_optimization_ir.py",
                    ),
                ),
                patch.object(
                    backend,
                    "build_generated_cpp_archive",
                    return_value=archive_result,
                ),
                patch.object(backend, "_run", side_effect=run),
                patch.object(backend, "compiler_sysroot", return_value=SimpleNamespace(identity="verified-sysroot", arguments=lambda: ())),
            ):
                arguments = {
                    "program_path": program,
                    "selection_path": selection,
                    "code_path": code,
                    "cache_root": root / "cache",
                    "toolchain": backend.WholeAotPluginToolchain(
                        compiler=compiler,
                        archiver=archiver,
                        support_library=support,
                        nlohmann_include=nlohmann,
                    ),
                    "shard_count": 2,
                    "jobs": 2,
                }
                built = backend.build_whole_aot_plugin(**arguments)
                reused = backend.build_whole_aot_plugin(**arguments)
                self.assertTrue(reused["early_cache_hit"])
                self.assertEqual(reused["objects_compiled"], 0)
                self.assertEqual(reused["objects_reused"], 0)
                self.assertEqual(reused["cached_build_objects_compiled"], 3)
                with patch.object(backend, "build_generated_cpp_archive", side_effect=AssertionError("archive must not be visited")):
                    backend.build_whole_aot_plugin(**arguments)
                self.assertEqual(len(commands), 2)
                (root / "lld-link.exe").write_bytes(b"updated linker")
                relinked = backend.build_whole_aot_plugin(**arguments)
                self.assertEqual(built["generated_directory"], relinked["generated_directory"])
                (a32 / "oot3d_aot_architectural_state.h").write_bytes(b"changed dependency")
                changed_header = backend.build_whole_aot_plugin(**arguments)
                self.assertEqual(built["generated_directory"], changed_header["generated_directory"])
                self.assertNotEqual(changed_header["cache_key"], relinked["cache_key"])
                # A failed consumer must release the shared producer lock.
                support.write_bytes(b"new support library")
                with patch.object(backend, "build_generated_cpp_archive", side_effect=ValueError("fixture failure")):
                    with self.assertRaisesRegex(backend.WholeAotPluginError, "fixture failure"):
                        backend.build_whole_aot_plugin(**arguments)
                recovered = backend.build_whole_aot_plugin(**arguments)
                self.assertEqual(built["generated_directory"], recovered["generated_directory"])
                support.write_bytes(support.name.encode("ascii"))
                Path(changed_header["plugin"]).write_bytes(b"corrupted plugin")
                with self.assertRaisesRegex(backend.WholeAotPluginError, "cache changed"):
                    backend.build_whole_aot_plugin(**arguments)

            self.assertEqual(built["status"], "built")
            self.assertEqual(built["backend"], "generated_cpp_whole_aot_plugin_v2")
            self.assertEqual(built["functions"], 2)
            self.assertEqual(reused["status"], "reused")
            self.assertEqual(reused["plugin_sha256"], built["plugin_sha256"])
            self.assertEqual(len(commands), 8)
            self.assertNotEqual(relinked["cache_key"], built["cache_key"])
            self.assertTrue(Path(str(reused["plugin"])).is_file())


if __name__ == "__main__":
    unittest.main()
