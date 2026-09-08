from pathlib import Path
import struct
import tempfile
import unittest

from oot3d_region_assets import INVALID, adapt_qm_table, normalize_romfs


def qm_fixture():
    data = bytearray(16+96*2)
    struct.pack_into('<4sIII', data, 0, b'QM\0\0', 4, 2, 0)
    for slot, text in ((1, b'English'), (5, b'French'), (7, b'Spanish')):
        struct.pack_into('<II', data, 16+16+slot*8, len(data), len(text))
        data.extend(text)
    return bytes(data)


def romfs_fixture():
    # Small synthetic tree: /message/us/us.qm and /misc/us/texture.bin.
    names = ('', 'message', 'us', 'misc', 'us')
    dirs, offsets = bytearray(), []
    for name in names:
        offsets.append(len(dirs))
        encoded = name.encode('utf-16-le')
        dirs.extend(bytes(24)+encoded)
        dirs.extend(bytes(-len(dirs) % 4))
    files = bytearray()
    payload = bytearray()
    foffsets = []
    for parent, name, data in ((2, 'us.qm', qm_fixture()), (4, 'texture.bin', b'unchanged texture')):
        foffsets.append(len(files))
        encoded = name.encode('utf-16-le')
        files.extend(struct.pack('<IIQQII', offsets[parent], INVALID, len(payload), len(data), INVALID, len(encoded)))
        files.extend(encoded)
        files.extend(bytes(-len(files) % 4))
        payload.extend(data)
    # parent, sibling, first child directory, first child file
    tree = ((0, None, 1, None), (0, 3, 2, None), (1, None, None, 0),
            (0, None, 4, None), (3, None, None, 1))
    for index, (parent, sibling, child, file) in enumerate(tree):
        struct.pack_into('<6I', dirs, offsets[index], offsets[parent],
                         offsets[sibling] if sibling is not None else INVALID,
                         offsets[child] if child is not None else INVALID,
                         foffsets[file] if file is not None else INVALID,
                         INVALID, len(names[index].encode('utf-16-le')))
    # A single hash bucket deliberately exercises collisions in the rebuilt chains.
    dh, do = 40, 44
    fh, fo = do+len(dirs), do+len(dirs)+4
    content = (fo+len(files)+15) & ~15
    header = struct.pack('<10I', 40, dh, 4, do, len(dirs), fh, 4, fo, len(files), content)
    return header+bytes([255])*4+dirs+bytes([255])*4+files+bytes(content-fo-len(files))+payload


def lookup(image, path):
    """Use the hashed lookup contract, not the normalizer's tree traversal."""
    h = struct.unpack_from('<10I', image)
    parent = 0
    parts = path.split('/')
    for index, name in enumerate(parts):
        directory = index != len(parts)-1
        encoded = name.encode('utf-16-le')
        value = parent ^ 123456789
        for unit in struct.unpack('<'+'H'*(len(encoded)//2), encoded):
            value = ((value//32 | (value % 32)*2**27) ^ unit) & 0xffffffff
        hashes, size, table, fixed, chain = (h[1], h[2], h[3], 24, 16) if directory else (h[5], h[6], h[7], 32, 24)
        offset = struct.unpack_from('<I', image, hashes+4*(value % (size//4)))[0]
        seen = set()
        while offset != INVALID:
            if offset in seen:
                raise AssertionError('hash chain cycle')
            seen.add(offset)
            p = table+offset
            owner = struct.unpack_from('<I', image, p)[0]
            length = struct.unpack_from('<I', image, p+fixed-4)[0]
            if owner == parent and image[p+fixed:p+fixed+length] == encoded:
                if directory:
                    parent = offset
                    break
                start, length = struct.unpack_from('<QQ', image, p+8)
                return image[h[9]+start:h[9]+start+length]
            offset = struct.unpack_from('<I', image, p+chain)[0]
        else:
            raise KeyError(path)
    raise KeyError(path)


class RegionAssetTests(unittest.TestCase):
    def test_qm_maps_references_without_replacing_payload(self):
        data = qm_fixture()
        end = 16+96*2
        result = adapt_qm_table(data[:end], len(data))
        for destination, source in ((2, 1), (4, 5), (6, 7)):
            self.assertEqual(result[32+destination*8:40+destination*8], data[32+source*8:40+source*8])
        for slot in (0, 3, 8):
            self.assertEqual(result[32+slot*8:40+slot*8], bytes(8))
        self.assertEqual(result[112:], data[112:end])  # Empty message stays empty.

    def test_qm_rejects_conflicts_and_out_of_range_references(self):
        for offset, size in ((1, 8), (10000, 3), (0, 1)):
            data = bytearray(qm_fixture())
            struct.pack_into('<II', data, 40, offset, size)
            with self.assertRaises(ValueError):
                adapt_qm_table(data[:208], len(data))
        data = bytearray(qm_fixture())
        data[48:56] = data[72:80]  # EU English already points at a different language.
        with self.assertRaisesRegex(ValueError, 'already populated'):
            adapt_qm_table(data[:208], len(data))

    def test_service_view_hashes_paths_and_payload_conservation(self):
        for prefix in (b'', b'IVFC'+bytes(4092)):
            with self.subTest(ivfc=bool(prefix)), tempfile.TemporaryDirectory() as temp:
                source, output = Path(temp)/'source', Path(temp)/'output'
                original = prefix+romfs_fixture()
                source.write_bytes(original)
                result = normalize_romfs(source, output)
                data = output.read_bytes()
                self.assertEqual(struct.unpack_from('<I', data)[0], 40)
                self.assertEqual(source.read_bytes(), original)
                qm = lookup(data, 'message/eu/eu.qm')
                self.assertEqual(qm[208:], qm_fixture()[208:])
                self.assertEqual(lookup(data, 'misc/eu/texture.bin'), b'unchanged texture')
                with self.assertRaises(KeyError):
                    lookup(data, 'message/us/us.qm')
                self.assertEqual(result['files'], 2)
                with self.assertRaises(ValueError):
                    normalize_romfs(source, output)

    def test_malformed_romfs_does_not_publish_output(self):
        for damage in ('cycle', 'overlap', 'data', 'truncated'):
            with self.subTest(damage=damage), tempfile.TemporaryDirectory() as temp:
                data = bytearray(romfs_fixture())
                h = struct.unpack_from('<10I', data)
                if damage == 'cycle':
                    struct.pack_into('<I', data, h[3]+8, 0)
                elif damage == 'overlap':
                    struct.pack_into('<I', data, 5*4, h[3])
                elif damage == 'data':
                    struct.pack_into('<I', data, 9*4, 16)
                else:
                    data = data[:100]
                source, output = Path(temp)/'source', Path(temp)/'output'
                source.write_bytes(data)
                with self.assertRaises(ValueError):
                    normalize_romfs(source, output)
                self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
