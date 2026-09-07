#!/usr/bin/env python3
"""Port text-free N64 OoT actor, object, and item semantics into OOT3D artifacts."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DEFAULT_N64_ROOT = ROOT.parent / "external" / "oot"

TABLE_ENTRY_RE = re.compile(
    r"/\*\s*0x(?P<value>[0-9A-Fa-f]+)\s*\*/\s*"
    r"(?P<macro>DEFINE_[A-Z_]+)\((?P<args>[^)]*)\)"
)
ENUM_RE = re.compile(
    r"typedef\s+enum\s+(?P<name>[A-Za-z0-9_]+)\s*\{(?P<body>.*?)\}\s*(?P<typedef>[A-Za-z0-9_]+)\s*;",
    re.DOTALL,
)
COMMENT_RE = re.compile(r"/\*.*?\*/|//.*?$", re.DOTALL | re.MULTILINE)
ENUM_ENTRY_RE = re.compile(r"(?P<name>[A-Z][A-Z0-9_]+)(?:\s*=\s*(?P<value>0x[0-9A-Fa-f]+|\d+))?")

ITEM_ENUMS_TO_PORT = [
    "EquipmentType",
    "EquipInvSword",
    "EquipInvShield",
    "EquipInvTunic",
    "EquipInvBoots",
    "EquipValueSword",
    "EquipValueShield",
    "EquipValueTunic",
    "EquipValueBoots",
    "UpgradeType",
    "QuestItem",
    "DungeonItem",
    "InventorySlot",
    "ItemID",
    "GetItemID",
    "GetItemDrawID",
]


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        try:
            return str(path.relative_to(ROOT.parent)).replace("\\", "/")
        except ValueError:
            return str(path).replace("\\", "/")


def git_commit(repo: Path) -> str | None:
    head = repo / ".git" / "HEAD"
    if not head.is_file():
        return None
    text = head.read_text(encoding="utf-8", errors="replace").strip()
    if text.startswith("ref: "):
        ref = repo / ".git" / text.removeprefix("ref: ")
        if ref.is_file():
            return ref.read_text(encoding="utf-8", errors="replace").strip()[:12]
    return text[:12]


def parse_int(value: str) -> int:
    return int(value, 16) if value.lower().startswith("0x") else int(value, 10)


def fmt_hex(value: int) -> str:
    width = 4 if value > 0xFF else 2
    return f"0x{value:0{width}X}"


def split_args(args: str) -> list[str]:
    return [part.strip() for part in args.split(",")]


def parse_macro_table(path: Path, kind: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for match in TABLE_ENTRY_RE.finditer(path.read_text(encoding="utf-8", errors="replace")):
        args = split_args(match.group("args"))
        macro = match.group("macro")
        if kind == "actor":
            if macro == "DEFINE_ACTOR_UNSET":
                enum_name = args[0]
                segment_name = None
                alloc_type = None
            else:
                segment_name = args[0]
                enum_name = args[1]
                alloc_type = args[2]
            row: dict[str, object] = {
                "value": int(match.group("value"), 16),
                "name": enum_name,
                "macro": macro,
                "segment": segment_name,
                "alloc_type": alloc_type,
            }
        elif kind == "object":
            if macro == "DEFINE_OBJECT_UNSET":
                enum_name = args[0]
                segment_name = None
            else:
                segment_name = args[0]
                enum_name = args[1]
            row = {
                "value": int(match.group("value"), 16),
                "name": enum_name,
                "macro": macro,
                "segment": segment_name,
            }
        else:
            raise ValueError(f"unsupported table kind: {kind}")
        rows.append(row)
    return rows


def parse_enum(text: str, enum_name: str) -> dict[str, object]:
    for match in ENUM_RE.finditer(text):
        if match.group("name") != enum_name:
            continue
        body = COMMENT_RE.sub("", match.group("body"))
        entries: list[dict[str, object]] = []
        next_value = 0
        for part in body.split(","):
            entry_match = ENUM_ENTRY_RE.search(part.strip())
            if not entry_match:
                continue
            explicit = entry_match.group("value")
            value = parse_int(explicit) if explicit else next_value
            entries.append({"name": entry_match.group("name"), "value": value})
            next_value = value + 1
        return {"name": enum_name, "typedef": match.group("typedef"), "entries": entries}
    raise ValueError(f"enum {enum_name} not found")


def parse_item_enums(path: Path) -> list[dict[str, object]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    enums = []
    for enum_name in ITEM_ENUMS_TO_PORT:
        enums.append(parse_enum(text, enum_name))
    return enums


def collect_semantics(n64_root: Path) -> dict[str, object]:
    sources = {
        "actor_profile": n64_root / "include" / "actor_profile.h",
        "actor_table": n64_root / "include" / "tables" / "actor_table.h",
        "object": n64_root / "include" / "object.h",
        "object_table": n64_root / "include" / "tables" / "object_table.h",
        "item": n64_root / "include" / "item.h",
    }
    missing = [path for path in sources.values() if not path.is_file()]
    if missing:
        raise SystemExit("Missing N64 actor/object sources: " + ", ".join(str(path) for path in missing))

    actor_rows = parse_macro_table(sources["actor_table"], "actor")
    object_rows = parse_macro_table(sources["object_table"], "object")
    actor_category = parse_enum(sources["actor_profile"].read_text(encoding="utf-8", errors="replace"), "ActorCategory")
    item_enums = parse_item_enums(sources["item"])

    actor_groups: dict[str, int] = {}
    for row in actor_rows:
        key = str(row["macro"]).removeprefix("DEFINE_ACTOR_")
        if key == "DEFINE_ACTOR":
            key = "NORMAL"
        actor_groups[key] = actor_groups.get(key, 0) + 1

    object_groups: dict[str, int] = {}
    for row in object_rows:
        key = str(row["macro"]).removeprefix("DEFINE_OBJECT_")
        if key == "DEFINE_OBJECT":
            key = "NORMAL"
        object_groups[key] = object_groups.get(key, 0) + 1

    return {
        "n64_root": rel(n64_root),
        "n64_commit": git_commit(n64_root),
        "sources": {name: rel(path) for name, path in sources.items()},
        "actor_count": len(actor_rows),
        "actor_groups": actor_groups,
        "actor_category_count": len(actor_category["entries"]),
        "object_count": len(object_rows),
        "object_groups": object_groups,
        "item_enum_count": len(item_enums),
        "item_enum_value_count": sum(len(enum["entries"]) for enum in item_enums),
        "actors": actor_rows,
        "actor_category": actor_category,
        "objects": object_rows,
        "item_enums": item_enums,
    }


def emit_enum(lines: list[str], typedef_name: str, enum_name: str, entries: list[dict[str, object]]) -> None:
    lines.append(f"typedef enum {enum_name} {{")
    for entry in entries:
        lines.append(f"    {entry['name']} = {fmt_hex(int(entry['value']))},")
    lines.append(f"}} {typedef_name};")
    lines.append("")


def make_header(report: dict[str, object]) -> str:
    lines = [
        "#ifndef OOT3D_ACTOR_OBJECT_SEMANTICS_H",
        "#define OOT3D_ACTOR_OBJECT_SEMANTICS_H",
        "",
        "/* Generated by scripts/port_n64_actor_object_semantics.py.",
        " * Text-free semantic IDs derived from zeldaret/oot actor, object, and item headers.",
        " */",
        "",
        "#include \"oot3d/types.h\"",
        "",
        f"#define OOT3D_N64_ACTOR_ID_COUNT {report['actor_count']}",
        f"#define OOT3D_N64_OBJECT_ID_COUNT {report['object_count']}",
        f"#define OOT3D_N64_ITEM_ENUM_COUNT {report['item_enum_count']}",
        "",
    ]

    emit_enum(lines, "Oot3dActorId", "Oot3dActorId", report["actors"])
    emit_enum(lines, "Oot3dActorCategory", "Oot3dActorCategory", report["actor_category"]["entries"])
    emit_enum(lines, "Oot3dObjectId", "Oot3dObjectId", report["objects"])
    for enum in report["item_enums"]:
        emit_enum(lines, f"Oot3d{enum['typedef']}", f"Oot3d{enum['name']}", enum["entries"])

    lines.append("#endif")
    lines.append("")
    return "\n".join(lines)


def make_markdown(report: dict[str, object]) -> str:
    lines = [
        "# N64 Actor/Object/Item Semantics Port",
        "",
        "Generated from `scripts/port_n64_actor_object_semantics.py`. Dialogue text and story text are not exported.",
        "",
        f"- N64 root: `{report['n64_root']}`",
        f"- N64 commit: `{report['n64_commit']}`",
        f"- Source files: {len(report['sources'])}",
        f"- Actor IDs ported: {report['actor_count']}",
        f"- Actor categories ported: {report['actor_category_count']}",
        f"- Object IDs ported: {report['object_count']}",
        f"- Item/inventory enums ported: {report['item_enum_count']}",
        f"- Item/inventory enum values ported: {report['item_enum_value_count']}",
        f"- Generated header: `include/oot3d/actor_object_semantics.h`",
        "",
        "## Source Files",
        "",
    ]
    for name, source in report["sources"].items():
        lines.append(f"- `{name}`: `{source}`")

    lines.extend(["", "## Actor Table Groups", "", "| Group | Count |", "| --- | ---: |"])
    for group, count in sorted(report["actor_groups"].items()):
        lines.append(f"| `{group}` | {count} |")

    lines.extend(["", "## Object Table Groups", "", "| Group | Count |", "| --- | ---: |"])
    for group, count in sorted(report["object_groups"].items()):
        lines.append(f"| `{group}` | {count} |")

    lines.extend(["", "## Item/Inventory Enums", "", "| Enum | Values | First | Last |", "| --- | ---: | --- | --- |"])
    for enum in report["item_enums"]:
        entries = enum["entries"]
        first = entries[0]["name"] if entries else "-"
        last = entries[-1]["name"] if entries else "-"
        lines.append(f"| `{enum['name']}` | {len(entries)} | `{first}` | `{last}` |")

    lines.extend(
        [
            "",
            "## Reuse Notes",
            "",
            "- These IDs are N64-derived semantic anchors for triage and naming in OOT3D.",
            "- Matching OOT3D tables still requires binary evidence from the 3DS executable and assets.",
            "- Actor/object segment names are preserved in the JSON report for lookup, but the C header exports only enum names and values.",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--n64-root", type=Path, default=DEFAULT_N64_ROOT)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "n64_actor_object_semantics.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "n64_actor_object_semantics.md")
    parser.add_argument("--out-header", type=Path, default=ROOT / "include" / "oot3d" / "actor_object_semantics.h")
    args = parser.parse_args()

    report = collect_semantics(args.n64_root)
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_header.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(report), encoding="utf-8")
    args.out_header.write_text(make_header(report), encoding="utf-8")
    print(
        "ported "
        f"{report['actor_count']} actor IDs, "
        f"{report['object_count']} object IDs, "
        f"{report['item_enum_count']} item enums / {report['item_enum_value_count']} values"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
