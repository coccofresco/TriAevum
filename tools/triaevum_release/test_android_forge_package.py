import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

from common import atomic_write_json
from precompiled_title_layout import artifact
from precompiled_titles import FORMAT, MODEL, CATALOG
from release_platform import ANDROID, WINDOWS
from android_forge_package import (ASSET_ROOT, INVENTORY, SIGNATURE, create_package, require_arm64_library,
                                   sign_android_package, verify_android_package)
import release_signature


def elf(path, machine=183, payload=b""):
    path.parent.mkdir(parents=True, exist_ok=True)
    header = bytearray(64)
    header[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<HH", header, 16, 3, machine)
    path.write_bytes(bytes(header) + payload)


class Fixture:
    def __init__(self, root: Path):
        self.reference = root / "reference"
        self.stage = root / "stage"
        self.output = root / "output"
        files = []

        def write(relative, data, role):
            path = self.reference / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            files.append(dict(artifact(path, relative), role=role))
            return artifact(path, relative)

        build = {"plugin_sha256": "1" * 64, "code_sha256": "2" * 64,
                 "translator_identity_sha256": "3" * 64, "program_sha256": "4" * 64}
        source = self.reference / "source/titles/fixture-translated.zip"
        source.parent.mkdir(parents=True)
        with zipfile.ZipFile(source, "w") as archive:
            archive.writestr("TITLE_SOURCE_MANIFEST.json", json.dumps({
                "format": "triaevum_translated_title_source_v1", "build": build,
                "files": {"function.cpp": "5" * 64}}))
        files.append(dict(artifact(source, "source/titles/fixture-translated.zip"), role="translated_title_source"))
        compiler = write("forge/oot3d_native_pica_aot_compiler.exe", b"MZ compiler", "shader_preparation_tool")
        shaderc = write("forge/shaderc_shared.dll", b"MZ shaderc", "forge_runtime_module")
        helper = write("forge/oot3d_native_nri_pipeline_prepare.exe", b"MZ helper", "shader_preparation_tool")
        write("TriAevum.exe", b"MZ runtime", "runtime_executable")
        write("LICENSE", b"license", "license")
        write("recipes/oot3d.json", b"{}", "revision_recipe")
        write("resources/gamecontrollerdb.txt", b"pads", "title_neutral_resource")
        write("resources/topscreen/singlescreen-3.0/custom_ui01.ctxb", b"atlas", "topscreen_singlescreen_asset")
        write("config/TriAevum.json", b"{}", "configuration")
        pipelines = write("forge/shader-corpus/pipelines-0.json", b"{\"pipelines\": []}", "portable_pipeline_recipes")
        pack = write("forge/shader-corpus/portable.o3ps", b"O3PS pack", "portable_shader_corpus")
        lights = write("forge/precomputed/scene-lights.json", b"{}", "precomputed_scene_lights")
        self.title = {
            "recipe": "fixture", "inputs": {"code": {"sha256": "2" * 64, "bytes": 4}},
            "abi_version": 2, "target": WINDOWS.target,
            "plugin": {"path": "titles/fixture/triaevum_title_aot.dll", "sha256": "1" * 64, "bytes": 9},
            "translator_identity_sha256": "3" * 64,
            "sources": [artifact(source, "source/titles/fixture-translated.zip")],
            "renderer_shader_preparation": {"format": "triaevum_renderer_shader_compiler_v1",
                                            "compiler": compiler, "dependencies": [shaderc]},
            "shader_preparation": {"format": "triaevum_shader_preparation_v1", "mode": "portable_pack",
                                   "pack": pack, "compiler": compiler, "dependencies": [shaderc]},
            "device_pipeline_preparation": {"format": "triaevum_device_pipeline_preparation_v1",
                                            "helper": helper, "dependencies": [], "manifests": [pipelines],
                                            "startup_programs": True},
            "precomputed_tables": {"format": "triaevum_precomputed_tables_v1", "scene_lights": lights},
        }
        self.catalog = {"format": FORMAT, "install_model": MODEL, "target": WINDOWS.target,
                        "runtime": {"path": WINDOWS.runtime, "sha256": "6" * 64, "bytes": 10},
                        "native_module": {"path": WINDOWS.native_module, "sha256": "7" * 64, "bytes": 1},
                        "titles": [self.title]}
        atomic_write_json(self.reference / CATALOG, self.catalog)
        files.append({"path": CATALOG, "role": "precompiled_catalog", "sha256": "0" * 64, "bytes": 1})
        self.files = files
        self.write_manifest()
        elf(self.stage / "arm64-v8a/libTriAevum.so", payload=b"runtime")
        elf(self.stage / "arm64-v8a/libtriaevum_title_aot.so", payload=b"title")
        elf(self.stage / "arm64-v8a/libtriaevum_forge.so", payload=b"forge")

    def write_manifest(self):
        atomic_write_json(self.reference / "release-manifest.json",
                          {"format": "triaevum_public_release_manifest_v1", "release": {}, "files": self.files})

    def create(self):
        return create_package(self.reference, self.stage, self.output, source_commit="8" * 40)


class AndroidForgePackageTests(unittest.TestCase):
    def test_every_release_policy_role_is_classified(self):
        import android_forge_package
        policy = json.loads((Path(__file__).parent / "release_policy.json").read_text())
        roles = set(policy["allowed_roles"])
        for platform in [policy, *policy.get("platforms", {}).values()]:
            roles |= set(platform.get("allowed_roles", {}))
            for profile in platform.get("forge_profiles", {}).values():
                roles |= set(profile.get("allowed_roles", {}))
        classified = android_forge_package.DATA_ROLES | android_forge_package.EXCLUDED_ROLES
        self.assertEqual(sorted(roles - classified), [])
        self.assertEqual(android_forge_package.DATA_ROLES & android_forge_package.EXCLUDED_ROLES, set())

    def test_requires_aarch64_shared_libraries(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "lib.so"
            elf(path, machine=62)
            with self.assertRaisesRegex(ValueError, "AArch64"):
                require_arm64_library(path)
            path.write_bytes(b"MZ" + bytes(64))
            with self.assertRaisesRegex(ValueError, "ELF64"):
                require_arm64_library(path)
            elf(path)
            require_arm64_library(path)

    def test_binds_the_runtime_library_and_copies_only_data(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            inventory = fixture.create()
            package = fixture.output / ASSET_ROOT
            catalog = json.loads((package / CATALOG).read_text())
            runtime = artifact(fixture.stage / "arm64-v8a/libTriAevum.so", ANDROID.runtime)
            title = catalog["titles"][0]
            self.assertEqual(catalog["target"], ANDROID.target)
            self.assertEqual(catalog["runtime"], runtime)
            self.assertNotIn("native_module", catalog)
            self.assertEqual(title["target"], ANDROID.target)
            self.assertEqual(title["plugin"]["path"], "lib/arm64-v8a/libtriaevum_title_aot.so")
            self.assertEqual(title["platform_build"]["build_target"], "ports/android/native")
            for key in ("inputs", "sources", "precomputed_tables", "translator_identity_sha256"):
                self.assertEqual(title[key], fixture.title[key])
            # The runtime library is the pass compiler and the device helper.
            for key in ("renderer_shader_preparation", "shader_preparation"):
                self.assertEqual(title[key]["compiler"], runtime)
                self.assertEqual(title[key]["dependencies"], [])
            self.assertEqual(title["shader_preparation"]["pack"], fixture.title["shader_preparation"]["pack"])
            self.assertEqual(title["device_pipeline_preparation"]["helper"], runtime)
            self.assertEqual(title["device_pipeline_preparation"]["manifests"],
                             fixture.title["device_pipeline_preparation"]["manifests"])
            copied = sorted(path.relative_to(package).as_posix() for path in package.rglob("*") if path.is_file())
            self.assertEqual(copied, sorted([
                CATALOG, INVENTORY, "recipes/oot3d.json", "resources/gamecontrollerdb.txt",
                "resources/topscreen/singlescreen-3.0/custom_ui01.ctxb",
                "forge/shader-corpus/pipelines-0.json", "forge/shader-corpus/portable.o3ps",
                "forge/precomputed/scene-lights.json"]))
            self.assertEqual(json.loads((package / INVENTORY).read_text()), inventory)
            forge = artifact(fixture.stage / "arm64-v8a/libtriaevum_forge.so", ANDROID.forge)
            self.assertEqual(inventory["libraries"], [runtime, title["plugin"], forge])
            self.assertEqual(inventory["corresponding_source"], ["source/titles/fixture-translated.zip"])
            self.assertEqual(json.loads((fixture.reference / CATALOG).read_text()), fixture.catalog)
            with self.assertRaisesRegex(ValueError, "not empty"):
                fixture.create()

    def test_unclassified_roles_and_undeclared_data_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            fixture.files.append({"path": "mods/pack.zip", "role": "mod_payload", "sha256": "9" * 64, "bytes": 1})
            fixture.write_manifest()
            with self.assertRaisesRegex(ValueError, "Unclassified reference release role: mod_payload"):
                fixture.create()
            fixture.files.pop()
            fixture.files = [item for item in fixture.files if item["role"] != "portable_shader_corpus"]
            fixture.write_manifest()
            with self.assertRaisesRegex(ValueError, "not in the reference release: forge/shader-corpus/portable.o3ps"):
                fixture.create()
            self.assertFalse(fixture.output.exists())

    def test_rust_forge_library_is_required(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            (fixture.stage / "arm64-v8a/libtriaevum_forge.so").unlink()
            with self.assertRaises(OSError):
                fixture.create()
            self.assertFalse(fixture.output.exists())
            # A library built without path remapping carries the builder's home folder.
            elf(fixture.stage / "arm64-v8a/libtriaevum_forge.so",
                payload=b"panic at C:\\Users\\builder\\.cargo\\registry\\src\\index\\serde\\lib.rs")
            with self.assertRaisesRegex(ValueError, "build-machine path"):
                fixture.create()
            self.assertFalse(fixture.output.exists())

    def test_different_title_builds_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            other = dict(fixture.title, recipe="other", translator_identity_sha256="a" * 64)
            fixture.catalog["titles"].append(other)
            atomic_write_json(fixture.reference / CATALOG, fixture.catalog)
            with self.assertRaisesRegex(ValueError, "rebuilt translated title"):
                fixture.create()


class AndroidForgePackageTitleTests(unittest.TestCase):
    def test_title_and_notices(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            inventory = create_package(fixture.reference, fixture.stage, fixture.output,
                                       source_commit="8" * 40, title="oot3d")
            self.assertEqual(inventory["title"], "oot3d")
            self.assertEqual([item["path"] for item in inventory["notices"]], ["LICENSE"])
            self.assertEqual((fixture.output / "notices/LICENSE").read_bytes(), b"license")
            self.assertFalse((fixture.output / ASSET_ROOT / "LICENSE").exists())

    def test_windows_reference_manifest_under_docs(self):
        # A Windows desktop package keeps its manifest at docs/release-manifest.json.
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            (fixture.reference / "docs").mkdir(exist_ok=True)
            (fixture.reference / "release-manifest.json").replace(fixture.reference / "docs/release-manifest.json")
            inventory = create_package(fixture.reference, fixture.stage, fixture.output,
                                       source_commit="8" * 40, title="oot3d")
            self.assertEqual([item["path"] for item in inventory["notices"]], ["LICENSE"])

    def test_negative_other_title(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            with self.assertRaisesRegex(ValueError, "is the oot3d release, not mm3d"):
                create_package(fixture.reference, fixture.stage, fixture.output, source_commit="8" * 40,
                               title="mm3d")
            self.assertFalse(fixture.output.exists())

    def test_policy_pinned_topscreen_asset_is_release_data(self):
        # The SingleScreen 3.0 .zar of the desktop release (manifest role
        # topscreen_singlescreen_asset, pinned by the release policy) is copied.
        real = Path(__file__).resolve().parents[2] / "runtime/triaevum_resources/topscreen/singlescreen-3.0/zelda_link_ext.zar"
        name = "resources/topscreen/singlescreen-3.0/zelda_link_ext.zar"
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            path = fixture.reference / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(real.read_bytes())
            fixture.files.append(dict(artifact(path, name), role="topscreen_singlescreen_asset"))
            fixture.write_manifest()
            fixture.create()
            self.assertTrue(any(p.name == "zelda_link_ext.zar" for p in fixture.output.rglob("*")))
        # A .zar that is not the pinned asset (other bytes, or another role) is still refused.
        for role, data in (("topscreen_singlescreen_asset", b"other zar"), ("title_neutral_resource", real.read_bytes())):
            with self.subTest(role=role), tempfile.TemporaryDirectory() as directory:
                fixture = Fixture(Path(directory))
                path = fixture.reference / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
                fixture.files.append(dict(artifact(path, name), role=role))
                fixture.write_manifest()
                with self.assertRaisesRegex(ValueError, "holds game content"):
                    fixture.create()

    def test_negative_game_content_and_missing_licences(self):
        for name in ("restoration/actors.gar", "restoration/title.mfpj", "resources/romfs/a.bin"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                fixture = Fixture(Path(directory))
                path = fixture.reference / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"data")
                fixture.files.append(dict(artifact(path, name), role="restoration_original_member"))
                fixture.write_manifest()
                with self.assertRaisesRegex(ValueError, "holds game content"):
                    fixture.create()
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            fixture.files = [item for item in fixture.files if item["role"] != "license"]
            fixture.write_manifest()
            with self.assertRaisesRegex(ValueError, "no licence files"):
                fixture.create()



class AndroidForgePackageSignatureTests(unittest.TestCase):
    """The publisher signature forge-android verifies on the device (A5)."""

    def _signed(self, root: Path):
        fixture = Fixture(root / "work")
        fixture.create()
        entry = release_signature.generate_key(root / "keys/test.pem")
        keys = root / "keys/keys.json"
        keys.write_text(json.dumps({"format": release_signature.KEYS_FORMAT, "keys": [entry]}), encoding="utf-8")
        package = fixture.output / ASSET_ROOT
        sign_android_package(package, root / "keys/test.pem", keys)
        return package, keys

    def test_signed_package_verifies(self):
        with tempfile.TemporaryDirectory() as directory:
            package, keys = self._signed(Path(directory))
            self.assertTrue((package / SIGNATURE).is_file())
            self.assertEqual(verify_android_package(package, keys), [])

    def test_negative_tampered_file_inventory_or_missing_signature(self):
        with tempfile.TemporaryDirectory() as directory:
            package, keys = self._signed(Path(directory))
            data = sorted(path for path in package.rglob("*") if path.is_file()
                          and path.name not in (INVENTORY, SIGNATURE, CATALOG))[0]
            data.write_bytes(data.read_bytes() + b"x")
            self.assertIn("does not match", " ".join(verify_android_package(package, keys)))
            (package / INVENTORY).write_bytes((package / INVENTORY).read_bytes() + b" ")
            self.assertIn("another manifest", " ".join(verify_android_package(package, keys)))
            (package / SIGNATURE).unlink()
            self.assertIn("not signed", " ".join(verify_android_package(package, keys)))

    def test_negative_untrusted_key(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = Fixture(root / "work")
            fixture.create()
            release_signature.generate_key(root / "keys/other.pem")
            with self.assertRaises(release_signature.SigningError):
                sign_android_package(fixture.output / ASSET_ROOT, root / "keys/other.pem")


class ShaderCorpusSubstitutionTests(unittest.TestCase):
    def test_android_corpus_replaces_the_reference_corpus_in_data_and_catalog(self):
        from unittest.mock import patch
        import android_forge_package as module
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            corpus = root / "android"
            corpus.mkdir()
            for name, content in zip(module.CORPUS_FILES, (b"pack", b"{}", b"{}")):
                (corpus / name).write_bytes(content)
            old = root / "old.o3ps"
            old.write_bytes(b"old")
            data = {"forge/shader-corpus/portable.o3ps": ({"path": "forge/shader-corpus/portable.o3ps",
                                                          "role": "portable_shader_corpus"}, old),
                    "forge/shader-corpus/pipelines-1.json": ({"path": "forge/shader-corpus/pipelines-1.json",
                                                             "role": "portable_pipeline_recipes"}, old),
                    "data/other.bin": ({"path": "data/other.bin", "role": "revision_recipe"}, old)}
            result = {"titles": [{"shader_preparation": {"mode": "portable_pack", "descriptor_schema_version": 2},
                                  "device_pipeline_preparation": {"manifests": [{}, {}]}}]}
            seen = {}
            with patch.object(module, "read_portable_corpus", return_value=(3, None)), \
                    patch.object(module, "read_pipeline_manifest", return_value={}), \
                    patch.object(module, "validate_qualification",
                                 side_effect=lambda record, **kwargs: seen.update(kwargs)):
                module.substitute_shader_corpus(result, data, corpus, root)
            self.assertEqual(sorted(data), ["data/other.bin", "forge/shader-corpus/pipelines-0.json",
                                            "forge/shader-corpus/portable.o3ps", module.QUALIFICATION_PATH])
            self.assertEqual(data["forge/shader-corpus/portable.o3ps"][1], corpus / "expanded-union.o3ps")
            title = result["titles"][0]
            self.assertEqual(title["shader_preparation"]["descriptor_schema_version"], 3)
            self.assertEqual(title["shader_preparation"]["pack"]["sha256"],
                             data["forge/shader-corpus/portable.o3ps"][0]["sha256"])
            self.assertEqual(len(title["device_pipeline_preparation"]["manifests"]), 1)
            self.assertEqual(seen["pack_sha256"], title["shader_preparation"]["pack"]["sha256"])

    def test_missing_corpus_file_is_refused(self):
        import android_forge_package as module
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "lacks"):
                module.substitute_shader_corpus({"titles": []}, {}, Path(directory), Path(directory))


if __name__ == "__main__":
    unittest.main()
