import subprocess
import unittest
from unittest.mock import patch

from linux_desktop import check_display_power, dpms_states


class DesktopPowerTests(unittest.TestCase):
    def check(self, output, code=0):
        with patch("linux_desktop.shutil.which", return_value="/usr/bin/kscreen-doctor"), \
             patch("linux_desktop.subprocess.run", return_value=subprocess.CompletedProcess([], code, output)) as run:
            result = check_display_power({"WAYLAND_DISPLAY": "wayland-0"})
            self.assertEqual(run.call_args.kwargs["env"]["QT_QPA_PLATFORM"], "wayland")
            return result

    def test_all_off_fails_before_launch(self):
        with self.assertRaisesRegex(ValueError, "Wake the monitor"):
            self.check("dpms mode for screen HDMI-A-2: off\n")

    def test_one_active_output_is_enough(self):
        self.assertEqual(self.check("dpms mode for screen HDMI-A-2: off\n"
                                    "dpms mode for screen eDP-1: on\n"), "on")

    def test_unknown_output_is_not_a_false_failure(self):
        self.assertEqual(self.check("unrecognized output"), "unavailable")
        self.assertEqual(self.check("dpms mode for screen HDMI-A-2: off", code=1), "unavailable")
        self.assertEqual(dpms_states("not a dpms record: off"), [])

    def test_optional_tool_missing(self):
        with patch("linux_desktop.shutil.which", return_value=None), patch("linux_desktop.subprocess.run") as run:
            self.assertEqual(check_display_power({"WAYLAND_DISPLAY": "wayland-0"}), "unavailable")
            run.assert_not_called()

    def test_timeout_is_not_a_false_failure(self):
        with patch("linux_desktop.shutil.which", return_value="kscreen-doctor"), \
             patch("linux_desktop.subprocess.run", side_effect=subprocess.TimeoutExpired([], 10)):
            self.assertEqual(check_display_power({"WAYLAND_DISPLAY": "wayland-0"}), "unavailable")


if __name__ == "__main__":
    unittest.main()
