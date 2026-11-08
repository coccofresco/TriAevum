"""Public-tree privacy sweep.

Scans every tracked text file (code, tests, docs, scripts, CMake, JSON) for
strings that identify a maintainer machine instead of describing the project:

* absolute paths on local work drives (any drive letter except the system
  drive C:, the example drive D: and the placeholder drives X: and Z:),
* Windows profile paths (C:/Users/<name>) and POSIX home paths
  (/home/<name>) that name a real account,
* the private work-directory names of the maintainer workspace,
* the current OS account name and host name, plus any extra terms passed with
  --term or TRIAEVUM_PRIVACY_TERMS (comma separated) -- these are read at run
  time and never written into the tree,
* private key material and RFC 1918 host addresses.

Documented placeholders are not findings: <user>, %USERPROFILE%, $HOME, ~,
C:/Users/<user>, /home/<user>, the example accounts listed in
ALLOWED_PROFILE_NAMES, and the container mount points in
ALLOWED_POSIX_HOMES. A line may carry the marker "privacy-sweep: allow" to
document a deliberate exception; files in ALLOWED_FILES are embedded binary
data (font blobs) whose base64 text can look like a path.

Exit status: 0 clean, 1 findings, 2 usage error.
"""
from __future__ import annotations

import argparse
import getpass
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
from typing import Iterable, Iterator

ALLOW_MARKER = "privacy-sweep: allow"

# Example accounts used by tests and docs as documented placeholders.
ALLOWED_PROFILE_NAMES = frozenset({
    "public", "default", "all users", "jdoe", "jane", "someone", "builder", "synthetic-builder",
    "runneradmin", "player", "user",
})
# POSIX home directories that are container mount points, not accounts.
ALLOWED_POSIX_HOMES = frozenset({
    "work", "src", "rust", "title-source", "user", "builder", "synthetic-builder", "runner",
    "player", "deck", "jdoe", "someone", "u",
})
# Generic account names that are not identifying (CI and container defaults).
GENERIC_ACCOUNTS = frozenset({
    "root", "runner", "runneradmin", "builder", "user", "admin", "administrator", "steam",
    "deck", "player", "ubuntu", "debian", "localhost",
})
ALLOWED_DRIVES = frozenset("CDXZ")
# Embedded binary data rendered as text (compressed font blobs).
ALLOWED_FILES = frozenset({
    "runtime/three_ds_recomp/include/ship/window/gui/Fonts.h",
})
MAX_BYTES = 8 * 1024 * 1024

_SEP = r"[/\\]"
DRIVE_PATH = re.compile(r"(?<![\w+/=.<\-])([A-Za-z]):" + _SEP + r"+[\w.$%<{~-]")
WINDOWS_PROFILE = re.compile(r"(?<![\w])[A-Za-z]:" + _SEP + r"+Users" + _SEP + r"+([^/\\\s\"'`<>%$*{]+)")
POSIX_HOME = re.compile(r"(?<![\w.~])/home/([A-Za-z_][\w-]*)")
WORK_TOKEN = re.compile(r"_triaevum_(?:work|scratch)", re.IGNORECASE)
PRIVATE_KEY = re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")
PRIVATE_IP = re.compile(
    # 10.0.x.y is the Windows SDK version scheme, not a host address.
    r"(?<![\w./])(?:10\.(?!0\.)\d{1,3}\.\d{1,3}\.\d{1,3}|192\.168\.\d{1,3}\.\d{1,3}"
    r"|172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3})(?![\w.])"
)


def private_terms(extra: Iterable[str] = (), environ=os.environ) -> list[str]:
    """Identifying names of this machine, resolved at run time only."""
    terms: set[str] = set()
    candidates = list(extra)
    candidates += [part for part in environ.get("TRIAEVUM_PRIVACY_TERMS", "").split(",")]
    for key in ("USERNAME", "USER", "LOGNAME"):
        candidates.append(environ.get(key, ""))
    try:
        candidates.append(getpass.getuser())
    except Exception:  # noqa: BLE001 - no account information is not an error
        pass
    try:
        candidates.append(socket.gethostname())
    except OSError:
        pass
    for candidate in candidates:
        value = candidate.strip()
        if len(value) >= 4 and value.lower() not in GENERIC_ACCOUNTS:
            terms.add(value)
    return sorted(terms, key=str.lower)


def tracked_files(root: Path) -> list[str]:
    try:
        output = subprocess.check_output(
            ["git", "-C", str(root), "ls-files", "-z"], stderr=subprocess.DEVNULL
        )
        return [name for name in output.decode("utf-8").split("\0") if name]
    except (OSError, subprocess.CalledProcessError):
        return sorted(
            path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()
        )


def read_text(path: Path) -> str | None:
    try:
        if path.stat().st_size > MAX_BYTES:
            return None
        payload = path.read_bytes()
    except OSError:
        return None
    if b"\0" in payload[:8192]:
        return None
    try:
        return payload.decode("utf-8")
    except UnicodeDecodeError:
        return payload.decode("latin-1")


def scan_line(line: str, term_patterns: list[tuple[str, re.Pattern[str]]]) -> Iterator[tuple[str, str]]:
    if ALLOW_MARKER in line:
        return
    for match in DRIVE_PATH.finditer(line):
        if match.group(1).upper() not in ALLOWED_DRIVES:
            yield "local-drive-path", match.group(0)
    for match in WINDOWS_PROFILE.finditer(line):
        if match.group(1).lower() not in ALLOWED_PROFILE_NAMES:
            yield "user-profile-path", match.group(0)
    for match in POSIX_HOME.finditer(line):
        if match.group(1).lower() not in ALLOWED_POSIX_HOMES:
            yield "user-home-path", match.group(0)
    for match in WORK_TOKEN.finditer(line):
        yield "work-directory-name", match.group(0)
    if PRIVATE_KEY.search(line):
        yield "private-key", "PRIVATE KEY block"
    for match in PRIVATE_IP.finditer(line):
        yield "private-address", match.group(0)
    for label, pattern in term_patterns:
        if pattern.search(line):
            yield "private-name", label


def sweep(root: Path, files: Iterable[str], terms: Iterable[str]) -> list[dict[str, object]]:
    term_patterns = [
        (f"term#{index}", re.compile(r"(?<![A-Za-z0-9])" + re.escape(term) + r"(?![A-Za-z0-9])", re.IGNORECASE))
        for index, term in enumerate(terms)
    ]
    findings: list[dict[str, object]] = []
    for name in files:
        if name in ALLOWED_FILES:
            continue
        text = read_text(root / name)
        if text is None:
            continue
        for number, line in enumerate(text.splitlines(), 1):
            for rule, excerpt in scan_line(line, term_patterns):
                findings.append({"file": name, "line": number, "rule": rule, "match": excerpt})
    return findings


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--term", action="append", default=[],
                        help="extra identifying string to refuse (not stored anywhere)")
    parser.add_argument("--no-host-terms", action="store_true",
                        help="do not add the current account and host names")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("paths", nargs="*", help="limit the sweep to these repo-relative files")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    if not root.is_dir():
        parser.error(f"not a directory: {root}")
    terms = private_terms(args.term, {} if args.no_host_terms else os.environ)
    if args.no_host_terms:
        terms = sorted({term for term in args.term if len(term.strip()) >= 4})
    files = args.paths or tracked_files(root)
    findings = sweep(root, files, terms)
    if args.json:
        print(json.dumps({"files": len(files), "terms": len(terms), "findings": findings}, indent=2))
    else:
        for item in findings:
            # A private-name finding never echoes the term itself.
            print(f"{item['file']}:{item['line']}: {item['rule']}: {item['match']}")
        print(f"privacy sweep: {len(files)} files, {len(terms)} run-time terms, "
              f"{len(findings)} findings")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
