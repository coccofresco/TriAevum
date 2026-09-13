"""Host storage policy; import and runtime code consume paths, not sandbox rules."""

from dataclasses import dataclass
import os
from pathlib import Path
import sys
from typing import Mapping


FLATPAK_ID = "io.github.coccofresco.TriAevum"


@dataclass(frozen=True)
class HostLayout:
    package: Path
    activation: Path
    use_file_portal: bool = False

    @property
    def data(self) -> Path:
        return self.activation / "data"


def for_package(package: Path, *, environ: Mapping[str, str] | None = None,
                platform: str | None = None) -> HostLayout:
    env = os.environ if environ is None else environ
    platform = sys.platform if platform is None else platform
    package = package.resolve()
    appdir = env.get("APPDIR", "")
    if platform == "linux" and appdir and Path(appdir).is_absolute() and package.is_relative_to(Path(appdir).resolve()):
        data_home = env.get("XDG_DATA_HOME", "")
        if not data_home or not Path(data_home).is_absolute():
            home = env.get("HOME", "")
            if not home or not Path(home).is_absolute():
                raise ValueError("AppImage requires an absolute HOME or XDG_DATA_HOME")
            data_home = str(Path(home) / ".local/share")
        activation = (Path(data_home) / "TriAevum").resolve()
        if activation.is_relative_to(Path(appdir).resolve()):
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
