#!/usr/bin/env python3
"""Create a focused review packet for one OOT3D function and N64 candidates."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DEFAULT_N64_ROOT = ROOT.parent / "external" / "oot"

sys.path.insert(0, str(ROOT / "scripts"))
from rank_n64_port_candidates import iter_c_functions, rel  # noqa: E402
from symbol_overlay import identifier_overlay, load_manual_symbols, overlay_identifier_text  # noqa: E402


def slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_")


def load_rank(path: Path) -> dict[str, object]:
    if not path.is_file():
        raise SystemExit(f"rank report not found: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def find_entry(report: dict[str, object], entry: str) -> dict[str, object]:
    normalized = entry.lower().removeprefix("0x")
    for row in report["matches"]:
        if str(row["oot3d_entry"]).lower() == normalized:
            return row
    raise SystemExit(f"entry {normalized} not found in rank report")


def extract_n64_function(n64_root: Path, relpath: str, function_name: str) -> str:
    path = n64_root / relpath
    if not path.is_file():
        raise SystemExit(f"N64 source not found: {path}")
    for name, body in iter_c_functions(path):
        if name == function_name:
            return body
    raise SystemExit(f"function {function_name} not found in {path}")


def clean_text(text: str) -> str:
    lines = [line.rstrip() for line in text.splitlines()]
    while lines and not lines[-1]:
        lines.pop()
    return "\n".join(lines) + "\n"


def make_markdown(
    row: dict[str, object],
    out_dir: Path,
    candidate_files: list[dict[str, str]],
    symbol_overlay: bool,
) -> str:
    lines = [
        f"# Port Packet `{row['oot3d_entry']}`",
        "",
        f"- OOT3D function: `{row['oot3d_name']}`",
        f"- OOT3D decompile: `oot3d.c`",
        f"- Source export path: `{row['oot3d_path']}`",
        f"- Manual symbol overlay applied: `{symbol_overlay}`",
        "",
        "## Review Steps",
        "",
        "1. Compare `oot3d.c` with the candidate extracts below.",
        "2. Confirm whether the N64 candidate is an actual port, a broad subsystem anchor, or only a weak lexical match.",
        "3. If confirmed, add a prototype to `src/semantic_labels.c` and run `scripts/promote_manual_symbol.py` with a conservative `oot3d_*` name.",
        "4. Run `scripts\\refresh-n64-porting-plan.ps1` for the fast no-Ghidra refresh; use selective/full Ghidra export only at a checkpoint.",
        "",
        "## Candidate Extracts",
        "",
        "| Score | N64 function | Extract | Source | Evidence |",
        "| ---: | --- | --- | --- | --- |",
    ]
    for item in candidate_files:
        evidence = item["evidence"]
        lines.append(
            f"| {item['score']} | `{item['n64_name']}` | `{item['extract']}` | "
            f"`{item['n64_path']}` | {evidence or '-'} |"
        )

    lines.extend(["", "## Promotion Template", ""])
    suggested = f"oot3d_{str(row['oot3d_name']).lower()}" if str(row["oot3d_name"]).startswith("FUN_") else row["oot3d_name"]
    lines.append("```powershell")
    lines.append(
        "python scripts\\promote_manual_symbol.py "
        f"--entry {row['oot3d_entry']} "
        f"--new-name {suggested} "
        "--source-file src/semantic_labels.c "
        "--confidence medium "
        '--notes "N64 port candidate packet reviewed; replace this with specific evidence."'
    )
    lines.append("```")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--entry", required=True, help="OOT3D entry address present in a rank report")
    parser.add_argument("--rank-json", type=Path, default=ANALYSIS / "n64_port_candidate_rank.json")
    parser.add_argument("--n64-root", type=Path, default=DEFAULT_N64_ROOT)
    parser.add_argument("--top", type=int, default=4)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--no-symbol-overlay", action="store_true")
    args = parser.parse_args()

    report = load_rank(args.rank_json)
    row = find_entry(report, args.entry)
    out_dir = args.out_dir or (ANALYSIS / "port_packets" / str(row["oot3d_entry"]).lower())
    out_dir.mkdir(parents=True, exist_ok=True)

    oot3d_source = ROOT / str(row["oot3d_path"])
    if not oot3d_source.is_file():
        raise SystemExit(f"OOT3D decompile not found: {oot3d_source}")
    oot3d_text = oot3d_source.read_text(encoding="utf-8", errors="replace")
    use_symbol_overlay = not args.no_symbol_overlay
    if use_symbol_overlay:
        oot3d_text = overlay_identifier_text(oot3d_text, identifier_overlay(load_manual_symbols()))
    (out_dir / "oot3d.c").write_text(clean_text(oot3d_text), encoding="utf-8")

    candidate_files: list[dict[str, str]] = []
    for index, candidate in enumerate(row["candidates"][: args.top], start=1):
        n64_name = str(candidate["n64_name"])
        n64_path = str(candidate["n64_path"])
        body = extract_n64_function(args.n64_root, n64_path, n64_name)
        filename = f"{index:02d}_{slug(n64_name)}.c"
        (out_dir / filename).write_text(clean_text(body), encoding="utf-8")
        evidence = []
        for label, values in candidate["evidence"].items():
            if values:
                evidence.append(f"{label}: " + ", ".join(f"`{value}`" for value in values[:6]))
        candidate_files.append(
            {
                "score": str(candidate["score"]),
                "n64_name": n64_name,
                "n64_path": n64_path,
                "extract": filename,
                "evidence": "<br>".join(evidence),
            }
        )

    (out_dir / "README.md").write_text(
        make_markdown(row, out_dir, candidate_files, use_symbol_overlay),
        encoding="utf-8",
    )
    print(f"wrote {rel(out_dir)}")
    print(f"copied OOT3D decompile and {len(candidate_files)} N64 extracts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
