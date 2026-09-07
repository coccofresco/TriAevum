from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from ctr_rom import ExtractedFile, ExtractedTitleInputs
from forge_gui import (
    InstallRequest,
    install_private_title,
    load_active_title,
    load_recipe_options,
    match_extracted_recipe,
)


class ForgeGuiTests(unittest.TestCase):
    def test_loads_supported_recipe_labels(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "recipes.json"
            path.write_text(
                json.dumps(
                    {
                        "format": "triaevum_supported_revisions_v1",
                        "recipes": [
                            {
                                "id": "fixture-eur",
                                "title": "Fixture Title",
                                "region": "EUR",
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            self.assertEqual(load_recipe_options(path)[0].label, "Fixture Title (EUR)")

    def test_gui_install_uses_the_existing_forge_pipeline(self) -> None:
        request = InstallRequest(
            rom=Path("game.cci"),
            data_root=Path("I:/user-data"),
        )
        source = request.data_root / "sources" / "fixture"
        extracted = ExtractedTitleInputs(
            container_kind="NCSD",
            partition_index=0,
            program_id=0x0004000000033600,
            code=ExtractedFile(source / "code.bin", 4, "1" * 64),
            exheader=ExtractedFile(source / "exheader.bin", 8, "2" * 64),
            romfs=ExtractedFile(source / "romfs.bin", 16, "3" * 64),
        )
        stages: list[str] = []
        output = request.data_root / "titles"
        prepared = output / "fixture" / "content"
        with (
            patch("forge_gui.forge.query_product"),
            patch("forge_gui.forge.probe_toolchain") as probe,
            patch("forge_gui.load_catalog", return_value={"titles": []}),
            patch("forge_gui.select_title"),
            patch("forge_gui.ctr_rom.extract_decrypted_rom", return_value=extracted),
            patch("forge_gui.ctr_rom.publish_extracted_inputs", return_value=extracted),
            patch("forge_gui.match_extracted_recipe", return_value={"id": "fixture"}),
            patch("forge_gui.forge.HashCache"),
            patch(
                "forge_gui.forge.verify_sources",
                return_value=({"code": object()}, None),
            ) as verify,
            patch(
                "forge_gui.forge.prepare_content",
                return_value={"status": "prepared", "directory": str(prepared)},
            ) as prepare,
            patch(
                "forge_gui.install_precompiled_title",
                return_value={"status": "ready"},
            ) as build,
        ):
            result = install_private_title(
                request, report=lambda stage, _message: stages.append(stage)
            )
            probe.assert_not_called()

        self.assertEqual(result["status"], "ready")
        self.assertEqual(stages, ["preflight", "extract", "verify", "prepare", "activate", "ready"])
        self.assertEqual(verify.call_args.kwargs["code_path"], extracted.code.path)
        self.assertEqual(prepare.call_args.kwargs["output_root"], output.resolve())
        self.assertEqual(build.call_args.args[0], prepared.resolve())
        self.assertEqual(build.call_args.kwargs["recipe"], {"id": "fixture"})
        self.assertEqual(
            build.call_args.kwargs["active_title_state"],
            request.data_root.resolve() / "active-title.json",
        )
        self.assertEqual(
            build.call_args.kwargs["runtime_plugin"].name,
            "triaevum_title_aot.dll",
        )
        self.assertEqual(
            build.call_args.kwargs["launch_profile"].name,
            "TriAevum.launch.json",
        )

    def test_matches_a_recipe_from_extracted_rom_contents(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            extracted = ExtractedTitleInputs(
                container_kind="NCSD",
                partition_index=0,
                program_id=7,
                code=ExtractedFile(root / "code.bin", 4, "1" * 64),
                exheader=ExtractedFile(root / "exheader.bin", 8, "2" * 64),
                romfs=ExtractedFile(root / "romfs.bin", 16, "3" * 64),
            )
            recipes = root / "recipes.json"
            recipes.write_text(
                json.dumps(
                    {
                        "format": "triaevum_supported_revisions_v1",
                        "recipes": [
                            {
                                "id": "fixture",
                                "inputs": {
                                    "code": {"bytes": 4, "sha256": "1" * 64},
                                    "exheader": {"bytes": 8, "sha256": "2" * 64},
                                    "romfs": {"bytes": 16, "sha256": "3" * 64},
                                },
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            self.assertEqual(
                match_extracted_recipe(extracted, recipes)["id"], "fixture"
            )

    def test_active_title_requires_a_matching_ready_module(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            title = root / "title"
            title.mkdir()
            active = root / "active-title.json"
            active.write_text(
                json.dumps(
                    {
                        "format": "triaevum_active_title_v1",
                        "recipe": "fixture",
                        "directory": str(title),
                        "module_sha256": "a" * 64,
                    }
                ),
                encoding="utf-8",
            )
            prepared = SimpleNamespace(
                state={"module": {"status": "ready", "sha256": "a" * 64}}
            )
            with patch("forge_gui.forge.load_prepared_content", return_value=prepared):
                result = load_active_title(active)
            self.assertIsNotNone(result)
            self.assertEqual(result.recipe_id, "fixture")


if __name__ == "__main__":
    unittest.main()
