# SPDX-License-Identifier: GPL-2.0-or-later
"""MM layout additions over O3RO; leaves the original RomFS and OoT builder alone.

Restoration introduces Joker.Main.New.gar, so a replacement-only overlay cannot
represent its layer. Preserve existing record offsets/hash chains, append new
records, relocate the file metadata table, and patch parent/hash heads.
Only additions in existing directories are supported; never invent directories.
"""
import os
from pathlib import PurePosixPath
import struct
import tempfile
from pathlib import Path

from romfs_mods import INVALID, MAXIMUM_MEMBER_BYTES, build_romfs_overlay, romfs_file_records


def align(value, multiple=16):
    return (value + multiple - 1) // multiple * multiple


def name_hash(parent, encoded):
    value = parent ^ 123456789
    for (unit,) in struct.iter_unpack('<H', encoded):
        value = (((value >> 5) | (value << 27)) ^ unit) & 0xffffffff
    return value


def build_overlay(romfs, *, service_offset, service_size, replacements, output):
    with romfs.open('rb') as source:
        if service_offset < 0 or service_size <= 0 or service_offset + service_size > romfs.stat().st_size:
            raise ValueError('Invalid service view')
        header, records = romfs_file_records(source, service_offset, service_size)
        if all(path in records for path in replacements):
            return build_romfs_overlay(romfs, service_offset=service_offset, service_size=service_size,
                                       replacements=replacements, output=output)
        def read(at, size):
            source.seek(service_offset + at)
            return source.read(size)
        old_header = read(0, 40)
        old_dirs, old_hashes = read(header[3], header[4]), read(header[5], header[6])
        dirs, hashes = bytearray(old_dirs), bytearray(old_hashes)
        files = bytearray(read(header[7], header[8]))
        if len(hashes) % 4:
            raise ValueError('Invalid file hash table')
        directories, pending = {}, [(0, '')]
        while pending:
            at, prefix = pending.pop()
            _, sibling, child, _, _, size = struct.unpack_from('<6I', dirs, at)
            name = dirs[at + 24:at + 24 + size].decode('utf-16-le')
            path = f'{prefix}/{name}' if prefix and name else name or prefix
            directories[path] = at
            if sibling != INVALID:
                pending.append((sibling, prefix))
            if child != INVALID:
                pending.append((child, path))
        payloads, added = [], 0
        cursor = service_size
        for path, member in sorted(replacements.items()):
            name = PurePosixPath(path)
            if name.is_absolute() or any(part in ('.', '..') for part in name.parts) or '\\' in path or '\0' in path:
                raise ValueError('Invalid addition path')
            parent_path = str(name.parent) if str(name.parent) != '.' else ''
            if parent_path not in directories:
                raise ValueError(f'Addition needs an existing parent: {path}')
            size = member.stat().st_size
            if not 0 < size <= MAXIMUM_MEMBER_BYTES:
                raise ValueError(f'Invalid member size: {path}')
            cursor = align(cursor)
            if path in records:
                record = records[path][0]
                struct.pack_into('<QQ', files, record + 8, cursor - header[9], size)
            else:
                parent = directories[parent_path]
                encoded = name.name.encode('utf-16-le')
                record = align(len(files), 4)
                files.extend(bytes(record - len(files)))
                bucket = name_hash(parent, encoded) % (len(hashes) // 4) * 4
                sibling = struct.unpack_from('<I', dirs, parent + 12)[0]
                chain = struct.unpack_from('<I', hashes, bucket)[0]
                files.extend(struct.pack('<IIQQII', parent, sibling, cursor - header[9], size, chain, len(encoded)))
                files.extend(encoded)
                files.extend(bytes(align(len(files), 4) - len(files)))
                struct.pack_into('<I', dirs, parent + 12, record)
                struct.pack_into('<I', hashes, bucket, record)
                added += 1
            payloads.append((cursor, member, size))
            cursor += size
        metadata_offset = align(cursor)
        if metadata_offset > 0xffffffff or len(files) > 16 * 1024 * 1024:
            raise ValueError('Overlay metadata exceeds native offsets')
        new_header = list(header)
        new_header[7:9] = metadata_offset, len(files)
        patches = sorted([(0, old_header, struct.pack('<10I', *new_header)),
                          (header[3], old_dirs, dirs), (header[5], old_hashes, hashes)])
        if any(a[0] + len(a[1]) > b[0] for a, b in zip(patches, patches[1:])):
            raise ValueError('Overlapping metadata tables')
        # Runtime O3RO parser caps each guarded patch at 64 KiB.
        patches = [(at + start, old[start:start + 65536], new[start:start + 65536])
                   for at, old, new in patches for start in range(0, len(old), 65536)]
        view_size = metadata_offset + len(files)
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix='.overlay-', dir=output.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(fd, 'wb') as stream:
            position = 64 + 32 * len(patches)
            tail = position + sum(len(old) * 2 for _, old, _ in patches)
            stream.write(struct.pack('<4sIQQIIQ24x', b'O3RO', 1, service_size, view_size, len(patches), 0, tail))
            for at, old, new in patches:
                stream.write(struct.pack('<QIIQQ', at, len(old), 0, position, position + len(old)))
                position += len(old) * 2
            for _, old, new in patches:
                stream.write(old)
                stream.write(new)
            position = service_size
            for at, member, size in payloads:
                stream.write(bytes(at - position))
                with member.open('rb') as data:
                    remaining = size
                    while remaining:
                        block = data.read(min(remaining, 1024 * 1024))
                        if not block:
                            raise ValueError('Member changed during preparation')
                        stream.write(block)
                        remaining -= len(block)
                    if data.read(1):
                        raise ValueError('Member changed during preparation')
                position = at + size
            stream.write(bytes(metadata_offset - position))
            stream.write(files)
            stream.flush()
            os.fsync(stream.fileno())
        if temporary.stat().st_size != tail + view_size - service_size:
            raise ValueError('Incomplete overlay')
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)
    return {'files': len(replacements), 'added_files': added, 'base_view_bytes': service_size,
            'appended_bytes': view_size - service_size}
