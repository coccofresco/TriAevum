from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path

from .binary import ParseError
from .cmb import CmbModel
from .legacy_fast_resource import normalize_texture_orientation, normalize_uv_orientation


CHARACTER_MESH_SELECTION_FORMAT = "oot3d_character_mesh_selection_v1"
LEGACY_CHARACTER_MESH_SELECTION_FORMATS = {"oot3d_static_base_selection_v1"}


@dataclass(frozen=True)
class CharacterMeshSelection:
    profile_id: str
    model_name: str | None
    primitive_keys: frozenset[tuple[int, int]]
    texture_orientation: str | None = None
    uv_orientation: str | None = None
    position_scale: float | None = None
    source_path: str | None = None
    source_format: str = CHARACTER_MESH_SELECTION_FORMAT

    @property
    def mesh_indices(self) -> frozenset[int]:
        return frozenset(mesh_index for mesh_index, _ in self.primitive_keys)

    def includes(self, mesh_index: int, primitive_index: int) -> bool:
        return (mesh_index, primitive_index) in self.primitive_keys


def load_character_mesh_selection(path: Path) -> CharacterMeshSelection:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise ParseError(f"{path}: failed to read character mesh selection manifest") from exc
    except json.JSONDecodeError as exc:
        raise ParseError(f"{path}: invalid JSON character mesh selection manifest") from exc

    if not isinstance(raw, dict):
        raise ParseError(f"{path}: character mesh selection manifest must be a JSON object")
    source_format = raw.get("format")
    if source_format not in {CHARACTER_MESH_SELECTION_FORMAT, *LEGACY_CHARACTER_MESH_SELECTION_FORMATS}:
        raise ParseError(f"{path}: unsupported character mesh selection manifest format")

    raw_keys = raw.get("primitive_keys")
    if not isinstance(raw_keys, list) or not raw_keys:
        raise ParseError(f"{path}: selection manifest must contain non-empty primitive_keys")
    primitive_keys: set[tuple[int, int]] = set()
    for index, raw_key in enumerate(raw_keys):
        if not isinstance(raw_key, dict):
            raise ParseError(f"{path}: primitive_keys[{index}] must be an object")
        mesh_index = raw_key.get("mesh_index")
        primitive_index = raw_key.get("primitive_index")
        if not isinstance(mesh_index, int) or not isinstance(primitive_index, int):
            raise ParseError(f"{path}: primitive_keys[{index}] must contain integer mesh_index and primitive_index")
        if mesh_index < 0 or primitive_index < 0:
            raise ParseError(f"{path}: primitive_keys[{index}] contains a negative index")
        primitive_key = (mesh_index, primitive_index)
        if primitive_key in primitive_keys:
            raise ParseError(f"{path}: primitive_keys[{index}] duplicates mesh_index/primitive_index {primitive_key}")
        primitive_keys.add(primitive_key)

    profile_id = raw.get("profile_id")
    if not isinstance(profile_id, str) or not profile_id:
        profile_id = path.stem

    model_name = raw.get("model_name")
    if model_name is not None and not isinstance(model_name, str):
        raise ParseError(f"{path}: model_name must be a string when present")

    position_scale = raw.get("position_scale")
    if position_scale is not None:
        if not isinstance(position_scale, (int, float)) or not math.isfinite(float(position_scale)) or float(position_scale) <= 0.0:
            raise ParseError(f"{path}: position_scale must be positive and finite when present")

    texture_orientation = raw.get("texture_orientation")
    if texture_orientation is not None:
        if not isinstance(texture_orientation, str):
            raise ParseError(f"{path}: texture_orientation must be a string when present")
        texture_orientation = normalize_texture_orientation(texture_orientation)

    uv_orientation = raw.get("uv_orientation")
    if uv_orientation is not None:
        if not isinstance(uv_orientation, str):
            raise ParseError(f"{path}: uv_orientation must be a string when present")
        uv_orientation = normalize_uv_orientation(uv_orientation)

    return CharacterMeshSelection(
        profile_id=profile_id,
        model_name=model_name,
        primitive_keys=frozenset(primitive_keys),
        texture_orientation=texture_orientation,
        uv_orientation=uv_orientation,
        position_scale=float(position_scale) if position_scale is not None else None,
        source_path=str(path),
        source_format=str(source_format),
    )


def validate_character_mesh_selection(selection: CharacterMeshSelection, model: CmbModel) -> None:
    if selection.model_name is not None and selection.model_name != model.name:
        raise ParseError(
            f"{selection.source_path}: selection profile {selection.profile_id!r} "
            f"targets model {selection.model_name!r}, got {model.name!r}"
        )

    missing: list[str] = []
    mesh_by_index = {mesh.index: mesh for mesh in model.meshes}
    for mesh_index, primitive_index in sorted(selection.primitive_keys):
        mesh = mesh_by_index.get(mesh_index)
        if mesh is None:
            missing.append(f"mesh {mesh_index} primitive {primitive_index}: missing mesh")
            continue
        if mesh.shape_index < 0 or mesh.shape_index >= len(model.shapes):
            missing.append(f"mesh {mesh_index} primitive {primitive_index}: missing shape {mesh.shape_index}")
            continue
        shape = model.shapes[mesh.shape_index]
        if primitive_index >= len(shape.primitives):
            missing.append(f"mesh {mesh_index} primitive {primitive_index}: missing primitive")

    if missing:
        sample = "; ".join(missing[:10])
        suffix = f"; plus {len(missing) - 10} more" if len(missing) > 10 else ""
        raise ParseError(f"{selection.source_path}: invalid primitive_keys: {sample}{suffix}")
