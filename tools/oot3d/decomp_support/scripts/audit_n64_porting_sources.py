#!/usr/bin/env python3
"""Audit zeldaret/oot sources that can guide OOT3D reconstruction."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DEFAULT_N64_ROOT = ROOT.parent / "external" / "oot"
FUNC_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_*\s]+\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*\{", re.MULTILINE)

PRIORITY_FILES = [
    "src/code/z_message.c",
    "include/message.h",
    "include/message_data_fmt.h",
    "include/message_data_static.h",
    "src/code/game.c",
    "src/code/graph.c",
    "src/code/object_table.c",
    "src/code/z_camera.c",
    "src/code/z_actor.c",
    "src/code/z_player_lib.c",
    "src/code/z_play.c",
    "src/code/z_scene.c",
    "src/code/z_map_data.c",
]


def rel(path: Path, root: Path = ROOT) -> str:
    try:
        return str(path.relative_to(root)).replace("\\", "/")
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


def count_functions(path: Path) -> int:
    if path.suffix != ".c":
        return 0
    text = path.read_text(encoding="utf-8", errors="replace")
    return len(FUNC_RE.findall(text))


def bucket_source(path: Path, n64_root: Path) -> str:
    relpath = rel(path, n64_root)
    if relpath.startswith("src/overlays/actors/"):
        return "actors"
    if relpath.startswith("src/overlays/effects/"):
        return "effects"
    if relpath.startswith("src/overlays/gamestates/"):
        return "gamestates"
    if relpath.startswith("src/overlays/misc/"):
        return "misc_overlays"
    if relpath.startswith("src/code/"):
        return "engine_code"
    if relpath.startswith("include/"):
        return "headers"
    return "other"


def make_markdown(report: dict[str, object]) -> str:
    lines = [
        "# N64 Porting Sources",
        "",
        "Generated from `scripts/audit_n64_porting_sources.py`.",
        "",
        f"- N64 root: `{report['n64_root']}`",
        f"- N64 commit: `{report['n64_commit']}`",
        f"- Source/header files audited: {report['source_file_count']}",
        f"- Estimated C functions in audited source: {report['function_count']}",
        "",
        "## Buckets",
        "",
        "| Bucket | Files | Estimated C functions |",
        "| --- | ---: | ---: |",
    ]
    for row in report["buckets"]:
        lines.append(f"| `{row['bucket']}` | {row['files']} | {row['functions']} |")

    lines.extend(
        [
            "",
            "## Priority Porting Order",
            "",
            "1. Message system: IDs, textbox metadata, control-code semantics, and message state names.",
            "2. Actor and item semantics: names, enum values, state-machine roles, and function boundaries.",
            "3. Scene/object tables: object IDs, scene names, room/actor placement semantics.",
            "4. Math/collision/camera helpers: port names and structures, then match ARM helpers incrementally.",
            "5. Rendering/audio internals: use N64 names as semantic anchors only; OOT3D platform code diverges heavily.",
            "",
            "## Priority Files",
            "",
            "| File | Exists | Estimated functions | Reason |",
            "| --- | --- | ---: | --- |",
        ]
    )
    for row in report["priority_files"]:
        lines.append(f"| `{row['path']}` | {row['exists']} | {row['functions']} | {row['reason']} |")

    lines.extend(
        [
            "",
            "## Largest Actor Sources",
            "",
            "| File | Estimated functions |",
            "| --- | ---: |",
        ]
    )
    for row in report["largest_actor_sources"][:20]:
        lines.append(f"| `{row['path']}` | {row['functions']} |")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--n64-root", type=Path, default=DEFAULT_N64_ROOT)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "n64_porting_sources.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "n64_porting_sources.md")
    args = parser.parse_args()

    n64_root = args.n64_root
    if not n64_root.is_dir():
        raise SystemExit(f"N64 source root not found: {n64_root}")

    source_files = sorted([*n64_root.glob("src/**/*.c"), *n64_root.glob("include/**/*.h")])
    bucket_counts: dict[str, Counter[str]] = {}
    source_rows: list[dict[str, object]] = []
    for path in source_files:
        funcs = count_functions(path)
        bucket = bucket_source(path, n64_root)
        bucket_counts.setdefault(bucket, Counter())
        bucket_counts[bucket]["files"] += 1
        bucket_counts[bucket]["functions"] += funcs
        source_rows.append({"path": rel(path, n64_root), "bucket": bucket, "functions": funcs})

    reasons = {
        "src/code/z_message.c": "N64 message state machine and public Message_* naming.",
        "include/message.h": "Message context, state, textbox, and icon declarations.",
        "include/message_data_fmt.h": "Text control codes and textbox metadata enums.",
        "include/message_data_static.h": "Message table entry layout and DEFINE_MESSAGE metadata.",
        "src/code/game.c": "Main game-state loop and engine callback structure.",
        "src/code/graph.c": "Graphics context orchestration names.",
        "src/code/object_table.c": "Object table semantics and object ID anchors.",
        "src/code/z_camera.c": "Camera state names and mode semantics.",
        "src/code/z_actor.c": "Actor lifecycle names and category semantics.",
        "src/code/z_player_lib.c": "Player helper naming and action semantics.",
        "src/code/z_play.c": "Play state lifecycle and scene runtime structure.",
        "src/code/z_scene.c": "Scene command processing semantics.",
        "src/code/z_map_data.c": "Map/minimap metadata anchors.",
    }
    priority_files = []
    for relpath in PRIORITY_FILES:
        path = n64_root / relpath
        priority_files.append(
            {
                "path": relpath,
                "exists": path.is_file(),
                "functions": count_functions(path) if path.is_file() else 0,
                "reason": reasons.get(relpath, "Port naming and structural semantics."),
            }
        )

    actor_sources = sorted(
        (row for row in source_rows if row["bucket"] == "actors"),
        key=lambda row: (-int(row["functions"]), str(row["path"])),
    )
    report = {
        "n64_root": rel(n64_root),
        "n64_commit": git_commit(n64_root),
        "source_file_count": len(source_files),
        "function_count": sum(int(row["functions"]) for row in source_rows),
        "buckets": [
            {"bucket": bucket, "files": counts["files"], "functions": counts["functions"]}
            for bucket, counts in sorted(bucket_counts.items())
        ],
        "priority_files": priority_files,
        "largest_actor_sources": actor_sources[:50],
    }
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(report), encoding="utf-8")
    print(f"wrote {args.out_json}")
    print(f"wrote {args.out_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
