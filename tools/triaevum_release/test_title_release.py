import json
import tempfile
import unittest
from pathlib import Path

import rust_forge_build
from title_release import (RESTORATION_RESOURCES, ROOT, compose_title_layout, restoration_members,
                           retarget_recipe, title_release)

POLICY = json.loads((Path(__file__).parent / "release_policy.json").read_text(encoding="utf-8"))


def fingerprints(root: Path, members: dict[str, bytes]) -> Path:
    import hashlib
    files = [{"input_root": "restoration", "input": f"romfs/{name}", "output": f"romfs/{name}",
              "input_sha256": hashlib.sha256(data).hexdigest()} for name, data in members.items()]
    files.append({"input_root": "original", "input": "romfs/x.bin", "output": "romfs/x.bin",
                  "input_sha256": "00"})
    path = root / "fingerprints.json"
    path.write_text(json.dumps({"files": files}), encoding="utf-8")
    return path


class TitleReleaseTests(unittest.TestCase):
    def test_oot3d_release_is_unchanged(self):
        facts = title_release("oot3d")
        self.assertEqual(facts["recipe_path"], "recipes/oot3d.json")
        self.assertEqual(facts["forge_features"], ())
        layout = [{"source": "tools/triaevum_release/supported_revisions.json",
                   "path": "recipes/oot3d.json", "role": "revision_recipe"}]
        retarget_recipe(layout, "oot3d")
        self.assertEqual(layout[0]["path"], "recipes/oot3d.json")

    def test_mm3d_release_has_its_own_recipe_and_forge_feature(self):
        facts = title_release("mm3d")
        layout = [{"source": "x", "path": "recipes/oot3d.json", "role": "revision_recipe"}]
        retarget_recipe(layout, "mm3d")
        self.assertEqual(layout[0]["path"], "recipes/mm3d.json")
        self.assertTrue(Path(layout[0]["source"]).is_file())
        recipes = json.loads(Path(layout[0]["source"]).read_text(encoding="utf-8"))
        self.assertEqual([r["id"] for r in recipes["recipes"]], [facts["default_recipe"]])
        self.assertIn("forge-cli/title-mm3d", facts["forge_features"])
        # The OoT3D input coverage (base recipe oot3d-eur-...) is not MM3D's.
        self.assertIsNone(facts["input_coverage"])
        self.assertEqual(title_release("oot3d")["input_coverage"].name, "input_coverage")
        # Every Windows Forge binary is per title, the update helper too (its
        # own steel-mask icon, owner decision 2026-11-08): each crate's feature.
        for crate in ("forge-desktop", "forge-cli", "forge-update-helper"):
            self.assertIn(f"{crate}/title-mm3d", facts["forge_features"])

    def test_composition_reshapes_only_the_mm3d_layout(self):
        layout = json.loads((ROOT / "tools/triaevum_release/runtime_release_layout.example.json").read_text())
        oot3d = json.loads(json.dumps(layout["files"]))
        compose_title_layout(oot3d, "oot3d")
        self.assertEqual(oot3d, layout["files"])
        mm3d = json.loads(json.dumps(layout["files"]))
        compose_title_layout(mm3d, "mm3d")
        paths = {item["path"]: item for item in mm3d}
        # Only the SingleScreen 3.1 HD font stays: the MM3D Forge builds the HD message font from it.
        self.assertEqual([p for p in paths if p.startswith("resources/topscreen/")],
                         ["resources/topscreen/singlescreen-3.1/ltn16.qbf"])
        self.assertIn("LICENSES/SingleScreen-3.0.md", paths)
        for gone in ("LICENSES/OoT3D-Vanilla-UI-HUD.md",
                     "LICENSES/Vanilla-Plus-Link-Remodel.md", "docs/releases/v0.8.0-beta.2.md", "docs/USER_GUIDE.md"):
            self.assertNotIn(gone, paths)
        self.assertEqual(paths["README.md"]["source"], "titles/mm3d/releases/README.md")
        for added in ("LICENSES/mm3d-dekui.md", "LICENSES/Project-Restoration.md",
                      "docs/MOONFALL_CURSE.md", "docs/releases/v0.5.0-alpha.1.md"):
            self.assertIn(added, paths)
            self.assertTrue((ROOT / paths[added]["source"]).is_file(), added)
        self.assertIn("LICENSES/iMMERSE-MXAO.md", paths)
        self.assertTrue((ROOT / "titles/mm3d/releases/README.md").read_text(encoding="utf-8")
                        .startswith("# Moonfall Curse\n"))

    def test_composition_refuses_a_stale_drop(self):
        layout = [{"source": "README.md", "path": "README.md", "role": "documentation"}]
        with self.assertRaisesRegex(ValueError, "names paths the layout lacks"):
            compose_title_layout(layout, "mm3d")

    def test_unknown_title_is_refused(self):
        with self.assertRaises(ValueError):
            title_release("mm3d-usa")

    @staticmethod
    def synthetic_release(root: Path):
        """A recipe folder and a release tree built by the real generator."""
        import restoration_layout as rl

        def gar(members):
            return rl.emit_gar({"hash": "jenkins", "types": ["unknown", "mfpk", "mfl", "ctxb"], "align": 4,
                                "members": members})
        play = gar([("Play.mfpk", b"P" * 16), ("Mod.mfl", b"M" * 32), ("Hud.mfl", b"H" * 32)])
        project = b"MFPJ" + b"\1" * 28
        release = {"layout/Joker.Main.Play.gar": gar([("Play.mfpk", b"P" * 16), ("Mod.mfl", b"M" * 31 + b"m")]),
                   "layout/Joker.Main.New.gar": gar([("Hud.mfl", b"H" * 16 + b"h" * 16), ("t.ctxb", b"T" * 32)]),
                   "layout/Joker.mfpj": project + b"New"}
        recipe, files = rl.build(play, project, release)
        recipe_dir = root / "recipe"
        for path, data in files.items():
            if "/patches/" in path:
                (recipe_dir / path).parent.mkdir(parents=True, exist_ok=True)
                (recipe_dir / path).write_bytes(data)
        recipe_dir.mkdir(exist_ok=True)
        (recipe_dir / "layout.recipe.json").write_bytes(rl.recipe_bytes(recipe))
        for name, data in release.items():
            (root / "src/romfs" / name).parent.mkdir(parents=True, exist_ok=True)
            (root / "src/romfs" / name).write_bytes(data)
        return recipe_dir, fingerprints(root, release)

    def test_members_are_recipe_patches_and_original_members_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            recipe_dir, manifest = self.synthetic_release(root)
            items = restoration_members(root / "src", root / "staging", manifest, recipe_dir)
            self.assertEqual({item["path"]: item["role"] for item in items}, {
                f"{RESTORATION_RESOURCES}/layout.recipe.json": "restoration_layout_recipe",
                f"{RESTORATION_RESOURCES}/layout/patches/Main.Play.Mod.mfl.bps": "restoration_patch",
                f"{RESTORATION_RESOURCES}/layout/patches/Main.New.Hud.mfl.bps": "restoration_patch",
                f"{RESTORATION_RESOURCES}/layout/patches/Joker.mfpj.bps": "restoration_patch",
                f"{RESTORATION_RESOURCES}/layout/members/t.ctxb": "restoration_original_member"})
            self.assertEqual((root / "staging/layout/members/t.ctxb").read_bytes(), b"T" * 32)
            for item in items:
                self.assertFalse(set(item["path"].split("/")) & set(POLICY["forbidden_path_components"]))
                self.assertNotIn(Path(item["path"]).suffix, (".gar", ".mfpj"))

    def test_a_changed_member_is_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            recipe_dir, manifest = self.synthetic_release(root)
            (root / "src/romfs/layout/Joker.Main.New.gar").write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "fingerprint"):
                restoration_members(root / "src", root / "staging", manifest, recipe_dir)

    def test_a_missing_member_is_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            recipe_dir, manifest = self.synthetic_release(root)
            (root / "src/romfs/layout/Joker.mfpj").unlink()
            with self.assertRaisesRegex(ValueError, "missing"):
                restoration_members(root / "src", root / "staging", manifest, recipe_dir)

    def test_a_changed_patch_is_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            recipe_dir, manifest = self.synthetic_release(root)
            (recipe_dir / "layout/patches/Joker.mfpj.bps").write_bytes(b"BPS1 changed")
            with self.assertRaisesRegex(ValueError, "does not match the recipe"):
                restoration_members(root / "src", root / "staging", manifest, recipe_dir)

    def test_real_recipe_files_are_the_policy_pins(self):
        recipe_path = Path(__file__).resolve().parents[2] / "titles/mm3d/restoration/package/layout.recipe.json"
        import hashlib
        recipe = json.loads(recipe_path.read_text(encoding="utf-8"))
        pins = POLICY["pinned_role_sha256"]
        self.assertEqual(pins["restoration_layout_recipe"], [hashlib.sha256(recipe_path.read_bytes()).hexdigest()])
        self.assertEqual(sorted(pins["restoration_patch"]),
                         sorted(d for p, d in recipe["files"].items() if "/patches/" in p))
        self.assertEqual(sorted(pins["restoration_original_member"]),
                         sorted(d for p, d in recipe["files"].items() if "/members/" in p))

    def test_forge_features_reach_cargo(self):
        self.assertEqual(rust_forge_build.feature_arguments(()), [])
        self.assertEqual(rust_forge_build.feature_arguments(("forge-cli/title-mm3d",)),
                         ["--features", "forge-cli/title-mm3d"])
        # The update helper is Windows-only: its feature is dropped on Linux.
        mm3d = title_release("mm3d")["forge_features"]
        linux = rust_forge_build.target_features("x86_64-unknown-linux-gnu", mm3d)
        self.assertEqual(linux, ("forge-desktop/title-mm3d", "forge-cli/title-mm3d"))
        self.assertEqual(rust_forge_build.target_features("x86_64-pc-windows-msvc", mm3d), tuple(mm3d))
        self.assertEqual(rust_forge_build.target_features("x86_64-unknown-linux-gnu", ()), ())
        # C sources (ring) get the same path remapping on Linux, not on Windows.
        linux_flags = rust_forge_build.c_flags("x86_64-unknown-linux-gnu", Path("/work/target"), "-O2")
        self.assertTrue(linux_flags.startswith("-O2 -ffile-prefix-map="))
        self.assertIn("=/cargo", linux_flags)
        self.assertIn("=/target", linux_flags)
        self.assertEqual(rust_forge_build.c_flags("x86_64-pc-windows-msvc", Path("/work/target"), "/O2"), "/O2")
        # Negatives: a feature of no Forge package, a bare feature.
        for bad in (("forge-other/title-mm3d",), ("title-mm3d",)):
            with self.assertRaisesRegex(ValueError, "Not a Forge package feature"):
                rust_forge_build.target_features("x86_64-unknown-linux-gnu", bad)


if __name__ == "__main__":
    unittest.main()
