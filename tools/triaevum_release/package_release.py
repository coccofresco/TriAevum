"""Build a TriAevum public package from an explicit file-role allowlist."""

from __future__ import annotations

import argparse
import json
import shutil
import tempfile
from pathlib import Path
from typing import Any, Sequence

try:
    from .audit_release import DEFAULT_POLICY, audit_release
    from .common import (
        atomic_write_json,
        load_json_object,
        normalize_relative_path,
        sha256_file,
    )
    from .readiness import DEFAULT_READINESS, evaluate_readiness
    from .product_contract import query_product
except ImportError:
    from audit_release import DEFAULT_POLICY, audit_release
    from common import (
        atomic_write_json,
        load_json_object,
        normalize_relative_path,
        sha256_file,
    )
    from readiness import DEFAULT_READINESS, evaluate_readiness
    from product_contract import query_product


def package_release(
    layout_path: Path,
    output: Path,
    *,
    source_root: Path,
    version: str,
    source_commit: str,
    policy_path: Path = DEFAULT_POLICY,
    readiness_path: Path = DEFAULT_READINESS,
    enforce_readiness: bool = True,
    verify_runtime: bool = True,
) -> dict[str, Any]:
    if enforce_readiness:
        readiness = evaluate_readiness(readiness_path)
        if not readiness.ready:
            details = "\n".join(
                f"  - {item}" for item in readiness.blockers + readiness.errors
            )
            raise ValueError(
                "public release readiness gates are not complete:\n" + details
            )
    layout = load_json_object(layout_path)
    if layout.get("format") != "triaevum_public_release_layout_v1":
        raise ValueError("unsupported release layout format")
    items = layout.get("files")
    if not isinstance(items, list) or not items:
        raise ValueError("release layout has no files")

    source_root = source_root.resolve()
    output = output.resolve()
    if output.exists():
        raise ValueError(f"release output already exists: {output}")
    if output == Path(output.anchor):
        raise ValueError("refusing to package into a volume root")
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(
        tempfile.mkdtemp(prefix=f".{output.name}.staging-", dir=output.parent)
    ).resolve()
    if staging.parent != output.parent:
        raise RuntimeError("temporary package escaped its output parent")

    inventory: list[dict[str, Any]] = []
    destinations: set[str] = set()
    try:
        for index, item in enumerate(items):
            if not isinstance(item, dict):
                raise ValueError(f"layout files[{index}] is not an object")
            source_value = str(item.get("source", ""))
            source = Path(source_value)
            if not source.is_absolute():
                source = source_root / source
            source = source.resolve()
            if not source.is_file() or source.is_symlink():
                raise ValueError(f"release input is not a regular file: {source}")
            relative = normalize_relative_path(str(item.get("path", "")))
            if relative in destinations:
                raise ValueError(f"duplicate release destination: {relative}")
            destinations.add(relative)
            destination = staging / Path(relative)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            inventory.append(
                {
                    "path": relative,
                    "role": str(item.get("role", "")),
                    "bytes": destination.stat().st_size,
                    "sha256": sha256_file(destination),
                }
            )

        runtime_contract = None
        if verify_runtime:
            runtimes = [item["path"] for item in inventory if item["role"] == "runtime_executable"]
            if len(runtimes) != 1:
                raise ValueError("release layout must declare exactly one runtime_executable")
            runtime_contract = query_product(staging / runtimes[0], source_commit)
            if runtime_contract["product"].get("private_title_loaded") is not False:
                raise ValueError("public package contains a private title plugin, not the stub")
        precompiled = any(item["role"] == "precompiled_catalog" for item in inventory)
        manifest = {
            "format": "triaevum_public_release_manifest_v1",
            "release": {
                "name": "TriAevum",
                "version": version,
                "source_commit": source_commit,
                "distribution_model": "precompiled_title_rom_import_v1" if precompiled else "local_compile_v1",
                "contains_title_code": precompiled,
                "contains_title_content": False,
                "proprietary_sdk_included": False,
                "redistributable": True,
                "qualification": "release" if enforce_readiness else "candidate",
                "runtime_contract": runtime_contract,
            },
            "files": sorted(inventory, key=lambda item: item["path"]),
        }
        atomic_write_json(staging / "release-manifest.json", manifest)
        audit = audit_release(staging, policy_path=policy_path)
        if not audit.ok:
            details = "\n".join(f"  - {error}" for error in audit.errors)
            raise ValueError(f"staged public release failed audit:\n{details}")
        staging.replace(output)
        return {
            "status": "packaged",
            "output": str(output),
            "files": audit.summary["verified_files"],
            "bytes": audit.summary["bytes"],
        }
    except BaseException:
        if staging.exists() and staging.parent == output.parent:
            shutil.rmtree(staging)
        raise


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--policy", type=Path, default=DEFAULT_POLICY)
    parser.add_argument("--readiness", type=Path, default=DEFAULT_READINESS)
    parser.add_argument("--candidate", action="store_true",
                        help="Build a verified candidate without claiming release qualification")
    args = parser.parse_args(argv)
    try:
        result = package_release(
            args.layout,
            args.output,
            source_root=args.source_root,
            version=args.version,
            source_commit=args.source_commit,
            policy_path=args.policy,
            readiness_path=args.readiness,
            enforce_readiness=not args.candidate,
        )
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"TriAevum packaging failed: {exc}")
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
