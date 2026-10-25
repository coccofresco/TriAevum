# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes random JSON values with the Python Forge's three json.dumps spellings."""

import json
import random
import sys


def value(rng, depth=0):
    kind = rng.randrange(9 if depth < 4 else 6)
    if kind == 0:
        return None
    if kind == 1:
        return rng.random() < 0.5
    if kind == 2:
        return rng.randrange(-2**63, 2**64)
    if kind == 3:
        choice = rng.randrange(5)
        if choice == 0:
            return rng.uniform(-1e6, 1e6)
        if choice == 1:
            return rng.random() * 10.0 ** rng.randrange(-30, 30)
        if choice == 2:
            return float(rng.randrange(-10**6, 10**6))
        if choice == 3:
            return rng.choice([0.0, -0.0, 1e16, 1e-5, 0.0001, 123456789012345678.0])
        return rng.randrange(-1000, 1000) / 64.0
    if kind in (4, 5):
        alphabet = "abcXYZ 09_-/\\\"\n\t\x01\x1fé中\U0001F600"
        return "".join(rng.choice(alphabet) for _ in range(rng.randrange(0, 12)))
    if kind == 6:
        return [value(rng, depth + 1) for _ in range(rng.randrange(0, 5))]
    keys = ["a", "B", "z", "_", "é", "aa", "Z1", "\U0001F600", "中"]
    return {rng.choice(keys) + str(rng.randrange(3)): value(rng, depth + 1)
            for _ in range(rng.randrange(0, 6))}


def main(output, count=3000):
    rng = random.Random(20261025)
    cases = []
    for _ in range(count):
        item = value(rng)
        cases.append({
            "input": json.dumps(item, ensure_ascii=False),
            "pretty": json.dumps(item, indent=2, sort_keys=True),
            "compact": json.dumps(item, sort_keys=True, separators=(",", ":")),
            "spaced": json.dumps(item, sort_keys=True),
            "pretty_unicode": json.dumps(item, indent=2, sort_keys=True, ensure_ascii=False),
        })
    with open(output, "w", encoding="utf-8") as stream:
        json.dump(cases, stream, ensure_ascii=False)


if __name__ == "__main__":
    main(sys.argv[1])
