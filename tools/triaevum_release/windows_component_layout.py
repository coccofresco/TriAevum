"""Map verified package layouts to the private Forge sysroot, without host lookup."""

import argparse
from pathlib import Path

try:
    from .common import load_json_object, normalize_relative_path, sha256_file
except ImportError:
    from common import load_json_object, normalize_relative_path, sha256_file


def component_trees(root: Path, *, msvc_version: str, sdk_version: str) -> list[tuple[Path, str]]:
    for version in (msvc_version, sdk_version):
        if not version or any(not part.isdigit() for part in version.split(".")):
            raise ValueError("Invalid component version")
    root = root.resolve(strict=True)
    receipt = load_json_object(root / "components.json")
    if (receipt.get("format") != "triaevum_windows_components_v1"
            or receipt.get("status") != "extracted_not_activated"):
        raise ValueError("Unsupported component receipt")
    components = receipt.get("components")
    if not isinstance(components, list) or not components:
        raise ValueError("Empty component receipt")
    trees = []
    for component in components:
        directory = root / normalize_relative_path(component["path"])
        if not directory.resolve().is_relative_to(root) or directory.is_symlink():
            raise ValueError("Component directory escaped its receipt")
        files = component["extraction"]["files"]
        expected = set()
        kind = component["payload"]["kind"]
        if kind not in ("vsix", "msi") or not files:
            raise ValueError("Unsupported or empty component inventory")
        for identifier, descriptor in files.items():
            name = normalize_relative_path(descriptor["path"] if kind == "msi" else identifier)
            path = directory / name
            if (not path.resolve().is_relative_to(directory) or path.is_symlink()
                    or not path.is_file() or path.stat().st_size != descriptor["bytes"]
                    or sha256_file(path) != descriptor["sha256"]):
                raise ValueError("Changed component file: " + name)
            if name.casefold() in expected:
                raise ValueError("Duplicate component destination")
            expected.add(name.casefold())
        actual = set()
        for path in directory.rglob("*"):
            if path.is_symlink():
                raise ValueError("Linked component entry")
            if path.is_file():
                actual.add(path.relative_to(directory).as_posix().casefold())
        if actual != expected:
            raise ValueError("Unindexed component files")
        if kind == "vsix":
            base = directory / "Contents/VC/Tools/MSVC" / msvc_version
            mappings = [(base / "include", "msvc/include"),
                        (base / "lib/x64", "msvc/lib/x64")]
        else:
            base = directory / "Windows Kits/10"
            mappings = [(base / "Include" / sdk_version, f"sdk/Include/{sdk_version}"),
                        (base / "Lib" / sdk_version / "ucrt/x64", f"sdk/Lib/{sdk_version}/ucrt/x64"),
                        (base / "Lib" / sdk_version / "um/x64", f"sdk/Lib/{sdk_version}/um/x64")]
        selected = [(path, prefix) for path, prefix in mappings if path.is_dir()]
        if not selected:
            raise ValueError("No supported toolset layout in component: " + component["path"])
        trees.extend(selected)
    return trees


if __name__ == "__main__":
    try:
        from .assemble_windows_sysroot import assemble
    except ImportError:
        from assemble_windows_sysroot import assemble
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--components", type=Path, action="append", required=True)
    parser.add_argument("--msvc-version", required=True)
    parser.add_argument("--sdk-version", required=True)
    parser.add_argument("--clang-resource", type=Path, required=True)
    parser.add_argument("--clang-version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    trees = []
    for root in args.components:
        trees.extend(component_trees(root, msvc_version=args.msvc_version, sdk_version=args.sdk_version))
    trees.append((args.clang_resource / "include", "clang/include"))
    print(assemble(trees, msvc_version=args.msvc_version, sdk_version=args.sdk_version,
                   clang_version=args.clang_version, output=args.output))
