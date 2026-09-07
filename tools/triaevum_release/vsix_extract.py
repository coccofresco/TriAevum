"""Extract a verified VSIX as data; never register extensions or run binaries."""

import hashlib
import ntpath
import stat
import tempfile
from pathlib import Path, PureWindowsPath
from zipfile import ZipFile

try:
    from .common import normalize_relative_path, sha256_file
except ImportError:
    from common import normalize_relative_path, sha256_file


def extract_vsix(archive: Path, expected_sha256: str, output: Path,
                 *, max_bytes: int = 2 * 1024**3, max_files: int = 100_000) -> dict:
    if archive.is_symlink() or sha256_file(archive) != expected_sha256:
        raise ValueError("VSIX identity mismatch")
    output = output.resolve()
    if output.exists():
        raise ValueError("VSIX extraction requires a new output directory")
    with ZipFile(archive) as package:
        members, names, total = [], set(), 0
        for item in package.infolist():
            name = normalize_relative_path(item.filename)
            for part in name.split("/"):
                reserved = ntpath.isreserved(part) if hasattr(ntpath, "isreserved") else PureWindowsPath(part).is_reserved()
                if reserved or part.endswith((".", " ")) or any(ord(char) < 32 for char in part):
                    raise ValueError("Unsafe Windows VSIX path")
            if stat.S_ISLNK(item.external_attr >> 16) or item.flag_bits & 1:
                raise ValueError("VSIX links/encrypted members are unsupported")
            if name.casefold() in names:
                raise ValueError("Duplicate VSIX path")
            names.add(name.casefold())
            if item.is_dir():
                continue
            total += item.file_size
            members.append((item, name))
            if total > max_bytes or len(members) > max_files:
                raise ValueError("VSIX exceeds extraction limits")
        if not members:
            raise ValueError("VSIX has no files")
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".vsix-extract-", dir=output.parent) as temporary:
            staging = Path(temporary).resolve()
            if not staging.is_relative_to(output.parent):
                raise ValueError("VSIX staging escaped its parent")
            tree = staging / "tree"
            tree.mkdir()
            files = {}
            for item, name in members:
                target = tree / name
                if not target.resolve().is_relative_to(tree):
                    raise ValueError("VSIX target escaped staging")
                target.parent.mkdir(parents=True, exist_ok=True)
                digest, received = hashlib.sha256(), 0
                with package.open(item) as source, target.open("xb") as destination:
                    while chunk := source.read(1024 * 1024):
                        received += len(chunk)
                        if received > item.file_size:
                            raise ValueError("VSIX member exceeded declared size")
                        digest.update(chunk)
                        destination.write(chunk)
                if received != item.file_size:
                    raise ValueError("VSIX member size mismatch")
                files[name] = {"bytes": received, "sha256": digest.hexdigest()}
            tree.replace(output)
    return {"archive_sha256": expected_sha256, "files": files, "bytes": total}
