"""Verified private SDK extraction with publication only after complete coverage."""

import tempfile
from pathlib import Path

try:
    from .cabinet_extract import extract_cabinet
    from .common import sha256_file
    from .publish_directory import publish_directory
    from .msi_metadata import read_cabinets, read_table, file_layout
except ImportError:
    from cabinet_extract import extract_cabinet
    from common import sha256_file
    from publish_directory import publish_directory
    from msi_metadata import read_cabinets, read_table, file_layout


def extract_msi_package(msi: Path, expected_sha256: str, cabinets: dict, output: Path) -> dict:
    if msi.is_symlink() or sha256_file(msi) != expected_sha256:
        raise ValueError("MSI identity mismatch")
    output = output.resolve()
    if output.exists():
        raise ValueError("SDK extraction must use a new output directory")
    layout = file_layout(*(read_table(msi, table) for table in ("Directory", "Component", "File")))
    names = read_cabinets(msi)
    if not layout or not names or any(name.startswith("#") for name in names):
        raise ValueError("SDK MSI needs a nonempty external-cabinet layout")
    selected = []
    for name in names:
        if name not in cabinets:
            raise ValueError("A required verified SDK cabinet is unavailable: " + name)
        path, digest = cabinets[name]
        selected.append((Path(path), digest))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".sdk-extract-", dir=output.parent) as temporary:
        staging = Path(temporary).resolve()
        if not staging.is_relative_to(output.parent):
            raise ValueError("SDK staging escaped the intended parent")
        assembled = staging / "assembled"
        assembled.mkdir()
        extracted = {}
        for index, (cabinet, digest) in enumerate(selected):
            cabinet_root = staging / str(index)
            files = extract_cabinet(cabinet, cabinet_root, layout, digest)
            if extracted.keys() & files.keys():
                raise ValueError("Duplicate SDK file in multiple cabinets")
            for descriptor in files.values():
                target = assembled / descriptor["path"]
                target.parent.mkdir(parents=True, exist_ok=True)
                (cabinet_root / descriptor["path"]).replace(target)
            extracted.update(files)
        if extracted.keys() != layout.keys():
            raise ValueError("SDK extraction is incomplete; no output was published")
        publish_directory(assembled, output)
    return {"files": extracted, "msi_sha256": expected_sha256,
            "bytes": sum(item["bytes"] for item in extracted.values())}
