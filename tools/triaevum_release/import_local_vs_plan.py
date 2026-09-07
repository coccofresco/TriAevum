"""Developer bootstrap from installed VS metadata; not a public catalog resolver."""

import argparse
import tempfile
from pathlib import Path

try:
    from .common import atomic_write_json, load_json_object, sha256_file
    from .plan_windows_toolchain import create_plan
except ImportError:
    from common import atomic_write_json, load_json_object, sha256_file
    from plan_windows_toolchain import create_plan


def import_plan(metadata: list[Path], *, msvc_package_version: str, sdk_build: str) -> dict:
    packages, sources = [], []
    for path in metadata:
        if path.is_symlink():
            raise ValueError("Linked installed metadata is unsupported")
        packages.append(load_json_object(path))
        sources.append({"path": str(path.resolve()), "sha256": sha256_file(path)})
    with tempfile.TemporaryDirectory(prefix="triaevum-local-vs-plan-") as temporary:
        catalog = Path(temporary) / "catalog.json"
        atomic_write_json(catalog, {"packages": packages})
        plan = create_plan(catalog, sha256_file(catalog), msvc_package_version, sdk_build)
    return {**plan, "metadata_source": "installed_visual_studio_private_bootstrap",
            "installed_metadata": sources}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metadata", type=Path, action="append", required=True)
    parser.add_argument("--msvc-package-version", required=True)
    parser.add_argument("--sdk-build", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    atomic_write_json(args.output, import_plan(args.metadata, msvc_package_version=args.msvc_package_version,
                                              sdk_build=args.sdk_build))
