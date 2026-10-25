# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic fixtures and the Python Forge's outputs for them.

usage: asset_cases.py REPOSITORY OUTPUT {assets|numerics}

`assets` writes fixtures under OUTPUT/fixtures, the Python outputs under
OUTPUT/expected and the case list in OUTPUT/cases.json. `numerics` writes
CPython float results (as IEEE bit patterns) to OUTPUT. No ROM data is used:
the fixtures are generated, the TopScreen assets are the ones TriAevum ships.
"""

import json
import math
import random
import shutil
import struct
import subprocess
import sys
from pathlib import Path

REPOSITORY = Path(sys.argv[1]).resolve()
OUTPUT = Path(sys.argv[2]).resolve()
sys.path[:0] = [str(REPOSITORY / "tools" / "triaevum_release"), str(REPOSITORY)]

RGBA8 = (0x6752, 0x1401)
MATERIAL_SIZE = 0x15C
CASES = []


def bits(value):
    return struct.unpack("<Q", struct.pack("<d", value))[0]


# --- numerics -----------------------------------------------------------------------------------------------

def numerics():
    rng = random.Random(20261025)
    def number():
        kind = rng.randrange(6)
        if kind == 0:
            return rng.uniform(-1e4, 1e4)
        if kind == 1:
            return rng.randrange(-4000, 4000) / 8.0
        if kind == 2:
            return rng.choice([0.0, -0.0, 1e16, -1e16, 1.0, 0.1, 1e-300, 5e-324, 1e300])
        if kind == 3:
            return rng.random() * 10.0 ** rng.randrange(-20, 20) * rng.choice([-1, 1])
        if kind == 4:
            return rng.randrange(-10 ** 6, 10 ** 6) / 1000.0 + 0.0005
        return float(rng.randrange(-5000, 5000))
    cases = {"sum": [], "round1": [], "round3": [], "round": [], "hypot": [], "dist": [], "floordiv": [],
             "mod": [], "cos": [], "sin": [], "atan2": [], "pow2": [], "sqrt": []}
    for _ in range(20000):
        values = [number() for _ in range(rng.randrange(1, 9))]
        cases["sum"].append([[bits(v) for v in values], bits(sum(values))])
        x = number()
        cases["round1"].append([bits(x), bits(round(x, 1))])
        cases["round3"].append([bits(x), bits(round(x, 3))])
        if math.isfinite(x) and abs(x) < 1e15:
            cases["round"].append([bits(x), round(x)])
        a, b = number(), number()
        cases["hypot"].append([[bits(a), bits(b)], bits(math.hypot(a, b))])
        p, q = [number() for _ in range(3)], [number() for _ in range(3)]
        cases["dist"].append([[bits(v) for v in p + q], bits(math.dist(p, q))])
        cases["floordiv"].append([bits(x), bits(x // 600)])
        cases["mod"].append([bits(x), bits(x % 1.0)])
        angle = rng.uniform(-10.0, 10.0) if rng.random() < 0.8 else number()
        cases["cos"].append([bits(angle), bits(math.cos(angle))])
        cases["sin"].append([bits(angle), bits(math.sin(angle))])
        cases["atan2"].append([[bits(a), bits(b)], bits(math.atan2(a, b))])
        small = rng.uniform(-1.0, 1.0)
        cases["pow2"].append([bits(small), bits(small ** 2)])
        cases["sqrt"].append([bits(abs(x)), bits(math.sqrt(abs(x)))])
    OUTPUT.write_text(json.dumps(cases))


# --- RomFS images -------------------------------------------------------------------------------------------

def romfs_image(files):
    """A Level-3 RomFS image of {"a/b/c": data}, nested directories included."""
    def name(text):
        encoded = text.encode("utf-16-le")
        return encoded + b"\0" * (-len(encoded) % 4)

    tree = {"dirs": {}, "files": []}
    for path, data in files.items():
        node = tree
        *directories, file_name = path.split("/")
        for directory in directories:
            node = node["dirs"].setdefault(directory, {"dirs": {}, "files": []})
        node["files"].append((file_name, data))
    order = []

    def walk(node, label, parent):
        order.append((node, label, parent))
        for child, sub in node["dirs"].items():
            walk(sub, child, node)
    walk(tree, "", None)
    offsets, offset = {}, 0
    for node, label, _ in order:
        offsets[id(node)] = offset
        offset += 24 + len(name(label))
    file_offsets, offset = {}, 0
    for node, _, _ in order:
        for file_name, _ in node["files"]:
            file_offsets[(id(node), file_name)] = offset
            offset += 32 + len(name(file_name))
    directory_table, file_table, file_data = b"", b"", b""
    none = 0xFFFFFFFF
    for node, label, parent in order:
        siblings = list(parent["dirs"].values()) if parent else [node]
        index = siblings.index(node)
        sibling = offsets[id(siblings[index + 1])] if index + 1 < len(siblings) else none
        children = list(node["dirs"].values())
        child = offsets[id(children[0])] if children else none
        first = file_offsets[(id(node), node["files"][0][0])] if node["files"] else none
        directory_table += struct.pack("<6I", offsets[id(parent)] if parent else 0, sibling, child, first, none,
                                       len(label) * 2) + name(label)
        for position, (file_name, data) in enumerate(node["files"]):
            following = (file_offsets[(id(node), node["files"][position + 1][0])]
                         if position + 1 < len(node["files"]) else none)
            file_table += struct.pack("<IIQQII", offsets[id(node)], following, len(file_data), len(data), none,
                                      len(file_name) * 2) + name(file_name)
            file_data += data
            file_data += b"\0" * (-len(file_data) % 16)
    directory_meta = 0x28
    file_meta = directory_meta + len(directory_table)
    data_start = file_meta + len(file_table)
    header = struct.pack("<10I", 0x28, 0, 0, directory_meta, len(directory_table), 0, 0, file_meta,
                         len(file_table), data_start)
    return header + directory_table + file_table + file_data


def write(relative, data):
    path = OUTPUT / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def write_tree(relative, files):
    for path, data in files.items():
        write(f"{relative}/{path}", data)
    return OUTPUT / relative


def rel(path):
    return Path(path).resolve().relative_to(OUTPUT).as_posix()


def tree_files(root):
    return {path.relative_to(root).as_posix(): rel(path) for path in sorted(root.rglob("*")) if path.is_file()}


# --- water --------------------------------------------------------------------------------------------------

def water_cases():
    import test_water_textures as fixtures
    import water_textures
    rng = random.Random(7)
    lake = bytes(range(256)) * 4
    fall = bytes(reversed(range(256))) * 4
    room = b"ZSI\x09" + b"\0" * 60 + fixtures.cmb([("spot_water", 32, 32, lake), ("spot_water_kabe", 32, 32, b"\1" * 1024)])
    actor = fixtures.zar([("model.cmb", fixtures.cmb([("hylia_taki_01", 32, 32, fall), ("riv_water", 32, 32, fall)]))])
    names = ["spot_water", "hylia_water_01", "kawa_02", "s04_kawa_01", "uvwater_01", "wtr_kabe_01", "kasumi_water",
             "ydan_mizu_x", "sea01", "WATE", "bmizu_l", "taki_ani", "riv_side", "labo_awa_01", "grass_01", "hwat_q"]
    files = {"scene/spot04_info.zsi": room, "scene/spot04_0_info.zsi": b"ZSI\x09", "actor/zelda_spot.zar": actor,
             "actor/readme.txt": b"cmb ", "scene/extra.zar": actor}
    for index in range(12):
        textures = []
        for _ in range(rng.randrange(1, 5)):
            size = rng.choice([8, 16, 32])
            data = bytes(rng.randrange(256) for _ in range(size * size))
            textures.append((rng.choice(names), size, size, data if rng.random() < 0.7 else lake))
        files[f"actor/random_{index}.zar"] = fixtures.zar([(f"m{index}.cmb", fixtures.cmb(textures))])
    romfs = write_tree("fixtures/water/romfs", files)
    image = romfs_image(files)
    level3 = write("fixtures/water/level3.bin", image)
    ivfc = write("fixtures/water/ivfc.bin", b"IVFC" + b"\0" * 0xFFC + image)
    for label, source in (("directory", romfs), ("level3", level3), ("ivfc", ivfc)):
        payload, summary = water_textures.build_catalog(source)
        CASES.append({"kind": "water_catalog", "name": label, "romfs": rel(source),
                      "expected": rel(write(f"expected/water/{label}.txt", payload)), "summary": summary})
    data = OUTPUT / "expected/water/prepared"
    water_textures.prepare_water_textures(data_root=data, recipe={"inputs": {"romfs": {"sha256": "a" * 64}}},
                                          romfs=level3)
    CASES.append({"kind": "prepare_water", "romfs": rel(level3), "recipe": {"inputs": {"romfs": {"sha256": "a" * 64}}},
                  "files": tree_files(data)})


# --- scene lights -------------------------------------------------------------------------------------------

def cmb(materials, textures, shapes, bones=((0, -1, (1, 1, 1), (0, 0, 0), (0, 0, 0)),)):
    """A version-6 CMB (extends test_scene_lights.cmb): materials [(texture,
    additive, (su, sv, tu, tv))], textures [(name, (format, type), width,
    height, data)], shapes [(material, points, triangles, options)] where
    options may set "kind" (0x1406 float or 0x1402 short with "scale"),
    "uvs" (per point, float) and "bone"; bones [(index, parent, scale,
    rotation, translation)]."""
    def chunk(magic, body):
        return magic + struct.pack("<I", 8 + len(body)) + body

    mats = bytearray(struct.pack("<I", len(materials)))
    for texture, additive, uv in materials:
        material = bytearray(MATERIAL_SIZE)
        struct.pack_into("<hhh", material, 0x10, texture, -1, -1)
        struct.pack_into("<h", material, 0x10 + 0x18, -1)
        struct.pack_into("<h", material, 0x10 + 0x30, -1)
        struct.pack_into("<5f", material, 0x5C, uv[0], uv[1], 0.0, uv[2], uv[3])
        if additive:
            struct.pack_into("<I", material, 0x138, 1)
            struct.pack_into("<H", material, 0x13E, 1)
        mats += material
    tex_entries, texture_data = b"", b""
    for name, (fmt, kind), width, height, data in textures:
        tex_entries += struct.pack("<IHHHHHHI16s", len(data), 1, 0, width, height, fmt, kind, len(texture_data),
                                   name.encode("ascii"))
        texture_data += data
        texture_data += b"\0" * (-len(texture_data) % 4)
    indices, positions, uvs = b"", b"", b""
    meshes, sepds = b"", []
    for index, (material, points, triangles, options) in enumerate(shapes):
        meshes += struct.pack("<HBB", index, material, 0)
        first = len(indices) // 2
        flat = [i for tri in triangles for i in tri]
        indices += struct.pack(f"<{len(flat)}H", *flat)
        kind = options.get("kind", 0x1406)
        scale = options.get("scale", 1.0)
        position_offset = len(positions)
        if kind == 0x1406:
            positions += b"".join(struct.pack("<3f", *p) for p in points)
        else:
            positions += b"".join(struct.pack("<3h", *(round(c / scale) for c in p)) for p in points)
        positions += b"\0" * (-len(positions) % 4)
        uv_offset = len(uvs)
        if "uvs" in options:
            uvs += b"".join(struct.pack("<2f", *uv) for uv in options["uvs"])
        sepds.append((first, len(flat), position_offset, kind, scale, uv_offset, "uvs" in options,
                      options.get("bone", 0)))
    mshs = chunk(b"mshs", struct.pack("<II", len(shapes), 0) + meshes)
    shp_head_size = 0x10 + 2 * len(shapes)
    shp_head_size += -shp_head_size % 4
    bodies, offsets = b"", []
    for first, count, position_offset, kind, scale, uv_offset, has_uvs, bone in sepds:
        sepd = bytearray(0x10C)
        sepd[0:4] = b"sepd"
        struct.pack_into("<HH", sepd, 8, 1, 1 | (8 if has_uvs else 0))
        struct.pack_into("<IfHH", sepd, 0x24, position_offset, scale, kind, 0)
        struct.pack_into("<IfHH", sepd, 0x78, uv_offset, 1.0, 0x1406, 0)
        struct.pack_into("<2f", sepd, 0x84, 0.25, 0.75)
        struct.pack_into("<H", sepd, 0x108, len(sepd))
        prms = bytearray(0x1C)
        prms[0:4] = b"prms"
        struct.pack_into("<IHHII", prms, 8, 1, 0, 1, 0x18, len(prms))
        struct.pack_into("<H", prms, 0x18, bone)
        prm = bytearray(0x18)
        prm[0:4] = b"prm "
        struct.pack_into("<IIHHHH", prm, 8, 1, 0, 0x1403, 0, count, first)
        offsets.append(shp_head_size + len(bodies))
        bodies += bytes(sepd) + bytes(prms) + bytes(prm)
    shp = bytearray(shp_head_size)
    shp[0:4] = b"shp "
    struct.pack_into("<II", shp, 4, shp_head_size + len(bodies), len(shapes))
    for i, offset in enumerate(offsets):
        struct.pack_into("<H", shp, 0x10 + 2 * i, offset)
    shp = bytes(shp) + bodies
    sklm_head = 0x10
    sklm = b"sklm" + struct.pack("<III", sklm_head + len(mshs) + len(shp), sklm_head, sklm_head + len(mshs))
    sklm += mshs + shp
    vatr = bytearray(0x2C)
    vatr[0:4] = b"vatr"
    struct.pack_into("<II", vatr, 4, 0x2C + len(positions) + len(uvs), 0)
    struct.pack_into("<II", vatr, 0x0C, len(positions), 0x2C)
    struct.pack_into("<II", vatr, 0x24, len(uvs), 0x2C + len(positions))
    vatr = bytes(vatr) + positions + uvs
    skl = chunk(b"skl ", struct.pack("<II", len(bones), 0) + b"".join(
        struct.pack("<hh9f", index, parent, *scale, *rotation, *translation)
        for index, parent, scale, rotation, translation in bones))
    parts = [("skl", skl), ("mats", chunk(b"mats", bytes(mats))),
             ("tex", chunk(b"tex ", struct.pack("<I", len(textures)) + tex_entries)),
             ("sklm", sklm), ("vatr", vatr), ("indices", indices), ("texdata", texture_data)]
    offset, layout, body = 0x44, {}, b""
    for name, data in parts:
        layout[name] = offset
        body += data + b"\0" * (-len(data) % 4)
        offset = 0x44 + len(body)
    header = bytearray(0x44)
    header[0:4] = b"cmb "
    struct.pack_into("<II", header, 4, 0x44 + len(body), 6)
    struct.pack_into("<9I", header, 0x20, len(indices) // 2, layout["skl"], layout["mats"], layout["tex"],
                     layout["sklm"], 0, layout["vatr"], layout["indices"], layout["texdata"])
    return bytes(header) + body


def box(low, high):
    points = [(x, y, z) for x in (low[0], high[0]) for y in (low[1], high[1]) for z in (low[2], high[2])]
    triangles = [(0, 1, 3), (0, 3, 2), (4, 6, 7), (4, 7, 5), (0, 4, 5), (0, 5, 1),
                 (2, 3, 7), (2, 7, 6), (0, 2, 6), (0, 6, 4), (1, 5, 7), (1, 7, 3)]
    return points, triangles


def stored_rgba(width, value):
    """A width x width RGBA8 texture in PICA 8x8 Morton tiles."""
    texels = [b"\0\0\0\0"] * (width * width)
    tiles = width // 8
    for y in range(width):
        for x in range(width):
            tile = (y // 8) * tiles + x // 8
            m = sum((((x % 8) >> i) & 1) << (2 * i) | (((y % 8) >> i) & 1) << (2 * i + 1) for i in range(3))
            level = max(0, min(255, round(255 * value(x, y))))
            texels[tile * 64 + m] = bytes([255, level, level, level])
    return b"".join(texels)


def texture(rng, name):
    choice = rng.randrange(12)
    if choice == 0:
        return (name, (0x675A, 0), 16, 16, bytes(rng.randrange(256) for _ in range(128)))
    if choice == 1:
        return (name, (0x675B, 0), 16, 16, bytes(rng.randrange(256) for _ in range(256)))
    if choice == 2:
        return (name, (0x6752, 0x8034), 8, 8, bytes(rng.randrange(256) for _ in range(128)))
    if choice == 3:
        return (name, (0x6754, 0x8363), 8, 8, bytes(rng.randrange(256) for _ in range(128)))
    if choice == 4:
        return (name, (0x6752, 0x8033), 8, 8, bytes(rng.randrange(256) for _ in range(128)))
    if choice == 5:
        return (name, (0x6758, 0x1401), 8, 8, bytes(rng.randrange(256) for _ in range(128)))
    if choice == 6:
        return (name, (0x6757, 0x6761), 8, 8, bytes(rng.randrange(256) for _ in range(32)))
    if choice == 7:
        centre, radius = 7.5 + rng.uniform(-1, 1), rng.uniform(5.0, 8.0)
        return (name, RGBA8, 16, 16, stored_rgba(16, lambda x, y: max(0.0, 1.0 - (math.hypot(x - centre, y - centre)
                                                                                 / radius) ** 2)))
    if choice == 8:
        return (name, RGBA8, 8, 8, bytes([255, 255, 255, 255]) * 64)
    if choice == 9:
        return (name, (0x6754, 0x1401), 8, 8, bytes(rng.randrange(256) for _ in range(192)))
    colour = [rng.randrange(256) for _ in range(3)]
    return (name, RGBA8, 8, 8, bytes([255 if rng.random() < 0.9 else 50, *colour]) * 64)


NAMES = ["mark_lt_01", "mark_tam_02", "maho_star_x", "spot01_mado", "s01_mado_light_", "neon_sign", "window_03",
         "dg01_white", "floor_01", "ganon_kabe", "gnn_torch_03", "s00_syoku_01", "hidan_yougan_01", "fire_01",
         "fir3_glow", "mgm_02", "hikari_01", "light_beam", "shop_any_lant_0", "turi_rantan_lig", "rm_ten_obj_02",
         "tar_rm_kana_01", "kok_rm_obj_04", "dg05_kabe_al", "gc_kabe_3t", "gcp_kabe_12_t", "ido_mark_lt", "lava_x",
         "kja_light", "plain_01"]


def random_model(rng):
    textures = [texture(rng, rng.choice(NAMES)) for _ in range(rng.randrange(1, 5))]
    materials = []
    for _ in range(rng.randrange(1, 5)):
        uv = (rng.choice([1.0, 0.0, 2.0, -1.0]), rng.choice([1.0, 0.5]), rng.uniform(-1, 1), rng.uniform(-1, 1))
        materials.append((rng.randrange(-1, len(textures)), rng.random() < 0.5, uv))
    bones = [(0, -1, (1, 1, 1), (0, 0, 0), (0, 0, 0))]
    if rng.random() < 0.5:
        bones.append((1, 0, (rng.uniform(0.5, 2), 1, rng.uniform(0.5, 2)),
                      (rng.uniform(-3, 3), rng.uniform(-3, 3), rng.uniform(-3, 3)),
                      (rng.uniform(-500, 500), rng.uniform(-100, 100), rng.uniform(-500, 500))))
    shapes = []
    for _ in range(rng.randrange(1, 7)):
        kind = rng.random()
        centre = [rng.uniform(-1500, 1500), rng.uniform(-200, 600), rng.uniform(-1500, 1500)]
        if kind < 0.2:  # a doorway-shaped volume
            size = [rng.uniform(30, 480), rng.uniform(95, 390), rng.uniform(30, 480)]
        elif kind < 0.4:  # lava sheets
            size = [rng.uniform(100, 1400), rng.uniform(0, 20), rng.uniform(100, 1400)]
        else:
            size = [rng.uniform(1, 200) for _ in range(3)]
        low = [c - s / 2 for c, s in zip(centre, size)]
        high = [c + s / 2 for c, s in zip(centre, size)]
        points, triangles = box(low, high)
        options = {"bone": rng.randrange(len(bones))}
        if rng.random() < 0.3:
            options.update(kind=0x1402, scale=0.5)
        if rng.random() < 0.6:
            options["uvs"] = [(rng.uniform(-2, 2), rng.uniform(-2, 2)) for _ in points]
        shapes.append((rng.randrange(len(materials) + 1) % len(materials), points, triangles, options))
        if rng.random() < 0.3:  # a neighbour of the same material, to merge
            near = [c + rng.uniform(-80, 80) for c in centre]
            points, triangles = box([c - 15 for c in near], [c + 15 for c in near])
            shapes.append((shapes[-1][0], points, triangles, {}))
    return cmb(materials, textures, shapes, bones)


def room(models, header=None):
    head = header if header is not None else bytes(range(256)) * 2
    return b"ZSI\x01" + b"room".ljust(12, b"\0") + head + b"".join(models)


def scene_cases():
    import precomputed_tables
    import scene_lights
    rng = random.Random(1234)
    floor = ([(-500.0, 0.0, -500.0), (500.0, 0.0, -500.0), (500.0, 0.0, 500.0), (-500.0, 0.0, 500.0)],
             [(0, 1, 2), (0, 2, 3)])
    corridor = ([(x, y, z) for x in (-90.0, 90.0) for y in (0.0, 190.0) for z in (600.0, 820.0)], box((0, 0, 0), (1, 1, 1))[1])
    white = bytes([255, 255, 255, 255]) * 64
    grey = bytes([255, 0x80, 0x80, 0x80]) * 64
    orange = bytes([255, 0x20, 0x80, 0xFF]) * 64
    no_uv = (1.0, 1.0, 0.0, 0.0)
    doorway = cmb([(0, False, no_uv), (1, True, no_uv)],
                  [("floor_01", RGBA8, 8, 8, grey), ("dg01_white", RGBA8, 8, 8, white)],
                  [(0, *floor, {}), (1, *corridor, {})])
    glow_points, glow_triangles = box((80, 30, -220), (120, 70, -180))
    glow = cmb([(0, False, no_uv)], [("mark_lt_01", RGBA8, 8, 8, orange)], [(0, glow_points, glow_triangles, {})])
    atlas = cmb([(0, False, (1.0, 1.0, 0.8, 0.6))], [("rm_ten_obj_02", RGBA8, 8, 8, grey)],
                [(0, glow_points, glow_triangles, {"uvs": [(0.0, 0.0)] * 8})])
    files = {"scene/ganon_1_info.zsi": room([glow]), "scene/ganon_1_dd_info.zsi": room([glow, doorway]),
             "scene/ganon_2_info.zsi": room([doorway]), "scene/tent_0_info.zsi": room([atlas]),
             "scene/unknown_0_info.zsi": room([glow]), "scene/ganon_info.zsi": room([glow]),
             "scene/ydan_0_info.zsi": room([b"cmb garbage", glow, b"cmb "])}
    for index in range(60):
        header = bytes(rng.randrange(256) for _ in range(512)) if rng.random() < 0.8 else None
        stem = rng.choice(["ydan", "hidan", "spot04", "kakariko", "link", "shop", "tent", "souko", "kokiri"])
        suffix = "_dd" if rng.random() < 0.2 else ""
        files[f"scene/{stem}_{index}{suffix}_info.zsi"] = room([random_model(rng) for _ in range(rng.randrange(1, 4))],
                                                               header)
    romfs = write_tree("fixtures/scene/romfs", files)
    image = write("fixtures/scene/romfs.bin", romfs_image(files))
    for label, source in (("directory", romfs), ("image", image)):
        payload, summary = scene_lights.build_catalog(source)
        CASES.append({"kind": "scene_catalog", "name": label, "romfs": rel(source),
                      "expected": rel(write(f"expected/scene/{label}.json", payload)), "summary": summary})
    for path in sorted(files):
        emitters = scene_lights.room_emitters(files[path])
        payload = json.dumps(emitters, separators=(",", ":"), allow_nan=False).encode("ascii")
        CASES.append({"kind": "room_emitters", "room": rel(OUTPUT / "fixtures/scene/romfs" / path),
                      "expected": rel(write(f"expected/scene/rooms/{Path(path).stem}.json", payload))})
    table = write("expected/scene/table.json", precomputed_tables.scene_lights_table(romfs, "ab" * 32))
    CASES.append({"kind": "scene_table", "romfs": rel(romfs), "romfs_sha256": "ab" * 32, "expected": rel(table)})
    known = precomputed_tables.read_scene_lights_table(table)["rooms"]
    partial = dict(list(known.items())[::2])
    partial_table = json.loads(table.read_text())
    partial_table["rooms"] = partial
    partial_path = write("fixtures/scene/partial_table.json", json.dumps(partial_table).encode())
    payload, summary = scene_lights.build_catalog(romfs, partial)
    CASES.append({"kind": "scene_catalog", "name": "partial table", "romfs": rel(romfs), "table": rel(partial_path),
                  "expected": rel(write("expected/scene/partial.json", payload)), "summary": summary})
    recipe = {"inputs": {"romfs": {"sha256": "cd" * 32}}}
    for label, table_path in (("plain", None), ("table", partial_path)):
        data = OUTPUT / f"expected/scene/prepared-{label}"
        scene_lights.prepare_scene_lights(data_root=data, recipe=recipe, romfs=image, table=table_path)
        CASES.append({"kind": "prepare_scene", "romfs": rel(image), "recipe": recipe,
                      "table": rel(table_path) if table_path else None, "files": tree_files(data)})
    broken = json.loads(table.read_text())
    next(iter(broken["rooms"].values())).append({"category": "mesh", "position": [0, 0, 0], "radius": 1,
                                                 "gain": 1.0, "colour": [1, 1, 1]})
    broken_path = write("fixtures/scene/broken_table.json", json.dumps(broken).encode())
    try:
        precomputed_tables.read_scene_lights_table(broken_path)
        message = None
    except ValueError as error:
        message = str(error)
    CASES.append({"kind": "scene_table_error", "table": rel(broken_path), "message": message})


# --- fonts and TopScreen ------------------------------------------------------------------------------------

def topscreen_cases():
    import test_qbf_font as fonts
    import topscreen_assets
    from tools.oot3d.decomp_support.scripts.build_topscreen_texture_override_pack import build_texture_pack
    from tools.oot3d.decomp_support.scripts.prepare_qbf_font_pack import prepare_font_pack
    native = fonts.font_bytes()
    variants = {
        "eu": {"message/eu/ltn16.qbf": native, "misc/x.bin": b"x"},
        "us": {"message/us/ltn16.qbf": native},
        "both": {"message/eu/ltn16.qbf": native,
                 "message/us/ltn16.qbf": fonts.font_bytes(16, ((32, 0, b"\x01\x02\0\0"), (42, 1, b"\0" * 4)))},
        "none": {"message/jp/ltn16.qbf": native},
        "alias": {"message/eu/ltn16.qbf": fonts.font_bytes(16, ((32, 0, b"\0" * 4), (42, 0, b"\0" * 4)))},
    }
    hd = write("fixtures/font/ltn16_hd.qbf", fonts.font_bytes(64, ((32, 1, b"\xff" * 4), (42, 0, b"\x05\x06\0\0"))))
    for label, files in variants.items():
        image = write(f"fixtures/font/{label}.bin", romfs_image(files))
        output = OUTPUT / f"expected/font/{label}.zip"
        try:
            prepare_font_pack(font=hd, romfs=image, output=output)
            case = {"expected": rel(output)}
        except ValueError as error:
            case = {"message": str(error)}
        CASES.append({"kind": "font_pack", "name": label, "font": rel(hd), "romfs": rel(image), **case})
    shipped = REPOSITORY / "runtime/triaevum_resources/topscreen"
    root = OUTPUT / "fixtures/package"
    for directory, name in (("singlescreen-3.0", "custom_ui01.ctxb"), ("singlescreen-3.0", "zelda_link_ext.zar"),
                            ("singlescreen-3.1", "ltn16.qbf")):
        target = root / "resources/topscreen" / directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(shipped / directory / name, target)
    atlas = root / "resources/topscreen/singlescreen-3.0/custom_ui01.ctxb"
    CASES.append({"kind": "o3tu", "atlas": rel(atlas),
                  "expected": rel(write("expected/o3tu/shipped.o3tu", build_texture_pack(singlescreen_ui_atlas=atlas.read_bytes())))})
    header = bytearray(0x48)
    header[:4] = b"ctxb"
    struct.pack_into("<HHHH", header, 0x2C, 8, 8, 0x675B, 0)
    synthetic = write("fixtures/o3tu/etc1a4.ctxb", bytes(header) + bytes(range(256)) * 2)
    CASES.append({"kind": "o3tu", "atlas": rel(synthetic),
                  "expected": rel(write("expected/o3tu/etc1a4.o3tu", build_texture_pack(singlescreen_ui_atlas=synthetic.read_bytes())))})
    # A native 16x16 font of the shipped HD font's first 40 characters, each
    # its own glyph (the shipped 64x64 font gives density 4).
    from tools.oot3d.decomp_support.scripts.qbf_font import parse_qbf
    shipped_font = parse_qbf((root / "resources/topscreen/singlescreen-3.1/ltn16.qbf").read_bytes())
    characters = [(entry.code, index, bytes([index, 1, 2, 3])) for index, entry in enumerate(shipped_font.characters[:40])]
    native_for_shipped = fonts.font_bytes(16, characters)
    png = (b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" + struct.pack(">IIBBBBB", 1024, 512, 8, 6, 0, 0, 0)
           + b"fixture")
    hd_atlas = write("fixtures/package/pack/ui.png", png)
    topscreen_romfs = write("fixtures/topscreen/romfs.bin", romfs_image({"message/eu/ltn16.qbf": native_for_shipped}))
    recipe = {"title": topscreen_assets.TITLE, "inputs": {"romfs": {"sha256": "a" * 64}}}
    for label, hd_path in (("native", None), ("hd", hd_atlas)):
        data = OUTPUT / f"expected/topscreen/{label}"
        try:
            topscreen_assets.prepare_topscreen_assets(root=root, data_root=data, recipe=recipe, romfs=topscreen_romfs,
                                                      ui_atlas_hd=hd_path)
            case = {"files": tree_files(data)}
        except ValueError as error:
            case = {"message": str(error), "files": tree_files(data) if data.exists() else {}}
        CASES.append({"kind": "topscreen", "name": label, "root": rel(root), "recipe": recipe,
                      "romfs": rel(topscreen_romfs), "ui_atlas_hd": rel(hd_path) if hd_path else None,
                      **case})


# --- interior sun -------------------------------------------------------------------------------------------

def interior_sun_cases():
    from unittest.mock import patch
    import interior_sun
    from common import sha256_file
    runtime = write("fixtures/sun/TriAevum.exe", b"neutral runtime fixture")
    record = {"path": runtime.name, "bytes": runtime.stat().st_size, "sha256": sha256_file(runtime)}
    cache = OUTPUT / "fixtures/sun/cache"
    table = write("fixtures/sun/interior-sun.masks", b"first table")
    shell = struct.pack("<4sIQ6I", b"RSSH", 3, 1, 64, 512, 0, 0, 1, 0) + struct.pack("<Q", 0)
    result = {"format": interior_sun.FORMAT, "criterion_version": 3, "game_booted": False,
              "cache_directory": str(cache), "rooms": 2, "built": 1, "precomputed": 1, "fallback": 1, "ms": 1.25,
              "entries": [{"status": "built", "fingerprint": "0000000000000001", "interior": True,
                           "path": "shadow-shells/0000000000000001.bin", "ms": 0.5},
                          {"status": "fallback", "reason": "unsupported CMB geometry", "fingerprint": "00000000000000ff"}]}

    def invoke(command, **kwargs):
        target = Path(command[command.index("--renderer-cache-directory") + 1]) / "shadow-shells/0000000000000001.bin"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(shell)
        Path(command[command.index("--manifest") + 1]).write_text(json.dumps(result))
        return subprocess.CompletedProcess(command, 0, "", "")
    for label, mask in (("plain", None), ("table", table)):
        data = OUTPUT / f"expected/sun/{label}"
        messages = []
        with patch.object(interior_sun, "run_native", side_effect=invoke):
            returned = interior_sun.prepare_interior_sun(
                root=runtime.parent, data_root=data, runtime_record=record, romfs=OUTPUT / "fixtures/sun/romfs.bin",
                romfs_sha256="a" * 64, cache_directory=cache, mask_table=mask,
                report=lambda _, text: messages.append(text))
        CASES.append({"kind": "interior_sun", "name": label, "runtime_sha256": record["sha256"],
                      "cache": rel(cache), "table": rel(mask) if mask else None, "shell": list(shell),
                      "result": result, "messages": messages, "returned": returned, "files": tree_files(data)})


def assets():
    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    OUTPUT.mkdir(parents=True)
    water_cases()
    scene_cases()
    topscreen_cases()
    interior_sun_cases()
    (OUTPUT / "cases.json").write_text(json.dumps(CASES, indent=1))


if __name__ == "__main__":
    {"assets": assets, "numerics": numerics}[sys.argv[3]]()
