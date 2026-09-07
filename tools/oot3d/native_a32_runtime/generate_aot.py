"""Run the pinned Zelda3drecomp A32 AOT generator from the vendored snapshot."""

from __future__ import annotations

import csv
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
UPSTREAM = ROOT / "upstream"
PROVENANCE = UPSTREAM / "provenance.json"
REPO_ROOT = ROOT.parents[2]
SUPPLEMENTAL_ENTRIES = ROOT / "runtime_discovered_entries.csv"
TRUE_AOT_BLOCKS = ROOT / "true_aot_blocks.json"
TRUE_AOT_GENERATOR = ROOT / "generate_true_aot.py"


def operational_path(entry_id: str) -> Path:
    config = json.loads(
        (REPO_ROOT / "tools/oot3d/operational_inputs.json").read_text(
            encoding="utf-8"
        )
    )
    variables = {
        str(key): str(value)
        for key, value in config.get("variables", {}).items()
    }
    variables["repoRoot"] = str(REPO_ROOT)
    row = next(
        entry for entry in config.get("entries", []) if entry.get("id") == entry_id
    )
    expanded = str(row["path"])
    for _ in range(len(variables) + 1):
        previous = expanded
        for key, value in variables.items():
            expanded = expanded.replace("${" + key + "}", value)
        if expanded == previous:
            return Path(expanded)
    raise ValueError(f"operational input variables contain a cycle: {entry_id}")


def has_option(arguments: list[str], option: str) -> bool:
    return any(value == option or value.startswith(option + "=") for value in arguments)


def option_value(arguments: list[str], option: str) -> str | None:
    for index, value in enumerate(arguments):
        if value == option:
            return arguments[index + 1]
        if value.startswith(option + "="):
            return value.split("=", 1)[1]
    return None


def replace_option(arguments: list[str], option: str, value: Path) -> None:
    for index, current in enumerate(arguments):
        if current == option:
            arguments[index + 1] = str(value)
            return
        if current.startswith(option + "="):
            arguments[index] = f"{option}={value}"
            return
    arguments.extend((option, str(value)))


def add_local_entry_intervals(
    inventory_path: Path,
    supplemental_path: Path,
    output_path: Path,
    entrypoint: int,
) -> Path:
    with inventory_path.open(newline="", encoding="utf-8-sig") as source:
        reader = csv.DictReader(source)
        fieldnames = list(reader.fieldnames or ())
        rows = list(reader)
    if not {"entry", "size", "name"}.issubset(fieldnames):
        raise ValueError("A32 inventory cannot accept a process entry interval")

    def integer(value: str) -> int:
        return int(value, 0)

    for row in rows:
        start = integer(row["entry"])
        if start <= entrypoint < start + integer(row["size"]):
            break
    else:
        next_entry = min(
            (
                integer(row["entry"])
                for row in rows
                if integer(row["entry"]) > entrypoint
            ),
            default=0,
        )
        if next_entry <= entrypoint or (next_entry - entrypoint) % 4 != 0:
            raise ValueError("cannot bound the process entry interval from inventory")

        generated = {field: "" for field in fieldnames}
        generated.update(
            {
                "profile": rows[0].get("profile", "") if rows else "",
                "entry": f"0x{entrypoint:08X}",
                "end": f"0x{next_entry - 1:08X}",
                "size": str(next_entry - entrypoint),
                "name": "oot3d_process_entry",
                "namespace": "Global",
                "memory_block": "oot3d_text",
                "maintained_name": "oot3d_process_entry",
                "maintained_kind": "process_entry",
                "maintained_confidence": "certain",
                "maintained_notes": "Generated from the ExHeader entrypoint to the first reviewed inventory interval.",
                "confidence": "certain",
                "evidence": "exheader_entrypoint",
                "status": "generated_process_entry",
            }
        )
        rows.append(generated)

    with supplemental_path.open(newline="", encoding="utf-8-sig") as source:
        supplemental = list(csv.DictReader(source))
    required = {"entry", "size", "name", "evidence"}
    if supplemental and not required.issubset(supplemental[0]):
        raise ValueError("supplemental A32 entries have an invalid schema")
    known_entries = {integer(row["entry"]): row for row in rows}
    for item in supplemental:
        start = integer(item["entry"])
        size = integer(item["size"])
        if start % 4 != 0 or size <= 0 or size % 4 != 0:
            raise ValueError(
                f"supplemental A32 entry is not word aligned: 0x{start:08X}+{size}"
            )
        if start in known_entries:
            existing = known_entries[start]
            if integer(existing["size"]) < size:
                raise ValueError(
                    f"supplemental A32 entry exceeds inventory interval: 0x{start:08X}"
                )
            continue
        generated = {field: "" for field in fieldnames}
        generated.update(
            {
                "profile": rows[0].get("profile", "") if rows else "",
                "entry": f"0x{start:08X}",
                "end": f"0x{start + size - 1:08X}",
                "size": str(size),
                "name": item["name"],
                "namespace": "Global",
                "memory_block": "oot3d_text",
                "maintained_name": item["name"],
                "maintained_kind": "runtime_discovered_function",
                "maintained_confidence": item.get("confidence", "high"),
                "maintained_notes": item.get("notes", ""),
                "confidence": item.get("confidence", "high"),
                "evidence": item["evidence"],
                "status": "runtime_discovered_entry",
            }
        )
        rows.append(generated)
        known_entries[start] = generated
    rows.sort(key=lambda row: integer(row["entry"]))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(target, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    return output_path


def build_cache_key(arguments: list[str], inputs: list[Path]) -> str:
    digest = hashlib.sha256()
    digest.update(json.dumps(arguments, separators=(",", ":")).encode("utf-8"))
    for path in sorted({item.resolve() for item in inputs}, key=str):
        digest.update(str(path).encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def cache_is_complete(output_path: Path, cache_key: str) -> bool:
    stamp_path = output_path / "wrapper_stamp.json"
    if not stamp_path.is_file():
        return False
    try:
        stamp = json.loads(stamp_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    required = (
        output_path / "oot3d_a32_generated.h",
        output_path / "registry.cpp",
        output_path / "manifest.json",
        output_path / "oot3d_a32_true_aot_generated.h",
        output_path / "oot3d_a32_true_aot_generated.cpp",
        output_path / "true_aot_manifest.json",
        output_path / "inventory_with_process_entry.csv",
    )
    return (
        stamp.get("format") == "oot3d_a32_wrapper_stamp_v1"
        and stamp.get("cache_key") == cache_key
        and all(path.is_file() for path in required)
        and any(output_path.glob("shard_*.cpp"))
    )


def validate_local_extensions(provenance: dict[str, object]) -> None:
    records = {
        str(item.get("path")): item
        for item in provenance.get("files", [])
        if isinstance(item, dict)
    }
    for extension in provenance.get("local_extensions", []):
        if not isinstance(extension, dict):
            raise ValueError("invalid pinned A32 local extension record")
        relative = str(extension.get("path", ""))
        base = records.get(relative)
        if base is None or base.get("sha256") != extension.get("base_sha256"):
            raise ValueError("pinned A32 local extension has no matching base")
        path = UPSTREAM / relative
        if (
            not path.is_file()
            or path.stat().st_size != extension.get("working_size")
            or hashlib.sha256(path.read_bytes()).hexdigest()
            != extension.get("working_sha256")
        ):
            raise ValueError(f"pinned A32 local extension drifted: {relative}")
def main() -> int:
    provenance = json.loads(PROVENANCE.read_text(encoding="utf-8"))
    if provenance.get("format") != "oot3d_pinned_a32_runtime_v1":
        raise ValueError("the pinned A32 runtime provenance is missing or invalid")
    validate_local_extensions(provenance)
    arguments = list(sys.argv[1:])
    defaults = {
        "--code": operational_path("oot3d_code_bin"),
        "--inventory": UPSTREAM / "analysis/codebin_function_inventory.csv",
        "--boundary-audit":
            UPSTREAM / "analysis/codebin_callable_boundary_residue_audit_166.csv",
        "--output": REPO_ROOT / "build-codex/oot3d_a32_generated",
    }
    for option, path in defaults.items():
        if not has_option(arguments, option):
            arguments.extend((option, str(path)))
    if not has_option(arguments, "--shard-size"):
        arguments.extend(("--shard-size", "32768"))
    for option in ("--code", "--inventory", "--boundary-audit"):
        index = arguments.index(option) if option in arguments else -1
        if index >= 0 and not Path(arguments[index + 1]).is_file():
            raise FileNotFoundError(f"{option} input is missing: {arguments[index + 1]}")

    output_path = Path(option_value(arguments, "--output") or defaults["--output"])
    inventory_path = Path(
        option_value(arguments, "--inventory") or defaults["--inventory"]
    )
    exheader_path = operational_path("oot3d_exheader")
    exheader = exheader_path.read_bytes()
    if len(exheader) < 0x18:
        raise ValueError("OOT3D ExHeader is truncated")
    entrypoint = int.from_bytes(exheader[0x10:0x14], "little")
    text_pages = int.from_bytes(exheader[0x14:0x18], "little")
    if text_pages == 0:
        raise ValueError("OOT3D ExHeader has an empty text segment")
    if not has_option(arguments, "--executable-size"):
        arguments.extend(("--executable-size", hex(text_pages * 0x1000)))
    cache_inputs = [
        Path(__file__),
        exheader_path,
        Path(option_value(arguments, "--code") or defaults["--code"]),
        inventory_path,
        SUPPLEMENTAL_ENTRIES,
        TRUE_AOT_BLOCKS,
        TRUE_AOT_GENERATOR,
        Path(
            option_value(arguments, "--boundary-audit")
            or defaults["--boundary-audit"]
        ),
        *sorted((UPSTREAM / "src/oot3d_pack").glob("*.py")),
    ]
    cache_key = build_cache_key(arguments, cache_inputs)
    if cache_is_complete(output_path, cache_key):
        print(f"A32 C++ AOT: cached ({output_path})")
        return 0
    generated_inventory = add_local_entry_intervals(
        inventory_path,
        SUPPLEMENTAL_ENTRIES,
        output_path / "inventory_with_process_entry.csv",
        entrypoint,
    )
    replace_option(arguments, "--inventory", generated_inventory)

    sys.path.insert(0, str(UPSTREAM / "src"))
    from oot3d_pack.a32_cpp_aot import main as generate  # noqa: PLC0415

    result = generate(arguments)
    if result == 0:
        from generate_true_aot import generate_true_aot  # noqa: PLC0415

        generate_true_aot(
            Path(option_value(arguments, "--code") or defaults["--code"]),
            TRUE_AOT_BLOCKS,
            output_path,
            int(option_value(arguments, "--base") or "0x00100000", 0),
        )
        (output_path / "wrapper_stamp.json").write_text(
            json.dumps(
                {
                    "format": "oot3d_a32_wrapper_stamp_v1",
                    "cache_key": cache_key,
                    "entrypoint": entrypoint,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
    return result


if __name__ == "__main__":
    raise SystemExit(main())
