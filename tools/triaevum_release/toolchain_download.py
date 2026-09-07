"""Bounded, hash-verified Microsoft payload acquisition into a private cache."""

import hashlib
import os
import re
import time
from pathlib import Path
from urllib.parse import urlsplit
from urllib.request import urlopen

try:
    from .cache_lock import acquire_cache_lock, release_cache_lock
except ImportError:
    from cache_lock import acquire_cache_lock, release_cache_lock


def validate_payload(payload: dict) -> tuple[str, int, str]:
    digest = str(payload.get("sha256", "")).lower()
    size = payload.get("size")
    url = str(payload.get("url", ""))
    parsed = urlsplit(url)
    if (not re.fullmatch("[0-9a-f]{64}", digest) or type(size) is not int
            or not 0 < size <= 2 * 1024**3
            or parsed.scheme != "https"
            or parsed.hostname != "download.visualstudio.microsoft.com"
            or parsed.username or parsed.password or parsed.port not in (None, 443)):
        raise ValueError("Invalid or untrusted Microsoft toolchain payload")
    return digest, size, url


def acquire_payload(payload: dict, cache: Path, *, max_seconds: float = 600) -> Path:
    digest, expected_size, url = validate_payload(payload)
    cache = cache.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    destination = cache / digest
    temporary = cache / (digest + ".part")
    if destination.is_symlink() or temporary.is_symlink():
        raise ValueError("Toolchain cache files must not be symlinks")
    lock = acquire_cache_lock(cache / (digest + ".lock"))
    try:
        if destination.is_file() and destination.stat().st_size <= expected_size:
            with destination.open("rb") as source:
                if hashlib.file_digest(source, "sha256").hexdigest() == digest:
                    return destination
        # Completed payloads are reused. An interrupted individual payload is
        # restarted; partial data is never published as a verified cache entry.
        deadline = time.monotonic() + max_seconds
        with urlopen(url, timeout=min(30, max_seconds)) as response:
            validate_payload({**payload, "url": response.geturl()})
            received = 0
            actual = hashlib.sha256()
            with temporary.open("wb") as output:
                while chunk := response.read(1024 * 1024):
                    if time.monotonic() > deadline:
                        raise TimeoutError("Toolchain download exceeded its time budget")
                    received += len(chunk)
                    if received > expected_size:
                        raise ValueError("Toolchain download exceeded its declared size")
                    actual.update(chunk)
                    output.write(chunk)
                output.flush()
                os.fsync(output.fileno())
        # Some VS package records overstate the archive size. The pinned hash
        # establishes exact content identity; size remains a strict upper bound.
        if actual.hexdigest() != digest:
            raise ValueError(
                "Toolchain download failed size/SHA-256 verification: "
                f"expected {expected_size} bytes, SHA-256 {digest}; "
                f"received {received} bytes, SHA-256 {actual.hexdigest()}. "
                "The payload was not activated. Check the pinned catalog and download source."
            )
        temporary.replace(destination)
        return destination
    finally:
        release_cache_lock(lock)
