from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from archive_release import create_release_archive
from audit_release import audit_release
from test_platform_policy import write_linux_package
from test_release_audit import write_clean_package


class ReleaseArchiveTests(unittest.TestCase):
    def test_windows_portable_zip_roundtrip_and_reproducibility(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "TriAevum portable"
            write_clean_package(package)
            first = create_release_archive(package, root / "first.zip")
            (package / "TriAevum.exe").touch()
            second = create_release_archive(package, root / "second.zip")
            self.assertEqual(first["sha256"], second["sha256"])
            with zipfile.ZipFile(first["archive"]) as archive:
                self.assertTrue(all(name.startswith(package.name + "/") for name in archive.namelist()))
                archive.extractall(root / "moved")
            result = audit_release(root / "moved" / package.name)
            self.assertTrue(result.ok, result.errors)

    def test_linux_tar_roundtrip_permissions_and_reproducibility(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "TriAevum-linux"
            write_linux_package(package)
            # Executability cannot depend on a publisher's checkout or host OS.
            (package / "TriAevum").chmod(0o644)
            first = create_release_archive(package, root / "first.tar.gz")
            second = create_release_archive(package, root / "second.tar.gz")
            self.assertEqual(first["sha256"], second["sha256"])
            with tarfile.open(first["archive"], "r:gz") as archive:
                for name in ("TriAevum", "TriAevumForge"):
                    self.assertEqual(archive.getmember(package.name + "/" + name).mode, 0o755)
                self.assertEqual(archive.getmember(package.name + "/README.md").mode, 0o644)
                self.assertTrue(all(m.uid == 0 and m.gid == 0 and m.mtime == 0 for m in archive.getmembers()))
                archive.extractall(root / "moved", filter="data")
            result = audit_release(root / "moved" / package.name)
            self.assertTrue(result.ok, result.errors)

    def test_rejects_wrong_format_existing_output_and_output_inside_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "package"
            write_linux_package(package)
            with self.assertRaisesRegex(ValueError, "tar.gz"):
                create_release_archive(package, root / "linux.zip")
            with self.assertRaisesRegex(ValueError, "outside"):
                create_release_archive(package, package / "output.tar.gz")
            existing = root / "existing.tar.gz"
            existing.write_bytes(b"previous release")
            with self.assertRaisesRegex(ValueError, "already exists"):
                create_release_archive(package, existing)
            self.assertEqual(existing.read_bytes(), b"previous release")

    def test_refuses_a_used_installation_with_private_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "package"
            write_clean_package(package)
            (package / "data").mkdir()
            (package / "data/save.state").write_bytes(b"private")
            with self.assertRaisesRegex(ValueError, "audit failed"):
                create_release_archive(package, root / "output.zip")
            self.assertFalse((root / "output.zip").exists())

    def test_failed_write_removes_partial_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "package"
            write_clean_package(package)
            with patch("archive_release.shutil.copyfileobj", side_effect=OSError("write failed")):
                with self.assertRaisesRegex(OSError, "write failed"):
                    create_release_archive(package, root / "output.zip")
            self.assertFalse((root / "output.zip").exists())


if __name__ == "__main__":
    unittest.main()
