from __future__ import annotations

import dataclasses
import hashlib
import io
import json
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

import romfs_mods
from romfs_mods import (
    INVALID,
    VANILLA_PLUS_LINK,
    ArchiveContract,
    archive_contract,
    build_romfs_overlay,
    download_archive,
    enabled_mods,
    extract_archive,
    find_bsdtar,
    import_archive,
    import_extracted,
    prepare_romfs_mods,
    romfs_file_records,
    set_mod_enabled,
    stored_members,
)

RECIPES = Path(__file__).with_name("supported_revisions.json")
PREFIX = 0x1000  # Like the IVFC superblock before the Level-3 view.


class FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.close()


def _entry(fixed: bytes, name: str) -> bytes:
    encoded = name.encode("utf-16-le")
    data = fixed + struct.pack("<I", len(encoded)) + encoded
    return data + bytes(-len(data) % 4)


def build_level3(files: dict[str, bytes]) -> bytes:
    """A minimal Level-3 RomFS: the root and one level of directories."""
    directories = sorted({path.split("/")[0] for path in files if "/" in path})
    root_files = sorted(path for path in files if "/" not in path)
    # Directory table: root, then each directory; file table in directory order.
    dir_offsets, offset = {"": 0}, len(_entry(bytes(20), ""))
    for name in directories:
        dir_offsets[name] = offset
        offset += len(_entry(bytes(20), name))
    groups = {"": root_files, **{name: sorted(p for p in files if p.startswith(name + "/"))
                                 for name in directories}}
    file_offsets, offset = {}, 0
    for owner in ["", *directories]:
        for path in groups[owner]:
            file_offsets[path] = offset
            offset += len(_entry(bytes(28), path.rsplit("/", 1)[-1]))
    payload_offsets, data = {}, b""
    for path in sorted(files):
        data += bytes(-len(data) % 16)
        payload_offsets[path] = len(data)
        data += files[path]
    file_table = b""
    for owner in ["", *directories]:
        paths = groups[owner]
        for index, path in enumerate(paths):
            following = file_offsets[paths[index + 1]] if index + 1 < len(paths) else INVALID
            fixed = struct.pack("<IIQQI", dir_offsets[owner], following, payload_offsets[path],
                                len(files[path]), INVALID)
            file_table += _entry(fixed, path.rsplit("/", 1)[-1])
    dir_table = _entry(struct.pack("<5I", 0, INVALID, dir_offsets[directories[0]] if directories else INVALID,
                                   file_offsets[root_files[0]] if root_files else INVALID, INVALID), "")
    for index, name in enumerate(directories):
        sibling = dir_offsets[directories[index + 1]] if index + 1 < len(directories) else INVALID
        first = file_offsets[groups[name][0]] if groups[name] else INVALID
        dir_table += _entry(struct.pack("<5I", 0, sibling, INVALID, first, INVALID), name)
    hash_table = struct.pack("<I", INVALID)
    layout, cursor = [], 40
    for block in (hash_table, dir_table, hash_table, file_table):
        layout.append((cursor, len(block)))
        cursor += len(block)
    data_offset = cursor + (-cursor % 16)
    header = struct.pack("<10I", 40, *[value for pair in layout for value in pair], data_offset)
    body = header + hash_table + dir_table + hash_table + file_table
    return body + bytes(data_offset - len(body)) + data


def compose(overlay: bytes, base_view: bytes) -> bytes:
    magic, version, base, view, count, _reserved, tail = struct.unpack_from("<4sIQQIIQ", overlay, 0)
    assert magic == b"O3RO" and version == 1 and base == len(base_view)
    composed = bytearray(base_view)
    for index in range(count):
        offset, length, _zero, expected, replacement = struct.unpack_from("<QIIQQ", overlay, 64 + 32 * index)
        assert composed[offset:offset + length] == overlay[expected:expected + length]
        composed[offset:offset + length] = overlay[replacement:replacement + length]
    composed += overlay[tail:]
    assert len(composed) == view
    return bytes(composed)


def small_mod(members: dict[str, bytes]) -> romfs_mods.RomFsMod:
    return dataclasses.replace(
        VANILLA_PLUS_LINK, mod_id="fixture", archive_kind="fixture_archive", version="0.1",
        downloads=("https://example.invalid/primary", "https://example.invalid/mirror"),
        members={name: (len(data), hashlib.sha256(data).hexdigest()) for name, data in members.items()})


class RomFsModsTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.data = self.root / "data"
        self.members = {"actor/a.zar": b"new A payload, longer than before",
                        "misc/menu.zar": b"m"}
        self.mod = small_mod(self.members)

    # Catalogue, licence and the shipped contract -------------------------------
    def test_catalogue_credits_licence_and_sources(self) -> None:
        mod = VANILLA_PLUS_LINK
        self.assertEqual(mod.author, "HugAMuffin")
        self.assertEqual([credit.name for credit in mod.credits],
                         ["HugAMuffin", "Fado", "M-1", "seiunsky_1084", "Jameriquiah"])
        self.assertEqual(mod.license_name, "CC BY-NC-ND 4.0")
        self.assertEqual(mod.downloads[0], "https://gamebanana.com/dl/1827260")
        # The mirror is a publication: it stays unused until approved.
        self.assertEqual(mod.downloads[1:], (romfs_mods.VANILLA_PLUS_LINK_MIRROR_URL,)
                         if romfs_mods.VANILLA_PLUS_LINK_MIRROR_APPROVED else ())
        self.assertEqual(len(mod.members), 26)
        self.assertTrue(all(name.startswith(("actor/", "misc/")) for name in mod.members))

    def test_shipped_recipes_pin_the_original_archive(self) -> None:
        contract = archive_contract(RECIPES, VANILLA_PLUS_LINK)
        self.assertEqual(contract, ArchiveContract(
            "vanilla_plus_link_1_9_archive", "link_remodel_vanilla_plus_1_9.rar", 6861936,
            "8b30a15569e5eb680deda6fdd9582929309ad5acd92dcf70fe2bd8effac0398c"))
        recipes = json.loads(RECIPES.read_text(encoding="utf-8"))["recipes"]
        self.assertTrue(all(archive_contract(recipe, VANILLA_PLUS_LINK) == contract for recipe in recipes))

    # Settings ------------------------------------------------------------------
    def test_settings_default_off_round_trip_and_reject_malformed(self) -> None:
        self.assertEqual(enabled_mods(self.data), ())
        self.assertEqual(set_mod_enabled(self.data, "vanilla_plus_link", True), ("vanilla_plus_link",))
        self.assertEqual(enabled_mods(self.data), ("vanilla_plus_link",))
        self.assertEqual(set_mod_enabled(self.data, "vanilla_plus_link", False), ())
        with self.assertRaises(ValueError):
            set_mod_enabled(self.data, "unknown", True)
        romfs_mods.settings_path(self.data).write_text('{"enabled": "yes"}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Invalid RomFS mod settings"):
            enabled_mods(self.data)

    # The archive ---------------------------------------------------------------
    def test_download_falls_back_to_the_next_source_and_keeps_only_verified_files(self) -> None:
        payload = b"original archive bytes"
        contract = ArchiveContract("fixture_archive", "fixture.rar", len(payload),
                                   hashlib.sha256(payload).hexdigest())
        requested, reports = [], []

        def opener(request, *, timeout, context):
            requested.append(request.full_url)
            if request.full_url.endswith("primary"):
                return FakeResponse(b"<html>challenge</html>")
            return FakeResponse(payload)

        path = download_archive(self.data, self.mod, contract, opener=opener, report=reports.append)
        self.assertEqual(requested, list(self.mod.downloads))
        self.assertEqual(path.read_bytes(), payload)
        self.assertIn("primary failed", reports[0])
        self.assertEqual(sorted(p.name for p in (self.data / "downloads").iterdir()), ["fixture.rar"])

        path.unlink()
        with self.assertRaisesRegex(ValueError, "primary.*mirror"):
            download_archive(self.data, self.mod, contract,
                             opener=lambda *_args, **_kwargs: FakeResponse(payload + b"newer"))
        self.assertEqual(list((self.data / "downloads").iterdir()), [])

    def test_selected_archive_must_be_the_original(self) -> None:
        payload = b"original archive bytes"
        contract = ArchiveContract("fixture_archive", "fixture.rar", len(payload),
                                   hashlib.sha256(payload).hexdigest())
        picked = self.root / "picked.rar"
        picked.write_bytes(payload[:-1] + b"X")
        with self.assertRaisesRegex(ValueError, "not the original"):
            import_archive(picked, self.data, self.mod, contract)
        picked.write_bytes(payload)
        self.assertEqual(import_archive(picked, self.data, self.mod, contract).read_bytes(), payload)

    # Members -------------------------------------------------------------------
    def test_extracted_folder_is_verified_member_by_member(self) -> None:
        folder = self.root / "extracted" / "romfs"
        for name, data in self.members.items():
            (folder / name).parent.mkdir(parents=True, exist_ok=True)
            (folder / name).write_bytes(data)
        (folder / "misc/menu.zar").write_bytes(b"x")
        with self.assertRaisesRegex(ValueError, "differs"):
            import_extracted(folder.parent, self.data, self.mod)
        self.assertIsNone(stored_members(self.data, self.mod))
        (folder / "misc/menu.zar").write_bytes(self.members["misc/menu.zar"])
        members = import_extracted(folder.parent, self.data, self.mod)
        self.assertEqual({name: path.read_bytes() for name, path in members.items()}, self.members)
        members["actor/a.zar"].write_bytes(b"tampered")
        self.assertIsNone(stored_members(self.data, self.mod))

    def test_archive_extraction_uses_libarchive_and_takes_only_catalogued_members(self) -> None:
        tool = find_bsdtar()
        if tool is None:
            self.skipTest("no libarchive bsdtar on this host")
        archive = self.root / "fixture.zip"
        with zipfile.ZipFile(archive, "w") as output:
            for name, data in self.members.items():
                output.writestr(name, data)
            output.writestr("readme.txt", b"not a RomFS member")
        contract = ArchiveContract("fixture_archive", archive.name, archive.stat().st_size,
                                   hashlib.sha256(archive.read_bytes()).hexdigest())
        members = extract_archive(archive, self.data, self.mod, contract, bsdtar=tool)
        self.assertEqual({name: path.read_bytes() for name, path in members.items()}, self.members)
        stored = romfs_mods.source_directory(self.data, self.mod)
        self.assertFalse((stored / "readme.txt").exists())
        with self.assertRaisesRegex(ValueError, "failed verification"):
            extract_archive(archive, self.data, self.mod,
                            dataclasses.replace(contract, sha256="0" * 64), bsdtar=tool)

    # The overlay ---------------------------------------------------------------
    def _romfs(self, files: dict[str, bytes]) -> tuple[Path, bytes]:
        view = build_level3(files)
        image = self.root / "romfs.bin"
        image.write_bytes(b"IVFC" + bytes(PREFIX - 4) + view + b"trailing")
        return image, view

    def test_overlay_appends_payloads_and_repoints_only_replaced_records(self) -> None:
        image, view = self._romfs({"actor/a.zar": b"A" * 21, "actor/b.zar": b"B" * 7,
                                   "misc/menu.zar": b"M" * 40, "root.bin": b"R" * 3})
        sources = self.root / "sources"
        replacements = {}
        for name, data in self.members.items():
            (sources / name).parent.mkdir(parents=True, exist_ok=True)
            (sources / name).write_bytes(data)
            replacements[name] = sources / name
        output = self.root / "overlay" / "romfs_overlay.o3ro"
        summary = build_romfs_overlay(image, service_offset=PREFIX, service_size=len(view),
                                      replacements=replacements, output=output)
        self.assertEqual(summary["files"], 2)
        composed = compose(output.read_bytes(), view)
        header, records = romfs_file_records(io.BytesIO(composed), 0, len(composed))
        for name, data in {**self.members, "actor/b.zar": b"B" * 7, "root.bin": b"R" * 3}.items():
            _record, payload, length = records[name]
            self.assertEqual(composed[header[9] + payload:header[9] + payload + length], data)
            if name in self.members:
                self.assertEqual((header[9] + payload) % 16, 0)
                self.assertGreaterEqual(header[9] + payload, len(view))
        self.assertEqual(composed[:len(view)].count(b"A" * 21), 1)  # Base bytes stay in place.
        with self.assertRaisesRegex(ValueError, "no actor/missing.zar"):
            build_romfs_overlay(image, service_offset=PREFIX, service_size=len(view),
                                replacements={"actor/missing.zar": replacements["actor/a.zar"]},
                                output=output)

    def test_prepare_is_inert_when_disabled_and_cached_when_enabled(self) -> None:
        image, view = self._romfs({"actor/a.zar": b"A" * 5, "misc/menu.zar": b"M"})
        recipe = {"optional_inputs": {"fixture_archive": {"name": "fixture.rar", "bytes": 1,
                                                          "sha256": "0" * 64}}}
        reports = []
        calls = []

        def view_of():
            calls.append(1)
            return PREFIX, len(view)

        romfs_mods.CATALOG["fixture"] = self.mod
        self.addCleanup(romfs_mods.CATALOG.pop, "fixture")
        arguments = dict(root=self.root, data_root=self.data, recipe=recipe, romfs=image,
                         romfs_sha256="f" * 64, service_view=view_of,
                         report=lambda _stage, message: reports.append(message))
        self.assertIsNone(prepare_romfs_mods(**arguments))
        self.assertEqual(calls, [])
        set_mod_enabled(self.data, "fixture", True)
        self.assertIsNone(prepare_romfs_mods(**arguments))
        self.assertIn("not downloaded", reports[-1])
        folder = self.root / "extracted"
        for name, data in self.members.items():
            (folder / name).parent.mkdir(parents=True, exist_ok=True)
            (folder / name).write_bytes(data)
        import_extracted(folder, self.data, self.mod)
        overlay = prepare_romfs_mods(**arguments)
        self.assertEqual(overlay.name, "romfs_overlay.o3ro")
        receipt = json.loads((overlay.parent / "import.json").read_text(encoding="utf-8"))
        self.assertFalse(receipt["redistributable"])
        self.assertIn("HugAMuffin", receipt["mods"][0]["attribution"])
        stamp = overlay.stat().st_mtime_ns
        self.assertEqual(prepare_romfs_mods(**arguments), overlay)
        self.assertEqual(overlay.stat().st_mtime_ns, stamp)
        composed = compose(overlay.read_bytes(), view)
        header, records = romfs_file_records(io.BytesIO(composed), 0, len(composed))
        _record, payload, length = records["actor/a.zar"]
        self.assertEqual(composed[header[9] + payload:header[9] + payload + length], self.members["actor/a.zar"])

    def test_a_romfs_without_a_replaced_file_keeps_the_native_files(self) -> None:
        image, view = self._romfs({"actor/a.zar": b"A" * 5})  # No misc/menu.zar.
        romfs_mods.CATALOG["fixture"] = self.mod
        self.addCleanup(romfs_mods.CATALOG.pop, "fixture")
        set_mod_enabled(self.data, "fixture", True)
        folder = self.root / "extracted"
        for name, data in self.members.items():
            (folder / name).parent.mkdir(parents=True, exist_ok=True)
            (folder / name).write_bytes(data)
        import_extracted(folder, self.data, self.mod)
        reports = []
        recipe = {"optional_inputs": {"fixture_archive": {"name": "f.rar", "bytes": 1, "sha256": "0" * 64}}}
        self.assertIsNone(prepare_romfs_mods(
            root=self.root, data_root=self.data, recipe=recipe, romfs=image, romfs_sha256="f" * 64,
            service_view=lambda: (PREFIX, len(view)), report=lambda _stage, text: reports.append(text)))
        self.assertIn("no misc/menu.zar", reports[-1])
        self.assertIn("native files", reports[-1])


if __name__ == "__main__":
    unittest.main()
