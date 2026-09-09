"""Bind the installation worker tree to its owner's process lifetime."""

import ctypes
import os
import select
import sys
import threading
import time

if os.name == "nt":
    from ctypes import wintypes

_job = None


def _watch_owner_linux(owner_pid: int) -> None:
    # A pidfd is a stable handle to exactly this process (no PID reuse), and
    # becomes readable when it exits, matching the SYNCHRONIZE wait on Windows.
    # The GUI, not any PyInstaller bootstrap in between, is the owner.
    try:
        fd = os.pidfd_open(owner_pid)
    except (AttributeError, OSError):
        # Pre-5.3 kernels or a sandbox denying pidfd_open: poll /proc instead.
        # The 1 s poll tolerates a PID-reuse window that pidfd does not have.
        def poll():
            while os.path.exists(f"/proc/{owner_pid}"):
                time.sleep(1.0)
            os._exit(125)
        threading.Thread(target=poll, name="ForgeOwnerLifetime", daemon=True).start()
        return

    def monitor():
        select.select([fd], [], [])
        os.close(fd)
        os._exit(125)
    threading.Thread(target=monitor, name="ForgeOwnerLifetime", daemon=True).start()


def watch_owner(owner_pid: int) -> None:
    if owner_pid <= 0 or owner_pid == os.getpid():
        raise ValueError("Invalid worker owner PID")
    if os.name != "nt":
        if sys.platform.startswith("linux"):
            _watch_owner_linux(owner_pid)
        return
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x00100000, False, owner_pid)  # SYNCHRONIZE
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    def monitor():
        result = kernel.WaitForSingleObject(handle, 0xFFFFFFFF)
        kernel.CloseHandle(handle)
        if result == 0:
            # Closing this worker also closes its job, killing all descendants.
            os._exit(125)
    threading.Thread(target=monitor, name="ForgeOwnerLifetime", daemon=True).start()


def bind_worker_lifetime() -> None:
    global _job
    if os.name != "nt" or _job is not None:
        return
    class Basic(ctypes.Structure):
        _fields_ = [("ProcessTime", ctypes.c_longlong), ("JobTime", ctypes.c_longlong),
                    ("Flags", wintypes.DWORD), ("MinimumWorkingSet", ctypes.c_size_t),
                    ("MaximumWorkingSet", ctypes.c_size_t), ("ActiveProcesses", wintypes.DWORD),
                    ("Affinity", ctypes.c_size_t), ("Priority", wintypes.DWORD),
                    ("Scheduling", wintypes.DWORD)]
    class Io(ctypes.Structure):
        _fields_ = [(name, ctypes.c_ulonglong) for name in (
            "ReadOperations", "WriteOperations", "OtherOperations", "ReadBytes", "WriteBytes", "OtherBytes")]
    class Extended(ctypes.Structure):
        _fields_ = [("Basic", Basic), ("Io", Io), ("ProcessMemory", ctypes.c_size_t),
                    ("JobMemory", ctypes.c_size_t), ("PeakProcessMemory", ctypes.c_size_t),
                    ("PeakJobMemory", ctypes.c_size_t)]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    kernel.CreateJobObjectW.restype = wintypes.HANDLE
    kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    kernel.SetInformationJobObject.restype = wintypes.BOOL
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    kernel.AssignProcessToJobObject.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateJobObjectW(None, None)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    limits = Extended()
    limits.Basic.Flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not kernel.SetInformationJobObject(handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
        error = ctypes.get_last_error()
        kernel.CloseHandle(handle)
        raise ctypes.WinError(error)
    if not kernel.AssignProcessToJobObject(handle, kernel.GetCurrentProcess()):
        error = ctypes.get_last_error()
        kernel.CloseHandle(handle)
        raise ctypes.WinError(error)
    # The OS closes this non-inheritable handle when this worker exits or dies.
    # Closing it here would also terminate the current process.
    _job = handle
