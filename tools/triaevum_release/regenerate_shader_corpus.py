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

The output is checked before it is accepted: every replayed module and every
recipe's module must be in the compiled pack (closure), and qualification.json
records the generator inputs of the tools used and the runtime inputs of each
session, which the publisher and the release audit check against the sources
being released (shader_corpus_qualification.py).

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
               compile_cache: Path | None = None, jobs: int | None = None) -> dict:
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
        command += ["--shader-inventory", str(shaders), "--pipeline-manifest", str(manifest),
                    "--report", str(report_path), "--require-witness"]
        # Exit 3: a request or recipe could not be rebuilt; exit 4: a rebuilt
        # output differs from the one the game produced. Both stop here.
        seconds, _ = _run(command, output / "variant-replay.log")
        report = json.loads(report_path.read_text(encoding="utf-8"))
        requests = sum(item["requests"] for item in report["sessions"])
        inventories.append(shaders)
        pipelines.append(manifest)
        steps.append({"variant_sessions": [{"directory": str(session["directory"]),
                                            **{f"{name}_sha256": _sha256(session[name])
                                               for name in ("raw_draws", "variant_requests", "pipelines",
                                                            "policy_inputs")}}
                                           for session in sessions],
                      "requests": requests,
                      "witness_mismatches": report["witness_mismatches"],
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
    parser.add_argument("--citra-cache", type=Path, action="append", default=[],
                        help="Citra legacy transferable cache .bin (repeatable)")
    parser.add_argument("--variant-records", type=Path, action="append", default=[],
                        help="Directory of recorded game sessions (raw-draws.jsonl, variant-requests.jsonl, "
                             "session-pipelines.json, runtime-inputs.json), searched recursively (repeatable)")
    parser.add_argument("--output", type=Path, required=True, help="New private output directory")
    parser.add_argument("--compile-cache", type=Path,
                        help="Publisher SPIR-V cache shared between regenerations (private)")
    parser.add_argument("--jobs", type=int, help="Parallel compiler workers (default: up to 8)")
    args = parser.parse_args()
    if not args.capture_corpus and not args.citra_cache:
        parser.error("at least one capture corpus or Citra cache is required")
    print(json.dumps(regenerate(args.tools, args.capture_corpus, args.citra_cache, args.output,
                                args.variant_records, compile_cache=args.compile_cache, jobs=args.jobs),
                     indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
