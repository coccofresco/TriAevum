"""Isolated GUI installation worker with a line-oriented progress protocol."""

import argparse
import json
from pathlib import Path

from .worker_job import bind_worker_lifetime, watch_owner


def main(arguments) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--data-root", type=Path, required=True)
    parser.add_argument("--owner-pid", type=int)
    args = parser.parse_args(arguments)
    def emit(event, payload):
        print(json.dumps({"forge_worker_event": event, "payload": payload}), flush=True)
    try:
        bind_worker_lifetime()
        if args.owner_pid is not None:
            watch_owner(args.owner_pid)
        from .forge_gui import InstallRequest, install_private_title
        result = install_private_title(InstallRequest(args.rom, args.data_root),
            report=lambda stage, message: emit("stage", [stage, message]))
        emit("complete", result)
        return 0
    except Exception as error:
        emit("error", str(error))
        return 1
