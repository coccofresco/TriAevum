"""Stage a private Linux qualification candidate, not an audited public release."""

import argparse
from pathlib import Path
import shutil

try:
    from .common import atomic_write_json, sha256_file
    from .linux_precompiled_catalog import create_catalog, require_elf
    from .release_platform import LINUX, host_platform
    from .rust_forge_build import BINARIES, RECEIPT
    from .rust_forge_notices import NOTICES, NOTICES_RELATIVE
except ImportError:
    from common import atomic_write_json, sha256_file
    from linux_precompiled_catalog import create_catalog, require_elf
    from release_platform import LINUX, host_platform
    from rust_forge_build import BINARIES, RECEIPT
    from rust_forge_notices import NOTICES, NOTICES_RELATIVE

# The Rust Forge build output (rust_forge_build.py) besides TriAevumForge.
RUST_FILES = (BINARIES[LINUX.target]["triaevum-forge"][0], RECEIPT)


def stage(reference: Path, forge_bundle: Path, runtime_build: Path,
          title: Path, output: Path, source_commit: str, *,
          shader_compiler: Path | None = None, shader_dependencies: list[Path] = ()):
    if host_platform() != LINUX:
        raise ValueError("Stage and qualify Linux candidates on Linux")
    if output.exists():
        raise ValueError("Candidate output must not exist; preserve existing installations")
    # A Rust Forge build, or (for older candidates) a PyInstaller onedir bundle.
    rust = (forge_bundle / RECEIPT).is_file()
    for path, shared in ((runtime_build / LINUX.runtime, False),
                         (runtime_build / LINUX.title_module, True),
                         (runtime_build / "oot3d_game_module.so", True), (title, True),
                         (forge_bundle / LINUX.forge, False),
                         *(((forge_bundle / RUST_FILES[0], False),) if rust else ())):
        require_elf(path, shared=shared)
    if sha256_file(title) == sha256_file(runtime_build / LINUX.title_module):
        raise ValueError("The game title cannot be the empty runtime bootstrap library")
    if not rust and not (forge_bundle / "_internal").is_dir():
        raise ValueError("Forge bundle is neither a Rust Forge build nor a PyInstaller bundle")
    output.mkdir(parents=True)
    # Preserve PyInstaller's internal symlinks; do not import data/ or any
    # Windows executable, title binary or release manifest from the reference.
    shutil.copy2(forge_bundle / LINUX.forge, output / LINUX.forge)
    if not rust:
        shutil.copytree(forge_bundle / "_internal", output / "_internal", symlinks=True)
    for name in ("recipes", "resources", "source/titles", "LICENSES"):
        shutil.copytree(reference / name, output / name)
    for name in ("LICENSE", "LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(reference / name, output / name)
    for name in (LINUX.runtime, LINUX.title_module):
        shutil.copy2(runtime_build / name, output / name)
    (output / "forge").mkdir()
    if rust:
        for relative in RUST_FILES:
            shutil.copy2(forge_bundle / relative, output / relative)
        shutil.copy2(NOTICES, output / NOTICES_RELATIVE)
    shutil.copy2(runtime_build / "oot3d_game_module.so", output / LINUX.native_module)
    plugin = output / "titles/shared-execution" / LINUX.title_module
    plugin.parent.mkdir(parents=True)
    shutil.copy2(title, plugin)
    if shader_compiler is not None:
        shutil.copy2(shader_compiler, output / "forge/oot3d_native_pica_aot_compiler")
        for dependency in shader_dependencies:
            shutil.copy2(dependency, output / "forge" / dependency.name)
    create_catalog(reference, output, plugin, source_commit=source_commit,
                   shader_compiler=shader_compiler, shader_dependencies=shader_dependencies)
    atomic_write_json(output / "linux-candidate.json", {
        "format": "triaevum_private_linux_candidate_v1",
        "public_release_qualified": False,
        "source_commit": source_commit,
        "runtime_sha256": sha256_file(output / LINUX.runtime),
        "bootstrap_sha256": sha256_file(output / LINUX.title_module),
        "forge_sha256": sha256_file(output / LINUX.forge),
        "forge_profile": "rust" if rust else "python_pyinstaller",
        "title_sha256": sha256_file(plugin),
        "system_libraries_bundled": False,
    })
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for argument in ("reference", "forge-bundle", "runtime-build", "title", "output"):
        parser.add_argument("--" + argument, type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--shader-compiler", type=Path)
    parser.add_argument("--shader-dependency", type=Path, action="append", default=[])
    args = parser.parse_args()
    print(stage(args.reference, args.forge_bundle, args.runtime_build, args.title,
                args.output, args.source_commit, shader_compiler=args.shader_compiler,
                shader_dependencies=args.shader_dependency))


if __name__ == "__main__":
    main()
