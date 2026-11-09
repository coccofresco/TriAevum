import json
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
import zipfile

import android_release_policy as policy
from android_release_policy import (ABI_DIR, FORGE_CATALOG, FORGE_INVENTORY, FORGE_SIGNATURE, REQUIRED_LIBRARIES, android_facts,
                                    audit_apk, elf_errors, parse_badging, privacy_sweep, version_code)
from release_platform import ANDROID, TITLES_ROOT
import platform_policy


def elf(align=0x4000, machine=183, debug=False) -> bytes:
    """A minimal ELF64 shared object: one LOAD header and a section table."""
    names = b"\0.text\0" + (b".debug_info\0" if debug else b"")
    header = bytearray(64)
    header[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<HH", header, 16, 3, machine)
    phoff, shoff = 64, 64 + 56
    shnum = 3 if debug else 2
    struct.pack_into("<QQ", header, 32, phoff, shoff)
    struct.pack_into("<HHHHH", header, 54, 56, 1, 64, shnum, 0)
    program = bytearray(56)
    struct.pack_into("<I", program, 0, 1)
    struct.pack_into("<Q", program, 48, align)
    strings_offset = shoff + 64 * shnum
    sections = bytearray(64 * shnum)
    struct.pack_into("<I", sections, 0, 0)
    struct.pack_into("<QQ", sections, 24, strings_offset, len(names))
    struct.pack_into("<I", sections, 64, 1)
    if debug:
        struct.pack_into("<I", sections, 128, 7)
    return bytes(header) + bytes(program) + bytes(sections) + names


BADGING = {
    "oot3d": "package: name='org.triaevum.echoesoftime' versionCode='800202' versionName='0.8.0-beta.2'\n"
             "application-label:'Echoes of Time'\n",
    "mm3d": "package: name='org.triaevum.moonfallcurse' versionCode='500101' versionName='0.5.0-alpha.1'\n"
            "application-label:'Moonfall Curse'\n",
}


def apk(path: Path, title="oot3d", *, extra=None, libraries=None, catalog=None, signed=True, inventory=None):
    libraries = {name: elf() for name in REQUIRED_LIBRARIES} if libraries is None else libraries
    catalog = {"title": title, "runtime": {"path": ANDROID.runtime, "sha256": "0" * 64}} if catalog is None else catalog
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("AndroidManifest.xml", b"\0")
        archive.writestr("classes.dex", b"dex\n")
        for name, data in libraries.items():
            archive.writestr(ABI_DIR + name, data)
        archive.writestr(FORGE_CATALOG, json.dumps(catalog))
        archive.writestr(FORGE_INVENTORY, json.dumps(inventory or {}))
        if signed:
            archive.writestr(FORGE_SIGNATURE, "{}")
        for name, data in (extra or {}).items():
            archive.writestr(name, data)
    return path


class VersionTests(unittest.TestCase):
    def test_codes(self):
        self.assertEqual(version_code("0.8.0-beta.2"), 800202)
        self.assertEqual(version_code("0.5.0-alpha.1"), 500101)
        self.assertEqual(version_code("1.0.0"), 10000900)
        self.assertLess(version_code("0.8.0-beta.2"), version_code("0.8.0-rc.1"))
        self.assertLess(version_code("0.8.0-rc.1"), version_code("0.8.0"))

    def test_rejects_unsupported(self):
        for value in ("0.1-intro-dev", "1.2", "0.8.0-beta", "0.100.0", ""):
            with self.subTest(value=value), self.assertRaises(ValueError):
                version_code(value)


class FactsTests(unittest.TestCase):
    def test_both_titles(self):
        eot, mc = android_facts("oot3d"), android_facts("mm3d")
        self.assertEqual((eot["application_id"], eot["label"], eot["flavor"], eot["version_code"]),
                         ("org.triaevum.echoesoftime", "Echoes of Time", "eot", 800202))
        self.assertEqual((mc["application_id"], mc["label"], mc["flavor"], mc["version_code"]),
                         ("org.triaevum.moonfallcurse", "Moonfall Curse", "mc", 500101))
        self.assertNotEqual(eot["application_id"], mc["application_id"])

    def test_negative_missing_or_inconsistent_fact(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            shutil.copytree(TITLES_ROOT / "oot3d", root / "oot3d", ignore=lambda d, n: [i for i in n if i not in
                                                                                       ("product.json", "android.json")])
            facts = json.loads((root / "oot3d" / "android.json").read_text(encoding="utf-8"))
            for change in ({"application_id": ""}, {"label": "Moonfall Curse"}, {"title": "mm3d"},
                           {"application_id": "Org.Bad"}, {"version_name": "dev"}):
                (root / "oot3d" / "android.json").write_text(json.dumps({**facts, **change}), encoding="utf-8")
                with self.subTest(change=change), self.assertRaises(ValueError):
                    android_facts("oot3d", root)
        with self.assertRaises(ValueError):
            android_facts("windwaker")


class ElfTests(unittest.TestCase):
    def test_aligned_stripped(self):
        self.assertEqual(elf_errors("a.so", elf()), [])
        self.assertEqual(elf_errors("a.so", elf(align=0x10000)), [])

    def test_negative_4k_debug_machine(self):
        self.assertIn("LOAD segment aligned to 0x1000", " ".join(elf_errors("a.so", elf(align=0x1000))))
        self.assertIn("unstripped (.debug_info)", " ".join(elf_errors("a.so", elf(debug=True))))
        self.assertIn("not an AArch64", " ".join(elf_errors("a.so", elf(machine=62))))
        self.assertIn("not an ELF64", " ".join(elf_errors("a.so", b"MZ" + b"\0" * 100)))


class BadgingTests(unittest.TestCase):
    def test_parse(self):
        facts = parse_badging(BADGING["oot3d"] + "application-debuggable\n")
        self.assertEqual(facts["application_id"], "org.triaevum.echoesoftime")
        self.assertEqual(facts["version_code"], 800202)
        self.assertTrue(facts["debuggable"])
        self.assertFalse(parse_badging(BADGING["mm3d"])["debuggable"])


class AuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def audit(self, path, title="oot3d", badging=None, **kwargs):
        kwargs.setdefault("signer_sha256", "ab" * 32)
        return audit_apk(path, title, badging=BADGING[title] if badging is None else badging, **kwargs)

    def test_release_candidates_pass(self):
        for title in ("oot3d", "mm3d"):
            with self.subTest(title=title):
                self.assertEqual(self.audit(apk(self.root / f"{title}.apk", title), title), [])

    def assertRejected(self, errors, text):
        self.assertTrue(any(text in error for error in errors), errors)

    def test_policy_pinned_topscreen_asset_in_forge_package(self):
        import hashlib
        real = (Path(__file__).resolve().parents[2]
                / "runtime/triaevum_resources/topscreen/singlescreen-3.0/zelda_link_ext.zar").read_bytes()
        relative = "resources/topscreen/singlescreen-3.0/zelda_link_ext.zar"
        name = FORGE_INVENTORY.rsplit("/", 1)[0] + "/" + relative
        record = {"path": relative, "role": "topscreen_singlescreen_asset",
                  "sha256": hashlib.sha256(real).hexdigest(), "bytes": len(real)}
        self.assertEqual(self.audit(apk(self.root / "z.apk", extra={name: real},
                                        inventory={"files": [record]})), [])
        # Unlisted, other bytes, or another role: still refused.
        for inventory, data in (({}, real), ({"files": [record]}, b"other"),
                                ({"files": [dict(record, role="title_neutral_resource")]}, real)):
            with self.subTest(inventory=inventory, size=len(data)):
                errors = self.audit(apk(self.root / "z.apk", extra={name: data}, inventory=inventory))
                self.assertRejected(errors, "forbidden extension")

    def test_negative_game_data_and_dev_artefacts(self):
        for name in ("assets/forge-package/a.gar", "assets/x/title.mfpj", "assets/romfs/actor.zar",
                     "assets/game.3ds", "lib/arm64-v8a/libTriAevum.so.debug", "release.keystore",
                     "assets/exefs.bin", "local.properties"):
            with self.subTest(name=name):
                errors = self.audit(apk(self.root / "x.apk", extra={name: b"x"}))
                self.assertTrue(errors, name)

    def test_negative_missing_or_misaligned_library(self):
        libraries = {name: elf() for name in REQUIRED_LIBRARIES if name != "libtriaevum_title_aot.so"}
        self.assertRejected(self.audit(apk(self.root / "a.apk", libraries=libraries)),
                            "missing lib/arm64-v8a/libtriaevum_title_aot.so")
        libraries = {name: elf() for name in REQUIRED_LIBRARIES}
        libraries["libTriAevum.so"] = elf(align=0x1000)
        self.assertRejected(self.audit(apk(self.root / "b.apk", libraries=libraries)), "needs 0x4000")
        libraries["libTriAevum.so"] = elf(debug=True)
        self.assertRejected(self.audit(apk(self.root / "c.apk", libraries=libraries)), "unstripped")
        self.assertRejected(self.audit(apk(self.root / "d.apk", extra={"lib/x86_64/libTriAevum.so": elf()})),
                            "only arm64-v8a")

    def test_negative_unsigned_forge_package(self):
        path = apk(self.root / "unsigned.apk", signed=False)
        self.assertIn(f"missing {FORGE_SIGNATURE} (android_forge_package.py --sign)", self.audit(path))

    def test_package_paths_match_the_packager(self):
        # The audit must look where android_forge_package.py writes (the first
        # real mcRelease audit found the catalog path wrong).
        import android_forge_package
        import precompiled_titles
        root = "assets/" + android_forge_package.ASSET_ROOT + "/"
        self.assertEqual(FORGE_CATALOG, root + precompiled_titles.CATALOG)
        self.assertEqual(FORGE_INVENTORY, root + android_forge_package.INVENTORY)
        self.assertEqual(FORGE_SIGNATURE, root + android_forge_package.SIGNATURE)

    def test_runtime_titles_per_product(self):
        self.assertEqual(android_facts("oot3d")["runtime_titles"], ["oot3d"])
        self.assertEqual(android_facts("mm3d")["runtime_titles"], ["oot3d", "mm3d"])

    def test_negative_runtime_titles(self):
        import json, shutil, tempfile
        for bad in ([], ["mm3d"], ["mm3d", "oot3d"], ["oot3d", "oot3d", "mm3d"], ["oot3d", "zelda", "mm3d"], "oot3d;mm3d"):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                shutil.copytree(TITLES_ROOT / "mm3d", root / "mm3d")
                facts = json.loads((root / "mm3d/android.json").read_text(encoding="utf-8"))
                facts["runtime_titles"] = bad
                (root / "mm3d/android.json").write_text(json.dumps(facts), encoding="utf-8")
                with self.assertRaises(ValueError, msg=repr(bad)):
                    android_facts("mm3d", root)

    def test_forge_features_per_title(self):
        self.assertEqual(android_facts("oot3d")["forge_features"], [])
        self.assertEqual(android_facts("mm3d")["forge_features"], ["forge-android/title-mm3d"])

    def test_negative_debuggable_wrong_identity_unsigned(self):
        path = apk(self.root / "a.apk")
        self.assertRejected(self.audit(path, badging=BADGING["oot3d"] + "application-debuggable\n"), "debuggable")
        self.assertRejected(self.audit(path, badging=BADGING["mm3d"]), "manifest application_id")
        self.assertRejected(self.audit(path, badging=BADGING["oot3d"].replace("800202", "1")),
                            "manifest version_code")
        self.assertRejected(self.audit(path, signer_sha256=None), "signer not verified")
        self.assertEqual(self.audit(path, signer_sha256=None, allow_unsigned=True), [])
        self.assertRejected(self.audit(path, expected_signer="cd" * 32), "signer certificate differs")
        self.assertRejected(audit_apk(path, "oot3d", badging=None, signer_sha256="ab"), "no manifest facts")

    def test_negative_title_aware_refusals(self):
        self.assertRejected(self.audit(apk(self.root / "a.apk", "oot3d", catalog={"title": "mm3d", "runtime": {
            "path": ANDROID.runtime}})), "is not oot3d")
        self.assertRejected(self.audit(apk(self.root / "b.apk", "mm3d", extra={
            "assets/forge-package/titles/oot3d/recipe.json": b"{}"}), "mm3d"), "belongs to oot3d")
        self.assertRejected(self.audit(apk(self.root / "c.apk", "mm3d", catalog={
            "title": "mm3d", "product": "Echoes of Time", "runtime": {"path": ANDROID.runtime}}), "mm3d"),
            "names oot3d")
        self.assertRejected(self.audit(apk(self.root / "d.apk", catalog={
            "title": "oot3d", "runtime": {"path": "lib/arm64-v8a/libother.so"}})), "Forge catalog runtime")

    def test_negative_private_path(self):
        markers = [r"[A-Za-z]:[\\/]Users[\\/]"]
        path = apk(self.root / "a.apk", extra={"assets/notes.txt": b"built in C:\\Users\\someone\\x"})
        self.assertRejected(self.audit(path, privacy_markers=markers), "build-machine path")


class PrivacySweepTests(unittest.TestCase):
    def test_sweep(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "app").mkdir()
            (root / "app" / "ok.kt").write_text("val x = 1\n", encoding="utf-8")
            (root / "build").mkdir()
            (root / "build" / "log.txt").write_text("/home/someone/x\n", encoding="utf-8")
            markers = ["/home/[a-z]+/"]
            self.assertEqual(privacy_sweep(root, markers), [])
            (root / "app" / "bad.kt").write_text("val p = \"/home/someone/x\"\n", encoding="utf-8")
            self.assertEqual(privacy_sweep(root, markers), ["app/bad.kt"])

    def test_ports_android_is_clean(self):
        repo = Path(__file__).resolve().parents[2]
        self.assertEqual(privacy_sweep(repo / "ports" / "android", policy._markers()), [])


class DesktopNeutralityTests(unittest.TestCase):
    def test_desktop_policy_still_rejects_android(self):
        raw = json.loads((Path(__file__).with_name("release_policy.json")).read_text(encoding="utf-8"))
        with self.assertRaises(ValueError):
            platform_policy.resolve_policy(raw, ANDROID.target)


if __name__ == "__main__":
    unittest.main()
