"""Extract the current qualified public release inputs for a macOS build."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import zipfile
from pathlib import Path, PurePosixPath

RELEASE = "2b"
RELEASE_SHA256 = "a30730b98827e316743cdbed25e7d2f5c8343b44eeffe21b5249fa14c1a2246d"


def download(build: Path) -> Path:
    path = build / f"upstream-alpha{RELEASE}-windows.zip"
    if not path.exists():
        temporary = path.with_suffix(".downloading")
        url = ("https://github.com/coccofresco/TriAevum/releases/download/"
               f"v0.6.0-alpha.{RELEASE}/TriAevum-v0.6.0-alpha.{RELEASE}-Windows-x64.zip")
        subprocess.run(["curl", "--fail", "--location", "--retry", "3", url,
                        "--output", str(temporary)], check=True)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != RELEASE_SHA256:
            raise ValueError(f"Release archive checksum mismatch: {RELEASE}")
        temporary.replace(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != RELEASE_SHA256:
        raise ValueError(f"Release archive checksum mismatch: {path}")
    return path


def members(archive: Path):
    with zipfile.ZipFile(archive) as source:
        for name in source.namelist():
            relative = name.removeprefix("Windows/")
            parts = PurePosixPath(relative)
            if parts.is_absolute() or ".." in parts.parts:
                raise ValueError("Invalid archive path")
            if not name.endswith("/"):
                yield relative, source.read(name)


def prepare(build: Path) -> None:
    build = build.resolve()
    build.mkdir(parents=True, exist_ok=True)
    installation = build / "installation"
    reference = build / "qualified-inputs"
    translated = build / "translated-title-alpha2"
    for name, payload in members(download(build)):
        if name.startswith(("recipes/", "resources/", "forge/shader-corpus/", "source/titles/")):
            destination = reference / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(payload)
        if name.startswith(("recipes/", "resources/", "forge/shader-corpus/")):
            if name == "recipes/precompiled-titles.json":
                continue
            destination = installation / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(payload)
        elif name.endswith("-translated.zip") and name.startswith("source/titles/"):
            archive = build / "alpha2-translated-title.zip"
            archive.write_bytes(payload)
            for source_name, source_data in members(archive):
                # The title CMake project verifies the source manifest hashes.
                if "/" in source_name or not source_name.endswith((".cpp", ".h", ".json", ".md")):
                    raise ValueError("Unexpected translated title entry")
                translated.mkdir(exist_ok=True)
                (translated / source_name).write_bytes(source_data)
    root = Path(__file__).resolve().parents[2]
    shutil.copytree(root / "runtime/three_ds_recomp/src/fast/shaders",
                    installation / "resources/shaders", dirs_exist_ok=True)
    print(f"Prepared qualified alpha {RELEASE} title, shaders and recipes in {build}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    prepare(parser.parse_args().build)
