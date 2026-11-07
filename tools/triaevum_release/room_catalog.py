"""Content-checked publisher room analysis; no user compiler or geometry table.

Unknown/changed/missing rooms fail closed. This is not a geometry analyser:
the title's publisher must regenerate the table for new supported content.
"""
import hashlib
import json
import math
from pathlib import Path
import re
try:
    from .common import atomic_write_bytes, atomic_write_json
    from .model_catalog import unpack_lzs
    from .water_textures import _romfs_image_files
except ImportError:
    from common import atomic_write_bytes, atomic_write_json
    from model_catalog import unpack_lzs
    from water_textures import _romfs_image_files


def compact(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True, allow_nan=False).encode("ascii")


def prepare(data_root: Path, recipe: dict, romfs: Path, report=lambda *_: None) -> Path:
    def require(ok):
        if not ok:
            raise ValueError("Invalid or uncovered room analysis; regenerate with this title's publisher tools")
    def hexd(value, length):
        return isinstance(value, str) and re.fullmatch(r"[a-f0-9]{%d}" % length, value) is not None
    def number(value):
        return type(value) in (int, float) and math.isfinite(value)
    def triple(value, colour=False):
        return isinstance(value, list) and len(value) == 3 and all(number(n) and (not colour or 0 <= n <= 4) for n in value)
    profile = recipe["room_catalog"]
    title, directory, generator = profile["title"], profile["directory"], profile["generator_sha256"]
    source, payload = recipe["inputs"]["romfs"]["sha256"], profile["catalog_json"]
    require(isinstance(payload, str) and payload.isascii() and len(payload) <= 16 * 1024 * 1024)
    catalog = json.loads(payload)
    require(isinstance(title, str) and re.fullmatch(r"[A-Za-z0-9-]{1,32}", title) and
            isinstance(directory, str) and re.fullmatch(r"[A-Za-z0-9_]+", directory) and
            hexd(source, 64) and hexd(generator, 64))
    require(type(profile["version"]) is int and profile["version"] == 1 and
            catalog["schema"] == "triaevum.scene-lights" and type(catalog["version"]) is int and
            catalog["version"] == 1 and catalog["title"] == title and catalog["rejected"] == [])
    rows = catalog["rooms"]
    require(isinstance(rows, list) and 0 < len(rows) <= 4096)
    room_name = lambda name: re.fullmatch(re.escape(directory) + r"/.+_[0-9]+_info\.zsi", name) is not None
    expected, keys, emitters = {}, set(), 0
    for row in rows:
        path, digest = row["source"], row["sourceSha256"]
        require(isinstance(path, str) and room_name(path) and ".." not in path and "\\" not in path and hexd(digest, 64))
        require(type(row["scene"]) is int and 0 <= row["scene"] <= 65535 and
                type(row["room"]) is int and -1 <= row["room"] <= 127 and hexd(row["fingerprint"], 16))
        key = (row["scene"], row["room"], row["fingerprint"])
        require(key not in keys and path not in expected)
        keys.add(key)
        expected[path] = digest
        lights = row["emitters"]
        require(isinstance(lights, list) and len(lights) <= 256)
        emitters += len(lights)
        for light in lights:
            require(light["category"] in ("lamp", "window-lamp", "doorway", "magic-glow", "light-shaft", "fire-lava") and
                    triple(light["position"]) and triple(light["colour"], True) and number(light["radius"]) and light["radius"] > 0 and
                    number(light["gain"]) and 0 <= light["gain"] <= 4 and type(light["independentEmission"]) is bool and
                    isinstance(light["surfaces"], list) and 0 < len(light["surfaces"]) <= 2 and
                    all(hexd(s, 16) for s in light["surfaces"]))
    excluded = profile.get("excluded_rooms", [])
    require(isinstance(excluded, list) and len(excluded) <= 4096)
    for row in excluded:
        path, digest = row["source"], row["sourceSha256"]
        require(isinstance(path, str) and room_name(path) and ".." not in path and "\\" not in path and
                path not in expected and hexd(digest, 64) and isinstance(row["reason"], str) and 0 < len(row["reason"]) <= 256)
        expected[path] = digest
    if romfs.is_file():
        with romfs.open("rb") as stream:
            require(hashlib.file_digest(stream, "sha256").hexdigest() == source)
        files = _romfs_image_files(romfs, (directory,))
    else:
        files = ((f"{directory}/{p.name}", p.read_bytes()) for p in sorted((romfs / directory).iterdir()) if p.is_file() and room_name(f"{directory}/{p.name}"))
    found = set()
    for path, packed in files:
        if not room_name(path):
            continue
        require(path in expected and path not in found and hashlib.sha256(unpack_lzs(packed)).hexdigest() == expected[path])
        found.add(path)
    require(len(found) == len(expected))
    identity = {"catalog_version": 1, "room_table_revision": 1, "romfs_sha256": source,
                "generator_sha256": generator, "profile_sha256": hashlib.sha256(compact(profile)).hexdigest()}
    key = hashlib.sha256(repr(sorted(identity.items())).encode("ascii")).hexdigest()
    out = data_root / "cache" / "scene-lights" / key
    output, receipt = out / "scene_lights.json", out / "receipt.json"
    if output.is_file() and receipt.is_file():
        try:
            prior = json.loads(receipt.read_text())
            if prior.get("source") == identity and prior.get("catalog_sha256") == hashlib.sha256(output.read_bytes()).hexdigest():
                return output
        except (ValueError, AttributeError):
            pass
    report("scene_lights", "Matching the title's computed room analysis to verified local content...")
    payload = payload.encode("ascii")
    out.mkdir(parents=True, exist_ok=True)
    atomic_write_bytes(output, payload)
    atomic_write_json(receipt, {"source": identity, "summary": {"rooms": len(rows), "emitters": emitters, "content_verified": len(found)},
                                "catalog_sha256": hashlib.sha256(payload).hexdigest()})
    return output
