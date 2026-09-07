"""Prepare content-addressed whole-AOT product sources outside daily builds."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
from typing import Sequence

from audit_whole_aot_product import DEFAULT_MANIFEST, audit_product
from generate_aot import operational_path
from generate_whole_aot import main as generate_program
from whole_aot_cpp import MANIFEST_NAME, generate


ROOT = Path(__file__).resolve().parent
REPO_ROOT = ROOT.parents[2]
DEFAULT_CACHE_ROOT = Path(
    os.environ.get(
        "OOT3D_WHOLE_AOT_PRODUCT_CACHE",
        str(REPO_ROOT.parent / "oot3d-whole-aot-product-cache"),
    )
)


def _hash_file(digest: hashlib._Hash, path: Path) -> None:
    digest.update(path.resolve().as_posix().encode("utf-8"))
    digest.update(b"\0")
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)


def product_cache_key(manifest_path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(b"oot3d_whole_aot_product_cache_v1\0")
    inputs = (
        manifest_path,
        ROOT / "whole_aot_functions.json",
        ROOT / "generate_aot.py",
        ROOT / "generate_whole_aot.py",
        ROOT / "whole_aot_program.py",
        ROOT / "whole_aot_cpp.py",
        ROOT / "whole_aot_optimization_ir.py",
        ROOT / "oot3d_aot_architectural_state.h",
        ROOT / "upstream/src/oot3d_pack/a32_cpp_aot.py",
        ROOT / "upstream/src/oot3d_pack/arm_decode.py",
        ROOT / "upstream/src/oot3d_pack/arm_ir.py",
        ROOT / "upstream/recomp/a32_runtime.h",
        ROOT / "upstream/recomp/a32_vfp_binary64.h",
        ROOT / "upstream/analysis/codebin_function_inventory.csv",
        ROOT / "upstream/analysis/codebin_callable_boundary_residue_audit_166.csv",
        REPO_ROOT / "tools/oot3d/native_game_runtime/oot3d_native_whole_aot_runtime.h",
        REPO_ROOT / "tools/oot3d/native_game_runtime/oot3d_native_a32_memory.h",
        ROOT / "oot3d_native_a32_vfp_ops.h",
        ROOT / "product_build/CMakeLists.txt",
        operational_path("oot3d_code_bin"),
        operational_path("oot3d_exheader"),
    )
    for path in inputs:
        _hash_file(digest, path)
    return digest.hexdigest()


def _write_if_different(path: Path, value: dict[str, object]) -> None:
    contents = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()
    if path.is_file() and path.read_bytes() == contents:
        return
    path.write_bytes(contents)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--cache-root", type=Path, default=DEFAULT_CACHE_ROOT)
    parser.add_argument("--program-only", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    key = product_cache_key(args.manifest)
    artifact_root = args.cache_root / key
    program_path = artifact_root / "aot_program.json"
    inventory_path = artifact_root / "inventory_with_process_entry.csv"
    generated_root = artifact_root / "cpp"
    generated_manifest = generated_root / MANIFEST_NAME
    artifact_root.mkdir(parents=True, exist_ok=True)

    result = generate_program(
        [
            "--output",
            str(program_path),
            "--inventory-output",
            str(inventory_path),
        ]
    )
    if result != 0:
        return result
    audit = audit_product(args.manifest, program_path)
    if not audit.ok:
        for error in audit.errors:
            print(f"whole-AOT product closure error: {error}")
        return 1

    if not args.program_only:
        contract = json.loads(args.manifest.read_text(encoding="utf-8"))
        codegen = contract["codegen"]
        generate(
            program_path,
            ROOT / "whole_aot_functions.json",
            operational_path("oot3d_code_bin"),
            generated_root,
            shard_count=int(codegen["shard_count"]),
            shard_strategy=str(codegen["shard_strategy"]),
        )
        audit = audit_product(
            args.manifest,
            program_path,
            generated_manifest_path=generated_manifest,
        )
        if not audit.ok:
            for error in audit.errors:
                print(f"whole-AOT generated artifact error: {error}")
            return 1

    metadata = {
        "format": "oot3d_whole_aot_product_artifacts_v1",
        "cache_key": key,
        "program": str(program_path.resolve()),
        "generated_directory": (
            str(generated_root.resolve()) if not args.program_only else None
        ),
        "generated_manifest": (
            str(generated_manifest.resolve()) if not args.program_only else None
        ),
        "selection": str((ROOT / "whole_aot_functions.json").resolve()),
        "audit": audit.summary,
    }
    _write_if_different(artifact_root / "product_artifacts.json", metadata)
    if args.json:
        print(json.dumps(metadata, indent=2))
    else:
        print(f"Whole-AOT product cache: {artifact_root}")
        print(f"  program: {program_path}")
        if not args.program_only:
            print(f"  generated C++: {generated_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
