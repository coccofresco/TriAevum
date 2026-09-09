"""Build an incremental native-object cache for verified whole-AOT sources.

The current producer is the generated-C++ development fallback.  Object and
archive identities are intentionally independent of that producer so a direct
structural-IR emitter can publish the same contract later.
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Sequence

try:
    from . import platforms
    from .bounded_build import run_bounded
    from .cache_lock import acquire_cache_lock, release_cache_lock
    from .windows_sysroot import WindowsSysroot, compiler_sysroot, build_environment
    from .common import (
        atomic_write_json,
        load_json_object,
        normalize_relative_path,
        sha256_file,
    )
except ImportError:
    import platforms
    from bounded_build import run_bounded
    from cache_lock import acquire_cache_lock, release_cache_lock
    from windows_sysroot import WindowsSysroot, compiler_sysroot, build_environment
    from common import (
        atomic_write_json,
        load_json_object,
        normalize_relative_path,
        sha256_file,
    )


FORMAT = "triaevum_whole_aot_object_cache_v1"
GENERATED_FORMAT = "oot3d_whole_aot_cpp_v1"
PROFILE = platforms.WINDOWS.profile


class WholeAotObjectError(ValueError):
    pass


@dataclass(frozen=True)
class NativeToolchain:
    compiler: Path
    archiver: Path
    target_triple: str = platforms.host().triple
    profile: str = platforms.host().profile
    sysroot: Path | None = None

    @property
    def platform(self) -> platforms.Platform:
        return platforms.for_triple(self.target_triple)


@dataclass(frozen=True)
class DependencyFile:
    role: str
    path: Path


@dataclass(frozen=True)
class GeneratedSource:
    name: str
    path: Path
    sha256: str


CommandRunner = Callable[[Sequence[str], Path], subprocess.CompletedProcess[str]]


def _default_runner(
    arguments: Sequence[str], working_directory: Path
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(arguments),
        cwd=working_directory,
        check=False,
        capture_output=True,
        text=True,
        env=build_environment() if "/vctoolsdir" in arguments else None,
    )


def _hash_record(digest: Any, role: str, size: int, file_hash: str) -> None:
    digest.update(role.encode("utf-8"))
    digest.update(b"\0")
    digest.update(str(size).encode("ascii"))
    digest.update(b"\0")
    digest.update(bytes.fromhex(file_hash))


def _validate_tool(path: Path, label: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file() or resolved.is_symlink():
        raise WholeAotObjectError(f"{label} is not a regular file: {resolved}")
    return resolved


def _toolchain_identity(toolchain: NativeToolchain, sysroot: WindowsSysroot | None = None) -> tuple[str, NativeToolchain]:
    compiler = _validate_tool(toolchain.compiler, "native compiler")
    archiver = _validate_tool(toolchain.archiver, "native archiver")
    digest = hashlib.sha256()
    digest.update(FORMAT.encode("ascii"))
    digest.update(b"\0toolchain\0")
    digest.update(toolchain.target_triple.encode("ascii"))
    digest.update(b"\0")
    digest.update(toolchain.profile.encode("ascii"))
    digest.update((sysroot.identity if sysroot else "host-discovery").encode("ascii"))
    digest.update(b"\0")
    for role, path in (("compiler", compiler), ("archiver", archiver)):
        _hash_record(digest, role, path.stat().st_size, sha256_file(path))
    return (
        digest.hexdigest(),
        NativeToolchain(
            compiler=compiler,
            archiver=archiver,
            target_triple=toolchain.target_triple,
            profile=toolchain.profile,
            sysroot=toolchain.sysroot,
        ),
    )


def _dependency_identity(
    dependencies: Sequence[DependencyFile],
) -> tuple[str, tuple[dict[str, object], ...]]:
    digest = hashlib.sha256()
    records: list[dict[str, object]] = []
    seen: set[str] = set()
    for dependency in sorted(dependencies, key=lambda item: item.role):
        role = normalize_relative_path(dependency.role)
        if role in seen:
            raise WholeAotObjectError(f"duplicate native dependency role: {role}")
        seen.add(role)
        path = dependency.path.expanduser().resolve()
        if not path.is_file() or path.is_symlink():
            raise WholeAotObjectError(
                f"native dependency is not a regular file: {path}"
            )
        file_hash = sha256_file(path)
        size = path.stat().st_size
        _hash_record(digest, role, size, file_hash)
        records.append({"role": role, "bytes": size, "sha256": file_hash})
    if not records:
        raise WholeAotObjectError("native dependency identity is empty")
    return digest.hexdigest(), tuple(records)


def _load_generated_sources(
    generated_directory: Path,
) -> tuple[dict[str, Any], tuple[GeneratedSource, ...]]:
    root = generated_directory.expanduser().resolve()
    if not root.is_dir() or root.is_symlink():
        raise WholeAotObjectError(f"generated whole-AOT directory is invalid: {root}")
    manifest_path = root / "whole_aot_cpp_manifest.json"
    try:
        manifest = load_json_object(manifest_path)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        raise WholeAotObjectError(
            f"generated whole-AOT manifest is invalid: {exc}"
        ) from exc
    if manifest.get("format") != GENERATED_FORMAT:
        raise WholeAotObjectError("unsupported generated whole-AOT format")
    files = manifest.get("files")
    if not isinstance(files, dict):
        raise WholeAotObjectError("generated whole-AOT file map is malformed")
    sources: list[GeneratedSource] = []
    for raw_name, raw_hash in sorted(files.items()):
        name = normalize_relative_path(str(raw_name))
        expected_hash = str(raw_hash).lower()
        path = root / name
        if not path.is_file() or path.is_symlink():
            raise WholeAotObjectError(f"generated whole-AOT file is missing: {path}")
        if sha256_file(path) != expected_hash:
            raise WholeAotObjectError(f"generated whole-AOT file changed: {path}")
        if name.endswith(".cpp"):
            sources.append(GeneratedSource(name, path, expected_hash))
    shard_count = int(manifest.get("shard_count", -1))
    expected_sources = 1 if shard_count == 1 else shard_count + 1
    if shard_count <= 0 or len(sources) != expected_sources:
        raise WholeAotObjectError(
            "generated whole-AOT source count does not match its manifest"
        )
    return manifest, tuple(sources)


def default_dependency_files(
    repo_root: Path, generated_directory: Path, nlohmann_include: Path
) -> tuple[DependencyFile, ...]:
    repo = repo_root.expanduser().resolve()
    generated = generated_directory.expanduser().resolve()
    nlohmann = nlohmann_include.expanduser().resolve()
    candidates = (
        repo / "tools/oot3d/native_game_runtime/oot3d_native_whole_aot_runtime.h",
        repo / "tools/oot3d/native_game_runtime/oot3d_native_a32_memory.h",
        repo / "tools/oot3d/native_a32_runtime/oot3d_aot_architectural_state.h",
        repo / "tools/oot3d/native_a32_runtime/oot3d_native_a32_vfp_ops.h",
        *(repo / "tools/oot3d/native_a32_runtime/upstream/recomp").glob("*.h"),
        *(generated.glob("*.h")),
        *((nlohmann / "nlohmann").rglob("*.hpp")),
    )
    dependencies: list[DependencyFile] = []
    for path in sorted(set(candidates), key=lambda item: item.as_posix()):
        resolved = path.resolve()
        try:
            role = "repo/" + resolved.relative_to(repo).as_posix()
        except ValueError:
            try:
                role = "nlohmann/" + resolved.relative_to(nlohmann).as_posix()
            except ValueError:
                role = "generated/" + resolved.relative_to(generated).as_posix()
        dependencies.append(DependencyFile(role, resolved))
    return tuple(dependencies)


def _compile_arguments(
    toolchain: NativeToolchain,
    generated_directory: Path,
    repo_root: Path,
    nlohmann_include: Path,
    sysroot: WindowsSysroot | None = None,
) -> tuple[str, ...]:
    include_roots = (
        generated_directory,
        repo_root / "tools/oot3d/native_game_runtime",
        repo_root / "tools/oot3d/native_a32_runtime",
        repo_root / "tools/oot3d/native_a32_runtime/upstream",
        nlohmann_include,
    )
    if platforms.is_windows(toolchain.platform):
        return (
            f"--target={toolchain.target_triple}",
            *(sysroot.arguments() if sysroot else ()),
            "/nologo",
            "/TP",
            "/DWIN32",
            "/D_WINDOWS",
            "/EHsc",
            "/O2",
            "/Ob2",
            "/DNDEBUG",
            "/std:c++20",
            "/MT",
            "/bigobj",
            "/fp:strict",
            "/w",
            "/Zc:preprocessor",
            "-flto=thin",
            "/DNOMINMAX",
            "/DOOT3D_NATIVE_GENERATED_WHOLE_AOT=1",
            *(f"/I{path.resolve()}" for path in include_roots),
            "/c",
        )
    # ELF: every symbol hidden except the two plugin exports, which the wrapper
    # marks visible. The host defines the same helpers and must not interpose.
    return (
        f"--target={toolchain.target_triple}",
        "-x", "c++",
        "-std=c++20",
        "-O2",
        "-DNDEBUG",
        "-fPIC",
        "-fvisibility=hidden",
        "-ffp-model=strict",
        "-w",
        "-flto=thin",
        "-DNOMINMAX",
        "-DOOT3D_NATIVE_GENERATED_WHOLE_AOT=1",
        *(f"-I{path.resolve()}" for path in include_roots),
        "-c",
    )


def source_object_key(
    source_sha256: str,
    dependency_identity: str,
    toolchain_identity: str,
    compile_arguments: Sequence[str],
) -> str:
    digest = hashlib.sha256()
    digest.update(FORMAT.encode("ascii"))
    digest.update(b"\0object\0")
    digest.update(bytes.fromhex(source_sha256))
    digest.update(bytes.fromhex(dependency_identity))
    digest.update(bytes.fromhex(toolchain_identity))
    for argument in compile_arguments:
        # Include paths are covered by dependency identities; stripping their
        # host locations keeps identical inputs portable across workspaces.
        normalized = "/I<verified>" if argument[:2] in ("/I", "-I") else argument
        digest.update(normalized.encode("utf-8"))
        digest.update(b"\0")
    return digest.hexdigest()


def _validate_cached_file(
    path: Path, metadata_path: Path, key: str
) -> dict[str, Any] | None:
    if not path.exists() and not metadata_path.exists():
        return None
    if not path.is_file() or path.is_symlink() or not metadata_path.is_file():
        raise WholeAotObjectError(f"native object cache entry is incomplete: {path}")
    metadata = load_json_object(metadata_path)
    if metadata.get("format") != FORMAT or metadata.get("cache_key") != key:
        raise WholeAotObjectError(f"native object cache identity mismatch: {path}")
    stat = path.stat()
    if stat.st_size != metadata.get("bytes"):
        raise WholeAotObjectError(f"native object cache entry changed: {path}")
    if stat.st_mtime_ns != metadata.get("mtime_ns"):
        if sha256_file(path) != metadata.get("sha256"):
            raise WholeAotObjectError(f"native object cache entry changed: {path}")
        metadata["mtime_ns"] = stat.st_mtime_ns
        atomic_write_json(metadata_path, metadata)
    return metadata


def _publish_file(
    temporary: Path, destination: Path, metadata_path: Path, key: str
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary.replace(destination)
    stat = destination.stat()
    atomic_write_json(
        metadata_path,
        {
            "format": FORMAT,
            "cache_key": key,
            "bytes": stat.st_size,
            "mtime_ns": stat.st_mtime_ns,
            "sha256": sha256_file(destination),
        },
    )


def _command_error(label: str, result: subprocess.CompletedProcess[str]) -> str:
    details = (result.stderr or result.stdout or "no compiler diagnostics").strip()
    return f"{label} failed with status {result.returncode}: {details}"


def _acquire_build_lock(path: Path) -> int:
    try:
        descriptor = acquire_cache_lock(path)
    except (OSError, ValueError) as exc:
        raise WholeAotObjectError(
            f"native object-cache build is already active: {path}"
        ) from exc
    return descriptor


def build_generated_cpp_archive(
    *,
    generated_directory: Path,
    repo_root: Path,
    nlohmann_include: Path,
    cache_root: Path,
    toolchain: NativeToolchain,
    jobs: int = 4,
    dependencies: Sequence[DependencyFile] | None = None,
    runner: CommandRunner = _default_runner,
    sysroot_contract: WindowsSysroot | None = None,
) -> dict[str, Any]:
    if jobs <= 0 or jobs > 16:
        raise WholeAotObjectError("native object-cache jobs must be between 1 and 16")
    if toolchain.profile != toolchain.platform.profile:
        raise WholeAotObjectError(
            f"unsupported native object-cache profile: {toolchain.profile}"
        )
    started = time.perf_counter()
    generated_directory = generated_directory.expanduser().resolve()
    repo_root = repo_root.expanduser().resolve()
    nlohmann_include = nlohmann_include.expanduser().resolve()
    manifest, sources = _load_generated_sources(generated_directory)
    sysroot = compiler_sysroot(toolchain.compiler, toolchain.sysroot, verified=sysroot_contract)
    toolchain_identity, toolchain = _toolchain_identity(toolchain, sysroot)
    dependencies = tuple(
        dependencies
        or default_dependency_files(repo_root, generated_directory, nlohmann_include)
    )
    dependency_identity, dependency_records = _dependency_identity(dependencies)
    compile_arguments = _compile_arguments(
        toolchain, generated_directory, repo_root, nlohmann_include, sysroot
    )
    object_records = tuple(
        (
            source,
            source_object_key(
                source.sha256,
                dependency_identity,
                toolchain_identity,
                compile_arguments,
            ),
        )
        for source in sources
    )
    archive_digest = hashlib.sha256()
    archive_digest.update(FORMAT.encode("ascii"))
    archive_digest.update(b"\0archive\0")
    archive_digest.update(bytes.fromhex(toolchain_identity))
    for _, key in object_records:
        archive_digest.update(bytes.fromhex(key))
    archive_key = archive_digest.hexdigest()

    cache_root = cache_root.expanduser().resolve()
    cache_root.mkdir(parents=True, exist_ok=True)
    windows = platforms.is_windows(toolchain.platform)
    archive = cache_root / "archives" / f"{archive_key}{'.lib' if windows else '.a'}"
    archive_metadata = archive.with_suffix(".json")
    cached_archive = _validate_cached_file(archive, archive_metadata, archive_key)
    if cached_archive is not None:
        return {
            "status": "reused",
            "backend": "generated_cpp_thinlto_fallback",
            "archive": str(archive),
            "archive_sha256": cached_archive["sha256"],
            "archive_cache_key": archive_key,
            "toolchain_identity_sha256": toolchain_identity,
            "sources": len(sources),
            "objects_compiled": 0,
            "objects_reused": len(sources),
            "elapsed_seconds": time.perf_counter() - started,
        }

    lock_path = cache_root / ".object-build.lock"
    lock_descriptor = _acquire_build_lock(lock_path)
    try:
        cached_archive = _validate_cached_file(archive, archive_metadata, archive_key)
        if cached_archive is not None:
            return {
                "status": "reused",
                "backend": "generated_cpp_thinlto_fallback",
                "archive": str(archive),
                "archive_sha256": cached_archive["sha256"],
                "archive_cache_key": archive_key,
                "toolchain_identity_sha256": toolchain_identity,
                "sources": len(sources),
                "objects_compiled": 0,
                "objects_reused": len(sources),
                "elapsed_seconds": time.perf_counter() - started,
            }
        object_paths: list[Path] = []
        compile_queue: list[tuple[GeneratedSource, str, Path, Path]] = []
        for source, key in object_records:
            object_path = cache_root / "objects" / key[:2] / f"{key}{'.obj' if windows else '.o'}"
            metadata_path = object_path.with_suffix(".json")
            object_paths.append(object_path)
            if _validate_cached_file(object_path, metadata_path, key) is None:
                compile_queue.append((source, key, object_path, metadata_path))

        def compile_one(item: tuple[GeneratedSource, str, Path, Path]) -> None:
            source, key, object_path, metadata_path = item
            object_path.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(
                prefix=f".{key[:16]}.", dir=object_path.parent
            ) as temporary:
                output = Path(temporary) / ("output.obj" if windows else "output.o")
                arguments = (
                    str(toolchain.compiler),
                    *compile_arguments,
                    *((f"/Fo{output}",) if windows else ("-o", str(output))),
                    str(source.path),
                )
                result = runner(arguments, generated_directory)
                if result.returncode != 0 or not output.is_file():
                    raise WholeAotObjectError(
                        _command_error(f"compiling {source.name}", result)
                    )
                _publish_file(output, object_path, metadata_path, key)

        run_bounded(compile_one, compile_queue, jobs)

        archive.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(
            prefix=f".{archive_key[:16]}.", dir=archive.parent
        ) as temporary:
            temporary_root = Path(temporary)
            output = temporary_root / ("whole_aot.lib" if windows else "whole_aot.a")
            response = temporary_root / "objects.rsp"
            response.write_text(
                "\n".join(f'"{path}"' for path in object_paths) + "\n",
                encoding="utf-8",
            )
            result = runner(
                (
                    str(toolchain.archiver),
                    *(("/nologo", f"/out:{output}") if windows else ("rcs", str(output))),
                    f"@{response}",
                ),
                archive.parent,
            )
            if result.returncode != 0 or not output.is_file():
                raise WholeAotObjectError(
                    _command_error("archiving AOT objects", result)
                )
            _publish_file(output, archive, archive_metadata, archive_key)

        atomic_write_json(
            cache_root / "archives" / f"{archive_key}.build.json",
            {
                "format": FORMAT,
                "backend": "generated_cpp_thinlto_fallback",
                "profile": toolchain.profile,
                "archive_cache_key": archive_key,
                "toolchain_identity_sha256": toolchain_identity,
                "dependency_identity_sha256": dependency_identity,
                "dependencies": dependency_records,
                "generated_program_sha256": manifest.get("program_sha256"),
                "generated_code_sha256": manifest.get("code_sha256"),
                "objects": [key for _, key in object_records],
            },
        )
        return {
            "status": "built",
            "backend": "generated_cpp_thinlto_fallback",
            "archive": str(archive),
            "archive_sha256": sha256_file(archive),
            "archive_cache_key": archive_key,
            "toolchain_identity_sha256": toolchain_identity,
            "sources": len(sources),
            "objects_compiled": len(compile_queue),
            "objects_reused": len(sources) - len(compile_queue),
            "elapsed_seconds": time.perf_counter() - started,
        }
    finally:
        release_cache_lock(lock_descriptor)
