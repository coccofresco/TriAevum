"""Expand the whole-function AOT selection from a deterministic hot profile."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Sequence

from generate_aot import operational_path
from whole_aot_cpp import (
    LoweringError,
    SELECTION_FORMAT,
    _load_functions,
    _render_function,
    _symbol,
)


ROOT = Path(__file__).resolve().parent
REPO_ROOT = ROOT.parents[2]


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _direct_call_closure(
    root: int,
    records: dict[int, dict[str, object]],
    retained_external: set[int],
) -> set[int]:
    closure: set[int] = set()
    pending = [root]
    while pending:
        entry = pending.pop()
        if entry in closure or entry in retained_external:
            continue
        record = records.get(entry)
        if record is None:
            raise ValueError(
                f"closure root 0x{root:08X} reaches absent function "
                f"0x{entry:08X}"
            )
        if not record.get("closed_static_cfg"):
            raise ValueError(
                f"closure root 0x{root:08X} reaches open-CFG function "
                f"0x{entry:08X}"
            )
        indirect_sites = record.get("indirect_sites", [])
        if indirect_sites:
            raise ValueError(
                f"closure root 0x{root:08X} reaches function "
                f"0x{entry:08X} with {len(indirect_sites)} indirect site(s)"
            )
        closure.add(entry)
        pending.extend(
            int(call["target"])
            for call in record.get("direct_calls", [])
            if int(call["target"]) not in retained_external
        )
    return closure


def _instruction_count(
    entries: set[int],
    records: dict[int, dict[str, object]],
    blocks: dict[int, dict[str, object]],
) -> int:
    return sum(
        int(blocks[int(block_id)]["instruction_count"])
        for entry in entries
        for block_id in records[entry]["blocks"]
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--program",
        type=Path,
        default=REPO_ROOT / "build-codex/oot3d_whole_aot/aot_program.json",
    )
    parser.add_argument(
        "--profile",
        type=Path,
        default=ROOT / "profiles/title_intro_logo_source_coverage.json",
    )
    parser.add_argument(
        "--selection",
        type=Path,
        default=ROOT / "whole_aot_functions.json",
    )
    parser.add_argument(
        "--exclude-entry",
        action="append",
        default=[],
        type=lambda value: int(value, 0),
        help="persistently exclude a benchmark-regressive function entry",
    )
    parser.add_argument(
        "--ignore-performance-exclusions",
        action="store_true",
        help=(
            "reconsider every historical benchmark exclusion for a complete "
            "product closure"
        ),
    )
    parser.add_argument(
        "--external-entry",
        action="append",
        default=[],
        type=lambda value: int(value, 0),
        help="retain a validated host implementation as an AOT call boundary",
    )
    parser.add_argument(
        "--include-entry",
        action="append",
        default=[],
        type=lambda value: int(value, 0),
        help="promote one entry selected from a newer residual profile",
    )
    parser.add_argument(
        "--include-closure-root",
        action="append",
        default=[],
        type=lambda value: int(value, 0),
        help=(
            "atomically promote a root and its complete direct-call closure; "
            "prior isolated benchmark exclusions do not apply inside it"
        ),
    )
    parser.add_argument(
        "--max-closure-functions",
        type=int,
        default=64,
        help="reject an explicit closure larger than this; zero disables the limit",
    )
    parser.add_argument(
        "--max-closure-instructions",
        type=int,
        default=4096,
        help="reject an explicit closure larger than this; zero disables the limit",
    )
    parser.add_argument(
        "--max-new-functions",
        type=int,
        default=0,
        help=(
            "limit profile-selected additions while retaining the existing "
            "selection; zero keeps every eligible profile function"
        ),
    )
    parser.add_argument(
        "--no-profile-additions",
        action="store_true",
        help="retain the current selection and add only explicit entries/closures",
    )
    parser.add_argument(
        "--all-lowerable",
        action="store_true",
        help=(
            "select every closed-CFG function accepted by the C++ lowerer; "
            "profile samples remain metadata rather than a coverage gate"
        ),
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    if args.max_new_functions < 0:
        parser.error("--max-new-functions must be non-negative")
    if args.max_closure_functions < 0:
        parser.error("--max-closure-functions must be non-negative")
    if args.max_closure_instructions < 0:
        parser.error("--max-closure-instructions must be non-negative")
    output = args.output or args.selection

    program = json.loads(args.program.read_text(encoding="utf-8"))
    profile = json.loads(args.profile.read_text(encoding="utf-8"))
    previous = json.loads(args.selection.read_text(encoding="utf-8"))
    code = operational_path("oot3d_code_bin").read_bytes()
    records = {int(item["entry"]): item for item in program["functions"]}
    blocks = {int(item["id"]): item for item in program["blocks"]}
    profile_records = {
        int(item["entry"]): item for item in profile.get("functions", [])
    }
    retained_external = {
        int(item["entry"])
        for item in previous.get("external_functions", [])
        if "validated host override" in str(item.get("reason", ""))
    } | set(args.external_entry)
    performance_exclusions = {
        int(item["entry"]): str(item.get("reason", "benchmark-regressive closure"))
        for item in previous.get("performance_exclusions", [])
    }
    if args.ignore_performance_exclusions:
        performance_exclusions.clear()
    for entry in args.exclude_entry:
        performance_exclusions[entry] = (
            "benchmark-regressive closure; lowering retained for future closed-call promotion"
        )
    previous_entries = {
        int(item["entry"]) for item in previous.get("functions", [])
    }
    closure_groups: list[dict[str, object]] = []
    closure_entries: set[int] = set()
    for root in args.include_closure_root:
        members = _direct_call_closure(root, records, retained_external)
        instruction_count = _instruction_count(
            members, records, blocks
        )
        if (args.max_closure_functions and
                len(members) > args.max_closure_functions):
            raise ValueError(
                f"closure root 0x{root:08X} has {len(members)} functions; "
                f"limit is {args.max_closure_functions}"
            )
        if (args.max_closure_instructions and
                instruction_count > args.max_closure_instructions):
            raise ValueError(
                f"closure root 0x{root:08X} has {instruction_count} "
                f"instructions; limit is {args.max_closure_instructions}"
            )
        internal_edges = sum(
            1
            for entry in members
            for call in records[entry].get("direct_calls", [])
            if int(call["target"]) in members
        )
        external_targets = sorted({
            int(call["target"])
            for entry in members
            for call in records[entry].get("direct_calls", [])
            if int(call["target"]) not in members
        })
        closure_groups.append({
            "root": root,
            "name": str(records[root]["name"]),
            "members": sorted(members),
            "function_count": len(members),
            "instruction_count": instruction_count,
            "internal_direct_edges": internal_edges,
            "external_targets": external_targets,
        })
        closure_entries.update(members)
    profiled_additions = sorted(
        (
            entry
            for entry in set(profile_records) - previous_entries
            if entry in records
            and records[entry].get("closed_static_cfg")
            and not records[entry].get("indirect_sites")
            and entry not in retained_external
            and entry not in performance_exclusions
        ),
        key=lambda entry: (
            -int(profile_records[entry].get("samples", 0)), entry
        ),
    )
    if args.no_profile_additions:
        profiled_additions = []
    if args.all_lowerable:
        profiled_additions = sorted(
            entry
            for entry, record in records.items()
            if record.get("closed_static_cfg")
            and entry not in retained_external
            and entry not in performance_exclusions
        )
    if args.max_new_functions:
        profiled_additions = profiled_additions[:args.max_new_functions]
    explicit_includes = set(args.include_entry)
    candidates = (
        set(profiled_additions) | previous_entries | set(args.include_entry) |
        closure_entries
    ) - retained_external - (
        set(performance_exclusions) - closure_entries - explicit_includes
    )
    candidates = {
        entry
        for entry in candidates
        if entry in records
        and records[entry].get("closed_static_cfg")
        and (
            not records[entry].get("indirect_sites")
            or entry in explicit_includes
            or args.all_lowerable
        )
    }
    candidate_external = {
        int(call["target"])
        for entry in candidates
        for call in records[entry].get("direct_calls", [])
        if int(call["target"]) not in candidates
    } | retained_external
    candidate_selection = {
        "format": SELECTION_FORMAT,
        "functions": [{"entry": entry} for entry in sorted(candidates)],
        "external_functions": [
            {"entry": entry} for entry in sorted(candidate_external)
        ],
    }
    functions, external = _load_functions(
        program, candidate_selection, code
    )
    symbols = {
        function.entry: _symbol(function.name, function.entry)
        for function in functions
    }
    supported = []
    rejected: dict[int, str] = {}
    for function in functions:
        try:
            _render_function(function, symbols, external)
        except LoweringError as error:
            rejected[function.entry] = str(error)
        else:
            supported.append(function.entry)

    functions_by_entry = {function.entry: function for function in functions}
    # Callable entries remain independently selectable even when recovered
    # functions share a resume block. Runtime dispatch gives every primary
    # entry ownership of itself and canonicalizes only duplicate resume aliases.
    accepted = sorted(supported)
    entry_owners = {entry: entry for entry in accepted}
    ownership_conflicts: dict[int, tuple[int, int]] = {}
    for entry in accepted:
        function = functions_by_entry[entry]
        for dispatch_entry in function.resume_entries:
            owner = entry_owners.setdefault(dispatch_entry, entry)
            if owner != entry and entry not in ownership_conflicts:
                ownership_conflicts[entry] = (dispatch_entry, owner)

    supported = sorted(accepted)
    supported_set = set(supported)
    for group in closure_groups:
        missing_members = set(group["members"]) - supported_set
        if missing_members:
            formatted = ", ".join(
                f"0x{entry:08X}" for entry in sorted(missing_members)
            )
            raise ValueError(
                f"closure root 0x{int(group['root']):08X} was not accepted "
                f"atomically; rejected member(s): {formatted}"
            )
    final_external = retained_external | {
        int(call["target"])
        for entry in supported
        for call in records[entry].get("direct_calls", [])
        if int(call["target"]) not in supported_set
    }
    final_selection = {
        "format": SELECTION_FORMAT,
        "source_profile": args.profile.resolve().relative_to(REPO_ROOT).as_posix(),
        "source_profile_sha256": _sha256(args.profile),
        "functions": [],
        "external_functions": [],
        "performance_exclusions": [],
        "selection_conflicts": [],
        "closure_groups": closure_groups,
        "selection_mode": (
            "all_lowerable" if args.all_lowerable else "profile_incremental"
        ),
    }
    for entry in sorted(supported):
        item = profile_records.get(entry)
        reason = "retained validated whole-function AOT seed"
        if item is not None:
            reason = (
                "profile-selected complete lowering: "
                f"{int(item['samples'])} sampled block entries"
            )
        if entry in closure_entries:
            roots = [
                int(group["root"])
                for group in closure_groups
                if entry in group["members"]
            ]
            reason = (
                "atomic direct-call closure for " +
                ", ".join(f"0x{root:08X}" for root in roots)
            )
        elif entry in args.include_entry:
            reason = "explicit residual-profile promotion"
        elif args.all_lowerable and item is None:
            reason = "whole-program closed-CFG lowering"
        final_selection["functions"].append(
            {
                "entry": entry,
                "name": str(records[entry]["name"]),
                "reason": reason,
            }
        )
    for entry in sorted(final_external):
        record = records.get(entry)
        final_selection["external_functions"].append(
            {
                "entry": entry,
                "name": str(record["name"]) if record else f"sub_{entry:08X}",
                "reason": (
                    "validated host override"
                    if entry in retained_external
                    else "ARM continuation boundary for an unselected callee"
                ),
            }
        )
    for entry, reason in sorted(performance_exclusions.items()):
        if entry in closure_entries:
            continue
        record = records.get(entry)
        final_selection["performance_exclusions"].append(
            {
                "entry": entry,
                "name": str(record["name"]) if record else f"sub_{entry:08X}",
                "reason": reason,
            }
        )
    for entry, (dispatch_entry, owner) in sorted(ownership_conflicts.items()):
        final_selection["selection_conflicts"].append(
            {
                "entry": entry,
                "name": str(records[entry]["name"]),
                "dispatch_entry": dispatch_entry,
                "selected_owner": owner,
                "reason": (
                    "shared resume alias uses one canonical dispatch owner; "
                    "the callable body remains selected"
                ),
            }
        )

    verified_functions, verified_external = _load_functions(
        program, final_selection, code
    )
    verified_symbols = {
        function.entry: _symbol(function.name, function.entry)
        for function in verified_functions
    }
    for function in verified_functions:
        _render_function(function, verified_symbols, verified_external)
    output.write_text(
        json.dumps(final_selection, indent=2) + "\n", encoding="utf-8"
    )
    selected_coverage = sum(
        float(profile_records[entry]["total_profile_coverage"])
        for entry in supported
        if entry in profile_records
    )
    selected_instructions = sum(
        sum(len(block.instructions) for block in function.blocks)
        for function in verified_functions
    )
    print(
        f"Whole-AOT profile selection: {len(supported)} functions, "
        f"{selected_instructions} instructions, "
        f"{selected_coverage:.3%} sampled coverage, "
        f"{len(rejected)} lowering rejections, "
        f"{len(ownership_conflicts)} ownership conflicts, "
        f"{len(closure_groups)} atomic closure(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
