"""Developer-only build of an existing AOT source set against current headers.

Uses a private object cache and never writes into the supplied source directory.
Does not regenerate title code, update a release, or replace a working module.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

TOOLS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TOOLS))
from triaevum_release.whole_aot_object_cache import NativeToolchain, build_generated_cpp_archive


def main():
    p = argparse.ArgumentParser(__doc__)
    for name in ("generated", "output", "compiler", "archiver", "support", "include"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--jobs", type=int, default=2)
    args = p.parse_args()
    repo = TOOLS.parent
    args.output.mkdir(parents=True, exist_ok=True)
    archive = build_generated_cpp_archive(
        generated_directory=args.generated, repo_root=repo,
        nlohmann_include=args.include, cache_root=args.output / "objects",
        toolchain=NativeToolchain(compiler=args.compiler, archiver=args.archiver),
        jobs=args.jobs)
    print(json.dumps(archive), flush=True)
    (args.output / "archive.json").write_text(json.dumps(archive, indent=2))
    runtime = repo / "tools/oot3d/native_game_runtime"
    a32 = repo / "tools/oot3d/native_a32_runtime"
    obj = args.output / "wrapper.obj"
    dll = args.output / "triaevum_title_aot.dll"
    compile_command = [str(args.compiler), "--target=x86_64-pc-windows-msvc",
        "/nologo", "/TP", "/EHsc", "/O2", "/Ob2", "/DNDEBUG", "/std:c++20",
        "/MT", "/bigobj", "/fp:strict", "/w", "/Zc:preprocessor", "/Brepro",
        "/DNOMINMAX", "/DOOT3D_NATIVE_GENERATED_WHOLE_AOT=1",
        *(f"/I{x}" for x in (args.generated, runtime, a32, a32 / "upstream", args.include)),
        "/c", f"/Fo{obj}", str(runtime / "triaevum_title_whole_aot_plugin.cpp")]
    link_command = [str(args.compiler), "--target=x86_64-pc-windows-msvc", "/nologo",
        "/LD", "/MT", f"/Fe{dll}", str(obj), archive["archive"], str(args.support),
        "-fuse-ld=lld", "/link", "/NOIMPLIB", "/OPT:REF", "/OPT:ICF",
        "/INCREMENTAL:NO", "/Brepro", f"/threads:{args.jobs}", f"/MAP:{args.output / 'title.map'}"]
    for label, command in (("wrapper", compile_command), ("link", link_command)):
        print(f"Starting {label}", flush=True)
        result = subprocess.run(command, capture_output=True, text=True, timeout=600)
        (args.output / f"{label}.log").write_text(result.stdout + result.stderr)
        result.check_returncode()
    print(f"Built {dll}: {dll.stat().st_size} bytes", flush=True)


if __name__ == "__main__":
    main()
