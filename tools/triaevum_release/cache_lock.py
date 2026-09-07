"""Kernel-owned build locks: process termination cannot leave an active lock."""

import os
from pathlib import Path


def acquire_cache_lock(path: Path) -> int:
    descriptor = os.open(path, os.O_CREAT | os.O_RDWR, 0o600)
    try:
        if os.read(descriptor, 4) == b"pid=":
            raise ValueError(f"Legacy build lock exists; close the old Forge before removing it: {path}")
        if os.fstat(descriptor).st_size == 0:
            os.write(descriptor, b"\0")
        os.lseek(descriptor, 0, os.SEEK_SET)
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(descriptor, msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return descriptor
    except BaseException:
        os.close(descriptor)
        raise


def release_cache_lock(descriptor: int) -> None:
    # Do not unlink: another process may already have opened the same inode.
    os.close(descriptor)
