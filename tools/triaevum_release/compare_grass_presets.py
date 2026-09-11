"""Bounded, private Grass preset comparisons; captures never contribute to timings."""
import argparse
import copy
import json
from pathlib import Path
import statistics
import subprocess
import sys


def gpu_statistics(document, warmup, pending=3):
    frames = document["frames"][warmup:len(document["frames"]) - pending]
    if not frames or any(f.get("presentation_vsync", True) for f in frames):
        raise ValueError("missing measured frames or renderer VSync still enabled")
    result = {}
    for name in ("frame_ms", "grass_ms", "native_pica_ms"):
        values = [f["gpu"][name] for f in frames if f["gpu"].get(name) is not None]
        if not values:
            raise ValueError(f"no GPU timestamps for {name}")
        result[name] = statistics.mean(values)
    if result["grass_ms"] < 0.01:
        raise ValueError("Grass GPU work absent; reject disabled/failed effect as a speedup")
    return result


def pixel_statistics(reference, candidate):
    import numpy as np
    from PIL import Image
    result = []
    for source in sorted(reference.glob("framebuffer_*.bmp")):
        target = candidate / source.name
        a = np.asarray(Image.open(source).convert("RGB"), dtype=np.int16)
        b = np.asarray(Image.open(target).convert("RGB"), dtype=np.int16)
        if a.shape != b.shape:
            raise ValueError("capture dimensions differ")
        delta = np.abs(a - b)
        # Include a coverage-sensitive view as well as the full image, whose
        # unchanged sky/HUD would otherwise dilute the reported difference.
        lower = delta[a.shape[0] // 3:, :, :]
        result.append({"frame": source.name, "mae": float(delta.mean()),
                       "lower_mae": float(lower.mean()), "max": int(delta.max()),
                       "changed_fraction": float((delta.max(axis=2) > 0).mean())})
    if not result:
        raise ValueError("no reference framebuffers")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    plan = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    (root / "plan.json").write_text(json.dumps(plan, indent=2))
    profile = json.loads(Path(plan["profile"]).read_text(encoding="utf-8-sig"))
    config_path = Path(profile["arguments"][profile["arguments"].index("--config") + 1])
    original = json.loads(config_path.read_text(encoding="utf-8-sig"))
    probe = Path(__file__).with_name("probe_renderer.py")
    results = []
    frames = plan.get("frames", 900)
    warmup = plan.get("warmup", 240)
    for variant in plan["variants"]:
        name = variant["name"]
        if not name.replace("-", "").replace("_", "").isalnum():
            raise ValueError("variant name must be a simple directory name")
        case = root / name
        case.mkdir()
        config = copy.deepcopy(original)
        config["Graphics"]["Grass"]["Performance"].update(variant.get("performance", {}))
        config["Graphics"]["GrassSavedPreset"] = copy.deepcopy(config["Graphics"]["Grass"])
        (case / "config.json").write_text(json.dumps(config, indent=2))
        launch = copy.deepcopy(profile)
        launch["arguments"][launch["arguments"].index("--config") + 1] = str(case / "config.json")
        (case / "launch.json").write_text(json.dumps(launch, indent=2))
        base = [sys.executable, str(probe), plan["installation"], plan["executable"]]
        common = ["--profile", str(case / "launch.json"), "--seconds", "120",
                  "--cache-directory", plan["cache"],
                  "--shader-pack", plan["shader_pack"]]
        if plan.get("checkpoint"):
            common += ["--load-state", plan["checkpoint"]]
        subprocess.run(base + [str(case / "timing")] + common + ["--throughput", "--no-captures",
            "--frames", str(frames), "--warmup-frames", str(warmup)], check=True, timeout=160)
        runtime = json.loads((case / "timing/runtime.json").read_text())
        window = runtime["benchmark_window"]
        if window["measured_frames"] != frames - warmup or window["pacing_enabled"] or window["vsync"]:
            raise ValueError("incomplete or paced throughput measurement")
        gpu = json.loads((case / "timing/gpu.json").read_text())
        stats = gpu_statistics(gpu, warmup)
        if plan.get("windows"):
            selected = []
            stats["windows"] = []
            for start, end in plan["windows"]:
                selected += gpu["frames"][start:end]
                if not warmup <= start < end <= frames - 3:
                    raise ValueError("measurement window includes warmup or unavailable frames")
                window_stats = gpu_statistics({"frames": gpu["frames"][start:end]}, 0, 0)
                stats["windows"].append({"start": start, "end": end, **window_stats})
            stats["selected"] = gpu_statistics({"frames": selected}, 0, 0)
        stats["host_frame_ms"] = 1000.0 / window["frames_per_second"]
        subprocess.run(base + [str(case / "capture")] + common + ["--frames", str(plan.get("capture_frames", 180)),
            "--capture-interval", str(plan.get("capture_interval", 30))], check=True, timeout=160)
        reference = Path(plan["reference_capture"]) if "reference_capture" in plan else root / plan["variants"][0]["name"] / "capture"
        pixels = pixel_statistics(reference, case / "capture")
        grass_samples = []
        for sample in sorted((case / "capture").glob("framebuffer_*.bmp.json")):
            metadata = json.loads(sample.read_text())
            if "grass" in metadata:
                grass_samples.append({"host_frame": metadata["host_frame"], **metadata["grass"]})
        if grass_samples and not any(s.get("status") == "drawing" and s.get("visible_blades", 0) for s in grass_samples):
            raise ValueError("captures do not contain a successfully drawn Grass effect")
        if variant.get("performance", {}).get("MidrangeClustersEnabled") and not any(
                s.get("status") == "drawing" and s.get("cluster_instances", 0) for s in grass_samples):
            raise ValueError("cluster test did not draw any cluster instances")
        result = {"name": name, "performance": variant.get("performance", {}),
                  "timing": stats, "pixels": pixels, "grass_samples": grass_samples}
        results.append(result)
        (root / "results.json").write_text(json.dumps(results, indent=2))
        print(json.dumps(result), flush=True)


if __name__ == "__main__":
    main()
