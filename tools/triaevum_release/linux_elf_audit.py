"""AppImage native-runtime dependency/ABI gate; never execute payload ELFs.

Forge's frozen Python closure remains owned by its bundle qualification. Audit
the game, modules, shader helpers and every lib/ ELF against payload providers.
"""
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
import os
import re
import subprocess

# Host OS/driver interfaces; never silently accept libstdc++, libgcc_s or libdecor.
HOST_LIBRARIES = frozenset({
    "libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1",
    "libresolv.so.2", "libutil.so.1", "ld-linux-x86-64.so.2",
    "libvulkan.so.1", "libGL.so.1", "libEGL.so.1", "libGLX.so.0",
    "libOpenGL.so.0", "libGLdispatch.so.0", "libdrm.so.2", "libgbm.so.1",
    "libX11.so.6", "libXext.so.6", "libXcursor.so.1", "libXfixes.so.3",
    "libXi.so.6", "libXrandr.so.2", "libXrender.so.1", "libXss.so.1",
    "libxcb.so.1", "libXau.so.6", "libXdmcp.so.6",
    "libwayland-client.so.0", "libwayland-cursor.so.0", "libwayland-egl.so.1",
    "libxkbcommon.so.0", "libasound.so.2", "libpulse.so.0", "libudev.so.1",
})

@dataclass
class Elf:
    needed: set[str] = field(default_factory=set)
    requirements: dict[str, set[str]] = field(default_factory=dict)
    definitions: set[str] = field(default_factory=set)
    runpath: tuple[str, ...] = ()
    rpath: tuple[str, ...] = ()

def parse_readelf(dynamic: str, versions: str) -> Elf:
    result = Elf(needed=set(re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", dynamic)))
    for tag in ("RUNPATH", "RPATH"):
        match = re.search(r"\(" + tag + r"\).*?\[([^\]]*)\]", dynamic)
        if match:
            setattr(result, tag.lower(), tuple(match[1].split(":")))
    section, provider = "", None
    for line in versions.splitlines():
        if line.startswith("Version needs section"):
            section, provider = "needs", None
        elif line.startswith("Version definition section"):
            section, provider = "definitions", None
        elif line.startswith("Version symbols section"):
            section, provider = "", None
        if section == "needs":
            match = re.search(r"File: (\S+)", line)
            if match:
                provider = match[1]
            match = re.search(r"Name: (\S+)", line)
            if match and provider:
                result.requirements.setdefault(provider, set()).add(match[1])
        elif section == "definitions":
            match = re.search(r"Name: (\S+)", line)
            if match:
                result.definitions.add(match[1])
    return result

def inspect_elf(path: Path) -> Elf:
    def read(option):
        try:
            return subprocess.run(["readelf", "--wide", option, str(path)],
                check=True, capture_output=True, text=True,
                env={**os.environ, "LC_ALL": "C"}).stdout
        except (OSError, subprocess.CalledProcessError) as exc:
            raise ValueError(f"ELF audit cannot inspect {path.name}: {exc}") from exc
    return parse_readelf(read("--dynamic"), read("--version-info"))

def _search_paths(relative: str, elf: Elf) -> list[str]:
    origin = str(PurePosixPath(relative).parent)
    paths = []
    for value in elf.runpath or elf.rpath:
        value = value.replace("$" + "{ORIGIN}", "$ORIGIN")
        if value != "$ORIGIN" and not value.startswith("$ORIGIN/"):
            raise ValueError(f"{relative}: non-relocatable library search path {value!r}")
        parts = []
        for part in (origin + value[len("$ORIGIN"):]).split("/"):
            if part in ("", "."):
                continue
            if part == "..":
                if not parts:
                    raise ValueError(f"{relative}: library search path escapes package")
                parts.pop()
            else:
                parts.append(part)
        paths.append("/".join(parts))
    # AppRun's LD_LIBRARY_PATH precedes RUNPATH but follows old DT_RPATH. It
    # adds lib/compat-cxx (the SDK libstdc++/libgcc_s) unless the host's is
    # newer, so the bundled copy must satisfy every requirement on its own.
    preload = ["lib/compat-cxx", "lib"]
    return [*preload, *paths] if elf.runpath or not elf.rpath else [*paths, *preload]

def dependency_errors(objects: dict[str, Elf], *, max_glibc=(2, 40)) -> list[str]:
    errors = []
    for relative, elf in sorted(objects.items()):
        try:
            search = _search_paths(relative, elf)
        except ValueError as exc:
            errors.append(str(exc))
            continue
        for name in sorted(elf.needed):
            if "/" in name:
                errors.append(f"{relative}: non-relocatable DT_NEEDED {name}")
                continue
            candidates = [f"{directory}/{name}" if directory else name for directory in search]
            provider = next((objects[p] for p in candidates if p in objects), None)
            if provider is None and name not in HOST_LIBRARIES:
                errors.append(f"{relative}: missing bundled dependency {name}")
            elif provider is not None:
                missing = elf.requirements.get(name, set()) - provider.definitions
                if missing:
                    errors.append(f"{relative}: bundled {name} lacks {', '.join(sorted(missing))}")
        for versions in elf.requirements.values():
            for version in versions:
                if version.startswith("GLIBC_"):
                    suffix = version.removeprefix("GLIBC_")
                    if not re.fullmatch(r"\d+(\.\d+)+", suffix):
                        errors.append(f"{relative}: unsupported glibc ABI {version}")
                    elif tuple(map(int, suffix.split("."))) > max_glibc:
                        errors.append(f"{relative}: requires {version}, AppImage ceiling is GLIBC_{'.'.join(map(str, max_glibc))}")
    return errors

def audit_native_elfs(package: Path) -> None:
    objects = {}
    for path in sorted(package.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(package).as_posix()
        if relative.startswith(("_internal/", "source/", "resources/")) or relative == "TriAevumForge":
            continue
        with path.open("rb") as stream:
            magic = stream.read(4)
        if magic == b"\x7fELF":
            objects[relative] = inspect_elf(path)
    if "TriAevum" not in objects:
        raise ValueError("AppImage ELF audit: TriAevum is not an ELF executable")
    errors = dependency_errors(objects)
    if errors:
        raise ValueError("AppImage ELF audit failed: " + "; ".join(errors))
