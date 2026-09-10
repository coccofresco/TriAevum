"""Private, bounded Forge -> GPU preparation -> game -> cache-reuse check.

Developer qualification only. Inputs and resulting game-derived artifacts must
stay outside public packages. Calls the real Forge services and renderer probe;
does not boot a game to prepare shaders or rebuild title code.
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

from common import atomic_write_json, load_json_object, sha256_file
from device_pipeline_preparation import (
    FORMAT as DEVICE_FORMAT, installation_cache_directory, prepare_device_pipelines,
)
from shader_preparation import FORMAT as SHADER_FORMAT, prepare_shader_seed


def shader_statistics(log: str) -> dict:
    def record(prefix):
        matches = re.findall(r"^" + prefix + r" (.+)$", log, re.MULTILINE)
        if len(matches) != 1:
            raise ValueError(f"Expected one {prefix} diagnostic, found {len(matches)}")
        return dict(re.findall(r"(\w+)=([^ ]+)", matches[0]))
    cache = record("TRIAEVUM_SPIRV_CACHE")
    pack = record("OOT3D_PICA_AOT_SHADER_RESOLUTION")
    audit = record("TRIAEVUM_SHADERC_AUDIT") if "TRIAEVUM_SHADERC_AUDIT " in log else None
    if int(cache["compile_failed"]) or int(cache["write_failed"]) or pack["strict"] != "0":
        raise ValueError("Shader fallback/cache failed or strict diagnostic mode is active")
    return {"pack_hits": int(pack["hits"]), "pack_misses": int(pack["misses"]),
            "compiled": int(cache["compiled"]), "cache_hits": int(cache["hits"]),
            "cache_writes": int(cache["writes"]), "compile_ms": float(cache["compile_ms"]),
            "all_pass_compiled": int(audit["calls"]) if audit else None,
            "all_pass_compile_ms": float(audit["compile_ms"]) if audit else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("output", "compiler", "helper", "installation", "runtime"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--inventory", type=Path, action="append", required=True)
    parser.add_argument("--manifest", type=Path, action="append", required=True)
    parser.add_argument("--dependency", type=Path, action="append", default=[])
    parser.add_argument("--schema", type=int, default=3)
    parser.add_argument("--profile", default="TriAevum.launch.json")
    parser.add_argument("--frames", type=int, default=900)
    parser.add_argument("--seconds", type=int, default=90)
    parser.add_argument("--native-fidelity", action="store_true")
    parser.add_argument("--require-pica-cold-zero", action="store_true",
                        help="Fail on PICA/scanout pack misses or cache compiler calls (not an all-pass audit)")
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    package = root / "preparation-inputs"
    package.mkdir()
    def artifact(source):
        source = source.resolve(strict=True)
        # Preserve dependency names for the native loader (notably Windows DLLs).
        name = source.name
        target = package / name
        if target.exists() and sha256_file(target) != sha256_file(source):
            raise ValueError(f"Conflicting preparation input filename: {name}")
        shutil.copy2(source, target)
        return {"path": name, "bytes": target.stat().st_size, "sha256": sha256_file(target)}
    title = {"recipe": "private-shader-handoff", "shader_preparation": {
        "format": SHADER_FORMAT, "mode": "source_inventories",
        "descriptor_schema_version": args.schema, "compiler": artifact(args.compiler),
        "inventories": [artifact(path) for path in args.inventory],
        "dependencies": [artifact(path) for path in args.dependency]},
        "device_pipeline_preparation": {"format": DEVICE_FORMAT,
            "helper": artifact(args.helper), "manifests": [artifact(path) for path in args.manifest]}}
    atomic_write_json(root / "title-record.json", title)
    data = root / "data"
    report = lambda stage, text: print(f"{stage}: {text}", flush=True)
    pack = prepare_shader_seed(root=package, data_root=data, title=title, report=report)
    stamp = pack.stat().st_mtime_ns
    if prepare_shader_seed(root=package, data_root=data, title=title, report=report) != pack or pack.stat().st_mtime_ns != stamp:
        raise ValueError("Unchanged Forge preparation did not reuse the portable pack")
    cache = installation_cache_directory(data)
    device = prepare_device_pipelines(root=package, data_root=data, title=title,
                                     pack=pack, cache_directory=cache, report=report)
    if device["device_pipeline_prewarm"] != "complete":
        raise ValueError(f"Preparation failed: {device}")
    runs = []
    for name in ("first-launch", "second-launch"):
        output = root / name
        command = [sys.executable, str(Path(__file__).with_name("probe_renderer.py")),
            str(args.installation.resolve()), str(args.runtime.resolve()), str(output),
            "--profile", args.profile, "--shader-pack", str(pack), "--cache-directory", str(cache),
            "--frames", str(args.frames), "--seconds", str(args.seconds), "--capture-interval", "150"]
        if args.native_fidelity:
            command.append("--native-fidelity")
        subprocess.run(command, check=True, timeout=args.seconds + 45)
        stats = shader_statistics((output / "launch.log").read_text(errors="replace"))
        pipeline = load_json_object(output / "gpu.json")["nri_pipeline_compilation"]
        if pipeline["initial_cache_bytes"] <= 0:
            raise ValueError("Game did not accept the prepared NRI driver cache")
        if not runs and pipeline["initial_cache_bytes"] != device["cache_bytes"]:
            raise ValueError("Game did not load the driver cache just prepared by Forge")
        captures = {path.name: sha256_file(path) for path in output.glob("framebuffer_*.bmp")}
        if not captures or stats["pack_hits"] <= 0:
            raise ValueError("Game did not produce captures and use the Forge shader pack")
        if args.require_pica_cold_zero and (stats["compiled"] or stats["pack_misses"]):
            raise ValueError(f"Complete Forge seed left runtime shader work: {stats}")
        runs.append({"name": name, "shaders": stats, "nri_pipelines": pipeline, "captures": captures})
    identical = runs[0]["captures"] == runs[1]["captures"]
    if args.native_fidelity and not identical:
        raise ValueError("Deterministic native captures differ across launches")
    if runs[1]["shaders"]["compiled"] != 0:
        raise ValueError("Second launch recompiled shaders instead of reusing the local cache")
    result = {"format": "triaevum_forge_shader_handoff_qualification_v1",
              "shader_preparation": load_json_object(pack.parent / "preparation.json"),
              "device_preparation": device, "runs": runs,
              "captures_identical": identical, "fps_benchmark": False,
              "compiler_measurement_scope": "pica_and_legacy_scanout_cache_not_all_renderer_passes",
              "full_game_coverage": False, "title_recompiled": False}
    atomic_write_json(root / "qualification.json", result)
    print(json.dumps({"device_pipelines": device["prepared"], "runs": [r["shaders"] for r in runs],
                      "captures_identical": identical}), flush=True)


if __name__ == "__main__":
    main()
