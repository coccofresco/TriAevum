"""Read-only OOT3D shader/material census; emits metadata, never asset payloads.

DVLB/DVLP/DVLE layout: nihstro/include/nihstro/shader_binary.h.
CMB layout: native 0x004C34AC / 0x003146E4 consumer contracts.
This scans uncompressed embedded CMBs, not arbitrary compressed archives.
"""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct


def span(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(f"out-of-bounds range {offset:#x}+{size:#x}")
    return data[offset:offset + size]


def u32(data, offset):
    return struct.unpack("<I", span(data, offset, 4))[0]


def u16(data, offset):
    return struct.unpack("<H", span(data, offset, 2))[0]


def inspect_shbin(data):
    if span(data, 0, 4) != b"DVLB":
        raise ValueError("not DVLB")
    count = u32(data, 4)
    span(data, 8, count * 4)
    p = 8 + count * 4
    if span(data, p, 4) != b"DVLP":
        raise ValueError("missing DVLP")
    words, swizzles = u32(data, p + 12), u32(data, p + 20)
    span(data, p + u32(data, p + 8), words * 4)
    span(data, p + u32(data, p + 16), swizzles * 8)
    programs = []
    for i in range(count):
        d = u32(data, 8 + i * 4)
        if span(data, d, 4) != b"DVLE":
            raise ValueError("missing DVLE")
        table = d + u32(data, d + 0x30)
        n = u32(data, d + 0x34)
        symbols = span(data, d + u32(data, d + 0x38), u32(data, d + 0x3C))
        span(data, table, n * 8)
        uniforms = []
        for j in range(n):
            entry = table + j * 8
            start = u32(data, entry)
            end = symbols.find(b"\0", start)
            if end < 0:
                raise ValueError("unterminated uniform name")
            uniforms.append({"name": symbols[start:end].decode("ascii"),
                             "register_start": u16(data, entry + 4),
                             "register_end": u16(data, entry + 6)})
        programs.append({"stage": span(data, d + 6, 1)[0],
                         "entry_word": u32(data, d + 8),
                         "end_main_word": u32(data, d + 12), "uniforms": uniforms})
    return {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
            "instruction_words": words, "swizzle_entries": swizzles,
            "programs": programs}


def inspect_cmb(data):
    if span(data, 0, 4) != b"cmb " or u32(data, 8) != 6:
        raise ValueError("expected OOT3D CMB version 6")
    data = span(data, 0, u32(data, 4))
    mats, luts = u32(data, 0x28), u32(data, 0x34)
    if span(data, mats, 4) != b"mats" or span(data, luts, 4) != b"luts":
        raise ValueError("missing MATS/LUTS")
    # The serialized size is not an inclusive bound for the final TEV record
    # (cube.cmb is four bytes short). Native consumers index up to TEX.
    tex = u32(data, 0x2C)
    if span(data, tex, 4) != b"tex ":
        raise ValueError("missing TEX boundary")
    span(data, mats, u32(data, mats + 4))
    mat_data = span(data, mats, tex - mats)
    lut_data = span(data, luts, u32(data, luts + 4))
    count, lut_count = u32(mat_data, 8), u32(lut_data, 8)
    span(mat_data, 12, count * 0x15C)
    span(lut_data, 16, lut_count * 4)
    keyframes = 0
    for i in range(lut_count):
        curve = u32(lut_data, 16 + i * 4)
        keys = u32(lut_data, curve + 4)
        span(lut_data, curve + 16, keys * 16)
        keyframes += keys
    ops, flags = Counter(), Counter()
    for i in range(count):
        p = 12 + i * 0x15C
        flags[f"vertex={mat_data[p+1]} fragment={mat_data[p]}"] += 1
        stages = u32(mat_data, p + 0x120)
        if stages > 6:
            raise ValueError("more than six TEV stages")
        for j in range(stages):
            index = struct.unpack("<h", span(mat_data, p + 0x124 + j * 2, 2))[0]
            if index < 0:
                raise ValueError("negative active TEV stage index")
            stage = span(mat_data, 12 + count * 0x15C + index * 0x28, 0x28)
            ops[f"{u16(stage, 0):04X}/{u16(stage, 2):04X}"] += 1
    return count, lut_count, keyframes, ops, flags


def census(root):
    result = {"schema": 1, "scope": "uncompressed embedded CMB v6 in .zar/.zsi/.cmb",
              "containers": 0, "cmb_occurrences": 0, "materials": 0,
              "lut_keyframes": 0, "errors": [], "shbin": {}}
    ops, flags, lut_counts = Counter(), Counter(), Counter()
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        suffix = path.suffix.lower()
        if suffix not in (".zar", ".zsi", ".cmb", ".shbin"):
            continue
        data = path.read_bytes()
        name = path.relative_to(root).as_posix()
        if suffix == ".shbin":
            result["shbin"][name] = inspect_shbin(data)
            continue
        result["containers"] += 1
        offset = 0
        while (offset := data.find(b"cmb ", offset)) >= 0:
            candidate = offset
            offset += 4
            if candidate + 0x44 > len(data) or u32(data, candidate + 8) != 6:
                continue
            try:
                model = span(data, candidate, u32(data, candidate + 4))
                count, lut_count, keys, local_ops, local_flags = inspect_cmb(model)
            except ValueError as error:
                result["errors"].append({"container": name, "offset": candidate,
                                         "error": str(error)})
                continue
            result["cmb_occurrences"] += 1
            result["materials"] += count
            result["lut_keyframes"] += keys
            lut_counts[str(lut_count)] += 1
            ops.update(local_ops)
            flags.update(local_flags)
    result.update(tev_stage_occurrences=ops.total(), rgb_alpha_operations=dict(sorted(ops.items())),
                  lighting_flags=dict(sorted(flags.items())), lut_counts=dict(sorted(lut_counts.items())))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--romfs", type=Path, required=True)
    parser.add_argument("--code", type=Path)
    args = parser.parse_args()
    if not args.romfs.is_dir():
        parser.error("ROMFS must be an existing directory")
    result = census(args.romfs)
    if args.code:
        result["code_sha256"] = hashlib.sha256(args.code.read_bytes()).hexdigest()
    print(json.dumps(result, indent=2))
    raise SystemExit(bool(result["errors"]))
