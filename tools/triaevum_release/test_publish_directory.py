import unittest
from pathlib import Path
from unittest.mock import patch

from publish_directory import publish_directory


class PublicationTests(unittest.TestCase):
    def test_bounded_retry_only_for_windows_sharing_errors(self):
        error = PermissionError("busy")
        error.winerror = 32
        with patch.object(Path, "exists", return_value=False), patch("publish_directory.time.sleep") as sleep:
            with patch.object(Path, "rename", side_effect=[error, None]) as rename:
                publish_directory(Path("source"), Path("destination"))
                self.assertEqual(rename.call_count, 2)
                sleep.assert_called_once()
            with patch.object(Path, "rename", side_effect=error) as rename:
                with self.assertRaises(PermissionError):
                    publish_directory(Path("source"), Path("destination"))
                self.assertEqual(rename.call_count, 6)
            with patch.object(Path, "rename", side_effect=OSError("disk")) as rename:
                with self.assertRaises(OSError):
                    publish_directory(Path("source"), Path("destination"))
                rename.assert_called_once()

    def test_never_overwrites_existing_output(self):
        with patch.object(Path, "exists", return_value=True), patch.object(Path, "rename") as rename:
            with self.assertRaises(FileExistsError):
                publish_directory(Path("source"), Path("destination"))
            rename.assert_not_called()
