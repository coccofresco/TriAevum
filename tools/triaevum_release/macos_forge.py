"""Thin AppKit adapter for the shared Forge ROM-installation contract."""
from __future__ import annotations

import argparse
import os
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--root", type=Path, required=True,
                        help="Read-only packaged runtime directory")
    parser.add_argument("--data", type=Path,
                        default=Path.home() / "Library/Application Support/TriAevum",
                        help="Writable per-user activation directory")
    args = parser.parse_args()
    root, data = args.root.expanduser().resolve(), args.data.expanduser().resolve()
    if not (root / "TriAevum").is_file():
        parser.error(f"Mac runtime is missing: {root / 'TriAevum'}")
    os.environ["TRIAEVUM_INSTALLATION_ROOT"] = str(root)
    os.environ["TRIAEVUM_ACTIVATION_ROOT"] = str(data)
    os.environ.setdefault("SSL_CERT_FILE", __import__("certifi").where())
    from tools.triaevum_release.forge_gui import InstallRequest, install_private_title
    try:
        install_private_title(InstallRequest(args.rom, data),
                              report=lambda stage, message: print(f"{stage}: {message}", flush=True))
    except Exception as error:
        print(f"error: {error}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
