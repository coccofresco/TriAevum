"""Recover completed scenario captures into a relocatable shader-only corpus.

This is a development tool, not a Forge installation job. Source dumps and saves
remain read-only. Resource events and draw snapshots retain their native order.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import tempfile


KEEP = {"capture_begin", "capture_end", "shader_seed_program", "shader_seed_luts", "draw_begin"}
MAX_LINE = 16 * 1024 * 1024


def compact_frame(source: Path, destination: Path) -> dict:
    digest = hashlib.sha256()
    counts = {"draws": 0, "seeded_draws": 0, "program_payloads": 0, "lut_snapshots": 0}
    began = ended = False
    with source.open("rb") as stream, destination.open("wb") as output:
        while line := stream.readline(MAX_LINE + 1):
            if len(line) > MAX_LINE:
                raise ValueError("capture record exceeds size limit")
            digest.update(line)
            event = json.loads(line)
            kind = event.get("event")
            if ended or (not began and kind != "capture_begin"):
                raise ValueError("invalid capture boundaries")
            if kind == "capture_begin":
                if began:
                    raise ValueError("nested capture")
                began = True
            elif kind == "capture_end":
                ended = True
            elif kind == "draw_begin":
                counts["draws"] += 1
                counts["seeded_draws"] += bool(event.get("shader_seed_resources", False))
            elif kind == "shader_seed_program":
                counts["program_payloads"] += 1
            elif kind == "shader_seed_luts":
                counts["lut_snapshots"] += 1
            if kind in KEEP:
                output.write(json.dumps(event, separators=(",", ":")).encode() + b"\n")
    if not began or not ended:
        raise ValueError("incomplete capture")
    if counts["draws"] == 0:
        raise ValueError("capture has no draws")
    return {"source_sha256": digest.hexdigest(), "source_bytes": source.stat().st_size,
            "compact_bytes": destination.stat().st_size, **counts}


def collect_summaries(matrices: list[Path], summaries: list[Path]) -> tuple[list[Path], list[dict]]:
    selected = list(summaries)
    excluded = []
    for path in matrices:
        matrix = json.loads(path.read_text(encoding="utf-8-sig"))
        if matrix.get("format") != "oot3d_azahar_coverage_matrix_v1":
            raise ValueError(f"unsupported scenario matrix: {path}")
        for result in matrix["results"]:
            if result["status"] == "passed":
                selected.append(Path(result["coverage_summary_path"]))
            else:
                excluded.append({"matrix": str(path), "scenario": result["scenario_id"],
                                 "status": result["status"]})
    return sorted(set(path.resolve() for path in selected)), excluded


def recover(matrices: list[Path], summaries: list[Path], root: Path) -> dict:
    selected, excluded = collect_summaries(matrices, summaries)
    root.mkdir(parents=True, exist_ok=True)
    frames_dir = root / "frames"
    frames_dir.mkdir(exist_ok=True)
    frames, errors, skipped_frames = [], [], []
    identities = set()
    seen_paths = set()
    for summary_path in selected:
        summary = json.loads(summary_path.read_text(encoding="utf-8-sig"))
        status = summary.get("scenario_status", {})
        if status.get("status") != "ready":
            errors.append({"summary": str(summary_path), "error": "scenario was not ready"})
            continue
        scenario = status["scenario_id"]
        if not summary["pica_frames"]:
            excluded.append({"summary": str(summary_path), "scenario": scenario,
                             "status": "no_retained_frames"})
            continue
        for frame_path in summary["pica_frames"]:
            source = Path(frame_path).resolve()
            if source in seen_paths:
                continue
            seen_paths.add(source)
            if source.is_relative_to(root.resolve()):
                raise ValueError("source capture must not be inside output corpus")
            with tempfile.TemporaryDirectory(prefix=".recover-", dir=root) as temporary:
                compact = Path(temporary) / "frame.jsonl"
                try:
                    info = compact_frame(source, compact)
                except (ValueError, OSError) as error:
                    destination = skipped_frames if str(error) == "capture has no draws" else errors
                    destination.append({"scenario": scenario, "frame": str(source), "error": str(error)})
                    continue
                identity = info["source_sha256"]
                if identity in identities:
                    continue
                identities.add(identity)
                target = frames_dir / f"{identity}.jsonl"
                with compact.open("rb") as compact_stream:
                    compact_hash = hashlib.file_digest(compact_stream, "sha256").hexdigest()
                if target.exists():
                    with target.open("rb") as existing:
                        if hashlib.file_digest(existing, "sha256").hexdigest() != compact_hash:
                            raise ValueError("existing compact capture does not match its source")
                else:
                    compact.replace(target)
                frames.append({"path": target.relative_to(root).as_posix(), "scenario": scenario,
                               "source": str(source), "summary": str(summary_path),
                               "sha256": compact_hash, **info})
    report = {
        "format": "oot3d_pica_capture_corpus_v1", "frames": frames,
        "excluded_scenarios": excluded, "errors": errors, "skipped_frames": skipped_frames,
        "counts": {"frames": len(frames), "scenarios": len({f["scenario"] for f in frames}),
                   **{key: sum(f[key] for f in frames) for key in
                      ("draws", "seeded_draws", "program_payloads", "lut_snapshots",
                       "source_bytes", "compact_bytes")}},
        "policy": "native_capture_shader_preparation_only_not_gameplay_replay",
    }
    manifest = root / "corpus.json"
    with tempfile.TemporaryDirectory(prefix=".manifest-", dir=root) as temporary:
        candidate = Path(temporary) / "corpus.json"
        candidate.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        candidate.replace(manifest)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix", type=Path, action="append", default=[])
    parser.add_argument("--summary", type=Path, action="append", default=[])
    parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args()
    if not args.matrix and not args.summary:
        parser.error("at least one matrix or capture summary is required")
    report = recover(args.matrix, args.summary, args.output_root)
    print(json.dumps(report["counts"]))
    return 1 if report["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
