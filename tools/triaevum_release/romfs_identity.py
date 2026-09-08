"""Bounded logical RomFS identity, independent of physical file placement.

CTR Level-3 metadata and lookup hashing follow Azahar's layered_fs.cpp.
Both directory traversal and hash lookup must describe the same records.
"""
import hashlib
import json
import struct
from pathlib import Path

try:
    from .oot3d_region_assets import name_hash, INVALID
except ImportError:
    from oot3d_region_assets import name_hash, INVALID


def fingerprint(path: Path) -> dict:
    with path.open('rb') as stream:
        size = path.stat().st_size
        base = 0x1000 if stream.read(4) == b'IVFC' else 0
        size -= base

        def read(offset, length):
            if offset < 0 or length < 0 or offset + length > size:
                raise ValueError('RomFS extent outside image')
            stream.seek(base + offset)
            data = stream.read(length)
            if len(data) != length:
                raise ValueError('Truncated RomFS')
            return data

        h = struct.unpack('<10I', read(0, 40))
        if h[0] != 40:
            raise ValueError('Invalid RomFS Level-3 header')
        end = 40
        for offset, length in ((h[1], h[2]), (h[3], h[4]), (h[5], h[6]), (h[7], h[8])):
            if offset < end or offset % 4 or not 0 < length <= 16*1024*1024:
                raise ValueError('Invalid RomFS metadata bounds')
            read(offset, length)
            end = offset + length
        if h[2] % 4 or h[6] % 4 or h[9] < end or h[9] > size or h[9] % 16:
            raise ValueError('Invalid RomFS data bounds')
        dirs, files = read(h[3], h[4]), read(h[7], h[8])
        directory_records, file_records = {}, {}
        extents = {0: [], 1: []}

        def record(table, offset, fixed, group):
            if offset < 0 or offset % 4 or offset + fixed > len(table):
                raise ValueError('Invalid RomFS record')
            length = struct.unpack_from('<I', table, offset+fixed-4)[0]
            if length % 2 or offset + fixed + length > len(table):
                raise ValueError('Invalid RomFS name bounds')
            encoded = table[offset+fixed:offset+fixed+length]
            name = encoded.decode('utf-16-le')
            if '/' in name or '\\' in name or '\0' in name or name in ('.', '..'):
                raise ValueError('Invalid RomFS name')
            extents[group].append((offset, (offset+fixed+length+3) & ~3))
            return encoded, name

        pending = [(0, '', 0)]
        while pending:
            offset, prefix, owner = pending.pop()
            if offset in directory_records:
                raise ValueError('Cyclic RomFS directory')
            encoded, name = record(dirs, offset, 24, 0)
            parent, sibling, child, file, chain, _ = struct.unpack_from('<6I', dirs, offset)
            if parent != owner or (offset == 0 and (name or sibling != INVALID)) or (offset and not name):
                raise ValueError('Invalid RomFS ancestry')
            full = prefix + '/' + name if prefix else name
            directory_records[offset] = (parent, encoded, chain, full)
            if sibling != INVALID:
                pending.append((sibling, prefix, owner))
            if child != INVALID:
                pending.append((child, full, offset))
            while file != INVALID:
                if file in file_records:
                    raise ValueError('Cyclic RomFS file')
                fname, text = record(files, file, 32, 1)
                parent, following, payload, length, chain, _ = struct.unpack_from('<IIQQII', files, file)
                if parent != offset or not text or h[9]+payload+length > size:
                    raise ValueError('Invalid RomFS file bounds or ancestry')
                file_records[file] = (parent, fname, chain, full+'/'+text if full else text, payload, length)
                file = following
        for ranges in extents.values():
            end = 0
            for start, limit in sorted(ranges):
                if start < end:
                    raise ValueError('Overlapping RomFS records')
                end = limit
        for records, offset, length in ((directory_records, h[1], h[2]), (file_records, h[5], h[6])):
            if len({r[3] for r in records.values()}) != len(records):
                raise ValueError('Duplicate RomFS path')
            visited = set()
            for bucket, (link,) in enumerate(struct.iter_unpack('<I', read(offset, length))):
                while link != INVALID:
                    if link in visited or link not in records:
                        raise ValueError('Invalid RomFS hash chain')
                    visited.add(link)
                    parent, name, following = records[link][:3]
                    if name_hash(parent, name) % (length//4) != bucket:
                        raise ValueError('Incorrect RomFS hash bucket')
                    link = following
            if visited != set(records):
                raise ValueError('RomFS traversal and lookup differ')
        entries = [['directory', row[3]] for row in directory_records.values()]
        end = 0
        for start, length in sorted((row[4], row[5]) for row in file_records.values() if row[5]):
            if start < end:
                raise ValueError('Overlapping RomFS payloads')
            end = start + length
        total = 0
        for row in file_records.values():
            _, _, _, full, offset, length = row
            stream.seek(base+h[9]+offset)
            digest = hashlib.sha256()
            remaining = length
            while remaining:
                chunk = stream.read(min(remaining, 4*1024*1024))
                if not chunk:
                    raise ValueError('Truncated RomFS file')
                digest.update(chunk)
                remaining -= len(chunk)
            entries.append(['file', full, length, digest.hexdigest()])
            total += length
        canonical = json.dumps(sorted(entries), ensure_ascii=True, separators=(',', ':')).encode('ascii')
        return {'sha256': hashlib.sha256(b'triaevum-romfs-tree-v1\0'+canonical).hexdigest(),
                'files': len(file_records), 'directories': len(directory_records), 'payload_bytes': total}
