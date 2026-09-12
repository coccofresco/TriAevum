import json
import os
import struct
import subprocess
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace
import game_language as language


def document(codes=("en", "it")):
    return {"format": language.FORMAT, "selected": codes[0], "available": [
        {"code": code, "label": code, "system_id": 1 if code == "en" else 4} for code in codes]}


class GameLanguageTests(unittest.TestCase):
    def test_shared_config_and_rom_switch(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            language.install(root, document())
            language.select(root, "it")
            language.install(root, document())
            self.assertEqual(json.loads(language.config_path(root).read_text())["selected"], "it")
            with self.assertRaises(ValueError):
                language.select(root, "fr")
            self.assertEqual(json.loads(language.config_path(root).read_text())["selected"], "it")
            language.install(root, document(("en",)))
            self.assertEqual(json.loads(language.config_path(root).read_text())["selected"], "en")

    def test_uses_native_detector(self):
        with patch("game_language.run_native", return_value=SimpleNamespace(returncode=0, stdout=json.dumps(document()))) as run:
            self.assertEqual(language.discover(Path("runtime"), Path("romfs")), document())
            self.assertEqual(run.call_args.args[0], ["runtime", "--game-language-info", "romfs"])

    def test_rejects_invalid_metadata(self):
        with self.assertRaises(ValueError):
            language.validate([])
        for change in ({"available": []}, {"selected": "fr"}, {"format": "unknown"}):
            with self.assertRaises(ValueError):
                language.validate(document() | change)

    def test_preserves_malformed_preferences(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path = language.config_path(root)
            path.parent.mkdir()
            path.write_text("broken")
            with self.assertRaises(ValueError):
                language.install(root, document())
            self.assertEqual(path.read_text(), "broken")


def romfs_languages(slots, *, missing_menu=False, malformed=False):
    """Synthetic native table layout; no original game data."""
    names = {1: "01_US_ENGLISH", 2: "02_EU_ENGLISH", 3: "03_EU_GERMAN",
             4: "04_EU_FRENCH", 5: "05_US_FRENCH", 6: "06_EU_SPANISH",
             7: "07_US_SPANISH", 8: "08_EU_ITALIAN"}
    directories = bytearray()
    def directory(name, parent):
        offset = len(directories)
        encoded = name.encode("utf-16-le")
        directories.extend(struct.pack("<6I", parent, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, len(encoded)))
        directories.extend(encoded)
        directories.extend(bytes(-len(directories) % 4))
        return offset
    directory("", 0)
    menu = directory("menu", 0)
    parents = {slot: directory(names[slot], menu) for slot in slots}
    files, data = bytearray(), bytearray()
    def file(name, parent, payload):
        encoded = name.encode("utf-16-le")
        files.extend(struct.pack("<IIQQII", parent, 0xffffffff, len(data), len(payload), 0xffffffff, len(encoded)))
        files.extend(encoded)
        files.extend(bytes(-len(files) % 4))
        data.extend(payload)
    qm = bytearray(112)
    struct.pack_into("<4sIII", qm, 0, b"QM\0\0", 4, 1, 0)
    for slot in slots:
        struct.pack_into("<II", qm, 32 + slot*8, 999999 if malformed else len(qm), 4)
        qm.extend(b"text")
        file("hud_all00.ctxb", parents[slot], b"hud")
        if not missing_menu:
            file("menu_file_select_parts00.ctxb", parents[slot], b"menu")
    file("eu.qm", 0, qm)
    do, fo = 40, 40+len(directories)
    payload = fo+len(files)
    return struct.pack("<10I", 40, 0, 0, do, len(directories), 0, 0, fo, len(files), payload) + directories + files + data


@unittest.skipUnless(os.environ.get("TRIAEVUM_TEST_RUNTIME"), "requires the built native runtime")
class NativeLanguageDetectorTests(unittest.TestCase):
    def inspect(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "romfs.bin"
            path.write_bytes(data)
            return subprocess.run([os.environ["TRIAEVUM_TEST_RUNTIME"], "--game-language-info", str(path)],
                                  capture_output=True, text=True, timeout=15)

    def test_europe_and_usa_slots(self):
        for slots, codes in (([2,3,4,6,8], ["en","de","fr","es","it"]),
                             ([1,5,7], ["en","fr","es"]),
                             ([2,4,6], ["en","fr","es"])):
            result = self.inspect(romfs_languages(slots))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual([v["code"] for v in json.loads(result.stdout)["available"]], codes)

    def test_messages_without_menu_are_not_a_supported_language(self):
        self.assertNotEqual(self.inspect(romfs_languages([2], missing_menu=True)).returncode, 0)

    def test_bad_qm_extents_and_truncated_romfs_are_rejected(self):
        self.assertNotEqual(self.inspect(romfs_languages([2], malformed=True)).returncode, 0)
        self.assertNotEqual(self.inspect(b"IVFC").returncode, 0)
