# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-2.0-or-later

"""Offline USA resource service-view adaptation for the canonical EUR module.

File payloads remain those of the input ROM. Only same-size region names,
RomFS hash chains and QM language slot references change. The output is a
Level-3 service view, not an IVFC image with invalid integrity hashes.
RomFS hash semantics follow Azahar's layered_fs.cpp (GPL-2.0-or-later).
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import struct
import tempfile

INVALID = 0xFFFFFFFF
ALGORITHM = 'oot3d_usa_resources_to_eur_v1'


def name_hash(parent: int, encoded: bytes) -> int:
    value = parent ^ 123456789
    for (unit,) in struct.iter_unpack('<H', encoded):
        value = (((value >> 5) | (value << 27)) ^ unit) & 0xFFFFFFFF
    return value


def adapt_qm_table(data: bytes, file_size: int) -> bytes:
    if len(data) < 16:
        raise ValueError('truncated QM header')
    magic, version, count, reserved = struct.unpack_from('<4sIII', data)
    if magic != b'QM\0\0' or version != 4 or reserved != 0 or not 0 < count <= 100000:
        raise ValueError('unsupported QM container')
    table_end = 16 + count * 96
    if len(data) != table_end or table_end > file_size:
        raise ValueError('invalid QM record table')
    output = bytearray(data)
    for index in range(count):
        entry = 16 + index * 96
        for slot in range(9):
            offset, size = struct.unpack_from('<II', data, entry + 16 + slot*8)
            if bool(offset) != bool(size) or (offset and (offset < table_end or offset+size > file_size)):
                raise ValueError('QM payload is outside file')
        # Native language slots: US English 1 -> EU English 2,
        # US French 5 -> EU French 4, US Spanish 7 -> EU Spanish 6.
        for destination, source in ((2, 1), (4, 5), (6, 7)):
            start = entry + 16 + destination*8
            original = data[start:start+8]
            replacement = data[entry+16+source*8:entry+24+source*8]
            if original != bytes(8) and original != replacement:
                raise ValueError('QM destination language is already populated')
            output[start:start+8] = replacement
    return bytes(output)


def normalize_romfs(source: Path, destination: Path) -> dict:
    if source.is_symlink() or not source.is_file() or destination.exists() or source.resolve() == destination.resolve():
        raise ValueError('normalization requires a regular input and a new output')
    with source.open('rb') as stream:
        image_size = source.stat().st_size
        base = 0x1000 if stream.read(4) == b'IVFC' else 0
        size = image_size - base
        def read(offset, count):
            if offset < 0 or count < 0 or offset+count > size:
                raise ValueError('RomFS extent outside image')
            stream.seek(base+offset)
            result = stream.read(count)
            if len(result) != count:
                raise ValueError('truncated RomFS extent')
            return result
        h = struct.unpack('<10I', read(0, 40))
        if h[0] != 40 or h[2] % 4 or h[6] % 4 or not h[2] or not h[6]:
            raise ValueError('invalid Level-3 header')
        if max(h[2], h[4], h[6], h[8]) > 16*1024*1024:
            raise ValueError('RomFS metadata exceeds limits')
        end = 40
        for start, length in ((h[1], h[2]), (h[3], h[4]), (h[5], h[6]), (h[7], h[8])):
            if start < end or start % 4 or not length or start+length > size:
                raise ValueError('invalid or overlapping RomFS metadata extents')
            end = start+length
        if h[9] < end or h[9] > size or h[9] % 16:
            raise ValueError('invalid RomFS data extent')
        dirs, files = bytearray(read(h[3], h[4])), bytearray(read(h[7], h[8]))
        dir_records, file_records = {}, {}
        extents = {id(dirs): [], id(files): []}
        def record(table, offset, fixed):
            if offset < 0 or offset+fixed > len(table) or offset % 4:
                raise ValueError('invalid RomFS record offset')
            name_size = struct.unpack_from('<I', table, offset+fixed-4)[0]
            if name_size % 2 or offset+fixed+name_size > len(table):
                raise ValueError('invalid RomFS record name')
            name = bytes(table[offset+fixed:offset+fixed+name_size])
            extents[id(table)].append((offset, (offset+fixed+name_size+3) & ~3))
            return name, name.decode('utf-16-le')
        pending = [(0, '', 0)]
        while pending:
            offset, prefix, expected_parent = pending.pop()
            if offset in dir_records:
                raise ValueError('cyclic or duplicate RomFS directory')
            encoded, name = record(dirs, offset, 24)
            parent, sibling, child_dir, child_file, _, _ = struct.unpack_from('<6I', dirs, offset)
            if (parent != expected_parent or '/' in name or '\\' in name or '\0' in name or name in ('.', '..')
                    or (offset == 0 and (name or sibling != INVALID)) or (offset != 0 and not name)):
                raise ValueError('invalid RomFS directory ancestry')
            path = prefix+'/'+name if prefix else name
            dir_records[offset] = (parent, encoded, path)
            if sibling != INVALID:
                pending.append((sibling, prefix, expected_parent))
            if child_dir != INVALID:
                pending.append((child_dir, path, offset))
            while child_file != INVALID:
                if child_file in file_records:
                    raise ValueError('cyclic or duplicate RomFS file')
                fname, text = record(files, child_file, 32)
                owner, following, payload, length, _, _ = struct.unpack_from('<IIQQII', files, child_file)
                if owner != offset or not text or '/' in text or '\\' in text or '\0' in text or text in ('.', '..'):
                    raise ValueError('invalid RomFS file ancestry')
                if h[9]+payload+length > size:
                    raise ValueError('RomFS file outside image')
                file_records[child_file] = (owner, fname, path+'/'+text if path else text, payload, length)
                child_file = following
        for records in extents.values():
            end = 0
            for start, limit in sorted(records):
                if start < end:
                    raise ValueError('overlapping RomFS records')
                end = limit
        if len({r[2] for r in dir_records.values()}) != len(dir_records) or len({r[2] for r in file_records.values()}) != len(file_records):
            raise ValueError('duplicate RomFS path')
        expected_dirs = {'message/us', 'misc/us'}
        if not expected_dirs.issubset({r[2] for r in dir_records.values()}):
            raise ValueError('missing USA resource directories')
        if {'message/eu', 'misc/eu'} & {r[2] for r in dir_records.values()}:
            raise ValueError('EU resource directories already exist')
        qm = [r for r in file_records.values() if r[2] == 'message/us/us.qm']
        if len(qm) != 1:
            raise ValueError('missing USA message container')
        qm_offset, qm_size = h[9]+qm[0][3], qm[0][4]
        head = read(qm_offset, 16)
        count = struct.unpack_from('<I', head, 8)[0]
        if not 0 < count <= 100000 or 16+count*96 > qm_size:
            raise ValueError('invalid QM count')
        qm_table = adapt_qm_table(read(qm_offset, 16+count*96), qm_size)
        directory_hashes, file_hashes = [INVALID]*(h[2]//4), [INVALID]*(h[6]//4)
        for offset, (parent, name, path) in sorted(dir_records.items()):
            if path in expected_dirs:
                name = 'eu'.encode('utf-16-le')
                dirs[offset+24:offset+28] = name
            bucket = name_hash(parent, name) % len(directory_hashes)
            struct.pack_into('<I', dirs, offset+16, directory_hashes[bucket])
            directory_hashes[bucket] = offset
        for offset, (parent, name, path, _, _) in sorted(file_records.items()):
            if path == 'message/us/us.qm':
                name = 'eu.qm'.encode('utf-16-le')
                files[offset+32:offset+32+len(name)] = name
            bucket = name_hash(parent, name) % len(file_hashes)
            struct.pack_into('<I', files, offset+24, file_hashes[bucket])
            file_hashes[bucket] = offset
        destination.parent.mkdir(parents=True, exist_ok=True)
        fd, temporary = tempfile.mkstemp(dir=destination.parent, suffix='.partial')
        try:
            with os.fdopen(fd, 'w+b') as output:
                stream.seek(base)
                shutil.copyfileobj(stream, output, 4*1024*1024)
                for offset, payload in ((h[1], struct.pack(f'<{len(directory_hashes)}I', *directory_hashes)),
                                        (h[3], dirs), (h[5], struct.pack(f'<{len(file_hashes)}I', *file_hashes)),
                                        (h[7], files), (qm_offset, qm_table)):
                    output.seek(offset)
                    output.write(payload)
                output.flush()
                os.fsync(output.fileno())
            os.replace(temporary, destination)
        finally:
            Path(temporary).unlink(missing_ok=True)
    return {'files':len(file_records), 'renamed_directories':2, 'renamed_files':1,
            'qm_records':count, 'language_slots':{'2':1, '4':5, '6':7},
            'output_format':'romfs_level3_service_view', 'payloads_replaced':0}
