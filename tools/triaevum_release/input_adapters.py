"""Offline input normalization between ROM extraction and title activation.

An adapter is revision-owned, hash-bound on both sides, and cannot change the
runtime or its hooks. Original and execution identities remain distinct.
"""

from __future__ import annotations

from dataclasses import replace
from pathlib import Path
import shutil
import time

try:
    from .common import atomic_write_json, load_json_object, sha256_file
    from .ctr_rom import ExtractedFile, ExtractedTitleInputs
    from .input_copy_adapter import normalize_file, validate_program
    from .oot3d_region_assets import ALGORITHM, normalize_romfs
    from .precompiled_titles import checked_file
except ImportError:
    from common import atomic_write_json, load_json_object, sha256_file
    from ctr_rom import ExtractedFile, ExtractedTitleInputs
    from input_copy_adapter import normalize_file, validate_program
    from oot3d_region_assets import ALGORITHM, normalize_romfs
    from precompiled_titles import checked_file


FORMAT = "triaevum_revision_input_adapter_v1"
KINDS = ("code", "exheader", "romfs")
RECEIPT = "input-adaptation.json"


def import_contract(recipe: dict) -> dict:
    adapter = recipe.get("input_adapter")
    if adapter is None:
        return recipe.get("inputs", {})
    if not isinstance(adapter, dict) or adapter.get("format") != FORMAT:
        raise ValueError("Unsupported revision input adapter")
    return adapter.get("source_inputs", {})


def validate_adapter(root: Path, recipe: dict) -> dict | None:
    adapter = recipe.get("input_adapter")
    if adapter is None:
        return None
    if (not isinstance(adapter, dict)
            or set(adapter) != {"format", "source_inputs", "code_copies", "resource_algorithm"}
            or adapter["format"] != FORMAT or adapter["resource_algorithm"] != ALGORITHM):
        raise ValueError("Unsupported revision input adapter")
    for contracts in (adapter["source_inputs"], recipe.get("inputs")):
        if not isinstance(contracts, dict) or set(contracts) != set(KINDS):
            raise ValueError("Adapter requires all three input identities")
        for record in contracts.values():
            if not isinstance(record, dict) or set(record) != {"bytes", "sha256"}:
                raise ValueError("Invalid adapter input identity")
            size, digest = record["bytes"], record["sha256"]
            if (type(size) is not int or size <= 0 or not isinstance(digest, str)
                    or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest)):
                raise ValueError("Invalid adapter input identity")
    if adapter["source_inputs"]["exheader"] != recipe["inputs"]["exheader"]:
        raise ValueError("This adapter must preserve the original ExHeader")
    record = adapter["code_copies"]
    if (not isinstance(record, dict) or set(record) != {"path", "bytes", "sha256"}
            or type(record["bytes"]) is not int or not 0 < record["bytes"] <= 32*1024*1024):
        raise ValueError("Invalid code COPY artifact")
    program = load_json_object(checked_file(root, record))
    validate_program(program)
    if (program["source"] != adapter["source_inputs"]["code"]
            or program["canonical"] != recipe["inputs"]["code"]):
        raise ValueError("Code COPY artifact does not match revision contracts")
    return program


def adapt_extracted_inputs(extracted: ExtractedTitleInputs, recipe: dict, *, root: Path,
                           output: Path) -> tuple[ExtractedTitleInputs, dict | None]:
    """Build new, verified inputs; never modify the ROM or the active installation."""
    program = validate_adapter(root, recipe)
    if program is None:
        return extracted, None
    started = time.perf_counter()
    adapter = recipe["input_adapter"]
    original = {}
    for kind, item in extracted.by_kind().items():
        if item.path.is_symlink() or not item.path.is_file():
            raise ValueError(f"Adapter {kind} source is not a regular file")
        original[kind] = {"bytes": item.path.stat().st_size, "sha256": sha256_file(item.path)}
    if original != adapter["source_inputs"]:
        try:
            from .data_compatibility import verify
        except ImportError:
            from data_compatibility import verify
        if recipe.get('data_compatibility') is None:
            raise ValueError("Adapter source identity mismatch")
        verify(recipe, {kind: item.path for kind, item in extracted.by_kind().items()}, phase='source')
    output = output.resolve()
    if any(item.path.resolve().is_relative_to(output) for item in extracted.by_kind().values()):
        raise ValueError("Adapter output must not contain the source inputs")
    output.mkdir(parents=True, exist_ok=False)
    try:
        normalize_file(extracted.code.path, output / "code.bin", program)
        shutil.copyfile(extracted.exheader.path, output / "exheader.bin")
        resources = normalize_romfs(extracted.romfs.path, output / "romfs.bin")
        files = {}
        for kind in KINDS:
            path = output / f"{kind}.bin"
            files[kind] = ExtractedFile(path, path.stat().st_size, sha256_file(path))
        actual = {kind: {"bytes": item.bytes, "sha256": item.sha256} for kind, item in files.items()}
        if actual != recipe["inputs"]:
            try:
                from .data_compatibility import verify
            except ImportError:
                from data_compatibility import verify
            if recipe.get('data_compatibility') is None:
                raise ValueError("Normalized input identity mismatch; title was not activated")
            verify(recipe, {kind: item.path for kind, item in files.items()}, phase='execution')
        receipt = {"format": "triaevum_input_adaptation_receipt_v1", "recipe": recipe["id"],
                   "program_id": f"{extracted.program_id:016X}",
                   "source_inputs": original, "execution_inputs": actual,
                   "adapter": adapter, "resources": resources, "objects_compiled": 0}
        atomic_write_json(output / RECEIPT, receipt)
    except BaseException:
        shutil.rmtree(output)
        raise
    return replace(extracted, **files), {**receipt, "wall_seconds": time.perf_counter()-started}
