"""Attribute external stack observations; never equate sample counts with time."""
import argparse
import bisect
from collections import Counter
import json
from pathlib import Path
import re
from sample_aot_windows import read_map, classify_symbol, sha256


def summarize(data, module_path, entries):
    target = str(module_path.resolve()).lower()
    modules = sorted(data["modules"], key=lambda m: m["base"])
    if not any(str(Path(m["path"]).resolve()).lower() == target for m in modules):
        raise ValueError("Requested AOT module is absent from the captured process")
    starts = [m["base"] for m in modules]
    symbol_starts = [pc for pc, _ in entries]

    def resolve(pc, returned=False):
        pc -= int(returned)  # attribute return PCs to the calling instruction
        idx = bisect.bisect_right(starts, pc) - 1
        if idx < 0 or pc >= modules[idx]["base"] + modules[idx]["size"]:
            return None, None, None
        module = modules[idx]
        rva = pc - module["base"]
        if str(Path(module["path"]).resolve()).lower() != target:
            return module["path"], None, rva
        sym = bisect.bisect_right(symbol_starts, rva) - 1
        return module["path"], entries[sym][1] if sym >= 0 else None, rva

    def owner(symbol):
        if not symbol or not symbol.startswith("?Execute_"):
            return None
        match = re.search(r"_([0-9A-F]{8})(?:_(?:Unobserved|Observed))?@", symbol)
        return match[1] if match else None

    leaves, direct, inclusive, paths = Counter(), Counter(), Counter(), Counter()
    direct_totals = Counter()
    module_leaves, depths = Counter(), Counter()
    aot_samples = unresolved = 0
    for record in data["stacks"]:
        count = record["count"]
        frames = [resolve(pc, i != 0) for i, pc in enumerate(record["pcs"])]
        if not frames:
            continue
        depths[len(frames)] += count
        module_leaves[frames[0][0] or "unknown"] += count
        if not frames[0][0] or str(Path(frames[0][0]).resolve()).lower() != target:
            continue
        aot_samples += count
        category = classify_symbol(frames[0][1])
        leaves[category] += count
        owners = [(owner(symbol), rva) for _, symbol, rva in frames if owner(symbol)]
        if owners:
            direct[(owners[0][0], category)] += count
            direct_totals[owners[0][0]] += count
            paths[tuple(o for o, _ in owners)] += count
            for entry in set(o for o, _ in owners):
                inclusive[entry] += count
        else:
            unresolved += count
    return {
        "method": "AOT leaf-only stack attribution; inclusive rows overlap; no time conversion",
        "samples": data["samples"], "aot_leaf_samples": aot_samples,
        "aot_without_translated_owner": unresolved,
        "module_leaves": dict(module_leaves), "stack_depths": dict(depths),
        "aot_categories": dict(leaves),
        "direct_owners": [{"entry": e, "category": c, "count": n}
                          for (e, c), n in direct.most_common()],
        "direct_totals": dict(direct_totals.most_common()),
        "inclusive_owners": dict(inclusive.most_common()),
        "paths": [{"entries": p, "count": n} for p, n in paths.most_common()],
    }


def main():
    p = argparse.ArgumentParser(__doc__)
    for name in ("input", "module", "map", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    args = p.parse_args()
    report = summarize(json.loads(args.input.read_text()), args.module, read_map(args.map))
    report.update(module_sha256=sha256(args.module), map_sha256=sha256(args.map))
    args.output.write_text(json.dumps(report, indent=2))
    print(json.dumps({k: report[k] for k in ("samples", "aot_leaf_samples",
                                           "aot_without_translated_owner", "aot_categories")}))
    print(json.dumps(report["direct_owners"][:20], indent=2))


if __name__ == "__main__":
    main()
