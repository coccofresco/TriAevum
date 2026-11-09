"""Scan binaries and logs for build-machine source paths.

A path is reported when an absolute Windows drive path or a POSIX home/build
path names a source or debug file (.c, .cc, .cpp, .h, .hpp, .inl, .rs, .pdb).
Data paths such as a ROM's own m:/menu/... strings are not source files and
are not reported. Exit 0 when nothing is found, 1 when a path is found, 2 on
a usage error. Only counts and redacted samples are printed (drive or root
plus file name), never the matched directories.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

_SOURCE = rb"[A-Za-z0-9_.+\-/\\ ]{1,240}\.(?:c|cc|cpp|cxx|h|hh|hpp|inl|rs|pdb)\b"
HOST_SOURCE_PATH = re.compile(
    rb"(?:(?<![A-Za-z0-9])[A-Za-z]:[\\/]" + _SOURCE
    + rb"|(?<![A-Za-z0-9._-])/(?:home|Users|root|mnt|work|build)/" + _SOURCE + rb")")


def find_host_paths(data: bytes) -> list[bytes]:
    return [match.group(0) for match in HOST_SOURCE_PATH.finditer(data)]


def redact(path: bytes) -> str:
    parts = re.split(r"[\\/]", path.decode("latin-1"))
    return f"{parts[0]}/<...>/{parts[-1]}" if len(parts) > 2 else "/".join(parts)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args(argv)
    found = 0
    for file in args.files:
        if not file.is_file():
            print(f"missing: {file.name}")
            return 2
        paths = find_host_paths(file.read_bytes())
        found += len(paths)
        unique = sorted({redact(path) for path in paths})
        print(f"{file.name}: {len(paths)} build-machine source paths ({len(unique)} distinct)")
        for sample in unique[:5]:
            print(f"  {sample}")
    print("BUILD_PATH_SCAN " + ("CLEAN" if found == 0 else f"FOUND {found}"))
    return 0 if found == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
