#!/usr/bin/env python3
"""Audit producer-side dispatch helpers used by the title-intro record path."""

from __future__ import annotations

import csv
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

EXPORT_DIR = ANALYSIS / "title_intro_dispatch_helper_ghidra_export"
DATA_WORDS = ANALYSIS / "title_intro_dispatch_helper_data_words.csv"
PRIMARY = ANALYSIS / "title_intro_primary_producer_audit.json"
CONTEXT_EXPORT_DIR = ANALYSIS / "title_intro_context_feed_producer_ghidra_export" / "decompiled"
PLAYBACK_HEADER = ROOT / "include" / "oot3d" / "title_intro_playback_runtime.h"
PLAYBACK_SOURCE = ROOT / "src" / "code" / "z_title_intro_playback_runtime.c"

OUT_JSON = ANALYSIS / "title_intro_dispatch_helper_audit.json"
OUT_MD = ANALYSIS / "title_intro_dispatch_helper_audit.md"


HELPERS = {
    "002cfcf0": {
        "role": "dispatch_code_5_with_context",
        "expected_signal": "FUN_0030c198(local_10,5)",
    },
    "002cfd24": {
        "role": "dispatch_code_7_and_scale_literal",
        "expected_signal": "FUN_0030c198(local_10,7)",
    },
    "002cfd74": {
        "role": "rebind_payload_and_copy_value",
        "expected_signal": "FUN_002dd484(uVar2,param_1,DAT_002cfdfc,0)",
    },
    "002cfe00": {
        "role": "dispatch_code_6_with_context",
        "expected_signal": "FUN_0030c198(local_10,6)",
    },
}

EXPECTED_DATA = {
    "002cfd6c": ("scale_nonzero", "0x3f800000", "1.0"),
    "002cfd70": ("scale_zero", "0x3fb33333", "1.4"),
    "002cfdfc": ("payload_tag_or_descriptor", "0x010004e0", "2.3513385E-38"),
}


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def decompiled_path(entry: str) -> Path:
    matches = list((EXPORT_DIR / "decompiled").glob(f"*_{entry}_*.c"))
    if len(matches) != 1:
        raise FileNotFoundError(f"expected one decompile for {entry}, found {len(matches)}")
    return matches[0]


def producer_text(name: str) -> str:
    return (CONTEXT_EXPORT_DIR / name).read_text(encoding="utf-8", errors="replace")


def build_report() -> dict[str, Any]:
    primary = read_json(PRIMARY)
    data_words = {row["address"].lower(): row for row in read_csv(DATA_WORDS)}
    playback_header = PLAYBACK_HEADER.read_text(encoding="utf-8")
    playback_source = PLAYBACK_SOURCE.read_text(encoding="utf-8")
    helpers = []
    helper_checks: dict[str, bool] = {}
    for entry, spec in HELPERS.items():
        path = decompiled_path(entry)
        text = path.read_text(encoding="utf-8", errors="replace")
        helper_checks[f"{entry}_signal"] = spec["expected_signal"] in " ".join(text.split())
        helpers.append(
            {
                "entry": entry,
                "role": spec["role"],
                "decompiled": rel(path),
                "signals": {
                    "expected_signal": spec["expected_signal"],
                    "has_expected_signal": helper_checks[f"{entry}_signal"],
                    "uses_lookup_wrapper": "FUN_0030ee14" in text,
                    "releases_lookup_wrapper": "FUN_0030ede0" in text,
                    "writes_param1_plus_c": "*(undefined4 *)(param_1 + 0xc)" in text,
                    "clears_existing_payload": "FUN_003102dc(*param_1,0)" in text,
                    "sets_handle_b8_from_payload": "*(undefined4 *)(*param_1 + 0xb8) = *param_2" in text,
                },
            }
        )

    literal_rows = []
    literal_checks = {}
    for address, (role, expected_u32, expected_float) in EXPECTED_DATA.items():
        row = data_words[address]
        literal_rows.append(
            {
                "address": address,
                "role": role,
                "u32": row["u32"].lower(),
                "float32": row["float32"],
                "expected_u32": expected_u32,
                "expected_float32": expected_float,
            }
        )
        literal_checks[f"{role}_matches"] = row["u32"].lower() == expected_u32

    source_selector = producer_text("99002_002d6798_FUN_002d6798.c")
    sequence_controller = producer_text("99010_00477a1c_FUN_00477a1c.c")
    producer_checks = {
        "source_selector_calls_all_helpers": all(f"FUN_{entry}" in source_selector for entry in HELPERS),
        "sequence_controller_calls_dispatch_subset": all(
            f"FUN_{entry}" in sequence_controller for entry in ("002cfcf0", "002cfd24", "002cfd74", "002cfe00")
        ),
        "primary_dispatch_context_resolved": primary["resolved_literals"]["dispatch_context"] == "0x005b12e8",
    }
    promoted_checks = {
        "source_level_adapters_promoted": all(
            token in playback_header and token in playback_source
            for token in [
                "Oot3d_TitleIntroDispatchCode5",
                "Oot3d_TitleIntroDispatchCode6",
                "Oot3d_TitleIntroDispatchCode7AndScale",
                "Oot3d_TitleIntroRebindPayload",
            ]
        ),
        "native_entrypoints_promoted": all(
            token in playback_header
            for token in [
                "OOT3D_TITLE_INTRO_DISPATCH_CODE5_ENTRY",
                "OOT3D_TITLE_INTRO_DISPATCH_CODE7_SCALE_ENTRY",
                "OOT3D_TITLE_INTRO_DISPATCH_PAYLOAD_REBIND_ENTRY",
                "OOT3D_TITLE_INTRO_DISPATCH_CODE6_ENTRY",
            ]
        ),
        "dispatch_gate_promoted": "OOT3D_TITLE_INTRO_DISPATCH_GATE_CONTEXT_ADDR" in playback_header
        and "Oot3d_TitleIntroDispatchGateOpen" in playback_source,
    }
    checks = {
        "four_helpers_exported": len(helpers) == 4,
        **helper_checks,
        **literal_checks,
        **producer_checks,
        **promoted_checks,
    }
    return {
        "format": "oot3d_title_intro_dispatch_helper_audit_v1",
        "inputs": {
            "helper_export": rel(EXPORT_DIR),
            "data_words": rel(DATA_WORDS),
            "primary_producer": rel(PRIMARY),
            "playback_header": rel(PLAYBACK_HEADER),
            "playback_source": rel(PLAYBACK_SOURCE),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "dispatch_context": primary["resolved_literals"]["dispatch_context"],
            "next_gate": "Use the promoted dispatch adapters inside the open-title scene orchestration path.",
        },
        "helpers": helpers,
        "literals": literal_rows,
        "producer_callers": [
            {
                "entry": "002d6798",
                "role": "record source selector final dispatch",
                "decompiled": "analysis/title_intro_context_feed_producer_ghidra_export/decompiled/99002_002d6798_FUN_002d6798.c",
            },
            {
                "entry": "00477a1c",
                "role": "sequence/record-B dispatch path using same helper family",
                "decompiled": "analysis/title_intro_context_feed_producer_ghidra_export/decompiled/99010_00477a1c_FUN_00477a1c.c",
            },
        ],
        "unresolved": [
            "FUN_0030EE14/FUN_0030EDE0/FUN_0030C198 still need naming; this audit only fixes their observed helper-code use.",
            "The payload descriptor 0x010004E0 is exported and bound, but its structure still needs a broader allocator/resource audit.",
            "The helper roles are promoted into maintained runtime code, but complete in-engine title playback still needs the open-title scene orchestration path.",
        ],
    }


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro Dispatch Helper Audit",
        "",
        "This audit types the four producer-side helper calls that complete the decoded title-intro record dispatch path.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- Dispatch context: `{data['summary']['dispatch_context']}`",
        f"- Next gate: {data['summary']['next_gate']}",
        "",
        "## Checks",
        "",
        "| Check | Status |",
        "| --- | --- |",
    ]
    for key, value in data["summary"]["checks"].items():
        lines.append(f"| `{key}` | `{value}` |")

    lines.extend(["", "## Helpers", "", "| Entry | Role | Signals |", "| --- | --- | --- |"])
    for helper in data["helpers"]:
        signals = [key for key, value in helper["signals"].items() if key != "expected_signal" and value]
        lines.append(f"| `{helper['entry']}` | `{helper['role']}` | `{';'.join(signals)}` |")

    lines.extend(["", "## Literals", "", "| Address | Role | U32 | Float |", "| --- | --- | --- | ---: |"])
    for row in data["literals"]:
        lines.append(f"| `{row['address']}` | `{row['role']}` | `{row['u32']}` | {row['float32']} |")

    lines.extend(["", "## Unresolved", ""])
    for item in data["unresolved"]:
        lines.append(f"- {item}")
    write_text(OUT_MD, "\n".join(lines) + "\n")


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title-intro dispatch helper audit failed")


if __name__ == "__main__":
    main()
