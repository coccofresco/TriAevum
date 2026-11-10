"""Write the whole-AOT title build receipt (whole-aot-plugin.json) for a title
module built by tools/triaevum_release/linux_title from a published translated
source (TITLE_SOURCE_MANIFEST.json with its files beside it).

The Windows clang-cl route writes this receipt itself (forge.py build-title,
whole_aot_plugin_backend). The llvm-mingw Windows build (XBUILD) and other
CMake-built title modules record the same correspondence here, so
prepare_release.py --title-build binds them the same way:

    python -m tools.triaevum_release.title_module_receipt \
        --plugin <build>/triaevum_title_aot.dll --target x86_64-w64-windows-gnu \
        --translated-manifest <translated>/TITLE_SOURCE_MANIFEST.json \
        --compiler <llvm-mingw>/bin/x86_64-w64-mingw32-clang++ --flags "-O2 -ffp-model=strict"
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

try:
    from .common import atomic_write_json, load_json_object, normalize_relative_path, sha256_file
    from .release_platform import for_target
    from .whole_aot_plugin_backend import FORMAT
    from .precompiled_title_layout import TRANSLATED_SOURCE_FORMAT
except ImportError:
    from common import atomic_write_json, load_json_object, normalize_relative_path, sha256_file
    from release_platform import for_target
    from whole_aot_plugin_backend import FORMAT
    from precompiled_title_layout import TRANSLATED_SOURCE_FORMAT

RECEIPT_NAME = "whole-aot-plugin.json"
BACKEND = "linux_title_cmake_v1"


def _identity(parts) -> str:
    digest = hashlib.sha256()
    for key, value in parts:
        digest.update(f"{key}={value}\n".encode("utf-8"))
    return digest.hexdigest()


def compiler_version(compiler: Path) -> str:
    completed = subprocess.run([str(compiler), "--version"], capture_output=True, text=True, check=True)
    return completed.stdout.splitlines()[0].strip()


def write_receipt(*, plugin: Path, translated_manifest: Path, target: str, compiler_identity: str,
                  flags: str, profile_data: Path | None = None) -> dict:
    platform = for_target(target)
    plugin = plugin.resolve(strict=True)
    if plugin.name != platform.title_module:
        raise ValueError(f"A {target} title module is named {platform.title_module}, not {plugin.name}")
    translated = load_json_object(translated_manifest)
    origin = translated.get("build")
    if translated.get("format") != TRANSLATED_SOURCE_FORMAT or not isinstance(origin, dict):
        raise ValueError("Not a translated title source manifest (TITLE_SOURCE_MANIFEST.json)")
    files = translated.get("files")
    if not isinstance(files, dict) or not files:
        raise ValueError("Translated-source manifest has no files")
    for relative, expected in sorted(files.items()):
        relative = normalize_relative_path(relative)
        path = translated_manifest.parent / relative
        if "/" in relative or path.is_symlink() or sha256_file(path) != expected:
            raise ValueError(f"Translated source differs from its manifest: {relative}")
    for key in ("code_sha256", "program_sha256", "shards", "functions"):
        if origin.get(key) is None:
            raise ValueError(f"Translated-source build record lacks {key}")
    toolchain = _identity((("target", target), ("compiler", compiler_identity), ("flags", flags)))
    profile_sha256 = sha256_file(profile_data) if profile_data is not None else None
    receipt = {
        "format": FORMAT,
        "status": "built",
        "backend": BACKEND,
        "profile": platform.profile,
        "target": target,
        "toolchain_identity_sha256": toolchain,
        "compiler": compiler_identity,
        "optimization": flags,
        "profile_data_sha256": profile_sha256,
        "translator_identity_sha256": origin.get("translator_identity_sha256"),
        "translated_source_manifest_sha256": sha256_file(translated_manifest),
        "program_sha256": origin["program_sha256"],
        "code_sha256": origin["code_sha256"],
        "plugin_sha256": sha256_file(plugin),
        "plugin_bytes": plugin.stat().st_size,
        "functions": origin["functions"],
        "shards": origin["shards"],
        "build_target": "tools/triaevum_release/linux_title",
    }
    atomic_write_json(plugin.parent / RECEIPT_NAME, receipt)
    return receipt


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--translated-manifest", type=Path, required=True)
    parser.add_argument("--target", required=True)
    identity = parser.add_mutually_exclusive_group(required=True)
    identity.add_argument("--compiler", type=Path, help="compiler whose --version identifies the toolchain")
    identity.add_argument("--compiler-identity", help="the compiler's version line, given directly")
    parser.add_argument("--flags", required=True, help="the title module's optimisation flags")
    parser.add_argument("--profile-data", type=Path, help="PGO profile the module was built with")
    args = parser.parse_args(argv)
    try:
        compiler = args.compiler_identity or compiler_version(args.compiler)
        receipt = write_receipt(plugin=args.plugin, translated_manifest=args.translated_manifest,
                                target=args.target, compiler_identity=compiler, flags=args.flags,
                                profile_data=args.profile_data)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"title module receipt refused: {error}", file=sys.stderr)
        return 1
    print(json.dumps(receipt, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
