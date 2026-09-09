"""Verify user-owned title inputs and prepare a private TriAevum content index."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Sequence

try:
    from . import TOOL_VERSION
    from .product_contract import ensure_runtime_config, query_product
    from .release_platform import WINDOWS, for_target, host_platform
    from .activation_transaction import activation_transaction
    from .installation_context import InstallationContext, resolve_reference
    from .toolchain_probe import probe_toolchain
    from .bundle_paths import (
        distribution_path,
        distribution_root,
        installation_path,
        activation_path,
    )
    from .common import (
        atomic_write_bytes,
        atomic_write_json,
        load_json_object,
        sha256_file,
    )
    from .tam_builder import (
        AUDIO_SERVICE_V1,
        FILESYSTEM_SERVICE_V1,
        INPUT_SERVICE_V1,
        PICA_SERVICE_V1,
        build_tam,
    )
    from .direct_aot_backend import (
        DirectAotToolchain,
        build_direct_aot_plugin,
    )
    from .whole_aot_source_backend import (
        SourceImage,
        load_default_frontend,
        prepare_whole_aot_ir,
    )
    from .whole_aot_object_cache import (
        NativeToolchain,
        build_generated_cpp_archive,
    )
    from .whole_aot_plugin_backend import (
        WholeAotPluginToolchain,
        build_whole_aot_plugin,
    )
except ImportError:
    from __init__ import TOOL_VERSION
    from product_contract import ensure_runtime_config, query_product
    from release_platform import WINDOWS, for_target, host_platform
    from activation_transaction import activation_transaction
    from installation_context import InstallationContext, resolve_reference
    from toolchain_probe import probe_toolchain
    from bundle_paths import distribution_path, distribution_root, installation_path, activation_path
    from common import (
        atomic_write_bytes,
        atomic_write_json,
        load_json_object,
        sha256_file,
    )
    from tam_builder import (
        AUDIO_SERVICE_V1,
        FILESYSTEM_SERVICE_V1,
        INPUT_SERVICE_V1,
        PICA_SERVICE_V1,
        build_tam,
    )
    from direct_aot_backend import DirectAotToolchain, build_direct_aot_plugin
    from whole_aot_source_backend import (
        SourceImage,
        load_default_frontend,
        prepare_whole_aot_ir,
    )
    from whole_aot_object_cache import (
        NativeToolchain,
        build_generated_cpp_archive,
    )
    from whole_aot_plugin_backend import (
        WholeAotPluginToolchain,
        build_whole_aot_plugin,
    )


REPO_ROOT = distribution_root()
ROOT = distribution_path("tools/triaevum_release")
DEFAULT_RECIPES = distribution_path(
    "tools/triaevum_release/supported_revisions.json"
)
# The package owns its supported inputs; embedded recipes are the source-tool
# fallback. Every packaged recipe is still checked against the title catalog.
if installation_path("recipes/oot3d.json").is_file():
    DEFAULT_RECIPES = installation_path("recipes/oot3d.json")


class ForgeError(RuntimeError):
    pass


ProcessManifestBuilder = Callable[[Path, Path, Path], dict[str, object]]


@dataclass(frozen=True)
class VerifiedInput:
    kind: str
    path: Path
    bytes: int
    sha256: str

    def as_json(self) -> dict[str, object]:
        return {
            "path": str(self.path),
            "bytes": self.bytes,
            "sha256": self.sha256,
        }


@dataclass(frozen=True)
class PreparedForgeContent:
    directory: Path
    index: dict[str, Any]
    state: dict[str, Any]
    inputs: dict[str, VerifiedInput]


def default_output_root() -> Path:
    return activation_path("data/titles").resolve()


def default_translation_cache_root() -> Path:
    return default_output_root().parent / "translator-cache"


def default_forge_support_path(name: str) -> Path:
    return installation_path(f"forge/{name}")


def default_forge_include_path() -> Path:
    bundled = distribution_path("forge/include")
    if bundled.is_dir():
        return bundled
    return default_forge_support_path("include")


def default_active_title_state_path() -> Path:
    return default_output_root().parent / "active-title.json"


def default_runtime_plugin_path() -> Path:
    return activation_path(host_platform().title_module)


def default_runtime_launch_profile_path() -> Path:
    return activation_path("TriAevum.launch.json")


class HashCache:
    def __init__(self, path: Path, *, enabled: bool = True) -> None:
        self.path = path
        self.enabled = enabled
        self.entries: dict[str, dict[str, object]] = {}
        if enabled and path.is_file():
            try:
                payload = load_json_object(path)
                if payload.get("format") == "triaevum_forge_hash_cache_v1":
                    raw_entries = payload.get("entries", {})
                    if isinstance(raw_entries, dict):
                        self.entries = {
                            str(key): value
                            for key, value in raw_entries.items()
                            if isinstance(value, dict)
                        }
            except (OSError, ValueError, json.JSONDecodeError):
                self.entries = {}

    def digest(self, path: Path) -> str:
        resolved = path.resolve()
        stat = resolved.stat()
        key = str(resolved)
        cached = self.entries.get(key)
        if (
            self.enabled
            and cached is not None
            and cached.get("bytes") == stat.st_size
            and cached.get("mtime_ns") == stat.st_mtime_ns
            and isinstance(cached.get("sha256"), str)
        ):
            return str(cached["sha256"])
        digest = sha256_file(resolved)
        if self.enabled:
            self.entries[key] = {
                "bytes": stat.st_size,
                "mtime_ns": stat.st_mtime_ns,
                "sha256": digest,
            }
        return digest

    def remember(self, path: Path, digest: str) -> None:
        """Record a digest computed while Forge itself wrote the file."""

        if not self.enabled:
            return
        resolved = path.resolve()
        stat = resolved.stat()
        self.entries[str(resolved)] = {
            "bytes": stat.st_size,
            "mtime_ns": stat.st_mtime_ns,
            "sha256": digest.lower(),
        }

    def save(self) -> None:
        if self.enabled:
            atomic_write_json(
                self.path,
                {"format": "triaevum_forge_hash_cache_v1", "entries": self.entries},
            )


def load_recipe(path: Path, recipe_id: str) -> dict[str, Any]:
    payload = load_json_object(path)
    if payload.get("format") != "triaevum_supported_revisions_v1":
        raise ForgeError("unsupported supported-revision recipe format")
    recipes = payload.get("recipes")
    if not isinstance(recipes, list):
        raise ForgeError("supported-revision recipe list is malformed")
    matches = [
        item
        for item in recipes
        if isinstance(item, dict) and item.get("id") == recipe_id
    ]
    if len(matches) != 1:
        raise ForgeError(f"unknown or duplicate revision recipe: {recipe_id}")
    return matches[0]


def verify_input(
    kind: str, path: Path, contract: dict[str, Any], cache: HashCache
) -> VerifiedInput:
    resolved = path.expanduser().resolve()
    if not resolved.is_file() or resolved.is_symlink():
        raise ForgeError(f"{kind} input is not a regular file: {resolved}")
    size = resolved.stat().st_size
    if size != int(contract.get("bytes", -1)):
        raise ForgeError(
            f"{kind} size mismatch: expected {contract.get('bytes')}, got {size}"
        )
    digest = cache.digest(resolved)
    expected = str(contract.get("sha256", "")).lower()
    if digest != expected:
        raise ForgeError(f"{kind} SHA-256 mismatch: expected {expected}, got {digest}")
    return VerifiedInput(kind, resolved, size, digest)


def verify_sources(
    recipe: dict[str, Any],
    *,
    code_path: Path,
    exheader_path: Path,
    romfs_path: Path,
    cache: HashCache,
    topscreen_archive: Path | None = None,
) -> tuple[dict[str, VerifiedInput], VerifiedInput | None]:
    contracts = recipe.get("inputs")
    if not isinstance(contracts, dict):
        raise ForgeError("revision recipe input contracts are malformed")
    paths = {"code": code_path, "exheader": exheader_path, "romfs": romfs_path}
    if recipe.get('data_compatibility') is not None:
        try:
            from .data_compatibility import verify as verify_family
        except ImportError:
            from data_compatibility import verify as verify_family
        actual = {kind: {'bytes': path.stat().st_size, 'sha256': cache.digest(path)}
                  for kind, path in paths.items()}
        if actual != contracts:
            verify_family(recipe, paths, phase='execution')
            contracts = actual
    verified: dict[str, VerifiedInput] = {}
    for kind, path in paths.items():
        contract = contracts.get(kind)
        if not isinstance(contract, dict):
            raise ForgeError(f"revision recipe has no {kind} contract")
        verified[kind] = verify_input(kind, path, contract, cache)

    verified_mod: VerifiedInput | None = None
    if topscreen_archive is not None:
        optional = recipe.get("optional_inputs", {})
        contract = (
            optional.get("topscreen_2_1_1_archive")
            if isinstance(optional, dict)
            else None
        )
        if not isinstance(contract, dict):
            raise ForgeError("revision recipe does not support a TopScreen archive")
        resolved = topscreen_archive.expanduser().resolve()
        if not resolved.is_file() or resolved.is_symlink():
            raise ForgeError(f"TopScreen archive is not a regular file: {resolved}")
        digest = cache.digest(resolved)
        expected = str(contract.get("sha256", "")).lower()
        if digest != expected:
            raise ForgeError(
                f"TopScreen archive SHA-256 mismatch: expected {expected}, got {digest}"
            )
        verified_mod = VerifiedInput(
            "topscreen_2_1_1_archive", resolved, resolved.stat().st_size, digest
        )
    cache.save()
    return verified, verified_mod


def content_key(recipe: dict[str, Any], verified: dict[str, VerifiedInput]) -> str:
    digest = hashlib.sha256()
    digest.update(b"triaevum-private-content-v1\0")
    digest.update(str(recipe["id"]).encode("utf-8"))
    for kind in ("code", "exheader", "romfs"):
        digest.update(b"\0")
        digest.update(verified[kind].sha256.encode("ascii"))
    return digest.hexdigest()


def _build_process_manifest(
    exheader_path: Path, code_path: Path, romfs_path: Path
) -> dict[str, object]:
    repository_root = REPO_ROOT
    if str(repository_root) not in sys.path:
        sys.path.insert(0, str(repository_root))
    from tools.oot3d.native_a32_runtime.build_process_manifest import build_manifest

    return build_manifest(exheader_path, code_path, romfs_path)


def _validate_process_manifest(
    manifest: dict[str, object],
    recipe: dict[str, Any],
    verified: dict[str, VerifiedInput],
) -> list[dict[str, int]]:
    if not isinstance(manifest, dict):
        raise ForgeError("process manifest builder did not return an object")
    if manifest.get("format") != "oot3d_native_process_manifest_v1":
        raise ForgeError("process manifest builder returned an unsupported format")
    source = manifest.get("source")
    process = manifest.get("process")
    if not isinstance(source, dict) or not isinstance(process, dict):
        raise ForgeError("process manifest is missing source or process metadata")
    expected_sources = {
        "exheader_sha256": verified["exheader"].sha256,
        "code_bin_sha256": verified["code"].sha256,
    }
    for field, expected in expected_sources.items():
        if str(source.get(field, "")).lower() != expected:
            raise ForgeError(f"process manifest {field} does not match verified input")

    process_contract = recipe.get("process")
    if isinstance(process_contract, dict):
        expected_entry = _manifest_integer(
            process_contract.get("entrypoint"), "recipe process entrypoint"
        )
        actual_entry = _manifest_integer(
            process.get("entrypoint"), "process manifest entrypoint"
        )
        if actual_entry != expected_entry:
            raise ForgeError("process manifest entrypoint does not match recipe")

    linear_heap = process.get("linear_heap")
    system_regions = process.get("system_regions")
    if not isinstance(linear_heap, dict) or not isinstance(system_regions, list):
        raise ForgeError("process manifest has no physical-memory layout")
    regions = [
        {
            "physical_base": 0x20000000,
            "guest_base": _manifest_integer(
                linear_heap.get("base_address"), "linear heap base address"
            ),
            "bytes": _manifest_integer(linear_heap.get("size"), "linear heap size"),
        }
    ]
    vram = next(
        (
            item
            for item in system_regions
            if isinstance(item, dict) and item.get("name") == "ctr_vram"
        ),
        None,
    )
    if not isinstance(vram, dict):
        raise ForgeError("process manifest has no CTR VRAM region")
    regions.append(
        {
            "physical_base": 0x18000000,
            "guest_base": _manifest_integer(vram.get("address"), "CTR VRAM address"),
            "bytes": _manifest_integer(vram.get("mapped_size"), "CTR VRAM mapped size"),
        }
    )
    for region in regions:
        if (
            region["guest_base"] < 0
            or region["guest_base"] > 0xFFFFFFFF
            or region["bytes"] <= 0
            or region["bytes"] > 0xFFFFFFFF
        ):
            raise ForgeError("process manifest physical-memory region is invalid")
    return regions


def _manifest_integer(value: object, field: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise ForgeError(f"{field} is not an integer")
    return value


def prepare_content(
    recipe: dict[str, Any],
    verified: dict[str, VerifiedInput],
    *,
    output_root: Path,
    verified_mod: VerifiedInput | None = None,
    process_manifest_builder: ProcessManifestBuilder | None = None,
) -> dict[str, Any]:
    key = content_key(recipe, verified)
    title_root = output_root.expanduser().resolve() / str(recipe["id"]) / key
    index_path = title_root / "content.tap"
    state_path = title_root / "forge-state.json"
    process_manifest_path = title_root / "process-manifest.json"
    builder = process_manifest_builder or _build_process_manifest
    try:
        process_manifest = builder(
            verified["exheader"].path,
            verified["code"].path,
            verified["romfs"].path,
        )
    except (OSError, ValueError) as exc:
        raise ForgeError(f"cannot derive CTR process manifest: {exc}") from exc
    physical_memory_regions = _validate_process_manifest(
        process_manifest, recipe, verified
    )
    # Preserve legacy manifests exactly; new installations use file-relative paths.
    try:
        portable = not title_root.exists() or load_json_object(index_path).get("path_mode") == "manifest_relative_v1"
    except (OSError, ValueError) as exc:
        raise ForgeError(f"existing Forge output is incomplete: {exc}") from exc
    context = InstallationContext(output_root.expanduser().resolve().parent)
    if portable:
        scopes = {}
        for field in ("code_bin_path", "exheader_path", "romfs_image_path"):
            value = process_manifest["source"].get(field)
            if isinstance(value, str):
                source_path = Path(value)
                process_manifest["source"][field] = context.reference(source_path, title_root)
                scopes[field] = context.scope(source_path, title_root)
        process_manifest["source_path_scopes"] = scopes
    process_manifest_bytes = (
        json.dumps(process_manifest, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")
    index = {
        "format": "triaevum_content_index_v1",
        "private_local_artifact": True,
        "redistributable": False,
        "recipe": str(recipe["id"]),
        "content_key": key,
        "inputs": {kind: value.as_json() for kind, value in verified.items()},
        "optional_inputs": (
            {verified_mod.kind: verified_mod.as_json()} if verified_mod else {}
        ),
        "process_manifest": {
            "path": str(process_manifest_path),
            "bytes": len(process_manifest_bytes),
            "sha256": hashlib.sha256(process_manifest_bytes).hexdigest(),
        },
        "physical_memory_regions": physical_memory_regions,
    }
    if portable:
        index["path_mode"] = "manifest_relative_v1"
        index["process_manifest"]["path"] = "process-manifest.json"
        for group in ("inputs", "optional_inputs"):
            for descriptor in index[group].values():
                source_path = Path(descriptor["path"])
                descriptor["path"] = context.reference(source_path, title_root)
                descriptor["path_scope"] = context.scope(source_path, title_root)
    state = {
        "format": "triaevum_forge_state_v1",
        "tool_version": TOOL_VERSION,
        "recipe": str(recipe["id"]),
        "content_key": key,
        "content": {"status": "ready", "index": "content.tap"},
        "process": {"status": "ready", "manifest": "process-manifest.json"},
        "module": {
            "status": "not_built",
            "reason": "direct game.tam code-generation backend is not implemented",
        },
        "pipelines": {"status": "not_built"},
    }

    if title_root.exists():
        try:
            existing_index = load_json_object(index_path)
            existing_state = load_json_object(state_path)
            existing_manifest = load_json_object(process_manifest_path)
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            raise ForgeError(f"existing Forge output is incomplete: {exc}") from exc
        state_matches = (
            existing_state.get("format") == state["format"]
            and existing_state.get("recipe") == state["recipe"]
            and existing_state.get("content_key") == state["content_key"]
            and existing_state.get("content") == state["content"]
            and existing_state.get("process") == state["process"]
        )
        if (
            existing_index != index
            or not state_matches
            or existing_manifest != process_manifest
        ):
            raise ForgeError(
                f"existing Forge output does not match inputs: {title_root}"
            )
        existing_module = existing_state.get("module")
        return {
            "status": "reused",
            "directory": str(title_root),
            "content_index": str(index_path),
            "process_manifest": str(process_manifest_path),
            "module_status": (
                existing_module.get("status")
                if isinstance(existing_module, dict)
                else "unknown"
            ),
        }

    title_root.mkdir(parents=True, exist_ok=False)
    try:
        atomic_write_json(process_manifest_path, process_manifest)
        atomic_write_json(index_path, index)
        atomic_write_json(state_path, state)
    except BaseException:
        for path in (process_manifest_path, index_path, state_path):
            path.unlink(missing_ok=True)
        title_root.rmdir()
        raise
    return {
        "status": "prepared",
        "directory": str(title_root),
        "content_index": str(index_path),
        "process_manifest": str(process_manifest_path),
        "module_status": "not_built",
    }


def load_prepared_content(
    prepared_directory: Path,
    *,
    required_inputs: Sequence[str],
) -> PreparedForgeContent:
    title_root = prepared_directory.expanduser().resolve()
    if not title_root.is_dir() or title_root.is_symlink():
        raise ForgeError(f"prepared directory is invalid: {title_root}")

    index_path = title_root / "content.tap"
    state_path = title_root / "forge-state.json"
    process_manifest_path = title_root / "process-manifest.json"
    try:
        index = load_json_object(index_path)
        state = load_json_object(state_path)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        raise ForgeError(f"prepared Forge output is incomplete: {exc}") from exc
    if (
        index.get("format") != "triaevum_content_index_v1"
        or index.get("private_local_artifact") is not True
        or index.get("redistributable") is not False
        or state.get("format") != "triaevum_forge_state_v1"
    ):
        raise ForgeError("prepared Forge output has an unsupported format")
    if (
        state.get("recipe") != index.get("recipe")
        or state.get("content_key") != index.get("content_key")
        or not isinstance(state.get("content"), dict)
        or state["content"].get("status") != "ready"
        or not isinstance(state.get("process"), dict)
        or state["process"].get("status") != "ready"
    ):
        raise ForgeError("Forge state does not match its prepared content")

    process_descriptor = index.get("process_manifest")
    input_descriptors = index.get("inputs")
    if not isinstance(process_descriptor, dict) or not isinstance(
        input_descriptors, dict
    ):
        raise ForgeError("content.tap is missing process or input metadata")
    indexed_process_path = resolve_reference(str(process_descriptor.get("path", "")), title_root)
    if indexed_process_path != process_manifest_path.resolve():
        raise ForgeError("content.tap process manifest escapes its prepared directory")
    try:
        process_bytes = process_manifest_path.stat().st_size
        process_hash = sha256_file(process_manifest_path)
    except OSError as exc:
        raise ForgeError(f"cannot verify process manifest: {exc}") from exc
    if (
        process_bytes != process_descriptor.get("bytes")
        or process_hash != str(process_descriptor.get("sha256", "")).lower()
    ):
        raise ForgeError("process manifest changed after content preparation")

    verified: dict[str, VerifiedInput] = {}
    for kind in required_inputs:
        descriptor = input_descriptors.get(kind)
        if not isinstance(descriptor, dict):
            raise ForgeError(f"content.tap is missing {kind} input metadata")
        path = resolve_reference(str(descriptor.get("path", "")), title_root)
        expected_bytes = descriptor.get("bytes")
        expected_hash = str(descriptor.get("sha256", "")).lower()
        try:
            valid = (
                path.is_file()
                and not path.is_symlink()
                and path.stat().st_size == expected_bytes
                and sha256_file(path) == expected_hash
            )
        except OSError as exc:
            raise ForgeError(f"cannot verify prepared {kind} input: {exc}") from exc
        if not valid:
            raise ForgeError(f"{kind} input changed after content preparation")
        verified[kind] = VerifiedInput(
            kind=kind,
            path=path,
            bytes=int(expected_bytes),
            sha256=expected_hash,
        )
    return PreparedForgeContent(title_root, index, state, verified)


def activate_prepared_title(
    prepared_directory: Path, *, active_title_state: Path
) -> dict[str, Any]:
    prepared = load_prepared_content(prepared_directory, required_inputs=())
    module = prepared.state.get("module")
    if not isinstance(module, dict) or module.get("status") != "ready":
        raise ForgeError("only a title with a verified private module can be active")
    active_path = active_title_state.expanduser().resolve()
    active = {
        "format": "triaevum_active_title_v1",
        "directory": InstallationContext(active_path.parent).reference(prepared.directory, active_path.parent),
        "directory_scope": InstallationContext(active_path.parent).scope(prepared.directory),
        "recipe": prepared.index.get("recipe"),
        "content_key": prepared.index.get("content_key"),
        "module_sha256": module.get("sha256"),
    }
    atomic_write_json(active_path, active)
    return {"status": "active", "active_title": str(active_path), **active}


def prepare_private_aot_ir(
    prepared_directory: Path,
    *,
    cache_root: Path,
    seed_program: Path | None = None,
) -> dict[str, Any]:
    prepared = load_prepared_content(
        prepared_directory, required_inputs=("code", "exheader")
    )
    recipe = prepared.index.get("recipe")
    if not isinstance(recipe, str) or not recipe:
        raise ForgeError("content.tap recipe is invalid")
    code = prepared.inputs["code"]
    exheader = prepared.inputs["exheader"]
    result = prepare_whole_aot_ir(
        recipe=recipe,
        code=SourceImage(code.path, code.bytes, code.sha256),
        exheader=SourceImage(exheader.path, exheader.bytes, exheader.sha256),
        cache_root=cache_root,
        seed_program=seed_program,
    )
    prepared.state["translation"] = {
        "status": "ir_ready",
        "backend": "whole_aot_structural_ir_v1",
        "cache_key": result["cache_key"],
        "directory": result["directory"],
        "program": result["program"],
        "program_sha256": result["program_sha256"],
        "translator_identity_sha256": result["translator_identity_sha256"],
        "audit": result["audit"],
    }
    atomic_write_json(prepared.directory / "forge-state.json", prepared.state)
    return result


def build_private_aot_fallback(
    prepared_directory: Path,
    *,
    generated_directory: Path,
    llvm_root: Path,
    nlohmann_include: Path,
    cache_root: Path,
    jobs: int = 4,
) -> dict[str, Any]:
    """Build the generated-C++ development fallback after a full source audit."""
    prepared = load_prepared_content(
        prepared_directory, required_inputs=("code", "exheader")
    )
    translation = prepared.state.get("translation")
    if not isinstance(translation, dict) or translation.get("status") != "ir_ready":
        raise ForgeError("structural AOT IR must be prepared before object compilation")
    if translation.get("backend") != "whole_aot_structural_ir_v1":
        raise ForgeError("prepared structural AOT IR backend is unsupported")
    ir_directory = Path(str(translation.get("directory", ""))).resolve()
    program = Path(str(translation.get("program", ""))).resolve()
    if (
        not ir_directory.is_dir()
        or ir_directory.is_symlink()
        or program.parent != ir_directory
        or program.name != "aot_program.json"
        or not program.is_file()
        or program.is_symlink()
    ):
        raise ForgeError("prepared structural AOT IR location is invalid")
    program_hash = sha256_file(program)
    if program_hash != str(translation.get("program_sha256", "")).lower():
        raise ForgeError("prepared structural AOT IR changed before object compilation")

    generated_directory = generated_directory.expanduser().resolve()
    generated_manifest = generated_directory / "whole_aot_cpp_manifest.json"
    frontend = load_default_frontend()
    audit = frontend.audit_product(
        frontend.product_manifest,
        program,
        code_path=prepared.inputs["code"].path,
        exheader_path=prepared.inputs["exheader"].path,
        selection_path=frontend.selection,
        generated_manifest_path=generated_manifest,
        verify_generated_files=True,
    )
    if not audit.ok:
        raise ForgeError(
            "generated-C++ fallback audit failed: "
            + "; ".join(str(error) for error in audit.errors)
        )

    llvm_bin = llvm_root.expanduser().resolve() / "bin"
    result = build_generated_cpp_archive(
        generated_directory=generated_directory,
        repo_root=REPO_ROOT,
        nlohmann_include=nlohmann_include,
        cache_root=cache_root,
        toolchain=NativeToolchain(
            compiler=llvm_bin / "clang-cl.exe",
            archiver=llvm_bin / "llvm-lib.exe",
        ),
        jobs=jobs,
    )
    archive_state = {
        "status": "ready",
        "backend": result["backend"],
        "archive": result["archive"],
        "archive_sha256": result["archive_sha256"],
        "archive_cache_key": result["archive_cache_key"],
        "toolchain_identity_sha256": result["toolchain_identity_sha256"],
        "program_sha256": program_hash,
        "audit": dict(audit.summary),
    }
    translation["native_object_archive"] = archive_state
    atomic_write_json(prepared.directory / "forge-state.json", prepared.state)
    return {**result, "audit": dict(audit.summary)}


def build_private_direct_aot(
    prepared_directory: Path,
    *,
    llc: Path,
    linker: Path,
    cache_root: Path,
    shard_count: int = 128,
    jobs: int = 8,
) -> dict[str, Any]:
    prepared = load_prepared_content(
        prepared_directory, required_inputs=("code", "exheader")
    )
    translation = prepared.state.get("translation")
    if not isinstance(translation, dict) or translation.get("status") != "ir_ready":
        raise ForgeError("structural AOT IR must be prepared before direct AOT")
    if translation.get("backend") != "whole_aot_structural_ir_v1":
        raise ForgeError("prepared structural AOT IR backend is unsupported")
    ir_directory = Path(str(translation.get("directory", ""))).resolve()
    program = Path(str(translation.get("program", ""))).resolve()
    if (
        not ir_directory.is_dir()
        or ir_directory.is_symlink()
        or program.parent != ir_directory
        or program.name != "aot_program.json"
        or not program.is_file()
        or program.is_symlink()
        or sha256_file(program)
        != str(translation.get("program_sha256", "")).lower()
    ):
        raise ForgeError("prepared structural AOT IR changed before direct AOT")

    frontend = load_default_frontend()
    try:
        result = build_direct_aot_plugin(
            program_path=program,
            selection_path=frontend.selection,
            code_path=prepared.inputs["code"].path,
            cache_root=cache_root,
            toolchain=DirectAotToolchain(llc=llc, linker=linker),
            shard_count=shard_count,
            jobs=jobs,
        )
    except ValueError as exc:
        raise ForgeError(f"direct AOT compilation failed: {exc}") from exc
    translation["direct_aot"] = {
        "status": "ready",
        "backend": result["backend"],
        "profile": result["profile"],
        "plugin": result["plugin"],
        "plugin_sha256": result["plugin_sha256"],
        "cache_key": result["cache_key"],
        "translator_identity_sha256": result[
            "translator_identity_sha256"
        ],
        "functions": result["functions"],
        "dispatch_entries": result["dispatch_entries"],
    }
    atomic_write_json(prepared.directory / "forge-state.json", prepared.state)
    return result


def build_private_whole_aot(
    prepared_directory: Path,
    *,
    compiler: Path,
    archiver: Path,
    support_library: Path,
    nlohmann_include: Path,
    cache_root: Path,
    shard_count: int = 256,
    jobs: int = 8,
    sysroot: Path | None = None,
    target_triple: str = WINDOWS.target,
) -> dict[str, Any]:
    """Build the direct generated-C++ plugin consumed by the mature runtime."""
    platform = for_target(target_triple)

    prepared = load_prepared_content(
        prepared_directory, required_inputs=("code", "exheader")
    )
    translation = prepared.state.get("translation")
    if not isinstance(translation, dict) or translation.get("status") != "ir_ready":
        raise ForgeError("structural AOT IR must be prepared before whole-AOT")
    if translation.get("backend") != "whole_aot_structural_ir_v1":
        raise ForgeError("prepared structural AOT IR backend is unsupported")
    ir_directory = Path(str(translation.get("directory", ""))).resolve()
    program = Path(str(translation.get("program", ""))).resolve()
    if (
        not ir_directory.is_dir()
        or ir_directory.is_symlink()
        or program.parent != ir_directory
        or program.name != "aot_program.json"
        or not program.is_file()
        or program.is_symlink()
        or sha256_file(program)
        != str(translation.get("program_sha256", "")).lower()
    ):
        raise ForgeError("prepared structural AOT IR changed before whole-AOT")

    frontend = load_default_frontend()
    try:
        result = build_whole_aot_plugin(
            program_path=program,
            selection_path=frontend.selection,
            code_path=prepared.inputs["code"].path,
            cache_root=cache_root,
            toolchain=WholeAotPluginToolchain(
                compiler=compiler,
                archiver=archiver,
                support_library=support_library,
                nlohmann_include=nlohmann_include,
                sysroot=sysroot,
                target_triple=platform.target,
                profile=platform.profile,
            ),
            shard_count=shard_count,
            jobs=jobs,
        )
    except ValueError as exc:
        raise ForgeError(f"whole-AOT compilation failed: {exc}") from exc
    translation["whole_aot_plugin"] = {
        key: result[key]
        for key in (
            "status",
            "backend",
            "profile",
            "plugin",
            "plugin_sha256",
            "cache_key",
            "translator_identity_sha256",
            "functions",
            "shards",
            "objects_compiled",
            "objects_reused",
        )
    }
    atomic_write_json(prepared.directory / "forge-state.json", prepared.state)
    return result


def publish_private_runtime(
    prepared_directory: Path,
    *,
    plugin: Path,
    runtime_plugin: Path,
    launch_profile: Path,
    data_root: Path,
    topscreen_texture_pack: Path | None = None,
    package_root: Path | None = None,
) -> dict[str, Any]:
    """Publish verified files; multi-file activation is not yet transactional."""

    prepared = load_prepared_content(prepared_directory, required_inputs=())
    process_manifest = (prepared.directory / "process-manifest.json").resolve()
    if not process_manifest.is_file() or process_manifest.is_symlink():
        raise ForgeError("prepared title process manifest is unavailable")
    source_plugin = plugin.expanduser().resolve()
    if not source_plugin.is_file() or source_plugin.is_symlink():
        raise ForgeError(f"private whole-AOT plugin is invalid: {source_plugin}")

    plugin_hash = sha256_file(source_plugin)
    installation = runtime_plugin.expanduser().resolve().parent
    package = (package_root or installation).expanduser().resolve()
    platform = host_platform()
    destination = installation / "private-plugins" / plugin_hash / platform.title_module
    profile_path = launch_profile.expanduser().resolve()
    private_root = data_root.expanduser().resolve()
    product_receipt = query_product(package / platform.runtime)
    destination.parent.mkdir(parents=True, exist_ok=True)
    profile_path.parent.mkdir(parents=True, exist_ok=True)
    private_root.mkdir(parents=True, exist_ok=True)
    config_root = private_root / "config"
    config_root.mkdir(parents=True, exist_ok=True)
    topscreen_config = config_root / "topscreen_ui.json"
    runtime_config = config_root / "TriAevum.json"
    # Validate both existing user files before changing any installed title file.
    for user_config in (topscreen_config, runtime_config):
        if user_config.exists():
            load_json_object(user_config)
    if not topscreen_config.is_file():
        source_config = distribution_path("config/topscreen_ui.example.json")
        try:
            payload = load_json_object(source_config)
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            raise ForgeError(f"default TopScreen configuration is invalid: {exc}") from exc
        atomic_write_json(topscreen_config, payload)

    ensure_runtime_config(runtime_config, product_receipt["product"])
    savedata = private_root / "savedata"
    savedata.mkdir(parents=True, exist_ok=True)
    output = private_root / "runtime-state.json"
    profile = {
        "format": "oot3d_native_game_launch_profile_v1",
        "arguments": [
            "--title-plugin",
            str(destination),
            "--a32-process-manifest",
            str(process_manifest),
            "--resource-root",
            str((package / "resources").resolve()),
            "--renderer",
            "nri",
            "--ui-profile",
            "topscreen",
            "--config",
            str(runtime_config),
            "--topscreen-config",
            str(topscreen_config),
            "--gameplay-timing",
            "native30_interpolated",
            "--presentation-rate",
            "60",
            "--save-data",
            str(savedata),
            "--output",
            str(output),
            "--width",
            "1280",
            "--height",
            "720",
        ],
    }
    if topscreen_texture_pack is not None:
        if not topscreen_texture_pack.is_file():
            raise ForgeError("TopScreen texture pack is missing before activation")
        profile["arguments"].extend(("--topscreen-texture-overrides", str(topscreen_texture_pack.resolve())))
    context = InstallationContext(installation)
    path_options = {"--title-plugin", "--a32-process-manifest", "--resource-root",
                    "--config", "--topscreen-config", "--save-data", "--output",
                    "--topscreen-texture-overrides"}
    scopes = {}
    for index, argument in enumerate(profile["arguments"][:-1]):
        if argument in path_options:
            target = Path(profile["arguments"][index + 1])
            profile["arguments"][index + 1] = context.profile_argument(target, profile_path)
            scopes[argument] = context.scope(target, profile_path.parent)
    profile["path_scopes"] = scopes
    if not destination.is_file() or sha256_file(destination) != plugin_hash:
        temporary = destination.with_name(
            f".{destination.name}.{os.getpid()}.installing"
        )
        try:
            shutil.copy2(source_plugin, temporary)
            if sha256_file(temporary) != plugin_hash:
                raise ForgeError("private whole-AOT plugin copy verification failed")
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)
    # Query the exact immutable generation in a short-lived process before the
    # single launch-profile replacement makes it visible to direct launches.
    query_product(package / platform.runtime, plugin=destination)
    atomic_write_json(profile_path, profile)
    prepared.state["runtime"] = {
        "status": "ready",
        "backend": "generated_cpp_whole_aot_plugin_v2",
        "target": platform.target,
        "runtime_sha256": product_receipt["runtime_sha256"],
        "plugin": context.reference(destination, prepared.directory),
        "plugin_scope": context.scope(destination, prepared.directory),
        "plugin_sha256": plugin_hash,
        "launch_profile": context.reference(profile_path, prepared.directory),
        "launch_profile_scope": context.scope(profile_path, prepared.directory),
        "launch_profile_sha256": sha256_file(profile_path),
        "ui_profile": "topscreen",
        "renderer": "nri",
    }
    if topscreen_texture_pack is not None:
        prepared.state["runtime"]["topscreen_textures"] = {
            "path": context.reference(topscreen_texture_pack, prepared.directory),
            "sha256": sha256_file(topscreen_texture_pack),
        }
    atomic_write_json(prepared.directory / "forge-state.json", prepared.state)
    return {
        "status": "ready",
        "plugin": str(destination),
        "plugin_sha256": plugin_hash,
        "launch_profile": str(profile_path),
        "topscreen_config": str(topscreen_config),
    }


def package_private_module(
    prepared_directory: Path,
    native_image: Path,
    *,
    title_aot_image: Path | None = None,
    target_triple: str,
    translator_identity: str,
    active_title_state: Path | None = None,
) -> dict[str, Any]:
    native_image = native_image.expanduser().resolve()
    if not native_image.is_file() or native_image.is_symlink():
        raise ForgeError(f"native module image is invalid: {native_image}")
    prepared = load_prepared_content(prepared_directory, required_inputs=("code",))
    title_root = prepared.directory
    index = prepared.index
    state = prepared.state
    state_path = title_root / "forge-state.json"
    regions = index.get("physical_memory_regions")
    recipe = index.get("recipe")
    if not isinstance(regions, list):
        raise ForgeError("content.tap is missing module identity metadata")
    source_identity = prepared.inputs["code"].sha256
    if not isinstance(recipe, str) or not recipe:
        raise ForgeError("content.tap recipe is invalid")
    try:
        memory_regions = tuple(
            (
                _manifest_integer(region["physical_base"], "physical base"),
                _manifest_integer(region["guest_base"], "guest base"),
                _manifest_integer(region["bytes"], "physical region size"),
            )
            for region in regions
            if isinstance(region, dict)
        )
    except KeyError as exc:
        raise ForgeError("content.tap physical-memory region is incomplete") from exc
    if len(memory_regions) != len(regions):
        raise ForgeError("content.tap physical-memory region is malformed")

    try:
        module, metadata = build_tam(
            native_image,
            title_aot_image=title_aot_image,
            recipe=recipe,
            target_triple=target_triple,
            source_identity=source_identity,
            translator_identity=translator_identity,
            required_services=(
                PICA_SERVICE_V1,
                AUDIO_SERVICE_V1,
                FILESYSTEM_SERVICE_V1,
                INPUT_SERVICE_V1,
            ),
            physical_memory_regions=memory_regions,
        )
    except ValueError as exc:
        raise ForgeError(f"cannot package private game module: {exc}") from exc
    native_hash = str(metadata["native_image"]["sha256"])
    title_aot_hash = str(
        metadata.get("title_aot_image", {}).get("sha256", "")
    )
    module_cache_key = hashlib.sha256(
        (
            "triaevum-private-module-v1\0"
            + target_triple
            + "\0"
            + str(metadata["translator_identity_sha256"])
            + "\0"
            + native_hash
            + "\0"
            + title_aot_hash
        ).encode("ascii")
    ).hexdigest()
    # Keep paths comfortably below legacy Win32 limits even when the title
    # cache root already contains a recipe ID and full content hash. The full
    # cache key and all constituent identities remain in forge-state.json.
    relative_module = Path("modules") / f"{module_cache_key[:32]}.tam"
    module_path = title_root / relative_module
    module_hash = hashlib.sha256(module).hexdigest()
    module_state = {
        "status": "ready",
        "container": relative_module.as_posix(),
        "bytes": len(module),
        "sha256": module_hash,
        "source_identity_sha256": source_identity,
        "translator_identity_sha256": metadata["translator_identity_sha256"],
        "native_image_sha256": native_hash,
        "title_aot_image_sha256": title_aot_hash or None,
        "cache_key": module_cache_key,
        "target_triple": target_triple,
        "required_services": sorted(
            (
                PICA_SERVICE_V1,
                AUDIO_SERVICE_V1,
                FILESYSTEM_SERVICE_V1,
                INPUT_SERVICE_V1,
            )
        ),
    }

    status = "packaged"
    if module_path.exists():
        if (
            module_path.stat().st_size != len(module)
            or sha256_file(module_path) != module_hash
        ):
            raise ForgeError("existing private module cache entry is corrupted")
        status = "reused"
    else:
        try:
            atomic_write_bytes(module_path, module)
        except OSError as exc:
            raise ForgeError(f"cannot write private game module: {exc}") from exc
    state["module"] = module_state
    atomic_write_json(state_path, state)
    result = {
        "status": status,
        "directory": str(title_root),
        "module": str(module_path),
        "module_sha256": module_hash,
        "native_image_sha256": native_hash,
        "title_aot_image_sha256": title_aot_hash or None,
    }
    if active_title_state is not None:
        activation = activate_prepared_title(
            title_root, active_title_state=active_title_state
        )
        result["active_title"] = activation["active_title"]
    return result


def build_private_title(
    prepared_directory: Path,
    *,
    native_image: Path,
    compiler: Path,
    archiver: Path,
    support_library: Path,
    nlohmann_include: Path,
    cache_root: Path,
    target_triple: str,
    active_title_state: Path,
    runtime_plugin: Path,
    launch_profile: Path,
    data_root: Path,
    seed_program: Path | None = None,
    shard_count: int = 256,
    jobs: int = 8,
    sysroot: Path | None = None,
) -> dict[str, Any]:
    prepared = load_prepared_content(
        prepared_directory, required_inputs=("code", "exheader")
    )
    translation = prepared.state.get("translation")
    if not isinstance(translation, dict) or translation.get("status") != "ir_ready":
        prepare_private_aot_ir(
            prepared.directory,
            cache_root=cache_root / "structural-ir",
            seed_program=seed_program,
        )
    whole_aot = build_private_whole_aot(
        prepared.directory,
        compiler=compiler,
        archiver=archiver,
        support_library=support_library,
        nlohmann_include=nlohmann_include,
        cache_root=cache_root / "whole-aot-v2",
        sysroot=sysroot,
        target_triple=target_triple,
        shard_count=shard_count,
        jobs=jobs,
    )
    with activation_transaction(runtime_plugin.resolve().parent, [
        runtime_plugin, launch_profile, active_title_state,
        prepared.directory / "forge-state.json",
        data_root / "config" / "TriAevum.json",
        data_root / "config" / "topscreen_ui.json",
    ]):
        packaged = package_private_module(
            prepared.directory,
            native_image,
            title_aot_image=Path(str(whole_aot["plugin"])),
            target_triple=target_triple,
            translator_identity=str(whole_aot["translator_identity_sha256"]),
        )
        runtime = publish_private_runtime(
            prepared.directory,
            plugin=Path(str(whole_aot["plugin"])),
            runtime_plugin=runtime_plugin,
            launch_profile=launch_profile,
            data_root=data_root,
        )
        activation = activate_prepared_title(
            prepared.directory, active_title_state=active_title_state
        )
    packaged["active_title"] = activation["active_title"]
    return {
        "status": "ready",
        "whole_aot": whole_aot,
        "package": packaged,
        "runtime": runtime,
    }


def _add_input_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--recipe", required=True)
    parser.add_argument("--recipes", type=Path, default=DEFAULT_RECIPES)
    parser.add_argument("--code", type=Path, required=True)
    parser.add_argument("--exheader", type=Path, required=True)
    parser.add_argument("--romfs", type=Path, required=True)
    parser.add_argument("--topscreen-archive", type=Path)
    parser.add_argument("--output-root", type=Path, default=default_output_root())
    parser.add_argument(
        "--rehash",
        action="store_true",
        help="ignore the local file-identity hash cache",
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    probe_parser = subparsers.add_parser("verify-toolchain", help="Compile and execute the synthetic native ABI probe")
    for name in ("compiler", "archiver", "support", "include", "sysroot", "output"):
        probe_parser.add_argument("--" + name, type=Path, required=True)
    migration_parser = subparsers.add_parser("migrate-paths", help="make installed paths portable without recompiling or modifying saves")
    migration_parser.add_argument("--installation", type=Path, required=True)
    migration_parser.add_argument("--prepared-directory", type=Path, required=True)
    migration_parser.add_argument("--data-root", type=Path, required=True)
    verify_parser = subparsers.add_parser("verify", help="verify private inputs")
    prepare_parser = subparsers.add_parser(
        "prepare", help="verify inputs and create a private content index"
    )
    _add_input_arguments(verify_parser)
    _add_input_arguments(prepare_parser)
    package_parser = subparsers.add_parser(
        "package-module", help="package a locally generated private title module"
    )
    package_parser.add_argument("--prepared-directory", type=Path, required=True)
    package_parser.add_argument("--native-image", type=Path, required=True)
    package_parser.add_argument("--title-aot-image", type=Path)
    package_parser.add_argument("--target-triple", required=True)
    package_parser.add_argument("--translator-identity", required=True)
    package_parser.add_argument(
        "--active-title-state",
        type=Path,
        default=default_active_title_state_path(),
        help="publish this verified module as the title used by TriAevum",
    )
    activate_parser = subparsers.add_parser(
        "activate", help="select an already prepared private title for play"
    )
    activate_parser.add_argument("--prepared-directory", type=Path, required=True)
    activate_parser.add_argument(
        "--active-title-state",
        type=Path,
        default=default_active_title_state_path(),
    )
    ir_parser = subparsers.add_parser(
        "prepare-aot-ir",
        help="prepare the verified structural whole-AOT IR from private inputs",
    )
    ir_parser.add_argument("--prepared-directory", type=Path, required=True)
    ir_parser.add_argument(
        "--cache-root", type=Path, default=default_translation_cache_root()
    )
    ir_parser.add_argument(
        "--seed-program",
        type=Path,
        help="reuse a local structural IR only after a complete product audit",
    )
    object_parser = subparsers.add_parser(
        "build-aot-fallback",
        help="build the audited generated-C++ development fallback incrementally",
    )
    object_parser.add_argument("--prepared-directory", type=Path, required=True)
    object_parser.add_argument("--generated-directory", type=Path, required=True)
    object_parser.add_argument("--llvm-root", type=Path, required=True)
    object_parser.add_argument("--nlohmann-include", type=Path, required=True)
    object_parser.add_argument(
        "--cache-root",
        type=Path,
        default=default_translation_cache_root() / "native-objects",
    )
    object_parser.add_argument("--jobs", type=int, default=4)
    direct_parser = subparsers.add_parser(
        "build-aot", help="compile verified structural IR to a private AOT plugin"
    )
    direct_parser.add_argument("--prepared-directory", type=Path, required=True)
    direct_parser.add_argument(
        "--llc", type=Path, default=default_forge_support_path("llc.exe")
    )
    direct_parser.add_argument(
        "--linker", type=Path, default=default_forge_support_path("lld-link.exe")
    )
    direct_parser.add_argument(
        "--cache-root",
        type=Path,
        default=default_translation_cache_root() / "direct-aot",
    )
    direct_parser.add_argument("--shards", type=int, default=128)
    direct_parser.add_argument("--jobs", type=int, default=8)
    title_parser = subparsers.add_parser(
        "build-title",
        help="compile, package, and activate a prepared private title",
    )
    title_parser.add_argument("--prepared-directory", type=Path, required=True)
    title_parser.add_argument(
        "--native-image",
        type=Path,
        default=default_forge_support_path(Path(host_platform().native_module).name),
    )
    title_parser.add_argument(
        "--compiler",
        type=Path,
        default=default_forge_support_path(host_platform().compiler),
    )
    title_parser.add_argument(
        "--archiver",
        type=Path,
        default=default_forge_support_path(host_platform().archiver),
    )
    title_parser.add_argument(
        "--support-library",
        type=Path,
        default=default_forge_support_path(
            host_platform().support_library
        ),
    )
    title_parser.add_argument(
        "--nlohmann-include",
        type=Path,
        default=default_forge_include_path(),
    )
    title_parser.add_argument(
        "--cache-root", type=Path, default=default_translation_cache_root()
    )
    title_parser.add_argument("--seed-program", type=Path)
    title_parser.add_argument("--shards", type=int, default=256)
    title_parser.add_argument("--jobs", type=int, default=8)
    title_parser.add_argument("--sysroot", type=Path,
                              help="Use a verified private Windows sysroot instead of host discovery")
    title_parser.add_argument(
        "--target-triple", default=host_platform().target
    )
    title_parser.add_argument(
        "--active-title-state",
        type=Path,
        default=default_active_title_state_path(),
    )
    title_parser.add_argument(
        "--runtime-plugin",
        type=Path,
        default=default_runtime_plugin_path(),
    )
    title_parser.add_argument(
        "--launch-profile",
        type=Path,
        default=default_runtime_launch_profile_path(),
    )
    title_parser.add_argument(
        "--data-root",
        type=Path,
        default=default_output_root().parent,
    )
    doctor_parser = subparsers.add_parser("doctor", help="report Forge capabilities")
    doctor_parser.add_argument("--recipes", type=Path, default=DEFAULT_RECIPES)
    doctor_parser.add_argument("--inventory", action="store_true",
                               help="List bundled components without claiming a working toolchain")
    args = parser.parse_args(argv)

    try:
        if args.command == "verify-toolchain":
            try:
                from .validate_whole_aot_toolchain import validate
            except ImportError:
                from validate_whole_aot_toolchain import validate
            result = validate(args.compiler, args.archiver, args.support, args.include, args.sysroot, args.output)
        elif args.command == "doctor":
            payload = load_json_object(args.recipes)
            recipes = payload.get("recipes", [])
            result = {
                "status": "inventory",
                "tool_version": TOOL_VERSION,
                "recipes": [
                    item.get("id") for item in recipes if isinstance(item, dict)
                ],
                "content_index": "available",
                "tam_container": "available",
                "private_module_packaging": "available",
                "structural_aot_ir": "available",
                "incremental_cpp_object_fallback": "available_for_development",
                "direct_game_module_backend": "generated_cpp_whole_aot_plugin_v2",
                "direct_aot_tools": {
                    "whole_aot_builder_available": distribution_path(
                        "tools/triaevum_release/whole_aot_plugin_backend.py"
                    ).is_file(),
                    "llc": str(default_forge_support_path("llc.exe")),
                    "llc_available": default_forge_support_path(
                        "llc.exe"
                    ).is_file(),
                    "linker": str(default_forge_support_path("lld-link.exe")),
                    "linker_available": default_forge_support_path(
                        "lld-link.exe"
                    ).is_file(),
                    "game_module": str(
                        default_forge_support_path("oot3d_game_module.dll")
                    ),
                    "game_module_available": default_forge_support_path(
                        "oot3d_game_module.dll"
                    ).is_file(),
                    "compiler": str(
                        default_forge_support_path("clang-cl.exe")
                    ),
                    "compiler_available": default_forge_support_path(
                        "clang-cl.exe"
                    ).is_file(),
                    "archiver": str(
                        default_forge_support_path("llvm-lib.exe")
                    ),
                    "archiver_available": default_forge_support_path(
                        "llvm-lib.exe"
                    ).is_file(),
                    "whole_aot_support_available": default_forge_support_path(
                        "triaevum_title_whole_aot_support.lib"
                    ).is_file(),
                    "compiler_headers": str(default_forge_include_path()),
                    "compiler_headers_available": (
                        default_forge_include_path() / "nlohmann/json_fwd.hpp"
                    ).is_file(),
                },
                "runtime_module_loader": "integrated",
                "runtime_host_services": [
                    "pica_v1",
                    "audio_v1",
                    "filesystem_v1",
                    "input_v1",
                ],
            }
            if not args.inventory:
                result["toolchain_probe"] = probe_toolchain(
                    default_forge_support_path("clang-cl.exe"),
                    default_forge_support_path("llvm-lib.exe"),
                    default_forge_support_path("triaevum_title_whole_aot_support.lib"),
                    default_forge_include_path(),
                )
                result["status"] = "ok"
        elif args.command == "package-module":
            result = package_private_module(
                args.prepared_directory,
                args.native_image,
                title_aot_image=args.title_aot_image,
                target_triple=args.target_triple,
                translator_identity=args.translator_identity,
                active_title_state=args.active_title_state,
            )
        elif args.command == "migrate-paths":
            try:
                from .migrate_installation import migrate_installation
            except ImportError:
                from migrate_installation import migrate_installation
            result = migrate_installation(args.installation, args.prepared_directory, args.data_root)
        elif args.command == "activate":
            result = activate_prepared_title(
                args.prepared_directory,
                active_title_state=args.active_title_state,
            )
        elif args.command == "prepare-aot-ir":
            result = prepare_private_aot_ir(
                args.prepared_directory,
                cache_root=args.cache_root,
                seed_program=args.seed_program,
            )
        elif args.command == "build-aot-fallback":
            result = build_private_aot_fallback(
                args.prepared_directory,
                generated_directory=args.generated_directory,
                llvm_root=args.llvm_root,
                nlohmann_include=args.nlohmann_include,
                cache_root=args.cache_root,
                jobs=args.jobs,
            )
        elif args.command == "build-aot":
            result = build_private_direct_aot(
                args.prepared_directory,
                llc=args.llc,
                linker=args.linker,
                cache_root=args.cache_root,
                shard_count=args.shards,
                jobs=args.jobs,
            )
        elif args.command == "build-title":
            result = build_private_title(
                args.prepared_directory,
                native_image=args.native_image,
                compiler=args.compiler,
                archiver=args.archiver,
                support_library=args.support_library,
                nlohmann_include=args.nlohmann_include,
                cache_root=args.cache_root,
                target_triple=args.target_triple,
                active_title_state=args.active_title_state,
                runtime_plugin=args.runtime_plugin,
                launch_profile=args.launch_profile,
                data_root=args.data_root,
                seed_program=args.seed_program,
                shard_count=args.shards,
                jobs=args.jobs,
                sysroot=args.sysroot,
            )
        else:
            recipe = load_recipe(args.recipes, args.recipe)
            output_root = args.output_root.expanduser().resolve()
            cache = HashCache(output_root / ".hash-cache.json", enabled=not args.rehash)
            verified, verified_mod = verify_sources(
                recipe,
                code_path=args.code,
                exheader_path=args.exheader,
                romfs_path=args.romfs,
                cache=cache,
                topscreen_archive=args.topscreen_archive,
            )
            if args.command == "verify":
                result = {
                    "status": "verified",
                    "recipe": recipe["id"],
                    "inputs": {
                        kind: value.as_json() for kind, value in verified.items()
                    },
                }
            else:
                result = prepare_content(
                    recipe,
                    verified,
                    output_root=output_root,
                    verified_mod=verified_mod,
                )
    except (ForgeError, ImportError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(json.dumps({"status": "error", "error": str(exc)}, indent=2))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
