"""One authority for persistent paths inside an installation and external inputs."""

import os
from dataclasses import dataclass
from pathlib import Path


def resolve_reference(value: str, base: Path) -> Path:
    path = Path(value)
    return (path if path.is_absolute() else base / path).resolve()


@dataclass(frozen=True)
class InstallationContext:
    root: Path

    def reference(self, path: Path, base: Path) -> str:
        path = path.resolve()
        if self.scope(path, base) == "relative":
            return Path(os.path.relpath(path, base.resolve())).as_posix()
        return str(path)

    def scope(self, path: Path, base: Path | None = None) -> str:
        internal = path.resolve().is_relative_to(self.root.resolve())
        if base is not None:
            internal = internal and base.resolve().is_relative_to(self.root.resolve())
        return "relative" if internal else "external"

    def profile_argument(self, path: Path, profile: Path, runtime_dir: Path | None = None) -> str:
        # A file of the runtime's own package is named from the runtime's
        # directory: an AppImage is mounted at a new path on every launch, so
        # an absolute path into it is stale by the next start.
        if runtime_dir is not None and path.resolve().is_relative_to(runtime_dir.resolve()):
            return "${runtime_dir}/" + path.resolve().relative_to(runtime_dir.resolve()).as_posix()
        reference = self.reference(path, profile.parent)
        return reference if Path(reference).is_absolute() else "${profile_dir}/" + reference


def expand_profile_argument(value: str, profile: Path, runtime_dir: Path | None = None) -> str:
    """The runtime expands ${runtime_dir} to its executable's directory."""
    if runtime_dir is not None:
        value = value.replace("${runtime_dir}", runtime_dir.resolve().as_posix())
    return value.replace("${profile_dir}", profile.resolve().parent.as_posix())
