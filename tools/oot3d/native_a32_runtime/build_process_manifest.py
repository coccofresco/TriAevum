#!/usr/bin/env python3
"""Build a native OOT3D process layout from ExHeader and decompressed .code."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


PAGE_SIZE = 0x1000
REPO_ROOT = Path(__file__).resolve().parents[3]
HEAP_VADDR_END = 0x10000000
TLS_AREA_VADDR = 0x1FF82000
TLS_PAGE_SIZE = 0x1000
CONFIG_MEMORY_VADDR = 0x1FF80000
SHARED_PAGE_VADDR = 0x1FF81000
DSP_RAM_VADDR = 0x1FF00000
DSP_RAM_SIZE = 0x00080000
LEGACY_LINEAR_HEAP_VADDR = 0x14000000
HEAP_VADDR = 0x08000000
HEAP_SIZE = 0x08000000
USER32_CPSR = 0x10
CTR_MAIN_FPSCR = 0x03C00010
NCCH_ROMFS_SUPERBLOCK_SIZE = 0x1000

# APPLICATION, SYSTEM and BASE region sizes indexed by the ExHeader memory
# mode. This is the CTR kernel layout also used by Azahar's KernelSystem.
CTR_MEMORY_REGION_SIZES = (
    (0x04000000, 0x02C00000, 0x01400000),
    (0, 0, 0),
    (0x06000000, 0x00C00000, 0x01400000),
    (0x05000000, 0x01C00000, 0x01400000),
    (0x04800000, 0x02400000, 0x01400000),
    (0x02000000, 0x04C00000, 0x01400000),
    (0x07C00000, 0x06400000, 0x02000000),
    (0x0B200000, 0x02E00000, 0x02000000),
)


def operational_path(entry_id: str) -> Path:
    document = json.loads(
        (REPO_ROOT / "tools/oot3d/operational_inputs.json").read_text(
            encoding="utf-8"
        )
    )
    variables = {
        str(key): str(value)
        for key, value in document.get("variables", {}).items()
    }
    variables["repoRoot"] = str(REPO_ROOT)
    row = next(
        entry
        for entry in document.get("entries", [])
        if entry.get("id") == entry_id
    )
    expanded = str(row["path"])
    for _ in range(len(variables) + 1):
        previous = expanded
        for key, value in variables.items():
            expanded = expanded.replace("${" + key + "}", value)
        if expanded == previous:
            return Path(expanded)
    raise ValueError(f"operational input variables contain a cycle: {entry_id}")


def u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"u32 at {offset:#x} is outside ExHeader")
    return struct.unpack_from("<I", data, offset)[0]


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def romfs_service_window(path: Path, file_size: int) -> tuple[int, int]:
    with path.open("rb") as stream:
        prefix = stream.read(4)
        if prefix == b"IVFC":
            if file_size <= NCCH_ROMFS_SUPERBLOCK_SIZE:
                raise ValueError("raw NCCH RomFS is smaller than its IVFC superblock")
            stream.seek(NCCH_ROMFS_SUPERBLOCK_SIZE)
            level3_header = stream.read(4)
            if level3_header != struct.pack("<I", 0x28):
                raise ValueError(
                    "raw NCCH RomFS has no Level-3 header after its IVFC superblock"
                )
            return NCCH_ROMFS_SUPERBLOCK_SIZE, file_size - NCCH_ROMFS_SUPERBLOCK_SIZE
        if prefix == struct.pack("<I", 0x28):
            return 0, file_size
    raise ValueError("RomFS image is neither raw NCCH IVFC nor a Level-3 service view")


def segment(exheader: bytes, offset: int) -> dict[str, int]:
    return {
        "address": u32(exheader, offset),
        "physical_pages": u32(exheader, offset + 4),
        "code_size": u32(exheader, offset + 8),
    }


def validate_segment(name: str, item: dict[str, int]) -> None:
    if item["address"] % PAGE_SIZE != 0:
        raise ValueError(f"{name} address is not page aligned")
    if item["physical_pages"] == 0:
        raise ValueError(f"{name} has no physical pages")
    mapped_size = item["physical_pages"] * PAGE_SIZE
    if item["code_size"] > mapped_size:
        raise ValueError(f"{name} code size exceeds mapped pages")


def initial_value(offset: int, size: int, value: int) -> dict[str, int]:
    return {"offset": offset, "size": size, "value": value}


def ctr_memory_layout(exheader: bytes) -> tuple[int, int, int, int]:
    memory_mode = exheader[0x20D] & 0x0F
    if memory_mode >= len(CTR_MEMORY_REGION_SIZES):
        raise ValueError(f"unsupported CTR memory mode {memory_mode}")
    app_memory, system_memory, base_memory = CTR_MEMORY_REGION_SIZES[memory_mode]
    if app_memory == 0:
        raise ValueError(f"unused CTR memory mode {memory_mode}")
    return memory_mode, app_memory, system_memory, base_memory


def ctr_system_regions(exheader: bytes) -> list[dict[str, object]]:
    memory_mode, app_memory, system_memory, base_memory = ctr_memory_layout(
        exheader
    )

    config_values = [
        initial_value(0x01, 1, 0x3A),
        initial_value(0x03, 1, 0x02),
        initial_value(0x08, 8, 0x0004013000008002),
        initial_value(0x10, 4, 0x02),
        initial_value(0x14, 1, 0x01),
        initial_value(0x16, 1, 0x01),
        initial_value(0x18, 4, 0x0000F450),
        initial_value(0x30, 4, memory_mode),
        initial_value(0x40, 4, app_memory),
        initial_value(0x44, 4, system_memory),
        initial_value(0x48, 4, base_memory),
        initial_value(0x62, 1, 0x3A),
        initial_value(0x63, 1, 0x02),
        initial_value(0x64, 4, 0x02),
        initial_value(0x68, 4, 0x0000F450),
    ]
    shared_values = [
        initial_value(0x04, 1, 0x01),
        initial_value(0x66, 1, 0x03),
        initial_value(0x67, 1, 0x02),
        initial_value(0x85, 1, 0x17),
        initial_value(0x86, 1, 0x01),
    ]
    return [
        {
            "name": "ctr_vram",
            "address": 0x1F000000,
            "mapped_size": 0x00600000,
            "writable": True,
            "executable": False,
            "initial_values": [],
        },
        {
            "name": "ctr_dsp_ram",
            "address": DSP_RAM_VADDR,
            "mapped_size": DSP_RAM_SIZE,
            "writable": True,
            "executable": False,
            "initial_values": [],
        },
        {
            "name": "ctr_config_memory",
            "address": CONFIG_MEMORY_VADDR,
            "mapped_size": PAGE_SIZE,
            "writable": False,
            "executable": False,
            "initial_values": config_values,
        },
        {
            "name": "ctr_shared_page",
            "address": SHARED_PAGE_VADDR,
            "mapped_size": PAGE_SIZE,
            "writable": False,
            "executable": False,
            "initial_values": shared_values,
        },
    ]


def build_manifest(
    exheader_path: Path, code_path: Path, romfs_image_path: Path
) -> dict[str, object]:
    exheader = exheader_path.read_bytes()
    code = code_path.read_bytes()
    if len(exheader) < 0x400:
        raise ValueError("ExHeader is truncated")
    if not romfs_image_path.is_file():
        raise ValueError(f"RomFS image does not exist: {romfs_image_path}")
    romfs_image_size = romfs_image_path.stat().st_size
    if romfs_image_size == 0:
        raise ValueError("RomFS image is empty")
    romfs_service_offset, romfs_service_size = romfs_service_window(
        romfs_image_path, romfs_image_size
    )

    text = segment(exheader, 0x10)
    ro = segment(exheader, 0x20)
    data = segment(exheader, 0x30)
    for name, item in (("text", text), ("ro", ro), ("data", data)):
        validate_segment(name, item)

    text_pages = text["physical_pages"] * PAGE_SIZE
    ro_pages = ro["physical_pages"] * PAGE_SIZE
    data_pages = data["physical_pages"] * PAGE_SIZE
    initialized_size = text_pages + ro_pages + data_pages
    if len(code) != initialized_size:
        raise ValueError(
            f"decompressed code size {len(code):#x} does not match ExHeader "
            f"page total {initialized_size:#x}"
        )

    if text["address"] + text_pages > ro["address"]:
        raise ValueError("text and ro virtual ranges overlap")
    if ro["address"] + ro_pages > data["address"]:
        raise ValueError("ro and data virtual ranges overlap")

    stack_size = u32(exheader, 0x1C)
    bss_size = u32(exheader, 0x3C)
    if stack_size == 0 or stack_size % PAGE_SIZE != 0:
        raise ValueError("main stack size is zero or not page aligned")
    bss_pages = align_up(bss_size, PAGE_SIZE)
    data_mapped_size = data_pages + bss_pages
    if data["address"] + data_mapped_size >= HEAP_VADDR_END - stack_size:
        raise ValueError("process image collides with the main stack")

    process_name = exheader[0:8].split(b"\0", 1)[0].decode("ascii", "strict")
    segments = [
        {
            "name": "text",
            "address": text["address"],
            "mapped_size": text_pages,
            "declared_code_size": text["code_size"],
            "file_offset": 0,
            "file_size": text_pages,
            "writable": False,
            "executable": True,
        },
        {
            "name": "rodata",
            "address": ro["address"],
            "mapped_size": ro_pages,
            "declared_code_size": ro["code_size"],
            "file_offset": text_pages,
            "file_size": ro_pages,
            "writable": False,
            "executable": False,
        },
        {
            "name": "data_bss",
            "address": data["address"],
            "mapped_size": data_mapped_size,
            "declared_code_size": data["code_size"],
            "file_offset": text_pages + ro_pages,
            "file_size": data_pages,
            "writable": True,
            "executable": False,
        },
    ]
    _, app_memory, _, _ = ctr_memory_layout(exheader)
    initial_commit = sum(item["mapped_size"] for item in segments) + stack_size
    resource_limit_values = [
        0x18,
        app_memory,
        0x20,
        0x20,
        0x20,
        0x08,
        0x08,
        0x10,
        0x02,
        0,
    ]
    resource_current_values = [0, initial_commit, 1, 0, 0, 0, 0, 0, 0, 0]

    return {
        "format": "oot3d_native_process_manifest_v1",
        "source": {
            "exheader_path": str(exheader_path.resolve()),
            "exheader_sha256": sha256(exheader),
            "code_bin_path": str(code_path.resolve()),
            "code_bin_sha256": sha256(code),
            "code_bin_size": len(code),
            "romfs_image_path": str(romfs_image_path.resolve()),
            "romfs_image_file_size": romfs_image_size,
            "romfs_service_offset": romfs_service_offset,
            "romfs_service_size": romfs_service_size,
            "layout_authority": "CTR ExHeader; page/BSS/stack/TLS semantics verified against Azahar NCCH loader",
        },
        "process": {
            "name": process_name,
            "entrypoint": text["address"],
            "page_size": PAGE_SIZE,
            "bss_declared_size": bss_size,
            "bss_mapped_size": bss_pages,
            "segments": segments,
            "system_regions": ctr_system_regions(exheader),
            "resource_limit": {
                "limit_values": resource_limit_values,
                "initial_values": resource_current_values,
            },
            "linear_heap": {
                "base_address": LEGACY_LINEAR_HEAP_VADDR,
                "size": app_memory,
            },
            "heap": {
                "base_address": HEAP_VADDR,
                "size": HEAP_SIZE,
            },
        },
        "primary_thread": {
            "stack_base_address": HEAP_VADDR_END - stack_size,
            "stack_size": stack_size,
            "tls_base_address": TLS_AREA_VADDR,
            "tls_size": TLS_PAGE_SIZE,
            "thread_pointer": TLS_AREA_VADDR,
            "argument0": 0,
            "initial_cpsr": USER32_CPSR,
            "initial_fpscr": CTR_MAIN_FPSCR,
            "priority": exheader[0x20F],
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--exheader", type=Path, default=operational_path("oot3d_exheader")
    )
    parser.add_argument(
        "--code-bin", type=Path, default=operational_path("oot3d_code_bin")
    )
    parser.add_argument(
        "--romfs-image",
        type=Path,
        default=operational_path("oot3d_romfs_image"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=REPO_ROOT / "build-codex/oot3d_native_process_manifest.json",
    )
    parser.add_argument("--expected-code-sha256")
    args = parser.parse_args()

    manifest = build_manifest(args.exheader, args.code_bin, args.romfs_image)
    actual_hash = manifest["source"]["code_bin_sha256"]
    if args.expected_code_sha256 and actual_hash.lower() != args.expected_code_sha256.lower():
        raise SystemExit(
            f"code.bin SHA-256 mismatch: expected {args.expected_code_sha256}, got {actual_hash}"
        )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(
        f"OOT3D process manifest: entry={manifest['process']['entrypoint']:#010x} "
        f"segments={len(manifest['process']['segments'])} output={args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
