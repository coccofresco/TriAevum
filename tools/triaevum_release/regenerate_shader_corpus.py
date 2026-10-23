"""Developer-only regeneration of the portable PICA shader corpus.

The pack's modules are keyed by the source text the runtime generates, so any
change to the PICA lowering, the NRI variants or the surface lighting leaves a
pack built by an older runtime unresolved (Windows qualification A8). The
private inputs are raw PICA state and stay valid: capture corpora (register
snapshots, LUTs, programs) and Citra transferable caches. This re-imports them
with the importers of the runtime that will consume the pack, writes the
observed pipeline manifest, compiles the union and records what it did.

Captures and Citra caches hold the Authentic programs only. A graphics
profile's instrumented variants (guides, per-pixel lighting, shadows, temporal
inputs) and its attachments depend on decisions the runtime makes per draw, so
they come from recorded game sessions. Sessions record requests, not shader
text (issue #76): a directory given with --variant-records holds, per session,
raw-draws.jsonl (OOT3D_PICA_RAW_DRAWS), variant-requests.jsonl
(OOT3D_PICA_VARIANT_REQUESTS), session-pipelines.json
(OOT3D_PICA_PIPELINE_INVENTORY) and runtime-inputs.json, the recording
runtime's inputs (shader_corpus_qualification.py record-inputs --target
TriAevum). oot3d_native_pica_variant_inventory rebuilds
every recorded request with this build's generators and the real variant
cache, requires each output to match the recorded witness, and rewrites the
recipes into runtime-pipelines.json, a manifest for device preparation. Shader
text a previous build wrote (*-shaders.json) is never an input: it goes stale
with any generator change.

Sessions sample the game; --synthesis-effects closes the corpus over the
programs they never drew (fight effects, enemies, cutscenes). Every fragment
program of the given Citra caches is transplanted onto a recorded draw and
resolved under each context class the sessions recorded with the effects
common to every session under those directories (the shipped profile). The
replay also requires every resolved request, recorded or synthesized, to have
an always-available program (strategy A.1): a draw without one would stall
while its specialized program compiles.

The output is checked before it is accepted: every replayed module and every
recipe's module must be in the compiled pack (closure), and qualification.json
records the generator inputs of the tools used and the runtime inputs of each
session, which the publisher and the release audit check against the sources
being released (shader_corpus_qualification.py).

A capture corpus is imported by oot3d_native_pica_capture_inventory, which
depends only on the captures and on itself, not on the runtime's generators.
When the captures themselves are no longer at hand, --capture-inventory takes
the import an earlier regeneration wrote from them (captures-N-inventory.json
and captures-N-pipelines.json) and copies it in; regeneration.json records
both files' SHA-256 and the importer's, so the reuse stays traceable.

Inputs, packs and manifests are game-derived and private; nothing here is
published. Build the tools from the same source as the runtime:
oot3d_native_pica_capture_inventory, oot3d_native_pica_cache_inventory,
oot3d_native_pica_variant_inventory, oot3d_native_pica_aot_compiler and
oot3d_native_pica_pipeline_manifest.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import time
from pathlib import Path

try:
    from .merge_shader_packs import decode
    from .shader_corpus_qualification import (GENERATOR_TARGETS, closure_failures, inputs_record,
                                              qualification_record)
except ImportError:
    from merge_shader_packs import decode
    from shader_corpus_qualification import (GENERATOR_TARGETS, closure_failures, inputs_record,
                                             qualification_record)

ROOT = Path(__file__).resolve().parents[2]


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def _run(command: list[str], log: Path, accept: tuple[int, ...] = (0,)) -> tuple[float, int]:
    start = time.monotonic()
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=False)
    if result.returncode not in accept:
        raise RuntimeError(f"{Path(command[0]).name} exited {result.returncode}; see {log}")
    return time.monotonic() - start, result.returncode


def _cache_report(log: Path) -> dict:
    text = log.read_text(encoding="utf-8")
    return json.loads(text[: text.rindex("}") + 1])


RAW_DRAWS = "raw-draws.jsonl"
VARIANT_REQUESTS = "variant-requests.jsonl"
SESSION_PIPELINES = "session-pipelines.json"
POLICY_INPUTS = "runtime-inputs.json"
CAPTURE_INVENTORY_FORMAT = "oot3d_pica_effective_shader_inventory_v1"
PIPELINE_MANIFEST_FORMAT = "oot3d_pica_pipeline_manifest_v2"


def _checked_copy(source: Path, destination: Path, expected_format: str) -> str:
    """Copy an earlier capture import after checking what it claims to be; returns its SHA-256."""
    try:
        actual = json.loads(source.read_text(encoding="utf-8")).get("format")
    except (OSError, ValueError) as error:
        raise RuntimeError(f"{source}: not a readable capture import: {error}") from error
    if actual != expected_format:
        raise RuntimeError(f"{source}: format {actual!r}, expected {expected_format!r}")
    shutil.copyfile(source, destination)
    return _sha256(source)


def variant_sessions(directories: list[Path]) -> list[dict[str, Path]]:
    """Every recorded session under the given directories, each complete."""
    sessions: list[dict[str, Path]] = []
    for directory in directories:
        found = sorted(directory.rglob(VARIANT_REQUESTS))
        if not found:
            raise RuntimeError(f"{directory}: no recorded session ({VARIANT_REQUESTS})")
        for requests in found:
            session = requests.parent
            files = {"raw_draws": session / RAW_DRAWS, "variant_requests": requests,
                     "pipelines": session / SESSION_PIPELINES, "policy_inputs": session / POLICY_INPUTS}
            missing = [path.name for path in files.values() if not path.is_file()]
            if missing:
                raise RuntimeError(f"{session}: incomplete recorded session, missing {', '.join(missing)}")
            sessions.append({"directory": session, **files})
    return sessions


def regenerate(tools: Path, captures: list[Path], caches: list[Path], output: Path,
               variant_records: list[Path] | None = None, root: Path = ROOT,
               compile_cache: Path | None = None, jobs: int | None = None,
               synthesis_effects: list[Path] | None = None,
               capture_imports: list[tuple[Path, Path]] | None = None) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if (tools / "oot3d_native_pica_aot_compiler.exe").exists() else ""
    tool = {name: str(tools / f"oot3d_native_pica_{name}{suffix}")
            for name in ("capture_inventory", "cache_inventory", "variant_inventory", "aot_compiler",
                         "pipeline_manifest")}
    inventories: list[Path] = []
    pipelines: list[Path] = []
    steps = []
    for index, corpus in enumerate(captures):
        inventory = output / f"captures-{index}-inventory.json"
        manifest = output / f"captures-{index}-pipelines.json"
        seconds, _ = _run([tool["capture_inventory"], "--manifest", str(corpus), "--output", str(inventory),
                           "--pipeline-manifest", str(manifest)], output / f"captures-{index}-import.log")
        inventories.append(inventory)
        pipelines.append(manifest)
        steps.append({"input": str(corpus), "inventory": inventory.name, "pipelines": manifest.name,
                      "seconds": round(seconds, 1)})
    for offset, (inventory_source, manifest_source) in enumerate(capture_imports or []):
        # An import of captures no longer at hand, written by this tool from them
        # earlier. The importer does not depend on the runtime's generators; its
        # identity is recorded so the reuse can be checked against the one that
        # wrote the import.
        index = len(captures) + offset
        inventory = output / f"captures-{index}-inventory.json"
        manifest = output / f"captures-{index}-pipelines.json"
        inventory_sha256 = _checked_copy(inventory_source, inventory, CAPTURE_INVENTORY_FORMAT)
        manifest_sha256 = _checked_copy(manifest_source, manifest, PIPELINE_MANIFEST_FORMAT)
        inventories.append(inventory)
        pipelines.append(manifest)
        importer = Path(tool["capture_inventory"])
        steps.append({"input": str(inventory_source), "input_sha256": inventory_sha256,
                      "pipelines_input": str(manifest_source), "pipelines_input_sha256": manifest_sha256,
                      "reused_import": True,
                      "importer_sha256": _sha256(importer) if importer.is_file() else None,
                      "inventory": inventory.name, "pipelines": manifest.name})
    for index, cache in enumerate(caches):
        inventory = output / f"cache-{index}-inventory.json"
        log = output / f"cache-{index}-import.log"
        # Exit 3 is a partial import. A transferable cache yields fragment
        # modules only; vertex/geometry records are reported, never imported,
        # and are the only gap accepted here (the TAS cache has one).
        seconds, code = _run([tool["cache_inventory"], "--input", str(cache), "--dialect", "citra-legacy-v1",
                              "--output", str(inventory)], log, accept=(0, 3))
        report = _cache_report(log)
        if code == 3 and any(item["failures"] for item in report["inputs"]):
            raise RuntimeError(f"{cache}: fragment records failed to import; see {log}")
        inventories.append(inventory)
        steps.append({"input": str(cache), "input_sha256": _sha256(cache), "inventory": inventory.name,
                      "complete_import": report["complete_import"],
                      "vertex_records_not_imported": sum(item["vertex_records_not_imported"]
                                                         for item in report["inputs"]),
                      "geometry_records_not_imported": sum(item["geometry_records_not_imported"]
                                                           for item in report["inputs"]),
                      "seconds": round(seconds, 1)})
    sessions = variant_sessions(variant_records or [])
    targets = variant_sessions(synthesis_effects or [])
    if targets and not sessions:
        raise RuntimeError("--synthesis-effects needs recorded sessions (--variant-records)")
    if targets and not caches:
        raise RuntimeError("--synthesis-effects needs a Citra cache (--citra-cache)")
    policies = [json.loads(session["policy_inputs"].read_text(encoding="utf-8")) for session in sessions]
    requests = 0
    if sessions:
        shaders = output / "variant-shaders.json"
        manifest = output / "runtime-pipelines.json"
        report_path = output / "variant-replay.json"
        command = [tool["variant_inventory"]]
        for session in sessions:
            command += ["--session", str(session["raw_draws"]), str(session["variant_requests"]),
                        "--pipelines", str(session["pipelines"])]
        for cache in caches if targets else []:
            command += ["--synthesize-cache", str(cache)]
        for target in targets:
            command += ["--synthesize-effects", str(target["variant_requests"])]
        command += ["--shader-inventory", str(shaders), "--pipeline-manifest", str(manifest),
                    "--report", str(report_path), "--require-witness", "--require-uber"]
        # Exit 3: a request or recipe could not be rebuilt; exit 4: a rebuilt
        # output differs from the one the game produced; exit 5: a request has
        # no always-available program. All stop here.
        seconds, _ = _run(command, output / "variant-replay.log")
        report = json.loads(report_path.read_text(encoding="utf-8"))
        requests = sum(item["requests"] for item in report["sessions"])
        synthesis = report.get("synthesis")
        inventories.append(shaders)
        pipelines.append(manifest)
        steps.append({"variant_sessions": [{"directory": str(session["directory"]),
                                            **{f"{name}_sha256": _sha256(session[name])
                                               for name in ("raw_draws", "variant_requests", "pipelines",
                                                            "policy_inputs")}}
                                           for session in sessions],
                      "requests": requests,
                      "witness_mismatches": report["witness_mismatches"],
                      "uber_gaps": report.get("uber_gaps"),
                      "synthesis": None if synthesis is None else {
                          "effects_sessions": [str(target["directory"]) for target in targets],
                          **{key: synthesis.get(key) for key in ("programs", "contexts", "resolves",
                                                                  "new_modules", "uber_programs")}},
                      "recipes": report.get("recipes"), "shaders": shaders.name, "pipelines": manifest.name,
                      "report": report_path.name, "seconds": round(seconds, 1)})
    pack = output / "expanded-union.o3ps"
    command = [tool["aot_compiler"]]
    for inventory in inventories:
        command += ["--inventory", str(inventory)]
    command += ["--pack", str(pack), "--manifest", str(output / "expanded-compile.json"),
                "--merged-inventory", str(output / "expanded-union-inventory.json")]
    # A module is reused only for the same source, stage, compiler and options.
    if compile_cache is not None:
        command += ["--compile-cache", str(compile_cache)]
    if jobs is not None:
        command += ["--jobs", str(jobs)]
    seconds, _ = _run(command, output / "compile.log")
    compiled = json.loads((output / "expanded-compile.json").read_text(encoding="utf-8"))
    # Gate 2, closure: what the replay produced and every recipe needs is in the pack.
    _, modules = decode(pack.read_bytes())
    closure = closure_failures(
        modules,
        [json.loads((output / "variant-shaders.json").read_text(encoding="utf-8"))] if sessions else [],
        [json.loads(path.read_text(encoding="utf-8")) for path in pipelines])
    if any(closure.values()):
        raise RuntimeError(f"compiled pack does not close over its inputs: {closure}")
    qualification = None
    if sessions:
        qualification = output / "qualification.json"
        qualification.write_text(json.dumps(qualification_record(
            generator=inputs_record(tools, GENERATOR_TARGETS, root), policies=policies,
            pack_sha256=_sha256(pack), manifest_sha256=[_sha256(path) for path in pipelines],
            modules=compiled["shader_count"], requests=requests), indent=2) + "\n", encoding="utf-8")
    receipt = {
        "format": "triaevum_shader_corpus_regeneration_v2",
        "steps": steps,
        "compile_seconds": round(seconds, 1),
        "compilation": compiled.get("compilation"),
        "modules": compiled["shader_count"],
        "descriptor_schema_version": compiled["descriptor_schema_version"],
        "pack": pack.name,
        "pack_sha256": _sha256(pack),
        "pipeline_manifests": [path.name for path in pipelines],
        "pipeline_counts": [json.loads(path.read_text(encoding="utf-8"))["pipeline_count"] for path in pipelines],
        "closure": closure,
        # Absent without recorded sessions: such a corpus is not releasable.
        "qualification": qualification.name if qualification else None,
    }
    (output / "regeneration.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return receipt


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tools", type=Path, required=True, help="Build directory holding the three tools")
    parser.add_argument("--capture-corpus", type=Path, action="append", default=[],
                        help="oot3d_pica_capture_corpus_v1 manifest (repeatable)")
    parser.add_argument("--capture-inventory", type=Path, nargs=2, action="append", default=[],
                        metavar=("INVENTORY", "PIPELINES"),
                        help="A capture import written by an earlier regeneration (captures-N-inventory.json "
                             "and captures-N-pipelines.json), reused when its captures are no longer at hand "
                             "(repeatable)")
    parser.add_argument("--citra-cache", type=Path, action="append", default=[],
                        help="Citra legacy transferable cache .bin (repeatable)")
    parser.add_argument("--variant-records", type=Path, action="append", default=[],
                        help="Directory of recorded game sessions (raw-draws.jsonl, variant-requests.jsonl, "
                             "session-pipelines.json, runtime-inputs.json), searched recursively (repeatable)")
    parser.add_argument("--synthesis-effects", type=Path, action="append", default=[],
                        help="Directory of recorded sessions of the shipped profile: the Citra caches' programs "
                             "are synthesized under the effects they all recorded (repeatable)")
    parser.add_argument("--output", type=Path, required=True, help="New private output directory")
    parser.add_argument("--compile-cache", type=Path,
                        help="Publisher SPIR-V cache shared between regenerations (private)")
    parser.add_argument("--jobs", type=int, help="Parallel compiler workers (default: up to 8)")
    args = parser.parse_args()
    if not args.capture_corpus and not args.capture_inventory and not args.citra_cache:
        parser.error("at least one capture corpus, capture inventory or Citra cache is required")
    print(json.dumps(regenerate(args.tools, args.capture_corpus, args.citra_cache, args.output,
                                args.variant_records, compile_cache=args.compile_cache, jobs=args.jobs,
                                synthesis_effects=args.synthesis_effects,
                                capture_imports=[tuple(pair) for pair in args.capture_inventory]),
                     indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
