#!/usr/bin/env python3
"""Generates MM3D's developer warp catalog from 2Ship2Harkinian (CC0-1.0).

The F1 Dev Tools tab lists MM3D's destinations the way 2Ship2Harkinian's
Better Map Select does: one row per scene of its scene table (columns 6-8:
entrance scene, Better index, human name), in Better order, each with the
spawns of its entrance group. A spawn is named by meaning from 2S2H's
randomizer region exits (`EXIT(TO, FROM)`: FROM is the spawn entering the
region, "from <TO scene>"). Scene and spawn of each row come from 2S2H's copy
of the decomp entrance table (`z_scene_table.c`).

MM3D keeps the N64 entrance groups. Its differences are facts of the MM3D EUR
v1.1 / USA v1.1 entrance table, stated below as indices only (the runtime
checks every row against the live table before offering it, so a wrong row is
shown disabled, never guessed): 34 added spawns, the telescope spawn that leads
to a separate scene, and two fishing-hole groups.

The curated zones (category, name, entrance, suggested day/time/form) are kept
in this file.

usage: generate_mm3d_dev_warp_catalog.py <2ship2harkinian root> <output .inc>
       generate_mm3d_dev_warp_catalog.py --check <2ship2harkinian root> <existing .inc>
"""
from __future__ import annotations

import glob
import hashlib
import re
import sys
from pathlib import Path

SCENE_TABLE = "mm/include/tables/scene_table.h"
SCENE_ENUM = "mm/include/z64scene.h"
ENTRANCE_TABLE = "mm/src/code/z_scene_table.c"
REGIONS = "mm/2s2h/Rando/Logic/Regions"
LOGIC = "mm/2s2h/Rando/Logic/Logic.cpp"

# MM3D's own rows: (group, spawn, layer-0 scene, layer-0 spawn). Statically
# decoded from the MM3D table at 0x69FB44 and compared with the decomp
# (analysis/devwarp/design-2026-11, n64_vs_mm3d.json): rows MM3D adds to N64
# groups (grotto spawn 7 is an empty slot on N64), and the single N64 row MM3D
# sends elsewhere.
MM3D_EXTRA_SPAWNS = [
    (0x06, 11, 0x00, 11), (0x06, 12, 0x00, 12), (0x0A, 7, 0x07, 7), (0x0A, 17, 0x07, 17),
    (0x10, 16, 0x13, 16), (0x11, 15, 0x14, 15), (0x18, 3, 0x1B, 3),
    (0x1A, 7, 0x1D, 7), (0x1F, 7, 0x22, 7), (0x28, 11, 0x2B, 11),
    (0x28, 12, 0x2B, 12), (0x28, 13, 0x2B, 13), (0x28, 14, 0x2B, 14),
    (0x28, 15, 0x2B, 15), (0x28, 16, 0x2B, 16), (0x34, 14, 0x37, 14),
    (0x34, 15, 0x37, 15), (0x35, 10, 0x38, 10), (0x35, 11, 0x38, 11),
    (0x38, 7, 0x3B, 7), (0x3D, 3, 0x40, 3), (0x40, 6, 0x43, 6),
    (0x42, 11, 0x45, 11), (0x42, 12, 0x45, 12), (0x43, 5, 0x46, 5),
    (0x45, 5, 0x48, 5), (0x48, 2, 0x4B, 2), (0x4A, 5, 0x4D, 5),
    (0x4D, 9, 0x50, 9), (0x50, 3, 0x53, 3), (0x55, 4, 0x58, 4),
    (0x57, 9, 0x5A, 9), (0x59, 4, 0x5C, 4), (0x64, 1, 0x67, 1),
    (0x6C, 11, 0x6F, 11),
]
# Termina Field spawn 10: N64 shows the telescope view in Termina Field
# (scene 0x2D); MM3D has a separate scene for it.
MM3D_CHANGED_SPAWNS = {(0x2A, 10): (0x73, 10, "telescope view, MM3D scene")}
# Groups MM3D adds after the N64 ones: (group, scene, name).
MM3D_EXTRA_GROUPS = [(0x6E, 0x71, "Swamp Fishing Hole"), (0x6F, 0x72, "Ocean Fishing Hole")]

# Curated zones: (category, name, ENTR token, spawn, day, "HHMM", form, note).
# None keeps the current value. Forms: human, deku, goron, zora, deity.
ZONES = [
    ("Clock Town", "South Clock Town (Clock Tower door)", "SOUTH_CLOCK_TOWN", 0, 1, "0600", "human", ""),
    ("Clock Town", "South Clock Town (from Termina Field)", "SOUTH_CLOCK_TOWN", 1, 1, "1200", "human", ""),
    ("Clock Town", "North Clock Town", "NORTH_CLOCK_TOWN", 2, 1, "1200", "human", ""),
    ("Clock Town", "East Clock Town", "EAST_CLOCK_TOWN", 1, 1, "1200", "human", ""),
    ("Clock Town", "West Clock Town", "WEST_CLOCK_TOWN", 1, 1, "1200", "human", ""),
    ("Clock Town", "Laundry Pool", "LAUNDRY_POOL", 0, 1, "2000", "human", ""),
    ("Clock Town", "Clock Tower Interior", "CLOCK_TOWER_INTERIOR", 1, None, None, None, ""),
    ("Clock Town", "Clock Tower Rooftop", "CLOCK_TOWER_ROOFTOP", 0, 3, "0000", "human",
     "Scripted for the final night; check the arrival."),
    ("Termina Field", "Termina Field (south gate)", "TERMINA_FIELD", 6, 1, "1200", "human", ""),
    ("Termina Field", "Termina Field (Astral Observatory)", "TERMINA_FIELD", 9, 1, "2200", "human", ""),
    ("Termina Field", "Milk Road", "MILK_ROAD", 0, 1, "1200", None, ""),
    ("Termina Field", "Romani Ranch", "ROMANI_RANCH", 0, 1, "0600", None, ""),
    ("Southern Swamp", "Road to Southern Swamp", "ROAD_TO_SOUTHERN_SWAMP", 0, 1, "1200", None, ""),
    ("Southern Swamp", "Southern Swamp (poisoned)", "SOUTHERN_SWAMP_POISONED", 0, 1, "1200", "deku", ""),
    ("Southern Swamp", "Southern Swamp (cleared)", "SOUTHERN_SWAMP_CLEARED", 0, 1, "1200", None,
     "Story state: the swamp after Woodfall Temple."),
    ("Southern Swamp", "Deku Palace", "DEKU_PALACE", 0, 1, "1200", "deku", ""),
    ("Southern Swamp", "Woods of Mystery", "WOODS_OF_MYSTERY", 0, 1, "1200", None, ""),
    ("Southern Swamp", "Woodfall", "WOODFALL", 0, 1, "1200", "deku", ""),
    ("Southern Swamp", "Woodfall Temple", "WOODFALL_TEMPLE", 0, None, None, "deku", ""),
    ("Mountain", "Path to Mountain Village", "PATH_TO_MOUNTAIN_VILLAGE", 0, 1, "1200", None, ""),
    ("Mountain", "Mountain Village (winter)", "MOUNTAIN_VILLAGE_WINTER", 6, 1, "1200", None, ""),
    ("Mountain", "Mountain Village (spring)", "MOUNTAIN_VILLAGE_SPRING", 0, 1, "1200", None,
     "Story state: spring after Snowhead Temple."),
    ("Mountain", "Goron Village (winter)", "GORON_VILLAGE_WINTER", 0, 1, "1200", "goron", ""),
    ("Mountain", "Snowhead", "SNOWHEAD", 0, 1, "1200", "goron", ""),
    ("Mountain", "Snowhead Temple", "SNOWHEAD_TEMPLE", 0, None, None, "goron", ""),
    ("Great Bay", "Great Bay Coast", "GREAT_BAY_COAST", 0, 1, "1200", None, ""),
    ("Great Bay", "Zora Cape", "ZORA_CAPE", 0, 1, "1200", "zora", ""),
    ("Great Bay", "Zora Hall", "ZORA_HALL", 0, 1, "1200", "zora", ""),
    ("Great Bay", "Pirates' Fortress (exterior)", "PIRATES_FORTRESS_EXTERIOR", 0, 1, "1200", "zora", ""),
    ("Great Bay", "Pinnacle Rock", "PINNACLE_ROCK", 0, None, None, "zora", ""),
    ("Great Bay", "Great Bay Temple", "GREAT_BAY_TEMPLE", 0, None, None, "zora", ""),
    ("Ikana", "Road to Ikana", "ROAD_TO_IKANA", 0, 1, "1200", None, ""),
    ("Ikana", "Ikana Graveyard", "IKANA_GRAVEYARD", 0, 1, "2200", None, ""),
    ("Ikana", "Ikana Canyon", "IKANA_CANYON", 0, 1, "1200", None, ""),
    ("Ikana", "Stone Tower", "STONE_TOWER", 0, 1, "1200", None, ""),
    ("Ikana", "Inverted Stone Tower", "STONE_TOWER_INVERTED", 0, 1, "1200", None, ""),
    ("Ikana", "Stone Tower Temple", "STONE_TOWER_TEMPLE", 0, None, None, None, ""),
    ("Ikana", "Stone Tower Temple (inverted)", "STONE_TOWER_TEMPLE_INVERTED", 0, None, None, None, ""),
    ("Ikana", "Ancient Castle of Ikana", "IKANA_CASTLE", 1, None, None, None, ""),
    ("Moon", "The Moon", "THE_MOON", 0, 3, "0000", None, "Normally reached from the rooftop; check the arrival."),
    ("Moon", "Moon Deku Trial", "MOON_DEKU_TRIAL", 0, None, None, "deku", ""),
    ("Moon", "Moon Goron Trial", "MOON_GORON_TRIAL", 0, None, None, "goron", ""),
    ("Moon", "Moon Zora Trial", "MOON_ZORA_TRIAL", 0, None, None, "zora", ""),
    ("Moon", "Moon Link Trial", "MOON_LINK_TRIAL", 0, None, None, "human", ""),
    ("Boss rooms", "Odolwa's Lair", "ODOLWAS_LAIR", 0, None, None, None, "Fierce Deity is allowed here."),
    ("Boss rooms", "Goht's Lair", "GOHTS_LAIR", 0, None, None, None, "Fierce Deity is allowed here."),
    ("Boss rooms", "Gyorg's Lair", "GYORGS_LAIR", 0, None, None, "zora", "Fierce Deity is allowed here."),
    ("Boss rooms", "Twinmold's Lair", "TWINMOLDS_LAIR", 0, None, None, None, "Fierce Deity is allowed here."),
    ("Boss rooms", "Igos du Ikana's Lair", "IGOS_DU_IKANAS_LAIR", 0, None, None, None, ""),
    ("Boss rooms", "Majora's Lair", "MAJORAS_LAIR", 0, None, None, None, "Fierce Deity is allowed here."),
    ("Interiors", "Stock Pot Inn", "STOCK_POT_INN", 0, 1, "1200", None, ""),
    ("Interiors", "Milk Bar", "MILK_BAR", 0, 1, "2200", None, ""),
    ("Interiors", "Astral Observatory", "ASTRAL_OBSERVATORY", 0, 1, "2200", None, ""),
    ("Interiors", "Mayor's Residence", "MAYORS_RESIDENCE", 0, 1, "1200", None, ""),
    ("Interiors", "Curiosity Shop", "CURIOSITY_SHOP", 0, 1, "2200", None, ""),
    ("Interiors", "Bomb Shop", "BOMB_SHOP", 0, 1, "1200", None, ""),
    ("Interiors", "Swordsman's School", "SWORDMANS_SCHOOL", 0, 1, "1200", None, ""),
    ("Interiors", "Post Office", "POST_OFFICE", 0, 1, "1200", None, ""),
    ("Interiors", "Goron Shrine", "GORON_SHRINE", 0, None, None, "goron", ""),
    ("Interiors", "Magic Hags' Potion Shop", "MAGIC_HAGS_POTION_SHOP", 0, 1, "1200", None, ""),
    ("Mini-dungeons", "Swamp Spider House", "SWAMP_SPIDER_HOUSE", 0, None, None, None, ""),
    ("Mini-dungeons", "Oceanside Spider House", "OCEANSIDE_SPIDER_HOUSE", 0, None, None, None, ""),
    ("Mini-dungeons", "Pirates' Fortress (interior)", "PIRATES_FORTRESS_INTERIOR", 0, None, None, "zora", ""),
    ("Mini-dungeons", "Beneath the Well", "BENEATH_THE_WELL", 0, None, None, None, ""),
    ("Mini-dungeons", "Beneath the Graveyard", "BENEATH_THE_GRAVERYARD", 0, 1, "2200", None, ""),
    ("Mini-dungeons", "Secret Shrine", "SECRET_SHRINE", 0, None, None, None, ""),
    ("Mini-dungeons", "Deku Shrine", "DEKU_SHRINE", 0, None, None, "deku", ""),
    ("Mini-dungeons", "Ghost Hut", "GHOST_HUT", 0, None, None, None, ""),
    ("Mini-dungeons", "Fairy Fountain (Clock Town)", "FAIRY_FOUNTAIN", 0, None, None, None, ""),
]
# Zones whose arrival has a reviewed live receipt (TRIAEVUM_DEV_WARP arrived
# and settled from a base savestate). The others are listed as unverified.
PROVEN_ZONES: set[str] = set()

FORMS = {"deity": 0, "goron": 1, "zora": 2, "deku": 3, "human": 4}


def clock(hhmm: str) -> int:
    """MM's clock: 0x10000 per day, 0x0000 at midnight. 06:00 is one past the
    boundary, as 2S2H's map select stages it (morning starts after 06:00)."""
    minutes = int(hhmm[:2]) * 60 + int(hhmm[2:])
    value = (minutes * 0x10000 + 720) // 1440
    return value + 1 if minutes == 6 * 60 else value


def slug(text: str) -> str:
    return re.sub(r"-+", "-", re.sub(r"[^a-z0-9]+", "-", text.lower().replace("'", ""))).strip("-")


def c_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


class Donor:
    def __init__(self, root: Path):
        self.root = root
        self.files: dict[str, str] = {}
        scene_table = self.read(SCENE_TABLE)
        # Scene id -> (SCENE_ enum, ENTR token, Better index, human name).
        self.scenes: dict[int, tuple[str, str, int, str]] = {}
        self.scene_ids: dict[str, int] = {}
        for match in re.finditer(r"/\* (0x[0-9A-F]+) \*/ DEFINE_SCENE\((\w+),\s*(SCENE_\w+),(.*)\)\s*$",
                                 scene_table, re.M):
            columns = [c.strip() for c in re.split(r",(?![^()]*\))", match.group(4))]
            human = re.search(r'"([^"]*)"', match.group(4)).group(1)
            scene = int(match.group(1), 16)
            self.scenes[scene] = (match.group(3), columns[-3], int(columns[-2]), human)
            self.scene_ids[match.group(3)] = scene
        enums = self.read(SCENE_ENUM)
        self.groups = {m.group(2): int(m.group(1), 16)
                       for m in re.finditer(r"/\* (0x[0-9A-F]+) \*/ ENTR_SCENE_(\w+),", enums)}
        self.group_tokens = {group: token for token, group in self.groups.items()}
        self.entrances = self.parse_entrances(self.read(ENTRANCE_TABLE))
        self.spawn_names = self.parse_exits()

    def read(self, relative: str) -> str:
        data = (self.root / relative).read_bytes()
        self.files[relative] = hashlib.sha256(data).hexdigest()
        return data.decode("utf-8").replace("\r\n", "\n")

    def parse_entrances(self, source: str) -> dict[int, list[tuple[int, int] | None]]:
        """Entrance group -> per spawn, its layer-0 (scene, spawn)."""
        arrays = {}
        for match in re.finditer(r"static EntranceTableEntry (s\w+)\[\] = \{(.*?)\};", source, re.S):
            rows = re.findall(r"\{\s*(-?)(SCENE_\w+),\s*(\d+),\s*(0x[0-9A-Fa-f]+)\s*\}", match.group(2))
            arrays[match.group(1)] = [(self.scene_ids[name], int(spawn)) for _, name, spawn, _ in rows]
        tables = {match.group(1): re.findall(r"(s\w+|NULL)", match.group(2))
                  for match in re.finditer(r"static EntranceTableEntry\* (s\w+)\[\] = \{(.*?)\};", source, re.S)}
        groups = {}
        for match in re.finditer(r"/\* (0x[0-9A-F]+) \*/ SCENE_ENTRANCE\((\w+), \"\w*\"\)", source):
            groups[int(match.group(1), 16)] = [arrays[name][0] if name in arrays and arrays[name] else None
                                               for name in tables[match.group(2)]]
        return groups

    def parse_exits(self) -> dict[tuple[int, int], list[tuple[str, str]]]:
        """(group, spawn) -> [(neighbour scene, comment)] from the rando region exits."""
        human_by_token: dict[str, str] = {}
        for _, (enum, token, _, human) in sorted(self.scenes.items()):
            human_by_token.setdefault(token, human)
        names: dict[tuple[int, int], list[tuple[str, str]]] = {}
        entrance = r"ENTRANCE\((\w+),\s*(\d+)\)"
        files = sorted(glob.glob(str(self.root / REGIONS / "*.cpp"))) + [str(self.root / LOGIC)]
        for path in files:
            relative = Path(path).relative_to(self.root).as_posix()
            text = self.read(relative)
            for region in re.finditer(r"Regions\[(RR_\w+)\] = RandoRegion\{(.*?)\n    \};", text, re.S):
                body = region.group(2)
                scene = re.search(r"\.sceneId = (SCENE_\w+)", body)
                region_scene = (self.scenes[self.scene_ids[scene.group(1)]][3]
                                if scene and scene.group(1) in self.scene_ids else "")
                for exit_ in re.finditer(r"EXIT\(\s*" + entrance + r"\s*,\s*(?:" + entrance +
                                         r"|ONE_WAY_EXIT)\s*,[^\n]*?\)(?:,)?\s*(//[^\n]*)?", body):
                    comment = (exit_.group(5) or "").strip("/ ").strip()
                    to = (exit_.group(1), int(exit_.group(2)))
                    if exit_.group(3):
                        # The spawn entering this region comes from the scene the exit leads to.
                        source_key = (exit_.group(3), int(exit_.group(4)))
                        if source_key[0] in self.groups:
                            names.setdefault((self.groups[source_key[0]], source_key[1]), []).append(
                                (human_by_token.get(to[0], to[0].replace("_", " ").title()), comment))
                    if to[0] in self.groups:
                        names.setdefault((self.groups[to[0]], to[1]), []).append(
                            (region_scene, "one way" if not exit_.group(3) else ""))
        return names

    def spawn_label(self, group: int, spawn: int) -> str | None:
        candidates = self.spawn_names.get((group, spawn), [])
        scenes: list[str] = []
        comments: dict[str, set[str]] = {}
        for scene, comment in candidates:
            if not scene:
                if comment and comment != "one way":
                    scenes.append(comment.lower())
                continue
            if scene not in scenes:
                scenes.append(scene)
            if comment and comment != "one way":
                comments.setdefault(scene, set()).add(comment)
        if not scenes:
            return None
        preferred = [s for s in scenes if s != "Lone Peak Shrine & Grottos"] or scenes
        first = preferred[0]
        label = "from " + first if first[0].isupper() else first
        if len(comments.get(first, ())) == 1:
            label += " (" + next(iter(comments[first])).lower() + ")"
        if len(preferred) > 1:
            label += " / " + preferred[1]
        return label


def build(root: Path) -> tuple[str, dict]:
    donor = Donor(root)
    changed = dict(MM3D_CHANGED_SPAWNS)
    extras: dict[int, dict[int, tuple[int, int]]] = {}
    for group, spawn, scene, row_spawn in MM3D_EXTRA_SPAWNS:
        extras.setdefault(group, {})[spawn] = (scene, row_spawn)

    # One slice of rows per entrance group, in group order.
    rows: list[tuple[str, int, int, int]] = []
    slices: dict[int, tuple[int, int]] = {}
    for group, spawns in sorted(donor.entrances.items()):
        token = donor.group_tokens[group]
        first = len(rows)
        count = max(len(spawns), max(extras.get(group, {0: None}).keys(), default=-1) + 1)
        for spawn in range(count):
            row = spawns[spawn] if spawn < len(spawns) else None
            note = None
            if (group, spawn) in changed:
                scene, row_spawn, note = changed[(group, spawn)]
                row = (scene, row_spawn)
            if spawn in extras.get(group, {}):
                if row is not None:
                    raise ValueError(f"MM3D extra spawn {group:#x}/{spawn} overlaps a donor row")
                row = extras[group][spawn]
                note = "MM3D spawn"
            if row is None:
                continue
            # "<spawn>: <meaning>", e.g. "1: from South Clock Town (to lower)".
            label = donor.spawn_label(group, spawn)
            parts = [part for part in (label, note) if part]
            label = f"{spawn}: " + (", ".join(parts) if parts else "spawn")
            rows.append((label, (group << 9) | (spawn << 4), row[0], row[1]))
        slices[group] = (first, len(rows) - first)
        del token
    for group, scene, name in MM3D_EXTRA_GROUPS:
        slices[group] = (len(rows), 1)
        rows.append(("0: MM3D scene", group << 9, scene, 0))

    # Scenes in 2S2H's Better order; each names its group's rows.
    scene_rows = []
    for scene_id, (_, token, better, human) in donor.scenes.items():
        group = donor.groups.get(token)
        if group is None or group not in slices:
            raise ValueError(f"scene {human} enters through unknown group {token}")
        scene_rows.append((better, human, slices[group]))
    scene_rows.sort()
    if [b for b, _, _ in scene_rows] != list(range(len(scene_rows))):
        raise ValueError("2S2H Better indices are not a permutation")
    for group, _, name in MM3D_EXTRA_GROUPS:
        scene_rows.append((len(scene_rows), name, slices[group]))

    by_entrance = {entrance: (label, scene, spawn) for label, entrance, scene, spawn in rows}
    zones = []
    seen = set()
    for category, name, token, spawn, day, time, form, note in ZONES:
        entrance = (donor.groups[token] << 9) | (spawn << 4)
        if entrance not in by_entrance:
            raise ValueError(f"zone {name}: entrance {entrance:#06x} is not in the catalog")
        zone_id = slug(name)
        if zone_id in seen:
            raise ValueError(f"duplicate zone id {zone_id}")
        seen.add(zone_id)
        preset = [day if day is not None else -1, clock(time) if time is not None else -1,
                  FORMS[form] if form is not None else -1, -1]
        zones.append((zone_id, category, name, note, entrance, preset, zone_id in PROVEN_ZONES))
    unknown = PROVEN_ZONES - seen
    if unknown:
        raise ValueError(f"proven zones not in the list: {sorted(unknown)}")

    out = ["// Generated by titles/mm3d/dev_warp/generate_mm3d_dev_warp_catalog.py; do not edit.",
           "// Data only. Names and spawn meanings: 2ship2harkinian (CC0-1.0):"]
    for relative in sorted(donor.files):
        out.append(f"//   {relative} sha256 {donor.files[relative]}")
    out += ["// MM3D's added spawns, groups and its telescope scene are stated in the",
            "// generator as indices; the runtime checks every row against MM3D's live",
            "// entrance table before offering it.",
            "inline constexpr DevWarpEntrance kMm3dDevEntrances[]{"]
    for label, entrance, scene, spawn in rows:
        text = c_string(label)
        out.append(f"  {{{text}, {text}, {text}, 0x{entrance:04X}, 0x{scene:02X}, {spawn}, -1}},")
    out.append("};")
    out.append("inline constexpr DevWarpScene kMm3dDevScenes[]{")
    for _, human, (first, count) in scene_rows:
        text = c_string(human)
        out.append(f"  {{{text}, {text}, {text}, {first}, {count}, false}},")
    out.append("};")
    out.append("// Curated zones: id, category, name, note, entrance, preset {day, time, form, -},")
    out.append("// arrival proven. -1 keeps the current value.")
    out.append("inline constexpr DevWarpZone kMm3dDevZones[]{")
    for zone_id, category, name, note, entrance, preset, proven in zones:
        values = ", ".join(f"0x{v:04X}" if i == 1 and v >= 0 else str(v) for i, v in enumerate(preset))
        out.append(f"  {{{c_string(zone_id)}, {c_string(category)}, {c_string(name)}, {c_string(note)}, "
                   f"0x{entrance:04X}, {{{values}}}, {'true' if proven else 'false'}}},")
    out.append("};")
    summary = {"entrances": len(rows), "scenes": len(scene_rows), "zones": len(zones),
               "named": sum(1 for label, *_ in rows if not label.endswith(": spawn"))}
    return "\n".join(out) + "\n", summary


def main(argv: list[str]) -> int:
    check = len(argv) == 4 and argv[1] == "--check"
    if len(argv) != 3 and not check:
        print(__doc__, file=sys.stderr)
        return 2
    root, output = Path(argv[-2]), Path(argv[-1])
    text, summary = build(root)
    if check:
        if output.read_bytes().decode("utf-8") != text:
            print(f"{output} differs from the generator output", file=sys.stderr)
            return 1
        print(f"{output} is current: {summary}")
        return 0
    output.write_bytes(text.encode("utf-8"))
    print(f"wrote {output}: {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
