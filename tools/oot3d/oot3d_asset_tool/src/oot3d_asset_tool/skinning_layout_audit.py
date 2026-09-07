from __future__ import annotations

import json
import math
from collections import Counter
from pathlib import Path

from .binary import BinaryView, ParseError
from .cmb import PICA_F32, PICA_U8, PICA_U16, data_type_size
from .csab_tracks import is_cmb_file
from .romfs_inventory import sorted_counter
from .zar import ZarArchive

SKINNING_INDEX_SLOT = 6
SKINNING_WEIGHT_SLOT = 7


def audit_actor_skinning_vertex_layout(
    actor_root: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
) -> dict[str, object]:
    if not actor_root.is_dir():
        raise ParseError(f"{actor_root}: expected an extracted OOT3D actor directory")

    paths = sorted(path for path in actor_root.rglob("*") if path.is_file())
    zar_paths = [path for path in paths if path.suffix.lower() == ".zar"]
    loose_cmb_paths = [path for path in paths if path.suffix.lower() == ".cmb"]

    model_counts: Counter[str] = Counter()
    shape_counts: Counter[str] = Counter()
    primitive_counts: Counter[str] = Counter()
    extra_attribute_kind_counts: Counter[str] = Counter()
    layout_counts: Counter[str] = Counter()
    influence_width_counts: Counter[str] = Counter()
    nonzero_influence_counts: Counter[str] = Counter()
    weight_sum_counts: Counter[str] = Counter()
    parse_errors: list[dict[str, object]] = []
    validation_errors: list[dict[str, object]] = []
    model_records: list[dict[str, object]] = []
    skinned_shape_samples: list[dict[str, object]] = []
    constant_attribute_samples: list[dict[str, object]] = []

    def add_model(
        data: bytes,
        *,
        container_path: Path,
        container_type: str,
        embedded_name: str | None = None,
        embedded_index: int | None = None,
        embedded_type: str | None = None,
    ) -> None:
        model_counts["discovered"] += 1
        try:
            record = audit_cmb_skinning_vertex_layout(
                data,
                source=f"{container_path}!{embedded_name}" if embedded_name else str(container_path),
                container_path=container_path.relative_to(actor_root).as_posix(),
                container_type=container_type,
                embedded_name=embedded_name,
                embedded_index=embedded_index,
                embedded_type=embedded_type,
                sample_limit=sample_limit,
            )
        except Exception as exc:
            parse_errors.append(
                {
                    "path": container_path.relative_to(actor_root).as_posix(),
                    "kind": "cmb_skinning_vertex_layout",
                    "embedded_name": embedded_name,
                    "message": str(exc),
                }
            )
            return
        model_counts["parsed"] += 1
        if record["skinned_shape_count"]:
            model_counts["skinned_model_count"] += 1
            model_records.append(record)
        merge_counts(shape_counts, record["shape_counts"])
        merge_counts(primitive_counts, record["primitive_counts"])
        merge_counts(extra_attribute_kind_counts, record["extra_attribute_kind_counts"])
        merge_counts(layout_counts, record["layout_counts"])
        merge_counts(influence_width_counts, record["influence_width_counts"])
        merge_counts(nonzero_influence_counts, record["nonzero_influence_counts"])
        merge_counts(weight_sum_counts, record["weight_sum_counts"])
        validation_errors.extend(record["validation_errors"])
        append_limited(skinned_shape_samples, record["skinned_shape_samples"], sample_limit)
        append_limited(
            constant_attribute_samples,
            record["constant_attribute_samples"],
            sample_limit,
        )

    for path in loose_cmb_paths:
        add_model(
            path.read_bytes(),
            container_path=path,
            container_type="cmb",
        )

    for path in zar_paths:
        try:
            archive = ZarArchive.from_path(path)
        except Exception as exc:
            parse_errors.append(
                {
                    "path": path.relative_to(actor_root).as_posix(),
                    "kind": "zar_archive",
                    "embedded_name": None,
                    "message": str(exc),
                }
            )
            continue
        for file in archive.files:
            if not is_cmb_file(file):
                continue
            add_model(
                archive.read_file(file),
                container_path=path,
                container_type="zar",
                embedded_name=file.name,
                embedded_index=file.index,
                embedded_type=file.type_name,
            )

    audit = {
        "format": "oot3d_actor_skinning_vertex_layout_v1",
        "actor_root": str(actor_root),
        "file_count": len(paths),
        "archive_count": len(zar_paths),
        "loose_cmb_count": len(loose_cmb_paths),
        "model_counts": {
            "discovered": model_counts["discovered"],
            "parsed": model_counts["parsed"],
            "parse_errors": len(parse_errors),
            "skinned_model_count": model_counts["skinned_model_count"],
        },
        "shape_counts": sorted_counter(shape_counts),
        "primitive_counts": sorted_counter(primitive_counts),
        "extra_attribute_kind_counts": sorted_counter(extra_attribute_kind_counts),
        "layout_counts": sorted_counter(layout_counts),
        "influence_width_counts": sorted_counter(influence_width_counts),
        "nonzero_influence_counts": sorted_counter(nonzero_influence_counts),
        "weight_sum_counts": sorted_counter(weight_sum_counts),
        "skinned_shape_samples": skinned_shape_samples,
        "constant_attribute_samples": constant_attribute_samples,
        "skinned_model_records": model_records,
        "validation_error_count": len(validation_errors),
        "validation_errors": validation_errors[:sample_limit],
        "parse_error_count": len(parse_errors),
        "parse_errors": parse_errors,
    }
    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(audit, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return audit


def audit_cmb_skinning_vertex_layout(
    data: bytes,
    *,
    source: str,
    container_path: str,
    container_type: str,
    embedded_name: str | None,
    embedded_index: int | None,
    embedded_type: str | None,
    sample_limit: int,
) -> dict[str, object]:
    view = BinaryView(data, source)
    if view.bytes(0, 4) != b"cmb ":
        raise ParseError(f"{source}: expected CMB magic")
    model_name = view.cstr(0x10, 0x10)
    sklm_off = view.u32(0x30)
    vatr_off = view.u32(0x38)
    indices_off = view.u32(0x3C)
    shp_off = sklm_off + view.u32(sklm_off + 0x0C)
    if view.bytes(shp_off, 4) != b"shp ":
        raise ParseError(f"{source}: expected SHP chunk at 0x{shp_off:x}")
    if view.bytes(vatr_off, 4) != b"vatr":
        raise ParseError(f"{source}: expected VATR chunk at 0x{vatr_off:x}")

    shape_count = view.u32(shp_off + 0x08)
    sepd_offsets = [shp_off + view.u16(shp_off + 0x10 + index * 2) for index in range(shape_count)]
    vlds = [read_vld(view, vatr_off, slot) for slot in range(8)]

    shape_counts: Counter[str] = Counter(total=shape_count)
    primitive_counts: Counter[str] = Counter()
    extra_attribute_kind_counts: Counter[str] = Counter()
    layout_counts: Counter[str] = Counter()
    influence_width_counts: Counter[str] = Counter()
    nonzero_influence_counts: Counter[str] = Counter()
    weight_sum_counts: Counter[str] = Counter()
    validation_errors: list[dict[str, object]] = []
    skinned_shape_samples: list[dict[str, object]] = []
    constant_attribute_samples: list[dict[str, object]] = []

    for shape_index, sepd_off in enumerate(sepd_offsets):
        if view.bytes(sepd_off, 4) != b"sepd":
            raise ParseError(f"{source}: expected SEPD chunk at 0x{sepd_off:x}")
        flags = view.u16(sepd_off + 0x0A)
        auto_flags = view.u16(sepd_off + 0x106)
        primitives = read_shape_primitives(view, sepd_off, indices_off)
        modes = {primitive["skinning_mode"] for primitive in primitives}
        for primitive in primitives:
            primitive_counts[f"mode_{primitive['skinning_mode']}"] += 1
        if modes == {0}:
            continue

        shape_counts["skinned"] += 1
        max_index = max(
            (max(primitive["indices"]) for primitive in primitives if primitive["indices"]),
            default=-1,
        )
        vertex_count = max_index + 1
        if vertex_count <= 0:
            add_validation_error(
                validation_errors,
                source,
                shape_index,
                "skinned_shape_without_visible_indices",
            )
            continue

        index_attr = vertex_attribute_info(
            view,
            vatr_off,
            sepd_offsets,
            sepd_off,
            SKINNING_INDEX_SLOT,
            vlds,
            vertex_count,
        )
        weight_attr = vertex_attribute_info(
            view,
            vatr_off,
            sepd_offsets,
            sepd_off,
            SKINNING_WEIGHT_SLOT,
            vlds,
            vertex_count,
        )
        extra_attribute_kind_counts[f"slot6_{index_attr['kind']}"] += 1
        extra_attribute_kind_counts[f"slot7_{weight_attr['kind']}"] += 1

        if modes == {1}:
            shape_counts["mode_1"] += 1
            validate_mode1_shape(
                view,
                source,
                shape_index,
                primitives,
                index_attr,
                weight_attr,
                flags,
                auto_flags,
                shape_counts,
                layout_counts,
                validation_errors,
            )
        elif modes == {2}:
            shape_counts["mode_2"] += 1
            validate_mode2_shape(
                view,
                source,
                shape_index,
                primitives,
                index_attr,
                weight_attr,
                flags,
                auto_flags,
                shape_counts,
                layout_counts,
                influence_width_counts,
                nonzero_influence_counts,
                weight_sum_counts,
                validation_errors,
            )
        else:
            shape_counts["mixed_mode"] += 1
            add_validation_error(
                validation_errors,
                source,
                shape_index,
                "mixed_skinning_modes_in_shape",
                modes=sorted(modes),
            )

        if len(skinned_shape_samples) < sample_limit:
            skinned_shape_samples.append(
                shape_sample(
                    container_path,
                    embedded_name,
                    model_name,
                    shape_index,
                    primitives,
                    index_attr,
                    weight_attr,
                )
            )
        if (
            (index_attr["kind"] == "constant" or weight_attr["kind"] == "constant")
            and len(constant_attribute_samples) < sample_limit
        ):
            constant_attribute_samples.append(
                shape_sample(
                    container_path,
                    embedded_name,
                    model_name,
                    shape_index,
                    primitives,
                    index_attr,
                    weight_attr,
                )
            )

    return {
        "container_path": container_path,
        "container_type": container_type,
        "embedded_name": embedded_name,
        "embedded_index": embedded_index,
        "embedded_type": embedded_type,
        "model_name": model_name,
        "shape_count": shape_count,
        "skinned_shape_count": shape_counts["skinned"],
        "shape_counts": sorted_counter(shape_counts),
        "primitive_counts": sorted_counter(primitive_counts),
        "extra_attribute_kind_counts": sorted_counter(extra_attribute_kind_counts),
        "layout_counts": sorted_counter(layout_counts),
        "influence_width_counts": sorted_counter(influence_width_counts),
        "nonzero_influence_counts": sorted_counter(nonzero_influence_counts),
        "weight_sum_counts": sorted_counter(weight_sum_counts),
        "skinned_shape_samples": skinned_shape_samples,
        "constant_attribute_samples": constant_attribute_samples,
        "validation_errors": validation_errors,
    }


def validate_mode1_shape(
    view: BinaryView,
    source: str,
    shape_index: int,
    primitives: list[dict[str, object]],
    index_attr: dict[str, object],
    weight_attr: dict[str, object],
    flags: int,
    auto_flags: int,
    shape_counts: Counter[str],
    layout_counts: Counter[str],
    validation_errors: list[dict[str, object]],
) -> None:
    layout_counts[f"mode1_slot6_{index_attr['kind']}_slot7_{weight_attr['kind']}"] += 1
    if not flags & (1 << SKINNING_INDEX_SLOT):
        add_validation_error(validation_errors, source, shape_index, "mode1_missing_slot6")
        return
    if flags & (1 << SKINNING_WEIGHT_SLOT):
        add_validation_error(validation_errors, source, shape_index, "mode1_unexpected_slot7")
        return
    if index_attr["width"] != 1:
        add_validation_error(
            validation_errors,
            source,
            shape_index,
            "mode1_slot6_width_not_one",
            width=index_attr["width"],
        )
        return
    if not mode1_index_attribute_is_supported(index_attr):
        add_validation_error(
            validation_errors,
            source,
            shape_index,
            "mode1_slot6_unsupported_encoding",
            attribute=index_attr,
        )
        return

    for primitive in primitives:
        bone_count = int(primitive["bone_count"])
        for vertex_index in sorted(set(primitive["indices"])):  # type: ignore[arg-type]
            value = int(round(attribute_row(view, index_attr, 1, vertex_index)[0]))
            shape_counts["mode1_vertex_rows"] += 1
            if value >= bone_count:
                add_validation_error(
                    validation_errors,
                    source,
                    shape_index,
                    "mode1_slot6_index_out_of_primitive_palette",
                    vertex_index=vertex_index,
                    value=value,
                    bone_count=bone_count,
                )
                return
    shape_counts["mode1_valid_layout"] += 1


def validate_mode2_shape(
    view: BinaryView,
    source: str,
    shape_index: int,
    primitives: list[dict[str, object]],
    index_attr: dict[str, object],
    weight_attr: dict[str, object],
    flags: int,
    auto_flags: int,
    shape_counts: Counter[str],
    layout_counts: Counter[str],
    influence_width_counts: Counter[str],
    nonzero_influence_counts: Counter[str],
    weight_sum_counts: Counter[str],
    validation_errors: list[dict[str, object]],
) -> None:
    layout_counts[f"mode2_slot6_{index_attr['kind']}_slot7_{weight_attr['kind']}"] += 1
    if not flags & (1 << SKINNING_INDEX_SLOT):
        add_validation_error(validation_errors, source, shape_index, "mode2_missing_slot6")
        return
    if not flags & (1 << SKINNING_WEIGHT_SLOT):
        add_validation_error(validation_errors, source, shape_index, "mode2_missing_slot7")
        return
    width = choose_influence_width(index_attr, weight_attr)
    if width is None or width <= 0:
        add_validation_error(
            validation_errors,
            source,
            shape_index,
            "mode2_cannot_infer_influence_width",
            index_attribute=index_attr,
            weight_attribute=weight_attr,
        )
        return
    if not mode2_index_attribute_is_supported(index_attr):
        add_validation_error(
            validation_errors,
            source,
            shape_index,
            "mode2_slot6_unsupported_encoding",
            attribute=index_attr,
        )
        return
    if not mode2_weight_attribute_is_supported(weight_attr):
        add_validation_error(
            validation_errors,
            source,
            shape_index,
            "mode2_slot7_unsupported_encoding",
            attribute=weight_attr,
        )
        return

    influence_width_counts[str(width)] += 1
    invalid_index = 0
    invalid_weight_sum = 0
    for primitive in primitives:
        bone_count = int(primitive["bone_count"])
        for vertex_index in sorted(set(primitive["indices"])):  # type: ignore[arg-type]
            indices = [int(round(value)) for value in attribute_row(view, index_attr, width, vertex_index)]
            weights = attribute_row(view, weight_attr, width, vertex_index)
            if weight_attr["kind"] == "data":
                weights = [weight * 100.0 for weight in weights]
            weight_sum = round(sum(weights), 4)
            weight_sum_counts[weight_sum_key(weight_sum)] += 1
            nonzero_influence_counts[str(sum(1 for weight in weights if not math.isclose(weight, 0.0, abs_tol=1e-6)))] += 1
            shape_counts["mode2_vertex_rows"] += 1
            if not math.isclose(weight_sum, 100.0, abs_tol=0.001):
                invalid_weight_sum += 1
            for index_value, weight in zip(indices, weights):
                if not math.isclose(weight, 0.0, abs_tol=1e-6) and index_value >= bone_count:
                    invalid_index += 1

    if invalid_index:
        add_validation_error(
            validation_errors,
            source,
            shape_index,
            "mode2_nonzero_slot6_index_out_of_primitive_palette",
            invalid_index_count=invalid_index,
        )
        return
    if invalid_weight_sum:
        add_validation_error(
            validation_errors,
            source,
            shape_index,
            "mode2_weight_sum_not_100",
            invalid_weight_sum_count=invalid_weight_sum,
        )
        return
    shape_counts["mode2_valid_layout"] += 1


def read_shape_primitives(
    view: BinaryView,
    sepd_off: int,
    indices_off: int,
) -> list[dict[str, object]]:
    primitives: list[dict[str, object]] = []
    prms_count = view.u16(sepd_off + 0x08)
    for prms_index in range(prms_count):
        prms_off = sepd_off + view.u16(sepd_off + 0x108 + prms_index * 2)
        if view.bytes(prms_off, 4) != b"prms":
            raise ParseError(f"{view.source}: expected PRMS chunk at 0x{prms_off:x}")
        skinning_mode = view.u16(prms_off + 0x0C)
        bone_count = view.u16(prms_off + 0x0E)
        bone_off = prms_off + view.u32(prms_off + 0x10)
        prm_off = prms_off + view.u32(prms_off + 0x14)
        bone_indices = tuple(view.u16(bone_off + index * 2) for index in range(bone_count))
        visible = view.u32(prm_off + 0x08)
        data_type = view.u16(prm_off + 0x10)
        count = view.u16(prm_off + 0x14)
        first = view.u16(prm_off + 0x16)
        if not visible:
            indices: tuple[int, ...] = ()
        elif data_type == PICA_U16:
            indices = tuple(view.u16(indices_off + (first + index) * 2) for index in range(count))
        else:
            raise ParseError(
                f"{view.source}: skinning layout audit expected U16 PRM indices, got 0x{data_type:x}"
            )
        primitives.append(
            {
                "primitive_index": prms_index,
                "skinning_mode": skinning_mode,
                "bone_count": bone_count,
                "bone_indices": bone_indices,
                "indices": indices,
                "triangle_count": len(indices) // 3,
            }
        )
    return primitives


def vertex_attribute_info(
    view: BinaryView,
    vatr_off: int,
    sepd_offsets: list[int],
    sepd_off: int,
    slot: int,
    vlds: list[dict[str, int]],
    vertex_count: int,
) -> dict[str, object]:
    flags = view.u16(sepd_off + 0x0A)
    auto_flags = view.u16(sepd_off + 0x106)
    if not flags & (1 << slot):
        return {"slot": slot, "kind": "missing", "width": 0}

    base = sepd_off + 0x24 + slot * 0x1C
    offset = view.u32(base)
    scale = view.f32(base + 0x04)
    data_type = view.u16(base + 0x08)
    mode = view.u16(base + 0x0A)
    constants = tuple(view.f32(base + 0x0C + index * 4) for index in range(4))
    if auto_flags & (1 << slot) or mode != 0:
        return {
            "slot": slot,
            "kind": "constant",
            "width": None,
            "data_type": data_type,
            "scale": scale,
            "mode": mode,
            "constants": constants,
        }

    vld = vlds[slot]
    enabled_offsets = sorted(
        {
            view.u32(other + 0x24 + slot * 0x1C)
            for other in sepd_offsets
            if view.u16(other + 0x0A) & (1 << slot)
            and not view.u16(other + 0x106) & (1 << slot)
        }
    )
    next_offsets = [candidate for candidate in enabled_offsets if candidate > offset]
    end = min(next_offsets) if next_offsets else vld["length"]
    byte_count = max(0, end - offset)
    width = infer_attribute_width(byte_count, vertex_count, data_type)
    return {
        "slot": slot,
        "kind": "data",
        "width": width,
        "data_type": data_type,
        "scale": scale,
        "mode": mode,
        "offset": offset,
        "byte_count": byte_count,
        "start": vatr_off + vld["offset"] + offset,
    }


def read_vld(view: BinaryView, vatr_off: int, slot: int) -> dict[str, int]:
    base = vatr_off + 0x0C + slot * 8
    return {
        "length": view.u32(base),
        "offset": view.u32(base + 4),
    }


def infer_attribute_width(
    byte_count: int,
    vertex_count: int,
    data_type: int,
) -> int | None:
    if vertex_count <= 0:
        return 0
    size = data_type_size(data_type)
    matches = []
    for width in range(0, 17):
        needed = vertex_count * width * size
        if needed <= byte_count and byte_count - needed <= 3:
            matches.append(width)
    return matches[-1] if matches else None


def choose_influence_width(
    index_attr: dict[str, object],
    weight_attr: dict[str, object],
) -> int | None:
    index_width = index_attr.get("width")
    weight_width = weight_attr.get("width")
    if isinstance(index_width, int) and isinstance(weight_width, int) and index_width == weight_width:
        return index_width
    if isinstance(index_width, int) and weight_attr["kind"] == "constant":
        return index_width
    if isinstance(weight_width, int) and index_attr["kind"] == "constant":
        return weight_width
    if index_attr["kind"] == "constant" and weight_attr["kind"] == "constant":
        constants = weight_attr.get("constants")
        if not isinstance(constants, tuple):
            return None
        for width in range(4, 0, -1):
            if math.isclose(sum(constants[:width]), 100.0, abs_tol=0.001) and all(
                math.isclose(value, 0.0, abs_tol=0.001) for value in constants[width:]
            ):
                return width
        return sum(1 for value in constants if not math.isclose(value, 0.0, abs_tol=0.001))
    return None


def attribute_row(
    view: BinaryView,
    attr: dict[str, object],
    width: int,
    vertex_index: int,
) -> list[float]:
    if attr["kind"] == "constant":
        constants = attr.get("constants")
        if not isinstance(constants, tuple):
            raise ParseError("constant skinning attribute is missing constants")
        return [float(constants[index]) for index in range(width)]
    if attr["kind"] != "data":
        raise ParseError("missing skinning attribute row")
    if attr["data_type"] != PICA_U8:
        raise ParseError(f"expected U8 skinning attribute data, got 0x{int(attr['data_type']):x}")
    start = int(attr["start"])
    scale = float(attr["scale"])
    return [
        float(view.u8(start + vertex_index * width + index)) * scale
        for index in range(width)
    ]


def mode1_index_attribute_is_supported(attr: dict[str, object]) -> bool:
    if attr["kind"] == "data":
        return attr["data_type"] == PICA_U8 and attr["scale"] == 1.0
    if attr["kind"] == "constant":
        return attr["data_type"] == PICA_F32 and attr["scale"] == 1.0
    return False


def mode2_index_attribute_is_supported(attr: dict[str, object]) -> bool:
    return mode1_index_attribute_is_supported(attr)


def mode2_weight_attribute_is_supported(attr: dict[str, object]) -> bool:
    if attr["kind"] == "data":
        return attr["data_type"] == PICA_U8 and round(float(attr["scale"]), 6) == 0.01
    if attr["kind"] == "constant":
        return attr["data_type"] == PICA_F32 and attr["scale"] == 1.0
    return False


def shape_sample(
    container_path: str,
    embedded_name: str | None,
    model_name: str,
    shape_index: int,
    primitives: list[dict[str, object]],
    index_attr: dict[str, object],
    weight_attr: dict[str, object],
) -> dict[str, object]:
    return {
        "container_path": container_path,
        "embedded_name": embedded_name,
        "model_name": model_name,
        "shape_index": shape_index,
        "primitive_count": len(primitives),
        "skinning_modes": sorted({primitive["skinning_mode"] for primitive in primitives}),
        "primitive_bone_counts": sorted({primitive["bone_count"] for primitive in primitives}),
        "index_attribute": compact_attribute(index_attr),
        "weight_attribute": compact_attribute(weight_attr),
    }


def compact_attribute(attr: dict[str, object]) -> dict[str, object]:
    compact = {
        "slot": attr["slot"],
        "kind": attr["kind"],
        "width": attr.get("width"),
    }
    for key in ("data_type", "scale", "mode", "byte_count", "constants"):
        if key in attr:
            compact[key] = attr[key]
    return compact


def add_validation_error(
    errors: list[dict[str, object]],
    source: str,
    shape_index: int,
    reason: str,
    **details: object,
) -> None:
    errors.append(
        {
            "source": source,
            "shape_index": shape_index,
            "reason": reason,
            **details,
        }
    )


def weight_sum_key(value: float) -> str:
    if math.isclose(value, round(value), abs_tol=0.0001):
        return str(int(round(value)))
    return f"{value:.4f}"


def merge_counts(target: Counter[str], source: object) -> None:
    if isinstance(source, dict):
        for key, value in source.items():
            target[str(key)] += int(value)


def append_limited(target: list[dict[str, object]], source: object, limit: int) -> None:
    if not isinstance(source, list):
        return
    remaining = limit - len(target)
    if remaining <= 0:
        return
    target.extend(source[:remaining])
