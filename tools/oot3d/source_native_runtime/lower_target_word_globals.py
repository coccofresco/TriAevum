#!/usr/bin/env python3
"""Materialize a source snapshot with 32-bit guest-word globals lowered."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

from audit_pointer_host_width_arithmetic import (
    has_generic_host_pointer_type,
    infer_pointee_types_by_symbol,
    mask_non_code,
    pointer_aliases,
)


LOWERING_CONTRACT = "target-word-lowering-v5-manifested-abi"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def lowering_input_fingerprint(args: argparse.Namespace) -> str:
    digest = hashlib.sha256(LOWERING_CONTRACT.encode("ascii"))
    for path in (
        args.snapshot_manifest.resolve(),
        args.declaration_report.resolve(),
        args.surface_contracts.resolve(),
        args.process_manifest.resolve(),
    ):
        digest.update(str(path).encode("utf-8"))
        digest.update(sha256(path).encode("ascii"))
    snapshot_root = args.snapshot_root.resolve()
    digest.update(str(snapshot_root).encode("utf-8"))
    for path in sorted(
            (item for item in snapshot_root.rglob("*") if item.is_file()),
            key=lambda item: item.relative_to(snapshot_root).as_posix()):
        stat = path.stat()
        digest.update(path.relative_to(snapshot_root).as_posix().encode("utf-8"))
        digest.update(f"{stat.st_size}:{stat.st_mtime_ns}".encode("ascii"))
    return digest.hexdigest()


def mapped_ranges(process_manifest: dict) -> list[tuple[int, int]]:
    process = process_manifest["process"]
    regions = [*process["segments"], *process["system_regions"]]
    for name in ("heap", "linear_heap"):
        region = process.get(name)
        if region:
            regions.append({
                "address": region["base_address"],
                "mapped_size": region["size"],
            })
    return [
        (region["address"], region["address"] + region["mapped_size"])
        for region in regions
    ]


def is_mapped(address: int, ranges: list[tuple[int, int]]) -> bool:
    return any(begin <= address < end for begin, end in ranges)


def rewrite_host_abi_identifiers(content: str) -> tuple[str, int, int]:
    type_replacements = {"uintptr_t": "uint32_t", "intptr_t": "int32_t"}
    symbol_replacements = {"mbstowcs": "oot3d_target_mbstowcs"}
    output = []
    offset = 0
    type_count = 0
    symbol_count = 0
    state = "code"
    while offset < len(content):
        character = content[offset]
        following = content[offset + 1] if offset + 1 < len(content) else ""
        if state == "code":
            if character == "/" and following == "/":
                output.extend((character, following))
                offset += 2
                state = "line_comment"
            elif character == "/" and following == "*":
                output.extend((character, following))
                offset += 2
                state = "block_comment"
            elif character in ('"', "'"):
                output.append(character)
                offset += 1
                state = "string" if character == '"' else "character"
            elif character == "_" or character.isalpha():
                end = offset + 1
                while end < len(content) and (
                    content[end] == "_" or content[end].isalnum()
                ):
                    end += 1
                identifier = content[offset:end]
                replacement = type_replacements.get(
                    identifier, symbol_replacements.get(identifier, identifier))
                output.append(replacement)
                type_count += identifier in type_replacements
                symbol_count += identifier in symbol_replacements
                offset = end
            else:
                output.append(character)
                offset += 1
        elif state == "line_comment":
            output.append(character)
            offset += 1
            if character == "\n":
                state = "code"
        elif state == "block_comment":
            output.append(character)
            offset += 1
            if character == "*" and following == "/":
                output.append(following)
                offset += 1
                state = "code"
        else:
            output.append(character)
            offset += 1
            if character == "\\" and offset < len(content):
                output.append(content[offset])
                offset += 1
            elif (state == "string" and character == '"') or (
                state == "character" and character == "'"
            ):
                state = "code"
    if state in ("block_comment", "string", "character"):
        raise ValueError(f"unterminated C lexical state: {state}")
    return "".join(output), type_count, symbol_count


def precise_pointer_replacements(
    snapshot_root: Path, report: dict
) -> dict[str, list[tuple[int, int, str, str]]]:
    """Return declarations whose local aliases prove an exact pointer type."""

    by_source: dict[str, dict[str, dict]] = {}
    for symbol in report.get("symbols", []):
        if symbol.get("disposition") != "pointer_host_width":
            continue
        if not has_generic_host_pointer_type(symbol):
            continue
        for declaration in symbol.get("declarations", []):
            by_source.setdefault(declaration["source"], {})[
                symbol["name"]
            ] = symbol

    replacements: dict[str, list[tuple[int, int, str, str]]] = {}
    unresolved_types = {"uintptr_t *", "intptr_t *", "void *"}
    for relative, symbols in sorted(by_source.items()):
        content = (snapshot_root / relative).read_text(encoding="utf-8")
        code = mask_non_code(content)
        inferred = infer_pointee_types_by_symbol(
            code, set(symbols), pointer_aliases(code)
        )
        for name, symbol in symbols.items():
            pointer_types = inferred.get(name, [])
            if len(pointer_types) != 1 or pointer_types[0] in unresolved_types:
                continue
            pointer_type = pointer_types[0]
            for declaration in symbol.get("declarations", []):
                if declaration["source"] != relative:
                    continue
                if declaration["storage_class"] != "extern":
                    raise SystemExit(
                        f"non-extern target pointer declaration: {name}"
                    )
                begin = declaration["declaration_offset"]
                end = declaration["name_offset"]
                if begin is None or end is None:
                    raise SystemExit(f"missing AST offset: {name}")
                replacements.setdefault(relative, []).append(
                    (begin, end, pointer_type, name)
                )
    return replacements


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", required=True, type=Path)
    parser.add_argument("--snapshot-manifest", required=True, type=Path)
    parser.add_argument("--declaration-report", required=True, type=Path)
    parser.add_argument("--surface-contracts", required=True, type=Path)
    parser.add_argument("--process-manifest", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--replace", action="store_true")
    args = parser.parse_args()

    output = args.output_root.resolve()
    snapshot_root = args.snapshot_root.resolve()
    if (output == snapshot_root or snapshot_root in output.parents
            or output in snapshot_root.parents):
        raise SystemExit("output must not replace or be nested in the input snapshot")
    input_fingerprint = lowering_input_fingerprint(args)
    existing_manifest_path = output / "lowered_snapshot_manifest.json"
    if output.is_dir() and existing_manifest_path.is_file():
        try:
            existing_manifest = json.loads(
                existing_manifest_path.read_text(encoding="utf-8")
            )
        except (OSError, json.JSONDecodeError):
            existing_manifest = {}
        if existing_manifest.get("lowering_input_fingerprint") == input_fingerprint:
            fast_report = {
                key: value for key, value in existing_manifest.items()
                if key not in ("touched_sources", "guest_abi_touched_files")
            }
            fast_report["pipeline_fast_path"] = True
            fast_report["output_root"] = str(output)
            print(json.dumps(fast_report, indent=2))
            return 0

    snapshot = json.loads(args.snapshot_manifest.read_text(encoding="utf-8"))
    report = json.loads(args.declaration_report.read_text(encoding="utf-8"))
    surface = json.loads(args.surface_contracts.read_text(encoding="utf-8"))
    process = json.loads(args.process_manifest.read_text(encoding="utf-8"))
    if snapshot["decomp_revision"] != report["decomp_revision"]:
        raise SystemExit("snapshot and declaration report revisions differ")

    external_data = {
        record["name"]
        for record in surface["external_symbols"]
        if record["category"] == "target_data_address"
    }
    ranges = mapped_ranges(process)
    symbols = {
        symbol["name"]: symbol
        for symbol in report["symbols"]
        if symbol["name"] in external_data and is_mapped(symbol["address"], ranges)
    }

    replacements: dict[str, list[tuple[int, int, str, str]]] = {}
    lowered_symbols = []
    absolute_symbols = []
    for symbol in symbols.values():
        if symbol["disposition"] == "absolute_symbol":
            absolute_symbols.append((symbol["name"], symbol["address"]))
        elif symbol["disposition"] == "target_word_host_width":
            lowered_symbols.append((symbol["name"], symbol["address"]))
            for declaration in symbol["declarations"]:
                if declaration["storage_class"] != "extern":
                    raise SystemExit(
                        f"non-extern target word declaration: {symbol['name']}"
                    )
                begin = declaration["declaration_offset"]
                name_offset = declaration["name_offset"]
                if begin is None or name_offset is None:
                    raise SystemExit(f"missing AST offset: {symbol['name']}")
                replacements.setdefault(declaration["source"], []).append(
                    (begin, name_offset, "uint32_t", symbol["name"])
                )

    pointer_replacements = precise_pointer_replacements(snapshot_root, report)
    for relative, operations in pointer_replacements.items():
        replacements.setdefault(relative, []).extend(operations)

    if output.exists():
        if not args.replace:
            raise SystemExit(f"output exists: {output}")
        shutil.rmtree(output)
    shutil.copytree(snapshot_root, output)

    replacement_count = 0
    pointer_replacement_count = 0
    normalized_pointer_symbols: set[str] = set()
    touched_sources = []
    for relative, operations in sorted(replacements.items()):
        path = output / relative
        content = path.read_text(encoding="utf-8")
        previous_begin = len(content) + 1
        for begin, end, replacement_type, symbol_name in sorted(
            set(operations), reverse=True
        ):
            if end > previous_begin:
                raise SystemExit(f"overlapping declarations in {relative}")
            prefix = content[begin:end]
            if replacement_type == "uint32_t":
                type_pattern = re.compile(r"\b(?:u?intptr_t)\b")
            else:
                type_pattern = re.compile(r"\buintptr_t\s*\*")
            replaced_prefix, count = type_pattern.subn(
                replacement_type, prefix, count=1
            )
            if count != 1:
                raise SystemExit(
                    f"target type absent in {relative}:{begin}-{end} "
                    f"for {symbol_name}"
                )
            content = content[:begin] + replaced_prefix + content[end:]
            previous_begin = begin
            if replacement_type == "uint32_t":
                replacement_count += 1
            else:
                pointer_replacement_count += 1
                normalized_pointer_symbols.add(symbol_name)
        path.write_text(content, encoding="utf-8", newline="\n")
        touched_sources.append(relative)

    abi_replacement_count = 0
    host_symbol_rename_count = 0
    host_symbol_declaration_injection_count = 0
    abi_touched_files = []
    for path in sorted(output.rglob("*")):
        if path.suffix not in (".c", ".h"):
            continue
        content = path.read_text(encoding="utf-8")
        renames_target_mbstowcs = re.search(r"\bmbstowcs\b", content) is not None
        lowered, type_count, symbol_count = rewrite_host_abi_identifiers(content)
        has_target_declaration = re.search(
            r"\b(?:extern\s+)?(?:unsigned\s+int|int|u32)\s+"
            r"oot3d_target_mbstowcs\s*\(", lowered)
        if (renames_target_mbstowcs and path.suffix == ".c"
                and not has_target_declaration):
            lowered = (
                "extern unsigned int oot3d_target_mbstowcs();\n" + lowered
            )
            host_symbol_declaration_injection_count += 1
        if type_count or symbol_count:
            path.write_text(lowered, encoding="utf-8", newline="\n")
            abi_replacement_count += type_count
            host_symbol_rename_count += symbol_count
            abi_touched_files.append(path.relative_to(output).as_posix())

    assembly = [
        "# Generated from mapped external target data; do not edit.",
        ".text",
    ]
    for name, address in sorted([*absolute_symbols, *lowered_symbols]):
        assembly.extend([f".globl {name}", f".set {name}, 0x{address:08x}"])
    assembly_path = output / "lowered_absolute_target_data_symbols.s"
    assembly_path.write_text("\n".join(assembly) + "\n", encoding="utf-8", newline="\n")

    cmake = ["set(OOT3D_DECOMP_LOWERED_SOURCES"]
    for source in snapshot["canonical_sources"]:
        cmake.append(f'    "${{OOT3D_SOURCE_LOWERED_ROOT}}/{source["path"]}"')
    cmake.extend([
        '    "${OOT3D_SOURCE_LOWERED_ROOT}/lowered_absolute_target_data_symbols.s"',
        ")",
        "",
    ])
    (output / "lowered_sources.cmake").write_text(
        "\n".join(cmake), encoding="utf-8", newline="\n"
    )

    lowered_manifest = {
        "schema_version": 1,
        "lowering_contract": LOWERING_CONTRACT,
        "decomp_revision": snapshot["decomp_revision"],
        "lowering_input_fingerprint": input_fingerprint,
        "source_count": len(snapshot["canonical_sources"]),
        "touched_source_count": len(touched_sources),
        "lowered_symbol_count": len(lowered_symbols),
        "absolute_symbol_count": len(absolute_symbols),
        "replacement_count": replacement_count,
        "precise_pointer_symbol_count": len(normalized_pointer_symbols),
        "precise_pointer_declaration_replacement_count":
            pointer_replacement_count,
        "guest_abi_replacement_count": abi_replacement_count,
        "host_symbol_rename_count": host_symbol_rename_count,
        "host_symbol_declaration_injection_count":
            host_symbol_declaration_injection_count,
        "guest_abi_touched_file_count": len(abi_touched_files),
        "assembly_sha256": sha256(assembly_path),
        "touched_sources": touched_sources,
        "guest_abi_touched_files": abi_touched_files,
    }
    (output / "lowered_snapshot_manifest.json").write_text(
        json.dumps(lowered_manifest, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(json.dumps({
        key: value
        for key, value in lowered_manifest.items()
        if key not in ("touched_sources", "guest_abi_touched_files")
    } | {"output_root": str(output)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
