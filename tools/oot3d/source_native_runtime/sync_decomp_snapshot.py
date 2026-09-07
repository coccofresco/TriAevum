#!/usr/bin/env python3
"""Materialize a revision-pinned OOT3D source snapshot for the host runtime."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile


ARCHIVE_PATHS = (
    "CMakeLists.txt",
    "config/source_lanes.json",
    "config/version_eur.json",
    "include",
    "metadata/maturity_summary.json",
    "src",
)


def remove_generated_tree(path: Path) -> None:
    def make_writable_and_retry(function, target, _error) -> None:
        os.chmod(target, 0o700)
        function(target)

    shutil.rmtree(path, onerror=make_writable_and_retry)


def run_git(repo: Path, *arguments: str, text: bool = True) -> str | bytes:
    result = subprocess.run(
        ["git", "-C", str(repo), *arguments],
        check=True,
        capture_output=True,
        text=text,
    )
    return result.stdout


def load_json_from_revision(repo: Path, revision: str, relative_path: str) -> dict:
    return json.loads(run_git(repo, "show", f"{revision}:{relative_path}"))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_cmake_source_list(path: Path, sources: list[str]) -> None:
    lines = ["set(OOT3D_DECOMP_CANONICAL_SOURCES"]
    lines.extend(f'    "${{OOT3D_SOURCE_SNAPSHOT_ROOT}}/{source}"' for source in sources)
    lines.append(")")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def write_snapshot_profile(path: Path, revision: str, source_count: int) -> None:
    path.write_text(
        "#pragma once\n\n"
        "#include <cstddef>\n"
        "#include <string_view>\n\n"
        "namespace Oot3dSourceSnapshotGenerated {\n"
        f'inline constexpr std::string_view kRevision = "{revision}";\n'
        f"inline constexpr std::size_t kCanonicalSourceCount = {source_count};\n"
        "} // namespace Oot3dSourceSnapshotGenerated\n",
        encoding="utf-8",
        newline="\n",
    )


def preserve_unchanged_timestamps(previous: Path, replacement: Path) -> None:
    if not previous.exists():
        return
    for candidate in replacement.rglob("*"):
        if not candidate.is_file():
            continue
        old = previous / candidate.relative_to(replacement)
        if (old.is_file() and old.stat().st_size == candidate.stat().st_size
                and sha256_file(old) == sha256_file(candidate)):
            shutil.copystat(old, candidate)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--decomp-root", required=True, type=Path)
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--replace", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    decomp_root = args.decomp_root.resolve()
    profile_path = args.profile.resolve()
    output_root = args.output_root.resolve()
    profile = json.loads(profile_path.read_text(encoding="utf-8"))
    expected_revision = profile["decomp_revision"]

    if not (decomp_root / ".git").exists():
        raise RuntimeError(f"decomp root is not a Git repository: {decomp_root}")
    actual_revision = run_git(
        decomp_root, "rev-parse", f"{expected_revision}^{{commit}}"
    ).strip()
    if actual_revision != expected_revision:
        raise RuntimeError(
            f"decomp revision mismatch: expected {expected_revision}, got {actual_revision}"
        )

    lanes = load_json_from_revision(decomp_root, actual_revision, "config/source_lanes.json")
    lane_name = profile["canonical_source_lane"]
    sources = lanes["lanes"][lane_name]["source_files"]
    if len(sources) != profile["canonical_source_count"] or len(set(sources)) != len(sources):
        raise RuntimeError("canonical source lane count or uniqueness does not match the profile")

    version = load_json_from_revision(decomp_root, actual_revision, "config/version_eur.json")
    if version["code_bin"]["sha256"] != profile["code_bin_sha256"]:
        raise RuntimeError("decomp target code.bin hash does not match the consumer profile")

    output_root.parent.mkdir(parents=True, exist_ok=True)
    staging = output_root.with_name(f"{output_root.name}.staging-{os.getpid()}")
    archive = output_root.with_name(f"{output_root.name}.staging-{os.getpid()}.zip")
    if staging.exists():
        remove_generated_tree(staging)
    if archive.exists():
        archive.unlink()

    try:
        subprocess.run(
            [
                "git",
                "-C",
                str(decomp_root),
                "archive",
                "--format=zip",
                f"--output={archive}",
                actual_revision,
                "--",
                *ARCHIVE_PATHS,
            ],
            check=True,
        )
        with zipfile.ZipFile(archive) as package:
            package.extractall(staging)

        canonical = set(sources)
        for source_path in (staging / "src").rglob("*.c"):
            relative = source_path.relative_to(staging).as_posix()
            if relative not in canonical:
                source_path.unlink()

        source_records = []
        for relative in sources:
            source_path = staging / relative
            if not source_path.is_file():
                raise RuntimeError(f"canonical source missing from snapshot: {relative}")
            source_records.append(
                {"path": relative, "sha256": sha256_file(source_path)}
            )

        manifest = {
            "schema_version": 1,
            "profile_id": profile["profile_id"],
            "decomp_revision": actual_revision,
            "profile_sha256": sha256_file(profile_path),
            "canonical_source_lane": lane_name,
            "canonical_sources": source_records,
            "target_entry_count": profile["target_entry_count"],
            "code_bin_sha256": profile["code_bin_sha256"],
        }
        (staging / "source_snapshot_manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n"
        )
        write_cmake_source_list(staging / "canonical_sources.cmake", sources)
        write_snapshot_profile(
            staging / "oot3d_source_snapshot_profile.h",
            actual_revision,
            len(sources),
        )

        if output_root.exists():
            if not args.replace:
                raise RuntimeError(
                    f"snapshot already exists (pass --replace): {output_root}"
                )
            preserve_unchanged_timestamps(output_root, staging)
            remove_generated_tree(output_root)
        staging.replace(output_root)
    finally:
        if archive.exists():
            archive.unlink()
        if staging.exists():
            remove_generated_tree(staging)

    print(output_root / "source_snapshot_manifest.json")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"source snapshot failed: {error}", file=sys.stderr)
        raise SystemExit(1)
