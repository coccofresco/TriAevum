import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from macos_forge_package import SOURCE_DOCUMENTS, verified_source_documents
from macos_private_content import MARKERS, PRIVATE_DIRECTORY, package_errors, stream_names_private_title


def source_fixture(path, extra=None, commit="a" * 40):
    files = {name: ("verified archive: " + name).encode() for name in
             (*SOURCE_DOCUMENTS, "LICENSES/Rust-Forge-crates.md", "LICENSE")}
    files.update(extra or {})
    aggregate = hashlib.sha256()
    with zipfile.ZipFile(path, "w") as archive:
        for name, contents in sorted(files.items()):
            aggregate.update(name.encode() + b"\0" + hashlib.sha256(contents).digest())
            archive.writestr(name, contents)
        archive.writestr("SOURCE_ARCHIVE_MANIFEST.json", json.dumps({
            "format": "triaevum_corresponding_source_v1", "source_commit": commit,
            "files": len(files), "uncompressed_bytes": sum(map(len, files.values())),
            "content_identity_sha256": aggregate.hexdigest()}))
    return files


class MacOSPrivateContentTests(unittest.TestCase):
    def test_documents_come_from_the_bound_archive_and_not_the_checkout(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "source.zip"
            files = source_fixture(archive)
            result = verified_source_documents(archive, "a" * 40)
            self.assertEqual(result, files)
            with self.assertRaisesRegex(ValueError, "commit/target"):
                verified_source_documents(archive, "b" * 40)
            with zipfile.ZipFile(archive, "a") as output:
                output.writestr("unrecorded.txt", b"unbound")
            with self.assertRaisesRegex(ValueError, "inventory mismatch"):
                verified_source_documents(archive, "a" * 40)

    def test_private_names_in_paths_and_text_are_rejected_before_promotion(self):
        for name, contents in (
                ("tools/oot3d/" + PRIVATE_DIRECTORY + "_address_map.py", b"fixture"),
                ("docs/" + MARKERS[1].decode().upper() + "_CURSE.md", b"fixture"),
                ("docs/neutral.md", b"See " + MARKERS[0].upper() + b"-dekui"),
                ("THIRD_PARTY_NOTICES.md", MARKERS[1].title() + b" Curse donor"),
                ("docs/neutral.txt", (MARKERS[2].decode() + " Mask 3D").encode("utf-16"))):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                archive = Path(temporary) / "source.zip"
                source_fixture(archive, {name: contents})
                with self.assertRaisesRegex(ValueError, "private title"):
                    verified_source_documents(archive, "a" * 40)
                self.assertTrue(any("private title" in error for error in package_errors(Path(temporary))))

    def test_plain_package_notice_and_stream_boundary_are_checked(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "notice.txt").write_text("credits: " + PRIVATE_DIRECTORY + "-dekui")
            self.assertTrue(any("package text" in error for error in package_errors(root)))
        self.assertTrue(stream_names_private_title(io.BytesIO(b" " * (1024 * 1024 - 2) + MARKERS[0].upper())))
        self.assertFalse(stream_names_private_title(io.BytesIO(b"public OOT3D notice")))

    def test_guard_publisher_policy_and_their_tests_can_ship_as_source(self):
        directory = Path(__file__).resolve().parent
        sources = {"tools/triaevum_release/" + name: (directory / name).read_bytes() for name in (
            "macos_private_content.py", "macos_forge_package.py", "release_policy.json",
            "platform_policy.py", "source_archive.py", "audit_release.py",
            "test_macos_private_content.py", "test_platform_policy.py", "test_source_archive.py")}
        for name, contents in sources.items():
            with self.subTest(source=name):
                for marker in MARKERS:
                    self.assertNotIn(marker, contents.lower())
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "source.zip"
            source_fixture(archive, sources)
            verified_source_documents(archive, "a" * 40)
            self.assertEqual(list(package_errors(Path(temporary))), [])

    def test_shader_tables_and_other_word_substrings_do_not_name_a_title(self):
        for payload in (b"ColMajor\0ArrayStride\0", b"ColMajorArrayStride\0", b"majoraxis\0",
                        b"col" + MARKERS[2][:6] + b"rraystride\0"):
            for prefix in (b"", bytes.fromhex("cffaedfe") + bytes(28)):
                self.assertFalse(stream_names_private_title(io.BytesIO(prefix + payload)))
        for encoding in ("utf-16le", "utf-16be", "utf-32le", "utf-32be"):
            self.assertFalse(stream_names_private_title(io.BytesIO("ColMajor\0ArrayStride".encode(encoding))))
            self.assertTrue(stream_names_private_title(io.BytesIO((MARKERS[2].decode() + " Mask 3D").encode(encoding))))

    def test_native_symbol_and_text_boundaries_preserve_real_rejections(self):
        title_component = MARKERS[0][:1].upper() + MARKERS[0][1:]
        for text in (b"Create" + title_component + b"TitleSession", b"TRIAEVUM_TITLE_" + MARKERS[0].upper(),
                     b"/src/titles/" + MARKERS[0] + b"/adapter.cpp", MARKERS[2] + b" Mask 3D"):
            payload = bytes.fromhex("cffaedfe") + bytes(28) + text + b"\0"
            self.assertTrue(stream_names_private_title(io.BytesIO(payload)))
            # Put the complete name across the stream boundary.
            payload = payload[:32] + b"\0" * (_CHUNK_FIXTURE - 34) + text + b"\0"
            self.assertTrue(stream_names_private_title(io.BytesIO(payload)))
        random_bytes = b"\xff\xa1" + MARKERS[0].title() + b"\xff\xa2"
        self.assertFalse(stream_names_private_title(io.BytesIO(bytes.fromhex("cffaedfe") + random_bytes)))

    def test_compressed_containers_are_not_scanned_as_raw_text(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "portable.o3ps").write_bytes(b"compressed fixture " + MARKERS[0])
            with zipfile.ZipFile(root / "source.zip", "w", compression=zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("public.txt", "public source")
            self.assertEqual(list(package_errors(root)), [])
            with zipfile.ZipFile(root / "source.zip", "a") as archive:
                archive.writestr("notice.txt", b"credits: " + MARKERS[1])
            self.assertTrue(list(package_errors(root)))


_CHUNK_FIXTURE = 1024 * 1024


if __name__ == "__main__":
    unittest.main()
