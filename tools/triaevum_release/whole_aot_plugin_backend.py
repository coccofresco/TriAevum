"""Build the private, generated-C++ whole-AOT title plugin used by TriAevum."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence

try:
    from . import platforms
    from .cache_lock import acquire_cache_lock, release_cache_lock
    from .windows_sysroot import WindowsSysroot, compiler_sysroot, build_environment
    from .bundle_paths import distribution_path, distribution_root
    from .common import atomic_write_json, load_json_object, sha256_file
    from .whole_aot_object_cache import (
        NativeToolchain,
        WholeAotObjectError,
        build_generated_cpp_archive,
    )
except ImportError:
    import platforms
    from cache_lock import acquire_cache_lock, release_cache_lock
    from windows_sysroot import WindowsSysroot, compiler_sysroot, build_environment
    from bundle_paths import distribution_path, distribution_root
    from common import atomic_write_json, load_json_object, sha256_file
    from whole_aot_object_cache import (
        NativeToolchain,
        WholeAotObjectError,
        build_generated_cpp_archive,
    )


FORMAT = "triaevum_generated_cpp_whole_aot_plugin_v2"
PROFILE = platforms.WINDOWS.profile
REPO_ROOT = distribution_root()
A32_ROOT = distribution_path("tools/oot3d/native_a32_runtime")
RUNTIME_ROOT = distribution_path("tools/oot3d/native_game_runtime")


class WholeAotPluginError(ValueError):
    pass


@dataclass(frozen=True)
class WholeAotPluginToolchain:
    compiler: Path
    archiver: Path
    support_library: Path
    nlohmann_include: Path
    target_triple: str = platforms.host().triple
    profile: str = platforms.host().profile
    sysroot: Path | None = None

    @property
    def platform(self) -> platforms.Platform:
        return platforms.for_triple(self.target_triple)


def _regular_file(path: Path, label: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file() or resolved.is_symlink():
        raise WholeAotPluginError(f"{label} is not a regular file: {resolved}")
    return resolved


def _directory(path: Path, label: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_dir() or resolved.is_symlink():
        raise WholeAotPluginError(f"{label} is not a directory: {resolved}")
    return resolved


def _identity(records: Sequence[tuple[str, str]]) -> str:
    digest = hashlib.sha256()
    digest.update(FORMAT.encode("ascii"))
    digest.update(b"\0")
    for role, value in records:
        digest.update(role.encode("utf-8"))
        digest.update(b"\0")
        digest.update(value.encode("utf-8"))
        digest.update(b"\0")
    return digest.hexdigest()


def _run(arguments: Sequence[str], cwd: Path, label: str) -> None:
    completed = subprocess.run(
        list(arguments), cwd=cwd, capture_output=True, text=True, check=False,
        env=build_environment() if "/vctoolsdir" in arguments else None,
    )
    if completed.returncode == 0:
        return
    details = (completed.stderr or completed.stdout or "no diagnostics").strip()
    if "cannot open include file" in details.lower() or "linker command failed" in details.lower():
        details += (
            "\nThe bundled LLVM tools require the title-neutral C++/Windows "
            "link environment shipped with Forge or a compatible Visual C++ "
            "Build Tools installation."
        )
    raise WholeAotPluginError(
        f"{label} failed with status {completed.returncode}: {details}"
    )


def _load_generator() -> tuple[Any, Path, Path]:
    if str(A32_ROOT) not in sys.path:
        sys.path.insert(0, str(A32_ROOT))
    try:
        import whole_aot_cpp
    except ImportError as exc:
        raise WholeAotPluginError(
            f"whole-AOT C++ generator is unavailable: {exc}"
        ) from exc
    generator = _regular_file(Path(whole_aot_cpp.__file__), "whole-AOT generator")
    optimizer = _regular_file(
        A32_ROOT / "whole_aot_optimization_ir.py",
        "whole-AOT optimization model",
    )
    return whole_aot_cpp, generator, optimizer


def _validate_cached_plugin(
    plugin: Path, manifest_path: Path, cache_key: str
) -> dict[str, Any] | None:
    if not plugin.exists() and not manifest_path.exists():
        return None
    if not plugin.is_file() or plugin.is_symlink() or not manifest_path.is_file():
        raise WholeAotPluginError(f"whole-AOT plugin cache is incomplete: {plugin}")
    manifest = load_json_object(manifest_path)
    if (
        manifest.get("format") != FORMAT
        or manifest.get("cache_key") != cache_key
        or manifest.get("plugin_sha256") != sha256_file(plugin)
        or manifest.get("plugin_bytes") != plugin.stat().st_size
    ):
        raise WholeAotPluginError(f"whole-AOT plugin cache changed: {plugin}")
    return manifest


def build_whole_aot_plugin(
    *,
    program_path: Path,
    selection_path: Path,
    code_path: Path,
    cache_root: Path,
    toolchain: WholeAotPluginToolchain,
    shard_count: int = 256,
    jobs: int = 8,
    sysroot_contract: WindowsSysroot | None = None,
) -> dict[str, Any]:
    """Generate, compile and link one content-addressed private title DLL."""

    if shard_count <= 0 or shard_count > 512:
        raise WholeAotPluginError("whole-AOT shard count must be between 1 and 512")
    if jobs <= 0 or jobs > 16:
        raise WholeAotPluginError("whole-AOT jobs must be between 1 and 16")
    if toolchain.profile != toolchain.platform.profile:
        raise WholeAotPluginError(
            f"unsupported whole-AOT plugin profile: {toolchain.profile}"
        )
    windows = platforms.is_windows(toolchain.platform)
    plugin_name = toolchain.platform.plugin

    started = time.perf_counter()
    program = _regular_file(program_path, "structural AOT program")
    selection = _regular_file(selection_path, "whole-AOT selection")
    code = _regular_file(code_path, "title code image")
    compiler = _regular_file(toolchain.compiler, "native C++ compiler")
    archiver = _regular_file(toolchain.archiver, "native object archiver")
    linker = _regular_file(compiler.with_name("lld-link.exe" if windows else "ld.lld"), "native linker")
    builder = _regular_file(
        distribution_path("tools/triaevum_release/whole_aot_plugin_backend.py"),
        "whole-AOT builder identity",
    )
    support = _regular_file(toolchain.support_library, "whole-AOT support library")
    nlohmann = _directory(toolchain.nlohmann_include, "nlohmann include root")
    if windows:
        sysroot = compiler_sysroot(compiler, toolchain.sysroot, verified=sysroot_contract)
    elif toolchain.sysroot is not None or sysroot_contract is not None:
        raise WholeAotPluginError("Verified sysroots are only defined for Windows targets")
    else:
        sysroot = None
    wrapper = _regular_file(
        RUNTIME_ROOT / "triaevum_title_whole_aot_plugin.cpp",
        "whole-AOT plugin wrapper",
    )
    generator_module, generator, optimizer = _load_generator()

    input_hashes = {
        "program": sha256_file(program),
        "selection": sha256_file(selection),
        "code": sha256_file(code),
        "generator": sha256_file(generator),
        "optimizer": sha256_file(optimizer),
    }
    generation_key = _identity(
        tuple(input_hashes.items())
        + (("shards", str(shard_count)), ("strategy", "affinity"))
    )
    cache = cache_root.expanduser().resolve()
    generated = cache / "generated" / generation_key
    # An early hit is sound only when system headers/libraries have a verified
    # identity. Host-discovered dependencies keep the existing full validation.
    request_key = None
    if sysroot:
        records = [("generation", generation_key), ("sysroot", sysroot.identity),
                   ("target", toolchain.target_triple), ("profile", toolchain.profile),
                   ("python", sys.version)]
        for role, path in (("compiler", compiler), ("archiver", archiver),
                           ("linker", linker), ("support", support), ("wrapper", wrapper)):
            records.append((role, sha256_file(path)))
        for role, root, suffixes in (
            ("a32", A32_ROOT, {".py", ".h", ".hpp", ".inl", ".inc"}),
            ("runtime", RUNTIME_ROOT, {".h", ".hpp", ".inl", ".inc"}),
            ("forge", builder.parent, {".py"}),
            ("json", nlohmann, {".h", ".hpp", ".inl", ".inc"}),
        ):
            for path in sorted(root.rglob("*")):
                if path.is_file() and path.suffix in suffixes:
                    if path.is_symlink():
                        raise WholeAotPluginError("Dependency identity cannot contain symlinks")
                    records.append((role + ":" + path.relative_to(root).as_posix(), sha256_file(path)))
        request_key = _identity(records)
        request_path = cache / "requests" / (request_key + ".json")
        if request_path.is_file():
            request = load_json_object(request_path)
            key = request.get("plugin_cache_key", "")
            if (request.get("request_key") != request_key or len(key) != 64
                    or any(c not in "0123456789abcdef" for c in key)):
                raise WholeAotPluginError("Invalid whole-AOT request cache identity")
            root = cache / "plugins" / key
            cached = _validate_cached_plugin(root / plugin_name, root / "whole-aot-plugin.json", key)
            if cached is not None and cached.get("request_key") == request_key:
                return {**cached, "status": "reused", "early_cache_hit": True,
                        "cached_build_objects_compiled": cached["objects_compiled"],
                        "objects_compiled": 0, "objects_reused": 0,
                        "plugin": str(root / plugin_name),
                        "generated_directory": str(generated),
                        "elapsed_seconds": time.perf_counter() - started}
    # Generated source depends on title/translator inputs, not linker or SDK.
    # Serialize producers sharing that immutable identity, including consumers
    # reading the files while another request might regenerate a damaged cache.
    generated.mkdir(parents=True, exist_ok=True)
    try:
        generation_lock = acquire_cache_lock(generated / ".generation.lock")
    except (OSError, ValueError) as exc:
        raise WholeAotPluginError(f"whole-AOT generation is already active: {generated}") from exc
    try:
        generated_manifest = generator_module.generate(
            program,
            selection,
            code,
            generated,
            shard_count=shard_count,
            shard_strategy="affinity",
        )
        archive = build_generated_cpp_archive(
            generated_directory=generated,
            repo_root=REPO_ROOT,
            nlohmann_include=nlohmann,
            cache_root=cache / "native-objects",
            toolchain=NativeToolchain(
                compiler=compiler,
                archiver=archiver,
                target_triple=toolchain.target_triple,
                sysroot=toolchain.sysroot,
            ),
            jobs=jobs,
            sysroot_contract=sysroot,
        )
    except (OSError, ValueError, WholeAotObjectError) as exc:
        raise WholeAotPluginError(f"whole-AOT code generation failed: {exc}") from exc
    finally:
        release_cache_lock(generation_lock)

    archive_path = _regular_file(Path(str(archive["archive"])), "whole-AOT archive")
    dependency_paths = (
        wrapper,
        RUNTIME_ROOT / "triaevum_title_aot_abi.h",
        RUNTIME_ROOT / "triaevum_title_whole_aot_abi.h",
        RUNTIME_ROOT / "oot3d_native_whole_aot_runtime.h",
        A32_ROOT / "oot3d_aot_architectural_state.h",
        A32_ROOT / "oot3d_native_a32_vfp_ops.h",
        generated / "oot3d_whole_aot_generated.h",
    )
    dependency_hashes = tuple(
        (f"dependency:{path.name}", sha256_file(_regular_file(path, path.name)))
        for path in dependency_paths
    )
    toolchain_identity = _identity(
        (
            ("compiler", sha256_file(compiler)),
            ("archiver", sha256_file(archiver)),
            ("linker", sha256_file(linker)),
            ("builder", sha256_file(builder)),
            ("support", sha256_file(support)),
            ("target", toolchain.target_triple),
            ("profile", toolchain.profile),
            ("sysroot", sysroot.identity if sysroot else "host-discovery"),
        )
    )
    cache_key = _identity(
        (
            ("generation", generation_key),
            ("archive", str(archive["archive_sha256"])),
            ("toolchain", toolchain_identity),
            ("request", request_key or "host-discovery"),
            *dependency_hashes,
        )
    )
    plugin_root = cache / "plugins" / cache_key
    plugin = plugin_root / plugin_name
    manifest_path = plugin_root / "whole-aot-plugin.json"
    cached = _validate_cached_plugin(plugin, manifest_path, cache_key)
    if cached is not None:
        return {
            **cached,
            "status": "reused",
            "cached_build_objects_compiled": cached["objects_compiled"],
            "objects_compiled": archive["objects_compiled"],
            "objects_reused": archive["objects_reused"],
            "plugin": str(plugin),
            "generated_directory": str(generated),
            "elapsed_seconds": time.perf_counter() - started,
        }

    plugin_root.mkdir(parents=True, exist_ok=True)
    lock_path = plugin_root / ".build.lock"
    try:
        lock = acquire_cache_lock(lock_path)
    except (OSError, ValueError) as exc:
        raise WholeAotPluginError(
            f"whole-AOT plugin build is already active: {lock_path}"
        ) from exc
    try:
        cached = _validate_cached_plugin(plugin, manifest_path, cache_key)
        if cached is not None:
            return {
                **cached,
                "status": "reused",
                "cached_build_objects_compiled": cached["objects_compiled"],
                "objects_compiled": archive["objects_compiled"],
                "objects_reused": archive["objects_reused"],
                "plugin": str(plugin),
                "generated_directory": str(generated),
                "elapsed_seconds": time.perf_counter() - started,
            }

        with tempfile.TemporaryDirectory(prefix=".link-", dir=plugin_root) as temporary:
            temporary_root = Path(temporary)
            wrapper_object = temporary_root / ("wrapper.obj" if windows else "wrapper.o")
            linked = temporary_root / plugin_name
            include_roots = (
                generated,
                RUNTIME_ROOT,
                A32_ROOT,
                A32_ROOT / "upstream",
            )
            if windows:
                compile_command = (
                    str(compiler),
                    f"--target={toolchain.target_triple}",
                    *(sysroot.arguments() if sysroot else ()),
                    "/nologo",
                    "/TP",
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
                    "/Brepro",
                    "/DNOMINMAX",
                    "/DOOT3D_NATIVE_GENERATED_WHOLE_AOT=1",
                    *(f"/I{path}" for path in include_roots),
                    "/c",
                    f"/Fo{wrapper_object}",
                    str(wrapper),
                )
                link_command = (
                    str(compiler),
                    f"--target={toolchain.target_triple}",
                    *(sysroot.arguments() if sysroot else ()),
                    "/nologo",
                    "/LD",
                    "/MT",
                    f"/Fe{linked}",
                    str(wrapper_object),
                    str(archive_path),
                    str(support),
                    "-fuse-ld=lld",
                    "/link",
                    "/NOIMPLIB",
                    "/NOEXP",
                    "/OPT:REF",
                    "/OPT:ICF",
                    "/INCREMENTAL:NO",
                    "/Brepro",
                )
            else:
                # Only the two query exports stay visible; -Bsymbolic keeps the
                # plugin's own whole-AOT helpers from binding to the host's copies.
                compile_command = (
                    str(compiler),
                    f"--target={toolchain.target_triple}",
                    "-x", "c++",
                    "-std=c++20",
                    "-O2",
                    "-DNDEBUG",
                    "-fPIC",
                    "-fvisibility=hidden",
                    "-ffp-model=strict",
                    "-w",
                    "-DNOMINMAX",
                    "-DOOT3D_NATIVE_GENERATED_WHOLE_AOT=1",
                    *(f"-I{path}" for path in include_roots),
                    "-c",
                    "-o", str(wrapper_object),
                    str(wrapper),
                )
                link_command = (
                    str(compiler),
                    f"--target={toolchain.target_triple}",
                    "-shared",
                    "-fuse-ld=lld",
                    "-flto=thin",
                    "-static-libstdc++",
                    "-static-libgcc",
                    "-Wl,--gc-sections",
                    "-Wl,--icf=all",
                    "-Wl,-Bsymbolic",
                    "-Wl,--exclude-libs,ALL",
                    "-Wl,--build-id=sha1",
                    "-o", str(linked),
                    str(wrapper_object),
                    str(archive_path),
                    str(support),
                )
            _run(compile_command, temporary_root, "compiling the whole-AOT plugin wrapper")
            _run(link_command, temporary_root, "linking the private whole-AOT plugin")
            if not linked.is_file():
                raise WholeAotPluginError("whole-AOT linker produced no title plugin")
            linked.replace(plugin)

        manifest = {
            "format": FORMAT,
            "status": "built",
            "backend": "generated_cpp_whole_aot_plugin_v2",
            "profile": toolchain.profile,
            "target": toolchain.target_triple,
            "cache_key": cache_key,
            "toolchain_identity_sha256": toolchain_identity,
            "translator_identity_sha256": _identity(
                (("generation", generation_key), ("toolchain", toolchain_identity))
            ),
            "program_sha256": input_hashes["program"],
            "code_sha256": input_hashes["code"],
            "plugin_sha256": sha256_file(plugin),
            "plugin_bytes": plugin.stat().st_size,
            "functions": len(generated_manifest.get("functions", [])),
            "shards": shard_count,
            "objects_compiled": archive["objects_compiled"],
            "objects_reused": archive["objects_reused"],
            "archive_cache_key": archive["archive_cache_key"],
            "request_key": request_key,
        }
        atomic_write_json(manifest_path, manifest)
        if request_key:
            atomic_write_json(request_path, {"request_key": request_key, "plugin_cache_key": cache_key})
        return {
            **manifest,
            "plugin": str(plugin),
            "generated_directory": str(generated),
            "elapsed_seconds": time.perf_counter() - started,
        }
    finally:
        release_cache_lock(lock)
