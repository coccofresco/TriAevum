#!/usr/bin/env python3
"""Regression gate for 32-bit guest pointers loaded through host LLVM IR."""

from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_LEGALIZER = ROOT / "build-source-native-llvm-pass" / "Oot3dGuestStorageLegalizer.exe"
DEFAULT_LLVM_DIS = Path(r"I:\oot3dre_tools\llvm-22.1.6\bin\llvm-dis.exe")
DEFAULT_PROBE = (
    Path(__file__).resolve().parent
    / "llvm_pass"
    / "guest_storage_contract_probe.ll"
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--legalizer", type=Path, default=DEFAULT_LEGALIZER)
    parser.add_argument("--llvm-dis", type=Path, default=DEFAULT_LLVM_DIS)
    parser.add_argument("--probe", type=Path, default=DEFAULT_PROBE)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="oot3d-guest-storage-") as temporary:
        root = Path(temporary)
        output = root / "guest_pointer.bc"
        subprocess.run(
            [str(args.legalizer), str(args.probe), "--strict", "-o", str(output)],
            check=True,
        )
        result = subprocess.run(
            [str(args.llvm_dis), str(output), "-o", "-"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout

    forbidden = ("load ptr, ptr %slot", "store ptr %value, ptr %slot")
    remaining = [fragment for fragment in forbidden if fragment in result]
    if remaining:
        raise SystemExit(f"host-width guest storage survived: {remaining}")
    if result.count("load i32, ptr %slot") != 1:
        raise SystemExit("guest pointer load was not lowered to one i32 load")
    if result.count("store i32") != 1:
        raise SystemExit("guest pointer store was not lowered to one i32 store")
    print("guest_storage_legalizer: pointer load/store lowered to i32")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
