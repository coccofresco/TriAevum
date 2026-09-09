"""Assemble a relocatable, locally signed Apple Silicon application."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import plistlib
import shutil
import subprocess
from pathlib import Path


def run(*args: str) -> str:
    return subprocess.check_output(args, text=True).strip()


def dependencies(path: Path) -> list[str]:
    return [line.strip().split(" (compatibility", 1)[0]
            for line in run("otool", "-L", str(path)).splitlines()[1:]]


def bundle_libraries(runtime: Path) -> list[dict]:
    library_dir = runtime / "lib"
    library_dir.mkdir()
    prefix = Path(run("brew", "--prefix"))
    molten = Path(run("brew", "--prefix", "molten-vk")) / "lib/libMoltenVK.dylib"
    pending = [(runtime / "TriAevum", runtime / "TriAevum"),
               (runtime / "triaevum_title_aot.dylib", runtime / "triaevum_title_aot.dylib")]
    copied: dict[str, Path] = {}

    def copy(source: Path) -> Path:
        name = source.name
        if name in copied:
            if copied[name].resolve() != source.resolve():
                raise ValueError(f"Conflicting bundled library: {name}")
            return library_dir / name
        target = library_dir / name
        shutil.copy2(source, target)
        target.chmod(0o755)
        copied[name] = source
        pending.append((source, target))
        return target

    copy(molten)
    visited = set()
    while pending:
        source, target = pending.pop()
        if target in visited:
            continue
        visited.add(target)
        for dependency in dependencies(source):
            if dependency.startswith(("/usr/lib/", "/System/Library/")):
                continue
            if dependency.startswith("@rpath/"):
                candidate = source.parent / dependency.removeprefix("@rpath/")
                if not candidate.exists():
                    candidate = prefix / "lib" / Path(dependency).name
            elif dependency.startswith("@loader_path/"):
                candidate = source.parent / dependency.removeprefix("@loader_path/")
            else:
                candidate = Path(dependency)
            if not candidate.is_file():
                raise ValueError(f"Cannot resolve {dependency} for {source}")
            # A dylib's own install name is listed by otool as well.
            if candidate.resolve() == source.resolve():
                continue
            child = copy(candidate)
            relative = os.path.relpath(child, target.parent)
            subprocess.run(["install_name_tool", "-change", dependency,
                            "@loader_path/" + relative, str(target)], check=True)
        if target.suffix == ".dylib":
            subprocess.run(["install_name_tool", "-id", "@rpath/" + target.name, str(target)], check=True)
        subprocess.run(["codesign", "--force", "--sign", "-", str(target)], check=True)
    (library_dir / "MoltenVK_icd.json").write_text(json.dumps({
        "file_format_version": "1.0.0",
        "ICD": {"library_path": "./libMoltenVK.dylib", "api_version": "1.2.0",
                "is_portability_driver": True}}, indent=2) + "\n")
    return [{"name": name, "source": str(source),
             "sha256": hashlib.sha256((library_dir / name).read_bytes()).hexdigest()}
            for name, source in sorted(copied.items())]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--title", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = args.build.resolve()
    app = args.output.resolve()
    if app.exists():
        raise ValueError(f"Output exists; use a new application path: {app}")
    contents = app / "Contents"
    resources = contents / "Resources"
    executable_dir = contents / "MacOS"
    executable_dir.mkdir(parents=True)
    runtime = resources / "runtime"
    runtime.mkdir(parents=True)
    shutil.copy2(build / "TriAevum", runtime / "TriAevum")
    shutil.copy2(args.title, runtime / "triaevum_title_aot.dylib")
    shutil.copytree(build / "installation/recipes", runtime / "recipes")
    shutil.copytree(build / "installation/resources", runtime / "resources")
    shutil.copytree(build / "forge-dist/TriAevumForge", resources / "forge", symlinks=True)
    inventory = bundle_libraries(runtime)
    minimum = run("sw_vers", "-productVersion")
    with (contents / "Info.plist").open("wb") as stream:
        plistlib.dump({"CFBundleName": "TriAevum", "CFBundleDisplayName": "TriAevum",
                       "CFBundleIdentifier": "org.triaevum.macos", "CFBundleVersion": "1",
                       "CFBundleShortVersionString": "0.6.0-macos-dev",
                       "CFBundleExecutable": "TriAevum", "CFBundlePackageType": "APPL",
                       "NSHighResolutionCapable": True, "LSMinimumSystemVersion": minimum}, stream)
    subprocess.run(["swiftc", "-O", "-swift-version", "5", "-framework", "AppKit",
                    str(root / "ports/macos/Launcher.swift"), "-o", str(executable_dir / "TriAevum")], check=True)
    for name in ("LICENSE", "LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(root / name, resources / name)
    shutil.copytree(root / "LICENSES", resources / "LICENSES")
    (resources / "macos-build.json").write_text(json.dumps({
        "format": "triaevum_macos_development_bundle_v1", "architecture": "arm64",
        "minimum_macos": minimum, "source_commit": run("git", "rev-parse", "HEAD"),
        "source_dirty": bool(run("git", "status", "--porcelain")),
        "libraries": inventory}, indent=2) + "\n")
    subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(app)], check=True)
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
    print(app)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
