"""Extract selected cabinet members using SetupAPI, never installer actions."""

import ctypes
import os
from pathlib import Path

try:
    from .common import normalize_relative_path, sha256_file
except ImportError:
    from common import normalize_relative_path, sha256_file


def extract_cabinet(cabinet: Path, output: Path, layout: dict, expected_sha256: str) -> dict:
    if os.name != "nt":
        raise OSError("Cabinet extraction requires Windows")
    if cabinet.is_symlink() or sha256_file(cabinet) != expected_sha256:
        raise ValueError("Cabinet identity mismatch")
    output = output.resolve()
    if output.exists():
        raise ValueError("Cabinet extraction requires a new staging directory")
    targets = {}
    for identifier, descriptor in layout.items():
        target = output / normalize_relative_path(descriptor["path"])
        if not target.resolve().is_relative_to(output) or len(str(target)) >= 260:
            raise ValueError("Cabinet target escapes staging or exceeds SetupAPI path capacity")
        targets[identifier] = target
    if len({str(path).casefold() for path in targets.values()}) != len(targets):
        raise ValueError("Duplicate cabinet targets")

    class CabinetFile(ctypes.Structure):
        _fields_ = [("name", ctypes.c_wchar_p), ("size", ctypes.c_uint32),
                    ("error", ctypes.c_uint32), ("date", ctypes.c_uint16),
                    ("time", ctypes.c_uint16), ("attributes", ctypes.c_uint16),
                    ("target", ctypes.c_wchar * 260)]

    class ExtractedFile(ctypes.Structure):
        _fields_ = [("target", ctypes.c_wchar_p), ("source", ctypes.c_wchar_p),
                    ("error", ctypes.c_uint32), ("flags", ctypes.c_uint32)]

    callback_type = ctypes.WINFUNCTYPE(ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint,
                                      ctypes.c_size_t, ctypes.c_size_t)
    api = ctypes.WinDLL("setupapi.dll", use_last_error=True, winmode=0x00000800)
    api.SetupIterateCabinetW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint, callback_type, ctypes.c_void_p]
    api.SetupIterateCabinetW.restype = ctypes.c_int
    selected, errors = set(), []

    @callback_type
    def callback(_context, notification, param1, _param2):
        try:
            if notification == 0x11:
                info = ctypes.cast(param1, ctypes.POINTER(CabinetFile)).contents
                if info.name not in targets:
                    return 2  # FILEOP_SKIP
                if info.name in selected or info.size != layout[info.name]["bytes"]:
                    raise ValueError("Cabinet member identity/size differs from MSI layout")
                path = targets[info.name]
                path.parent.mkdir(parents=True, exist_ok=True)
                info.target = str(path)
                selected.add(info.name)
                return 1  # FILEOP_DOIT
            if notification == 0x12:
                raise ValueError("Spanned cabinets require explicit acquisition support")
            if notification == 0x13:
                info = ctypes.cast(param1, ctypes.POINTER(ExtractedFile)).contents
                if info.error:
                    raise OSError(info.error, "Cabinet member extraction failed")
            return 0
        except BaseException as error:
            errors.append(error)
            return 0 if notification == 0x11 else 13

    output.mkdir(parents=True)
    success = api.SetupIterateCabinetW(str(cabinet.resolve()), 0, callback, None)
    if errors:
        raise errors[0]
    if not success:
        raise ctypes.WinError(ctypes.get_last_error())
    extracted = {}
    for identifier in sorted(selected):
        path = targets[identifier]
        if not path.is_file() or path.stat().st_size != layout[identifier]["bytes"]:
            raise ValueError("Cabinet extraction did not produce its declared file")
        extracted[identifier] = {**layout[identifier], "sha256": sha256_file(path)}
    if not extracted:
        raise ValueError("No expected files were found in the cabinet")
    return extracted
