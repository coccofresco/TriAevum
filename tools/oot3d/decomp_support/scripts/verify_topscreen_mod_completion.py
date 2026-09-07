#!/usr/bin/env python3
"""Validate the complete source-port contract for the supplied TopScreen mod."""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as source:
        value = json.load(source)
    if not isinstance(value, dict):
        raise ValueError(f"{path} does not contain a JSON object")
    return value


def resolve_value(root: Any, path: str) -> Any:
    current = root
    for component in path.split("."):
        if isinstance(current, list):
            current = current[int(component)]
        elif isinstance(current, dict) and component in current:
            current = current[component]
        else:
            raise KeyError(f"missing JSON path {path} at {component}")
    return current


def assert_value(actual: Any, assertion: dict[str, Any]) -> None:
    operation = assertion["op"]
    expected = assertion.get("value")
    if operation == "eq":
        accepted = actual == expected
    elif operation == "gt":
        accepted = actual > expected
    elif operation == "ge":
        accepted = actual >= expected
    elif operation == "empty":
        accepted = len(actual) == 0
    elif operation == "not_empty":
        accepted = len(actual) > 0
    elif operation == "true":
        accepted = actual is True
    elif operation == "false":
        accepted = actual is False
    else:
        raise ValueError(f"unsupported assertion operation {operation}")
    if not accepted:
        raise ValueError(
            f"{assertion['path']} {operation} failed: "
            f"actual={actual!r}, expected={expected!r}"
        )


def verify_json_evidence(
    evidence: dict[str, Any], base: Path
) -> dict[str, Any]:
    path = base / evidence["path"]
    if not path.is_file():
        raise FileNotFoundError(f"missing evidence {path}")
    document = load_json(path)
    assertions = evidence.get("assertions", [])
    for assertion in assertions:
        assert_value(resolve_value(document, assertion["path"]), assertion)
    return {
        "id": evidence["id"],
        "path": str(path),
        "assertions": len(assertions),
        "status": "passed",
    }


def verify_file_evidence(
    evidence: dict[str, Any], base: Path
) -> dict[str, Any]:
    path = base / evidence["path"]
    if not path.is_file():
        raise FileNotFoundError(f"missing evidence {path}")
    minimum_size = int(evidence.get("minimum_size", 1))
    size = path.stat().st_size
    if size < minimum_size:
        raise ValueError(
            f"evidence {path} has {size} bytes, expected at least {minimum_size}"
        )
    result = {
        "id": evidence["id"],
        "path": str(path),
        "size": size,
        "status": "passed",
    }
    minimum_non_black_pixels = evidence.get("minimum_non_black_pixels")
    if minimum_non_black_pixels is not None:
        payload = path.read_bytes()
        if len(payload) < 54 or payload[:2] != b"BM":
            raise ValueError(f"pixel-content evidence {path} is not a BMP")
        pixel_offset = struct.unpack_from("<I", payload, 10)[0]
        width = struct.unpack_from("<i", payload, 18)[0]
        height = abs(struct.unpack_from("<i", payload, 22)[0])
        planes, bits_per_pixel = struct.unpack_from("<HH", payload, 26)
        compression = struct.unpack_from("<I", payload, 30)[0]
        if (
            width <= 0
            or height <= 0
            or planes != 1
            or bits_per_pixel not in (24, 32)
            or compression != 0
        ):
            raise ValueError(
                f"pixel-content evidence {path} has an unsupported BMP layout"
            )
        bytes_per_pixel = bits_per_pixel // 8
        row_stride = ((width * bits_per_pixel + 31) // 32) * 4
        required_size = pixel_offset + row_stride * height
        if required_size > len(payload):
            raise ValueError(f"pixel-content evidence {path} is truncated")
        non_black_pixels = 0
        for row_index in range(height):
            row_start = pixel_offset + row_index * row_stride
            row = payload[row_start : row_start + width * bytes_per_pixel]
            non_black_pixels += sum(
                row[index] != 0
                or row[index + 1] != 0
                or row[index + 2] != 0
                for index in range(0, len(row), bytes_per_pixel)
            )
        if non_black_pixels < int(minimum_non_black_pixels):
            raise ValueError(
                f"evidence {path} has {non_black_pixels} non-black pixels, "
                f"expected at least {minimum_non_black_pixels}"
            )
        result["non_black_pixels"] = non_black_pixels
    return result


def run_test(executable: str, test_bin_dir: Path) -> dict[str, Any]:
    path = test_bin_dir / executable
    if not path.is_file():
        raise FileNotFoundError(f"missing TopScreen test executable {path}")
    completed = subprocess.run(
        [str(path)],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"{path} failed with exit code {completed.returncode}\n"
            f"{completed.stdout}{completed.stderr}"
        )
    return {
        "executable": str(path),
        "status": "passed",
        "output": completed.stdout.strip(),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", required=True, type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--artifact-root", required=True, type=Path)
    parser.add_argument("--test-bin-dir", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()

    contract = load_json(arguments.contract)
    if contract.get("format") != "oot3d_topscreen_completion_contract_v1":
        raise ValueError("unsupported TopScreen completion contract")

    static_results = [
        verify_json_evidence(evidence, arguments.repo_root)
        for evidence in contract.get("static_evidence", [])
    ]
    runtime_results = [
        verify_json_evidence(evidence, arguments.artifact_root)
        for evidence in contract.get("runtime_evidence", [])
    ]
    file_results = [
        verify_file_evidence(evidence, arguments.artifact_root)
        for evidence in contract.get("file_evidence", [])
    ]
    test_results = [
        run_test(executable, arguments.test_bin_dir)
        for executable in contract.get("test_executables", [])
    ]

    manual = contract.get("manual_evidence", [])
    incomplete_manual = [
        evidence.get("id", "<unknown>")
        for evidence in manual
        if evidence.get("status") != "passed"
    ]
    if incomplete_manual:
        raise ValueError(f"incomplete manual evidence: {incomplete_manual}")

    output = {
        "format": "oot3d_topscreen_completion_result_v1",
        "contract": str(arguments.contract),
        "summary": {
            "static_evidence": len(static_results),
            "runtime_evidence": len(runtime_results),
            "file_evidence": len(file_results),
            "tests": len(test_results),
            "manual_evidence": len(manual),
            "status": "passed",
        },
        "static_evidence": static_results,
        "runtime_evidence": runtime_results,
        "file_evidence": file_results,
        "tests": test_results,
        "manual_evidence": manual,
    }
    rendered = json.dumps(output, indent=2) + "\n"
    if arguments.output is not None:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
