# SPDX-License-Identifier: GPL-2.0-or-later
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest

from prepare_message_edits import decode, encode, messages, prepare
from prepare_install import compile_messages, LANGUAGES, price_edits, publish, sha
from romfs_overlay import build_overlay, name_hash
from romfs_mods import romfs_file_records


def archive(mask=0x10, text='Price 10 coins', metadata=0xfffffc0a):
    raw = encode([text, (1,), (0x25, 1), (0,)]) + bytes(24)
    return (struct.pack('<4sIII', b'GMSG', 1, 1, mask) +
            struct.pack('<HHIIII', 62, 0, metadata, 0, 36, len(raw)) + raw)


class PreparationTests(unittest.TestCase):
    def test_dialogue_mask_and_capacity(self):
        spec = {'edits': [{'id': '003e', 'start': 'Price', 'text': 'Select'}]}
        rows, skipped = prepare(archive(), spec)
        self.assertEqual(skipped, [])
        self.assertEqual(rows[0]['language'], 4)
        with self.assertRaises(ValueError):
            prepare(archive(0x20), spec)
        spec['language_masks'] = [0x20]
        self.assertEqual(prepare(archive(0x20), spec)[0][0]['language'], 5)
        spec['edits'][0]['text'] = 'X' * 500
        self.assertTrue(prepare(archive(0x20), spec)[1])
        spec['edits'][0]['occurrence'] = -1
        self.assertTrue(prepare(archive(0x20), spec)[1])

    def test_gmsg_bounds(self):
        for size in range(36):
            with self.subTest(size=size), self.assertRaises(ValueError):
                messages(archive()[:size])
        for offset, value in [(8, 20001), (12, 0), (12, 3), (12, 1 << 14), (28, 15), (32, 0xffffffff)]:
            data = bytearray(archive())
            struct.pack_into('<I', data, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                messages(data)

    def test_controls(self):
        for tokens in [[(0x32, 0x12345678), 'hello', (0,)],
                       ['prefix', (0x32, 42), (0x20, 1, b'one', b'many'), (0,)],
                       [(0x2f, 2), (0x34,), (0x1e,), (0,)]]:
            self.assertEqual(decode(encode(tokens)), tokens)
        for bad in [b'\x7f', b'\x7f\0', b'\x7f\0\x25\0', b'\x7f\0\x20\0\0\0x', b'\x7f\0\x32\0\0']:
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                decode(bad)

    def test_reviewed_price_metadata_and_flow(self):
        for mask in [x[1] for x in LANGUAGES.values()]:
            data = archive(mask)
            raw = messages(data)[0][62]
            spec = {'language_mask': mask, 'prices': [{'id': '003e', 'original_sha256': sha(raw),
                    'original_metadata': 0xfffffc0a, 'double_fields': [0, 10, 20],
                    'replacements': [{'old': '10', 'new': '20'}]}]}
            result = price_edits(data, spec)[0]
            self.assertEqual(result['edited_metadata'], 0xfffffc14)
            self.assertEqual(result['original_metadata'], 0xfffffc0a)
            self.assertTrue(result['price'])
            self.assertEqual(len(bytes.fromhex(result['edited'])), len(raw))
            self.assertEqual([x for x in decode(bytes.fromhex(result['edited'])) if not isinstance(x, str)],
                             [x for x in decode(raw) if not isinstance(x, str)])
            spec['prices'][0]['original_metadata'] = 0
            with self.assertRaises(ValueError):
                price_edits(data, spec)
            spec['prices'][0]['original_metadata'] = 0xfffffc0a
            spec['prices'][0]['replacements'][0]['new'] = '\x7f'
            with self.assertRaises(ValueError):
                price_edits(data, spec)

    def test_five_languages_explicit_coverage(self):
        class Rom:
            def read(self, path):
                return archive(next(mask for name, mask in LANGUAGES.values() if name in path))
        spec = {'en': {'edits': [{'id': '003e', 'start': 'Price', 'text': 'Choose'}]}}
        result, coverage = compile_messages(Rom(), spec)
        self.assertEqual(len(result['edits']), 1)
        self.assertEqual(len(coverage), 5)
        self.assertEqual(coverage['de']['dialogue'], 'not-prepared')
        self.assertEqual(coverage['it']['prices'], 'not-prepared')

    def test_overlay_addition_and_replacement(self):
        # Entirely synthetic Level-3 view: root/layout/old.bin.
        invalid = 0xffffffff
        dirs = (struct.pack('<6I', 0, invalid, 24, invalid, invalid, 0) +
                struct.pack('<6I', 0, invalid, invalid, 0, invalid, 12) + 'layout'.encode('utf-16-le'))
        name = 'old.bin'.encode('utf-16-le')
        files = struct.pack('<IIQQII', 24, invalid, 0, 3, invalid, len(name)) + name + bytes(2)
        header = struct.pack('<10I', 40, 40, 4, 44, len(dirs), 104, 4, 108, len(files), 160)
        base = header + struct.pack('<I', 0) + dirs + struct.pack('<I', 0) + files + bytes(4) + b'old'
        self.assertEqual(len(base), 163)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            image, output = root / 'romfs.bin', root / 'overlay.o3ro'
            image.write_bytes(base)
            (root / 'old').write_bytes(b'replacement')
            (root / 'new').write_bytes(b'addition')
            result = build_overlay(image, service_offset=0, service_size=len(base),
                                   replacements={'layout/old.bin': root / 'old', 'layout/new.bin': root / 'new'}, output=output)
            self.assertEqual(result['added_files'], 1)
            overlay = output.read_bytes()
            magic, version, size, virtual, count, _, tail = struct.unpack_from('<4sIQQIIQ', overlay)
            self.assertEqual((magic, version, size), (b'O3RO', 1, len(base)))
            view = bytearray(base + overlay[tail:])
            self.assertEqual(len(view), virtual)
            for i in range(count):
                at, length, _, expected, patched = struct.unpack_from('<QIIQQ', overlay, 64 + i * 32)
                self.assertEqual(view[at:at + length], overlay[expected:expected + length])
                view[at:at + length] = overlay[patched:patched + length]
            h, records = romfs_file_records(io.BytesIO(view), 0, len(view))
            for path, expected in [('layout/old.bin', b'replacement'), ('layout/new.bin', b'addition')]:
                _, at, length = records[path]
                self.assertEqual(view[h[9] + at:h[9] + at + length], expected)
            new_record = records['layout/new.bin'][0]
            self.assertEqual(struct.unpack_from('<I', view, h[5])[0], new_record)
            self.assertEqual(struct.unpack_from('<I', view, h[7] + new_record + 24)[0], 0)
            with self.assertRaises(ValueError):
                build_overlay(image, service_offset=0, service_size=len(base),
                              replacements={'missing/new.bin': root / 'new'}, output=output)

    def test_atomic_cache_repair_and_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            def build(stage):
                (stage / 'message_edits.json').write_text('{}')
                (stage / 'romfs_overlay.o3ro').write_bytes(b'fixture')
                return {'languages': {}}
            first, reused = publish(output, {'title': 'mm3d', 'input': 'one'}, build)
            self.assertFalse(reused)
            self.assertEqual(publish(output, first['identity'], build), (first, True))
            (output / first['generation'] / 'message_edits.json').write_text('corrupt')
            repaired, reused = publish(output, first['identity'], build)
            self.assertFalse(reused)
            self.assertNotEqual(first['generation'], repaired['generation'])
            previous = (output / 'prepared.json').read_bytes()
            def fail(stage):
                (stage / 'partial').write_text('partial')
                raise RuntimeError('cancelled')
            with self.assertRaises(RuntimeError):
                publish(output, {'title': 'wrong-title'}, fail)
            self.assertEqual((output / 'prepared.json').read_bytes(), previous)
            self.assertFalse(list(output.glob('.prepare-*')))
            changed, reused = publish(output, {'title': 'mm3d', 'input': 'two'}, build)
            self.assertFalse(reused)
            self.assertNotEqual(changed['generation'], repaired['generation'])


if __name__ == '__main__':
    unittest.main()
