"""Private, bounded GUI-ROM-install/reinstall/native-boot qualification on Linux."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import time

try:
    from .common import atomic_write_json, sha256_file
    from .release_platform import LINUX, host_platform
except ImportError:
    from common import atomic_write_json, sha256_file
    from release_platform import LINUX, host_platform


def desktop_environment():
    environment = os.environ.copy()
    state = subprocess.run(["systemctl", "--user", "show-environment"],
                           capture_output=True, text=True, check=True, timeout=10)
    allowed = {"DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS"}
    for assignment in state.stdout.splitlines():
        key, separator, value = assignment.partition("=")
        if separator and key in allowed:
            environment[key] = value
    if not environment.get("DISPLAY"):
        raise ValueError("Log into a graphical Linux session before testing Forge")
    return environment


def run_logged(command, log: Path, *, cwd: Path, environment: dict, timeout: float, accepted_codes=(0,)):
    started = time.monotonic()
    with log.open("w", encoding="utf-8") as stream:
        process = subprocess.Popen(command, cwd=cwd, env=environment,
                                   stdout=stream, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=timeout)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    if code not in accepted_codes:
        raise RuntimeError(f"Process exited {code}; see {log}")
    return time.monotonic() - started


def installation_check(state: dict) -> bool:
    """Return GUI visibility; an iconified window cannot certify visual usability."""
    if state.get("installation_ready") is not True:
        raise ValueError(f"GUI installation failed: {state}")
    if state.get("status") == "passed":
        return True
    if (state.get("window_state") == "iconic" and state.get("widgets") and
            state.get("clipped_widgets") == [] and state.get("unmapped_widgets") and
            state.get("prepare_button_tracks_input") is True and
            not any(item.get("kind") == "error" for item in state.get("notifications", []))):
        return False
    raise ValueError(f"GUI qualification failed beyond visibility: {state}")


def qualify(installation: Path, rom: Path, output: Path, *, seconds: int):
    if host_platform() != LINUX:
        raise ValueError("This integration test requires native Linux")
    installation, rom, output = installation.resolve(), rom.resolve(strict=True), output.resolve()
    if (installation / "data/active-title.json").exists():
        raise ValueError("Use a fresh candidate, not an existing personal installation")
    output.mkdir(parents=True, exist_ok=False)
    environment = desktop_environment()
    report = {"status": "running", "installation": str(installation), "rom_sha256": sha256_file(rom)}
    try:
        visible = True
        for phase in ("install", "reinstall"):
            result = output / f"{phase}.json"
            elapsed = run_logged([str(installation / LINUX.forge), "--gui-smoke", "--rom", str(rom),
                                  "--timeout", "240", "--output", str(result)],
                                 output / f"{phase}.log", cwd=installation,
                                 environment=environment, timeout=260, accepted_codes=(0, 1))
            state = json.loads(result.read_text())
            visible = installation_check(state) and visible
            report[phase] = {"wall_seconds": elapsed, "gui": state}
            if phase == "install":
                sentinel = installation / "data/savedata/.forge-preservation-probe"
                sentinel.write_bytes(b"TriAevum private reinstall test\n")
                preserved = [sentinel, installation / "data/config/TriAevum.json",
                             installation / "data/config/topscreen_ui.json"]
                hashes = {path: sha256_file(path) for path in preserved}
            elif any(sha256_file(path) != expected for path, expected in hashes.items()):
                raise ValueError("Reinstallation changed existing configuration or save sentinel")
        report["reinstall_preserved_config_and_save"] = True
        # Capture only two early frames. This is a visual boot check, not an
        # FPS benchmark: readback and cold first-run caches affect these timings.
        elapsed = run_logged([str(installation / LINUX.runtime), "--launch-profile",
                              str(installation / "TriAevum.launch.json"), "--frames", "0",
                              "--max-seconds", str(seconds), "--output", str(output / "runtime.json"),
                              "--screenshot", str(output / "framebuffer.bmp"),
                              "--screenshot-start-frame", "120", "--screenshot-interval", "600",
                              "--screenshot-sequence"], output / "game.log", cwd=installation,
                             environment=environment, timeout=seconds + 45)
        report.update(status="passed" if visible else "functional_pass_gui_visibility_pending",
                      gui_visibility_verified=visible, game_wall_seconds=elapsed,
                      framebuffer_captures=[str(path) for path in sorted(output.glob("*.bmp"))])
        if not report["framebuffer_captures"]:
            raise ValueError("Game run produced no framebuffer capture")
    except Exception as error:
        report.update(status="failed", error=str(error))
        raise
    finally:
        atomic_write_json(output / "qualification.json", report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for argument in ("installation", "rom", "output"):
        parser.add_argument("--" + argument, type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=60)
    args = parser.parse_args()
    if args.seconds < 15 or args.seconds > 300:
        parser.error("Boot qualification must last between 15 and 300 seconds")
    report = qualify(args.installation, args.rom, args.output, seconds=args.seconds)
    print(json.dumps({"status": report["status"], "report": str(args.output / "qualification.json")}))


if __name__ == "__main__":
    main()
