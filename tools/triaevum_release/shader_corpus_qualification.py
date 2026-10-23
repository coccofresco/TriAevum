"""Freshness and closure of the portable shader corpus (issue #76 gates 1-2).

A corpus is qualified only for the sources that generated it. Regeneration
records, from the build graph, every in-repository file the generating tools
were compiled from (sources, headers, embedded data) with its SHA-256: the
generator inputs. Each recorded game session records the same for the runtime
that recorded it; the qualification keeps the decision-policy inputs: the
files that decide which variant a draw requests (the shader text and identity
the generator shares with the runtime, the PICA draw path that composes the
request, the scene semantics, settings and effect policies it consults; see
POLICY_PATTERNS). A corpus whose generator inputs changed must be regenerated
(minutes, CPU only); one whose policy inputs changed needs fresh recordings.
Both over-invalidate within their set: any change to a listed file counts,
whether or not it reaches a shader. Runtime files outside the set (window,
input, audio, UI, game logic, GPU memory and upload management, pipeline
caches) cannot change a request, so they never invalidate a recording: the
release runtime may differ from the recording runtime there.

The qualification record is the publishable part of the regeneration receipt:
repository-relative paths and digests only, never private paths. The
publisher checks it against its checkout (bind_shader_corpus) and the audit
against the corresponding source archive the package ships.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path
from typing import Callable, Iterable

try:
    from .common import atomic_write_json, load_json_object, sha256_file
except ImportError:
    from common import atomic_write_json, load_json_object, sha256_file

FORMAT = "triaevum_shader_corpus_qualification_v1"
INPUTS_FORMAT = "triaevum_build_inputs_v1"
# The tools whose output is the corpus: canonical importers, variant replay
# and the SPIR-V compiler.
GENERATOR_TARGETS = ("oot3d_native_pica_capture_inventory", "oot3d_native_pica_cache_inventory",
                     "oot3d_native_pica_variant_inventory", "oot3d_native_pica_aot_compiler")
STAGES = {"vertex": 1, "fragment": 2, "nri_fragment": 3}
# Decision-policy inputs of a recording runtime: its build inputs the generator
# also compiles from (shader text, identity, settings and semantics types) and
# the runtime-only files below. A variant request is composed per draw in the
# Vulkan PICA draw path from the draw's PICA state, the graphics settings and
# the per-draw decisions (surface lighting, reflection material, water, shadow
# receivers, relief, temporal motion); these patterns name the modules that
# take part in that decision. Reviewed with the generator target list.
POLICY_SELECTION = "decision_policy_v2"
POLICY_PATTERNS = (
    r"^runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_pica",
    r"^runtime/three_ds_recomp/src/fast/backends/gfx_vulkan_shaders\.cpp$",
    r"^runtime/three_ds_recomp/include/fast/backends/gfx_vulkan\.h$",
    r"^runtime/three_ds_recomp/include/fast/backends/gfx_native_pica_layout\.h$",
    r"^runtime/three_ds_recomp/(src|include)/fast/renderer3ds/(pica_|nri_pica_pipeline)",
    r"^runtime/three_ds_recomp/(src|include)/fast/oot3d/(pica_|scene_|effect_|display_effect|graphics_settings"
    r"|graphics_preset|gpu_profile|reflection_|azahar_|texture_assignment|texture_catalog|pbr_material"
    r"|native_scene_view|sample_view|perspective_fov|vulkan_adapter_policy|renderer_presentation"
    r"|presentation_settings|temporal_|motion_vector|upscaler_|nri_upscaler)",
    r"^runtime/three_ds_recomp/include/oot3d/",
    r"^tools/oot3d/native_game_runtime/oot3d_native_pica_",
    r"^tools/oot3d/native_pica_frontend/",
)

Reader = Callable[[str], "bytes | None"]


def _ninja(build: Path) -> str:
    cache = build / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("CMAKE_MAKE_PROGRAM:"):
                program = line.split("=", 1)[1].strip()
                if Path(program).is_file():
                    return program
    found = shutil.which("ninja")
    if found is None:
        raise RuntimeError(f"{build}: no Ninja to read the build graph")
    return found


def _query(build: Path, *arguments: str) -> list[str]:
    result = subprocess.run([_ninja(build), "-C", str(build), "-t", *arguments],
                            capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError(f"ninja -t {arguments[0]} failed: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout.splitlines()


def _target_file(build: Path, target: str) -> str:
    for suffix in (".exe", ""):
        if (build / f"{target}{suffix}").is_file():
            return f"{target}{suffix}"
    raise RuntimeError(f"{build}: {target} is not built")


def build_inputs(build: Path, targets: Iterable[str], root: Path) -> dict[str, str]:
    """Every in-repository file the targets are built from, with its SHA-256."""
    build, root = build.resolve(), root.resolve()
    files = [_target_file(build, target) for target in targets]
    inputs = _query(build, "inputs", *files)
    objects = [path for path in inputs if path.endswith((".obj", ".o"))]
    headers: list[str] = []
    for start in range(0, len(objects), 200):
        headers += [line.strip() for line in _query(build, "deps", *objects[start:start + 200])
                    if line.startswith("    ")]
    manifest: dict[str, str] = {}
    for path in {*inputs, *headers}:
        resolved = (build / path).resolve()
        if not resolved.is_file():
            continue
        try:
            relative = resolved.relative_to(root).as_posix()
        except ValueError:
            continue  # toolchain, SDK and package headers are outside the repository
        manifest[relative] = sha256_file(resolved)
    if not manifest:
        raise RuntimeError(f"{build}: no repository inputs found for {', '.join(targets)}")
    return dict(sorted(manifest.items()))


def inputs_digest(manifest: dict[str, str]) -> str:
    digest = hashlib.sha256()
    for path, sha in sorted(manifest.items()):
        digest.update(f"{path}\0{sha}\n".encode("utf-8"))
    return digest.hexdigest()


def inputs_record(build: Path, targets: Iterable[str], root: Path) -> dict:
    targets = list(targets)
    manifest = build_inputs(build, targets, root)
    return {"format": INPUTS_FORMAT, "targets": targets, "sha256": inputs_digest(manifest), "files": manifest}


def changed_inputs(manifest: dict[str, str], read: Reader) -> list[str]:
    """Listed files that are missing or whose content differs."""
    changed = []
    for path, sha in manifest.items():
        data = read(path)
        if data is None or hashlib.sha256(data).hexdigest() != sha:
            changed.append(path)
    return changed


def repository_reader(root: Path) -> Reader:
    def read(path: str) -> bytes | None:
        candidate = root / path
        return candidate.read_bytes() if candidate.is_file() else None
    return read


def _checked_inputs(record: dict, what: str) -> dict[str, str]:
    if (not isinstance(record, dict) or record.get("format") != INPUTS_FORMAT
            or not isinstance(record.get("files"), dict) or not record["files"]
            or inputs_digest(record["files"]) != record.get("sha256")):
        raise ValueError(f"Invalid {what} input record")
    return record["files"]


def module_keys(inventory: dict) -> set[tuple[int, int, int, int]]:
    """(stage, id, secondary hash, size) of an effective shader inventory."""
    return {(STAGES[shader["stage"]], int(shader["source_id"], 16), int(shader["secondary_hash"], 16),
             int(shader["source_size"])) for shader in inventory["shaders"]}


def recipe_keys(manifest: dict) -> set[tuple[int, int, int, int]]:
    """Every module a pipeline manifest's recipes need from the pack."""
    keys = set()
    for pipeline in manifest["pipelines"]:
        sources = [("vertex", pipeline["vertex_source"]), ("fragment", pipeline["fragment_source"])]
        if pipeline.get("nri_fragment_available"):
            sources.append(("nri_fragment", pipeline["nri_fragment_source"]))
        for stage, source in sources:
            keys.add((STAGES[stage], int(source["source_id"], 16), int(source["secondary_hash"], 16),
                      int(source["source_size"])))
    return keys


def closure_failures(pack_modules: Iterable[tuple], inventories: Iterable[dict],
                     manifests: Iterable[dict]) -> dict[str, int]:
    """Gate 2: every replayed module and every recipe's module is in the pack."""
    available = set(pack_modules)
    missing_modules = set().union(*(module_keys(item) for item in inventories)) - available
    missing_recipes = set().union(*(recipe_keys(item) for item in manifests)) - available
    return {"missing_modules": len(missing_modules), "missing_recipe_modules": len(missing_recipes)}


def is_policy_input(path: str, generator_files: Iterable[str] | set[str]) -> bool:
    return path in generator_files or any(re.match(pattern, path) for pattern in POLICY_PATTERNS)


def policy_inputs(recorded: dict, generator: dict) -> dict:
    """The decision-policy subset of a recording runtime's input record."""
    files = _checked_inputs(recorded, "decision-policy")
    generator_files = set(_checked_inputs(generator, "generator"))
    selected = {path: sha for path, sha in files.items() if is_policy_input(path, generator_files)}
    if not selected:
        raise ValueError("Recording runtime inputs hold no decision-policy file")
    return {"format": INPUTS_FORMAT, "targets": recorded["targets"], "selection": POLICY_SELECTION,
            "recorded_files": len(files), "sha256": inputs_digest(selected), "files": selected}


def qualification_record(*, generator: dict, policies: list[dict], pack_sha256: str,
                         manifest_sha256: list[str], modules: int, requests: int) -> dict:
    """The publishable qualification: digests and repository paths only.

    Each policy is a recording runtime's full input record; the qualification
    keeps its decision-policy subset (policy_inputs). User decision 2026-10-22:
    release work after a freeze stays offline.
    """
    _checked_inputs(generator, "generator")
    for policy in policies:
        _checked_inputs(policy, "decision-policy")
    policies = [policy_inputs(policy, generator) for policy in policies]
    if not policies or not requests:
        raise ValueError("A qualified corpus requires recorded variant sessions")
    return {"format": FORMAT, "generator_inputs": generator, "policy_inputs": policies,
            "pack_sha256": pack_sha256, "pipeline_manifest_sha256": sorted(manifest_sha256),
            "modules": modules, "variant_requests": requests}


def validate_qualification(record: dict, *, pack_sha256: str, manifest_sha256: Iterable[str],
                           read: Reader) -> None:
    """Refuse a corpus that is not the one qualified, or not for these sources."""
    if not isinstance(record, dict) or record.get("format") != FORMAT:
        raise ValueError("Shader corpus has no qualification record")
    if record.get("pack_sha256") != pack_sha256:
        raise ValueError("Shader corpus differs from its qualification record")
    if record.get("pipeline_manifest_sha256") != sorted(manifest_sha256):
        raise ValueError("Pipeline recipes differ from the shader corpus qualification")
    if not record.get("variant_requests") or not record.get("policy_inputs"):
        raise ValueError("Shader corpus was not regenerated from recorded variant sessions")
    stale = changed_inputs(_checked_inputs(record.get("generator_inputs"), "generator"), read)
    if stale:
        raise ValueError(f"Stale shader corpus: {len(stale)} generator inputs changed since regeneration "
                         f"(first: {stale[0]}); regenerate it")
    for policy in record["policy_inputs"]:
        stale = changed_inputs(_checked_inputs(policy, "decision-policy"), read)
        if stale:
            raise ValueError(f"Stale variant recordings: {len(stale)} runtime inputs changed since recording "
                             f"(first: {stale[0]}); record the sessions again")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    record = commands.add_parser("record-inputs", help="Write the input record of built targets")
    record.add_argument("--build", type=Path, required=True)
    record.add_argument("--target", action="append", required=True)
    record.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    record.add_argument("--output", type=Path, required=True)
    check = commands.add_parser("check", help="Check a qualification record against a checkout")
    check.add_argument("--qualification", type=Path, required=True)
    check.add_argument("--pack", type=Path, required=True)
    check.add_argument("--pipeline-manifest", type=Path, action="append", required=True)
    check.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    select = commands.add_parser("policy-inputs",
                                 help="List the decision-policy subset of a recording's input record")
    select.add_argument("--inputs", type=Path, required=True, help="a session's runtime-inputs.json")
    select.add_argument("--qualification", type=Path, required=True,
                        help="the qualification whose generator inputs complete the selection")
    args = parser.parse_args()
    if args.command == "record-inputs":
        result = inputs_record(args.build, args.target, args.root)
        atomic_write_json(args.output, result)
        print(json.dumps({"files": len(result["files"]), "sha256": result["sha256"]}))
        return 0
    if args.command == "policy-inputs":
        record = load_json_object(args.qualification)
        selected = policy_inputs(load_json_object(args.inputs), record["generator_inputs"])
        for path in selected["files"]:
            print(path)
        print(json.dumps({"selection": POLICY_SELECTION, "recorded_files": selected["recorded_files"],
                          "files": len(selected["files"]), "sha256": selected["sha256"]}))
        return 0
    validate_qualification(load_json_object(args.qualification), pack_sha256=sha256_file(args.pack),
                           manifest_sha256=[sha256_file(path) for path in args.pipeline_manifest],
                           read=repository_reader(args.root))
    print("qualified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
