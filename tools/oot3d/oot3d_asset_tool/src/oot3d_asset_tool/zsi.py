from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path

from .binary import BinaryView, ParseError
from .cmb import CmbModel

ZSI_MAGIC = b"ZSI\x01"
CMB_MAGIC = b"cmb "
ZSI_RESOURCE_BASE_OFFSET = 0x10
SCENE_SETUP_START = 0x18
SCENE_END_COMMAND_ID = 0x14
SCENE_COLLISION_COMMAND_ID = 0x03
SCENE_SETUP_FIRST_COMMAND_IDS = frozenset((0x04, 0x15))
ROOM_SETUP_FIRST_COMMAND_IDS = frozenset((0x08, 0x16))
SETUP_FIRST_COMMAND_IDS = SCENE_SETUP_FIRST_COMMAND_IDS | ROOM_SETUP_FIRST_COMMAND_IDS
MAX_SETUP_COMMAND_COUNT = 0x20


@dataclass(frozen=True)
class ZsiEmbeddedCmb:
    index: int
    offset: int
    size: int
    model: CmbModel


@dataclass(frozen=True)
class ZsiSceneCommand:
    setup_index: int
    offset: int
    command_id: int
    command_word: int
    argument: int

    @property
    def parameter(self) -> int:
        return (self.command_word >> 8) & 0xFF

    def summary(self) -> dict[str, object]:
        return {
            "offset": self.offset,
            "command_id": self.command_id,
            "parameter": self.parameter,
            "command_word": self.command_word,
            "argument": self.argument,
        }


@dataclass(frozen=True)
class ZsiSceneSetup:
    index: int
    offset: int
    end_offset: int
    commands: tuple[ZsiSceneCommand, ...]

    def first_command(self, command_id: int) -> ZsiSceneCommand | None:
        for command in self.commands:
            if command.command_id == command_id:
                return command
        return None

    def summary(self) -> dict[str, object]:
        collision_command = self.first_command(SCENE_COLLISION_COMMAND_ID)
        return {
            "index": self.index,
            "offset": self.offset,
            "end_offset": self.end_offset,
            "command_count": len(self.commands),
            "command_ids": [command.command_id for command in self.commands],
            "collision_command": collision_command.summary()
            if collision_command
            else None,
        }


@dataclass(frozen=True)
class ZsiWaterBox:
    x_min: int
    y_surface: int
    z_min: int
    x_length: int
    z_length: int
    properties: int

    def is_plausible(self) -> bool:
        return (
            -20000 <= self.x_min <= 20000
            and -20000 <= self.y_surface <= 20000
            and -20000 <= self.z_min <= 20000
            and 0 < self.x_length <= 20000
            and 0 < self.z_length <= 20000
        )

    def summary(self) -> dict[str, object]:
        return {
            "x_min": self.x_min,
            "y_surface": self.y_surface,
            "z_min": self.z_min,
            "x_length": self.x_length,
            "z_length": self.z_length,
            "properties": self.properties,
            "plausible": self.is_plausible(),
        }


@dataclass(frozen=True)
class ZsiCollisionVertex:
    x: int
    y: int
    z: int

    def summary(self) -> dict[str, object]:
        return {"x": self.x, "y": self.y, "z": self.z}


@dataclass(frozen=True)
class ZsiCollisionPolygon:
    type: int
    raw_vtx_a: int
    raw_vtx_b: int
    raw_vtx_c: int
    marker: int
    normal: tuple[int, int, int]
    dist: float

    @property
    def vtx_a(self) -> int:
        return self.raw_vtx_a & 0x1FFF

    @property
    def vtx_b(self) -> int:
        return self.raw_vtx_b & 0x1FFF

    @property
    def vtx_c(self) -> int:
        return self.raw_vtx_c

    @property
    def vertex_indices(self) -> tuple[int, int, int]:
        return (self.vtx_a, self.vtx_b, self.vtx_c)

    def summary(self) -> dict[str, object]:
        return {
            "type": self.type,
            "vertex_indices": self.vertex_indices,
            "raw_vertex_indices": (self.raw_vtx_a, self.raw_vtx_b, self.raw_vtx_c),
            "marker": self.marker,
            "normal": self.normal,
            "dist": self.dist,
        }


@dataclass(frozen=True)
class ZsiSurfaceType:
    data1: int
    data2: int

    def summary(self) -> dict[str, object]:
        return {"data1": self.data1, "data2": self.data2}


@dataclass(frozen=True)
class ZsiBgCamInfo:
    setting: int
    count: int
    data_offset: int

    def summary(self) -> dict[str, object]:
        return {
            "setting": self.setting,
            "count": self.count,
            "data_offset": self.data_offset,
        }


@dataclass(frozen=True)
class ZsiCollisionHeaderCandidate:
    setup_index: int
    command_offset: int
    command_argument: int
    offset: int
    bounds_min: tuple[int, int, int]
    bounds_max: tuple[int, int, int]
    vertex_count: int
    raw_polygon_count: int
    surface_type_count: int
    bgcam_count: int
    water_box_count: int
    vertex_offset: int
    effective_vertex_offset: int
    vertex_prefix_hex: str
    polygon_offset: int
    effective_polygon_offset: int
    effective_polygon_count: int
    polygon_prefix_hex: str
    polygon_tail_hex: str
    surface_type_offset: int
    effective_surface_type_offset: int
    bgcam_offset: int
    effective_bgcam_offset: int
    camera_position_offset: int
    camera_pointer_adjustment: int
    water_boxes_offset: int
    vertices: tuple[ZsiCollisionVertex, ...]
    polygons: tuple[ZsiCollisionPolygon, ...]
    surface_types: tuple[ZsiSurfaceType, ...]
    effective_surface_types: tuple[ZsiSurfaceType, ...]
    bg_cam_info: tuple[ZsiBgCamInfo, ...]
    effective_bg_cam_info: tuple[ZsiBgCamInfo, ...]
    camera_position_indices: tuple[int, ...]
    camera_position_vectors: tuple[tuple[int, int, int], ...]
    water_boxes: tuple[ZsiWaterBox, ...]
    effective_water_boxes: tuple[ZsiWaterBox, ...]
    prefixed_water_boxes: tuple[ZsiWaterBox, ...]
    evidence: tuple[str, ...]

    def summary(self) -> dict[str, object]:
        return {
            "setup_index": self.setup_index,
            "command_offset": self.command_offset,
            "command_argument": self.command_argument,
            "offset": self.offset,
            "bounds_min": self.bounds_min,
            "bounds_max": self.bounds_max,
            "vertex_count": self.vertex_count,
            "raw_polygon_count": self.raw_polygon_count,
            "effective_polygon_count": self.effective_polygon_count,
            "surface_type_count": self.surface_type_count,
            "bgcam_count": self.bgcam_count,
            "water_box_count": self.water_box_count,
            "vertex_offset": self.vertex_offset,
            "effective_vertex_offset": self.effective_vertex_offset,
            "vertex_prefix_hex": self.vertex_prefix_hex,
            "polygon_offset": self.polygon_offset,
            "effective_polygon_offset": self.effective_polygon_offset,
            "polygon_prefix_hex": self.polygon_prefix_hex,
            "polygon_tail_hex": self.polygon_tail_hex,
            "surface_type_offset": self.surface_type_offset,
            "effective_surface_type_offset": self.effective_surface_type_offset,
            "bgcam_offset": self.bgcam_offset,
            "effective_bgcam_offset": self.effective_bgcam_offset,
            "camera_position_offset": self.camera_position_offset,
            "camera_pointer_adjustment": self.camera_pointer_adjustment,
            "water_boxes_offset": self.water_boxes_offset,
            "vertices_sample": [vertex.summary() for vertex in self.vertices[:3]],
            "polygons_sample": [polygon.summary() for polygon in self.polygons[:3]],
            "polygon_type_usage": polygon_type_usage(self.polygons),
            "surface_types_sample": [
                surface.summary() for surface in self.surface_types[:5]
            ],
            "effective_surface_types_sample": [
                surface.summary() for surface in self.effective_surface_types[:5]
            ],
            "bg_cam_info": [camera.summary() for camera in self.bg_cam_info],
            "effective_bg_cam_info": [
                camera.summary() for camera in self.effective_bg_cam_info
            ],
            "camera_position_indices": list(self.camera_position_indices),
            "camera_position_count": len(self.camera_position_vectors),
            "camera_position_sample": list(self.camera_position_vectors[:6]),
            "water_boxes": [box.summary() for box in self.water_boxes],
            "effective_water_boxes": [
                box.summary() for box in self.effective_water_boxes
            ],
            "prefixed_water_boxes": [
                box.summary() for box in self.prefixed_water_boxes
            ],
            "evidence": list(self.evidence),
        }


@dataclass(frozen=True)
class ZsiFile:
    path: Path
    data: bytes

    @classmethod
    def from_path(cls, path: Path) -> "ZsiFile":
        return cls(path=path, data=path.read_bytes())

    def embedded_cmbs(self) -> list[ZsiEmbeddedCmb]:
        if not self.data.startswith(ZSI_MAGIC):
            raise ParseError(f"{self.path}: expected ZSI magic")

        cmbs: list[ZsiEmbeddedCmb] = []
        search_from = 0
        while True:
            offset = self.data.find(CMB_MAGIC, search_from)
            if offset < 0:
                break
            search_from = offset + 1

            if not looks_like_cmb_at(self.data, offset):
                continue

            size = int.from_bytes(self.data[offset + 4 : offset + 8], "little")
            model_data = self.data[offset : offset + size]
            model = CmbModel.parse(
                model_data,
                f"{self.path}!cmb[{len(cmbs)}]@0x{offset:x}",
            )
            cmbs.append(
                ZsiEmbeddedCmb(
                    index=len(cmbs),
                    offset=offset,
                    size=size,
                    model=model,
                )
            )

        return cmbs

    def scene_setups(self) -> list[ZsiSceneSetup]:
        if not self.data.startswith(ZSI_MAGIC):
            raise ParseError(f"{self.path}: expected ZSI magic")

        view = BinaryView(self.data, str(self.path))
        setups: list[ZsiSceneSetup] = []
        cursor = SCENE_SETUP_START
        while cursor + 8 <= len(self.data):
            first_word = view.u32(cursor)
            first_command_id = first_word & 0xFF
            if first_command_id not in SETUP_FIRST_COMMAND_IDS:
                break

            setup_index = len(setups)
            commands: list[ZsiSceneCommand] = []
            command_offset = cursor
            while (
                command_offset + 8 <= len(self.data)
                and len(commands) < MAX_SETUP_COMMAND_COUNT
            ):
                command_word = view.u32(command_offset)
                argument = view.u32(command_offset + 4)
                command = ZsiSceneCommand(
                    setup_index=setup_index,
                    offset=command_offset,
                    command_id=command_word & 0xFF,
                    command_word=command_word,
                    argument=argument,
                )
                commands.append(command)
                command_offset += 8
                if command.command_id == SCENE_END_COMMAND_ID:
                    break

            if not commands or commands[-1].command_id != SCENE_END_COMMAND_ID:
                break

            setups.append(
                ZsiSceneSetup(
                    index=setup_index,
                    offset=cursor,
                    end_offset=command_offset,
                    commands=tuple(commands),
                )
            )
            cursor = command_offset

        return setups

    def collision_header_candidates(self) -> list[ZsiCollisionHeaderCandidate]:
        view = BinaryView(self.data, str(self.path))
        candidates: list[ZsiCollisionHeaderCandidate] = []
        seen_offsets: set[int] = set()

        for setup in self.scene_setups():
            command = setup.first_command(SCENE_COLLISION_COMMAND_ID)
            if command is None:
                continue

            # Command pointers are relative to the native resource base at file+0x10.
            # Keep the raw candidate for older diagnostics while preferring that base.
            for offset in (
                command.argument + ZSI_RESOURCE_BASE_OFFSET,
                command.argument,
            ):
                if offset in seen_offsets:
                    continue
                candidate = self._parse_collision_header_candidate(
                    view, setup, command, offset
                )
                if candidate is None:
                    continue
                seen_offsets.add(offset)
                candidates.append(candidate)

        return candidates

    def summary(self) -> dict[str, object]:
        cmbs = self.embedded_cmbs()
        scene_setups = self.scene_setups()
        collision_candidates = self.collision_header_candidates()
        return {
            "path": str(self.path),
            "format": "zsi",
            "size": len(self.data),
            "cmb_count": len(cmbs),
            "cmbs": [
                {
                    "index": cmb.index,
                    "offset": cmb.offset,
                    "size": cmb.size,
                    "summary": cmb.model.summary(),
                }
                for cmb in cmbs
            ],
            "scene_setup_count": len(scene_setups),
            "scene_setups": [setup.summary() for setup in scene_setups],
            "collision_header_candidates": [
                candidate.summary() for candidate in collision_candidates
            ],
        }

    def _parse_collision_header_candidate(
        self,
        view: BinaryView,
        setup: ZsiSceneSetup,
        command: ZsiSceneCommand,
        offset: int,
    ) -> ZsiCollisionHeaderCandidate | None:
        if offset < 0 or offset + 0x2C > len(self.data):
            return None

        bounds_min = (view.s16(offset), view.s16(offset + 2), view.s16(offset + 4))
        bounds_max = (view.s16(offset + 6), view.s16(offset + 8), view.s16(offset + 10))
        if not collision_bounds_are_plausible(bounds_min, bounds_max):
            return None

        vertex_count = view.u16(offset + 0x0C)
        raw_polygon_count = view.u16(offset + 0x0E)
        surface_type_count = view.u16(offset + 0x10)
        bgcam_count = view.u16(offset + 0x12)
        water_box_count = view.u16(offset + 0x14)
        if vertex_count == 0 or raw_polygon_count == 0 or surface_type_count == 0:
            return None
        if bgcam_count > 0x100 or water_box_count > 0x40:
            return None

        vertex_offset = view.u32(offset + 0x18)
        polygon_offset = view.u32(offset + 0x1C)
        surface_type_offset = view.u32(offset + 0x20)
        bgcam_offset = view.u32(offset + 0x24)
        water_boxes_offset = view.u32(offset + 0x28)
        pointer_offsets = (
            vertex_offset,
            polygon_offset,
            surface_type_offset,
            bgcam_offset,
            water_boxes_offset,
        )
        if not all(
            is_file_offset(pointer_offset, len(self.data))
            for pointer_offset in pointer_offsets
        ):
            return None
        if vertex_offset + vertex_count * 6 > len(self.data):
            return None
        if polygon_offset + raw_polygon_count * 0x14 != surface_type_offset:
            return None
        if surface_type_offset + surface_type_count * 8 > len(self.data):
            return None
        if bgcam_offset + bgcam_count * 8 > len(self.data):
            return None
        if water_boxes_offset + water_box_count * 0x10 > len(self.data):
            return None

        polygon_layout = find_effective_polygon_layout(
            view,
            polygon_offset,
            surface_type_offset,
            raw_polygon_count,
            vertex_count,
            surface_type_count,
        )
        if polygon_layout is None:
            return None
        effective_polygon_offset, effective_polygon_count = polygon_layout

        polygons = tuple(
            read_collision_polygon(view, effective_polygon_offset + index * 0x14)
            for index in range(effective_polygon_count)
        )
        effective_vertex_offset = find_effective_vertex_layout(
            view,
            vertex_offset,
            effective_polygon_offset,
            vertex_count,
            bounds_min,
            bounds_max,
            polygons,
        )
        if effective_vertex_offset is None:
            return None

        vertices = tuple(
            read_collision_vertex(view, effective_vertex_offset + index * 6)
            for index in range(vertex_count)
        )
        bg_cam_info = tuple(
            read_bg_cam_info(view, bgcam_offset + index * 8)
            for index in range(bgcam_count)
        )
        bg_cam_layout = find_effective_bg_cam_layout(
            view,
            bgcam_offset,
            bgcam_count,
            len(self.data),
        )
        if bg_cam_layout is None:
            effective_bgcam_offset = bgcam_offset
            camera_position_offset = 0
            camera_pointer_adjustment = 0
            effective_bg_cam_info = bg_cam_info
            camera_position_indices = tuple(0 for _entry in effective_bg_cam_info)
            camera_position_vectors: tuple[tuple[int, int, int], ...] = ()
        else:
            (
                effective_bgcam_offset,
                camera_position_offset,
                camera_pointer_adjustment,
                effective_bg_cam_info,
                camera_position_indices,
                camera_position_vectors,
            ) = bg_cam_layout

        surface_types = tuple(
            read_surface_type(view, surface_type_offset + index * 8)
            for index in range(surface_type_count)
        )
        effective_surface_type_offset, effective_surface_types = (
            find_effective_surface_type_layout(
                view,
                surface_type_offset,
                surface_type_count,
                effective_bgcam_offset,
                bgcam_count,
            )
        )

        water_boxes = tuple(
            read_zsi_water_box(view, water_boxes_offset + index * 0x10)
            for index in range(water_box_count)
        )
        effective_water_boxes = plausible_water_boxes_at(
            view,
            water_boxes_offset + 0x10,
            water_box_count,
            len(self.data),
        )
        prefix_offset = offset - 0x10
        prefixed_water_boxes: tuple[ZsiWaterBox, ...] = ()
        if (
            water_box_count
            and 0 <= prefix_offset
            and prefix_offset + water_box_count * 0x10 <= len(self.data)
        ):
            prefix_boxes = tuple(
                read_zsi_water_box(view, prefix_offset + index * 0x10)
                for index in range(water_box_count)
            )
            if any(box.is_plausible() for box in prefix_boxes):
                prefixed_water_boxes = prefix_boxes

        evidence = [
            "bounds are ordered signed 16-bit values",
            "pointers at +0x18/+0x1c/+0x20/+0x24/+0x28 are valid ZSI file offsets",
            "vertex records may start after a 16-byte section prefix; the effective vertex table is selected by header-bounds validation",
            "surface type pointer matches oot3d_surface_type_get_word header+0x20 usage",
            "bg camera count matches FUN_00273070 header+0x12 relocation loop",
            "static polygon records are 20 bytes; vertex indices are at +0x02/+0x04/+0x06",
            "prefixed polygon records can end at the next section's effective table boundary",
        ]
        if polygons and all(
            polygon.marker == polygons[0].marker for polygon in polygons
        ):
            evidence.append(
                f"all decoded static polygon records carry marker 0x{polygons[0].marker:04x}"
            )
        if effective_bgcam_offset != bgcam_offset:
            evidence.append(
                "the camera section stores a 16-byte prefix; camera data offsets require the same adjustment"
            )
        if effective_surface_type_offset != surface_type_offset:
            evidence.append(
                "surface type records start after a 16-byte section prefix and may extend into the next section prefix"
            )
        if prefixed_water_boxes:
            evidence.append(
                "a plausible N64/Shipwright-format water box is stored in the 16-byte prefix before the bounds header"
            )

        return ZsiCollisionHeaderCandidate(
            setup_index=setup.index,
            command_offset=command.offset,
            command_argument=command.argument,
            offset=offset,
            bounds_min=bounds_min,
            bounds_max=bounds_max,
            vertex_count=vertex_count,
            raw_polygon_count=raw_polygon_count,
            surface_type_count=surface_type_count,
            bgcam_count=bgcam_count,
            water_box_count=water_box_count,
            vertex_offset=vertex_offset,
            effective_vertex_offset=effective_vertex_offset,
            vertex_prefix_hex=self.data[vertex_offset:effective_vertex_offset].hex(),
            polygon_offset=polygon_offset,
            effective_polygon_offset=effective_polygon_offset,
            effective_polygon_count=effective_polygon_count,
            polygon_prefix_hex=self.data[polygon_offset:effective_polygon_offset].hex(),
            polygon_tail_hex=self.data[
                effective_polygon_offset
                + effective_polygon_count * 0x14 : surface_type_offset
            ].hex(),
            surface_type_offset=surface_type_offset,
            effective_surface_type_offset=effective_surface_type_offset,
            bgcam_offset=bgcam_offset,
            effective_bgcam_offset=effective_bgcam_offset,
            camera_position_offset=camera_position_offset,
            camera_pointer_adjustment=camera_pointer_adjustment,
            water_boxes_offset=water_boxes_offset,
            vertices=vertices,
            polygons=polygons,
            surface_types=surface_types,
            effective_surface_types=effective_surface_types,
            bg_cam_info=bg_cam_info,
            effective_bg_cam_info=effective_bg_cam_info,
            camera_position_indices=camera_position_indices,
            camera_position_vectors=camera_position_vectors,
            water_boxes=water_boxes,
            effective_water_boxes=effective_water_boxes,
            prefixed_water_boxes=prefixed_water_boxes,
            evidence=tuple(evidence),
        )


def looks_like_cmb_at(data: bytes, offset: int) -> bool:
    if offset < 0 or offset + 0x44 > len(data):
        return False
    if data[offset : offset + 4] != CMB_MAGIC:
        return False

    size = int.from_bytes(data[offset + 4 : offset + 8], "little")
    version = int.from_bytes(data[offset + 8 : offset + 12], "little")
    if version != 6:
        return False
    if size < 0x44 or offset + size > len(data):
        return False
    return True


def is_file_offset(offset: int, size: int) -> bool:
    return 0 <= offset < size


def collision_bounds_are_plausible(
    bounds_min: tuple[int, int, int],
    bounds_max: tuple[int, int, int],
) -> bool:
    if any(bounds_min[index] > bounds_max[index] for index in range(3)):
        return False
    return any(bounds_min[index] < bounds_max[index] for index in range(3))


def read_zsi_water_box(view: BinaryView, offset: int) -> ZsiWaterBox:
    return ZsiWaterBox(
        x_min=view.s16(offset),
        y_surface=view.s16(offset + 2),
        z_min=view.s16(offset + 4),
        x_length=view.s16(offset + 6),
        z_length=view.s16(offset + 8),
        properties=view.u32(offset + 0x0C),
    )


def read_collision_vertex(view: BinaryView, offset: int) -> ZsiCollisionVertex:
    return ZsiCollisionVertex(
        x=view.s16(offset),
        y=view.s16(offset + 2),
        z=view.s16(offset + 4),
    )


def find_effective_vertex_layout(
    view: BinaryView,
    vertex_offset: int,
    section_end_offset: int,
    vertex_count: int,
    bounds_min: tuple[int, int, int],
    bounds_max: tuple[int, int, int],
    polygons: tuple[ZsiCollisionPolygon, ...],
) -> int | None:
    candidates: list[dict[str, object]] = []
    for prefix_size in range(0, 0x20, 2):
        candidate_offset = vertex_offset + prefix_size
        candidate_end = candidate_offset + vertex_count * 6
        if candidate_end > len(view.data):
            continue
        if section_end_offset > 0 and candidate_end > section_end_offset:
            continue

        vertices = tuple(
            read_collision_vertex(view, candidate_offset + index * 6)
            for index in range(vertex_count)
        )
        decoded_bounds = vertex_bounds(vertices)
        signed_delta = decoded_bounds[0] + decoded_bounds[1]
        header_values = bounds_min + bounds_max
        absolute_delta = tuple(
            abs(signed_delta[index] - header_values[index]) for index in range(6)
        )
        max_delta = max(absolute_delta, default=0)
        total_delta = sum(absolute_delta)
        normal_match_count, normal_alignment = vertex_polygon_normal_alignment(
            vertices, polygons
        )
        section_end_gap = (
            abs(section_end_offset - candidate_end) if section_end_offset > 0 else 0
        )
        candidates.append(
            {
                "offset": candidate_offset,
                "prefix_size": prefix_size,
                "max_delta": max_delta,
                "total_delta": total_delta,
                "section_end_gap": section_end_gap,
                "normal_match_count": normal_match_count,
                "normal_alignment": normal_alignment,
            }
        )

    if not candidates:
        return None

    bounds_candidates = [
        candidate for candidate in candidates if int(candidate["max_delta"]) <= 512
    ]
    if bounds_candidates:
        best = min(
            bounds_candidates,
            key=lambda candidate: (
                int(candidate["max_delta"]),
                int(candidate["total_delta"]),
                int(candidate["section_end_gap"]),
                -int(candidate["normal_match_count"]),
                -float(candidate["normal_alignment"]),
                int(candidate["prefix_size"]),
                int(candidate["offset"]),
            ),
        )
        return int(best["offset"])

    best_normal = max(
        candidates,
        key=lambda candidate: (
            int(candidate["normal_match_count"]),
            float(candidate["normal_alignment"]),
            -int(candidate["section_end_gap"]),
            -int(candidate["prefix_size"]),
        ),
    )
    required_normal_matches = max(1, int(len(polygons) * 0.8))
    if (
        int(best_normal["normal_match_count"]) >= required_normal_matches
        and float(best_normal["normal_alignment"]) >= 0.95
    ):
        return int(best_normal["offset"])

    best = min(
        candidates,
        key=lambda candidate: (
            int(candidate["max_delta"]),
            int(candidate["total_delta"]),
            int(candidate["section_end_gap"]),
            -int(candidate["normal_match_count"]),
            -float(candidate["normal_alignment"]),
            int(candidate["prefix_size"]),
            int(candidate["offset"]),
        ),
    )
    return int(best["offset"])


def vertex_bounds(
    vertices: tuple[ZsiCollisionVertex, ...],
) -> tuple[tuple[int, int, int], tuple[int, int, int]]:
    return (
        (
            min(vertex.x for vertex in vertices),
            min(vertex.y for vertex in vertices),
            min(vertex.z for vertex in vertices),
        ),
        (
            max(vertex.x for vertex in vertices),
            max(vertex.y for vertex in vertices),
            max(vertex.z for vertex in vertices),
        ),
    )


def vertex_polygon_normal_alignment(
    vertices: tuple[ZsiCollisionVertex, ...],
    polygons: tuple[ZsiCollisionPolygon, ...],
    *,
    sample_limit: int = 1024,
) -> tuple[int, float]:
    match_count = 0
    alignment_sum = 0.0
    sample_count = 0
    for polygon in polygons[:sample_limit]:
        try:
            triangle = tuple(vertices[index] for index in polygon.vertex_indices)
        except IndexError:
            continue
        computed = collision_triangle_normal(triangle)
        if computed is None:
            continue
        polygon_normal = polygon.normal
        polygon_length = math.sqrt(
            sum(component * component for component in polygon_normal)
        )
        if polygon_length < 1.0:
            continue
        dot = abs(
            sum(computed[index] * polygon_normal[index] for index in range(3))
            / polygon_length
        )
        alignment_sum += dot
        sample_count += 1
        if dot >= 0.95:
            match_count += 1
    if sample_count == 0:
        return 0, 0.0
    return match_count, alignment_sum / sample_count


def collision_triangle_normal(
    vertices: tuple[ZsiCollisionVertex, ZsiCollisionVertex, ZsiCollisionVertex],
) -> tuple[float, float, float] | None:
    a, b, c = vertices
    ab = (b.x - a.x, b.y - a.y, b.z - a.z)
    ac = (c.x - a.x, c.y - a.y, c.z - a.z)
    cross = (
        ab[1] * ac[2] - ab[2] * ac[1],
        ab[2] * ac[0] - ab[0] * ac[2],
        ab[0] * ac[1] - ab[1] * ac[0],
    )
    length = math.sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2])
    if length < 1.0:
        return None
    return (cross[0] / length, cross[1] / length, cross[2] / length)


def read_collision_polygon(view: BinaryView, offset: int) -> ZsiCollisionPolygon:
    return ZsiCollisionPolygon(
        type=view.u16(offset),
        raw_vtx_a=view.u16(offset + 2),
        raw_vtx_b=view.u16(offset + 4),
        raw_vtx_c=view.u16(offset + 6),
        marker=view.u16(offset + 8),
        normal=(view.s16(offset + 10), view.s16(offset + 12), view.s16(offset + 14)),
        dist=view.f32(offset + 16),
    )


def read_surface_type(view: BinaryView, offset: int) -> ZsiSurfaceType:
    return ZsiSurfaceType(
        data1=view.u32(offset),
        data2=view.u32(offset + 4),
    )


def find_effective_surface_type_layout(
    view: BinaryView,
    surface_type_offset: int,
    surface_type_count: int,
    section_end_offset: int,
    bgcam_count: int,
) -> tuple[int, tuple[ZsiSurfaceType, ...]]:
    raw = tuple(
        read_surface_type(view, surface_type_offset + index * 8)
        for index in range(surface_type_count)
    )
    prefixed_offset = surface_type_offset + 0x10
    prefixed_end = prefixed_offset + surface_type_count * 8
    if prefixed_end <= section_end_offset and surface_type_has_section_prefix(raw):
        return (
            prefixed_offset,
            tuple(
                read_surface_type(view, prefixed_offset + index * 8)
                for index in range(surface_type_count)
            ),
        )
    if prefixed_end == section_end_offset:
        prefixed = tuple(
            read_surface_type(view, prefixed_offset + index * 8)
            for index in range(surface_type_count)
        )
        if surface_type_layout_score(prefixed, bgcam_count) > surface_type_layout_score(
            raw,
            bgcam_count,
        ):
            return prefixed_offset, prefixed
    return surface_type_offset, raw


def surface_type_layout_score(
    surface_types: tuple[ZsiSurfaceType, ...],
    bgcam_count: int,
) -> int:
    if not surface_types:
        return -0x10000
    score = 0
    for index, surface_type in enumerate(surface_types):
        cam_data_index = surface_type.data1 & 0xFF
        camera_valid = bgcam_count <= 0 or cam_data_index < bgcam_count
        score += 4 if camera_valid else -4
        if index < 2 and not camera_valid:
            score -= 8
        if (surface_type.data2 & 0xFFFF) == 0x55DA:
            score -= 8
    return score


def surface_type_has_section_prefix(surface_types: tuple[ZsiSurfaceType, ...]) -> bool:
    if len(surface_types) < 3:
        return False
    first = surface_types[0]
    second = surface_types[1]
    third = surface_types[2]
    return (
        (first.data2 & 0xFFFF) == 0x55DA
        and first.data1 != 0
        and second.data1 != 0
        and (third.data1 & 0xFF) < 0x40
    )


def read_bg_cam_info(view: BinaryView, offset: int) -> ZsiBgCamInfo:
    return ZsiBgCamInfo(
        setting=view.u16(offset),
        count=view.u16(offset + 2),
        data_offset=view.u32(offset + 4),
    )


def find_effective_bg_cam_layout(
    view: BinaryView,
    bgcam_offset: int,
    bgcam_count: int,
    file_size: int,
) -> (
    tuple[
        int,
        int,
        int,
        tuple[ZsiBgCamInfo, ...],
        tuple[int, ...],
        tuple[tuple[int, int, int], ...],
    ]
    | None
):
    for effective_offset in (bgcam_offset, bgcam_offset + 0x10):
        if effective_offset < 0 or effective_offset + bgcam_count * 8 > file_size:
            continue

        entries = tuple(
            read_bg_cam_info(view, effective_offset + index * 8)
            for index in range(bgcam_count)
        )
        pointer_adjustment = effective_offset - bgcam_offset
        position_offset = effective_offset + bgcam_count * 8
        indices: list[int] = []
        max_position_index = 0
        valid = True

        for entry in entries:
            if entry.setting > 0xFF or entry.count > 0x40:
                valid = False
                break
            if entry.count == 0:
                if entry.data_offset != 0:
                    valid = False
                    break
                indices.append(0)
                continue

            adjusted_offset = entry.data_offset + pointer_adjustment
            if (
                adjusted_offset < position_offset
                or adjusted_offset + entry.count * 6 > file_size
            ):
                valid = False
                break
            if (adjusted_offset - position_offset) % 6 != 0:
                valid = False
                break

            index = (adjusted_offset - position_offset) // 6
            indices.append(index)
            max_position_index = max(max_position_index, index + entry.count)

        if not valid:
            continue

        position_vectors = tuple(
            read_camera_position_vector(view, position_offset + index * 6)
            for index in range(max_position_index)
        )
        return (
            effective_offset,
            position_offset,
            pointer_adjustment,
            entries,
            tuple(indices),
            position_vectors,
        )

    return None


def read_camera_position_vector(view: BinaryView, offset: int) -> tuple[int, int, int]:
    return (view.s16(offset), view.s16(offset + 2), view.s16(offset + 4))


def find_effective_polygon_layout(
    view: BinaryView,
    polygon_offset: int,
    surface_type_offset: int,
    raw_polygon_count: int,
    vertex_count: int,
    surface_type_count: int,
) -> tuple[int, int] | None:
    full_count_candidates: list[tuple[int, int]] = []
    best: tuple[int, int] | None = None
    best_valid = -1
    for prefix_size in range(0, 0x12, 2):
        candidate_offset = polygon_offset + prefix_size
        candidate_end = candidate_offset + raw_polygon_count * 0x14
        if candidate_end > len(view.data):
            continue
        if candidate_end > surface_type_offset + 0x10:
            continue

        valid_count = 0
        for index in range(raw_polygon_count):
            polygon = read_collision_polygon(view, candidate_offset + index * 0x14)
            if not collision_polygon_is_valid(
                polygon, vertex_count, surface_type_count
            ):
                break
            valid_count += 1

        if valid_count == raw_polygon_count:
            full_count_candidates.append((candidate_offset, raw_polygon_count))
        if valid_count > best_valid:
            best_valid = valid_count
            best = (candidate_offset, valid_count)

    if full_count_candidates:
        return min(full_count_candidates, key=lambda candidate: candidate[0])

    if best is not None and best_valid > 0:
        return best
    return None


def collision_polygon_is_valid(
    polygon: ZsiCollisionPolygon,
    vertex_count: int,
    surface_type_count: int,
) -> bool:
    return (
        polygon.type < surface_type_count
        and polygon.vtx_a < vertex_count
        and polygon.vtx_b < vertex_count
        and polygon.vtx_c < vertex_count
    )


def plausible_water_boxes_at(
    view: BinaryView,
    offset: int,
    count: int,
    data_size: int,
) -> tuple[ZsiWaterBox, ...]:
    if count <= 0 or offset < 0 or offset + count * 0x10 > data_size:
        return ()
    boxes = tuple(
        read_zsi_water_box(view, offset + index * 0x10) for index in range(count)
    )
    if all(box.is_plausible() for box in boxes):
        return boxes
    return ()


def polygon_type_usage(
    polygons: tuple[ZsiCollisionPolygon, ...],
) -> list[dict[str, int]]:
    usage: dict[int, int] = {}
    for polygon in polygons:
        usage[polygon.type] = usage.get(polygon.type, 0) + 1
    return [
        {"type": polygon_type, "count": usage[polygon_type]}
        for polygon_type in sorted(usage)
    ]
