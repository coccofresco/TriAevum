from __future__ import annotations

import hashlib
import io
import json
import shutil
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from release_platform import host_platform
from types import SimpleNamespace
from unittest.mock import patch

from common import load_json_object, atomic_write_json, sha256_file
from migrate_installation import migrate_installation
from installed_runtime import validate_installed_runtime
from forge import (
    ForgeError,
    HashCache,
    activate_prepared_title,
    build_private_aot_fallback,
    load_recipe,
    load_prepared_content,
    main as forge_main,
    package_private_module,
    prepare_content,
    prepare_private_aot_ir,
    verify_sources,
)
from tam_builder import (
    AUDIO_SERVICE_V1,
    FILESYSTEM_SERVICE_V1,
    INPUT_SERVICE_V1,
    PICA_SERVICE_V1,
)
from installation_context import resolve_reference
from validate_product_run import clone_validation_content


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class ForgeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.code = self.root / "private" / "code.fixture"
        self.exheader = self.root / "private" / "exheader.fixture"
        self.romfs = self.root / "private" / "romfs.fixture"
        self.code.parent.mkdir()
        self.code.write_bytes(b"code-fixture")
        self.exheader.write_bytes(b"exheader-fixture")
        self.romfs.write_bytes(b"romfs-fixture")
        self.recipe_path = self.root / "recipes.json"
        self.recipe_path.write_text(
            json.dumps(
                {
                    "format": "triaevum_supported_revisions_v1",
                    "recipes": [
                        {
                            "id": "fixture",
                            "inputs": {
                                "code": {
                                    "bytes": self.code.stat().st_size,
                                    "sha256": digest(self.code.read_bytes()),
                                },
                                "exheader": {
                                    "bytes": self.exheader.stat().st_size,
                                    "sha256": digest(self.exheader.read_bytes()),
                                },
                                "romfs": {
                                    "bytes": self.romfs.stat().st_size,
                                    "sha256": digest(self.romfs.read_bytes()),
                                },
                            },
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        self.output = self.root / "forge-output"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def verify(self):
        recipe = load_recipe(self.recipe_path, "fixture")
        cache = HashCache(self.output / ".hash-cache.json")
        verified, verified_mod = verify_sources(
            recipe,
            code_path=self.code,
            exheader_path=self.exheader,
            romfs_path=self.romfs,
            cache=cache,
        )
        return recipe, verified, verified_mod

    def build_process_manifest(self, exheader: Path, code: Path, romfs: Path):
        return {
            "format": "oot3d_native_process_manifest_v1",
            "source": {
                "exheader_path": str(exheader.resolve()),
                "exheader_sha256": digest(exheader.read_bytes()),
                "code_bin_path": str(code.resolve()),
                "code_bin_sha256": digest(code.read_bytes()),
                "romfs_image_path": str(romfs.resolve()),
            },
            "process": {
                "entrypoint": 0x100000,
                "linear_heap": {"base_address": 0x14000000, "size": 0x4000000},
                "system_regions": [
                    {
                        "name": "ctr_vram",
                        "address": 0x1F000000,
                        "mapped_size": 0x600000,
                    }
                ],
            },
        }

    def test_prepares_index_without_copying_private_inputs(self) -> None:
        recipe, verified, verified_mod = self.verify()
        result = prepare_content(
            recipe,
            verified,
            output_root=self.output,
            verified_mod=verified_mod,
            process_manifest_builder=self.build_process_manifest,
        )
        self.assertEqual(result["status"], "prepared")
        directory = Path(result["directory"])
        self.assertEqual(
            {path.name for path in directory.iterdir()},
            {"content.tap", "forge-state.json", "process-manifest.json"},
        )
        index = load_json_object(directory / "content.tap")
        self.assertFalse(index["redistributable"])
        self.assertEqual(resolve_reference(index["inputs"]["code"]["path"], directory), self.code.resolve())
        self.assertEqual(index["inputs"]["code"]["path_scope"], "relative")
        self.assertEqual(
            index["physical_memory_regions"],
            [
                {
                    "physical_base": 0x20000000,
                    "guest_base": 0x14000000,
                    "bytes": 0x4000000,
                },
                {
                    "physical_base": 0x18000000,
                    "guest_base": 0x1F000000,
                    "bytes": 0x600000,
                },
            ],
        )
        self.assertEqual(
            index["process_manifest"]["sha256"],
            digest((directory / "process-manifest.json").read_bytes()),
        )
        self.assertFalse((directory / "game.tam").exists())

    def test_prepared_content_survives_installation_move(self) -> None:
        recipe, verified, verified_mod = self.verify()
        result = prepare_content(
            recipe, verified, output_root=self.output, verified_mod=verified_mod,
            process_manifest_builder=self.build_process_manifest,
        )
        original = Path(result["directory"])
        with tempfile.TemporaryDirectory() as temporary:
            moved = Path(temporary) / "installation with spaces \u00e8"
            shutil.copytree(self.root, moved)
            # Remove the original inputs so accidental absolute references fail.
            self.code.unlink()
            self.exheader.unlink()
            self.romfs.unlink()
            directory = moved / original.relative_to(self.root)
            prepared = load_prepared_content(directory, required_inputs=("code", "exheader", "romfs"))
            self.assertEqual(prepared.inputs["code"].path, moved / "private" / "code.fixture")
            manifest = load_json_object(directory / "process-manifest.json")
            for key in ("code_bin_path", "exheader_path", "romfs_image_path"):
                self.assertTrue(resolve_reference(manifest["source"][key], directory).is_file())

    def test_validation_clone_copies_and_relocates_relative_inputs(self) -> None:
        recipe, verified, verified_mod = self.verify()
        result = prepare_content(
            recipe, verified, output_root=self.output, verified_mod=verified_mod,
            process_manifest_builder=self.build_process_manifest,
        )
        source = Path(result["directory"]) / "process-manifest.json"
        original_hash = digest(source.read_bytes())
        target = self.root / "test installation"
        target.mkdir()
        clone_validation_content(source, target, copy_inputs=True)
        self.assertEqual(digest(source.read_bytes()), original_hash)
        moved = self.root / "moved validation"
        target.rename(moved)
        self.code.unlink()
        self.exheader.unlink()
        self.romfs.unlink()
        prepared = load_prepared_content(moved, required_inputs=("code", "exheader", "romfs"))
        self.assertTrue(all(item.path.is_relative_to(moved) for item in prepared.inputs.values()))

    def prepare_legacy_installation(self):
        recipe, verified, verified_mod = self.verify()
        result = prepare_content(recipe, verified, output_root=self.output,
                                 verified_mod=verified_mod, process_manifest_builder=self.build_process_manifest)
        title = Path(result["directory"])
        manifest_path = title / "process-manifest.json"
        atomic_write_json(manifest_path, self.build_process_manifest(self.exheader, self.code, self.romfs))
        index = load_json_object(title / "content.tap")
        index.pop("path_mode")
        for descriptor in index["inputs"].values():
            descriptor["path"] = str(resolve_reference(descriptor["path"], title))
            descriptor.pop("path_scope")
        index["process_manifest"] = {"path": str(manifest_path), "bytes": manifest_path.stat().st_size,
                                     "sha256": sha256_file(manifest_path)}
        atomic_write_json(title / "content.tap", index)
        platform = host_platform()
        exe, plugin = self.root / platform.runtime, self.root / platform.title_module
        exe.write_bytes(b"synthetic host")
        plugin.write_bytes(b"synthetic plugin")
        profile = self.root / "TriAevum.launch.json"
        arguments = []
        for option, path in (("--a32-process-manifest", manifest_path),
                             ("--config", self.output / "config/TriAevum.json"),
                             ("--topscreen-config", self.output / "config/topscreen_ui.json"),
                             ("--save-data", self.output / "savedata")):
            if option in ("--config", "--topscreen-config"):
                atomic_write_json(path, {"user_preference": "preserve"})
            arguments.extend((option, str(path)))
        atomic_write_json(profile, {"format": "oot3d_native_game_launch_profile_v1", "arguments": arguments})
        state = load_json_object(title / "forge-state.json")
        state["runtime"] = {"status": "ready", "runtime_sha256": sha256_file(exe),
                            "plugin_sha256": sha256_file(plugin), "launch_profile": str(profile),
                            "launch_profile_sha256": sha256_file(profile)}
        atomic_write_json(title / "forge-state.json", state)
        atomic_write_json(self.output / "active-title.json", {"directory": str(title)})
        save = self.output / "savedata/save00.bin"
        save.parent.mkdir()
        save.write_bytes(b"user save must not change")
        return title, save

    def test_migration_is_idempotent_preserves_saves_and_can_move(self):
        title, save = self.prepare_legacy_installation()
        save_before = save.read_bytes()
        self.assertEqual(migrate_installation(self.root, title, self.output)["status"], "migrated")
        self.assertEqual(migrate_installation(self.root, title, self.output)["status"], "unchanged")
        self.assertEqual(save.read_bytes(), save_before)
        self.assertEqual(load_json_object(self.output / "config/TriAevum.json"), {"user_preference": "preserve"})
        with tempfile.TemporaryDirectory() as temporary:
            moved = Path(temporary) / "moved"
            shutil.copytree(self.root, moved)
            new_title = moved / title.relative_to(self.root)
            runtime = load_json_object(new_title / "forge-state.json")["runtime"]
            validate_installed_runtime(moved / host_platform().runtime, new_title, moved / "forge-output", runtime)
            self.assertEqual((moved / save.relative_to(self.root)).read_bytes(), save_before)

    def test_migration_rolls_back_every_metadata_file_on_late_failure(self):
        title, save = self.prepare_legacy_installation()
        paths = [title / name for name in ("process-manifest.json", "content.tap", "forge-state.json")]
        paths += [self.root / "TriAevum.launch.json", self.output / "active-title.json", save]
        before = {path: path.read_bytes() for path in paths}
        with patch("migrate_installation.validate_installed_runtime", side_effect=[self.root / "TriAevum.launch.json", ValueError("late failure")]):
            with self.assertRaisesRegex(ValueError, "late failure"):
                migrate_installation(self.root, title, self.output)
        self.assertEqual({path: path.read_bytes() for path in paths}, before)

    def test_reuses_identical_content_addressed_output(self) -> None:
        recipe, verified, verified_mod = self.verify()
        prepare_content(
            recipe,
            verified,
            output_root=self.output,
            process_manifest_builder=self.build_process_manifest,
        )
        result = prepare_content(
            recipe,
            verified,
            output_root=self.output,
            process_manifest_builder=self.build_process_manifest,
        )
        self.assertEqual(result["status"], "reused")

    def test_rejects_wrong_input_identity(self) -> None:
        self.code.write_bytes(b"wrong")
        recipe = load_recipe(self.recipe_path, "fixture")
        cache = HashCache(self.output / ".hash-cache.json", enabled=False)
        with self.assertRaises(ForgeError):
            verify_sources(
                recipe,
                code_path=self.code,
                exheader_path=self.exheader,
                romfs_path=self.romfs,
                cache=cache,
            )

    def test_rejects_process_manifest_for_different_code(self) -> None:
        recipe, verified, _ = self.verify()

        def mismatched_manifest(exheader: Path, code: Path, romfs: Path):
            manifest = self.build_process_manifest(exheader, code, romfs)
            manifest["source"]["code_bin_sha256"] = "0" * 64
            return manifest

        with self.assertRaisesRegex(ForgeError, "does not match verified input"):
            prepare_content(
                recipe,
                verified,
                output_root=self.output,
                process_manifest_builder=mismatched_manifest,
            )

    def test_rejects_malformed_process_memory_layout(self) -> None:
        recipe, verified, _ = self.verify()

        def malformed_manifest(exheader: Path, code: Path, romfs: Path):
            manifest = self.build_process_manifest(exheader, code, romfs)
            manifest["process"]["linear_heap"]["size"] = None
            return manifest

        with self.assertRaisesRegex(ForgeError, "linear heap size is not an integer"):
            prepare_content(
                recipe,
                verified,
                output_root=self.output,
                process_manifest_builder=malformed_manifest,
            )

    def test_packages_private_module_in_content_addressed_cache(self) -> None:
        recipe, verified, _ = self.verify()
        prepared = prepare_content(
            recipe,
            verified,
            output_root=self.output,
            process_manifest_builder=self.build_process_manifest,
        )
        native = self.root / "private" / "game-module.dll"
        native.write_bytes(b"MZ private translated module")
        translator = "7" * 64
        packaged = package_private_module(
            Path(prepared["directory"]),
            native,
            target_triple="x86_64-pc-windows-msvc",
            translator_identity=translator,
        )
        self.assertEqual(packaged["status"], "packaged")
        module = Path(packaged["module"])
        self.assertTrue(module.is_file())
        self.assertEqual(len(module.stem), 32)
        state = load_json_object(Path(prepared["directory"]) / "forge-state.json")
        self.assertEqual(state["module"]["status"], "ready")
        self.assertEqual(state["module"]["translator_identity_sha256"], translator)
        self.assertEqual(len(state["module"]["cache_key"]), 64)
        self.assertEqual(state["module"]["sha256"], digest(module.read_bytes()))
        self.assertEqual(
            state["module"]["required_services"],
            sorted(
                (
                    PICA_SERVICE_V1,
                    AUDIO_SERVICE_V1,
                    FILESYSTEM_SERVICE_V1,
                    INPUT_SERVICE_V1,
                )
            ),
        )
        reused = package_private_module(
            Path(prepared["directory"]),
            native,
            target_triple="x86_64-pc-windows-msvc",
            translator_identity=translator,
        )
        self.assertEqual(reused["status"], "reused")

    def test_activation_publishes_a_verified_ready_module(self) -> None:
        recipe, verified, _ = self.verify()
        prepared = prepare_content(
            recipe,
            verified,
            output_root=self.output,
            process_manifest_builder=self.build_process_manifest,
        )
        native = self.root / "private" / "game-module.dll"
        native.write_bytes(b"MZ private translated module")
        package_private_module(
            Path(prepared["directory"]),
            native,
            target_triple="x86_64-pc-windows-msvc",
            translator_identity="7" * 64,
        )
        active_path = self.root / "user-data" / "active-title.json"
        result = activate_prepared_title(
            Path(prepared["directory"]), active_title_state=active_path
        )
        active = load_json_object(active_path)
        state = load_json_object(Path(prepared["directory"]) / "forge-state.json")
        self.assertEqual(result["status"], "active")
        self.assertEqual(active["format"], "triaevum_active_title_v1")
        self.assertEqual(active["directory"], prepared["directory"])
        self.assertEqual(active["content_key"], state["content_key"])
        self.assertEqual(active["module_sha256"], state["module"]["sha256"])

    def test_doctor_reports_integrated_runtime_boundary(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output):
            result = forge_main(["doctor", "--inventory", "--recipes", str(self.recipe_path)])
        self.assertEqual(result, 0)
        report = json.loads(output.getvalue())
        self.assertEqual(report["runtime_module_loader"], "integrated")
        self.assertEqual(report["structural_aot_ir"], "available")
        self.assertEqual(
            report["incremental_cpp_object_fallback"],
            "available_for_development",
        )
        self.assertEqual(
            report["runtime_host_services"],
            ["pica_v1", "audio_v1", "filesystem_v1", "input_v1"],
        )

    def test_prepares_aot_ir_from_verified_forge_inputs(self) -> None:
        recipe, verified, _ = self.verify()
        prepared = prepare_content(
            recipe,
            verified,
            output_root=self.output,
            process_manifest_builder=self.build_process_manifest,
        )
        expected = {
            "status": "prepared",
            "cache_key": "1" * 64,
            "directory": str(self.root / "translator-cache" / ("1" * 64)),
            "program": str(self.root / "translator-cache" / "program.json"),
            "program_sha256": "2" * 64,
            "translator_identity_sha256": "3" * 64,
            "audit": {"residual_a32_entries": 0},
        }
        with patch("forge.prepare_whole_aot_ir", return_value=expected) as backend:
            result = prepare_private_aot_ir(
                Path(prepared["directory"]),
                cache_root=self.root / "translator-cache",
            )
        self.assertEqual(result, expected)
        arguments = backend.call_args.kwargs
        self.assertEqual(arguments["recipe"], "fixture")
        self.assertEqual(arguments["code"].path, self.code.resolve())
        self.assertEqual(arguments["exheader"].path, self.exheader.resolve())
        state = load_json_object(Path(prepared["directory"]) / "forge-state.json")
        self.assertEqual(state["translation"]["status"], "ir_ready")
        self.assertEqual(state["translation"]["cache_key"], "1" * 64)

        reused = prepare_content(
            recipe,
            verified,
            output_root=self.output,
            process_manifest_builder=self.build_process_manifest,
        )
        self.assertEqual(reused["status"], "reused")
        preserved = load_json_object(Path(prepared["directory"]) / "forge-state.json")
        self.assertEqual(preserved["translation"]["status"], "ir_ready")

    def test_builds_audited_development_object_fallback(self) -> None:
        recipe, verified, _ = self.verify()
        prepared = prepare_content(
            recipe,
            verified,
            output_root=self.output,
            process_manifest_builder=self.build_process_manifest,
        )
        ir_directory = self.root / "translator-cache" / ("1" * 64)
        ir_directory.mkdir(parents=True)
        program = ir_directory / "aot_program.json"
        program.write_bytes(b"verified structural program")
        prepared_directory = Path(prepared["directory"])
        state_path = prepared_directory / "forge-state.json"
        state = load_json_object(state_path)
        state["translation"] = {
            "status": "ir_ready",
            "backend": "whole_aot_structural_ir_v1",
            "directory": str(ir_directory),
            "program": str(program),
            "program_sha256": digest(program.read_bytes()),
        }
        state_path.write_text(json.dumps(state), encoding="utf-8")
        generated = self.root / "generated"
        generated.mkdir()
        (generated / "whole_aot_cpp_manifest.json").write_text("{}", encoding="utf-8")
        expected = {
            "status": "built",
            "backend": "generated_cpp_thinlto_fallback",
            "archive": str(self.root / "objects" / "whole-aot.lib"),
            "archive_sha256": "4" * 64,
            "archive_cache_key": "5" * 64,
            "toolchain_identity_sha256": "6" * 64,
            "sources": 257,
            "objects_compiled": 257,
            "objects_reused": 0,
            "elapsed_seconds": 1.0,
        }
        audit = SimpleNamespace(
            ok=True,
            errors=(),
            summary={"functions": 12_422, "residual_a32_entries": 0},
        )
        frontend = SimpleNamespace(
            product_manifest=self.root / "product.json",
            selection=self.root / "selection.json",
            audit_product=lambda *args, **kwargs: audit,
        )
        with (
            patch("forge.load_default_frontend", return_value=frontend),
            patch(
                "forge.build_generated_cpp_archive", return_value=expected
            ) as backend,
        ):
            result = build_private_aot_fallback(
                prepared_directory,
                generated_directory=generated,
                llvm_root=self.root / "llvm",
                nlohmann_include=self.root / "include",
                cache_root=self.root / "objects",
                jobs=3,
            )
        self.assertEqual(result["backend"], "generated_cpp_thinlto_fallback")
        self.assertEqual(result["audit"]["residual_a32_entries"], 0)
        self.assertEqual(backend.call_args.kwargs["jobs"], 3)
        updated = load_json_object(state_path)
        archive_state = updated["translation"]["native_object_archive"]
        self.assertEqual(archive_state["status"], "ready")
        self.assertEqual(archive_state["archive_sha256"], "4" * 64)

    def test_rejects_packaging_after_process_manifest_tamper(self) -> None:
        recipe, verified, _ = self.verify()
        prepared = prepare_content(
            recipe,
            verified,
            output_root=self.output,
            process_manifest_builder=self.build_process_manifest,
        )
        native = self.root / "private" / "game-module.dll"
        native.write_bytes(b"MZ private translated module")
        (Path(prepared["directory"]) / "process-manifest.json").write_text(
            "{}", encoding="utf-8"
        )
        with self.assertRaisesRegex(ForgeError, "changed after content preparation"):
            package_private_module(
                Path(prepared["directory"]),
                native,
                target_triple="x86_64-pc-windows-msvc",
                translator_identity="7" * 64,
            )

    def test_rejects_packaging_after_code_tamper(self) -> None:
        recipe, verified, _ = self.verify()
        prepared = prepare_content(
            recipe,
            verified,
            output_root=self.output,
            process_manifest_builder=self.build_process_manifest,
        )
        native = self.root / "private" / "game-module.dll"
        native.write_bytes(b"MZ private translated module")
        self.code.write_bytes(b"changed-code")
        with self.assertRaisesRegex(ForgeError, "code input changed"):
            package_private_module(
                Path(prepared["directory"]),
                native,
                target_triple="x86_64-pc-windows-msvc",
                translator_identity="7" * 64,
            )


if __name__ == "__main__":
    unittest.main()
