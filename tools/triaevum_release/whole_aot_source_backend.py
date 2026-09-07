"""Prepare the verified structural whole-AOT IR for a private Forge title.

This is the release-facing front-end boundary.  It deliberately consumes
explicit, hash-checked source images instead of the development workspace's
``operational_inputs.json`` and does not emit or compile generated C++.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Sequence

try:
    from .bundle_paths import distribution_path, distribution_root
    from .common import atomic_write_json, load_json_object, sha256_file
except ImportError:
    from bundle_paths import distribution_path, distribution_root
    from common import atomic_write_json, load_json_object, sha256_file


REPO_ROOT = distribution_root()
ROOT = distribution_path("tools/triaevum_release")
A32_ROOT = distribution_path("tools/oot3d/native_a32_runtime")
CACHE_FORMAT = "triaevum_whole_aot_ir_cache_v1"


class WholeAotSourceError(ValueError):
    pass


@dataclass(frozen=True)
class SourceImage:
    path: Path
    bytes: int
    sha256: str


@dataclass(frozen=True)
class FrontendAudit:
    ok: bool
    summary: dict[str, object]
    errors: tuple[str, ...]


@dataclass(frozen=True)
class WholeAotFrontend:
    inventory: Path
    supplemental_entries: Path
    boundary_audit: Path
    product_manifest: Path
    selection: Path
    default_base: int
    identity_files: tuple[tuple[str, Path], ...]
    add_local_entry_intervals: Callable[[Path, Path, Path, int], Path]
    extract_program: Callable[[Sequence[str] | None], int]
    audit_product: Callable[..., Any]


def _translator_identity_files() -> tuple[tuple[str, Path], ...]:
    candidates = {
        A32_ROOT / "generate_aot.py",
        A32_ROOT / "whole_aot_program.py",
        A32_ROOT / "audit_whole_aot_product.py",
        A32_ROOT / "runtime_discovered_entries.csv",
        A32_ROOT / "whole_aot_product_manifest.json",
        A32_ROOT / "whole_aot_functions.json",
        A32_ROOT / "upstream/analysis/codebin_function_inventory.csv",
        A32_ROOT
        / "upstream/analysis/codebin_callable_boundary_residue_audit_166.csv",
        Path(__file__).resolve(),
    }
    candidates.update((A32_ROOT / "upstream/src/oot3d_pack").glob("*.py"))
    return tuple(
        (
            path.resolve().relative_to(REPO_ROOT.resolve()).as_posix(),
            path.resolve(),
        )
        for path in sorted(candidates, key=lambda item: item.as_posix())
    )


def load_default_frontend() -> WholeAotFrontend:
    if str(A32_ROOT) not in sys.path:
        sys.path.insert(0, str(A32_ROOT))
    from audit_whole_aot_product import audit_product
    from generate_aot import (
        SUPPLEMENTAL_ENTRIES,
        UPSTREAM,
        add_local_entry_intervals,
    )
    from whole_aot_program import DEFAULT_BASE, main as extract_program

    return WholeAotFrontend(
        inventory=UPSTREAM / "analysis/codebin_function_inventory.csv",
        supplemental_entries=SUPPLEMENTAL_ENTRIES,
        boundary_audit=(
            UPSTREAM
            / "analysis/codebin_callable_boundary_residue_audit_166.csv"
        ),
        product_manifest=A32_ROOT / "whole_aot_product_manifest.json",
        selection=A32_ROOT / "whole_aot_functions.json",
        default_base=DEFAULT_BASE,
        identity_files=_translator_identity_files(),
        add_local_entry_intervals=add_local_entry_intervals,
        extract_program=extract_program,
        audit_product=audit_product,
    )


def _validate_source(label: str, source: SourceImage) -> SourceImage:
    path = source.path.expanduser().resolve()
    if not path.is_file() or path.is_symlink():
        raise WholeAotSourceError(f"{label} is not a regular file: {path}")
    if path.stat().st_size != source.bytes:
        raise WholeAotSourceError(f"{label} changed after Forge preparation")
    digest = sha256_file(path)
    if digest != source.sha256.lower():
        raise WholeAotSourceError(f"{label} changed after Forge preparation")
    return SourceImage(path, source.bytes, digest)


def _hash_identity(
    digest: Any, role: str, *, file_hash: str, file_bytes: int
) -> None:
    digest.update(role.encode("utf-8"))
    digest.update(b"\0")
    digest.update(str(file_bytes).encode("ascii"))
    digest.update(b"\0")
    digest.update(bytes.fromhex(file_hash))


def whole_aot_ir_cache_key(
    recipe: str,
    code: SourceImage,
    exheader: SourceImage,
    frontend: WholeAotFrontend,
) -> str:
    digest = hashlib.sha256()
    digest.update(CACHE_FORMAT.encode("ascii"))
    digest.update(b"\0")
    digest.update(recipe.encode("utf-8"))
    digest.update(b"\0")
    _hash_identity(
        digest,
        "source/code.bin",
        file_hash=code.sha256.lower(),
        file_bytes=code.bytes,
    )
    _hash_identity(
        digest,
        "source/exheader.bin",
        file_hash=exheader.sha256.lower(),
        file_bytes=exheader.bytes,
    )
    for role, path in sorted(frontend.identity_files):
        resolved = path.resolve()
        if not resolved.is_file() or resolved.is_symlink():
            raise WholeAotSourceError(
                f"whole-AOT frontend identity file is invalid: {resolved}"
            )
        _hash_identity(
            digest,
            f"translator/{role}",
            file_hash=sha256_file(resolved),
            file_bytes=resolved.stat().st_size,
        )
    return digest.hexdigest()


def _read_exheader(exheader: Path) -> tuple[int, int]:
    contents = exheader.read_bytes()
    if len(contents) < 0x18:
        raise WholeAotSourceError("verified ExHeader is truncated")
    entrypoint = int.from_bytes(contents[0x10:0x14], "little")
    executable_size = int.from_bytes(contents[0x14:0x18], "little") * 0x1000
    if executable_size == 0:
        raise WholeAotSourceError("verified ExHeader has an empty text segment")
    return entrypoint, executable_size


def _audit_program(
    frontend: WholeAotFrontend,
    program: Path,
    code: Path,
    exheader: Path,
) -> FrontendAudit:
    result = frontend.audit_product(
        frontend.product_manifest,
        program,
        code_path=code,
        exheader_path=exheader,
        selection_path=frontend.selection,
    )
    audit = FrontendAudit(
        ok=bool(result.ok),
        summary=dict(result.summary),
        errors=tuple(str(error) for error in result.errors),
    )
    if not audit.ok:
        raise WholeAotSourceError(
            "whole-AOT product audit failed: " + "; ".join(audit.errors)
        )
    return audit


def _artifact_result(
    status: str, artifact: Path, metadata: dict[str, Any]
) -> dict[str, Any]:
    output = artifact / "aot_program.json"
    return {
        "status": status,
        "cache_key": str(metadata["cache_key"]),
        "directory": str(artifact),
        "program": str(output),
        "program_sha256": str(metadata["output"]["sha256"]),
        "translator_identity_sha256": str(
            metadata["translator_identity_sha256"]
        ),
        "audit": metadata["audit"],
    }


def _validate_cached_artifact(
    artifact: Path, expected_key: str
) -> dict[str, Any] | None:
    metadata_path = artifact / "source-backend.json"
    if not artifact.exists():
        return None
    if (
        not artifact.is_dir()
        or artifact.is_symlink()
        or not metadata_path.is_file()
    ):
        raise WholeAotSourceError(
            f"whole-AOT IR cache entry is incomplete: {artifact}"
        )
    try:
        metadata = load_json_object(metadata_path)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        raise WholeAotSourceError(
            f"whole-AOT IR cache metadata is invalid: {exc}"
        ) from exc
    if (
        metadata.get("format") != CACHE_FORMAT
        or metadata.get("cache_key") != expected_key
    ):
        raise WholeAotSourceError(
            f"whole-AOT IR cache identity mismatch: {artifact}"
        )
    output = metadata.get("output")
    if not isinstance(output, dict):
        raise WholeAotSourceError(
            f"whole-AOT IR cache output is malformed: {artifact}"
        )
    program = artifact / "aot_program.json"
    if not program.is_file() or program.is_symlink():
        raise WholeAotSourceError(f"whole-AOT IR cache program is missing: {program}")
    stat = program.stat()
    if stat.st_size != output.get("bytes"):
        raise WholeAotSourceError(f"whole-AOT IR cache program changed: {program}")
    if stat.st_mtime_ns != output.get("mtime_ns"):
        if sha256_file(program) != output.get("sha256"):
            raise WholeAotSourceError(f"whole-AOT IR cache program changed: {program}")
        output["mtime_ns"] = stat.st_mtime_ns
        atomic_write_json(metadata_path, metadata)
    return metadata


def _acquire_lock(path: Path) -> int:
    try:
        descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError as exc:
        raise WholeAotSourceError(
            f"whole-AOT IR preparation is already active: {path}"
        ) from exc
    os.write(descriptor, f"pid={os.getpid()}\n".encode("ascii"))
    return descriptor


def prepare_whole_aot_ir(
    *,
    recipe: str,
    code: SourceImage,
    exheader: SourceImage,
    cache_root: Path,
    frontend: WholeAotFrontend | None = None,
    seed_program: Path | None = None,
) -> dict[str, Any]:
    frontend = frontend or load_default_frontend()
    code = _validate_source("code.bin", code)
    exheader = _validate_source("ExHeader", exheader)
    entrypoint, executable_size = _read_exheader(exheader.path)
    key = whole_aot_ir_cache_key(recipe, code, exheader, frontend)
    cache_root = cache_root.expanduser().resolve()
    cache_root.mkdir(parents=True, exist_ok=True)
    if cache_root.is_symlink():
        raise WholeAotSourceError(
            f"whole-AOT IR cache root is a symlink: {cache_root}"
        )
    artifact = cache_root / key
    cached = _validate_cached_artifact(artifact, key)
    if cached is not None:
        return _artifact_result("reused", artifact, cached)

    lock_path = cache_root / f".{key}.lock"
    lock_descriptor = _acquire_lock(lock_path)
    try:
        cached = _validate_cached_artifact(artifact, key)
        if cached is not None:
            return _artifact_result("reused", artifact, cached)
        with tempfile.TemporaryDirectory(
            prefix=f".{key[:16]}.", dir=cache_root
        ) as temporary:
            staging = Path(temporary)
            inventory = staging / "inventory.csv"
            program = staging / "aot_program.json"
            frontend.add_local_entry_intervals(
                frontend.inventory,
                frontend.supplemental_entries,
                inventory,
                entrypoint,
            )
            if seed_program is not None:
                seed = seed_program.expanduser().resolve()
                if not seed.is_file() or seed.is_symlink():
                    raise WholeAotSourceError(
                        f"whole-AOT seed program is invalid: {seed}"
                    )
                _audit_program(frontend, seed, code.path, exheader.path)
                shutil.copyfile(seed, program)
            else:
                result = frontend.extract_program(
                    [
                        "--code",
                        str(code.path),
                        "--inventory",
                        str(inventory),
                        "--boundary-audit",
                        str(frontend.boundary_audit),
                        "--base",
                        hex(frontend.default_base),
                        "--executable-size",
                        hex(executable_size),
                        "--output",
                        str(program),
                        "--force",
                    ]
                )
                if result != 0:
                    raise WholeAotSourceError(
                        f"whole-AOT structural extraction failed with status {result}"
                    )
            audit = _audit_program(frontend, program, code.path, exheader.path)
            program_stat = program.stat()
            translator_digest = hashlib.sha256()
            for role, path in sorted(frontend.identity_files):
                _hash_identity(
                    translator_digest,
                    role,
                    file_hash=sha256_file(path),
                    file_bytes=path.stat().st_size,
                )
            metadata = {
                "format": CACHE_FORMAT,
                "cache_key": key,
                "recipe": recipe,
                "source": {
                    "code": {"bytes": code.bytes, "sha256": code.sha256},
                    "exheader": {
                        "bytes": exheader.bytes,
                        "sha256": exheader.sha256,
                    },
                    "entrypoint": entrypoint,
                    "executable_size": executable_size,
                },
                "translator_identity_sha256": translator_digest.hexdigest(),
                "output": {
                    "path": "aot_program.json",
                    "bytes": program_stat.st_size,
                    "mtime_ns": program_stat.st_mtime_ns,
                    "sha256": sha256_file(program),
                },
                "audit": audit.summary,
                "seeded": seed_program is not None,
            }
            atomic_write_json(staging / "source-backend.json", metadata)
            staging.replace(artifact)
        return _artifact_result("prepared", artifact, metadata)
    finally:
        os.close(lock_descriptor)
        lock_path.unlink(missing_ok=True)
