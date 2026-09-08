"""Offline address-window evidence for porting a title to another ROM revision.

Matches are migration candidates, never proof of equivalent functions or a
runtime relocation table. No source, ROM, recipe or runtime patch is changed.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct

try:
    from .common import atomic_write_json
except ImportError:
    from common import atomic_write_json


def text_layout(code: bytes, exheader: bytes) -> tuple[int, int]:
    if len(exheader) < 0x20:
        raise ValueError("truncated ExHeader")
    base, pages, size = struct.unpack_from("<III", exheader, 0x10)
    if base % 4 or not size or size > pages * 0x1000 or pages * 0x1000 > len(code):
        raise ValueError("invalid ExHeader text bounds")
    return base, size


def compare_windows(reference: bytes, target: bytes, addresses: list[int], *,
                    reference_base: int, target_base: int,
                    window: int = 32) -> list[dict]:
    if window < 16 or window % 4:
        raise ValueError("window must be a multiple of four, at least 16 bytes")
    signatures: dict[bytes, list[int]] = defaultdict(list)
    records = []
    for address in sorted(set(addresses)):
        offset = address - reference_base
        record = {"reference_address": address}
        records.append(record)
        if offset < 0 or offset % 4 or offset + window > len(reference):
            record["status"] = "outside_text_window"
            continue
        signature = reference[offset:offset + window]
        signatures[signature].append(len(records) - 1)
        record["window_sha256"] = hashlib.sha256(signature).hexdigest()
    hits: dict[bytes, list[int]] = defaultdict(list)
    counts: Counter = Counter()
    for offset in range(0, len(target) - window + 1, 4):
        signature = target[offset:offset + window]
        if signature in signatures:
            counts[signature] += 1
            if len(hits[signature]) < 8:
                hits[signature].append(target_base + offset)
    for signature, indices in signatures.items():
        for index in indices:
            record = records[index]
            count = counts[signature]
            record["candidate_count"] = count
            record["candidate_addresses"] = hits[signature]
            address = record["reference_address"]
            offset = address - target_base
            record["same_address_bytes_equal"] = (
                offset >= 0 and target[offset:offset + window] == signature)
            if count == 1:
                destination = hits[signature][0]
                record["status"] = "unique_same_address" if destination == address else "unique_relocated"
                record["delta"] = destination - address
            else:
                record["status"] = "ambiguous" if count else "changed_or_missing"
    return records


def audit(reference_code: Path, reference_exheader: Path, target_code: Path,
          target_exheader: Path, selection: Path, source_root: Path | None) -> dict:
    source = reference_code.read_bytes()
    target = target_code.read_bytes()
    source_base, source_size = text_layout(source, reference_exheader.read_bytes())
    target_base, target_size = text_layout(target, target_exheader.read_bytes())
    entries = json.loads(selection.read_text(encoding="utf-8-sig"))["functions"]
    labels = defaultdict(list)
    for entry in entries:
        address = int(entry["entry"])
        labels[address].append({"kind": "selected_function", "name": entry["name"]})
    if source_root:
        # Lexical inventory only: literals can be data, masks or dead code.
        # Classification stays explicit; no inferred hook is applied at runtime.
        for path in sorted(source_root.rglob("*")):
            if path.suffix not in {".h", ".cpp", ".inl"} or not path.is_file():
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
                for match in re.finditer(r"\b0[xX]([0-9a-fA-F]{6,8})(?:[uUlL]*)\b", line):
                    address = int(match.group(1), 16)
                    if source_base <= address < source_base + source_size:
                        labels[address].append({"kind": "source_address_literal",
                            "path": path.relative_to(source_root).as_posix(), "line": number})
    records = compare_windows(source[:source_size], target[:target_size], list(labels),
        reference_base=source_base, target_base=target_base)
    for record in records:
        record["uses"] = labels[record["reference_address"]]
    def summary(kind):
        chosen = [r for r in records if any(u["kind"] == kind for u in r["uses"])]
        return {"addresses": len(chosen), "statuses": dict(Counter(r["status"] for r in chosen)),
            "unique_match_deltas": dict(Counter(str(r["delta"]) for r in chosen if "delta" in r))}
    return {"format": "triaevum_revision_port_audit_v1", "window_bytes": 32,
        "qualification": "address-window evidence only; not function equivalence or runtime compatibility",
        "reference": {"sha256": hashlib.sha256(source).hexdigest(), "text_base": source_base, "text_bytes": source_size},
        "target": {"sha256": hashlib.sha256(target).hexdigest(), "text_base": target_base, "text_bytes": target_size},
        "functions": summary("selected_function"), "source_literals": summary("source_address_literal"),
        "records": records}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("reference-code", "reference-exheader", "target-code", "target-exheader", "selection", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--source-root", type=Path)
    args = parser.parse_args()
    report = audit(args.reference_code, args.reference_exheader, args.target_code,
        args.target_exheader, args.selection, args.source_root)
    atomic_write_json(args.output, report)
    print(json.dumps({k: v for k, v in report.items() if k != "records"}, indent=2))


if __name__ == "__main__":
    main()
