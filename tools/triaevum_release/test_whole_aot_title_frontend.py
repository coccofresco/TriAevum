"""Per-title whole-AOT frontend (owner decision 2026-11-08: Forge is per title).

The Forge's whole-AOT source frontend is selected by the prepared recipe's
title: OoT3D keeps the shared A32 tooling's inputs, MM3D reads its own title
facts from titles/mm3d/a32/whole_aot_frontend.json.
"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import forge
from forge import ForgeError, recipe_title
from whole_aot_source_backend import (
    A32_ROOT,
    REPO_ROOT,
    WholeAotSourceError,
    load_default_frontend,
    load_title_frontend,
    title_frontend_paths,
)

MM3D_RECIPE = "mm3d-eur-local-6eec27b1"
OOT3D_RECIPE = "oot3d-eur-project-baseline-16a6b0aa"
MM3D_TITLE = (REPO_ROOT / "titles/mm3d").resolve()


class RecipeTitleTests(unittest.TestCase):
    def test_mm3d_recipe_selects_mm3d(self) -> None:
        self.assertEqual(recipe_title(MM3D_RECIPE), "mm3d")

    def test_oot3d_and_private_recipes_keep_oot3d(self) -> None:
        self.assertEqual(recipe_title(OOT3D_RECIPE), "oot3d")
        self.assertEqual(recipe_title("fixture"), "oot3d")

    def test_invalid_recipe_is_refused(self) -> None:
        with self.assertRaises(ForgeError):
            recipe_title("")
        with self.assertRaises(ForgeError):
            recipe_title(None)

    def test_recipe_in_two_title_catalogs_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            both = Path(temporary) / "both.json"
            both.write_text(
                json.dumps({"recipes": [{"id": MM3D_RECIPE}]}), encoding="utf-8"
            )
            catalogs = {
                "oot3d": ("recipes/none.json", str(both)),
                "mm3d": forge.TITLE_RECIPE_CATALOGS["mm3d"],
            }
            with (
                patch.object(forge, "TITLE_RECIPE_CATALOGS", catalogs),
                patch.object(forge, "distribution_path", side_effect=lambda p: Path(p) if Path(p).is_absolute() else REPO_ROOT / p),
            ):
                with self.assertRaises(ForgeError):
                    recipe_title(MM3D_RECIPE)


class TitleFrontendTests(unittest.TestCase):
    def test_oot3d_default_is_unchanged(self) -> None:
        frontend = load_default_frontend()
        self.assertEqual(frontend, load_default_frontend("oot3d"))
        self.assertEqual(
            frontend.product_manifest, A32_ROOT / "whole_aot_product_manifest.json"
        )
        self.assertEqual(frontend.selection, A32_ROOT / "whole_aot_functions.json")

    def test_mm3d_descriptor_names_only_mm3d_inputs(self) -> None:
        descriptor, paths, base = title_frontend_paths("mm3d")
        self.assertEqual(base, 0x100000)
        self.assertEqual(
            descriptor, (REPO_ROOT / "titles/mm3d/a32/whole_aot_frontend.json").resolve()
        )
        oot_inputs = {
            path.resolve()
            for path in (
                load_default_frontend("oot3d").inventory,
                load_default_frontend("oot3d").boundary_audit,
                load_default_frontend("oot3d").product_manifest,
                load_default_frontend("oot3d").selection,
            )
        }
        for role, path in paths.items():
            self.assertIn(MM3D_TITLE, path.parents, role)
            self.assertNotIn(path, oot_inputs, role)
        # The inventory, supplemental entries and boundary audit exist today.
        for role in ("inventory", "supplemental_entries", "boundary_audit"):
            self.assertTrue(paths[role].is_file(), role)

    def test_mm3d_frontend_loads_the_product_contract(self) -> None:
        # The descriptor's inputs are the CATALOGS product contract (OoT3D's
        # shape): selection, boundary audit and the Forge-built inventory carry
        # exactly the SHA-256 values the manifest pins.
        import hashlib

        frontend = load_default_frontend("mm3d")
        self.assertEqual(frontend.default_base, 0x100000)
        manifest = json.loads(frontend.product_manifest.read_text(encoding="utf-8"))
        sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
        self.assertEqual(
            (REPO_ROOT / manifest["selection"]["path"]).resolve(), frontend.selection
        )
        self.assertEqual(sha(frontend.selection), manifest["selection"]["sha256"])
        self.assertEqual(
            sha(frontend.boundary_audit), manifest["frontend"]["boundary_audit_sha256"]
        )
        with tempfile.TemporaryDirectory() as temporary:
            built = Path(temporary) / "inventory.csv"
            frontend.add_local_entry_intervals(
                frontend.inventory, frontend.supplemental_entries, built, 0x100000
            )
            self.assertEqual(sha(built), manifest["frontend"]["inventory_sha256"])
        self.assertEqual(
            [flag for flag, _ in frontend.extraction_inputs],
            ["--inline-argument-calls", "--thumb-ranges"],
        )
        for _, path in frontend.extraction_inputs:
            self.assertIn(MM3D_TITLE, path.parents)
        self.assertEqual(load_default_frontend("oot3d").extraction_inputs, ())
        roles = {role for role, _ in frontend.identity_files}
        self.assertIn("titles/mm3d/a32/whole_aot_frontend.json", roles)
        self.assertIn("titles/mm3d/a32/thumb_ranges.csv", roles)

    def test_negative_missing_selection_or_empty_optional_role(self) -> None:
        facts = json.loads(
            (MM3D_TITLE / "a32/whole_aot_frontend.json").read_text(encoding="utf-8")
        )
        with tempfile.TemporaryDirectory(dir=MM3D_TITLE / "a32") as temporary:
            descriptor = Path(temporary) / "frontend.json"
            missing = dict(facts, selection="titles/mm3d/a32/absent_functions.json")
            descriptor.write_text(json.dumps(missing), encoding="utf-8")
            with self.assertRaisesRegex(WholeAotSourceError, "selection="):
                load_title_frontend("mm3d", descriptor)
            empty = dict(facts, thumb_ranges="")
            descriptor.write_text(json.dumps(empty), encoding="utf-8")
            with self.assertRaisesRegex(WholeAotSourceError, "thumb_ranges is not named"):
                title_frontend_paths("mm3d", descriptor)

    def test_mm3d_frontend_with_complete_inputs(self) -> None:
        # A descriptor whose contract files exist (stand-ins inside titles/mm3d)
        # builds the MM3D frontend from MM3D inputs only.
        with tempfile.TemporaryDirectory(dir=MM3D_TITLE / "a32") as temporary:
            root = Path(temporary)
            (root / "manifest.json").write_text("{}", encoding="utf-8")
            (root / "selection.json").write_text("{}", encoding="utf-8")
            rel = root.resolve().relative_to(REPO_ROOT.resolve()).as_posix()
            facts = json.loads(
                (MM3D_TITLE / "a32/whole_aot_frontend.json").read_text(encoding="utf-8")
            )
            facts["product_manifest"] = f"{rel}/manifest.json"
            facts["selection"] = f"{rel}/selection.json"
            descriptor = root / "frontend.json"
            descriptor.write_text(json.dumps(facts), encoding="utf-8")
            frontend = load_title_frontend("mm3d", descriptor)
            self.assertEqual(
                frontend.inventory, (MM3D_TITLE / "a32/whole_aot_inventory.csv").resolve()
            )
            self.assertEqual(frontend.selection, (root / "selection.json").resolve())
            for _, path in frontend.identity_files:
                self.assertTrue(path.is_file(), path)
            self.assertNotIn(
                "tools/oot3d/native_a32_runtime/whole_aot_product_manifest.json",
                {role for role, _ in frontend.identity_files},
            )

    def test_negative_unknown_title_and_foreign_inputs(self) -> None:
        with self.assertRaises(WholeAotSourceError):
            load_default_frontend("generic")
        with tempfile.TemporaryDirectory(dir=MM3D_TITLE / "a32") as temporary:
            facts = json.loads(
                (MM3D_TITLE / "a32/whole_aot_frontend.json").read_text(encoding="utf-8")
            )
            # An MM3D descriptor pointing at OoT3D's product contract is refused.
            facts["product_manifest"] = (
                "tools/oot3d/native_a32_runtime/whole_aot_product_manifest.json"
            )
            descriptor = Path(temporary) / "frontend.json"
            descriptor.write_text(json.dumps(facts), encoding="utf-8")
            with self.assertRaisesRegex(WholeAotSourceError, "not a mm3d title input"):
                title_frontend_paths("mm3d", descriptor)
            facts["title"] = "oot3d"
            descriptor.write_text(json.dumps(facts), encoding="utf-8")
            with self.assertRaises(WholeAotSourceError):
                title_frontend_paths("mm3d", descriptor)


class ForgeCallSiteTests(unittest.TestCase):
    def test_prepare_ir_selects_frontend_by_recipe_title(self) -> None:
        seen: list[str] = []

        def fake_prepare(**kwargs):
            seen.append(kwargs["title"])
            raise RuntimeError("stop after selection")

        class Prepared:
            index = {"recipe": MM3D_RECIPE}
            inputs = {
                "code": type("I", (), {"path": Path("c"), "bytes": 1, "sha256": "0" * 64}),
                "exheader": type("I", (), {"path": Path("e"), "bytes": 1, "sha256": "0" * 64}),
            }
            state: dict = {}

        with (
            patch.object(forge, "load_prepared_content", return_value=Prepared()),
            patch.object(forge, "prepare_whole_aot_ir", side_effect=fake_prepare),
        ):
            with self.assertRaisesRegex(RuntimeError, "stop after selection"):
                forge.prepare_private_aot_ir(Path("."), cache_root=Path("."))
        self.assertEqual(seen, ["mm3d"])


if __name__ == "__main__":
    unittest.main()
