#!/usr/bin/env python3
"""Port text-free N64 OoT message semantics into local OOT3D artifacts."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DEFAULT_N64_ROOT = ROOT.parent / "external" / "oot"

ENUMS_TO_PORT = [
    "TextBoxIcon",
    "MessageMode",
    "TextState",
    "TextColor",
    "TextboxBackgroundIndex",
    "TextboxBackgroundForegroundColor",
    "TextboxBackgroundBackgroundColor",
    "TextboxBackgroundYOffsetIndex",
    "TextBoxType",
    "TextBoxBackground",
    "TextBoxPosition",
]

DEFINE_PREFIXES = (
    "MESSAGE_",
    "MESSAGE_WIDE_",
    "MESSAGE_CHAR_",
    "TEXTBOX_ENDTYPE_",
    "MESSAGE_STATIC_TEX_SIZE",
    "MESSAGE_TEXTURE_STATIC_TEX_SIZE",
)

DEFINE_RE = re.compile(r"^#define\s+(?P<name>[A-Z0-9_]+)\s+(?P<value>0x[0-9A-Fa-f]+|\d+)\b", re.MULTILINE)
ENUM_RE = re.compile(r"typedef\s+enum\s+(?P<name>[A-Za-z0-9_]+)\s*\{(?P<body>.*?)\}\s*(?P<typedef>[A-Za-z0-9_]+)\s*;", re.DOTALL)
COMMENT_RE = re.compile(r"/\*.*?\*/|//.*?$", re.DOTALL | re.MULTILINE)
ENUM_ENTRY_RE = re.compile(r"(?P<name>[A-Z][A-Z0-9_]+)(?:\s*=\s*(?P<value>0x[0-9A-Fa-f]+|\d+))?")


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


def load_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def parse_defines(text: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for match in DEFINE_RE.finditer(text):
        name = match.group("name")
        if not name.startswith(DEFINE_PREFIXES):
            continue
        rows.append({"name": name, "value": parse_int(match.group("value"))})
    return rows


def parse_enums(text: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for match in ENUM_RE.finditer(text):
        enum_name = match.group("name")
        if enum_name not in ENUMS_TO_PORT:
            continue
        body = COMMENT_RE.sub("", match.group("body"))
        next_value = 0
        entries: list[dict[str, object]] = []
        for part in body.split(","):
            entry_match = ENUM_ENTRY_RE.search(part.strip())
            if not entry_match:
                continue
            explicit = entry_match.group("value")
            value = parse_int(explicit) if explicit else next_value
            entries.append({"name": entry_match.group("name"), "value": value})
            next_value = value + 1
        rows.append({"name": enum_name, "typedef": match.group("typedef"), "entries": entries})
    return rows


def collect_semantics(n64_root: Path) -> dict[str, object]:
    sources = [
        n64_root / "include" / "message.h",
        n64_root / "include" / "message_data_fmt.h",
        n64_root / "include" / "message_data_static.h",
    ]
    missing = [path for path in sources if not path.is_file()]
    if missing:
        raise SystemExit("Missing N64 message sources: " + ", ".join(str(path) for path in missing))

    define_rows: list[dict[str, object]] = []
    enum_rows: list[dict[str, object]] = []
    source_map: dict[str, str] = {}
    for path in sources:
        text = load_text(path)
        defines = parse_defines(text)
        enums = parse_enums(text)
        for row in defines:
            source_map[row["name"]] = rel(path)
        for row in enums:
            source_map[row["name"]] = rel(path)
        define_rows.extend(defines)
        enum_rows.extend(enums)

    seen_defines: set[str] = set()
    unique_defines = []
    for row in define_rows:
        if row["name"] in seen_defines:
            continue
        seen_defines.add(str(row["name"]))
        unique_defines.append(row)

    return {
        "n64_root": rel(n64_root),
        "n64_commit": git_commit(n64_root),
        "sources": [rel(path) for path in sources],
        "define_count": len(unique_defines),
        "enum_count": len(enum_rows),
        "enum_value_count": sum(len(row["entries"]) for row in enum_rows),
        "defines": unique_defines,
        "enums": enum_rows,
        "source_map": source_map,
    }


def make_header(report: dict[str, object]) -> str:
    lines = [
        "#ifndef OOT3D_MESSAGE_SEMANTICS_H",
        "#define OOT3D_MESSAGE_SEMANTICS_H",
        "",
        "/* Generated by scripts/port_n64_message_semantics.py.",
        " * Text-free semantic constants derived from zeldaret/oot message headers.",
        " */",
        "",
        "#include \"oot3d/types.h\"",
        "",
    ]
    for row in report["defines"]:
        lines.append(f"#define {row['name']} {fmt_hex(int(row['value']))}")
    lines.append("")

    for enum in report["enums"]:
        lines.append(f"typedef enum Oot3d{enum['name']} {{")
        for entry in enum["entries"]:
            lines.append(f"    {entry['name']} = {fmt_hex(int(entry['value']))},")
        lines.append(f"}} Oot3d{enum['typedef']};")
        lines.append("")

    lines.append("#endif")
    lines.append("")
    return "\n".join(lines)


def make_markdown(report: dict[str, object]) -> str:
    lines = [
        "# N64 Message Semantics Port",
        "",
        "Generated from `scripts/port_n64_message_semantics.py`. Dialogue text is not exported.",
        "",
        f"- N64 root: `{report['n64_root']}`",
        f"- N64 commit: `{report['n64_commit']}`",
        f"- Source headers: {len(report['sources'])}",
        f"- Ported object-like defines: {report['define_count']}",
        f"- Ported enums: {report['enum_count']}",
        f"- Ported enum values: {report['enum_value_count']}",
        f"- Generated header: `include/oot3d/message_semantics.h`",
        "",
        "## Source Headers",
        "",
    ]
    for source in report["sources"]:
        lines.append(f"- `{source}`")

    lines.extend(
        [
            "",
            "## Ported Enums",
            "",
            "| Enum | Values | First | Last |",
            "| --- | ---: | --- | --- |",
        ]
    )
    for enum in report["enums"]:
        entries = enum["entries"]
        first = entries[0]["name"] if entries else "-"
        last = entries[-1]["name"] if entries else "-"
        lines.append(f"| `{enum['name']}` | {len(entries)} | `{first}` | `{last}` |")

    lines.extend(
        [
            "",
            "## Define Groups",
            "",
            "| Group | Count |",
            "| --- | ---: |",
        ]
    )
    groups: dict[str, int] = {}
    for row in report["defines"]:
        name = str(row["name"])
        if name.startswith("MESSAGE_WIDE_"):
            group = "MESSAGE_WIDE"
        elif name.startswith("MESSAGE_CHAR_"):
            group = "MESSAGE_CHAR"
        elif name.startswith("MESSAGE_"):
            group = "MESSAGE"
        elif name.startswith("TEXTBOX_ENDTYPE_"):
            group = "TEXTBOX_ENDTYPE"
        else:
            group = "OTHER"
        groups[group] = groups.get(group, 0) + 1
    for group, count in sorted(groups.items()):
        lines.append(f"| `{group}` | {count} |")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--n64-root", type=Path, default=DEFAULT_N64_ROOT)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "n64_message_semantics.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "n64_message_semantics.md")
    parser.add_argument("--out-header", type=Path, default=ROOT / "include" / "oot3d" / "message_semantics.h")
    args = parser.parse_args()

    report = collect_semantics(args.n64_root)
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_header.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(report), encoding="utf-8")
    args.out_header.write_text(make_header(report), encoding="utf-8")
    print(f"ported {report['define_count']} defines and {report['enum_value_count']} enum values")
    print(f"wrote {args.out_json}")
    print(f"wrote {args.out_md}")
    print(f"wrote {args.out_header}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
