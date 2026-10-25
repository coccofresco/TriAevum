# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Python side of the lock and activation-journal interoperability test."""

import os
import sys
from pathlib import Path

sys.path.insert(0, sys.argv[1])
from activation_transaction import activation_transaction  # noqa: E402
from cache_lock import acquire_cache_lock  # noqa: E402


def main(mode, *arguments):
    if mode == "hold":
        acquire_cache_lock(Path(arguments[0]))
        print("held", flush=True)
        sys.stdin.read()
    elif mode == "try":
        try:
            acquire_cache_lock(Path(arguments[0]))
            print("acquired")
        except OSError:
            print("busy")
    elif mode == "crash":
        installation, *targets = (Path(argument) for argument in arguments)
        with activation_transaction(installation, targets):
            for target in targets:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b"half-written by python")
            os._exit(3)
    elif mode == "commit":
        installation, *targets = (Path(argument) for argument in arguments)
        with activation_transaction(installation, targets):
            pass
        print("committed")


if __name__ == "__main__":
    main(*sys.argv[2:])
