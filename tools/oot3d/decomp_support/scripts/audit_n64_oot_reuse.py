#!/usr/bin/env python3
"""Audit reusable evidence from the N64 OoT decompilation."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
METADATA_ROMFS = ROOT / "metadata" / "romfs_manifest.json"
GHIDRA_DECOMPILED = ROOT / "ghidra_export" / "decompiled"
DEFAULT_N64_ROOT = ROOT.parent / "external" / "oot"
DEFAULT_EXTRACTED_ROMFS = ROOT.parent / "work" / "extract" / "romfs"

DEFINE_MESSAGE_RE = re.compile(r"\bDEFINE_MESSAGE(?:_[A-Z0-9]+)?\s*\(\s*(0x[0-9A-Fa-f]+|\d+)")
CONTROL_RE = re.compile(r"^\s*#define\s+(MESSAGE(?:_WIDE)?_[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|\d+)", re.MULTILINE)
MESSAGE_FUNC_RE = re.compile(r"^\w[\w\s*]*\s+(Message_[A-Za-z0-9_]+)\s*\(", re.MULTILINE)
ROM_MESSAGE_RE = re.compile(r"u_rom__message_[A-Za-z0-9_]+_(?:qm|qbf)_[0-9a-fA-F]{8}")


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        try:
            return str(path.relative_to(ROOT.parent)).replace("\\", "/")
        except ValueError:
            return str(path).replace("\\", "/")


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def audit_n64_text_sources(n64_root: Path) -> dict[str, object]:
    text_files = [
        n64_root / "assets" / "text" / "message_data.h",
        n64_root / "assets" / "text" / "message_data_staff.h",
        n64_root / "assets" / "text" / "nes_message_data_static.c",
        n64_root / "assets" / "text" / "jpn_message_data_static.c",
        n64_root / "assets" / "text" / "ger_message_data_static.c",
        n64_root / "assets" / "text" / "fra_message_data_static.c",
        n64_root / "assets" / "text" / "staff_message_data_static.c",
    ]
    interface_files = [
        n64_root / "include" / "message_data_fmt.h",
        n64_root / "include" / "message_data_static.h",
        n64_root / "include" / "message.h",
        n64_root / "src" / "code" / "z_message.c",
        n64_root / "tools" / "extract_text.py",
        n64_root / "tools" / "msgenc.py",
    ]

    sources: list[dict[str, object]] = []
    all_message_ids: set[str] = set()
    extracted_text_files = sorted((n64_root / "extracted").glob("*/text/*.h"))
    for path in text_files + extracted_text_files:
        if not path.is_file():
            continue
        text = read_text(path)
        ids = sorted({match.group(1).lower() for match in DEFINE_MESSAGE_RE.finditer(text)})
        all_message_ids.update(ids)
        sources.append(
            {
                "path": rel(path),
                "size": path.stat().st_size,
                "define_message_count": len(ids),
                "message_id_sample": ids[:12],
            }
        )

    controls: list[dict[str, str]] = []
    fmt = n64_root / "include" / "message_data_fmt.h"
    if fmt.is_file():
        for match in CONTROL_RE.finditer(read_text(fmt)):
            controls.append({"name": match.group(1), "value": match.group(2).lower()})

    message_functions: list[str] = []
    z_message = n64_root / "src" / "code" / "z_message.c"
    if z_message.is_file():
        message_functions = sorted(set(MESSAGE_FUNC_RE.findall(read_text(z_message))))

    makefile = n64_root / "Makefile"
    extraction_rules: list[str] = []
    if makefile.is_file():
        for line in read_text(makefile).splitlines():
            if "extract_text.py" in line or "$(EXTRACTED_DIR)/text" in line:
                extraction_rules.append(line.strip())

    return {
        "root": rel(n64_root),
        "git_commit": git_commit(n64_root),
        "text_sources": sources,
        "interface_sources": [{"path": rel(path), "size": path.stat().st_size} for path in interface_files if path.is_file()],
        "extracted_text_file_count": len(extracted_text_files),
        "text_extraction_rules": extraction_rules,
        "unique_message_id_count": len(all_message_ids),
        "message_ids": sorted(all_message_ids),
        "message_id_sample": sorted(all_message_ids)[:24],
        "control_code_count": len(controls),
        "control_code_sample": controls[:24],
        "message_function_count": len(message_functions),
        "message_function_sample": message_functions[:32],
    }


def git_commit(repo: Path) -> str | None:
    head = repo / ".git" / "HEAD"
    if not head.is_file():
        return None
    head_text = read_text(head).strip()
    if head_text.startswith("ref: "):
        ref = repo / ".git" / head_text.removeprefix("ref: ")
        if ref.is_file():
            return read_text(ref).strip()[:12]
    return head_text[:12]


def audit_oot3d_messages(extracted_romfs: Path) -> dict[str, object]:
    manifest_paths: list[str] = []
    if METADATA_ROMFS.is_file():
        manifest = json.loads(read_text(METADATA_ROMFS))
        manifest_paths = sorted(
            row["path"].replace("\\", "/")
            for row in manifest
            if row.get("path", "").replace("\\", "/").startswith("message/")
        )

    extracted: list[dict[str, object]] = []
    message_root = extracted_romfs / "message"
    if message_root.is_dir():
        for path in sorted(message_root.rglob("*")):
            if not path.is_file():
                continue
            header = path.read_bytes()[:16].hex()
            extracted.append(
                {
                    "path": rel(path),
                    "size": path.stat().st_size,
                    "sha256": file_sha256(path),
                    "first16_hex": header,
                }
            )

    ghidra_refs: list[dict[str, object]] = []
    if GHIDRA_DECOMPILED.is_dir():
        for path in sorted(GHIDRA_DECOMPILED.glob("*.c")):
            text = read_text(path)
            refs = sorted(set(ROM_MESSAGE_RE.findall(text)))
            if refs:
                ghidra_refs.append({"path": rel(path), "symbols": refs})

    return {
        "manifest_message_paths": manifest_paths,
        "extracted_message_files": extracted,
        "ghidra_message_load_refs": ghidra_refs,
    }


def make_markdown(report: dict[str, object]) -> str:
    n64 = report["n64_oot"]
    oot3d = report["oot3d"]
    lines = [
        "# N64 OoT Reuse Audit",
        "",
        "Generated from `scripts/audit_n64_oot_reuse.py`.",
        "",
        "## Inputs",
        "",
        f"- N64 decomp root: `{n64['root']}`",
        f"- N64 decomp commit: `{n64.get('git_commit') or 'unknown'}`",
        "- N64 source provenance: zeldaret/oot local clone.",
        f"- N64 extracted text files present: {n64['extracted_text_file_count']}",
        f"- OOT3D message manifest entries: {len(oot3d['manifest_message_paths'])}",
        f"- OOT3D extracted message files: {len(oot3d['extracted_message_files'])}",
        "",
        "## What Is Reusable",
        "",
        "- Dialogue/message IDs and control-code concepts are reusable as a semantic map, once OOT3D `.qm` files are decoded.",
        "- The extracted N64 dialogue headers are generated by `tools/extract_text.py` from a local N64 baserom; when present they provide direct message-ID reuse anchors.",
        "- `z_message.c` names and state-machine structure are useful for naming OOT3D message functions, but the compiled code is not directly reusable because this target is ARM/CTR and the N64 source is MIPS/N64.",
        "- Textbox types, positions, color/control constants, and item/icon message hooks are likely good anchors for future symbol promotion.",
        "- Actor, item, scene, and texture names from the N64 project can provide labels and fuzzy-match hints, but asset formats differ on 3DS.",
        "",
        "## N64 Text Sources",
        "",
        f"- Unique message IDs observed in audited text files: {n64['unique_message_id_count']}",
        f"- Message control constants observed: {n64['control_code_count']}",
        f"- Message functions observed in `z_message.c`: {n64['message_function_count']}",
    ]
    if n64["text_extraction_rules"]:
        lines.extend(["", "N64 text extraction rules observed:"])
        lines.extend(f"- `{rule}`" for rule in n64["text_extraction_rules"][:6])
    lines.extend(
        [
            "",
            "| Source | Size | Message defs | Sample IDs |",
            "| --- | ---: | ---: | --- |",
        ]
    )
    for row in n64["text_sources"]:
        sample = ", ".join(row["message_id_sample"]) or "-"
        lines.append(f"| `{row['path']}` | {row['size']} | {row['define_message_count']} | {sample} |")

    lines.extend(
        [
            "",
            "## OOT3D Message Containers",
            "",
            "| File | Size | First 16 bytes | SHA-256 |",
            "| --- | ---: | --- | --- |",
        ]
    )
    for row in oot3d["extracted_message_files"]:
        lines.append(f"| `{row['path']}` | {row['size']} | `{row['first16_hex']}` | `{row['sha256'][:16]}...` |")

    lines.extend(
        [
            "",
            "## Ghidra Message Load Sites",
            "",
            "| Decompiled file | Message symbols |",
            "| --- | --- |",
        ]
    )
    for row in oot3d["ghidra_message_load_refs"]:
        lines.append(f"| `{row['path']}` | {', '.join(f'`{symbol}`' for symbol in row['symbols'])} |")

    lines.extend(
        [
            "",
            "## Next Steps",
            "",
            "1. Derive the `.qm` container layout from the extracted OOT3D files and implement a decoder that emits message IDs, raw payloads, and control-byte streams.",
            "2. Join decoded OOT3D message IDs against the N64 message ID set and report exact ID overlap before comparing localized text.",
            "3. Use N64 `Message_*` function names as naming candidates for OOT3D functions that reference the `.qm` and `.qbf` loaders.",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--n64-root", type=Path, default=DEFAULT_N64_ROOT)
    parser.add_argument("--extracted-romfs", type=Path, default=DEFAULT_EXTRACTED_ROMFS)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "n64_oot_reuse_audit.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "n64_oot_reuse_audit.md")
    args = parser.parse_args()

    if not args.n64_root.is_dir():
        raise SystemExit(f"N64 OoT decomp root not found: {args.n64_root}")

    report = {
        "n64_oot": audit_n64_text_sources(args.n64_root),
        "oot3d": audit_oot3d_messages(args.extracted_romfs),
    }
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(report), encoding="utf-8")
    print(f"wrote {args.out_json}")
    print(f"wrote {args.out_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
