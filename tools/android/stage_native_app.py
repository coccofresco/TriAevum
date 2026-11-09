#!/usr/bin/env python3
"""Stage and strip publisher-built Android libraries, without rebuilding or copying game data."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

from android_elf_checks import check_stage


def host_llvm(ndk: Path) -> Path:
    """The NDK's prebuilt LLVM directory of the build host (Linux, Windows or macOS)."""
    prebuilt = ndk / "toolchains/llvm/prebuilt"
    hosts = sorted(path for path in prebuilt.iterdir() if path.is_dir()) if prebuilt.is_dir() else []
    if len(hosts) != 1:
        raise FileNotFoundError(f"Expected one NDK host toolchain under {prebuilt}, found {len(hosts)}")
    return hosts[0]


def stage(runtime: Path, title: Path, forge: Path, ndk: Path, output: Path) -> None:
    llvm = host_llvm(ndk)
    exe = ".exe" if (llvm / "bin/llvm-readelf.exe").is_file() else ""
    readelf, strip = llvm / f"bin/llvm-readelf{exe}", llvm / f"bin/llvm-strip{exe}"
    source = {
        "libSDL2.so": runtime / "_deps/sdl2-build/libSDL2.so",
        "libTriAevum.so": runtime / "product/libTriAevum.so",
        "libtriaevum_title_bootstrap.so": runtime / "product/libtriaevum_title_bootstrap.so",
        "libtriaevum_title_aot.so": title,
        "libtriaevum_forge.so": forge,
        "libc++_shared.so": llvm / "sysroot/usr/lib/aarch64-linux-android/libc++_shared.so",
    }
    for name, path in source.items():
        if not path.is_file():
            raise FileNotFoundError(f"{name}: {path}")
        header = subprocess.check_output([readelf, "-h", path], text=True)
        if "AArch64" not in header:
            raise ValueError(f"Not an ARM64 library: {path}")
    output = output.resolve()
    if any(output == path.resolve().parent or path.resolve().is_relative_to(output) for path in source.values()):
        raise ValueError("Stage outside the input library directories")
    destination = output / "arm64-v8a"
    destination.mkdir(parents=True, exist_ok=True)
    allowed = set(source) | {"libc.so", "libm.so", "libdl.so", "liblog.so", "libandroid.so",
                             "libEGL.so", "libGLESv1_CM.so", "libGLESv2.so", "libGLESv3.so", "libvulkan.so",
                             "libOpenSLES.so", "libaaudio.so", "libz.so"}
    for name, path in source.items():
        dynamic = subprocess.check_output([readelf, "-d", path], text=True)
        needed = set(re.findall(r"\(NEEDED\).*\[(.*?)\]", dynamic))
        if needed - allowed:
            raise ValueError(f"Unpackaged dependency for {name}: {needed - allowed}")
        target = destination / name
        shutil.copy2(path, target)
        subprocess.run([strip, "--strip-debug", target], check=True)
        print(f"{name}: {target.stat().st_size} bytes; dependencies {sorted(needed)}")
    # 16 KiB LOAD alignment, one NDK, libc++ symbol closure (android_elf_checks.py).
    problems = check_stage(destination)
    if problems:
        raise ValueError("Staged libraries fail the release checks: " + "; ".join(problems))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-build", type=Path, required=True)
    parser.add_argument("--title-module", type=Path, required=True)
    parser.add_argument("--forge-library", type=Path, required=True,
                        help="libtriaevum_forge.so from the Rust Forge (crates/forge-android)")
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    stage(args.runtime_build, args.title_module, args.forge_library, args.ndk, args.output)
