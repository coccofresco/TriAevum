# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generates synthetic catalog fixtures with the Python Forge's own test
builders, runs the Python functions on them and writes a manifest of the
expected results for tests/python_oracle_catalog.rs.

Usage: python -I catalog_cases.py RELEASE_DIR REPO_ROOT WORKDIR
"""

import copy
import json
import os
from pathlib import Path
import random
import struct
import sys

RELEASE, REPO, WORK = (Path(argument).resolve() for argument in sys.argv[1:4])
sys.path[:0] = [str(RELEASE), str(REPO)]

from common import atomic_write_json, load_json_object, sha256_file  # noqa: E402
from ctr_rom import ExtractedFile, ExtractedTitleInputs  # noqa: E402
import data_compatibility  # noqa: E402
from forge import HashCache, load_recipe, verify_input  # noqa: E402
from forge_gui import load_recipe_options, match_extracted_recipe  # noqa: E402
import input_adapters  # noqa: E402
import input_copy_adapter  # noqa: E402
from oot3d_region_assets import ALGORITHM, INVALID, adapt_qm_table, name_hash, normalize_romfs  # noqa: E402
import precompiled_titles  # noqa: E402
from precompiled_variants import DEFINITIONS, add_verified_variants  # noqa: E402
import qualified_input_coverage  # noqa: E402
from release_platform import LINUX, host_platform, place_installed_runtime  # noqa: E402
from romfs_identity import fingerprint  # noqa: E402
from shader_preparation import RENDERER_CONTRACT  # noqa: E402
from test_oot3d_region_assets import USA_MOVIES, qm_fixture, romfs_fixture  # noqa: E402

cases = []
rng = random.Random(20261025)


def rel(path):
    return Path(path).resolve().relative_to(WORK).as_posix()


def write(name, data):
    path = WORK / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return name


def run(op, args, function):
    try:
        value = function()
        expect = {"ok": True, "text": json.dumps(value)}
    except Exception as error:  # noqa: BLE001 - the oracle records every failure
        expect = {"ok": False, "type": type(error).__name__, "message": str(error)}
    cases.append({"op": op, "args": args, "expect": expect})
    return expect


def utf16(name):
    # surrogatepass lets fixtures carry the unpaired surrogates both parsers reject.
    return name.encode("utf-16-le", "surrogatepass")


# ---------------------------------------------------------------- Level-3 trees

def build_level3(tree, rand):
    """A valid Level-3 image of a nested {name: dict | bytes} tree."""
    directories, files = [], []

    def visit(node, name, parent):
        index = len(directories)
        directories.append({"name": name, "parent": parent, "dirs": [], "files": []})
        for child, value in node.items():
            if isinstance(value, dict):
                directories[index]["dirs"].append(visit(value, child, index))
            else:
                directories[index]["files"].append(len(files))
                files.append({"name": child, "parent": index, "data": value})
        return index

    visit(tree, "", 0)
    offset = 0
    for item in directories:
        item["offset"] = offset
        offset += (24 + len(utf16(item["name"])) + 3) & ~3
    dir_size = offset
    offset = 0
    for item in files:
        item["offset"] = offset
        offset += (32 + len(utf16(item["name"])) + 3) & ~3
    file_size = max(offset, 4)
    payload = bytearray()
    for item in files:
        payload.extend(bytes(-len(payload) % 16 + 16 * rand.randrange(2)))
        item["payload"] = len(payload)
        payload.extend(item["data"])
    dir_buckets, file_buckets = rand.randrange(1, 6), rand.randrange(1, 8)
    dirs, file_table = bytearray(dir_size), bytearray(file_size)
    dir_heads, file_heads = [INVALID] * dir_buckets, [INVALID] * file_buckets
    for index, item in enumerate(directories):
        encoded = utf16(item["name"])
        parent = directories[item["parent"]]["offset"]
        sibling = INVALID
        if index:
            siblings = directories[item["parent"]]["dirs"]
            position = siblings.index(index)
            if position + 1 < len(siblings):
                sibling = directories[siblings[position + 1]]["offset"]
        child = directories[item["dirs"][0]]["offset"] if item["dirs"] else INVALID
        first = files[item["files"][0]]["offset"] if item["files"] else INVALID
        bucket = name_hash(parent, encoded) % dir_buckets
        struct.pack_into("<6I", dirs, item["offset"], parent, sibling, child, first, dir_heads[bucket], len(encoded))
        dir_heads[bucket] = item["offset"]
        dirs[item["offset"] + 24:item["offset"] + 24 + len(encoded)] = encoded
    for index, item in enumerate(files):
        encoded = utf16(item["name"])
        owner = directories[item["parent"]]
        position = owner["files"].index(index)
        following = files[owner["files"][position + 1]]["offset"] if position + 1 < len(owner["files"]) else INVALID
        bucket = name_hash(owner["offset"], encoded) % file_buckets
        struct.pack_into("<IIQQII", file_table, item["offset"], owner["offset"], following, item["payload"],
                         len(item["data"]), file_heads[bucket], len(encoded))
        file_heads[bucket] = item["offset"]
        file_table[item["offset"] + 32:item["offset"] + 32 + len(encoded)] = encoded
    dh = 40
    do = dh + 4 * dir_buckets
    fh = do + len(dirs)
    fo = fh + 4 * file_buckets
    content = (fo + len(file_table) + 15) & ~15
    header = struct.pack("<10I", 40, dh, 4 * dir_buckets, do, len(dirs), fh, 4 * file_buckets, fo,
                         len(file_table), content)
    image = (header + struct.pack(f"<{dir_buckets}I", *dir_heads) + dirs
             + struct.pack(f"<{file_buckets}I", *file_heads) + file_table)
    return image + bytes(content - len(image)) + payload


NAMES = ["a", "B", "z", "_", "é", "ü.bin", "日本", "\U0001F600", "x\"q", "tab\tname",
         "ctl\x01", "a.b", "A", "aa", "1", "~", "ñame", "ÿ", "space name", " "]


def random_tree(rand, depth=0):
    node = {}
    for _ in range(rand.randrange(0 if depth else 1, 5)):
        name = rand.choice(NAMES) + (str(rand.randrange(3)) if rand.random() < 0.5 else "")
        if name in node:
            continue
        if depth < 3 and rand.random() < 0.35:
            node[name] = random_tree(rand, depth + 1)
        else:
            node[name] = bytes(rand.randrange(256) for _ in range(rand.randrange(0, 40)))
    return node


def rehash(image):
    """Rebuilds valid lookup chains for a Level-3 image with contiguous records."""
    data = bytearray(image)
    h = struct.unpack_from("<10I", data)
    for hashes, size, table, length, fixed, chain in ((h[1], h[2], h[3], h[4], 24, 16),
                                                      (h[5], h[6], h[7], h[8], 32, 24)):
        heads = [INVALID] * (size // 4)
        offset = 0
        while offset < length:
            name_size = struct.unpack_from("<I", data, table + offset + fixed - 4)[0]
            parent = struct.unpack_from("<I", data, table + offset)[0]
            name = bytes(data[table + offset + fixed:table + offset + fixed + name_size])
            bucket = name_hash(parent, name) % len(heads)
            struct.pack_into("<I", data, table + offset + chain, heads[bucket])
            heads[bucket] = offset
            offset += (fixed + name_size + 3) & ~3
        struct.pack_into(f"<{len(heads)}I", data, hashes, *heads)
    return bytes(data)


# ---------------------------------------------------------------- RomFS cases

def mutate(base, rand, limit):
    data = bytearray(base)
    for _ in range(rand.randrange(1, 4)):
        if rand.random() < 0.3:
            position = rand.randrange(0, (limit - 4) // 4) * 4
            value = rand.choice([0, INVALID, 4, 16, 40, 0x10000, rand.randrange(1 << 32),
                                 struct.unpack_from("<I", data, rand.randrange(0, (limit - 4) // 4) * 4)[0]])
            struct.pack_into("<I", data, position, value)
        else:
            position = rand.randrange(0, limit)
            data[position] = rand.choice([0, 0xFF, rand.randrange(256), data[position] ^ (1 << rand.randrange(8))])
    return bytes(data)


def romfs_inputs():
    base = romfs_fixture()
    h = struct.unpack_from("<10I", base)
    yield "base", base
    yield "ivfc", b"IVFC" + bytes(4092) + base
    yield "rehashed", rehash(base)
    for movies in (USA_MOVIES[:2], USA_MOVIES + ("hint183.moflex",)):
        yield f"movies{len(movies)}", romfs_fixture(movies)
    for damage in ("cycle", "overlap", "data", "truncated"):
        data = bytearray(base)
        if damage == "cycle":
            struct.pack_into("<I", data, h[3] + 8, 0)
        elif damage == "overlap":
            struct.pack_into("<I", data, 5 * 4, h[3])
        elif damage == "data":
            struct.pack_into("<I", data, 9 * 4, 16)
        else:
            data = data[:100]
        yield f"damage-{damage}", bytes(data)
    yield "lone-surrogate", base.replace(utf16("texture.bin"), utf16("\ud800exture.bin"))
    for index in range(220):
        yield f"fuzz-meta-{index}", mutate(base, rng, h[9])
    for index in range(60):
        yield f"fuzz-qm-{index}", mutate(base, rng, h[9] + 16 + 96 * 2 + 30)
    for index in range(20):
        yield f"truncate-{index}", base[:rng.randrange(0, len(base))]
    yield "tiny", b"IV"
    yield "ivfc-short", b"IVFC" + bytes(100)


def romfs_cases():
    for name, data in romfs_inputs():
        source = write(f"romfs/{name}.bin", data)
        output = f"romfs/{name}.py.out"
        result = run("normalize", {"source": source, "output": output, "rust_output": f"romfs/{name}.rs.out"},
                     lambda: normalize_romfs(WORK / source, WORK / output))
        run("fingerprint", {"path": source}, lambda: fingerprint(WORK / source))
        if result["ok"]:
            run("fingerprint", {"path": output}, lambda: fingerprint(WORK / output))
            normalized = (WORK / output).read_bytes()
            content = struct.unpack_from("<I", normalized, 36)[0]
            moved = bytearray(normalized)
            moved[content:content] = bytes(32)
            struct.pack_into("<I", moved, 36, content + 32)
            layout = write(f"romfs/{name}.moved.bin", b"IVFC" + bytes(4092) + bytes(moved))
            run("fingerprint", {"path": layout}, lambda: fingerprint(WORK / layout))
            for index in range(3):
                broken = write(f"romfs/{name}.normfuzz{index}.bin", mutate(normalized, rng, content))
                run("fingerprint", {"path": broken}, lambda: fingerprint(WORK / broken))
    for index, name in enumerate(("\ud800", "a\udc00b", "\udbff")):
        tree = write(f"trees/surrogate{index}.bin", build_level3({"d": {name: b"x"}}, rng))
        run("fingerprint", {"path": tree}, lambda: fingerprint(WORK / tree))
    for index in range(40):
        tree = write(f"trees/{index}.bin", build_level3(random_tree(rng), rng))
        run("fingerprint", {"path": tree}, lambda: fingerprint(WORK / tree))
        broken = write(f"trees/{index}.fuzz.bin", mutate((WORK / tree).read_bytes(), rng, 120))
        run("fingerprint", {"path": broken}, lambda: fingerprint(WORK / broken))


def qm_cases():
    base = qm_fixture()
    variants = [("base", base, len(base))]
    for offset, size in ((1, 8), (10000, 3), (0, 1)):
        data = bytearray(base)
        struct.pack_into("<II", data, 40, offset, size)
        variants.append((f"slot-{offset}-{size}", bytes(data), len(data)))
    data = bytearray(base)
    data[48:56] = data[72:80]
    variants.append(("populated", bytes(data), len(data)))
    for index in range(80):
        variants.append((f"fuzz-{index}", mutate(base, rng, 208), len(base) + rng.choice([0, 0, -10, 5])))
    for name, data, size in variants:
        table = data[:rng.choice([208, 208, 208, 15, 112])]
        source = write(f"qm/{name}.bin", table)
        output = f"qm/{name}.py.out"

        def adapt():
            write(output, adapt_qm_table(table, size))
            return output
        run("qm", {"data": source, "file_size": size, "output": output}, adapt)


# ---------------------------------------------------------------- COPY programs

def copy_cases():
    source = bytes(range(256)) * 8
    pairs = [(source, source[100:700] + b"abc" + source[:80]), (b"0123456789abcdef", b"89abcdef01234567"),
             (b"a" * 32, b"b")]
    for _ in range(25):
        size = rng.randrange(1, 5000)
        source = bytes(rng.randrange(4) if rng.random() < 0.5 else rng.randrange(256) for _ in range(size))
        canonical = bytearray()
        for _ in range(rng.randrange(1, 12)):
            start = rng.randrange(0, size)
            canonical.extend(source[start:start + rng.randrange(1, 900)])
            if rng.random() < 0.3:
                canonical.append(source[rng.randrange(size)])
        pairs.append((source, bytes(canonical)))
    pairs.append((b"", b"x"))
    for index, (source, canonical) in enumerate(pairs):
        source_name = write(f"copy/{index}.source", source)
        canonical_name = write(f"copy/{index}.canonical", canonical)
        program_name = f"copy/{index}.py.json"

        def build():
            program = input_copy_adapter.build_program(source, canonical)
            atomic_write_json(WORK / program_name, program)
            return program
        expect = run("copy_build", {"source": source_name, "canonical": canonical_name, "program_file": program_name},
                     build)
        if not expect["ok"]:
            continue
        program = json.loads(expect["text"])
        variants = [program]
        for _ in range(6):
            changed = copy.deepcopy(program)
            choice = rng.randrange(7)
            if choice == 0:
                changed["copies"][0][0] += rng.choice([-1, 1])
            elif choice == 1:
                changed["copies"].append([0, 1])
            elif choice == 2:
                changed["source"]["bytes"] = rng.choice([0, True, 1.0, 16 * 1024 * 1024 + 1, "1"])
            elif choice == 3:
                changed["canonical"]["sha256"] = changed["canonical"]["sha256"].upper()
            elif choice == 4:
                changed["copies"] = [rng.choice([[-1, 10], [0, 0], [True, 1], ["0", 1], [0], [0.0, 1]])]
            elif choice == 5:
                changed["literal"] = "x"
            else:
                changed["copies"] = []
            variants.append(changed)
        for number, variant in enumerate(variants):
            name = write(f"copy/{index}.variant{number}.json", json.dumps(variant).encode())
            output = f"copy/{index}.variant{number}.py.out"

            def apply():
                write(output, input_copy_adapter.apply_program(source, variant))
                return output
            run("copy_apply", {"source": source_name, "program": name, "output": output}, apply)


# ---------------------------------------------------------------- adapters, families, recipes

def adapter_fixture(directory, exheader=bytes(64), romfs=None):
    root = WORK / directory
    source = root / "original"
    source.mkdir(parents=True)
    canonical = b"89abcdef01234567"
    for kind, data in (("code", b"0123456789abcdef"), ("exheader", exheader), ("romfs", romfs or romfs_fixture())):
        (source / f"{kind}.bin").write_bytes(data)
    files = {kind: ExtractedFile(source / f"{kind}.bin", (source / f"{kind}.bin").stat().st_size,
                                 sha256_file(source / f"{kind}.bin")) for kind in ("code", "exheader", "romfs")}
    extracted = ExtractedTitleInputs("NCSD", 0, 0x0004000000033500, **files)
    contracts = {kind: {"bytes": item.bytes, "sha256": item.sha256} for kind, item in files.items()}
    normalize_romfs(source / "romfs.bin", root / "expected.romfs")
    inputs = {"code": input_copy_adapter.identity(canonical), "exheader": contracts["exheader"],
              "romfs": input_copy_adapter.identity((root / "expected.romfs").read_bytes())}
    program = input_copy_adapter.build_program((source / "code.bin").read_bytes(), canonical)
    program_path = root / "recipes/adapters/copies.json"
    atomic_write_json(program_path, program)
    record = {"path": "recipes/adapters/copies.json", "sha256": sha256_file(program_path),
              "bytes": program_path.stat().st_size}
    recipe = {"id": "fixture-adapted", "inputs": inputs, "input_adapter": {
        "format": input_adapters.FORMAT, "source_inputs": contracts, "resource_algorithm": ALGORITHM,
        "code_copies": record}}
    return root, extracted, recipe


def extracted_args(extracted):
    return {"program_id": extracted.program_id,
            "files": {kind: [rel(item.path), item.bytes, item.sha256] for kind, item in extracted.by_kind().items()}}


def adapter_cases():
    for number, change in enumerate(["none", "good", "corrupt-source", "wrong-romfs", "wrong-code", "literal",
                                     "bad-format", "exheader-changed", "copy-too-big", "existing-output",
                                     "family-source", "no-adapter"]):
        directory = f"adapt/{number}"
        if change == "family-source":
            root, extracted, recipe = adapter_fixture(directory, bytes(2048), rehash(romfs_fixture()))
        else:
            root, extracted, recipe = adapter_fixture(directory)
        if change == "corrupt-source":
            extracted.code.path.write_bytes(bytes(16))
        elif change == "wrong-romfs":
            recipe["inputs"]["romfs"]["sha256"] = "0" * 64
        elif change == "wrong-code":
            recipe["inputs"]["code"]["sha256"] = "0" * 64
        elif change == "literal":
            path = root / "recipes/adapters/copies.json"
            program = load_json_object(path)
            program["literal"] = "not allowed"
            atomic_write_json(path, program)
            recipe["input_adapter"]["code_copies"].update(bytes=path.stat().st_size, sha256=sha256_file(path))
        elif change == "bad-format":
            recipe["input_adapter"]["format"] = "other"
        elif change == "exheader-changed":
            recipe["input_adapter"]["source_inputs"]["exheader"]["sha256"] = "1" * 64
        elif change == "copy-too-big":
            recipe["input_adapter"]["code_copies"]["bytes"] = 32 * 1024 * 1024 + 1
        elif change == "existing-output":
            (root / "py_out").mkdir()
            (root / "rs_out").mkdir()
        elif change == "family-source":
            family = data_compatibility.identity({kind: item.path for kind, item in extracted.by_kind().items()})
            execution = data_compatibility.identity({"code": root / "original/code.bin",
                                                     "exheader": root / "original/exheader.bin",
                                                     "romfs": root / "expected.romfs"})
            execution["code"] = recipe["inputs"]["code"]
            recipe["data_compatibility"] = {"format": data_compatibility.FORMAT, "source": family,
                                            "execution": execution}
            # Storage flags differ from the exact source identity but not from the family.
            extracted.exheader.path.write_bytes(bytes(13) + b"\x03" + bytes(2034))
        elif change == "no-adapter":
            del recipe["input_adapter"]
        if change == "none":
            recipe = {"id": "native"}
        recipe_file = write(f"{directory}/recipe.json", json.dumps(recipe).encode())
        args = {"root": directory, "recipe": recipe_file, "extracted": extracted_args(extracted)}
        run("import_contract", {"recipe": recipe_file}, lambda: input_adapters.import_contract(recipe))
        run("adapter_validate", args, lambda: input_adapters.validate_adapter(root, recipe))

        def adapt():
            result, receipt = input_adapters.adapt_extracted_inputs(extracted, recipe, root=root,
                                                                    output=root / "py_out")
            if receipt is not None:
                receipt.pop("wall_seconds")
            return {"inputs": extracted_args(result), "receipt": receipt}
        run("adapt", {**args, "output": f"{directory}/py_out", "rust_output": f"{directory}/rs_out"}, adapt)
        recipes = f"{directory}/recipes.json"
        atomic_write_json(WORK / recipes, {"format": "triaevum_supported_revisions_v1", "recipes": [recipe]})
        run("match", {"recipes": recipes, "extracted": extracted_args(extracted)},
            lambda: match_extracted_recipe(extracted, WORK / recipes))


def family_cases():
    directory = WORK / "family"
    directory.mkdir()
    paths = {kind: directory / f"{kind}.bin" for kind in ("code", "exheader", "romfs")}
    paths["code"].write_bytes(b"synthetic code and initialized data")
    paths["exheader"].write_bytes(bytes(2048))
    write("family/source.romfs", romfs_fixture())
    normalize_romfs(WORK / "family/source.romfs", paths["romfs"])
    recipe = {"id": "synthetic", "inputs": {k: {"bytes": p.stat().st_size, "sha256": sha256_file(p)}
                                            for k, p in paths.items()}}
    family = data_compatibility.identity(paths)
    recipe["data_compatibility"] = {"format": data_compatibility.FORMAT, "source": family, "execution": family}
    variants = [("exact", lambda r: None)]
    pointers = [("data_compatibility", "source", "code", "sha256"), ("data_compatibility", "source", "exheader"),
                ("data_compatibility", "execution", "romfs", "files"), ("data_compatibility", "format"),
                ("data_compatibility", "execution", "romfs", "sha256"), ("data_compatibility", "source", "code"),
                ("inputs", "code", "bytes"), ("data_compatibility", "execution", "code", "bytes")]
    values = ["a" * 64, "B" * 64, True, 0, 1.0, "unbound", None, {"sha256": "c" * 64}, 36.0, -1, [], "1"]
    for index in range(60):
        pointer, value = rng.choice(pointers), rng.choice(values)

        def change(target, pointer=pointer, value=value):
            for key in pointer[:-1]:
                target = target[key]
            target[pointer[-1]] = value
        variants.append((f"mutation-{index}", change))
    variants += [("adapter-null", lambda r: r.update(input_adapter=None)),
                 ("adapter-no-source", lambda r: r.update(input_adapter={"format": "x"})),
                 ("no-family", lambda r: r.pop("data_compatibility")),
                 ("family-list", lambda r: r.update(data_compatibility=[]))]
    for name, change in variants:
        changed = copy.deepcopy(recipe)
        change(changed)
        recipe_file = write(f"family/{name}.json", json.dumps(changed).encode())
        for phase in ("source", "execution", "other"):
            run("family_expected", {"recipe": recipe_file, "phase": phase},
                lambda: data_compatibility.expected(changed, phase))
    exact = write("family/exact.json", json.dumps(recipe).encode())
    header = bytearray(2048)
    header[0xd] = 3
    header[0x400:0x600] = b"S" * 512
    for name, data in (("flags", header), ("short", bytes(2047)), ("changed", bytes(0x28) + b"\x01" + bytes(2019))):
        exheader = write(f"family/exheader-{name}.bin", bytes(data))
        args = {"code": "family/code.bin", "exheader": exheader, "romfs": "family/romfs.bin"}
        variant = {**paths, "exheader": WORK / exheader}
        run("family_identity", args, lambda: data_compatibility.identity(variant))
        run("family_verify", {**args, "recipe": exact, "phase": "source"},
            lambda: data_compatibility.verify(recipe, variant, phase="source"))
    changed = write("family/romfs-changed.bin", (WORK / "family/romfs.bin").read_bytes()[:-1] + b"\x01")
    args = {"code": "family/code.bin", "exheader": "family/exheader.bin", "romfs": changed}
    run("family_verify", {**args, "recipe": exact, "phase": "execution"},
        lambda: data_compatibility.verify(recipe, {**paths, "romfs": WORK / changed}, phase="execution"))
    # Byte matching, then the fallback to the declared source family.
    files = {k: ExtractedFile(p, p.stat().st_size, sha256_file(p)) for k, p in paths.items()}
    extracted = ExtractedTitleInputs("NCSD", 0, 7, **files)
    exact_recipes = write("family/recipes.json", json.dumps(
        {"format": "triaevum_supported_revisions_v1", "recipes": [recipe]}).encode())
    run("match", {"recipes": exact_recipes, "extracted": extracted_args(extracted)},
        lambda: match_extracted_recipe(extracted, WORK / exact_recipes))
    wrapped = write("family/romfs-ivfc.bin", b"IVFC" + bytes(4092) + paths["romfs"].read_bytes())
    wrapped_inputs = ExtractedTitleInputs("NCSD", 0, 7, **{**files,
                                                           "romfs": ExtractedFile(WORK / wrapped, 1, "0" * 64)})
    for name, listed in (("one", [recipe]), ("two", [recipe, {**recipe, "id": "second"}]), ("none", [{"id": "x"}])):
        recipes_file = write(f"family/recipes-{name}.json", json.dumps(
            {"format": "triaevum_supported_revisions_v1", "recipes": [1, *listed]}).encode())
        run("match", {"recipes": recipes_file, "extracted": extracted_args(wrapped_inputs)},
            lambda: match_extracted_recipe(wrapped_inputs, WORK / recipes_file))


def recipe_cases():
    files = {
        "embedded": (RELEASE / "supported_revisions.json").read_bytes(),
        "bom": b"\xef\xbb\xbf" + json.dumps({"format": "triaevum_supported_revisions_v1", "recipes": [
            {"id": "a", "title": "", "region": " EUR \n"}, {"id": "b", "title": 5, "region": None},
            {"id": 3}, "x", {"id": "c", "title": "T", "region": "　"}]}).encode(),
        "format": json.dumps({"format": "other", "recipes": []}).encode(),
        "list": json.dumps({"format": "triaevum_supported_revisions_v1", "recipes": {}}).encode(),
        "empty": json.dumps({"format": "triaevum_supported_revisions_v1", "recipes": [{"title": "x"}]}).encode(),
        "duplicate": json.dumps({"format": "triaevum_supported_revisions_v1",
                                 "recipes": [{"id": "a"}, {"id": "a"}]}).encode(),
    }
    for name, data in files.items():
        path = write(f"recipes/{name}.json", data)
        run("options", {"path": path}, lambda: [[o.recipe_id, o.label] for o in load_recipe_options(WORK / path)])
        for recipe_id in ("a", "oot3d-eur-catalogue-a584b067", "missing"):
            run("load_recipe", {"path": path, "id": recipe_id}, lambda: load_recipe(WORK / path, recipe_id))
    data = write("recipes/input.bin", b"abc")
    digest = sha256_file(WORK / data)
    cache = HashCache(WORK / "unused", enabled=False)
    for contract in ({"bytes": 3, "sha256": digest}, {"bytes": "3", "sha256": digest.upper()}, {"sha256": digest},
                     {"bytes": 4, "sha256": digest}, {"bytes": 3}, {"bytes": 3.9, "sha256": digest},
                     {"bytes": True, "sha256": digest}, {"bytes": " 3 ", "sha256": digest},
                     {"bytes": "x", "sha256": digest}, {"bytes": 3, "sha256": 5}):
        run("verify_input", {"path": data, "contract": contract},
            lambda: {k: v for k, v in verify_input("code", WORK / data, contract, cache).as_json().items()
                     if k != "path"})
    run("verify_input", {"path": "recipes", "contract": {"bytes": 3}},
        lambda: verify_input("code", WORK / "recipes", {"bytes": 3}, cache))


# ---------------------------------------------------------------- catalogs

def catalog_fixture(directory, platform):
    root = WORK / directory

    def artifact(name):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(name.encode())
        return {"path": name, "bytes": path.stat().st_size, "sha256": sha256_file(path)}
    recipe = {"id": "test", "inputs": {kind: {"sha256": str(i) * 64, "bytes": i}
                                       for i, kind in enumerate(("code", "exheader", "romfs"), 1)}}
    catalog = {"format": precompiled_titles.FORMAT, "install_model": precompiled_titles.MODEL,
               "target": platform.target, "runtime": artifact(platform.runtime),
               "native_module": artifact(platform.native_module),
               "titles": [{"recipe": "test", "inputs": recipe["inputs"], "abi_version": 2, "target": platform.target,
                           "translator_identity_sha256": "a" * 64, "plugin": artifact("titles/test/contract.dll")}]}
    return root, recipe, catalog, artifact


def catalog_cases():
    host = host_platform()
    changes = ["ok", "linux", "legacy-target", "corrupt-runtime", "corrupt-native", "corrupt-plugin", "revision",
               "family-unbound", "duplicate", "empty-titles", "bad-format", "abi-float", "abi-bool", "translator",
               "renderer-ok", "renderer-bad", "renderer-deps", "runtime-binding", "placed-runtime", "target-null",
               "title-not-dict", "missing-plugin"]
    for number, change in enumerate(changes):
        directory = f"catalog/{number}"
        root, recipe, catalog, artifact = catalog_fixture(directory, LINUX if change == "linux" else host)
        title = catalog["titles"][0]
        if change == "legacy-target":
            catalog.pop("target")
        elif change.startswith("corrupt-"):
            record = {"runtime": catalog["runtime"], "native": catalog["native_module"],
                      "plugin": title["plugin"]}[change.split("-")[1]]
            (root / record["path"]).write_bytes(b"changed")
        elif change == "revision":
            recipe["inputs"]["code"]["sha256"] = "b" * 64
        elif change == "family-unbound":
            recipe["data_compatibility"] = {"format": "unbound"}
        elif change == "duplicate":
            catalog["titles"] *= 2
        elif change == "empty-titles":
            catalog["titles"] = []
        elif change == "bad-format":
            catalog["install_model"] = "compile"
        elif change == "abi-float":
            title["abi_version"] = 2.0
        elif change == "abi-bool":
            title["abi_version"] = True
        elif change == "translator":
            title["translator_identity_sha256"] = "A" * 64
        elif change.startswith("renderer"):
            title["renderer_shader_preparation"] = {"format": RENDERER_CONTRACT,
                                                    "compiler": artifact("forge/compiler.exe"),
                                                    "dependencies": [artifact("forge/dep.dll")]}
            if change == "renderer-bad":
                title["renderer_shader_preparation"]["format"] = "other"
            elif change == "renderer-deps":
                title["renderer_shader_preparation"]["dependencies"] = [5]
        elif change == "runtime-binding":
            catalog["runtime"]["path"] = "elsewhere"
        elif change == "placed-runtime":
            place_installed_runtime(root, host)
        elif change == "target-null":
            catalog["target"] = None
        elif change == "title-not-dict":
            catalog["titles"].append(7)
        elif change == "missing-plugin":
            title.pop("plugin")
        atomic_write_json(root / precompiled_titles.CATALOG, catalog)
        recipe_file = write(f"{directory}/recipe.json", json.dumps(recipe).encode())
        run("load_catalog", {"root": directory}, lambda: precompiled_titles.load_catalog(root))
        run("validate_title", {"root": directory, "recipe": recipe_file},
            lambda: precompiled_titles.validate_title(root, recipe))
        run("select_title", {"root": directory, "recipe": recipe_file},
            lambda: precompiled_titles.select_title(root, recipe))
        run("runtime_record", {"root": directory}, lambda: precompiled_titles.runtime_record(
            root, load_json_object(root / precompiled_titles.CATALOG)))
    root = WORK / "checked"
    (root / "dir").mkdir(parents=True)
    (root / "dir/file.bin").write_bytes(b"payload")
    (root / "outside.bin").write_bytes(b"x")
    digest = sha256_file(root / "dir/file.bin")
    links = []
    try:
        os.symlink(root / "dir", root / "link")
        links.append("link/file.bin")
    except OSError:
        pass
    try:
        import _winapi
        _winapi.CreateJunction(str(root / "dir"), str(root / "junction"))
        links.append("junction/file.bin")
    except (ImportError, OSError):
        pass
    records = [{"path": path, "bytes": 7, "sha256": digest}
               for path in ["dir/file.bin", "dir//file.bin", "./dir/file.bin", "dir\\file.bin", "dir/./file.bin",
                            "../outside.bin", "/dir/file.bin", "C:dir/file.bin", "dir/missing.bin", "dir", ".",
                            "", *links]]
    records += [{"path": "dir/file.bin", "bytes": 7.0, "sha256": digest},
                {"path": "dir/file.bin", "bytes": 7, "sha256": digest.upper()},
                {"path": "dir/file.bin", "bytes": True, "sha256": digest}, {"bytes": 7}, {"path": None},
                {"path": 5}]
    for record in records:
        run("checked_file", {"root": "checked", "record": record},
            lambda: precompiled_titles.checked_file(root, record).relative_to(root.resolve()).as_posix())


# ---------------------------------------------------------------- publisher bindings

def binding_cases():
    definitions = load_json_object(DEFINITIONS)
    base, variant = copy.deepcopy(definitions["recipes"])
    title = {"recipe": base["id"], "inputs": base["inputs"],
             "plugin": {"path": "titles/existing/triaevum_title_aot.dll"},
             "sources": ["existing-build-source", "existing-translated-source"],
             "abi_version": 2, "translator_identity_sha256": "a" * 64}
    scenarios = [("plain", {"titles": [title]}, {"recipes": [base]}, definitions)]
    for change in ("code", "romfs", "process", "input_adapter", "optional_inputs"):
        changed = copy.deepcopy(definitions)
        if change in ("code", "romfs"):
            changed["recipes"][1]["inputs"][change]["sha256"] = "0" * 64
        else:
            changed["recipes"][1][change] = {}
        scenarios.append((change, {"titles": [title]}, {"recipes": [base]}, changed))
    scenarios.append(("no-title", {"titles": []}, {"recipes": [base]}, definitions))
    duplicate = {"recipes": [base, {**variant, "id": "unnecessary-duplicate"}]}
    scenarios.append(("duplicate", {"titles": [title]}, duplicate, definitions))
    once = add_verified_variants({"titles": [title]}, {"recipes": [base]}, definitions)
    scenarios.append(("rerun", once[0], once[1], definitions))
    conflict = copy.deepcopy(once)
    conflict[0]["titles"][1]["plugin"]["path"] = "wrong.dll"
    scenarios.append(("conflict", conflict[0], conflict[1], definitions))
    usa = {"id": "usa", "inputs": {"code": {"sha256": "f" * 64}}, "input_adapter": {"existing": True}}
    scenarios.append(("usa", {"titles": [title, {"recipe": "usa", "inputs": usa["inputs"],
                                                 "input_adapter": usa["input_adapter"]}]},
                      {"recipes": [base, usa]}, definitions))
    header = copy.deepcopy(definitions)
    header["recipes"][1]["inputs"]["exheader"]["bytes"] = 2048.0
    scenarios.append(("exheader-float", {"titles": [title]}, {"recipes": [base]}, header))
    header = copy.deepcopy(definitions)
    header["recipes"][1]["inputs"]["exheader"]["sha256"] = "A" * 64
    scenarios.append(("exheader-upper", {"titles": [title]}, {"recipes": [base]}, header))
    for name, catalog, recipes, defs in scenarios:
        args = {"catalog": catalog, "recipes": recipes, "definitions": defs}
        run("variants", args, lambda: list(add_verified_variants(catalog, recipes, defs)))

    root = str(RELEASE / "input_coverage")
    recipes = load_json_object(DEFINITIONS)
    coverage_title = {"recipe": recipes["recipes"][0]["id"], "inputs": recipes["recipes"][0]["inputs"],
                      "target": "current-platform", "plugin": {"sha256": "new-gameplay-fixes", "path": "new-title"},
                      "sources": ["new-translated-source"],
                      "renderer_shader_preparation": {"compiler": "new-compiler"},
                      "device_pipeline_preparation": {"recipes": "new-pipelines"}}
    catalog, recipes = add_verified_variants({"titles": [coverage_title]}, recipes, recipes)
    scenarios = [("plain", catalog, recipes)]
    for field in ("inputs", "process"):
        changed = copy.deepcopy(recipes)
        changed["recipes"][0][field] = {}
        scenarios.append((f"canonical-{field}", catalog, changed))
    scenarios.append(("no-title", {"titles": []}, recipes))
    bound = qualified_input_coverage.bind(catalog, recipes)
    scenarios.append(("rebind", bound[0], bound[1]))
    for name, catalog_value, recipes_value in scenarios:
        args = {"catalog": catalog_value, "recipes": recipes_value, "root_abs": root}
        run("bind", args, lambda: list(qualified_input_coverage.bind(catalog_value, recipes_value)))
        run("require_coverage", args, lambda: qualified_input_coverage.require_coverage(catalog_value, recipes_value))
    lost = copy.deepcopy(bound)
    lost[0]["titles"][-1].pop("data_compatibility")
    run("require_coverage", {"catalog": lost[0], "recipes": lost[1], "root_abs": root},
        lambda: qualified_input_coverage.require_coverage(lost[0], lost[1]))


romfs_cases()
qm_cases()
copy_cases()
adapter_cases()
family_cases()
recipe_cases()
catalog_cases()
binding_cases()
(WORK / "manifest.json").write_text(json.dumps({"host": host_platform().target, "cases": cases}), encoding="utf-8")
print(len(cases))
