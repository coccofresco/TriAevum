"""Map sampled A32 blocks to functions and locally available source evidence."""

from __future__ import annotations

import argparse
import csv
import json
import re
from dataclasses import dataclass
from pathlib import Path


FORMAT = "oot3d_hot_source_coverage_v1"


@dataclass(frozen=True)
class FunctionInterval:
    entry: int
    end: int
    name: str


def parse_integer(value: str) -> int:
    return int(value, 0)


def load_inventory(path: Path) -> list[FunctionInterval]:
    intervals: list[FunctionInterval] = []
    with path.open(newline="", encoding="utf-8-sig") as source:
        for row in csv.DictReader(source):
            entry = parse_integer(row["entry"])
            size = parse_integer(row["size"])
            if size <= 0:
                continue
            name = row.get("name") or row.get("maintained_name") or ""
            intervals.append(FunctionInterval(entry, entry + size, name))
    return intervals


def source_mentions(root: Path, names: set[str]) -> dict[str, list[str]]:
    result = {name: [] for name in names}
    searchable = sorted((name for name in names if name), key=len, reverse=True)
    if not searchable:
        return result
    pattern = re.compile(
        r"\b(" + "|".join(re.escape(name) for name in searchable) + r")\b"
    )
    for path in sorted(root.rglob("*")):
        if path.suffix.lower() not in {".c", ".cc", ".cpp"}:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        relative = path.relative_to(root).as_posix()
        for name in set(pattern.findall(text)):
            result[name].append(relative)
    return result


def analyze(
    profile: dict[str, object],
    intervals: list[FunctionInterval],
    maintained_source_root: Path,
    ghidra_root: Path,
) -> dict[str, object]:
    if profile.get("format") != "oot3d_a32_hot_block_profile_v1":
        raise ValueError("invalid A32 hot-block profile")
    total_samples = int(profile["total_samples"])
    by_function: dict[FunctionInterval, dict[str, int]] = {}
    unmapped_samples = 0
    for block in profile.get("blocks", []):
        pc = int(block["pc"])
        samples = int(block["samples"])
        matches = [item for item in intervals if item.entry <= pc < item.end]
        if not matches:
            unmapped_samples += samples
            continue
        function = min(
            matches, key=lambda item: (item.end - item.entry, -item.entry)
        )
        aggregate = by_function.setdefault(function, {"samples": 0, "blocks": 0})
        aggregate["samples"] += samples
        aggregate["blocks"] += 1

    mentions = source_mentions(
        maintained_source_root, {item.name for item in by_function}
    )
    ghidra_by_entry: dict[int, list[str]] = {}
    for path in sorted(ghidra_root.rglob("*.c")):
        match = re.search(r"_([0-9a-fA-F]{8})_", path.name)
        if match:
            relative = path.relative_to(ghidra_root).as_posix()
            ghidra_by_entry.setdefault(int(match.group(1), 16), []).append(relative)

    functions = []
    for function, aggregate in sorted(
        by_function.items(), key=lambda item: (-item[1]["samples"], item[0].entry)
    ):
        samples = aggregate["samples"]
        functions.append(
            {
                "entry": function.entry,
                "end": function.end,
                "name": function.name,
                "samples": samples,
                "total_profile_coverage": samples / total_samples,
                "profiled_blocks": aggregate["blocks"],
                "maintained_source_mentions": mentions.get(function.name, []),
                "ghidra_pseudocode": ghidra_by_entry.get(function.entry, []),
            }
        )
    return {
        "format": FORMAT,
        "profile_format": profile["format"],
        "profile_total_samples": total_samples,
        "profile_selected_samples": int(profile["selected_samples"]),
        "mapped_samples": sum(item["samples"] for item in functions),
        "unmapped_samples": unmapped_samples,
        "mapped_functions": len(functions),
        "functions": functions,
    }


def render_markdown(report: dict[str, object], maximum: int) -> str:
    functions = report["functions"]
    lines = [
        "# OOT3D Title Intro Hot Source Coverage",
        "",
        "Generated from the deterministic title/logo A32 profile and the local",
        "function inventory. A maintained-source mention is a discovery aid, not",
        "proof that the function is exact or host-compilable.",
        "",
        f"- Profile samples: {report['profile_total_samples']}",
        f"- Selected samples: {report['profile_selected_samples']}",
        f"- Mapped samples: {report['mapped_samples']}",
        f"- Unmapped samples: {report['unmapped_samples']}",
        f"- Mapped functions: {report['mapped_functions']}",
        "",
        "| Rank | Coverage | Samples | Entry | Function | Maintained source | Ghidra |",
        "| ---: | ---: | ---: | ---: | --- | --- | --- |",
    ]
    for rank, item in enumerate(functions[:maximum], 1):
        source = ", ".join(item["maintained_source_mentions"]) or "missing"
        ghidra = ", ".join(item["ghidra_pseudocode"]) or "missing"
        lines.append(
            f"| {rank} | {item['total_profile_coverage']:.3%} | "
            f"{item['samples']} | `0x{item['entry']:08X}` | "
            f"`{item['name']}` | {source} | {ghidra} |"
        )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--maintained-source-root", type=Path, required=True)
    parser.add_argument("--ghidra-root", type=Path, required=True)
    parser.add_argument("--json-output", type=Path, required=True)
    parser.add_argument("--markdown-output", type=Path, required=True)
    parser.add_argument("--markdown-functions", type=int, default=50)
    args = parser.parse_args()
    if args.markdown_functions <= 0:
        raise ValueError("--markdown-functions must be positive")

    profile = json.loads(args.profile.read_text(encoding="utf-8"))
    report = analyze(
        profile,
        load_inventory(args.inventory),
        args.maintained_source_root,
        args.ghidra_root,
    )
    args.json_output.parent.mkdir(parents=True, exist_ok=True)
    args.markdown_output.parent.mkdir(parents=True, exist_ok=True)
    args.json_output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    args.markdown_output.write_text(
        render_markdown(report, args.markdown_functions),
        encoding="utf-8",
        newline="\n",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
