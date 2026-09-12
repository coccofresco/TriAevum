import hashlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch
import urllib.error

from verified_download import download_verified


def response(data, url="https://example.test/archive"):
    stream = io.BytesIO(data)
    stream.geturl = lambda: url
    return stream


class VerifiedDownloadTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.output = Path(temporary.name) / "archive.zip"
        self.data = b"expected archive"

    def acquire(self, **kwargs):
        return download_verified("https://example.test/archive", self.output,
                                 size=len(self.data), sha256=hashlib.sha256(self.data).hexdigest(), **kwargs)

    def test_timeout_then_truncated_then_success(self):
        retry = Mock()
        with patch("verified_download.urllib.request.urlopen", side_effect=[
                TimeoutError("slow CDN"), response(b"short"), response(self.data)]) as fetch, \
                patch("verified_download.time.sleep"):
            self.acquire(retry=retry)
            self.assertEqual(fetch.call_count, 3)
            self.assertEqual([call.args[0] for call in retry.call_args_list], [2, 3])
        self.assertEqual(self.output.read_bytes(), self.data)
        self.assertFalse(list(self.output.parent.glob("*.partial")))

    def test_integrity_failure_does_not_retry_or_replace_existing_cache(self):
        self.output.write_bytes(b"old cache")
        with patch("verified_download.urllib.request.urlopen", return_value=response(b"x" * len(self.data))) as fetch:
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                self.acquire()
            self.assertEqual(fetch.call_count, 1)
        self.assertEqual(self.output.read_bytes(), b"old cache")
        self.assertFalse(list(self.output.parent.glob("*.partial")))

    def test_retryable_status_and_permanent_failure(self):
        for code, calls in ((503, 3), (429, 3), (403, 1), (404, 1)):
            with self.subTest(code=code), patch("verified_download.time.sleep"), \
                    patch("verified_download.urllib.request.urlopen", side_effect=
                          urllib.error.HTTPError("https://example.test", code, "CDN error", {}, None)) as fetch:
                with self.assertRaises(urllib.error.HTTPError):
                    self.acquire()
                self.assertEqual(fetch.call_count, calls)
                self.assertFalse(self.output.exists())

    def test_deadline_and_insecure_redirect(self):
        with patch("verified_download.time.monotonic", side_effect=[0, 901]), \
                patch("verified_download.urllib.request.urlopen") as fetch:
            with self.assertRaises(TimeoutError):
                self.acquire(attempts=1)
            fetch.assert_not_called()
        with patch("verified_download.urllib.request.urlopen", return_value=response(self.data, "http://example.test")):
            with self.assertRaisesRegex(ValueError, "insecure"):
                self.acquire()
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
