"""Boolean-only build-path detection for every native macOS package input."""

import re


HOST_PATH = re.compile(
    rb"(?:[A-Za-z]:[\\/]Users[\\/]|/(?:Users|home)/[^/\s\x00]+/|"
    rb"\.cargo[\\/]registry[\\/]src|\.rustup[\\/]toolchains)"
)


def contains_host_path(path):
    # Strip NULs to cover UTF-16/32 as well as native C/C++ strings. Keep only
    # a boolean result: audit errors must never disclose a matched user name.
    overlap = b""
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            data = overlap + chunk.replace(b"\0", b"")
            if HOST_PATH.search(data):
                return True
            overlap = data[-4096:]
    return False
