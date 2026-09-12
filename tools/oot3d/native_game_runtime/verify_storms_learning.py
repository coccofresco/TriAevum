#!/usr/bin/env python3
"""Read-only Song of Storms learning check for the EUR baseline fixture.

This is a title-specific qualification tool, not a runtime gameplay rule.
The caller must use the documented EUR baseline and original unlearned save.
"""

import argparse
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "source_overlay"))
from compare_savestates import load


def snapshot(path):
    regions = load(path, False)["process"]["memory"]["regions"]

    def read(address, fmt):
        for region in regions:
            offset = address - region["base_address"]
            if 0 <= offset <= len(region["bytes"]) - struct.calcsize(fmt):
                return struct.unpack_from(fmt, region["bytes"], offset)[0]
        raise ValueError(f"Unmapped baseline address: {address:#x}")

    return {
        "quest_flags": read(0x587A14, "<I"),
        "lesson_gate_event": read(0x58884E, "<H"),
        "lesson_completion_event": read(0x588850, "<H"),
    }


def verify(before, after):
    # Native song table: Storms owns quest bit 17. EnFu's completion action
    # 00104720 calls Item_Give(0x65), then sets event word +0xF8 bit 0x20.
    return {
        "song_was_unlearned": not bool(before["quest_flags"] & 0x20000),
        "song_is_learned": bool(after["quest_flags"] & 0x20000),
        "only_expected_quest_bit_changed":
            before["quest_flags"] ^ after["quest_flags"] == 0x20000,
        "completion_event_was_clear":
            not bool(before["lesson_completion_event"] & 0x20),
        "completion_event_is_set": bool(after["lesson_completion_event"] & 0x20),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    args = parser.parse_args()
    before, after = snapshot(args.before), snapshot(args.after)
    checks = verify(before, after)
    print(json.dumps({"before": before, "after": after, "checks": checks,
                      "passed": all(checks.values())}, indent=2))
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
