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
they come from recorded game sessions: a directory given with
--runtime-inventory holds *-shaders.json (OOT3D_PICA_EFFECTIVE_SHADER_INVENTORY)
and *-pipelines.json (OOT3D_PICA_PIPELINE_INVENTORY) written by sessions that
played with that profile. Their programs join the union and their recipes merge
into runtime-pipelines.json, a manifest for device preparation.

Inputs, packs and manifests are game-derived and private; nothing here is
published. Build the tools from the same source as the runtime:
oot3d_native_pica_capture_inventory, oot3d_native_pica_cache_inventory,
oot3d_native_pica_aot_compiler and oot3d_native_pica_pipeline_manifest.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import time
from pathlib import Path


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


def _runtime_inventories(directories: list[Path]) -> tuple[list[Path], list[Path]]:
    shaders: list[Path] = []
    pipelines: list[Path] = []
    for directory in directories:
        found_shaders = sorted(directory.glob("*-shaders.json"))
        found_pipelines = sorted(directory.glob("*-pipelines.json"))
        if not found_shaders or not found_pipelines:
            raise RuntimeError(f"{directory}: no *-shaders.json and *-pipelines.json session inventories")
        shaders += found_shaders
        pipelines += found_pipelines
    for path in shaders:
        if json.loads(path.read_text(encoding="utf-8")).get("format") != "oot3d_pica_effective_shader_inventory_v1":
            raise RuntimeError(f"{path}: not an effective shader inventory")
    return shaders, pipelines


def regenerate(tools: Path, captures: list[Path], caches: list[Path], output: Path,
               sessions: list[Path] | None = None) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if (tools / "oot3d_native_pica_aot_compiler.exe").exists() else ""
    tool = {name: str(tools / f"oot3d_native_pica_{name}{suffix}")
            for name in ("capture_inventory", "cache_inventory", "aot_compiler", "pipeline_manifest")}
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
    session_shaders, session_pipelines = _runtime_inventories(sessions or [])
    if session_pipelines:
        manifest = output / "runtime-pipelines.json"
        command = [tool["pipeline_manifest"]]
        for path in session_pipelines:
            command += ["--inventory", str(path)]
        seconds, _ = _run(command + ["--output", str(manifest)], output / "runtime-pipelines.log")
        inventories += session_shaders
        pipelines.append(manifest)
        steps.append({"runtime_sessions": [str(path) for path in sessions or []],
                      "shader_inventories": len(session_shaders), "pipeline_inventories": len(session_pipelines),
                      "pipelines": manifest.name, "seconds": round(seconds, 1)})
    pack = output / "expanded-union.o3ps"
    command = [tool["aot_compiler"]]
    for inventory in inventories:
        command += ["--inventory", str(inventory)]
    command += ["--pack", str(pack), "--manifest", str(output / "expanded-compile.json"),
                "--merged-inventory", str(output / "expanded-union-inventory.json")]
    seconds, _ = _run(command, output / "compile.log")
    compiled = json.loads((output / "expanded-compile.json").read_text(encoding="utf-8"))
    receipt = {
        "format": "triaevum_shader_corpus_regeneration_v1",
        "steps": steps,
        "compile_seconds": round(seconds, 1),
        "modules": compiled["shader_count"],
        "descriptor_schema_version": compiled["descriptor_schema_version"],
        "pack": pack.name,
        "pack_sha256": _sha256(pack),
        "pipeline_manifests": [path.name for path in pipelines],
        "pipeline_counts": [json.loads(path.read_text(encoding="utf-8"))["pipeline_count"] for path in pipelines],
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
    parser.add_argument("--runtime-inventory", type=Path, action="append", default=[],
                        help="Directory of recorded game sessions' *-shaders.json and *-pipelines.json (repeatable)")
    parser.add_argument("--output", type=Path, required=True, help="New private output directory")
    args = parser.parse_args()
    if not args.capture_corpus and not args.citra_cache:
        parser.error("at least one capture corpus or Citra cache is required")
    print(json.dumps(regenerate(args.tools, args.capture_corpus, args.citra_cache, args.output,
                                args.runtime_inventory), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
