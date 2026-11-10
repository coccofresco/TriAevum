"""Publisher build of the Rust Forge for a desktop release package.

Builds TriAevumForge (window and launcher), triaevum-forge (command line) and,
for Windows, triaevum-update-helper (Update from ZIP) from tools/triaevum_forge with the pinned toolchain and Cargo.lock, remaps
build-machine paths out of the binaries, and writes the build receipt
(forge/rust-forge.json) the release audit binds to the corresponding source.
The public import path then needs no Python: the release tooling stays Python.

    python tools/triaevum_release/rust_forge_build.py --target x86_64-pc-windows-msvc \
        --source-commit <commit> --output <empty folder>
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

try:
    from .audit_release import DEFAULT_POLICY, RUST_FORGE_RECEIPT_FORMAT, build_machine_path
    from .common import atomic_write_json, load_json_object, sha256_file
    from .linux_elf_audit import glibc_errors, inspect_elf
    from .release_platform import ANDROID, LINUX, MACOS, WINDOWS, WINDOWS_GNU, for_target, host_platform
    from . import rust_forge_notices
except ImportError:
    from audit_release import DEFAULT_POLICY, RUST_FORGE_RECEIPT_FORMAT, build_machine_path
    from common import atomic_write_json, load_json_object, sha256_file
    from linux_elf_audit import glibc_errors, inspect_elf
    from release_platform import ANDROID, LINUX, MACOS, WINDOWS, WINDOWS_GNU, for_target, host_platform
    import rust_forge_notices


REPO_ROOT = Path(__file__).resolve().parents[2]
FORGE_ROOT = REPO_ROOT / "tools/triaevum_forge"
RECEIPT = "forge/rust-forge.json"
PACKAGES = ("forge-desktop", "forge-cli")
# The Windows package also carries the update helper (UX plan WP-04B).
TARGET_PACKAGES = {WINDOWS.target: (*PACKAGES, "forge-update-helper"), LINUX.target: PACKAGES,
                   MACOS.target: PACKAGES}
# The llvm-mingw Windows product (XBUILD Q2): Rust's gnullvm target links
# against the same UCRT and LLVM libunwind; same packages and binaries.
TARGET_PACKAGES[WINDOWS_GNU.target] = TARGET_PACKAGES[WINDOWS.target]
CARGO_TARGETS = {WINDOWS_GNU.target: "x86_64-pc-windows-gnullvm"}


def cargo_target(target: str) -> str:
    """Rust's target triple for a product target (the same except the gnu product)."""
    return CARGO_TARGETS.get(target, target)
# Cargo binary name -> (package path, release role), per desktop target.
BINARIES = {
    WINDOWS.target: {"TriAevumForge.exe": (WINDOWS.forge, "forge_executable"),
                     "triaevum-forge.exe": ("forge/triaevum-forge.exe", "forge_command_line"),
                     "triaevum-update-helper.exe": ("forge/triaevum-update-helper.exe", "forge_update_helper")},
    LINUX.target: {"TriAevumForge": (LINUX.forge, "forge_executable"),
                   "triaevum-forge": ("forge/triaevum-forge", "forge_command_line")},
    MACOS.target: {"TriAevumForge": (MACOS.forge, "forge_executable"),
                   "triaevum-forge": ("forge/triaevum-forge", "forge_command_line")},
}
BINARIES[WINDOWS_GNU.target] = BINARIES[WINDOWS.target]


def toolchain_channel(root: Path = FORGE_ROOT) -> str:
    return tomllib.loads((root / "rust-toolchain.toml").read_text(encoding="utf-8"))["toolchain"]["channel"]


def _home(variable: str, default: str) -> Path:
    return Path(os.environ.get(variable) or Path.home() / default).resolve()


def rustflags(target: str, target_dir: Path | None = None) -> list[str]:
    """Flags of a public build (CARGO_ENCODED_RUSTFLAGS, which replaces the
    workspace config's target flags, so those come first): no build-machine
    paths in panic locations or debug records, and on Windows a static CRT
    (TriAevumForge.exe sits in the package root, away from the runtime's Visual
    C++ DLLs). Android's libtriaevum_forge.so takes the same remapping.
    `target_dir` (the Cargo target folder) is remapped too: build scripts
    generate sources there that the binary names in panic locations (Linux
    glutin's EGL/GLX bindings)."""
    config = tomllib.loads((FORGE_ROOT / ".cargo/config.toml").read_text(encoding="utf-8"))
    flags = [str(flag) for flag in config.get("target", {}).get(cargo_target(target), {}).get("rustflags", [])]
    flags += [f"--remap-path-prefix={source}={replacement}" for source, replacement in _remaps(target_dir)]
    if target == WINDOWS.target:
        flags += ["-C", "target-feature=+crt-static", "-C", "link-arg=/PDBALTPATH:%_PDB%"]
    elif target == WINDOWS_GNU.target:
        # gnullvm: +crt-static links LLVM libunwind statically, so the Forge
        # executables (root and forge/) need no libunwind.dll; UCRT is a system
        # component on Windows 10+. strip=debuginfo: the PE has no separate PDB,
        # and the toolchain's prebuilt mingw-w64 CRT objects carry their CI
        # build paths in DWARF.
        flags += ["-C", "target-feature=+crt-static", "-C", "strip=debuginfo"]
    return flags


def _remaps(target_dir: Path | None) -> list[tuple[Path, str]]:
    remaps = [(_home("RUSTUP_HOME", ".rustup"), "/rustup"), (_home("CARGO_HOME", ".cargo"), "/cargo"),
              (REPO_ROOT, "/triaevum")]
    if target_dir is not None:
        remaps.append((target_dir.resolve(), "/target"))
    return remaps


def c_flags(target: str, target_dir: Path | None, inherited: str = "") -> str:
    """CFLAGS/ASMFLAGS for crates that compile C or assembly through `cc`
    (ring): the same prefixes are remapped, because a build with debug line
    tables (CARGO_PROFILE_RELEASE_DEBUG) records their source paths, which
    --remap-path-prefix does not reach. GCC and Clang only: MSVC has no
    equivalent, and the Windows build records no C debug paths."""
    if target == WINDOWS.target:
        return inherited
    maps = [f"-ffile-prefix-map={source}={replacement}" for source, replacement in _remaps(target_dir)]
    return " ".join(part for part in (inherited, *maps) if part)


def _rustc_release(cargo: str) -> str:
    rustc = Path(cargo).with_name("rustc" + Path(cargo).suffix) if Path(cargo).parent != Path() else "rustc"
    version = subprocess.run([str(rustc), "-vV"], cwd=FORGE_ROOT, capture_output=True, text=True, check=True).stdout
    match = re.search(r"^release: (\S+)$", version, re.MULTILINE)
    if match is None:
        raise ValueError("rustc -vV did not report its release")
    return match[1]


def target_features(target: str, features) -> tuple[str, ...]:
    """A title's Forge features for the packages `target` builds: the update
    helper is Windows-only, so its feature is dropped on Linux and macOS (Cargo
    refuses a feature of a package it does not build). A feature of a package
    no target builds is an error."""
    known = {package for packages in TARGET_PACKAGES.values() for package in packages}
    selected = []
    for feature in features:
        package = feature.split("/", 1)[0]
        if "/" not in feature or package not in known:
            raise ValueError(f"Not a Forge package feature: {feature}")
        if package in TARGET_PACKAGES[target]:
            selected.append(feature)
    return tuple(selected)


def feature_arguments(features) -> list[str]:
    """Cargo arguments for a title's Forge features (none for OoT3D)."""
    return ["--features", ",".join(features)] if features else []


def build(output: Path, *, target: str, source_commit: str, cargo: str = "cargo",
          target_dir: Path | None = None, policy_path: Path = DEFAULT_POLICY,
          features: tuple[str, ...] = ()) -> dict:
    platform = for_target(target)
    if target not in BINARIES:
        raise ValueError(f"No desktop Rust Forge build for {target}")
    if len(source_commit) != 40 or any(c not in "0123456789abcdef" for c in source_commit):
        raise ValueError("Source commit must be a full Git commit ID")
    errors = rust_forge_notices.check_checked_in()
    if errors:
        raise ValueError("Rust crate notices are stale: " + "; ".join(errors))
    channel = toolchain_channel()
    if _rustc_release(cargo) != channel:
        raise ValueError(f"Build with the pinned Rust {channel} (rustup honors rust-toolchain.toml)")
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"Rust Forge output must be a new or empty folder: {output}")
    target_dir = (target_dir or FORGE_ROOT / "target").resolve()
    environment = {**os.environ, "CARGO_TARGET_DIR": str(target_dir),
                   "CARGO_ENCODED_RUSTFLAGS": "\x1f".join(rustflags(target, target_dir))}
    environment.pop("RUSTFLAGS", None)
    for variable in ("CFLAGS", "ASMFLAGS"):
        flags = c_flags(target, target_dir, environment.get(variable, ""))
        if flags:
            environment[variable] = flags
    features = target_features(target, features)
    command = [cargo, "build", "--release", "--locked", "--target", cargo_target(target),
               *[argument for package in TARGET_PACKAGES[target] for argument in ("-p", package)],
               *feature_arguments(features)]
    subprocess.run(command, cwd=FORGE_ROOT, env=environment, check=True)

    policy = load_json_object(policy_path)
    built = target_dir / cargo_target(target) / "release"
    binaries = []
    output.mkdir(parents=True, exist_ok=True)
    for name, (relative, role) in BINARIES[target].items():
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(built / name, destination)
        destination.chmod(0o755)
        leaked = build_machine_path(destination, policy)
        if leaked is not None:
            raise ValueError(f"{name} embeds a build-machine path ({leaked[:80]})")
        if target == LINUX.target:
            # A rolling host links its newer glibc symbols (atan2f@GLIBC_2.43):
            # build in the Steam Runtime 4 SDK (docs/TRIAEVUM_LINUX_FORGE.md).
            errors = glibc_errors(name, inspect_elf(destination))
            if errors:
                raise ValueError("; ".join(errors) + " (build the Linux Forge in the Steam Runtime 4 SDK)")
        binaries.append({"path": relative, "role": role, "bytes": destination.stat().st_size,
                         "sha256": sha256_file(destination)})
    if host_platform() == platform:
        command_line = next(item["path"] for item in binaries if item["role"] == "forge_command_line")
        completed = subprocess.run([str(output / command_line), "capabilities"],
                                   capture_output=True, text=True, check=True)
        if not isinstance(json.loads(completed.stdout), dict):
            raise RuntimeError("triaevum-forge capabilities did not print a JSON object")
    receipt = {
        "format": RUST_FORGE_RECEIPT_FORMAT,
        "target": target,
        "source_commit": source_commit,
        "toolchain": channel,
        "packages": list(TARGET_PACKAGES[target]),
        "features": list(features),
        "static_crt": target in (WINDOWS.target, WINDOWS_GNU.target),
        "cargo_lock_sha256": sha256_file(FORGE_ROOT / "Cargo.lock"),
        "notices_sha256": sha256_file(rust_forge_notices.NOTICES),
        "binaries": binaries,
    }
    atomic_write_json(output / RECEIPT, receipt)
    files = [{"source": str(output / item["path"]), "path": item["path"], "role": item["role"]} for item in binaries]
    files += [{"source": str(output / RECEIPT), "path": RECEIPT, "role": "forge_build_receipt"},
              {"source": str(rust_forge_notices.NOTICES), "path": rust_forge_notices.NOTICES_RELATIVE,
               "role": "license"}]
    return {"receipt": receipt, "files": files}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--target", required=True, choices=sorted([*BINARIES, ANDROID.target]))
    parser.add_argument("--source-commit")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cargo", default="cargo")
    parser.add_argument("--target-dir", type=Path, help="Cargo target folder (default tools/triaevum_forge/target)")
    parser.add_argument("--title", default="oot3d",
                        help="the title whose Forge to build (its forge_features, title_release.py; default oot3d)")
    parser.add_argument("--print-rustflags", action="store_true",
                        help="print CARGO_ENCODED_RUSTFLAGS for the target (Android's forge-android build) and exit")
    args = parser.parse_args()
    if args.print_rustflags:
        sys.stdout.write("\x1f".join(rustflags(args.target, args.target_dir)))
        return 0
    if args.target == ANDROID.target or args.source_commit is None or args.output is None:
        parser.error("a desktop build needs a desktop --target, --source-commit and --output")
    try:
        try:
            from .title_release import title_release
        except ImportError:
            from title_release import title_release
        result = build(args.output, target=args.target, source_commit=args.source_commit,
                       cargo=args.cargo, target_dir=args.target_dir,
                       features=tuple(title_release(args.title)["forge_features"]))
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"Rust Forge build failed: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
