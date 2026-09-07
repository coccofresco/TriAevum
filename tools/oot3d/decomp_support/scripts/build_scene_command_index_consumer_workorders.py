"""Build downstream consumer workorders for native OOT3D scene indices.

The scene command handlers prove where setup payload pointers are installed in
Play state. This script takes the focused Ghidra offset scan and turns it into
an evidence queue for the downstream readers of those fields. It is intentionally
conservative: ARM immediates such as 0xC20 can be real Play+0x5C20 accesses or
local/actor struct offsets, so rows are classified as candidates until their
decompiled bodies prove the base object.
"""

from __future__ import annotations

import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OFFSET_ACCESSES = ROOT / "analysis" / "scene_command_exit_path_offset_accesses.csv"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_command_index_consumer_workorders.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_command_index_consumer_workorders.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_command_index_consumer_workorders.md"
DEFAULT_ENTRIES_TXT = ROOT / "analysis" / "scene_command_index_consumer_export_entries.txt"
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_SCENE_INDEX = ROOT / "analysis" / "oot3d_native_scene_index_all_summary.json"
CODE_LOAD_BASE = 0x00100000
PATH_OFFSET_LITERAL_ADDRESS = 0x00298680

EXIT_OFFSETS = {"0x5c1c", "0xc1c"}
PATH_OFFSETS = {"0x5c20", "0xc20"}
INDEX_OFFSETS = EXIT_OFFSETS | PATH_OFFSETS

HANDLER_ENTRIES = {
    "002a9e2c": "exit_list_handler",
    "002985f0": "path_list_handler",
}


def normalize_entry(value: str) -> str:
    value = str(value or "").strip().lower()
    if value.startswith("0x"):
        value = value[2:]
    return value.zfill(8) if value else ""


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        raise FileNotFoundError(f"{path}: offset access CSV not found")
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_u32(path: Path, address: int) -> int | None:
    if not path.is_file():
        return None
    data = path.read_bytes()
    offset = address - CODE_LOAD_BASE
    if offset < 0 or offset + 4 > len(data):
        return None
    return int.from_bytes(data[offset : offset + 4], "little")


def path_point_status_counts(path: Path) -> dict[str, int]:
    if not path.is_file():
        return {}
    payload = json.loads(path.read_text(encoding="utf-8"))
    counts: Counter[str] = Counter()
    for record in payload.get("records", []):
        for setup in record.get("setups", []):
            for command in setup.get("commands", []):
                if int(command.get("command_id", -1)) != 0x0D:
                    continue
                decoded = command.get("decoded")
                if not isinstance(decoded, dict):
                    continue
                for path_record in decoded.get("raw_records", []):
                    if isinstance(path_record, dict):
                        counts[str(path_record.get("points_status", "unknown"))] += 1
    return dict(sorted(counts.items()))


def index_kind(matched_offset: str) -> str:
    offset = matched_offset.lower()
    if offset in EXIT_OFFSETS:
        return "exit_list"
    if offset in PATH_OFFSETS:
        return "path_list"
    return "other"


def role_for(row: dict[str, str]) -> tuple[str, str]:
    entry = normalize_entry(row.get("entry", ""))
    function = row.get("function", "")
    instruction = row.get("instruction", "").lower()
    kind = index_kind(row.get("matched_offset", ""))

    if entry in HANDLER_ENTRIES:
        return "scene_command_handler", "confirmed"
    if function == "Path_GetByIndex":
        return "general_path_accessor", "high"
    if "sp,#" in instruction:
        return "stack_or_local_offset_candidate", "low"
    if kind == "exit_list" and function in {"EnMd_Init", "EnTa_Init"}:
        return "actor_exit_override_or_consumer", "medium"
    if kind == "path_list" and function.startswith(("En", "Obj", "Boss")):
        return "actor_path_consumer_candidate", "medium"
    if instruction.startswith(("str", "strb", "strh")):
        return "runtime_mutator_candidate", "medium"
    return "runtime_reader_candidate", "medium"


def should_export(row: dict[str, Any]) -> bool:
    if row["role"] == "scene_command_handler":
        return False
    if row["confidence"] == "low":
        return False
    return True


def build_workorders(rows: list[dict[str, str]]) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    groups: dict[tuple[str, str], dict[str, Any]] = {}
    raw_count = 0
    for row in rows:
        kind = index_kind(row.get("matched_offset", ""))
        if kind == "other":
            continue
        raw_count += 1
        entry = normalize_entry(row.get("entry", ""))
        role, confidence = role_for(row)
        key = (kind, entry)
        group = groups.setdefault(
            key,
            {
                "index_kind": kind,
                "entry": entry,
                "function": row.get("function", ""),
                "role": role,
                "confidence": confidence,
                "access_count": 0,
                "matched_offsets": set(),
                "accesses": [],
            },
        )
        group["access_count"] += 1
        group["matched_offsets"].add(row.get("matched_offset", "").lower())
        group["accesses"].append(
            {
                "address": row.get("address", ""),
                "mnemonic": row.get("mnemonic", ""),
                "instruction": row.get("instruction", ""),
                "matched_offset": row.get("matched_offset", "").lower(),
            }
        )

    workorders: list[dict[str, Any]] = []
    for group in groups.values():
        group["matched_offsets"] = sorted(group["matched_offsets"])
        group["export_for_decompilation"] = should_export(group)
        score = 100.0
        if group["confidence"] == "high":
            score -= 80
        elif group["confidence"] == "medium":
            score -= 40
        if group["role"].startswith("actor_"):
            score += 10
        if group["role"] == "scene_command_handler":
            score += 50
        score -= min(int(group["access_count"]), 10)
        group["priority_score"] = round(score, 4)
        workorders.append(group)

    workorders.sort(
        key=lambda row: (
            float(row["priority_score"]),
            row["index_kind"],
            row["entry"],
        )
    )
    for rank, row in enumerate(workorders, start=1):
        row["rank"] = rank

    role_counts = Counter(row["role"] for row in workorders)
    export_entries = sorted({row["entry"] for row in workorders if row["export_for_decompilation"]})
    path_offset = read_u32(DEFAULT_CODE_BIN, PATH_OFFSET_LITERAL_ADDRESS)
    summary = {
        "source_offset_accesses": str(DEFAULT_OFFSET_ACCESSES),
        "raw_index_access_count": raw_count,
        "workorder_count": len(workorders),
        "export_entry_count": len(export_entries),
        "role_counts": dict(sorted(role_counts.items())),
        "exit_list_runtime_offset": "0x5c1c",
        "path_list_runtime_offset": f"0x{path_offset:04x}" if path_offset is not None else "unresolved",
        "path_list_offset_literal_address": f"0x{PATH_OFFSET_LITERAL_ADDRESS:08x}",
        "path_point_status_counts": path_point_status_counts(DEFAULT_SCENE_INDEX),
    }
    return workorders, summary


def serializable(row: dict[str, Any]) -> dict[str, Any]:
    return {key: sorted(value) if isinstance(value, set) else value for key, value in row.items()}


def write_json(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {"summary": summary, "workorders": [serializable(row) for row in rows]}
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "rank",
        "priority_score",
        "index_kind",
        "entry",
        "function",
        "role",
        "confidence",
        "access_count",
        "matched_offsets",
        "export_for_decompilation",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow(
                {
                    **{field: row.get(field, "") for field in fields},
                    "matched_offsets": " ".join(row["matched_offsets"]),
                }
            )


def write_entries(path: Path, rows: list[dict[str, Any]]) -> None:
    entries = sorted({row["entry"] for row in rows if row["export_for_decompilation"]})
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(entries) + ("\n" if entries else ""), encoding="utf-8")


def write_markdown(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    lines = [
        "# Scene Command Index Consumer Workorders",
        "",
        "Generated from Ghidra offset-access evidence. These rows identify downstream candidates for scene command `0x13` exits and `0x0D` paths, and keep remaining work focused on semantics that still need consumer confirmation.",
        "",
        "## Summary",
        "",
        f"- Source offset scan: `{summary['source_offset_accesses']}`",
        f"- Raw index accesses: {summary['raw_index_access_count']}",
        f"- Workorders: {summary['workorder_count']}",
        f"- Export entries: {summary['export_entry_count']}",
        f"- Exit list runtime offset: `{summary['exit_list_runtime_offset']}`",
        f"- Path list runtime offset: `{summary['path_list_runtime_offset']}` from literal `{summary['path_list_offset_literal_address']}`",
        f"- Path point status counts: `{json.dumps(summary['path_point_status_counts'], sort_keys=True)}`",
        "",
        "## Confirmed Findings",
        "",
        "- `0x0D path_list`: handler `0x002985F0` installs the path table at `play+0x5C20`; `Path_GetByIndex` at `0x00348FF0` returns `play->paths + index * 8` unless `index == sentinel`; exported consumers read record byte `+0` as point count and record word `+4` as a relocated `Vec3s` point-list pointer. Record bytes `+1..+3` remain unknown/padding. Native records whose point offset does not resolve inside the containing ZSI are preserved as `invalid_points_offset`, not treated as external asset references.",
        "- `0x13 exit_list`: handler `0x002A9E2C` installs the exit list at `play+0x5C1C`; consumer `0x003365B0` reads signed halfwords from `play->exits + collisionResult * 2 - 2`. Signed values `< 0x7FF9`, special `0x7FFF`, and high-remap values `0x7FF9..0x7FFE` are now represented as native categories; the high-remap delta table and asset-proven entrance table window are emitted from `code.bin`.",
        "",
        "## Role Counts",
        "",
    ]
    for role, count in summary["role_counts"].items():
        lines.append(f"- `{role}`: {count}")

    lines.extend(
        [
            "",
            "## Queue",
            "",
            "| Rank | Kind | Entry | Function | Role | Confidence | Accesses | Offsets | Export |",
            "| ---: | --- | --- | --- | --- | --- | ---: | --- | --- |",
        ]
    )
    for row in rows:
        offsets = " ".join(f"`{offset}`" for offset in row["matched_offsets"])
        lines.append(
            f"| {row['rank']} | `{row['index_kind']}` | `{row['entry']}` | `{row['function']}` | `{row['role']}` | `{row['confidence']}` | {row['access_count']} | {offsets} | `{row['export_for_decompilation']}` |"
        )

    lines.extend(["", "## Access Samples", ""])
    for row in rows[:20]:
        lines.append(f"### {row['index_kind']} {row['entry']} {row['function']}")
        lines.append("")
        for access in row["accesses"][:6]:
            lines.append(
                f"- `{access['address']}` `{access['matched_offset']}` `{access['instruction']}`"
            )
        lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def main() -> int:
    rows, summary = build_workorders(read_csv(DEFAULT_OFFSET_ACCESSES))
    write_json(DEFAULT_OUT_JSON, rows, summary)
    write_csv(DEFAULT_OUT_CSV, rows)
    write_markdown(DEFAULT_OUT_MD, rows, summary)
    write_entries(DEFAULT_ENTRIES_TXT, rows)
    print(DEFAULT_OUT_JSON)
    print(DEFAULT_OUT_CSV)
    print(DEFAULT_OUT_MD)
    print(DEFAULT_ENTRIES_TXT)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
