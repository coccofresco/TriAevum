# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs ctr_rom/extracted_inputs jobs with the Python Forge and records the results.

Usage: ctr_rom_jobs.py RELEASE_DIR JOBS_JSON RESULTS_JSON
"""

import json
import sys
from pathlib import Path


def describe(extracted):
    return {
        "container_kind": extracted.container_kind,
        "partition_index": extracted.partition_index,
        "program_id": extracted.program_id,
        "files": {
            kind: {"path": str(item.path), "bytes": item.bytes, "sha256": item.sha256}
            for kind, item in extracted.by_kind().items()
        },
    }


def failure(step, exc):
    return {"ok": False, "step": step, "error_type": type(exc).__name__, "error": str(exc)}


def run(job, ctr_rom, extracted_inputs, test_ctr_rom):
    op = job["op"]
    if op == "build_rom":
        test_ctr_rom._build_rom(Path(job["path"]), declares_decrypted=job["declares_decrypted"])
        return {"ok": True}
    if op == "write":
        path = Path(job["path"])
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(bytes.fromhex(job["hex"]))
        return {"ok": True}
    if op == "blz":
        try:
            output = ctr_rom.decompress_exefs_code(Path(job["input"]).read_bytes())
        except Exception as exc:
            return failure("blz", exc)
        Path(job["output"]).write_bytes(output)
        return {"ok": True}
    try:
        if op == "extract":
            extracted = ctr_rom.extract_decrypted_rom(
                Path(job["rom"]), Path(job["output"]), require_rom_suffix=job["require_suffix"])
        else:
            extracted = extracted_inputs.stage_directory(Path(job["source"]), Path(job["output"]))
    except Exception as exc:
        return failure(op, exc)
    result = {"ok": True, "extracted": describe(extracted), "identity": ctr_rom._input_identity(extracted)}
    if job.get("publish"):
        try:
            result["published"] = describe(ctr_rom.publish_extracted_inputs(extracted, Path(job["publish"])))
        except Exception as exc:
            return {**failure("publish", exc), "extracted": result["extracted"], "identity": result["identity"]}
    return result


def main(release_dir, jobs_path, results_path):
    sys.path.insert(0, release_dir)
    import ctr_rom
    import extracted_inputs
    import test_ctr_rom

    jobs = json.loads(Path(jobs_path).read_text(encoding="utf-8"))
    results = [run(job, ctr_rom, extracted_inputs, test_ctr_rom) for job in jobs]
    Path(results_path).write_text(json.dumps(results, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main(*sys.argv[1:])
