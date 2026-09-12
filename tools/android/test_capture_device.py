import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

from tools.android import capture_device as capture


class DeviceCaptureTests(unittest.TestCase):
    def test_authorized_selection(self):
        self.assertEqual(capture.select_device("List of devices attached\nA device model:test\nB unauthorized\n"), "A")

    def test_unauthorized_rejected(self):
        with self.assertRaises(ValueError):
            capture.select_device("List of devices attached\nA unauthorized\n")

    def test_multiple_require_selection(self):
        output = "List of devices attached\nA device\nB device\n"
        with self.assertRaises(ValueError):
            capture.select_device(output)
        self.assertEqual(capture.select_device(output, "B"), "B")

    def test_public_output_rejected(self):
        with self.assertRaises(ValueError):
            capture.create_session(capture.ROOT / "captures")

    def test_sessions_do_not_overwrite(self):
        with tempfile.TemporaryDirectory() as root:
            first, second = capture.create_session(root), capture.create_session(root)
            self.assertNotEqual(first, second)
            self.assertTrue(first.is_dir() and second.is_dir())

    def test_bounded_adb_command(self):
        command = capture.adb_video_command(["adb", "-s", "A"], 5, "/tmp/a.mp4", "1080x2340")
        self.assertIn("--time-limit", command)
        self.assertEqual(command[-3:], ["--size", "1080x2340", "/tmp/a.mp4"])

    def test_scrcpy_cannot_inject_input(self):
        command = capture.scrcpy_command("scrcpy", "A", 5, Path("capture.mp4"))
        for flag in ("--no-control", "--no-playback", "--no-window", "--no-audio", "--time-limit=5"):
            self.assertIn(flag, command)

    def test_audio_requires_explicit_request(self):
        command = capture.scrcpy_command("scrcpy", "A", 5, Path("capture.mp4"), True)
        self.assertNotIn("--no-audio", command)
        self.assertIn("--audio-codec=aac", command)

    def test_screenshot_preserves_binary(self):
        data = capture.PNG_SIGNATURE + bytes(range(256))
        with tempfile.TemporaryDirectory() as root, patch.object(capture, "run") as run:
            run.return_value.stdout = data
            target = Path(root) / "screen.png"
            capture.screenshot(["adb"], target)
            self.assertEqual(target.read_bytes(), data)

    def test_invalid_screenshot_rejected(self):
        with tempfile.TemporaryDirectory() as root, patch.object(capture, "run") as run:
            run.return_value.stdout = b"error: no devices"
            with self.assertRaises(ValueError):
                capture.screenshot(["adb"], Path(root) / "screen.png")


if __name__ == "__main__":
    unittest.main()
