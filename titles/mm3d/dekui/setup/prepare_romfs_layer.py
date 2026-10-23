#!/usr/bin/env python3
# Ported from mm3d-dekui tools/mm3d/build_release_assets.py and
# setup_game_folder.py (tgacquin/mm3d-dekui).
# SPDX-License-Identifier: GPL-2.0-or-later
"""The RomFS layer dekui's setup prepares, from the user's own files.

Reads the layouts dekui edits from the user's mounted RomFS (a RomFS image
whose level 3 starts at --romfs-offset, or an extracted RomFS directory),
takes Project Restoration's RomFS files from the user's Restoration
directory (its romfs/ folder), applies dekui's setup transformations and
writes a layer directory mirroring RomFS paths for the runtime's RomFS
overlay. Every input and output is checked against dekui's published
fingerprints (setup-fingerprints.json); nothing is written unless all
match. The originals dekui keeps for its later re-edits
(*.mm3d-*-source) go to --sources, outside the layer. No game data or
Restoration asset is part of this tool.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import struct
import tempfile

from dekui_gar import restore_actions, stop_map_slides

HERE = Path(__file__).resolve().parent


def digest(data):
    return hashlib.sha256(data).hexdigest()


def floats(data, values):
    result = bytearray(data)
    for offset, value in values.items():
        struct.pack_into('<f', result, offset, value)
    return bytes(result)


def transform(name, source, original_play):
    """dekui's frozen setup edits (build_release_assets.transform)."""
    if name == 'Joker.Main.Play.gar':
        return restore_actions(original_play, source)
    if name == 'Joker.Main.Play.SubMenu.gar':
        result = bytearray(stop_map_slides(source)[0])
        # UpQuestlist null_plate07/08 object flags: default visibility.
        for offset in (0x9eec, 0x9f74):
            if struct.unpack_from('<H', result, offset)[0] != 14:
                raise ValueError('Unexpected notebook row visibility flags')
            struct.pack_into('<H', result, offset, 15)
        return bytes(result)
    if name == 'Joker.Main.New.gar':
        # Hud2 groups moved into the 16:9 margins.
        margin = struct.unpack('<f', struct.pack('<f', 240 * 16 / 9))[0]
        margin = (margin - 400) / 2
        values = {}
        for offset, x, shift in ((0x1388, 0, margin), (0x13cc, 373, margin),
                                 (0x1498, 20, -margin), (0x1c4c, 8, -margin), (0x1e28, 8, -margin),
                                 (0x2994, 0, -margin), (0x6ee4, 0, margin)):
            values[offset] = x + shift
        values.update({0x149c: 53.6, 0x2f70: -23.4, 0x2f7c: 2,
                       0x6ae8: 15 - 40 / 3, 0x6aec: 49.6})
        return floats(source, values)
    if name == 'Joker.Main.gar':
        values = {}
        for base, x, y in ((0xb368, -13.209999084472656, .5199999809265137),
                           (0xba0c, -13.209999084472656, .5199999809265137),
                           (0xba94, -12.949999809265137, .5199999809265137),
                           (0xbb1c, 66.54999542236328, -.570000171661377)):
            values.update({base: x, base + 4: y, base + 12: .87, base + 16: .87})
        values.update({0xc3e4: -40, 0xc3f0: 480, 0xc414: -50, 0xc420: 500})
        return floats(source, values)
    if name == 'Joker.Main.gar.mm3d-header-source':
        return floats(source, {0xd03c: 10, 0xd054: 1.3, 0xd058: 1.3,
                               0xd094: -40 / 3, 0xd0a0: 400 + 80 / 3,
                               0xd0a4: 24 * struct.unpack('<f', struct.pack('<f', 1.3))[0]})
    if name == 'Joker.gar':
        return floats(source, {0x590: 400 + 80 / 3})
    return source


class RomFs:
    """Files of a RomFS level 3 by path: an image or an extracted directory."""

    def __init__(self, path, offset):
        self.path = Path(path)
        self.offset = offset
        if self.path.is_dir():
            return
        with self.path.open('rb') as stream:
            stream.seek(offset)
            header = struct.unpack('<10I', stream.read(40))
            if header[0] != 40:
                raise ValueError('Unsupported RomFS level 3 header')
            stream.seek(offset + header[3])
            self.directories = stream.read(header[4])
            stream.seek(offset + header[7])
            self.files = stream.read(header[8])
            self.data = header[9]

    def read(self, name):
        if self.path.is_dir():
            return (self.path / name).read_bytes()
        directory = 0
        parts = PurePosixPath(name).parts
        for part in parts[:-1]:
            child = struct.unpack_from('<I', self.directories, directory + 8)[0]
            while child != 0xFFFFFFFF:
                length = struct.unpack_from('<I', self.directories, child + 20)[0]
                if self.directories[child + 24:child + 24 + length].decode('utf-16le') == part:
                    break
                child = struct.unpack_from('<I', self.directories, child + 4)[0]
            if child == 0xFFFFFFFF:
                raise FileNotFoundError(name)
            directory = child
        file = struct.unpack_from('<I', self.directories, directory + 12)[0]
        while file != 0xFFFFFFFF:
            _, sibling, at, size, _, length = struct.unpack_from('<IIQQII', self.files, file)
            if self.files[file + 32:file + 32 + length].decode('utf-16le') == parts[-1]:
                with self.path.open('rb') as stream:
                    stream.seek(self.offset + self.data + at)
                    return stream.read(size)
            file = sibling
        raise FileNotFoundError(name)


def prepare(romfs, restoration, layer, sources):
    manifest = json.loads((HERE / 'setup-fingerprints.json').read_text())
    entries = [e for e in manifest['files'] if e['output'].startswith('romfs/')]
    sources_data = {}
    for entry in entries:
        relative = entry['input'][len('romfs/'):]
        if entry['input_root'] == 'original':
            data = romfs.read(relative)
        else:
            data = (restoration / 'romfs' / relative).read_bytes()
        if digest(data) != entry['input_sha256']:
            raise ValueError(f'unsupported {entry["input_root"]} input: {entry["input"]}')
        sources_data[entry['output']] = data
    play = romfs.read('layout/Joker.Main.Play.gar')
    if digest(play) != manifest['original_action_sha256']:
        raise ValueError('unsupported original layout/Joker.Main.Play.gar')
    for target in (layer, sources):
        if target.exists():
            raise ValueError(f'{target} exists; refusing to overwrite')
    stage = Path(tempfile.mkdtemp(prefix='.dekui-romfs-', dir=layer.parent))
    try:
        for entry in entries:
            name = PurePosixPath(entry['output'])
            data = transform(name.name, sources_data[entry['output']], play)
            if len(data) != entry['size'] or digest(data) != entry['output_sha256']:
                raise ValueError('reconstruction mismatch: ' + entry['output'])
            relative = PurePosixPath(*name.parts[1:])
            # dekui's own re-edit sources stay outside the RomFS layer.
            root = stage / ('sources' if '.mm3d-' in name.name else 'layer')
            path = root.joinpath(*relative.parts)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (stage / 'layer').rename(layer)
        if (stage / 'sources').exists():
            (stage / 'sources').rename(sources)
    finally:
        shutil.rmtree(stage, ignore_errors=True)
    return len(entries)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--romfs', type=Path, required=True,
                        help='the user\'s RomFS image (or extracted RomFS directory)')
    parser.add_argument('--romfs-offset', type=lambda v: int(v, 0), default=0x1000,
                        help='level 3 offset inside the image (default 0x1000)')
    parser.add_argument('--restoration', type=Path, required=True,
                        help='the user\'s Project Restoration directory (holding romfs/)')
    parser.add_argument('--layer', type=Path, required=True, help='RomFS layer to create')
    parser.add_argument('--sources', type=Path, required=True,
                        help='dekui re-edit sources to create (outside the layer)')
    args = parser.parse_args()
    try:
        count = prepare(RomFs(args.romfs, args.romfs_offset), args.restoration, args.layer, args.sources)
    except (ValueError, OSError) as error:
        parser.exit(1, f'prepare_romfs_layer: {error}\n')
    print(f'prepare_romfs_layer: {count} files verified and written')
