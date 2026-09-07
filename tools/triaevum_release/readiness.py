"""Evaluate machine-readable TriAevum public-release readiness gates."""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

try:
    from .common import load_json_object
except ImportError:
    from common import load_json_object


ROOT = Path(__file__).resolve().parent
DEFAULT_READINESS = ROOT / "release_readiness.json"


@dataclass(frozen=True)
class ReadinessResult:
    complete: tuple[str, ...]
    blockers: tuple[str, ...]
    errors: tuple[str, ...]

    @property
    def ready(self) -> bool:
        return not self.blockers and not self.errors


def evaluate_readiness(path: Path = DEFAULT_READINESS) -> ReadinessResult:
    errors: list[str] = []
    complete: list[str] = []
    blockers: list[str] = []
    try:
        payload = load_json_object(path)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        return ReadinessResult((), (), (f"cannot load release readiness: {exc}",))
    if payload.get("format") != "triaevum_release_readiness_v1":
        return ReadinessResult((), (), ("unsupported release readiness format",))
    gates = payload.get("gates")
    if not isinstance(gates, list):
        return ReadinessResult((), (), ("release readiness gates are malformed",))
    seen: set[str] = set()
    for index, gate in enumerate(gates):
        if not isinstance(gate, dict):
            errors.append(f"gates[{index}] is not an object")
            continue
        gate_id = str(gate.get("id", ""))
        if not gate_id or gate_id in seen:
            errors.append(f"invalid or duplicate readiness gate: {gate_id!r}")
            continue
        seen.add(gate_id)
        required = gate.get("required") is True
        status = str(gate.get("status", ""))
        if status == "complete":
            complete.append(gate_id)
        elif required:
            reason = str(gate.get("reason", "unspecified"))
            blockers.append(f"{gate_id}: {reason}")
        elif status not in ("pending", "blocked"):
            errors.append(f"gate {gate_id} has unsupported status {status!r}")
    if not gates:
        errors.append("release readiness contains no gates")
    return ReadinessResult(tuple(complete), tuple(blockers), tuple(errors))


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--readiness", type=Path, default=DEFAULT_READINESS)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    result = evaluate_readiness(args.readiness)
    payload = {
        "ready": result.ready,
        "complete": result.complete,
        "blockers": result.blockers,
        "errors": result.errors,
    }
    if args.json:
        print(json.dumps(payload, indent=2))
    else:
        print(
            f"TriAevum release readiness: {len(result.complete)} complete, "
            f"{len(result.blockers)} blocked, {len(result.errors)} invalid"
        )
        for blocker in result.blockers:
            print(f"  - {blocker}")
        for error in result.errors:
            print(f"  - ERROR: {error}")
    return 0 if result.ready else 1


if __name__ == "__main__":
    raise SystemExit(main())
