# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Python side of the end-to-end install comparison.

Runs forge_gui.install_private_title as the frozen Forge of PACKAGE would:
the installation root is the package folder, the distribution files
(config/topscreen_ui.example.json) come from the source tree.

Usage: install_e2e.py REPO PACKAGE ROM DATA_ROOT RESULT_JSON
"""

import json
import sys
from pathlib import Path


def main(repo, package, rom, data_root, result_path):
    sys.frozen = True
    sys.executable = str(Path(package) / "TriAevumForge.exe")
    sys.path.insert(0, repo)
    from tools.triaevum_release.forge_gui import InstallRequest, install_private_title

    result = install_private_title(
        InstallRequest(Path(rom), Path(data_root)),
        report=lambda stage, message: print(f"[{stage}] {message}", file=sys.stderr, flush=True))
    Path(result_path).write_text(json.dumps(result, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main(*sys.argv[1:6])
