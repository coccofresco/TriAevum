from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Sequence

from whole_aot_object_cache import (
    DependencyFile,
    NativeToolchain,
    PROFILE,
    WholeAotObjectError,
    build_generated_cpp_archive,
    source_object_key,
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class FakeNativeTools:
    def __init__(self) -> None:
        self.commands: list[tuple[str, ...]] = []

    def __call__(
        self, arguments: Sequence[str], working_directory: Path
    ) -> subprocess.CompletedProcess[str]:
        del working_directory
        command = tuple(arguments)
        self.commands.append(command)
        output_argument = next(
            item for item in command if item.startswith(("/Fo", "/out:"))
        )
        if output_argument.startswith("/Fo"):
            output = Path(output_argument[3:])
            source = Path(command[-1])
            output.write_bytes(b"OBJ\0" + source.read_bytes())
        else:
            output = Path(output_argument[5:])
            output.write_bytes(b"LIB\0" + str(len(command)).encode("ascii"))
        return subprocess.CompletedProcess(command, 0, "", "")


class WholeAotObjectCacheTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.generated = self.root / "generated"
        self.generated.mkdir()
        self.sources = (
            self.generated / "oot3d_native_whole_aot_shard_00.cpp",
            self.generated / "oot3d_native_whole_aot_shard_01.cpp",
            self.generated / "oot3d_native_whole_aot.cpp",
        )
        for index, source in enumerate(self.sources):
            source.write_text(f"void generated_{index}() {{}}\n", encoding="ascii")
        header = self.generated / "oot3d_native_whole_aot.h"
        header.write_text("#pragma once\n", encoding="ascii")
        files = {path.name: digest(path) for path in (*self.sources, header)}
        (self.generated / "whole_aot_cpp_manifest.json").write_text(
            json.dumps(
                {
                    "format": "oot3d_whole_aot_cpp_v1",
                    "program_sha256": "1" * 64,
                    "code_sha256": "2" * 64,
                    "shard_count": 2,
                    "files": files,
                }
            ),
            encoding="utf-8",
        )
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.nlohmann = self.root / "nlohmann"
        self.nlohmann.mkdir()
        self.compiler = self.root / "clang-cl.exe"
        self.archiver = self.root / "llvm-lib.exe"
        self.compiler.write_bytes(b"fake clang-cl v1")
        self.archiver.write_bytes(b"fake llvm-lib v1")
        self.dependency = self.root / "runtime.h"
        self.dependency.write_bytes(b"runtime interface v1")
        self.cache = self.root / "cache"
        self.runner = FakeNativeTools()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def build(self, **overrides):
        arguments = {
            "generated_directory": self.generated,
            "repo_root": self.repo,
            "nlohmann_include": self.nlohmann,
            "cache_root": self.cache,
            "toolchain": NativeToolchain(self.compiler, self.archiver),
            "jobs": 2,
            "dependencies": (DependencyFile("runtime/runtime.h", self.dependency),),
            "runner": self.runner,
        }
        arguments.update(overrides)
        return build_generated_cpp_archive(**arguments)

    def test_builds_thinlto_objects_once_then_reuses_archive(self) -> None:
        result = self.build()
        self.assertEqual(result["status"], "built")
        self.assertEqual(result["objects_compiled"], 3)
        self.assertEqual(result["objects_reused"], 0)
        self.assertEqual(len(self.runner.commands), 4)
        compile_commands = self.runner.commands[:-1]
        self.assertTrue(all("-flto=thin" in command for command in compile_commands))
        self.assertTrue(all("/Zi" not in command for command in compile_commands))
        self.assertTrue(all("/Z7" not in command for command in compile_commands))

        reused = self.build()
        self.assertEqual(reused["status"], "reused")
        self.assertEqual(reused["objects_compiled"], 0)
        self.assertEqual(reused["objects_reused"], 3)
        self.assertEqual(len(self.runner.commands), 4)

    def test_rearchives_without_recompiling_valid_cached_objects(self) -> None:
        first = self.build()
        archive = Path(first["archive"])
        archive.unlink()
        archive.with_suffix(".json").unlink()
        self.runner.commands.clear()

        rebuilt = self.build()
        self.assertEqual(rebuilt["status"], "built")
        self.assertEqual(rebuilt["objects_compiled"], 0)
        self.assertEqual(rebuilt["objects_reused"], 3)
        self.assertEqual(len(self.runner.commands), 1)
        self.assertEqual(Path(self.runner.commands[0][0]), self.archiver)

    def test_accepts_legacy_monolithic_generated_product(self) -> None:
        for source in self.sources[:2]:
            source.unlink()
        header = self.generated / "oot3d_native_whole_aot.h"
        manifest_path = self.generated / "whole_aot_cpp_manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["shard_count"] = 1
        manifest["files"] = {
            path.name: digest(path) for path in (self.sources[2], header)
        }
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        result = self.build()
        self.assertEqual(result["sources"], 1)
        self.assertEqual(result["objects_compiled"], 1)
        self.assertEqual(len(self.runner.commands), 2)

    def test_rejects_generated_source_changed_after_manifest(self) -> None:
        self.sources[0].write_text("void tampered() {}\n", encoding="ascii")
        with self.assertRaisesRegex(WholeAotObjectError, "file changed"):
            self.build()

    def test_source_key_is_independent_of_verified_include_paths(self) -> None:
        first = source_object_key(
            "1" * 64,
            "2" * 64,
            "3" * 64,
            ("/O2", "/IC:/one/path", "-flto=thin"),
        )
        second = source_object_key(
            "1" * 64,
            "2" * 64,
            "3" * 64,
            ("/O2", "/ID:/different/path", "-flto=thin"),
        )
        self.assertEqual(first, second)

    def test_rejects_unknown_toolchain_profile(self) -> None:
        with self.assertRaisesRegex(WholeAotObjectError, "unsupported.*profile"):
            self.build(
                toolchain=NativeToolchain(
                    self.compiler,
                    self.archiver,
                    profile=PROFILE + "-changed",
                )
            )


if __name__ == "__main__":
    unittest.main()
