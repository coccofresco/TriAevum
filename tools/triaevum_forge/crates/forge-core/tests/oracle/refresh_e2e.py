# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Python side of the package-update (refresh) comparison.

Runs package_update.refresh_packaged_runtime on a synthetic installation the
way forge_gui.launch_runtime did before playing ("launch": under the
activation lock, refuse a pending journal and place the packaged runtime,
then refresh), or without the journal check ("refresh"), so that the
refresh's own refusal is seen. The runtime's native answers are those of
the Rust tests' FakeRuntime (tests/common/mod.rs).

Usage: refresh_e2e.py REPO MODE PACKAGE DATA_ROOT RESULT_JSON
Writes {"refreshed": bool} or {"error": message}.
"""

import json
import sys
from pathlib import Path
from types import SimpleNamespace

PRODUCT = {
    "format": "triaevum_product_info_v1", "runtime": "oot3d_native_game", "whole_aot_plugin_abi": 2,
    "capabilities": {"nri": True, "f1": True, "topscreen": True},
    "default_config": {"Graphics": {"Preset": "Enhanced", "FrameRate": {"Mode": "Interpolated2x"}}},
}


def shell_bytes():
    out = b"RSSH" + (3).to_bytes(4, "little") + (1).to_bytes(8, "little")
    for value in (64, 512, 0, 0, 1, 0):
        out += value.to_bytes(4, "little")
    return out + (0).to_bytes(8, "little")


def fake_run_native(command, *args, **kwargs):
    command = [str(part) for part in command]
    if "--product-info" in command or "--verify-title-plugin" in command:
        product = dict(PRODUCT)
        if "--verify-title-plugin" in command:
            product["private_title_loaded"] = True
        stdout = "startup noise\n" + json.dumps(product, separators=(",", ":")) + "\n"
        return SimpleNamespace(returncode=0, stdout=stdout, stderr="")
    if "--prebuild-interior-sun" in command:
        cache = Path(command[command.index("--renderer-cache-directory") + 1])
        manifest = Path(command[command.index("--manifest") + 1])
        relative = "shadow-shells/0000000000000001.bin"
        (cache / relative).parent.mkdir(parents=True, exist_ok=True)
        (cache / relative).write_bytes(shell_bytes())
        result = {"format": "triaevum_interior_sun_v1", "criterion_version": 3, "game_booted": False,
                  "cache_directory": str(cache), "rooms": 1, "fallback": 0,
                  "entries": [{"status": "built", "fingerprint": "0000000000000001", "interior": True,
                               "path": relative}]}
        manifest.write_text(json.dumps(result, separators=(",", ":")), encoding="utf-8")
        return SimpleNamespace(returncode=0, stdout="", stderr="")
    raise AssertionError(f"unexpected native call: {command}")


def main(repo, mode, package, data_root, result_path):
    sys.path.insert(0, repo)
    from tools.triaevum_release import forge_gui, interior_sun, package_update, product_contract
    from tools.triaevum_release.activation_transaction import installation_lock, journal_path
    from tools.triaevum_release.host_layout import for_package
    from tools.triaevum_release.release_platform import host_platform, installed_runtime_path, place_installed_runtime

    for module in (product_contract, interior_sun):
        module.run_native = fake_run_native
    package, data_root = Path(package).resolve(), Path(data_root).resolve()
    try:
        layout = for_package(package)
        with installation_lock(layout.activation):
            if mode == "launch" and journal_path(layout.activation).exists():
                raise ValueError("An activation was interrupted; run Forge again before playing")
            place_installed_runtime(package, host_platform())
        active = forge_gui.load_active_title(data_root / "active-title.json")
        refreshed = package_update.refresh_packaged_runtime(
            executable=installed_runtime_path(package, host_platform()).resolve(), title=active.directory,
            data_root=data_root, recipe_id=active.recipe_id, recipes=package / "recipes/oot3d.json")
        outcome = {"refreshed": refreshed}
    except Exception as exc:  # the comparison wants the message
        outcome = {"error": str(exc)}
    Path(result_path).write_text(json.dumps(outcome), encoding="utf-8")


if __name__ == "__main__":
    main(*sys.argv[1:6])
