"""Read MSI cabinet dependencies without running installation actions."""

import ctypes
import os
import ntpath
from pathlib import Path, PureWindowsPath


TABLES = {
    "Media": ("DiskId", "Cabinet"),
    "Directory": ("Directory", "Directory_Parent", "DefaultDir"),
    "Component": ("Component", "Directory_"),
    "File": ("File", "Component_", "FileName", "FileSize"),
}


def read_table(path: Path, table: str) -> list[dict[str, str]]:
    columns = TABLES[table]
    if os.name != "nt":
        raise OSError("MSI metadata inspection requires Windows")
    api = ctypes.WinDLL("msi.dll", winmode=0x00000800)
    handle = ctypes.c_uint
    pointer = ctypes.POINTER(handle)
    signatures = {
        "MsiOpenDatabaseW": [ctypes.c_wchar_p, ctypes.c_void_p, pointer],
        "MsiDatabaseOpenViewW": [handle, ctypes.c_wchar_p, pointer],
        "MsiViewExecute": [handle, handle],
        "MsiViewFetch": [handle, pointer],
        "MsiRecordGetStringW": [handle, ctypes.c_uint, ctypes.POINTER(ctypes.c_wchar), pointer],
        "MsiCloseHandle": [handle],
    }
    for name, args in signatures.items():
        function = getattr(api, name)
        function.argtypes, function.restype = args, ctypes.c_uint

    def check(status):
        if status:
            raise OSError(status, "MSI metadata read failed")

    database, view = handle(), handle()
    check(api.MsiOpenDatabaseW(str(path.resolve()), None, ctypes.byref(database)))
    try:
        query = "SELECT " + ", ".join("`" + column + "`" for column in columns) + " FROM `" + table + "`"
        check(api.MsiDatabaseOpenViewW(database, query, ctypes.byref(view)))
        try:
            check(api.MsiViewExecute(view, 0))
            rows = []
            while True:
                record = handle()
                status = api.MsiViewFetch(view, ctypes.byref(record))
                if status == 259:  # ERROR_NO_MORE_ITEMS
                    return rows
                check(status)
                try:
                    row = {}
                    for index, column in enumerate(columns, 1):
                        size = handle()
                        status = api.MsiRecordGetStringW(record, index, None, ctypes.byref(size))
                        if status not in (0, 234):
                            check(status)
                        if size.value > 32767:
                            raise ValueError("MSI field exceeds the supported length")
                        buffer = ctypes.create_unicode_buffer(size.value + 1)
                        size.value += 1
                        check(api.MsiRecordGetStringW(record, index, buffer, ctypes.byref(size)))
                        row[column] = buffer.value
                    rows.append(row)
                finally:
                    api.MsiCloseHandle(record)
        finally:
            api.MsiCloseHandle(view)
    finally:
        api.MsiCloseHandle(database)


def read_cabinets(path: Path) -> list[str]:
    return [row["Cabinet"] for row in sorted(read_table(path, "Media"), key=lambda row: int(row["DiskId"])) if row["Cabinet"]]


def file_layout(directories: list[dict], components: list[dict], files: list[dict]) -> dict:
    """Map cabinet file IDs to target-relative names, never MSI custom actions."""
    directory_index = {row["Directory"]: row for row in directories}
    component_index = {row["Component"]: row["Directory_"] for row in components}
    if len(directory_index) != len(directories) or len(component_index) != len(components):
        raise ValueError("Duplicate MSI directory/component identity")

    def segment(value):
        value = value.split("|", 1)[-1]
        if (not value or value in (".", "..") or any(c in value for c in '\\/:<>"?*')
                or any(ord(c) < 32 for c in value)
                or (ntpath.isreserved(value) if hasattr(ntpath, "isreserved") else PureWindowsPath(value).is_reserved())
                or value.endswith((".", " "))):
            raise ValueError("Unsafe MSI target path component")
        return value

    def directory_path(key):
        parts, visited = [], set()
        while key:
            if key in visited or key not in directory_index:
                raise ValueError("Cyclic or missing MSI directory")
            visited.add(key)
            row = directory_index[key]
            name = row["DefaultDir"].split(":", 1)[0]
            if row["Directory_Parent"] and name != ".":
                parts.insert(0, segment(name))
            key = row["Directory_Parent"]
        return parts

    result, targets = {}, set()
    for row in files:
        identifier = segment(row["File"])
        destination = "/".join(directory_path(component_index[row["Component_"]]) + [segment(row["FileName"])])
        if identifier in result or destination.casefold() in targets:
            raise ValueError("Duplicate MSI file identity or target")
        size = int(row["FileSize"])
        if size < 0:
            raise ValueError("Invalid MSI file size")
        result[identifier] = {"path": destination, "bytes": size}
        targets.add(destination.casefold())
    return result


def resolve_cabinet_payloads(names: list[str], catalog: dict) -> list[dict]:
    """Resolve only declared external cabinets; embedded streams need no download."""
    try:
        from .toolchain_download import validate_payload
    except ImportError:
        from toolchain_download import validate_payload
    resolved, seen = [], set()
    for name in names:
        if name.startswith("#"):
            continue
        if (PureWindowsPath(name).name != name or not name.lower().endswith(".cab")
                or any(char in name for char in (":", "/", "\\"))):
            raise ValueError("Invalid external MSI cabinet name")
        key = name.casefold()
        if key in seen:
            continue
        payload = catalog.get(key)
        if not isinstance(payload, dict) or PureWindowsPath(payload.get("fileName", "")).name.casefold() != key:
            raise ValueError("MSI cabinet is absent from the pinned catalog: " + name)
        validate_payload(payload)
        resolved.append(payload)
        seen.add(key)
    return resolved
