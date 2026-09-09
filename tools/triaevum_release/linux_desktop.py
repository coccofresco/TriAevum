"""Developer graphical-test prerequisites; never change desktop preferences."""

import os
import shutil
import subprocess


def dpms_states(output: str) -> list[str]:
    return [line.rpartition(":")[2].strip().lower() for line in output.splitlines()
            if line.startswith("dpms mode for screen ")]


def check_display_power(environment: dict) -> str:
    doctor = shutil.which("kscreen-doctor", path=environment.get("PATH"))
    if not doctor or not environment.get("WAYLAND_DISPLAY"):
        return "unavailable"
    env = dict(environment, QT_QPA_PLATFORM="wayland", LC_ALL="C")
    try:
        result = subprocess.run([doctor, "--dpms", "show"], env=env,
                                capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return "unavailable"
    states = dpms_states(result.stdout) if result.returncode == 0 else []
    if states and all(state == "off" for state in states):
        raise ValueError("All desktop outputs are powered off (DPMS). Wake the monitor before "
                         "graphical qualification; X11 windows can remain iconified or block while mapping.")
    return "on" if "on" in states else "unavailable"


if __name__ == "__main__":
    print("Desktop display-power check: " + check_display_power(os.environ))
