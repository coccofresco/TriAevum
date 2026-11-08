import struct
import unittest
from unittest.mock import patch
import scene_emitters as mm


def model10(texture="room_mado_01", additive=False):
    def chunk(tag, body):
        return tag + struct.pack("<I", len(body) + 8) + body
    material = bytearray(0x16C)
    for k, index in enumerate((0, -1, -1)):
        struct.pack_into("<h", material, 0x10 + k * 0x18, index)
    if additive:
        struct.pack_into("<I", material, 0x138, 1)
        struct.pack_into("<H", material, 0x13E, 1)
    mats = chunk(b"mats", struct.pack("<I", 1) + material)
    pixels = bytes([127, 255, 11, 41]) * 64
    tex = chunk(b"tex ", struct.pack("<I", 1) + struct.pack("<IHHHHHHI16s", len(pixels), 1, 1, 8, 8,
                0x6752, 0x1401, 0, texture.encode()))
    skl = chunk(b"skl ", struct.pack("<IIhh9fI", 1, 0, 0, -1, 1, 1, 1, 0, 0, 0, 10, 20, 30, 0))
    # One rigid triangle; v10's tangent makes nine attributes and moves UV0.
    sepd = bytearray(0x128)
    sepd[:4] = b"sepd"
    struct.pack_into("<HH", sepd, 8, 1, 1)
    struct.pack_into("<IfHH", sepd, 0x24, 0, 1.0, 0x1406, 0)
    struct.pack_into("<H", sepd, 0x124, len(sepd))
    prms = bytearray(0x1C)
    prms[:4] = b"prms"
    struct.pack_into("<IHHII", prms, 8, 1, 0, 1, 0x18, len(prms))
    prm = bytearray(0x18)
    prm[:4] = b"prm "
    struct.pack_into("<IIHHHH", prm, 8, 1, 0, 0x1403, 0, 3, 0)
    mshs = chunk(b"mshs", struct.pack("<IIHBBII", 1, 0, 0, 0, 0, 0, 0))
    shp = chunk(b"shp ", struct.pack("<IIHH", 1, 0, 0x14, 0) + sepd + prms + prm)
    sklm = chunk(b"sklm", struct.pack("<II", 16, 16 + len(mshs)) + mshs + shp)
    vatr = bytearray(0x54)
    vatr[:4] = b"vatr"
    struct.pack_into("<II", vatr, 12, 36, len(vatr))
    vatr.extend(struct.pack("<9f", 0, 0, 0, 100, 0, 0, 0, 100, 0))
    indices = struct.pack("<3H", 0, 1, 2)
    parts = [skl, b"", mats, tex, sklm, b"", bytes(vatr), indices, pixels]
    header, body, offsets = bytearray(0x48), bytearray(), []
    for part in parts:
        offsets.append(len(header) + len(body))
        body.extend(part)
        body.extend(bytes(-len(body) % 4))
    header[:4] = b"cmb "
    struct.pack_into("<II", header, 4, len(header) + len(body), 10)
    struct.pack_into("<10I", header, 0x20, 3, *offsets)
    return bytes(header + body), pixels


def room_header():
    return bytearray(b"ZSI\x09" + bytes(12) + bytes([1, 3, 0, 0, 64, 0, 0, 0]) +
                     bytes([10, 0, 0, 0, 80, 0, 0, 0]) + bytes([20]) + bytes(7))


class MmSceneEmitterTests(unittest.TestCase):
    def test_native_room_relocations_do_not_change_identity(self):
        rom = room_header()
        native = rom.copy()
        native[18:24] = bytes([1, 0, 0x22, 0x33, 0x44, 9])
        native[26:32] = bytes([1, 0, 0x55, 0x66, 0x77, 9])
        self.assertEqual(mm.room_fingerprint(rom), mm.room_fingerprint(native))
        native[17] += 1
        self.assertNotEqual(mm.room_fingerprint(rom), mm.room_fingerprint(native))
        self.assertEqual(mm.room_fingerprint(rom), mm.room_fingerprint(rom + b"runtime-patched model"))

    def test_unknown_or_unterminated_room_fails_closed(self):
        for data in (b"", b"ZSI\x01" + bytes(100), b"ZSI\x09" + bytes(268)):
            with self.assertRaises(ValueError):
                mm.room_fingerprint(data)

    def test_authored_categories_not_fullbright_or_image_statistics(self):
        expected = {"room_mado_01": "window-lamp", "room_lamp_01": "lamp", "room_hono_01": "fire-lava",
                    "room_hikari_01": "light-shaft", "room_magic_01": "magic-glow", "room_door_light": "doorway"}
        for name, category in expected.items():
            self.assertEqual(mm.category(name, True), category)
        for name in ("gs_kamado_01", "room_torch_01", "syokudai_fire", "water_light", "unknown_white", "sky", "hud"):
            self.assertIsNone(mm.category(name, True))
        # Opaque: not a doorway; its "light" name is a light shaft as in OoT3D.
        self.assertEqual(mm.category("room_door_light", False), "light-shaft")

    def test_light_names_follow_oot3d_light_shaft_recipe(self):
        # MM3D authored additive glow cards seen in the EUR room census.
        for name in ("ka_dailight_01", "kksi_light_01", "mn_light_03", "zse_bos_light_0", "deku_mini_light"):
            self.assertEqual(mm.category(name, True), "light-shaft")
        for name in ("zk_water_light", "room_torch_light", "knk_shadow_light"):
            self.assertIsNone(mm.category(name, True))
        data, pixels = model10("mn_light_03", additive=True)
        with patch.object(mm.shared, "texels", side_effect=AssertionError("image analysis forbidden")):
            emitters = mm.model_emitters(mm.Model(data, 0))
        self.assertEqual([e["category"] for e in emitters], ["light-shaft"])
        self.assertEqual(emitters[0]["surfaces"], [f"{mm.content_hash(pixels):016x}"])

    def test_v10_rigid_geometry_bones_and_native_blend_offsets(self):
        data, pixels = model10(additive=True)
        model = mm.Model(data, 0)
        self.assertTrue(model.materials[0][1])
        self.assertEqual(model.textures[0][-1], pixels)
        self.assertEqual([point for _, point in model.triangles(0)[0]], [(10, 20, 30), (110, 20, 30), (10, 120, 30)])
        with patch.object(mm.shared, "texels", side_effect=AssertionError("image analysis forbidden")):
            emitters = mm.model_emitters(model)
        self.assertEqual(len(emitters), 1)
        self.assertEqual(emitters[0]["category"], "window-lamp")
        self.assertEqual(emitters[0]["surfaces"], [f"{mm.content_hash(pixels):016x}"])

    def test_torch_stand_and_unknown_fullbright_are_not_emitters(self):
        for texture in ("room_torch_01", "unknown_white"):
            data, _ = model10(texture)
            self.assertEqual(mm.model_emitters(mm.Model(data, 0)), [])

    def test_truncated_or_wrong_version_model_is_rejected(self):
        data, _ = model10()
        for bad in (data[:50], data[:8] + struct.pack("<I", 6) + data[12:]):
            with self.assertRaises(ValueError):
                mm.Model(bad, 0)

    def test_literal_lzs_and_bad_extent(self):
        raw = b"abc"
        packed = b"LzS\1" + bytes(4) + struct.pack("<II", 3, 4) + b"\x07abc"
        self.assertEqual(mm.decompress(packed), raw)
        with self.assertRaises(ValueError):
            mm.decompress(packed[:-1])


if __name__ == "__main__":
    unittest.main()
