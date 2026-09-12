"""Verify a relocated Mac bundle without importing or executing a ROM."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

try:
    from .product_contract import validate_product_info
except ImportError:
    from product_contract import validate_product_info


def run(*args: str, cwd: Path | None = None) -> str:
    try:
        return subprocess.check_output(args, cwd=cwd, text=True, stderr=subprocess.STDOUT,
                                       timeout=60).strip()
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(f"Bundle check failed: {args}:\n{exc.output}") from exc


def verify(app: Path) -> dict:
    app = app.resolve(strict=True)
    metadata = json.loads((app / "Contents/Resources/macos-build.json").read_text())
    run("codesign", "--verify", "--deep", "--strict", str(app))
    # Use a path containing spaces. Dependency validation below rejects paths
    # to the original bundle and other external non-system libraries.
    with tempfile.TemporaryDirectory(prefix="TriAevum relocated ") as directory:
        moved = Path(directory).resolve() / app.name
        shutil.copytree(app, moved, symlinks=True)
        run("codesign", "--verify", "--deep", "--strict", str(moved))
        runtime = moved / "Contents/Resources/runtime"
        binaries = [moved / "Contents/MacOS/TriAevum", runtime / "TriAevum",
                    runtime / "triaevum_title_aot.dylib",
                    runtime / "oot3d_native_pica_aot_compiler",
                    moved / "Contents/Resources/forge/TriAevumForge"]
        binaries.extend(sorted((runtime / "lib").glob("*.dylib")))
        for binary in binaries:
            run("lipo", str(binary), "-verify_arch", "arm64")
        for binary in binaries:
            if not binary.is_relative_to(runtime):
                continue
            for line in run("otool", "-L", str(binary)).splitlines()[1:]:
                dependency = line.strip().split(" (compatibility", 1)[0]
                if dependency.startswith(("/usr/lib/", "/System/Library/")):
                    continue
                if dependency == "@rpath/" + binary.name and binary.suffix == ".dylib":
                    continue  # The dylib's own install name.
                if not dependency.startswith("@loader_path/"):
                    raise ValueError(f"External runtime dependency: {binary.name}: {dependency}")
                target = (binary.parent / dependency.removeprefix("@loader_path/")).resolve(strict=True)
                if not target.is_relative_to(runtime):
                    raise ValueError(f"Dependency escapes runtime bundle: {dependency}")
        icd = json.loads((runtime / "lib/MoltenVK_icd.json").read_text())
        if icd["ICD"]["library_path"] != "./libMoltenVK.dylib":
            raise ValueError("MoltenVK ICD is not bundle-relative")
        pack = runtime / "forge/shader-corpus/portable.o3ps"
        if not pack.is_file() or not pack.stat().st_size:
            raise ValueError("Missing portable shader corpus")
        product = json.loads(run(str(runtime / "TriAevum"), "--verify-title-plugin",
                                 str(runtime / "triaevum_title_aot.dylib"), cwd=runtime))
        validate_product_info(product, metadata["source_commit"], renderer="vulkan")
        if product.get("private_title_loaded") is not True:
            raise ValueError("Packaged alpha 2 title failed ABI preflight")
        run(str(moved / "Contents/Resources/forge/TriAevumForge"), "--help", cwd=runtime)
        return {"format": "triaevum_macos_app_verification_v1",
                "source_commit": metadata["source_commit"], "architecture": "arm64",
                "signature_verified": True, "relocated_title_loaded": True,
                "frozen_forge_help_passed": True, "runtime_dependencies_bundled": True,
                "portable_shader_corpus_present": True, "rom_required": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = verify(args.app)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
