"""Scan binaries and logs for build-machine source paths.

A path is reported when an absolute Windows drive path or a POSIX home/build
path names a source or debug file (.c, .cc, .cpp, .h, .hpp, .inl, .rs, .pdb).
Data paths such as the ROM's own menu/... strings (RomFS drive) are not source files and
are not reported. Exit 0 when nothing is found, 1 when a path is found, 2 on
a usage error. Only counts and redacted samples are printed (drive or root
plus file name), never the matched directories.

--allow MARKER (repeatable) names a third-party build-tree marker, such as
"/vcpkg/buildtrees/" or "/dvs/p4/build/": a path containing it (case-insensitive,
either slash) is listed as third-party and does not fail the scan. Our own
build-machine paths never carry such a marker, so they still fail.
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


def is_allowed(path: bytes, markers: list[str]) -> bool:
    text = path.decode("latin-1").replace("\\", "/").lower()
    return any(marker.replace("\\", "/").lower() in text for marker in markers)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--allow", action="append", default=[], metavar="MARKER")
    args = parser.parse_args(argv)
    found = 0
    third_party = 0
    for file in args.files:
        if not file.is_file():
            print(f"missing: {file.name}")
            return 2
        all_paths = find_host_paths(file.read_bytes())
        paths = [path for path in all_paths if not is_allowed(path, args.allow)]
        allowed = len(all_paths) - len(paths)
        third_party += allowed
        found += len(paths)
        if allowed:
            print(f"{file.name}: {allowed} third-party build-tree paths (allowed)")
        unique = sorted({redact(path) for path in paths})
        print(f"{file.name}: {len(paths)} build-machine source paths ({len(unique)} distinct)")
        for sample in unique[:5]:
            print(f"  {sample}")
    print("BUILD_PATH_SCAN " + ("CLEAN" if found == 0 else f"FOUND {found}")
          + (f" (third-party allowed {third_party})" if third_party else ""))
    return 0 if found == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
