#!/usr/bin/env python3
# Ported from mm3d-dekui tools/mm3d/single_screen_text.py (tgacquin/mm3d-dekui).
# SPDX-License-Identifier: GPL-2.0-or-later
"""dekui's single-screen wording, compiled against the user's own messages.

dekui replaces in-game references to the lower screen ("Tap ...") with its
controls, as reviewed English wording (single-screen.json). This reads the
user's English GMSG from their RomFS (EUR message/eu/eue.gmsg by default),
applies each message's edits as dekui does - the replacement may not exceed
the message's allocation and the dialogue flow (pages, prompts, fanfares)
must stay native - and writes the guarded in-memory edits the Restoration
port's message getter applies (Mm3dRestorationPort::SetMessageEdits): per
message the original and edited bytes (zero-padded to the original's size)
with their FNV-1a hashes and the GMSG language bit. A message whose English
differs from the reviewed one is skipped and reported; no wording is
invented. The output holds the user's own text: keep it on their machine.
"""
import argparse
import json
from pathlib import Path
import re
import struct

from prepare_romfs_layer import RomFs

HERE = Path(__file__).resolve().parent
# Commands present in the reviewed messages. Unknown commands fail closed.
ARGS = {0: 0, 1: 0, 2: 0, 0x1e: 0, 0x25: 1, 0x27: 0, 0x28: 0,
        0x29: 1, 0x2b: 1, 0x2c: 0, 0x2f: 1, 0x31: 1, 0x34: 0, 0x3a: 1}
ICONS = {'A': 1, 'B': 2, 'X': 3, 'Y': 4, 'L': 5, 'R': 6, 'Dpad': 0x0a, 'Stick': 0x11}


def decode(raw):
    result, i = [], 0
    while i < len(raw):
        if raw[i] != 127:
            start = i
            while i < len(raw) and raw[i] != 127:
                i += 1
            result.append(raw[start:i].decode('utf-8'))
            continue
        i += 1
        if i & 1:
            if i >= len(raw) or raw[i] != 0:
                raise ValueError('Bad control alignment')
            i += 1
        if i + 2 > len(raw):
            raise ValueError('Truncated control')
        command = struct.unpack_from('<H', raw, i)[0]
        i += 2
        if command == 0x20:
            if i + 2 > len(raw):
                raise ValueError('Truncated plural control')
            argument = struct.unpack_from('<H', raw, i)[0]
            i += 2
            forms = []
            for _ in range(2):
                end = raw.find(b'\0', i)
                if end < 0:
                    raise ValueError('Truncated plural string')
                forms.append(raw[i:end])
                i = end + 1
            if i & 1:
                if i >= len(raw) or raw[i] != 0:
                    raise ValueError('Bad plural alignment')
                i += 1
            result.append((command, argument, *forms))
            continue
        if command == 0x32:
            if i != 4:
                if raw[i:i + 2] != b'\0\0':
                    raise ValueError('Bad extended control alignment')
                i += 2
            if i + 4 > len(raw):
                raise ValueError('Truncated extended control')
            result.append((command, struct.unpack_from('<I', raw, i)[0]))
            i += 4
            continue
        if command not in ARGS:
            raise ValueError(f'Unsupported command {command:x}')
        n = ARGS[command]
        if i + 2 * n > len(raw):
            raise ValueError('Truncated control argument')
        values = struct.unpack_from('<' + 'H' * n, raw, i)
        i += 2 * n
        result.append((command, *values))
        if command == 0:
            if any(raw[i:]):
                raise ValueError('Data beyond terminator')
            break
    if not result or result[-1] != (0,):
        raise ValueError('Missing end command')
    return result


def encode(tokens):
    result = bytearray()
    for token in tokens:
        if isinstance(token, str):
            result.extend(token.encode('utf-8'))
            continue
        result.append(127)
        if len(result) & 1:
            result.append(0)
        result.extend(struct.pack('<H', token[0]))
        if token[0] == 0x20:
            result.extend(struct.pack('<H', token[1]) + token[2] + b'\0' + token[3] + b'\0')
            if len(result) & 1:
                result.append(0)
        elif token[0] == 0x32:
            if len(result) != 4:
                result.extend(b'\0\0')
            result.extend(struct.pack('<I', token[1]))
        else:
            result.extend(struct.pack('<' + 'H' * (len(token) - 1), *token[1:]))
    return bytes(result)


def markup(text):
    tokens = []
    for part in re.split(r'(\{[^}]+\}|\n)', text):
        if not part:
            continue
        if part == '\n':
            tokens.append((1,))
        elif part.startswith('{Color:'):
            tokens.append((0x3a, int(part[7:-1])))
        elif part.startswith('{'):
            tokens.append((0x25, ICONS[part[1:-1]]))
        else:
            tokens.append(part)
    return tokens


def fnv(data):
    value = 0xcbf29ce484222325
    for b in data:
        value = ((value ^ b) * 0x100000001b3) & 0xffffffffffffffff
    return value


def messages(data):
    """{id: bytes} of a single-language GMSG, and its language bit."""
    if len(data) < 16 or len(data) > 0x1000000 or data[:8] != b'GMSG\x01\0\0\0':
        raise ValueError('Unsupported GMSG')
    count, languages = struct.unpack_from('<II', data, 8)
    if not 0 < count <= 20000 or 16 + 20 * count > len(data) or languages == 0 or languages & (languages - 1) or languages >= (1 << 14):
        raise ValueError('Expected a single-language GMSG')
    result, extents = {}, []
    previous_id = -1
    for i in range(count):
        at = 16 + 20 * i
        mid = struct.unpack_from('<H', data, at)[0]
        offset, size = struct.unpack_from('<II', data, at + 12)
        if offset < 16 + 20 * count or not size or offset + size > len(data) or offset & 1 or mid <= previous_id:
            raise ValueError('Invalid entry')
        previous_id = mid
        extents.append((offset, offset + size))
        result[mid] = data[offset:offset + size]
    ordered = sorted(extents)
    if any(left[1] > right[0] for left, right in zip(ordered, ordered[1:])):
        raise ValueError('Overlapping message allocations')
    return result, languages.bit_length() - 1


def compile_message(raw, edits):
    """dekui's edits of one message (compile_messages), or ValueError."""
    tokens = decode(raw)
    for edit in edits:
        matches = [i for i, t in enumerate(tokens) if isinstance(t, str) and edit['start'] in t]
        occurrence = edit.get('occurrence', 0)
        if type(occurrence) is not int or occurrence < 0:
            raise ValueError('Invalid occurrence')
        if len(matches) <= occurrence:
            raise ValueError(f'text not found: {edit["start"]!r}')
        start = matches[occurrence]
        prefix = tokens[start].split(edit['start'], 1)[0]
        end = start + 1
        while end < len(tokens) and not (isinstance(tokens[end], tuple) and tokens[end][0] in (0, 2, 0x2b, 0x31)):
            end += 1
        tokens[start:end] = ([prefix] if prefix else []) + markup(edit['text'])
    out = encode(tokens)
    if len(out) > len(raw):
        raise ValueError(f'replacement exceeds the original allocation ({len(out)}/{len(raw)} bytes)')
    flow = lambda ts: [t for t in ts if isinstance(t, tuple) and t[0] not in (1, 0x25, 0x3a)]
    if flow(tokens) != flow(decode(raw)):
        raise ValueError('dialogue flow changed')
    if decode(out) != tokens:
        raise ValueError('control round trip failed')
    return out + bytes(len(raw) - len(out))


def prepare(data, spec):
    originals, language = messages(data)
    # Historical specification is English, never silently apply it to DE/IT.
    allowed = spec.get('language_masks', [0x2, 0x10])
    if (1 << language) not in allowed:
        raise ValueError('Specification does not own this archive language')
    grouped = {}
    for edit in spec['edits']:
        grouped.setdefault(int(edit['id'], 16), []).append(edit)
    edits, skipped = [], []
    for mid, group in sorted(grouped.items()):
        if mid not in originals:
            skipped.append((mid, 'message missing'))
            continue
        raw = originals[mid]
        try:
            edited = compile_message(raw, group)
        except (ValueError, KeyError) as error:
            skipped.append((mid, str(error)))
            continue
        edits.append({'id': mid, 'language': language, 'original': raw.hex(), 'edited': edited.hex(),
                      'original_hash': f'{fnv(raw):016x}', 'edited_hash': f'{fnv(edited):016x}',
                      'price': False})
    return edits, skipped


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--romfs', type=Path, required=True,
                        help='the user\'s RomFS image (or extracted RomFS directory)')
    parser.add_argument('--romfs-offset', type=lambda v: int(v, 0), default=0x1000)
    parser.add_argument('--messages', default='message/eu/eue.gmsg',
                        help='the English GMSG inside the RomFS (default: EUR English)')
    parser.add_argument('--output', type=Path, required=True, help='message edits JSON to create')
    args = parser.parse_args()
    try:
        spec = json.loads((HERE / 'single-screen.json').read_text())
        edits, skipped = prepare(RomFs(args.romfs, args.romfs_offset).read(args.messages), spec)
        if args.output.exists():
            raise ValueError(f'{args.output} exists; refusing to overwrite')
        args.output.write_text(json.dumps({'format': 'triaevum_mm3d_message_edits_v1',
                                           'source': args.messages, 'edits': edits}, indent=1) + '\n')
    except (ValueError, OSError) as error:
        parser.exit(1, f'prepare_message_edits: {error}\n')
    for mid, reason in skipped:
        print(f'skipped {mid:04x}: {reason}')
    print(f'prepare_message_edits: {len(edits)} messages edited, {len(skipped)} skipped')
