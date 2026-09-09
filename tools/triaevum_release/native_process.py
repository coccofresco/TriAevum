"""Separate the native game's loader environment from the frozen Python bundle."""

import os
import sys


def native_process_environment() -> dict[str, str]:
    environment = os.environ.copy()
    if sys.platform.startswith("linux") and getattr(sys, "frozen", False):
        # PyInstaller saves the caller's original search path before injecting
        # its own. Preserve e.g. a Steam Runtime path, not Forge's Python libs.
        original = environment.pop("LD_LIBRARY_PATH_ORIG", None)
        if original:
            environment["LD_LIBRARY_PATH"] = original
        else:
            environment.pop("LD_LIBRARY_PATH", None)
    return environment
