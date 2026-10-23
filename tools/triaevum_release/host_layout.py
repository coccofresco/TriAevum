"""Host storage policy; import and runtime code consume paths, not sandbox rules."""

from dataclasses import dataclass
import os
from pathlib import Path
import sys
from typing import Mapping


FLATPAK_ID = "io.github.coccofresco.TriAevum"
APPIMAGE_PAYLOAD = ("usr", "lib", "triaevum")


@dataclass(frozen=True)
class HostLayout:
    package: Path
    activation: Path
    use_file_portal: bool = False

    @property
    def data(self) -> Path:
        return self.activation / "data"


def installation_state(installation: Path, platform: str | None = None) -> Path:
    """Forge's launch profile and activation journal for an installation.

    The Windows installation root holds only the executables, README.md and
    LICENSE, so they live in its data/ folder (the runtime looks there too).
    """
    platform = sys.platform if platform is None else platform
    return installation / "data" if platform == "win32" else installation


def appimage_root(package: Path, appdir: str = "") -> Path | None:
    """Return the AppDir that owns `package`, or None outside an AppImage.

    The layout is authoritative, the environment is only corroboration: a
    payload launched without `APPDIR` (direct binary, sanitized environment,
    wrapper) must still keep user data outside the mounted package, otherwise
    every prepared title, save and cache is discarded when the mount goes away.
    """
    candidates = []
    if appdir and Path(appdir).is_absolute():
        candidates.append(Path(appdir).resolve())
    if tuple(package.parts[-len(APPIMAGE_PAYLOAD):]) == APPIMAGE_PAYLOAD:
        candidates.append(Path(*package.parts[:-len(APPIMAGE_PAYLOAD)]))
    for candidate in candidates:
        if candidate == Path(candidate.anchor) or not package.is_relative_to(candidate):
            continue
        return candidate
    return None


def for_package(package: Path, *, environ: Mapping[str, str] | None = None,
                platform: str | None = None) -> HostLayout:
    env = os.environ if environ is None else environ
    platform = sys.platform if platform is None else platform
    package = package.resolve()
    appdir = appimage_root(package, env.get("APPDIR", ""))
    if platform == "linux" and appdir is not None and package.is_relative_to(appdir):
        data_home = env.get("XDG_DATA_HOME", "")
        if not data_home or not Path(data_home).is_absolute():
            home = env.get("HOME", "")
            if not home or not Path(home).is_absolute():
                raise ValueError("AppImage requires an absolute HOME or XDG_DATA_HOME")
            data_home = str(Path(home) / ".local/share")
        activation = (Path(data_home) / "TriAevum").resolve()
        if activation.is_relative_to(appdir):
            raise ValueError("AppImage activation must be outside the read-only package")
        return HostLayout(package, activation)
    if platform != "linux" or env.get("FLATPAK_ID") != FLATPAK_ID:
        return HostLayout(package, package)
    data_home = env.get("XDG_DATA_HOME", "")
    if not data_home or not Path(data_home).is_absolute():
        raise ValueError("Flatpak did not provide an absolute XDG_DATA_HOME")
    activation = (Path(data_home) / "TriAevum").resolve()
    if activation.is_relative_to(package):
        raise ValueError("Flatpak activation must be outside the read-only package")
    return HostLayout(package, activation, use_file_portal=True)
