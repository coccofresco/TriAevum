"""Separate the native game's loader environment from the frozen Python bundle."""

import os
import sys
import ctypes
import ntpath
import subprocess
import threading
from contextlib import contextmanager


_loader_lock = threading.RLock()


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
    if sys.platform == "win32" and getattr(sys, "frozen", False):
        bundle = getattr(sys, "_MEIPASS", None)
        if bundle and "PATH" in environment:
            root = ntpath.normcase(ntpath.abspath(bundle))
            def outside_bundle(entry):
                path = ntpath.normcase(ntpath.abspath(entry.strip('"')))
                try:
                    return ntpath.commonpath((root, path)) != root
                except ValueError:
                    return True
            environment["PATH"] = ";".join(
                entry for entry in environment["PATH"].split(";") if outside_bundle(entry))
    return environment


@contextmanager
def _native_library_search():
    if sys.platform != "win32" or not getattr(sys, "frozen", False):
        yield
        return
    # PyInstaller's SetDllDirectory is inherited by children; env alone cannot
    # undo it. Restore Forge's search path immediately after CreateProcess.
    with _loader_lock:
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        get_directory = kernel.GetDllDirectoryW
        get_directory.argtypes = [ctypes.c_uint32, ctypes.c_wchar_p]
        get_directory.restype = ctypes.c_uint32
        set_directory = kernel.SetDllDirectoryW
        set_directory.argtypes = [ctypes.c_wchar_p]
        set_directory.restype = ctypes.c_int
        size = get_directory(0, None)
        previous = ctypes.create_unicode_buffer(size + 1)
        if get_directory(len(previous), previous) > size:
            raise OSError("DLL search directory changed during native launch")
        if not set_directory(None):
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            yield
        finally:
            if not set_directory(previous.value or None):
                raise ctypes.WinError(ctypes.get_last_error())


def popen_native(*args, **kwargs):
    kwargs.setdefault("env", native_process_environment())
    with _native_library_search():
        return subprocess.Popen(*args, **kwargs)


def run_native(*args, capture_output=False, timeout=None, check=False, **kwargs):
    if capture_output:
        if "stdout" in kwargs or "stderr" in kwargs:
            raise ValueError("capture_output conflicts with stdout/stderr")
        kwargs.update(stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    with popen_native(*args, **kwargs) as process:
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except BaseException:
            process.kill()
            process.communicate()
            raise
        if check and process.returncode:
            raise subprocess.CalledProcessError(process.returncode, process.args, stdout, stderr)
        return subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)


def describe_exit_status(code: int) -> str:
    status = code & 0xFFFFFFFF
    diagnoses = {
        0xC0000139: "a required DLL export is missing (incompatible dependency)",
        0xC0000135: "a required DLL is missing",
        0xC000007B: "a dependency has the wrong executable format or architecture",
        0xC000001D: "the CPU encountered an unsupported instruction",
    }
    if status in diagnoses:
        return (f"0x{status:08X}: {diagnoses[status]}. This failed before ROM processing. "
                "Use a freshly extracted complete release, without mixing DLLs from other builds. "
                "If it persists, report your Windows version and this code")
    return str(code)
