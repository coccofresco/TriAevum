from __future__ import annotations

import csv
import io
import re
from collections import defaultdict
from dataclasses import dataclass
from typing import Any, Iterable


STRUCT_FIELD_PATTERN = re.compile(
    r"^analysis/(?:codebin.*fields|codebin_actor_instance_structs)\.csv$",
    re.IGNORECASE,
)
EXPECTED_HEADER = (
    "structure,structure_size,offset,type,name,confidence,source,notes"
)
ACTOR_INIT_RECORDS_PATH = "analysis/codebin_actor_init_records.csv"

_IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_NATIVE_TYPE = re.compile(r"\bOot3d[A-Za-z0-9_]+\b")
_MEMBER_ACCESS = re.compile(r"(?:->|\.)\s*([A-Za-z_][A-Za-z0-9_]*)")
_ARRAY_TYPE = re.compile(r"^(?P<element>.+)\[(?P<count>[0-9]+)\]$")

_PRIMITIVE_SIZES = {
    "bool": 1,
    "u8": 1,
    "s8": 1,
    "byte": 1,
    "sbyte": 1,
    "undef1": 1,
    "undefined": 1,
    "undefined1": 1,
    "u16": 2,
    "s16": 2,
    "ushort": 2,
    "undef2": 2,
    "undefined2": 2,
    "u32": 4,
    "s32": 4,
    "uint": 4,
    "f32": 4,
    "undef4": 4,
    "undefined4": 4,
    "u64": 8,
    "s64": 8,
    "f64": 8,
    "undefined8": 8,
}


@dataclass(frozen=True)
class LayoutPromotion:
    header: str
    manifest: dict[str, Any]


@dataclass(frozen=True)
class FieldCandidate:
    structure: str
    structure_size: int
    offset: int
    type_name: str
    name: str
    confidence: str
    source: str
    notes: str
    evidence_file: str


def _parse_int(value: object) -> int:
    text = str(value or "").strip()
    if not text:
        raise ValueError("empty native layout integer")
    return int(text, 0) if text.lower().startswith("0x") else int(text, 10)


def _read_fields(path: str, payload: bytes) -> list[FieldCandidate]:
    text = payload.decode("utf-8-sig", errors="strict")
    first_line = text.splitlines()[0] if text.splitlines() else ""
    if first_line != EXPECTED_HEADER:
        return []
    rows = csv.DictReader(io.StringIO(text))
    result: list[FieldCandidate] = []
    for row in rows:
        structure = str(row.get("structure") or "").strip()
        name = str(row.get("name") or "").strip()
        type_name = str(row.get("type") or "").strip()
        if not _IDENTIFIER.fullmatch(structure):
            raise ValueError(f"invalid recovered structure name in {path}: {structure!r}")
        if not _IDENTIFIER.fullmatch(name):
            raise ValueError(f"invalid recovered field name in {path}: {name!r}")
        size = _parse_int(row.get("structure_size"))
        offset = _parse_int(row.get("offset"))
        if size <= 0 or offset < 0 or offset >= size:
            raise ValueError(
                f"invalid recovered field extent in {path}: {structure}.{name}"
            )
        result.append(
            FieldCandidate(
                structure=structure,
                structure_size=size,
                offset=offset,
                type_name=type_name,
                name=name,
                confidence=str(row.get("confidence") or ""),
                source=str(row.get("source") or ""),
                notes=str(row.get("notes") or ""),
                evidence_file=path,
            )
        )
    return result


def _base_type(type_name: str) -> tuple[str, int]:
    value = type_name.strip()
    array = _ARRAY_TYPE.fullmatch(value)
    if array is not None:
        return _base_type(array.group("element"))
    pointer_depth = 0
    while value.endswith("*"):
        pointer_depth += 1
        value = value[:-1].strip()
    return value, pointer_depth


def _type_size(type_name: str, structure_sizes: dict[str, int]) -> int | None:
    value = type_name.strip()
    array = _ARRAY_TYPE.fullmatch(value)
    if array is not None:
        element_size = _type_size(array.group("element"), structure_sizes)
        return (
            None
            if element_size is None
            else element_size * int(array.group("count"))
        )
    if value.endswith("*"):
        return 4
    if value in _PRIMITIVE_SIZES:
        return _PRIMITIVE_SIZES[value]
    return structure_sizes.get(value)


def _field_declaration(type_name: str, name: str) -> str:
    value = type_name.strip()
    array = _ARRAY_TYPE.fullmatch(value)
    if array is not None:
        return (
            f"{_field_declaration(array.group('element'), name)}"
            f"[{array.group('count')}]"
        )
    if value.endswith("*"):
        target, pointer_depth = _base_type(value)
        rendered = target
        for _ in range(pointer_depth):
            rendered = f"GuestPtr32<{rendered}>"
        return f"{rendered} {name}"
    return f"{value} {name}"


def _body_requirements(bodies: Iterable[str]) -> tuple[set[str], set[str]]:
    type_names: set[str] = set()
    member_names: set[str] = set()
    for body in bodies:
        type_names.update(_NATIVE_TYPE.findall(body))
        member_names.update(_MEMBER_ACCESS.findall(body))
    return type_names, member_names


def _candidate_identity(candidate: FieldCandidate) -> tuple[str, str]:
    return candidate.type_name, candidate.name


def build_native_layout_promotion(
    files: dict[str, bytes], bodies: Iterable[str]
) -> LayoutPromotion:
    field_files = {
        path: payload
        for path, payload in files.items()
        if STRUCT_FIELD_PATTERN.fullmatch(path)
    }
    all_fields: list[FieldCandidate] = []
    compatible_files: set[str] = set()
    for path, payload in sorted(field_files.items()):
        rows = _read_fields(path, payload)
        if rows:
            compatible_files.add(path)
            all_fields.extend(rows)
    actor_init_payload = files.get(ACTOR_INIT_RECORDS_PATH)
    actor_init_prefix_count = 0
    if actor_init_payload is not None:
        actor_rows = csv.DictReader(
            io.StringIO(actor_init_payload.decode("utf-8-sig", errors="strict"))
        )
        for row in actor_rows:
            structure = str(row.get("structure_name") or "").strip()
            prefix_type = str(row.get("prefix_type") or "").strip()
            if not _IDENTIFIER.fullmatch(structure) or not _IDENTIFIER.fullmatch(
                prefix_type
            ):
                raise ValueError("invalid ActorInit structure or prefix type")
            all_fields.append(
                FieldCandidate(
                    structure=structure,
                    structure_size=_parse_int(row.get("instance_size")),
                    offset=0,
                    type_name=prefix_type,
                    name="dyna" if prefix_type == "Oot3dDynaPolyActor" else "actor",
                    confidence=str(row.get("confidence") or ""),
                    source=str(row.get("evidence") or ""),
                    notes="Exact common prefix from the native ActorInit record.",
                    evidence_file=ACTOR_INIT_RECORDS_PATH,
                )
            )
            actor_init_prefix_count += 1

    structure_sizes: dict[str, int] = {}
    fields_by_structure: dict[str, list[FieldCandidate]] = defaultdict(list)
    for field in all_fields:
        structure_sizes[field.structure] = max(
            structure_sizes.get(field.structure, 0), field.structure_size
        )
        fields_by_structure[field.structure].append(field)

    requested_types, member_names = _body_requirements(bodies)
    selected_types = set(requested_types)
    selected_fields: dict[str, list[FieldCandidate]] = defaultdict(list)
    unresolved: dict[str, list[dict[str, Any]]] = defaultdict(list)
    overlay_reasons: dict[str, list[dict[str, Any]]] = defaultdict(list)
    queue = sorted(selected_types)
    visited: set[str] = set()
    while queue:
        structure = queue.pop(0)
        if structure in visited:
            continue
        visited.add(structure)
        slots: dict[int, list[FieldCandidate]] = defaultdict(list)
        for field in fields_by_structure.get(structure, []):
            slots[field.offset].append(field)
        for offset, candidates in sorted(slots.items()):
            unique: dict[tuple[str, str], FieldCandidate] = {}
            evidence: dict[tuple[str, str], list[str]] = defaultdict(list)
            for candidate in candidates:
                identity = _candidate_identity(candidate)
                unique.setdefault(identity, candidate)
                evidence[identity].append(candidate.evidence_file)
            referenced = [
                candidate
                for identity, candidate in unique.items()
                if identity[1] in member_names
            ]
            if not referenced:
                continue
            if len(referenced) > 1:
                names = [candidate.name for candidate in referenced]
                if len(names) != len(set(names)):
                    unresolved[structure].append(
                        {
                            "kind": "same_name_slot_conflict",
                            "offset": f"0x{offset:04X}",
                            "candidates": [
                                {"type": candidate.type_name, "name": candidate.name}
                                for candidate in referenced
                            ],
                        }
                    )
                    continue
                overlay_reasons[structure].append(
                    {
                        "kind": "referenced_slot_aliases",
                        "offset": f"0x{offset:04X}",
                        "candidates": [
                            {
                                "type": candidate.type_name,
                                "name": candidate.name,
                                "evidence_files": sorted(
                                    set(evidence[_candidate_identity(candidate)])
                                ),
                            }
                            for candidate in referenced
                        ],
                    }
                )
            for field in referenced:
                field_size = _type_size(field.type_name, structure_sizes)
                if field_size is None:
                    unresolved[structure].append(
                        {
                            "kind": "unknown_field_type",
                            "offset": f"0x{offset:04X}",
                            "type": field.type_name,
                            "name": field.name,
                        }
                    )
                    continue
                if field.offset + field_size > structure_sizes[structure]:
                    unresolved[structure].append(
                        {
                            "kind": "field_outside_structure",
                            "offset": f"0x{offset:04X}",
                            "type": field.type_name,
                            "name": field.name,
                        }
                    )
                    continue
                selected_fields[structure].append(field)
                base, _ = _base_type(field.type_name)
                if base.startswith("Oot3d") and base not in selected_types:
                    selected_types.add(base)
                    queue.append(base)

    renderable: dict[str, list[FieldCandidate]] = {}
    render_modes: dict[str, str] = {}
    for structure in sorted(selected_types):
        if structure not in structure_sizes:
            unresolved[structure].append({"kind": "missing_structure_size"})
            continue
        fields = sorted(
            selected_fields.get(structure, []), key=lambda row: (row.offset, row.name)
        )
        previous_end = 0
        overlap = False
        for field in fields:
            field_size = _type_size(field.type_name, structure_sizes)
            if field_size is None:
                overlap = True
                break
            if field.offset < previous_end:
                overlay_reasons[structure].append(
                    {
                        "kind": "referenced_field_overlap",
                        "offset": f"0x{field.offset:04X}",
                        "type": field.type_name,
                        "name": field.name,
                    }
                )
                overlap = True
            previous_end = max(previous_end, field.offset + field_size)
        if not unresolved.get(structure):
            renderable[structure] = fields
            render_modes[structure] = (
                "anonymous_union_overlay"
                if overlap or overlay_reasons.get(structure)
                else "sequential_partial_layout"
            )

    # Embedded structures must be emitted before their owners.
    ordered: list[str] = []
    temporary: set[str] = set()
    permanent: set[str] = set()

    def visit(structure: str) -> None:
        if structure in permanent or structure not in renderable:
            return
        if structure in temporary:
            unresolved[structure].append({"kind": "embedded_structure_cycle"})
            renderable.pop(structure, None)
            return
        temporary.add(structure)
        for field in renderable.get(structure, []):
            base, pointer_depth = _base_type(field.type_name)
            if pointer_depth == 0 and base.startswith("Oot3d"):
                visit(base)
        temporary.remove(structure)
        permanent.add(structure)
        if structure in renderable:
            ordered.append(structure)

    for structure in sorted(renderable):
        visit(structure)

    lines = [
        "#pragma once",
        "",
        "// Generated from revision-pinned OOT3D structure-field evidence.",
        "// Unknown ranges are intentional; guest pointers remain 32-bit values.",
        "template <typename T>",
        "struct GuestPtr32 {",
        "    u32 address;",
        "    T* get() const;",
        "    T* operator->() const;",
        "    operator T*() const;",
        "    GuestPtr32& operator=(T* value);",
        "    GuestPtr32& operator=(std::nullptr_t value);",
        "};",
        "static_assert(sizeof(GuestPtr32<void>) == 4);",
        "",
        "#pragma pack(push, 1)",
    ]
    emitted_field_count = 0
    for structure in ordered:
        size = structure_sizes[structure]
        fields = renderable[structure]
        lines.extend([f"struct {structure} {{"])
        if render_modes[structure] == "anonymous_union_overlay":
            lines.extend(["    union {", f"        byte _opaque[0x{size:X}];"])
            for index, field in enumerate(fields):
                lines.extend(["        struct {"])
                if field.offset:
                    lines.append(f"            byte _pad_{index:03d}[0x{field.offset:X}];")
                lines.append(
                    f"            {_field_declaration(field.type_name, field.name)};"
                )
                lines.append("        };")
                emitted_field_count += 1
            lines.append("    };")
        else:
            cursor = 0
            padding_index = 0
            for field in fields:
                if field.offset > cursor:
                    lines.append(
                        f"    byte _pad_{padding_index:03d}[0x{field.offset - cursor:X}];"
                    )
                    padding_index += 1
                lines.append(f"    {_field_declaration(field.type_name, field.name)};")
                field_size = _type_size(field.type_name, structure_sizes)
                if field_size is None:
                    raise AssertionError("validated field lost its size")
                cursor = field.offset + field_size
                emitted_field_count += 1
            if cursor < size:
                lines.append(f"    byte _pad_{padding_index:03d}[0x{size - cursor:X}];")
        lines.extend(["};", f"static_assert(sizeof({structure}) == 0x{size:X});"])
        for field in fields:
            lines.append(
                f"static_assert(offsetof({structure}, {field.name}) == "
                f"0x{field.offset:X});"
            )
        lines.append("")
    lines.append("#pragma pack(pop)")
    lines.append("")

    structure_records = []
    for structure in sorted(selected_types):
        fields = renderable.get(structure, [])
        structure_records.append(
            {
                "name": structure,
                "size": structure_sizes.get(structure),
                "status": (
                    "generated_partial_layout"
                    if structure in renderable
                    else "forward_declaration_only"
                ),
                "layout_mode": render_modes.get(structure),
                "emitted_fields": [
                    {
                        "offset": f"0x{field.offset:04X}",
                        "type": field.type_name,
                        "name": field.name,
                        "evidence_file": field.evidence_file,
                    }
                    for field in fields
                ],
                "unresolved": unresolved.get(structure, []),
                "overlay_reasons": overlay_reasons.get(structure, []),
            }
        )
    manifest = {
        "field_catalog_files": len(compatible_files),
        "field_catalog_rows": len(all_fields),
        "actor_init_prefix_records": actor_init_prefix_count,
        "catalog_structures": len(structure_sizes),
        "closure_type_references": len(requested_types),
        "selected_structures": len(selected_types),
        "generated_structures": len(renderable),
        "forward_only_structures": len(selected_types) - len(renderable),
        "emitted_fields": emitted_field_count,
        "member_names_observed": len(member_names),
        "structures": structure_records,
    }
    return LayoutPromotion(header="\n".join(lines), manifest=manifest)
