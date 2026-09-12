"""PyInstaller entry point for the public TriAevum Forge application."""

from __future__ import annotations

import sys
from collections.abc import Sequence

from tools.triaevum_release import forge


def main(argv: Sequence[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    if arguments and arguments[0] == "--play":
        from tools.triaevum_release.desktop_launcher import main as launcher_main
        return launcher_main(arguments[1:])
    if arguments and arguments[0] == "--install-worker":
        from tools.triaevum_release.install_worker import main as worker_main
        return worker_main(arguments[1:])
    if arguments and arguments[0] == "--gui-smoke":
        from tools.triaevum_release.forge_gui_probe import main as smoke_main
        return smoke_main(arguments[1:])
    if arguments:
        return forge.main(arguments)

    from tools.triaevum_release.forge_gui import main as gui_main

    return gui_main()


if __name__ == "__main__":
    raise SystemExit(main())
