"""Verify the immutable inputs and zero-residual whole-AOT product closure."""

from __future__ import annotations

import argparse
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

from generate_aot import operational_path


ROOT = Path(__file__).resolve().parent
REPO_ROOT = ROOT.parents[2]
DEFAULT_MANIFEST = ROOT / "whole_aot_product_manifest.json"


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _load_json(path: Path) -> dict[str, object]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path} does not contain a JSON object")
    return value


def _repo_path(value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else REPO_ROOT / path


@dataclass(frozen=True)
class AuditResult:
    summary: dict[str, object]
    errors: tuple[str, ...]

    @property
    def ok(self) -> bool:
        return not self.errors


def audit_product(
    manifest_path: Path,
    program_path: Path,
    *,
    code_path: Path | None = None,
    exheader_path: Path | None = None,
    selection_path: Path | None = None,
    generated_manifest_path: Path | None = None,
    verify_generated_files: bool = True,
) -> AuditResult:
    errors: list[str] = []

    def expect(condition: bool, message: str) -> None:
        if not condition:
            errors.append(message)

    manifest = _load_json(manifest_path)
    expect(
        manifest.get("format") == "oot3d_whole_aot_product_contract_v1",
        "unsupported whole-AOT product manifest format",
    )
    source = manifest["source_image"]
    selection_contract = manifest["selection"]
    closure = manifest["closure"]
    codegen = manifest["codegen"]
    frontend = manifest["frontend"]
    assert isinstance(source, dict)
    assert isinstance(selection_contract, dict)
    assert isinstance(closure, dict)
    assert isinstance(codegen, dict)
    assert isinstance(frontend, dict)

    code_contract = source["code"]
    exheader_contract = source["exheader"]
    assert isinstance(code_contract, dict)
    assert isinstance(exheader_contract, dict)
    code_path = code_path or operational_path(str(code_contract["operational_id"]))
    exheader_path = exheader_path or operational_path(
        str(exheader_contract["operational_id"])
    )
    selection_path = selection_path or _repo_path(str(selection_contract["path"]))

    for label, path in (
        ("code.bin", code_path),
        ("ExHeader", exheader_path),
        ("selection", selection_path),
        ("program", program_path),
    ):
        expect(path.is_file(), f"{label} does not exist: {path}")
    if errors:
        return AuditResult({}, tuple(errors))

    expect(
        code_path.stat().st_size == int(code_contract["bytes"]),
        "code.bin byte size does not match the product contract",
    )
    code_hash = _sha256(code_path)
    expect(
        code_hash == str(code_contract["sha256"]).lower(),
        "code.bin SHA-256 does not match the product contract",
    )
    expect(
        exheader_path.stat().st_size == int(exheader_contract["bytes"]),
        "ExHeader byte size does not match the product contract",
    )
    expect(
        _sha256(exheader_path) == str(exheader_contract["sha256"]).lower(),
        "ExHeader SHA-256 does not match the product contract",
    )
    exheader = exheader_path.read_bytes()
    entrypoint = int.from_bytes(exheader[0x10:0x14], "little")
    executable_size = int.from_bytes(exheader[0x14:0x18], "little") * 0x1000
    expect(entrypoint == int(source["entrypoint"]), "ExHeader entrypoint mismatch")
    expect(
        executable_size == int(source["executable_size"]),
        "ExHeader executable text size mismatch",
    )

    selection_hash = _sha256(selection_path)
    expect(
        selection_hash == str(selection_contract["sha256"]).lower(),
        "whole-AOT selection SHA-256 does not match the product contract",
    )
    selection = _load_json(selection_path)
    expect(
        selection.get("format") == selection_contract["format"],
        "whole-AOT selection format mismatch",
    )
    expect(
        selection.get("selection_mode") == selection_contract["mode"],
        "whole-AOT selection is not in product all-lowerable mode",
    )

    program_hash = _sha256(program_path)
    program = _load_json(program_path)
    expect(program.get("format") == frontend["program_format"], "program format mismatch")
    expect(program.get("code_sha256") == code_hash, "program/code SHA-256 mismatch")
    expect(program.get("base") == source["base"], "program base mismatch")
    expect(
        program.get("executable_size") == source["executable_size"],
        "program executable size mismatch",
    )
    inputs = program.get("inputs", {})
    expect(
        isinstance(inputs, dict)
        and inputs.get("inventory_sha256") == frontend["inventory_sha256"],
        "program inventory identity mismatch",
    )
    expect(
        isinstance(inputs, dict)
        and inputs.get("boundary_audit_sha256")
        == frontend["boundary_audit_sha256"],
        "program callable-boundary audit identity mismatch",
    )

    functions = program.get("functions", [])
    blocks = program.get("blocks", [])
    selected_items = selection.get("functions", [])
    external_items = selection.get("external_functions", [])
    exclusions = selection.get("performance_exclusions", [])
    conflicts = selection.get("selection_conflicts", [])
    expect(isinstance(functions, list), "program functions are malformed")
    expect(isinstance(blocks, list), "program blocks are malformed")
    expect(isinstance(selected_items, list), "selection functions are malformed")
    expect(isinstance(external_items, list), "selection externals are malformed")
    if errors:
        return AuditResult({}, tuple(errors))

    program_by_entry = {int(item["entry"]): item for item in functions}
    blocks_by_id = {int(item["id"]): item for item in blocks}
    selected_by_entry = {int(item["entry"]): item for item in selected_items}
    external_by_entry = {int(item["entry"]): item for item in external_items}
    expect(
        len(program_by_entry) == len(functions),
        "program contains duplicate function entries",
    )
    expect(len(blocks_by_id) == len(blocks), "program contains duplicate block ids")
    expect(
        len(selected_by_entry) == len(selected_items),
        "selection contains duplicate function entries",
    )
    expect(
        not (set(selected_by_entry) & set(external_by_entry)),
        "selected and external function sets overlap",
    )

    expected_hosts = {
        int(item["entry"]): str(item["name"])
        for item in closure["host_boundaries"]
    }
    actual_hosts = {
        entry: str(item.get("name", ""))
        for entry, item in external_by_entry.items()
    }
    expect(actual_hosts == expected_hosts, "host-boundary set differs from the contract")
    expect(
        all(item.get("reason") == "validated host override" for item in external_items),
        "an external entry is not a validated host override",
    )
    expect(not exclusions, "performance exclusions are forbidden in product mode")
    expect(
        set(program_by_entry) == set(selected_by_entry) | set(external_by_entry),
        "program closure contains unselected, non-host A32 entries",
    )
    expect(
        all(bool(item.get("closed_static_cfg")) for item in functions),
        "one or more function CFGs remain open",
    )
    expect(
        all(not item.get("unresolved_static_edges") for item in functions),
        "one or more static CFG edges remain unresolved",
    )
    expect(not program.get("unclaimed_blocks"), "program has unclaimed basic blocks")

    def function_block_entries(item: dict[str, object]) -> set[int]:
        return {
            int(blocks_by_id[int(block_id)]["pc"])
            for block_id in item.get("blocks", [])
        }

    all_block_entries = {int(item["pc"]) for item in blocks}
    dispatcher_entries = set(selected_by_entry)
    for entry in selected_by_entry:
        dispatcher_entries.update(function_block_entries(program_by_entry[entry]))
    host_block_entries: set[int] = set()
    for entry in external_by_entry:
        host_block_entries.update(function_block_entries(program_by_entry[entry]))
    atomic_host_blocks = all_block_entries - dispatcher_entries
    expect(
        atomic_host_blocks <= host_block_entries,
        "a non-host basic block has no generated dispatcher entry",
    )

    available_calls = set(selected_by_entry) | set(external_by_entry)
    missing_call_targets = sorted(
        {
            int(call["target"])
            for entry, item in program_by_entry.items()
            if entry in selected_by_entry
            for call in item.get("direct_calls", [])
            if int(call["target"]) not in available_calls
        }
    )
    expect(
        not missing_call_targets,
        "direct-call closure has missing targets: "
        + ", ".join(f"0x{entry:08X}" for entry in missing_call_targets),
    )
    name_mismatches = sorted(
        entry
        for entry, item in selected_by_entry.items()
        if str(item.get("name", ""))
        != str(program_by_entry.get(entry, {}).get("name", ""))
    )
    expect(
        not name_mismatches,
        "selection/program function names differ at: "
        + ", ".join(f"0x{entry:08X}" for entry in name_mismatches[:16]),
    )

    counts = closure["counts"]
    program_counts = program.get("counts", {})
    assert isinstance(counts, dict)
    expect(len(functions) == counts["functions"], "function count mismatch")
    expect(len(selected_items) == counts["selected_functions"], "selected count mismatch")
    expect(len(external_items) == counts["host_boundaries"], "host count mismatch")
    expect(len(blocks) == counts["blocks"], "block count mismatch")
    expect(
        isinstance(program_counts, dict)
        and program_counts.get("instructions") == counts["unique_instructions"],
        "unique instruction count mismatch",
    )
    expect(len(conflicts) == counts["dispatch_aliases"], "dispatch alias count mismatch")
    expect(
        len(dispatcher_entries) == counts["dispatcher_entries"],
        "dispatcher entry count mismatch",
    )
    expect(
        len(atomic_host_blocks) == counts["atomic_host_blocks"],
        "atomic host-block count mismatch",
    )
    emitted_instructions = sum(
        int(blocks[block_id]["instruction_count"])
        for item in functions
        if int(item["entry"]) in selected_by_entry
        for block_id in item["blocks"]
    )
    expect(
        emitted_instructions == counts["emitted_instructions"],
        "emitted instruction count mismatch",
    )
    expect(
        int(closure["residual_a32_entries"]) == 0,
        "product contract permits residual A32 entries",
    )

    if generated_manifest_path is not None:
        expect(
            generated_manifest_path.is_file(),
            f"generated C++ manifest does not exist: {generated_manifest_path}",
        )
        if generated_manifest_path.is_file():
            generated = _load_json(generated_manifest_path)
            expect(generated.get("format") == codegen["format"], "codegen format mismatch")
            expect(generated.get("program_sha256") == program_hash, "codegen/program mismatch")
            expect(generated.get("selection_sha256") == selection_hash, "codegen/selection mismatch")
            expect(generated.get("code_sha256") == code_hash, "codegen/code mismatch")
            expect(generated.get("shard_count") == codegen["shard_count"], "shard count mismatch")
            expect(
                generated.get("shard_strategy") == codegen["shard_strategy"],
                "shard strategy mismatch",
            )
            generated_entries = {
                int(item["entry"]) for item in generated.get("functions", [])
            }
            expect(generated_entries == set(selected_by_entry), "generated function set mismatch")
            generated_dispatch_entries = set(generated_entries)
            for item in generated.get("functions", []):
                generated_dispatch_entries.update(
                    int(entry) for entry in item.get("dispatch_entries", [])
                )
            expect(
                generated_dispatch_entries == dispatcher_entries,
                "generated dispatcher does not cover every selected basic block",
            )
            expect(
                set(generated.get("external_functions", [])) == set(external_by_entry),
                "generated host-boundary set mismatch",
            )
            shard_entries = [
                int(entry)
                for shard in generated.get("shards", [])
                for entry in shard.get("entries", [])
            ]
            expect(len(shard_entries) == len(set(shard_entries)), "duplicate shard ownership")
            expect(set(shard_entries) == set(selected_by_entry), "incomplete shard ownership")
            if verify_generated_files:
                generated_root = generated_manifest_path.parent
                for name, digest in generated.get("files", {}).items():
                    path = generated_root / name
                    expect(path.is_file(), f"missing generated file: {path}")
                    if path.is_file():
                        expect(
                            _sha256(path) == digest,
                            f"generated file hash mismatch: {path}",
                        )

    summary = {
        "program_sha256": program_hash,
        "selection_sha256": selection_hash,
        "functions": len(functions),
        "selected_functions": len(selected_items),
        "host_boundaries": len(external_items),
        "blocks": len(blocks),
        "dispatcher_entries": len(dispatcher_entries),
        "atomic_host_blocks": len(atomic_host_blocks),
        "unique_instructions": program_counts.get("instructions"),
        "emitted_instructions": emitted_instructions,
        "residual_a32_entries": len(
            set(program_by_entry) - set(selected_by_entry) - set(external_by_entry)
        ),
        "generated_artifacts_verified": generated_manifest_path is not None,
    }
    return AuditResult(summary, tuple(errors))


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--program", type=Path, required=True)
    parser.add_argument("--code", type=Path)
    parser.add_argument("--exheader", type=Path)
    parser.add_argument("--selection", type=Path)
    parser.add_argument("--generated-manifest", type=Path)
    parser.add_argument("--skip-generated-file-hashes", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    result = audit_product(
        args.manifest,
        args.program,
        code_path=args.code,
        exheader_path=args.exheader,
        selection_path=args.selection,
        generated_manifest_path=args.generated_manifest,
        verify_generated_files=not args.skip_generated_file_hashes,
    )
    payload = {"ok": result.ok, "summary": result.summary, "errors": result.errors}
    if args.json:
        print(json.dumps(payload, indent=2))
    elif result.ok:
        print(
            "Whole-AOT product audit: "
            f"{result.summary['selected_functions']} compiled functions, "
            f"{result.summary['host_boundaries']} host boundaries, "
            "0 residual A32 entries"
        )
    else:
        print("Whole-AOT product audit failed:")
        for error in result.errors:
            print(f"  - {error}")
    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
