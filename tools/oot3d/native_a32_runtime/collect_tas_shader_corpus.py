"""Bounded private CTM replay using the instrumented Azahar collector.

Movie EOF proves input consumption, not gameplay synchronization. Inspect the
captured milestones before accepting a complete corpus. No user saves are used.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time


def inspect_ctm(path):
    data = Path(path).read_bytes()
    if len(data) < 256 or data[:4] != b'CTM\x1b':
        raise ValueError('Not a CTM replay')
    body = data[256:]
    if len(body) % 7:
        raise ValueError('Truncated CTM input record')
    counts = [0] * 6
    for offset in range(0, len(body), 7):
        kind = body[offset]
        if kind >= len(counts):
            raise ValueError(f'Unknown CTM input kind {kind}')
        counts[kind] += 1
    expected = struct.unpack_from('<Q', data, 0x54)[0]
    if not counts[0] or (expected and expected != counts[0]):
        raise ValueError('CTM pad count does not match the input stream')
    return {'movie_sha256': hashlib.sha256(data).hexdigest(),
            'title_id': f'{struct.unpack_from("<Q", data, 4)[0]:016x}',
            'expected_input_count': expected, 'pad_input_count': counts[0],
            'event_counts': dict(zip(('pad', 'touch', 'accelerometer', 'gyroscope',
                                     'ir_rst', 'extra_hid'), counts))}


def read_appended_log(path, offset):
    if not path.exists():
        return '', offset
    with path.open('rb') as stream:
        if path.stat().st_size < offset:
            offset = 0
        stream.seek(offset)
        text = stream.read().decode('utf-8', errors='replace')
        return text, stream.tell()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'rom', 'movie', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--seconds', type=int, default=7200)
    args = p.parse_args()
    if args.seconds <= 0:
        p.error('seconds must be positive')
    movie = args.movie.resolve(strict=True)
    movie_metadata = inspect_ctm(movie)
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    app = root / 'emulator'
    app.mkdir()
    exe = args.exe.resolve(strict=True)
    for path in exe.parent.iterdir():
        if path.is_file() and (path.suffix.lower() == '.dll' or path.name in (exe.name, 'qt.conf')):
            shutil.copy2(path, app / path.name)
    shutil.copytree(exe.parent / 'plugins', app / 'plugins')
    config = app / 'user/config'
    config.mkdir(parents=True)
    (config / 'qt-config.ini').write_text('''[UI]
first_start=false
first_start\\default=false
callout_flags=4294967295
callout_flags\\default=false
confirm_before_closing=false
confirm_before_closing\\default=false
check_for_update_on_start=false
check_for_update_on_start\\default=false
[Core]
cpu_clock_percentage=100
cpu_clock_percentage\\default=false
[System]
is_new_3ds=false
is_new_3ds\\default=false
region_value=1
region_value\\default=false
[Audio]
audio_emulation=0
audio_emulation\\default=false
[Renderer]
graphics_api=2
graphics_api\\default=false
resolution_factor=1
resolution_factor\\default=false
frame_limit=0
frame_limit\\default=false
use_vsync_new=false
use_vsync_new\\default=false
''')
    captures = root / 'captures'
    captures.mkdir()
    env = {k: v for k, v in os.environ.items() if not k.startswith(('OOT3D_', 'TRIAEVUM_TAS_'))}
    env.update(OOT3D_DIAGNOSTIC_FORCE_NATIVE_RENDER='1', OOT3D_DIAGNOSTIC_GRAPHICS_API='vulkan',
               OOT3D_PICA_DUMP='1', OOT3D_PICA_DUMP_DIR=str(captures),
               OOT3D_PICA_DUMP_FRAMES='1000000', OOT3D_PICA_DUMP_IMMEDIATE='1',
               OOT3D_PICA_DUMP_SHADER_SEED='1', OOT3D_PICA_DUMP_UNIQUE_SEED='1',
               OOT3D_PICA_DUMP_MAX_VERTICES='1', TRIAEVUM_TAS_STATUS=str(root / 'progress.json'),
               OOT3D_AUTO_SCREENSHOT_PATH=str(root / 'latest.png'),
               OOT3D_AUTO_SCREENSHOT_TRIGGER=str(root / 'screenshot.trigger'))
    manifest = {**movie_metadata,
                'emulator_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
                'rom_path': str(args.rom.resolve(strict=True)), 'sync_verified': False}
    (root / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    started = time.monotonic()
    # The meta frontend's legacy getopt probe can misinterpret the long option
    # as compression flags. Qt's equivalent short movie option avoids that path.
    console = (root / 'console.log').open('w')
    process = subprocess.Popen([str(app / exe.name), '-p', str(movie), str(args.rom.resolve())],
                               cwd=app, env=env, startupinfo=startup, stdout=console, stderr=console)
    last_report = 0
    screenshot_at = 0
    log_offset = 0
    log_tail = ''
    try:
        while process.poll() is None:
            elapsed = time.monotonic() - started
            appended, log_offset = read_appended_log(app / 'user/log/azahar_log.txt', log_offset)
            log_tail = (log_tail + appended)[-8192:]
            if 'Your playback will be out of sync' in log_tail:
                raise RuntimeError('Input-order desynchronization; corpus is not qualified')
            if elapsed > args.seconds:
                raise TimeoutError('Replay exceeded its bounded observation window')
            if shutil.disk_usage(root).free < 256 * 1024 * 1024:
                raise RuntimeError('Capture stopped before exhausting disk space')
            if elapsed - screenshot_at >= 30:
                if (root / 'latest.png').exists():
                    shutil.copy2(root / 'latest.png', root / f'milestone-{int(elapsed):05}.png')
                (root / 'screenshot.trigger').touch()
                screenshot_at = elapsed
            if elapsed - last_report >= 15:
                progress = (root / 'progress.json').read_text() if (root / 'progress.json').exists() else 'no progress'
                print(f'{elapsed:.0f}s {progress.strip()}', flush=True)
                last_report = elapsed
            time.sleep(1)
    except Exception as error:
        manifest['error'] = str(error)
    finally:
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=10)
        console.close()
        manifest['exit_code'] = process.returncode
        manifest['elapsed_seconds'] = time.monotonic() - started
        if (root / 'progress.json').exists():
            manifest['last_progress'] = json.loads((root / 'progress.json').read_text())
        (root / 'result.json').write_text(json.dumps(manifest, indent=2))
    print(json.dumps(manifest, indent=2), flush=True)
    if manifest.get('error') or process.returncode or not manifest.get('last_progress', {}).get('completed'):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
