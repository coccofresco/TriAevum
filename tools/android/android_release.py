#!/usr/bin/env python3
"""The Android release recipe of one title: the exact command list, from the
title facts (titles/<title>/android.json) and the publisher's inputs.

Steps: runtime and renderer, title module (TRIAEVUM_ANDROID_TITLE), Rust
Forge (the title's forge_features), staging with the release ELF checks,
Forge package (signed when TRIAEVUM_RELEASE_SIGNING_KEY is set), Gradle
<flavor>Release (signed when the four TRIAEVUM_ANDROID_* signing variables
are set, else an unsigned candidate), then the APK audit.

By default the commands are printed, nothing runs. `--run` executes them in
order and stops at the first failure. Every path comes from the arguments:
this file names no build machine, drive or user folder.

    python -I tools/android/android_release.py --title mm3d --ndk <ndk> --work <build dir> \\
        --translated-sources <dir> --reference-package <desktop release> --source-commit <40 hex> \\
        --gradle <gradle> [--run]
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/triaevum_release"))
from android_release_policy import android_facts  # noqa: E402

API = 29
TARGET = "aarch64-linux-android"
SIGNING_VARIABLES = ("TRIAEVUM_ANDROID_KEYSTORE", "TRIAEVUM_ANDROID_KEYSTORE_PASS",
                     "TRIAEVUM_ANDROID_KEY_ALIAS", "TRIAEVUM_ANDROID_KEY_PASS")


def ndk_tool(ndk: Path, name: str, windows: bool) -> str:
    host = "windows-x86_64" if windows else "linux-x86_64"
    suffix = ".cmd" if windows and "clang" in name else (".exe" if windows else "")
    return str(ndk / "toolchains/llvm/prebuilt" / host / "bin" / f"{name}{suffix}")


def cmake_base(source: Path, build: Path, ndk: Path) -> list[str]:
    return ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={ndk / 'build/cmake/android.toolchain.cmake'}",
            "-DANDROID_ABI=arm64-v8a", f"-DANDROID_PLATFORM=android-{API}",
            "-DANDROID_STL=c++_shared", "-DCMAKE_BUILD_TYPE=Release"]


def recipe(title: str, *, ndk: Path, work: Path, translated_sources: Path, reference_package: Path,
           source_commit: str, gradle: str = "gradle", windows: bool = os.name == "nt",
           environ: dict | None = None) -> list[tuple[str, list[str], dict]]:
    """[(step, argv, extra environment)] for `title`."""
    facts = android_facts(title)
    environ = os.environ if environ is None else environ
    if len(source_commit) != 40 or any(c not in "0123456789abcdef" for c in source_commit):
        raise ValueError("--source-commit must be a full Git commit ID")
    flavor = facts["flavor"][0].upper() + facts["flavor"][1:]
    runtime, title_build, forge = work / "runtime", work / f"title-{title}", work / "forge"
    stage, package = work / f"stage-{title}", work / f"forge-package-{title}"
    features = [item.split("/", 1)[1] for item in facts["forge_features"]]
    clang = ndk_tool(ndk, f"aarch64-linux-android{API}-clang", windows)
    steps = [
        ("runtime configure", cmake_base(ROOT / "ports/android/runtime", runtime, ndk), {}),
        ("runtime build", ["cmake", "--build", str(runtime), "--target", "oot3d_native_game", "--parallel", "2"], {}),
        ("title configure", cmake_base(ROOT / "ports/android/native", title_build, ndk) + [
            "-DTRIAEVUM_ANDROID_BUILD_NRI=OFF", f"-DTRIAEVUM_ANDROID_TITLE={title}",
            f"-DTRIAEVUM_TRANSLATED_TITLE_DIR={translated_sources}",
            "-DTRIAEVUM_ANDROID_TITLE_OPTIMIZATION=1", "-DTRIAEVUM_ANDROID_TITLE_DEBUG_INFO=OFF"], {}),
        ("title build", ["cmake", "--build", str(title_build), "--target", "triaevum_android_title",
                         "--parallel", "1"], {}),
        ("forge build", ["cargo", "build", "--release", "--manifest-path",
                         str(ROOT / "tools/triaevum_forge/Cargo.toml"), "-p", "forge-android", "--target", TARGET,
                         *(["--features", ",".join(features)] if features else [])],
         {"CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER": clang, "CC_aarch64_linux_android": clang,
          "AR_aarch64_linux_android": ndk_tool(ndk, "llvm-ar", windows), "CARGO_TARGET_DIR": str(forge)}),
        ("stage", [sys.executable, "-I", str(ROOT / "tools/android/stage_native_app.py"),
                   "--runtime-build", str(runtime), "--title-module", str(title_build / "libtriaevum_title_aot.so"),
                   "--forge-library", str(forge / TARGET / "release/libtriaevum_forge.so"),
                   "--ndk", str(ndk), "--output", str(stage)], {}),
        ("forge package", [sys.executable, "-I", str(ROOT / "tools/triaevum_release/android_forge_package.py"),
                           "--title", title, "--reference-package", str(reference_package),
                           "--native-stage", str(stage), "--source-commit", source_commit, "--output", str(package),
                           *(["--sign"] if environ.get("TRIAEVUM_RELEASE_SIGNING_KEY") else [])], {}),
    ]
    signed = all(environ.get(name) for name in SIGNING_VARIABLES)
    steps.append(("apk", [gradle, "-p", str(ROOT / "ports/android"), f":app:assemble{flavor}Release",
                          f"-PtriaevumSdlSource={runtime / '_deps/sdl2-src'}", f"-PtriaevumNativeStage={stage}",
                          f"-PtriaevumForgePackage={package}",
                          *([] if signed else ["-PtriaevumUnsignedReleaseCandidate=true"]),
                          "--no-daemon", "--max-workers=2"], {}))
    return steps


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--title", required=True, choices=("oot3d", "mm3d"))
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True, help="build directory (outside the repository)")
    parser.add_argument("--translated-sources", type=Path, required=True)
    parser.add_argument("--reference-package", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--gradle", default="gradle")
    parser.add_argument("--run", action="store_true", help="execute the steps (default: print them)")
    args = parser.parse_args(argv)
    if args.work.resolve().is_relative_to(ROOT):
        parser.error("--work must be outside the repository")
    steps = recipe(args.title, ndk=args.ndk, work=args.work, translated_sources=args.translated_sources,
                   reference_package=args.reference_package, source_commit=args.source_commit, gradle=args.gradle)
    for step, command, extra in steps:
        prefix = " ".join(f"{key}={shlex.quote(value)}" for key, value in extra.items())
        print(f"# {step}\n{prefix + ' ' if prefix else ''}{shlex.join(command)}")
        if args.run:
            completed = subprocess.run(command, cwd=ROOT, env={**os.environ, **extra})
            if completed.returncode:
                print(f"step {step!r} failed with exit code {completed.returncode}", file=sys.stderr)
                return completed.returncode
    print("# then: aapt2 dump badging, apksigner verify --print-certs, android_release_policy.py audit "
          f"<apk> --title {args.title} (docs/TRIAEVUM_ANDROID_RELEASE.md)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
