from pathlib import Path
import subprocess
import tempfile
import threading
import unittest
from unittest.mock import patch, MagicMock

from portal_picker import choose, selected_path


class PortalPickerTests(unittest.TestCase):
    def test_decodes_one_local_uri_without_losing_spaces_or_percent(self):
        with tempfile.TemporaryDirectory() as temp:
            expected = Path(temp) / "Personal ROM 100% #1.cci"
            self.assertEqual(selected_path(expected.as_uri() + "\n"), expected)

    def test_rejects_remote_relative_multiple_and_nul(self):
        for value in ("", "https://example.com/rom", "file://server/rom", "file:relative",
                      "file:///rom?query", "file:///rom#fragment", "file:///rom%00.cci",
                      "file:///one\nfile:///two", "file:///rom\rfile:///other"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                selected_path(value)

    def process(self, code, output="", error=""):
        process = MagicMock()
        process.__enter__.return_value = process
        process.returncode = code
        process.communicate.return_value = (output, error)
        return process

    def test_cancel_is_not_error_and_command_is_not_shell_interpolated(self):
        process = self.process(2)
        with patch("portal_picker.subprocess.Popen", return_value=process) as spawn:
            self.assertIsNone(choose(Path("/app/chooser"), title="one; $(two)", directory=True,
                                     parent="x11:12", closing=threading.Event()))
            self.assertEqual(spawn.call_args.args[0],
                             [str(Path("/app/chooser")),
                              "--title", "one; $(two)", "--parent", "x11:12", "--directory"])
            self.assertNotIn("shell", spawn.call_args.kwargs)

    def test_portal_failure_has_actionable_error(self):
        with patch("portal_picker.subprocess.Popen", return_value=self.process(1, error="No desktop portal")):
            with self.assertRaisesRegex(RuntimeError, "No desktop portal"):
                choose(Path("helper"), title="ROM", directory=False, parent="", closing=threading.Event())

    def test_closing_gui_terminates_and_reaps_picker(self):
        process = self.process(0)
        process.communicate.side_effect = [subprocess.TimeoutExpired("helper", 0.1), ("", "")]
        closing = threading.Event()
        closing.set()
        with patch("portal_picker.subprocess.Popen", return_value=process):
            self.assertIsNone(choose(Path("helper"), title="ROM", directory=False, parent="", closing=closing))
        process.terminate.assert_called_once()
        self.assertEqual(process.communicate.call_count, 2)

    def test_nonresponding_picker_is_killed_and_reaped(self):
        process = self.process(0)
        process.communicate.side_effect = [subprocess.TimeoutExpired("helper", 0.1),
                                          subprocess.TimeoutExpired("helper", 12), ("", "")]
        closing = threading.Event()
        closing.set()
        with patch("portal_picker.subprocess.Popen", return_value=process):
            self.assertIsNone(choose(Path("helper"), title="ROM", directory=False, parent="", closing=closing))
        process.kill.assert_called_once()
        self.assertEqual(process.communicate.call_count, 3)
