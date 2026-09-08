from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

from build_forge_binary import REPO_ROOT, required_data
from source_archive import DEFAULT_POLICY, create_source_archive, source_path_allowed


class SourceArchiveTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.repository = self.root / "repository"
        self.repository.mkdir()
        subprocess.run(["git", "init", "-q", str(self.repository)], check=True)
        subprocess.run(
            ["git", "-C", str(self.repository), "config", "user.email", "test@example.invalid"],
            check=True,
        )
        subprocess.run(
            ["git", "-C", str(self.repository), "config", "user.name", "Test"],
            check=True,
        )
        (self.repository / "src").mkdir()
        (self.repository / "src/main.cpp").write_text("int main() {}\n", encoding="utf-8")
        (self.repository / "private.bin").write_bytes(b"private")
        subprocess.run(["git", "-C", str(self.repository), "add", "."], check=True)
        subprocess.run(
            ["git", "-C", str(self.repository), "commit", "-q", "-m", "fixture"],
            check=True,
        )
        self.commit = subprocess.check_output(
            ["git", "-C", str(self.repository), "rev-parse", "HEAD"], text=True
        ).strip()
        self.policy = self.root / "policy.json"
        self.policy.write_text(
            json.dumps(
                {
                    "format": "triaevum_public_release_policy_v1",
                    "forbidden_basenames": [],
                    "forbidden_extensions": [".bin"],
                    "forbidden_path_components": [],
                    "source_archive_forbidden_prefixes": [],
                }
            ),
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_archive_is_deterministic_and_filtered(self) -> None:
        first = self.root / "first.zip"
        second = self.root / "second.zip"
        one = create_source_archive(
            self.repository, first, source_commit=self.commit, policy_path=self.policy
        )
        two = create_source_archive(
            self.repository, second, source_commit=self.commit, policy_path=self.policy
        )
        self.assertEqual(one["archive_sha256"], two["archive_sha256"])
        with zipfile.ZipFile(first) as archive:
            self.assertIn("src/main.cpp", archive.namelist())
            self.assertNotIn("private.bin", archive.namelist())
            self.assertIn("SOURCE_ARCHIVE_MANIFEST.json", archive.namelist())

    def test_source_specific_prefix_is_rejected(self) -> None:
        policy = {
            "source_archive_forbidden_prefixes": ["private/evidence"],
            "forbidden_basenames": [],
            "forbidden_extensions": [],
            "forbidden_path_components": [],
        }
        self.assertFalse(source_path_allowed("private/evidence/dump.txt", policy))
        self.assertTrue(source_path_allowed("src/runtime.cpp", policy))

    def test_imported_archive_receipt_is_replaced_not_duplicated(self) -> None:
        receipt = "SOURCE_ARCHIVE_MANIFEST.json"
        (self.repository / receipt).write_text('{"source_commit": "stale"}\n', encoding="utf-8")
        subprocess.run(["git", "-C", str(self.repository), "add", receipt], check=True)
        subprocess.run(
            ["git", "-C", str(self.repository), "commit", "-q", "-m", "imported receipt"],
            check=True,
        )
        commit = subprocess.check_output(
            ["git", "-C", str(self.repository), "rev-parse", "HEAD"], text=True
        ).strip()
        output = self.root / "imported.zip"
        result = create_source_archive(
            self.repository, output, source_commit=commit, policy_path=self.policy
        )
        with zipfile.ZipFile(output) as archive:
            self.assertEqual(archive.namelist().count(receipt), 1)
            self.assertEqual(json.loads(archive.read(receipt))["source_commit"], commit)
        self.assertEqual(result["excluded_tracked_files"], 2)

    def test_public_a32_runtime_sources_are_in_corresponding_source(self) -> None:
        policy = json.loads(DEFAULT_POLICY.read_text(encoding="utf-8"))
        required_runtime_sources = (
            "tools/oot3d/native_a32_runtime/upstream/recomp/a32_core.cpp",
            "tools/oot3d/native_a32_runtime/upstream/recomp/a32_runtime.cpp",
            "tools/oot3d/native_a32_runtime/upstream/recomp/a32_vfp_binary64.cpp",
        )
        for relative in required_runtime_sources:
            with self.subTest(relative=relative):
                self.assertTrue(source_path_allowed(relative, policy))

    def test_frozen_forge_inputs_are_in_corresponding_source(self) -> None:
        policy = json.loads(DEFAULT_POLICY.read_text(encoding="utf-8"))
        for source, _destination in required_data():
            relative = source.relative_to(REPO_ROOT).as_posix()
            with self.subTest(relative=relative):
                self.assertTrue(source_path_allowed(relative, policy))

    def test_public_runtime_does_not_require_decompiled_title_sources(self) -> None:
        policy = json.loads(DEFAULT_POLICY.read_text(encoding="utf-8"))
        self.assertFalse(
            source_path_allowed(
                "tools/oot3d/decomp_support/src/code/z_fog_material_scalar.c",
                policy,
            )
        )
        root_cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        renderer = (
            REPO_ROOT
            / "runtime/three_ds_recomp/src/oot3d/Oot3dNativeFast3dRenderer.cpp"
        ).read_text(encoding="utf-8")
        self.assertNotIn("z_fog_material_scalar.c", root_cmake)
        self.assertNotIn("fog_material_scalar.h", renderer)


if __name__ == "__main__":
    unittest.main()
