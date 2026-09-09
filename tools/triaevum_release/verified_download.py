"""Platform/title-independent, bounded HTTPS download with atomic activation."""

import hashlib
import http.client
import os
from pathlib import Path
import re
import tempfile
import time
import urllib.error
import urllib.request

try:
    from .https_transport import download_ssl_context
except ImportError:
    from https_transport import download_ssl_context


def download_verified(url: str, destination: Path, *, size: int, sha256: str,
                      progress=lambda received, total: None,
                      retry=lambda attempt, error: None, attempts: int = 3,
                      max_seconds: float = 900) -> Path:
    if (not url.startswith("https://") or type(size) is not int or size <= 0
            or not re.fullmatch(r"[0-9a-f]{64}", sha256)
            or not 1 <= attempts <= 3 or max_seconds <= 0):
        raise ValueError("Invalid verified-download contract")
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_symlink():
        raise ValueError("Download cache must not be a symbolic link")
    deadline = time.monotonic() + max_seconds
    for attempt in range(1, attempts + 1):
        temporary = None
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Download exceeded its time limit")
            request = urllib.request.Request(url, headers={"User-Agent": "TriAevum-Forge"})
            with urllib.request.urlopen(request, timeout=min(30, remaining),
                                        context=download_ssl_context()) as response:
                if not response.geturl().startswith("https://"):
                    raise ValueError("Download redirected to an insecure URL")
                digest = hashlib.sha256()
                count = 0
                with tempfile.NamedTemporaryFile(dir=destination.parent, suffix=".partial", delete=False) as stream:
                    temporary = Path(stream.name)
                    while block := response.read(1024 * 1024):
                        count += len(block)
                        if count > size or time.monotonic() > deadline:
                            raise ValueError("Download exceeded its size or time limit")
                        digest.update(block)
                        stream.write(block)
                        progress(count, size)
                if count != size:
                    raise EOFError(f"Incomplete download: received {count} of {size} bytes")
                if digest.hexdigest() != sha256:
                    raise ValueError("Download failed SHA-256 integrity verification")
            os.replace(temporary, destination)
            return destination
        except (OSError, EOFError, http.client.HTTPException) as exc:
            retryable = not isinstance(exc, urllib.error.HTTPError) or exc.code in (408, 429, 500, 502, 503, 504)
            if not retryable or attempt == attempts or time.monotonic() >= deadline:
                raise
            retry(attempt + 1, str(exc))
            time.sleep(min(float(attempt), max(0.0, deadline - time.monotonic())))
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    raise RuntimeError("Unreachable verified-download state")
