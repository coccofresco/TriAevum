"""Archive an audited public package: portable ZIP on Windows, tar.gz on Linux."""

from __future__ import annotations

import argparse
import gzip
import json
from pathlib import Path
import shutil
import stat
import tarfile
import zipfile

try:
    from .audit_release import DEFAULT_POLICY, audit_release
    from .common import load_json_object, normalize_relative_path, sha256_file
    from .release_platform import WINDOWS, for_target
except ImportError:
    from audit_release import DEFAULT_POLICY, audit_release
    from common import load_json_object, normalize_relative_path, sha256_file
    from release_platform import WINDOWS, for_target


def create_release_archive(root: Path, output: Path, *, policy_path: Path = DEFAULT_POLICY) -> dict:
    root = root.resolve()
    output = output.absolute()
    if output.resolve().is_relative_to(root):
        raise ValueError("release archive must be outside the package")
    if output.exists() or output.is_symlink():
        raise ValueError(f"release archive already exists: {output}")
    audit = audit_release(root, policy_path=policy_path)
    if not audit.ok:
        raise ValueError("release archive audit failed:\n" + "\n".join(audit.errors))
    manifest = load_json_object(root / "release-manifest.json")
    platform = for_target(manifest["release"].get("target", WINDOWS.target))
    if not output.name.endswith(platform.archive_suffix):
        raise ValueError(f"{platform.target} requires a {platform.archive_suffix} archive")
    prefix = normalize_relative_path(root.name)
    entries = {item["path"]: item for item in manifest["files"]}
    entries["release-manifest.json"] = {"role": "manifest"}
    # Never recursively archive an installation: only the audited public inventory.
    paths = sorted(entries)
    directories = {prefix}
    for relative in paths:
        parent = Path(prefix, relative).parent
        directories.update(p.as_posix() for p in (parent, *parent.parents) if p != Path("."))

    def mode(relative: str) -> int:
        return 0o755 if entries[relative]["role"] in ("runtime_executable", "forge_executable") else 0o644

    output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation protects existing releases. Failed writes never leave an archive.
    with output.open("xb") as stream:
        try:
            if platform == WINDOWS:
                with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
                    for directory in sorted(directories):
                        info = zipfile.ZipInfo(directory + "/")
                        info.create_system = 3
                        info.external_attr = (stat.S_IFDIR | 0o755) << 16 | 0x10
                        archive.writestr(info, b"")
                    for relative in paths:
                        info = zipfile.ZipInfo(prefix + "/" + relative)
                        info.create_system = 3
                        info.compress_type = zipfile.ZIP_DEFLATED
                        info.external_attr = (stat.S_IFREG | mode(relative)) << 16
                        with (root / relative).open("rb") as source, archive.open(info, "w", force_zip64=True) as target:
                            shutil.copyfileobj(source, target, 1024 * 1024)
            else:
                with gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=0, compresslevel=6) as compressed:
                    with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as archive:
                        for directory in sorted(directories):
                            info = tarfile.TarInfo(directory)
                            info.type = tarfile.DIRTYPE
                            info.mode = 0o755
                            archive.addfile(info)
                        for relative in paths:
                            info = tarfile.TarInfo(prefix + "/" + relative)
                            info.mode = mode(relative)
                            info.size = (root / relative).stat().st_size
                            with (root / relative).open("rb") as source:
                                archive.addfile(info, source)
        except BaseException:
            stream.close()
            output.unlink()
            raise
    return {"archive": str(output), "target": platform.target,
            "bytes": output.stat().st_size, "sha256": sha256_file(output),
            "qualification": manifest["release"].get("qualification", "unspecified")}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(create_release_archive(args.package, args.output), indent=2))
    except (OSError, ValueError) as exc:
        parser.exit(1, f"TriAevum archive failed: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
