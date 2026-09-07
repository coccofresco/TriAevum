"""Snapshot locally licensed C++/Windows dependencies into a private Forge sysroot.

Developer preparation only; this command does not authorize redistribution.
"""

import argparse
from pathlib import Path

try:
    from .assemble_windows_sysroot import assemble
except ImportError:
    from assemble_windows_sysroot import assemble


def snapshot(msvc: Path, sdk: Path, sdk_version: str, clang: Path, output: Path) -> dict:
    if not sdk_version or any(not part.isdigit() for part in sdk_version.split(".")):
        raise ValueError("Invalid Windows SDK version")
    output = output.resolve()
    if output.exists():
        raise ValueError("Sysroot output must be a new directory")
    trees = [(msvc / "include", "msvc/include"), (msvc / "lib/x64", "msvc/lib/x64"),
             (sdk / "Include" / sdk_version, f"sdk/Include/{sdk_version}"),
             (sdk / "Lib" / sdk_version / "ucrt/x64", f"sdk/Lib/{sdk_version}/ucrt/x64"),
             (sdk / "Lib" / sdk_version / "um/x64", f"sdk/Lib/{sdk_version}/um/x64"),
             (clang / "include", "clang/include")]
    for source, _ in trees:
        if not source.is_dir():
            raise ValueError(f"Missing SDK tree: {source}")
    return assemble(trees, sdk_version=sdk_version, msvc_version=msvc.name,
                    clang_version=clang.name, output=output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("msvc", "sdk", "clang", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--sdk-version", required=True)
    args = parser.parse_args()
    print(snapshot(args.msvc, args.sdk, args.sdk_version, args.clang, args.output))
