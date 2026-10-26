"""The Rust Forge release profile: accepted packages and known-bad fixtures."""

import hashlib
import json
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from archive_release import create_release_archive
from audit_release import DEFAULT_POLICY, audit_release
from common import atomic_write_json, load_json_object, sha256_file
from flatpak_package import stage_flatpak
from package_release import package_release
from platform_policy import resolve_policy, source_policy
from release_platform import LINUX, WINDOWS
import test_platform_policy
import test_release_audit as fixture

PYTHON_ONLY = {"LICENSES/Python-3.13.txt", "LICENSES/Capstone-BSD.txt",
               "LICENSES/PyInstaller-GPL-2.0-or-later-with-bootloader-exception.txt", "_internal/base_library.zip"}
COMPILER_ROLES = {"forge_tool", "forge_link_library", "forge_tool_resource"}
NOTICES = b"# TriAevum Forge (Rust): third-party crates\n\nfixture\n"
LOCK = b"version = 4\n"
TOOLCHAIN = b'[toolchain]\nchannel = "1.92.0"\n'
CLI = {WINDOWS.target: "forge/triaevum-forge.exe", LINUX.target: "forge/triaevum-forge"}
HELPER = "forge/triaevum-update-helper.exe"
BINARY_ROLES = ("forge_executable", "forge_command_line", "forge_update_helper")


def rust_files(target):
    base = fixture.FILES if target == WINDOWS.target else test_platform_policy.linux_files()
    files = {name: record for name, record in base.items()
             if name not in PYTHON_ONLY and record[0] not in COMPILER_ROLES}
    executable = b"MZ" if target == WINDOWS.target else b"\x7fELF"
    files.update({
        WINDOWS.forge if target == WINDOWS.target else LINUX.forge: ("forge_executable", executable + b" rust window"),
        CLI[target]: ("forge_command_line", executable + b" rust command line"),
        "LICENSES/Rust-Forge-crates.md": ("license", NOTICES),
    })
    if target == WINDOWS.target:
        files[HELPER] = ("forge_update_helper", executable + b" rust update helper")
    return files


def source_entries(lock=LOCK, toolchain=TOOLCHAIN, notices=NOTICES, omit=()):
    entries = {"tools/triaevum_forge/Cargo.lock": lock, "tools/triaevum_forge/rust-toolchain.toml": toolchain,
               "LICENSES/Rust-Forge-crates.md": notices}
    profile = load_json_object(DEFAULT_POLICY)["forge_profiles"]["rust"]
    for relative in profile["source_archive_required_paths"]:
        entries.setdefault(relative, b"// source fixture\n")
    return {name: data for name, data in entries.items() if name not in omit}


def receipt_for(files, target, /, **overrides):
    binaries = [{"path": path, "role": role, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                for path, (role, data) in sorted(files.items())
                if role in BINARY_ROLES]
    receipt = {"format": "triaevum_rust_forge_build_v1", "target": target, "source_commit": "0" * 40,
               "toolchain": "1.92.0", "packages": ["forge-desktop", "forge-cli"],
               "static_crt": target == WINDOWS.target,
               "cargo_lock_sha256": hashlib.sha256(LOCK).hexdigest(),
               "notices_sha256": hashlib.sha256(NOTICES).hexdigest(), "binaries": binaries}
    receipt.update(overrides)
    return receipt


def write_rust_package(root, target=WINDOWS.target, files=None, receipt=None, source=None, release=None):
    """A complete Rust-profile package written like package_release would."""
    files = dict(rust_files(target) if files is None else files)
    if receipt is not False:
        files["forge/rust-forge.json"] = ("forge_build_receipt", json.dumps(
            receipt_for(files, target) if receipt is None else receipt).encode())
    inventory = []
    for relative, (role, data) in files.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        inventory.append({"path": relative, "role": role, "bytes": len(data), "sha256": sha256_file(path)})
    archive_path = root / "source/TriAevum-source.zip"
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive_path, "w") as archive:
        fixture.write_source_fixture(archive)
        for name, data in (source_entries() if source is None else source).items():
            archive.writestr(name, data)
    inventory.append({"path": "source/TriAevum-source.zip", "role": "corresponding_source",
                      "bytes": archive_path.stat().st_size, "sha256": sha256_file(archive_path)})
    manifest = "docs/release-manifest.json" if target == WINDOWS.target else "release-manifest.json"
    atomic_write_json(root / manifest, {
        "format": "triaevum_public_release_manifest_v1",
        "release": {"name": "TriAevum", "version": "test", "target": target, "source_commit": "0" * 40,
                    "contains_title_code": False, "contains_title_content": False,
                    "proprietary_sdk_included": False, "redistributable": True, "forge_profile": "rust",
                    **(release or {})},
        "files": inventory,
    })


class RustForgeProfileTests(unittest.TestCase):
    def audit(self, **arguments):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_rust_package(root, **arguments)
            return audit_release(root)

    def assertRejected(self, result, *fragments):
        self.assertFalse(result.ok)
        for fragment in fragments:
            self.assertTrue(any(fragment in error for error in result.errors), (fragment, result.errors))

    def test_rust_packages_pass_without_python_payload_or_notices(self):
        for target in (WINDOWS.target, LINUX.target):
            with self.subTest(target=target):
                result = self.audit(target=target)
                self.assertTrue(result.ok, result.errors)
                self.assertEqual(result.summary["forge_profile"], "rust")
                self.assertIn("forge_command_line", result.summary["roles"])

    def test_profile_adds_rules_without_removing_common_exclusions(self):
        raw = load_json_object(DEFAULT_POLICY)
        for target in (WINDOWS.target, LINUX.target):
            python, rust = resolve_policy(raw, target, "python_pyinstaller"), resolve_policy(raw, target, "rust")
            for key in ("forbidden_basenames", "forbidden_extensions", "forbidden_path_components",
                        "forbidden_sha256", "forbidden_json_true_fields", "forbidden_json_false_fields",
                        "source_archive_forbidden_prefixes", "required_roles"):
                self.assertTrue(set(python.get(key, [])) <= set(rust.get(key, [])), key)
            python_required = set(python["required_paths"]) - PYTHON_ONLY
            self.assertTrue(python_required <= set(rust["required_paths"]))
            self.assertNotIn("forge_frozen_resource", rust["allowed_roles"])
            self.assertEqual(set(python["allowed_roles"]) - set(rust["allowed_roles"]),
                             {"forge_frozen_resource"} if target == LINUX.target else set())
        with self.assertRaises(ValueError):
            resolve_policy(raw, WINDOWS.target, "electron")
        with self.assertRaises(ValueError):
            resolve_policy(raw, "aarch64-linux-android", "rust")
        self.assertIn("tools/triaevum_forge/Cargo.lock",
                      source_policy(raw, "rust")["source_archive_required_paths"])

    def test_unknown_profile_is_rejected(self):
        self.assertRejected(self.audit(release={"forge_profile": "nodejs"}), "no Forge profile 'nodejs'")

    def test_python_payload_is_rejected_beside_a_rust_forge(self):
        for target, name, role in ((LINUX.target, "_internal/base_library.zip", "forge_frozen_resource"),
                                   (WINDOWS.target, "docs/python313.dll", "documentation"),
                                   (LINUX.target, "docs/forge_entry.pyc", "documentation")):
            with self.subTest(name=name):
                files = rust_files(target)
                files[name] = (role, b"python payload")
                self.assertRejected(self.audit(target=target, files=files), f"{name} is forbidden")

    def test_compiler_payloads_are_rejected(self):
        files = rust_files(WINDOWS.target)
        files["forge/clang-cl.exe"] = ("forge_tool", b"MZ LLVM compiler fixture")
        self.assertRejected(self.audit(files=files), "role forge_tool is not allowed")

    def test_private_data_exclusions_still_apply(self):
        cases = (("game.3ds", "license", "forbidden extension"),
                 ("resources/code.bin", "title_neutral_resource", "forbidden filename"),
                 ("resources/save.sav", "title_neutral_resource", "forbidden extension"),
                 ("resources/slot.state", "title_neutral_resource", "forbidden extension"),
                 ("resources/captures/frame.png", "title_neutral_resource", "forbidden path component"),
                 ("resources/mod/pack.rar", "title_neutral_resource", "forbidden"),
                 ("resources/cache/object.obj", "title_neutral_resource", "forbidden extension"),
                 ("resources/module.tam", "title_neutral_resource", "forbidden extension"))
        for name, role, message in cases:
            with self.subTest(name=name):
                files = rust_files(WINDOWS.target)
                files[name] = (role, b"private fixture")
                self.assertRejected(self.audit(files=files), message)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_rust_package(root)
            (root / "forge/personal.cci").write_bytes(b"undeclared ROM")
            self.assertRejected(audit_release(root), "undeclared file in public package: forge/personal.cci")

    def test_private_files_hidden_in_corresponding_source_are_rejected(self):
        entries = source_entries()
        entries["tools/triaevum_forge/tests/fixture/code.bin"] = b"ROM-derived"
        self.assertRejected(self.audit(source=entries), "source archive forbidden filename")

    def test_receipt_must_match_packaged_binaries(self):
        files = rust_files(WINDOWS.target)
        stale = receipt_for(files, WINDOWS.target)
        files[CLI[WINDOWS.target]] = ("forge_command_line", b"MZ rebuilt after the receipt")
        self.assertRejected(self.audit(files=files, receipt=stale),
                            "does not match the packaged binary: forge/triaevum-forge.exe")
        files = rust_files(WINDOWS.target)
        extra = receipt_for(files, WINDOWS.target)
        extra["binaries"].append({"path": "forge/other.exe", "role": "forge_command_line", "bytes": 1,
                                  "sha256": "0" * 64})
        self.assertRejected(self.audit(files=files, receipt=extra), "Rust Forge build receipt names")
        self.assertRejected(self.audit(receipt=False), "required release file is missing: forge/rust-forge.json",
                            "exactly one forge_build_receipt")
        for override, message in (({"target": LINUX.target}, "target differs"),
                                  ({"source_commit": "1" * 40}, "source commit differs"),
                                  ({"format": "other"}, "unsupported Rust Forge build receipt format"),
                                  ({"notices_sha256": "0" * 64}, "does not bind the packaged crate notices"),
                                  ({"binaries": "all"}, "binaries are malformed")):
            with self.subTest(override=override):
                files = rust_files(WINDOWS.target)
                self.assertRejected(self.audit(files=files, receipt=receipt_for(files, WINDOWS.target, **override)),
                                    message)

    def test_receipt_is_bound_to_corresponding_source(self):
        self.assertRejected(self.audit(source=source_entries(lock=b"version = 4\n# edited\n")),
                            "not built from the corresponding source's Cargo.lock")
        self.assertRejected(self.audit(source=source_entries(toolchain=b'[toolchain]\nchannel = "stable"\n')),
                            "toolchain differs")
        self.assertRejected(self.audit(source=source_entries(notices=b"other notices\n")),
                            "notices differ from the corresponding source")
        self.assertRejected(self.audit(source=source_entries(omit={"tools/triaevum_forge/Cargo.lock"})),
                            "required public source missing: tools/triaevum_forge/Cargo.lock")

    def test_notices_are_required(self):
        files = rust_files(WINDOWS.target)
        del files["LICENSES/Rust-Forge-crates.md"]
        self.assertRejected(self.audit(files=files), "required release file is missing: LICENSES/Rust-Forge-crates.md")

    def test_binaries_must_not_embed_build_machine_paths(self):
        for target, leak in ((WINDOWS.target, b"C:\\Users\\builder\\.cargo\\registry\\src\\index\\serde\\lib.rs"),
                             (LINUX.target, b"/home/builder/src/main.rs"),
                             (LINUX.target, b"/root/.cargo/registry/src/index/serde/lib.rs"),
                             (WINDOWS.target, b"C:/Users/builder/.rustup/toolchains/1.92.0/lib")):
            with self.subTest(leak=leak):
                files = rust_files(target)
                files[CLI[target]] = ("forge_command_line", files[CLI[target]][1] + b"\0panic at " + leak)
                self.assertRejected(self.audit(target=target, files=files), "embeds a build-machine path")

    def test_proprietary_sdk_code_is_rejected_in_the_command_line(self):
        files = rust_files(WINDOWS.target)
        files[CLI[WINDOWS.target]] = ("forge_command_line", b"MZ NVSDK_NGX_VULKAN_Init")
        self.assertRejected(self.audit(files=files), "contains proprietary SDK code")

    def test_packager_records_the_profile_and_archive_keeps_the_cli_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs = root / "inputs"
            write_rust_package(inputs, target=LINUX.target)
            manifest = json.loads((inputs / "release-manifest.json").read_text())
            layout = root / "layout.json"
            atomic_write_json(layout, {
                "format": "triaevum_public_release_layout_v1", "target": LINUX.target, "forge_profile": "rust",
                "files": [{**item, "source": str(inputs / item["path"])} for item in manifest["files"]]})
            package_release(layout, root / "out", source_root=inputs, version="test", source_commit="0" * 40,
                            enforce_readiness=False, verify_runtime=False)
            packaged = json.loads((root / "out/release-manifest.json").read_text())
            self.assertEqual(packaged["release"]["forge_profile"], "rust")
            archive = create_release_archive(root / "out", root / "out.tar.gz")
            with tarfile.open(archive["archive"]) as tar:
                modes = {member.name.split("/", 1)[1]: member.mode for member in tar.getmembers() if "/" in member.name}
            self.assertEqual(modes["forge/triaevum-forge"], 0o755)
            self.assertEqual(modes["TriAevumForge"], 0o755)

    def test_flatpak_drops_the_python_file_chooser_helper(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_rust_package(root / "package", target=LINUX.target)
            manifest_path = stage_flatpak(root / "package", root / "build")
            module = json.loads(manifest_path.read_text())["modules"][0]
            commands = "\n".join(module["build-commands"])
            self.assertNotIn("file_chooser", commands)
            self.assertIn("/app/lib/triaevum/forge/triaevum-forge", commands)
            self.assertFalse((root / "build/file_chooser.c").exists())
            self.assertEqual(json.loads((root / "build/flatpak-staging.json").read_text())["forge_profile"], "rust")

    def test_appimage_audits_the_rust_forge_elfs(self):
        from tools.triaevum_release import appimage_package
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_rust_package(root / "package", target=LINUX.target)
            with patch.object(appimage_package, "audit_native_elfs") as elf_audit:
                appdir = appimage_package.stage_appimage(root / "package", root / "AppDir")
            self.assertEqual(elf_audit.call_args.kwargs, {"frozen_forge": False})
            self.assertTrue(audit_release(appdir / "usr/lib/triaevum").ok)


class RustForgeBuildFlagsTests(unittest.TestCase):
    def test_the_cargo_target_folder_is_remapped(self):
        # Linux glutin includes bindings generated in the target folder; an
        # unmapped folder outside the checkout leaks the builder's home.
        import rust_forge_build
        with tempfile.TemporaryDirectory() as directory:
            target_dir = Path(directory) / "target"
            flags = rust_forge_build.rustflags(LINUX.target, target_dir)
            self.assertIn(f"--remap-path-prefix={target_dir.resolve()}=/target", flags)
            self.assertFalse(any(flag.endswith("=/target") for flag in rust_forge_build.rustflags(LINUX.target)))


if __name__ == "__main__":
    unittest.main()
