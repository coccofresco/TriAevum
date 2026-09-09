"""Single application entry: prepare a title on first run, otherwise play it."""

from collections.abc import Sequence
import sys

from . import forge_gui


def main(argv: Sequence[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    if arguments not in ([], ["--forge"]):
        print("Usage: triaevum [--forge]", file=sys.stderr)
        return 2
    if arguments == ["--forge"] or forge_gui.load_active_title() is None:
        return forge_gui.main()
    try:
        return forge_gui.launch_runtime().wait()
    except (OSError, ValueError, forge_gui.forge.ForgeError) as exc:
        # Show the failure and keep import/repair accessible after an update.
        return forge_gui.main(startup_error=str(exc))
