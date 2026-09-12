"""Forge's optional, recipe-bound portable shader preparation stage.

This compiles GPU shader modules, never title C++ or an SDK. Device pipeline
prewarm is a separate renderer job, hosted by device_pipeline_preparation.py.
No seed is discovered by filename or silently downloaded.
"""

from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import tempfile
from pathlib import Path
from typing import Callable

try:
    from .common import atomic_write_bytes, atomic_write_json, load_json_object, sha256_file
    from .precompiled_titles import checked_file
    from .native_process import run_native, native_helper_environment
except ImportError:
    from common import atomic_write_bytes, atomic_write_json, load_json_object, sha256_file
    from precompiled_titles import checked_file
    from native_process import run_native, native_helper_environment


FORMAT = "triaevum_shader_preparation_v1"
RENDERER_FORMAT = "triaevum_renderer_shader_preparation_v1"
RENDERER_CONTRACT = "triaevum_renderer_shader_compiler_v1"


def _pack_header(path: Path, schema: int) -> int:
    size = path.stat().st_size
    if size < 24 or size > 512 * 1024 * 1024:
        raise ValueError("Invalid portable shader pack size")
    with path.open("rb") as stream:
        magic, version, actual_schema, count, reserved = struct.unpack("<8s4I", stream.read(24))
    if (magic != b"O3PSAOT\0" or version != 1 or actual_schema != schema
            or count == 0 or reserved != 0 or 24 + count * 56 > size):
        raise ValueError("Portable shader pack has incompatible header/schema")
    return count


def _run(command: list[str], root: Path) -> None:
    result = run_native(command, cwd=root,
                            env=native_helper_environment(command[0]),
                            capture_output=True, text=True, errors="replace", timeout=600,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    if result.returncode:
        raise ValueError("Shader preparation failed: " + (result.stderr or result.stdout)[-4000:])


def prepare_renderer_shader_cache(*, root: Path, data_root: Path, title: dict,
                                 cache_directory: Path,
                                 report: Callable[[str, str], None] = lambda *_: None) -> dict | None:
    # Renderer-owned sources are distributable independently of a private game
    # inventory. Retain compatibility with the original combined catalog.
    seed = title.get("renderer_shader_preparation", title.get("shader_preparation"))
    if seed is None:
        return None
    expected = RENDERER_CONTRACT if "renderer_shader_preparation" in title else FORMAT
    if not isinstance(seed, dict) or seed.get("format") != expected:
        raise ValueError("Unsupported shader preparation contract")
    if "compiler" not in seed:
        return None
    compiler = checked_file(root, seed["compiler"])
    dependencies = [checked_file(root, value) for value in seed.get("dependencies", [])]
    cache = cache_directory.resolve()
    receipts = data_root.resolve() / "shader-seeds" / "renderer-preparation"
    receipts.mkdir(parents=True, exist_ok=True)
    report("shaders", "Preparing renderer pass shaders (no game boot)...")
    with tempfile.TemporaryDirectory(prefix=".preparing-", dir=receipts) as temporary:
        manifest = Path(temporary) / "result.json"
        try:
            _run([str(compiler), "--prepare-renderer-cache", str(cache), "--manifest", str(manifest)], root)
            result = load_json_object(manifest)
            modules, hits, compiled = (result.get(key) for key in ("modules", "hits", "compiled"))
            if (result.get("format") != RENDERER_FORMAT or result.get("game_booted") is not False
                    or any(type(value) is not int or value < 0 for value in (modules, hits, compiled))
                    or not 0 < modules <= 256 or hits + compiled != modules
                    or result.get("compile_failed") != 0 or result.get("write_failed") != 0
                    or result.get("writes") != compiled
                    or not isinstance(result.get("cache_directory"), str)
                    or Path(result.get("cache_directory", "")).resolve() != cache):
                raise ValueError("Renderer shader preparation did not persist the requested cache")
            result["renderer_shader_preparation"] = "complete"
        except (OSError, ValueError, subprocess.TimeoutExpired) as error:
            # Optional acceleration, like device pipeline preparation. Invalid
            # catalog artifacts fail above; unsupported old tools remain playable.
            result = {"format": RENDERER_FORMAT, "renderer_shader_preparation": "failed", "error": str(error)}
        result.update(compiler_sha256=sha256_file(compiler),
                      dependency_sha256=[sha256_file(path) for path in dependencies])
        atomic_write_json(receipts / "latest.json", result)
        report("shaders", "Renderer shader preparation: " + result["renderer_shader_preparation"])
        return result


def prepare_shader_seed(*, root: Path, data_root: Path, title: dict,
                        report: Callable[[str, str], None] = lambda *_: None) -> Path | None:
    seed = title.get("shader_preparation")
    if seed is None:
        return None
    if not isinstance(seed, dict) or seed.get("format") != FORMAT:
        raise ValueError("Unsupported shader preparation contract")
    schema = seed.get("descriptor_schema_version")
    if type(schema) is not int or schema <= 0:
        raise ValueError("Shader preparation requires a descriptor schema")
    mode = seed.get("mode")
    if mode == "portable_pack":
        pack = checked_file(root, seed["pack"])
        _pack_header(pack, schema)
        artifacts = [pack]
    elif mode in ("citra_transferable", "source_inventories"):
        sources = []
        importer = None
        if mode == "citra_transferable":
            dialect = seed.get("dialect")
            if dialect not in ("citra-legacy-v1", "azahar-v1"):
                raise ValueError("Shader cache requires an explicit Citra/Azahar dialect")
            sources = [checked_file(root, value) for value in seed.get("caches", [])]
            if not sources:
                raise ValueError("Shader preparation has no transferable inputs")
            importer = checked_file(root, seed["importer"])
        compiler = checked_file(root, seed["compiler"])
        inventories = [checked_file(root, value) for value in seed.get("inventories", [])]
        if mode == "source_inventories" and not inventories:
            raise ValueError("Shader preparation has no source inventories")
        # Include dynamically linked compiler dependencies in the cache identity.
        dependencies = [checked_file(root, value) for value in seed.get("dependencies", [])]
        artifacts = [*sources, *([importer] if importer else []), compiler, *inventories, *dependencies]
    else:
        raise ValueError("Unsupported shader preparation mode")
    identity = {"contract": seed, "recipe": title["recipe"],
                "artifact_hashes": [sha256_file(path) for path in artifacts]}
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    directory = data_root.resolve() / "shader-seeds" / key
    pack_path = directory / "portable.o3ps"
    receipt_path = directory / "preparation.json"
    if receipt_path.is_file() and pack_path.is_file():
        try:
            receipt = load_json_object(receipt_path)
            if (receipt.get("identity") == identity
                    and receipt.get("pack_sha256") == sha256_file(pack_path)):
                _pack_header(pack_path, schema)
                report("shaders", "Reusing prepared portable shaders...")
                return pack_path
        except (OSError, ValueError):
            pass
    directory.mkdir(parents=True, exist_ok=True)
    report("shaders", "Preparing portable shaders (no game recompilation)...")
    with tempfile.TemporaryDirectory(prefix=".preparing-", dir=directory) as temporary:
        stage = Path(temporary)
        if mode == "citra_transferable":
            inventory = stage / "transferable.json"
            command = [str(importer), "--dialect", dialect, "--output", str(inventory)]
            for source in sources:
                command.extend(("--input", str(source)))
            _run(command, root)
            imported = load_json_object(inventory)
            if imported.get("transferable_import", {}).get("complete_import") is not True:
                raise ValueError("Transferable shader import was incomplete")
            inventories = [inventory, *inventories]
        if mode in ("citra_transferable", "source_inventories"):
            pack = stage / "compiled.o3ps"
            command = [str(compiler), "--pack", str(pack), "--manifest", str(stage / "compile.json")]
            for source in inventories:
                command.extend(("--inventory", str(source)))
            _run(command, root)
        count = _pack_header(pack, schema)
        atomic_write_bytes(pack_path, pack.read_bytes())
        atomic_write_json(receipt_path, {"format": FORMAT, "identity": identity,
            "pack_sha256": sha256_file(pack_path), "modules": count,
            "device_pipeline_prewarm": "not_performed", "game_coverage_proven": False})
    report("shaders", f"Prepared {count} portable shader modules.")
    return pack_path


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Prepare a catalogued shader seed without running the game")
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--data-root", required=True, type=Path)
    parser.add_argument("--title-record", required=True, type=Path)
    arguments = parser.parse_args()
    print(prepare_shader_seed(root=arguments.root, data_root=arguments.data_root,
                              title=load_json_object(arguments.title_record),
                              report=lambda _stage, message: print(message, flush=True)))
