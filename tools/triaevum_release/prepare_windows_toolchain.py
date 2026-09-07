"""Prepare a private toolchain generation, publishing only after native ABI proof."""

import argparse
import tempfile
from pathlib import Path

try:
    from .common import atomic_write_json, load_json_object
    from .acquire_windows_components import acquire_components
    from .windows_component_layout import component_trees
    from .assemble_windows_sysroot import assemble
    from .validate_whole_aot_toolchain import validate
    from .publish_directory import publish_directory
except ImportError:
    from common import atomic_write_json, load_json_object
    from acquire_windows_components import acquire_components
    from windows_component_layout import component_trees
    from assemble_windows_sysroot import assemble
    from validate_whole_aot_toolchain import validate
    from publish_directory import publish_directory


def prepare(plan: dict, *, cache: Path, output: Path, diagnostics: Path,
            compiler: Path, archiver: Path, support: Path, include: Path,
            clang_resource: Path, clang_version: str, msvc_version: str,
            sdk_version: str, accept_licenses: bool, progress=None) -> dict:
    if not accept_licenses:
        raise ValueError("Applicable Microsoft licenses must be explicitly accepted")
    output = output.resolve()
    if output.exists():
        raise FileExistsError("Toolchain preparation requires a new generation")
    output.parent.mkdir(parents=True, exist_ok=True)
    def report(stage, message):
        if progress:
            progress(stage, message)
    with tempfile.TemporaryDirectory(prefix=".toolchain-prepare-", dir=output.parent) as temporary:
        staging = Path(temporary).resolve()
        if not staging.is_relative_to(output.parent):
            raise ValueError("Toolchain staging escaped its parent")
        components = staging / "components"
        acquisition = acquire_components(plan, cache, components, accept_licenses=True, progress=progress)
        report("assemble", "Verifying and assembling the private Windows dependencies")
        trees = component_trees(components, msvc_version=msvc_version, sdk_version=sdk_version)
        trees.append((clang_resource / "include", "clang/include"))
        generation = staging / "generation"
        generation.mkdir()
        assembled = assemble(trees, msvc_version=msvc_version, sdk_version=sdk_version,
                             clang_version=clang_version, output=generation / "sysroot")
        report("probe", "Compiling and loading the native ABI probe")
        proof = validate(compiler, archiver, support, include, generation / "sysroot", diagnostics)
        if proof.get("status") != "passed" or proof.get("sysroot_identity") != assembled["identity"]:
            raise ValueError("Native proof does not match the prepared sysroot")
        receipt = {"format": "triaevum_prepared_toolchain_v1", "status": "native_probe_passed",
                   "sysroot": "sysroot", "identity": assembled["identity"],
                   "acquisition": acquisition, "native_probe": proof,
                   "scope": "private_toolchain_not_full_title_qualification"}
        atomic_write_json(generation / "toolchain.json", receipt)
        # Preserve complete original package metadata/license material alongside
        # the minimal sysroot. It is private and is never added to public bundles.
        publish_directory(components, generation / "components")
        publish_directory(generation, output)
    return receipt


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("plan", "cache", "output", "diagnostics", "compiler", "archiver",
                 "support", "include", "clang-resource"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in ("clang-version", "msvc-version", "sdk-version"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--accept-licenses", action="store_true")
    args = vars(parser.parse_args())
    args["plan"] = load_json_object(args["plan"])
    print(prepare(**args, progress=lambda stage, message: print(stage, message, flush=True))["status"])
