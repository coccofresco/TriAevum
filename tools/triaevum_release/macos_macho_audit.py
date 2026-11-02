"""Read-only arm64 Mach-O and sealed-bundle gates; no SDK is required.

The byte parser is usable by the cross-platform release audit. On macOS the
publisher additionally verifies Apple's code signatures after assembling the
whole bundle. An ad-hoc signature is integrity sealing, not notarization.
"""

from __future__ import annotations

import json
import hashlib
from pathlib import Path
import plistlib
import re
import struct
import subprocess
import sys

try:
    from .common import load_json_object, sha256_file
    from .macos_binary_privacy import contains_host_path
except ImportError:
    from common import load_json_object, sha256_file
    from macos_binary_privacy import contains_host_path


RECEIPT = "docs/macos-bundle.json"
FORMAT = "triaevum_macos_bundle_v1"
MINIMUM_MACOS = "14.0"
MACHO_ROLES = frozenset({"runtime_executable", "forge_executable", "forge_command_line",
                         "forge_runtime_module", "shader_preparation_tool", "precompiled_title",
                         "runtime_library"})
LAUNCHER = "TriAevumLauncher"
EXTERNAL_FILES = frozenset({
    "Contents/Info.plist",
    "Contents/Frameworks/libSDL2-2.0.0.dylib",
    "Contents/Frameworks/libvulkan.1.dylib",
    "Contents/Frameworks/libMoltenVK.dylib",
    "Contents/MacOS/TriAevumLauncher",
})
SIGNATURE = "Contents/_CodeSignature/CodeResources"
SYSTEM_PREFIXES = ("/usr/lib/", "/System/Library/Frameworks/", "/System/Library/PrivateFrameworks/")
# Both byte orders of thin 32/64-bit and universal 32/64-bit Mach-O. Only
# thin little-endian arm64 is accepted for declared native roles, but all
# loadable image forms must be recognized under resource/document roles.
MACHO_MAGICS = frozenset(bytes.fromhex(value) for value in (
    "feedface", "cefaedfe", "feedfacf", "cffaedfe",
    "cafebabe", "bebafeca", "cafebabf", "bfbafeca"))


def version(value: str) -> tuple[int, int, int]:
    if not isinstance(value, str) or not re.fullmatch(r"[0-9]+\.[0-9]+(?:\.[0-9]+)?", value):
        raise ValueError(f"Invalid macOS deployment version: {value!r}")
    parts = tuple(map(int, value.split(".")))
    return (parts + (0,))[:3]


def _packed_version(value: int) -> str:
    return f"{value >> 16}.{(value >> 8) & 255}.{value & 255}"


def inspect_macho(path: Path) -> dict:
    """Require a thin ARM64 executable/dylib, bounded load commands and minos.

    A fat binary is rejected: the public package currently targets arm64 only.
    This parses no symbols or executable code and never executes an input.
    """
    with path.open("rb") as stream:
        header = stream.read(32)
        if len(header) != 32 or header[:4] != b"\xcf\xfa\xed\xfe":
            raise ValueError(f"Expected thin Mach-O64 little-endian artifact: {path.name}")
        _, cpu, subtype, kind, count, size, _, _ = struct.unpack("<8I", header)
        if cpu != 0x0100000C or subtype & 0xFFFFFF != 0 or kind not in (2, 6):
            raise ValueError(f"Expected arm64 Mach-O executable or dylib: {path.name}")
        if not 0 < count <= 16384 or not 8 * count <= size <= 16 * 1024 * 1024:
            raise ValueError(f"Invalid Mach-O load-command bounds: {path.name}")
        commands = stream.read(size)
    if len(commands) != size:
        raise ValueError(f"Truncated Mach-O load commands: {path.name}")
    result = {"kind": kind, "dependencies": [], "rpaths": [], "id": None, "signed": False}
    minima = []
    offset = 0
    for _ in range(count):
        if offset + 8 > size:
            raise ValueError("Truncated Mach-O load command")
        command, length = struct.unpack_from("<II", commands, offset)
        if length < 8 or length % 8 or offset + length > size:
            raise ValueError("Invalid Mach-O load command size")
        data = commands[offset:offset + length]
        if command == 0x32:  # LC_BUILD_VERSION
            if length < 24:
                raise ValueError("Truncated LC_BUILD_VERSION")
            platform, minimum, _, tools = struct.unpack_from("<4I", data, 8)
            if platform != 1 or 24 + tools * 8 != length:
                raise ValueError("Mach-O is not a macOS build")
            minima.append(_packed_version(minimum))
        elif command == 0x24:  # LC_VERSION_MIN_MACOSX
            if length != 16:
                raise ValueError("Invalid LC_VERSION_MIN_MACOSX")
            minima.append(_packed_version(struct.unpack_from("<I", data, 8)[0]))
        elif command in (0xC, 0xD, 0x80000018, 0x8000001F, 0x20, 0x80000023, 0x8000001C):
            minimum_length = 12 if command == 0x8000001C else 24
            if length < minimum_length:
                raise ValueError("Truncated Mach-O path command")
            start = struct.unpack_from("<I", data, 8)[0]
            if start < minimum_length or start >= length or b"\0" not in data[start:]:
                raise ValueError("Invalid Mach-O path offset")
            name = data[start:].split(b"\0", 1)[0].decode("utf-8")
            if not name:
                raise ValueError("Empty Mach-O dependency path")
            if command == 0xD:
                result["id"] = name
            else:
                result["rpaths" if command == 0x8000001C else "dependencies"].append(name)
        elif command == 0x1D:  # LC_CODE_SIGNATURE
            if length != 16:
                raise ValueError("Invalid LC_CODE_SIGNATURE")
            start, length_bytes = struct.unpack_from("<II", data, 8)
            if start < 32 + size or length_bytes == 0 or start + length_bytes > path.stat().st_size:
                raise ValueError("Invalid Mach-O code signature bounds")
            result["signed"] = True
        offset += length
    if offset != size or len(minima) != 1:
        raise ValueError("Mach-O must have exactly one macOS deployment command")
    result["minimum_macos"] = minima[0]
    return result


def require_macho(path: Path, *, minimum_macos: str, shared: bool | None = None) -> dict:
    result = inspect_macho(path)
    if shared is not None and result["kind"] != (6 if shared else 2):
        raise ValueError(f"Wrong Mach-O file type: {path.name}")
    if version(result["minimum_macos"]) > version(minimum_macos):
        raise ValueError(f"{path.name} requires macOS {result['minimum_macos']}, above {minimum_macos}")
    return result


def launcher_identity(path: Path) -> dict:
    """Hash every launcher byte except the bundle's circular signing metadata.

    codesign seals CodeResources in the main executable; CodeResources seals
    the release manifest. Only this dedicated entry uses a code digest. All
    Forge/runtime/title records still use the complete signed file's SHA-256.
    The signature's size and __LINKEDIT size change on resource sealing; their
    bounded values are parsed and the complete signature is verified on macOS.
    """
    info = inspect_macho(path)
    if info["kind"] != 2 or not info["signed"]:
        raise ValueError("Bundle launcher must be a signed Mach-O executable")
    data = bytearray(path.read_bytes())
    count = struct.unpack_from("<I", data, 16)[0]
    offset, end, signatures = 32, None, 0
    for _ in range(count):
        command, length = struct.unpack_from("<II", data, offset)
        if command == 0x1D:
            end, size = struct.unpack_from("<II", data, offset + 8)
            if end + size != len(data) or size > 1024 * 1024:
                raise ValueError("Bundle launcher signature must be bounded and last")
            data[offset + 12:offset + 16] = bytes(4)
            signatures += 1
        elif command == 0x19 and length >= 72 and data[offset + 8:offset + 24].rstrip(b"\0") == b"__LINKEDIT":
            data[offset + 32:offset + 40] = bytes(8)  # vmsize
            data[offset + 48:offset + 56] = bytes(8)  # filesize
        offset += length
    if signatures != 1:
        raise ValueError("Bundle launcher must have exactly one signature")
    return {"bytes": end, "sha256": hashlib.sha256(data[:end]).hexdigest(),
            "hash_mode": "macho_code_without_bundle_signature_v1"}


def _binary_errors(path: Path, app: Path, minimum_macos: str):
    info = require_macho(path, minimum_macos=minimum_macos)
    if not info["signed"]:
        yield f"Mach-O has no code signature: {path.name}"
    frameworks = app / "Contents/Frameworks"
    if any(name.startswith("@rpath/") for name in info["dependencies"]) and len(info["rpaths"]) != 1:
        yield f"Mach-O with @rpath dependencies must have exactly one LC_RPATH: {path.name}"
    for rpath in info["rpaths"]:
        # Exactly one bundle-local framework search path is installed relative
        # to each executable/library. Absolute build-tree rpaths never ship.
        if not rpath.startswith(("@loader_path/", "@executable_path/")):
            yield f"Non-bundle Mach-O rpath in {path.name}: {rpath}"
            continue
        relative = rpath.split("/", 1)[1]
        # Executables use their own directory; dylibs use @loader_path.
        if rpath.startswith("@executable_path/") and info["kind"] != 2:
            yield f"Dylib rpath depends on its caller: {path.name}"
        if (path.parent / relative).resolve() != frameworks.resolve():
            yield f"Mach-O rpath escapes Frameworks: {path.name}"
    for name in info["dependencies"]:
        if name.startswith(SYSTEM_PREFIXES):
            if "/../" in name:
                yield f"Unsafe system dependency: {name}"
            continue
        if not name.startswith("@rpath/") or "/" in name[len("@rpath/"):]:
            yield f"Non-bundle Mach-O dependency in {path.name}: {name}"
        elif not (frameworks / name[len("@rpath/"):]).is_file():
            yield f"Missing bundle dependency in {path.name}: {name}"
    if info["id"] is not None and info["id"] != "@rpath/" + path.name:
        yield f"Non-bundle Mach-O install name: {path.name}"


def bundle_errors(root: Path, declared: dict, release: dict):
    """Validate all files outside the catalog root, as well as its native code.

    Signing's generated CodeResources cannot hash itself. It is the sole
    un-inventoried outer file, required and verified by codesign on macOS.
    """
    root = root.resolve()
    if root.name != "Resources" or root.parent.name != "Contents" or root.parent.parent.suffix != ".app":
        yield "macOS package must be in an .app/Contents/Resources directory"
        return
    app = root.parent.parent
    try:
        minimum = release.get("minimum_macos")
        if version(minimum) < version(MINIMUM_MACOS):
            raise ValueError(f"macOS package minimum cannot be below {MINIMUM_MACOS}")
        if release.get("forge_profile") != "rust":
            raise ValueError("macOS requires the Rust Forge profile")
        receipt = load_json_object(root / RECEIPT)
        if (receipt.get("format") != FORMAT or receipt.get("minimum_macos") != minimum or
                receipt.get("signing") != "ad-hoc" or receipt.get("notarized") is not False):
            raise ValueError("Invalid macOS bundle receipt")
        entries = receipt.get("files")
        if not isinstance(entries, list) or len(entries) != len(EXTERNAL_FILES):
            raise ValueError("Invalid outer bundle inventory")
        records = {entry["path"]: entry for entry in entries}
        if set(records) != EXTERNAL_FILES:
            raise ValueError("Unexpected outer bundle file")
        for relative, record in records.items():
            path = app / relative
            if path.is_symlink() or not path.is_file():
                raise ValueError(f"Missing or linked outer bundle file: {relative}")
            actual_identity = (launcher_identity(path) if relative == "Contents/MacOS/" + LAUNCHER else
                               {"bytes": path.stat().st_size, "sha256": sha256_file(path)})
            if any(record.get(key) != value for key, value in actual_identity.items()):
                raise ValueError(f"Outer bundle integrity mismatch: {relative}")
        actual = set()
        for path in app.rglob("*"):
            if path.is_symlink():
                yield f"Symbolic link in macOS bundle: {path.relative_to(app)}"
            elif path.is_file() and not path.is_relative_to(root):
                actual.add(path.relative_to(app).as_posix())
        if actual != EXTERNAL_FILES | {SIGNATURE}:
            yield f"Outer bundle allowlist mismatch: {sorted(actual ^ (EXTERNAL_FILES | {SIGNATURE}))}"
        plist = plistlib.loads((app / "Contents/Info.plist").read_bytes())
        if (plist.get("CFBundleExecutable") != LAUNCHER or
                plist.get("CFBundlePackageType") != "APPL" or
                plist.get("LSMinimumSystemVersion") != minimum or
                plist.get("NSHighResolutionCapable") is not True):
            yield "Info.plist differs from the macOS bundle contract"
        icd = load_json_object(root / "config/vulkan/MoltenVK_icd.json")
        if (icd.get("file_format_version") != "1.0.0" or
                icd.get("ICD", {}).get("library_path") != "../../../Frameworks/libMoltenVK.dylib" or
                icd.get("ICD", {}).get("is_portability_driver") is not True):
            yield "MoltenVK ICD is not bundle-relative"
        native = [root / name for name, item in declared.items() if item.get("role") in MACHO_ROLES]
        native += [app / name for name in EXTERNAL_FILES if name.endswith(".dylib")]
        native += [app / "Contents/MacOS" / LAUNCHER]
        for path in native:
            if contains_host_path(path):
                yield f"Native macOS binary embeds a build-machine path: {path.relative_to(app)}"
        for path in native:
            yield from _binary_errors(path, app, minimum)
        # No native executable may be hidden under an asset/document role.
        known = set(native)
        for path in root.rglob("*"):
            if path.is_file() and path not in known:
                with path.open("rb") as stream:
                    magic = stream.read(4)
                if magic in MACHO_MAGICS or magic == b"\x7fELF" or magic[:2] == b"MZ":
                    yield f"Undeclared native-code role: {path.relative_to(root)}"
        if sys.platform == "darwin":
            checked = subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(app)],
                                     capture_output=True, text=True, timeout=60)
            if checked.returncode:
                yield "macOS bundle signature verification failed: " + checked.stderr.strip()
    except (OSError, ValueError, TypeError, KeyError, struct.error, plistlib.InvalidFileException,
            subprocess.SubprocessError) as exc:
        yield f"Invalid macOS bundle: {exc}"
