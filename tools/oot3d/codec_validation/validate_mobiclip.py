"""Compare external decomp MobiClip with FFmpeg on user-owned movie packets.

Developer diagnostic only: neither the copied source nor movie-derived output
belongs in a release. The external repository is never modified.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args, timeout=120):
    return subprocess.run(list(map(str, args)), check=True, capture_output=True,
                          text=True, timeout=timeout)


def compare(reference, actual, frame_bytes):
    if frame_bytes <= 0:
        raise ValueError('frame size must be positive')
    result = dict(compared_frames=0, first_mismatch=None, differing_bytes=0,
                  maximum_error=0, absolute_error=0)
    with reference.open('rb') as ref, actual.open('rb') as got:
        while True:
            a, b = ref.read(frame_bytes), got.read(frame_bytes)
            if len(a) != frame_bytes or len(b) != frame_bytes:
                result['complete'] = not a and not b and result['compared_frames'] > 0
                break
            if a != b:
                if result['first_mismatch'] is None:
                    result['first_mismatch'] = result['compared_frames']
                errors = [abs(x-y) for x, y in zip(a, b)]
                result['differing_bytes'] += sum(e != 0 for e in errors)
                result['absolute_error'] += sum(errors)
                result['maximum_error'] = max(result['maximum_error'], max(errors))
            result['compared_frames'] += 1
    return result


def repair_planar(source):
    old = 'const s32 shift = size == 16 ? 3 : 2;'
    new = 'const s32 shift = Oot3dMobiclip_AdjustPlanar((s32)size, size) == 8 ? 3 : 2;'
    if source.count(old) != 1:
        raise ValueError('planar diagnostic patch requires exactly one matching expression')
    return source.replace(old, new)


def passed(entry):
    comparison = entry.get('comparison', {})
    return (not entry.get('error') and entry.get('returncode') == 0 and
            entry.get('decoder', {}).get('status') == 0 and
            entry.get('decoder', {}).get('decoded_frames') == entry.get('requested_frames') and
            comparison.get('compared_frames') == entry.get('requested_frames') and
            comparison.get('complete') and comparison.get('differing_bytes') == 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp-root', type=Path, required=True)
    parser.add_argument('--ffmpeg-dir', type=Path, required=True)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--frames', type=int, default=64)
    parser.add_argument('--repair-planar', action='store_true',
                        help='apply the diagnosed 8x8 planar shift repair to the private copy only')
    parser.add_argument('movies', nargs='+', type=Path)
    args = parser.parse_args()
    if not 1 <= args.frames <= 10000:
        parser.error('--frames must be between 1 and 10000')
    root = args.output.resolve()
    decomp = args.decomp_root.resolve()
    if root == decomp or decomp in root.parents:
        parser.error('output must be outside the read-only decomp repository')
    if (root/'report.json').exists():
        parser.error('use a new output directory to preserve previous evidence')
    root.mkdir(parents=True, exist_ok=True)
    files = ('src/middleware/mobiclip.c', 'include/oot3d/mobiclip.h', 'include/oot3d/types.h')
    hashes = {}
    for name in files:
        dest = root/'snapshot'/name
        dest.parent.mkdir(parents=True, exist_ok=True)
        hashes[name] = sha(decomp/name)
        shutil.copy2(decomp/name, dest)
        assert sha(dest) == hashes[name]
    source = root/'snapshot/src/middleware/mobiclip.c'
    if args.repair_planar:
        source.write_text(repair_planar(source.read_text()), encoding='utf-8')
    suffix = '.exe' if os.name == 'nt' else ''
    ffmpeg, ffprobe = (args.ffmpeg_dir/(tool+suffix) for tool in ('ffmpeg', 'ffprobe'))
    exe = root/('mobiclip-probe'+suffix)
    run([args.cc, '-std=c11', '-O2', '-I', root/'snapshot/include',
         root/'snapshot/src/middleware/mobiclip.c', Path(__file__).with_name('mobiclip_probe.c'), '-o', exe])
    report = {'format': 'triaevum_mobiclip_comparison_v1', 'source_sha256': hashes,
              'compiled_source_sha256': sha(source), 'repair_planar': args.repair_planar,
              'compiler': run([args.cc, '--version']).stdout.splitlines()[0],
              'probe_sha256': sha(Path(__file__).with_name('mobiclip_probe.c')),
              'decomp_commit': run(['git', '-C', decomp, 'rev-parse', 'HEAD']).stdout.strip(),
              'reference': run([ffmpeg, '-version']).stdout.splitlines()[0], 'movies': []}
    for index, movie in enumerate(args.movies):
        out = root/f'{index:03d}-{movie.stem}'
        out.mkdir()
        entry = {'movie': movie.name, 'sha256': sha(movie)}
        report['movies'].append(entry)
        try:
            metadata = json.loads(run([ffprobe, '-v', 'error', '-select_streams', 'v:0',
                '-show_streams', '-show_packets', '-show_entries', 'packet=size:stream=width,height,codec_name',
                '-of', 'json', movie]).stdout)
            stream = metadata['streams'][0]
            if stream['codec_name'] != 'mobiclip':
                raise ValueError('not a MobiClip video stream')
            width, height = stream['width'], stream['height']
            sizes = [int(p['size']) for p in metadata['packets'][:args.frames]]
            if not sizes: raise ValueError('no packets')
            (out/'sizes.txt').write_text('\n'.join(map(str, sizes))+'\n')
            run([ffmpeg, '-v', 'error', '-i', movie, '-map', '0:v:0', '-c', 'copy',
                 '-copyinkf', '-frames:v', len(sizes), '-f', 'data', out/'packets.bin'])
            if (out/'packets.bin').stat().st_size != sum(sizes):
                raise ValueError('packet boundaries do not match extracted stream')
            run([ffmpeg, '-v', 'error', '-i', movie, '-map', '0:v:0', '-frames:v', len(sizes),
                 '-fps_mode', 'passthrough', '-pix_fmt', 'yuv420p', '-f', 'rawvideo', out/'reference.yuv'])
            result = subprocess.run([str(exe), str(width), str(height), str(out/'packets.bin'),
                str(out/'sizes.txt'), str(out/'decomp.yuv')], capture_output=True, text=True, timeout=120)
            entry.update(width=width, height=height, requested_frames=len(sizes), returncode=result.returncode,
                         stderr=result.stderr, decoder=json.loads(result.stdout),
                         comparison=compare(out/'reference.yuv', out/'decomp.yuv', width*height*3//2))
        except (subprocess.SubprocessError, ValueError, KeyError, IndexError, OSError) as error:
            entry['error'] = str(error)
            if isinstance(error, subprocess.CalledProcessError):
                entry['stderr'] = error.stderr
        (root/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print(json.dumps(entry), flush=True)
    return int(not all(passed(e) for e in report['movies']))


if __name__ == '__main__':
    raise SystemExit(main())
