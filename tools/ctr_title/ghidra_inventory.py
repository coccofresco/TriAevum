#!/usr/bin/env python3
"""Builds a raw function inventory of a 3DS title's decompressed .code with
headless Ghidra, from the title's process manifest (build_process_manifest.py).

The code-set layout (text, rodata, data, bss) comes from the manifest, so the
same command serves every title. Ghidra and its JDK are machine inputs:

  ghidra_inventory.py --manifest process-manifest.json --code code.bin \
      --prefix mm3d --profile eur_v2 --ghidra-home <ghidra> --java-home <jdk> \
      --project-dir <dir> --output inventory_raw.csv [--keep-project]

The output has the columns the A32 generators read (entry, end, size, name,
...); maintained symbols are a later, title-specific layer.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent / "ghidra_scripts"


def code_set_layout(manifest: dict) -> dict[str, int]:
    segments = {segment["name"]: segment for segment in manifest["process"]["segments"]}
    for name in ("text", "rodata", "data_bss"):
        if name not in segments:
            raise SystemExit(f"process manifest has no {name} segment")
    data = segments["data_bss"]
    bss = data["address"] + data["file_size"]
    return {
        "text": segments["text"]["address"],
        "rodata": segments["rodata"]["address"],
        "data": data["address"],
        "bss": bss,
        "bss_size": data["mapped_size"] - data["file_size"],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--code", type=Path, required=True)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--ghidra-home", type=Path, required=True)
    parser.add_argument("--java-home", type=Path, required=True)
    parser.add_argument("--project-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--keep-project", action="store_true",
                        help="retain the analysed project for later scripts")
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    layout = code_set_layout(manifest)
    headless = args.ghidra_home / "support" / (
        "analyzeHeadless.bat" if os.name == "nt" else "analyzeHeadless")
    project = f"{args.prefix}_code_{args.profile}"
    args.project_dir.mkdir(parents=True, exist_ok=True)
    command = [
        str(headless), str(args.project_dir), project,
        "-import", str(args.code.resolve()),
        "-processor", "ARM:LE:32:v8",
        "-loader", "BinaryLoader",
        "-loader-baseAddr", hex(layout["text"]),
        "-scriptPath", str(SCRIPTS),
        "-preScript", "ImportCtrCodeSet.java", args.prefix, args.profile,
        hex(layout["text"]), hex(layout["rodata"]), hex(layout["data"]),
        hex(layout["bss"]), hex(layout["bss_size"]),
        "-postScript", "ExportFunctionInventory.java", args.profile, str(args.output.resolve()),
    ]
    if not args.keep_project:
        command.append("-deleteProject")
    environment = dict(os.environ)
    environment["JAVA_HOME"] = str(args.java_home)
    environment["PATH"] = str(args.java_home / "bin") + os.pathsep + environment.get("PATH", "")
    print("layout", {key: hex(value) for key, value in layout.items()}, flush=True)
    return subprocess.call(command, env=environment)


if __name__ == "__main__":
    sys.exit(main())
