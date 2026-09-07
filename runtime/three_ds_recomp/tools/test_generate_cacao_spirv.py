import struct
import unittest

from generate_cacao_spirv import STORAGE_FORMATS, annotate_storage_formats, verify_storage_formats


def module(format_id):
    def instruction(opcode, *args):
        return [((len(args) + 1) << 16) | opcode, *args]

    name = b"g_PrepareNormals_NormalOut\0"
    name += b"\0" * (-len(name) % 4)
    words = [0x07230203, 0x00010300, 0, 8, 0]
    words += instruction(5, 4, *struct.unpack(f"<{len(name) // 4}I", name))
    words += instruction(25, 2, 1, 1, 0, 1, 0, 2, format_id)
    words += instruction(32, 3, 0, 2)
    words += instruction(59, 3, 4, 0)
    return struct.pack(f"<{len(words)}I", *words)


class CacaoStorageFormatsTest(unittest.TestCase):
    def test_decodes_actual_spirv_storage_image_format(self):
        verify_storage_formats(module(5))

    def test_rejects_the_former_rgba32f_normal_output(self):
        with self.assertRaisesRegex(ValueError, "format mismatch"):
            verify_storage_formats(module(1))

    def test_rejects_unknown_untyped_format(self):
        with self.assertRaisesRegex(ValueError, "format mismatch"):
            verify_storage_formats(module(0))

    def test_annotates_all_bindings_and_preserves_integer_texel_addresses(self):
        source = "\n".join(f"RWTexture2D<float> {name} : register(u0);" for name in STORAGE_FORMATS)
        for texture in ("g_DepthIn", "g_BilateralUpscaleDepth"):
            source += f"\nreturn {texture}.Load(int3(coord, 0), offset);"
        result = annotate_storage_formats(source)
        self.assertEqual(result.count("[[vk::image_format("), len(STORAGE_FORMATS))
        self.assertIn('[[vk::image_format("rgba8snorm")]]', result)
        self.assertEqual(result.count("Load(int3(coord + offset, 0))"), 2)
        with self.assertRaisesRegex(ValueError, "Unexpected CACAO storage binding"):
            annotate_storage_formats(source + "\nRWTexture2D<float> newOutput : register(u0);")


if __name__ == "__main__":
    unittest.main()
