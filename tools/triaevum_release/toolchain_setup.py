"""Package-owned acquisition configuration, separate from Forge's GUI."""

from dataclasses import dataclass
from pathlib import Path

try:
    from .common import load_json_object, normalize_relative_path, sha256_file
    from .prepare_windows_toolchain import prepare
except ImportError:
    from common import load_json_object, normalize_relative_path, sha256_file
    from prepare_windows_toolchain import prepare


@dataclass(frozen=True)
class Setup:
    plan: dict
    license_text: str
    clang_resource: Path
    clang_version: str
    msvc_version: str
    sdk_version: str
    identity: str


def load_setup(path: Path) -> Setup | None:
    if not path.exists():
        return None
    config = load_json_object(path)
    if config.get("format") != "triaevum_toolchain_setup_v1":
        raise ValueError("Unsupported packaged toolchain setup")
    root = path.parent.resolve()
    def local(name):
        target = root / normalize_relative_path(config[name])
        if target.is_symlink() or not target.resolve().is_relative_to(root):
            raise ValueError("Toolchain setup file escaped the package")
        return target
    plan, license_file = local("plan"), local("license")
    for name, file in (("plan", plan), ("license", license_file)):
        if sha256_file(file) != config.get(name + "_sha256"):
            raise ValueError("Packaged toolchain " + name + " changed")
    text = license_file.read_text(encoding="utf-8")
    if not text.strip():
        raise ValueError("Packaged toolchain license is empty")
    versions = [config[name] for name in ("clang_version", "msvc_version", "sdk_version")]
    if any(not isinstance(v, str) or not v or any(not p.isdigit() for p in v.split(".")) for v in versions):
        raise ValueError("Invalid packaged toolchain version")
    return Setup(load_json_object(plan), text, local("clang_resource"), *versions, sha256_file(path))


def run_setup(setup: Setup, data_root: Path, *, compiler: Path, archiver: Path,
              support: Path, include: Path, accepted_identity: str, progress=None) -> dict:
    if accepted_identity != setup.identity:
        raise ValueError("Toolchain license acceptance does not match this setup")
    return prepare(setup.plan, cache=data_root / "toolchain-cache", output=data_root / "toolchain",
                   diagnostics=data_root / "toolchain-diagnostics", compiler=compiler, archiver=archiver,
                   support=support, include=include, clang_resource=setup.clang_resource,
                   clang_version=setup.clang_version, msvc_version=setup.msvc_version,
                   sdk_version=setup.sdk_version, accept_licenses=True, progress=progress)
