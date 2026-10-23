"""Results the release computed from the supported RomFS, keyed to room content.

Two installation stages analyse the user's rooms: the interior sun (which
triangles may stop casting toward each of 64 light directions) and the scene
lights (where the rooms' own light sources are, their colour and reach). Their
results are TriAevum's own analysis, not game content: per-triangle direction
bits and light placements. The release ships those results for the rooms of
the supported revision, so Forge does not repeat about a quarter of an hour of
analysis on every machine.

The tables carry no game geometry, texture, text or code. Every entry is keyed
by a hash of one room's content and applies only where the user's own RomFS
has exactly that content: Forge still reads each room from the user's RomFS
(the interior sun welds its geometry from it, the lights hash its file), and
computes locally any room a table does not cover. A table never changes what
is computed, only whether it is computed again: tests prove the result is
byte-identical.

Interior-sun masks: the native table of oot3d_interior_sun_prebuild.cpp
("TISP"), keyed by the room's content fingerprint. Scene lights (JSON):

    {"schema": "triaevum.scene-lights-rooms", "version": 1,
     "catalog_version": 1, "generator_revision": 4, "romfs_sha256": "<64 hex>",
     "rooms": {"<room file SHA-256>": [emitter, ...]}}

with emitters exactly as in the scene-light catalog.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import re
import struct
import subprocess
import tempfile
from pathlib import Path

try:
    from .common import atomic_write_bytes, load_json_object
    from .interior_sun import CRITERION_VERSION
    from .precompiled_titles import checked_file
    from .precompiled_title_layout import artifact
    from . import scene_lights
except ImportError:
    from common import atomic_write_bytes, load_json_object
    from interior_sun import CRITERION_VERSION
    from precompiled_titles import checked_file
    from precompiled_title_layout import artifact
    import scene_lights

FORMAT = "triaevum_precomputed_tables_v1"
MASKS_ROLE = "precomputed_interior_sun"
LIGHTS_ROLE = "precomputed_scene_lights"
MASKS_PATH = "forge/precomputed/interior-sun.masks"
LIGHTS_PATH = "forge/precomputed/scene-lights.json"
LIGHTS_SCHEMA = "triaevum.scene-lights-rooms"
MASKS_MAGIC = b"TISP"
MASKS_FORMAT = 1
MAX_MASKS_BYTES = 256 * 1024 * 1024
MAX_LIGHTS_BYTES = 16 * 1024 * 1024
MAX_TRIANGLES = 500_000
EMITTER_KEYS = {"category", "position", "radius", "gain", "colour", "texture", "surfaces"}
HEX16 = re.compile(r"[0-9a-f]{16}")
HEX64 = re.compile(r"[0-9a-f]{64}")
TEXTURE_NAME = re.compile(r"[A-Za-z0-9_.-]{1,64}")


def _checksum(data: bytes) -> int:
    value = 1469598103934665603
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def read_interior_sun_masks(path: Path) -> dict:
    """The table's summary; anything but fingerprints, counts and masks fails."""
    if path.is_symlink() or not path.is_file() or not 24 <= path.stat().st_size <= MAX_MASKS_BYTES:
        raise ValueError("Invalid interior-sun mask table file")
    data = path.read_bytes()
    magic, version, criterion, count = struct.unpack_from("<4s3I", data)
    if magic != MASKS_MAGIC or version != MASKS_FORMAT or criterion != CRITERION_VERSION:
        raise ValueError("Incompatible interior-sun mask table")
    if struct.unpack_from("<Q", data, len(data) - 8)[0] != _checksum(data[:-8]):
        raise ValueError("Interior-sun mask table checksum differs")
    masks_at = 16 + count * 24
    if masks_at > len(data) - 8 or (len(data) - 8 - masks_at) % 8:
        raise ValueError("Invalid interior-sun mask table extent")
    previous, following, fallbacks = -1, 0, 0
    for index in range(count):
        fingerprint, triangles, kind, first = struct.unpack_from("<QIIQ", data, 16 + index * 24)
        if (fingerprint <= previous or kind > 1 or first != following
                or (kind == 1 and triangles) or triangles > MAX_TRIANGLES):
            raise ValueError("Invalid interior-sun mask table entry")
        previous, following, fallbacks = fingerprint, following + triangles, fallbacks + kind
    if following != (len(data) - 8 - masks_at) // 8:
        raise ValueError("Invalid interior-sun mask table mask count")
    return {"entries": count, "masks": following, "bound_fallbacks": fallbacks}


def _number(value) -> bool:
    return type(value) in (int, float) and math.isfinite(value)


def read_scene_lights_table(path: Path) -> dict:
    """The validated table; emitters may hold only the catalog's own fields."""
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_LIGHTS_BYTES:
        raise ValueError("Invalid scene-light table file")
    table = load_json_object(path)
    rooms = table.get("rooms")
    if (set(table) != {"schema", "version", "catalog_version", "generator_revision", "romfs_sha256", "rooms"}
            or table["schema"] != LIGHTS_SCHEMA or table["version"] != 1
            or table["catalog_version"] != scene_lights.CATALOG_VERSION
            or table["generator_revision"] != scene_lights.GENERATOR_REVISION
            or not isinstance(table["romfs_sha256"], str) or not HEX64.fullmatch(table["romfs_sha256"])
            or not isinstance(rooms, dict) or not rooms):
        raise ValueError("Incompatible scene-light table")
    for digest, emitters in rooms.items():
        if not HEX64.fullmatch(digest) or not isinstance(emitters, list):
            raise ValueError("Invalid scene-light table room")
        for emitter in emitters:
            if (not isinstance(emitter, dict) or set(emitter) - EMITTER_KEYS
                    or {"category", "position", "radius", "gain", "colour"} - set(emitter)
                    or emitter["category"] not in scene_lights.MERGE
                    or not isinstance(emitter["position"], list) or len(emitter["position"]) != 3
                    or not isinstance(emitter["colour"], list) or len(emitter["colour"]) != 3
                    or not all(map(_number, [*emitter["position"], *emitter["colour"],
                                             emitter["radius"], emitter["gain"]]))
                    or ("texture" in emitter and (not isinstance(emitter["texture"], str)
                                                  or not TEXTURE_NAME.fullmatch(emitter["texture"])))
                    or ("surfaces" in emitter and (not isinstance(emitter["surfaces"], list)
                                                   or len(emitter["surfaces"]) > scene_lights.MAX_SURFACES
                                                   or not all(isinstance(s, str) and HEX16.fullmatch(s)
                                                              for s in emitter["surfaces"])))):
                raise ValueError("Invalid scene-light table emitter")
    return table


def scene_lights_table(romfs: Path, romfs_sha256: str) -> bytes:
    rooms = {}
    for path, data in scene_lights.romfs_room_files(romfs):
        match = scene_lights.ROOM_FILE.fullmatch(path)
        if match and match.group(1) in scene_lights.SCENE_IDS:
            rooms[hashlib.sha256(data).hexdigest()] = scene_lights.room_emitters(data)
    return json.dumps({"schema": LIGHTS_SCHEMA, "version": 1,
                       "catalog_version": scene_lights.CATALOG_VERSION,
                       "generator_revision": scene_lights.GENERATOR_REVISION,
                       "romfs_sha256": romfs_sha256, "rooms": dict(sorted(rooms.items()))},
                      separators=(",", ":"), allow_nan=False).encode("ascii")


def generate(*, runtime: Path, romfs: Path, romfs_sha256: str, output: Path) -> dict:
    """Publisher step: both tables from the supported RomFS, with the release
    runtime's own builder."""
    if not HEX64.fullmatch(romfs_sha256):
        raise ValueError("Invalid RomFS identity")
    output.mkdir(parents=True, exist_ok=True)
    masks = output / Path(MASKS_PATH).name
    with tempfile.TemporaryDirectory(prefix="interior-sun-") as temporary:
        manifest = Path(temporary) / "result.json"
        subprocess.run([str(runtime), "--prebuild-interior-sun", "--romfs", str(romfs),
                        "--renderer-cache-directory", str(Path(temporary) / "cache"),
                        "--manifest", str(manifest), "--export-mask-table", str(masks)],
                       check=True, capture_output=True, timeout=7200)
        result = load_json_object(manifest)
    lights = output / Path(LIGHTS_PATH).name
    atomic_write_bytes(lights, scene_lights_table(romfs, romfs_sha256))
    return {"interior_sun": read_interior_sun_masks(masks), "interior_sun_built": result.get("built"),
            "scene_light_rooms": len(read_scene_lights_table(lights)["rooms"])}


def bind_precomputed_tables(catalog: dict, masks: Path, lights: Path) -> tuple[dict, list[dict]]:
    """Every title names both tables. They are computed from a catalogued
    revision's RomFS, but apply by room content: another revision's identical
    rooms use them too, and its other rooms are computed locally."""
    read_interior_sun_masks(masks)
    romfs = read_scene_lights_table(lights)["romfs_sha256"]
    titles = catalog.get("titles", [])
    if not any(title.get("inputs", {}).get("romfs", {}).get("sha256") == romfs for title in titles):
        raise ValueError("The precomputed tables come from an uncatalogued RomFS")
    records = {"interior_sun_masks": artifact(masks, MASKS_PATH), "scene_lights": artifact(lights, LIGHTS_PATH)}
    result = copy.deepcopy(catalog)
    for title in result["titles"]:
        title["precomputed_tables"] = {"format": FORMAT, "source_romfs_sha256": romfs, **copy.deepcopy(records)}
    return result, [{"source": str(masks.resolve()), "path": MASKS_PATH, "role": MASKS_ROLE},
                    {"source": str(lights.resolve()), "path": LIGHTS_PATH, "role": LIGHTS_ROLE}]


def validate_precomputed_tables(root: Path, catalog: dict, declared: dict) -> None:
    """Release audit: declared tables are exactly the catalogued, valid ones."""
    roles = {MASKS_ROLE: "interior_sun_masks", LIGHTS_ROLE: "scene_lights"}
    actual = {path for path, record in declared.items() if record["role"] in roles}
    expected = set()
    sources = {title["inputs"]["romfs"]["sha256"] for title in catalog.get("titles", [])}
    for title in catalog.get("titles", []):
        tables = title.get("precomputed_tables")
        if tables is None:
            continue
        if tables.get("format") != FORMAT or tables.get("source_romfs_sha256") not in sources:
            raise ValueError("Precomputed tables come from an uncatalogued RomFS")
        for role, key in roles.items():
            record = tables[key]
            if declared.get(record["path"], {}).get("role") != role:
                raise ValueError("Precomputed table lacks its explicit release role")
            path = checked_file(root, record)
            if role == MASKS_ROLE:
                read_interior_sun_masks(path)
            elif read_scene_lights_table(path)["romfs_sha256"] != tables["source_romfs_sha256"]:
                raise ValueError("Scene-light table names another RomFS")
            expected.add(record["path"])
    if actual != expected:
        raise ValueError("Uncatalogued precomputed table")


def title_tables(root: Path, title: dict) -> dict[str, Path]:
    """Forge: the title's integrity-checked tables, or none."""
    tables = title.get("precomputed_tables")
    if not tables:
        return {}
    if tables.get("format") != FORMAT:
        raise ValueError("Incompatible precomputed tables")
    return {key: checked_file(root, tables[key]) for key in ("interior_sun_masks", "scene_lights")}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--runtime", type=Path, required=True, help="The release's TriAevum runtime")
    parser.add_argument("--romfs", type=Path, required=True, help="The supported revision's RomFS")
    parser.add_argument("--romfs-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True, help="Publisher directory, outside the package")
    args = parser.parse_args()
    print(json.dumps(generate(runtime=args.runtime, romfs=args.romfs,
                              romfs_sha256=args.romfs_sha256, output=args.output), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
