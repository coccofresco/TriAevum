"""Assemble private SDK component trees without installing or executing packages."""

import shutil
import tempfile
from pathlib import Path

try:
    from .common import atomic_write_json, normalize_relative_path, sha256_file
    from .windows_sysroot import load_sysroot
except ImportError:
    from common import atomic_write_json, normalize_relative_path, sha256_file
    from windows_sysroot import load_sysroot


def assemble(trees: list[tuple[Path, str]], *, sdk_version: str, msvc_version: str,
             clang_version: str, output: Path) -> dict:
    output = output.resolve()
    if output.exists():
        raise ValueError("Sysroot assembly requires a new output directory")
    sources = []
    for root, prefix in trees:
        if root.is_symlink() or not root.is_dir():
            raise ValueError("Invalid sysroot component tree: " + str(root))
        prefix = normalize_relative_path(prefix)
        for source in sorted(root.rglob("*")):
            if source.is_symlink():
                raise ValueError("Sysroot components must not contain symbolic links")
            if source.is_file():
                relative = normalize_relative_path(prefix + "/" + source.relative_to(root).as_posix())
                sources.append((source, relative))
    if not sources:
        raise ValueError("Sysroot component trees are empty")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".sysroot-assembly-", dir=output.parent) as temporary:
        staging = Path(temporary).resolve()
        if not staging.is_relative_to(output.parent):
            raise ValueError("Sysroot staging escaped its parent")
        tree = staging / "tree"
        tree.mkdir()
        files, names = {}, {}
        for source, relative in sources:
            descriptor = {"bytes": source.stat().st_size, "sha256": sha256_file(source)}
            previous = names.get(relative.casefold())
            if previous:
                if files[previous] != descriptor:
                    raise ValueError("Conflicting sysroot component file: " + relative)
                continue
            target = tree / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            files[relative] = descriptor
            names[relative.casefold()] = relative
        manifest = {"format": "triaevum_windows_sysroot_v1", "sdk_version": sdk_version,
                    "msvc_version": msvc_version, "clang_version": clang_version,
                    "distribution": "private_local_not_redistribution_approved", "files": files}
        atomic_write_json(tree / "sysroot.json", manifest)
        verified = load_sysroot(tree)
        tree.replace(output)
    return {"root": str(output), "identity": verified.identity, "files": len(files),
            "bytes": sum(item["bytes"] for item in files.values())}
