from __future__ import annotations

import json
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from audit_release import DEFAULT_POLICY, audit_release
from common import atomic_write_json, sha256_file
from package_release import package_release


FILES = {
    "TriAevum.exe": ("runtime_executable", b"MZ generic runtime"),
    "TriAevumForge.exe": ("forge_executable", b"MZ generic forge"),
    "triaevum_title_aot.dll": ("runtime_library", b"MZ generic AOT stub"),
    "forge/oot3d_game_module.dll": (
        "forge_runtime_module",
        b"MZ generic game adapter",
    ),
    "forge/clang-cl.exe": ("forge_tool", b"MZ LLVM compiler fixture"),
    "forge/llvm-lib.exe": ("forge_tool", b"MZ LLVM archiver fixture"),
    "forge/lld-link.exe": ("forge_tool", b"MZ LLVM linker fixture"),
    "forge/triaevum_title_whole_aot_support.lib": (
        "forge_link_library",
        b"title-neutral support archive",
    ),
    "lib/clang/22/lib/windows/clang_rt.builtins-x86_64.lib": (
        "forge_tool_resource",
        b"LLVM builtins fixture",
    ),
    "README.md": ("documentation", b"# TriAevum\n"),
    "LICENSE_SCOPE.md": ("documentation", b"mixed license scope\n"),
    "THIRD_PARTY_NOTICES.md": ("documentation", b"third-party notices\n"),
    "SOURCE_OFFER.md": ("documentation", b"corresponding source included\n"),
    "LICENSE": ("license", b"GPL-3.0-or-later test fixture\n"),
    "LICENSES/GPL-2.0-or-later.txt": ("license", b"GPL test fixture\n"),
    "LICENSES/shaderc-Apache-2.0.txt": ("license", b"Apache test fixture\n"),
    "LICENSES/Python-3.13.txt": ("license", b"Python test fixture\n"),
    "LICENSES/Capstone-BSD.txt": ("license", b"BSD test fixture\n"),
    "LICENSES/PyInstaller-GPL-2.0-or-later-with-bootloader-exception.txt": (
        "license",
        b"PyInstaller test fixture\n",
    ),
    "LICENSES/LLVM-exception.txt": ("license", b"LLVM exception fixture\n"),
    "recipes/oot3d.json": ("revision_recipe", b'{"format":"recipe-test"}\n'),
}


def write_clean_package(root: Path) -> None:
    inventory = []
    for relative, (role, data) in FILES.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        inventory.append(
            {
                "path": relative,
                "role": role,
                "bytes": len(data),
                "sha256": sha256_file(path),
            }
        )
    source_archive = root / "source" / "TriAevum-source.zip"
    source_archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(source_archive, "w") as archive:
        archive.writestr("src/main.cpp", "int main() { return 0; }\n")
    inventory.append(
        {
            "path": "source/TriAevum-source.zip",
            "role": "corresponding_source",
            "bytes": source_archive.stat().st_size,
            "sha256": sha256_file(source_archive),
        }
    )
    atomic_write_json(
        root / "release-manifest.json",
        {
            "format": "triaevum_public_release_manifest_v1",
            "release": {
                "name": "TriAevum",
                "version": "test",
                "source_commit": "0" * 40,
                "contains_title_code": False,
                "contains_title_content": False,
                "proprietary_sdk_included": False,
                "redistributable": True,
            },
            "files": inventory,
        },
    )


class PublicReleaseAuditTests(unittest.TestCase):
    def test_private_plugin_is_rejected_before_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            exe = root / "input.exe"
            exe.write_bytes(b"fixture")
            layout = root / "layout.json"
            atomic_write_json(layout, {"format": "triaevum_public_release_layout_v1", "files": [
                {"source": str(exe), "path": "TriAevum.exe", "role": "runtime_executable"}
            ]})
            with patch("package_release.query_product", return_value={
                "product": {"private_title_loaded": True}
            }):
                with self.assertRaisesRegex(ValueError, "private title plugin"):
                    package_release(layout, root / "output", source_root=root,
                                    version="test", source_commit="0" * 40,
                                    enforce_readiness=False)
            self.assertFalse((root / "output").exists())

    def test_accepts_exact_clean_allowlist(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_clean_package(root)
            result = audit_release(root)
            self.assertTrue(result.ok, result.errors)
            self.assertEqual(result.summary["verified_files"], len(FILES) + 1)

    def test_rejects_private_file_hidden_in_source_archive(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_clean_package(root)
            source_archive = root / "source" / "TriAevum-source.zip"
            with zipfile.ZipFile(source_archive, "w") as archive:
                archive.writestr("private/code.bin", b"not public")
            manifest_path = root / "release-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            item = next(
                entry
                for entry in manifest["files"]
                if entry["path"] == "source/TriAevum-source.zip"
            )
            item["bytes"] = source_archive.stat().st_size
            item["sha256"] = sha256_file(source_archive)
            atomic_write_json(manifest_path, manifest)
            result = audit_release(root)
            self.assertFalse(result.ok)
            self.assertTrue(any("code.bin" in error for error in result.errors))

    def test_rejects_undeclared_private_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_clean_package(root)
            (root / "game.tam").write_bytes(b"translated title code")
            result = audit_release(root)
            self.assertFalse(result.ok)
            self.assertIn("undeclared file in public package: game.tam", result.errors)

    def test_rejects_topscreen_payload_and_derived_atlas(self) -> None:
        for relative in ("resources/top_screen.ips", "resources/atlas_overrides.o3tu"):
            with (
                self.subTest(relative=relative),
                tempfile.TemporaryDirectory() as temporary,
            ):
                root = Path(temporary)
                write_clean_package(root)
                private = root / relative
                private.parent.mkdir(parents=True, exist_ok=True)
                private.write_bytes(b"private TopScreen evidence")
                manifest_path = root / "release-manifest.json"
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                manifest["files"].append(
                    {
                        "path": relative,
                        "role": "title_neutral_resource",
                        "bytes": private.stat().st_size,
                        "sha256": sha256_file(private),
                    }
                )
                atomic_write_json(manifest_path, manifest)
                result = audit_release(root)
                self.assertFalse(result.ok)
                self.assertTrue(
                    any(relative in error for error in result.errors), result.errors
                )

    def test_rejects_hash_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_clean_package(root)
            (root / "TriAevum.exe").write_bytes(b"changed")
            result = audit_release(root)
            self.assertFalse(result.ok)
            self.assertIn("SHA-256 mismatch: TriAevum.exe", result.errors)

    def test_rejects_private_json_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_clean_package(root)
            private = root / "config" / "private.json"
            private.parent.mkdir()
            private.write_text(
                json.dumps({"original_game_data_included": True}),
                encoding="utf-8",
            )
            manifest_path = root / "release-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["files"].append(
                {
                    "path": "config/private.json",
                    "role": "configuration",
                    "bytes": private.stat().st_size,
                    "sha256": sha256_file(private),
                }
            )
            atomic_write_json(manifest_path, manifest)
            result = audit_release(root)
            self.assertFalse(result.ok)
            self.assertTrue(
                any("original_game_data_included" in error for error in result.errors)
            )

    def test_packager_builds_and_audits_only_declared_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source-inputs"
            source.mkdir()
            layout_items = []
            for relative, (role, data) in FILES.items():
                source_path = source / relative
                source_path.parent.mkdir(parents=True, exist_ok=True)
                source_path.write_bytes(data)
                layout_items.append(
                    {"source": relative, "path": relative, "role": role}
                )
            source_archive = source / "source" / "TriAevum-source.zip"
            source_archive.parent.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(source_archive, "w") as archive:
                archive.writestr("src/main.cpp", "int main() { return 0; }\n")
            layout_items.append(
                {
                    "source": "source/TriAevum-source.zip",
                    "path": "source/TriAevum-source.zip",
                    "role": "corresponding_source",
                }
            )
            layout = root / "layout.json"
            atomic_write_json(
                layout,
                {
                    "format": "triaevum_public_release_layout_v1",
                    "files": layout_items,
                },
            )
            output = root / "package"
            result = package_release(
                layout,
                output,
                source_root=source,
                version="test",
                source_commit="0" * 40,
                policy_path=DEFAULT_POLICY,
                enforce_readiness=False,
                verify_runtime=False,
            )
            self.assertEqual(result["status"], "packaged")
            self.assertTrue(audit_release(output).ok)

    def test_public_packager_refuses_incomplete_readiness(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            readiness = root / "readiness.json"
            atomic_write_json(
                readiness,
                {
                    "format": "triaevum_release_readiness_v1",
                    "gates": [
                        {"id": "blocked", "required": True, "status": "blocked"}
                    ],
                },
            )
            layout = root / "layout.json"
            atomic_write_json(
                layout,
                {
                    "format": "triaevum_public_release_layout_v1",
                    "files": [
                        {
                            "source": "missing",
                            "path": "TriAevum.exe",
                            "role": "runtime_executable",
                        }
                    ],
                },
            )
            with self.assertRaisesRegex(ValueError, "readiness gates"):
                package_release(
                    layout,
                    root / "output",
                    source_root=root,
                    version="test",
                    source_commit="0" * 40,
                    readiness_path=readiness,
                )


if __name__ == "__main__":
    unittest.main()
