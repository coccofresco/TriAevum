#!/usr/bin/env python3
"""Validate the original process manifest and emit a source-runtime profile."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def cpp_string(value: str) -> str:
    return json.dumps(value)


def validate_range(address: int, size: int, page_size: int) -> None:
    if (
        address < 0
        or address > 0xFFFFFFFF
        or size <= 0
        or address + size > 0x100000000
        or address % page_size
        or size % page_size
    ):
        raise ValueError("invalid mapped process range")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--consumer-profile", required=True, type=Path)
    parser.add_argument("--code-bin", type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    consumer = json.loads(args.consumer_profile.read_text(encoding="utf-8"))
    if manifest.get("format") != "oot3d_native_process_manifest_v1":
        raise ValueError("unsupported process manifest format")

    source = manifest["source"]
    process = manifest["process"]
    code_bin = (args.code_bin or Path(source["code_bin_path"])).resolve()
    if not code_bin.is_file():
        raise ValueError(f"code.bin is missing: {code_bin}")
    if code_bin.stat().st_size != source["code_bin_size"]:
        raise ValueError("code.bin size does not match the process manifest")
    actual_hash = sha256_file(code_bin)
    if actual_hash != source["code_bin_sha256"] or actual_hash != consumer["code_bin_sha256"]:
        raise ValueError("code.bin hash does not match the pinned target profile")

    page_size = process["page_size"]
    if page_size <= 0 or page_size & (page_size - 1):
        raise ValueError("invalid process page size")

    segments = process["segments"]
    regions = process["system_regions"]
    primary_thread = manifest["primary_thread"]
    zero_regions = [
        {"name": "heap", "address": process["heap"]["base_address"], "mapped_size": process["heap"]["size"]},
        {"name": "linear_heap", "address": process["linear_heap"]["base_address"], "mapped_size": process["linear_heap"]["size"]},
        {"name": "primary_tls", "address": primary_thread["tls_base_address"], "mapped_size": primary_thread["tls_size"]},
    ]
    if not segments:
        raise ValueError("process manifest has no segments")
    names: set[str] = set()
    ranges: list[tuple[int, int]] = []
    for item in [*segments, *regions, *zero_regions]:
        if not item["name"] or item["name"] in names:
            raise ValueError("duplicate or empty process region name")
        names.add(item["name"])
        validate_range(item["address"], item["mapped_size"], page_size)
        current = (item["address"], item["address"] + item["mapped_size"])
        if any(current[0] < end and start < current[1] for start, end in ranges):
            raise ValueError("overlapping process regions")
        ranges.append(current)
    for segment in segments:
        if (
            segment["file_offset"] < 0
            or segment["file_size"] < 0
            or segment["file_size"] > segment["mapped_size"]
            or segment["file_offset"] + segment["file_size"] > source["code_bin_size"]
        ):
            raise ValueError("invalid process segment file range")

    stack_start = primary_thread["stack_base_address"]
    stack_end = stack_start + primary_thread["stack_size"]
    if not any(start <= stack_start and stack_end <= end for start, end in ranges):
        raise ValueError("primary stack is not covered by the process mappings")

    initial_values: list[dict] = []
    region_records: list[tuple[dict, int, int]] = []
    for region in regions:
        start = len(initial_values)
        occupied: set[int] = set()
        for value in region["initial_values"]:
            if value["size"] not in (1, 2, 4, 8):
                raise ValueError("invalid system initial value size")
            if value["offset"] < 0 or value["offset"] + value["size"] > region["mapped_size"]:
                raise ValueError("system initial value exceeds its region")
            byte_range = set(range(value["offset"], value["offset"] + value["size"]))
            if occupied & byte_range:
                raise ValueError("overlapping system initial values")
            occupied |= byte_range
            initial_values.append(value)
        region_records.append((region, start, len(initial_values) - start))

    lines = [
        "#pragma once",
        "",
        '#include "oot3d_source_process_image.h"',
        "",
        "#include <array>",
        "",
        "namespace Oot3dSourceProcessGenerated {",
        "using namespace Oot3dSourceRuntime;",
        "",
        f"inline constexpr std::array<SourceImageSegment, {len(segments)}> kSegments{{{{",
    ]
    for segment in segments:
        lines.append(
            "    {"
            f"{cpp_string(segment['name'])}, {segment['address']}U, "
            f"{segment['mapped_size']}U, {segment['file_offset']}U, "
            f"{segment['file_size']}U, "
            f"{'true' if segment['writable'] else 'false'}, "
            f"{'true' if segment['executable'] else 'false'}"
            "},"
        )
    lines.extend(["}};", "", f"inline constexpr std::array<SourceImageInitialValue, {len(initial_values)}> kInitialValues{{{{"])
    for value in initial_values:
        lines.append(f"    {{{value['offset']}U, {value['size']}U, {value['value']}ULL}},")
    lines.extend(["}};", "", f"inline constexpr std::array<SourceImageSystemRegion, {len(region_records)}> kSystemRegions{{{{"])
    for region, start, count in region_records:
        lines.append(
            "    {"
            f"{cpp_string(region['name'])}, {region['address']}U, "
            f"{region['mapped_size']}U, "
            f"{'true' if region['writable'] else 'false'}, "
            f"{'true' if region['executable'] else 'false'}, {start}U, {count}U"
            "},"
        )
    lines.extend(["}};", "", f"inline constexpr std::array<SourceImageZeroRegion, {len(zero_regions)}> kZeroRegions{{{{"])
    for region in zero_regions:
        lines.append(
            "    {"
            f"{cpp_string(region['name'])}, {region['address']}U, {region['mapped_size']}U, true, false"
            "},"
        )
    lines.extend([
        "}};",
        "",
        "inline constexpr SourceProcessImageDescriptor kDescriptor{",
        f"    {cpp_string(actual_hash)},",
        f"    {source['code_bin_size']}U,",
        f"    {process['entrypoint']}U,",
        f"    {page_size}U,",
        "    kSegments,",
        "    kSystemRegions,",
        "    kZeroRegions,",
        "    kInitialValues,",
        "    {"
        f"{primary_thread['stack_base_address']}U, {primary_thread['stack_size']}U, "
        f"{primary_thread['thread_pointer']}U, {primary_thread['tls_base_address']}U, "
        f"{primary_thread['tls_size']}U, {primary_thread['argument0']}U, "
        f"{primary_thread['initial_cpsr']}U, {primary_thread['initial_fpscr']}U, "
        f"{primary_thread['priority']}U"
        "},",
        "};",
        "",
        "} // namespace Oot3dSourceProcessGenerated",
        "",
    ])

    output_root = args.output_root.resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    output = output_root / "oot3d_source_process_profile.h"
    output.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    metadata = {
        "schema_version": 1,
        "process_manifest": str(args.manifest.resolve()),
        "code_bin": str(code_bin),
        "code_bin_sha256": actual_hash,
        "segments": len(segments),
        "system_regions": len(regions),
        "zero_regions": len(zero_regions),
        "entrypoint": process["entrypoint"],
    }
    (output_root / "source_process_profile.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
