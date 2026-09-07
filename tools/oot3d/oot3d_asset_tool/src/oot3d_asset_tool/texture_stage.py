from __future__ import annotations

from .cmb import Material


RAW_MATERIAL_TEXTURE_STAGE_CANDIDATE_START = 0xA0
RAW_MATERIAL_TEXTURE_STAGE_CANDIDATE_END = 0x130
RAW_MATERIAL_TEXTURE_STAGE_COUNT_OFFSET = 0x120
RAW_MATERIAL_TEXTURE_STAGE_INDEX_OFFSET = 0x124


def raw_texture_stage_selector(material: Material) -> dict[str, object]:
    raw = material.raw_material
    if len(raw) < RAW_MATERIAL_TEXTURE_STAGE_INDEX_OFFSET + 8:
        return {
            "stage_count": 0,
            "stage_indices": [],
            "raw_stage_slots": [],
            "matches_texture_mappers_used": material.texture_mappers_used == 0,
            "stage_count_exceeds_texture_mappers_used": False,
            "source_offsets": {
                "stage_count": f"0x{RAW_MATERIAL_TEXTURE_STAGE_COUNT_OFFSET:03x}",
                "stage_indices": [],
            },
        }

    stage_count = int.from_bytes(
        raw[RAW_MATERIAL_TEXTURE_STAGE_COUNT_OFFSET : RAW_MATERIAL_TEXTURE_STAGE_COUNT_OFFSET + 4],
        "little",
    )
    raw_stage_slots = [
        signed_u16(raw, RAW_MATERIAL_TEXTURE_STAGE_INDEX_OFFSET + offset)
        for offset in range(0, 8, 2)
    ]
    stage_indices = [
        value
        for value in raw_stage_slots[:stage_count]
        if value >= 0
    ]
    return {
        "stage_count": stage_count,
        "stage_indices": stage_indices,
        "raw_stage_slots": raw_stage_slots,
        "matches_texture_mappers_used": stage_count == material.texture_mappers_used,
        "stage_count_exceeds_texture_mappers_used": stage_count > material.texture_mappers_used,
        "source_offsets": {
            "stage_count": f"0x{RAW_MATERIAL_TEXTURE_STAGE_COUNT_OFFSET:03x}",
            "stage_indices": [
                f"0x{RAW_MATERIAL_TEXTURE_STAGE_INDEX_OFFSET + offset:03x}"
                for offset in range(0, 8, 2)
            ],
        },
    }


def signed_u16(raw: bytes, offset: int) -> int:
    value = int.from_bytes(raw[offset : offset + 2], "little")
    return value - 0x10000 if value & 0x8000 else value
