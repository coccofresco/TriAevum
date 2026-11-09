# SPDX-License-Identifier: GPL-2.0-or-later
"""Project Restoration layout recipe (triaevum_restoration_layout_v1).

Offline developer tool, never shipped data. Project Restoration's HD-HUD
layout files (Joker.Main.New.gar, Joker.Main.Play.gar, Joker.mfpj) mix
Restoration-original members with game members. Moonfall Curse ships none of
the game bytes: this tool writes a recipe that the Forge follows at install
time to rebuild the three files from the user's own EUR dump:

  * a member byte-identical to a member of the dump's
    layout/Joker.Main.Play.gar is a {"dump": ...} reference;
  * a member Restoration modified is a BPS patch of its dump base;
  * a member with no dump counterpart ships as Project Restoration
    distributes it ({"ship": ...}, GPL-2.0-or-later, see
    LICENSES/Project-Restoration.md);
  * Joker.mfpj is a whole-file BPS patch of the dump's layout/Joker.mfpj.

GAR v2 facts come from the tree's own titles/mm3d/dekui/setup/dekui_gar.py
(header, record and offset tables) and from the Restoration files read
here; BPS follows the public format description (beat: "BPS1", varint
sizes, four actions, CRC32 trailer). Read the dump with python -I.

  generate  --romfs romfs.bin --restoration <release dir> [--out <dir>]
  check     --romfs romfs.bin --restoration <release dir> [--recipe <dir>]
  members   --restoration <release dir> [--recipe <dir>] --out <dir>
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[2]
FINGERPRINTS = ROOT / "titles/mm3d/dekui/setup/setup-fingerprints.json"
RECIPE_DIR = ROOT / "titles/mm3d/restoration/package"
FORMAT = "triaevum_restoration_layout_v1"
RECIPE_NAME = "layout.recipe.json"
DUMP_ARCHIVE = "layout/Joker.Main.Play.gar"
DUMP_PROJECT = "layout/Joker.mfpj"
ARCHIVES = ("layout/Joker.Main.New.gar", "layout/Joker.Main.Play.gar")
LEVEL3_OFFSET = 0x1000
UPSTREAM = {"project": "leoetlino/project-restoration", "commit": "8bb2a7b8",
            "license": "GPL-2.0-or-later", "notice": "LICENSES/Project-Restoration.md"}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


# ---------------------------------------------------------------- GAR v2 --
def _u16(b, at):
    return struct.unpack_from("<H", b, at)[0]


def _u32(b, at):
    return struct.unpack_from("<I", b, at)[0]


def _cstr(b, at):
    if not 0 <= at < len(b):
        raise ValueError("Invalid GAR string offset")
    return b[at:b.index(b"\0", at)].decode("ascii")


def parse_gar(b: bytes) -> dict:
    """A GAR v2 as {hash, types, align, members: [(name, payload)]}; emit_gar
    of the result must give the same bytes (round trip) or it is refused."""
    if b[:4] != b"GAR\x02" or len(b) < 0x20 or _u32(b, 4) != len(b):
        raise ValueError("Unsupported GAR header")
    ntypes, count = _u16(b, 8), _u16(b, 10)
    types_at, records, offsets = _u32(b, 12), _u32(b, 16), _u32(b, 20)
    hash_name = b[24:32].rstrip(b"\0").decode("ascii")
    types = [_cstr(b, _u32(b, types_at + i * 16 + 8)) for i in range(ntypes)]
    members = []
    for i in range(count):
        record = records + i * 12
        size, start = _u32(b, record), _u32(b, offsets + i * 4)
        if start + size > len(b):
            raise ValueError("Invalid GAR entry")
        members.append((_cstr(b, _u32(b, record + 8)), b[start:start + size]))
    if len({name for name, _ in members}) != len(members):
        raise ValueError("Duplicate GAR entry")
    model = {"hash": hash_name, "types": types, "align": 4, "members": members}
    if emit_gar(model) != b:
        raise ValueError("GAR layout is not the canonical v2 layout this recipe re-emits")
    return model


def emit_gar(model: dict) -> bytes:
    """Canonical GAR v2: header, type table (count, index list, name, -1),
    per type its index list then its name (4-aligned), file records (size,
    stem, full name), name pairs (4-aligned), offsets, payloads."""
    types, members, align = model["types"], model["members"], model["align"]
    by_type = {name: [] for name in types}
    for index, (name, _) in enumerate(members):
        kind = name.rsplit(".", 1)[1] if "." in name else "unknown"
        if kind not in by_type:
            raise ValueError(f"GAR member type {kind} is not declared")
        by_type[kind].append(index)
    out = bytearray(0x20 + 16 * len(types))
    for t, name in enumerate(types):
        indices = by_type[name]
        list_at = len(out) if indices else 0xFFFFFFFF
        for index in indices:
            out += struct.pack("<I", index)
        name_at = len(out)
        out += name.encode("ascii") + b"\0"
        out += b"\0" * (-len(out) % 4)
        struct.pack_into("<IIII", out, 0x20 + t * 16, len(indices), list_at, name_at, 0xFFFFFFFF)
    records = len(out)
    out += b"\0" * (12 * len(members))
    for i, (name, payload) in enumerate(members):
        full_at = len(out)
        out += name.encode("ascii") + b"\0"
        stem_at = len(out)
        out += name.rsplit(".", 1)[0].encode("ascii") + b"\0"
        out += b"\0" * (-len(out) % 4)
        struct.pack_into("<III", out, records + i * 12, len(payload), stem_at, full_at)
    offsets = len(out)
    out += b"\0" * (4 * len(members))
    for i, (_, payload) in enumerate(members):
        if i:
            out += b"\0" * (-len(out) % align)
        struct.pack_into("<I", out, offsets + i * 4, len(out))
        out += payload
    hash_bytes = model["hash"].encode("ascii")
    if len(hash_bytes) >= 8:
        raise ValueError("GAR hash name too long")
    struct.pack_into("<4sIHHIII8s", out, 0, b"GAR\x02", len(out), len(types), len(members),
                     0x20, records, offsets, hash_bytes)
    return bytes(out)


# ------------------------------------------------------------------- BPS --
def _varint(n: int) -> bytes:
    out = bytearray()
    while True:
        x = n & 0x7F
        n >>= 7
        if n == 0:
            out.append(0x80 | x)
            return bytes(out)
        out.append(x)
        n -= 1


def _read_varint(b: bytes, at: int):
    data, shift = 0, 1
    while True:
        if at >= len(b):
            raise ValueError("Truncated BPS number")
        x = b[at]
        at += 1
        data += (x & 0x7F) * shift
        if x & 0x80:
            return data, at
        shift <<= 7
        data += shift


def _signed(n: int) -> bytes:
    return _varint((abs(n) << 1) | (n < 0))


def bps_encode(source: bytes, target: bytes) -> bytes:
    """Greedy, deterministic BPS: SourceRead/SourceCopy/TargetCopy for runs of
    four or more bytes already present, TargetRead only for new bytes."""
    out = bytearray(b"BPS1" + _varint(len(source)) + _varint(len(target)) + _varint(0))
    index = {}
    for i in range(len(source) - 3):
        index.setdefault(source[i:i + 4], []).append(i)
    tindex = {}
    indexed = 0
    literal = bytearray()
    source_rel = target_rel = 0
    t = 0
    while t < len(target):
        while indexed + 4 <= t:
            tindex.setdefault(target[indexed:indexed + 4], []).append(indexed)
            indexed += 1
        run = 0
        while t + run < len(target) and t + run < len(source) and source[t + run] == target[t + run]:
            run += 1
        best = (run, 0, 0)  # (length, command, start)
        key = target[t:t + 4]
        for start in index.get(key, ()):
            n = 0
            while t + n < len(target) and start + n < len(source) and source[start + n] == target[t + n]:
                n += 1
            if n > best[0] + 1:
                best = (n, 2, start)
        for start in tindex.get(key, ()):
            n = 0
            while t + n < len(target) and target[start + n] == target[t + n]:
                n += 1
            if n > best[0] + 1:
                best = (n, 3, start)
        length, command, start = best
        if length < 4:
            literal.append(target[t])
            t += 1
            continue
        if literal:
            out.extend(_varint(((len(literal) - 1) << 2) | 1) + literal)
            literal.clear()
        out.extend(_varint(((length - 1) << 2) | command))
        if command == 2:
            out.extend(_signed(start - source_rel))
            source_rel = start + length
        elif command == 3:
            out.extend(_signed(start - target_rel))
            target_rel = start + length
        t += length
    if literal:
        out.extend(_varint(((len(literal) - 1) << 2) | 1) + literal)
    out += struct.pack("<II", zlib.crc32(source), zlib.crc32(target))
    out += struct.pack("<I", zlib.crc32(bytes(out)))
    return bytes(out)


def bps_apply(patch: bytes, source: bytes) -> bytes:
    if len(patch) < 16 or patch[:4] != b"BPS1":
        raise ValueError("Not a BPS patch")
    if zlib.crc32(patch[:-4]) != _u32(patch, len(patch) - 4):
        raise ValueError("BPS patch checksum mismatch")
    source_size, at = _read_varint(patch, 4)
    target_size, at = _read_varint(patch, at)
    metadata, at = _read_varint(patch, at)
    at += metadata
    if source_size != len(source) or zlib.crc32(source) != _u32(patch, len(patch) - 12):
        raise ValueError("BPS source mismatch")
    target = bytearray()
    source_rel = target_rel = 0
    end = len(patch) - 12
    while at < end:
        data, at = _read_varint(patch, at)
        command, length = data & 3, (data >> 2) + 1
        if len(target) + length > target_size:
            raise ValueError("BPS action exceeds target")
        if command == 0:
            if len(target) + length > len(source):
                raise ValueError("BPS SourceRead exceeds source")
            target += source[len(target):len(target) + length]
        elif command == 1:
            if at + length > end:
                raise ValueError("Truncated BPS TargetRead")
            target += patch[at:at + length]
            at += length
        else:
            offset, at = _read_varint(patch, at)
            offset = -(offset >> 1) if offset & 1 else offset >> 1
            if command == 2:
                source_rel += offset
                if source_rel < 0 or source_rel + length > len(source):
                    raise ValueError("BPS SourceCopy out of range")
                target += source[source_rel:source_rel + length]
                source_rel += length
            else:
                target_rel += offset
                if target_rel < 0 or target_rel >= len(target):
                    raise ValueError("BPS TargetCopy out of range")
                for _ in range(length):
                    target.append(target[target_rel])
                    target_rel += 1
    if at != end or len(target) != target_size or zlib.crc32(bytes(target)) != _u32(patch, len(patch) - 8):
        raise ValueError("BPS target mismatch")
    return bytes(target)


# ----------------------------------------------------------------- recipe --
def _patch_name(output: str, member: str | None) -> str:
    stem = Path(output).name.removeprefix("Joker.").removesuffix(".gar")
    return f"layout/patches/{stem}.{member}.bps" if member else f"layout/patches/{Path(output).name}.bps"


def build(dump_play: bytes, dump_project: bytes, release: dict[str, bytes]):
    """(recipe, {relative path: bytes}) for the patches and shipped members.
    D24 Q1 default: a member with no dump counterpart ships; the owner may
    override one to "derive at install" by giving it a patch source."""
    base = dict(parse_gar(dump_play)["members"])
    files, outputs = {}, []
    for output in ARCHIVES:
        model = parse_gar(release[output])
        members = []
        for name, payload in model["members"]:
            if base.get(name) == payload:
                source = {"dump": f"{DUMP_ARCHIVE}#{name}"}
            elif name in base:
                path = _patch_name(output, name)
                files[path] = bps_encode(base[name], payload)
                source = {"patch": path, "base": f"{DUMP_ARCHIVE}#{name}", "base_sha256": sha256(base[name])}
            else:
                path = f"layout/members/{name}"
                files[path] = payload
                source = {"ship": path}
            source["sha256"] = sha256(payload)
            source["size"] = len(payload)
            members.append({"name": name, "source": source})
        outputs.append({"path": output, "sha256": sha256(release[output]), "size": len(release[output]),
                        "gar": {"version": 2, "hash": model["hash"], "types": model["types"],
                                "align": model["align"]}, "members": members})
    project = release[DUMP_PROJECT]
    path = _patch_name(DUMP_PROJECT, None)
    files[path] = bps_encode(dump_project, project)
    outputs.append({"path": DUMP_PROJECT, "sha256": sha256(project), "size": len(project),
                    "source": {"patch": path, "base": DUMP_PROJECT, "base_sha256": sha256(dump_project),
                               "sha256": sha256(project), "size": len(project)}})
    recipe = {"format": FORMAT, "upstream": UPSTREAM,
              "dump": {"archive": DUMP_ARCHIVE, "archive_sha256": sha256(dump_play)},
              "outputs": outputs,
              "files": {p: sha256(b) for p, b in sorted(files.items())}}
    return recipe, files


def reconstruct(recipe: dict, read_dump, read_resource) -> dict[str, bytes]:
    """{output path: bytes}. read_dump(name) -> bytes | None (user dump),
    read_resource(relative) -> bytes (package files)."""
    if recipe.get("format") != FORMAT:
        raise ValueError("Unknown Restoration layout recipe")
    cache = {}

    def dump_file(name):
        if name not in cache:
            data = read_dump(name)
            if data is None:
                raise ValueError(f"The game dump has no {name}: use the EUR CTR-P-AJRP dump")
            cache[name] = data
        return cache[name]

    def dump_member(ref):
        archive, _, member = ref.partition("#")
        if not member:
            return dump_file(archive)
        members = dict(parse_gar(dump_file(archive))["members"])
        if member not in members:
            raise ValueError(f"The game dump's {archive} has no member {member}")
        return members[member]

    def produce(source):
        if "dump" in source:
            data = dump_member(source["dump"])
        elif "patch" in source:
            base = dump_member(source["base"])
            if sha256(base) != source["base_sha256"]:
                raise ValueError(f"The game dump's {source['base']} differs from the EUR release")
            data = bps_apply(read_resource(source["patch"]), base)
        elif "ship" in source:
            data = read_resource(source["ship"])
        else:
            raise ValueError("Unknown Restoration member source")
        if sha256(data) != source["sha256"]:
            what = source.get("dump") or source.get("base") or source.get("ship")
            raise ValueError(f"Restoration layout member {what} does not match the recipe")
        return data

    result = {}
    for output in recipe["outputs"]:
        if "gar" in output:
            gar = output["gar"]
            data = emit_gar({"hash": gar["hash"], "types": gar["types"], "align": gar["align"],
                             "members": [(m["name"], produce(m["source"])) for m in output["members"]]})
        else:
            data = produce(output["source"])
        if sha256(data) != output["sha256"]:
            raise ValueError(f"Reconstructed {output['path']} does not match the recipe")
        result[output["path"]] = data
    return result


def fingerprinted_inputs(fingerprints: Path = FINGERPRINTS) -> dict[str, str]:
    manifest = json.loads(fingerprints.read_text(encoding="utf-8"))
    return {e["input"].removeprefix("romfs/"): e["input_sha256"] for e in manifest["files"]
            if e.get("input_root") == "restoration" and e["input"].startswith("romfs/")}


def _romfs(path: Path):
    sys.path.insert(0, str(ROOT / "titles/mm3d/dekui/setup"))
    from prepare_romfs_layer import RomFs
    image = RomFs(path, LEVEL3_OFFSET)

    def read(name):
        try:
            return image.read(name)
        except FileNotFoundError:
            return None
    return read


def release_files(directory: Path) -> dict[str, bytes]:
    return {name: (directory / "romfs" / name).read_bytes() for name in (*ARCHIVES, DUMP_PROJECT)}


def _write_if_changed(path: Path, data: bytes) -> bool:
    if path.is_file() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True


def recipe_bytes(recipe: dict) -> bytes:
    return (json.dumps(recipe, indent=1) + "\n").encode("utf-8")


def ship_members(recipe: dict, release: dict[str, bytes]) -> dict[str, bytes]:
    """{relative path: bytes} of the shipped members, cut from the release."""
    out = {}
    for output in recipe["outputs"]:
        if "gar" not in output:
            continue
        payloads = dict(parse_gar(release[output["path"]])["members"])
        for member in output["members"]:
            source = member["source"]
            if "ship" in source:
                data = payloads[member["name"]]
                if sha256(data) != source["sha256"]:
                    raise ValueError(f"Project Restoration member {member['name']} does not match the recipe")
                out[source["ship"]] = data
    return out


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", choices=("generate", "check", "members"))
    parser.add_argument("--romfs", type=Path)
    parser.add_argument("--restoration", type=Path, required=True)
    parser.add_argument("--recipe", type=Path, default=RECIPE_DIR)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args(argv)
    release = release_files(args.restoration)
    expected = fingerprinted_inputs()
    for name, data in release.items():
        if sha256(data) != expected.get(name):
            raise SystemExit(f"Project Restoration file {name} does not match setup-fingerprints.json")
    if args.mode == "members":
        recipe = json.loads((args.recipe / RECIPE_NAME).read_text(encoding="utf-8"))
        for path, data in sorted(ship_members(recipe, release).items()):
            _write_if_changed(args.out / path, data)
            print(f"member {path} {sha256(data)}")
        return 0
    if args.romfs is None:
        parser.error("--romfs is required for generate and check")
    read = _romfs(args.romfs)
    if args.mode == "generate":
        out = args.out or args.recipe
        recipe, files = build(read(DUMP_ARCHIVE), read(DUMP_PROJECT), release)
        changed = 0
        for path, data in sorted(files.items()):
            if "/patches/" in path:
                changed += _write_if_changed(out / path, data)
                print(f"patch {path} {len(data)} B {sha256(data)}")
            else:
                print(f"ship  {path} {len(data)} B {sha256(data)} (cut from the release at packaging)")
        changed += _write_if_changed(out / RECIPE_NAME, recipe_bytes(recipe))
        print(f"recipe {sha256(recipe_bytes(recipe))} changed_files={changed}")
        return 0
    recipe = json.loads((args.recipe / RECIPE_NAME).read_text(encoding="utf-8"))
    members = ship_members(recipe, release)
    for path, digest in recipe["files"].items():
        data = members[path] if path in members else (args.recipe / path).read_bytes()
        if sha256(data) != digest:
            raise SystemExit(f"FAIL recipe file {path} changed")

    def resource(path):
        return members[path] if path in members else (args.recipe / path).read_bytes()
    result = reconstruct(recipe, read, resource)
    for name, data in result.items():
        if sha256(data) != expected[name] or data != release[name]:
            raise SystemExit(f"FAIL {name}")
        print(f"reconstructed {name} {sha256(data)} == fingerprint input_sha256")
    print("check: the recipe reconstructs the three Restoration layout files byte-identical")
    return 0


if __name__ == "__main__":
    sys.exit(main())
