"""Local-only, single-attempt CATALOGS GPU acceptance via the real Forge host.

The coordinator approved this lane-local helper launcher. It owns the same
mkdir/owner-token lock as W/tools/run_safe.sh; never nest the two launchers.
This is NOT a supported title recipe, installer, or whole-game qualification.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import signal
import sys
import time
import uuid

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools/triaevum_release"))
from common import atomic_write_json, sha256_file
from device_pipeline_preparation import FORMAT, prepare_device_pipelines

GPU_LOCK = Path("I:/_triaevum_work/gpu-run.lock")
DISK_FLOOR = 8 * 1024**3


@contextmanager
def owned_gpu_lock(path: Path, *, wait_seconds: float, poll_seconds: float = 2):
    """No stale-lock reclamation. Release only the token this invocation wrote."""
    token = f"catalogs_headless {os.getpid()} {uuid.uuid4().hex}"
    deadline = time.monotonic() + wait_seconds
    acquired = False
    try:
        while True:
            try:
                path.mkdir()
            except FileExistsError:
                if time.monotonic() >= deadline:
                    raise TimeoutError(f"GPU lock busy (left untouched): {path}")
                time.sleep(poll_seconds)
                continue
            acquired = True
            (path / "owner").write_text(token, encoding="utf-8")
            break
        yield token
    finally:
        if acquired:
            try:
                owner = path / "owner"
                if owner.read_text(encoding="utf-8") == token:
                    shutil.rmtree(path)
                    if path.exists() and (path / "owner").read_text(encoding="utf-8") == token:
                        raise RuntimeError(f"GPU lock release failed: {path}")
            except FileNotFoundError:
                pass


def artifact(root: Path, source: Path, name: str) -> dict:
    target = root / name
    shutil.copyfile(source, target)
    return {"path": name, "bytes": target.stat().st_size, "sha256": sha256_file(target)}


def verify_inputs(receipt: Path) -> str:
    """Refuse stale runtime source inputs before spending an acceptance attempt."""
    record = json.loads(receipt.read_text(encoding="utf-8"))
    files = record["files"]
    if not isinstance(files, dict) or not files:
        raise ValueError("runtime input receipt has no files")
    for relative, expected in files.items():
        path = (REPO / relative).resolve()
        if not path.is_relative_to(REPO) or sha256_file(path) != expected:
            raise ValueError(f"stale runtime input: {relative}")
    return sha256_file(receipt)


def interrupted(signum, _frame):
    # Forge's _run finally kills/reaps its own helper before our lock unwinds.
    raise KeyboardInterrupt(f"signal {signum}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--title-program-pack", type=Path, required=True)
    parser.add_argument("--runtime-inputs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="new directory; existing attempt is never overwritten")
    parser.add_argument("--lock-wait", type=int, default=600)
    args = parser.parse_args()
    if args.lock_wait < 0:
        parser.error("lock wait must be nonnegative")
    inputs_sha = verify_inputs(args.runtime_inputs)
    if shutil.disk_usage("I:/").free < DISK_FLOOR:
        raise RuntimeError("I: is below the owner 8 GiB floor")
    for source in (args.helper, args.pack, args.manifest, args.title_program_pack):
        if not source.is_file():
            raise ValueError(f"missing artifact: {source}")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    package = out / "package"
    package.mkdir()
    title = {"title_program_pack": artifact(package, args.title_program_pack, "title.spvpack"),
             "device_pipeline_preparation": {
                 "format": FORMAT,
                 "helper": artifact(package, args.helper, "prepare.exe"),
                 "manifest": artifact(package, args.manifest, "pipelines.json"),
                 "startup_programs": True}}
    pack_record = artifact(package, args.pack, "pack.o3ps")
    atomic_write_json(out / "inputs.json", {"title": title, "pack": pack_record,
        "runtime_inputs_sha256": inputs_sha, "launcher_sha256": sha256_file(Path(__file__)),
        "source_paths": {key: str(getattr(args, key).resolve()) for key in
                         ("helper", "pack", "manifest", "title_program_pack", "runtime_inputs")},
        "supported_recipe": False, "acceptance": "F05/G18 observed-subset headless preparation"})
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, interrupted)
    os.environ["TRIAEVUM_CRASH_DIALOG"] = "0"
    os.environ["TRIAEVUM_PIPELINE_PREPARE_THREADS"] = "4"
    result = None
    with owned_gpu_lock(GPU_LOCK, wait_seconds=args.lock_wait) as token:
        if shutil.disk_usage("I:/").free < DISK_FLOOR:
            raise RuntimeError("I: fell below floor while waiting; helper not launched")
        atomic_write_json(out / "ATTEMPTED.json", {"token": token, "time": time.time(), "helper_launches": 1})
        result = prepare_device_pipelines(root=package, data_root=out / "data", title=title,
            pack=package / pack_record["path"], cache_directory=out / "cache",
            report=lambda stage, message: print(f"{stage}: {message}", flush=True))
        atomic_write_json(out / "result.json", result)
    return 0 if result and result.get("device_pipeline_prewarm") == "complete" else 1


if __name__ == "__main__":
    raise SystemExit(main())
