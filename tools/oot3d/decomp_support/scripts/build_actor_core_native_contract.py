#!/usr/bin/env python3
"""Build the native Actor Core runtime contract from imported evidence."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path


REQUIRED_FUNCTIONS = (
    "Actor_Noop",
    "Actor_Destroy",
    "Actor_Delete",
    "Actor_Spawn",
    "Actor_Kill",
    "Actor_ChangeType",
    "Actor_InitContext",
    "Actor_UpdateAll",
    "Actor_Init",
)


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as source:
        return list(csv.DictReader(source))


def parse_int(value: str) -> int:
    return int(value, 0)


def active_snapshot(repo_root: Path) -> Path:
    config = json.loads(
        (repo_root / "tools/oot3d/operational_inputs.json").read_text(encoding="utf-8")
    )
    value = config["variables"]["zelda3dRecompEvidence"]
    value = value.replace("${repoRoot}", str(repo_root))
    return Path(value)


def require_native_body_invariants(dossier: str) -> dict[str, object]:
    required_fragments = {
        "spawn_guard": "actor_context->total < 0xc9",
        "category_prepend": "actor_context->lists[bVar1].head = actor",
        "category_loop": "local_4c < 0xc",
        "kill_update": "actor->update = (void *)0x0",
        "kill_draw": "actor->draw = (void *)0x0",
    }
    missing = [name for name, fragment in required_fragments.items() if fragment not in dossier]
    if missing:
        raise RuntimeError(f"Actor Core typed dossier is missing invariants: {', '.join(missing)}")
    return {
        "spawn_total_guard_operator": "less_than",
        "spawn_total_guard_value": 0xC9,
        "category_insertion": "prepend_head",
        "category_iteration": "ascending_index_then_head_to_tail",
        "kill_semantics": "clear_update_draw_and_actor_flag_bit_0_then_delete_in_update_all",
    }


def build_contract(snapshot: Path) -> dict[str, object]:
    manifest_path = snapshot / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    files = {entry["path"]: entry for entry in manifest["files"]}
    analysis = snapshot / "analysis"
    workflow_path = analysis / "codebin_actor_core_workflow.csv"
    signatures_path = analysis / "codebin_actor_core_signatures.csv"
    fields_path = analysis / "codebin_actor_core_struct_fields.csv"
    dossier_path = snapshot / "build/analysis/decomp_batches/actor_core_semantic_typed.md"
    selected_paths = (
        "analysis/codebin_actor_core_workflow.csv",
        "analysis/codebin_actor_core_signatures.csv",
        "analysis/codebin_actor_core_struct_fields.csv",
        "build/analysis/decomp_batches/actor_core_semantic_typed.md",
    )
    if any(path not in files for path in selected_paths):
        raise RuntimeError("active snapshot does not contain the complete Actor Core evidence")

    workflow = read_rows(workflow_path)
    signatures = {row["name"]: row for row in read_rows(signatures_path)}
    fields = read_rows(fields_path)
    if len(workflow) != 38 or any(row["confidence"] != "high" for row in workflow):
        raise RuntimeError("Actor Core workflow is incomplete or not high-confidence")
    by_name = {row["name"]: row for row in workflow}
    missing = [name for name in REQUIRED_FUNCTIONS if name not in by_name or name not in signatures]
    if missing:
        raise RuntimeError(f"Actor Core workflow is missing required functions: {', '.join(missing)}")

    actor_entry_fields = [row for row in fields if row["structure"] == "Oot3dActorEntry"]
    actor_context_fields = [row for row in fields if row["structure"] == "Oot3dActorContext"]
    if not actor_entry_fields or not actor_context_fields:
        raise RuntimeError("Actor Core structure fields are incomplete")
    list_field = next(row for row in actor_context_fields if row["name"] == "lists")
    list_match = re.fullmatch(r"Oot3dActorListEntry\[(\d+)]", list_field["type"])
    if list_match is None:
        raise RuntimeError("Actor Core category-list count is not explicit")

    functions = {}
    for name in REQUIRED_FUNCTIONS:
        row = by_name[name]
        signature = signatures[name]
        functions[name] = {
            "address": parse_int(row["entry"]),
            "size": parse_int(row["size"]),
            "family": row["family"],
            "return_type": signature["return_type"],
            "parameter_types": signature["param_types"].split(";"),
            "confidence": row["confidence"],
        }

    dossier = dossier_path.read_text(encoding="utf-8")
    native_invariants = require_native_body_invariants(dossier)
    return {
        "format": "oot3d_actor_core_native_contract_v1",
        "status": "ready",
        "source": {
            "snapshot_id": manifest["snapshot_id"],
            "source_base_revision": manifest["source_base_revision"],
            "code_bin_sha256": manifest["code_bin_sha256"],
            "source_worktree_dirty": manifest["source_worktree_dirty"],
            "files": [
                {"path": path, "sha256": files[path]["sha256"]} for path in selected_paths
            ],
        },
        "structures": {
            "actor_entry": {
                "size": parse_int(actor_entry_fields[0]["structure_size"]),
                "fields": {
                    row["name"]: {
                        "offset": parse_int(row["offset"]),
                        "type": row["type"],
                    }
                    for row in actor_entry_fields
                },
            },
            "actor_context": {
                "size": parse_int(actor_context_fields[0]["structure_size"]),
                "category_list_count": int(list_match.group(1)),
                "fields": {
                    row["name"]: {
                        "offset": parse_int(row["offset"]),
                        "type": row["type"],
                    }
                    for row in actor_context_fields
                },
            },
        },
        "functions": functions,
        "native_invariants": native_invariants,
        "lifecycle": {
            "spawn": ["resolve_profile", "allocate_instance_size", "copy_profile_callbacks", "prepend_category", "common_init"],
            "update_all": ["spawn_pending_entries", "decrement_freeze_timer", "iterate_categories", "initialize_resident", "update_active", "destroy_killed", "delete_unlink"],
        },
    }


def main() -> None:
    repo_root = Path(__file__).resolve().parents[4]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=Path, default=active_snapshot(repo_root))
    parser.add_argument(
        "--output",
        type=Path,
        default=repo_root / "tools/oot3d/decomp_support/analysis/actor_core_native_contract.json",
    )
    args = parser.parse_args()
    document = build_contract(args.snapshot.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(
        f"actor_core_contract={args.output} snapshot={document['source']['snapshot_id']} "
        f"functions={len(document['functions'])} categories="
        f"{document['structures']['actor_context']['category_list_count']}"
    )


if __name__ == "__main__":
    main()
