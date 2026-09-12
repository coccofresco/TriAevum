#!/usr/bin/env python3
"""Stage and strip publisher-built Android libraries, without rebuilding or copying game data."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess


def stage(runtime: Path, title: Path, ndk: Path, output: Path) -> None:
    llvm = ndk / "toolchains/llvm/prebuilt/linux-x86_64"
    source = {
        "libSDL2.so": runtime / "_deps/sdl2-build/libSDL2.so",
        "libTriAevum.so": runtime / "product/libTriAevum.so",
        "libtriaevum_title_bootstrap.so": runtime / "product/libtriaevum_title_bootstrap.so",
        "libtriaevum_title_aot.so": title,
        "libc++_shared.so": llvm / "sysroot/usr/lib/aarch64-linux-android/libc++_shared.so",
    }
    for name, path in source.items():
        if not path.is_file():
            raise FileNotFoundError(f"{name}: {path}")
        header = subprocess.check_output([llvm / "bin/llvm-readelf", "-h", path], text=True)
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
        dynamic = subprocess.check_output([llvm / "bin/llvm-readelf", "-d", path], text=True)
        needed = set(re.findall(r"\(NEEDED\).*\[(.*?)\]", dynamic))
        if needed - allowed:
            raise ValueError(f"Unpackaged dependency for {name}: {needed - allowed}")
        target = destination / name
        shutil.copy2(path, target)
        subprocess.run([llvm / "bin/llvm-strip", "--strip-debug", target], check=True)
        print(f"{name}: {target.stat().st_size} bytes; dependencies {sorted(needed)}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-build", type=Path, required=True)
    parser.add_argument("--title-module", type=Path, required=True)
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    stage(args.runtime_build, args.title_module, args.ndk, args.output)
