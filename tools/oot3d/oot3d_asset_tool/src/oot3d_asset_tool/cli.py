from __future__ import annotations

import argparse
import json
import re
import zipfile
from pathlib import Path
from typing import Iterator

from .actor_inventory import inventory_actors
from .actor_ccb_audit import audit_actor_ccb_payloads
from .actor_material_lighting_audit import audit_actor_material_lighting
from .actor_qdb_audit import audit_actor_qdb_payloads
from .actor_zsi_audit import audit_actor_zsi_payloads
from .audio_asset_audit import audit_audio_assets
from .animation_glb_export import AnimationGlbOptions, export_skinned_animation_glb
from .animation_like_audit import audit_actor_animation_like_payloads
from .anb_export import export_anb_payload_batch
from .anb_semantic_audit import audit_anb_semantic_candidates
from .asset_replacement_ledger import audit_asset_replacement_ledger
from .binary import ParseError
from .cmb import CmbModel
from .collision_gate_audit import (
    audit_zsi_collision_gates,
    export_zsi_collision_activation_manifest,
)
from .collision import (
    COLLISION_BUDGET_POLICIES,
    COLLISION_RESOURCE_FORMATS,
    DEFAULT_REFERENCE_FLOOR_PROBE_COUNT,
    DEFAULT_REFERENCE_FLOOR_QUERY_ABOVE,
    DEFAULT_REFERENCE_FLOOR_TOLERANCE,
    DEFAULT_VISUAL_MESH_FLOOR_PROBE_COUNT,
    CollisionMetadata,
    discover_scene_collision_path_from_o2r,
    export_scene_collision,
    export_zsi_scene_collision,
    load_collision_metadata_from_o2r,
)
from .ctxb import audit_ctxb_textures, parse_ctxb
from .ctxb_export import export_ctxb_textures
from .extraction_classification import audit_extraction_classification
from .csab_track_package import (
    audit_csab_track_batch_package,
    pack_csab_track_batch_manifest,
)
from .csab_tracks import (
    batch_export_csab_rigid_tracks,
    batch_export_csab_skeleton_tracks,
    export_csab_rigid_tracks,
    export_csab_skeleton_tracks,
)
from .csab_target_audit import audit_csab_target_resolution
from .cmab_audit import audit_cmab_payloads
from .character_conversion_manifest import export_character_conversion_manifest
from .character_conversion_package import (
    audit_character_conversion_package,
    pack_character_conversion_manifest,
)
from .character_segment_continuity import audit_character_segment_continuity
from .character_visibility_reconstruction import audit_character_visibility_reconstruction
from .kankyo_environment_audit import audit_kankyo_environment_assets
from .kankyo_environment_export import export_kankyo_environment_assets
from .kankyo_environment_package import (
    audit_kankyo_environment_export_package,
    pack_kankyo_environment_export_manifest,
)
from .link_child_animation_semantic_mapping import (
    audit_link_child_animation_semantic_mapping,
    audit_link_child_pose_source_callsite_context,
    build_link_child_anonymous_numeric_diagnostic_tracks,
    build_link_child_pose_source_risk_diagnostic_tracks,
    build_link_child_route_runtime_diagnostic_tracks,
)
from .kokiri_actor_asset_readiness import audit_kokiri_actor_asset_readiness
from .kokiri_audio_binding_plan import export_kokiri_audio_binding_plan
from .kokiri_kankyo_binding_plan import export_kokiri_kankyo_binding_plan
from .kokiri_runtime_route_audit import audit_kokiri_runtime_route
from .kokiri_runtime_manifest import export_kokiri_runtime_manifest
from .kokiri_static_actor_binding_plan import export_kokiri_static_actor_binding_plan
from .material_audit import audit_scene_materials
from .material_stage_inventory import inventory_material_stages
from .media_asset_audit import audit_media_assets
from .minimal_runtime_player_export import (
    MinimalRuntimePlayerExportOptions,
    MinimalRuntimeTrackExportOptions,
    audit_minimal_runtime_player_export,
    audit_minimal_runtime_track_export,
)
from .moflex_movie_audit import audit_moflex_movies
from .n64_animation_reference_audit import audit_n64_animation_reference
from .native_corpus_promotion import promote_native_corpus
from .package_audit import audit_scene_package
from .player_animation_group_native_contract import (
    build_player_animation_group_native_contract,
)
from .player_collision_action_native_contract import (
    build_player_collision_action_native_contract,
)
from .prerendered_room_audit import audit_prerendered_room_replacements
from .prerendered_room_export import (
    audit_prerendered_room_export_package,
    export_prerendered_room_replacements,
    pack_prerendered_room_export_manifest,
)
from .q_format_audit import audit_q_format_assets
from .romfs_inventory import inventory_romfs
from .room_compilation_unit import (
    compile_room_compilation_unit,
    validate_room_compilation_unit,
    write_room_compilation_markdown,
    write_room_compilation_unit,
)
from .runtime_draw_dump_audit import audit_runtime_draw_dump
from .legacy_fast_resource import LegacyFastResourceOptions, export_static_model
from .skinned_animation import (
    batch_export_skinned_animation_pose_samples,
    export_skinned_animation_pose_samples,
)
from .skinned_animation_readiness_audit import (
    audit_skinned_animation_readiness,
    export_skinned_animation_binding_manifest,
)
from .skinned_bind_pose_package import (
    audit_skinned_bind_pose_batch_package,
    pack_skinned_bind_pose_batch_manifest,
)
from .skinned_bind_pose_native import export_skinned_bind_pose_native
from .skinned_bind_pose_native_audit import audit_skinned_bind_pose_native_contract
from .skinned_export import batch_export_actor_skinned_bind_poses, export_skinned_bind_pose
from .skinning_audit import audit_actor_skinning_queue
from .skinning_layout_audit import audit_actor_skinning_vertex_layout
from .static_batch_audit import audit_static_batch
from .static_batch_package import audit_static_batch_package, pack_static_batch_manifest
from .static_glb_export import (
    DEFAULT_GLB_TEXTURE_ORIENTATION,
    DEFAULT_POSITION_SCALE,
    GlbOptions,
    export_static_base_glb,
)
from .zar import ZarArchive
from .zsi import ZsiFile
from .zsi_cutscene_audit import (
    export_zsi_cutscene_camera_data,
    audit_zsi_cutscene_metadata,
)
from .zsi_scene_audit import audit_zsi_scene_metadata, zsi_scene_stem
from .zsi_scene_index import export_zsi_scene_index


DETERMINISTIC_ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="oot3d-assets",
        description="Inspect, audit, and convert native OOT3D assets.",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    inspect_parser = subparsers.add_parser("inspect", help="Inspect a CMB model, ZAR archive, or ZSI file.")
    inspect_parser.add_argument("input", type=Path)

    player_animation_group_parser = subparsers.add_parser(
        "export-player-animation-group-native-contract",
        help="Decode the native Player animation-group table from code.bin and its actor ZAR.",
    )
    player_animation_group_parser.add_argument("code_bin", type=Path)
    player_animation_group_parser.add_argument("actor_zar", type=Path)
    player_animation_group_parser.add_argument("--output", type=Path, required=True)

    player_collision_action_parser = subparsers.add_parser(
        "export-player-collision-action-native-contract",
        help="Decode native Player collision/action parameters from code.bin.",
    )
    player_collision_action_parser.add_argument("code_bin", type=Path)
    player_collision_action_parser.add_argument("--output", type=Path, required=True)

    inventory_parser = subparsers.add_parser(
        "inventory-romfs",
        help="Write a JSON inventory of an extracted OOT3D RomFS and classify current conversion coverage.",
    )
    inventory_parser.add_argument("romfs_root", type=Path)
    inventory_parser.add_argument("--output", type=Path, required=True)
    inventory_parser.add_argument(
        "--shallow",
        action="store_true",
        help="Count files only; skip parsing CMB/ZAR/ZSI containers.",
    )
    inventory_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write only aggregate counts and parse errors, omitting per-file and per-model records.",
    )

    extraction_classification_parser = subparsers.add_parser(
        "audit-extraction-classification",
        help="Classify extracted OOT3D files as RomFS data to interpret/convert or ExeFS code to decompile.",
    )
    extraction_classification_parser.add_argument("extraction_root", type=Path)
    extraction_classification_parser.add_argument("--output", type=Path, required=True)
    extraction_classification_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write only aggregate RomFS counts, omitting per-file RomFS records.",
    )

    replacement_ledger_parser = subparsers.add_parser(
        "audit-asset-replacement-ledger",
        help="Aggregate generated OOT3D gates into a conservative replacement-readiness ledger.",
    )
    replacement_ledger_parser.add_argument("work_root", type=Path)
    replacement_ledger_parser.add_argument("--output", type=Path, required=True)
    replacement_ledger_parser.add_argument(
        "--decomp-support-root",
        type=Path,
        default=None,
        help="Optional OOT3D decomp support root used to include ExeFS code decompilation frontier evidence.",
    )
    replacement_ledger_parser.add_argument(
        "--code-bin",
        type=Path,
        default=None,
        help="Optional extracted ExeFS code.bin whose hash must match decomp-support metadata.",
    )

    material_stage_inventory_parser = subparsers.add_parser(
        "inventory-material-stages",
        help="Write a RomFS-wide audit of CMB material texture-stage selectors and export gaps.",
    )
    material_stage_inventory_parser.add_argument("romfs_root", type=Path)
    material_stage_inventory_parser.add_argument("--output", type=Path, required=True)
    material_stage_inventory_parser.add_argument(
        "--sample-limit",
        type=int,
        default=200,
        help="Maximum number of risky material samples to include in the JSON output.",
    )
    material_stage_inventory_parser.add_argument(
        "--model-limit",
        type=int,
        default=None,
        help="Optional cap on parsed CMB models for bounded exploratory runs.",
    )
    material_stage_inventory_parser.add_argument(
        "--no-model-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-model summaries.",
    )

    media_asset_audit_parser = subparsers.add_parser(
        "audit-media-assets",
        help="Write a metadata-only audit of OOT3D Moflex, audio, UI layout, font, and CTXB support assets.",
    )
    media_asset_audit_parser.add_argument(
        "romfs_root",
        type=Path,
        help="Extracted OOT3D RomFS root.",
    )
    media_asset_audit_parser.add_argument("--output", type=Path, required=True)
    media_asset_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    media_asset_audit_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-file records.",
    )

    audio_asset_audit_parser = subparsers.add_parser(
        "audit-audio-assets",
        help="Write a focused metadata audit of OOT3D BCSAR/BCSTM audio containers.",
    )
    audio_asset_audit_parser.add_argument(
        "romfs_root",
        type=Path,
        help="Extracted OOT3D RomFS root.",
    )
    audio_asset_audit_parser.add_argument("--output", type=Path, required=True)
    audio_asset_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    audio_asset_audit_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-file records.",
    )

    moflex_movie_audit_parser = subparsers.add_parser(
        "audit-moflex-movies",
        help="Write a focused metadata audit of OOT3D Moflex movie containers.",
    )
    moflex_movie_audit_parser.add_argument(
        "romfs_root",
        type=Path,
        help="Extracted OOT3D RomFS root.",
    )
    moflex_movie_audit_parser.add_argument("--output", type=Path, required=True)
    moflex_movie_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    moflex_movie_audit_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-file records.",
    )

    q_format_audit_parser = subparsers.add_parser(
        "audit-q-format-assets",
        help="Write a focused metadata audit of OOT3D Q-format UI layout/font/support assets.",
    )
    q_format_audit_parser.add_argument(
        "romfs_root",
        type=Path,
        help="Extracted OOT3D RomFS root.",
    )
    q_format_audit_parser.add_argument("--output", type=Path, required=True)
    q_format_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    q_format_audit_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-file records.",
    )

    ctxb_audit_parser = subparsers.add_parser(
        "audit-ctxb-textures",
        help="Parse and decode OOT3D CTXB texture containers without writing converted texture assets.",
    )
    ctxb_audit_parser.add_argument(
        "romfs_root",
        type=Path,
        help="Extracted OOT3D RomFS root.",
    )
    ctxb_audit_parser.add_argument("--output", type=Path, required=True)
    ctxb_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    ctxb_audit_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-texture records.",
    )

    ctxb_export_parser = subparsers.add_parser(
        "export-ctxb-textures",
        help="Export decoded OOT3D CTXB texture containers as Shipwright Texture resources outside the repo.",
    )
    ctxb_export_parser.add_argument(
        "romfs_root",
        type=Path,
        help="Extracted OOT3D RomFS root.",
    )
    ctxb_export_parser.add_argument("--output", type=Path, required=True)
    ctxb_export_parser.add_argument(
        "--resource-prefix",
        default="textures/oot3d/ctxb",
        help="Generated Texture resource root prefix.",
    )
    ctxb_export_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    ctxb_export_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-texture records.",
    )

    kankyo_audit_parser = subparsers.add_parser(
        "audit-kankyo-environment-assets",
        help="Write a metadata-only audit of OOT3D kankyo sky/environment ZAR assets.",
    )
    kankyo_audit_parser.add_argument(
        "kankyo_root",
        type=Path,
        help="Extracted OOT3D RomFS kankyo directory.",
    )
    kankyo_audit_parser.add_argument("--output", type=Path, required=True)
    kankyo_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of archive sample records to include.",
    )
    kankyo_audit_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-archive records.",
    )

    kankyo_export_parser = subparsers.add_parser(
        "export-kankyo-environment-assets",
        help="Export OOT3D kankyo static sky/environment CMB assets and audit generated resources.",
    )
    kankyo_export_parser.add_argument(
        "kankyo_root",
        type=Path,
        help="Extracted OOT3D RomFS kankyo directory.",
    )
    kankyo_export_parser.add_argument("--output", type=Path, required=True)
    kankyo_export_parser.add_argument(
        "--resource-prefix",
        default="environments/oot3d/kankyo",
        help="Generated resource root prefix.",
    )
    kankyo_export_parser.add_argument("--no-textures", action="store_true")
    kankyo_export_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum source-audit sample records to include.",
    )

    kankyo_pack_parser = subparsers.add_parser(
        "pack-kankyo-environment-export",
        help="Pack a kankyo environment export manifest into a Shipwright .o2r mod archive.",
    )
    kankyo_pack_parser.add_argument("export_manifest", type=Path)
    kankyo_pack_parser.add_argument("--output", type=Path, required=True, help="Output .o2r path.")
    kankyo_pack_parser.add_argument("--name", default="OOT3D Kankyo Environment Candidates")
    kankyo_pack_parser.add_argument("--author", default="local")
    kankyo_pack_parser.add_argument("--version", default="0.1.0")

    kankyo_package_audit_parser = subparsers.add_parser(
        "audit-kankyo-environment-export-package",
        help="Verify a packed O2R archive generated from a kankyo environment export manifest.",
    )
    kankyo_package_audit_parser.add_argument("export_manifest", type=Path)
    kankyo_package_audit_parser.add_argument("archive", type=Path)
    kankyo_package_audit_parser.add_argument("--output", type=Path, required=True)
    kankyo_package_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum sample records to include for package issues.",
    )

    kokiri_runtime_route_parser = subparsers.add_parser(
        "audit-kokiri-runtime-route",
        help="Inventory the Link house to Kokiri Forest OOT3D runtime route before runtime routing work.",
    )
    kokiri_runtime_route_parser.add_argument(
        "romfs_root",
        type=Path,
        help="Extracted OOT3D RomFS root.",
    )
    kokiri_runtime_route_parser.add_argument("--output", type=Path, required=True)
    kokiri_runtime_route_parser.add_argument(
        "--route-stem",
        action="append",
        dest="route_stems",
        help="Scene stem to include; repeat for multiple stems. Defaults to link and spot04.",
    )
    kokiri_runtime_route_parser.add_argument(
        "--actor-object-semantics",
        type=Path,
        help="Optional generated OOT3D/N64 actor-object semantic enum header for names.",
    )
    kokiri_runtime_route_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum sample records to include.",
    )
    kokiri_runtime_route_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-file setup records.",
    )
    kokiri_runtime_manifest_parser = subparsers.add_parser(
        "export-kokiri-runtime-manifest",
        help="Write a compact runtime routing manifest from the Kokiri route audit.",
    )
    kokiri_runtime_manifest_parser.add_argument(
        "runtime_route_audit",
        type=Path,
        help="JSON emitted by audit-kokiri-runtime-route.",
    )
    kokiri_runtime_manifest_parser.add_argument("--output", type=Path, required=True)
    kokiri_runtime_manifest_parser.add_argument(
        "--route-id",
        default="link_house_to_kokiri_forest",
        help="Stable route identifier written into the manifest.",
    )
    kokiri_runtime_manifest_parser.add_argument(
        "--n64-rom",
        default=None,
        help="Optional local N64 ROM reference path recorded as metadata only.",
    )
    kokiri_runtime_manifest_parser.add_argument(
        "--runtime-enablement-status",
        choices=("n64_fallback_only", "oot3d_runtime_enabled"),
        default="n64_fallback_only",
        help="Runtime status to write; oot3d_runtime_enabled is rejected unless blockers are zero.",
    )
    kokiri_runtime_manifest_parser.add_argument(
        "--n64-decomp-reference",
        default="https://github.com/zeldaret/oot",
        help="Primary N64 decomp reference recorded as metadata only.",
    )
    kokiri_runtime_manifest_parser.add_argument(
        "--sample-limit",
        type=int,
        default=20,
        help="Maximum number of setup sample records to include where bounded.",
    )
    kokiri_actor_asset_parser = subparsers.add_parser(
        "audit-kokiri-actor-asset-readiness",
        help="Audit route-local Kokiri actor/object asset coverage from generated manifests.",
    )
    kokiri_actor_asset_parser.add_argument(
        "runtime_manifest",
        type=Path,
        help="JSON emitted by export-kokiri-runtime-manifest.",
    )
    kokiri_actor_asset_parser.add_argument(
        "actor_inventory",
        type=Path,
        help="JSON emitted by inventory-actors.",
    )
    kokiri_actor_asset_parser.add_argument(
        "static_batch_manifest",
        type=Path,
        help="JSON emitted by batch-static for actor exports.",
    )
    kokiri_actor_asset_parser.add_argument(
        "skinned_binding_manifest",
        type=Path,
        help="JSON emitted by export-skinned-animation-binding-manifest.",
    )
    kokiri_actor_asset_parser.add_argument("--output", type=Path, required=True)
    kokiri_actor_asset_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum sample records to include.",
    )
    kokiri_static_actor_binding_parser = subparsers.add_parser(
        "export-kokiri-static-actor-binding-plan",
        help="Write the first-wave Kokiri static actor draw binding plan from generated audits.",
    )
    kokiri_static_actor_binding_parser.add_argument(
        "runtime_manifest",
        type=Path,
        help="JSON emitted by export-kokiri-runtime-manifest.",
    )
    kokiri_static_actor_binding_parser.add_argument(
        "actor_asset_readiness",
        type=Path,
        help="JSON emitted by audit-kokiri-actor-asset-readiness.",
    )
    kokiri_static_actor_binding_parser.add_argument(
        "static_batch_manifest",
        type=Path,
        help="JSON emitted by batch-static for actor exports.",
    )
    kokiri_static_actor_binding_parser.add_argument("--output", type=Path, required=True)
    kokiri_static_actor_binding_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum sample records to include.",
    )
    kokiri_kankyo_binding_parser = subparsers.add_parser(
        "export-kokiri-kankyo-binding-plan",
        help="Write the Kokiri sky/environment binding plan from runtime and kankyo export manifests.",
    )
    kokiri_kankyo_binding_parser.add_argument(
        "runtime_manifest",
        type=Path,
        help="JSON emitted by export-kokiri-runtime-manifest.",
    )
    kokiri_kankyo_binding_parser.add_argument(
        "kankyo_export_manifest",
        type=Path,
        help="JSON emitted by export-kankyo-environment-assets.",
    )
    kokiri_kankyo_binding_parser.add_argument("--output", type=Path, required=True)
    kokiri_kankyo_binding_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum sample records to include.",
    )
    kokiri_audio_binding_parser = subparsers.add_parser(
        "export-kokiri-audio-binding-plan",
        help="Write the Kokiri route audio fallback binding plan from runtime and audio audits.",
    )
    kokiri_audio_binding_parser.add_argument(
        "runtime_manifest",
        type=Path,
        help="JSON emitted by export-kokiri-runtime-manifest.",
    )
    kokiri_audio_binding_parser.add_argument(
        "audio_asset_audit",
        type=Path,
        help="JSON emitted by audit-audio-assets.",
    )
    kokiri_audio_binding_parser.add_argument("--output", type=Path, required=True)
    kokiri_audio_binding_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum sample records to include.",
    )

    prerendered_room_parser = subparsers.add_parser(
        "audit-prerendered-room-replacements",
        help="Map N64 prerendered-background room families to OOT3D full-3D room candidates.",
    )
    prerendered_room_parser.add_argument(
        "asset_xml_root",
        type=Path,
        help="Shipwright asset XML root, for example soh/assets/xml/GC_NMQ_D.",
    )
    prerendered_room_parser.add_argument(
        "scene_root",
        type=Path,
        help="Extracted OOT3D RomFS scene directory.",
    )
    prerendered_room_parser.add_argument("--output", type=Path, required=True)
    prerendered_room_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    prerendered_room_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-family records.",
    )

    prerendered_room_export_parser = subparsers.add_parser(
        "export-prerendered-room-replacements",
        help="Export OOT3D full-3D room candidates for N64 prerendered-background rooms and audit generated resources.",
    )
    prerendered_room_export_parser.add_argument(
        "asset_xml_root",
        type=Path,
        help="Shipwright asset XML root, for example soh/assets/xml/GC_NMQ_D.",
    )
    prerendered_room_export_parser.add_argument(
        "scene_root",
        type=Path,
        help="Extracted OOT3D RomFS scene directory.",
    )
    prerendered_room_export_parser.add_argument("--output", type=Path, required=True)
    prerendered_room_export_parser.add_argument(
        "--resource-prefix",
        default="scenes/prerendered_room_replacements/oot3d",
        help="Generated resource root prefix.",
    )
    prerendered_room_export_parser.add_argument("--no-textures", action="store_true")
    prerendered_room_export_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of mapping audit sample records to include.",
    )

    prerendered_room_pack_parser = subparsers.add_parser(
        "pack-prerendered-room-export",
        help="Pack a prerendered-room replacement export manifest into a Shipwright .o2r mod archive.",
    )
    prerendered_room_pack_parser.add_argument("export_manifest", type=Path)
    prerendered_room_pack_parser.add_argument("--output", type=Path, required=True, help="Output .o2r path.")
    prerendered_room_pack_parser.add_argument("--name", default="OOT3D Prerendered Room Candidates")
    prerendered_room_pack_parser.add_argument("--author", default="local")
    prerendered_room_pack_parser.add_argument("--version", default="0.1.0")

    prerendered_room_package_audit_parser = subparsers.add_parser(
        "audit-prerendered-room-export-package",
        help="Verify a packed O2R archive generated from a prerendered-room replacement export manifest.",
    )
    prerendered_room_package_audit_parser.add_argument("export_manifest", type=Path)
    prerendered_room_package_audit_parser.add_argument("archive", type=Path)
    prerendered_room_package_audit_parser.add_argument("--output", type=Path, required=True)
    prerendered_room_package_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample issue records to include.",
    )

    actor_inventory_parser = subparsers.add_parser(
        "inventory-actors",
        help="Write a metadata-only inventory of actor ZAR/CMB models and animation-bearing files.",
    )
    actor_inventory_parser.add_argument(
        "actor_root",
        type=Path,
        help="Directory containing OOT3D actor files, for example work/extract/romfs/actor.",
    )
    actor_inventory_parser.add_argument("--output", type=Path, required=True)
    actor_inventory_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of non-static model samples to include in the JSON output.",
    )
    actor_inventory_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-archive and per-model records.",
    )

    actor_qdb_parser = subparsers.add_parser(
        "audit-actor-qdb-payloads",
        help="Write a metadata-only audit of actor ZAR embedded QDB payloads.",
    )
    actor_qdb_parser.add_argument(
        "actor_root",
        type=Path,
        help="Extracted OOT3D RomFS actor directory.",
    )
    actor_qdb_parser.add_argument("--output", type=Path, required=True)
    actor_qdb_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    actor_qdb_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-payload records.",
    )

    actor_ccb_parser = subparsers.add_parser(
        "audit-actor-ccb-payloads",
        help="Write a metadata-only audit of actor ZAR embedded CCB camera/demo payloads.",
    )
    actor_ccb_parser.add_argument(
        "actor_root",
        type=Path,
        help="Extracted OOT3D RomFS actor directory.",
    )
    actor_ccb_parser.add_argument("--output", type=Path, required=True)
    actor_ccb_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    actor_ccb_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-payload records.",
    )

    actor_zsi_parser = subparsers.add_parser(
        "audit-actor-zsi-payloads",
        help="Write a metadata-only audit of actor ZAR embedded ZSI BGDataInfo payloads.",
    )
    actor_zsi_parser.add_argument(
        "actor_root",
        type=Path,
        help="Extracted OOT3D RomFS actor directory.",
    )
    actor_zsi_parser.add_argument("--output", type=Path, required=True)
    actor_zsi_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    actor_zsi_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-payload records.",
    )

    actor_material_lighting_parser = subparsers.add_parser(
        "audit-actor-material-lighting",
        help="Audit actor CMB material-lighting blocks for native PICA field coverage.",
    )
    actor_material_lighting_parser.add_argument(
        "actor_root",
        type=Path,
        help="Extracted OOT3D RomFS actor directory.",
    )
    actor_material_lighting_parser.add_argument("--output", type=Path, required=True)
    actor_material_lighting_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    actor_material_lighting_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-material records.",
    )

    csab_track_parser = subparsers.add_parser(
        "export-csab-rigid-tracks",
        help="Export decoded f32 CSAB rigid animation tracks for one explicit ZAR CSAB/CMB pair.",
    )
    csab_track_parser.add_argument("zar", type=Path, help="Actor ZAR containing the CSAB and CMB.")
    csab_track_parser.add_argument("--csab-name", required=True, help="Embedded CSAB path/name.")
    csab_track_parser.add_argument("--cmb-name", required=True, help="Embedded target CMB path/name.")
    csab_track_parser.add_argument("--output", type=Path, required=True)

    csab_skeleton_track_parser = subparsers.add_parser(
        "export-csab-skeleton-tracks",
        help="Export decoded f32 CSAB skeleton animation tracks for one explicit ZAR CSAB/CMB pair.",
    )
    csab_skeleton_track_parser.add_argument("zar", type=Path, help="Actor ZAR containing the CSAB and CMB.")
    csab_skeleton_track_parser.add_argument("--csab-name", required=True, help="Embedded CSAB path/name.")
    csab_skeleton_track_parser.add_argument("--cmb-name", required=True, help="Embedded target CMB path/name.")
    csab_skeleton_track_parser.add_argument("--output", type=Path, required=True)

    csab_track_batch_parser = subparsers.add_parser(
        "batch-csab-rigid-tracks",
        help="Export decoded f32 CSAB rigid animation tracks for all currently supported actor targets.",
    )
    csab_track_batch_parser.add_argument(
        "actor_root",
        type=Path,
        help="Directory containing OOT3D actor files, for example work/extract/romfs/actor.",
    )
    csab_track_batch_parser.add_argument("--output", type=Path, required=True)

    csab_skeleton_track_batch_parser = subparsers.add_parser(
        "batch-csab-skeleton-tracks",
        help="Export decoded f32 CSAB skeleton tracks for all skinned actor targets that currently validate.",
    )
    csab_skeleton_track_batch_parser.add_argument(
        "actor_root",
        type=Path,
        help="Directory containing OOT3D actor files, for example work/extract/romfs/actor.",
    )
    csab_skeleton_track_batch_parser.add_argument("--output", type=Path, required=True)

    csab_track_pack_parser = subparsers.add_parser(
        "pack-csab-track-batch",
        help="Pack a CSAB track batch manifest into an offline Shipwright .o2r candidate archive.",
    )
    csab_track_pack_parser.add_argument("batch_manifest", type=Path)
    csab_track_pack_parser.add_argument("--output", type=Path, required=True, help="Output .o2r path.")
    csab_track_pack_parser.add_argument("--name", default="OOT3D CSAB Track Candidates")
    csab_track_pack_parser.add_argument("--author", default="local")
    csab_track_pack_parser.add_argument("--version", default="0.1.0")
    csab_track_pack_parser.add_argument(
        "--archive-prefix",
        default="animations/oot3d/csab",
        help="Archive path prefix for generated track JSON entries.",
    )

    csab_track_package_audit_parser = subparsers.add_parser(
        "audit-csab-track-batch-package",
        help="Verify a packed O2R archive generated from a CSAB track batch manifest.",
    )
    csab_track_package_audit_parser.add_argument("batch_manifest", type=Path)
    csab_track_package_audit_parser.add_argument("archive", type=Path)
    csab_track_package_audit_parser.add_argument("--output", type=Path, required=True)
    csab_track_package_audit_parser.add_argument(
        "--archive-prefix",
        default="animations/oot3d/csab",
        help="Archive path prefix used for generated track JSON entries.",
    )
    csab_track_package_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum sample records to include for package issues.",
    )

    csab_target_audit_parser = subparsers.add_parser(
        "audit-csab-target-resolution",
        help="Write a focused audit of actor CSAB payloads whose target CMB cannot be resolved conservatively.",
    )
    csab_target_audit_parser.add_argument(
        "actor_root",
        type=Path,
        help="Directory containing OOT3D actor files, for example work/extract/romfs/actor.",
    )
    csab_target_audit_parser.add_argument("--output", type=Path, required=True)
    csab_target_audit_parser.add_argument(
        "--include-resolved-records",
        action="store_true",
        help="Include one record for every resolved CSAB target in addition to unresolved records.",
    )

    cmab_audit_parser = subparsers.add_parser(
        "audit-cmab-payloads",
        help="Write a focused audit of actor CMAB material-animation payload structure and target candidates.",
    )
    cmab_audit_parser.add_argument(
        "actor_root",
        type=Path,
        help="Directory containing OOT3D actor files, for example work/extract/romfs/actor.",
    )
    cmab_audit_parser.add_argument("--output", type=Path, required=True)
    cmab_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample and unresolved-target records to include.",
    )
    cmab_audit_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-CMAB records.",
    )

    animation_like_audit_parser = subparsers.add_parser(
        "audit-actor-animation-like-payloads",
        help="Write a focused audit of actor ANB and FACEB payload structure.",
    )
    animation_like_audit_parser.add_argument(
        "actor_root",
        type=Path,
        help="Directory containing OOT3D actor files, for example work/extract/romfs/actor.",
    )
    animation_like_audit_parser.add_argument("--output", type=Path, required=True)
    animation_like_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    animation_like_audit_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-payload records.",
    )

    anb_export_parser = subparsers.add_parser(
        "export-anb-payload-batch",
        help="Export OOT3D ANB payloads from an actor archive as stable raw IR records.",
    )
    anb_export_parser.add_argument("primary_archive", type=Path)
    anb_export_parser.add_argument("--output", type=Path, required=True)
    anb_export_parser.add_argument(
        "--duplicate-archive",
        type=Path,
        default=None,
        help="Optional archive with matching ANB names; payload bytes are compared when supplied.",
    )
    anb_export_parser.add_argument(
        "--csab-binding-manifest",
        type=Path,
        default=None,
        help="Optional skinned animation binding manifest used to build exact ANB-to-CSAB stem lookup records.",
    )
    anb_export_parser.add_argument(
        "--csab-target-archive",
        default=None,
        help="Optional target archive name used to scope the CSAB binding lookup.",
    )
    anb_export_parser.add_argument(
        "--csab-target-cmb",
        default=None,
        help="Optional target CMB name used to scope the CSAB binding lookup.",
    )
    anb_export_parser.add_argument(
        "--sample-limit",
        type=int,
        default=20,
        help="Maximum sample records and issues to include.",
    )

    anb_semantic_parser = subparsers.add_parser(
        "audit-anb-semantic-candidates",
        help="Correlate decoded ANB frame channels against exact-match CSAB track components.",
    )
    anb_semantic_parser.add_argument("anb_export", type=Path)
    anb_semantic_parser.add_argument("character_manifest", type=Path)
    anb_semantic_parser.add_argument("--output", type=Path, required=True)
    anb_semantic_parser.add_argument(
        "--unresolved-channel-csv-output",
        type=Path,
        default=None,
        help="Optional CSV worklist for ANB channels that are not stable semantic mappings yet.",
    )
    anb_semantic_parser.add_argument(
        "--track-root",
        type=Path,
        default=None,
        help="Directory containing CSAB track JSON exports. Defaults to the workroot skinned_animation_batch/tracks.",
    )
    anb_semantic_parser.add_argument(
        "--min-abs-correlation",
        type=float,
        default=0.98,
        help="Minimum absolute Pearson correlation accepted as a semantic candidate.",
    )
    anb_semantic_parser.add_argument(
        "--min-consensus-count",
        type=int,
        default=2,
        help="Minimum repeated component matches required for a stable channel mapping candidate.",
    )
    anb_semantic_parser.add_argument(
        "--sample-limit",
        type=int,
        default=20,
        help="Maximum compared/missing record samples to include.",
    )
    anb_semantic_parser.add_argument(
        "--include-frame-mismatch-resample",
        action="store_true",
        help="Compare matched ANB/CSAB records with different frame counts by proportionally resampling CSAB tracks.",
    )

    skinning_audit_parser = subparsers.add_parser(
        "audit-actor-skinning-queue",
        help="Write a focused JSON audit of actor CMBs and CSAB targets blocked by skinning support.",
    )
    skinning_audit_parser.add_argument(
        "actor_root",
        type=Path,
        help="Directory containing OOT3D actor files, for example work/extract/romfs/actor.",
    )
    skinning_audit_parser.add_argument("--output", type=Path, required=True)
    skinning_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of pilot candidates and blocked CSAB samples to include.",
    )

    skinning_layout_parser = subparsers.add_parser(
        "audit-actor-skinning-layout",
        help="Write a focused JSON audit of CMB skinning vertex index/weight attribute layout.",
    )
    skinning_layout_parser.add_argument(
        "actor_root",
        type=Path,
        help="Directory containing OOT3D actor files, for example work/extract/romfs/actor.",
    )
    skinning_layout_parser.add_argument("--output", type=Path, required=True)
    skinning_layout_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of skinned shape and constant-attribute samples to include.",
    )

    skinned_bind_pose_parser = subparsers.add_parser(
        "export-skinned-bind-pose",
        help="Export a decoded OOT3D CMB skinned mesh/intermediate bind-pose JSON.",
    )
    skinned_bind_pose_parser.add_argument("input", type=Path)
    skinned_bind_pose_parser.add_argument("--output", type=Path, required=True)
    skinned_bind_pose_parser.add_argument("--cmb-index", type=int, default=0)
    skinned_bind_pose_parser.add_argument("--cmb-name", default=None)
    skinned_bind_pose_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of vertex samples and validation errors to include.",
    )

    skinned_bind_pose_native_parser = subparsers.add_parser(
        "export-skinned-bind-pose-native",
        help="Export an OOT3D skinned CMB bind-pose as native Shipwright Texture/Vertex/DisplayList resources.",
    )
    skinned_bind_pose_native_parser.add_argument("input", type=Path)
    skinned_bind_pose_native_parser.add_argument("--output-dir", type=Path, required=True)
    skinned_bind_pose_native_parser.add_argument("--resource-root", required=True)
    skinned_bind_pose_native_parser.add_argument("--symbol", required=True)
    skinned_bind_pose_native_parser.add_argument("--cmb-index", type=int, default=0)
    skinned_bind_pose_native_parser.add_argument("--cmb-name", default=None)
    skinned_bind_pose_native_parser.add_argument(
        "--selection-manifest",
        type=Path,
        default=None,
        help="Optional JSON primitive-selection profile shared with static GLB inspection exports.",
    )
    skinned_bind_pose_native_parser.add_argument("--texture-orientation", default=None)
    skinned_bind_pose_native_parser.add_argument(
        "--uv-orientation",
        default="normal",
        help="Optional UV orientation adjustment: normal, flip_x, flip_y, or flip_xy.",
    )
    skinned_bind_pose_native_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of validation issue samples to include.",
    )

    static_base_glb_parser = subparsers.add_parser(
        "export-static-base-glb",
        help="Export an OOT3D CMB as a static base-position GLB for Blender inspection.",
    )
    static_base_glb_parser.add_argument("input", type=Path)
    static_base_glb_parser.add_argument("--output", type=Path, required=True)
    static_base_glb_parser.add_argument("--manifest-output", type=Path, default=None)
    static_base_glb_parser.add_argument(
        "--selection-manifest",
        type=Path,
        default=None,
        help="Optional JSON primitive-selection profile for data-driven base-character GLB exports.",
    )
    static_base_glb_parser.add_argument("--cmb-index", type=int, default=0)
    static_base_glb_parser.add_argument("--cmb-name", default=None)
    static_base_glb_parser.add_argument(
        "--draw-profile",
        default="all_meshes",
        help=(
            "Primitive filter for inspection: all_meshes, skinned_primitives_only, "
            "skinned_modes_only, rigid_primitives_only, selected_primitives/base_character, or visibility:<id>."
        ),
    )
    static_base_glb_parser.add_argument(
        "--texture-orientation",
        default=DEFAULT_GLB_TEXTURE_ORIENTATION,
        help="PNG orientation for embedded GLB textures. Defaults to flip_y for skinned GLB inspection.",
    )
    static_base_glb_parser.add_argument(
        "--uv-orientation",
        default="normal",
        help="Optional UV orientation adjustment: normal, flip_x, flip_y, or flip_xy.",
    )
    static_base_glb_parser.add_argument(
        "--position-scale",
        type=float,
        default=DEFAULT_POSITION_SCALE,
        help="Scale applied to exported GLB vertex positions. Defaults to 0.0001.",
    )
    static_base_glb_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of validation issue samples to include.",
    )

    character_visibility_parser = subparsers.add_parser(
        "audit-character-visibility-reconstruction",
        help="Reconstruct CMB visibility/draw-state evidence for a skinned character selection profile.",
    )
    character_visibility_parser.add_argument("input", type=Path)
    character_visibility_parser.add_argument("--output", type=Path, required=True)
    character_visibility_parser.add_argument("--selection-manifest", type=Path, default=None)
    character_visibility_parser.add_argument("--cmb-index", type=int, default=0)
    character_visibility_parser.add_argument("--cmb-name", default=None)
    character_visibility_parser.add_argument(
        "--n64-player-lib",
        type=Path,
        default=None,
        help="Optional z_player_lib.c reference used to record N64 Player draw-state anchors.",
    )
    character_visibility_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum primitive samples to include per visibility group.",
    )

    skinned_animation_glb_parser = subparsers.add_parser(
        "export-skinned-animation-glb",
        help="Export an OOT3D skinned CMB plus CSAB animation as a diagnostic baked-morph GLB.",
    )
    skinned_animation_glb_parser.add_argument("input", type=Path, help="Input actor ZAR archive.")
    skinned_animation_glb_parser.add_argument("--output", type=Path, required=True)
    skinned_animation_glb_parser.add_argument("--manifest-output", type=Path, default=None)
    skinned_animation_glb_parser.add_argument("--cmb-name", required=True)
    skinned_animation_glb_parser.add_argument("--csab-name", required=True)
    skinned_animation_glb_parser.add_argument("--selection-manifest", type=Path, default=None)
    skinned_animation_glb_parser.add_argument(
        "--texture-orientation",
        default=None,
        help="PNG orientation for embedded GLB textures. Defaults to the selection profile, then skinned flip_y.",
    )
    skinned_animation_glb_parser.add_argument(
        "--uv-orientation",
        default="normal",
        help="Optional UV orientation adjustment: normal, flip_x, flip_y, or flip_xy.",
    )
    skinned_animation_glb_parser.add_argument(
        "--position-scale",
        type=float,
        default=DEFAULT_POSITION_SCALE,
        help="Scale applied to exported GLB vertex positions. Defaults to 0.0001.",
    )
    skinned_animation_glb_parser.add_argument(
        "--frame-step",
        type=int,
        default=1,
        help="CSAB integer frame step to bake. Defaults to every frame.",
    )
    skinned_animation_glb_parser.add_argument(
        "--fps",
        type=float,
        default=60.0,
        help="Playback rate used for GLB animation times. Defaults to 60.",
    )
    skinned_animation_glb_parser.add_argument(
        "--interpolation",
        default="STEP",
        help="Morph weight interpolation for baked CSAB frames: STEP or LINEAR. Defaults to STEP.",
    )
    skinned_animation_glb_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of validation issue samples to include.",
    )

    minimal_runtime_player_parser = subparsers.add_parser(
        "audit-minimal-runtime-player-export",
        help="Export a player through the minimal runtime path and compare it frame-by-frame with the validated GLB oracle.",
    )
    minimal_runtime_player_parser.add_argument("character_manifest", type=Path)
    minimal_runtime_player_parser.add_argument("--output", type=Path, required=True, help="Audit JSON output.")
    minimal_runtime_player_parser.add_argument(
        "--runtime-glb-output",
        type=Path,
        required=True,
        help="GLB produced by the minimal runtime/package-data path.",
    )
    minimal_runtime_player_parser.add_argument(
        "--validated-glb-output",
        type=Path,
        default=None,
        help="Optional independently generated validated GLB oracle output.",
    )
    minimal_runtime_player_parser.add_argument(
        "--validated-manifest-output",
        type=Path,
        default=None,
        help="Optional manifest output for the validated GLB oracle.",
    )
    minimal_runtime_player_parser.add_argument("--zar", type=Path, default=None, help="Optional source actor ZAR override.")
    minimal_runtime_player_parser.add_argument("--cmb-name", default=None)
    minimal_runtime_player_parser.add_argument("--csab-name", default="child/anim/nml_run_free.csab")
    minimal_runtime_player_parser.add_argument("--selection-manifest", type=Path, default=None)
    minimal_runtime_player_parser.add_argument(
        "--position-scale",
        type=float,
        default=None,
        help="Scale applied to exported GLB vertex positions. Defaults to native manifest or 0.0001.",
    )
    minimal_runtime_player_parser.add_argument(
        "--frame-step",
        type=int,
        default=1,
        help="CSAB integer frame step to bake and compare. Defaults to every frame.",
    )
    minimal_runtime_player_parser.add_argument("--fps", type=float, default=60.0)
    minimal_runtime_player_parser.add_argument(
        "--interpolation",
        default="STEP",
        help="Morph weight interpolation for exported GLBs: STEP or LINEAR. Defaults to STEP.",
    )
    minimal_runtime_player_parser.add_argument(
        "--position-tolerance",
        type=float,
        default=0.00001,
        help="Maximum allowed position delta in GLB units.",
    )
    minimal_runtime_player_parser.add_argument(
        "--normal-tolerance",
        type=float,
        default=0.00001,
        help="Maximum allowed normal delta.",
    )
    minimal_runtime_player_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum mismatch samples to include.",
    )

    minimal_runtime_track_parser = subparsers.add_parser(
        "audit-minimal-runtime-track-export",
        help="Audit the minimal runtime player path against one materialized CSAB skeleton-track JSON.",
    )
    minimal_runtime_track_parser.add_argument("character_manifest", type=Path)
    minimal_runtime_track_parser.add_argument("track_export", type=Path)
    minimal_runtime_track_parser.add_argument("--output", type=Path, required=True, help="Audit JSON output.")
    minimal_runtime_track_parser.add_argument(
        "--runtime-glb-output",
        type=Path,
        required=True,
        help="GLB produced by the minimal runtime/package-data path.",
    )
    minimal_runtime_track_parser.add_argument(
        "--csab-name",
        default=None,
        help="Expected CSAB name for the track export. Defaults to the track JSON csab_name.",
    )
    minimal_runtime_track_parser.add_argument(
        "--position-scale",
        type=float,
        default=None,
        help="Scale applied to exported GLB vertex positions. Defaults to native manifest or 0.0001.",
    )
    minimal_runtime_track_parser.add_argument(
        "--frame-step",
        type=int,
        default=1,
        help="Track integer frame step to bake. Defaults to every frame.",
    )
    minimal_runtime_track_parser.add_argument("--fps", type=float, default=60.0)
    minimal_runtime_track_parser.add_argument(
        "--interpolation",
        default="STEP",
        help="Morph weight interpolation for exported GLBs: STEP or LINEAR. Defaults to STEP.",
    )
    minimal_runtime_track_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum issue samples to include.",
    )

    character_segment_continuity_parser = subparsers.add_parser(
        "audit-character-segment-continuity",
        help="Audit root-motion continuity across an ordered set of character CSAB segments.",
    )
    character_segment_continuity_parser.add_argument("character_manifest", type=Path)
    character_segment_continuity_parser.add_argument("--output", type=Path, required=True)
    character_segment_continuity_parser.add_argument(
        "--csab-name",
        action="append",
        required=True,
        help="CSAB segment name in playback order. Repeat for each segment.",
    )
    character_segment_continuity_parser.add_argument(
        "--root-motion-bone",
        type=int,
        default=None,
        help="Root-motion bone index. Defaults to the N64 mapping in the character manifest, then bone 0.",
    )
    character_segment_continuity_parser.add_argument(
        "--tolerance",
        type=float,
        default=0.001,
        help="Maximum allowed normalized transition delta in source units.",
    )
    character_segment_continuity_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum segment and transition records to include.",
    )

    runtime_draw_dump_parser = subparsers.add_parser(
        "audit-runtime-draw-dump",
        help="Compare an in-game C++ Link-child runtime draw dump against the canonical offline runtime computation.",
    )
    runtime_draw_dump_parser.add_argument("character_manifest", type=Path)
    runtime_draw_dump_parser.add_argument("runtime_dump", type=Path)
    runtime_draw_dump_parser.add_argument("--output", type=Path, required=True)
    runtime_draw_dump_parser.add_argument(
        "--track-export",
        type=Path,
        default=None,
        help="Optional explicit CSAB skeleton track export to compare against the runtime dump.",
    )
    runtime_draw_dump_parser.add_argument(
        "--segment-continuity-audit",
        type=Path,
        default=None,
        help="Optional segment continuity audit/contract used to normalize authored segment-chain draw positions.",
    )
    runtime_draw_dump_parser.add_argument(
        "--anb-semantic-audit",
        type=Path,
        default=None,
        help="Optional ANB semantic candidate audit whose runtime-state counts must appear in the dump.",
    )
    runtime_draw_dump_parser.add_argument(
        "--position-tolerance",
        type=float,
        default=1.0,
        help="Maximum allowed native-unit bounds delta per primitive.",
    )
    runtime_draw_dump_parser.add_argument(
        "--normal-tolerance",
        type=float,
        default=0.0001,
        help="Reserved normal tolerance reported in the audit contract.",
    )
    runtime_draw_dump_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum mismatch samples to include.",
    )

    skinned_bind_pose_native_audit_parser = subparsers.add_parser(
        "audit-skinned-bind-pose-native",
        help="Verify that a native skinned bind-pose export matches its CMB source-position contract.",
    )
    skinned_bind_pose_native_audit_parser.add_argument("bind_pose", type=Path)
    skinned_bind_pose_native_audit_parser.add_argument("native_manifest", type=Path)
    skinned_bind_pose_native_audit_parser.add_argument("--output", type=Path, required=True)
    skinned_bind_pose_native_audit_parser.add_argument(
        "--position-tolerance",
        type=float,
        default=1.0,
        help="Maximum accepted native/source bound delta after s16 rounding.",
    )
    skinned_bind_pose_native_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=50,
        help="Maximum number of validation issue samples to include.",
    )

    skinned_bind_pose_batch_parser = subparsers.add_parser(
        "batch-skinned-bind-poses",
        help="Export decoded OOT3D skinned CMB bind-pose JSON intermediates for actor RomFS.",
    )
    skinned_bind_pose_batch_parser.add_argument(
        "actor_root",
        type=Path,
        help="Directory containing OOT3D actor files, for example work/extract/romfs/actor.",
    )
    skinned_bind_pose_batch_parser.add_argument("--output", type=Path, required=True)
    skinned_bind_pose_batch_parser.add_argument(
        "--sample-limit",
        type=int,
        default=25,
        help="Maximum number of vertex samples and validation errors per exported CMB.",
    )

    skinned_bind_pose_pack_parser = subparsers.add_parser(
        "pack-skinned-bind-pose-batch",
        help="Pack a skinned bind-pose batch manifest into an offline Shipwright .o2r candidate archive.",
    )
    skinned_bind_pose_pack_parser.add_argument("batch_manifest", type=Path)
    skinned_bind_pose_pack_parser.add_argument("--output", type=Path, required=True, help="Output .o2r path.")
    skinned_bind_pose_pack_parser.add_argument("--name", default="OOT3D Skinned Bind-Pose Candidates")
    skinned_bind_pose_pack_parser.add_argument("--author", default="local")
    skinned_bind_pose_pack_parser.add_argument("--version", default="0.1.0")
    skinned_bind_pose_pack_parser.add_argument(
        "--archive-prefix",
        default="objects/oot3d/skinned_bind_pose",
        help="Archive path prefix for generated skinned bind-pose JSON entries.",
    )

    skinned_bind_pose_package_audit_parser = subparsers.add_parser(
        "audit-skinned-bind-pose-batch-package",
        help="Verify a packed O2R archive generated from a skinned bind-pose batch manifest.",
    )
    skinned_bind_pose_package_audit_parser.add_argument("batch_manifest", type=Path)
    skinned_bind_pose_package_audit_parser.add_argument("archive", type=Path)
    skinned_bind_pose_package_audit_parser.add_argument("--output", type=Path, required=True)
    skinned_bind_pose_package_audit_parser.add_argument(
        "--archive-prefix",
        default="objects/oot3d/skinned_bind_pose",
        help="Archive path prefix used for generated skinned bind-pose JSON entries.",
    )
    skinned_bind_pose_package_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum sample records to include for package issues.",
    )

    skinned_animation_readiness_parser = subparsers.add_parser(
        "audit-skinned-animation-readiness",
        help="Verify that exported skinned CSAB tracks resolve to exported skinned bind-pose targets.",
    )
    skinned_animation_readiness_parser.add_argument("bind_pose_manifest", type=Path)
    skinned_animation_readiness_parser.add_argument("csab_track_manifest", type=Path)
    skinned_animation_readiness_parser.add_argument("--output", type=Path, required=True)
    skinned_animation_readiness_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum sample records to include for readiness issues and top targets.",
    )

    skinned_animation_binding_parser = subparsers.add_parser(
        "export-skinned-animation-binding-manifest",
        help="Write an offline target-to-animation binding manifest from skinned bind-pose and CSAB batches.",
    )
    skinned_animation_binding_parser.add_argument("bind_pose_manifest", type=Path)
    skinned_animation_binding_parser.add_argument("csab_track_manifest", type=Path)
    skinned_animation_binding_parser.add_argument("--output", type=Path, required=True)
    skinned_animation_binding_parser.add_argument(
        "--bind-pose-archive-prefix",
        default="objects/oot3d/skinned_bind_pose",
        help="Archive path prefix used by the skinned bind-pose candidate package.",
    )
    skinned_animation_binding_parser.add_argument(
        "--csab-track-archive-prefix",
        default="animations/oot3d/csab/skinned",
        help="Archive path prefix used by the skinned CSAB track candidate package.",
    )
    skinned_animation_binding_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum sample records to include in embedded readiness summary fields.",
    )

    skinned_animation_pose_parser = subparsers.add_parser(
        "export-skinned-animation-pose-samples",
        help="Export offline skinned vertex pose samples for one explicit ZAR CSAB/CMB pair.",
    )
    skinned_animation_pose_parser.add_argument(
        "zar",
        type=Path,
        help="Actor ZAR containing the CSAB and skinned CMB.",
    )
    skinned_animation_pose_parser.add_argument("--csab-name", required=True)
    skinned_animation_pose_parser.add_argument("--cmb-name", required=True)
    skinned_animation_pose_parser.add_argument("--output", type=Path, required=True)
    skinned_animation_pose_parser.add_argument(
        "--frames",
        help="Comma-separated integer frame indices to sample; defaults to start, midpoint, and end.",
    )
    skinned_animation_pose_parser.add_argument(
        "--sample-limit",
        type=int,
        default=25,
        help="Maximum number of vertex samples to include per sampled frame.",
    )

    skinned_animation_pose_batch_parser = subparsers.add_parser(
        "batch-skinned-animation-pose-samples",
        help="Batch-export offline skinned vertex pose samples for resolved skinned actor CSAB targets.",
    )
    skinned_animation_pose_batch_parser.add_argument(
        "actor_root",
        type=Path,
        help="Directory containing OOT3D actor files, for example work/extract/romfs/actor.",
    )
    skinned_animation_pose_batch_parser.add_argument("--output", type=Path, required=True)
    skinned_animation_pose_batch_parser.add_argument(
        "--sample-limit",
        type=int,
        default=0,
        help="Maximum number of vertex samples to include per sampled frame in each target export.",
    )

    character_conversion_parser = subparsers.add_parser(
        "export-character-conversion-manifest",
        help="Compose a character-level OOT3D conversion gate from skinned, animation-like, and CMAB manifests.",
    )
    character_conversion_parser.add_argument("skinned_binding_manifest", type=Path)
    character_conversion_parser.add_argument("animation_like_audit", type=Path)
    character_conversion_parser.add_argument("cmab_audit", type=Path)
    character_conversion_parser.add_argument("--output", type=Path, required=True)
    character_conversion_parser.add_argument("--profile-id", required=True)
    character_conversion_parser.add_argument("--model-archive", required=True)
    character_conversion_parser.add_argument("--model-cmb", required=True)
    character_conversion_parser.add_argument(
        "--pose-batch-manifest",
        type=Path,
        default=None,
        help="Optional skinned animation pose batch manifest used to validate sampled deformation volumes.",
    )
    character_conversion_parser.add_argument(
        "--native-bind-pose-manifest",
        type=Path,
        default=None,
        help="Optional native Shipwright resource manifest for the skinned bind-pose display list.",
    )
    character_conversion_parser.add_argument(
        "--n64-reference-audit",
        type=Path,
        default=None,
        help="Optional N64 animation reference audit used as the time-normalized comparison oracle inventory.",
    )
    character_conversion_parser.add_argument(
        "--auxiliary-archive",
        action="append",
        default=[],
        help="Additional archive to include for related ANB/FACEB/CMAB payloads. May be repeated.",
    )
    character_conversion_parser.add_argument(
        "--reference-label",
        default="oot_n64_time_normalized_reference",
        help="Label describing the external animation/behavior reference used by validation.",
    )
    character_conversion_parser.add_argument(
        "--runtime-skinning-diagnostic-csab",
        action="append",
        default=[],
        help="CSAB name to embed as a compact runtime skinning diagnostic sample. May be repeated.",
    )
    character_conversion_parser.add_argument(
        "--sample-limit",
        type=int,
        default=25,
        help="Maximum number of sample records to include for each payload class.",
    )

    character_package_parser = subparsers.add_parser(
        "pack-character-conversion",
        help="Pack one character conversion manifest plus native bind-pose/CSAB resources into an O2R archive.",
    )
    character_package_parser.add_argument("character_manifest", type=Path)
    character_package_parser.add_argument("--output", type=Path, required=True, help="Output .o2r path.")
    character_package_parser.add_argument("--name", default="OOT3D Character Conversion Profile")
    character_package_parser.add_argument("--author", default="local")
    character_package_parser.add_argument("--version", default="0.1.0")
    character_package_parser.add_argument(
        "--runtime-profile-path",
        default=None,
        help="Archive path for the generated compact runtime profile JSON.",
    )
    character_package_parser.add_argument(
        "--segment-continuity-audit",
        type=Path,
        default=None,
        help="Optional segment continuity audit to embed as a runtime normalization contract.",
    )
    character_package_parser.add_argument(
        "--anb-export",
        type=Path,
        default=None,
        help="Optional ANB raw IR export JSON to package and expose through the runtime profile.",
    )
    character_package_parser.add_argument(
        "--anb-semantic-audit",
        type=Path,
        default=None,
        help="Optional ANB semantic candidate audit JSON to expose through the runtime profile.",
    )
    character_package_parser.add_argument(
        "--runtime-semantics",
        type=Path,
        default=None,
        help="Optional native runtime-semantics JSON to validate and embed in the runtime profile.",
    )

    character_package_audit_parser = subparsers.add_parser(
        "audit-character-conversion-package",
        help="Verify an O2R archive generated from a character conversion manifest.",
    )
    character_package_audit_parser.add_argument("character_manifest", type=Path)
    character_package_audit_parser.add_argument("archive", type=Path)
    character_package_audit_parser.add_argument("--output", type=Path, required=True)
    character_package_audit_parser.add_argument(
        "--runtime-profile-path",
        default=None,
        help="Archive path for the generated compact runtime profile JSON.",
    )
    character_package_audit_parser.add_argument(
        "--segment-continuity-audit",
        type=Path,
        default=None,
        help="Optional segment continuity audit expected in the runtime profile.",
    )
    character_package_audit_parser.add_argument(
        "--anb-export",
        type=Path,
        default=None,
        help="Optional ANB raw IR export JSON expected in the runtime profile and archive.",
    )
    character_package_audit_parser.add_argument(
        "--anb-semantic-audit",
        type=Path,
        default=None,
        help="Optional ANB semantic candidate audit JSON expected in the runtime profile.",
    )
    character_package_audit_parser.add_argument(
        "--runtime-semantics",
        type=Path,
        default=None,
        help="Optional native runtime-semantics JSON expected in the runtime profile.",
    )
    character_package_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum sample records to include for package issues.",
    )

    n64_animation_reference_parser = subparsers.add_parser(
        "audit-n64-animation-reference",
        help="Build a name-based N64 PlayerAnimation reference map for an OOT3D skinned target.",
    )
    n64_animation_reference_parser.add_argument("skinned_binding_manifest", type=Path)
    n64_animation_reference_parser.add_argument("n64_player_animation_xml", type=Path)
    n64_animation_reference_parser.add_argument("n64_link_object_xml", type=Path)
    n64_animation_reference_parser.add_argument("--output", type=Path, required=True)
    n64_animation_reference_parser.add_argument("--n64-player-animation-data-xml", type=Path, default=None)
    n64_animation_reference_parser.add_argument("--n64-base-o2r", type=Path, default=None)
    n64_animation_reference_parser.add_argument("--oot3d-pose-batch-manifest", type=Path, default=None)
    n64_animation_reference_parser.add_argument("--profile-id", required=True)
    n64_animation_reference_parser.add_argument("--model-archive", required=True)
    n64_animation_reference_parser.add_argument("--model-cmb", required=True)
    n64_animation_reference_parser.add_argument("--n64-skeleton-name", default=None)
    n64_animation_reference_parser.add_argument("--oot3d-strip-prefix", action="append", default=[])
    n64_animation_reference_parser.add_argument("--n64-strip-prefix", action="append", default=[])
    n64_animation_reference_parser.add_argument("--reference-version", default="N64_NTSC_12")
    n64_animation_reference_parser.add_argument(
        "--sample-limit",
        type=int,
        default=25,
        help="Maximum number of sample mapping records to include.",
    )

    link_child_animation_mapping_parser = subparsers.add_parser(
        "audit-link-child-animation-semantic-mapping",
        help="Build an explicit N64 PlayerAnimation -> OOT3D CSAB mapping coverage audit.",
    )
    link_child_animation_mapping_parser.add_argument("n64_reference_audit", type=Path)
    link_child_animation_mapping_parser.add_argument("n64_player_animation_xml", type=Path)
    link_child_animation_mapping_parser.add_argument("--output", type=Path, required=True)
    link_child_animation_mapping_parser.add_argument("--n64-player-animation-data-xml", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--candidate-validation-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--candidate-pose-metric-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-alias-promotion-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-alias-route-review-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-alias-resample-review-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-alias-temporal-bake-review-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-alias-bake-contract-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-alias-ambiguity-arbitration-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-alias-accepted-overlay-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--unresolved-near-candidate-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--unresolved-near-candidate-pose-metric-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--unresolved-near-candidate-bake-contract-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--anonymous-numeric-candidate-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--anonymous-numeric-pose-metric-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-resolution-contract-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-blocked-resolution-frontier-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--character-profile-o2r", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-materialization-plan-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-materialization-plan-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-materialized-track-output-dir", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-materialized-track-manifest-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-materialized-track-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-materialized-pose-metric-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-ownership-derivative-plan-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-ownership-derivative-plan-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-ownership-derivative-track-output-dir", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-ownership-derivative-track-manifest-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-ownership-derivative-track-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-ownership-derivative-pose-metric-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-ownership-derivative-frontier-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-ownership-derivative-frontier-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-ownership-reuse-arbitration-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-ownership-reuse-arbitration-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-route-proven-ownership-callsite-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument("--semantic-route-proven-ownership-callsite-csv-output", type=Path, default=None)
    link_child_animation_mapping_parser.add_argument(
        "--source-search-root",
        type=Path,
        action="append",
        default=[],
        help="Optional C/C++ source root searched for direct references to anonymous numeric N64 PlayerAnimation symbols.",
    )
    link_child_animation_mapping_parser.add_argument(
        "--sample-limit",
        type=int,
        default=50,
        help="Maximum sample records to include per mapping bucket.",
    )

    link_child_anonymous_numeric_parser = subparsers.add_parser(
        "build-link-child-anonymous-numeric-diagnostic-tracks",
        help="Materialize diagnostic tracks for anonymous numeric Link child PlayerAnimation candidates.",
    )
    link_child_anonymous_numeric_parser.add_argument("semantic_blocked_resolution_frontier_csv", type=Path)
    link_child_anonymous_numeric_parser.add_argument("character_profile_o2r", type=Path)
    link_child_anonymous_numeric_parser.add_argument("--output-dir", type=Path, required=True)
    link_child_anonymous_numeric_parser.add_argument("--summary-output", type=Path, required=True)
    link_child_anonymous_numeric_parser.add_argument("--plan-output", type=Path, default=None)
    link_child_anonymous_numeric_parser.add_argument("--plan-csv-output", type=Path, default=None)
    link_child_anonymous_numeric_parser.add_argument("--track-manifest-output", type=Path, default=None)
    link_child_anonymous_numeric_parser.add_argument("--track-csv-output", type=Path, default=None)
    link_child_anonymous_numeric_parser.add_argument(
        "--sample-limit",
        type=int,
        default=50,
        help="Maximum sample records to include per diagnostic bucket.",
    )

    link_child_pose_source_risk_parser = subparsers.add_parser(
        "build-link-child-pose-source-risk-diagnostic-tracks",
        help="Materialize diagnostic tracks for outside-envelope Link child PlayerAnimation candidates.",
    )
    link_child_pose_source_risk_parser.add_argument("semantic_blocked_resolution_frontier_csv", type=Path)
    link_child_pose_source_risk_parser.add_argument("character_profile_o2r", type=Path)
    link_child_pose_source_risk_parser.add_argument("--output-dir", type=Path, required=True)
    link_child_pose_source_risk_parser.add_argument("--summary-output", type=Path, required=True)
    link_child_pose_source_risk_parser.add_argument("--plan-output", type=Path, default=None)
    link_child_pose_source_risk_parser.add_argument("--plan-csv-output", type=Path, default=None)
    link_child_pose_source_risk_parser.add_argument("--track-manifest-output", type=Path, default=None)
    link_child_pose_source_risk_parser.add_argument("--track-csv-output", type=Path, default=None)
    link_child_pose_source_risk_parser.add_argument(
        "--sample-limit",
        type=int,
        default=50,
        help="Maximum sample records to include per diagnostic bucket.",
    )

    link_child_route_runtime_parser = subparsers.add_parser(
        "build-link-child-route-runtime-diagnostic-tracks",
        help="Materialize diagnostic tracks for route/runtime-gated Link child PlayerAnimation candidates.",
    )
    link_child_route_runtime_parser.add_argument("semantic_blocked_resolution_frontier_csv", type=Path)
    link_child_route_runtime_parser.add_argument("character_profile_o2r", type=Path)
    link_child_route_runtime_parser.add_argument("--output-dir", type=Path, required=True)
    link_child_route_runtime_parser.add_argument("--summary-output", type=Path, required=True)
    link_child_route_runtime_parser.add_argument("--plan-output", type=Path, default=None)
    link_child_route_runtime_parser.add_argument("--plan-csv-output", type=Path, default=None)
    link_child_route_runtime_parser.add_argument("--track-manifest-output", type=Path, default=None)
    link_child_route_runtime_parser.add_argument("--track-csv-output", type=Path, default=None)
    link_child_route_runtime_parser.add_argument(
        "--sample-limit",
        type=int,
        default=50,
        help="Maximum sample records to include per diagnostic bucket.",
    )

    link_child_pose_source_callsite_parser = subparsers.add_parser(
        "audit-link-child-pose-source-callsite-context",
        help="Classify N64 source callsites for outside-envelope Link child animation candidates.",
    )
    link_child_pose_source_callsite_parser.add_argument("semantic_resolution_contract_csv", type=Path)
    link_child_pose_source_callsite_parser.add_argument("--output", type=Path, required=True)
    link_child_pose_source_callsite_parser.add_argument("--csv-output", type=Path, default=None)
    link_child_pose_source_callsite_parser.add_argument(
        "--source-search-root",
        type=Path,
        action="append",
        default=[],
        help="C/C++ source root searched for direct N64 PlayerAnimation symbol callsites.",
    )
    link_child_pose_source_callsite_parser.add_argument(
        "--sample-limit",
        type=int,
        default=50,
        help="Maximum sample rows and contexts to include.",
    )

    convert_parser = subparsers.add_parser(
        "convert-static",
        help="Convert a static CMB, or an embedded CMB from a ZAR/ZSI, to Shipwright custom resources.",
    )
    convert_parser.add_argument("input", type=Path)
    convert_parser.add_argument("--output", type=Path, required=True)
    convert_parser.add_argument("--resource-root", default=None)
    convert_parser.add_argument("--symbol", default=None)
    convert_parser.add_argument("--cmb-index", type=int, default=0)
    convert_parser.add_argument("--cmb-name", default=None)
    convert_parser.add_argument("--no-textures", action="store_true")
    convert_parser.add_argument(
        "--texture-orientation",
        choices=("normal", "flip-x", "flip-y", "flip-xy"),
        default=None,
        help=(
            "Orient decoded CMB textures before writing Shipwright resources. "
            "Defaults from the mesh/texture export profile."
        ),
    )
    convert_parser.add_argument(
        "--uv-orientation",
        choices=("normal", "flip-x", "flip-y", "flip-xy"),
        default="normal",
        help="Orient exported UVs globally in texture space before converting them to N64 S/T.",
    )
    convert_parser.add_argument(
        "--allow-nonstatic",
        action="store_true",
        help="Bypass the rigid mode-0 candidate guard for manual experiments.",
    )

    batch_parser = subparsers.add_parser(
        "batch-static",
        help="Scan files/directories and convert every static CMB candidate that is currently supported.",
    )
    batch_parser.add_argument("input", type=Path)
    batch_parser.add_argument("--output", type=Path, required=True)
    batch_parser.add_argument("--resource-prefix", default="objects/oot3d_batch")
    batch_parser.add_argument("--limit", type=int, default=None)
    batch_parser.add_argument("--no-textures", action="store_true")
    batch_parser.add_argument(
        "--texture-orientation",
        choices=("normal", "flip-x", "flip-y", "flip-xy"),
        default=None,
        help=(
            "Orient decoded CMB textures before writing Shipwright resources. "
            "Defaults from the mesh/texture export profile."
        ),
    )
    batch_parser.add_argument(
        "--uv-orientation",
        choices=("normal", "flip-x", "flip-y", "flip-xy"),
        default="normal",
        help="Orient exported UVs globally in texture space before converting them to N64 S/T.",
    )
    batch_parser.add_argument(
        "--only-rigid-multibone",
        action="store_true",
        help="Only convert rigid export candidates with more than one skeleton bone.",
    )
    batch_parser.add_argument(
        "--allow-nonstatic",
        action="store_true",
        help="Bypass static-candidate filtering for manual experiments.",
    )

    batch_audit_parser = subparsers.add_parser(
        "audit-static-batch",
        help="Verify generated files and resource bindings in a batch-static manifest.",
    )
    batch_audit_parser.add_argument("batch_manifest", type=Path)
    batch_audit_parser.add_argument("--output", type=Path, required=True)

    static_batch_pack_parser = subparsers.add_parser(
        "pack-static-batch",
        help="Pack a batch-static manifest into a Shipwright .o2r mod archive.",
    )
    static_batch_pack_parser.add_argument("batch_manifest", type=Path)
    static_batch_pack_parser.add_argument("--output", type=Path, required=True, help="Output .o2r path.")
    static_batch_pack_parser.add_argument("--name", default="OOT3D Static Batch")
    static_batch_pack_parser.add_argument("--author", default="local")
    static_batch_pack_parser.add_argument("--version", default="0.1.0")

    static_batch_package_audit_parser = subparsers.add_parser(
        "audit-static-batch-package",
        help="Verify a packed O2R archive generated from a batch-static manifest.",
    )
    static_batch_package_audit_parser.add_argument("batch_manifest", type=Path)
    static_batch_package_audit_parser.add_argument("archive", type=Path)
    static_batch_package_audit_parser.add_argument("--output", type=Path, required=True)
    static_batch_package_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum sample records to include for package issues.",
    )

    scene_parser = subparsers.add_parser(
        "convert-scene",
        help="Convert every static room CMB embedded in a scene's room ZSI files.",
    )
    scene_parser.add_argument(
        "scene_dir",
        type=Path,
        help="Directory containing OOT3D scene files, for example work/extract/romfs/scene.",
    )
    scene_parser.add_argument("--scene", required=True, help="Scene stem, for example spot04.")
    scene_parser.add_argument("--output", type=Path, required=True)
    scene_parser.add_argument(
        "--resource-prefix",
        default=None,
        help="Generated resource root prefix. Defaults to scenes/overworld/<scene>/oot3d.",
    )
    scene_parser.add_argument(
        "--shipwright-root",
        default=None,
        help="Shipwright scene root used in the assignment manifest. Defaults to scenes/overworld/<scene>.",
    )
    scene_parser.add_argument("--no-textures", action="store_true")
    scene_parser.add_argument(
        "--texture-orientation",
        choices=("normal", "flip-x", "flip-y", "flip-xy"),
        default=None,
        help=(
            "Orient decoded CMB textures before writing Shipwright resources. "
            "Defaults from the mesh/texture export profile."
        ),
    )
    scene_parser.add_argument(
        "--uv-orientation",
        choices=("normal", "flip-x", "flip-y", "flip-xy"),
        default="normal",
        help="Orient exported UVs globally in texture space before converting them to N64 S/T.",
    )
    scene_parser.add_argument(
        "--generate-collision",
        action="store_true",
        help=(
            "Deprecated and disabled: scene collision must come from native OOT3D ZSI collision data. "
            "Use export-zsi-collision instead."
        ),
    )
    scene_parser.add_argument(
        "--no-collision",
        action="store_true",
        help="Compatibility flag; collision replacement generation is disabled by default.",
    )
    scene_parser.add_argument(
        "--collision-path",
        default=None,
        help=(
            "Deprecated with convert-scene; use export-zsi-collision with this resource path so collision comes "
            "from native OOT3D scene data."
        ),
    )
    scene_parser.add_argument(
        "--base-o2r",
        type=Path,
        default=None,
        help="Optional base OTR/O2R archive used to copy camera data and water boxes into generated collision.",
    )
    scene_parser.add_argument(
        "--allow-nonstatic",
        action="store_true",
        help="Bypass static-candidate filtering for manual experiments.",
    )

    zsi_collision_parser = subparsers.add_parser(
        "export-zsi-collision",
        help="Export a decoded OOT3D scene ZSI collision candidate to Shipwright CollisionHeader XML.",
    )
    zsi_collision_parser.add_argument("input", type=Path, help="Scene-level OOT3D .zsi file.")
    zsi_collision_parser.add_argument("--output", type=Path, required=True)
    zsi_collision_parser.add_argument(
        "--resource-path",
        default=None,
        help=(
            "Shipwright CollisionHeader resource path to write into the generated file metadata. "
            "Required unless --reference-o2r can resolve it by scene stem."
        ),
    )
    zsi_collision_parser.add_argument("--candidate-index", type=int, default=0)
    zsi_collision_parser.add_argument(
        "--format",
        choices=sorted(COLLISION_RESOURCE_FORMATS),
        default="binary",
        help="Output resource format. Use xml for inspection; binary is Shipwright's runtime OCOL format.",
    )
    zsi_collision_parser.add_argument(
        "--collision-node-budget",
        type=int,
        default=None,
        help="Optional Shipwright static lookup node budget for generated collision.",
    )
    zsi_collision_parser.add_argument(
        "--collision-budget-policy",
        choices=sorted(COLLISION_BUDGET_POLICIES),
        default="none",
        help="Policy used with --collision-node-budget. preserve-floors removes non-floor polygons first.",
    )
    zsi_collision_parser.add_argument(
        "--reference-o2r",
        type=Path,
        default=None,
        help="Optional OTR/O2R archive containing the original N64/Shipwright collision resource to compare against.",
    )
    zsi_collision_parser.add_argument(
        "--reference-resource-path",
        default=None,
        help="Collision resource path inside --reference-o2r. Defaults to --resource-path.",
    )
    zsi_collision_parser.add_argument(
        "--reference-floor-probes",
        type=int,
        default=DEFAULT_REFERENCE_FLOOR_PROBE_COUNT,
        help="Maximum reference floor polygons to sample for walkability comparison.",
    )
    zsi_collision_parser.add_argument(
        "--reference-floor-query-above",
        type=float,
        default=DEFAULT_REFERENCE_FLOOR_QUERY_ABOVE,
        help="Y offset above each reference floor probe used for the converted raycast.",
    )
    zsi_collision_parser.add_argument(
        "--reference-floor-tolerance",
        type=float,
        default=DEFAULT_REFERENCE_FLOOR_TOLERANCE,
        help="Maximum absolute floor Y delta accepted for each reference-derived probe.",
    )
    zsi_collision_parser.add_argument(
        "--require-reference-match",
        action="store_true",
        help="Fail export when --reference-o2r comparison does not pass the general collision acceptance checks.",
    )
    zsi_collision_parser.add_argument(
        "--visual-room-dir",
        type=Path,
        default=None,
        help=(
            "Optional scene directory containing OOT3D room ZSI files. Enables a diagnostic-only floor audit "
            "against the room CMB visual meshes; it is not used as collision input."
        ),
    )
    zsi_collision_parser.add_argument(
        "--visual-scene",
        default=None,
        help="Scene stem used with --visual-room-dir. Defaults to the input stem before _info.",
    )
    zsi_collision_parser.add_argument(
        "--visual-floor-probes",
        type=int,
        default=DEFAULT_VISUAL_MESH_FLOOR_PROBE_COUNT,
        help="Maximum OOT3D visual floor proxy polygons to sample for the diagnostic-only floor audit.",
    )

    zsi_collision_gate_audit_parser = subparsers.add_parser(
        "audit-zsi-collision-gates",
        help="Run the native-ZSI collision acceptance gate across scene ZSI files without enabling runtime replacement.",
    )
    zsi_collision_gate_audit_parser.add_argument(
        "scene_root",
        type=Path,
        help="Directory containing OOT3D scene files, for example work/extract/romfs/scene.",
    )
    zsi_collision_gate_audit_parser.add_argument(
        "--reference-o2r",
        type=Path,
        required=True,
        help="OTR/O2R archive containing original N64/Shipwright collision resources.",
    )
    zsi_collision_gate_audit_parser.add_argument("--output", type=Path, required=True)
    zsi_collision_gate_audit_parser.add_argument(
        "--scene",
        action="append",
        default=[],
        help="Optional scene stem filter. Can be passed more than once.",
    )
    zsi_collision_gate_audit_parser.add_argument(
        "--limit",
        type=int,
        default=None,
        help="Maximum number of scene ZSI files to audit after filtering.",
    )
    zsi_collision_gate_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of records to include in sample_records.",
    )
    zsi_collision_gate_audit_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting the full records array.",
    )
    zsi_collision_gate_audit_parser.add_argument(
        "--format",
        choices=sorted(COLLISION_RESOURCE_FORMATS),
        default="binary",
        help="Temporary generated collision resource format used by the exact export path.",
    )
    zsi_collision_gate_audit_parser.add_argument(
        "--reference-floor-probes",
        type=int,
        default=DEFAULT_REFERENCE_FLOOR_PROBE_COUNT,
        help="Maximum reference floor polygons to sample for walkability comparison.",
    )
    zsi_collision_gate_audit_parser.add_argument(
        "--reference-floor-query-above",
        type=float,
        default=DEFAULT_REFERENCE_FLOOR_QUERY_ABOVE,
        help="Y offset above each reference floor probe used for the converted raycast.",
    )
    zsi_collision_gate_audit_parser.add_argument(
        "--reference-floor-tolerance",
        type=float,
        default=DEFAULT_REFERENCE_FLOOR_TOLERANCE,
        help="Maximum absolute floor Y delta accepted for each reference-derived probe.",
    )
    zsi_collision_gate_audit_parser.add_argument(
        "--visual-room-dir",
        type=Path,
        default=None,
        help=(
            "Optional scene directory containing OOT3D room ZSI files. Adds diagnostic-only "
            "visual mesh floor metrics to records; it is not used as collision input."
        ),
    )
    zsi_collision_gate_audit_parser.add_argument(
        "--visual-floor-probes",
        type=int,
        default=DEFAULT_VISUAL_MESH_FLOOR_PROBE_COUNT,
        help="Maximum OOT3D visual floor proxy polygons to sample for each diagnostic record.",
    )

    zsi_collision_activation_parser = subparsers.add_parser(
        "export-zsi-collision-activation-manifest",
        help="Export only native-ZSI collision resources accepted by the scene collision gate.",
    )
    zsi_collision_activation_parser.add_argument(
        "scene_root",
        type=Path,
        help="Directory containing OOT3D scene files, for example work/extract/romfs/scene.",
    )
    zsi_collision_activation_parser.add_argument(
        "--reference-o2r",
        type=Path,
        required=True,
        help="OTR/O2R archive containing original N64/Shipwright collision resources.",
    )
    zsi_collision_activation_parser.add_argument("--output", type=Path, required=True)
    zsi_collision_activation_parser.add_argument(
        "--resource-output-dir",
        type=Path,
        required=True,
        help="Directory where accepted CollisionHeader resources are written.",
    )
    zsi_collision_activation_parser.add_argument(
        "--scene",
        action="append",
        default=[],
        help="Optional scene stem filter. Can be passed more than once.",
    )
    zsi_collision_activation_parser.add_argument(
        "--limit",
        type=int,
        default=None,
        help="Maximum number of scene ZSI files to audit after filtering.",
    )
    zsi_collision_activation_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of records to include in the internal gate sample.",
    )
    zsi_collision_activation_parser.add_argument(
        "--format",
        choices=sorted(COLLISION_RESOURCE_FORMATS),
        default="binary",
        help="Generated collision resource format. Binary is Shipwright's runtime OCOL format.",
    )
    zsi_collision_activation_parser.add_argument(
        "--reference-floor-probes",
        type=int,
        default=DEFAULT_REFERENCE_FLOOR_PROBE_COUNT,
        help="Maximum reference floor polygons to sample for walkability comparison.",
    )
    zsi_collision_activation_parser.add_argument(
        "--reference-floor-query-above",
        type=float,
        default=DEFAULT_REFERENCE_FLOOR_QUERY_ABOVE,
        help="Y offset above each reference floor probe used for the converted raycast.",
    )
    zsi_collision_activation_parser.add_argument(
        "--reference-floor-tolerance",
        type=float,
        default=DEFAULT_REFERENCE_FLOOR_TOLERANCE,
        help="Maximum absolute floor Y delta accepted for each reference-derived probe.",
    )
    zsi_collision_activation_parser.add_argument(
        "--visual-room-dir",
        type=Path,
        default=None,
        help=(
            "Optional scene directory containing OOT3D room ZSI files. Allows the same "
            "diagnostic-only visual floor policy as audit-zsi-collision-gates."
        ),
    )
    zsi_collision_activation_parser.add_argument(
        "--visual-floor-probes",
        type=int,
        default=DEFAULT_VISUAL_MESH_FLOOR_PROBE_COUNT,
        help="Maximum OOT3D visual floor proxy polygons to sample for each diagnostic record.",
    )

    zsi_scene_audit_parser = subparsers.add_parser(
        "audit-zsi-scene-metadata",
        help="Write a RomFS scene-directory audit of ZSI setup, collision, camera, water, and room metadata.",
    )
    zsi_scene_audit_parser.add_argument(
        "scene_root",
        type=Path,
        help="Directory containing OOT3D scene files, for example work/extract/romfs/scene.",
    )
    zsi_scene_audit_parser.add_argument("--output", type=Path, required=True)
    zsi_scene_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    zsi_scene_audit_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-ZSI records.",
    )

    zsi_scene_index_parser = subparsers.add_parser(
        "export-zsi-scene-index",
        help="Export native OOT3D scene/room indices analogous to N64 scene source tables.",
    )
    zsi_scene_index_parser.add_argument(
        "scene_root",
        type=Path,
        help="Directory containing OOT3D scene files, for example work/extract/romfs/scene.",
    )
    zsi_scene_index_parser.add_argument("--output", type=Path, required=True)
    zsi_scene_index_parser.add_argument(
        "--markdown-output",
        type=Path,
        default=None,
        help="Optional human-readable Markdown summary path.",
    )
    zsi_scene_index_parser.add_argument(
        "--c-output-dir",
        type=Path,
        default=None,
        help="Optional directory for generated C-like scene index sources. Requires --full-entries.",
    )
    zsi_scene_index_parser.add_argument(
        "--scene",
        dest="scenes",
        action="append",
        default=[],
        help="Scene stem to export, for example link or spot04. Repeat for multiple stems. Omit for all scenes.",
    )
    zsi_scene_index_parser.add_argument(
        "--actor-object-semantics",
        type=Path,
        default=None,
        help="Optional OOT3D actor/object enum header for semantic names.",
    )
    zsi_scene_index_parser.add_argument(
        "--sample-limit",
        type=int,
        default=20,
        help="Maximum entries to keep in sampled command/room tables unless --full-entries is set.",
    )
    zsi_scene_index_parser.add_argument(
        "--full-entries",
        action="store_true",
        help="Keep full decoded entry lists instead of samples.",
    )
    zsi_scene_index_parser.add_argument(
        "--no-room-mesh-summaries",
        action="store_true",
        help="Skip embedded CMB summary parsing for faster RomFS-wide index runs.",
    )

    room_compilation_parser = subparsers.add_parser(
        "compile-room-unit",
        help="Compile one semantic route into a native OOT3D room/scene execution unit.",
    )
    room_compilation_parser.add_argument("--scene-root", type=Path, required=True)
    room_compilation_parser.add_argument("--code-bin", type=Path, required=True)
    room_compilation_parser.add_argument(
        "--semantic-route-catalog", type=Path, required=True
    )
    room_compilation_parser.add_argument("--asset-catalog", type=Path, required=True)
    room_compilation_parser.add_argument(
        "--scene-resource-table", type=Path, required=True
    )
    room_compilation_parser.add_argument(
        "--room-callback-contracts", type=Path, required=True
    )
    room_compilation_parser.add_argument(
        "--evidence-snapshot", type=Path, required=True
    )
    room_compilation_parser.add_argument("--route-id", required=True)
    room_compilation_parser.add_argument("--setup-index", type=int, default=None)
    room_compilation_parser.add_argument("--output", type=Path, required=True)
    room_compilation_parser.add_argument("--markdown-output", type=Path, default=None)

    room_compilation_validate_parser = subparsers.add_parser(
        "validate-room-unit",
        help="Validate structure, source identities and deterministic digest of a room unit.",
    )
    room_compilation_validate_parser.add_argument("input", type=Path)

    native_corpus_parser = subparsers.add_parser(
        "promote-native-corpus",
        help=(
            "Extract the C bodies reachable from a room unit from one pinned "
            "Zelda3drecomp commit."
        ),
    )
    native_corpus_parser.add_argument("--room-unit", type=Path, required=True)
    native_corpus_parser.add_argument(
        "--zelda3drecomp-root", type=Path, required=True
    )
    native_corpus_parser.add_argument(
        "--revision",
        default="HEAD",
        help=(
            "Git commit or ref for tracked evidence; an ignored generated corpus "
            "sidecar is accepted only when checkout HEAD matches it."
        ),
    )
    native_corpus_parser.add_argument("--output", type=Path, required=True)
    native_corpus_parser.add_argument(
        "--replace",
        action="store_true",
        help="Replace an existing output only when it has a matching promotion manifest.",
    )

    zsi_cutscene_audit_parser = subparsers.add_parser(
        "audit-zsi-cutscene-metadata",
        help="Write a RomFS scene-directory audit of OOT3D ZSI cutscene command data and strict N64 decode coverage.",
    )
    zsi_cutscene_audit_parser.add_argument(
        "scene_root",
        type=Path,
        help="Directory containing OOT3D scene files, for example work/extract/romfs/scene.",
    )
    zsi_cutscene_audit_parser.add_argument("--output", type=Path, required=True)
    zsi_cutscene_audit_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    zsi_cutscene_audit_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-command records.",
    )

    zsi_cutscene_export_parser = subparsers.add_parser(
        "export-zsi-cutscene-cameras",
        help="Export strict N64-decodable OOT3D ZSI cutscene camera blocks as JSON outside the repo.",
    )
    zsi_cutscene_export_parser.add_argument(
        "scene_root",
        type=Path,
        help="Directory containing OOT3D scene files, for example work/extract/romfs/scene.",
    )
    zsi_cutscene_export_parser.add_argument("--output", type=Path, required=True)
    zsi_cutscene_export_parser.add_argument(
        "--sample-limit",
        type=int,
        default=100,
        help="Maximum number of sample records to include.",
    )
    zsi_cutscene_export_parser.add_argument(
        "--no-records",
        action="store_true",
        help="Write aggregate counts and samples, omitting per-cutscene export records.",
    )

    material_audit_parser = subparsers.add_parser(
        "audit-scene-materials",
        help="Write a JSON audit of OOT3D scene room materials, texture slots, and selected Shipwright texture paths.",
    )
    material_audit_parser.add_argument(
        "scene_dir",
        type=Path,
        help="Directory containing OOT3D scene files, for example work/extract/romfs/scene.",
    )
    material_audit_parser.add_argument("--scene", required=True, help="Scene stem, for example spot04.")
    material_audit_parser.add_argument("--output", type=Path, required=True)
    material_audit_parser.add_argument(
        "--resource-prefix",
        default=None,
        help="Generated resource root prefix. Defaults to scenes/overworld/<scene>/oot3d.",
    )
    material_audit_parser.add_argument(
        "--shipwright-root",
        default=None,
        help="Shipwright scene root used for generated material resource path prediction.",
    )

    pack_parser = subparsers.add_parser(
        "pack-scene-mod",
        help="Pack a converted scene manifest into a Shipwright .o2r mod archive.",
    )
    pack_parser.add_argument("scene_manifest", type=Path)
    pack_parser.add_argument("--output", type=Path, required=True, help="Output .o2r path.")
    pack_parser.add_argument("--name", default="OOT3D Kokiri Forest")
    pack_parser.add_argument("--author", default="local")
    pack_parser.add_argument("--version", default="0.1.0")

    package_audit_parser = subparsers.add_parser(
        "audit-scene-package",
        help="Verify that a packed scene O2R material/mesh resources match the scene material audit.",
    )
    package_audit_parser.add_argument("scene_manifest", type=Path)
    package_audit_parser.add_argument("material_audit", type=Path)
    package_audit_parser.add_argument("archive", type=Path)
    package_audit_parser.add_argument("--output", type=Path, required=True)

    args = parser.parse_args(argv)
    try:
        if args.command == "inspect":
            print(json.dumps(inspect_input(args.input), indent=2))
            return 0
        if args.command == "export-player-animation-group-native-contract":
            contract = build_player_animation_group_native_contract(args.code_bin, args.actor_zar)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(contract, indent=2) + "\n", encoding="utf-8")
            print(str(args.output))
            return 0
        if args.command == "export-player-collision-action-native-contract":
            contract = build_player_collision_action_native_contract(args.code_bin)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(contract, indent=2) + "\n", encoding="utf-8")
            print(str(args.output))
            return 0
        if args.command == "inventory-romfs":
            inventory_romfs(
                args.romfs_root,
                args.output,
                deep=not args.shallow,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "audit-extraction-classification":
            audit_extraction_classification(
                args.extraction_root,
                args.output,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "audit-asset-replacement-ledger":
            ledger = audit_asset_replacement_ledger(
                args.work_root,
                args.output,
                decomp_support_root=args.decomp_support_root,
                code_bin_path=args.code_bin,
            )
            print(str(args.output))
            print(json.dumps({"record_count": ledger.get("record_count"), "status_counts": ledger.get("status_counts")}, indent=2))
            return 0
        if args.command == "inventory-material-stages":
            inventory_material_stages(
                args.romfs_root,
                args.output,
                sample_limit=args.sample_limit,
                include_model_records=not args.no_model_records,
                model_limit=args.model_limit,
            )
            print(str(args.output))
            return 0
        if args.command == "audit-media-assets":
            audit_media_assets(
                args.romfs_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "audit-audio-assets":
            audit_audio_assets(
                args.romfs_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "audit-moflex-movies":
            audit_moflex_movies(
                args.romfs_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "audit-q-format-assets":
            audit_q_format_assets(
                args.romfs_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "audit-ctxb-textures":
            audit_ctxb_textures(
                args.romfs_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "export-ctxb-textures":
            manifest = export_ctxb_textures(
                args.romfs_root,
                args.output,
                resource_prefix=args.resource_prefix,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output / "ctxb_texture_export_manifest.json"))
            print(json.dumps(manifest.get("resource_audit_summary", {}), indent=2))
            return 0
        if args.command == "audit-kankyo-environment-assets":
            audit_kankyo_environment_assets(
                args.kankyo_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "export-kankyo-environment-assets":
            manifest = export_kankyo_environment_assets(
                args.kankyo_root,
                args.output,
                resource_prefix=args.resource_prefix,
                include_textures=not args.no_textures,
                sample_limit=args.sample_limit,
            )
            print(str(args.output / "kankyo_environment_export_manifest.json"))
            print(json.dumps(manifest.get("resource_audit_summary", {}), indent=2))
            return 0
        if args.command == "pack-kankyo-environment-export":
            archive = pack_kankyo_environment_export_manifest(
                args.export_manifest,
                args.output,
                name=args.name,
                author=args.author,
                version=args.version,
            )
            print(str(archive))
            return 0
        if args.command == "audit-kankyo-environment-export-package":
            audit = audit_kankyo_environment_export_package(
                args.export_manifest,
                args.archive,
                args.output,
                sample_limit=args.sample_limit,
            )
            print(json.dumps(audit.get("issue_counts", {}), indent=2))
            return 0
        if args.command == "audit-kokiri-runtime-route":
            audit = audit_kokiri_runtime_route(
                args.romfs_root,
                args.output,
                route_stems=tuple(args.route_stems or ("link", "spot04")),
                actor_object_semantics=args.actor_object_semantics,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            print(json.dumps(audit.get("runtime_gap_counts", {}), indent=2))
            return 0
        if args.command == "export-kokiri-runtime-manifest":
            manifest = export_kokiri_runtime_manifest(
                args.runtime_route_audit,
                args.output,
                route_id=args.route_id,
                n64_rom=args.n64_rom,
                n64_decomp_reference=args.n64_decomp_reference,
                runtime_enablement_status=args.runtime_enablement_status,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(json.dumps(manifest.get("runtime_blocker_counts", {}), indent=2))
            return 0
        if args.command == "audit-kokiri-actor-asset-readiness":
            audit = audit_kokiri_actor_asset_readiness(
                args.runtime_manifest,
                args.actor_inventory,
                args.static_batch_manifest,
                args.skinned_binding_manifest,
                args.output,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(json.dumps(audit.get("asset_blocker_counts", {}), indent=2))
            return 0
        if args.command == "export-kokiri-static-actor-binding-plan":
            plan = export_kokiri_static_actor_binding_plan(
                args.runtime_manifest,
                args.actor_asset_readiness,
                args.static_batch_manifest,
                args.output,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(json.dumps(plan.get("actor_binding_status_counts", {}), indent=2))
            return 0
        if args.command == "export-kokiri-kankyo-binding-plan":
            plan = export_kokiri_kankyo_binding_plan(
                args.runtime_manifest,
                args.kankyo_export_manifest,
                args.output,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(json.dumps(plan.get("profile_binding_status_counts", {}), indent=2))
            return 0
        if args.command == "export-kokiri-audio-binding-plan":
            plan = export_kokiri_audio_binding_plan(
                args.runtime_manifest,
                args.audio_asset_audit,
                args.output,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(json.dumps(plan.get("profile_binding_status_counts", {}), indent=2))
            return 0
        if args.command == "audit-prerendered-room-replacements":
            audit_prerendered_room_replacements(
                args.asset_xml_root,
                args.scene_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "export-prerendered-room-replacements":
            manifest = export_prerendered_room_replacements(
                args.asset_xml_root,
                args.scene_root,
                args.output,
                resource_prefix=args.resource_prefix,
                include_textures=not args.no_textures,
                sample_limit=args.sample_limit,
            )
            print(str(args.output / "prerendered_room_export_manifest.json"))
            print(json.dumps(manifest.get("resource_audit_summary", {}), indent=2))
            return 0
        if args.command == "pack-prerendered-room-export":
            archive = pack_prerendered_room_export_manifest(
                args.export_manifest,
                args.output,
                name=args.name,
                author=args.author,
                version=args.version,
            )
            print(str(archive))
            return 0
        if args.command == "audit-prerendered-room-export-package":
            audit = audit_prerendered_room_export_package(
                args.export_manifest,
                args.archive,
                args.output,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(json.dumps(audit.get("issue_counts", {}), indent=2))
            return 0
        if args.command == "inventory-actors":
            inventory_actors(
                args.actor_root,
                args.output,
                include_records=not args.no_records,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            return 0
        if args.command == "audit-actor-qdb-payloads":
            audit_actor_qdb_payloads(
                args.actor_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "audit-actor-ccb-payloads":
            audit_actor_ccb_payloads(
                args.actor_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "audit-actor-zsi-payloads":
            audit_actor_zsi_payloads(
                args.actor_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "audit-actor-material-lighting":
            audit = audit_actor_material_lighting(
                args.actor_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            print(json.dumps(audit.get("scope_counts", {}), indent=2, default=list))
            return 0
        if args.command == "export-csab-rigid-tracks":
            export_csab_rigid_tracks(
                args.zar,
                args.csab_name,
                args.cmb_name,
                args.output,
            )
            print(str(args.output))
            return 0
        if args.command == "export-csab-skeleton-tracks":
            export_csab_skeleton_tracks(
                args.zar,
                args.csab_name,
                args.cmb_name,
                args.output,
            )
            print(str(args.output))
            return 0
        if args.command == "batch-csab-rigid-tracks":
            manifest = batch_export_csab_rigid_tracks(args.actor_root, args.output)
            print(str(manifest))
            return 0
        if args.command == "batch-csab-skeleton-tracks":
            manifest = batch_export_csab_skeleton_tracks(args.actor_root, args.output)
            print(str(manifest))
            return 0
        if args.command == "pack-csab-track-batch":
            archive = pack_csab_track_batch_manifest(
                args.batch_manifest,
                args.output,
                name=args.name,
                author=args.author,
                version=args.version,
                archive_prefix=args.archive_prefix,
            )
            print(str(archive))
            return 0
        if args.command == "audit-csab-track-batch-package":
            audit = audit_csab_track_batch_package(
                args.batch_manifest,
                args.archive,
                args.output,
                archive_prefix=args.archive_prefix,
                sample_limit=args.sample_limit,
            )
            print(json.dumps(audit.get("issue_counts", {}), indent=2))
            return 0
        if args.command == "audit-csab-target-resolution":
            audit_csab_target_resolution(
                args.actor_root,
                args.output,
                include_resolved_records=args.include_resolved_records,
            )
            print(str(args.output))
            return 0
        if args.command == "audit-cmab-payloads":
            audit_cmab_payloads(
                args.actor_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "audit-actor-animation-like-payloads":
            audit_actor_animation_like_payloads(
                args.actor_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "export-anb-payload-batch":
            export = export_anb_payload_batch(
                args.primary_archive,
                args.output,
                duplicate_archive_path=args.duplicate_archive,
                csab_binding_manifest_path=args.csab_binding_manifest,
                csab_target_archive=args.csab_target_archive,
                csab_target_cmb=args.csab_target_cmb,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(
                json.dumps(
                    {
                        "payload_count": export.get("payload_count"),
                        "duplicate_payload_match_count": export.get("duplicate_payload_match_count"),
                        "duplicate_payload_missing_count": export.get("duplicate_payload_missing_count"),
                        "duplicate_payload_mismatch_count": export.get("duplicate_payload_mismatch_count"),
                        "invalid_header_count": export.get("invalid_header_count"),
                        "issue_count": export.get("issue_count"),
                    },
                    indent=2,
                )
            )
            return 0 if export.get("issue_count") == 0 else 1
        if args.command == "audit-anb-semantic-candidates":
            audit = audit_anb_semantic_candidates(
                args.anb_export,
                args.character_manifest,
                args.output,
                track_root=args.track_root,
                min_abs_correlation=args.min_abs_correlation,
                min_consensus_count=args.min_consensus_count,
                sample_limit=args.sample_limit,
                unresolved_channel_csv_output_path=args.unresolved_channel_csv_output,
                include_frame_mismatch_resample=args.include_frame_mismatch_resample,
            )
            print(str(args.output))
            print(
                json.dumps(
                    {
                        "compared_record_count": audit.get("compared_record_count"),
                        "candidate_channel_count": audit.get("candidate_channel_count"),
                        "strong_candidate_count": audit.get("strong_candidate_count"),
                        "stable_candidate_channel_count": audit.get("stable_candidate_channel_count"),
                        "status_counts": audit.get("status_counts"),
                    },
                    indent=2,
                )
            )
            return 0
        if args.command == "audit-actor-skinning-queue":
            audit_actor_skinning_queue(
                args.actor_root,
                args.output,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            return 0
        if args.command == "audit-actor-skinning-layout":
            audit_actor_skinning_vertex_layout(
                args.actor_root,
                args.output,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            return 0
        if args.command == "export-skinned-bind-pose":
            export_skinned_bind_pose(
                args.input,
                args.output,
                cmb_index=args.cmb_index,
                cmb_name=args.cmb_name,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            return 0
        if args.command == "export-skinned-bind-pose-native":
            export_skinned_bind_pose_native(
                args.input,
                args.output_dir,
                resource_root=args.resource_root,
                symbol=args.symbol,
                cmb_index=args.cmb_index,
                cmb_name=args.cmb_name,
                sample_limit=args.sample_limit,
                selection_manifest=args.selection_manifest,
                texture_orientation=args.texture_orientation,
                uv_orientation=args.uv_orientation,
            )
            print(str(args.output_dir / "native_bind_pose_manifest.json"))
            return 0
        if args.command == "export-static-base-glb":
            manifest = export_static_base_glb(
                args.input,
                GlbOptions(
                    output=args.output,
                    manifest_output=args.manifest_output,
                    selection_manifest=args.selection_manifest,
                    cmb_index=args.cmb_index,
                    cmb_name=args.cmb_name,
                    draw_profile=args.draw_profile,
                    texture_orientation=args.texture_orientation,
                    uv_orientation=args.uv_orientation,
                    position_scale=args.position_scale,
                    sample_limit=args.sample_limit,
                ),
            )
            print(str(args.manifest_output or args.output.with_suffix(".manifest.json")))
            print(json.dumps(manifest.get("counts", {}), indent=2))
            return 0
        if args.command == "audit-character-visibility-reconstruction":
            audit = audit_character_visibility_reconstruction(
                args.input,
                args.output,
                selection_manifest=args.selection_manifest,
                cmb_index=args.cmb_index,
                cmb_name=args.cmb_name,
                n64_player_lib=args.n64_player_lib,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(json.dumps(audit.get("selection", {}), indent=2))
            return 0
        if args.command == "export-skinned-animation-glb":
            manifest = export_skinned_animation_glb(
                args.input,
                AnimationGlbOptions(
                    output=args.output,
                    manifest_output=args.manifest_output,
                    cmb_name=args.cmb_name,
                    csab_name=args.csab_name,
                    selection_manifest=args.selection_manifest,
                    texture_orientation=args.texture_orientation,
                    uv_orientation=args.uv_orientation,
                    position_scale=args.position_scale,
                    frame_step=args.frame_step,
                    fps=args.fps,
                    interpolation=args.interpolation,
                    sample_limit=args.sample_limit,
                ),
            )
            print(str(args.manifest_output or args.output.with_suffix(".manifest.json")))
            print(json.dumps(manifest.get("counts", {}), indent=2))
            return 0
        if args.command == "audit-minimal-runtime-player-export":
            audit = audit_minimal_runtime_player_export(
                args.character_manifest,
                MinimalRuntimePlayerExportOptions(
                    output=args.runtime_glb_output,
                    audit_output=args.output,
                    validated_glb_output=args.validated_glb_output,
                    validated_manifest_output=args.validated_manifest_output,
                    zar_path=args.zar,
                    cmb_name=args.cmb_name,
                    csab_name=args.csab_name,
                    selection_manifest=args.selection_manifest,
                    position_scale=args.position_scale,
                    frame_step=args.frame_step,
                    fps=args.fps,
                    interpolation=args.interpolation,
                    position_tolerance=args.position_tolerance,
                    normal_tolerance=args.normal_tolerance,
                    sample_limit=args.sample_limit,
                ),
            )
            print(str(args.output))
            print(json.dumps(audit.get("comparison", {}), indent=2))
            return 0 if audit.get("status") == "valid" else 1
        if args.command == "audit-minimal-runtime-track-export":
            audit = audit_minimal_runtime_track_export(
                args.character_manifest,
                args.track_export,
                MinimalRuntimeTrackExportOptions(
                    output=args.runtime_glb_output,
                    audit_output=args.output,
                    csab_name=args.csab_name,
                    position_scale=args.position_scale,
                    frame_step=args.frame_step,
                    fps=args.fps,
                    interpolation=args.interpolation,
                    sample_limit=args.sample_limit,
                ),
            )
            print(str(args.output))
            print(json.dumps(audit.get("comparison", {}), indent=2))
            return 0 if audit.get("status") == "valid" else 1
        if args.command == "audit-character-segment-continuity":
            audit = audit_character_segment_continuity(
                args.character_manifest,
                args.output,
                csab_names=args.csab_name,
                root_motion_bone=args.root_motion_bone,
                tolerance=args.tolerance,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(
                json.dumps(
                    {
                        "status": audit.get("status"),
                        "raw_discontinuity_count": audit.get("raw_discontinuity_count"),
                        "normalized_discontinuity_count": audit.get("normalized_discontinuity_count"),
                        "max_raw_transition_delta": audit.get("max_raw_transition_delta"),
                        "max_normalized_transition_delta": audit.get("max_normalized_transition_delta"),
                    },
                    indent=2,
                )
            )
            return 0 if audit.get("status") == "valid" else 1
        if args.command == "audit-runtime-draw-dump":
            audit = audit_runtime_draw_dump(
                args.character_manifest,
                args.runtime_dump,
                args.output,
                track_export_path=args.track_export,
                segment_continuity_audit_path=args.segment_continuity_audit,
                anb_semantic_audit_path=args.anb_semantic_audit,
                position_tolerance=args.position_tolerance,
                normal_tolerance=args.normal_tolerance,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(json.dumps(audit.get("comparison", {}), indent=2))
            return 0 if audit.get("status") == "valid" else 1
        if args.command == "audit-skinned-bind-pose-native":
            audit = audit_skinned_bind_pose_native_contract(
                args.bind_pose,
                args.native_manifest,
                args.output,
                position_tolerance=args.position_tolerance,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(json.dumps(audit.get("counts", {}), indent=2))
            return 0 if audit.get("status") == "valid" else 1
        if args.command == "batch-skinned-bind-poses":
            manifest = batch_export_actor_skinned_bind_poses(
                args.actor_root,
                args.output,
                sample_limit=args.sample_limit,
            )
            print(str(manifest))
            return 0
        if args.command == "pack-skinned-bind-pose-batch":
            archive = pack_skinned_bind_pose_batch_manifest(
                args.batch_manifest,
                args.output,
                name=args.name,
                author=args.author,
                version=args.version,
                archive_prefix=args.archive_prefix,
            )
            print(str(archive))
            return 0
        if args.command == "audit-skinned-bind-pose-batch-package":
            audit = audit_skinned_bind_pose_batch_package(
                args.batch_manifest,
                args.archive,
                args.output,
                archive_prefix=args.archive_prefix,
                sample_limit=args.sample_limit,
            )
            print(json.dumps(audit.get("issue_counts", {}), indent=2))
            return 0
        if args.command == "audit-skinned-animation-readiness":
            audit = audit_skinned_animation_readiness(
                args.bind_pose_manifest,
                args.csab_track_manifest,
                args.output,
                sample_limit=args.sample_limit,
            )
            print(json.dumps(audit.get("issue_counts", {}), indent=2))
            return 0
        if args.command == "export-skinned-animation-binding-manifest":
            manifest = export_skinned_animation_binding_manifest(
                args.bind_pose_manifest,
                args.csab_track_manifest,
                args.output,
                bind_pose_archive_prefix=args.bind_pose_archive_prefix,
                csab_track_archive_prefix=args.csab_track_archive_prefix,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(json.dumps(manifest.get("readiness_summary", {}).get("issue_counts", {}), indent=2))
            return 0
        if args.command == "export-skinned-animation-pose-samples":
            export_skinned_animation_pose_samples(
                args.zar,
                args.csab_name,
                args.cmb_name,
                args.output,
                sample_frames=parse_int_list(args.frames),
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            return 0
        if args.command == "batch-skinned-animation-pose-samples":
            manifest = batch_export_skinned_animation_pose_samples(
                args.actor_root,
                args.output,
                sample_limit=args.sample_limit,
            )
            print(str(manifest))
            return 0
        if args.command == "export-character-conversion-manifest":
            manifest = export_character_conversion_manifest(
                args.skinned_binding_manifest,
                args.animation_like_audit,
                args.cmab_audit,
                args.output,
                profile_id=args.profile_id,
                model_archive=args.model_archive,
                model_cmb=args.model_cmb,
                auxiliary_archives=args.auxiliary_archive,
                pose_batch_manifest_path=args.pose_batch_manifest,
                native_bind_pose_manifest_path=args.native_bind_pose_manifest,
                n64_reference_audit_path=args.n64_reference_audit,
                reference_label=args.reference_label,
                runtime_skinning_diagnostic_csab_names=args.runtime_skinning_diagnostic_csab,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(json.dumps(manifest.get("issue_counts", {}), indent=2))
            return 0
        if args.command == "pack-character-conversion":
            archive = pack_character_conversion_manifest(
                args.character_manifest,
                args.output,
                name=args.name,
                author=args.author,
                version=args.version,
                runtime_profile_path=args.runtime_profile_path,
                segment_continuity_audit_path=args.segment_continuity_audit,
                anb_export_path=args.anb_export,
                anb_semantic_audit_path=args.anb_semantic_audit,
                runtime_semantics_path=args.runtime_semantics,
            )
            print(str(archive))
            return 0
        if args.command == "audit-character-conversion-package":
            audit = audit_character_conversion_package(
                args.character_manifest,
                args.archive,
                args.output,
                runtime_profile_path=args.runtime_profile_path,
                segment_continuity_audit_path=args.segment_continuity_audit,
                anb_export_path=args.anb_export,
                anb_semantic_audit_path=args.anb_semantic_audit,
                runtime_semantics_path=args.runtime_semantics,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(json.dumps(audit.get("issue_counts", {}), indent=2))
            return 0
        if args.command == "audit-n64-animation-reference":
            audit = audit_n64_animation_reference(
                args.skinned_binding_manifest,
                args.n64_player_animation_xml,
                args.n64_link_object_xml,
                args.output,
                n64_player_animation_data_xml_path=args.n64_player_animation_data_xml,
                n64_base_o2r_path=args.n64_base_o2r,
                oot3d_pose_batch_manifest_path=args.oot3d_pose_batch_manifest,
                profile_id=args.profile_id,
                model_archive=args.model_archive,
                model_cmb=args.model_cmb,
                n64_skeleton_name=args.n64_skeleton_name,
                oot3d_strip_prefixes=args.oot3d_strip_prefix,
                n64_strip_prefixes=args.n64_strip_prefix,
                reference_version=args.reference_version,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(json.dumps(audit.get("blocker_counts", {}), indent=2))
            return 0
        if args.command == "audit-link-child-animation-semantic-mapping":
            audit = audit_link_child_animation_semantic_mapping(
                args.n64_reference_audit,
                args.n64_player_animation_xml,
                args.output,
                n64_player_animation_data_xml_path=args.n64_player_animation_data_xml,
                csv_output_path=args.csv_output,
                candidate_validation_csv_output_path=args.candidate_validation_csv_output,
                candidate_pose_metric_csv_output_path=args.candidate_pose_metric_csv_output,
                semantic_alias_promotion_csv_output_path=args.semantic_alias_promotion_csv_output,
                semantic_alias_route_review_csv_output_path=args.semantic_alias_route_review_csv_output,
                semantic_alias_resample_review_csv_output_path=args.semantic_alias_resample_review_csv_output,
                semantic_alias_temporal_bake_review_csv_output_path=args.semantic_alias_temporal_bake_review_csv_output,
                semantic_alias_bake_contract_csv_output_path=args.semantic_alias_bake_contract_csv_output,
                semantic_alias_ambiguity_arbitration_csv_output_path=args.semantic_alias_ambiguity_arbitration_csv_output,
                semantic_alias_accepted_overlay_csv_output_path=args.semantic_alias_accepted_overlay_csv_output,
                unresolved_near_candidate_csv_output_path=args.unresolved_near_candidate_csv_output,
                unresolved_near_candidate_pose_metric_csv_output_path=args.unresolved_near_candidate_pose_metric_csv_output,
                unresolved_near_candidate_bake_contract_csv_output_path=args.unresolved_near_candidate_bake_contract_csv_output,
                anonymous_numeric_candidate_csv_output_path=args.anonymous_numeric_candidate_csv_output,
                anonymous_numeric_pose_metric_csv_output_path=args.anonymous_numeric_pose_metric_csv_output,
                semantic_resolution_contract_csv_output_path=args.semantic_resolution_contract_csv_output,
                semantic_blocked_resolution_frontier_csv_output_path=args.semantic_blocked_resolution_frontier_csv_output,
                character_profile_o2r_path=args.character_profile_o2r,
                semantic_materialization_plan_output_path=args.semantic_materialization_plan_output,
                semantic_materialization_plan_csv_output_path=args.semantic_materialization_plan_csv_output,
                semantic_materialized_track_output_dir=args.semantic_materialized_track_output_dir,
                semantic_materialized_track_manifest_output_path=args.semantic_materialized_track_manifest_output,
                semantic_materialized_track_csv_output_path=args.semantic_materialized_track_csv_output,
                semantic_materialized_pose_metric_csv_output_path=args.semantic_materialized_pose_metric_csv_output,
                semantic_ownership_derivative_plan_output_path=args.semantic_ownership_derivative_plan_output,
                semantic_ownership_derivative_plan_csv_output_path=args.semantic_ownership_derivative_plan_csv_output,
                semantic_ownership_derivative_track_output_dir=args.semantic_ownership_derivative_track_output_dir,
                semantic_ownership_derivative_track_manifest_output_path=args.semantic_ownership_derivative_track_manifest_output,
                semantic_ownership_derivative_track_csv_output_path=args.semantic_ownership_derivative_track_csv_output,
                semantic_ownership_derivative_pose_metric_csv_output_path=args.semantic_ownership_derivative_pose_metric_csv_output,
                semantic_ownership_derivative_frontier_output_path=args.semantic_ownership_derivative_frontier_output,
                semantic_ownership_derivative_frontier_csv_output_path=args.semantic_ownership_derivative_frontier_csv_output,
                semantic_ownership_reuse_arbitration_output_path=args.semantic_ownership_reuse_arbitration_output,
                semantic_ownership_reuse_arbitration_csv_output_path=args.semantic_ownership_reuse_arbitration_csv_output,
                semantic_route_proven_ownership_callsite_output_path=args.semantic_route_proven_ownership_callsite_output,
                semantic_route_proven_ownership_callsite_csv_output_path=args.semantic_route_proven_ownership_callsite_csv_output,
                source_search_roots=args.source_search_root,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(json.dumps(audit.get("coverage", {}), indent=2))
            return 0
        if args.command == "build-link-child-anonymous-numeric-diagnostic-tracks":
            summary = build_link_child_anonymous_numeric_diagnostic_tracks(
                args.semantic_blocked_resolution_frontier_csv,
                args.character_profile_o2r,
                args.output_dir,
                plan_output_path=args.plan_output,
                plan_csv_output_path=args.plan_csv_output,
                track_manifest_output_path=args.track_manifest_output,
                track_csv_output_path=args.track_csv_output,
                summary_output_path=args.summary_output,
                sample_limit=args.sample_limit,
            )
            print(str(args.summary_output))
            print(json.dumps({"status": summary.get("status"), "counts": summary.get("counts", {})}, indent=2))
            return 0
        if args.command == "build-link-child-pose-source-risk-diagnostic-tracks":
            summary = build_link_child_pose_source_risk_diagnostic_tracks(
                args.semantic_blocked_resolution_frontier_csv,
                args.character_profile_o2r,
                args.output_dir,
                plan_output_path=args.plan_output,
                plan_csv_output_path=args.plan_csv_output,
                track_manifest_output_path=args.track_manifest_output,
                track_csv_output_path=args.track_csv_output,
                summary_output_path=args.summary_output,
                sample_limit=args.sample_limit,
            )
            print(str(args.summary_output))
            print(json.dumps({"status": summary.get("status"), "counts": summary.get("counts", {})}, indent=2))
            return 0
        if args.command == "build-link-child-route-runtime-diagnostic-tracks":
            summary = build_link_child_route_runtime_diagnostic_tracks(
                args.semantic_blocked_resolution_frontier_csv,
                args.character_profile_o2r,
                args.output_dir,
                plan_output_path=args.plan_output,
                plan_csv_output_path=args.plan_csv_output,
                track_manifest_output_path=args.track_manifest_output,
                track_csv_output_path=args.track_csv_output,
                summary_output_path=args.summary_output,
                sample_limit=args.sample_limit,
            )
            print(str(args.summary_output))
            print(json.dumps({"status": summary.get("status"), "counts": summary.get("counts", {})}, indent=2))
            return 0
        if args.command == "audit-link-child-pose-source-callsite-context":
            audit = audit_link_child_pose_source_callsite_context(
                args.semantic_resolution_contract_csv,
                args.output,
                csv_output_path=args.csv_output,
                source_search_roots=args.source_search_root,
                sample_limit=args.sample_limit,
            )
            print(str(args.output))
            print(
                json.dumps(
                    {
                        "status": audit.get("status"),
                        "row_count": audit.get("row_count"),
                        "callsite_review_class_counts": audit.get("callsite_review_class_counts", {}),
                    },
                    indent=2,
                )
            )
            return 0
        if args.command == "convert-static":
            model = load_model_from_input(args.input, args.cmb_index, args.cmb_name)
            resource_root = args.resource_root or f"objects/oot3d/{sanitize_path_part(args.input.stem)}"
            symbol = args.symbol or f"gOot3d{to_pascal_case(model.name or args.input.stem)}"
            result = export_static_model(
                model,
                LegacyFastResourceOptions(
                    output_dir=args.output,
                    resource_root=resource_root,
                    symbol=symbol,
                    include_textures=not args.no_textures,
                    allow_nonstatic=args.allow_nonstatic,
                    texture_orientation=args.texture_orientation,
                    uv_orientation=args.uv_orientation,
                ),
            )
            print(str(result.manifest_path))
            return 0
        if args.command == "batch-static":
            manifest = batch_static(args)
            print(str(manifest))
            return 0
        if args.command == "audit-static-batch":
            audit_static_batch(args.batch_manifest, args.output)
            print(str(args.output))
            return 0
        if args.command == "pack-static-batch":
            archive = pack_static_batch_manifest(
                args.batch_manifest,
                args.output,
                name=args.name,
                author=args.author,
                version=args.version,
            )
            print(str(archive))
            return 0
        if args.command == "audit-static-batch-package":
            audit = audit_static_batch_package(
                args.batch_manifest,
                args.archive,
                args.output,
                sample_limit=args.sample_limit,
            )
            print(json.dumps(audit.get("issue_counts", {}), indent=2))
            return 0
        if args.command == "convert-scene":
            manifest = convert_scene(args)
            print(str(manifest))
            return 0
        if args.command == "export-zsi-collision":
            resource_path = zsi_collision_resource_path_for_cli(
                args.input,
                args.resource_path,
                args.reference_o2r,
            )
            visual_models = None
            if args.visual_room_dir is not None:
                visual_scene = args.visual_scene or scene_stem_from_zsi_path(args.input)
                visual_models = load_visual_room_models(args.visual_room_dir, visual_scene)
            result = export_zsi_scene_collision(
                args.input,
                args.output,
                resource_path,
                candidate_index=args.candidate_index,
                resource_format=args.format,
                static_lookup_node_budget=args.collision_node_budget,
                static_lookup_filter_policy=args.collision_budget_policy,
                reference_o2r=args.reference_o2r,
                reference_resource_path=args.reference_resource_path,
                reference_floor_probe_count=args.reference_floor_probes,
                reference_floor_query_above=args.reference_floor_query_above,
                reference_floor_tolerance=args.reference_floor_tolerance,
                require_reference_match=args.require_reference_match,
                visual_models=visual_models,
                visual_floor_probe_count=args.visual_floor_probes,
            )
            print(json.dumps({"resource": result.resource.__dict__, "stats": result.stats}, indent=2))
            return 0
        if args.command == "audit-zsi-collision-gates":
            audit = audit_zsi_collision_gates(
                args.scene_root,
                args.reference_o2r,
                args.output,
                scenes=tuple(args.scene),
                limit=args.limit,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
                resource_format=args.format,
                reference_floor_probe_count=args.reference_floor_probes,
                reference_floor_query_above=args.reference_floor_query_above,
                reference_floor_tolerance=args.reference_floor_tolerance,
                visual_room_dir=args.visual_room_dir,
                visual_floor_probe_count=args.visual_floor_probes,
            )
            print(json.dumps(audit.get("status_counts", {}), indent=2))
            return 0
        if args.command == "export-zsi-collision-activation-manifest":
            manifest = export_zsi_collision_activation_manifest(
                args.scene_root,
                args.reference_o2r,
                args.output,
                args.resource_output_dir,
                scenes=tuple(args.scene),
                limit=args.limit,
                sample_limit=args.sample_limit,
                resource_format=args.format,
                reference_floor_probe_count=args.reference_floor_probes,
                reference_floor_query_above=args.reference_floor_query_above,
                reference_floor_tolerance=args.reference_floor_tolerance,
                visual_room_dir=args.visual_room_dir,
                visual_floor_probe_count=args.visual_floor_probes,
            )
            print(
                json.dumps(
                    {
                        "accepted_scene_count": manifest["accepted_scene_count"],
                        "fallback_scene_count": manifest["fallback_scene_count"],
                    },
                    indent=2,
                )
            )
            return 0
        if args.command == "audit-zsi-scene-metadata":
            audit_zsi_scene_metadata(
                args.scene_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "export-zsi-scene-index":
            export_zsi_scene_index(
                args.scene_root,
                args.output,
                markdown_output_path=args.markdown_output,
                c_output_dir=args.c_output_dir,
                scene_stems=args.scenes or None,
                actor_object_semantics=args.actor_object_semantics,
                sample_limit=args.sample_limit,
                full_entries=args.full_entries,
                include_room_mesh_summaries=not args.no_room_mesh_summaries,
            )
            print(str(args.output))
            if args.markdown_output is not None:
                print(str(args.markdown_output))
            if args.c_output_dir is not None:
                print(str(args.c_output_dir))
            return 0
        if args.command == "compile-room-unit":
            unit = compile_room_compilation_unit(
                scene_root=args.scene_root,
                code_bin=args.code_bin,
                semantic_route_catalog_path=args.semantic_route_catalog,
                asset_catalog_path=args.asset_catalog,
                scene_resource_table_path=args.scene_resource_table,
                room_callback_contracts_path=args.room_callback_contracts,
                evidence_snapshot_root=args.evidence_snapshot,
                route_id=args.route_id,
                setup_index=args.setup_index,
            )
            write_room_compilation_unit(unit, args.output)
            if args.markdown_output is not None:
                write_room_compilation_markdown(unit, args.markdown_output)
            print(
                json.dumps(
                    {
                        "output": str(args.output),
                        "status": unit["status"],
                        "payload_sha256": unit["identity"]["payload_sha256"],
                        "closure": unit["closure"],
                    },
                    indent=2,
                )
            )
            return 0
        if args.command == "validate-room-unit":
            unit = json.loads(args.input.read_text(encoding="utf-8-sig"))
            validate_room_compilation_unit(unit)
            print(
                json.dumps(
                    {
                        "input": str(args.input),
                        "status": unit["status"],
                        "payload_sha256": unit["identity"]["payload_sha256"],
                    },
                    indent=2,
                )
            )
            return 0
        if args.command == "promote-native-corpus":
            package = promote_native_corpus(
                args.room_unit,
                args.zelda3drecomp_root,
                args.output,
                revision=args.revision,
                replace=args.replace,
            )
            print(
                json.dumps(
                    {
                        "output": str(args.output),
                        "payload_sha256": package.manifest["payload_sha256"],
                        "source_revision": package.manifest["source"]["revision"],
                        "counts": package.manifest["counts"],
                        "compile_readiness_counts": package.manifest[
                            "compile_readiness_counts"
                        ],
                    },
                    indent=2,
                )
            )
            return 0
        if args.command == "audit-zsi-cutscene-metadata":
            audit_zsi_cutscene_metadata(
                args.scene_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output))
            return 0
        if args.command == "export-zsi-cutscene-cameras":
            manifest = export_zsi_cutscene_camera_data(
                args.scene_root,
                args.output,
                sample_limit=args.sample_limit,
                include_records=not args.no_records,
            )
            print(str(args.output / "zsi_cutscene_camera_export_manifest.json"))
            print(json.dumps(manifest.get("issue_counts", {}), indent=2))
            return 0
        if args.command == "audit-scene-materials":
            audit_scene_materials(
                args.scene_dir,
                args.scene,
                args.output,
                shipwright_root=args.legacy_fast_resource_root,
                resource_prefix=args.resource_prefix,
            )
            print(str(args.output))
            return 0
        if args.command == "pack-scene-mod":
            archive = pack_scene_mod_manifest(
                args.scene_manifest,
                args.output,
                name=args.name,
                author=args.author,
                version=args.version,
            )
            print(str(archive))
            return 0
        if args.command == "audit-scene-package":
            audit_scene_package(
                args.scene_manifest,
                args.material_audit,
                args.archive,
                args.output,
            )
            print(str(args.output))
            return 0
    except (OSError, ParseError, ValueError) as exc:
        parser.exit(2, f"error: {exc}\n")
    return 1


def inspect_input(path: Path) -> dict[str, object]:
    suffix = path.suffix.lower()
    if suffix == ".cmb":
        return CmbModel.from_path(path).summary()
    if suffix == ".zar":
        return ZarArchive.from_path(path).summary()
    if suffix == ".zsi":
        return ZsiFile.from_path(path).summary()
    if suffix == ".ctxb":
        return parse_ctxb(path.read_bytes(), str(path)).summary()
    raise ParseError(f"{path}: expected .cmb, .zar, .zsi, or .ctxb input")


def load_model_from_input(path: Path, cmb_index: int, cmb_name: str | None) -> CmbModel:
    suffix = path.suffix.lower()
    if suffix == ".cmb":
        return CmbModel.from_path(path)
    if suffix == ".zsi":
        cmbs = ZsiFile.from_path(path).embedded_cmbs()
        if not cmbs:
            raise ParseError(f"{path}: ZSI file contains no embedded CMB")
        if cmb_name:
            for cmb in cmbs:
                if cmb.model.name == cmb_name:
                    return cmb.model
            raise ParseError(f"{path}: no embedded CMB named {cmb_name!r}")
        if cmb_index < 0 or cmb_index >= len(cmbs):
            raise ParseError(f"{path}: CMB index {cmb_index} out of range 0..{len(cmbs)-1}")
        return cmbs[cmb_index].model
    if suffix != ".zar":
        raise ParseError(f"{path}: expected .cmb, .zar, or .zsi input")

    archive = ZarArchive.from_path(path)
    cmb_files = archive.cmb_files()
    if not cmb_files:
        raise ParseError(f"{path}: ZAR archive contains no CMB files")

    selected = None
    if cmb_name:
        for file in cmb_files:
            if file.name == cmb_name or Path(file.name).name == cmb_name:
                selected = file
                break
        if selected is None:
            raise ParseError(f"{path}: no embedded CMB named {cmb_name!r}")
    else:
        if cmb_index < 0 or cmb_index >= len(cmb_files):
            raise ParseError(f"{path}: CMB index {cmb_index} out of range 0..{len(cmb_files)-1}")
        selected = cmb_files[cmb_index]

    return CmbModel.parse(archive.read_file(selected), f"{path}!{selected.name}")


def convert_scene(args: argparse.Namespace) -> Path:
    scene = sanitize_path_part(args.scene)
    output = args.output
    output.mkdir(parents=True, exist_ok=True)

    if (args.generate_collision or args.collision_path is not None) and not args.no_collision:
        raise ParseError(
            "convert-scene no longer derives CollisionHeader resources from visual CMB meshes. "
            "Export native OOT3D scene collision with export-zsi-collision instead."
        )

    shipwright_root = (args.legacy_fast_resource_root or f"scenes/overworld/{scene}").strip("/")
    resource_prefix = (args.resource_prefix or f"{shipwright_root}/oot3d").strip("/")

    records: list[dict[str, object]] = []
    considered = 0
    converted = 0
    skipped = 0
    failed = 0
    collision_models: list[tuple[int, CmbModel]] = []

    for room_index, zsi_path in discover_scene_room_zsis(args.scene_dir, scene):
        zsi = ZsiFile.from_path(zsi_path)
        cmbs = zsi.embedded_cmbs()
        if not cmbs:
            failed += 1
            records.append(
                {
                    "status": "failed",
                    "reason": "room_zsi_contains_no_cmb",
                    "room": room_index,
                    "source": str(zsi_path),
                }
            )
            continue

        for cmb in cmbs:
            considered += 1
            room_name = f"{scene}_room_{room_index}"
            asset_id = room_name if len(cmbs) == 1 else f"{room_name}_cmb_{cmb.index}"
            resource_root = f"{resource_prefix}/{asset_id}"
            symbol = f"gOot3d{to_pascal_case(scene)}Room{room_index}"
            if len(cmbs) != 1:
                symbol += f"Cmb{cmb.index}"

            record_base = {
                "room": room_index,
                "cmb_index": cmb.index,
                "cmb_offset": cmb.offset,
                "cmb_size": cmb.size,
                "source": cmb.model.source,
                "asset_id": asset_id,
                "shipwright_equivalent": f"{shipwright_root}/{room_name}",
                "resource_root": resource_root,
                "symbol": symbol,
                "top_display_list": f"{resource_root}/{symbol}",
                "summary": cmb.model.summary(),
            }

            if not args.allow_nonstatic and not cmb.model.is_rigid_export_candidate():
                skipped += 1
                records.append(
                    {
                        "status": "skipped",
                        "reason": "not_rigid_export_candidate",
                        **record_base,
                    }
                )
                continue

            try:
                result = export_static_model(
                    cmb.model,
                    LegacyFastResourceOptions(
                        output_dir=output / asset_id,
                        resource_root=resource_root,
                        symbol=symbol,
                        include_textures=not args.no_textures,
                        allow_nonstatic=args.allow_nonstatic,
                        texture_orientation=getattr(args, "texture_orientation", None),
                        uv_orientation=getattr(args, "uv_orientation", "normal"),
                    ),
                )
            except Exception as exc:
                failed += 1
                records.append(
                    {
                        "status": "failed",
                        "reason": str(exc),
                        **record_base,
                    }
                )
                continue

            converted += 1
            collision_models.append((room_index, cmb.model))
            records.append(
                {
                    "status": "converted",
                    "manifest": str(result.manifest_path),
                    "resources": [resource.__dict__ for resource in result.resources],
                    **record_base,
                }
            )

    collision_record: dict[str, object] | None = None
    generate_collision = (args.generate_collision or args.collision_path is not None) and not args.no_collision
    collision_path = args.collision_path
    if generate_collision and not collision_path:
        raise ParseError("--generate-collision requires --collision-path for legacy visual-mesh collision export")
    if generate_collision and collision_path and collision_models:
        metadata = CollisionMetadata()
        metadata_source = None
        if args.base_o2r:
            try:
                metadata = load_collision_metadata_from_o2r(args.base_o2r, collision_path)
                metadata_source = str(args.base_o2r)
            except (OSError, KeyError, zipfile.BadZipFile, ParseError) as exc:
                metadata_source = f"unavailable: {exc}"
        try:
            collision = export_scene_collision(
                collision_models,
                output / "collision",
                collision_path,
                metadata=metadata,
            )
            collision_record = {
                "status": "converted",
                "kind": "CollisionHeader",
                "resource_path": collision_path,
                "metadata_source": metadata_source,
                "summary": collision.stats,
                "resources": [collision.resource.__dict__],
            }
            records.append(collision_record)
        except Exception as exc:
            failed += 1
            records.append(
                {
                    "status": "failed",
                    "kind": "CollisionHeader",
                    "resource_path": collision_path,
                    "reason": str(exc),
                }
            )

    manifest = {
        "scene": scene,
        "scene_dir": str(args.scene_dir),
        "output": str(output),
        "shipwright_root": shipwright_root,
        "resource_prefix": resource_prefix,
        "considered": considered,
        "converted": converted,
        "skipped": skipped,
        "failed": failed,
        "collision": collision_record,
        "assignment_notes": [
            "Generated resources are DisplayList/Vertex/Texture assets.",
            "shipwright_equivalent names the Room resource that this generated room model is intended to replace or be assigned to.",
            "Room resource binary rewriting is not generated by this prototype.",
        ],
        "records": records,
    }
    manifest_path = output / "scene_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    return manifest_path


def pack_scene_mod_manifest(
    scene_manifest_path: Path,
    output_path: Path,
    *,
    name: str,
    author: str,
    version: str,
) -> Path:
    if output_path.suffix.lower() != ".o2r":
        raise ValueError(f"{output_path}: output path must end in .o2r")

    scene_manifest = json.loads(scene_manifest_path.read_text(encoding="utf-8-sig"))
    output_path.parent.mkdir(parents=True, exist_ok=True)

    root_manifest = {
        "name": name,
        "author": author,
        "version": version,
        "description": "OOT3D scene replacement resources for Shipwright testing.",
        "license": "Generated from user-provided local assets; do not redistribute without rights.",
        "code_version": 1,
    }

    written_paths: set[str] = set()
    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        write_deterministic_zip_entry(
            archive,
            "manifest.json",
            json.dumps(root_manifest, indent=2) + "\n",
        )
        write_deterministic_zip_entry(
            archive,
            "oot3d_scene_manifest.json",
            json.dumps(scene_manifest, indent=2) + "\n",
        )
        written_paths.update({"manifest.json", "oot3d_scene_manifest.json"})

        for record in scene_manifest.get("records", []):
            if record.get("status") != "converted":
                continue
            for resource in record.get("resources", []):
                archive_path = str(resource["path"]).replace("\\", "/").strip("/")
                if archive_path in written_paths:
                    continue

                source_path = Path(resource["file"])
                if not source_path.exists() and not source_path.is_absolute():
                    source_path = scene_manifest_path.parent / source_path
                if not source_path.exists():
                    raise ValueError(f"missing generated resource for {archive_path}: {resource['file']}")

                write_deterministic_zip_entry(archive, archive_path, source_path.read_bytes())
                written_paths.add(archive_path)

    return output_path


def write_deterministic_zip_entry(
    archive: zipfile.ZipFile,
    archive_path: str,
    data: str | bytes,
) -> None:
    entry = zipfile.ZipInfo(archive_path, DETERMINISTIC_ZIP_TIMESTAMP)
    entry.compress_type = zipfile.ZIP_DEFLATED
    entry.external_attr = 0o644 << 16
    archive.writestr(entry, data)


def discover_scene_room_zsis(scene_dir: Path, scene: str) -> list[tuple[int, Path]]:
    if not scene_dir.is_dir():
        raise ParseError(f"{scene_dir}: expected a scene directory")
    pattern = re.compile(rf"^{re.escape(scene)}_(\d+)_info\.zsi$", re.IGNORECASE)
    rooms: list[tuple[int, Path]] = []
    for path in scene_dir.glob(f"{scene}_*_info.zsi"):
        match = pattern.match(path.name)
        if match:
            rooms.append((int(match.group(1)), path))
    if not rooms:
        raise ParseError(f"{scene_dir}: no room ZSI files found for scene {scene!r}")
    return sorted(rooms, key=lambda item: item[0])


def scene_stem_from_zsi_path(path: Path) -> str:
    return zsi_scene_stem(path)


def zsi_collision_resource_path_for_cli(
    input_path: Path,
    resource_path: str | None,
    reference_o2r: Path | None,
) -> str:
    if resource_path:
        return resource_path
    if reference_o2r is None:
        raise ParseError("--resource-path is required when --reference-o2r is not provided")

    scene = scene_stem_from_zsi_path(input_path)
    discovered_path = discover_scene_collision_path_from_o2r(reference_o2r, scene)
    if discovered_path is None:
        raise ParseError(
            f"{reference_o2r}: could not discover a CollisionHeader resource for scene {scene!r}; "
            "pass --resource-path explicitly"
        )
    return discovered_path


def load_visual_room_models(scene_dir: Path, scene: str) -> list[tuple[int, CmbModel]]:
    models: list[tuple[int, CmbModel]] = []
    for room_index, zsi_path in discover_scene_room_zsis(scene_dir, scene):
        zsi = ZsiFile.from_path(zsi_path)
        for embedded in zsi.embedded_cmbs():
            models.append((room_index, embedded.model))
    if not models:
        raise ParseError(f"{scene_dir}: no embedded room CMBs found for scene {scene!r}")
    return models


def batch_static(args: argparse.Namespace) -> Path:
    output = args.output
    output.mkdir(parents=True, exist_ok=True)

    records: list[dict[str, object]] = []
    scanned = 0
    filtered = 0
    considered = 0
    converted = 0
    skipped = 0
    failed = 0
    parse_failed = 0
    only_rigid_multibone = bool(getattr(args, "only_rigid_multibone", False))

    for entry in iter_model_entries(args.input):
        if isinstance(entry, BatchParseError):
            parse_failed += 1
            records.append(
                {
                    "status": "parse_failed",
                    "asset_id": unique_asset_id(entry.asset_id, records),
                    "source": entry.source,
                    "reason": entry.reason,
                }
            )
            continue

        asset = entry
        if only_rigid_multibone and not is_rigid_multibone_candidate(asset.model):
            filtered += 1
            continue
        if args.limit is not None and considered >= args.limit:
            break
        scanned += 1
        considered += 1

        asset_id = unique_asset_id(asset.asset_id, records)
        resource_root = f"{args.resource_prefix.strip('/')}/{asset_id}"
        symbol = f"gOot3d{to_pascal_case(asset.model.name or asset_id)}"

        if not args.allow_nonstatic and not asset.model.is_rigid_export_candidate():
            skipped += 1
            records.append(
                {
                    "status": "skipped",
                    "reason": "not_rigid_export_candidate",
                    "asset_id": asset_id,
                    "source": asset.model.source,
                    "bone_count": asset.model.bone_count,
                    "summary": asset.model.summary(),
                }
            )
            continue

        try:
            result = export_static_model(
                asset.model,
                LegacyFastResourceOptions(
                    output_dir=output / asset_id,
                    resource_root=resource_root,
                    symbol=symbol,
                    include_textures=not args.no_textures,
                    allow_nonstatic=args.allow_nonstatic,
                    texture_orientation=getattr(args, "texture_orientation", None),
                    uv_orientation=getattr(args, "uv_orientation", "normal"),
                ),
            )
        except Exception as exc:
            failed += 1
            records.append(
                {
                    "status": "failed",
                    "asset_id": asset_id,
                    "source": asset.model.source,
                    "reason": str(exc),
                    "summary": asset.model.summary(),
                }
            )
            continue

        converted += 1
        records.append(
            {
                "status": "converted",
                "asset_id": asset_id,
                "source": asset.model.source,
                "manifest": str(result.manifest_path),
                "resource_root": resource_root,
                "symbol": symbol,
                "resource_count": len(result.resources),
                "resources": [resource.__dict__ for resource in result.resources],
                "summary": asset.model.summary(),
            }
        )

    manifest = {
        "input": str(args.input),
        "output": str(output),
        "resource_prefix": args.resource_prefix.strip("/"),
        "filter": "rigid_multibone" if only_rigid_multibone else "rigid_export_candidates",
        "scanned": scanned,
        "filtered": filtered,
        "considered": considered,
        "converted": converted,
        "skipped": skipped,
        "failed": failed,
        "parse_failed": parse_failed,
        "records": records,
    }
    manifest_path = output / "batch_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    return manifest_path


def is_rigid_multibone_candidate(model: CmbModel) -> bool:
    return model.bone_count > 1 and model.is_rigid_export_candidate()


class BatchAsset:
    def __init__(self, asset_id: str, model: CmbModel) -> None:
        self.asset_id = asset_id
        self.model = model


class BatchParseError:
    def __init__(self, asset_id: str, source: str, reason: str) -> None:
        self.asset_id = asset_id
        self.source = source
        self.reason = reason


def iter_models(input_path: Path) -> Iterator[BatchAsset]:
    for entry in iter_model_entries(input_path):
        if isinstance(entry, BatchParseError):
            raise ParseError(entry.reason)
        yield entry


def iter_model_entries(input_path: Path) -> Iterator[BatchAsset | BatchParseError]:
    paths: list[Path]
    if input_path.is_dir():
        paths = sorted(
            path for path in input_path.rglob("*") if path.suffix.lower() in {".cmb", ".zar", ".zsi"}
        )
        base = input_path
    else:
        paths = [input_path]
        base = input_path.parent

    for path in paths:
        suffix = path.suffix.lower()
        rel_id = sanitize_path_part("_".join(path.relative_to(base).with_suffix("").parts))
        if suffix == ".cmb":
            try:
                yield BatchAsset(rel_id, CmbModel.from_path(path))
            except (OSError, ParseError) as exc:
                yield BatchParseError(rel_id, str(path), str(exc))
        elif suffix == ".zar":
            try:
                archive = ZarArchive.from_path(path)
            except (OSError, ParseError) as exc:
                yield BatchParseError(rel_id, str(path), str(exc))
                continue
            for cmb_file in archive.cmb_files():
                embedded_id = sanitize_path_part(Path(cmb_file.name).with_suffix("").as_posix())
                asset_id = sanitize_path_part(f"{rel_id}_{embedded_id}")
                source = f"{path}!{cmb_file.name}"
                try:
                    yield BatchAsset(
                        asset_id,
                        CmbModel.parse(archive.read_file(cmb_file), source),
                    )
                except (OSError, ParseError) as exc:
                    yield BatchParseError(asset_id, source, str(exc))
        elif suffix == ".zsi":
            try:
                zsi = ZsiFile.from_path(path)
                cmbs = zsi.embedded_cmbs()
            except (OSError, ParseError) as exc:
                yield BatchParseError(rel_id, str(path), str(exc))
                continue
            for cmb in cmbs:
                embedded_id = sanitize_path_part(cmb.model.name or f"cmb_{cmb.index}")
                asset_id = sanitize_path_part(f"{rel_id}_{embedded_id}")
                yield BatchAsset(asset_id, cmb.model)


def unique_asset_id(asset_id: str, records: list[dict[str, object]]) -> str:
    used = {str(record.get("asset_id")) for record in records}
    if asset_id not in used:
        return asset_id
    index = 1
    while f"{asset_id}_{index}" in used:
        index += 1
    return f"{asset_id}_{index}"


def parse_int_list(value: str | None) -> list[int] | None:
    if value is None or not value.strip():
        return None
    items: list[int] = []
    for part in value.split(","):
        part = part.strip()
        if part:
            items.append(int(part, 10))
    return items


def to_pascal_case(value: str) -> str:
    parts = [part for part in sanitize_path_part(value).split("_") if part]
    return "".join(part[:1].upper() + part[1:] for part in parts) or "Model"


def sanitize_path_part(value: str) -> str:
    chars = []
    for char in value:
        if char.isalnum():
            chars.append(char)
        else:
            chars.append("_")
    return "_".join(part for part in "".join(chars).split("_") if part).lower()
