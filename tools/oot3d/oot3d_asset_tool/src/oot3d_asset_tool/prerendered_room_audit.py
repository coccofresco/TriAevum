from __future__ import annotations

import json
import re
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path

from .binary import ParseError
from .romfs_inventory import sorted_counter
from .zsi import ZsiFile


BACKGROUND_ROOM_ALIASES: dict[str, dict[str, str]] = {
    "vr_ALVR_static": {
        "label": "Market Potion Shop",
        "n64_scene_stem": "alley_shop",
        "n64_scene_enum": "SCENE_POTION_SHOP_MARKET",
        "oot3d_scene_stem": "shop_alley",
    },
    "vr_DGVR_static": {
        "label": "Kakariko Potion Shop",
        "n64_scene_stem": "drag",
        "n64_scene_enum": "SCENE_POTION_SHOP_KAKARIKO",
        "oot3d_scene_stem": "shop_drag",
    },
    "vr_FCVR_static": {
        "label": "Happy Mask Shop",
        "n64_scene_stem": "face_shop",
        "n64_scene_enum": "SCENE_HAPPY_MASK_SHOP",
        "oot3d_scene_stem": "shop_face",
    },
    "vr_GLVR_static": {
        "label": "Goron Shop",
        "n64_scene_stem": "golon",
        "n64_scene_enum": "SCENE_GORON_SHOP",
        "oot3d_scene_stem": "shop_golon",
    },
    "vr_IPVR_static": {
        "label": "Richard's House",
        "n64_scene_stem": "impa",
        "n64_scene_enum": "SCENE_DOG_LADY_HOUSE",
        "oot3d_scene_stem": "kakariko_impa",
    },
    "vr_K3VR_static": {
        "label": "House of Twins",
        "n64_scene_stem": "kokiri_home3",
        "n64_scene_enum": "SCENE_TWINS_HOUSE",
        "oot3d_scene_stem": "k_home3",
    },
    "vr_K4VR_static": {
        "label": "Mido's House",
        "n64_scene_stem": "kokiri_home4",
        "n64_scene_enum": "SCENE_MIDOS_HOUSE",
        "oot3d_scene_stem": "k_home4",
    },
    "vr_K5VR_static": {
        "label": "Saria's House",
        "n64_scene_stem": "kokiri_home5",
        "n64_scene_enum": "SCENE_SARIAS_HOUSE",
        "oot3d_scene_stem": "k_home5",
    },
    "vr_KHVR_static": {
        "label": "Know-It-All Bros House",
        "n64_scene_stem": "kokiri_home",
        "n64_scene_enum": "SCENE_KNOW_IT_ALL_BROS_HOUSE",
        "oot3d_scene_stem": "k_home",
    },
    "vr_KKRVR_static": {
        "label": "Carpenters House",
        "n64_scene_stem": "kakariko",
        "n64_scene_enum": "SCENE_KAKARIKO_CENTER_GUEST_HOUSE",
        "oot3d_scene_stem": "kakariko",
    },
    "vr_KR3VR_static": {
        "label": "Back Alley House",
        "n64_scene_stem": "kakariko3",
        "n64_scene_enum": "SCENE_BACK_ALLEY_HOUSE",
        "oot3d_scene_stem": "kakariko_home3",
    },
    "vr_KSVR_static": {
        "label": "Kokiri Shop",
        "n64_scene_stem": "kokiri_shop",
        "n64_scene_enum": "SCENE_KOKIRI_SHOP",
        "oot3d_scene_stem": "shop",
    },
    "vr_LBVR_static": {
        "label": "Cow House",
        "n64_scene_stem": "souko",
        "n64_scene_enum": "SCENE_LON_LON_BUILDINGS",
        "oot3d_scene_stem": "souko",
    },
    "vr_LHVR_static": {
        "label": "Link's House",
        "n64_scene_stem": "link_home",
        "n64_scene_enum": "SCENE_LINKS_HOUSE",
        "oot3d_scene_stem": "link",
    },
    "vr_MDVR_static": {
        "label": "Market Day",
        "n64_scene_stem": "market_day",
        "n64_scene_enum": "SCENE_MARKET_DAY",
        "oot3d_scene_stem": "market_day",
    },
    "vr_MLVR_static": {
        "label": "Stable",
        "n64_scene_stem": "malon_stable",
        "n64_scene_enum": "SCENE_STABLE",
        "oot3d_scene_stem": "stable",
    },
    "vr_MNVR_static": {
        "label": "Market Night",
        "n64_scene_stem": "market_night",
        "n64_scene_enum": "SCENE_MARKET_NIGHT",
        "oot3d_scene_stem": "market_night",
    },
    "vr_NSVR_static": {
        "label": "Bombchu Shop",
        "n64_scene_stem": "night_shop",
        "n64_scene_enum": "SCENE_BOMBCHU_SHOP",
        "oot3d_scene_stem": "shop_night",
    },
    "vr_RUVR_static": {
        "label": "Market Ruins",
        "n64_scene_stem": "market_ruins",
        "n64_scene_enum": "SCENE_MARKET_RUINS",
        "oot3d_scene_stem": "market_ruins",
    },
    "vr_SP1a_static": {
        "label": "Bazaar",
        "n64_scene_stem": "shop1",
        "n64_scene_enum": "SCENE_BAZAAR",
        "oot3d_scene_stem": "shop",
    },
    "vr_TTVR_static": {
        "label": "Carpenters Tent",
        "n64_scene_stem": "tent",
        "n64_scene_enum": "SCENE_CARPENTERS_TENT",
        "oot3d_scene_stem": "tent",
    },
    "vr_ZRVR_static": {
        "label": "Zora Shop",
        "n64_scene_stem": "zoora",
        "n64_scene_enum": "SCENE_ZORA_SHOP",
        "oot3d_scene_stem": "zoora",
    },
}


ROOM_ZSI_RE_TEMPLATE = r"^{stem}_(\d+)_info\.zsi$"


def audit_prerendered_room_replacements(
    asset_xml_root: Path,
    scene_root: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
    include_records: bool = True,
) -> dict[str, object]:
    if not asset_xml_root.is_dir():
        raise ParseError(f"{asset_xml_root}: expected a Shipwright asset XML root")
    if not scene_root.is_dir():
        raise ParseError(f"{scene_root}: expected an extracted OOT3D scene directory")

    background_xml = asset_xml_root / "textures" / "backgrounds.xml"
    if not background_xml.is_file():
        raise ParseError(f"{background_xml}: expected Shipwright background texture XML")

    families = parse_background_families(background_xml)
    shipwright_scenes = index_shipwright_scene_xmls(asset_xml_root / "scenes")

    records: list[dict[str, object]] = []
    sample_records: list[dict[str, object]] = []
    status_counts: Counter[str] = Counter()
    texture_count_counts: Counter[str] = Counter()
    oot3d_room_count_counts: Counter[str] = Counter()
    n64_scene_enum_counts: Counter[str] = Counter()
    n64_scene_stem_counts: Counter[str] = Counter()
    oot3d_scene_stem_counts: Counter[str] = Counter()

    mapped_family_count = 0
    shipwright_candidate_count = 0
    oot3d_candidate_count = 0
    ready_family_count = 0
    room_file_total = 0
    embedded_cmb_total = 0
    room_parse_error_total = 0
    triangle_total = 0
    vertex_total = 0
    mesh_total = 0
    texture_total = 0

    for family in families:
        alias = BACKGROUND_ROOM_ALIASES.get(family["file_name"])
        if alias is not None:
            mapped_family_count += 1
            n64_scene_enum_counts[alias["n64_scene_enum"]] += 1
            n64_scene_stem_counts[alias["n64_scene_stem"]] += 1
            oot3d_scene_stem_counts[alias["oot3d_scene_stem"]] += 1

        shipwright = resolve_shipwright_candidate(alias, shipwright_scenes)
        oot3d = resolve_oot3d_candidate(alias, scene_root)

        if shipwright["exists"]:
            shipwright_candidate_count += 1
        if oot3d["room_file_count"] > 0:
            oot3d_candidate_count += 1

        room_file_total += int(oot3d["room_file_count"])
        embedded_cmb_total += int(oot3d["embedded_cmb_count"])
        room_parse_error_total += int(oot3d["parse_error_count"])
        triangle_total += int(oot3d["triangle_count"])
        vertex_total += int(oot3d["vertex_count"])
        mesh_total += int(oot3d["mesh_count"])
        texture_total += int(oot3d["texture_count"])

        texture_count_counts[str(len(family["textures"]))] += 1
        oot3d_room_count_counts[str(oot3d["room_file_count"])] += 1

        status = replacement_status(alias, shipwright, oot3d)
        status_counts[status] += 1
        if status == "ready_for_pilot_export":
            ready_family_count += 1

        record = {
            **family,
            "alias": alias,
            "shipwright_candidate": shipwright,
            "oot3d_candidate": oot3d,
            "status": status,
            "replacement_notes": [
                "N64/Shipwright room type-1 prerendered-background rendering stays the fallback.",
                "OoT3D room CMBs are static full-3D replacement candidates; runtime routing is not changed by this audit.",
                "Disable 2D Pre-Rendered Scenes / Disable Fixed Camera are existing Shipwright toggles for mod-side testing.",
            ],
        }
        if include_records:
            records.append(record)
        if len(sample_records) < sample_limit:
            sample_records.append(record)

    audit: dict[str, object] = {
        "format": "oot3d_prerendered_room_replacement_audit_v1",
        "asset_xml_root": str(asset_xml_root),
        "background_xml": str(background_xml),
        "oot3d_scene_root": str(scene_root),
        "background_family_count": len(families),
        "background_texture_count": sum(len(family["textures"]) for family in families),
        "mapped_family_count": mapped_family_count,
        "unmapped_family_count": len(families) - mapped_family_count,
        "shipwright_candidate_count": shipwright_candidate_count,
        "oot3d_candidate_count": oot3d_candidate_count,
        "unique_n64_scene_stem_count": len(n64_scene_stem_counts),
        "unique_oot3d_scene_stem_count": len(oot3d_scene_stem_counts),
        "ready_for_pilot_export_family_count": ready_family_count,
        "oot3d_room_file_count": room_file_total,
        "oot3d_room_embedded_cmb_count": embedded_cmb_total,
        "oot3d_room_parse_error_count": room_parse_error_total,
        "oot3d_triangle_count": triangle_total,
        "oot3d_vertex_count": vertex_total,
        "oot3d_mesh_count": mesh_total,
        "oot3d_texture_count": texture_total,
        "status_counts": sorted_counter(status_counts),
        "background_texture_count_counts": sorted_counter(texture_count_counts),
        "oot3d_room_count_counts": sorted_counter(oot3d_room_count_counts),
        "n64_scene_enum_counts": sorted_counter(n64_scene_enum_counts),
        "n64_scene_stem_counts": sorted_counter(n64_scene_stem_counts),
        "oot3d_scene_stem_counts": sorted_counter(oot3d_scene_stem_counts),
        "duplicated_oot3d_scene_stem_counts": sorted_counter(
            Counter({stem: count for stem, count in oot3d_scene_stem_counts.items() if count > 1})
        ),
        "runtime_reference": {
            "n64_room_draw_handlers": [
                "Room Draw Polygon Type 1 - Single Format",
                "Room Draw Polygon Type 1 - Multi Format",
                "Room_DrawBackground2D",
            ],
            "shipwright_existing_cvars": [
                'CVAR_ENHANCEMENT("3DSceneRender")',
                'CVAR_ENHANCEMENT("DisableFixedCamera")',
            ],
        },
        "alias_source": "Curated from Shipwright scene_table.h scene stems plus local OOT3D RomFS scene stems.",
        "sample_records": sample_records,
        "records": records if include_records else [],
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8", newline="\n")
    return audit


def parse_background_families(background_xml: Path) -> list[dict[str, object]]:
    try:
        root = ET.parse(background_xml).getroot()
    except ET.ParseError as exc:
        raise ParseError(f"{background_xml}: invalid XML: {exc}") from exc

    families: list[dict[str, object]] = []
    for file_elem in root.findall("File"):
        file_name = file_elem.attrib.get("Name", "")
        if not file_name or file_name.endswith("_pal_static"):
            continue

        textures: list[dict[str, object]] = []
        for texture in file_elem.findall("Texture"):
            textures.append(
                {
                    "name": texture.attrib.get("Name", ""),
                    "out_name": texture.attrib.get("OutName", ""),
                    "format": texture.attrib.get("Format", ""),
                    "width": int(texture.attrib.get("Width", "0"), 0),
                    "height": int(texture.attrib.get("Height", "0"), 0),
                    "offset": texture.attrib.get("Offset", ""),
                    "external_tlut": texture.attrib.get("ExternalTlut", ""),
                    "external_tlut_offset": texture.attrib.get("ExternalTlutOffset", ""),
                }
            )

        if not textures:
            continue

        families.append(
            {
                "file_name": file_name,
                "family_label": family_label_from_texture(textures[0]["out_name"]),
                "textures": textures,
                "palette_files": sorted(
                    {str(texture["external_tlut"]) for texture in textures if texture.get("external_tlut")}
                ),
            }
        )
    return families


def family_label_from_texture(out_name: object) -> str:
    label = re.sub(r"\d+$", "", str(out_name)).strip("_")
    return label or str(out_name)


def index_shipwright_scene_xmls(scene_xml_root: Path) -> dict[str, dict[str, object]]:
    scenes: dict[str, dict[str, object]] = {}
    if not scene_xml_root.is_dir():
        return scenes

    for path in sorted(scene_xml_root.rglob("*.xml")):
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError:
            continue

        scene_names = [scene.attrib.get("Name", "") for scene in root.iter("Scene") if scene.attrib.get("Name")]
        room_names = [room.attrib.get("Name", "") for room in root.iter("Room") if room.attrib.get("Name")]
        stems = {strip_scene_suffix(name) for name in scene_names}
        if not stems:
            stems.add(path.stem)
        for stem in stems:
            scenes[stem] = {
                "exists": True,
                "xml": path.as_posix(),
                "scene_names": scene_names,
                "room_names": room_names,
                "room_count": len(room_names),
            }
    return scenes


def strip_scene_suffix(name: str) -> str:
    return name[:-6] if name.endswith("_scene") else name


def resolve_shipwright_candidate(
    alias: dict[str, str] | None,
    shipwright_scenes: dict[str, dict[str, object]],
) -> dict[str, object]:
    if alias is None:
        return {"exists": False, "reason": "no_alias"}
    stem = alias["n64_scene_stem"]
    scene = shipwright_scenes.get(stem)
    if scene is None:
        return {"exists": False, "stem": stem, "reason": "scene_xml_not_found"}
    return {"stem": stem, **scene}


def resolve_oot3d_candidate(alias: dict[str, str] | None, scene_root: Path) -> dict[str, object]:
    if alias is None:
        return {
            "exists": False,
            "reason": "no_alias",
            "room_file_count": 0,
            "embedded_cmb_count": 0,
            "parse_error_count": 0,
            "triangle_count": 0,
            "vertex_count": 0,
            "mesh_count": 0,
            "texture_count": 0,
            "all_rigid_export_candidates": False,
            "rooms": [],
        }

    stem = alias["oot3d_scene_stem"]
    scene_file = scene_root / f"{stem}_info.zsi"
    archive_file = scene_root / f"{stem}.zar"
    room_files = discover_room_files(scene_root, stem)

    rooms: list[dict[str, object]] = []
    parse_errors = 0
    embedded_cmb_count = 0
    static_cmb_count = 0
    rigid_cmb_count = 0
    triangle_count = 0
    vertex_count = 0
    mesh_count = 0
    texture_count = 0

    for room_index, room_file in room_files:
        room = {
            "room_index": room_index,
            "path": room_file.as_posix(),
            "exists": True,
        }
        try:
            zsi = ZsiFile.from_path(room_file)
            cmbs = zsi.embedded_cmbs()
        except (OSError, ParseError) as exc:
            parse_errors += 1
            rooms.append({**room, "parse_error": str(exc)})
            continue

        summaries = [cmb.model.summary() for cmb in cmbs]
        embedded_cmb_count += len(summaries)
        static_cmb_count += sum(1 for summary in summaries if summary.get("static_candidate"))
        rigid_cmb_count += sum(1 for summary in summaries if summary.get("rigid_export_candidate"))
        triangle_count += sum(int(summary.get("triangle_count", 0)) for summary in summaries)
        vertex_count += sum(int(summary.get("vertex_count", 0)) for summary in summaries)
        mesh_count += sum(int(summary.get("mesh_count", 0)) for summary in summaries)
        texture_count += sum(len(summary.get("textures", [])) for summary in summaries)

        rooms.append(
            {
                **room,
                "cmb_count": len(summaries),
                "cmb_names": [str(summary.get("name", "")) for summary in summaries],
                "static_candidate_count": sum(1 for summary in summaries if summary.get("static_candidate")),
                "rigid_export_candidate_count": sum(
                    1 for summary in summaries if summary.get("rigid_export_candidate")
                ),
                "triangle_count": sum(int(summary.get("triangle_count", 0)) for summary in summaries),
                "vertex_count": sum(int(summary.get("vertex_count", 0)) for summary in summaries),
                "mesh_count": sum(int(summary.get("mesh_count", 0)) for summary in summaries),
                "texture_count": sum(len(summary.get("textures", [])) for summary in summaries),
            }
        )

    return {
        "exists": scene_file.exists() or bool(room_files),
        "stem": stem,
        "scene_file": scene_file.as_posix() if scene_file.exists() else None,
        "archive_file": archive_file.as_posix() if archive_file.exists() else None,
        "room_file_count": len(room_files),
        "embedded_cmb_count": embedded_cmb_count,
        "static_candidate_cmb_count": static_cmb_count,
        "rigid_export_candidate_cmb_count": rigid_cmb_count,
        "parse_error_count": parse_errors,
        "triangle_count": triangle_count,
        "vertex_count": vertex_count,
        "mesh_count": mesh_count,
        "texture_count": texture_count,
        "all_rigid_export_candidates": embedded_cmb_count > 0 and embedded_cmb_count == rigid_cmb_count,
        "rooms": rooms,
    }


def discover_room_files(scene_root: Path, stem: str) -> list[tuple[int, Path]]:
    room_re = re.compile(ROOM_ZSI_RE_TEMPLATE.format(stem=re.escape(stem)), re.IGNORECASE)
    rooms: list[tuple[int, Path]] = []
    for path in scene_root.glob(f"{stem}_*_info.zsi"):
        match = room_re.match(path.name)
        if match:
            rooms.append((int(match.group(1)), path))
    return sorted(rooms, key=lambda item: item[0])


def replacement_status(
    alias: dict[str, str] | None,
    shipwright: dict[str, object],
    oot3d: dict[str, object],
) -> str:
    if alias is None:
        return "unmapped_background_family"
    if not shipwright.get("exists"):
        return "missing_shipwright_scene"
    if int(oot3d.get("room_file_count", 0)) == 0:
        return "missing_oot3d_room_candidate"
    if int(oot3d.get("parse_error_count", 0)) != 0:
        return "candidate_needs_review"
    if not oot3d.get("all_rigid_export_candidates"):
        return "candidate_needs_review"
    return "ready_for_pilot_export"
