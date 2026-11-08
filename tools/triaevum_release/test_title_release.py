import json
import tempfile
import unittest
from pathlib import Path

import rust_forge_build
from title_release import RESTORATION_RESOURCES, restoration_members, retarget_recipe, title_release

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

    def test_unknown_title_is_refused(self):
        with self.assertRaises(ValueError):
            title_release("mm3d-usa")

    def test_members_are_verified_and_policy_clean(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            members = {"layout/Joker.Main.New.gar": b"new", "layout/Joker.mfpj": b"mfpj"}
            for name, data in members.items():
                (root / "src/romfs" / name).parent.mkdir(parents=True, exist_ok=True)
                (root / "src/romfs" / name).write_bytes(data)
            items = restoration_members(root / "src", fingerprints(root, members))
            self.assertEqual({item["path"]: item["role"] for item in items}, {
                f"{RESTORATION_RESOURCES}/layout/Joker.Main.New.gar": "restoration_mm3d_asset",
                f"{RESTORATION_RESOURCES}/layout/Joker.mfpj": "title_neutral_resource"})
            for item in items:
                self.assertFalse(set(item["path"].split("/")) & set(POLICY["forbidden_path_components"]))

    def test_a_changed_member_is_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = fingerprints(root, {"layout/Joker.Main.New.gar": b"new"})
            (root / "src/romfs/layout").mkdir(parents=True)
            (root / "src/romfs/layout/Joker.Main.New.gar").write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "fingerprint"):
                restoration_members(root / "src", manifest)

    def test_a_missing_member_is_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = fingerprints(root, {"layout/Joker.Main.Play.gar": b"play"})
            with self.assertRaisesRegex(ValueError, "missing"):
                restoration_members(root / "src", manifest)

    def test_real_fingerprints_pin_the_policy_gar_hashes(self):
        manifest = json.loads((Path(__file__).resolve().parents[2] /
                               "titles/mm3d/dekui/setup/setup-fingerprints.json").read_text(encoding="utf-8"))
        gar = sorted({e["input_sha256"] for e in manifest["files"]
                     if e.get("input_root") == "restoration" and e["input"].endswith(".gar")})
        self.assertEqual(gar, sorted(POLICY["pinned_role_sha256"]["restoration_mm3d_asset"]))

    def test_forge_features_reach_cargo(self):
        self.assertEqual(rust_forge_build.feature_arguments(()), [])
        self.assertEqual(rust_forge_build.feature_arguments(("forge-cli/title-mm3d",)),
                         ["--features", "forge-cli/title-mm3d"])


if __name__ == "__main__":
    unittest.main()
