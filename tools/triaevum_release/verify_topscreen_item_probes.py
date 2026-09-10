#!/usr/bin/env python3
"""Check phase-shifted product input probes; framebuffer inspection is separate."""

import argparse
import json
from pathlib import Path


FIELDS = (
    "topscreen_input_native_updates",
    "topscreen_input_zr_press_updates",
    "topscreen_input_zl_press_updates",
    "topscreen_items_selection_updates",
    "topscreen_items_selection_begins",
    "topscreen_items_selection_completions",
    "topscreen_item_query_calls",
    "topscreen_item_query_true_results",
)


def counters(document):
    found = {}

    def visit(value):
        if isinstance(value, dict):
            for key, child in value.items():
                if key in FIELDS:
                    if key in found:
                        raise ValueError(f"duplicate diagnostic: {key}")
                    found[key] = child
                else:
                    visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)

    visit(document)
    for key in FIELDS:
        value = found.get(key)
        if key.endswith(("_calls", "_results")):
            valid = isinstance(value, list) and len(value) == 4
            values = value if valid else []
        else:
            valid = value is not None
            values = [value]
        if not valid or any(type(x) is not int or x < 0 for x in values):
            raise ValueError(f"missing or invalid diagnostic: {key}")
    return found


def verify(documents, *, updates, zr_presses, zl_presses, assignments):
    if len(documents) < 2:
        raise ValueError("at least two refresh phases are required")
    if updates <= 0 or min(zr_presses, zl_presses, assignments) <= 0:
        raise ValueError("positive update, press and assignment expectations are required")
    expected = {
        "topscreen_input_native_updates": updates,
        "topscreen_input_zr_press_updates": zr_presses,
        "topscreen_input_zl_press_updates": zl_presses,
        "topscreen_items_selection_begins": assignments,
        "topscreen_items_selection_completions": assignments,
    }
    baseline = None
    for document in documents:
        current = counters(document)
        for key, count in expected.items():
            if current[key] != count:
                raise ValueError(f"{key}: expected {count}, got {current[key]}")
        if not all(current["topscreen_item_query_calls"]):
            raise ValueError("not all four product query paths were reached")
        if current["topscreen_items_selection_updates"] < assignments:
            raise ValueError("assignment owner did not run")
        if baseline is not None and current != baseline:
            raise ValueError("product input behavior differs between refresh phases")
        baseline = current
    return baseline


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path)
    parser.add_argument("--updates", type=int, required=True)
    parser.add_argument("--zr-presses", type=int, required=True)
    parser.add_argument("--zl-presses", type=int, required=True)
    parser.add_argument("--assignments", type=int, required=True)
    args = parser.parse_args()
    try:
        result = verify([json.loads(p.read_text(encoding="utf-8")) for p in args.reports],
                        updates=args.updates, zr_presses=args.zr_presses,
                        zl_presses=args.zl_presses, assignments=args.assignments)
    except (OSError, ValueError) as error:
        parser.exit(1, f"TopScreen phase verification failed: {error}\n")
    print(json.dumps({"phase_parity": True, "counters": result}, indent=2))
    print("This verifies routing/cadence, not item-use or visual parity.")


if __name__ == "__main__":
    main()
