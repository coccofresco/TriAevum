"""Check that every Windows PE in a package finds its DLL imports where Windows looks.

Windows resolves a module's eager imports from the directory of the process
executable and from the system; it never searches a parent folder. A package
whose runtime/TriAevum.exe imports libc++.dll kept only at the package root
starts on a build host whose runner adds extra search paths, and fails on
Windows with 0xC0000135. The rule checked here: each non-system import of a PE
exists in the PE's own directory; a module that is only ever loaded into
another program's process (release_policy.json "pe_imports" "hosts") may
instead find it in every directory of its host executables. A role may list
imports it is allowed to miss when the module is optional and loaded on demand
("optional_missing").
"""

from __future__ import annotations

from pathlib import Path, PurePosixPath
from typing import Iterable, Mapping

try:
    from .vulkan_imports import eager_imports, is_pe
except ImportError:
    from vulkan_imports import eager_imports, is_pe


def _is_system(name: str, system: set[str]) -> bool:
    lower = name.lower()
    return lower in system or lower.startswith(("api-ms-win-", "ext-ms-win-"))


def _host_directories(directory: str, hosts: Mapping[str, Iterable[str]]) -> list[str] | None:
    for prefix, directories in hosts.items():
        if (directory + "/").startswith(prefix):
            return [str(item).rstrip("/") for item in directories]
    return None


def pe_import_errors(root: Path, declared: Mapping[str, dict], config: Mapping) -> list[str]:
    """Errors for `declared` (package-relative path -> manifest item with its role)."""
    system = {str(name).lower() for name in config.get("system", [])}
    hosts = config.get("hosts", {})
    optional = {str(role): {str(name).lower() for name in names}
                for role, names in config.get("optional_missing", {}).items()}
    by_directory: dict[str, set[str]] = {}
    for relative in declared:
        pure = PurePosixPath(relative)
        by_directory.setdefault(str(pure.parent) if str(pure.parent) != "." else "", set()).add(pure.name.lower())
    errors = []
    for relative, item in sorted(declared.items()):
        pure = PurePosixPath(relative)
        if pure.suffix.lower() not in (".exe", ".dll"):
            continue
        image = (root / relative).read_bytes()
        if not is_pe(image):
            continue
        directory = str(pure.parent) if str(pure.parent) != "." else ""
        allowed_missing = optional.get(str(item.get("role")), set())
        for name in eager_imports(image):
            lower = name.lower()
            if _is_system(name, system) or lower in by_directory.get(directory, set()):
                continue
            host = _host_directories(directory, hosts) if pure.suffix.lower() == ".dll" else None
            if host and all(lower in by_directory.get(d, set()) for d in host):
                continue
            if lower in allowed_missing:
                continue
            where = f"its directory {directory or '(root)'}" + (f" or every host directory {host}" if host else "")
            errors.append(f"{relative} imports {name}, which is neither a system DLL nor in {where}")
    return errors
