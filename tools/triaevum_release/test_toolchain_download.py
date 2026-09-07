import hashlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from toolchain_download import acquire_payload, validate_payload


class Response(io.BytesIO):
    def geturl(self):
        return "https://download.visualstudio.microsoft.com/payload"


class ToolchainDownloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.payload = {"url": "https://download.visualstudio.microsoft.com/payload",
                        "size": 7, "sha256": hashlib.sha256(b"fixture").hexdigest()}

    def test_download_verifies_and_reuses_completed_content(self):
        with patch("toolchain_download.urlopen", return_value=Response(b"fixture")) as request:
            path = acquire_payload(self.payload, self.root)
            self.assertEqual(path.read_bytes(), b"fixture")
            self.assertEqual(acquire_payload(self.payload, self.root), path)
            request.assert_called_once()

    def test_rejects_corruption_and_releases_lock_for_retry(self):
        with patch("toolchain_download.urlopen", return_value=Response(b"corrupt")):
            with self.assertRaisesRegex(ValueError, "SHA-256") as failure:
                acquire_payload(self.payload, self.root)
            self.assertIn(self.payload["sha256"], str(failure.exception))
            self.assertIn(hashlib.sha256(b"corrupt").hexdigest(), str(failure.exception))
            self.assertIn("received 7 bytes", str(failure.exception))
        self.assertFalse((self.root / self.payload["sha256"]).exists())
        with patch("toolchain_download.urlopen", return_value=Response(b"fixture")):
            self.assertEqual(acquire_payload(self.payload, self.root).read_bytes(), b"fixture")

    def test_rejects_size_overrun(self):
        with patch("toolchain_download.urlopen", return_value=Response(b"too many bytes")):
            with self.assertRaisesRegex(ValueError, "declared size"):
                acquire_payload(self.payload, self.root)

    def test_overstated_size_accepts_only_exact_pinned_hash_and_reuses(self):
        payload = {**self.payload, "size": 12}
        with patch("toolchain_download.urlopen", return_value=Response(b"fixture")) as request:
            path = acquire_payload(payload, self.root)
            self.assertEqual(path.read_bytes(), b"fixture")
            self.assertEqual(acquire_payload(payload, self.root), path)
            request.assert_called_once()

    def test_rejects_untrusted_urls_and_malformed_contracts(self):
        for change in ({"url": "https://example.com/file"}, {"url": "http://download.visualstudio.microsoft.com/file"},
                       {"sha256": "../other"}, {"size": -1}, {"size": True}):
            with self.assertRaises(ValueError):
                validate_payload({**self.payload, **change})

    def test_rejects_redirect_to_another_host(self):
        response = Response(b"fixture")
        response.geturl = lambda: "https://example.com/file"
        with patch("toolchain_download.urlopen", return_value=response):
            with self.assertRaisesRegex(ValueError, "untrusted"):
                acquire_payload(self.payload, self.root)
