#!/usr/bin/env python3
"""Build the revision-pinned native process-startup closure."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


SOURCE_FILES = (
    "src/startup/process_entry.c",
    "src/startup/static_initializers.c",
    "src/startup/callback_constructors.c",
    "src/startup/runtime_constructors.c",
)
HOST_COMPILE_CONTRACT = (
    "-DOOT3D_HOST_CTR_CONNECT_TO_PORT=1",
    "-DOOT3D_HOST_CTR_RESOURCE_LIMIT_QUERY=1",
    "-DOOT3D_HOST_TARGET_SYSTEM_TICK=1",
    "-DOOT3D_HOST_TARGET_THREAD_POINTER=1",
    "-DOOT3D_HOST_OWNER_ACTOR_GLOBAL_RESOLVER=1",
    "-DOOT3D_HOST_SYSTEM_ARENA_THREAD_RUNTIME=1",
    "-DnninitStartUp=oot3d_boot_contract_nninit_startup_004160ec",
    "-DglDepthFunc=oot3d_guest_glDepthFunc",
    "-DglEnable=oot3d_guest_glEnable",
    "-DglFrontFace=oot3d_guest_glFrontFace",
    "-DglViewport=oot3d_guest_glViewport",
)
DIRECT_OWNER_BINDINGS = {
    "nninitStartUp": "oot3d_boot_contract_nninit_startup_004160ec",
}
REQUIRED_EXPORTS = {
    "oot3d_call_process_initializers",
    "oot3d_process_entry",
    "oot3d_register_startup_target_function_overlay",
    "oot3d_startup_construct_array_address",
}


def run(command: list[str]) -> str:
    result = subprocess.run(
        command, capture_output=True, text=True, errors="replace"
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"{result.stdout[-2000:]}\n{result.stderr[-6000:]}"
        )
    return result.stdout


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_symbols(llvm_nm: Path, path: Path, option: str) -> set[str]:
    symbols: set[str] = set()
    for line in run([str(llvm_nm), option, str(path)]).splitlines():
        match = re.search(r"(?:^|\s)([A-Za-z_][A-Za-z0-9_]*)$", line)
        if match:
            symbols.add(match.group(1))
    return symbols


def validate_import(source_root: Path) -> tuple[dict, str]:
    manifest_path = source_root / "import_manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    digest = hashlib.sha256()
    digest.update(manifest_path.read_bytes())
    for relative, expected in sorted(manifest["files"].items()):
        path = source_root / relative
        actual = sha256_file(path)
        if actual != expected:
            raise SystemExit(
                f"startup import hash mismatch for {relative}: "
                f"expected {expected}, got {actual}"
            )
        digest.update(relative.encode("utf-8"))
        digest.update(actual.encode("ascii"))
    return manifest, digest.hexdigest()


def validate_overlays(overlay_root: Path) -> tuple[dict, str]:
    manifest_path = overlay_root / "overlays.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    digest = hashlib.sha256(manifest_path.read_bytes())
    seen_addresses: set[int] = set()
    seen_symbols: set[str] = set()
    for overlay in manifest["overlays"]:
        address = int(overlay["guest_address"], 0)
        symbol = overlay["symbol"]
        source = overlay_root / overlay["source"]
        if address == 0 or address > 0xFFFFFFFF:
            raise SystemExit(f"invalid boot overlay address: {address:#x}")
        if address in seen_addresses or symbol in seen_symbols:
            raise SystemExit(f"duplicate boot overlay: {address:#x} {symbol}")
        if not source.is_file():
            raise SystemExit(f"boot overlay source is missing: {source}")
        seen_addresses.add(address)
        seen_symbols.add(symbol)
        digest.update(overlay["source"].encode("utf-8"))
        digest.update(sha256_file(source).encode("ascii"))
    return manifest, digest.hexdigest()


def input_fingerprint(
    args: argparse.Namespace, import_digest: str, overlay_digest: str
) -> str:
    digest = hashlib.sha256()
    digest.update(b"oot3d-native-startup-closure-v1")
    digest.update(sha256_file(Path(__file__).resolve()).encode("ascii"))
    digest.update(import_digest.encode("ascii"))
    digest.update(overlay_digest.encode("ascii"))
    digest.update(args.target_triple.encode("ascii"))
    for item in HOST_COMPILE_CONTRACT:
        digest.update(item.encode("ascii"))
    for tool in (args.clang, args.llvm_ar, args.llvm_nm, args.legalizer):
        resolved = tool.resolve()
        stat = resolved.stat()
        digest.update(str(resolved).encode("utf-8"))
        digest.update(f"{stat.st_size}:{stat.st_mtime_ns}".encode("ascii"))
    digest.update(sha256_file(args.base_archive.resolve()).encode("ascii"))
    return digest.hexdigest()


def main() -> int:
    root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", required=True, type=Path)
    parser.add_argument("--llvm-ar", required=True, type=Path)
    parser.add_argument("--llvm-nm", required=True, type=Path)
    parser.add_argument("--legalizer", required=True, type=Path)
    parser.add_argument("--base-archive", required=True, type=Path)
    parser.add_argument(
        "--source-root",
        type=Path,
        default=root / "source_imports" / "startup_8f00bb64",
    )
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument(
        "--target-triple", default="x86_64-w64-windows-gnu",
        choices=("x86_64-w64-windows-gnu",),
    )
    args = parser.parse_args()

    for path in (
        args.clang,
        args.llvm_ar,
        args.llvm_nm,
        args.legalizer,
        args.base_archive,
    ):
        if not path.is_file():
            raise SystemExit(f"required startup-closure input is missing: {path}")

    source_root = args.source_root.resolve()
    overlay_root = root / "source_overlays" / "boot_contracts"
    output = args.output_root.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest, import_digest = validate_import(source_root)
    overlay_manifest, overlay_digest = validate_overlays(overlay_root)
    fingerprint = input_fingerprint(args, import_digest, overlay_digest)
    archive = output / "liboot3d_source_startup_closure.a"
    report_path = output / "startup_closure_build.json"
    if archive.is_file() and report_path.is_file():
        previous = json.loads(report_path.read_text(encoding="utf-8"))
        if previous.get("input_fingerprint") == fingerprint:
            print(json.dumps({**previous, "build_cache_hit": True}, indent=2))
            return 0

    include_root = source_root / "include"
    objects: list[Path] = []
    defined: set[str] = set()
    source_objects: dict[str, Path] = {}
    compile_sources = [
        (source_root / relative, Path(relative).stem)
        for relative in SOURCE_FILES
    ]
    overlay_sources = list(dict.fromkeys(
        overlay["source"] for overlay in overlay_manifest["overlays"]
    ))
    compile_sources.extend(
        (overlay_root / relative, f"boot_overlay_{index:03d}")
        for index, relative in enumerate(overlay_sources)
    )
    for source, stem in compile_sources:
        bitcode = output / f"{stem}.bc"
        legalized = output / f"{stem}.legal.bc"
        object_path = output / f"{stem}.o"
        run([
            str(args.clang), f"--target={args.target_triple}", "-std=gnu11",
            *HOST_COMPILE_CONTRACT, "-O2", "-w", "-I", str(include_root),
            "-emit-llvm", "-c", str(source), "-o", str(bitcode),
        ])
        run([
            str(args.legalizer), str(bitcode), "--strict", "-o",
            str(legalized),
        ])
        run([
            str(args.clang), f"--target={args.target_triple}", "-O2",
            "-mcmodel=large", "-c", str(legalized), "-o",
            str(object_path),
        ])
        objects.append(object_path)
        source_objects[source.name] = object_path
        defined.update(read_symbols(args.llvm_nm, object_path, "--defined-only"))

    process_entry_undefined = read_symbols(
        args.llvm_nm, source_objects["process_entry.c"], "--undefined-only"
    )
    for baseline, replacement in DIRECT_OWNER_BINDINGS.items():
        if replacement not in process_entry_undefined:
            raise SystemExit(
                f"process entry does not bind validated owner {replacement}"
            )
        if baseline in process_entry_undefined:
            raise SystemExit(
                f"process entry still binds stale baseline owner {baseline}"
            )

    callback_text = (
        source_root / "src/startup/callback_constructors.c"
    ).read_text(encoding="utf-8")
    callback_symbols = {
        entry.lower(): f"oot3d_startup_callback_{entry.lower()}"
        for entry in re.findall(
            r"OOT3D_STARTUP_NOOP_CALLBACK\(([0-9A-Fa-f]{8})\)",
            callback_text,
        )
    }
    for entry in re.findall(
        r"void\s+oot3d_startup_callback_([0-9A-Fa-f]{8})\s*\(",
        callback_text,
    ):
        callback_symbols[entry.lower()] = (
            f"oot3d_startup_callback_{entry.lower()}"
        )
    for symbol, entry in re.findall(
        r"^void\s+([A-Za-z_][A-Za-z0-9_]*At([0-9A-Fa-f]{8}))\s*\(",
        callback_text,
        flags=re.MULTILINE,
    ):
        callback_symbols[entry.lower()] = symbol
    callback_entries = sorted(
        callback_symbols.items(), key=lambda item: int(item[0], 16)
    )
    registry_source = output / "startup_target_registry.c"
    registry_lines = [
        "#include <stdint.h>",
        "extern void oot3d_host_register_target_function_overlay(",
        "    uint32_t address, void* function, const char* name);",
    ]
    for _, symbol in callback_entries:
        registry_lines.append(f"extern void {symbol}(void* element);")
    for overlay in overlay_manifest["overlays"]:
        registry_lines.append(f"extern void {overlay['symbol']}(void);")
    registry_lines.extend((
        "void oot3d_register_startup_target_function_overlay(void) {",
    ))
    for entry, name in callback_entries:
        registry_lines.extend((
            "    oot3d_host_register_target_function_overlay(",
            f"        0x{entry}u, (void*)(uintptr_t)&{name}, \"{name}\");",
        ))
    for overlay in overlay_manifest["overlays"]:
        address = int(overlay["guest_address"], 0)
        symbol = overlay["symbol"]
        registry_lines.extend((
            "    oot3d_host_register_target_function_overlay(",
            f"        0x{address:08x}u, (void*)(uintptr_t)&{symbol}, "
            f"\"{symbol}\");",
        ))
    registry_lines.append("}")
    registry_source.write_text(
        "\n".join(registry_lines) + "\n", encoding="utf-8", newline="\n"
    )
    registry_object = output / "startup_target_registry.o"
    run([
        str(args.clang), f"--target={args.target_triple}", "-std=gnu11",
        "-O2", "-w", "-c", str(registry_source), "-o",
        str(registry_object),
    ])
    objects.append(registry_object)
    defined.update(read_symbols(
        args.llvm_nm, registry_object, "--defined-only"
    ))

    required_exports = REQUIRED_EXPORTS | {
        overlay["symbol"] for overlay in overlay_manifest["overlays"]
    }
    missing_exports = required_exports - defined
    if missing_exports:
        raise SystemExit(
            "startup closure is missing required exports: "
            + ", ".join(sorted(missing_exports))
        )

    base_symbols = read_symbols(
        args.llvm_nm, args.base_archive.resolve(), "--defined-only"
    )
    target_data = set()
    for source, _ in compile_sources:
        text = source.read_text(encoding="utf-8")
        target_data.update(re.findall(r"\bDAT_([0-9A-Fa-f]{8})\b", text))
    bindings = [
        (f"DAT_{suffix.upper()}", int(suffix, 16))
        for suffix in target_data
        if f"DAT_{suffix.upper()}" not in base_symbols
    ]
    bindings.sort()
    assembly = output / "startup_absolute_data.s"
    lines = [
        "# Generated absolute guest-data bindings for native startup.",
        ".text",
    ]
    for name, address in bindings:
        lines.extend((f".globl {name}", f".set {name}, 0x{address:08x}"))
    assembly.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    absolute_object = output / "startup_absolute_data.o"
    run([
        str(args.clang), f"--target={args.target_triple}", "-c",
        str(assembly), "-o", str(absolute_object),
    ])
    objects.insert(0, absolute_object)

    if archive.exists():
        archive.unlink()
    run([str(args.llvm_ar), "rcs", str(archive), *map(str, objects)])
    report = {
        "schema_version": 1,
        "import_id": manifest["import_id"],
        "decomp_revision": manifest["decomp_revision"],
        "input_fingerprint": fingerprint,
        "target_triple": args.target_triple,
        "source_count": len(compile_sources),
        "target_callback_count": len(callback_entries),
        "target_owner_overlay_count": len(overlay_manifest["overlays"]),
        "direct_owner_binding_count": len(DIRECT_OWNER_BINDINGS),
        "absolute_binding_count": len(bindings),
        "archive": str(archive),
        "archive_sha256": sha256_file(archive),
        "build_cache_hit": False,
    }
    report_path.write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
