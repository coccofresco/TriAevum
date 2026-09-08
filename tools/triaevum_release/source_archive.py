"""Create deterministic, policy-filtered TriAevum corresponding source."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Sequence

try:
    from .common import load_json_object, normalize_relative_path, sha256_file
except ImportError:
    from common import load_json_object, normalize_relative_path, sha256_file


ROOT = Path(__file__).resolve().parent
DEFAULT_POLICY = ROOT / "release_policy.json"
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


@dataclass(frozen=True)
class SourceFile:
    repository: Path
    repository_prefix: str
    relative: str
    mode: str

    @property
    def archive_path(self) -> str:
        return normalize_relative_path(
            f"{self.repository_prefix}/{self.relative}"
            if self.repository_prefix
            else self.relative
        )


def _git(repository: Path, *arguments: str, binary: bool = False) -> str | bytes:
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=True,
        capture_output=True,
    )
    return result.stdout if binary else result.stdout.decode("utf-8").strip()


def _tracked(repository: Path, prefix: str = "") -> tuple[list[SourceFile], list[tuple[str, str]]]:
    output = _git(repository, "ls-files", "--stage", "-z", binary=True)
    assert isinstance(output, bytes)
    files: list[SourceFile] = []
    submodules: list[tuple[str, str]] = []
    for raw in output.split(b"\0"):
        if not raw:
            continue
        metadata, encoded_path = raw.split(b"\t", 1)
        mode, object_id, _stage = metadata.decode("ascii").split(" ")
        relative = encoded_path.decode("utf-8")
        if mode == "160000":
            submodules.append((relative, object_id))
        elif mode == "120000":
            raise ValueError(f"symbolic links are not supported in source archive: {relative}")
        elif mode in {"100644", "100755"}:
            files.append(SourceFile(repository, prefix, relative, mode))
        else:
            raise ValueError(f"unsupported Git mode {mode}: {relative}")
    return files, submodules


def _assert_clean_commit(repository: Path, expected: str) -> str:
    actual = str(_git(repository, "rev-parse", "HEAD"))
    if actual != expected:
        raise ValueError(
            f"repository commit mismatch at {repository}: expected {expected}, got {actual}"
        )
    subprocess.run(
        ["git", "-C", str(repository), "diff", "--quiet", "--ignore-submodules=none"],
        check=True,
    )
    subprocess.run(
        ["git", "-C", str(repository), "diff", "--cached", "--quiet"],
        check=True,
    )
    return actual


def collect_source_files(
    repository: Path, source_commit: str
) -> tuple[list[SourceFile], dict[str, str]]:
    repository = repository.resolve()
    commits = {".": _assert_clean_commit(repository, source_commit)}
    files, submodules = _tracked(repository)
    pending = [(repository / path, path, object_id) for path, object_id in submodules]
    while pending:
        child, prefix, expected = pending.pop(0)
        if not child.is_dir():
            raise ValueError(f"required source submodule is not initialized: {child}")
        commits[prefix] = _assert_clean_commit(child, expected)
        child_files, child_submodules = _tracked(child, prefix)
        files.extend(child_files)
        for relative, object_id in child_submodules:
            child_prefix = f"{prefix}/{relative}"
            pending.append((child / relative, child_prefix, object_id))
    return files, commits


def source_path_allowed(relative: str, policy: dict[str, Any]) -> bool:
    path = PurePosixPath(normalize_relative_path(relative))
    lowered = relative.lower()
    # A checkout imported from a source ZIP can still track its old receipt.
    # The root receipt is generated below for this archive, never copied.
    if path.as_posix().lower() == "source_archive_manifest.json":
        return False
    if path.name.lower() in {
        str(item).lower() for item in policy.get("forbidden_basenames", [])
    }:
        return False
    if path.suffix.lower() in {
        str(item).lower() for item in policy.get("forbidden_extensions", [])
    }:
        return False
    if path.suffix.lower() == ".zip":
        return False
    forbidden_components = {
        str(item).lower()
        for item in policy.get("forbidden_path_components", [])
    }
    if any(part.lower() in forbidden_components for part in path.parts):
        return False
    return not any(
        lowered.startswith(str(prefix).lower().rstrip("/") + "/")
        for prefix in policy.get("source_archive_forbidden_prefixes", [])
    )


def _zip_info(relative: str, mode: int = 0o100644) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(relative, ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = mode << 16
    return info


def create_source_archive(
    repository: Path,
    output: Path,
    *,
    source_commit: str,
    policy_path: Path = DEFAULT_POLICY,
) -> dict[str, Any]:
    policy = load_json_object(policy_path)
    if policy.get("format") != "triaevum_public_release_policy_v1":
        raise ValueError("unsupported release policy format")
    files, commits = collect_source_files(repository, source_commit)
    included = sorted(
        (item for item in files if source_path_allowed(item.archive_path, policy)),
        key=lambda item: item.archive_path,
    )
    if not included:
        raise ValueError("source policy selected no files")
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    aggregate = hashlib.sha256()
    total_bytes = 0
    with zipfile.ZipFile(temporary, "w", compresslevel=9) as archive:
        for item in included:
            contents = (item.repository / item.relative).read_bytes()
            aggregate.update(item.archive_path.encode("utf-8"))
            aggregate.update(b"\0")
            aggregate.update(hashlib.sha256(contents).digest())
            total_bytes += len(contents)
            mode = 0o100755 if item.mode == "100755" else 0o100644
            archive.writestr(_zip_info(item.archive_path, mode), contents)
        metadata = {
            "format": "triaevum_corresponding_source_v1",
            "source_commit": source_commit,
            "repositories": commits,
            "files": len(included),
            "uncompressed_bytes": total_bytes,
            "content_identity_sha256": aggregate.hexdigest(),
            "excluded_tracked_files": len(files) - len(included),
        }
        archive.writestr(
            _zip_info("SOURCE_ARCHIVE_MANIFEST.json"),
            (json.dumps(metadata, indent=2, sort_keys=True) + "\n").encode("utf-8"),
        )
    temporary.replace(output)
    return {
        **metadata,
        "archive": str(output),
        "archive_bytes": output.stat().st_size,
        "archive_sha256": sha256_file(output),
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--policy", type=Path, default=DEFAULT_POLICY)
    args = parser.parse_args(argv)
    try:
        result = create_source_archive(
            args.repository,
            args.output,
            source_commit=args.source_commit,
            policy_path=args.policy,
        )
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(json.dumps({"status": "error", "error": str(exc)}, indent=2))
        return 1
    print(json.dumps({"status": "ok", **result}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
