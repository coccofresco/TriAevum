#!/usr/bin/env python3
"""Snapshot selected Zelda3drecomp evidence without modifying its checkout."""

from __future__ import annotations

import argparse
import csv
import fnmatch
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


PACKAGE_SOURCE_ROOT = (
    Path(__file__).resolve().parents[2]
    / "oot3d_asset_tool"
    / "src"
)
if str(PACKAGE_SOURCE_ROOT) not in sys.path:
    sys.path.insert(0, str(PACKAGE_SOURCE_ROOT))

from oot3d_asset_tool.native_abi_catalog import (  # noqa: E402
    build_native_abi_catalog,
)


EXPECTED_CODE_SHA256 = (
    "16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220"
)
SELECTED_FILES = (
    "analysis/codebin_source_paths.csv",
    "analysis/codebin_function_inventory.csv",
    "analysis/codebin_embedded_helper_signatures.csv",
    "analysis/codebin_embedded_struct_fields.csv",
    "analysis/codebin_native_curve_runtime_signatures.csv",
    "analysis/codebin_native_curve_runtime_symbols.csv",
    "analysis/codebin_object_bank_async_signatures.csv",
    "analysis/codebin_object_bank_async_struct_fields.csv",
    "analysis/codebin_object_bank_async_symbols.csv",
    "analysis/codebin_animation_producers_signatures.csv",
    "analysis/codebin_animation_producers_symbols.csv",
    "analysis/codebin_skelanime_draw_pipeline_signatures.csv",
    "analysis/codebin_skelanime_draw_pipeline_symbols.csv",
    "analysis/codebin_skelanime_draw_pipeline_struct_fields.csv",
    "analysis/codebin_actor_init_records.csv",
    "analysis/codebin_actor_workflow_candidates.csv",
    "analysis/codebin_actor_callback_entries.csv",
    "analysis/codebin_actor_callback_signatures.csv",
    "analysis/codebin_actor_callback_body_closure_tranche_162.csv",
    "analysis/codebin_actor_callback_body_closure_tranche_162_signatures.csv",
    "analysis/codebin_actor_callback_body_closure_tranche_162_symbols.csv",
    "analysis/codebin_actor_instance_structs.csv",
    "analysis/codebin_actor_embedded_fields.csv",
    "analysis/codebin_actor_embedded_placements.csv",
    "analysis/codebin_actor_animation_table_calls.csv",
    "analysis/codebin_actor_animation_table_fields.csv",
    "analysis/codebin_actor_animation_table_placements.csv",
    "analysis/codebin_actor_face_animation_fields.csv",
    "analysis/codebin_actor_face_animation_calls.csv",
    "analysis/codebin_actor_face_animation_placements.csv",
    "analysis/codebin_actor_material_binding_fields.csv",
    "analysis/codebin_actor_material_binding_placements.csv",
    "analysis/codebin_actor_material_binding_uses.csv",
    "analysis/codebin_actor_material_animation_fields.csv",
    "analysis/codebin_actor_material_animation_placements.csv",
    "analysis/codebin_actor_material_animation_uses.csv",
    "analysis/codebin_actor_typed_helper_fields.csv",
    "analysis/codebin_actor_typed_helper_fields.md",
    "analysis/codebin_actor_typed_helper_placements.csv",
    "analysis/codebin_actor_native_storage_span_fields.csv",
    "analysis/codebin_actor_native_storage_span_placements.csv",
    "analysis/codebin_actor_structural_coverage_audit_165.csv",
    "analysis/codebin_actor_structural_coverage_audit_165.md",
    "analysis/codebin_actor_common_layout_closure_tranche_167.csv",
    "analysis/codebin_actor_common_layout_closure_tranche_167_signatures.csv",
    "analysis/codebin_actor_tail_scalar_fields.csv",
    "analysis/codebin_actor_ambiguous_storage_fields.csv",
    "analysis/codebin_actor_ambiguous_storage_fields.md",
    "analysis/codebin_actor_ambiguous_storage_placements.csv",
    "analysis/codebin_actor_structural_coverage_audit_168.csv",
    "analysis/codebin_actor_structural_coverage_audit_168.md",
    "analysis/codebin_actor_native_record_fields.csv",
    "analysis/codebin_actor_native_record_fields.md",
    "analysis/codebin_actor_native_record_placements.csv",
    "analysis/codebin_actor_structural_coverage_audit_169.csv",
    "analysis/codebin_actor_structural_coverage_audit_169.md",
    "analysis/codebin_actor_fixed_stride_array_fields.csv",
    "analysis/codebin_actor_fixed_stride_array_fields.md",
    "analysis/codebin_actor_fixed_stride_array_placements.csv",
    "analysis/codebin_actor_tail_scalar_fixed_stride_final.csv",
    "analysis/codebin_actor_structural_coverage_audit_170.csv",
    "analysis/codebin_actor_structural_coverage_audit_170.md",
    "analysis/codebin_actor_core_address_map.csv",
    "analysis/codebin_actor_core_signatures.csv",
    "analysis/codebin_actor_core_struct_fields.csv",
    "analysis/codebin_actor_core_symbols.csv",
    "analysis/codebin_actor_core_workflow.csv",
    "analysis/codebin_limb_callback_helpers.csv",
    "analysis/codebin_limb_callback_helper_signatures.csv",
    "analysis/codebin_limb_callback_helper_struct_fields.csv",
    "analysis/codebin_limb_callback_helper_symbols.csv",
    "analysis/codebin_skelanime_draw_callbacks.csv",
    "analysis/codebin_skelanime_draw_callback_entries.csv",
    "analysis/codebin_skelanime_draw_preexisting_entries.csv",
    "analysis/codebin_cmb_model_instance_lifecycle_signatures.csv",
    "analysis/codebin_cmb_model_instance_lifecycle_symbols.csv",
    "analysis/codebin_cmb_renderer_signatures.csv",
    "analysis/codebin_cmb_renderer_symbols.csv",
    "analysis/codebin_cmb_geometry_signatures.csv",
    "analysis/codebin_cmb_geometry_symbols.csv",
    "analysis/codebin_model_runtime_signatures.csv",
    "analysis/codebin_model_runtime_symbols.csv",
    "analysis/codebin_cmb_material_lane_signatures.csv",
    "analysis/codebin_cmb_material_lane_symbols.csv",
    "analysis/codebin_cmb_runtime_lighting_signatures.csv",
    "analysis/codebin_cmb_raster_material_signatures.csv",
    "analysis/codebin_cmb_tev_override_signatures.csv",
    "analysis/codebin_scene_room_lifecycle_signatures.csv",
    "analysis/codebin_scene_room_lifecycle_struct_fields.csv",
    "analysis/codebin_scene_room_lifecycle_symbols.csv",
    "analysis/codebin_room_runtime_resources_signatures.csv",
    "analysis/codebin_room_runtime_resources_struct_fields.csv",
    "analysis/codebin_room_runtime_resources_symbols.csv",
    "analysis/codebin_room_scene_callback_entries.csv",
    "analysis/codebin_room_scene_callback_data_symbols.csv",
    "analysis/codebin_room_scene_callback_signatures.csv",
    "analysis/codebin_room_scene_callback_struct_fields.csv",
    "analysis/codebin_room_scene_callback_symbols.csv",
    "analysis/codebin_room_scene_callback_table.csv",
    "analysis/codebin_room_scene_semantics.csv",
    "analysis/codebin_room_scene_semantics_signatures.csv",
    "analysis/codebin_room_scene_semantics_struct_fields.csv",
    "analysis/codebin_room_scene_semantics_symbols.csv",
    "analysis/codebin_room_pose_hooks.csv",
    "analysis/codebin_room_pose_hook_data_symbols.csv",
    "analysis/codebin_room_pose_hook_signatures.csv",
    "analysis/codebin_room_pose_hook_struct_fields.csv",
    "analysis/codebin_room_pose_hook_symbols.csv",
    "analysis/codebin_camera_mode_dispatch.csv",
    "analysis/codebin_camera_workflow.csv",
    "analysis/codebin_camera_workflow_data_symbols.csv",
    "analysis/codebin_camera_workflow_signatures.csv",
    "analysis/codebin_camera_workflow_struct_fields.csv",
    "analysis/codebin_camera_workflow_symbols.csv",
    "analysis/codebin_onepoint_cutscene_cases.csv",
    "analysis/codebin_onepoint_cutscene_workflow.csv",
    "analysis/codebin_onepoint_cutscene_workflow_data_symbols.csv",
    "analysis/codebin_onepoint_cutscene_workflow_signatures.csv",
    "analysis/codebin_onepoint_cutscene_workflow_struct_fields.csv",
    "analysis/codebin_onepoint_cutscene_workflow_symbols.csv",
    "analysis/codebin_collision_at_ac_dispatch.csv",
    "analysis/codebin_collision_at_ac_workflow.csv",
    "analysis/codebin_collision_at_ac_workflow_data_symbols.csv",
    "analysis/codebin_collision_at_ac_workflow_signatures.csv",
    "analysis/codebin_collision_at_ac_workflow_struct_fields.csv",
    "analysis/codebin_collision_at_ac_workflow_symbols.csv",
    "analysis/codebin_collision_oc_damage_dispatch.csv",
    "analysis/codebin_collision_oc_damage_data_symbols.csv",
    "analysis/codebin_collision_oc_damage_field_promotions.csv",
    "analysis/codebin_collision_oc_damage_signatures.csv",
    "analysis/codebin_collision_oc_damage_struct_fields.csv",
    "analysis/codebin_collision_oc_damage_symbols.csv",
    "analysis/codebin_collision_oc_damage_workflow.csv",
    "analysis/codebin_ready_large_band_tranche_144.csv",
    "analysis/codebin_ready_large_band_tranche_144_signatures.csv",
    "analysis/codebin_ready_large_band_tranche_144_symbols.csv",
    "analysis/codebin_runtime_divzero_thunk_closure_tranche_164.csv",
    "analysis/codebin_runtime_divzero_thunk_closure_tranche_164_signatures.csv",
    "analysis/codebin_runtime_divzero_thunk_closure_tranche_164_symbols.csv",
    "analysis/codebin_variadic_format_wrapper_closure_tranche_166.csv",
    "analysis/codebin_variadic_format_wrapper_closure_tranche_166_signatures.csv",
    "analysis/codebin_variadic_format_wrapper_closure_tranche_166_symbols.csv",
    "analysis/codebin_callable_boundary_residue_audit_166.csv",
    "analysis/codebin_decompilation_progress.csv",
    "analysis/codebin_decompilation_progress.md",
    "analysis/codebin_struct_fields.csv",
    "analysis/codebin_decompilation_batch_026.md",
    "analysis/codebin_decompilation_batch_027.md",
    "analysis/codebin_decompilation_batch_028.md",
    "analysis/codebin_decompilation_batch_029.md",
    "analysis/codebin_decompilation_batch_030.md",
    "analysis/codebin_decompilation_batch_056.md",
    "build/analysis/decomp_batches/actor_core_semantic_typed.md",
    "build/analysis/decomp_batches/actor_embedded_helper_core_typed.md",
    "build/analysis/decomp_batches/camera_workflow_semantic_typed.md",
    "build/analysis/decomp_batches/collision_at_ac_semantic_typed.md",
    "build/analysis/decomp_batches/collision_oc_damage_semantic_typed.md",
    "build/analysis/decomp_batches/limb_callback_helpers_semantic_typed.md",
    "build/analysis/decomp_batches/object_bank_async_typed.md",
    "build/analysis/decomp_batches/onepoint_cutscene_workflow_semantic_typed.md",
    "build/analysis/decomp_batches/room_pose_hooks_typed.md",
    "build/analysis/decomp_batches/room_runtime_resources_typed.md",
    "build/analysis/decomp_batches/room_scene_callbacks_typed.md",
    "build/analysis/decomp_batches/room_scene_semantics_typed.md",
    "build/analysis/decomp_batches/scene_room_lifecycle_typed.md",
    "build/analysis/decomp_batches/skelanime_draw_pipeline_semantic_typed.md",
)

OPTIONAL_GLOBS = (
    # Keep the executable A32 backend and its deterministic generator beside
    # the semantic catalogs. These are optional for historical revisions, but
    # once present they must come from the exact requested Git tree.
    "recomp/a32_*.cpp",
    "recomp/a32_*.h",
    "src/oot3d_pack/a32_cpp_aot.py",
    "src/oot3d_pack/arm_decode.py",
    "src/oot3d_pack/arm_ir.py",
    "src/oot3d_pack/arm_lift.py",
    "src/oot3d_pack/arm_softfloat.py",
    "src/oot3d_pack/arm_softfloat64.py",
    # Keep the complete, typed native UI substitution boundary together.  These
    # sources are generated from reviewed code.bin bodies and are intended to be
    # consumed as one contract; selecting individual tranches would silently
    # omit shared value domains, lifecycle descriptors, or retained native
    # action/query seams as Zelda3drecomp extends the boundary.
    "oot3d_ui/*.cpp",
    "oot3d_ui/*.h",
    "docs/oot3d_ui_substitution_contract.md",
    "analysis/codebin_ui_*.csv",
    "analysis/codebin_gameplay_hud_*.csv",
    "analysis/codebin_pause_*_tranche_[0-9][0-9][0-9].csv",
    # Root tranche rows are compact, machine-readable workflow closure evidence.
    # Suffix catalogs remain owned by Zelda3drecomp and are imported separately
    # only when a consumer needs their additional detail.
    "analysis/codebin_actor_workflow_tranche_[0-9][0-9][0-9].csv",
    # The base tranche rows identify functions, but a room consumer also needs
    # the native action graph and bounded actor layout. These suffix catalogs
    # are small and remain mechanically scoped to the same reviewed tranches.
    "analysis/codebin_actor_workflow_tranche_[0-9][0-9][0-9]_action_targets.csv",
    "analysis/codebin_actor_workflow_tranche_[0-9][0-9][0-9]_signatures.csv",
    "analysis/codebin_actor_workflow_tranche_[0-9][0-9][0-9]_struct_fields.csv",
    # Generic semantic tranches include newly promoted scene-adjacent helpers
    # (map initial state, background-camera data and environment paths). Keeping
    # these globbed makes a read-only realignment pick up later closures without
    # teaching this workspace Zelda3drecomp's batch numbering.
    "analysis/codebin_semantic_tranche_[0-9][0-9][0-9].csv",
    "analysis/codebin_decompilation_batch_[0-9][0-9][0-9].md",
    # Zero-direct-caller roots are native action/state candidates. Import the
    # reviewed catalogs themselves, not only their generic semantic overlay, so
    # consumers can retain tranche identity and the independently reviewed ABI.
    "analysis/codebin_indirect_ready_*_roots_tranche_[0-9][0-9][0-9].csv",
    "analysis/codebin_indirect_ready_*_roots_tranche_[0-9][0-9][0-9]_signatures.csv",
    "analysis/codebin_indirect_ready_*_roots_tranche_[0-9][0-9][0-9]_symbols.csv",
    # Parent-owned closures and maintained-name ABI closures are reviewed public
    # function boundaries. Keep their catalog triplets so one shared runtime ABI
    # registry can consume them without duplicating the corpus in every room.
    "analysis/codebin_parent_owned_*_tranche_[0-9][0-9][0-9].csv",
    "analysis/codebin_parent_owned_*_tranche_[0-9][0-9][0-9]_signatures.csv",
    "analysis/codebin_parent_owned_*_tranche_[0-9][0-9][0-9]_symbols.csv",
    "analysis/codebin_maintained_abi_*closure_tranche_[0-9][0-9][0-9].csv",
    "analysis/codebin_maintained_abi_*closure_tranche_[0-9][0-9][0-9]_signatures.csv",
    "analysis/codebin_maintained_abi_*closure_tranche_[0-9][0-9][0-9]_symbols.csv",
    "analysis/codebin_maintained_abi_*_tranche_[0-9][0-9][0-9]_exclusions.csv",
    # Later Actor closure/layout tranches use a common reviewed artifact shape.
    # Importing this family keeps RCU alignment independent of batch numbering
    # while still excluding generators and mutable build products.
    "analysis/codebin_actor_*_tranche_[0-9][0-9][0-9].csv",
    "analysis/codebin_actor_*_tranche_[0-9][0-9][0-9]_signatures.csv",
    "analysis/codebin_actor_*_tranche_[0-9][0-9][0-9]_symbols.csv",
    "analysis/codebin_actor_*_tranche_[0-9][0-9][0-9]_fields.csv",
    "analysis/codebin_actor_*_tranche_[0-9][0-9][0-9].md",
    "analysis/codebin_actor_structural_coverage_audit_[0-9][0-9][0-9].csv",
    "analysis/codebin_actor_structural_coverage_audit_[0-9][0-9][0-9].md",
    "analysis/codebin_callable_identity_closure_tranche_[0-9][0-9][0-9].csv",
    "analysis/codebin_callable_identity_closure_tranche_[0-9][0-9][0-9]_signatures.csv",
    "analysis/codebin_callable_identity_closure_tranche_[0-9][0-9][0-9]_symbols.csv",
)

GENERATED_ARTIFACT_PREFIX = "build/analysis/decomp_batches/"
GENERATED_OPTIONAL_GLOBS = (
    "build/analysis/decomp_batches/maintained_abi_*closure_tranche_[0-9][0-9][0-9]_typed.md",
)
MAINTAINED_TRANCHE_RE = re.compile(
    r"(?:^|/)maintained_abi_.+closure_tranche_(\d{3})_typed\.md$"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(source: Path, *args: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(source), *args], text=True, encoding="utf-8"
    ).strip()


def git_bytes(source: Path, *args: str) -> bytes:
    return subprocess.check_output(["git", "-C", str(source), *args])


def normalize_entry(value: str) -> str:
    return value.lower().removeprefix("0x").zfill(8)


def read_operational_paths(repo_root: Path) -> tuple[Path, Path]:
    config_path = repo_root / "tools/oot3d/operational_inputs.json"
    config = json.loads(config_path.read_text(encoding="utf-8"))
    variables = {key: str(value) for key, value in config["variables"].items()}
    variables["repoRoot"] = str(repo_root)

    def expand(value: str) -> str:
        for _ in range(len(variables) + 1):
            previous = value
            for key, replacement in variables.items():
                value = value.replace("${" + key + "}", replacement)
            if value == previous:
                return value
        return value

    source = Path(expand(variables["zelda3dRecompRoot"]))
    code_bin = next(
        Path(expand(entry["path"]))
        for entry in config["entries"]
        if entry["id"] == "oot3d_code_bin"
    )
    return source, code_bin


def revision_files(source: Path, revision: str) -> set[str]:
    listing = git(source, "ls-tree", "-r", "--name-only", revision)
    return set(listing.splitlines()) if listing else set()


def selected_files(
    source: Path,
    revision_tree: set[str] | None = None,
    *,
    include_generated: bool = False,
) -> tuple[str, ...]:
    paths = {
        path
        for path in SELECTED_FILES
        if not path.startswith(GENERATED_ARTIFACT_PREFIX)
    }
    for relative in (
        path
        for path in SELECTED_FILES
        if path.startswith(GENERATED_ARTIFACT_PREFIX)
    ):
        if (
            (revision_tree is not None and relative in revision_tree)
            or (include_generated and (source / relative).is_file())
            or (revision_tree is None and (source / relative).is_file())
        ):
            paths.add(relative)
    for pattern in OPTIONAL_GLOBS:
        if revision_tree is None:
            paths.update(
                path.relative_to(source).as_posix() for path in source.glob(pattern)
            )
        else:
            paths.update(
                path for path in revision_tree if fnmatch.fnmatchcase(path, pattern)
            )
    if include_generated and revision_tree is not None:
        committed_tranches = {
            match.group(1)
            for path in revision_tree
            if (
                match := re.fullmatch(
                    r"analysis/codebin_maintained_abi_.+closure_tranche_(\d{3})\.csv",
                    path,
                )
            )
        }
        for pattern in GENERATED_OPTIONAL_GLOBS:
            for path in source.glob(pattern):
                relative = path.relative_to(source).as_posix()
                match = MAINTAINED_TRANCHE_RE.search(relative)
                if match is not None and match.group(1) in committed_tranches:
                    paths.add(relative)
    return tuple(sorted(paths))


def build_symbol_overlay(
    repo_root: Path, source: Path, selected: tuple[str, ...]
) -> dict[str, object]:
    maintained: dict[str, str] = {}
    with (repo_root / "tools/oot3d/decomp_support/symbols/manual_symbols.csv").open(
        newline="", encoding="utf-8"
    ) as input_file:
        for row in csv.DictReader(input_file):
            maintained[normalize_entry(row["entry"])] = row["new_name"]

    entries: list[dict[str, str]] = []
    for relative in selected:
        if not relative.endswith("_symbols.csv"):
            continue
        with (source / relative).open(newline="", encoding="utf-8") as input_file:
            reader = csv.DictReader(input_file)
            if not reader.fieldnames or not {"entry", "new_name"}.issubset(
                reader.fieldnames
            ):
                continue
            for row in reader:
                address = normalize_entry(row["entry"])
                existing = maintained.get(address)
                status = "new"
                if existing == row["new_name"]:
                    status = "same_name"
                elif existing is not None:
                    status = "conflict"
                entries.append(
                    {
                        "entry": address,
                        "name": row["new_name"],
                        "existing_name": existing or "",
                        "status": status,
                        "confidence": row.get("confidence", ""),
                        "source_catalog": relative,
                        "source_evidence": row.get("source_file", ""),
                        "notes": row.get("notes", ""),
                    }
                )
    entries.sort(key=lambda row: (row["entry"], row["name"]))
    return {
        "format": "oot3d_external_semantic_overlay_v1",
        "entry_count": len(entries),
        "new_count": sum(row["status"] == "new" for row in entries),
        "same_name_count": sum(row["status"] == "same_name" for row in entries),
        "conflict_count": sum(row["status"] == "conflict" for row in entries),
        "entries": entries,
    }


def verify_existing_snapshot(
    output: Path,
    file_rows: list[dict[str, object]],
    generated_files: dict[str, str],
) -> None:
    expected_paths = {str(row["path"]) for row in file_rows} | set(generated_files)
    actual_paths = {
        path.relative_to(output).as_posix()
        for path in output.rglob("*")
        if path.is_file()
    }
    if actual_paths != expected_paths:
        raise SystemExit(
            f"immutable snapshot file set mismatch: {output} "
            f"expected={len(expected_paths)} actual={len(actual_paths)}"
        )
    for row in file_rows:
        destination = output / str(row["path"])
        if sha256(destination) != row["sha256"]:
            raise SystemExit(f"immutable snapshot hash mismatch: {destination}")
    for relative, content in generated_files.items():
        destination = output / relative
        if destination.read_text(encoding="utf-8") != content:
            raise SystemExit(f"immutable snapshot metadata mismatch: {destination}")


def main() -> None:
    repo_root = Path(__file__).resolve().parents[4]
    default_source, canonical_code = read_operational_paths(repo_root)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=default_source)
    parser.add_argument(
        "--revision",
        default="HEAD",
        help=(
            "Import tracked evidence exactly from this commit instead of the live "
            "worktree. Generated Ghidra artifacts explicitly listed by the importer "
            "may be read from the checkout only when it is at the requested commit "
            "(default: HEAD)."
        ),
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=repo_root / "tools/oot3d/decomp_support/evidence/zelda3drecomp",
    )
    parser.add_argument(
        "--tracked-only",
        action="store_true",
        help=(
            "Exclude generated checkout artifacts even when the checkout is at the "
            "requested revision; only git-revision bytes enter the snapshot."
        ),
    )
    args = parser.parse_args()

    source = args.source.resolve()
    source_code = source / "build/oot3d-native/extracted/exefs/code.bin"
    canonical_hash = sha256(canonical_code)
    source_hash = sha256(source_code)
    if canonical_hash != EXPECTED_CODE_SHA256 or source_hash != EXPECTED_CODE_SHA256:
        raise SystemExit(
            "code.bin identity mismatch: "
            f"canonical={canonical_hash} external={source_hash} expected={EXPECTED_CODE_SHA256}"
        )

    checkout_revision = git(source, "rev-parse", "HEAD")
    source_revision = (
        git(source, "rev-parse", f"{args.revision}^{{commit}}")
        if args.revision
        else checkout_revision
    )
    tree_files = revision_files(source, source_revision) if args.revision else None
    selected = selected_files(
        source,
        tree_files,
        include_generated=(
            not args.tracked_only
            and tree_files is not None
            and checkout_revision == source_revision
        ),
    )
    source_branch = git(source, "branch", "--show-current")
    status_lines = git(
        source, "status", "--porcelain=v1", "--untracked-files=all"
    ).splitlines()
    source_status_sha256 = hashlib.sha256(
        "\n".join(status_lines).encode("utf-8")
    ).hexdigest()
    manual_symbols_path = (
        repo_root / "tools/oot3d/decomp_support/symbols/manual_symbols.csv"
    )
    manual_symbols_sha256 = sha256(manual_symbols_path)
    args.output_root.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".tmp-zelda3drecomp-", dir=args.output_root))
    try:
        generated_artifacts: list[str] = []
        for relative in selected:
            source_path = source / relative
            destination = staging / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            if tree_files is not None and relative in tree_files:
                destination.write_bytes(
                    git_bytes(source, "show", f"{source_revision}:{relative}")
                )
            elif tree_files is not None:
                if (
                    not relative.startswith(GENERATED_ARTIFACT_PREFIX)
                    or checkout_revision != source_revision
                    or not source_path.is_file()
                ):
                    raise SystemExit(
                        "required evidence is absent from the requested revision: "
                        f"{relative}"
                    )
                shutil.copyfile(source_path, destination)
                generated_artifacts.append(relative)
            else:
                if not source_path.is_file():
                    raise SystemExit(f"required evidence is missing: {source_path}")
                shutil.copyfile(source_path, destination)

        if tree_files is None:
            final_checkout_revision = git(source, "rev-parse", "HEAD")
            final_status_lines = git(
                source, "status", "--porcelain=v1", "--untracked-files=all"
            ).splitlines()
            if (
                final_checkout_revision != checkout_revision
                or final_status_lines != status_lines
            ):
                raise SystemExit(
                    "Zelda3drecomp changed while the live evidence snapshot was copied"
                )
            for relative in selected:
                source_path = source / relative
                if not source_path.is_file() or sha256(source_path) != sha256(
                    staging / relative
                ):
                    raise SystemExit(
                        "Zelda3drecomp evidence changed while being copied: "
                        f"{relative}"
                    )

        file_rows: list[dict[str, object]] = []
        aggregate = hashlib.sha256()
        selection_version = 16
        aggregate.update(
            f"oot3d_zelda3drecomp_evidence_snapshot_selection_v{selection_version}\0".encode(
                "ascii"
            )
        )
        aggregate.update(source_revision.encode("ascii"))
        if tree_files is None:
            aggregate.update(source_branch.encode("utf-8"))
            aggregate.update(bytes.fromhex(source_status_sha256))
        aggregate.update(bytes.fromhex(manual_symbols_sha256))
        for relative in selected:
            copied_path = staging / relative
            file_hash = sha256(copied_path)
            aggregate.update(relative.encode("utf-8"))
            aggregate.update(bytes.fromhex(file_hash))
            file_rows.append(
                {
                    "path": relative,
                    "size": copied_path.stat().st_size,
                    "sha256": file_hash,
                    "source_kind": (
                        "generated_worktree_artifact"
                        if relative in generated_artifacts
                        else (
                            "git_revision"
                            if tree_files is not None
                            else "live_worktree"
                        )
                    ),
                }
            )

        snapshot_id = aggregate.hexdigest()[:16]
        output = args.output_root / snapshot_id
        overlay = build_symbol_overlay(repo_root, staging, selected)
        native_abi_catalog = build_native_abi_catalog(
            staging,
            selected,
            snapshot_id=snapshot_id,
            source_base_revision=source_revision,
            code_bin_sha256=canonical_hash,
        )
        overlay_text = json.dumps(overlay, indent=2, sort_keys=True) + "\n"
        native_abi_text = (
            json.dumps(native_abi_catalog, indent=2, sort_keys=True) + "\n"
        )
        derived_artifacts = []
        for relative, content in (
            ("semantic_overlay.json", overlay_text),
            ("native_abi_catalog.json", native_abi_text),
        ):
            encoded = content.encode("utf-8")
            derived_artifacts.append(
                {
                    "path": relative,
                    "size": len(encoded),
                    "sha256": hashlib.sha256(encoded).hexdigest(),
                }
            )

        manifest = {
            "format": "oot3d_zelda3drecomp_evidence_snapshot_v1",
            "selection_version": selection_version,
            "snapshot_id": snapshot_id,
            "source_base_revision": source_revision,
            "source_branch": source_branch,
            "source_selection_mode": (
                "git_revision_with_generated_artifacts"
                if tree_files is not None and generated_artifacts
                else ("git_revision" if tree_files is not None else "live_worktree")
            ),
            "source_worktree_dirty": (
                False if tree_files is not None else bool(status_lines)
            ),
            "source_worktree_change_count": (
                0 if tree_files is not None else len(status_lines)
            ),
            "source_status_sha256": (
                hashlib.sha256(b"").hexdigest()
                if tree_files is not None
                else source_status_sha256
            ),
            "generated_artifact_count": len(generated_artifacts),
            "generated_artifacts": generated_artifacts,
            "code_bin_sha256": canonical_hash,
            "manual_symbols_sha256": manual_symbols_sha256,
            "file_count": len(file_rows),
            "files": file_rows,
            "derived_artifact_count": len(derived_artifacts),
            "derived_artifacts": derived_artifacts,
        }
        generated_files = {
            ".gitignore": (
                "# Selected files under build/ are immutable source evidence.\n"
                "!build/\n"
                "!build/**\n"
            ),
            "manifest.json": json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            "semantic_overlay.json": overlay_text,
            "native_abi_catalog.json": native_abi_text,
        }
        for relative, content in generated_files.items():
            (staging / relative).write_text(content, encoding="utf-8", newline="\n")

        if output.exists():
            verify_existing_snapshot(output, file_rows, generated_files)
        else:
            try:
                staging.rename(output)
            except PermissionError:
                # Some Windows filter drivers deny renaming a freshly populated
                # directory even on the same volume. Copying then verifying keeps
                # the immutable publish contract without making imports flaky.
                shutil.copytree(staging, output)
                verify_existing_snapshot(output, file_rows, generated_files)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    print(
        f"snapshot={snapshot_id} files={len(file_rows)} symbols={overlay['entry_count']} "
        f"new={overlay['new_count']} conflicts={overlay['conflict_count']} output={output}"
    )


if __name__ == "__main__":
    main()
