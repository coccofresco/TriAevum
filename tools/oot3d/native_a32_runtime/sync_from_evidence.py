"""Copy the pinned Zelda3drecomp A32 runtime from an immutable evidence snapshot."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


FORMAT = "oot3d_pinned_a32_runtime_v1"
SOURCE_PATHS = (
    "analysis/codebin_callable_boundary_residue_audit_166.csv",
    "analysis/codebin_function_inventory.csv",
    "recomp/a32_core.cpp",
    "recomp/a32_core.h",
    "recomp/a32_core_alu.cpp",
    "recomp/a32_core_internal.h",
    "recomp/a32_core_memory.cpp",
    "recomp/a32_runtime.cpp",
    "recomp/a32_runtime.h",
    "recomp/a32_vfp_binary64.cpp",
    "recomp/a32_vfp_binary64.h",
    "recomp/a32_vfp_scalar.cpp",
    "recomp/a32_vfp_scalar.h",
    "recomp/a32_vfp_transport.cpp",
    "recomp/a32_vfp_transport.h",
    "src/oot3d_pack/a32_cpp_aot.py",
    "src/oot3d_pack/arm_decode.py",
    "src/oot3d_pack/arm_ir.py",
    "src/oot3d_pack/arm_lift.py",
    "src/oot3d_pack/arm_softfloat.py",
    "src/oot3d_pack/arm_softfloat64.py",
)


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def load_snapshot(snapshot: Path) -> tuple[dict[str, object], dict[str, dict[str, object]]]:
    manifest_path = snapshot / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != "oot3d_zelda3drecomp_evidence_snapshot_v1":
        raise ValueError(f"unsupported evidence snapshot: {manifest_path}")
    records = {
        str(record["path"]): record
        for record in manifest.get("files", [])
        if isinstance(record, dict) and record.get("path")
    }
    return manifest, records


def sync(snapshot: Path, output: Path) -> dict[str, object]:
    manifest, records = load_snapshot(snapshot)
    copied: list[dict[str, object]] = []
    aggregate = hashlib.sha256()
    aggregate.update(FORMAT.encode("ascii") + b"\0")
    aggregate.update(str(manifest["source_base_revision"]).encode("ascii") + b"\0")

    previous_paths: set[str] = set()
    previous_manifest = output / "provenance.json"
    if previous_manifest.is_file():
        previous = json.loads(previous_manifest.read_text(encoding="utf-8"))
        previous_paths = {
            str(record["path"])
            for record in previous.get("files", [])
            if isinstance(record, dict) and record.get("path")
        }

    for relative in SOURCE_PATHS:
        record = records.get(relative)
        if record is None:
            raise ValueError(f"A32 source is absent from evidence snapshot: {relative}")
        source = snapshot / relative
        payload = source.read_bytes()
        digest = sha256_bytes(payload)
        if digest != record.get("sha256") or len(payload) != record.get("size"):
            raise ValueError(f"A32 source differs from snapshot manifest: {relative}")
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.is_file() or destination.read_bytes() != payload:
            destination.write_bytes(payload)
        copied.append({"path": relative, "size": len(payload), "sha256": digest})
        aggregate.update(relative.encode("utf-8") + b"\0" + bytes.fromhex(digest))

    for relative in sorted(previous_paths - set(SOURCE_PATHS)):
        stale = output / relative
        if stale.is_file():
            stale.unlink()

    result: dict[str, object] = {
        "format": FORMAT,
        "source_revision": manifest["source_base_revision"],
        "source_snapshot_id": manifest["snapshot_id"],
        "source_selection_version": manifest["selection_version"],
        "aggregate_sha256": aggregate.hexdigest(),
        "file_count": len(copied),
        "files": copied,
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "oot3d_a32_provenance.h").write_text(
        "\n".join(
            (
                "#pragma once",
                "",
                "#include <string_view>",
                "",
                "namespace oot3d::recomp {",
                f'inline constexpr std::string_view kA32SourceRevision = "{result["source_revision"]}";',
                f'inline constexpr std::string_view kA32SourceSnapshotId = "{result["source_snapshot_id"]}";',
                f'inline constexpr std::string_view kA32SourceAggregateSha256 = "{result["aggregate_sha256"]}";',
                "}  // namespace oot3d::recomp",
                "",
            )
        ),
        encoding="utf-8",
        newline="\n",
    )
    previous_manifest.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parent / "upstream",
    )
    args = parser.parse_args()
    result = sync(args.snapshot.resolve(), args.output.resolve())
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
