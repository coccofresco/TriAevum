"""Bounded, device-native captures; no renderer changes or desktop screenshots."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import uuid


ROOT = Path(__file__).resolve().parents[2]
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def run(command, **kwargs):
    return subprocess.run(command, check=True, timeout=kwargs.pop("timeout", 20),
                          creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), **kwargs)


def select_device(output, requested=None):
    devices = [line.split() for line in output.splitlines()
               if line.strip() and not line.startswith(("List of devices", "*"))]
    ready = [row[0] for row in devices if len(row) >= 2 and row[1] == "device"]
    if requested:
        if requested not in ready:
            raise ValueError("Requested device is absent or unauthorized")
        return requested
    if len(ready) != 1:
        raise ValueError("Expected one authorized device; use --serial to select explicitly")
    return ready[0]


def create_session(output):
    root = Path(output).expanduser().resolve()
    if root == ROOT or ROOT in root.parents:
        raise ValueError("Captures must be outside the source repository")
    session = root / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + uuid.uuid4().hex[:8])
    session.mkdir(parents=True, exist_ok=False)
    return session


def adb_video_command(adb, seconds, remote, size=None):
    command = [*adb, "shell", "screenrecord", "--time-limit", str(seconds), "--bit-rate", "12000000"]
    if size:
        command += ["--size", size]
    return command + [remote]


def scrcpy_command(executable, serial, seconds, output, audio=False):
    command = [executable, "--serial", serial, "--no-control", "--no-playback", "--no-window",
               f"--time-limit={seconds}", f"--record={output}"]
    return command + (["--audio-codec=aac"] if audio else ["--no-audio"])


def screenshot(adb, path):
    result = run([*adb, "exec-out", "screencap", "-p"], capture_output=True)
    if not result.stdout.startswith(PNG_SIGNATURE):
        raise ValueError("Device did not return a PNG screenshot")
    path.write_bytes(result.stdout)


def capture(args):
    if not 1 <= args.seconds <= 180:
        raise ValueError("Capture duration must be between 1 and 180 seconds")
    if args.package and not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+", args.package):
        raise ValueError("Invalid Android package name")
    if args.size and not re.fullmatch(r"[1-9][0-9]*x[1-9][0-9]*", args.size):
        raise ValueError("Size must be WIDTHxHEIGHT")
    if args.backend == "adb" and args.audio:
        raise ValueError("ADB screenrecord has no audio; select --backend scrcpy")
    if args.backend == "scrcpy" and args.size:
        raise ValueError("--size applies to screenrecord only; scrcpy keeps the device dimensions")
    if args.backend == "scrcpy" and not args.scrcpy:
        raise ValueError("Set --scrcpy or SCRCPY to the local scrcpy executable")
    listed = run([args.adb, "devices", "-l"], capture_output=True, text=True)
    serial = select_device(listed.stdout, args.serial)
    adb = [args.adb, "-s", serial]
    output = create_session(args.output)
    remote = "/data/local/tmp/triaevum-capture-" + uuid.uuid4().hex + ".mp4"
    manifest = {"format": "triaevum_android_capture_v1", "status": "running",
                "backend": args.backend, "duration_requested_seconds": args.seconds,
                "audio_requested": args.audio, "package": args.package,
                "scope": "device_display_capture_not_game_fps_or_renderer_depth_evidence",
                "timestamps": {}, "files": {}}
    log_process = None
    log_file = None

    def text_command(*command):
        return run([*adb, *command], capture_output=True, text=True).stdout.strip()

    def stamp(name):
        before = datetime.now(timezone.utc).isoformat()
        device = text_command("shell", "date", "+%s")
        manifest["timestamps"][name] = {"host_before_utc": before, "device_epoch_seconds": device,
                                        "host_after_utc": datetime.now(timezone.utc).isoformat()}

    try:
        manifest["device"] = {key: text_command("shell", "getprop", prop) for key, prop in (
            ("model", "ro.product.model"), ("sdk", "ro.build.version.sdk"), ("abis", "ro.product.cpu.abilist"))}
        manifest["device"]["display_size"] = text_command("shell", "wm", "size")
        # Limit logs to an explicitly selected running app; never clear device logs.
        if args.package:
            pid = text_command("shell", "pidof", "-s", args.package)
            if not pid.isdecimal():
                raise ValueError("The selected app is not running")
            manifest["logcat_pid"] = int(pid)
            log_file = (output / "logcat.txt").open("wb")
            log_process = subprocess.Popen([*adb, "logcat", "-v", "epoch", "-T", "1", f"--pid={pid}"],
                stdout=log_file, stderr=subprocess.STDOUT,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        screenshot(adb, output / "before.png")
        stamp("record_start")
        started = time.monotonic()
        video = output / "display.mp4"
        with (output / "recorder.log").open("wb") as log:
            if args.backend == "adb":
                command = adb_video_command(adb, args.seconds, remote, args.size)
                run(command, stdout=log, stderr=subprocess.STDOUT, timeout=args.seconds + 30)
                run([*adb, "pull", remote, str(video)], stdout=log, stderr=subprocess.STDOUT, timeout=60)
            else:
                command = scrcpy_command(args.scrcpy, serial, args.seconds, video, args.audio)
                env = dict(os.environ, ADB=str(Path(args.adb).resolve()) if Path(args.adb).is_file() else args.adb)
                run(command, stdout=log, stderr=subprocess.STDOUT, env=env, timeout=args.seconds + 45)
        manifest["host_record_and_transfer_seconds"] = time.monotonic() - started
        stamp("record_end")
        if not video.is_file() or video.stat().st_size < 32:
            raise ValueError("Recorder did not produce a nonempty video")
        screenshot(adb, output / "after.png")
        if args.ffprobe:
            inspected = run([args.ffprobe, "-v", "error", "-count_frames", "-show_streams", "-show_format",
                             "-of", "json", str(video)], capture_output=True, text=True, timeout=60)
            metadata = json.loads(inspected.stdout)
            if not any(s.get("codec_type") == "video" and int(s.get("nb_read_frames", "0")) > 0
                       for s in metadata.get("streams", [])):
                raise ValueError("Video contains no decoded frames")
            (output / "media.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
            manifest["media_validation"] = "ffprobe_decoded_frames"
        else:
            manifest["media_validation"] = "size_only_ffprobe_unavailable"
        manifest["status"] = "captured"
    except BaseException as error:
        manifest["status"] = "failed"
        manifest["error"] = str(error)
        raise
    finally:
        if log_process:
            log_process.terminate()
            try:
                log_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                log_process.kill()
                log_process.wait(timeout=5)
        if log_file:
            log_file.close()
        if args.backend == "adb":
            try:
                # This exact generated file is owned by this session. No directory deletion.
                run([*adb, "shell", "rm", "-f", remote], capture_output=True, timeout=5)
            except (subprocess.SubprocessError, OSError):
                manifest["device_cleanup_pending"] = remote
        for path in sorted(output.iterdir()):
            if path.is_file():
                with path.open("rb") as stream:
                    digest = hashlib.file_digest(stream, "sha256").hexdigest()
                manifest["files"][path.name] = {"bytes": path.stat().st_size, "sha256": digest}
        (output / "capture.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        print(output, flush=True)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default=os.environ.get("ADB", "adb"))
    parser.add_argument("--serial")
    parser.add_argument("--output", type=Path, required=True, help="Private captures directory outside the repo")
    parser.add_argument("--seconds", type=int, default=15)
    parser.add_argument("--backend", choices=("adb", "scrcpy"), default="scrcpy")
    parser.add_argument("--scrcpy", default=os.environ.get("SCRCPY", shutil.which("scrcpy")))
    parser.add_argument("--audio", action="store_true")
    parser.add_argument("--size", help="Optional ADB encoder WIDTHxHEIGHT; default device size")
    parser.add_argument("--package", help="Optional running app package for PID-filtered logcat")
    parser.add_argument("--ffprobe", default=shutil.which("ffprobe"))
    args = parser.parse_args()
    try:
        capture(args)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Capture failed: {error}\n")


if __name__ == "__main__":
    main()
