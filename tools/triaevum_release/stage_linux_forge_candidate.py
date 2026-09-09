"""Stage a private Linux qualification candidate, not an audited public release."""

import argparse
from pathlib import Path
import shutil

try:
    from .common import atomic_write_json, sha256_file
    from .linux_precompiled_catalog import create_catalog, require_elf
    from .release_platform import LINUX, host_platform
except ImportError:
    from common import atomic_write_json, sha256_file
    from linux_precompiled_catalog import create_catalog, require_elf
    from release_platform import LINUX, host_platform


def stage(reference: Path, forge_bundle: Path, runtime_build: Path,
          title: Path, output: Path, source_commit: str):
    if host_platform() != LINUX:
        raise ValueError("Stage and qualify Linux candidates on Linux")
    if output.exists():
        raise ValueError("Candidate output must not exist; preserve existing installations")
    for path, shared in ((runtime_build / LINUX.runtime, False),
                         (runtime_build / LINUX.title_module, True),
                         (runtime_build / "oot3d_game_module.so", True), (title, True),
                         (forge_bundle / LINUX.forge, False)):
        require_elf(path, shared=shared)
    if sha256_file(title) == sha256_file(runtime_build / LINUX.title_module):
        raise ValueError("The game title cannot be the empty runtime bootstrap library")
    output.mkdir(parents=True)
    # Preserve PyInstaller's internal symlinks; do not import data/ or any
    # Windows executable, title binary or release manifest from the reference.
    shutil.copy2(forge_bundle / LINUX.forge, output / LINUX.forge)
    shutil.copytree(forge_bundle / "_internal", output / "_internal", symlinks=True)
    for name in ("recipes", "resources", "source/titles", "LICENSES"):
        shutil.copytree(reference / name, output / name)
    for name in ("LICENSE", "LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(reference / name, output / name)
    for name in (LINUX.runtime, LINUX.title_module):
        shutil.copy2(runtime_build / name, output / name)
    (output / "forge").mkdir()
    shutil.copy2(runtime_build / "oot3d_game_module.so", output / LINUX.native_module)
    plugin = output / "titles/shared-execution" / LINUX.title_module
    plugin.parent.mkdir(parents=True)
    shutil.copy2(title, plugin)
    create_catalog(reference, output, plugin, source_commit=source_commit)
    atomic_write_json(output / "linux-candidate.json", {
        "format": "triaevum_private_linux_candidate_v1",
        "public_release_qualified": False,
        "source_commit": source_commit,
        "runtime_sha256": sha256_file(output / LINUX.runtime),
        "bootstrap_sha256": sha256_file(output / LINUX.title_module),
        "forge_sha256": sha256_file(output / LINUX.forge),
        "title_sha256": sha256_file(plugin),
        "system_libraries_bundled": False,
    })
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for argument in ("reference", "forge-bundle", "runtime-build", "title", "output"):
        parser.add_argument("--" + argument, type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    args = parser.parse_args()
    print(stage(args.reference, args.forge_bundle, args.runtime_build, args.title,
                args.output, args.source_commit))


if __name__ == "__main__":
    main()
