from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from .binary import BinaryView, ParseError
from .media_asset_audit import size_summary
from .romfs_inventory import sorted_counter
from .zsi import ZsiFile
from .zsi_scene_audit import zsi_scene_role, zsi_scene_stem


ZSI_CUTSCENE_CAMERA_EXPORT_MANIFEST = "zsi_cutscene_camera_export_manifest.json"
SCENE_CUTSCENE_COMMAND_ID = 0x17
HEADER_DELTAS = tuple(range(0, 0x34, 4))
PREFERRED_DELTAS = (0x18, 0x20, 0x24, 0x14, 0x1C, 0x28, 0x2C, 0x30, 0x10, 0x00)
OOT3D_CUTSCENE_MAGIC = 0x51444220
OOT3D_CUTSCENE_HEADER_DELTAS = (0x10, 0x00)
OOT3D_CUTSCENE_CAMERA_LIST_COMMANDS = {0x0001, 0x0002, 0x0005, 0x0006}
OOT3D_CUTSCENE_SINGLE_CAMERA_COMMANDS = {0x0007, 0x0008}
OOT3D_CUTSCENE_PACKED_3WORD_PAIR_COMMANDS = {0x0009}
OOT3D_CUTSCENE_COUNTED_3WORD_COMMANDS = {0x0013, 0x008C}
OOT3D_CUTSCENE_FIXED_16_COMMANDS = {0x002D, 0x03E8}
OOT3D_CUTSCENE_BLOB_16BIT_COUNT_COMMANDS = {0x0096}
OOT3D_CUTSCENE_BLOB_SIZE_COMMANDS = {0x0097}

CS_CMD_CAM_EYE = 0x0001
CS_CMD_CAM_AT = 0x0002
CS_CMD_MISC = 0x0003
CS_CMD_SET_LIGHTING = 0x0004
CS_CMD_CAM_EYE_REL_TO_PLAYER = 0x0005
CS_CMD_CAM_AT_REL_TO_PLAYER = 0x0006
CS_CMD_07 = 0x0007
CS_CMD_08 = 0x0008
CS_CMD_09 = 0x0009
CS_CMD_SET_PLAYER_ACTION = 0x000A
CS_CMD_TEXTBOX = 0x0013
CS_CMD_SCENE_TRANS_FX = 0x002D
CS_CMD_PLAYBGM = 0x0056
CS_CMD_STOPBGM = 0x0057
CS_CMD_FADEBGM = 0x007C
CS_CMD_SETTIME = 0x008C
CS_CMD_TERMINATOR = 0x03E8
CS_CMD_END = -1

CAMERA_LIST_COMMANDS = {
    CS_CMD_CAM_EYE,
    CS_CMD_CAM_AT,
    CS_CMD_CAM_EYE_REL_TO_PLAYER,
    CS_CMD_CAM_AT_REL_TO_PLAYER,
}
SINGLE_CAMERA_POINT_COMMANDS = {CS_CMD_07, CS_CMD_08}
THREE_WORD_ENTRY_COMMANDS = {CS_CMD_09, CS_CMD_TEXTBOX, CS_CMD_SETTIME}
SIMPLE_16_BYTE_COMMANDS = {CS_CMD_SCENE_TRANS_FX, CS_CMD_TERMINATOR}
TWELVE_WORD_ENTRY_COMMANDS = {
    CS_CMD_MISC,
    CS_CMD_SET_LIGHTING,
    CS_CMD_SET_PLAYER_ACTION,
    CS_CMD_PLAYBGM,
    CS_CMD_STOPBGM,
    CS_CMD_FADEBGM,
    0x000E,
    0x000F,
    0x0010,
    0x0011,
    0x0012,
    0x0017,
    0x0018,
    0x0019,
    0x001D,
    0x001E,
    0x001F,
    0x0022,
    0x0023,
    0x0024,
    0x0025,
    0x0026,
    0x0027,
    0x0028,
    0x0029,
    0x002A,
    0x002B,
    0x002C,
    0x002E,
    0x002F,
    0x0030,
    0x0031,
    0x0032,
    0x0033,
    0x0034,
    0x0035,
    0x0036,
    0x0037,
    0x0039,
    0x003A,
    0x003C,
    0x003F,
    0x0040,
    0x0041,
    0x0042,
    0x0043,
    0x0044,
    0x0045,
    0x0046,
    0x004A,
    0x004B,
    0x004C,
    0x004D,
    0x004E,
    0x004F,
    0x0050,
    0x0051,
    0x0052,
    0x0053,
    0x0054,
    0x0055,
    0x0058,
    0x0059,
    0x005A,
    0x005D,
    0x005E,
    0x0069,
    0x006A,
    0x006B,
    0x006C,
    0x006E,
    0x006F,
    0x0072,
    0x0073,
    0x0074,
    0x0075,
    0x0076,
    0x0077,
    0x0078,
    0x0079,
    0x007B,
    0x007D,
    0x007E,
    0x007F,
    0x0080,
    0x0081,
    0x0082,
    0x0083,
    0x0084,
    0x0085,
    0x0086,
    0x0087,
    0x0088,
    0x0089,
    0x008A,
    0x008B,
    0x008F,
    0x0090,
}
KNOWN_N64_CUTSCENE_COMMANDS = (
    CAMERA_LIST_COMMANDS
    | SINGLE_CAMERA_POINT_COMMANDS
    | THREE_WORD_ENTRY_COMMANDS
    | SIMPLE_16_BYTE_COMMANDS
    | TWELVE_WORD_ENTRY_COMMANDS
    | {CS_CMD_END}
)

CS_COMMAND_NAMES = {
    CS_CMD_CAM_EYE: "CS_CMD_CAM_EYE",
    CS_CMD_CAM_AT: "CS_CMD_CAM_AT",
    CS_CMD_MISC: "CS_CMD_MISC",
    CS_CMD_SET_LIGHTING: "CS_CMD_SET_LIGHTING",
    CS_CMD_CAM_EYE_REL_TO_PLAYER: "CS_CMD_CAM_EYE_REL_TO_PLAYER",
    CS_CMD_CAM_AT_REL_TO_PLAYER: "CS_CMD_CAM_AT_REL_TO_PLAYER",
    CS_CMD_07: "CS_CMD_07",
    CS_CMD_08: "CS_CMD_08",
    CS_CMD_09: "CS_CMD_09",
    CS_CMD_SET_PLAYER_ACTION: "CS_CMD_SET_PLAYER_ACTION",
    CS_CMD_TEXTBOX: "CS_CMD_TEXTBOX",
    CS_CMD_SCENE_TRANS_FX: "CS_CMD_SCENE_TRANS_FX",
    CS_CMD_PLAYBGM: "CS_CMD_PLAYBGM",
    CS_CMD_STOPBGM: "CS_CMD_STOPBGM",
    CS_CMD_FADEBGM: "CS_CMD_FADEBGM",
    CS_CMD_SETTIME: "CS_CMD_SETTIME",
    CS_CMD_TERMINATOR: "CS_CMD_TERMINATOR",
    CS_CMD_END: "CS_CMD_END",
}


def audit_zsi_cutscene_metadata(
    scene_root: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
    include_records: bool = True,
) -> dict[str, object]:
    if not scene_root.is_dir():
        raise ParseError(f"{scene_root}: expected an extracted OOT3D scene directory")

    records: list[dict[str, object]] = []
    sample_records: list[dict[str, object]] = []
    parse_errors: list[dict[str, object]] = []

    zsi_file_count = 0
    cutscene_files: set[str] = set()
    cutscene_setups: set[tuple[str, int]] = set()
    cutscene_command_total = 0
    argument_in_bounds_count = 0
    strict_decoded_count = 0
    strict_decode_error_count = 0
    strict_camera_point_total = 0
    strict_command_total = 0
    oot3d_native_decoded_count = 0
    oot3d_native_decode_error_count = 0
    oot3d_native_command_total = 0

    role_counts: Counter[str] = Counter()
    cutscene_role_counts: Counter[str] = Counter()
    cutscene_setup_count_counts: Counter[str] = Counter()
    first_word_counts: Counter[str] = Counter()
    header_candidate_counts_by_delta: Counter[str] = Counter()
    strict_decode_counts_by_delta: Counter[str] = Counter()
    strict_command_id_counts: Counter[str] = Counter()
    strict_command_category_counts: Counter[str] = Counter()
    strict_end_frame_counts: Counter[str] = Counter()
    oot3d_native_header_delta_counts: Counter[str] = Counter()
    oot3d_native_magic_counts: Counter[str] = Counter()
    oot3d_native_command_id_counts: Counter[str] = Counter()
    oot3d_native_command_category_counts: Counter[str] = Counter()
    oot3d_native_end_frame_counts: Counter[str] = Counter()
    unique_offsets: set[tuple[str, int]] = set()

    for path in sorted(scene_root.rglob("*.zsi")):
        zsi_file_count += 1
        rel = path.relative_to(scene_root).as_posix()
        role = zsi_scene_role(path)
        stem = zsi_scene_stem(path)
        role_counts[role] += 1
        try:
            zsi = ZsiFile.from_path(path)
            setups = zsi.scene_setups()
            view = BinaryView(zsi.data, str(path))
        except Exception as exc:
            add_parse_error(parse_errors, rel, "scene_setup", exc)
            continue

        file_records: list[dict[str, object]] = []
        for setup in setups:
            setup_records: list[dict[str, object]] = []
            for command in setup.commands:
                if command.command_id != SCENE_CUTSCENE_COMMAND_ID:
                    continue
                cutscene_files.add(rel)
                cutscene_setups.add((rel, setup.index))
                cutscene_command_total += 1
                unique_offsets.add((rel, command.argument))

                argument_in_bounds = 0 <= command.argument < len(zsi.data)
                if argument_in_bounds:
                    argument_in_bounds_count += 1
                    if command.argument + 4 <= len(zsi.data):
                        first_word_counts[f"0x{view.u32(command.argument):08x}"] += 1

                header_candidates = cutscene_header_candidates(view, command.argument)
                for candidate in header_candidates:
                    if candidate["plausible_n64_header"]:
                        header_candidate_counts_by_delta[f"+0x{candidate['delta']:02x}"] += 1

                strict_decode = best_strict_cutscene_decode(view, command.argument)
                if strict_decode["decoded"]:
                    strict_decoded_count += 1
                    strict_camera_point_total += int(strict_decode["camera_point_count"])
                    strict_command_total += int(strict_decode["command_count"])
                    strict_decode_counts_by_delta[f"+0x{strict_decode['delta']:02x}"] += 1
                    strict_end_frame_counts[str(strict_decode["end_frame"])] += 1
                    for decoded_command in strict_decode["commands"]:
                        command_id = f"0x{int(decoded_command['command_id']) & 0xFFFF:04x}"
                        strict_command_id_counts[command_id] += 1
                        strict_command_category_counts[str(decoded_command["category"])] += 1
                else:
                    strict_decode_error_count += 1

                oot3d_native_decode = best_oot3d_native_cutscene_decode(view, command.argument)
                if oot3d_native_decode["decoded"]:
                    oot3d_native_decoded_count += 1
                    oot3d_native_command_total += int(oot3d_native_decode["command_count"])
                    oot3d_native_header_delta_counts[
                        f"+0x{int(oot3d_native_decode['header_delta']):02x}"
                    ] += 1
                    oot3d_native_magic_counts[str(oot3d_native_decode["magic_hex"])] += 1
                    oot3d_native_end_frame_counts[str(oot3d_native_decode["end_frame"])] += 1
                    for decoded_command in oot3d_native_decode["commands"]:
                        command_id = f"0x{int(decoded_command['command_id']) & 0xFFFFFFFF:08x}"
                        oot3d_native_command_id_counts[command_id] += 1
                        oot3d_native_command_category_counts[
                            str(decoded_command["category"])
                        ] += 1
                else:
                    oot3d_native_decode_error_count += 1

                record = {
                    "path": rel,
                    "role": role,
                    "scene_stem": stem,
                    "setup_index": setup.index,
                    "setup_offset": setup.offset,
                    "command_offset": command.offset,
                    "command_word": command.command_word,
                    "command_parameter": command.parameter,
                    "argument": command.argument,
                    "argument_in_bounds": argument_in_bounds,
                    "bytes_available_from_argument": max(0, len(zsi.data) - command.argument)
                    if argument_in_bounds
                    else 0,
                    "raw_prefix_hex": raw_prefix_hex(view, command.argument, 0x40),
                    "header_candidates": header_candidates,
                    "strict_n64_decode": strict_decode,
                    "oot3d_native_decode": oot3d_native_decode,
                }
                setup_records.append(record)
                if include_records:
                    records.append(record)
                if len(sample_records) < sample_limit:
                    sample_records.append(record)

            if setup_records:
                file_records.extend(setup_records)

        if file_records:
            cutscene_role_counts[role] += 1
            cutscene_setup_count_counts[str(len(file_records))] += 1

    audit: dict[str, object] = {
        "format": "oot3d_zsi_cutscene_metadata_audit_v2",
        "scene_root": str(scene_root),
        "reference": {
            "n64_scene_command": "SCENE_CMD_ID_CUTSCENE_DATA",
            "n64_scene_command_id": "0x17",
            "shipwright_scene_reference": "soh/include/z64scene.h",
            "shipwright_cutscene_reference": "soh/include/z64cutscene.h",
            "shipwright_runtime_reference": "soh/src/code/z_demo.c",
            "oot3d_note": "OOT3D scene command 0x17 blocks often carry an OOT3D prefix or non-N64 command stream; strict N64 decode is conservative.",
            "oot3d_native_runtime_reference": "OOT3D code.bin Cutscene_ProcessCommands at 0x002C5BA0",
        },
        "zsi_file_count": zsi_file_count,
        "role_counts": sorted_counter(role_counts),
        "cutscene_file_count": len(cutscene_files),
        "cutscene_role_counts": sorted_counter(cutscene_role_counts),
        "cutscene_setup_count": len(cutscene_setups),
        "cutscene_command_total": cutscene_command_total,
        "unique_cutscene_data_offset_count": len(unique_offsets),
        "argument_in_bounds_count": argument_in_bounds_count,
        "argument_out_of_bounds_count": cutscene_command_total - argument_in_bounds_count,
        "cutscene_setup_count_counts": sorted_counter(cutscene_setup_count_counts),
        "first_word_counts": sorted_counter(first_word_counts),
        "header_candidate_counts_by_delta": sorted_counter(header_candidate_counts_by_delta),
        "strict_n64_decoded_count": strict_decoded_count,
        "strict_n64_decode_error_count": strict_decode_error_count,
        "strict_n64_decode_counts_by_delta": sorted_counter(strict_decode_counts_by_delta),
        "strict_n64_command_total": strict_command_total,
        "strict_n64_camera_point_total": strict_camera_point_total,
        "strict_n64_command_id_counts": sorted_counter(strict_command_id_counts),
        "strict_n64_command_category_counts": sorted_counter(strict_command_category_counts),
        "strict_n64_end_frame_counts": sorted_counter(strict_end_frame_counts),
        "oot3d_native_decoded_count": oot3d_native_decoded_count,
        "oot3d_native_decode_error_count": oot3d_native_decode_error_count,
        "oot3d_native_command_total": oot3d_native_command_total,
        "oot3d_native_header_delta_counts": sorted_counter(oot3d_native_header_delta_counts),
        "oot3d_native_magic_counts": sorted_counter(oot3d_native_magic_counts),
        "oot3d_native_command_id_counts": sorted_counter(oot3d_native_command_id_counts),
        "oot3d_native_command_category_counts": sorted_counter(
            oot3d_native_command_category_counts
        ),
        "oot3d_native_end_frame_counts": sorted_counter(oot3d_native_end_frame_counts),
        "parse_error_count": len(parse_errors),
        "parse_errors": parse_errors,
        "sample_records": sample_records,
        "records": records if include_records else [],
        "fallback_policy": [
            "This audit records cutscene/camera evidence only.",
            "No Shipwright cutscene resources are generated by this gate.",
            "Strict N64-compatible decode remains subset evidence only; OOT3D-native decode follows the code.bin interpreter layout.",
        ],
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(audit, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return audit


def export_zsi_cutscene_camera_data(
    scene_root: Path,
    output_dir: Path,
    *,
    sample_limit: int = 100,
    include_records: bool = True,
) -> dict[str, object]:
    output_dir.mkdir(parents=True, exist_ok=True)
    cutscene_output_dir = output_dir / "cutscenes"
    cutscene_output_dir.mkdir(parents=True, exist_ok=True)

    audit = audit_zsi_cutscene_metadata(
        scene_root,
        None,
        sample_limit=sample_limit,
        include_records=True,
    )

    records: list[dict[str, object]] = []
    sample_records: list[dict[str, object]] = []
    exported_files: list[int] = []
    command_id_counts: Counter[str] = Counter()
    command_category_counts: Counter[str] = Counter()
    per_scene_counts: Counter[str] = Counter()
    issue_counts: Counter[str] = Counter()
    exported_count = 0
    command_count = 0
    camera_point_count = 0
    camera_command_count = 0
    export_issue_count = 0

    for source_record in audit["records"]:
        strict_decode = source_record["strict_n64_decode"]
        if not strict_decode["decoded"]:
            continue

        block = strict_cutscene_export_block(source_record, strict_decode)
        export_name = cutscene_export_file_name(source_record)
        export_path = cutscene_output_dir / export_name
        export_path.write_text(
            json.dumps(block, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        issues = validate_strict_cutscene_export_block(block)
        if issues:
            export_issue_count += 1
            for issue in issues:
                issue_counts[issue] += 1
        else:
            issue_counts["none"] += 1

        exported_count += 1
        file_size = export_path.stat().st_size
        exported_files.append(file_size)
        command_count += int(strict_decode["command_count"])
        camera_point_count += int(strict_decode["camera_point_count"])
        per_scene_counts[str(source_record["scene_stem"])] += 1
        for command in strict_decode["commands"]:
            command_id_counts[str(command["command_id_hex"])] += 1
            command_category_counts[str(command["category"])] += 1
            if command["category"] in {"camera_list", "single_camera_point"}:
                camera_command_count += 1

        record = {
            "path": source_record["path"],
            "scene_stem": source_record["scene_stem"],
            "setup_index": source_record["setup_index"],
            "argument": source_record["argument"],
            "delta": strict_decode["delta"],
            "offset": strict_decode["offset"],
            "end_frame": strict_decode["end_frame"],
            "command_count": strict_decode["command_count"],
            "camera_point_count": strict_decode["camera_point_count"],
            "export_file": str(export_path),
            "export_size": file_size,
            "issues": issues,
        }
        if len(sample_records) < sample_limit:
            sample_records.append(record)
        if include_records:
            records.append(record)

    manifest: dict[str, object] = {
        "format": "oot3d_zsi_cutscene_camera_export_manifest_v1",
        "scene_root": str(scene_root),
        "output_dir": str(output_dir),
        "cutscene_output_dir": str(cutscene_output_dir),
        "source_cutscene_command_total": audit["cutscene_command_total"],
        "source_strict_n64_decoded_count": audit["strict_n64_decoded_count"],
        "source_strict_n64_decode_error_count": audit["strict_n64_decode_error_count"],
        "exported_cutscene_count": exported_count,
        "exported_command_count": command_count,
        "exported_camera_command_count": camera_command_count,
        "exported_camera_point_count": camera_point_count,
        "export_issue_count": export_issue_count,
        "issue_counts": sorted_counter(issue_counts),
        "per_scene_counts": sorted_counter(per_scene_counts),
        "command_id_counts": sorted_counter(command_id_counts),
        "command_category_counts": sorted_counter(command_category_counts),
        "export_file_size_summary": size_summary(exported_files),
        "sample_records": sample_records,
        "records": records if include_records else [],
        "notes": [
            "Only OOT3D scene command 0x17 blocks that pass the strict N64 cutscene decoder are exported.",
            "This export preserves decoded camera points and command metadata for offline validation.",
            "It does not generate Shipwright runtime cutscene resources or change scene routing.",
        ],
    }

    manifest_path = output_dir / ZSI_CUTSCENE_CAMERA_EXPORT_MANIFEST
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return manifest


def cutscene_header_candidates(view: BinaryView, argument: int) -> list[dict[str, object]]:
    candidates: list[dict[str, object]] = []
    for delta in HEADER_DELTAS:
        offset = argument + delta
        if offset < 0 or offset + 8 > len(view.data):
            continue
        total_entries = view.s32(offset)
        end_frame = view.s32(offset + 4)
        plausible = 1 <= total_entries <= 200 and 1 <= end_frame <= 20000
        first_command = None
        if plausible and offset + 12 <= len(view.data):
            first_command = view.s32(offset + 8)
        candidates.append(
            {
                "delta": delta,
                "offset": offset,
                "total_entries": total_entries,
                "end_frame": end_frame,
                "plausible_n64_header": plausible,
                "first_command_id": first_command,
                "first_command_name": cutscene_command_name(first_command)
                if first_command is not None
                else None,
            }
        )
    return candidates


def best_strict_cutscene_decode(view: BinaryView, argument: int) -> dict[str, object]:
    attempts: list[dict[str, object]] = []
    errors: list[dict[str, object]] = []
    for delta in PREFERRED_DELTAS:
        offset = argument + delta
        try:
            decoded = parse_strict_n64_cutscene_block(view, offset)
        except ParseError as exc:
            errors.append({"delta": delta, "offset": offset, "error": str(exc)})
            continue
        decoded["delta"] = delta
        decoded["offset"] = offset
        attempts.append(decoded)

    if not attempts:
        return {
            "decoded": False,
            "error_count": len(errors),
            "sample_errors": errors[:5],
        }

    attempts.sort(
        key=lambda item: (
            0 if int(item["delta"]) == 0x18 else 1,
            -int(item["decoded_size"]),
            int(item["delta"]),
        )
    )
    selected = attempts[0]
    selected["decoded"] = True
    selected["alternate_decode_count"] = len(attempts) - 1
    if len(attempts) > 1:
        selected["alternate_decodes"] = [
            {
                "delta": attempt["delta"],
                "offset": attempt["offset"],
                "total_entries": attempt["total_entries"],
                "end_frame": attempt["end_frame"],
                "command_count": attempt["command_count"],
                "decoded_size": attempt["decoded_size"],
            }
            for attempt in attempts[1:6]
        ]
    return selected


def best_oot3d_native_cutscene_decode(view: BinaryView, argument: int) -> dict[str, object]:
    attempts: list[dict[str, object]] = []
    errors: list[dict[str, object]] = []
    for delta in OOT3D_CUTSCENE_HEADER_DELTAS:
        offset = argument + delta
        try:
            decoded = parse_oot3d_native_cutscene_block(view, offset)
        except ParseError as exc:
            errors.append({"delta": delta, "offset": offset, "error": str(exc)})
            continue
        decoded["header_delta"] = delta
        decoded["header_offset"] = offset
        attempts.append(decoded)

    if not attempts:
        return {
            "decoded": False,
            "error_count": len(errors),
            "sample_errors": errors[:5],
        }

    attempts.sort(key=lambda item: (0 if int(item["header_delta"]) == 0x10 else 1))
    selected = attempts[0]
    selected["decoded"] = True
    selected["alternate_decode_count"] = len(attempts) - 1
    if len(attempts) > 1:
        selected["alternate_decodes"] = [
            {
                "header_delta": attempt["header_delta"],
                "header_offset": attempt["header_offset"],
                "command_count": attempt["command_count"],
                "end_frame": attempt["end_frame"],
                "decoded_size": attempt["decoded_size"],
            }
            for attempt in attempts[1:6]
        ]
    return selected


def parse_oot3d_native_cutscene_block(view: BinaryView, offset: int) -> dict[str, object]:
    require_available(view, offset, 0x10)
    magic = view.u32(offset)
    if magic != OOT3D_CUTSCENE_MAGIC:
        raise ParseError(
            f"OOT3D cutscene magic mismatch 0x{magic:08x}, expected 0x{OOT3D_CUTSCENE_MAGIC:08x}"
        )

    version_or_flags = view.u32(offset + 4)
    command_count = view.s32(offset + 8)
    end_frame = view.s32(offset + 0xC)
    if not (0 <= command_count <= 1000):
        raise ParseError(f"implausible OOT3D cutscene command count {command_count}")
    if not (0 <= end_frame <= 200000):
        raise ParseError(f"implausible OOT3D cutscene end frame {end_frame}")

    cursor = offset + 0x10
    commands: list[dict[str, object]] = []
    for index in range(command_count):
        command = parse_oot3d_native_cutscene_command(view, cursor, index)
        commands.append(command)
        cursor += int(command["total_size"])

    return {
        "magic": magic,
        "magic_hex": f"0x{magic:08x}",
        "version_or_flags": version_or_flags,
        "version_or_flags_hex": f"0x{version_or_flags:08x}",
        "command_count": command_count,
        "end_frame": end_frame,
        "command_stream_offset": offset + 0x10,
        "decoded_size": cursor - offset,
        "commands": commands,
    }


def parse_oot3d_native_cutscene_command(
    view: BinaryView,
    offset: int,
    index: int,
) -> dict[str, object]:
    require_available(view, offset, 4)
    command_id = view.s32(offset)
    category = oot3d_native_cutscene_command_category(command_id)

    entry_count: int | None = None
    camera_point_count = 0
    blob_size: int | None = None
    packed_stride = 0

    if category == "noop":
        total_size = 4
    elif category == "camera_list":
        cursor = offset + 0xC
        while True:
            require_available(view, cursor, 0x10)
            camera_point_count += 1
            continue_flag = view.s8(cursor)
            cursor += 0x10
            if continue_flag == -1:
                break
            if camera_point_count > 500:
                raise ParseError(
                    f"OOT3D camera list did not terminate at command 0x{offset:x}"
                )
        total_size = cursor - offset
    elif category == "single_camera_fixed":
        require_available(view, offset, 0x1C)
        camera_point_count = 1
        total_size = 0x1C
    elif category == "packed_3word_pairs":
        entry_count = read_oot3d_command_count(view, offset + 4)
        if entry_count < 1:
            total_size = 8
        elif entry_count & 1:
            total_size = 0x14 + ((entry_count - 1) // 2) * 0x18
        else:
            total_size = 8 + (entry_count // 2) * 0x18
        packed_stride = 0x18
        require_available(view, offset, total_size)
    elif category == "counted_3word_entries":
        entry_count = read_oot3d_command_count(view, offset + 4)
        total_size = 8 + entry_count * 0xC
        require_available(view, offset, total_size)
    elif category == "fixed16":
        require_available(view, offset, 0x10)
        total_size = 0x10
    elif category == "blob_16bit_count":
        require_available(view, offset, 0xC)
        entry_count = view.u16(offset + 0xA)
        total_size = 0xC + entry_count * 0x20
        blob_size = entry_count * 0x20
        require_available(view, offset, total_size)
    elif category == "blob_u32_size":
        require_available(view, offset, 8)
        blob_size = view.u32(offset + 4)
        if blob_size > len(view.data):
            raise ParseError(f"implausible OOT3D cutscene blob size {blob_size}")
        total_size = 8 + blob_size
        require_available(view, offset, total_size)
    else:
        entry_count = read_oot3d_command_count(view, offset + 4)
        total_size = 8 + entry_count * 0x30
        require_available(view, offset, total_size)

    return {
        "index": index,
        "offset": offset,
        "command_id": command_id,
        "command_id_hex": f"0x{command_id & 0xFFFFFFFF:08x}",
        "name": oot3d_native_cutscene_command_name(command_id),
        "category": category,
        "entry_count": entry_count,
        "camera_point_count": camera_point_count,
        "blob_size": blob_size,
        "packed_stride": packed_stride,
        "total_size": total_size,
        "payload_size": max(0, total_size - 4),
        "raw_prefix_hex": raw_prefix_hex(view, offset, min(total_size, 0x40)),
    }


def read_oot3d_command_count(view: BinaryView, offset: int) -> int:
    require_available(view, offset, 4)
    entry_count = view.s32(offset)
    if not (0 <= entry_count <= 10000):
        raise ParseError(f"implausible OOT3D cutscene command entry count {entry_count}")
    return entry_count


def oot3d_native_cutscene_command_category(command_id: int) -> str:
    if command_id == 0:
        return "noop"
    if command_id in OOT3D_CUTSCENE_CAMERA_LIST_COMMANDS:
        return "camera_list"
    if command_id in OOT3D_CUTSCENE_SINGLE_CAMERA_COMMANDS:
        return "single_camera_fixed"
    if command_id in OOT3D_CUTSCENE_PACKED_3WORD_PAIR_COMMANDS:
        return "packed_3word_pairs"
    if command_id in OOT3D_CUTSCENE_COUNTED_3WORD_COMMANDS:
        return "counted_3word_entries"
    if command_id in OOT3D_CUTSCENE_FIXED_16_COMMANDS:
        return "fixed16"
    if command_id in OOT3D_CUTSCENE_BLOB_16BIT_COUNT_COMMANDS:
        return "blob_16bit_count"
    if command_id in OOT3D_CUTSCENE_BLOB_SIZE_COMMANDS:
        return "blob_u32_size"
    return "counted_12word_entries"


def oot3d_native_cutscene_command_name(command_id: int) -> str:
    base_name = cutscene_command_name(command_id)
    if base_name:
        return base_name
    if command_id == 0:
        return "OOT3D_CS_CMD_NOOP"
    if command_id == 0x96:
        return "OOT3D_CS_CMD_CAMERA_BLOB_96"
    if command_id == 0x97:
        return "OOT3D_CS_CMD_CAMERA_BLOB_97"
    return f"OOT3D_CS_CMD_{command_id & 0xFFFFFFFF:08X}"


def parse_strict_n64_cutscene_block(view: BinaryView, offset: int) -> dict[str, object]:
    if offset < 0 or offset + 8 > len(view.data):
        raise ParseError("cutscene header outside file")
    total_entries = view.s32(offset)
    end_frame = view.s32(offset + 4)
    if not (1 <= total_entries <= 200):
        raise ParseError(f"implausible cutscene entry count {total_entries}")
    if not (1 <= end_frame <= 20000):
        raise ParseError(f"implausible cutscene end frame {end_frame}")

    cursor = offset + 8
    commands: list[dict[str, object]] = []
    camera_point_count = 0
    terminated_by_end = False
    for index in range(total_entries):
        if cursor + 4 > len(view.data):
            raise ParseError("cutscene command outside file")
        command_offset = cursor
        command_id = view.s32(cursor)
        cursor += 4
        if command_id == CS_CMD_END:
            terminated_by_end = True
            commands.append(
                cutscene_command_record(index, command_offset, command_id, "end", 0, 4)
            )
            break
        if command_id not in KNOWN_N64_CUTSCENE_COMMANDS:
            raise ParseError(f"unsupported strict N64 cutscene command 0x{command_id & 0xFFFF:04x}")

        category = cutscene_command_category(command_id)
        payload_start = cursor
        entry_count = None
        local_camera_points = 0
        list_header = None
        camera_points: list[dict[str, object]] = []
        entries: list[dict[str, object]] = []
        raw_words: list[str] = []
        if command_id in CAMERA_LIST_COMMANDS:
            require_available(view, cursor, 8)
            list_header = read_cutscene_list_header(view, cursor)
            cursor += 8
            while True:
                require_available(view, cursor, 16)
                point = read_cutscene_camera_point(view, cursor)
                camera_points.append(point)
                continue_flag = int(point["continue_flag"])
                cursor += 16
                local_camera_points += 1
                if continue_flag == -1:
                    break
                if local_camera_points > 500:
                    raise ParseError("camera list did not terminate within 500 points")
        elif command_id in SINGLE_CAMERA_POINT_COMMANDS:
            require_available(view, cursor, 24)
            list_header = read_cutscene_list_header(view, cursor)
            cursor += 24
            local_camera_points = 1
            camera_points.append(read_cutscene_camera_point(view, cursor - 16))
        elif command_id in SIMPLE_16_BYTE_COMMANDS:
            require_available(view, cursor, 12)
            raw_words = read_raw_words(view, cursor, 12)
            cursor += 12
        elif command_id in THREE_WORD_ENTRY_COMMANDS:
            entry_count = read_entry_count(view, cursor)
            cursor += 4
            require_available(view, cursor, entry_count * 12)
            entries = [
                read_cutscene_three_word_entry(view, cursor + entry_index * 12)
                for entry_index in range(entry_count)
            ]
            cursor += entry_count * 12
        else:
            entry_count = read_entry_count(view, cursor)
            cursor += 4
            require_available(view, cursor, entry_count * 48)
            entries = [
                read_cutscene_twelve_word_entry(view, cursor + entry_index * 48)
                for entry_index in range(entry_count)
            ]
            cursor += entry_count * 48

        camera_point_count += local_camera_points
        command_record = cutscene_command_record(
            index,
            command_offset,
            command_id,
            category,
            4 + (cursor - payload_start),
            cursor - command_offset,
            entry_count=entry_count,
            camera_point_count=local_camera_points,
        )
        if list_header is not None:
            command_record["list_header"] = list_header
        if camera_points:
            command_record["camera_points"] = camera_points
        if entries:
            command_record["entries"] = entries
        if raw_words:
            command_record["raw_words"] = raw_words
        commands.append(command_record)

    return {
        "total_entries": total_entries,
        "end_frame": end_frame,
        "command_count": len(commands),
        "camera_point_count": camera_point_count,
        "decoded_size": cursor - offset,
        "terminated_by_end": terminated_by_end,
        "commands": commands,
    }


def cutscene_command_record(
    index: int,
    offset: int,
    command_id: int,
    category: str,
    payload_size: int,
    total_size: int,
    *,
    entry_count: int | None = None,
    camera_point_count: int = 0,
) -> dict[str, object]:
    return {
        "index": index,
        "offset": offset,
        "command_id": command_id,
        "command_id_hex": f"0x{command_id & 0xFFFF:04x}",
        "name": cutscene_command_name(command_id),
        "category": category,
        "entry_count": entry_count,
        "camera_point_count": camera_point_count,
        "payload_size": payload_size,
        "total_size": total_size,
    }


def read_entry_count(view: BinaryView, offset: int) -> int:
    require_available(view, offset, 4)
    entry_count = view.s32(offset)
    if not (0 <= entry_count <= 1000):
        raise ParseError(f"implausible cutscene entry count {entry_count}")
    return entry_count


def require_available(view: BinaryView, offset: int, size: int) -> None:
    if offset < 0 or offset + size > len(view.data):
        raise ParseError(f"cutscene read outside file at 0x{offset:x} size 0x{size:x}")


def read_cutscene_list_header(view: BinaryView, offset: int) -> dict[str, object]:
    word0 = view.u32(offset)
    word1 = view.u32(offset + 4)
    return {
        "offset": offset,
        "word0": f"0x{word0:08x}",
        "word1": f"0x{word1:08x}",
        "param": view.s16(offset),
        "start_frame": view.s16(offset + 2),
        "end_frame": view.s16(offset + 4),
        "unused": view.s16(offset + 6),
    }


def read_cutscene_camera_point(view: BinaryView, offset: int) -> dict[str, object]:
    return {
        "offset": offset,
        "continue_flag": view.s8(offset),
        "camera_roll": view.s8(offset + 1),
        "next_point_frame": view.u16(offset + 2),
        "view_angle": view.f32(offset + 4),
        "pos": {
            "x": view.s16(offset + 8),
            "y": view.s16(offset + 10),
            "z": view.s16(offset + 12),
        },
        "unused": view.s16(offset + 14),
    }


def read_cutscene_three_word_entry(view: BinaryView, offset: int) -> dict[str, object]:
    word0 = view.u32(offset)
    word1 = view.u32(offset + 4)
    word2 = view.u32(offset + 8)
    return {
        "offset": offset,
        "raw_words": [f"0x{word0:08x}", f"0x{word1:08x}", f"0x{word2:08x}"],
        "primary": view.s16(offset),
        "start_frame": view.s16(offset + 2),
        "end_frame": view.s16(offset + 4),
    }


def read_cutscene_twelve_word_entry(view: BinaryView, offset: int) -> dict[str, object]:
    return {
        "offset": offset,
        "raw_words": read_raw_words(view, offset, 48),
        "primary": view.s16(offset),
        "start_frame": view.s16(offset + 2),
        "end_frame": view.s16(offset + 4),
    }


def read_raw_words(view: BinaryView, offset: int, size: int) -> list[str]:
    return [f"0x{view.u32(offset + index):08x}" for index in range(0, size, 4)]


def cutscene_command_category(command_id: int) -> str:
    if command_id in CAMERA_LIST_COMMANDS:
        return "camera_list"
    if command_id in SINGLE_CAMERA_POINT_COMMANDS:
        return "single_camera_point"
    if command_id in THREE_WORD_ENTRY_COMMANDS:
        return "three_word_entries"
    if command_id in SIMPLE_16_BYTE_COMMANDS:
        return "simple_16_byte"
    if command_id in TWELVE_WORD_ENTRY_COMMANDS:
        return "twelve_word_entries"
    if command_id == CS_CMD_END:
        return "end"
    return "unsupported"


def cutscene_command_name(command_id: int | None) -> str | None:
    if command_id is None:
        return None
    if command_id in CS_COMMAND_NAMES:
        return CS_COMMAND_NAMES[command_id]
    if command_id in TWELVE_WORD_ENTRY_COMMANDS:
        return "CS_CMD_ACTOR_ACTION_OR_DEFAULT_12_WORD"
    return None


def raw_prefix_hex(view: BinaryView, offset: int, size: int) -> str:
    if offset < 0 or offset >= len(view.data):
        return ""
    return view.bytes(offset, min(size, len(view.data) - offset)).hex()


def add_parse_error(
    parse_errors: list[dict[str, object]],
    path: str,
    stage: str,
    exc: Exception,
) -> None:
    parse_errors.append(
        {
            "path": path,
            "stage": stage,
            "error": str(exc),
        }
    )


def strict_cutscene_export_block(
    source_record: dict[str, object],
    strict_decode: dict[str, object],
) -> dict[str, object]:
    return {
        "format": "oot3d_zsi_strict_n64_cutscene_block_v1",
        "source": {
            "path": source_record["path"],
            "role": source_record["role"],
            "scene_stem": source_record["scene_stem"],
            "setup_index": source_record["setup_index"],
            "setup_offset": source_record["setup_offset"],
            "command_offset": source_record["command_offset"],
            "command_word": source_record["command_word"],
            "argument": source_record["argument"],
        },
        "decode": strict_decode,
        "reference": {
            "shipwright_cutscene_reference": "soh/include/z64cutscene.h",
            "shipwright_cutscene_macro_reference": "soh/include/z64cutscene_commands.h",
            "parser_mode": "strict N64-compatible OOT3D scene command 0x17 block",
        },
    }


def validate_strict_cutscene_export_block(block: dict[str, object]) -> list[str]:
    issues: list[str] = []
    decode = block["decode"]
    commands = decode["commands"]
    if decode["command_count"] != len(commands):
        issues.append("command_count_mismatch")

    camera_points = 0
    for command in commands:
        command_points = command.get("camera_points", [])
        if command["camera_point_count"] != len(command_points):
            issues.append("camera_point_count_mismatch")
        camera_points += len(command_points)
        if command["category"] == "camera_list" and command_points:
            if command_points[-1]["continue_flag"] != -1:
                issues.append("unterminated_camera_list")
    if decode["camera_point_count"] != camera_points:
        issues.append("block_camera_point_count_mismatch")
    return sorted(set(issues))


def cutscene_export_file_name(source_record: dict[str, object]) -> str:
    rel = str(source_record["path"]).replace("\\", "/")
    scene = sanitize_export_part(str(source_record["scene_stem"]))
    path_id = sanitize_export_part(Path(rel).with_suffix("").as_posix())
    return (
        f"{scene}_setup{int(source_record['setup_index']):02d}"
        f"_arg{int(source_record['argument']):06x}_{path_id}.json"
    )


def sanitize_export_part(value: str) -> str:
    cleaned = []
    for char in value:
        if char.isalnum() or char in {"_", "-"}:
            cleaned.append(char)
        else:
            cleaned.append("_")
    result = "".join(cleaned).strip("_")
    return result or "unnamed"
