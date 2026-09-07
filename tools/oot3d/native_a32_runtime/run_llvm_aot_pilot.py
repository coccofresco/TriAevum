#!/usr/bin/env python3
"""Compile one real whole-AOT shard to LLVM IR and quantify guest-state traffic."""

from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
from pathlib import Path


FORMAT = "oot3d_llvm_aot_pilot_v1"


def _command_for_source(database: Path, source: Path) -> dict:
    commands = json.loads(database.read_text(encoding="utf-8"))
    source = source.resolve()
    for command in commands:
        if Path(command["file"]).resolve() == source:
            return command
    raise ValueError(f"source is absent from compile database: {source}")


def _source_for_symbol(database: Path, source_root: Path, symbol: str) -> Path:
    commands = json.loads(database.read_text(encoding="utf-8"))
    root = source_root.resolve()
    declaration = re.compile(
        rf"^Oot3dWholeAotFlow {re.escape(symbol)}\($", re.MULTILINE
    )
    matches: list[Path] = []
    for command in commands:
        source = Path(command["file"])
        try:
            source.resolve().relative_to(root)
        except ValueError:
            continue
        if source.is_file() and declaration.search(source.read_text(encoding="utf-8")):
            # Declarations end in a semicolon after the signature. A
            # definition has an opening brace after the entryPc parameter.
            text = source.read_text(encoding="utf-8")
            start = text.find(f"Oot3dWholeAotFlow {symbol}(")
            while start >= 0:
                close = text.find("uint32_t entryPc)", start)
                if close >= 0 and text[close:close + 40].find("{") >= 0:
                    matches.append(source)
                    break
                start = text.find(f"Oot3dWholeAotFlow {symbol}(", start + 1)
    if len(matches) != 1:
        raise ValueError(
            f"expected one definition of '{symbol}' below {root}, found {len(matches)}"
        )
    return matches[0]


def _extract_function(ir: str, symbol: str) -> list[str]:
    lines = ir.splitlines()
    start = next(
        (index for index, line in enumerate(lines)
         if line.startswith("define ") and symbol in line),
        None,
    )
    if start is None:
        raise ValueError(f"LLVM function containing '{symbol}' was not found")
    for end in range(start + 1, len(lines)):
        if lines[end] == "}":
            return lines[start:end + 1]
    raise ValueError(f"LLVM function containing '{symbol}' is unterminated")


def _state_access_metrics(lines: list[str]) -> dict[str, int]:
    frame_pointer = None
    for line in lines:
        match = re.match(r"\s*(%[-.A-Za-z0-9]+) = load ptr, ptr %frame,", line)
        if match:
            frame_pointer = match.group(1)
            break
    if frame_pointer is None:
        guest_metrics = {
            "function_ir_lines": len(lines),
            "allocas": sum(" = alloca " in line for line in lines),
            "guest_pointer_values": 0,
            "guest_loads": 0,
            "guest_stores": 0,
        }
    else:
        guest_metrics = None

    def pointer_metrics(root: str | None) -> tuple[int, int, int]:
        if root is None:
            return 0, 0, 0
        pointers = {root}
        changed = True
        while changed:
            changed = False
            for line in lines:
                match = re.match(
                    r"\s*(%[-.A-Za-z0-9]+) = (?:getelementptr|bitcast) .*",
                    line,
                )
                if match and any(f"ptr {pointer}" in line for pointer in pointers):
                    if match.group(1) not in pointers:
                        pointers.add(match.group(1))
                        changed = True
        loads = 0
        stores = 0
        for line in lines:
            if not any(f"ptr {pointer}" in line for pointer in pointers):
                continue
            loads += int(re.search(r"\bload\b", line) is not None)
            stores += int(re.search(r"\bstore\b", line) is not None)
        return len(pointers), loads, stores

    guest_pointers, guest_loads, guest_stores = pointer_metrics(frame_pointer)
    promoted_root = "%state" if any("%state" in line for line in lines[:2]) else None
    promoted_pointers, promoted_loads, promoted_stores = pointer_metrics(promoted_root)
    if guest_metrics is not None:
        guest_metrics.update({
            "promoted_pointer_values": promoted_pointers,
            "promoted_loads": promoted_loads,
            "promoted_stores": promoted_stores,
        })
        return guest_metrics
    return {
        "function_ir_lines": len(lines),
        "allocas": sum(" = alloca " in line for line in lines),
        "guest_pointer_values": guest_pointers,
        "guest_loads": guest_loads,
        "guest_stores": guest_stores,
        "promoted_pointer_values": promoted_pointers,
        "promoted_loads": promoted_loads,
        "promoted_stores": promoted_stores,
    }


def run_pilot(
    database: Path,
    source: Path,
    symbol: str,
    output: Path,
    llvm_root: Path,
) -> dict:
    command = _command_for_source(database, source)
    arguments = shlex.split(command["command"], posix=False)
    arguments = [
        argument for argument in arguments
        if not argument.startswith("/Fo")
        and not argument.startswith("/Fd")
        and argument not in {"-Zi", "/Z7", "-c"}
    ]
    arguments[1:1] = [
        "/clang:-S",
        "/clang:-emit-llvm",
        "/clang:-fno-discard-value-names",
    ]

    output.mkdir(parents=True, exist_ok=True)
    emitted = output / f"{source.stem}.ll"
    optimized = output / f"{source.stem}.optimized.ll"
    assembly = output / f"{source.stem}.optimized.s"
    for stale in (emitted, optimized, assembly):
        stale.unlink(missing_ok=True)

    subprocess.run(arguments, cwd=output, check=True)
    if not emitted.is_file():
        raise RuntimeError(f"clang-cl did not emit expected IR: {emitted}")

    opt = llvm_root / "bin" / "opt.exe"
    llc = llvm_root / "bin" / "llc.exe"
    subprocess.run(
        [str(opt), "-passes=default<O2>", "-S", str(emitted), "-o", str(optimized)],
        check=True,
    )
    subprocess.run(
        [str(llc), "--filetype=asm", "--mtriple=x86_64-pc-windows-msvc",
         str(optimized), "-o", str(assembly)],
        check=True,
    )

    emitted_lines = _extract_function(emitted.read_text(encoding="utf-8"), symbol)
    optimized_lines = _extract_function(
        optimized.read_text(encoding="utf-8"), symbol
    )
    return {
        "format": FORMAT,
        "source": str(source.resolve()),
        "symbol_query": symbol,
        "compiler": arguments[0],
        "artifacts": {
            "llvm_ir": str(emitted),
            "optimized_llvm_ir": str(optimized),
            "assembly": str(assembly),
        },
        "bytes": {
            "source": source.stat().st_size,
            "llvm_ir": emitted.stat().st_size,
            "optimized_llvm_ir": optimized.stat().st_size,
            "assembly": assembly.stat().st_size,
        },
        "clang_o2": _state_access_metrics(emitted_lines),
        "opt_o2": _state_access_metrics(optimized_lines),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compile-database", type=Path, required=True)
    source_group = parser.add_mutually_exclusive_group(required=True)
    source_group.add_argument("--source", type=Path)
    source_group.add_argument("--source-root", type=Path)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--llvm-root", type=Path,
        default=Path(r"I:\oot3dre_tools\llvm-22.1.6"),
    )
    args = parser.parse_args()
    source = args.source or _source_for_symbol(
        args.compile_database, args.source_root, args.symbol
    )
    report = run_pilot(
        args.compile_database, source, args.symbol,
        args.output, args.llvm_root,
    )
    report_path = args.output / "pilot_report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
