from __future__ import annotations

import json
import os
import struct
import tempfile
import unittest
import zipfile
from dataclasses import replace
from pathlib import Path
from types import SimpleNamespace

from oot3d_asset_tool.actor_inventory import inventory_actors
from oot3d_asset_tool.actor_ccb_audit import audit_actor_ccb_payloads
from oot3d_asset_tool.actor_qdb_audit import audit_actor_qdb_payloads
from oot3d_asset_tool.actor_zsi_audit import audit_actor_zsi_payloads
from oot3d_asset_tool.audio_asset_audit import audit_audio_assets
from oot3d_asset_tool.animation_like_audit import (
    audit_actor_animation_like_payloads,
)
from oot3d_asset_tool.binary import BinaryView, ParseError
from oot3d_asset_tool.collision_gate_audit import (
    audit_zsi_collision_gates,
    export_zsi_collision_activation_manifest,
)
from oot3d_asset_tool.cli import (
    batch_static,
    convert_scene,
    discover_scene_room_zsis,
    pack_scene_mod_manifest,
    zsi_collision_resource_path_for_cli,
)
from oot3d_asset_tool.collision import (
    CameraData,
    CameraPositionData,
    CollisionMetadata,
    CollisionPolygon,
    CollisionScene,
    SurfaceType,
    WaterBox,
    build_scene_collision,
    collision_header_xml,
    collision_visual_diagnostic_policy,
    compare_collision_scenes,
    decode_collision_polygon_flags,
    decode_surface_type,
    discover_scene_collision_path_from_o2r,
    export_zsi_scene_collision,
    load_collision_metadata_from_o2r,
    load_collision_scene_from_o2r,
    native_zsi_collision_acceptance_gate,
    visual_mesh_collision_floor_audit,
    visual_mesh_source_spawn_floor_audit,
    visual_mesh_floor_diagnosis,
    write_collision_resource,
    zsi_collision_candidate_to_shipwright,
)
from oot3d_asset_tool.ctxb import audit_ctxb_textures, parse_ctxb
from oot3d_asset_tool.ctxb_export import export_ctxb_textures
from oot3d_asset_tool.csab_track_package import (
    audit_csab_track_batch_package,
    pack_csab_track_batch_manifest,
)
from oot3d_asset_tool.csab_tracks import (
    batch_export_csab_rigid_tracks,
    batch_export_csab_skeleton_tracks,
    export_csab_rigid_tracks,
    export_csab_skeleton_tracks,
)
from oot3d_asset_tool.csab_target_audit import audit_csab_target_resolution
from oot3d_asset_tool.cmab_audit import audit_cmab_payloads
from oot3d_asset_tool.character_conversion_manifest import (
    export_character_conversion_manifest,
)
from oot3d_asset_tool.character_conversion_package import (
    animation_controller_contract,
    animation_semantic_binding_contract,
    animation_time_source_contract,
    audit_character_conversion_package,
    pack_character_conversion_manifest,
    root_motion_ownership_contract,
    runtime_transform_policy,
    skel_anime_sampling_contract,
)
from oot3d_asset_tool.kankyo_environment_audit import (
    audit_kankyo_environment_assets,
)
from oot3d_asset_tool.kankyo_environment_export import (
    export_kankyo_environment_assets,
)
from oot3d_asset_tool.kankyo_environment_package import (
    audit_kankyo_environment_export_package,
    pack_kankyo_environment_export_manifest,
)
from oot3d_asset_tool.kokiri_runtime_route_audit import (
    audit_kokiri_runtime_route,
    route_audio_runtime_mapping,
)
from oot3d_asset_tool.kokiri_actor_asset_readiness import (
    audit_kokiri_actor_asset_readiness,
)
from oot3d_asset_tool.kokiri_kankyo_binding_plan import (
    export_kokiri_kankyo_binding_plan,
)
from oot3d_asset_tool.kokiri_runtime_manifest import (
    export_kokiri_runtime_manifest,
)
from oot3d_asset_tool.kokiri_static_actor_binding_plan import (
    export_kokiri_static_actor_binding_plan,
)
from oot3d_asset_tool.material_audit import (
    audit_scene_materials,
    material_audit_record,
    material_issues,
)
from oot3d_asset_tool.material_stage_inventory import inventory_material_stages
from oot3d_asset_tool.media_asset_audit import audit_media_assets
from oot3d_asset_tool.moflex_movie_audit import audit_moflex_movies
from oot3d_asset_tool.n64_animation_reference_audit import audit_n64_animation_reference
from oot3d_asset_tool.package_audit import audit_scene_package
from oot3d_asset_tool.prerendered_room_audit import (
    audit_prerendered_room_replacements,
)
from oot3d_asset_tool.prerendered_room_export import (
    audit_prerendered_room_export_package,
    export_prerendered_room_replacements,
    pack_prerendered_room_export_manifest,
)
from oot3d_asset_tool.q_format_audit import audit_q_format_assets
from oot3d_asset_tool.romfs_inventory import inventory_romfs
from oot3d_asset_tool.skinning_audit import audit_actor_skinning_queue
from oot3d_asset_tool.skinning_layout_audit import (
    audit_actor_skinning_vertex_layout,
)
from oot3d_asset_tool.static_batch_audit import audit_static_batch
from oot3d_asset_tool.static_batch_package import (
    audit_static_batch_package,
    pack_static_batch_manifest,
)
from oot3d_asset_tool.cmb import (
    CmbModel,
    Color,
    Mesh,
    OOT3D_MATERIAL_LIGHTING_BLOCK_OFFSET,
    PICA_TEXTURE_LUMINANCE,
    PICA_TEXTURE_LUMINANCE_ALPHA,
    PICA_UNSIGNED_BYTE_4_4,
    PICA_UNSIGNED_4BITS,
    PICA_TEXTURE_WRAP_CLAMP_TO_EDGE,
    PICA_TEXTURE_WRAP_MIRRORED_REPEAT,
    PICA_TEXTURE_WRAP_REPEAT,
    Primitive,
    Shape,
    Skeleton,
    Vec2,
    Vec3,
    VertexList,
    VertexListData,
    decode_l4_to_rgba16,
    decode_l8_to_rgba16,
    decode_la8_to_rgba16,
    material_lighting_block_summary,
    parse_material_lighting_block,
    read_color_attribute,
    read_vec2_attribute,
    read_vec3_attribute,
    texture_mask,
)
from oot3d_asset_tool.legacy_fast_resource import (
    LegacyFastResourceOptions,
    apply_texture_coord,
    export_static_model,
    first_valid_texture,
    invert_texture_coord,
    material_primary_texture_index,
    material_primary_texture_slot,
    material_texture,
    material_texture_mapper,
    material_xml,
    mesh_uv_offset,
    multiply_rgba16_pixels,
    normalize_texture_orientation,
    normalize_uv_orientation,
    orient_rgba16_image,
    secondary_material_texture,
    secondary_material_texture_mapper,
    secondary_material_texture_slot,
    secondary_texture_coord_export_status,
    skeleton_world_transforms,
    to_legacy_fast_resource_vertex,
    uv_orientation_overrides,
)
from oot3d_asset_tool.skinned_animation import (
    batch_export_skinned_animation_pose_samples,
    export_skinned_animation_pose_samples,
)
from oot3d_asset_tool.skinned_animation_readiness_audit import (
    audit_skinned_animation_readiness,
    export_skinned_animation_binding_manifest,
)
from oot3d_asset_tool.skinned_bind_pose_package import (
    audit_skinned_bind_pose_batch_package,
    pack_skinned_bind_pose_batch_manifest,
)
from oot3d_asset_tool.skinned_export import (
    batch_export_actor_skinned_bind_poses,
    export_skinned_bind_pose,
)
from oot3d_asset_tool.zar import ZarArchive
from oot3d_asset_tool.zsi import ZsiFile, ZsiWaterBox
from oot3d_asset_tool.zsi_cutscene_audit import (
    audit_zsi_cutscene_metadata,
    export_zsi_cutscene_camera_data,
)
from oot3d_asset_tool.zsi_scene_audit import audit_zsi_scene_metadata


class CmbConstantAttributeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.view = BinaryView(b"", "constant_attribute.cmb")
        self.vld = VertexListData(length=0, offset=0)

    def test_reads_constant_color_without_vatr_array(self) -> None:
        vertex_list = VertexList(
            offset=0,
            scale=1.0,
            data_type=0x1406,
            mode=1,
            constant=(0.25, 0.5, 0.75, 1.0),
        )

        colors = read_color_attribute(
            self.view,
            0,
            vertex_list,
            self.vld,
            3,
            enabled=True,
            constant=True,
        )

        self.assertEqual(colors, [Color(64, 128, 191, 255)] * 3)

    def test_reads_constant_normal_and_uv_without_vatr_arrays(self) -> None:
        normal_list = VertexList(
            offset=0,
            scale=1.0,
            data_type=0x1406,
            mode=1,
            constant=(0.0, 1.0, 0.0, 0.0),
        )
        uv_list = VertexList(
            offset=0,
            scale=1.0,
            data_type=0x1406,
            mode=1,
            constant=(0.25, 0.75, 0.0, 0.0),
        )

        normals = read_vec3_attribute(
            self.view,
            0,
            normal_list,
            self.vld,
            2,
            enabled=True,
            constant=True,
        )
        uvs = read_vec2_attribute(
            self.view,
            0,
            uv_list,
            self.vld,
            2,
            enabled=True,
            constant=True,
        )

        self.assertEqual(normals, [Vec3(0.0, 1.0, 0.0)] * 2)
        self.assertEqual(uvs, [Vec2(0.25, 0.75)] * 2)


class TextureOrientationTests(unittest.TestCase):
    @staticmethod
    def rgba16_words(*values: int) -> bytes:
        return b"".join(value.to_bytes(2, "big") for value in values)

    def test_rgba16_orientation_variants_reorder_whole_pixels(self) -> None:
        image = self.rgba16_words(1, 2, 3, 4)

        self.assertEqual(orient_rgba16_image(image, 2, 2, "normal"), image)
        self.assertEqual(orient_rgba16_image(image, 2, 2, "flip-x"), self.rgba16_words(2, 1, 4, 3))
        self.assertEqual(orient_rgba16_image(image, 2, 2, "flip-y"), self.rgba16_words(3, 4, 1, 2))
        self.assertEqual(orient_rgba16_image(image, 2, 2, "flip-xy"), self.rgba16_words(4, 3, 2, 1))

    def test_texture_orientation_aliases_match_cli_names(self) -> None:
        self.assertEqual(normalize_texture_orientation("FlipXY"), "flip_xy")
        self.assertEqual(normalize_texture_orientation("flip-xy"), "flip_xy")
        self.assertEqual(normalize_texture_orientation("none"), "normal")
        self.assertEqual(normalize_uv_orientation("flip-xy"), "flip_xy")

    def test_pica_luminance_textures_keep_opaque_alpha(self) -> None:
        for decoded in (
            decode_l8_to_rgba16(bytes((0, 128, 255)), 3),
            decode_l4_to_rgba16(bytes((0, 8, 15)), 3),
        ):
            pixels = [int.from_bytes(decoded[offset : offset + 2], "big") for offset in range(0, 6, 2)]
            self.assertEqual([pixel & 1 for pixel in pixels], [1, 1, 1])
            self.assertEqual(pixels[0], 1)
            self.assertEqual(pixels[-1], 0xFFFF)


ROOT = Path(__file__).resolve().parents[4]
ROMFS_ROOT = Path(os.environ.get("OOT3D_ROMFS", ROOT / "work" / "extract" / "romfs"))
CUBE_CMB = ROMFS_ROOT / "actor" / "keep" / "cube.cmb"
DK_LIGHTBOX_ZAR = ROMFS_ROOT / "actor" / "dk_lightbox.zar"
ZELDA_AHG_ZAR = ROMFS_ROOT / "actor" / "zelda_ahg.zar"
ZELDA_AM_ZAR = ROMFS_ROOT / "actor" / "zelda_am.zar"
ZELDA_BOX_ZAR = ROMFS_ROOT / "actor" / "zelda_box.zar"
ZELDA_BV_ZAR = ROMFS_ROOT / "actor" / "zelda_bv.zar"
ZELDA_DEKUBABA_ZAR = ROMFS_ROOT / "actor" / "zelda_dekubaba.zar"
ZELDA_EC_ZAR = ROMFS_ROOT / "actor" / "zelda_ec.zar"
ZELDA_FIELD_KEEP_ZAR = ROMFS_ROOT / "actor" / "zelda_field_keep.zar"
ZELDA_FZ_ZAR = ROMFS_ROOT / "actor" / "zelda_fz.zar"
ZELDA_LINK_BOY_NEW_ZAR = ROMFS_ROOT / "actor" / "zelda_link_boy_new.zar"
ZELDA_LINK_BOY_ULTRA_ZAR = ROMFS_ROOT / "actor" / "zelda_link_boy_ultra.zar"
ZELDA_LINK_CHILD_NEW_ZAR = ROMFS_ROOT / "actor" / "zelda_link_child_new.zar"
ZELDA_KEEP_ZAR = ROMFS_ROOT / "actor" / "zelda_keep.zar"
ZELDA_KEEP_OPENING_ZAR = ROMFS_ROOT / "actor" / "zelda_keep_opening.zar"
ZELDA_ZL4_ZAR = ROMFS_ROOT / "actor" / "zelda_zl4.zar"
ZELDA_XC_ZAR = ROMFS_ROOT / "actor" / "zelda_xc.zar"
BDAN_OBJECTS_ZAR = ROMFS_ROOT / "actor" / "zelda_bdan_objects.zar"
SCENE_DIR = ROMFS_ROOT / "scene"
BDAN_SCENE_ZSI = SCENE_DIR / "bdan_info.zsi"
GANON_TOU_SCENE_ZSI = SCENE_DIR / "ganon_tou_info.zsi"
KENJYANOMA_SCENE_ZSI = SCENE_DIR / "kenjyanoma_info.zsi"
HIRAL_DEMO_SCENE_ZSI = SCENE_DIR / "hiral_demo_info.zsi"
SPOT04_SCENE_ZSI = SCENE_DIR / "spot04_info.zsi"
SPOT04_ROOM0_ZSI = SCENE_DIR / "spot04_0_info.zsi"
SPOT04_ROOM1_ZSI = SCENE_DIR / "spot04_1_info.zsi"
TOKINOMA_ROOM0_ZSI = SCENE_DIR / "tokinoma_0_info.zsi"
BASE_O2R = Path(os.environ.get("OOT3D_BASE_O2R", ROOT / "_research" / "Shipwright" / "oot.o2r"))
HINT000_MOFLEX = ROMFS_ROOT / "misc" / "hint" / "movie" / "hint000.moflex"
HINT012_MOFLEX = ROMFS_ROOT / "misc" / "hint" / "movie" / "hint012.moflex"
HINT023_MOFLEX = ROMFS_ROOT / "misc" / "hint" / "movie" / "hint023.moflex"
QUEEN_SOUND_BCSAR = ROMFS_ROOT / "sound" / "QueenSound.bcsar"
QUEEN_STREAM_BCSAR = ROMFS_ROOT / "sound" / "QueenStream.bcsar"
QUEEN_ROLL_BCSTM = ROMFS_ROOT / "sound" / "stream" / "STRM_QUEEN_ROLL.bcstm"
MESSAGE_ANIM_QAN = ROMFS_ROOT / "message" / "anim.qan"
MESSAGE_COLOR_QCL = ROMFS_ROOT / "message" / "color.qcl"
MESSAGE_LAYOUT_QLY = ROMFS_ROOT / "message" / "layout.qly"
MESSAGE_SPRITE_QSP = ROMFS_ROOT / "message" / "sprite.qsp"
MESSAGE_SYS8_QBF = ROMFS_ROOT / "message" / "sys8.qbf"
MESSAGE_LTN16_QBF = ROMFS_ROOT / "message" / "eu" / "ltn16.qbf"
MISC_BOSS_RUSH_QBR = ROMFS_ROOT / "misc" / "bossRush.qbr"
MISC_ENDING_QAN = ROMFS_ROOT / "misc" / "ending.qan"
MISC_ENDING_QSP = ROMFS_ROOT / "misc" / "ending.qsp"
MISC_ENDING_TOP_QLY = ROMFS_ROOT / "misc" / "ending_top.qly"
MISC_ENDING_BOTTOM_QLY = ROMFS_ROOT / "misc" / "ending_bottom.qly"
MISC_ENGLISH_CHALLENGE_QAN = ROMFS_ROOT / "misc" / "eu" / "english" / "challenge.qan"
MISC_ENGLISH_CHALLENGE_QSP = ROMFS_ROOT / "misc" / "eu" / "english" / "challenge.qsp"
MISC_ENGLISH_CHALLENGE_TOP_QLY = (
    ROMFS_ROOT / "misc" / "eu" / "english" / "challenge_top.qly"
)
MISC_ENGLISH_CHALLENGE_BOTTOM_QLY = (
    ROMFS_ROOT / "misc" / "eu" / "english" / "challenge_bottom.qly"
)
MISC_HINT_LIST_QHM = ROMFS_ROOT / "misc" / "hint" / "list.qhm"
ENDING_STILL_CTXB = ROMFS_ROOT / "ending" / "ending_still.ctxb"
KANKYO_BLUEONLY_ZAR = ROMFS_ROOT / "kankyo" / "BlueOnly.zar"
KANKYO_COMMON_ZAR = ROMFS_ROOT / "kankyo" / "kankyo_common.zar"


@unittest.skipUnless(CUBE_CMB.exists(), "local OOT3D cube.cmb fixture is not present")
class UvOrientationTests(unittest.TestCase):
    def assertVec2AlmostEqual(self, actual: Vec2, expected: Vec2) -> None:
        self.assertAlmostEqual(actual.x, expected.x)
        self.assertAlmostEqual(actual.y, expected.y)

    def test_uv_orientation_overrides_flip_global_texture_space(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        shape = replace(
            model.shapes[0],
            uv0=(
                Vec2(0.10, 0.20),
                Vec2(0.40, 0.80),
                Vec2(0.20, 0.60),
                Vec2(0.55, 0.10),
                Vec2(0.75, 0.90),
                Vec2(0.95, 0.30),
            ),
        )
        material = model.materials[0]
        texture = model.textures[0]

        overrides = uv_orientation_overrides(
            shape,
            material,
            [(0, 1, 2), (3, 4, 5)],
            texture,
            "flip-xy",
        )

        self.assertVec2AlmostEqual(overrides[0], Vec2(0.90, 0.80))
        self.assertVec2AlmostEqual(overrides[1], Vec2(0.60, 0.20))
        self.assertVec2AlmostEqual(overrides[2], Vec2(0.80, 0.40))
        self.assertVec2AlmostEqual(overrides[3], Vec2(0.45, 0.90))
        self.assertVec2AlmostEqual(overrides[4], Vec2(0.25, 0.10))
        self.assertVec2AlmostEqual(overrides[5], Vec2(0.05, 0.70))

        vertex = to_legacy_fast_resource_vertex(
            shape,
            texture,
            material,
            0,
            uv_override=overrides[0],
        )
        self.assertEqual(vertex.s, int(round(0.90 * texture.width * 32.0)))
        self.assertEqual(vertex.t, int(round(0.80 * texture.height * 32.0)))


def scene_command(command_id: int, parameter: int, argument: int) -> bytes:
    return bytes((command_id, parameter, 0, 0)) + int(argument).to_bytes(4, "little")


def transition_actor_entry(
    front_room: int,
    front_effect: int,
    back_room: int,
    back_effect: int,
    actor_id: int,
    x: int,
    y: int,
    z: int,
    params: int,
) -> bytes:
    return (
        int(front_room).to_bytes(1, "little", signed=True)
        + int(front_effect).to_bytes(1, "little", signed=True)
        + int(back_room).to_bytes(1, "little", signed=True)
        + int(back_effect).to_bytes(1, "little", signed=True)
        + int(actor_id).to_bytes(2, "little", signed=True)
        + int(x).to_bytes(2, "little", signed=True)
        + int(y).to_bytes(2, "little", signed=True)
        + int(z).to_bytes(2, "little", signed=True)
        + (0).to_bytes(2, "little", signed=True)
        + int(params).to_bytes(2, "little", signed=True)
    )


def actor_entry(
    actor_id: int,
    x: int,
    y: int,
    z: int,
    rot_x: int,
    rot_y: int,
    rot_z: int,
    params: int,
) -> bytes:
    return (
        int(actor_id).to_bytes(2, "little", signed=True)
        + int(x).to_bytes(2, "little", signed=True)
        + int(y).to_bytes(2, "little", signed=True)
        + int(z).to_bytes(2, "little", signed=True)
        + int(rot_x).to_bytes(2, "little", signed=True)
        + int(rot_y).to_bytes(2, "little", signed=True)
        + int(rot_z).to_bytes(2, "little", signed=True)
        + int(params).to_bytes(2, "little", signed=True)
    )


def kokiri_manifest_test_audit() -> dict[str, object]:
    return {
        "format": "oot3d_kokiri_runtime_route_audit_v4",
        "romfs_root": "E:/romfs",
        "scene_root": "E:/romfs/scene",
        "route_stems": ["link", "spot04"],
        "required_scene_files": ["link_info.zsi", "spot04_info.zsi"],
        "missing_required_file_count": 0,
        "zsi_file_count": 4,
        "scene_file_count": 2,
        "room_file_count": 2,
        "embedded_cmb_total": 2,
        "collision_candidate_total": 2,
        "collision_effective_polygon_total": 20,
        "collision_surface_type_total": 4,
        "bgcam_total": 2,
        "water_box_total": 1,
        "room_reference_count": 2,
        "unique_room_references": ["link_0_info.zsi", "spot04_0_info.zsi"],
        "spawn_candidate_command_count": 2,
        "spawn_entry_candidate_total": 2,
        "spawn_player_candidate_total": 2,
        "entrance_entry_candidate_total": 2,
        "path_block_candidate_count": 1,
        "path_file_offset_candidate_total": 1,
        "exit_value_candidate_total": 2,
        "transition_actor_count": 2,
        "transition_actor_name_counts": {"ACTOR_EN_HOLL": 1},
        "actor_object_binding_summary": {
            "status": "route_actor_object_bindings_resolved",
            "object_list_dependency_status": "covered_without_scene_object_list",
            "object_list_command_count": 0,
            "requires_scene_object_list_for_known_route_actors": False,
            "transition_actor_binding_counts": {"ACTOR_EN_HOLL->OBJECT_GAMEPLAY_KEEP": 1},
            "transition_actor_required_object_counts": {"OBJECT_GAMEPLAY_KEEP": 1},
            "unresolved_transition_actor_binding_count": 0,
            "unresolved_transition_actor_name_counts": {},
            "empty_transition_actor_slot_count": 1,
            "special_keep_object_counts": {},
            "special_keep_objects": [],
            "player_object_binding": {
                "status": "runtime_binding_identified",
                "validated_player_spawn_layout_count": 1,
                "runtime_object_source": "gLinkObjectIds[gSaveContext.linkAge]",
                "runtime_object_names": ["OBJECT_LINK_BOY", "OBJECT_LINK_CHILD"],
                "runtime_object_ids": {"OBJECT_LINK_BOY": 0x14, "OBJECT_LINK_CHILD": 0x15},
            },
        },
        "standard_actor_list_command_count": 0,
        "object_list_command_count": 0,
        "room_actor_list_candidate_count": 2,
        "selected_room_actor_list_count": 2,
        "selected_room_actor_entry_total": 4,
        "selected_room_actor_name_counts": {"ACTOR_EN_HOLL": 2},
        "selected_room_actor_object_name_counts": {"OBJECT_GAMEPLAY_KEEP": 2},
        "sound_setting_count": 1,
        "skybox_setting_count": 1,
        "layout_validation_counts": {
            "validated_route_room_indices": 1,
        },
        "spawn_payload_profile_counts": {
            "validated_n64_player_start_list": 1,
        },
        "runtime_gap_counts": {
            "needs_oot3d_spawn_list_layout_validation": 1,
            "needs_oot3d_entrance_list_layout_validation": 1,
        },
        "audio_runtime_mapping": {
            "status": "mapped",
            "sound_setting_count": 1,
            "mapped_record_count": 1,
            "unresolved_record_count": 0,
            "profile_count": 1,
            "mapped_profile_count": 1,
            "unresolved_profile_count": 0,
            "profiles": [
                {
                    "status": "mapped",
                    "path": "link_info.zsi",
                    "scene_stem": "link",
                    "setup_index": 0,
                    "oot3d_sound_settings": {
                        "spec_id": 5,
                        "nature_ambience_id": 0,
                        "data3": 0,
                        "bgm_sound_id": 0x0100058D,
                        "bgm_sound_id_hex": "0x0100058d",
                    },
                    "native_audio_settings": {
                        "sound_spec_id": 5,
                        "nature_ambience_id": 0,
                        "bgm_sound_id": 0x0100058D,
                        "consumer": "NativeAudioService::ApplySceneAudio",
                    },
                    "mapping_reason": "native_zsi_sound_settings",
                    "record_count": 1,
                }
            ],
        },
        "audio_asset_summary": {
            "file_count": 1,
            "extension_counts": {".bcsar": 1},
            "records": [{"path": "sound/QueenSound.bcsar", "extension": ".bcsar"}],
        },
        "kankyo_asset_summary": {
            "archive_count": 1,
            "archive_file_count_total": 4,
            "embedded_type_counts": {"cmb": 1, "cmab": 1},
            "environment_model_group_counts": {"sky": 1},
        },
        "kankyo_runtime_selection": {
            "status": "selected",
            "profile_count": 1,
            "status_counts": {"n64_filter_only_no_kankyo_resource_needed": 1},
            "unresolved_selection_count": 0,
            "records": [
                {
                    "skybox_id": 0x1D,
                    "skybox_name": "SKYBOX_UNSET_1D",
                    "weather_or_unk_05": 0,
                    "indoors": 0,
                    "setup_count": 1,
                    "status": "n64_filter_only_no_kankyo_resource_needed",
                }
            ],
        },
        "fallback_policy": ["N64 scene resources remain fallback."],
        "records": [
            kokiri_manifest_scene_record("link_info.zsi", "link", "link_0_info.zsi"),
            kokiri_manifest_room_record("link_0_info.zsi", "link"),
            kokiri_manifest_scene_record("spot04_info.zsi", "spot04", "spot04_0_info.zsi"),
            kokiri_manifest_room_record("spot04_0_info.zsi", "spot04"),
        ],
    }


def kokiri_manifest_scene_record(path: str, stem: str, room_ref: str) -> dict[str, object]:
    return {
        "path": path,
        "role": "scene",
        "scene_stem": stem,
        "size": 512,
        "setup_count": 1,
        "embedded_cmb_count": 0,
        "embedded_cmbs": [],
        "collision_candidate_count": 1,
        "collision_header_candidates": [kokiri_manifest_collision_candidate()],
        "scene_setups": [kokiri_manifest_setup(room_ref)],
    }


def kokiri_manifest_room_record(path: str, stem: str) -> dict[str, object]:
    return {
        "path": path,
        "role": "room",
        "scene_stem": stem,
        "size": 1024,
        "setup_count": 0,
        "embedded_cmb_count": 1,
        "embedded_cmbs": [
            {
                "index": 0,
                "offset": 64,
                "size": 128,
                "model_name": f"{stem}_room_model",
                "mesh_count": 1,
                "material_count": 1,
                "texture_count": 1,
                "bone_count": 0,
            }
        ],
        "collision_candidate_count": 0,
        "collision_header_candidates": [],
        "room_actor_list_candidate_count": 1,
        "selected_room_actor_list_candidate": kokiri_manifest_room_actor_candidate(),
        "scene_setups": [],
    }


def kokiri_manifest_room_actor_candidate() -> dict[str, object]:
    return {
        "status": "object_prefixed_room_actor_list_candidate",
        "confidence": "strong_object_prefixed_actor_list",
        "start_offset": 256,
        "entry_count": 2,
        "score": 160,
        "object_id_count": 1,
        "unknown_object_id_count": 0,
        "actor_name_counts": {"ACTOR_EN_HOLL": 2},
        "object_name_counts": {"OBJECT_GAMEPLAY_KEEP": 1},
        "object_prefix": {
            "start_offset": 248,
            "byte_count": 8,
            "object_ids": [
                {
                    "offset": 248,
                    "object_id": 1,
                    "object_name": "OBJECT_GAMEPLAY_KEEP",
                    "semantic_status": "known_object_id",
                }
            ],
            "unknown_object_ids": [],
            "zero_padding": [],
        },
        "entries": [
            {
                "index": 0,
                "offset": 256,
                "actor_id": 0x23,
                "actor_name": "ACTOR_EN_HOLL",
                "pos": [1, 2, 3],
                "rot": [4, 5, 6],
                "params": 7,
            },
            {
                "index": 1,
                "offset": 272,
                "actor_id": 0x23,
                "actor_name": "ACTOR_EN_HOLL",
                "pos": [8, 9, 10],
                "rot": [11, 12, 13],
                "params": 14,
            },
        ],
    }


def kokiri_manifest_collision_candidate() -> dict[str, object]:
    return {
        "setup_index": 0,
        "command_argument": 128,
        "offset": 144,
        "bounds_min": [-1, -2, -3],
        "bounds_max": [4, 5, 6],
        "vertex_count": 4,
        "effective_polygon_count": 10,
        "surface_type_count": 2,
        "bgcam_count": 1,
        "water_box_count": 1,
        "evidence": ["synthetic"],
    }


def kokiri_manifest_setup(room_ref: str) -> dict[str, object]:
    return {
        "index": 0,
        "command_count": 8,
        "commands": [
            {"command_id": "0x04", "room_references": [room_ref]},
            {
                "command_id": "0x00",
                "parameter": 1,
                "spawn_entry_candidate_count": 1,
                "spawn_player_candidate_count": 1,
                "selected_spawn_list_candidate": {
                    "start_delta": 0,
                    "confidence": "strong",
                    "entry_count": 1,
                    "expected_entry_count": 1,
                    "fits_payload_window": True,
                    "actor_name_counts": {"ACTOR_PLAYER": 1},
                    "entries": [{"actor_id": 0, "actor_name": "ACTOR_PLAYER"}],
                },
                "spawn_payload_profile": {
                    "status": "validated_n64_player_start_list",
                    "layout_validation_status": "validated_player_spawn_indices",
                    "entry_count": 1,
                    "used_spawn_indices": [0],
                    "runtime_mapping": "Scene_CommandSpawnList compatible",
                },
            },
            {
                "command_id": "0x06",
                "parameter": 1,
                "entrance_entry_candidate_count": 1,
                "selected_entrance_list_candidate": {
                    "start_delta": 0,
                    "confidence": "strong",
                    "entry_count": 1,
                    "expected_entry_count": 1,
                    "entries": [{"spawn": 0, "room": 0}],
                },
            },
            {
                "command_id": "0x0d",
                "parameter": 1,
                "path_file_offset_candidate_count": 1,
                "path_block_candidate": {
                    "valid_file_offset_u32s": [{"value_hex": "0x00000080"}],
                },
            },
            {
                "command_id": "0x0e",
                "transition_actors": [
                    {"actor_id": 0, "actor_name": "ACTOR_PLAYER"},
                    {"actor_id": 0x23, "actor_name": "ACTOR_EN_HOLL"},
                ],
            },
            {
                "command_id": "0x13",
                "exit_value_candidate_count": 1,
                "exit_values_candidate": [{"value_hex": "0x0000"}],
            },
            {
                "command_id": "0x15",
                "sound_settings": {
                    "spec_id": 5,
                    "nature_ambience_id": 0,
                    "data3": 0,
                    "bgm_sound_id": 0x0100058D,
                },
            },
            {"command_id": "0x11", "skybox_settings": {"skybox_id": 0x1D}},
        ],
    }


class CsabTrackPackageTests(unittest.TestCase):
    def test_pack_csab_track_batch_manifest_writes_auditable_o2r(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            track_dir = root / "tracks"
            track_dir.mkdir()
            first_track = {
                "format": "oot3d_csab_rigid_track_export_v1",
                "csab_name": "Anim/first.csab",
                "target_cmb_name": "Model/first.cmb",
            }
            second_track = {
                "format": "oot3d_csab_rigid_track_export_v1",
                "csab_name": "Anim/second.csab",
                "target_cmb_name": "Model/second.cmb",
            }
            (track_dir / "first.json").write_text(
                json.dumps(first_track, indent=2) + "\n",
                encoding="utf-8",
            )
            (track_dir / "second.json").write_text(
                json.dumps(second_track, indent=2) + "\n",
                encoding="utf-8",
            )
            manifest = {
                "format": "oot3d_csab_rigid_track_batch_v1",
                "considered_csab": 3,
                "exported": 2,
                "failed": 0,
                "records": [
                    {
                        "status": "exported",
                        "archive_path": "actor_a.zar",
                        "csab_name": "Anim/first.csab",
                        "target_cmb_name": "Model/first.cmb",
                        "track_export": "tracks/first.json",
                    },
                    {
                        "status": "skipped",
                        "archive_path": "actor_b.zar",
                        "csab_name": "Anim/skipped.csab",
                        "reason": "target_unresolved_or_missing",
                    },
                    {
                        "status": "exported",
                        "archive_path": "actor_c.zar",
                        "csab_name": "Anim/second.csab",
                        "target_cmb_name": "Model/second.cmb",
                        "track_export": "tracks/second.json",
                    },
                ],
            }
            manifest_path = root / "csab_rigid_track_batch_manifest.json"
            manifest_path.write_text(
                json.dumps(manifest, indent=2) + "\n",
                encoding="utf-8",
            )
            archive_path = root / "csab_tracks.o2r"
            audit_path = root / "csab_track_package_audit.json"

            pack_csab_track_batch_manifest(
                manifest_path,
                archive_path,
                archive_prefix="animations/test/csab",
            )
            audit = audit_csab_track_batch_package(
                manifest_path,
                archive_path,
                audit_path,
                archive_prefix="animations/test/csab",
            )
            with zipfile.ZipFile(archive_path, "r") as archive:
                archive_names = set(archive.namelist())

        self.assertEqual(audit["format"], "oot3d_csab_track_batch_package_audit_v1")
        self.assertEqual(audit["archive_entry_count"], 4)
        self.assertEqual(audit["expected_track_count"], 2)
        self.assertEqual(audit["expected_unique_track_count"], 2)
        self.assertEqual(audit["track_entry_count"], 2)
        self.assertTrue(audit["has_manifest"])
        self.assertTrue(audit["has_csab_track_batch_manifest"])
        self.assertTrue(audit["archived_csab_track_batch_manifest_matches"])
        self.assertEqual(audit["issue_counts"]["total"], 0)
        self.assertEqual(
            audit["archived_track_format_counts"]["oot3d_csab_rigid_track_export_v1"],
            2,
        )
        self.assertIn("animations/test/csab/tracks/first.json", archive_names)
        self.assertIn("animations/test/csab/tracks/second.json", archive_names)


class SkinnedBindPosePackageTests(unittest.TestCase):
    def test_pack_skinned_bind_pose_batch_manifest_writes_auditable_o2r(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            export_dir = root / "exports"
            export_dir.mkdir()
            first_export = {
                "format": "oot3d_skinned_bind_pose_export_v1",
                "source": "actor_a.zar!Model/a.cmb",
                "model_name": "a",
            }
            second_export = {
                "format": "oot3d_skinned_bind_pose_export_v1",
                "source": "actor_b.zar!Model/b.cmb",
                "model_name": "b",
            }
            first_path = export_dir / "first.json"
            second_path = export_dir / "second.json"
            first_path.write_text(
                json.dumps(first_export, indent=2) + "\n",
                encoding="utf-8",
            )
            second_path.write_text(
                json.dumps(second_export, indent=2) + "\n",
                encoding="utf-8",
            )
            manifest = {
                "format": "oot3d_skinned_bind_pose_batch_v1",
                "output_dir": str(root),
                "export_dir": str(export_dir),
                "counts": {"exported": 2},
                "records": [
                    {
                        "status": "exported",
                        "container_path": "actor_a.zar",
                        "embedded_name": "Model/a.cmb",
                        "output": str(first_path),
                    },
                    {
                        "status": "skipped",
                        "container_path": "actor_c.zar",
                        "embedded_name": "Model/c.cmb",
                    },
                    {
                        "status": "exported",
                        "container_path": "actor_b.zar",
                        "embedded_name": "Model/b.cmb",
                        "output": str(second_path),
                    },
                ],
            }
            manifest_path = root / "skinned_bind_pose_batch_manifest.json"
            manifest_path.write_text(
                json.dumps(manifest, indent=2) + "\n",
                encoding="utf-8",
            )
            archive_path = root / "skinned_bind_pose.o2r"
            audit_path = root / "skinned_bind_pose_package_audit.json"

            pack_skinned_bind_pose_batch_manifest(
                manifest_path,
                archive_path,
                archive_prefix="objects/test/skinned_bind_pose",
            )
            audit = audit_skinned_bind_pose_batch_package(
                manifest_path,
                archive_path,
                audit_path,
                archive_prefix="objects/test/skinned_bind_pose",
            )
            with zipfile.ZipFile(archive_path, "r") as archive:
                archive_names = set(archive.namelist())

        self.assertEqual(audit["format"], "oot3d_skinned_bind_pose_batch_package_audit_v1")
        self.assertEqual(audit["archive_entry_count"], 4)
        self.assertEqual(audit["expected_export_count"], 2)
        self.assertEqual(audit["expected_unique_export_count"], 2)
        self.assertEqual(audit["export_entry_count"], 2)
        self.assertTrue(audit["has_manifest"])
        self.assertTrue(audit["has_skinned_bind_pose_batch_manifest"])
        self.assertTrue(audit["archived_skinned_bind_pose_batch_manifest_matches"])
        self.assertEqual(audit["issue_counts"]["total"], 0)
        self.assertEqual(
            audit["archived_export_format_counts"]["oot3d_skinned_bind_pose_export_v1"],
            2,
        )
        self.assertIn("objects/test/skinned_bind_pose/exports/first.json", archive_names)
        self.assertIn("objects/test/skinned_bind_pose/exports/second.json", archive_names)


class SkinnedAnimationReadinessAuditTests(unittest.TestCase):
    def test_audit_skinned_animation_readiness_matches_tracks_to_bind_poses(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bind_export_dir = root / "bind_exports"
            track_export_dir = root / "tracks"
            bind_export_dir.mkdir()
            track_export_dir.mkdir()
            bind_a_path = bind_export_dir / "a.json"
            bind_b_path = bind_export_dir / "b.json"
            track_a0_path = track_export_dir / "a0.json"
            track_a1_path = track_export_dir / "a1.json"
            track_b0_path = track_export_dir / "b0.json"
            for path, payload in (
                (bind_a_path, {"format": "oot3d_skinned_bind_pose_export_v1"}),
                (bind_b_path, {"format": "oot3d_skinned_bind_pose_export_v1"}),
                (track_a0_path, {"format": "oot3d_csab_skeleton_track_export_v1"}),
                (track_a1_path, {"format": "oot3d_csab_skeleton_track_export_v1"}),
                (track_b0_path, {"format": "oot3d_csab_skeleton_track_export_v1"}),
            ):
                path.write_text(json.dumps(payload) + "\n", encoding="utf-8")
            bind_manifest = {
                "format": "oot3d_skinned_bind_pose_batch_v1",
                "records": [
                    {
                        "status": "exported",
                        "container_path": "actor_a.zar",
                        "embedded_name": "Model/a.cmb",
                        "model_name": "a",
                        "bone_count": 4,
                        "output": str(bind_a_path),
                        "counts": {
                            "mesh_count": 1,
                            "skinned_primitive_count": 2,
                            "mode_1_primitive_count": 1,
                            "mode_2_primitive_count": 1,
                            "skinned_vertex_rows": 12,
                            "skeleton_bone_count": 4,
                            "finite_bind_world_matrix_entries": 64,
                            "validation_error_count": 0,
                        },
                    },
                    {
                        "status": "exported",
                        "container_path": "actor_b.zar",
                        "embedded_name": "Model/b.cmb",
                        "model_name": "b",
                        "bone_count": 7,
                        "output": str(bind_b_path),
                        "counts": {
                            "mesh_count": 2,
                            "skinned_primitive_count": 3,
                            "mode_1_primitive_count": 0,
                            "mode_2_primitive_count": 3,
                            "skinned_vertex_rows": 30,
                            "skeleton_bone_count": 7,
                            "finite_bind_world_matrix_entries": 112,
                            "validation_error_count": 0,
                        },
                    },
                ],
            }
            track_manifest = {
                "format": "oot3d_csab_skeleton_track_batch_v1",
                "output": str(root),
                "records": [
                    {
                        "status": "exported",
                        "archive_path": "actor_a.zar",
                        "csab_name": "Anim/a0.csab",
                        "target_cmb_name": "Model/a.cmb",
                        "target_support_status": "needs_skinning_mode_1_support",
                        "target_resolution_status": "single_bone_count_match",
                        "track_export": "tracks/a0.json",
                        "counts": {"track_count": 4, "channel_count": 9},
                        "validation": {"target_bone_count": 4},
                    },
                    {
                        "status": "exported",
                        "archive_path": "actor_a.zar",
                        "csab_name": "Anim/a1.csab",
                        "target_cmb_name": "Model/a.cmb",
                        "target_support_status": "needs_skinning_mode_1_support",
                        "target_resolution_status": "single_bone_count_match",
                        "track_export": "tracks/a1.json",
                        "counts": {"track_count": 4, "channel_count": 10},
                        "validation": {"target_bone_count": 4},
                    },
                    {
                        "status": "exported",
                        "archive_path": "actor_b.zar",
                        "csab_name": "Anim/b0.csab",
                        "target_cmb_name": "Model/b.cmb",
                        "target_support_status": "needs_skinning_mode_2_support",
                        "target_resolution_status": "multiple_bone_count_exact_name_match",
                        "track_export": "tracks/b0.json",
                        "counts": {"track_count": 7, "channel_count": 12},
                        "validation": {"target_bone_count": 7},
                    },
                ],
            }
            bind_manifest_path = root / "bind_manifest.json"
            track_manifest_path = root / "track_manifest.json"
            audit_path = root / "readiness.json"
            bind_manifest_path.write_text(
                json.dumps(bind_manifest, indent=2) + "\n",
                encoding="utf-8",
            )
            track_manifest_path.write_text(
                json.dumps(track_manifest, indent=2) + "\n",
                encoding="utf-8",
            )

            audit = audit_skinned_animation_readiness(
                bind_manifest_path,
                track_manifest_path,
                audit_path,
            )

        self.assertEqual(audit["format"], "oot3d_skinned_animation_readiness_audit_v1")
        self.assertEqual(audit["bind_pose_export_count"], 2)
        self.assertEqual(audit["csab_track_export_count"], 3)
        self.assertEqual(audit["unique_csab_target_count"], 2)
        self.assertEqual(audit["csab_targets_with_bind_pose_count"], 2)
        self.assertEqual(audit["unused_bind_pose_export_count"], 0)
        self.assertEqual(audit["issue_counts"]["total"], 0)
        self.assertEqual(audit["animation_count_per_target_counts"]["1"], 1)
        self.assertEqual(audit["animation_count_per_target_counts"]["2"], 1)
        self.assertEqual(audit["aggregate_track_counts"]["track_count"], 15)
        self.assertEqual(audit["aggregate_track_counts"]["channel_count"], 31)
        self.assertEqual(audit["referenced_bind_pose_counts"]["skinned_vertex_rows"], 42)
        self.assertEqual(audit["unique_model_support_status_counts"]["needs_skinning_mode_1_support"], 1)
        self.assertEqual(audit["unique_model_support_status_counts"]["needs_skinning_mode_2_support"], 1)

    def test_export_skinned_animation_binding_manifest_writes_target_table(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bind_export_dir = root / "bind_exports"
            track_export_dir = root / "tracks"
            bind_export_dir.mkdir()
            track_export_dir.mkdir()
            bind_a_path = bind_export_dir / "a.json"
            bind_b_path = bind_export_dir / "b.json"
            track_a0_path = track_export_dir / "a0.json"
            track_a1_path = track_export_dir / "a1.json"
            track_b0_path = track_export_dir / "b0.json"
            for path, payload in (
                (bind_a_path, {"format": "oot3d_skinned_bind_pose_export_v1"}),
                (bind_b_path, {"format": "oot3d_skinned_bind_pose_export_v1"}),
                (track_a0_path, {"format": "oot3d_csab_skeleton_track_export_v1"}),
                (track_a1_path, {"format": "oot3d_csab_skeleton_track_export_v1"}),
                (track_b0_path, {"format": "oot3d_csab_skeleton_track_export_v1"}),
            ):
                path.write_text(json.dumps(payload) + "\n", encoding="utf-8")
            bind_manifest = {
                "format": "oot3d_skinned_bind_pose_batch_v1",
                "output_dir": str(root),
                "records": [
                    {
                        "status": "exported",
                        "container_path": "actor_a.zar",
                        "embedded_name": "Model/a.cmb",
                        "model_name": "a",
                        "bone_count": 4,
                        "output": str(bind_a_path),
                        "counts": {"skinned_vertex_rows": 12, "validation_error_count": 0},
                    },
                    {
                        "status": "exported",
                        "container_path": "actor_b.zar",
                        "embedded_name": "Model/b.cmb",
                        "model_name": "b",
                        "bone_count": 7,
                        "output": str(bind_b_path),
                        "counts": {"skinned_vertex_rows": 30, "validation_error_count": 0},
                    },
                ],
            }
            track_manifest = {
                "format": "oot3d_csab_skeleton_track_batch_v1",
                "output": str(root),
                "records": [
                    {
                        "status": "exported",
                        "archive_path": "actor_a.zar",
                        "csab_name": "Anim/a0.csab",
                        "target_cmb_name": "Model/a.cmb",
                        "target_support_status": "needs_skinning_mode_1_support",
                        "target_resolution_status": "single_bone_count_match",
                        "track_export": "tracks/a0.json",
                        "frame_slot_count": 5,
                        "counts": {"track_count": 4, "channel_count": 9},
                        "validation": {
                            "valid": True,
                            "target_bone_count": 4,
                            "non_f32_channel_blocks": 0,
                        },
                    },
                    {
                        "status": "exported",
                        "archive_path": "actor_a.zar",
                        "csab_name": "Anim/a1.csab",
                        "target_cmb_name": "Model/a.cmb",
                        "target_support_status": "needs_skinning_mode_1_support",
                        "target_resolution_status": "single_bone_count_match",
                        "track_export": "tracks/a1.json",
                        "frame_slot_count": 8,
                        "counts": {"track_count": 4, "channel_count": 10},
                        "validation": {
                            "valid": True,
                            "target_bone_count": 4,
                            "non_f32_channel_blocks": 0,
                        },
                    },
                    {
                        "status": "exported",
                        "archive_path": "actor_b.zar",
                        "csab_name": "Anim/b0.csab",
                        "target_cmb_name": "Model/b.cmb",
                        "target_support_status": "needs_skinning_mode_2_support",
                        "target_resolution_status": "multiple_bone_count_exact_name_match",
                        "track_export": "tracks/b0.json",
                        "frame_slot_count": 9,
                        "counts": {"track_count": 7, "channel_count": 12},
                        "validation": {
                            "valid": True,
                            "target_bone_count": 7,
                            "non_f32_channel_blocks": 0,
                        },
                    },
                ],
            }
            bind_manifest_path = root / "bind_manifest.json"
            track_manifest_path = root / "track_manifest.json"
            output_path = root / "binding_manifest.json"
            bind_manifest_path.write_text(
                json.dumps(bind_manifest, indent=2) + "\n",
                encoding="utf-8",
            )
            track_manifest_path.write_text(
                json.dumps(track_manifest, indent=2) + "\n",
                encoding="utf-8",
            )

            manifest = export_skinned_animation_binding_manifest(
                bind_manifest_path,
                track_manifest_path,
                output_path,
                bind_pose_archive_prefix="objects/test/skinned_bind_pose",
                csab_track_archive_prefix="animations/test/csab/skinned",
            )
            written = json.loads(output_path.read_text(encoding="utf-8"))

        self.assertEqual(manifest["format"], "oot3d_skinned_animation_binding_manifest_v1")
        self.assertEqual(written["format"], manifest["format"])
        self.assertEqual(manifest["target_count"], 2)
        self.assertEqual(manifest["animation_count"], 3)
        self.assertEqual(manifest["unused_bind_pose_target_count"], 0)
        self.assertEqual(manifest["readiness_summary"]["issue_counts"]["total"], 0)
        first_target = manifest["targets"][0]
        self.assertEqual(first_target["archive_path"], "actor_a.zar")
        self.assertEqual(first_target["animation_count"], 2)
        self.assertEqual(
            first_target["bind_pose"]["package_entry"],
            "objects/test/skinned_bind_pose/bind_exports/a.json",
        )
        self.assertEqual(
            first_target["animations"][0]["track_package_entry"],
            "animations/test/csab/skinned/tracks/a0.json",
        )
        self.assertEqual(first_target["animations"][0]["validation"]["target_bone_count"], 4)


@unittest.skipUnless(
    CUBE_CMB.exists() and DK_LIGHTBOX_ZAR.exists() and SPOT04_ROOM0_ZSI.exists(),
    "local OOT3D inventory fixtures are not present",
)
class RomFsInventoryTests(unittest.TestCase):
    def test_inventory_romfs_classifies_containers_and_static_models(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            romfs = Path(tmp) / "romfs"
            (romfs / "actor" / "keep").mkdir(parents=True)
            (romfs / "actor" / "keep" / "cube.cmb").write_bytes(CUBE_CMB.read_bytes())
            (romfs / "actor").mkdir(exist_ok=True)
            (romfs / "actor" / "dk_lightbox.zar").write_bytes(DK_LIGHTBOX_ZAR.read_bytes())
            (romfs / "scene").mkdir()
            (romfs / "scene" / "spot04_0_info.zsi").write_bytes(SPOT04_ROOM0_ZSI.read_bytes())
            (romfs / "misc").mkdir()
            (romfs / "misc" / "dummy.ctxb").write_bytes(b"ctxb")

            output = Path(tmp) / "inventory.json"
            inventory = inventory_romfs(romfs, output)

            self.assertTrue(output.exists())

        self.assertEqual(inventory["file_count"], 4)
        self.assertEqual(inventory["extension_counts"][".cmb"], 1)
        self.assertEqual(inventory["extension_counts"][".zar"], 1)
        self.assertEqual(inventory["extension_counts"][".zsi"], 1)
        self.assertEqual(inventory["extension_counts"][".ctxb"], 1)
        self.assertEqual(inventory["container_counts"]["ctxb_files"], 1)
        self.assertEqual(inventory["model_counts"]["discovered"], 3)
        self.assertEqual(inventory["model_counts"]["parsed"], 3)
        self.assertEqual(inventory["model_counts"]["static_candidate"], 3)
        self.assertEqual(inventory["model_counts"]["nonstatic_candidate"], 0)
        self.assertEqual(inventory["parse_error_count"], 0)
        statuses = {record["support_status"] for record in inventory["model_records"]}
        self.assertEqual(statuses, {"static_cmb_export_supported"})

    def test_material_stage_inventory_counts_romfs_texture_stage_risks(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            romfs = Path(tmp) / "romfs"
            (romfs / "actor" / "keep").mkdir(parents=True)
            (romfs / "actor" / "keep" / "cube.cmb").write_bytes(CUBE_CMB.read_bytes())
            (romfs / "actor").mkdir(exist_ok=True)
            (romfs / "actor" / "dk_lightbox.zar").write_bytes(DK_LIGHTBOX_ZAR.read_bytes())
            (romfs / "scene").mkdir()
            (romfs / "scene" / "spot04_0_info.zsi").write_bytes(SPOT04_ROOM0_ZSI.read_bytes())

            output = Path(tmp) / "material_stages.json"
            inventory = inventory_material_stages(romfs, output, sample_limit=10)

            self.assertTrue(output.exists())

        self.assertEqual(inventory["file_count"], 3)
        self.assertEqual(inventory["model_counts"]["discovered"], 3)
        self.assertEqual(inventory["model_counts"]["parsed"], 3)
        self.assertEqual(inventory["model_counts"]["parse_errors"], 0)
        self.assertGreater(inventory["material_count"], 0)
        self.assertGreater(inventory["textured_material_count"], 0)
        self.assertIn("stage_count_1", inventory["raw_texture_stage_selector_counts"])
        self.assertGreater(inventory["raw_texture_stage_export_gap_material_count"], 0)
        self.assertGreater(inventory["texture_stage_risk_material_count"], 0)
        self.assertIn("raw_texture_stage_candidate_summary_counts", inventory)
        self.assertGreater(sum(inventory["raw_texture_stage_candidate_summary_counts"].values()), 0)
        self.assertIn("raw_texture_stage_export_gap_candidate_summary_counts", inventory)
        self.assertGreater(
            sum(inventory["raw_texture_stage_export_gap_candidate_summary_counts"].values()),
            0,
        )
        self.assertIn("raw_texture_stage_non_gap_candidate_summary_counts", inventory)
        self.assertGreater(
            sum(inventory["raw_texture_stage_non_gap_candidate_summary_counts"].values()),
            0,
        )
        self.assertIn("raw_texture_stage_export_classification_counts", inventory)
        self.assertGreater(
            sum(inventory["raw_texture_stage_export_classification_counts"].values()),
            0,
        )
        self.assertIn("raw_texture_stage_export_gap_classification_counts", inventory)
        self.assertGreater(
            sum(inventory["raw_texture_stage_export_gap_classification_counts"].values()),
            0,
        )
        self.assertIn("raw_texture_stage_export_blocker_counts", inventory)
        self.assertGreater(sum(inventory["raw_texture_stage_export_blocker_counts"].values()), 0)
        self.assertIn("raw_texture_stage_alignment_case_counts", inventory)
        self.assertGreater(sum(inventory["raw_texture_stage_alignment_case_counts"].values()), 0)
        self.assertIn("raw_texture_stage_export_gap_alignment_case_counts", inventory)
        self.assertGreater(
            sum(inventory["raw_texture_stage_export_gap_alignment_case_counts"].values()),
            0,
        )
        self.assertIn("raw_texture_stage_resolution_case_counts", inventory)
        self.assertGreater(sum(inventory["raw_texture_stage_resolution_case_counts"].values()), 0)
        self.assertIn("raw_texture_stage_export_gap_resolution_case_counts", inventory)
        self.assertGreater(
            sum(inventory["raw_texture_stage_export_gap_resolution_case_counts"].values()),
            0,
        )
        self.assertIn("raw_texture_stage_export_order_case_counts", inventory)
        self.assertGreater(sum(inventory["raw_texture_stage_export_order_case_counts"].values()), 0)
        self.assertIn("raw_texture_stage_export_gap_order_case_counts", inventory)
        self.assertGreater(
            sum(inventory["raw_texture_stage_export_gap_order_case_counts"].values()),
            0,
        )
        self.assertIn("raw_texture_stage_f3d_limit_case_counts", inventory)
        self.assertIn("raw_texture_stage_slot_bounds_case_counts", inventory)
        self.assertGreater(sum(inventory["raw_texture_stage_slot_bounds_case_counts"].values()), 0)
        self.assertIn("raw_texture_stage_export_gap_slot_bounds_case_counts", inventory)
        self.assertGreater(
            sum(inventory["raw_texture_stage_export_gap_slot_bounds_case_counts"].values()),
            0,
        )
        self.assertIn("raw_texture_stage_slot_sequence_case_counts", inventory)
        self.assertGreater(sum(inventory["raw_texture_stage_slot_sequence_case_counts"].values()), 0)
        self.assertIn("raw_texture_stage_export_gap_slot_sequence_case_counts", inventory)
        self.assertGreater(
            sum(inventory["raw_texture_stage_export_gap_slot_sequence_case_counts"].values()),
            0,
        )
        self.assertIn("raw_texture_stage_oob_delta_counts", inventory)
        self.assertGreater(sum(inventory["raw_texture_stage_oob_delta_counts"].values()), 0)
        self.assertIn("raw_texture_stage_export_gap_oob_delta_counts", inventory)
        self.assertGreater(
            sum(inventory["raw_texture_stage_export_gap_oob_delta_counts"].values()),
            0,
        )
        self.assertIn("raw_texture_stage_unexported_stage_position_counts", inventory)
        self.assertIn("raw_texture_stage_export_gap_unexported_stage_position_counts", inventory)
        self.assertIn("raw_texture_stage_non_gap_unexported_stage_position_counts", inventory)
        self.assertIn("raw_texture_stage_unexported_material_ref_stage_position_counts", inventory)
        self.assertIn("raw_texture_stage_export_gap_unexported_material_ref_stage_position_counts", inventory)
        self.assertIn("raw_texture_stage_non_gap_unexported_material_ref_stage_position_counts", inventory)
        self.assertIn("raw_texture_stage_unexported_material_index_ref_stage_position_counts", inventory)
        self.assertIn(
            "raw_texture_stage_export_gap_unexported_material_index_ref_stage_position_counts",
            inventory,
        )
        self.assertIn(
            "raw_texture_stage_non_gap_unexported_material_index_ref_stage_position_counts",
            inventory,
        )
        self.assertIn("raw_texture_stage_non_material_index_unexported_stage_position_counts", inventory)
        self.assertIn(
            "raw_texture_stage_export_gap_non_material_index_unexported_stage_position_counts",
            inventory,
        )
        self.assertIn(
            "raw_texture_stage_non_gap_non_material_index_unexported_stage_position_counts",
            inventory,
        )
        self.assertIn("raw_texture_stage_direct_unexported_stage_position_counts", inventory)
        self.assertIn("raw_texture_stage_export_gap_direct_unexported_stage_position_counts", inventory)
        self.assertIn("raw_texture_stage_non_gap_direct_unexported_stage_position_counts", inventory)
        self.assertIn("raw_texture_stage_export_gap_candidate_samples", inventory)
        self.assertGreater(len(inventory["raw_texture_stage_export_gap_candidate_samples"]), 0)
        for samples in inventory["raw_texture_stage_export_gap_candidate_samples"].values():
            self.assertLessEqual(len(samples), 5)
            self.assertIn("raw_texture_stage_slot_bounds", samples[0])
            self.assertIn("sequence_case", samples[0]["raw_texture_stage_slot_bounds"])
            self.assertIn("raw_texture_stage_texture_candidates", samples[0])
            self.assertIn("raw_texture_stage_export_classification", samples[0])
            self.assertIn("raw_texture_stage_alignment_case", samples[0])
            self.assertIn("raw_texture_stage_resolution_case", samples[0])
            self.assertIn("raw_texture_stage_export_order_case", samples[0])
            self.assertIn("raw_texture_stage_f3d_limit_case", samples[0])
        self.assertIn("raw_texture_stage_non_gap_candidate_samples", inventory)
        for samples in inventory["raw_texture_stage_non_gap_candidate_samples"].values():
            self.assertLessEqual(len(samples), 5)
            self.assertEqual(samples[0]["raw_texture_stage_export_gap"], 0)
            self.assertIn("raw_texture_stage_unexported_valid_textures", samples[0])
            self.assertIn("raw_texture_stage_unexported_material_refs", samples[0])
            self.assertIn("raw_texture_stage_unexported_material_index_refs", samples[0])
            self.assertGreater(
                len(samples[0]["raw_texture_stage_unexported_valid_textures"]),
                0,
            )
        self.assertIn(
            "raw_texture_stage_non_gap_non_material_index_candidate_samples",
            inventory,
        )
        self.assertLessEqual(
            len(inventory["raw_texture_stage_non_gap_non_material_index_candidate_samples"]),
            10,
        )
        for sample in inventory[
            "raw_texture_stage_non_gap_non_material_index_candidate_samples"
        ]:
            self.assertEqual(sample["raw_texture_stage_export_gap"], 0)
            self.assertGreater(
                len(sample["raw_texture_stage_non_gap_non_material_index_candidates"]),
                0,
            )
            for candidate in sample[
                "raw_texture_stage_non_gap_non_material_index_candidates"
            ]:
                self.assertTrue(candidate["valid_texture_index"])
                self.assertFalse(candidate["currently_exported"])
                self.assertFalse(candidate["valid_material_index"])
        self.assertGreater(inventory["secondary_texture_coord_transform_count"], 0)
        self.assertIn(
            "scale_translation",
            inventory["secondary_texture_coord_transform_kind_counts"],
        )
        self.assertLessEqual(len(inventory["secondary_texture_coord_transform_samples"]), 10)
        self.assertIn(
            "secondary_texture_coord_transform_signature",
            inventory["secondary_texture_coord_transform_samples"][0],
        )
        self.assertLessEqual(len(inventory["sample_records"]), 10)
        self.assertIn(
            "raw_texture_stage_candidate_summary",
            inventory["sample_records"][0],
        )
        self.assertIn("raw_texture_stage_slot_bounds", inventory["sample_records"][0])
        self.assertIn("raw_texture_stage_export_classification", inventory["sample_records"][0])
        self.assertIn("raw_texture_stage_alignment_case", inventory["sample_records"][0])
        self.assertIn("raw_texture_stage_resolution_case", inventory["sample_records"][0])
        self.assertIn("raw_texture_stage_export_order_case", inventory["sample_records"][0])
        self.assertTrue(inventory["sample_records"][0]["texture_stage_risk_tags"])

    def test_actor_inventory_classifies_static_skinned_and_animation_payloads(self) -> None:
        if not ZELDA_AHG_ZAR.exists():
            self.skipTest("local OOT3D zelda_ahg.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            (actor_root / "keep").mkdir(parents=True)
            (actor_root / "keep" / "cube.cmb").write_bytes(CUBE_CMB.read_bytes())
            (actor_root / "dk_lightbox.zar").write_bytes(DK_LIGHTBOX_ZAR.read_bytes())
            (actor_root / "zelda_ahg.zar").write_bytes(ZELDA_AHG_ZAR.read_bytes())

            output = Path(tmp) / "actor_inventory.json"
            inventory = inventory_actors(actor_root, output, sample_limit=5)

            self.assertTrue(output.exists())

        self.assertEqual(inventory["file_count"], 3)
        self.assertEqual(inventory["archive_count"], 2)
        self.assertEqual(inventory["loose_cmb_count"], 1)
        self.assertEqual(inventory["embedded_file_count"], 11)
        self.assertEqual(inventory["embedded_type_counts"]["cmb"], 2)
        self.assertEqual(inventory["embedded_type_counts"]["zsi"], 4)
        self.assertEqual(inventory["embedded_type_counts"]["csab"], 4)
        self.assertEqual(inventory["embedded_type_counts"]["cmab"], 1)
        self.assertEqual(inventory["animation_file_counts"]["csab"], 4)
        self.assertEqual(inventory["animation_file_counts"]["cmab"], 1)
        self.assertEqual(inventory["archive_counts"]["with_known_animation"], 1)
        self.assertEqual(inventory["archive_counts"]["static_payload_only"], 1)
        self.assertEqual(
            inventory["archive_support_counts"]["static_payload_supported_by_current_exporter"],
            1,
        )
        self.assertEqual(
            inventory["archive_support_counts"]["needs_skeleton_skinning_and_animation_support"],
            1,
        )
        self.assertEqual(inventory["cmb_counts"]["discovered"], 3)
        self.assertEqual(inventory["cmb_counts"]["parsed"], 3)
        self.assertEqual(inventory["cmb_counts"]["static_candidate"], 2)
        self.assertEqual(inventory["cmb_counts"]["nonstatic_candidate"], 1)
        self.assertEqual(
            inventory["model_support_counts"]["static_cmb_export_supported"],
            2,
        )
        self.assertEqual(
            inventory["model_support_counts"]["needs_skinning_mode_1_and_2_support"],
            1,
        )
        self.assertIn("0", inventory["skinning_mode_primitive_counts"])

        self.assertIn("1", inventory["skinning_mode_primitive_counts"])
        self.assertIn("2", inventory["skinning_mode_primitive_counts"])
        self.assertEqual(inventory["primitive_bone_count_counts"]["1"], 4)
        self.assertEqual(inventory["primitive_bone_count_counts"]["2"], 5)
        self.assertEqual(inventory["skinning_mode_bone_count_counts"]["mode=0;bones=1"], 4)
        self.assertEqual(inventory["skinning_mode_bone_count_counts"]["mode=1;bones=2"], 3)
        self.assertEqual(inventory["skinning_mode_bone_count_counts"]["mode=2;bones=2"], 2)
        self.assertEqual(inventory["bone_count_counts"]["1"], 2)
        self.assertEqual(inventory["bone_count_counts"]["19"], 1)
        skeleton_counts = inventory["skeleton_metadata_counts"]
        self.assertEqual(skeleton_counts["chunk_size_delta_counts"]["0"], 3)
        self.assertEqual(skeleton_counts["header_word_0c_counts"]["0"], 1)
        self.assertEqual(skeleton_counts["header_word_0c_counts"]["2"], 2)
        self.assertEqual(skeleton_counts["root_count_counts"]["1"], 3)
        self.assertEqual(skeleton_counts["max_depth_counts"]["0"], 2)
        self.assertEqual(skeleton_counts["max_depth_counts"]["5"], 1)
        self.assertEqual(skeleton_counts["bone_index_mismatch_count_counts"]["0"], 3)
        self.assertEqual(skeleton_counts["parent_out_of_range_count_counts"]["0"], 3)
        self.assertEqual(skeleton_counts["non_identity_scale_count_counts"]["0"], 3)
        self.assertEqual(skeleton_counts["nonzero_rotation_count_counts"]["0"], 2)
        self.assertEqual(skeleton_counts["nonzero_rotation_count_counts"]["6"], 1)
        self.assertEqual(skeleton_counts["nonzero_translation_count_counts"]["0"], 2)
        self.assertEqual(skeleton_counts["nonzero_translation_count_counts"]["14"], 1)
        self.assertEqual(inventory["animation_payload_magic_counts"]["csab"]["ascii:csab"], 4)
        self.assertEqual(inventory["animation_payload_magic_counts"]["cmab"]["ascii:cmab"], 1)
        self.assertEqual(
            inventory["animation_payload_declared_size_match_counts"]["csab"]["matches"],
            4,
        )
        self.assertEqual(
            inventory["animation_payload_declared_size_match_counts"]["cmab"]["matches"],
            1,
        )
        self.assertEqual(inventory["animation_payload_header_version_counts"]["csab"]["3"], 4)
        self.assertEqual(inventory["animation_payload_header_version_counts"]["cmab"]["1"], 1)
        self.assertEqual(inventory["animation_payload_header_field_counts"]["csab"]["word_0c"]["0"], 4)
        self.assertEqual(inventory["animation_payload_header_field_counts"]["csab"]["word_10"]["1"], 4)
        self.assertEqual(inventory["animation_payload_header_field_counts"]["csab"]["word_14"]["24"], 4)
        self.assertEqual(inventory["animation_payload_header_field_counts"]["csab"]["word_28"]["19"], 3)
        self.assertEqual(inventory["animation_payload_header_field_counts"]["csab"]["word_28"]["27"], 1)
        self.assertEqual(inventory["animation_payload_header_field_counts"]["cmab"]["word_0c"]["0"], 1)
        self.assertEqual(inventory["animation_payload_header_field_counts"]["cmab"]["word_10"]["1"], 1)
        self.assertEqual(inventory["animation_payload_header_field_counts"]["cmab"]["word_14"]["32"], 1)
        self.assertEqual(
            inventory["animation_payload_archive_support_counts"]["csab"][
                "needs_skeleton_skinning_and_animation_support"
            ],
            4,
        )
        self.assertEqual(
            inventory["animation_payload_archive_support_counts"]["cmab"][
                "needs_skeleton_skinning_and_animation_support"
            ],
            1,
        )
        self.assertEqual(inventory["animation_payload_size_summary"]["csab"]["min"], 4160)
        self.assertEqual(inventory["animation_payload_size_summary"]["csab"]["max"], 13640)
        self.assertEqual(inventory["animation_payload_size_summary"]["cmab"]["count"], 1)
        self.assertEqual(inventory["csab_node_table_counts"]["valid_node_tables"], 4)
        self.assertEqual(inventory["csab_node_table_counts"].get("invalid_node_tables", 0), 0)
        self.assertEqual(
            inventory["csab_node_table_counts"]["skeleton_bone_table_entries"],
            76,
        )
        self.assertEqual(inventory["csab_node_table_counts"]["animated_node_offsets"], 60)
        self.assertEqual(
            inventory["csab_node_table_counts"]["unused_skeleton_bone_entries"],
            16,
        )
        self.assertEqual(inventory["csab_anod_record_counts"]["valid_record_sets"], 4)
        self.assertEqual(inventory["csab_anod_record_counts"].get("invalid_record_sets", 0), 0)
        self.assertEqual(inventory["csab_anod_record_counts"]["anod_records"], 60)
        self.assertEqual(inventory["csab_anod_record_counts"]["channel_offset_entries"], 600)
        self.assertEqual(inventory["csab_anod_record_counts"]["active_channel_offsets"], 188)
        self.assertEqual(inventory["csab_anod_record_counts"]["zero_channel_offsets"], 412)
        self.assertEqual(
            inventory["csab_anod_record_counts"][
                "payloads_record_low16_bone_indices_match_bone_table"
            ],
            4,
        )
        self.assertEqual(
            inventory["csab_anod_record_counts"]["payloads_channel_offsets_valid"],
            4,
        )
        self.assertEqual(inventory["csab_anod_active_channel_count_counts"]["1"], 10)
        self.assertEqual(inventory["csab_anod_active_channel_count_counts"]["3"], 38)
        self.assertEqual(inventory["csab_anod_active_channel_count_counts"]["6"], 8)
        self.assertEqual(inventory["csab_anod_channel_slot_active_counts"]["5"], 60)
        self.assertEqual(inventory["csab_anod_record_field_04_high16_counts"]["0"], 60)
        self.assertEqual(inventory["csab_anod_channel_block_counts"]["valid_block_sets"], 4)
        self.assertEqual(inventory["csab_anod_channel_block_counts"].get("invalid_block_sets", 0), 0)
        self.assertEqual(inventory["csab_anod_channel_block_counts"]["channel_blocks"], 188)
        self.assertEqual(
            inventory["csab_anod_channel_block_counts"]["channel_block_key_entries"],
            1430,
        )
        self.assertEqual(
            inventory["csab_anod_channel_block_counts"]["payloads_type2_key_frames_valid"],
            4,
        )
        self.assertEqual(
            inventory["csab_anod_channel_block_class_counts"]["type1_f32_const_len24"],
            17,
        )
        self.assertEqual(
            inventory["csab_anod_channel_block_class_counts"][
                "type2_f32_keys_len16_plus_count16"
            ],
            171,
        )
        self.assertEqual(
            inventory["csab_anod_channel_block_class_key_entry_counts"][
                "type2_f32_keys_len16_plus_count16"
            ],
            1413,
        )
        self.assertEqual(inventory["csab_anod_channel_block_key_count_counts"]["4"], 46)
        self.assertEqual(
            inventory["csab_anod_channel_block_slot_class_counts"][
                "slot=5;class=type2_f32_keys_len16_plus_count16"
            ],
            60,
        )
        self.assertEqual(
            inventory["csab_skeleton_match_counts"]["matches_single_cmb_bone_count"],
            4,
        )
        self.assertEqual(
            inventory["csab_skeleton_match_counts"][
                "animated_bone_count_lte_skeleton_candidate"
            ],
            4,
        )

        self.assertEqual(
            inventory["csab_target_resolution_counts"]["single_bone_count_match"],
            4,
        )
        self.assertEqual(
            inventory["csab_resolved_target_support_counts"][
                "needs_skinning_mode_1_and_2_support"
            ],
            4,
        )
        self.assertEqual(
            inventory["csab_playback_blocker_counts"]["target_needs_skinning_support"],
            4,
        )
        self.assertEqual(inventory["csab_playback_candidate_node_table_counts"], {})
        self.assertEqual(inventory["csab_playback_candidate_samples"], [])
        self.assertEqual(inventory["csab_skeleton_match_samples"], [])
        self.assertEqual(len(inventory["animation_payload_samples"]), 5)
        self.assertEqual(inventory["animation_payload_samples"][0]["magic"], "ascii:csab")
        self.assertEqual(inventory["animation_payload_samples"][0]["header_version"], 3)
        self.assertEqual(inventory["animation_payload_samples"][0]["header_fields"]["word_14"], 24)
        self.assertEqual(
            inventory["animation_payload_samples"][0]["header_candidates"],
            {
                "frame_count_candidate": 19,
                "animated_bone_count_candidate": 15,
                "skeleton_bone_count_candidate": 19,
            },
        )
        self.assertEqual(
            inventory["animation_payload_samples"][0]["node_table"]["node_offset_table_start"],
            96,
        )
        self.assertEqual(
            inventory["animation_payload_samples"][0]["node_table"]["unused_skeleton_bone_entries"],
            4,
        )
        self.assertEqual(
            inventory["animation_payload_samples"][0]["anod_records"]["anod_records"],
            15,
        )
        self.assertTrue(
            inventory["animation_payload_samples"][0]["anod_records"][
                "record_low16_bone_indices_match_bone_table"
            ]
        )
        self.assertEqual(
            inventory["animation_payload_samples"][0]["anod_channel_blocks"]["channel_blocks"],
            45,
        )
        self.assertEqual(
            inventory["animation_payload_samples"][0]["anod_channel_blocks"]["class_counts"][
                "type2_f32_keys_len16_plus_count16"
            ],
            44,
        )
        self.assertEqual(len(inventory["nonstatic_model_samples"]), 1)
        sample = inventory["nonstatic_model_samples"][0]
        self.assertEqual(sample["container_path"], "zelda_ahg.zar")
        self.assertEqual(sample["embedded_name"], "Model/hyliaman2.cmb")
        self.assertEqual(sample["support_status"], "needs_skinning_mode_1_and_2_support")
        self.assertEqual(sample["skeleton"]["bone_count"], 19)
        self.assertEqual(sample["skeleton"]["root_count"], 1)
        self.assertEqual(sample["skeleton"]["max_depth"], 5)
        self.assertEqual(inventory["parse_error_count"], 0)

    def test_cmab_audit_records_payload_markers_and_target_candidates(self) -> None:
        if not ZELDA_AHG_ZAR.exists() or not BDAN_OBJECTS_ZAR.exists():
            self.skipTest("local OOT3D CMAB actor fixtures are not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_ahg.zar").write_bytes(ZELDA_AHG_ZAR.read_bytes())
            (actor_root / "zelda_bdan_objects.zar").write_bytes(
                BDAN_OBJECTS_ZAR.read_bytes()
            )

            output = Path(tmp) / "cmab_audit.json"
            audit = audit_cmab_payloads(actor_root, output, sample_limit=4)

            self.assertTrue(output.exists())

        self.assertEqual(audit["archive_count"], 2)
        self.assertEqual(audit["archives_with_cmab"], 2)
        self.assertEqual(audit["cmab_count"], 8)
        self.assertEqual(audit["total_mmad_count"], 10)
        self.assertEqual(audit["archive_parse_error_count"], 0)
        self.assertEqual(audit["cmb_parse_error_count"], 0)
        self.assertEqual(audit["magic_counts"]["ascii:cmab"], 8)
        self.assertEqual(audit["declared_size_match_counts"]["matches"], 8)
        self.assertEqual(audit["header_version_counts"]["1"], 8)
        self.assertEqual(audit["layout_status_counts"]["markers_consistent"], 8)
        self.assertEqual(audit["mads_record_count_counts"]["1"], 6)
        self.assertEqual(audit["mads_record_count_counts"]["2"], 2)
        self.assertEqual(audit["mmads_per_payload_counts"]["1"], 6)
        self.assertEqual(audit["mmads_per_payload_counts"]["2"], 2)
        self.assertEqual(audit["mmads_scalar_track_status_counts"]["scalar_keyframes_decoded"], 2)
        self.assertEqual(audit["mmads_scalar_track_status_counts"]["raw_or_non_scalar_payload"], 8)
        self.assertEqual(audit["txpt_marker_status_counts"]["points_to_txpt"], 1)
        self.assertEqual(audit["txpt_marker_status_counts"]["no_txpt_offset"], 7)
        self.assertEqual(audit["txpt_texture_count_counts"]["3"], 1)
        self.assertEqual(audit["total_txpt_texture_count"], 3)
        self.assertEqual(
            audit["archive_support_counts"]["needs_skeleton_skinning_and_animation_support"],
            1,
        )
        self.assertEqual(
            audit["archive_support_counts"]["static_models_with_animation_data"],
            7,
        )
        self.assertEqual(audit["target_resolved_count"], 8)
        self.assertEqual(audit["target_unresolved_or_ambiguous_count"], 0)
        self.assertEqual(audit["target_resolution_counts"]["single_exact_name_match"], 7)
        self.assertEqual(audit["target_resolution_counts"]["single_contained_name_match"], 1)
        self.assertEqual(audit["target_support_counts"]["static_cmb_export_supported"], 7)
        self.assertEqual(
            audit["target_support_counts"]["needs_skinning_mode_1_and_2_support"],
            1,
        )
        self.assertLessEqual(len(audit["sample_records"]), 4)
        self.assertEqual(len(audit["records"]), 8)

    def test_cmab_audit_resolves_link_child_material_targets_by_texture_names(self) -> None:
        if not ZELDA_LINK_CHILD_NEW_ZAR.exists():
            self.skipTest("local OOT3D Link child fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_link_child_new.zar").write_bytes(
                ZELDA_LINK_CHILD_NEW_ZAR.read_bytes()
            )

            audit = audit_cmab_payloads(actor_root, Path(tmp) / "cmab_audit.json")

        self.assertEqual(audit["cmab_count"], 2)
        self.assertEqual(audit["target_unresolved_or_ambiguous_count"], 0)
        self.assertEqual(audit["target_resolution_counts"]["single_texture_name_match"], 2)
        records = {record["cmab_name"]: record for record in audit["records"]}
        self.assertEqual(
            records["child/misc/childlink_eye.cmab"]["target_cmb"]["embedded_name"],
            "child/model/childlink_v2.cmb",
        )
        self.assertEqual(
            records["child/misc/childlink_mouth.cmab"]["target_cmb"]["embedded_name"],
            "child/model/childlink_v2.cmb",
        )
        self.assertEqual(
            records["child/misc/childlink_eye.cmab"]["layout"]["string_table_names"][0],
            "c_eye01",
        )
        self.assertEqual(records["child/misc/childlink_eye.cmab"]["frame_count_candidate"], 8)
        eye_layout = records["child/misc/childlink_eye.cmab"]["layout"]
        self.assertEqual(eye_layout["txpt_offset_candidate"], 0x84)
        self.assertEqual(
            eye_layout["txpt_offset_candidate_marker_status"],
            "does_not_point_to_txpt",
        )
        self.assertEqual(eye_layout["txpt_offset"], 0xA4)
        self.assertEqual(eye_layout["txpt_offset_resolution_status"], "scanned_txpt_marker")
        self.assertEqual(eye_layout["txpt_texture_record_count"], 8)
        self.assertEqual(eye_layout["txpt_texture_records"][0]["name"], "c_eye01")
        self.assertEqual(eye_layout["txpt_texture_records"][0]["data_size"], 8192)
        self.assertEqual(eye_layout["txpt_texture_records"][0]["width"], 64)
        self.assertEqual(eye_layout["txpt_texture_records"][0]["height"], 64)
        self.assertEqual(eye_layout["txpt_texture_records"][0]["texture_format"], 0x6754)
        self.assertEqual(eye_layout["txpt_texture_records"][0]["data_type"], 0x8363)
        self.assertEqual(eye_layout["txpt_texture_records"][0]["payload_status"], "payload_in_bounds")
        eye_mmad = eye_layout["mmad_records"][0]
        self.assertTrue(eye_mmad["scalar_keyframes_decoded"])
        self.assertEqual(eye_mmad["keyframe_count_candidate"], 8)
        self.assertEqual(eye_mmad["last_frame_candidate"], 7)
        self.assertEqual(eye_mmad["scalar_keyframe_last"]["frame"], 7)
        self.assertEqual(eye_mmad["scalar_keyframe_last"]["value"], 7.0)
        self.assertEqual(
            records["child/misc/childlink_mouth.cmab"]["layout"]["string_table_names"][0],
            "c_mouth01",
        )
        self.assertEqual(records["child/misc/childlink_mouth.cmab"]["frame_count_candidate"], 4)
        mouth_layout = records["child/misc/childlink_mouth.cmab"]["layout"]
        self.assertEqual(mouth_layout["txpt_offset_candidate"], 0x64)
        self.assertEqual(mouth_layout["txpt_offset"], 0x84)
        self.assertEqual(mouth_layout["txpt_offset_resolution_status"], "scanned_txpt_marker")
        self.assertEqual(mouth_layout["txpt_texture_record_count"], 4)
        self.assertEqual(mouth_layout["txpt_texture_records"][0]["name"], "c_mouth01")
        self.assertEqual(mouth_layout["txpt_texture_records"][0]["data_size"], 2048)
        self.assertEqual(mouth_layout["txpt_texture_records"][0]["width"], 32)
        self.assertEqual(mouth_layout["txpt_texture_records"][0]["height"], 32)
        mouth_mmad = mouth_layout["mmad_records"][0]
        self.assertTrue(mouth_mmad["scalar_keyframes_decoded"])
        self.assertEqual(mouth_mmad["keyframe_count_candidate"], 4)
        self.assertEqual(mouth_mmad["last_frame_candidate"], 3)

    def test_actor_qdb_audit_records_demo_payload_headers_and_duplicates(self) -> None:
        if (
            not ZELDA_KEEP_ZAR.exists()
            or not ZELDA_KEEP_OPENING_ZAR.exists()
            or not ZELDA_ZL4_ZAR.exists()
        ):
            self.skipTest("local OOT3D QDB actor fixtures are not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_keep.zar").write_bytes(ZELDA_KEEP_ZAR.read_bytes())
            (actor_root / "zelda_keep_opening.zar").write_bytes(
                ZELDA_KEEP_OPENING_ZAR.read_bytes()
            )
            (actor_root / "zelda_zl4.zar").write_bytes(ZELDA_ZL4_ZAR.read_bytes())

            output = Path(tmp) / "actor_qdb_audit.json"
            audit = audit_actor_qdb_payloads(actor_root, output, sample_limit=5)

            self.assertTrue(output.exists())

        self.assertEqual(audit["format"], "oot3d_actor_qdb_audit_v1")
        self.assertEqual(audit["zar_file_count"], 3)
        self.assertEqual(audit["parsed_zar_count"], 3)
        self.assertEqual(audit["parse_error_count"], 0)
        self.assertEqual(audit["qdb_file_count"], 27)
        self.assertEqual(audit["archive_with_qdb_count"], 3)
        self.assertEqual(audit["total_size"], 38144)
        self.assertEqual(
            audit["size_summary"],
            {
                "count": 27,
                "min": 128,
                "max": 8864,
                "total": 38144,
                "unique_size_count": 10,
            },
        )
        self.assertEqual(
            audit["archive_qdb_counts"],
            {
                "zelda_keep.zar": 12,
                "zelda_keep_opening.zar": 12,
                "zelda_zl4.zar": 3,
            },
        )
        self.assertEqual(audit["qdb_count_distribution"], {"12": 2, "3": 1})
        self.assertEqual(audit["embedded_parent_dir_counts"], {"demo_lowercase": 27})
        self.assertEqual(audit["type_name_counts"], {"qdb": 27})
        self.assertEqual(audit["magic4_counts"], {"ascii: BDQ": 27})
        self.assertEqual(audit["version_counts"], {"3": 27})
        self.assertEqual(audit["size_mod16_counts"], {"0": 27})
        self.assertEqual(audit["header_word_counts"]["word_00"], {"1363427872": 27})
        self.assertEqual(audit["header_word_counts"]["word_01"], {"3": 27})
        self.assertEqual(audit["embedded_name_count"], 15)
        self.assertEqual(audit["embedded_stem_count"], 15)
        self.assertEqual(audit["duplicate_embedded_names"]["demo/fuusa_data.qdb"], 2)
        self.assertEqual(audit["duplicate_embedded_stems"]["fuusa_data"], 2)
        self.assertEqual(len(audit["duplicate_embedded_names"]), 12)
        self.assertEqual(audit["issue_count"], 0)
        self.assertEqual(audit["issues"], [])
        self.assertLessEqual(len(audit["sample_records"]), 5)
        self.assertEqual(len(audit["records"]), 27)

    def test_actor_ccb_audit_records_camera_payload_header_and_offsets(self) -> None:
        if not ZELDA_ZL4_ZAR.exists():
            self.skipTest("local OOT3D CCB actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_zl4.zar").write_bytes(ZELDA_ZL4_ZAR.read_bytes())

            output = Path(tmp) / "actor_ccb_audit.json"
            audit = audit_actor_ccb_payloads(actor_root, output, sample_limit=3)

            self.assertTrue(output.exists())

        self.assertEqual(audit["format"], "oot3d_actor_ccb_audit_v1")
        self.assertEqual(audit["zar_file_count"], 1)
        self.assertEqual(audit["parsed_zar_count"], 1)
        self.assertEqual(audit["parse_error_count"], 0)
        self.assertEqual(audit["ccb_file_count"], 1)
        self.assertEqual(audit["archive_with_ccb_count"], 1)
        self.assertEqual(audit["total_size"], 8304)
        self.assertEqual(
            audit["size_summary"],
            {
                "count": 1,
                "min": 8304,
                "max": 8304,
                "total": 8304,
                "unique_size_count": 1,
            },
        )
        self.assertEqual(audit["archive_ccb_counts"], {"zelda_zl4.zar": 1})
        self.assertEqual(audit["ccb_count_distribution"], {"1": 1})
        self.assertEqual(audit["embedded_parent_dir_counts"], {"ccam": 1})
        self.assertEqual(audit["type_name_counts"], {"ccb": 1})
        self.assertEqual(audit["magic4_counts"], {"hex:63636200": 1})
        self.assertEqual(audit["version_counts"], {"3": 1})
        self.assertEqual(audit["declared_size_delta_counts"], {"48": 1})
        self.assertEqual(audit["size_mod16_counts"], {"0": 1})
        self.assertEqual(audit["caad_count_counts"], {"11": 1})
        self.assertEqual(audit["caad_marker_count_counts"], {"11": 1})
        self.assertEqual(audit["mads_marker_count_counts"], {"11": 1})
        self.assertEqual(audit["cmad_marker_count_counts"], {"27": 1})
        self.assertEqual(
            audit["caad_index_sequence_counts"],
            {"zero_based_contiguous": 1},
        )
        self.assertEqual(audit["issue_count"], 0)
        self.assertEqual(audit["issues"], [])
        self.assertEqual(len(audit["sample_records"]), 1)
        self.assertEqual(len(audit["records"]), 1)
        record = audit["records"][0]
        self.assertEqual(record["embedded_name"], "ccam/demo00.ccb")
        self.assertEqual(record["version_candidate"], 3)
        self.assertEqual(record["declared_size_candidate"], 8256)
        self.assertEqual(record["caad_count_candidate"], 11)
        self.assertEqual(record["table_offset_count"], 11)
        self.assertEqual(record["table_offsets"][0], 0x44)
        self.assertEqual(record["caad_records"][0]["marker"], "ascii:caad")
        self.assertEqual(record["caad_records"][0]["index"], 0)

    def test_actor_zsi_audit_records_bgdata_footer_layout(self) -> None:
        if not DK_LIGHTBOX_ZAR.exists() or not ZELDA_BOX_ZAR.exists():
            self.skipTest("local OOT3D actor ZSI fixtures are not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "dk_lightbox.zar").write_bytes(DK_LIGHTBOX_ZAR.read_bytes())
            (actor_root / "zelda_box.zar").write_bytes(ZELDA_BOX_ZAR.read_bytes())

            output = Path(tmp) / "actor_zsi_audit.json"
            audit = audit_actor_zsi_payloads(actor_root, output, sample_limit=3)

            self.assertTrue(output.exists())

        self.assertEqual(audit["format"], "oot3d_actor_zsi_audit_v1")
        self.assertEqual(audit["zar_file_count"], 2)
        self.assertEqual(audit["parsed_zar_count"], 2)
        self.assertEqual(audit["parse_error_count"], 0)
        self.assertEqual(audit["zsi_file_count"], 5)
        self.assertEqual(audit["archive_with_zsi_count"], 2)
        self.assertEqual(audit["total_size"], 1868)
        self.assertEqual(
            audit["size_summary"],
            {
                "count": 5,
                "min": 372,
                "max": 380,
                "total": 1868,
                "unique_size_count": 2,
            },
        )
        self.assertEqual(
            audit["archive_zsi_counts"],
            {"dk_lightbox.zar": 4, "zelda_box.zar": 1},
        )
        self.assertEqual(audit["zsi_count_distribution"], {"1": 1, "4": 1})
        self.assertEqual(audit["embedded_parent_dir_counts"], {"collision": 5})
        self.assertEqual(audit["type_name_counts"], {"zsi": 5})
        self.assertEqual(audit["magic4_counts"], {"hex:5a534901": 5})
        self.assertEqual(audit["bgdata_marker_counts"], {"ShUnqueen": 5})
        self.assertEqual(audit["version_counts"], {"3": 5})
        self.assertEqual(audit["declared_size_delta_counts"], {"60": 5})
        self.assertEqual(audit["size_mod4_counts"], {"0": 5})
        self.assertEqual(audit["size_mod16_counts"], {"12": 1, "4": 4})
        self.assertEqual(audit["footer_layout_status_counts"], {"valid": 5})
        self.assertEqual(
            audit["totals"],
            {
                "polygon_count": 60,
                "surface_type_count": 6,
                "unknown_count": 5,
                "vertex_count": 40,
            },
        )
        self.assertEqual(audit["footer_field_counts"]["surface_type_count"], {"1": 4, "2": 1})
        self.assertEqual(audit["footer_field_counts"]["unknown_count"], {"1": 5})
        self.assertEqual(audit["footer_field_counts"]["reserved_0"], {"0": 5})
        self.assertEqual(audit["footer_field_counts"]["reserved_1"], {"0": 5})
        self.assertEqual(audit["footer_field_counts"]["vertex_rel_offset"], {"8": 5})
        self.assertEqual(audit["footer_field_counts"]["terminator"], {"0": 5})
        self.assertEqual(audit["scene_setup_count_counts"], {"0": 5})
        self.assertEqual(audit["embedded_cmb_count_counts"], {"0": 5})
        self.assertEqual(audit["collision_candidate_count_counts"], {"0": 5})
        self.assertEqual(audit["embedded_name_count"], 5)
        self.assertEqual(audit["duplicate_embedded_names"], {})
        self.assertEqual(audit["issue_count"], 0)
        self.assertEqual(audit["issues"], [])
        self.assertLessEqual(len(audit["sample_records"]), 3)
        self.assertEqual(len(audit["records"]), 5)
        record = audit["records"][0]
        self.assertEqual(record["footer"]["layout_status"], "valid")
        self.assertTrue(
            record["footer"]["layout_checks"]["polygon_rel_offset_matches_vertex_count"]
        )
        self.assertTrue(
            record["footer"]["layout_checks"]["surface_type_rel_offset_matches_polygon_count"]
        )

    def test_animation_like_audit_records_anb_and_faceb_payload_shapes(self) -> None:
        if not ZELDA_LINK_BOY_ULTRA_ZAR.exists() or not ZELDA_LINK_BOY_NEW_ZAR.exists():
            self.skipTest("local OOT3D Link animation-like fixtures are not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_link_boy_ultra.zar").write_bytes(
                ZELDA_LINK_BOY_ULTRA_ZAR.read_bytes()
            )
            (actor_root / "zelda_link_boy_new.zar").write_bytes(
                ZELDA_LINK_BOY_NEW_ZAR.read_bytes()
            )

            output = Path(tmp) / "animation_like_audit.json"
            audit = audit_actor_animation_like_payloads(actor_root, output, sample_limit=6)

            self.assertTrue(output.exists())

        self.assertEqual(audit["archive_count"], 2)
        self.assertEqual(audit["payload_type_counts"]["anb"], 561)
        self.assertEqual(audit["payload_type_counts"]["faceb"], 582)
        self.assertEqual(audit["archives_with_type"]["anb"], 1)
        self.assertEqual(audit["archives_with_type"]["faceb"], 1)
        self.assertEqual(audit["archive_payload_count_counts"]["anb"]["561"], 1)
        self.assertEqual(audit["archive_payload_count_counts"]["faceb"]["582"], 1)
        self.assertEqual(audit["payload_size_summary"]["anb"]["min"], 276)
        self.assertEqual(audit["payload_size_summary"]["anb"]["max"], 48918)
        self.assertEqual(audit["payload_size_summary"]["faceb"]["min"], 12)
        self.assertEqual(audit["payload_size_summary"]["faceb"]["max"], 124)
        self.assertEqual(audit["first_word_high16_counts"]["anb"]["8"], 561)
        self.assertEqual(audit["word04_counts"]["anb"]["2425356288"], 561)
        self.assertEqual(audit["faceb_magic_status_counts"]["fkb01"], 582)
        self.assertEqual(audit["faceb_size_match_counts"]["matches"], 582)
        self.assertEqual(audit["faceb_entry_total"], 1480)
        self.assertEqual(audit["faceb_entry_count_counts"]["1"], 448)
        self.assertEqual(audit["payload_name_stats"]["anb"]["unique_name_count"], 561)
        self.assertEqual(
            audit["payload_name_stats"]["anb"]["multiplicity_counts"]["1"],
            561,
        )
        self.assertEqual(audit["payload_name_stats"]["faceb"]["unique_name_count"], 582)
        self.assertLessEqual(len(audit["sample_records"]), 6)
        self.assertEqual(len(audit["records"]), 1143)

    def test_actor_inventory_samples_rigid_playback_candidate_f32_channels(self) -> None:
        if not ZELDA_BOX_ZAR.exists():
            self.skipTest("local OOT3D zelda_box.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir(parents=True)
            (actor_root / "zelda_box.zar").write_bytes(ZELDA_BOX_ZAR.read_bytes())

            inventory = inventory_actors(actor_root, sample_limit=10)

        self.assertEqual(
            inventory["csab_playback_blocker_counts"][
                "target_export_supported_by_current_rigid_path"
            ],
            5,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_f32_sampler_counts"]["valid_sample_sets"],
            5,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_f32_sampler_counts"].get(
                "invalid_sample_sets", 0
            ),
            0,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_f32_sampler_counts"][
                "sampled_channel_blocks"
            ],
            297,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_f32_sampler_counts"][
                "sampled_channel_values"
            ],
            88353,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_f32_sampler_counts"][
                "finite_sampled_channel_values"
            ],
            88353,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_f32_sampler_counts"][
                "non_f32_channel_blocks"
            ],
            0,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_f32_sampler_frame_count_counts"]["326"],
            1,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_f32_sampler_slot_sample_counts"]["8"],
            9219,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_pose_sample_counts"][
                "valid_pose_sample_sets"
            ],
            5,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_pose_sample_counts"].get(
                "invalid_pose_sample_sets", 0
            ),
            0,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_pose_sample_counts"][
                "sampled_pose_frames"
            ],
            10,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_pose_sample_counts"][
                "sampled_skeleton_bone_transforms"
            ],
            94,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_pose_sample_counts"][
                "sampled_channel_values"
            ],
            594,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_pose_sample_counts"][
                "finite_world_matrix_entries"
            ],
            1504,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_pose_sample_slot_counts"]["8"],
            54,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_full_pose_sample_counts"][
                "valid_pose_sample_sets"
            ],
            5,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_full_pose_sample_counts"].get(
                "invalid_pose_sample_sets", 0
            ),
            0,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_full_pose_sample_counts"][
                "sampled_pose_frames"
            ],
            983,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_full_pose_sample_counts"][
                "sampled_skeleton_bone_transforms"
            ],
            13893,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_full_pose_sample_counts"][
                "sampled_channel_values"
            ],
            88353,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_full_pose_sample_counts"][
                "finite_world_matrix_entries"
            ],
            222288,
        )
        self.assertEqual(
            inventory["csab_playback_candidate_full_pose_sample_slot_counts"]["8"],
            9219,
        )
        sample = inventory["csab_playback_candidate_samples"][0]["f32_sampler"]
        self.assertTrue(sample["valid"])
        self.assertEqual(sample["frame_count_candidate"], 130)
        self.assertEqual(sample["sampled_channel_values"], 2358)
        self.assertEqual(sample["value_range_by_slot"]["4"]["min"], -1.57084)
        pose_sample = inventory["csab_playback_candidate_samples"][0]["pose_sample"]
        self.assertTrue(pose_sample["valid"])
        self.assertEqual(pose_sample["sampled_pose_frame_indices"], [0, 130])
        self.assertEqual(pose_sample["target_bone_count"], 3)
        self.assertEqual(pose_sample["world_matrix_entries"], 96)
        self.assertEqual(
            pose_sample["world_translation_range_by_axis"]["z"]["max"],
            1982.0,
        )
        full_pose_sample = inventory["csab_playback_candidate_samples"][0][
            "full_pose_sample"
        ]
        self.assertTrue(full_pose_sample["valid"])
        self.assertEqual(full_pose_sample["sampled_pose_frames"], 131)
        self.assertEqual(full_pose_sample["sampled_pose_frame_indices"][-1], 130)
        self.assertEqual(full_pose_sample["sampled_channel_values"], 2358)
        self.assertEqual(full_pose_sample["finite_world_matrix_entries"], 6288)

    def test_export_csab_rigid_tracks_writes_pilot_track_manifest(self) -> None:
        if not ZELDA_BOX_ZAR.exists():
            self.skipTest("local OOT3D zelda_box.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "cdemo_box_boxA_tracks.json"
            export = export_csab_rigid_tracks(
                ZELDA_BOX_ZAR,
                "Anim/cdemo_box_boxA.csab",
                "Model/tr_box.cmb",
                output,
            )
            written = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(export["format"], "oot3d_csab_rigid_track_export_v1")
        self.assertEqual(written["format"], export["format"])
        self.assertEqual(export["csab_name"], "Anim/cdemo_box_boxA.csab")
        self.assertEqual(export["target_cmb_name"], "Model/tr_box.cmb")
        self.assertEqual(export["frame_count_candidate"], 130)
        self.assertEqual(export["frame_slot_count"], 131)
        self.assertEqual(export["counts"]["track_count"], 3)
        self.assertEqual(export["counts"]["channel_count"], 18)
        self.assertEqual(export["counts"]["const_channel_count"], 17)
        self.assertEqual(export["counts"]["keyed_channel_count"], 1)
        self.assertEqual(export["counts"]["keyframe_count"], 28)
        self.assertTrue(export["validation"]["full_pose_sample"]["valid"])
        self.assertEqual(
            export["validation"]["full_pose_sample"]["finite_world_matrix_entries"],
            6288,
        )
        self.assertEqual(export["tracks"][0]["channels"][0]["semantic"], "translation_x")
        keyed_channels = [
            channel
            for track in export["tracks"]
            for channel in track["channels"]
            if channel["encoding"] == "keyed_f32_hermite"
        ]
        self.assertEqual(len(keyed_channels), 1)
        self.assertEqual(keyed_channels[0]["key_count"], 28)

    def test_batch_export_csab_rigid_tracks_writes_manifest(self) -> None:
        if not ZELDA_BOX_ZAR.exists():
            self.skipTest("local OOT3D zelda_box.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_box.zar").write_bytes(ZELDA_BOX_ZAR.read_bytes())
            output = Path(tmp) / "tracks"

            manifest_path = batch_export_csab_rigid_tracks(actor_root, output)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

        self.assertEqual(manifest["format"], "oot3d_csab_rigid_track_batch_v1")
        self.assertEqual(manifest["considered_csab"], 5)
        self.assertEqual(manifest["exported"], 5)
        self.assertEqual(manifest["failed"], 0)
        self.assertEqual(manifest["target_unresolved_or_missing"], 0)
        self.assertEqual(manifest["target_needs_skinning_support"], 0)
        self.assertEqual(manifest["counts"]["track_count"], 36)
        self.assertEqual(manifest["counts"]["channel_count"], 297)
        self.assertEqual(manifest["counts"]["const_channel_count"], 275)
        self.assertEqual(manifest["counts"]["keyed_channel_count"], 22)
        self.assertEqual(manifest["counts"]["keyframe_count"], 238)
        self.assertEqual(manifest["counts"]["frame_slot_count"], 983)
        self.assertEqual(manifest["counts"]["sampled_channel_values"], 88353)
        self.assertEqual(manifest["counts"]["finite_world_matrix_entries"], 222288)
        self.assertEqual(
            sum(1 for record in manifest["records"] if record["status"] == "exported"),
            5,
        )

    def test_export_csab_skeleton_tracks_writes_skinned_target_manifest(self) -> None:
        if not ZELDA_DEKUBABA_ZAR.exists():
            self.skipTest("local OOT3D zelda_dekubaba.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "dekubaba_tracks.json"
            export = export_csab_skeleton_tracks(
                ZELDA_DEKUBABA_ZAR,
                "Anim/db_P_tobidasu.csab",
                "Model/dekubaba.cmb",
                output,
            )
            written = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(export["format"], "oot3d_csab_skeleton_track_export_v1")
        self.assertEqual(written["format"], export["format"])
        self.assertEqual(export["target_model_name"], "dekubaba")
        self.assertEqual(export["target_support_status"], "needs_skinning_mode_1_support")
        self.assertEqual(export["frame_count_candidate"], 24)
        self.assertEqual(export["frame_slot_count"], 25)
        self.assertEqual(export["animated_bone_count_candidate"], 3)
        self.assertEqual(export["skeleton_bone_count_candidate"], 3)
        self.assertEqual(export["counts"]["track_count"], 3)
        self.assertEqual(export["counts"]["channel_count"], 12)
        self.assertEqual(export["counts"]["const_channel_count"], 8)
        self.assertEqual(export["counts"]["keyed_channel_count"], 4)
        self.assertEqual(export["counts"]["keyframe_count"], 42)
        self.assertTrue(export["validation"]["full_pose_sample"]["valid"])
        self.assertEqual(
            export["validation"]["full_pose_sample"]["finite_world_matrix_entries"],
            1200,
        )
        self.assertEqual(
            export["validation"]["full_pose_sample"]["non_f32_channel_blocks"],
            0,
        )

    def test_export_skinned_animation_pose_samples_deforms_skinned_fixture(self) -> None:
        if not ZELDA_DEKUBABA_ZAR.exists():
            self.skipTest("local OOT3D zelda_dekubaba.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "dekubaba_pose_sample.json"
            export = export_skinned_animation_pose_samples(
                ZELDA_DEKUBABA_ZAR,
                "Anim/db_P_tobidasu.csab",
                "Model/dekubaba.cmb",
                output,
                sample_frames=[0, 12, 24],
                sample_limit=5,
            )
            written = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(export["format"], "oot3d_skinned_animation_pose_sample_v1")
        self.assertEqual(written["format"], export["format"])
        self.assertEqual(export["target_model_name"], "dekubaba")
        self.assertEqual(export["target_support_status"], "needs_skinning_mode_1_support")
        self.assertEqual(export["frame_count_candidate"], 24)
        self.assertEqual(export["sample_frames"], [0, 12, 24])
        self.assertEqual(export["bind_pose_counts"]["skinned_vertex_rows"], 91)
        self.assertEqual(export["bind_pose_counts"]["mode_1_primitive_count"], 1)
        self.assertEqual(export["pose_counts"]["sampled_pose_frames"], 3)
        self.assertEqual(export["pose_counts"]["sampled_channel_values"], 36)
        self.assertEqual(export["pose_counts"]["finite_world_matrix_entries"], 144)
        self.assertEqual(export["pose_counts"]["non_f32_channel_blocks"], 0)
        self.assertEqual(export["counts"]["sampled_vertex_rows"], 273)
        self.assertEqual(export["counts"]["finite_position_rows"], 273)
        self.assertEqual(export["counts"]["finite_normal_rows"], 273)
        self.assertEqual(export["counts"]["changed_position_rows"], 273)
        self.assertEqual(export["counts"]["validation_error_count"], 0)
        self.assertTrue(export["validation"]["valid"])
        self.assertEqual(len(export["frames"]), 3)
        self.assertEqual(export["frames"][0]["counts"]["sampled_vertex_rows"], 91)
        self.assertEqual(len(export["frames"][0]["vertex_samples"]), 5)

    def test_batch_export_skinned_animation_pose_samples_writes_manifest(self) -> None:
        if not (
            ZELDA_DEKUBABA_ZAR.exists()
            and ZELDA_FIELD_KEEP_ZAR.exists()
            and ZELDA_EC_ZAR.exists()
        ):
            self.skipTest("local OOT3D skinned actor fixtures are not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_dekubaba.zar").write_bytes(ZELDA_DEKUBABA_ZAR.read_bytes())
            (actor_root / "zelda_field_keep.zar").write_bytes(ZELDA_FIELD_KEEP_ZAR.read_bytes())
            (actor_root / "zelda_ec.zar").write_bytes(ZELDA_EC_ZAR.read_bytes())
            output = Path(tmp) / "pose_batch"

            manifest_path = batch_export_skinned_animation_pose_samples(
                actor_root,
                output,
                sample_limit=0,
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            exported_files_exist = all(
                (output / record["pose_sample_export"]).exists()
                for record in manifest["records"]
                if record["status"] == "exported"
            )

        self.assertEqual(manifest["format"], "oot3d_skinned_animation_pose_batch_v1")
        self.assertEqual(manifest["considered_csab"], 39)
        self.assertEqual(manifest["target_unresolved_or_missing"], 19)
        self.assertEqual(manifest["target_not_skinned"], 4)
        self.assertEqual(manifest["resolved_skinned_targets"], 16)
        self.assertEqual(manifest["exported"], 16)
        self.assertEqual(manifest["failed"], 0)
        self.assertEqual(manifest["support_status_counts"]["needs_skinning_mode_1_support"], 2)
        self.assertEqual(manifest["support_status_counts"]["needs_skinning_mode_2_support"], 12)
        self.assertEqual(
            manifest["support_status_counts"]["needs_skinning_mode_1_and_2_support"],
            2,
        )
        self.assertEqual(manifest["counts"]["source_vertex_rows"], 6766)
        self.assertEqual(manifest["counts"]["sampled_vertex_rows"], 20298)
        self.assertEqual(manifest["counts"]["finite_pose_rows"], 20298)
        self.assertEqual(manifest["counts"]["validation_error_count"], 0)
        self.assertEqual(manifest["pose_counts"]["non_f32_channel_blocks"], 0)
        self.assertEqual(manifest["pose_counts"]["finite_world_matrix_entries"], 11376)
        self.assertEqual(manifest["bind_pose_counts"]["skinned_primitive_count"], 71)
        self.assertTrue(exported_files_exist)

    def test_export_csab_skeleton_tracks_decodes_s16_rotation_channels(self) -> None:
        if not ZELDA_LINK_BOY_NEW_ZAR.exists():
            self.skipTest("local OOT3D zelda_link_boy_new.zar actor fixture is not present")

        export = export_csab_skeleton_tracks(
            ZELDA_LINK_BOY_NEW_ZAR,
            "boy/anim/sude_nwait.csab",
            "boy/model/link_v2.cmb",
        )

        self.assertEqual(export["format"], "oot3d_csab_skeleton_track_export_v1")
        self.assertEqual(export["target_model_name"], "link_v2")
        self.assertEqual(export["target_support_status"], "needs_skinning_mode_2_support")
        self.assertEqual(export["frame_count_candidate"], 44)
        self.assertEqual(export["frame_slot_count"], 45)
        self.assertEqual(export["animated_bone_count_candidate"], 21)
        self.assertEqual(export["skeleton_bone_count_candidate"], 25)
        self.assertEqual(export["counts"]["track_count"], 21)
        self.assertEqual(export["counts"]["channel_count"], 56)
        self.assertEqual(export["counts"]["const_channel_count"], 18)
        self.assertEqual(export["counts"]["keyed_channel_count"], 38)
        self.assertEqual(export["counts"]["keyframe_count"], 128)
        self.assertEqual(export["counts"]["encoding_counts"]["constant_s16_rotation"], 9)
        self.assertEqual(
            export["counts"]["encoding_counts"]["keyed_s16_rotation_hermite"],
            36,
        )
        self.assertTrue(export["validation"]["full_pose_sample"]["valid"])
        self.assertEqual(
            export["validation"]["full_pose_sample"]["finite_world_matrix_entries"],
            18000,
        )
        self.assertEqual(
            export["validation"]["full_pose_sample"]["non_f32_channel_blocks"],
            0,
        )

    def test_batch_export_csab_skeleton_tracks_writes_skinned_manifest(self) -> None:
        if not ZELDA_AM_ZAR.exists():
            self.skipTest("local OOT3D zelda_am.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_am.zar").write_bytes(ZELDA_AM_ZAR.read_bytes())
            output = Path(tmp) / "tracks"

            manifest_path = batch_export_csab_skeleton_tracks(actor_root, output)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

        self.assertEqual(manifest["format"], "oot3d_csab_skeleton_track_batch_v1")
        self.assertEqual(manifest["considered_csab"], 3)
        self.assertEqual(manifest["target_unresolved_or_missing"], 0)
        self.assertEqual(manifest["target_not_skinned"], 0)
        self.assertEqual(manifest["resolved_skinned_targets"], 3)
        self.assertEqual(manifest["target_unsupported_non_f32_channels"], 0)
        self.assertEqual(manifest["exported"], 3)
        self.assertEqual(manifest["failed"], 0)
        self.assertEqual(
            manifest["support_status_counts"]["needs_skinning_mode_2_support"],
            3,
        )
        self.assertEqual(
            manifest["exported_support_status_counts"]["needs_skinning_mode_2_support"],
            3,
        )
        self.assertEqual(manifest["unsupported_support_status_counts"], {})
        self.assertEqual(manifest["counts"]["track_count"], 12)
        self.assertEqual(manifest["counts"]["channel_count"], 36)
        self.assertEqual(manifest["counts"]["const_channel_count"], 15)
        self.assertEqual(manifest["counts"]["keyed_channel_count"], 21)
        self.assertEqual(manifest["counts"]["keyframe_count"], 223)
        self.assertEqual(manifest["counts"]["frame_slot_count"], 55)
        self.assertEqual(manifest["counts"]["sampled_pose_frames"], 55)
        self.assertEqual(manifest["counts"]["sampled_channel_values"], 651)
        self.assertEqual(manifest["counts"]["finite_world_matrix_entries"], 4400)
        self.assertEqual(manifest["counts"]["non_f32_channel_blocks"], 0)
        self.assertEqual(manifest["encoding_counts"]["constant_f32"], 15)
        self.assertEqual(manifest["encoding_counts"]["keyed_f32_hermite"], 21)
        self.assertEqual(manifest["unsupported_counts"]["non_f32_channel_blocks"], 0)
        self.assertEqual(
            sum(1 for record in manifest["records"] if record["status"] == "exported"),
            3,
        )

    def test_csab_target_resolution_audit_reports_unresolved_candidates(self) -> None:
        if not ZELDA_BV_ZAR.exists() or not ZELDA_KEEP_OPENING_ZAR.exists():
            self.skipTest("local OOT3D CSAB target-resolution fixtures are not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_bv.zar").write_bytes(ZELDA_BV_ZAR.read_bytes())
            (actor_root / "zelda_keep_opening.zar").write_bytes(
                ZELDA_KEEP_OPENING_ZAR.read_bytes()
            )
            output = Path(tmp) / "csab_targets.json"

            audit = audit_csab_target_resolution(actor_root, output)
            written = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(audit["format"], "oot3d_csab_target_resolution_audit_v1")
        self.assertEqual(written["format"], audit["format"])
        self.assertEqual(audit["considered_csab"], 25)
        self.assertEqual(audit["resolved_count"], 6)
        self.assertEqual(audit["unresolved_or_missing"], 19)
        self.assertEqual(audit["archive_parse_error_count"], 0)
        self.assertEqual(
            audit["target_resolution_counts"]["multiple_bone_count_unresolved"],
            8,
        )
        self.assertEqual(audit["target_resolution_counts"]["no_matching_cmb_bone_count"], 11)
        self.assertEqual(audit["target_resolution_counts"]["single_bone_count_match"], 6)
        self.assertEqual(audit["unresolved_archive_counts"]["zelda_bv.zar"], 8)
        self.assertEqual(
            audit["unresolved_archive_counts"]["zelda_keep_opening.zar"],
            11,
        )
        self.assertEqual(audit["unresolved_same_bone_candidate_count_counts"]["0"], 11)
        self.assertEqual(audit["unresolved_same_bone_candidate_count_counts"]["3"], 8)
        self.assertEqual(
            audit["unresolved_namespace_candidate_count_counts"]["0"],
            8,
        )
        self.assertEqual(
            audit["unresolved_namespace_candidate_count_counts"]["1"],
            6,
        )
        first = audit["unresolved_records"][0]
        self.assertEqual(first["target_resolution_status"], "multiple_bone_count_unresolved")
        self.assertEqual(first["same_bone_candidate_count"], 3)
        keep_record = next(
            record
            for record in audit["unresolved_records"]
            if record["archive_path"] == "zelda_keep_opening.zar"
        )
        self.assertEqual(keep_record["target_resolution_status"], "no_matching_cmb_bone_count")
        self.assertGreater(keep_record["namespace_candidate_count"], 0)

    def test_skinning_queue_audit_reports_rigid_archive_as_unblocked(self) -> None:
        if not ZELDA_BOX_ZAR.exists():
            self.skipTest("local OOT3D zelda_box.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_box.zar").write_bytes(ZELDA_BOX_ZAR.read_bytes())
            output = Path(tmp) / "skinning.json"

            audit = audit_actor_skinning_queue(actor_root, output)
            self.assertTrue(output.exists())

        self.assertEqual(audit["format"], "oot3d_actor_skinning_queue_v1")
        self.assertEqual(audit["model_counts"]["skinned_model_count"], 0)
        self.assertEqual(audit["csab_counts"]["considered"], 5)
        self.assertEqual(
            audit["csab_counts"]["target_export_supported_by_current_rigid_path"],
            5,
        )
        self.assertEqual(audit["csab_counts"]["target_needs_skinning_support"], 0)
        self.assertEqual(audit["parse_error_count"], 0)

    def test_skinning_queue_audit_reports_skinned_actor_fixture(self) -> None:
        if not ZELDA_AHG_ZAR.exists():
            self.skipTest("local OOT3D zelda_ahg.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_ahg.zar").write_bytes(ZELDA_AHG_ZAR.read_bytes())

            audit = audit_actor_skinning_queue(actor_root, sample_limit=10)

        self.assertEqual(audit["model_counts"]["skinned_model_count"], 1)
        self.assertEqual(
            audit["skinned_support_status_counts"]["needs_skinning_mode_1_and_2_support"],
            1,
        )
        self.assertGreater(audit["skinned_primitive_profile"]["skinned_primitive_count"], 0)
        self.assertGreater(audit["csab_counts"]["target_needs_skinning_support"], 0)
        self.assertEqual(audit["skinned_primitive_profile"]["invalid_bone_index_primitives"], 0)

    def test_skinning_layout_audit_reports_rigid_archive_as_unskinned(self) -> None:
        if not ZELDA_BOX_ZAR.exists():
            self.skipTest("local OOT3D zelda_box.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_box.zar").write_bytes(ZELDA_BOX_ZAR.read_bytes())

            audit = audit_actor_skinning_vertex_layout(actor_root)

        self.assertEqual(audit["format"], "oot3d_actor_skinning_vertex_layout_v1")
        self.assertEqual(audit["model_counts"]["skinned_model_count"], 0)
        self.assertEqual(audit["shape_counts"]["total"], 9)
        self.assertEqual(audit["primitive_counts"]["mode_0"], 9)
        self.assertEqual(audit["validation_error_count"], 0)

    def test_skinning_layout_audit_validates_mode1_fixture(self) -> None:
        if not ZELDA_DEKUBABA_ZAR.exists():
            self.skipTest("local OOT3D zelda_dekubaba.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_dekubaba.zar").write_bytes(ZELDA_DEKUBABA_ZAR.read_bytes())

            audit = audit_actor_skinning_vertex_layout(actor_root)

        self.assertEqual(audit["model_counts"]["skinned_model_count"], 1)
        self.assertEqual(audit["shape_counts"]["mode_1"], 1)
        self.assertEqual(audit["shape_counts"]["mode1_valid_layout"], 1)
        self.assertEqual(audit["shape_counts"]["mode1_vertex_rows"], 91)
        self.assertEqual(audit["extra_attribute_kind_counts"]["slot6_data"], 1)
        self.assertEqual(audit["extra_attribute_kind_counts"]["slot7_missing"], 1)
        self.assertEqual(audit["layout_counts"]["mode1_slot6_data_slot7_missing"], 1)
        self.assertEqual(audit["validation_error_count"], 0)

    def test_skinning_layout_audit_validates_mode2_fixture(self) -> None:
        if not ZELDA_FIELD_KEEP_ZAR.exists():
            self.skipTest("local OOT3D zelda_field_keep.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_field_keep.zar").write_bytes(ZELDA_FIELD_KEEP_ZAR.read_bytes())

            audit = audit_actor_skinning_vertex_layout(actor_root)

        self.assertEqual(audit["model_counts"]["skinned_model_count"], 1)
        self.assertEqual(audit["shape_counts"]["mode_2"], 1)
        self.assertEqual(audit["shape_counts"]["mode2_valid_layout"], 1)
        self.assertEqual(audit["shape_counts"]["mode2_vertex_rows"], 28)
        self.assertEqual(audit["layout_counts"]["mode2_slot6_data_slot7_data"], 1)
        self.assertEqual(audit["influence_width_counts"]["2"], 1)
        self.assertEqual(audit["weight_sum_counts"]["100"], 28)
        self.assertEqual(audit["nonzero_influence_counts"]["1"], 24)
        self.assertEqual(audit["nonzero_influence_counts"]["2"], 4)
        self.assertEqual(audit["validation_error_count"], 0)

    def test_skinning_layout_audit_validates_constant_index_fixture(self) -> None:
        if not ZELDA_EC_ZAR.exists():
            self.skipTest("local OOT3D zelda_ec.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_ec.zar").write_bytes(ZELDA_EC_ZAR.read_bytes())

            audit = audit_actor_skinning_vertex_layout(actor_root)

        self.assertEqual(audit["extra_attribute_kind_counts"]["slot6_constant"], 5)
        self.assertEqual(audit["layout_counts"]["mode2_slot6_constant_slot7_data"], 5)
        self.assertEqual(audit["influence_width_counts"]["4"], 1)
        self.assertEqual(audit["validation_error_count"], 0)

    def test_export_skinned_bind_pose_decodes_mode1_fixture(self) -> None:
        if not ZELDA_DEKUBABA_ZAR.exists():
            self.skipTest("local OOT3D zelda_dekubaba.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "dekubaba_skinned.json"
            export = export_skinned_bind_pose(
                ZELDA_DEKUBABA_ZAR,
                output,
                cmb_name="Model\\dekubaba.cmb",
                sample_limit=5,
            )
            written = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(export["format"], "oot3d_skinned_bind_pose_export_v1")
        self.assertEqual(written["format"], export["format"])
        self.assertEqual(export["model_name"], "dekubaba")
        self.assertEqual(export["bone_count"], 3)
        self.assertEqual(export["counts"]["mesh_count"], 1)
        self.assertEqual(export["counts"]["skinned_primitive_count"], 1)
        self.assertEqual(export["counts"]["mode_1_primitive_count"], 1)
        self.assertEqual(export["counts"]["mode_2_primitive_count"], 0)
        self.assertEqual(export["counts"]["skinned_vertex_rows"], 91)
        self.assertEqual(export["counts"]["influence_width_rows"]["1"], 91)
        self.assertEqual(export["counts"]["nonzero_influence_rows"]["1"], 91)
        self.assertEqual(export["counts"]["skeleton_bone_count"], 3)
        self.assertEqual(export["counts"]["finite_bind_world_matrix_entries"], 48)
        self.assertEqual(export["counts"]["validation_error_count"], 0)
        self.assertEqual(len(export["skeleton_bones"]), 3)
        self.assertEqual(export["skeleton_bones"][0]["parent_index"], -1)
        self.assertEqual(len(export["skeleton_bones"][0]["bind_world_matrix"]), 4)
        self.assertEqual(len(export["skeleton_bones"][0]["bind_world_matrix"][0]), 4)
        sample = export["vertex_samples"][0]
        self.assertEqual(sample["weight_sum_percent"], 100.0)
        self.assertEqual(sample["nonzero_influence_count"], 1)
        self.assertEqual(sample["influences"][0]["weight_percent"], 100.0)
        self.assertIsInstance(sample["influences"][0]["bone_index"], int)

    def test_export_skinned_bind_pose_decodes_mode2_fixture(self) -> None:
        if not ZELDA_FIELD_KEEP_ZAR.exists():
            self.skipTest("local OOT3D zelda_field_keep.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "butterfly_skinned.json"
            export = export_skinned_bind_pose(
                ZELDA_FIELD_KEEP_ZAR,
                output,
                cmb_name="Model/butterfly.cmb",
                sample_limit=5,
            )

        self.assertEqual(export["model_name"], "butterfly")
        self.assertEqual(export["bone_count"], 3)
        self.assertEqual(export["counts"]["mesh_count"], 1)
        self.assertEqual(export["counts"]["skinned_primitive_count"], 1)
        self.assertEqual(export["counts"]["mode_1_primitive_count"], 0)
        self.assertEqual(export["counts"]["mode_2_primitive_count"], 1)
        self.assertEqual(export["counts"]["skinned_vertex_rows"], 28)
        self.assertEqual(export["counts"]["influence_width_rows"]["2"], 28)
        self.assertEqual(export["counts"]["nonzero_influence_rows"]["1"], 24)
        self.assertEqual(export["counts"]["nonzero_influence_rows"]["2"], 4)
        self.assertEqual(export["counts"]["skeleton_bone_count"], 3)
        self.assertEqual(export["counts"]["finite_bind_world_matrix_entries"], 48)
        self.assertEqual(export["counts"]["validation_error_count"], 0)
        primitive = export["meshes"][0]["primitives"][0]
        self.assertEqual(primitive["influence_width"], 2)
        self.assertEqual(primitive["unique_vertex_count"], 28)
        sample = export["vertex_samples"][0]
        self.assertEqual(sample["weight_sum_percent"], 100.0)
        self.assertEqual(len(sample["influences"]), 2)

    def test_export_skinned_bind_pose_decodes_constant_index_fixture(self) -> None:
        if not ZELDA_EC_ZAR.exists():
            self.skipTest("local OOT3D zelda_ec.zar actor fixture is not present")

        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "kokiripeople_skinned.json"
            export = export_skinned_bind_pose(
                ZELDA_EC_ZAR,
                output,
                cmb_name="Model/kokiripeople.cmb",
                sample_limit=5,
            )

        self.assertEqual(export["model_name"], "kokiripeople")
        self.assertEqual(export["counts"]["skinned_primitive_count"], 11)
        self.assertEqual(export["counts"]["mode_2_primitive_count"], 11)
        self.assertEqual(export["counts"]["skinned_vertex_rows"], 843)
        self.assertEqual(export["counts"]["influence_width_rows"]["2"], 323)
        self.assertEqual(export["counts"]["influence_width_rows"]["3"], 476)
        self.assertEqual(export["counts"]["influence_width_rows"]["4"], 44)
        self.assertEqual(export["counts"]["nonzero_influence_rows"]["1"], 487)
        self.assertEqual(export["counts"]["nonzero_influence_rows"]["2"], 273)
        self.assertEqual(export["counts"]["nonzero_influence_rows"]["3"], 83)
        self.assertEqual(export["counts"]["skeleton_bone_count"], 19)
        self.assertEqual(export["counts"]["finite_bind_world_matrix_entries"], 304)
        self.assertEqual(export["counts"]["validation_error_count"], 0)

    def test_batch_export_skinned_bind_poses_writes_manifest(self) -> None:
        if not (
            ZELDA_DEKUBABA_ZAR.exists()
            and ZELDA_FIELD_KEEP_ZAR.exists()
            and ZELDA_EC_ZAR.exists()
        ):
            self.skipTest("local OOT3D skinned actor fixtures are not present")

        with tempfile.TemporaryDirectory() as tmp:
            actor_root = Path(tmp) / "actor"
            actor_root.mkdir()
            (actor_root / "zelda_dekubaba.zar").write_bytes(ZELDA_DEKUBABA_ZAR.read_bytes())
            (actor_root / "zelda_field_keep.zar").write_bytes(ZELDA_FIELD_KEEP_ZAR.read_bytes())
            (actor_root / "zelda_ec.zar").write_bytes(ZELDA_EC_ZAR.read_bytes())
            output = Path(tmp) / "skinned_batch"

            manifest_path = batch_export_actor_skinned_bind_poses(
                actor_root,
                output,
                sample_limit=5,
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            exported_files_exist = all(
                Path(record["output"]).exists() for record in manifest["records"]
            )

        self.assertEqual(manifest["format"], "oot3d_skinned_bind_pose_batch_v1")
        self.assertEqual(manifest["counts"]["considered_cmb"], 48)
        self.assertEqual(manifest["counts"]["parsed"], 48)
        self.assertEqual(manifest["counts"]["exported"], 25)
        self.assertEqual(manifest["counts"]["skipped_unskinned"], 23)
        self.assertEqual(manifest["counts"]["failed"], 0)
        self.assertEqual(manifest["counts"]["skinned_primitive_count"], 142)
        self.assertEqual(manifest["counts"]["mode_1_primitive_count"], 11)
        self.assertEqual(manifest["counts"]["mode_2_primitive_count"], 131)
        self.assertEqual(manifest["counts"]["skinned_vertex_rows"], 13639)
        self.assertEqual(manifest["counts"]["skeleton_bone_count"], 382)
        self.assertEqual(manifest["counts"]["finite_bind_world_matrix_entries"], 6112)
        self.assertEqual(manifest["counts"]["validation_error_count"], 0)
        self.assertEqual(manifest["influence_width_rows"]["1"], 924)
        self.assertEqual(manifest["influence_width_rows"]["2"], 6732)
        self.assertEqual(manifest["influence_width_rows"]["3"], 5939)
        self.assertEqual(manifest["influence_width_rows"]["4"], 44)
        self.assertEqual(manifest["nonzero_influence_rows"]["1"], 8992)
        self.assertEqual(manifest["nonzero_influence_rows"]["2"], 4465)
        self.assertEqual(manifest["nonzero_influence_rows"]["3"], 182)
        self.assertEqual(len(manifest["records"]), 25)
        self.assertTrue(exported_files_exist)


@unittest.skipUnless(
    all(
        path.exists()
        for path in (
            MESSAGE_ANIM_QAN,
            MESSAGE_COLOR_QCL,
            MESSAGE_LAYOUT_QLY,
            MESSAGE_SPRITE_QSP,
            MESSAGE_SYS8_QBF,
            MESSAGE_LTN16_QBF,
            MISC_ENDING_QAN,
            MISC_ENDING_QSP,
            MISC_ENDING_TOP_QLY,
            MISC_ENDING_BOTTOM_QLY,
            MISC_ENGLISH_CHALLENGE_QAN,
            MISC_ENGLISH_CHALLENGE_QSP,
            MISC_ENGLISH_CHALLENGE_TOP_QLY,
            MISC_ENGLISH_CHALLENGE_BOTTOM_QLY,
            MISC_BOSS_RUSH_QBR,
            MISC_HINT_LIST_QHM,
        )
    ),
    "local OOT3D Q-format fixtures are not present",
)
class QFormatAssetAuditTests(unittest.TestCase):
    def test_q_format_asset_audit_groups_ui_sets_and_fonts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            romfs = Path(tmp) / "romfs"
            fixtures = (
                (MESSAGE_ANIM_QAN, "message/anim.qan"),
                (MESSAGE_COLOR_QCL, "message/color.qcl"),
                (MESSAGE_LAYOUT_QLY, "message/layout.qly"),
                (MESSAGE_SPRITE_QSP, "message/sprite.qsp"),
                (MESSAGE_SYS8_QBF, "message/sys8.qbf"),
                (MESSAGE_LTN16_QBF, "message/eu/ltn16.qbf"),
                (MISC_ENDING_QAN, "misc/ending.qan"),
                (MISC_ENDING_QSP, "misc/ending.qsp"),
                (MISC_ENDING_TOP_QLY, "misc/ending_top.qly"),
                (MISC_ENDING_BOTTOM_QLY, "misc/ending_bottom.qly"),
                (MISC_ENGLISH_CHALLENGE_QAN, "misc/eu/english/challenge.qan"),
                (MISC_ENGLISH_CHALLENGE_QSP, "misc/eu/english/challenge.qsp"),
                (
                    MISC_ENGLISH_CHALLENGE_TOP_QLY,
                    "misc/eu/english/challenge_top.qly",
                ),
                (
                    MISC_ENGLISH_CHALLENGE_BOTTOM_QLY,
                    "misc/eu/english/challenge_bottom.qly",
                ),
                (MISC_BOSS_RUSH_QBR, "misc/bossRush.qbr"),
                (MISC_HINT_LIST_QHM, "misc/hint/list.qhm"),
            )
            for source, relative in fixtures:
                target = romfs / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.read_bytes())

            output = Path(tmp) / "q_format_asset_audit.json"
            audit = audit_q_format_assets(romfs, output, sample_limit=4)

            self.assertTrue(output.exists())

        self.assertEqual(audit["format"], "oot3d_q_format_asset_audit_v1")
        self.assertEqual(audit["file_count"], 16)
        self.assertEqual(
            audit["extension_counts"],
            {
                ".qan": 3,
                ".qbf": 2,
                ".qbr": 1,
                ".qcl": 1,
                ".qhm": 1,
                ".qly": 5,
                ".qsp": 3,
            },
        )
        self.assertEqual(audit["top_level_counts"], {"message": 6, "misc": 10})
        self.assertEqual(
            audit["group_kind_counts"],
            {"message_system": 1, "metadata_singleton": 2, "screen_layout_set": 2},
        )
        self.assertEqual(audit["group_completion_counts"], {"complete": 5})
        self.assertEqual(
            audit["screen_layout_set_completion_counts"],
            {"complete": 2},
        )
        self.assertEqual(audit["language_counts"], {"english": 4})
        self.assertEqual(
            audit["screen_counts"],
            {"bossRush": 1, "challenge": 4, "ending": 4, "hint": 1, "message": 6},
        )

        groups = {group["group_id"]: group for group in audit["groups"]}
        self.assertEqual(
            set(groups),
            {
                "message/system",
                "misc/bossRush",
                "misc/ending",
                "misc/eu/english/challenge",
                "misc/hint/list",
            },
        )
        self.assertTrue(groups["message/system"]["complete"])
        self.assertEqual(
            set(groups["message/system"]["roles"]),
            {"animation", "color_table", "eu_ltn16_font", "layout", "sprite", "sys8_font"},
        )
        self.assertTrue(groups["misc/eu/english/challenge"]["complete"])
        self.assertEqual(audit["issues"], [])

        fonts = {font["path"]: font for font in audit["qbf_font_records"]}
        self.assertEqual(fonts["message/sys8.qbf"]["texture_width_candidate_le"], 288)
        self.assertEqual(fonts["message/sys8.qbf"]["texture_height_candidate_le"], 192)
        self.assertEqual(fonts["message/sys8.qbf"]["glyph_count_candidate_le"], 42)
        self.assertEqual(fonts["message/sys8.qbf"]["cell_width_candidate"], 8)
        self.assertEqual(fonts["message/sys8.qbf"]["cell_height_candidate"], 8)
        self.assertEqual(fonts["message/eu/ltn16.qbf"]["texture_width_candidate_le"], 199)
        self.assertEqual(fonts["message/eu/ltn16.qbf"]["texture_height_candidate_le"], 208)
        self.assertLessEqual(len(audit["sample_records"]), 4)
        self.assertEqual(len(audit["records"]), 16)


@unittest.skipUnless(
    QUEEN_SOUND_BCSAR.exists()
    and QUEEN_STREAM_BCSAR.exists()
    and QUEEN_ROLL_BCSTM.exists(),
    "local OOT3D audio asset fixtures are not present",
)
class AudioAssetAuditTests(unittest.TestCase):
    def test_audio_asset_audit_validates_common_headers_and_sections(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            romfs = Path(tmp) / "romfs"
            fixtures = (
                (QUEEN_SOUND_BCSAR, "sound/QueenSound.bcsar"),
                (QUEEN_STREAM_BCSAR, "sound/QueenStream.bcsar"),
                (QUEEN_ROLL_BCSTM, "sound/stream/STRM_QUEEN_ROLL.bcstm"),
            )
            for source, relative in fixtures:
                target = romfs / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.read_bytes())

            output = Path(tmp) / "audio_asset_audit.json"
            audit = audit_audio_assets(romfs, output, sample_limit=2)

            self.assertTrue(output.exists())

        self.assertEqual(audit["format"], "oot3d_audio_asset_audit_v1")
        self.assertEqual(audit["file_count"], 3)
        self.assertEqual(audit["total_size"], 12734648)
        self.assertEqual(audit["extension_counts"], {".bcsar": 2, ".bcstm": 1})
        self.assertEqual(
            audit["category_counts"],
            {"audio_archive": 2, "audio_stream": 1},
        )
        self.assertEqual(audit["top_level_counts"], {"sound": 3})
        self.assertEqual(audit["parent_dir_counts"], {"sound": 2, "sound/stream": 1})
        self.assertEqual(audit["bom_counts"], {"fffe": 3})
        self.assertEqual(audit["byte_order_counts"], {"little": 3})
        self.assertEqual(audit["version_counts"], {"0x02000000": 3})
        self.assertEqual(audit["header_size_counts"], {"64": 3})
        self.assertEqual(audit["section_count_counts"], {"3": 3})
        self.assertEqual(audit["declared_size_match_count"], 3)
        self.assertEqual(audit["section_record_count"], 9)
        self.assertEqual(audit["section_declared_size_match_count"], 9)
        self.assertEqual(audit["issue_count"], 0)
        self.assertEqual(audit["issues"], [])
        self.assertEqual(
            audit["section_type_counts"],
            {
                "0x2000": 2,
                "0x2001": 2,
                "0x2002": 2,
                "0x4000": 1,
                "0x4001": 1,
                "0x4002": 1,
            },
        )
        self.assertEqual(
            audit["section_magic_counts"],
            {"DATA": 1, "FILE": 2, "INFO": 3, "SEEK": 1, "STRG": 2},
        )
        self.assertEqual(
            audit["section_layout_counts"],
            {
                "0x2000:STRG,0x2001:INFO,0x2002:FILE": 2,
                "0x4000:INFO,0x4001:SEEK,0x4002:DATA": 1,
            },
        )
        self.assertEqual(
            audit["section_size_summary_by_type"]["0x4002"],
            {
                "count": 1,
                "min": 5707040,
                "max": 5707040,
                "total": 5707040,
                "unique_size_count": 1,
            },
        )
        self.assertLessEqual(len(audit["sample_records"]), 2)
        self.assertEqual(len(audit["records"]), 3)


class KokiriRuntimeRouteAuditTests(unittest.TestCase):
    def test_kokiri_route_audio_runtime_mapping_resolves_route_profiles(self) -> None:
        sound_settings = [
            {
                "path": "link_info.zsi",
                "scene_stem": "link",
                "setup_index": 0,
                "spec_id": 5,
                "nature_ambience_id": 0,
                "data3": 0,
                "bgm_sound_id": 0x0100058D,
            },
            {
                "path": "link_info.zsi",
                "scene_stem": "link",
                "setup_index": 2,
                "spec_id": 5,
                "nature_ambience_id": 0,
                "data3": 0,
                "bgm_sound_id": 0,
            },
            {
                "path": "spot04_info.zsi",
                "scene_stem": "spot04",
                "setup_index": 0,
                "spec_id": 1,
                "nature_ambience_id": 0,
                "data3": 0,
                "bgm_sound_id": 0x010005A9,
            },
            {
                "path": "spot04_info.zsi",
                "scene_stem": "spot04",
                "setup_index": 4,
                "spec_id": 1,
                "nature_ambience_id": 0,
                "data3": 0,
                "bgm_sound_id": 0x010005B8,
            },
            {
                "path": "spot04_info.zsi",
                "scene_stem": "spot04",
                "setup_index": 3,
                "spec_id": 1,
                "nature_ambience_id": 0,
                "data3": 0,
                "bgm_sound_id": 0x010005BA,
            },
            {
                "path": "spot04_info.zsi",
                "scene_stem": "spot04",
                "setup_index": 9,
                "spec_id": 1,
                "nature_ambience_id": 0,
                "data3": 0,
                "bgm_sound_id": 0x010005D4,
            },
            {
                "path": "spot04_info.zsi",
                "scene_stem": "spot04",
                "setup_index": 7,
                "spec_id": 1,
                "nature_ambience_id": 0,
                "data3": 0,
                "bgm_sound_id": 0x7F,
            },
        ]

        mapping = route_audio_runtime_mapping(
            sound_settings,
            {"file_count": 3},
            sample_limit=20,
        )

        self.assertEqual(mapping["status"], "mapped")
        self.assertEqual(mapping["mapped_record_count"], 7)
        self.assertEqual(mapping["unresolved_profile_count"], 0)
        self.assertEqual(mapping["profile_count"], 7)
        sound_ids = {
            profile["native_audio_settings"]["bgm_sound_id"]
            for profile in mapping["profiles"]
        }
        self.assertIn(0x0100058D, sound_ids)
        self.assertIn(0x010005A9, sound_ids)
        self.assertIn(0x7F, sound_ids)

    def test_kokiri_runtime_route_audit_records_scene_dependencies(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scene_dir = root / "scene"
            scene_dir.mkdir()
            semantics = root / "actor_object_semantics.h"
            semantics.write_text(
                "\n".join(
                    [
                        "typedef enum Oot3dActorId {",
                        "    ACTOR_PLAYER = 0x00,",
                        "    ACTOR_EN_HOLL = 0x23,",
                        "} Oot3dActorId;",
                        "typedef enum Oot3dObjectId {",
                        "    OBJECT_GAMEPLAY_KEEP = 0x0001,",
                        "} Oot3dObjectId;",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            route_scene = bytearray(b"ZSI\x01" + b"\0" * 0x17C)
            route_scene[0x18:0x20] = scene_command(0x15, 5, 0x0100002A)
            route_scene[0x20:0x28] = scene_command(0x04, 1, 0x80)
            route_scene[0x28:0x30] = scene_command(0x06, 1, 0xE0)
            route_scene[0x30:0x38] = scene_command(0x0D, 1, 0xE8)
            route_scene[0x38:0x40] = scene_command(0x00, 1, 0xF0)
            route_scene[0x40:0x48] = scene_command(0x0E, 2, 0xC0)
            route_scene[0x48:0x50] = scene_command(0x11, 0, 0x1D)
            route_scene[0x50:0x58] = scene_command(0x19, 0x20, 4)
            route_scene[0x58:0x60] = scene_command(0x13, 0, 0x100)
            route_scene[0x60:0x68] = scene_command(0x0F, 0, 0x104)
            route_scene[0x68:0x70] = scene_command(0x14, 0, 0)
            room_ref = b"rom:/scene/test_0_info.zsi\0"
            route_scene[0x80 : 0x80 + len(room_ref)] = room_ref
            route_scene[0xC0:0xD0] = transition_actor_entry(0, 0, 0, 0, 0, 0, 0, 0, 0)
            route_scene[0xD0:0xE0] = transition_actor_entry(0, -1, 1, -1, 0x23, 1, 2, 3, 0x13F)
            route_scene[0xE0:0xE2] = bytes((0, 0))
            route_scene[0xE8:0xF0] = int(1).to_bytes(4, "little") + int(0xE0).to_bytes(4, "little")
            route_scene[0xF0:0x100] = actor_entry(0, 1, 2, 3, 4, 5, 6, 7)
            route_scene[0x100:0x104] = (
                int(0x0123).to_bytes(2, "little", signed=True)
                + int(-1).to_bytes(2, "little", signed=True)
            )
            (scene_dir / "test_info.zsi").write_bytes(route_scene)
            (scene_dir / "test_0_info.zsi").write_bytes(b"ZSI\x01" + b"\0" * 0x20)

            output = root / "route_audit.json"
            audit = audit_kokiri_runtime_route(
                root,
                output,
                route_stems=("test",),
                actor_object_semantics=semantics,
            )
            written = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(audit["format"], "oot3d_kokiri_runtime_route_audit_v4")
        self.assertEqual(written["format"], audit["format"])
        self.assertEqual(audit["zsi_file_count"], 2)
        self.assertEqual(audit["scene_file_count"], 1)
        self.assertEqual(audit["room_file_count"], 1)
        self.assertEqual(audit["setup_total"], 1)
        self.assertEqual(audit["command_total"], 11)
        self.assertEqual(audit["room_reference_count"], 1)
        self.assertEqual(audit["unique_room_references"], ["test_0_info.zsi"])
        self.assertEqual(audit["spawn_candidate_command_count"], 1)
        self.assertEqual(audit["spawn_entry_candidate_total"], 1)
        self.assertEqual(audit["spawn_player_candidate_total"], 1)
        self.assertEqual(
            audit["spawn_payload_profile_counts"]["validated_n64_player_start_list"],
            1,
        )
        self.assertEqual(audit["entrance_entry_candidate_total"], 1)
        self.assertEqual(audit["path_block_candidate_count"], 1)
        self.assertEqual(audit["exit_value_candidate_total"], 2)
        self.assertEqual(audit["transition_actor_count"], 2)
        self.assertEqual(audit["transition_actor_name_counts"]["ACTOR_EN_HOLL"], 1)
        self.assertEqual(
            audit["actor_object_binding_summary"]["status"],
            "route_actor_object_bindings_resolved",
        )
        self.assertEqual(
            audit["actor_object_binding_summary"]["object_list_dependency_status"],
            "covered_without_scene_object_list",
        )
        self.assertEqual(
            audit["actor_object_binding_summary"]["transition_actor_required_object_counts"][
                "OBJECT_GAMEPLAY_KEEP"
            ],
            1,
        )
        self.assertEqual(
            audit["actor_object_binding_summary"]["unresolved_transition_actor_binding_count"],
            0,
        )
        self.assertEqual(
            audit["actor_object_binding_summary"]["empty_transition_actor_slot_count"],
            1,
        )
        self.assertEqual(
            audit["actor_object_binding_summary"]["player_object_binding"]["status"],
            "runtime_binding_identified",
        )
        self.assertEqual(audit["sound_setting_records"][0]["spec_id"], 5)
        self.assertEqual(
            audit["sound_setting_records"][0]["bgm_sound_id"],
            0x0100002A,
        )
        self.assertEqual(audit["skybox_setting_records"][0]["skybox_id"], 0x1D)
        self.assertEqual(
            audit["skybox_setting_records"][0]["skybox_name"],
            "SKYBOX_UNSET_1D",
        )
        self.assertEqual(audit["kankyo_runtime_selection"]["unresolved_selection_count"], 0)
        self.assertEqual(
            audit["layout_validation_counts"]["validated_s16_exit_payload_window"],
            1,
        )
        self.assertEqual(
            audit["layout_validation_counts"]["validated_path_record_window"],
            1,
        )
        self.assertEqual(
            audit["layout_validation_counts"]["validated_player_spawn_indices"],
            1,
        )
        spawn_command = audit["records"][0]["scene_setups"][0]["commands"][4]
        self.assertEqual(
            spawn_command["spawn_payload_profile"]["status"],
            "validated_n64_player_start_list",
        )
        self.assertEqual(
            audit["kankyo_runtime_selection"]["status_counts"][
                "n64_filter_only_no_kankyo_resource_needed"
            ],
            1,
        )
        self.assertNotIn("needs_kankyo_runtime_selection", audit["runtime_gap_counts"])
        self.assertEqual(audit["runtime_gap_counts"]["missing_standard_actor_list_commands"], 1)
        self.assertNotIn("missing_object_list_commands", audit["runtime_gap_counts"])
        self.assertNotIn(
            "needs_oot3d_spawn_list_layout_validation",
            audit["runtime_gap_counts"],
        )
        self.assertEqual(audit["layout_validation_counts"]["validated_route_room_indices"], 1)
        self.assertNotIn(
            "needs_oot3d_entrance_list_layout_validation",
            audit["runtime_gap_counts"],
        )

    def test_kokiri_runtime_route_audit_validates_prefixed_player_starts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scene_dir = root / "scene"
            scene_dir.mkdir()
            semantics = root / "actor_object_semantics.h"
            semantics.write_text(
                "\n".join(
                    [
                        "typedef enum Oot3dActorId {",
                        "    ACTOR_PLAYER = 0x00,",
                        "} Oot3dActorId;",
                        "typedef enum Oot3dObjectId {",
                        "    OBJECT_GAMEPLAY_KEEP = 0x0001,",
                        "} Oot3dObjectId;",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            route_scene = bytearray(b"ZSI\x01" + b"\0" * 0xFC)
            route_scene[0x18:0x20] = scene_command(0x15, 5, 0x0100002A)
            route_scene[0x20:0x28] = scene_command(0x04, 1, 0xE0)
            route_scene[0x28:0x30] = scene_command(0x06, 2, 0x80)
            route_scene[0x30:0x38] = scene_command(0x00, 2, 0xA0)
            route_scene[0x38:0x40] = scene_command(0x13, 0, 0xC0)
            route_scene[0x40:0x48] = scene_command(0x0F, 0, 0xC4)
            route_scene[0x48:0x50] = scene_command(0x14, 0, 0)
            route_scene[0x80:0x90] = b"\x11\x22\x33\x44" * 4
            route_scene[0x90:0x94] = bytes((0, 0, 1, 0))
            route_scene[0xA0:0xB0] = b"\x7F\x7F\x7E\x7E" * 4
            route_scene[0xB0:0xC0] = actor_entry(0, 1, 2, 3, 0, 4, 0, 0x0FFF)
            route_scene[0xC0:0xD0] = actor_entry(0, 5, 6, 7, 0, 8, 0, 0x0DFF)
            route_scene[0xD0:0xD4] = (
                int(0x0123).to_bytes(2, "little", signed=True)
                + int(-1).to_bytes(2, "little", signed=True)
            )
            room_ref = b"rom:/scene/test_0_info.zsi\0"
            route_scene[0xE0 : 0xE0 + len(room_ref)] = room_ref
            (scene_dir / "test_info.zsi").write_bytes(route_scene)
            (scene_dir / "test_0_info.zsi").write_bytes(b"ZSI\x01" + b"\0" * 0x20)

            audit = audit_kokiri_runtime_route(
                root,
                route_stems=("test",),
                actor_object_semantics=semantics,
            )

        self.assertEqual(
            audit["layout_validation_counts"]["validated_prefixed_player_spawn_indices"],
            1,
        )
        self.assertEqual(
            audit["layout_validation_counts"]["validated_prefixed_route_room_indices"],
            1,
        )
        self.assertEqual(
            audit["layout_validation_counts"]["validated_prefixed_s16_exit_payload_window"],
            1,
        )
        self.assertEqual(
            audit["spawn_payload_profile_counts"]["validated_prefixed_player_start_list"],
            1,
        )
        self.assertNotIn(
            "needs_oot3d_spawn_list_layout_validation",
            audit["runtime_gap_counts"],
        )
        spawn_command = audit["records"][0]["scene_setups"][0]["commands"][3]
        self.assertEqual(
            spawn_command["layout_validation"]["status"],
            "validated_prefixed_player_spawn_indices",
        )
        self.assertTrue(
            spawn_command["layout_validation"]["overlaps_following_command_payload"]
        )
        self.assertEqual(
            spawn_command["layout_validation"]["command_payload_overlap"][0]["command_id"],
            "0x13",
        )
        entrance_command = audit["records"][0]["scene_setups"][0]["commands"][2]
        self.assertEqual(
            entrance_command["layout_validation"]["status"],
            "validated_prefixed_route_room_indices",
        )
        self.assertEqual(entrance_command["layout_validation"]["start_delta"], 0x10)
        exit_command = audit["records"][0]["scene_setups"][0]["commands"][4]
        self.assertEqual(
            exit_command["layout_validation"]["status"],
            "validated_prefixed_s16_exit_payload_window",
        )
        self.assertEqual(exit_command["layout_validation"]["entry_count"], 2)
        self.assertEqual(exit_command["layout_validation"]["start_delta"], 0x10)
        self.assertEqual(
            spawn_command["spawn_payload_profile"]["status"],
            "validated_prefixed_player_start_list",
        )

    def test_kokiri_runtime_route_audit_identifies_room_actor_payloads(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scene_dir = root / "scene"
            scene_dir.mkdir()
            semantics = root / "actor_object_semantics.h"
            semantics.write_text(
                "\n".join(
                    [
                        "typedef enum Oot3dActorId {",
                        "    ACTOR_PLAYER = 0x00,",
                        "    ACTOR_OBJ_TSUBO = 0x111,",
                        "    ACTOR_EN_OKUTA = 0x0E,",
                        "    ACTOR_EN_WONDER_TALK2 = 0x185,",
                        "    ACTOR_EN_COW = 0x1C6,",
                        "} Oot3dActorId;",
                        "typedef enum Oot3dObjectId {",
                        "    OBJECT_NIW = 0x0013,",
                        "    OBJECT_TSUBO = 0x012C,",
                        "    OBJECT_COW = 0x018B,",
                        "} Oot3dObjectId;",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            route_scene = bytearray(b"ZSI\x01" + b"\0" * 0x2C)
            route_scene[0x18:0x20] = scene_command(0x15, 0, 0)
            route_scene[0x20:0x28] = scene_command(0x14, 0, 0)
            room = bytearray(b"ZSI\x01" + b"\0" * 0x17C)
            room[0x100:0x108] = (
                int(0x012C).to_bytes(2, "little", signed=True)
                + int(0x018B).to_bytes(2, "little", signed=True)
                + int(0x0013).to_bytes(2, "little", signed=True)
                + int(0).to_bytes(2, "little", signed=True)
            )
            room[0x108:0x118] = actor_entry(0x185, 78, 38, 116, 0, -29126, 0, -30017)
            room[0x118:0x128] = actor_entry(0x1C6, -83, 0, -78, 0, -2912, 0, 0)
            room[0x128:0x138] = actor_entry(0x111, -118, 0, 51, 0, 0, 0, 16643)
            room[0x138:0x148] = actor_entry(0x0E, 41, 52, 75, 14, 41, 52, 75)
            (scene_dir / "test_info.zsi").write_bytes(route_scene)
            (scene_dir / "test_0_info.zsi").write_bytes(room)

            audit = audit_kokiri_runtime_route(
                root,
                route_stems=("test",),
                actor_object_semantics=semantics,
            )

        self.assertEqual(audit["room_actor_list_candidate_count"], 1)
        self.assertEqual(audit["selected_room_actor_list_count"], 1)
        self.assertEqual(audit["selected_room_actor_entry_total"], 3)
        self.assertEqual(
            audit["room_actor_payload_status_counts"][
                "object_prefixed_room_actor_list_identified"
            ],
            1,
        )
        self.assertEqual(
            audit["selected_room_actor_name_counts"]["ACTOR_EN_WONDER_TALK2"],
            1,
        )
        self.assertEqual(
            audit["selected_room_actor_object_name_counts"]["OBJECT_TSUBO"],
            1,
        )
        self.assertNotIn("missing_standard_actor_list_commands", audit["runtime_gap_counts"])
        self.assertNotIn("needs_oot3d_room_actor_runtime_routing", audit["runtime_gap_counts"])
        room_record = next(record for record in audit["records"] if record["role"] == "room")
        selected = room_record["selected_room_actor_list_candidate"]
        self.assertEqual(selected["object_id_count"], 3)
        self.assertEqual(selected["entry_count"], 3)
        self.assertEqual(selected["entries"][0]["actor_name"], "ACTOR_EN_WONDER_TALK2")
        self.assertNotIn("ACTOR_EN_OKUTA", selected["actor_name_counts"])


class KokiriRuntimeManifestTests(unittest.TestCase):
    def test_kokiri_runtime_manifest_records_targets_and_fallbacks(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            audit_path = root / "audit.json"
            manifest_path = root / "manifest.json"
            audit = kokiri_manifest_test_audit()
            audit_path.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")

            manifest = export_kokiri_runtime_manifest(
                audit_path,
                manifest_path,
                n64_rom="H:/Rom&Iso/n64/oot.z64",
                sample_limit=3,
            )
            written = json.loads(manifest_path.read_text(encoding="utf-8"))

        self.assertEqual(manifest["format"], "oot3d_kokiri_runtime_manifest_v1")
        self.assertEqual(written["format"], manifest["format"])
        self.assertEqual(manifest["runtime_enablement_status"], "n64_fallback_only")
        self.assertEqual(manifest["readiness"]["mesh"]["status"], "source_candidate_ready")
        self.assertEqual(
            manifest["readiness"]["runtime_enablement"]["blocker_total"],
            2,
        )
        self.assertEqual(
            manifest["readiness"]["sky_environment"]["status"],
            "skybox_runtime_selection_ready",
        )
        self.assertEqual(
            manifest["readiness"]["actors"]["status"],
            "room_actor_list_runtime_routing_ready",
        )
        self.assertEqual(
            manifest["readiness"]["animations"]["status"],
            "route_actor_bindings_ready_animation_assets_pending",
        )
        self.assertEqual(
            manifest["readiness"]["audio"]["status"],
            "native_oot3d_sound_settings_ready",
        )
        self.assertEqual(
            manifest["audio_summary"]["runtime_mapping"]["mapped_profile_count"],
            1,
        )
        self.assertEqual(
            manifest["readiness"]["actors"]["actor_object_binding_summary"][
                "transition_actor_required_object_counts"
            ]["OBJECT_GAMEPLAY_KEEP"],
            1,
        )
        self.assertEqual(
            manifest["readiness"]["scene_layout"]["spawn_payload_profile_counts"][
                "validated_n64_player_start_list"
            ],
            1,
        )
        self.assertEqual(
            manifest["setup_bindings"][0]["spawn"]["payload_profile"]["status"],
            "validated_n64_player_start_list",
        )
        self.assertEqual(manifest["kankyo_summary"]["status"], "runtime_selection_ready")
        target_kinds = [target["kind"] for target in manifest["resource_targets"]]
        self.assertEqual(target_kinds.count("room_mesh"), 2)
        self.assertEqual(target_kinds.count("scene_collision"), 2)
        self.assertEqual(target_kinds.count("room_actor_list"), 2)
        room_actor_target = next(
            target
            for target in manifest["resource_targets"]
            if target["kind"] == "room_actor_list"
        )
        self.assertEqual(room_actor_target["entry_count"], 2)
        self.assertEqual(len(room_actor_target["actor_entries"]), 2)
        self.assertEqual(room_actor_target["actor_entries"][1]["params"], 14)
        self.assertEqual(room_actor_target["object_ids"][0]["object_id"], 1)
        link_room = next(
            record
            for record in manifest["route_files"]
            if record["path"] == "link_0_info.zsi"
        )
        self.assertEqual(
            link_room["fallback"]["shipwright_resource"],
            "scenes/indoors/link_home/link_home_room_0",
        )
        self.assertEqual(
            manifest["setup_bindings"][0]["spawn"]["selected"]["confidence"],
            "strong",
        )
        self.assertEqual(
            manifest["setup_bindings"][0]["transition_actor_name_counts"]["ACTOR_EN_HOLL"],
            1,
        )

    def test_kokiri_runtime_manifest_can_be_explicitly_promoted_when_unblocked(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            audit_path = root / "audit.json"
            manifest_path = root / "manifest.json"
            audit = kokiri_manifest_test_audit()
            audit["runtime_gap_counts"] = {}
            audit_path.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")

            manifest = export_kokiri_runtime_manifest(
                audit_path,
                manifest_path,
                n64_rom="H:/Rom&Iso/n64/oot.z64",
                runtime_enablement_status="oot3d_runtime_enabled",
                sample_limit=3,
            )

        self.assertEqual(manifest["runtime_enablement_status"], "oot3d_runtime_enabled")
        self.assertEqual(manifest["runtime_blocker_counts"], {})
        self.assertEqual(
            manifest["readiness"]["runtime_enablement"]["status"],
            "ready_for_opt_in",
        )

    def test_kokiri_runtime_manifest_rejects_promotion_with_blockers(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            audit_path = root / "audit.json"
            manifest_path = root / "manifest.json"
            audit_path.write_text(
                json.dumps(kokiri_manifest_test_audit(), indent=2) + "\n",
                encoding="utf-8",
            )

            with self.assertRaises(ParseError):
                export_kokiri_runtime_manifest(
                    audit_path,
                    manifest_path,
                    n64_rom="H:/Rom&Iso/n64/oot.z64",
                    runtime_enablement_status="oot3d_runtime_enabled",
                    sample_limit=3,
                )


class KokiriActorAssetReadinessTests(unittest.TestCase):
    def test_kokiri_actor_asset_readiness_correlates_route_objects(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            runtime_manifest_path = root / "runtime_manifest.json"
            actor_inventory_path = root / "actor_inventory.json"
            static_batch_manifest_path = root / "static_batch_manifest.json"
            skinned_binding_manifest_path = root / "skinned_binding_manifest.json"
            output_path = root / "kokiri_actor_asset_readiness.json"

            runtime_manifest_path.write_text(
                json.dumps(
                    {
                        "route_id": "link_house_to_kokiri_forest",
                        "runtime_enablement_status": "n64_fallback_only",
                        "resource_targets": [
                            {
                                "kind": "room_actor_list",
                                "oot3d_source": "link_0_info.zsi",
                                "actor_entries": [
                                    {"actor_name": "ACTOR_EN_BOX", "actor_id": 1},
                                    {"actor_name": "ACTOR_EN_COW", "actor_id": 2},
                                    {"actor_name": "ACTOR_EN_WONDER_ITEM", "actor_id": 3},
                                    {"actor_name": "ACTOR_EN_DEKUBABA", "actor_id": 4},
                                ],
                                "object_ids": [
                                    {"object_name": "OBJECT_BOX", "object_id": 0x100},
                                    {"object_name": "OBJECT_COW", "object_id": 0x101},
                                    {"object_name": "OBJECT_DEKUBABA", "object_id": 0x102},
                                    {"object_name": "OBJECT_FA", "object_id": 0x103},
                                ],
                            }
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            actor_inventory_path.write_text(
                json.dumps(
                    {
                        "archive_records": [
                            {
                                "path": "zelda_box.zar",
                                "support_status": "static_payload_supported_by_current_exporter",
                                "cmb_count": 1,
                                "static_cmb_count": 1,
                                "nonstatic_cmb_count": 0,
                                "known_animation_file_count": 0,
                            },
                            {
                                "path": "zelda_cow.zar",
                                "support_status": "needs_skeleton_skinning_and_animation_support",
                                "cmb_count": 1,
                                "static_cmb_count": 0,
                                "nonstatic_cmb_count": 1,
                                "known_animation_file_count": 2,
                            },
                            {
                                "path": "zelda_dekubaba.zar",
                                "support_status": "static_models_with_animation_data",
                                "cmb_count": 2,
                                "static_cmb_count": 1,
                                "nonstatic_cmb_count": 1,
                                "known_animation_file_count": 2,
                            },
                            {
                                "path": "zelda_fa.zar",
                                "support_status": "needs_skeleton_skinning_and_animation_support",
                                "cmb_count": 1,
                                "static_cmb_count": 0,
                                "nonstatic_cmb_count": 1,
                                "known_animation_file_count": 3,
                            },
                        ],
                        "model_records": [
                            {"container_path": "zelda_box.zar", "name": "Model/tr_box.cmb"},
                            {"container_path": "zelda_cow.zar", "name": "Model/cow.cmb"},
                            {"container_path": "zelda_dekubaba.zar", "name": "Model/dekubaba.cmb"},
                            {"container_path": "zelda_fa.zar", "name": "Model/fa.cmb"},
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            static_batch_manifest_path.write_text(
                json.dumps(
                    {
                        "records": [
                            {
                                "status": "converted",
                                "source": "E:/romfs/actor/zelda_box.zar!Model/tr_box.cmb",
                                "asset_id": "zelda_box_model_tr_box",
                                "resource_root": "objects/oot3d_actor_static/zelda_box_model_tr_box",
                                "resource_count": 4,
                                "summary": {"bone_count": 1},
                            },
                            {
                                "status": "converted",
                                "source": "E:/romfs/actor/zelda_dekubaba.zar!Model/db_ha_model.cmb",
                                "asset_id": "zelda_dekubaba_model_db_ha_model",
                                "resource_root": "objects/oot3d_actor_static/zelda_dekubaba",
                                "resource_count": 5,
                                "summary": {"bone_count": 1},
                            },
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            skinned_binding_manifest_path.write_text(
                json.dumps(
                    {
                        "targets": [
                            {
                                "archive_path": "zelda_dekubaba.zar",
                                "target_id": "zelda_dekubaba_model_dekubaba",
                                "target_cmb_name": "Model/dekubaba.cmb",
                                "model_name": "dekubaba",
                                "bone_count": 10,
                                "animation_count": 2,
                                "bind_pose": {
                                    "package_entry": "objects/oot3d/skinned_bind_pose/dekubaba.json"
                                },
                                "support_status_counts": {},
                            },
                            {
                                "archive_path": "zelda_fa.zar",
                                "target_id": "zelda_fa_model_fairy",
                                "target_cmb_name": "Model/fa.cmb",
                                "model_name": "fairy",
                                "bone_count": 12,
                                "animation_count": 3,
                                "bind_pose": {
                                    "package_entry": "objects/oot3d/skinned_bind_pose/fa.json"
                                },
                                "support_status_counts": {},
                            },
                        ],
                        "unused_bind_pose_targets": [
                            {
                                "archive_path": "zelda_cow.zar",
                                "target_id": "zelda_cow_model_cow",
                                "target_cmb_name": "Model/cow.cmb",
                                "model_name": "cow",
                                "bone_count": 18,
                                "bind_pose_package_entry": "objects/oot3d/skinned_bind_pose/cow.json",
                            }
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )

            audit = audit_kokiri_actor_asset_readiness(
                runtime_manifest_path,
                actor_inventory_path,
                static_batch_manifest_path,
                skinned_binding_manifest_path,
                output_path,
                sample_limit=2,
            )
            written = json.loads(output_path.read_text(encoding="utf-8"))

        self.assertEqual(audit["format"], "oot3d_kokiri_actor_asset_readiness_v1")
        self.assertEqual(written["format"], audit["format"])
        self.assertEqual(audit["route_id"], "link_house_to_kokiri_forest")
        self.assertEqual(audit["route_room_actor_target_count"], 1)
        self.assertEqual(audit["route_actor_entry_count"], 4)
        self.assertEqual(audit["unique_route_actor_count"], 4)
        self.assertEqual(audit["route_object_prefix_reference_count"], 4)
        self.assertEqual(audit["unique_route_object_count"], 4)
        self.assertEqual(audit["mapped_route_object_count"], 4)
        self.assertEqual(audit["archive_mapping_issue_counts"], {})
        self.assertEqual(
            audit["object_asset_readiness_status_counts"],
            {
                "mixed_static_and_skinned_animation_bindings_ready": 1,
                "skinned_animation_bindings_ready": 1,
                "skinned_bind_pose_ready_animation_tracks_unresolved": 1,
                "static_or_rigid_exports_ready": 1,
            },
        )
        self.assertEqual(
            audit["asset_blocker_counts"],
            {
                "needs_shipwright_runtime_actor_asset_binding": 3,
                "needs_skinned_animation_track_resolution": 1,
            },
        )
        self.assertEqual(
            audit["actor_behavior_status_counts"],
            {
                "n64_hidden_item_trigger_behavior_fallback": 1,
                "n64_visual_actor_behavior_fallback": 3,
            },
        )
        records_by_object = {
            record["object_name"]: record for record in audit["object_records"]
        }
        self.assertEqual(
            records_by_object["OBJECT_BOX"]["asset_readiness_status"],
            "static_or_rigid_exports_ready",
        )
        self.assertEqual(
            records_by_object["OBJECT_COW"]["asset_readiness_status"],
            "skinned_bind_pose_ready_animation_tracks_unresolved",
        )
        self.assertEqual(
            records_by_object["OBJECT_DEKUBABA"]["asset_readiness_status"],
            "mixed_static_and_skinned_animation_bindings_ready",
        )
        self.assertEqual(
            records_by_object["OBJECT_FA"]["asset_readiness_status"],
            "skinned_animation_bindings_ready",
        )
        self.assertEqual(len(audit["actor_records"]), 2)


class KokiriStaticActorBindingPlanTests(unittest.TestCase):
    def test_static_actor_binding_plan_validates_first_wave_display_lists(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            runtime_manifest_path = root / "runtime_manifest.json"
            asset_readiness_path = root / "actor_asset_readiness.json"
            static_batch_manifest_path = root / "static_batch_manifest.json"
            output_path = root / "static_actor_binding_plan.json"

            runtime_manifest_path.write_text(
                json.dumps(
                    {
                        "route_id": "link_house_to_kokiri_forest",
                        "runtime_enablement_status": "n64_fallback_only",
                        "resource_targets": [
                            {
                                "kind": "room_actor_list",
                                "oot3d_source": "link_0_info.zsi",
                                "actor_entries": [
                                    {"actor_name": "ACTOR_OBJ_TSUBO", "actor_id": 1},
                                    {"actor_name": "ACTOR_EN_KUSA", "actor_id": 2, "params": 0},
                                    {"actor_name": "ACTOR_EN_KUSA", "actor_id": 2, "params": 1},
                                    {"actor_name": "ACTOR_EN_WONDER_ITEM", "actor_id": 3},
                                    {"actor_name": "ACTOR_DOOR_ANA", "actor_id": 4},
                                    {"actor_name": "ACTOR_EN_A_OBJ", "actor_id": 5, "params": 0x100A},
                                    {"actor_name": "ACTOR_EN_ISHI", "actor_id": 6, "params": 0x200},
                                    {"actor_name": "ACTOR_OBJ_HANA", "actor_id": 7, "params": 1},
                                    {"actor_name": "ACTOR_OBJ_HANA", "actor_id": 7, "params": 2},
                                ],
                                "object_ids": [
                                    {"object_name": "OBJECT_TSUBO", "object_id": 0x12C},
                                    {"object_name": "OBJECT_KUSA", "object_id": 0x12B},
                                ],
                            }
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            asset_readiness_path.write_text(
                json.dumps(
                    {
                        "object_records": [
                            {
                                "object_name": "OBJECT_TSUBO",
                                "archive_path": "zelda_tsubo.zar",
                                "asset_readiness_status": "static_or_rigid_exports_ready",
                                "route_reference_count": 1,
                                "static_exported_model_count": 1,
                            },
                            {
                                "object_name": "OBJECT_KUSA",
                                "archive_path": "zelda_kusa.zar",
                                "asset_readiness_status": "static_or_rigid_exports_ready",
                                "route_reference_count": 1,
                                "static_exported_model_count": 2,
                            },
                        ]
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            static_batch_manifest_path.write_text(
                json.dumps(
                    {
                        "records": [
                            {
                                "status": "converted",
                                "asset_id": "zelda_tsubo_model_tubo2_model",
                                "source": "E:/romfs/actor/zelda_tsubo.zar!Model/tubo2_model.cmb",
                                "resource_root": "objects/oot3d_actor_static/zelda_tsubo_model_tubo2_model",
                                "symbol": "gOot3dTubo2Model",
                                "resource_count": 1,
                                "resources": [
                                    {
                                        "kind": "DisplayList",
                                        "path": "objects/oot3d_actor_static/zelda_tsubo_model_tubo2_model/gOot3dTubo2Model",
                                    }
                                ],
                            },
                            {
                                "status": "converted",
                                "asset_id": "zelda_field_keep_model_ana01_modelt",
                                "source": "E:/romfs/actor/zelda_field_keep.zar!Model/ana01_modelT.cmb",
                                "resource_root": "objects/oot3d_actor_static/zelda_field_keep_model_ana01_modelt",
                                "symbol": "gOot3dAna01Modelt",
                                "resource_count": 1,
                                "resources": [
                                    {
                                        "kind": "DisplayList",
                                        "path": "objects/oot3d_actor_static/zelda_field_keep_model_ana01_modelt/gOot3dAna01Modelt",
                                    }
                                ],
                            },
                            {
                                "status": "converted",
                                "asset_id": "zelda_keep_objects_model_kanban2_model",
                                "source": "E:/romfs/actor/zelda_keep.zar!objects/model/kanban2_model.cmb",
                                "resource_root": "objects/oot3d_actor_static/zelda_keep_objects_model_kanban2_model",
                                "symbol": "gOot3dKanban2Model",
                                "resource_count": 1,
                                "resources": [
                                    {
                                        "kind": "DisplayList",
                                        "path": "objects/oot3d_actor_static/zelda_keep_objects_model_kanban2_model/gOot3dKanban2Model",
                                    }
                                ],
                            },
                            {
                                "status": "converted",
                                "asset_id": "zelda_field_keep_model_obj_isi01_model",
                                "source": "E:/romfs/actor/zelda_field_keep.zar!Model/obj_isi01_model.cmb",
                                "resource_root": "objects/oot3d_actor_static/zelda_field_keep_model_obj_isi01_model",
                                "symbol": "gOot3dObjIsi01Model",
                                "resource_count": 1,
                                "resources": [
                                    {
                                        "kind": "DisplayList",
                                        "path": "objects/oot3d_actor_static/zelda_field_keep_model_obj_isi01_model/gOot3dObjIsi01Model",
                                    }
                                ],
                            },
                            {
                                "status": "converted",
                                "asset_id": "zelda_field_keep_model_grass05_model",
                                "source": "E:/romfs/actor/zelda_field_keep.zar!Model/grass05_model.cmb",
                                "resource_root": "objects/oot3d_actor_static/zelda_field_keep_model_grass05_model",
                                "symbol": "gOot3dGrass05Model",
                                "resource_count": 1,
                                "resources": [
                                    {
                                        "kind": "DisplayList",
                                        "path": "objects/oot3d_actor_static/zelda_field_keep_model_grass05_model/gOot3dGrass05Model",
                                    }
                                ],
                            },
                            {
                                "status": "converted",
                                "asset_id": "zelda_keep_objects_model_field_kusa_model",
                                "source": "E:/romfs/actor/zelda_keep.zar!objects/model/field_kusa_model.cmb",
                                "resource_root": "objects/oot3d_actor_static/zelda_keep_objects_model_field_kusa_model",
                                "symbol": "gOot3dFieldKusaModel",
                                "resource_count": 1,
                                "resources": [
                                    {
                                        "kind": "DisplayList",
                                        "path": "objects/oot3d_actor_static/zelda_keep_objects_model_field_kusa_model/gOot3dFieldKusaModel",
                                    }
                                ],
                            },
                            {
                                "status": "converted",
                                "asset_id": "zelda_kusa_model_obj_kusa01_model",
                                "source": "E:/romfs/actor/zelda_kusa.zar!Model/obj_kusa01_model.cmb",
                                "resource_root": "objects/oot3d_actor_static/zelda_kusa_model_obj_kusa01_model",
                                "symbol": "gOot3dObjKusa01Model",
                                "resource_count": 1,
                                "resources": [
                                    {
                                        "kind": "DisplayList",
                                        "path": "objects/oot3d_actor_static/zelda_kusa_model_obj_kusa01_model/gOot3dObjKusa01Model",
                                    }
                                ],
                            },
                            {
                                "status": "converted",
                                "asset_id": "zelda_kusa_model_obj_kusa03_model",
                                "source": "E:/romfs/actor/zelda_kusa.zar!Model/obj_kusa03_model.cmb",
                                "resource_root": "objects/oot3d_actor_static/zelda_kusa_model_obj_kusa03_model",
                                "symbol": "gOot3dObjKusa03Model",
                                "resource_count": 1,
                                "resources": [
                                    {
                                        "kind": "DisplayList",
                                        "path": "objects/oot3d_actor_static/zelda_kusa_model_obj_kusa03_model/gOot3dObjKusa03Model",
                                    }
                                ],
                            },
                        ]
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )

            plan = export_kokiri_static_actor_binding_plan(
                runtime_manifest_path,
                asset_readiness_path,
                static_batch_manifest_path,
                output_path,
            )
            written = json.loads(output_path.read_text(encoding="utf-8"))

        self.assertEqual(plan["format"], "oot3d_kokiri_static_actor_binding_plan_v1")
        self.assertEqual(written["format"], plan["format"])
        self.assertEqual(plan["route_actor_entry_count"], 9)
        self.assertEqual(plan["visual_actor_entry_count"], 8)
        self.assertEqual(plan["behavior_actor_entry_count"], 1)
        self.assertEqual(plan["route_object_with_static_exports_count"], 2)
        self.assertEqual(plan["route_static_exported_model_count"], 3)
        self.assertEqual(plan["ready_static_binding_count"], 6)
        self.assertEqual(plan["ready_static_actor_entry_count"], 8)
        self.assertEqual(plan["ready_static_top_display_list_count"], 8)
        self.assertEqual(plan["ready_binding_issue_total"], 0)
        self.assertEqual(
            plan["actor_binding_status_counts"],
            {
                "n64_behavior_fallback": 1,
                "ready_param_selected_display_list_binding": 4,
                "ready_single_display_list_binding": 2,
            },
        )
        ready = next(
            record
            for record in plan["ready_binding_records"]
            if record["actor_name"] == "ACTOR_OBJ_TSUBO"
        )
        self.assertEqual(ready["actor_name"], "ACTOR_OBJ_TSUBO")
        self.assertEqual(ready["actor_ids"], [1])
        self.assertEqual(ready["primary_actor_id"], 1)
        self.assertEqual(ready["top_display_list_resource_status"], "present")
        self.assertEqual(
            ready["top_display_list"],
            "objects/oot3d_actor_static/zelda_tsubo_model_tubo2_model/gOot3dTubo2Model",
        )
        selector = next(
            record
            for record in plan["ready_binding_records"]
            if record["actor_name"] == "ACTOR_EN_KUSA"
        )
        self.assertEqual(selector["binding_status"], "ready_param_selected_display_list_binding")
        self.assertEqual(selector["selector_mask"], 3)
        self.assertEqual(selector["route_selector_value_counts"], {0: 1, 1: 1})
        self.assertEqual(selector["room_object_coverage_status"], "covered_by_room_object_prefixes")
        self.assertEqual(selector["selected_asset_ids"], [
            "zelda_keep_objects_model_field_kusa_model",
            "zelda_kusa_model_obj_kusa01_model",
            "zelda_kusa_model_obj_kusa03_model",
        ])
        self.assertEqual(len(selector["selector_records"]), 2)
        selector_by_value = {
            record["selector_value"]: record for record in selector["selector_records"]
        }
        self.assertEqual(
            selector_by_value[0]["top_display_list"],
            "objects/oot3d_actor_static/zelda_keep_objects_model_field_kusa_model/gOot3dFieldKusaModel",
        )
        self.assertEqual(
            selector_by_value[1]["top_display_list"],
            "objects/oot3d_actor_static/zelda_kusa_model_obj_kusa01_model/gOot3dObjKusa01Model",
        )
        self.assertEqual(
            selector_by_value[1]["destroyed_top_display_list"],
            "objects/oot3d_actor_static/zelda_kusa_model_obj_kusa03_model/gOot3dObjKusa03Model",
        )
        door = next(
            record
            for record in plan["ready_binding_records"]
            if record["actor_name"] == "ACTOR_DOOR_ANA"
        )
        self.assertEqual(door["draw_layer"], "translucent")
        self.assertEqual(door["room_object_coverage_status"], "covered_by_room_object_prefixes")
        self.assertEqual(
            door["top_display_list"],
            "objects/oot3d_actor_static/zelda_field_keep_model_ana01_modelt/gOot3dAna01Modelt",
        )
        sign = next(
            record
            for record in plan["ready_binding_records"]
            if record["actor_name"] == "ACTOR_EN_A_OBJ"
        )
        self.assertEqual(sign["selector_mask"], 0xFF)
        self.assertEqual(sign["route_selector_value_counts"], {10: 1})
        self.assertEqual(
            sign["selector_records"][0]["top_display_list"],
            "objects/oot3d_actor_static/zelda_keep_objects_model_kanban2_model/gOot3dKanban2Model",
        )
        stone = next(
            record
            for record in plan["ready_binding_records"]
            if record["actor_name"] == "ACTOR_EN_ISHI"
        )
        self.assertEqual(stone["selector_mask"], 1)
        self.assertEqual(stone["route_selector_value_counts"], {0: 1})
        hana = next(
            record
            for record in plan["ready_binding_records"]
            if record["actor_name"] == "ACTOR_OBJ_HANA"
        )
        self.assertEqual(hana["route_selector_value_counts"], {1: 1, 2: 1})
        self.assertEqual(len(hana["selector_records"]), 2)

    def test_static_actor_binding_plan_emits_stateful_bean_and_kanban_records(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            runtime_manifest_path = root / "runtime_manifest.json"
            asset_readiness_path = root / "actor_asset_readiness.json"
            static_batch_manifest_path = root / "static_batch_manifest.json"

            def converted_record(asset_id: str, archive: str, embedded_name: str, symbol: str) -> dict[str, object]:
                resource_root = f"objects/oot3d_actor_static/{asset_id}"
                return {
                    "status": "converted",
                    "asset_id": asset_id,
                    "source": f"E:/romfs/actor/{archive}!Model/{embedded_name}.cmb",
                    "resource_root": resource_root,
                    "symbol": symbol,
                    "resource_count": 1,
                    "resources": [
                        {
                            "kind": "DisplayList",
                            "path": f"{resource_root}/{symbol}",
                        }
                    ],
                }

            runtime_manifest_path.write_text(
                json.dumps(
                    {
                        "route_id": "link_house_to_kokiri_forest",
                        "runtime_enablement_status": "n64_fallback_only",
                        "resource_targets": [
                            {
                                "kind": "room_actor_list",
                                "oot3d_source": "spot04_0_info.zsi",
                                "actor_entries": [
                                    {"actor_name": "ACTOR_OBJ_BEAN", "actor_id": 0x0126, "params": 0x1F09},
                                    {"actor_name": "ACTOR_EN_KANBAN", "actor_id": 0x0141, "params": 0x0340},
                                ],
                                "object_ids": [
                                    {"object_name": "OBJECT_MAMENOKI", "object_id": 0x011E},
                                    {"object_name": "OBJECT_KANBAN", "object_id": 0x012F},
                                ],
                            }
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            asset_readiness_path.write_text(
                json.dumps(
                    {
                        "object_records": [
                            {
                                "object_name": "OBJECT_MAMENOKI",
                                "archive_path": "zelda_mamenoki.zar",
                                "asset_readiness_status": "static_or_rigid_exports_ready",
                                "route_reference_count": 1,
                                "static_exported_model_count": 4,
                            },
                            {
                                "object_name": "OBJECT_KANBAN",
                                "archive_path": "zelda_kanban.zar",
                                "asset_readiness_status": "static_or_rigid_exports_ready",
                                "route_reference_count": 1,
                                "static_exported_model_count": 1,
                            },
                        ]
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            static_batch_manifest_path.write_text(
                json.dumps(
                    {
                        "records": [
                            converted_record(
                                "zelda_mamenoki_model_c_mame_jr_model",
                                "zelda_mamenoki.zar",
                                "c_mame_jr_model",
                                "gOot3dCMameJrModel",
                            ),
                            converted_record(
                                "zelda_mamenoki_model_c_mame_kuki_model",
                                "zelda_mamenoki.zar",
                                "c_mame_kuki_model",
                                "gOot3dCMameKukiMod",
                            ),
                            converted_record(
                                "zelda_mamenoki_model_c_mame_lift_model",
                                "zelda_mamenoki.zar",
                                "c_mame_lift_model",
                                "gOot3dCMameLiftMod",
                            ),
                            converted_record(
                                "zelda_mamenoki_model_c_mame_place_model",
                                "zelda_mamenoki.zar",
                                "c_mame_place_model",
                                "gOot3dCMamePlaceMo",
                            ),
                            converted_record(
                                "zelda_keep_objects_model_kanban1_model",
                                "zelda_keep.zar",
                                "objects/model/kanban1_model",
                                "gOot3dKanban1Model",
                            ),
                        ]
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )

            plan = export_kokiri_static_actor_binding_plan(
                runtime_manifest_path,
                asset_readiness_path,
                static_batch_manifest_path,
            )

        self.assertEqual(plan["route_actor_entry_count"], 2)
        self.assertEqual(plan["visual_actor_entry_count"], 2)
        self.assertEqual(plan["behavior_actor_entry_count"], 0)
        self.assertEqual(plan["route_object_with_static_exports_count"], 2)
        self.assertEqual(plan["route_static_exported_model_count"], 5)
        self.assertEqual(plan["ready_static_binding_count"], 2)
        self.assertEqual(plan["ready_static_actor_entry_count"], 2)
        self.assertEqual(plan["ready_static_top_display_list_count"], 5)
        self.assertEqual(plan["ready_binding_issue_total"], 0)
        self.assertEqual(
            plan["actor_binding_status_counts"],
            {"ready_stateful_display_list_binding": 2},
        )

        records = {
            record["actor_name"]: record
            for record in plan["ready_binding_records"]
        }
        bean = records["ACTOR_OBJ_BEAN"]
        self.assertEqual(bean["binding_kind"], "stateful_obj_bean_draw")
        self.assertEqual(bean["stateful_status"], "present")
        self.assertEqual(bean["selected_asset_ids"], [
            "zelda_mamenoki_model_c_mame_jr_model",
            "zelda_mamenoki_model_c_mame_kuki_model",
            "zelda_mamenoki_model_c_mame_lift_model",
            "zelda_mamenoki_model_c_mame_place_model",
        ])
        bean_states = {
            record["state_name"]: record
            for record in bean["state_records"]
        }
        self.assertEqual(bean_states["seedling"]["state_flag_mask"], 1 << 1)
        self.assertEqual(bean_states["platform"]["state_flag_mask"], 1 << 2)
        self.assertEqual(bean_states["soft_soil"]["state_flag_mask"], 1 << 0)
        self.assertEqual(bean_states["soft_soil"]["draw_layer"], "translucent")
        self.assertEqual(bean_states["stalk"]["state_flag_mask"], 1 << 3)

        kanban = records["ACTOR_EN_KANBAN"]
        self.assertEqual(kanban["binding_kind"], "stateful_en_kanban_intact_sign")
        self.assertEqual(kanban["selected_asset_ids"], ["zelda_keep_objects_model_kanban1_model"])
        self.assertEqual(
            kanban["state_records"][0]["state_predicate"],
            "en_kanban_action_sign_and_all_parts",
        )
        self.assertEqual(
            kanban["state_records"][0]["top_display_list"],
            "objects/oot3d_actor_static/zelda_keep_objects_model_kanban1_model/gOot3dKanban1Model",
        )


class KokiriKankyoBindingPlanTests(unittest.TestCase):
    def test_kankyo_binding_plan_selects_blue_sky_records_and_keeps_filter_fallbacks(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            runtime_manifest_path = root / "runtime_manifest.json"
            kankyo_manifest_path = root / "kankyo_environment_export_manifest.json"
            output_path = root / "kankyo_binding_plan.json"

            runtime_manifest_path.write_text(
                json.dumps(
                    {
                        "route_id": "link_house_to_kokiri_forest",
                        "runtime_enablement_status": "oot3d_runtime_enabled",
                        "kankyo_summary": {
                            "runtime_selection": {
                                "unresolved_selection_count": 0,
                                "records": [
                                    {
                                        "skybox_id": 1,
                                        "skybox_name": "SKYBOX_NORMAL_SKY",
                                        "weather_or_unk_05": 0,
                                        "indoors": 0,
                                        "setup_count": 2,
                                        "locations": [{"path": "spot04_info.zsi", "setup_index": 7}],
                                        "status": "selected_kankyo_archive_candidate",
                                        "runtime_selection": {
                                            "kind": "oot3d_kankyo_archive_candidate",
                                            "archive_candidates": ["BlueSky.zar"],
                                            "environment_groups": ["tenkyu_sky_dome", "sun"],
                                        },
                                    },
                                    {
                                        "skybox_id": 29,
                                        "skybox_name": "SKYBOX_UNSET_1D",
                                        "weather_or_unk_05": 0,
                                        "indoors": 1,
                                        "setup_count": 1,
                                        "locations": [{"path": "link_info.zsi", "setup_index": 2}],
                                        "status": "n64_filter_only_no_kankyo_resource_needed",
                                        "runtime_selection": {
                                            "kind": "n64_filter_only",
                                            "fallback": "shipwright_n64_fog_color_filter",
                                        },
                                    },
                                ],
                            },
                        },
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            kankyo_manifest_path.write_text(
                json.dumps(
                    {
                        "format": "oot3d_kankyo_environment_export_manifest_v1",
                        "records": [
                            {
                                "status": "converted",
                                "asset_id": "BlueSky_model_fine_tenkyu_0",
                                "archive_path": "BlueSky.zar",
                                "embedded_name": "model/fine_tenkyu_0.cmb",
                                "model_name": "fine_tenkyu_0",
                                "environment_group": "tenkyu_sky_dome",
                                "resource_root": "environments/oot3d/kankyo/BlueSky_model_fine_tenkyu_0",
                                "symbol": "gFineTenkyu0",
                                "resource_count": 2,
                                "resources": [
                                    {
                                        "kind": "DisplayList",
                                        "path": "environments/oot3d/kankyo/BlueSky_model_fine_tenkyu_0/gFineTenkyu0_mat_0",
                                    },
                                    {
                                        "kind": "DisplayList",
                                        "path": "environments/oot3d/kankyo/BlueSky_model_fine_tenkyu_0/gFineTenkyu0",
                                    },
                                ],
                            },
                            {
                                "status": "converted",
                                "asset_id": "BlueSky_model_fine_sun",
                                "archive_path": "BlueSky.zar",
                                "embedded_name": "model/fine_sun.cmb",
                                "model_name": "fine_sun",
                                "environment_group": "sun",
                                "resource_root": "environments/oot3d/kankyo/BlueSky_model_fine_sun",
                                "symbol": "gFineSun",
                                "resource_count": 1,
                                "resources": [
                                    {
                                        "kind": "DisplayList",
                                        "path": "environments/oot3d/kankyo/BlueSky_model_fine_sun/gFineSun",
                                    }
                                ],
                            },
                            {
                                "status": "converted",
                                "asset_id": "Dark_model_dark_tenkyu0",
                                "archive_path": "Dark.zar",
                                "embedded_name": "model/dark_tenkyu0.cmb",
                                "model_name": "dark_tenkyu0",
                                "environment_group": "tenkyu_sky_dome",
                                "resource_root": "environments/oot3d/kankyo/Dark_model_dark_tenkyu0",
                                "symbol": "gDarkTenkyu0",
                                "resource_count": 1,
                                "resources": [
                                    {
                                        "kind": "DisplayList",
                                        "path": "environments/oot3d/kankyo/Dark_model_dark_tenkyu0/gDarkTenkyu0",
                                    }
                                ],
                            },
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )

            plan = export_kokiri_kankyo_binding_plan(
                runtime_manifest_path,
                kankyo_manifest_path,
                output_path,
            )
            written = json.loads(output_path.read_text(encoding="utf-8"))

        self.assertEqual(plan["format"], "oot3d_kokiri_kankyo_binding_plan_v1")
        self.assertEqual(written["format"], plan["format"])
        self.assertEqual(plan["skybox_profile_count"], 2)
        self.assertEqual(plan["selected_kankyo_profile_count"], 1)
        self.assertEqual(plan["n64_filter_only_profile_count"], 1)
        self.assertEqual(plan["unresolved_selection_count"], 0)
        self.assertEqual(plan["selected_archives"], ["BlueSky.zar"])
        self.assertEqual(plan["selected_environment_groups"], ["sun", "tenkyu_sky_dome"])
        self.assertEqual(plan["ready_environment_record_count"], 2)
        self.assertEqual(plan["ready_environment_resource_count"], 3)
        self.assertEqual(plan["ready_top_display_list_count"], 2)
        self.assertEqual(plan["ready_binding_issue_total"], 0)
        self.assertEqual(
            plan["profile_binding_status_counts"],
            {
                "n64_filter_only_fallback": 1,
                "ready_kankyo_environment_binding": 1,
            },
        )
        self.assertEqual(plan["ready_environment_group_counts"], {"sun": 1, "tenkyu_sky_dome": 1})
        ready_asset_ids = {
            record["asset_id"] for record in plan["ready_environment_records"]
        }
        self.assertEqual(
            ready_asset_ids,
            {"BlueSky_model_fine_sun", "BlueSky_model_fine_tenkyu_0"},
        )


@unittest.skipUnless(
    HINT000_MOFLEX.exists() and HINT012_MOFLEX.exists() and HINT023_MOFLEX.exists(),
    "local OOT3D Moflex movie fixtures are not present",
)
class MoflexMovieAuditTests(unittest.TestCase):
    def test_moflex_movie_audit_tracks_hint_numbering_and_header_profiles(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            romfs = Path(tmp) / "romfs"
            fixtures = (
                (HINT000_MOFLEX, "misc/hint/movie/hint000.moflex"),
                (HINT012_MOFLEX, "misc/hint/movie/hint012.moflex"),
                (HINT023_MOFLEX, "misc/hint/movie/hint023.moflex"),
            )
            for source, relative in fixtures:
                target = romfs / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.read_bytes())

            output = Path(tmp) / "moflex_movie_audit.json"
            audit = audit_moflex_movies(romfs, output, sample_limit=2)

            self.assertTrue(output.exists())

        self.assertEqual(audit["format"], "oot3d_moflex_movie_audit_v1")
        self.assertEqual(audit["file_count"], 3)
        self.assertEqual(audit["total_size"], 3547418)
        self.assertEqual(audit["extension_counts"], {".moflex": 3})
        self.assertEqual(audit["category_counts"], {"moflex_movie": 3})
        self.assertEqual(audit["top_level_counts"], {"misc": 3})
        self.assertEqual(audit["parent_dir_counts"], {"misc/hint/movie": 3})
        self.assertEqual(
            audit["size_summary"],
            {
                "count": 3,
                "min": 1014089,
                "max": 1505684,
                "total": 3547418,
                "unique_size_count": 3,
            },
        )
        self.assertEqual(audit["size_class_counts"], {"large": 1, "small": 2})
        self.assertEqual(audit["magic4_counts"], {"hex:4c32aaab": 3})
        self.assertEqual(
            audit["signature16_counts"],
            {"4c 32 aa ab 00 00 00 00 00 00 00 01 0f ff 03 0d": 3},
        )
        self.assertEqual(audit["dimension_candidate_counts"], {"400x240": 3})
        self.assertEqual(
            audit["timing_profile_counts"],
            {"39062/652": 1, "50000/835": 2},
        )
        self.assertEqual(audit["header_field_counts"]["u32_04"], {"0": 3})
        self.assertEqual(audit["header_field_counts"]["u32_08"], {"1": 3})
        self.assertEqual(audit["header_field_counts"]["u32_1c"], {"9": 3})
        self.assertEqual(
            audit["hint_movie_numbering"],
            {
                "count": 3,
                "unique_count": 3,
                "min": 0,
                "max": 23,
                "missing_count": 21,
                "missing_numbers": [
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7,
                    8,
                    9,
                    10,
                    11,
                    13,
                    14,
                    15,
                    16,
                    17,
                    18,
                    19,
                    20,
                    21,
                    22,
                ],
            },
        )
        self.assertEqual(audit["duplicate_hint_movie_numbers"], [])
        self.assertEqual(audit["issue_count"], 0)
        self.assertEqual(audit["issues"], [])
        self.assertLessEqual(len(audit["sample_records"]), 2)
        self.assertEqual(len(audit["records"]), 3)


@unittest.skipUnless(
    all(
        path.exists()
        for path in (
            HINT000_MOFLEX,
            QUEEN_SOUND_BCSAR,
            QUEEN_ROLL_BCSTM,
            MESSAGE_ANIM_QAN,
            MESSAGE_COLOR_QCL,
            MESSAGE_LAYOUT_QLY,
            MESSAGE_SPRITE_QSP,
            MESSAGE_SYS8_QBF,
            MISC_BOSS_RUSH_QBR,
            MISC_HINT_LIST_QHM,
            ENDING_STILL_CTXB,
        )
    ),
    "local OOT3D media asset fixtures are not present",
)
class MediaAssetAuditTests(unittest.TestCase):
    def test_media_asset_audit_records_headers_and_hint_movie_shape(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            romfs = Path(tmp) / "romfs"
            fixtures = (
                (HINT000_MOFLEX, "misc/hint/movie/hint000.moflex"),
                (QUEEN_SOUND_BCSAR, "sound/QueenSound.bcsar"),
                (QUEEN_ROLL_BCSTM, "sound/stream/STRM_QUEEN_ROLL.bcstm"),
                (MESSAGE_ANIM_QAN, "message/anim.qan"),
                (MESSAGE_COLOR_QCL, "message/color.qcl"),
                (MESSAGE_LAYOUT_QLY, "message/layout.qly"),
                (MESSAGE_SPRITE_QSP, "message/sprite.qsp"),
                (MESSAGE_SYS8_QBF, "message/sys8.qbf"),
                (MISC_BOSS_RUSH_QBR, "misc/bossRush.qbr"),
                (MISC_HINT_LIST_QHM, "misc/hint/list.qhm"),
                (ENDING_STILL_CTXB, "ending/ending_still.ctxb"),
            )
            for source, relative in fixtures:
                target = romfs / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.read_bytes())

            output = Path(tmp) / "media_asset_audit.json"
            audit = audit_media_assets(romfs, output, sample_limit=5)

            self.assertTrue(output.exists())

        self.assertEqual(audit["format"], "oot3d_media_asset_audit_v1")
        self.assertEqual(audit["file_count"], 11)
        self.assertEqual(audit["extension_counts"][".moflex"], 1)
        self.assertEqual(audit["extension_counts"][".bcsar"], 1)
        self.assertEqual(audit["extension_counts"][".bcstm"], 1)
        self.assertEqual(audit["extension_counts"][".ctxb"], 1)
        self.assertEqual(audit["extension_counts"][".qan"], 1)
        self.assertEqual(audit["extension_counts"][".qbf"], 1)
        self.assertEqual(audit["extension_counts"][".qbr"], 1)
        self.assertEqual(audit["extension_counts"][".qcl"], 1)
        self.assertEqual(audit["extension_counts"][".qhm"], 1)
        self.assertEqual(audit["extension_counts"][".qly"], 1)
        self.assertEqual(audit["extension_counts"][".qsp"], 1)
        self.assertEqual(
            audit["top_level_counts"],
            {"ending": 1, "message": 5, "misc": 3, "sound": 2},
        )
        self.assertEqual(audit["magic4_counts"][".moflex"]["hex:4c32aaab"], 1)
        self.assertEqual(audit["magic4_counts"][".bcsar"]["ascii:CSAR"], 1)
        self.assertEqual(audit["magic4_counts"][".bcstm"]["ascii:CSTM"], 1)
        self.assertEqual(audit["magic4_counts"][".ctxb"]["ascii:ctxb"], 1)
        self.assertEqual(audit["magic4_counts"][".qbf"]["ascii:QBF1"], 1)
        self.assertEqual(audit["moflex_dimension_candidate_counts"], {"400x240": 1})
        self.assertEqual(
            audit["moflex_hint_numbering"],
            {
                "count": 1,
                "unique_count": 1,
                "min": 0,
                "max": 0,
                "missing_count": 0,
                "missing_numbers": [],
            },
        )
        self.assertLessEqual(len(audit["sample_records"]), 5)
        self.assertEqual(len(audit["records"]), 11)


@unittest.skipUnless(
    ENDING_STILL_CTXB.exists() and KANKYO_BLUEONLY_ZAR.exists() and ZELDA_KEEP_ZAR.exists(),
    "local OOT3D CTXB fixtures are not present",
)
class CtxbTextureAuditTests(unittest.TestCase):
    def test_parse_ctxb_decodes_loose_texture(self) -> None:
        ctxb = parse_ctxb(ENDING_STILL_CTXB.read_bytes(), str(ENDING_STILL_CTXB))
        summary = ctxb.summary()

        self.assertEqual(ctxb.width, 512)
        self.assertEqual(ctxb.height, 256)
        self.assertEqual(ctxb.format_pair, "0x675a/0x0000")
        self.assertEqual(ctxb.payload_size, 65536)
        self.assertEqual(ctxb.expected_payload_size(), 65536)
        self.assertEqual(summary["decoded_rgba16_size"], 262144)
        self.assertTrue(summary["decoded_rgba16_size_match"])

    def test_ctxb_audit_decodes_loose_embedded_and_alpha_textures(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            romfs = Path(tmp) / "romfs"
            (romfs / "ending").mkdir(parents=True)
            (romfs / "ending" / "ending_still.ctxb").write_bytes(
                ENDING_STILL_CTXB.read_bytes()
            )
            (romfs / "kankyo").mkdir()
            (romfs / "kankyo" / "BlueOnly.zar").write_bytes(KANKYO_BLUEONLY_ZAR.read_bytes())
            (romfs / "actor").mkdir()
            (romfs / "actor" / "zelda_keep.zar").write_bytes(ZELDA_KEEP_ZAR.read_bytes())

            output = Path(tmp) / "ctxb_audit.json"
            audit = audit_ctxb_textures(romfs, output, sample_limit=5)

            self.assertTrue(output.exists())

        self.assertEqual(audit["format"], "oot3d_ctxb_texture_audit_v1")
        self.assertEqual(audit["ctxb_count"], 82)
        self.assertEqual(audit["loose_ctxb_count"], 1)
        self.assertEqual(audit["embedded_ctxb_count"], 81)
        self.assertEqual(audit["parsed_ctxb_count"], 82)
        self.assertEqual(audit["parse_error_count"], 0)
        self.assertEqual(audit["decode_error_count"], 0)
        self.assertEqual(audit["zar_archive_count"], 2)
        self.assertEqual(audit["zar_with_ctxb_count"], 2)
        self.assertEqual(audit["source_kind_counts"], {"embedded_zar": 81, "loose": 1})
        self.assertEqual(audit["top_level_counts"], {"actor": 76, "ending": 1, "kankyo": 5})
        self.assertEqual(audit["expected_payload_size_match_counts"], {"matches": 82})
        self.assertEqual(audit["decoded_rgba16_status_counts"], {"decoded": 82})
        self.assertEqual(audit["decoded_rgba16_size_match_counts"], {"matches": 82})
        self.assertEqual(audit["format_pair_counts"]["0x6756/0x1401"], 1)
        self.assertLessEqual(len(audit["sample_records"]), 5)
        self.assertEqual(len(audit["records"]), 82)

    def test_ctxb_export_writes_shipwright_texture_resources(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            romfs = Path(tmp) / "romfs"
            (romfs / "ending").mkdir(parents=True)
            (romfs / "ending" / "ending_still.ctxb").write_bytes(
                ENDING_STILL_CTXB.read_bytes()
            )
            (romfs / "kankyo").mkdir()
            (romfs / "kankyo" / "BlueOnly.zar").write_bytes(KANKYO_BLUEONLY_ZAR.read_bytes())
            (romfs / "actor").mkdir()
            (romfs / "actor" / "zelda_keep.zar").write_bytes(ZELDA_KEEP_ZAR.read_bytes())

            output = Path(tmp) / "ctxb_export"
            manifest = export_ctxb_textures(romfs, output, sample_limit=5)
            first_resource = Path(manifest["resources"][0]["file"])

            self.assertTrue((output / "ctxb_texture_export_manifest.json").exists())
            self.assertTrue(first_resource.exists())

        self.assertEqual(manifest["format"], "oot3d_ctxb_texture_export_manifest_v1")
        self.assertEqual(manifest["ctxb_count"], 82)
        self.assertEqual(manifest["parsed_count"], 82)
        self.assertEqual(manifest["exported_count"], 82)
        self.assertEqual(manifest["parse_error_count"], 0)
        self.assertEqual(manifest["export_error_count"], 0)
        self.assertEqual(manifest["resource_count"], 82)
        self.assertEqual(manifest["resource_audit_summary"]["checked_resource_count"], 82)
        self.assertEqual(manifest["resource_audit_summary"]["issue_record_count"], 0)
        self.assertEqual(manifest["resource_audit_summary"]["issue_counts"], {"none": 82})
        self.assertEqual(
            manifest["resource_audit_summary"]["generated_resource_size_summary"]["total"],
            2071928,
        )
        self.assertEqual(manifest["status_counts"], {"exported": 82})
        self.assertEqual(manifest["format_pair_counts"]["0x6756/0x1401"], 1)
        self.assertLessEqual(len(manifest["sample_records"]), 5)
        self.assertEqual(len(manifest["records"]), 82)


class PrerenderedRoomReplacementAuditTests(unittest.TestCase):
    def test_prerendered_room_replacement_audit_maps_backgrounds_to_scene_candidates(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            asset_xml_root = root / "assets"
            backgrounds = asset_xml_root / "textures" / "backgrounds.xml"
            backgrounds.parent.mkdir(parents=True)
            backgrounds.write_text(
                """
<Root>
    <File Name="vr_SP1a_static">
        <Texture Name="gBazaarBgTex" OutName="bazaar" Format="ci8" Width="256" Height="256" Offset="0x0" ExternalTlut="vr_SP1a_pal_static" ExternalTlutOffset="0x0"/>
        <Texture Name="gBazaar2BgTex" OutName="bazaar2" Format="ci8" Width="256" Height="256" Offset="0x10000" ExternalTlut="vr_SP1a_pal_static" ExternalTlutOffset="0x200"/>
    </File>
    <File Name="vr_SP1a_pal_static">
        <Texture Name="gBazaarBgTLUT" OutName="bazaar_tlut" Format="rgba16" Width="16" Height="16" Offset="0x0"/>
    </File>
    <File Name="vr_TEST_static">
        <Texture Name="gTestBgTex" OutName="test_room" Format="ci8" Width="256" Height="256" Offset="0x0" ExternalTlut="vr_TEST_pal_static" ExternalTlutOffset="0x0"/>
    </File>
</Root>
""".strip()
                + "\n",
                encoding="utf-8",
            )

            scene_xml = asset_xml_root / "scenes" / "shops" / "shop1.xml"
            scene_xml.parent.mkdir(parents=True)
            scene_xml.write_text(
                """
<Root>
    <File Name="shop1_scene" Segment="2">
        <Scene Name="shop1_scene" Offset="0x0"/>
    </File>
    <File Name="shop1_room_0" Segment="3">
        <Room Name="shop1_room_0" Offset="0x0"/>
    </File>
</Root>
""".strip()
                + "\n",
                encoding="utf-8",
            )

            scene_root = root / "scene"
            scene_root.mkdir()
            (scene_root / "shop_info.zsi").write_bytes(b"not a zsi")
            (scene_root / "shop_0_info.zsi").write_bytes(b"not a zsi")

            output = root / "prerendered_room_audit.json"
            audit = audit_prerendered_room_replacements(asset_xml_root, scene_root, output, sample_limit=10)

            self.assertTrue(output.exists())

        self.assertEqual(audit["format"], "oot3d_prerendered_room_replacement_audit_v1")
        self.assertEqual(audit["background_family_count"], 2)
        self.assertEqual(audit["background_texture_count"], 3)
        self.assertEqual(audit["mapped_family_count"], 1)
        self.assertEqual(audit["unmapped_family_count"], 1)
        self.assertEqual(audit["shipwright_candidate_count"], 1)
        self.assertEqual(audit["oot3d_candidate_count"], 1)
        self.assertEqual(audit["unique_n64_scene_stem_count"], 1)
        self.assertEqual(audit["unique_oot3d_scene_stem_count"], 1)
        self.assertEqual(audit["oot3d_room_file_count"], 1)
        self.assertEqual(audit["oot3d_room_parse_error_count"], 1)
        self.assertEqual(
            audit["status_counts"],
            {"candidate_needs_review": 1, "unmapped_background_family": 1},
        )
        mapped = audit["records"][0]
        self.assertEqual(mapped["file_name"], "vr_SP1a_static")
        self.assertEqual(mapped["alias"]["n64_scene_stem"], "shop1")
        self.assertEqual(mapped["alias"]["oot3d_scene_stem"], "shop")
        self.assertEqual(mapped["shipwright_candidate"]["room_count"], 1)
        self.assertEqual(mapped["oot3d_candidate"]["room_file_count"], 1)
        self.assertEqual(mapped["oot3d_candidate"]["parse_error_count"], 1)
        self.assertEqual(mapped["status"], "candidate_needs_review")

    @unittest.skipUnless(SPOT04_ROOM0_ZSI.exists(), "local OOT3D room ZSI fixture is not present")
    def test_prerendered_room_replacement_export_writes_batch_manifest_and_audit(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            asset_xml_root = root / "assets"
            backgrounds = asset_xml_root / "textures" / "backgrounds.xml"
            backgrounds.parent.mkdir(parents=True)
            backgrounds.write_text(
                """
<Root>
    <File Name="vr_SP1a_static">
        <Texture Name="gBazaarBgTex" OutName="bazaar" Format="ci8" Width="256" Height="256" Offset="0x0" ExternalTlut="vr_SP1a_pal_static" ExternalTlutOffset="0x0"/>
    </File>
</Root>
""".strip()
                + "\n",
                encoding="utf-8",
            )

            scene_xml = asset_xml_root / "scenes" / "shops" / "shop1.xml"
            scene_xml.parent.mkdir(parents=True)
            scene_xml.write_text(
                """
<Root>
    <File Name="shop1_scene" Segment="2">
        <Scene Name="shop1_scene" Offset="0x0"/>
    </File>
    <File Name="shop1_room_0" Segment="3">
        <Room Name="shop1_room_0" Offset="0x0"/>
    </File>
</Root>
""".strip()
                + "\n",
                encoding="utf-8",
            )

            scene_root = root / "scene"
            scene_root.mkdir()
            (scene_root / "shop_0_info.zsi").write_bytes(SPOT04_ROOM0_ZSI.read_bytes())

            manifest = export_prerendered_room_replacements(
                asset_xml_root,
                scene_root,
                root / "out",
                resource_prefix="scenes/test/prerendered",
                sample_limit=5,
            )

        self.assertEqual(manifest["format"], "oot3d_prerendered_room_export_manifest_v1")
        self.assertEqual(manifest["replacement_background_family_count"], 1)
        self.assertEqual(manifest["replacement_ready_family_count"], 1)
        self.assertEqual(manifest["unique_oot3d_scene_count"], 1)
        self.assertEqual(manifest["room_file_count"], 1)
        self.assertEqual(manifest["considered"], 1)
        self.assertEqual(manifest["converted"], 1)
        self.assertEqual(manifest["failed"], 0)
        self.assertEqual(manifest["parse_failed"], 0)
        self.assertEqual(manifest["skipped"], 0)
        self.assertIn("Texture", manifest["resource_kind_counts"])
        self.assertEqual(manifest["records"][0]["asset_id"], "shop_room_0")
        self.assertEqual(manifest["records"][0]["status"], "converted")
        self.assertEqual(
            manifest["records"][0]["associations"][0]["n64_scene_stem"],
            "shop1",
        )
        self.assertEqual(manifest["resource_audit_summary"]["issue_counts"]["total"], 0)

    @unittest.skipUnless(SPOT04_ROOM0_ZSI.exists(), "local OOT3D room ZSI fixture is not present")
    def test_prerendered_room_replacement_export_packs_o2r_and_audits_entries(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            asset_xml_root = root / "assets"
            backgrounds = asset_xml_root / "textures" / "backgrounds.xml"
            backgrounds.parent.mkdir(parents=True)
            backgrounds.write_text(
                """
<Root>
    <File Name="vr_SP1a_static">
        <Texture Name="gBazaarBgTex" OutName="bazaar" Format="ci8" Width="256" Height="256" Offset="0x0" ExternalTlut="vr_SP1a_pal_static" ExternalTlutOffset="0x0"/>
    </File>
</Root>
""".strip()
                + "\n",
                encoding="utf-8",
            )

            scene_xml = asset_xml_root / "scenes" / "shops" / "shop1.xml"
            scene_xml.parent.mkdir(parents=True)
            scene_xml.write_text(
                """
<Root>
    <File Name="shop1_scene" Segment="2">
        <Scene Name="shop1_scene" Offset="0x0"/>
    </File>
    <File Name="shop1_room_0" Segment="3">
        <Room Name="shop1_room_0" Offset="0x0"/>
    </File>
</Root>
""".strip()
                + "\n",
                encoding="utf-8",
            )

            scene_root = root / "scene"
            scene_root.mkdir()
            (scene_root / "shop_0_info.zsi").write_bytes(SPOT04_ROOM0_ZSI.read_bytes())

            manifest = export_prerendered_room_replacements(
                asset_xml_root,
                scene_root,
                root / "out",
                resource_prefix="scenes/test/prerendered",
                sample_limit=5,
            )
            manifest_path = root / "out" / "prerendered_room_export_manifest.json"
            archive_path = root / "oot3d_prerendered_room_candidates.o2r"
            audit_path = root / "package_audit.json"

            pack_prerendered_room_export_manifest(
                manifest_path,
                archive_path,
                name="OOT3D Prerendered Room Candidates",
                author="local",
                version="0.1.0",
            )
            audit = audit_prerendered_room_export_package(manifest_path, archive_path, audit_path)

            self.assertTrue(archive_path.exists())
            self.assertTrue(audit_path.exists())
            with zipfile.ZipFile(archive_path) as archive:
                archive_names = set(archive.namelist())
                first_resource_path = manifest["records"][0]["resources"][0]["path"]
                self.assertIn("manifest.json", archive_names)
                self.assertIn("oot3d_prerendered_room_export_manifest.json", archive_names)
                self.assertIn(first_resource_path, archive_names)

        self.assertEqual(audit["format"], "oot3d_prerendered_room_export_package_audit_v1")
        self.assertTrue(audit["has_manifest"])
        self.assertTrue(audit["has_prerendered_room_export_manifest"])
        self.assertTrue(audit["archived_export_manifest_matches"])
        self.assertEqual(audit["converted_record_count"], 1)
        self.assertEqual(audit["expected_resource_count"], manifest["records"][0]["resource_count"])
        self.assertEqual(audit["expected_resource_count"], audit["resource_entry_count"])
        self.assertEqual(audit["missing_resource_entry_count"], 0)
        self.assertEqual(audit["extra_resource_entry_count"], 0)
        self.assertEqual(audit["issue_counts"]["total"], 0)


@unittest.skipUnless(
    KANKYO_BLUEONLY_ZAR.exists() and KANKYO_COMMON_ZAR.exists(),
    "local OOT3D kankyo fixtures are not present",
)
class KankyoEnvironmentAuditTests(unittest.TestCase):
    def test_kankyo_environment_audit_tracks_static_sky_assets_and_support_payloads(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            kankyo_root = root / "kankyo"
            kankyo_root.mkdir()
            (kankyo_root / "BlueOnly.zar").write_bytes(KANKYO_BLUEONLY_ZAR.read_bytes())
            (kankyo_root / "kankyo_common.zar").write_bytes(KANKYO_COMMON_ZAR.read_bytes())

            output = root / "kankyo_audit.json"
            audit = audit_kankyo_environment_assets(kankyo_root, output, sample_limit=2)

            self.assertTrue(output.exists())

        self.assertEqual(audit["format"], "oot3d_kankyo_environment_audit_v1")
        self.assertEqual(audit["archive_count"], 2)
        self.assertEqual(audit["archive_file_count_total"], 20)
        self.assertEqual(audit["archive_parse_error_count"], 0)
        self.assertEqual(
            audit["embedded_type_counts"],
            {"cmab": 2, "cmb": 11, "ctxb": 5, "tbd": 2},
        )
        self.assertEqual(audit["cmb_counts"], {"discovered": 11, "parsed": 11, "parse_errors": 0})
        self.assertEqual(audit["cmb_support_counts"], {"static_cmb_export_supported": 11})
        self.assertEqual(audit["cmb_bone_count_counts"], {"1": 11})
        self.assertEqual(audit["cmb_skinning_mode_primitive_counts"], {"0": 11})
        self.assertEqual(audit["cmb_texture_count_counts"], {"0": 5, "1": 6})
        self.assertEqual(audit["cmb_triangle_total"], 1376)
        self.assertEqual(audit["cmb_vertex_total"], 1326)
        self.assertEqual(
            audit["environment_model_group_counts"],
            {"kumo_cloud": 5, "star": 1, "sun": 1, "tenkyu_sky_dome": 4},
        )
        self.assertEqual(audit["cmab_count"], 2)
        self.assertEqual(audit["cmab_size_summary"]["total"], 256)
        self.assertEqual(audit["cmab_magic_counts"], {"ascii:cmab": 2})
        self.assertEqual(audit["cmab_declared_size_match_counts"], {"matches": 2})
        self.assertEqual(audit["cmab_header_version_counts"], {"1": 2})
        self.assertEqual(audit["cmab_layout_status_counts"], {"markers_consistent": 2})
        self.assertEqual(audit["cmab_frame_count_candidate_counts"], {"900": 2})
        self.assertEqual(audit["ctxb_count"], 5)
        self.assertEqual(audit["ctxb_size_summary"]["total"], 65896)
        self.assertEqual(audit["ctxb_magic_counts"], {"ascii:ctxb": 5})
        self.assertEqual(audit["tbd_count"], 2)
        self.assertEqual(audit["tbd_size_summary"]["total"], 648)
        self.assertEqual(audit["tbd_magic_counts"], {"hex:74626400": 2})
        self.assertEqual(audit["tbd_entry_count_counts"], {"2": 1, "5": 1})
        self.assertEqual(len(audit["sample_records"]), 2)
        self.assertEqual(len(audit["records"]), 2)

    def test_kankyo_environment_export_writes_static_resources(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            kankyo_root = root / "kankyo"
            kankyo_root.mkdir()
            (kankyo_root / "BlueOnly.zar").write_bytes(KANKYO_BLUEONLY_ZAR.read_bytes())
            (kankyo_root / "kankyo_common.zar").write_bytes(KANKYO_COMMON_ZAR.read_bytes())

            output = root / "kankyo_export"
            manifest = export_kankyo_environment_assets(kankyo_root, output, sample_limit=2)

            self.assertTrue((output / "kankyo_environment_export_manifest.json").exists())
            self.assertTrue((output / "kankyo_environment_export_resource_audit.json").exists())

        self.assertEqual(manifest["format"], "oot3d_kankyo_environment_export_manifest_v1")
        self.assertEqual(manifest["archive_count"], 2)
        self.assertEqual(manifest["considered"], 11)
        self.assertEqual(manifest["converted"], 11)
        self.assertEqual(manifest["skipped"], 0)
        self.assertEqual(manifest["failed"], 0)
        self.assertEqual(manifest["parse_failed"], 0)
        self.assertEqual(manifest["status_counts"], {"converted": 11})
        self.assertEqual(
            manifest["environment_model_group_counts"],
            {"kumo_cloud": 5, "star": 1, "sun": 1, "tenkyu_sky_dome": 4},
        )
        self.assertEqual(
            manifest["resource_kind_counts"],
            {"DisplayList": 99, "Texture": 6, "Vertex": 66},
        )
        audit = manifest["resource_audit_summary"]
        self.assertEqual(audit["converted_record_count"], 11)
        self.assertEqual(audit["resource_count"], 171)
        self.assertEqual(audit["material_display_list_count"], 11)
        self.assertEqual(audit["mesh_display_list_count"], 11)
        self.assertEqual(audit["vertex_resource_count"], 66)
        self.assertEqual(audit["set_texture_image_count"], 6)
        self.assertEqual(audit["load_texture_block_count"], 0)
        self.assertEqual(audit["negative_st_count"], 498)
        self.assertEqual(audit["min_st"], -624)
        self.assertEqual(audit["max_st"], 19149)
        self.assertEqual(audit["issue_counts"]["negative_vertex_st"], 22)
        self.assertEqual(audit["issue_counts"]["total"], 22)

    def test_kankyo_environment_package_writes_auditable_o2r(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            kankyo_root = root / "kankyo"
            kankyo_root.mkdir()
            (kankyo_root / "BlueOnly.zar").write_bytes(KANKYO_BLUEONLY_ZAR.read_bytes())
            (kankyo_root / "kankyo_common.zar").write_bytes(KANKYO_COMMON_ZAR.read_bytes())

            output = root / "kankyo_export"
            manifest = export_kankyo_environment_assets(kankyo_root, output, sample_limit=2)
            manifest_path = output / "kankyo_environment_export_manifest.json"
            archive_path = root / "oot3d_kankyo_environment_candidates.o2r"
            audit_path = root / "kankyo_environment_package_audit.json"

            pack_kankyo_environment_export_manifest(
                manifest_path,
                archive_path,
                name="OOT3D Kankyo Environment Candidates",
                author="local",
                version="0.1.0",
            )
            audit = audit_kankyo_environment_export_package(manifest_path, archive_path, audit_path)

            self.assertTrue(archive_path.exists())
            self.assertTrue(audit_path.exists())
            with zipfile.ZipFile(archive_path) as archive:
                archive_names = set(archive.namelist())
                first_resource_path = manifest["records"][0]["resources"][0]["path"]
                self.assertIn("manifest.json", archive_names)
                self.assertIn("oot3d_kankyo_environment_export_manifest.json", archive_names)
                self.assertIn(first_resource_path, archive_names)

        self.assertEqual(audit["format"], "oot3d_kankyo_environment_export_package_audit_v1")
        self.assertTrue(audit["has_manifest"])
        self.assertTrue(audit["has_kankyo_environment_export_manifest"])
        self.assertTrue(audit["archived_export_manifest_matches"])
        self.assertEqual(audit["converted_record_count"], 11)
        self.assertEqual(audit["expected_resource_count"], 171)
        self.assertEqual(audit["expected_resource_count"], audit["resource_entry_count"])
        self.assertEqual(audit["archive_entry_count"], 173)
        self.assertEqual(audit["resource_kind_counts_archive"], {"DisplayList": 99, "Texture": 6, "Vertex": 66})
        self.assertEqual(audit["missing_resource_entry_count"], 0)
        self.assertEqual(audit["extra_resource_entry_count"], 0)
        self.assertEqual(audit["duplicate_archive_entry_count"], 0)
        self.assertEqual(audit["duplicate_expected_resource_path_count"], 0)
        self.assertEqual(audit["invalid_resource_xml_count"], 0)
        self.assertEqual(audit["issue_counts"]["total"], 0)


class MaterialLightingBlockTests(unittest.TestCase):
    def test_material_lighting_block_decodes_proven_pica_fields(self) -> None:
        raw = bytearray(0x15C)
        base = OOT3D_MATERIAL_LIGHTING_BLOCK_OFFSET
        struct.pack_into("<H", raw, base + 0x10, 0x84C2)
        struct.pack_into("<H", raw, base + 0x12, 0x62C9)
        raw[base + 0x14] = 2
        struct.pack_into("<H", raw, base + 0x18, 0x62B7)
        struct.pack_into("<H", raw, base + 0x1C, 0x62C3)
        raw[base + 0x1E] = 1
        raw[base + 0x20] = 1
        raw[base + 0x24] = 1
        struct.pack_into("<H", raw, base + 0x26, 0x62A5)
        struct.pack_into("<I", raw, base + 0x28, 0x3E800000)

        block = parse_material_lighting_block(bytes(raw))

        self.assertIsNotNone(block)
        assert block is not None
        self.assertEqual(block.pica_bump_texture_unit, 2)
        self.assertTrue(block.pica_bump_texture_unit_recognized)
        self.assertEqual(block.pica_bump_mode, 1)
        self.assertTrue(block.pica_bump_mode_recognized)
        self.assertEqual(block.flag1_raw, 2)
        self.assertTrue(block.flag1)
        self.assertEqual(block.pica_lighting_config, 8)
        self.assertTrue(block.pica_lighting_config_recognized)
        self.assertEqual(block.unresolved_enum4_encoded, 3)
        self.assertTrue(block.unresolved_enum4_recognized)
        self.assertEqual(block.pica_62c0_selector, 3)
        self.assertTrue(block.pica_62c0_selector_recognized)
        self.assertEqual(block.pica_lut_input_abs_d0_selector, 3)
        self.assertTrue(block.pica_lut_input_abs_d0_selector_recognized)
        self.assertEqual(block.pica_lut_input_abs_d0_disable_bit, 0)
        self.assertTrue(block.pica_lut_input_abs_d0_disable_bit_resolved)
        self.assertEqual(block.pica_lut_input_abs_sp_disable_bit, 0)
        self.assertTrue(block.pica_lut_input_abs_sp_disable_bit_resolved)
        self.assertEqual(block.pica_lut_scale_sp, 1)
        self.assertTrue(block.pica_lut_scale_sp_resolved)
        self.assertEqual(block.pica_lut_input_fr, 1)
        self.assertTrue(block.pica_lut_input_fr_resolved)
        self.assertEqual(block.pica_lut_input_abs_fr_disable_bit, 1)
        self.assertTrue(block.pica_lut_input_abs_fr_disable_bit_resolved)
        self.assertEqual(block.pica_lut_input_abs_rb_disable_bit, 0)
        self.assertTrue(block.pica_lut_input_abs_rb_disable_bit_resolved)
        self.assertEqual(block.pica_lut_input, 5)
        self.assertTrue(block.pica_lut_input_recognized)
        self.assertEqual(block.pica_lut_input_rb, 5)
        self.assertTrue(block.pica_lut_input_rb_recognized)
        self.assertEqual(block.pica_lut_scale_source_bits, 0x3E800000)
        self.assertEqual(block.pica_lut_scale, 6)
        self.assertTrue(block.pica_lut_scale_recognized)
        self.assertEqual(block.pica_lut_scale_rb, 6)
        self.assertTrue(block.pica_lut_scale_rb_recognized)

        summary = material_lighting_block_summary(block)
        self.assertIsNotNone(summary)
        assert summary is not None
        self.assertEqual(summary["source_offset"], "0x0cc")
        self.assertEqual(summary["pica_bump_texture_unit"]["native"], "0x84c2")
        self.assertEqual(summary["pica_lighting_config"]["decoded"], 8)
        self.assertEqual(summary["pica_62c0_selector"]["decoded"], 3)
        self.assertEqual(summary["pica_lut_input_abs_d0"]["decoded"], 3)
        self.assertEqual(summary["pica_lut_input_abs_d0"]["register"], "0x1d0")
        self.assertEqual(summary["pica_lut_input_abs_d0"]["field_name"], "disable_d0")
        self.assertEqual(summary["pica_lut_input_abs_d0"]["sampler"], "d0")
        self.assertEqual(summary["pica_lut_input_abs_d0"]["pica_disable_bit"], 0)
        self.assertEqual(
            summary["pica_lut_input_abs_d0"]["native_selector_disable_bit_by_decoded_value"][0],
            1,
        )
        self.assertEqual(summary["pica_lut_input_abs_sp"]["field_name"], "disable_sp")
        self.assertEqual(summary["pica_lut_input_abs_sp"]["bit_shift"], 9)
        self.assertEqual(summary["pica_lut_input_abs_sp"]["pica_disable_bit"], 0)
        self.assertEqual(summary["pica_lut_scale_sp"]["register"], "0x1d2")
        self.assertEqual(summary["pica_lut_scale_sp"]["decoded_lighting_scale"], 1)
        self.assertEqual(summary["pica_lut_input_fr"]["register"], "0x1d1")
        self.assertEqual(summary["pica_lut_input_fr"]["decoded_lighting_lut_input"], 1)
        self.assertEqual(summary["pica_lut_input_abs_fr"]["field_name"], "disable_fr")
        self.assertEqual(summary["pica_lut_input_abs_fr"]["pica_disable_bit"], 1)
        self.assertEqual(summary["pica_lut_input_abs_rb"]["field_name"], "disable_rb")
        self.assertEqual(summary["pica_lut_input_abs_rb"]["bit_shift"], 17)
        self.assertEqual(summary["pica_lut_input_abs_rb"]["pica_disable_bit"], 0)
        self.assertTrue(summary["unresolved_enum4_semantic_resolved"])
        self.assertEqual(summary["unresolved_enum4_semantic_alias"], "pica_lut_input_abs_d0")
        self.assertTrue(summary["unresolved_enum4_runtime_lane_mapping_resolved"])
        self.assertEqual(summary["pica_lut_input_rb"]["register"], "0x1d1")
        self.assertEqual(summary["pica_lut_input_rb"]["field_name"], "rb")
        self.assertEqual(summary["pica_lut_input_rb"]["decoded_lighting_lut_input"], 5)
        self.assertEqual(summary["pica_lut_scale"]["native_bits"], "0x3e800000")
        self.assertEqual(summary["pica_lut_scale_rb"]["register"], "0x1d2")
        self.assertEqual(summary["pica_lut_scale_rb"]["field_name"], "rb")
        self.assertEqual(summary["pica_lut_scale_rb"]["decoded_lighting_scale"], 6)
        self.assertEqual(summary["flags"]["flag1"]["raw"], 2)
        self.assertEqual(summary["flags"]["flag0"]["material_offset"], "0x0f0")
        self.assertEqual(summary["flags"]["flag0"]["runtime_lane_byte_offset"], "0x1a5")
        self.assertEqual(summary["flags"]["flag5"]["source_local_offset"], "0x23")
        self.assertEqual(summary["flags"]["flag5"]["material_offset"], "0x0ef")
        self.assertEqual(summary["flags"]["flag5"]["runtime_lane_byte_offset"], "0x1a1")
        self.assertTrue(summary["flags"]["flag5"]["semantic_resolved"])
        self.assertEqual(summary["flags"]["flag5"]["consumer_address"], "0x003fa5d0")
        self.assertEqual(summary["flags"]["flag5"]["final_upload_helper_address"], "0x004093f8")
        self.assertEqual(summary["flags"]["flag5"]["descriptor_payload_3_scale_offsets"], [0xB0, 0xB1, 0xB2])
        self.assertFalse(summary["flag_semantics_resolved"])
        self.assertTrue(summary["flag_semantics_partially_resolved"])
        self.assertEqual(
            summary["resolved_flag_semantics"]["flag0"],
            "pica_lut_input_abs_rb_disable_bit",
        )
        self.assertEqual(
            summary["resolved_flag_semantics"]["flag5"],
            ["pica_lut_input_abs_fr_disable_bit", "runtime_payload3_material_scale_gate"],
        )
        self.assertTrue(summary["flag_runtime_lane_mapping_resolved"])

    def test_material_lighting_block_preserves_unrecognized_native_values(self) -> None:
        raw = bytearray(0x15C)
        base = OOT3D_MATERIAL_LIGHTING_BLOCK_OFFSET
        struct.pack_into("<H", raw, base + 0x10, 0xFFFF)
        struct.pack_into("<H", raw, base + 0x12, 0xFFFF)
        struct.pack_into("<H", raw, base + 0x18, 0xFFFF)
        struct.pack_into("<H", raw, base + 0x1C, 0xFFFF)
        struct.pack_into("<H", raw, base + 0x26, 0xFFFF)
        struct.pack_into("<I", raw, base + 0x28, 0xDEADBEEF)

        block = parse_material_lighting_block(bytes(raw))

        self.assertIsNotNone(block)
        assert block is not None
        self.assertEqual(block.pica_bump_texture_unit_raw, 0xFFFF)
        self.assertEqual(block.pica_bump_texture_unit, 0)
        self.assertFalse(block.pica_bump_texture_unit_recognized)
        self.assertEqual(block.pica_bump_mode, 0)
        self.assertFalse(block.pica_bump_mode_recognized)
        self.assertEqual(block.pica_lighting_config, 0)
        self.assertFalse(block.pica_lighting_config_recognized)
        self.assertEqual(block.unresolved_enum4_encoded, 0)
        self.assertFalse(block.unresolved_enum4_recognized)
        self.assertEqual(block.pica_62c0_selector, 0)
        self.assertFalse(block.pica_62c0_selector_recognized)
        self.assertEqual(block.pica_lut_input, 0)
        self.assertFalse(block.pica_lut_input_recognized)
        self.assertEqual(block.pica_lut_scale_source_bits, 0xDEADBEEF)
        self.assertEqual(block.pica_lut_scale, 0)
        self.assertFalse(block.pica_lut_scale_recognized)


@unittest.skipUnless(CUBE_CMB.exists(), "local OOT3D cube.cmb fixture is not present")
class CubeConversionTests(unittest.TestCase):
    def test_parse_cube_summary(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        summary = model.summary()
        self.assertEqual(summary["name"], "cube")
        self.assertEqual(summary["mesh_count"], 1)
        self.assertEqual(summary["shape_count"], 1)
        self.assertEqual(summary["triangle_count"], 12)
        self.assertEqual(summary["vertex_count"], 24)
        self.assertEqual(len(model.textures), 1)
        self.assertEqual(model.textures[0].width, 32)
        self.assertEqual(model.textures[0].height, 32)
        self.assertEqual(summary["materials"][0]["raw_material_size"], 0x15C)
        self.assertEqual(len(summary["materials"][0]["raw_material_sha256"]), 64)
        self.assertEqual(summary["skeleton"]["chunk_size"], 0x38)
        self.assertEqual(summary["skeleton"]["expected_chunk_size"], 0x38)
        self.assertEqual(summary["skeleton"]["chunk_size_delta"], 0)
        self.assertEqual(summary["skeleton"]["header_word_0c"], 2)
        self.assertEqual(summary["skeleton"]["bone_count"], 1)
        self.assertEqual(summary["skeleton"]["root_count"], 1)
        self.assertEqual(summary["skeleton"]["max_depth"], 0)
        self.assertEqual(summary["skeleton"]["bone_index_mismatch_count"], 0)
        self.assertEqual(summary["skeleton"]["parent_out_of_range_count"], 0)
        self.assertEqual(model.skeleton.bones[0].parent_index, -1)

    def test_export_cube_shipwright_resources(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = export_static_model(
                model,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )
            self.assertTrue(result.manifest_path.exists())
            self.assertTrue((out / "gOot3dCube").exists())
            self.assertTrue((out / "gOot3dCube_mat_0").exists())
            self.assertTrue((out / "cube_01.rgba16").exists())
            self.assertGreater((out / "cube_01.rgba16").stat().st_size, 0x40)

            vertex_files = sorted(out.glob("*_vtx"))
            tri_files = sorted(out.glob("*_tri"))
            self.assertEqual(len(vertex_files), 1)
            self.assertEqual(len(tri_files), 1)
            self.assertEqual(vertex_files[0].read_text(encoding="utf-8").count("<Vtx "), 24)
            self.assertIn("<Triangles2", tri_files[0].read_text(encoding="utf-8"))

    def test_material_sampler_and_uv_transform_are_exported(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        material = model.materials[0]
        texture = model.textures[0]

        mapper = replace(
            material.texture_mappers[0],
            wrap_s=PICA_TEXTURE_WRAP_CLAMP_TO_EDGE,
            wrap_t=PICA_TEXTURE_WRAP_MIRRORED_REPEAT,
        )
        sampler_material = replace(
            material,
            texture_mappers=(mapper,) + material.texture_mappers[1:],
            texture_mappers_used=1,
        )
        xml = material_xml(sampler_material, texture, "objects/oot3d/cube/cube_01.rgba16")
        self.assertNotIn("<LoadTextureBlock", xml)
        self.assertIn("<SetTextureImage", xml)
        self.assertIn("<LoadTile", xml)
        self.assertIn('Cms0="G_TX_CLAMP"', xml)
        self.assertIn('Cms1="G_TX_NOMIRROR"', xml)
        self.assertIn('Cmt0="G_TX_WRAP"', xml)
        self.assertIn('Cmt1="G_TX_MIRROR"', xml)
        self.assertIn('MaskS="0"', xml)
        self.assertIn(f'MaskT="{texture_mask(texture.height)}"', xml)

        coord = replace(
            material.texture_coords[0],
            scale=Vec2(2.0, 3.0),
            rotation=0.0,
            translation=Vec2(0.25, -0.5),
        )
        uv_material = replace(
            material,
            texture_coords=(coord,) + material.texture_coords[1:],
            texture_coords_used=1,
        )
        vertex = to_legacy_fast_resource_vertex(model.shapes[0], texture, uv_material, 0)
        uv = model.shapes[0].uv0[0]
        transformed_uv = Vec2(uv.x * 2.0 + 0.25, uv.y * 3.0 - 0.5)
        self.assertEqual(vertex.s, int(round(transformed_uv.x * texture.width * 32.0)))
        self.assertEqual(vertex.t, int(round(transformed_uv.y * texture.height * 32.0)))
        self.assertEqual(apply_texture_coord(coord, uv), transformed_uv)

        second_mapper = replace(material.texture_mappers[1], index=7)
        multi_material = replace(
            material,
            texture_indices=(material.texture_mappers[0].index, second_mapper.index, -1),
            texture_mappers=(material.texture_mappers[0], second_mapper, material.texture_mappers[2]),
            texture_mappers_used=2,
        )
        self.assertEqual(first_valid_texture(multi_material), material.texture_mappers[0].index)

    def test_material_audit_does_not_flag_exported_uv_rotation(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        material = model.materials[0]
        primary_coord = replace(
            material.texture_coords[0],
            scale=Vec2(1.0, 1.0),
            rotation=0.4,
            translation=Vec2(0.0, 0.0),
        )
        primary_rotated = replace(
            material,
            texture_coords=(primary_coord,) + material.texture_coords[1:],
            texture_coords_used=1,
        )
        primary_record = material_audit_record(model, primary_rotated, "objects/oot3d/cube", "gCube")

        self.assertNotIn("rotated_texture_coord", material_issues(primary_record))

        secondary = replace(model.textures[0], index=1, name="cube_overlay")
        secondary_mapper = replace(material.texture_mappers[1], index=1)
        secondary_coord = replace(
            material.texture_coords[1],
            scale=Vec2(0.5, 1.0),
            rotation=0.4,
            translation=Vec2(0.0, 0.0),
        )
        secondary_rotated = replace(
            material,
            texture_indices=(0, 1, -1),
            texture_mappers=(material.texture_mappers[0], secondary_mapper, material.texture_mappers[2]),
            texture_coords=(material.texture_coords[0], secondary_coord, material.texture_coords[2]),
            texture_mappers_used=2,
            texture_coords_used=2,
        )
        secondary_model = replace(model, textures=(model.textures[0], secondary))
        secondary_record = material_audit_record(
            secondary_model,
            secondary_rotated,
            "objects/oot3d/cube",
            "gCube",
        )

        self.assertEqual(
            secondary_record["selected_secondary_texture_coord_export_status"],
            "baked_texture",
        )
        self.assertNotIn("rotated_texture_coord", material_issues(secondary_record))

    def test_material_audit_flags_unexported_uv_rotation(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        primary = model.textures[0]
        secondary = replace(primary, index=1, name="cube_secondary")
        tertiary = replace(primary, index=2, name="cube_tertiary")
        material = model.materials[0]
        secondary_mapper = replace(material.texture_mappers[1], index=1)
        tertiary_mapper = replace(material.texture_mappers[2], index=2)
        tertiary_coord = replace(
            material.texture_coords[2],
            scale=Vec2(1.0, 1.0),
            rotation=0.4,
            translation=Vec2(0.0, 0.0),
        )
        three_stage_material = replace(
            material,
            texture_indices=(0, 1, 2),
            texture_mappers=(material.texture_mappers[0], secondary_mapper, tertiary_mapper),
            texture_coords=(material.texture_coords[0], material.texture_coords[1], tertiary_coord),
            texture_mappers_used=3,
            texture_coords_used=3,
        )
        three_stage_model = replace(model, textures=(primary, secondary, tertiary))
        record = material_audit_record(
            three_stage_model,
            three_stage_material,
            "objects/oot3d/cube",
            "gCube",
        )

        self.assertIn("rotated_texture_coord", material_issues(record))

    def test_repeat_uv_offset_normalizes_negative_coordinates(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        shape = model.shapes[0]
        material = model.materials[0]
        mapper = replace(
            material.texture_mappers[0],
            wrap_s=PICA_TEXTURE_WRAP_REPEAT,
            wrap_t=PICA_TEXTURE_WRAP_REPEAT,
        )
        repeat_material = replace(
            material,
            texture_mappers=(mapper,) + material.texture_mappers[1:],
            texture_mappers_used=1,
        )
        shifted_shape = replace(
            shape,
            uv0=tuple(replace(uv, y=uv.y - 1.25) for uv in shape.uv0),
        )

        offset = mesh_uv_offset(shifted_shape, repeat_material, [(0, 1, 2)])
        self.assertEqual(offset, Vec2(0.0, 2.0))

        raw_vertex = to_legacy_fast_resource_vertex(shifted_shape, model.textures[0], repeat_material, 0)
        normalized_vertex = to_legacy_fast_resource_vertex(
            shifted_shape,
            model.textures[0],
            repeat_material,
            0,
            uv_offset=offset,
        )
        self.assertLess(raw_vertex.t, 0)
        self.assertGreater(normalized_vertex.t, 0)

        translated_coord = replace(
            material.texture_coords[0],
            scale=Vec2(1.0, 1.0),
            rotation=0.0,
            translation=Vec2(0.0, -1.25),
        )
        translated_material = replace(
            repeat_material,
            texture_coords=(translated_coord,) + repeat_material.texture_coords[1:],
            texture_coords_used=1,
        )

        translated_offset = mesh_uv_offset(shape, translated_material, [(0, 1, 2)])
        self.assertEqual(translated_offset, Vec2(0.0, 2.0))

        translated_raw_vertex = to_legacy_fast_resource_vertex(shape, model.textures[0], translated_material, 0)
        translated_normalized_vertex = to_legacy_fast_resource_vertex(
            shape,
            model.textures[0],
            translated_material,
            0,
            uv_offset=translated_offset,
        )
        self.assertLess(translated_raw_vertex.t, 0)
        self.assertGreater(translated_normalized_vertex.t, 0)

    def test_texture_coord_invert_round_trips_rotation(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        material = model.materials[0]
        coord = replace(
            material.texture_coords[0],
            scale=Vec2(0.5, 1.5),
            rotation=0.4,
            translation=Vec2(0.8, -0.25),
        )
        uv = Vec2(0.3, 0.7)

        transformed = apply_texture_coord(coord, uv)
        restored = invert_texture_coord(coord, transformed)

        self.assertAlmostEqual(restored.x, uv.x)
        self.assertAlmostEqual(restored.y, uv.y)

    def test_multiply_rgba16_pixels_multiplies_channels_and_alpha(self) -> None:
        def rgba16(r: int, g: int, b: int, a: int) -> bytes:
            return ((r << 11) | (g << 6) | (b << 1) | a).to_bytes(2, "big")

        self.assertEqual(
            multiply_rgba16_pixels(rgba16(31, 31, 31, 1), rgba16(31, 31, 31, 1)),
            rgba16(31, 31, 31, 1),
        )
        self.assertEqual(
            multiply_rgba16_pixels(rgba16(31, 20, 10, 1), rgba16(16, 8, 31, 0)),
            rgba16(16, 5, 10, 0),
        )

    def test_material_xml_loads_distinct_secondary_texture(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        primary = model.textures[0]
        secondary = replace(primary, index=1, name="cube_overlay")
        material = model.materials[0]
        secondary_mapper = replace(material.texture_mappers[1], index=1)
        secondary_coord = replace(
            material.texture_coords[1],
            scale=Vec2(2.0, 1.0),
            rotation=0.0,
            translation=Vec2(0.25, 0.0),
        )
        multi_material = replace(
            material,
            texture_indices=(0, 1, -1),
            texture_mappers=(material.texture_mappers[0], secondary_mapper, material.texture_mappers[2]),
            texture_coords=(material.texture_coords[0], secondary_coord, material.texture_coords[2]),
            texture_mappers_used=2,
            texture_coords_used=2,
        )

        model_with_secondary = replace(model, textures=(primary, secondary), materials=(multi_material,))
        self.assertEqual(secondary_material_texture(model_with_secondary, multi_material, 0), secondary)
        self.assertEqual(secondary_texture_coord_export_status(multi_material, 0), "baked_texture")

        xml = material_xml(
            multi_material,
            primary,
            "objects/oot3d/cube/cube_01.rgba16",
            secondary_texture=secondary,
            secondary_texture_path="objects/oot3d/cube/cube_overlay.rgba16",
        )
        self.assertEqual(xml.count("<SetTextureImage"), 2)
        self.assertIn('G_CCMUX_TEXEL1', xml)
        self.assertIn('TMem="256" Tile="7"', xml)
        self.assertIn('TMem="256" Tile="1"', xml)

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = export_static_model(
                model_with_secondary,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )

            baked_resource = next(
                resource
                for resource in result.resources
                if resource.kind == "Texture" and resource.path.endswith("cube_overlay_mat_0_uv1.rgba16")
            )
            baked_file = Path(baked_resource.file)
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")
            self.assertTrue(baked_file.exists())
            self.assertEqual(
                baked_file.stat().st_size,
                0x40 + 0x1C + primary.width * primary.height * 2,
            )

        self.assertIn("cube_overlay_mat_0_uv1.rgba16", material_output)

    def test_export_bakes_rotated_secondary_texture(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        primary = model.textures[0]
        secondary = replace(primary, index=1, name="cube_overlay")
        material = model.materials[0]
        secondary_mapper = replace(material.texture_mappers[1], index=1)
        secondary_coord = replace(
            material.texture_coords[1],
            scale=Vec2(0.5, 1.0),
            rotation=0.4,
            translation=Vec2(0.8, 0.0),
        )
        multi_material = replace(
            material,
            texture_indices=(0, 1, -1),
            texture_mappers=(material.texture_mappers[0], secondary_mapper, material.texture_mappers[2]),
            texture_coords=(material.texture_coords[0], secondary_coord, material.texture_coords[2]),
            texture_mappers_used=2,
            texture_coords_used=2,
        )
        model_with_secondary = replace(model, textures=(primary, secondary), materials=(multi_material,))

        self.assertEqual(secondary_texture_coord_export_status(multi_material, 0), "baked_texture")

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = export_static_model(
                model_with_secondary,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )

            baked_resource = next(
                resource
                for resource in result.resources
                if resource.kind == "Texture" and resource.path.endswith("cube_overlay_mat_0_uv1.rgba16")
            )
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")
            self.assertTrue(Path(baked_resource.file).exists())

        self.assertIn("cube_overlay_mat_0_uv1.rgba16", material_output)

    def test_material_xml_loads_two_stage_raw_secondary_for_single_mapper(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        primary = model.textures[0]
        secondary = replace(primary, index=1, name="cube_overlay")
        material = model.materials[0]
        inactive_mapper = replace(material.texture_mappers[1], index=-1)
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (2).to_bytes(4, "little")
        raw[0x124:0x126] = (0).to_bytes(2, "little")
        raw[0x126:0x128] = (1).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, -1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                inactive_mapper,
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
            raw_material=bytes(raw),
        )
        model_with_secondary = replace(model, textures=(primary, secondary), materials=(raw_material,))

        self.assertEqual(secondary_material_texture(model_with_secondary, raw_material, 0), secondary)
        self.assertEqual(secondary_material_texture_mapper(raw_material), raw_material.texture_mappers[0])
        self.assertEqual(secondary_texture_coord_export_status(raw_material, 0), "identity")

        xml = material_xml(
            raw_material,
            primary,
            "objects/oot3d/cube/cube_01.rgba16",
            secondary_texture=secondary,
            secondary_texture_path="objects/oot3d/cube/cube_overlay.rgba16",
        )
        self.assertEqual(xml.count("<SetTextureImage"), 2)
        self.assertIn('G_CCMUX_TEXEL1', xml)

        raw[0x124:0x126] = (1).to_bytes(2, "little")
        raw[0x126:0x128] = (0).to_bytes(2, "little")
        reversed_raw_material = replace(raw_material, raw_material=bytes(raw))
        reversed_model = replace(model, textures=(primary, secondary), materials=(reversed_raw_material,))
        self.assertIsNone(secondary_material_texture(reversed_model, reversed_raw_material, 0))

    def test_material_uses_raw_stage_pair_when_parsed_primary_is_absent_from_selector(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        parsed_primary = model.textures[0]
        raw_primary = replace(parsed_primary, index=1, name="cube_raw_primary")
        raw_secondary = replace(parsed_primary, index=2, name="cube_raw_secondary")
        material = model.materials[0]
        inactive_mapper = replace(material.texture_mappers[1], index=-1)
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (2).to_bytes(4, "little")
        raw[0x124:0x126] = (1).to_bytes(2, "little")
        raw[0x126:0x128] = (2).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, -1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                inactive_mapper,
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
            raw_material=bytes(raw),
        )
        model_with_raw_pair = replace(
            model,
            textures=(parsed_primary, raw_primary, raw_secondary),
            materials=(raw_material,),
        )

        self.assertEqual(first_valid_texture(raw_material), 0)
        self.assertEqual(material_primary_texture_index(model_with_raw_pair, raw_material), 1)
        self.assertEqual(material_texture(model_with_raw_pair, raw_material), raw_primary)
        self.assertEqual(
            secondary_material_texture(model_with_raw_pair, raw_material, 1),
            raw_secondary,
        )
        self.assertEqual(
            secondary_material_texture_mapper(raw_material),
            raw_material.texture_mappers[0],
        )

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            export_static_model(
                model_with_raw_pair,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")

        self.assertEqual(material_output.count("<SetTextureImage"), 2)
        self.assertIn("cube_raw_primary.rgba16", material_output)
        self.assertIn("cube_raw_secondary.rgba16", material_output)
        self.assertIn("G_CCMUX_TEXEL1", material_output)

    def test_scene_raw_stage_pair_skips_ambiguous_material_index_refs(self) -> None:
        model = replace(
            CmbModel.from_path(CUBE_CMB),
            source="E:/romfs/scene/test_scene_0_info.zsi!cmb[0]@0x60",
        )
        parsed_primary = model.textures[0]
        raw_primary = replace(parsed_primary, index=1, name="cube_raw_primary")
        raw_secondary = replace(parsed_primary, index=2, name="cube_raw_secondary")
        material = model.materials[0]
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (2).to_bytes(4, "little")
        raw[0x124:0x126] = (1).to_bytes(2, "little")
        raw[0x126:0x128] = (2).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, -1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                replace(material.texture_mappers[1], index=-1),
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
            raw_material=bytes(raw),
        )
        material_ref_primary = replace(
            material,
            index=1,
            texture_indices=(0, -1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                material.texture_mappers[1],
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
        )
        material_ref_secondary = replace(
            material,
            index=2,
            texture_indices=(1, -1, -1),
            texture_mappers=(
                replace(material.texture_mappers[0], index=1),
                material.texture_mappers[1],
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
        )
        scene_model = replace(
            model,
            textures=(parsed_primary, raw_primary, raw_secondary),
            materials=(raw_material, material_ref_primary, material_ref_secondary),
        )

        self.assertEqual(material_primary_texture_index(scene_model, raw_material), 0)
        self.assertEqual(material_texture(scene_model, raw_material), parsed_primary)
        self.assertIsNone(secondary_material_texture(scene_model, raw_material, 0))

        audit_record = material_audit_record(
            scene_model,
            raw_material,
            "scenes/oot3d/test",
            "gOot3dTestScene",
        )
        self.assertEqual(audit_record["texture_stage_selection_strategy"], "first_valid_mapper")
        self.assertEqual(audit_record["exported_texture_stage_count"], 1)
        self.assertEqual(audit_record["raw_texture_stage_export_gap"], 1)
        self.assertIn("raw_stage_export_gap", audit_record["texture_stage_risk_tags"])
        self.assertIn(
            "raw_stage_count_exceeds_mapper_count",
            audit_record["texture_stage_risk_tags"],
        )
        self.assertEqual(
            audit_record["raw_texture_stage_unexported_material_index_refs"][0][
                "raw_stage_index"
            ],
            1,
        )

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            export_static_model(
                scene_model,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="scenes/oot3d/test",
                    symbol="gOot3dTestScene",
                ),
            )
            material_output = (out / "gOot3dTestScene_mat_0").read_text(encoding="utf-8")

        self.assertEqual(material_output.count("<SetTextureImage"), 1)
        self.assertIn("cube_01.rgba16", material_output)
        self.assertNotIn("cube_raw_primary.rgba16", material_output)
        self.assertNotIn("cube_raw_secondary.rgba16", material_output)
        self.assertNotIn("G_CCMUX_TEXEL1", material_output)

    def test_scene_single_mapper_secondary_skips_ambiguous_material_index_ref(self) -> None:
        model = replace(
            CmbModel.from_path(CUBE_CMB),
            source="E:/romfs/scene/test_scene_0_info.zsi!cmb[0]@0x60",
        )
        parsed_primary = model.textures[0]
        raw_secondary = replace(parsed_primary, index=1, name="cube_raw_secondary")
        material = model.materials[0]
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (2).to_bytes(4, "little")
        raw[0x124:0x126] = (0).to_bytes(2, "little")
        raw[0x126:0x128] = (1).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, -1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                replace(material.texture_mappers[1], index=-1),
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
            raw_material=bytes(raw),
        )
        material_ref = replace(
            material,
            index=1,
            texture_indices=(0, -1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                material.texture_mappers[1],
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
        )
        scene_model = replace(
            model,
            textures=(parsed_primary, raw_secondary),
            materials=(raw_material, material_ref),
        )

        self.assertEqual(material_primary_texture_index(scene_model, raw_material), 0)
        self.assertIsNone(secondary_material_texture(scene_model, raw_material, 0))

        audit_record = material_audit_record(
            scene_model,
            raw_material,
            "scenes/oot3d/test",
            "gOot3dTestScene",
        )
        self.assertEqual(audit_record["selected_primary_texture"]["name"], "cube_01")
        self.assertIsNone(audit_record["selected_secondary_texture"])
        self.assertEqual(audit_record["exported_texture_stage_count"], 1)
        self.assertEqual(audit_record["raw_texture_stage_export_gap"], 1)
        self.assertIn("raw_stage_export_gap", audit_record["texture_stage_risk_tags"])

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            export_static_model(
                scene_model,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="scenes/oot3d/test",
                    symbol="gOot3dTestScene",
                ),
            )
            material_output = (out / "gOot3dTestScene_mat_0").read_text(encoding="utf-8")

        self.assertEqual(material_output.count("<SetTextureImage"), 1)
        self.assertIn("cube_01.rgba16", material_output)
        self.assertNotIn("cube_raw_secondary.rgba16", material_output)
        self.assertNotIn("G_CCMUX_TEXEL1", material_output)

    def test_material_uses_single_raw_stage_direct_primary(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        parsed_primary = model.textures[0]
        raw_primary = replace(parsed_primary, index=1, name="cube_raw_primary")
        material = model.materials[0]
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (1).to_bytes(4, "little")
        raw[0x124:0x126] = (1).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, -1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                replace(material.texture_mappers[1], index=-1),
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
            raw_material=bytes(raw),
        )
        model_with_raw_primary = replace(
            model,
            textures=(parsed_primary, raw_primary),
            materials=(raw_material,),
        )

        self.assertEqual(first_valid_texture(raw_material), 0)
        self.assertEqual(
            material_primary_texture_index(model_with_raw_primary, raw_material),
            1,
        )
        self.assertEqual(material_texture(model_with_raw_primary, raw_material), raw_primary)
        self.assertEqual(material_primary_texture_slot(raw_material, 1), 0)
        self.assertIsNone(secondary_material_texture(model_with_raw_primary, raw_material, 1))

        audit_record = material_audit_record(
            model_with_raw_primary,
            raw_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )
        self.assertEqual(
            audit_record["texture_stage_selection_strategy"],
            "raw_selector_single_direct_primary",
        )
        self.assertEqual(
            audit_record["texture_stage_confidence"],
            "raw_selector_single_direct_primary",
        )
        self.assertEqual(audit_record["texture_stage_risk_tags"], [])
        self.assertEqual(
            audit_record["raw_texture_stage_export_order_case"],
            "raw_prefix_matches_exported_order",
        )

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            export_static_model(
                model_with_raw_primary,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")

        self.assertEqual(material_output.count("<SetTextureImage"), 1)
        self.assertNotIn("cube_01.rgba16", material_output)
        self.assertIn("cube_raw_primary.rgba16", material_output)

    def test_material_uses_single_raw_stage_material_ref_primary(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        parsed_primary = model.textures[0]
        raw_primary = replace(parsed_primary, index=1, name="cube_raw_primary")
        material = model.materials[0]
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (1).to_bytes(4, "little")
        raw[0x124:0x126] = (1).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, -1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                replace(material.texture_mappers[1], index=-1),
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
            raw_material=bytes(raw),
        )
        material_ref = replace(
            material,
            index=1,
            texture_indices=(1, -1, -1),
            texture_mappers=(
                replace(material.texture_mappers[0], index=1),
                material.texture_mappers[1],
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
        )
        ambiguous_model = replace(
            model,
            textures=(parsed_primary, raw_primary),
            materials=(raw_material, material_ref),
        )

        self.assertEqual(material_primary_texture_index(ambiguous_model, raw_material), 1)
        self.assertEqual(material_texture(ambiguous_model, raw_material), raw_primary)

        audit_record = material_audit_record(
            ambiguous_model,
            raw_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )
        self.assertEqual(
            audit_record["texture_stage_selection_strategy"],
            "raw_selector_single_material_ref_primary",
        )
        self.assertEqual(
            audit_record["texture_stage_confidence"],
            "raw_selector_single_material_ref_primary",
        )
        self.assertEqual(audit_record["texture_stage_risk_tags"], [])
        self.assertEqual(audit_record["raw_texture_stage_unexported_material_index_refs"], [])
        self.assertEqual(
            audit_record["raw_texture_stage_export_order_case"],
            "raw_prefix_matches_exported_order",
        )

    def test_single_raw_stage_primary_skips_ambiguous_material_index_ref(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        parsed_primary = model.textures[0]
        raw_primary = replace(parsed_primary, index=1, name="cube_raw_primary")
        material = model.materials[0]
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (1).to_bytes(4, "little")
        raw[0x124:0x126] = (1).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, -1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                replace(material.texture_mappers[1], index=-1),
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
            raw_material=bytes(raw),
        )
        material_ref = replace(
            material,
            index=1,
            texture_indices=(0, -1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                material.texture_mappers[1],
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
        )
        ambiguous_model = replace(
            model,
            textures=(parsed_primary, raw_primary),
            materials=(raw_material, material_ref),
        )

        self.assertEqual(material_primary_texture_index(ambiguous_model, raw_material), 0)
        self.assertEqual(material_texture(ambiguous_model, raw_material), parsed_primary)

        audit_record = material_audit_record(
            ambiguous_model,
            raw_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )
        self.assertEqual(audit_record["texture_stage_selection_strategy"], "first_valid_mapper")
        self.assertEqual(
            audit_record["raw_texture_stage_unexported_material_index_refs"][0]["raw_stage_index"],
            1,
        )
        self.assertEqual(
            audit_record["raw_texture_stage_export_order_case"],
            "selected_primary_absent_from_raw_order",
        )

    def test_static_scene_keeps_mapper_for_single_raw_stage_material_index(self) -> None:
        model = replace(
            CmbModel.from_path(CUBE_CMB),
            source="E:/romfs/scene/test_scene_0_info.zsi!cmb[0]@0x60",
        )
        parsed_primary = model.textures[0]
        raw_primary = replace(parsed_primary, index=1, name="cube_raw_primary")
        material = model.materials[0]
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (1).to_bytes(4, "little")
        raw[0x124:0x126] = (1).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, -1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                replace(material.texture_mappers[1], index=-1),
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
            raw_material=bytes(raw),
        )
        material_ref = replace(
            material,
            index=1,
            texture_indices=(0, -1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                material.texture_mappers[1],
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
        )
        scene_model = replace(
            model,
            textures=(parsed_primary, raw_primary),
            materials=(raw_material, material_ref),
        )

        self.assertEqual(material_primary_texture_index(scene_model, raw_material), 0)
        self.assertEqual(material_texture(scene_model, raw_material), parsed_primary)

        audit_record = material_audit_record(
            scene_model,
            raw_material,
            "scenes/oot3d/test",
            "gOot3dTestScene",
        )
        self.assertEqual(audit_record["texture_stage_selection_strategy"], "first_valid_mapper")
        self.assertEqual(audit_record["texture_stage_confidence"], "single_texture_mapper")
        self.assertEqual(
            audit_record["raw_texture_stage_unexported_valid_textures"][0]["raw_stage_index"],
            1,
        )
        self.assertEqual(
            audit_record["raw_texture_stage_unexported_material_index_refs"][0]["raw_stage_index"],
            1,
        )
        self.assertEqual(
            audit_record["raw_texture_stage_export_order_case"],
            "selected_primary_absent_from_raw_order",
        )

    def test_material_uses_raw_prefix_when_it_starts_with_parsed_secondary(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        parsed_primary = model.textures[0]
        parsed_secondary = replace(parsed_primary, index=1, name="cube_raw_primary")
        raw_secondary = replace(parsed_primary, index=2, name="cube_raw_secondary")
        raw_tertiary = replace(parsed_primary, index=3, name="cube_raw_tertiary")
        material = model.materials[0]
        parsed_secondary_mapper = replace(material.texture_mappers[1], index=1)
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (3).to_bytes(4, "little")
        raw[0x124:0x126] = (1).to_bytes(2, "little")
        raw[0x126:0x128] = (2).to_bytes(2, "little")
        raw[0x128:0x12A] = (3).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, 1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                parsed_secondary_mapper,
                material.texture_mappers[2],
            ),
            texture_mappers_used=2,
            texture_coords_used=2,
            raw_material=bytes(raw),
        )
        model_with_raw_prefix = replace(
            model,
            textures=(parsed_primary, parsed_secondary, raw_secondary, raw_tertiary),
            materials=(raw_material,),
        )

        self.assertEqual(first_valid_texture(raw_material), 0)
        self.assertEqual(
            material_primary_texture_index(model_with_raw_prefix, raw_material),
            1,
        )
        self.assertEqual(material_primary_texture_slot(raw_material, 1), 1)
        self.assertEqual(material_texture_mapper(raw_material, 1), parsed_secondary_mapper)
        self.assertEqual(material_texture(model_with_raw_prefix, raw_material), parsed_secondary)
        self.assertEqual(
            secondary_material_texture(model_with_raw_prefix, raw_material, 1),
            raw_secondary,
        )
        self.assertEqual(
            secondary_material_texture_mapper(raw_material, 1),
            parsed_secondary_mapper,
        )
        self.assertEqual(secondary_texture_coord_export_status(raw_material, 1), "identity")

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            export_static_model(
                model_with_raw_prefix,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")

        self.assertEqual(material_output.count("<SetTextureImage"), 2)
        self.assertIn("cube_raw_primary.rgba16", material_output)
        self.assertIn("cube_raw_secondary.rgba16", material_output)
        self.assertNotIn("cube_raw_tertiary.rgba16", material_output)
        self.assertIn("G_CCMUX_TEXEL1", material_output)

    def test_material_uses_two_stage_raw_prefix_when_it_starts_with_parsed_secondary(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        parsed_primary = model.textures[0]
        parsed_secondary = replace(parsed_primary, index=1, name="cube_raw_primary")
        raw_secondary = replace(parsed_primary, index=2, name="cube_raw_secondary")
        material = model.materials[0]
        parsed_secondary_mapper = replace(material.texture_mappers[1], index=1)
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (2).to_bytes(4, "little")
        raw[0x124:0x126] = (1).to_bytes(2, "little")
        raw[0x126:0x128] = (2).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, 1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                parsed_secondary_mapper,
                material.texture_mappers[2],
            ),
            texture_mappers_used=2,
            texture_coords_used=2,
            raw_material=bytes(raw),
        )
        model_with_raw_prefix = replace(
            model,
            textures=(parsed_primary, parsed_secondary, raw_secondary),
            materials=(raw_material,),
        )

        self.assertEqual(first_valid_texture(raw_material), 0)
        self.assertEqual(
            material_primary_texture_index(model_with_raw_prefix, raw_material),
            1,
        )
        self.assertEqual(material_primary_texture_slot(raw_material, 1), 1)
        self.assertEqual(material_texture_mapper(raw_material, 1), parsed_secondary_mapper)
        self.assertEqual(material_texture(model_with_raw_prefix, raw_material), parsed_secondary)
        self.assertEqual(
            secondary_material_texture(model_with_raw_prefix, raw_material, 1),
            raw_secondary,
        )
        self.assertEqual(
            secondary_material_texture_mapper(raw_material, 1),
            parsed_secondary_mapper,
        )

        audit_record = material_audit_record(
            model_with_raw_prefix,
            raw_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )
        self.assertEqual(
            audit_record["texture_stage_selection_strategy"],
            "raw_selector_prefix_primary_plus_raw_secondary",
        )
        self.assertEqual(
            audit_record["texture_stage_confidence"],
            "raw_selector_prefix_primary_plus_raw_secondary",
        )
        self.assertEqual(audit_record["texture_stage_risk_tags"], [])
        self.assertEqual(
            audit_record["raw_texture_stage_export_order_case"],
            "raw_prefix_matches_exported_order",
        )

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            export_static_model(
                model_with_raw_prefix,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")

        self.assertEqual(material_output.count("<SetTextureImage"), 2)
        self.assertNotIn("cube_01.rgba16", material_output)
        self.assertIn("cube_raw_primary.rgba16", material_output)
        self.assertIn("cube_raw_secondary.rgba16", material_output)
        self.assertIn("G_CCMUX_TEXEL1", material_output)

    def test_material_uses_direct_raw_secondary_when_primary_prefix_matches(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        primary = model.textures[0]
        parsed_secondary = replace(primary, index=1, name="cube_placeholder_secondary")
        raw_secondary = replace(primary, index=2, name="cube_raw_secondary")
        material = model.materials[0]
        parsed_secondary_mapper = replace(material.texture_mappers[1], index=1)
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (2).to_bytes(4, "little")
        raw[0x124:0x126] = (0).to_bytes(2, "little")
        raw[0x126:0x128] = (2).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, 1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                parsed_secondary_mapper,
                material.texture_mappers[2],
            ),
            texture_mappers_used=2,
            texture_coords_used=2,
            raw_material=bytes(raw),
        )
        model_with_raw_secondary = replace(
            model,
            textures=(primary, parsed_secondary, raw_secondary),
            materials=(raw_material,),
        )

        self.assertEqual(
            material_primary_texture_index(model_with_raw_secondary, raw_material),
            0,
        )
        self.assertEqual(
            secondary_material_texture(model_with_raw_secondary, raw_material, 0),
            raw_secondary,
        )
        self.assertEqual(secondary_material_texture_slot(raw_material, 0), 1)
        self.assertEqual(
            secondary_material_texture_mapper(raw_material, 0),
            parsed_secondary_mapper,
        )

        audit_record = material_audit_record(
            model_with_raw_secondary,
            raw_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )
        self.assertEqual(
            audit_record["texture_stage_selection_strategy"],
            "raw_selector_primary_plus_direct_raw_secondary",
        )
        self.assertEqual(
            audit_record["texture_stage_confidence"],
            "raw_selector_primary_plus_direct_raw_secondary",
        )
        self.assertEqual(audit_record["texture_stage_risk_tags"], [])
        self.assertEqual(
            audit_record["raw_texture_stage_export_order_case"],
            "raw_prefix_matches_exported_order",
        )

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            export_static_model(
                model_with_raw_secondary,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")

        self.assertEqual(material_output.count("<SetTextureImage"), 2)
        self.assertIn("cube_01.rgba16", material_output)
        self.assertNotIn("cube_placeholder_secondary.rgba16", material_output)
        self.assertIn("cube_raw_secondary.rgba16", material_output)
        self.assertIn("G_CCMUX_TEXEL1", material_output)

        material_ref = replace(
            material,
            index=2,
            texture_indices=(2, -1, -1),
            texture_mappers=(
                replace(material.texture_mappers[0], index=2),
                material.texture_mappers[1],
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
        )
        ambiguous_model = replace(
            model_with_raw_secondary,
            materials=(raw_material, material, material_ref),
        )
        ambiguous_record = material_audit_record(
            ambiguous_model,
            raw_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )
        self.assertEqual(
            secondary_material_texture(ambiguous_model, raw_material, 0),
            parsed_secondary,
        )
        self.assertEqual(
            ambiguous_record["texture_stage_selection_strategy"],
            "first_valid_mapper_plus_distinct_secondary",
        )
        self.assertIn(
            "multi_texture_stage_selection_unverified",
            ambiguous_record["texture_stage_risk_tags"],
        )

    def test_material_uses_direct_raw_primary_when_secondary_suffix_matches(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        parsed_primary = model.textures[0]
        parsed_secondary = replace(parsed_primary, index=1, name="cube_exported_secondary")
        raw_primary = replace(parsed_primary, index=2, name="cube_raw_primary")
        material = model.materials[0]
        parsed_secondary_mapper = replace(material.texture_mappers[1], index=1)
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (2).to_bytes(4, "little")
        raw[0x124:0x126] = (2).to_bytes(2, "little")
        raw[0x126:0x128] = (1).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, 1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                parsed_secondary_mapper,
                material.texture_mappers[2],
            ),
            texture_mappers_used=2,
            texture_coords_used=2,
            raw_material=bytes(raw),
        )
        model_with_raw_primary = replace(
            model,
            textures=(parsed_primary, parsed_secondary, raw_primary),
            materials=(raw_material,),
        )

        self.assertEqual(first_valid_texture(raw_material), 0)
        self.assertEqual(
            material_primary_texture_index(model_with_raw_primary, raw_material),
            2,
        )
        self.assertEqual(material_texture(model_with_raw_primary, raw_material), raw_primary)
        self.assertEqual(material_primary_texture_slot(raw_material, 2), 0)
        self.assertEqual(
            secondary_material_texture(model_with_raw_primary, raw_material, 2),
            parsed_secondary,
        )
        self.assertEqual(secondary_material_texture_slot(raw_material, 2), 1)
        self.assertEqual(
            secondary_material_texture_mapper(raw_material, 2),
            parsed_secondary_mapper,
        )

        audit_record = material_audit_record(
            model_with_raw_primary,
            raw_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )
        self.assertEqual(
            audit_record["texture_stage_selection_strategy"],
            "raw_selector_direct_primary_plus_exported_secondary",
        )
        self.assertEqual(
            audit_record["texture_stage_confidence"],
            "raw_selector_direct_primary_plus_exported_secondary",
        )
        self.assertEqual(audit_record["texture_stage_risk_tags"], [])
        self.assertEqual(
            audit_record["raw_texture_stage_export_order_case"],
            "raw_prefix_matches_exported_order",
        )

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            export_static_model(
                model_with_raw_primary,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")

        self.assertEqual(material_output.count("<SetTextureImage"), 2)
        self.assertNotIn("cube_01.rgba16", material_output)
        self.assertIn("cube_raw_primary.rgba16", material_output)
        self.assertIn("cube_exported_secondary.rgba16", material_output)
        self.assertIn("G_CCMUX_TEXEL1", material_output)

        material_ref = replace(
            material,
            index=2,
            texture_indices=(2, -1, -1),
            texture_mappers=(
                replace(material.texture_mappers[0], index=2),
                material.texture_mappers[1],
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
        )
        ambiguous_model = replace(
            model_with_raw_primary,
            materials=(raw_material, material, material_ref),
        )
        ambiguous_record = material_audit_record(
            ambiguous_model,
            raw_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )
        self.assertEqual(
            material_primary_texture_index(ambiguous_model, raw_material),
            0,
        )
        self.assertEqual(
            ambiguous_record["texture_stage_selection_strategy"],
            "first_valid_mapper_plus_distinct_secondary",
        )
        self.assertIn(
            "multi_texture_stage_selection_unverified",
            ambiguous_record["texture_stage_risk_tags"],
        )

    def test_material_uses_raw_prefix_for_repeated_mapper_placeholders(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        parsed_primary = model.textures[0]
        raw_primary = replace(parsed_primary, index=1, name="cube_raw_primary")
        raw_secondary = replace(parsed_primary, index=2, name="cube_raw_secondary")
        material = model.materials[0]
        repeated_mapper = replace(material.texture_mappers[1], index=0)
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (2).to_bytes(4, "little")
        raw[0x124:0x126] = (1).to_bytes(2, "little")
        raw[0x126:0x128] = (2).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, 0, -1),
            texture_mappers=(
                material.texture_mappers[0],
                repeated_mapper,
                material.texture_mappers[2],
            ),
            texture_mappers_used=2,
            texture_coords_used=2,
            raw_material=bytes(raw),
        )
        model_with_raw_prefix = replace(
            model,
            textures=(parsed_primary, raw_primary, raw_secondary),
            materials=(raw_material,),
        )

        self.assertEqual(first_valid_texture(raw_material), 0)
        self.assertEqual(
            material_primary_texture_index(model_with_raw_prefix, raw_material),
            1,
        )
        self.assertEqual(material_primary_texture_slot(raw_material, 1), 0)
        self.assertEqual(material_texture_mapper(raw_material, 1), raw_material.texture_mappers[0])
        self.assertEqual(material_texture(model_with_raw_prefix, raw_material), raw_primary)
        self.assertEqual(
            secondary_material_texture(model_with_raw_prefix, raw_material, 1),
            raw_secondary,
        )
        self.assertEqual(secondary_material_texture_slot(raw_material, 1), 1)
        self.assertEqual(
            secondary_material_texture_mapper(raw_material, 1),
            raw_material.texture_mappers[1],
        )
        self.assertEqual(secondary_texture_coord_export_status(raw_material, 1), "identity")

        audit_record = material_audit_record(
            model_with_raw_prefix,
            raw_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )
        self.assertEqual(
            audit_record["texture_stage_selection_strategy"],
            "raw_selector_repeated_mapper_prefix",
        )
        self.assertEqual(
            audit_record["texture_stage_confidence"],
            "raw_selector_repeated_mapper_prefix",
        )
        self.assertEqual(audit_record["selected_secondary_slot"], 1)
        self.assertEqual(audit_record["raw_texture_stage_export_gap"], 0)
        self.assertEqual(
            audit_record["raw_texture_stage_export_order_case"],
            "raw_prefix_matches_exported_order",
        )
        self.assertNotIn("repeated_active_texture_index", material_issues(audit_record))
        self.assertNotIn(
            "multi_texture_stage_selection_unverified",
            audit_record["texture_stage_risk_tags"],
        )

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            export_static_model(
                model_with_raw_prefix,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")

        self.assertEqual(material_output.count("<SetTextureImage"), 2)
        self.assertNotIn("cube_01.rgba16", material_output)
        self.assertIn("cube_raw_primary.rgba16", material_output)
        self.assertIn("cube_raw_secondary.rgba16", material_output)
        self.assertIn("G_CCMUX_TEXEL1", material_output)

    def test_material_uses_raw_active_mapper_pair_when_parsed_primary_is_absent(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        parsed_primary = model.textures[0]
        raw_secondary = replace(parsed_primary, index=1, name="cube_raw_secondary")
        raw_primary = replace(parsed_primary, index=2, name="cube_raw_primary")
        material = model.materials[0]
        secondary_mapper = replace(material.texture_mappers[1], index=1)
        primary_mapper = replace(material.texture_mappers[2], index=2)
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (2).to_bytes(4, "little")
        raw[0x124:0x126] = (2).to_bytes(2, "little")
        raw[0x126:0x128] = (1).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, 1, 2),
            texture_mappers=(
                material.texture_mappers[0],
                secondary_mapper,
                primary_mapper,
            ),
            texture_mappers_used=3,
            texture_coords_used=3,
            raw_material=bytes(raw),
        )
        model_with_raw_pair = replace(
            model,
            textures=(parsed_primary, raw_secondary, raw_primary),
            materials=(raw_material,),
        )

        self.assertEqual(first_valid_texture(raw_material), 0)
        self.assertEqual(
            material_primary_texture_index(model_with_raw_pair, raw_material),
            2,
        )
        self.assertEqual(material_primary_texture_slot(raw_material, 2), 2)
        self.assertEqual(material_texture(model_with_raw_pair, raw_material), raw_primary)
        self.assertEqual(
            secondary_material_texture(model_with_raw_pair, raw_material, 2),
            raw_secondary,
        )
        self.assertEqual(secondary_material_texture_slot(raw_material, 2), 1)
        self.assertEqual(
            secondary_material_texture_mapper(raw_material, 2),
            secondary_mapper,
        )

        audit_record = material_audit_record(
            model_with_raw_pair,
            raw_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )
        self.assertEqual(
            audit_record["texture_stage_selection_strategy"],
            "raw_selector_active_mapper_pair",
        )
        self.assertEqual(
            audit_record["texture_stage_confidence"],
            "raw_selector_active_mapper_pair",
        )
        self.assertEqual(audit_record["selected_primary_slot"], 2)
        self.assertEqual(audit_record["selected_secondary_slot"], 1)
        self.assertEqual(audit_record["raw_texture_stage_export_gap"], 0)
        self.assertEqual(
            audit_record["raw_texture_stage_export_order_case"],
            "raw_prefix_matches_exported_order",
        )
        self.assertEqual(audit_record["texture_stage_risk_tags"], [])

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            export_static_model(
                model_with_raw_pair,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")

        self.assertEqual(material_output.count("<SetTextureImage"), 2)
        self.assertNotIn("cube_01.rgba16", material_output)
        self.assertIn("cube_raw_primary.rgba16", material_output)
        self.assertIn("cube_raw_secondary.rgba16", material_output)
        self.assertIn("G_CCMUX_TEXEL1", material_output)

    def test_material_audit_resolves_single_stage_same_texture_duplicate_mapper(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        material = model.materials[0]
        repeated_mapper = replace(material.texture_mappers[1], index=0)
        secondary_coord = replace(
            material.texture_coords[1],
            translation=Vec2(0.0, 1.0),
        )
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (1).to_bytes(4, "little")
        raw[0x124:0x126] = (0).to_bytes(2, "little")
        same_texture_material = replace(
            material,
            texture_indices=(0, 0, -1),
            texture_mappers=(
                material.texture_mappers[0],
                repeated_mapper,
                material.texture_mappers[2],
            ),
            texture_coords=(
                material.texture_coords[0],
                secondary_coord,
                material.texture_coords[2],
            ),
            texture_mappers_used=2,
            texture_coords_used=2,
            raw_material=bytes(raw),
        )
        model_with_same_texture = replace(model, materials=(same_texture_material,))

        self.assertIsNone(secondary_material_texture(model_with_same_texture, same_texture_material, 0))
        self.assertIsNone(secondary_material_texture_slot(same_texture_material, 0))
        self.assertEqual(secondary_texture_coord_export_status(same_texture_material, 0), "identity")

        audit_record = material_audit_record(
            model_with_same_texture,
            same_texture_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )
        self.assertEqual(
            audit_record["texture_stage_selection_strategy"],
            "raw_selector_single_stage_duplicate_same_texture",
        )
        self.assertEqual(
            audit_record["texture_stage_confidence"],
            "raw_selector_single_stage_duplicate_same_texture",
        )
        self.assertEqual(audit_record["selected_secondary_slot"], None)
        self.assertEqual(audit_record["exported_texture_stage_count"], 1)
        self.assertEqual(audit_record["raw_texture_stage_export_gap"], 0)
        self.assertNotIn("multi_texture_without_distinct_secondary", material_issues(audit_record))
        self.assertNotIn("repeated_active_texture_index", material_issues(audit_record))
        self.assertNotIn(
            "multi_texture_stage_selection_unverified",
            audit_record["texture_stage_risk_tags"],
        )
        self.assertNotIn("secondary_stage_not_distinct", audit_record["texture_stage_risk_tags"])

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            export_static_model(
                model_with_same_texture,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")

        self.assertEqual(material_output.count("<SetTextureImage"), 1)
        self.assertIn("cube_01.rgba16", material_output)
        self.assertNotIn("G_CCMUX_TEXEL1", material_output)

    def test_material_exports_same_texture_raw_mapper_slot_prefix(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        primary = model.textures[0]
        material = model.materials[0]
        repeated_mapper = replace(material.texture_mappers[1], index=0)
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (2).to_bytes(4, "little")
        raw[0x124:0x126] = (0).to_bytes(2, "little")
        raw[0x126:0x128] = (1).to_bytes(2, "little")
        same_texture_material = replace(
            material,
            texture_indices=(0, 0, -1),
            texture_mappers=(
                material.texture_mappers[0],
                repeated_mapper,
                material.texture_mappers[2],
            ),
            texture_mappers_used=2,
            texture_coords_used=2,
            raw_material=bytes(raw),
        )
        model_with_same_texture = replace(model, materials=(same_texture_material,))

        self.assertEqual(
            secondary_material_texture(model_with_same_texture, same_texture_material, 0),
            primary,
        )
        self.assertEqual(secondary_material_texture_slot(same_texture_material, 0), 1)
        self.assertEqual(
            secondary_material_texture_mapper(same_texture_material, 0),
            same_texture_material.texture_mappers[1],
        )
        self.assertEqual(secondary_texture_coord_export_status(same_texture_material, 0), "identity")

        audit_record = material_audit_record(
            model_with_same_texture,
            same_texture_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )
        self.assertEqual(
            audit_record["texture_stage_selection_strategy"],
            "raw_selector_mapper_slot_same_texture",
        )
        self.assertEqual(
            audit_record["texture_stage_confidence"],
            "raw_selector_mapper_slot_same_texture",
        )
        self.assertEqual(audit_record["selected_secondary_slot"], 1)
        self.assertEqual(audit_record["selected_secondary_texture"]["name"], primary.name)
        self.assertEqual(audit_record["selected_secondary_texture_coord_export_status"], "identity")
        self.assertEqual(audit_record["raw_texture_stage_export_gap"], 0)
        self.assertNotIn("multi_texture_without_distinct_secondary", material_issues(audit_record))
        self.assertNotIn("repeated_active_texture_index", material_issues(audit_record))
        self.assertNotIn("secondary_stage_not_distinct", audit_record["texture_stage_risk_tags"])

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            export_static_model(
                model_with_same_texture,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")

        self.assertEqual(material_output.count("<SetTextureImage"), 2)
        self.assertEqual(material_output.count("cube_01.rgba16"), 2)
        self.assertIn("G_CCMUX_TEXEL1", material_output)

    def test_material_exports_same_texture_secondary_when_uv_differs(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        primary = model.textures[0]
        material = model.materials[0]
        repeated_mapper = replace(material.texture_mappers[1], index=0)
        secondary_coord = replace(
            material.texture_coords[1],
            scale=Vec2(2.0, 2.0),
            rotation=0.0,
            translation=Vec2(0.0, 0.5),
        )
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (2).to_bytes(4, "little")
        raw[0x124:0x126] = (0).to_bytes(2, "little")
        raw[0x126:0x128] = (1).to_bytes(2, "little")
        same_texture_material = replace(
            material,
            texture_indices=(0, 0, -1),
            texture_mappers=(
                material.texture_mappers[0],
                repeated_mapper,
                material.texture_mappers[2],
            ),
            texture_coords=(
                material.texture_coords[0],
                secondary_coord,
                material.texture_coords[2],
            ),
            texture_mappers_used=2,
            texture_coords_used=2,
            raw_material=bytes(raw),
        )
        model_with_same_texture = replace(model, materials=(same_texture_material,))

        self.assertEqual(
            secondary_material_texture(model_with_same_texture, same_texture_material, 0),
            primary,
        )
        self.assertEqual(secondary_material_texture_slot(same_texture_material, 0), 1)
        self.assertEqual(
            secondary_material_texture_mapper(same_texture_material, 0),
            same_texture_material.texture_mappers[1],
        )
        self.assertEqual(secondary_texture_coord_export_status(same_texture_material, 0), "baked_texture")

        audit_record = material_audit_record(
            model_with_same_texture,
            same_texture_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )
        self.assertEqual(
            audit_record["texture_stage_selection_strategy"],
            "same_texture_transformed_secondary",
        )
        self.assertEqual(
            audit_record["texture_stage_confidence"],
            "same_texture_transformed_secondary",
        )
        self.assertEqual(audit_record["selected_secondary_slot"], 1)
        self.assertEqual(audit_record["selected_secondary_texture"]["name"], primary.name)
        self.assertEqual(audit_record["selected_secondary_texture_coord_export_status"], "baked_texture")
        self.assertEqual(audit_record["raw_texture_stage_export_gap"], 0)
        self.assertNotIn("multi_texture_without_distinct_secondary", material_issues(audit_record))
        self.assertNotIn("repeated_active_texture_index", material_issues(audit_record))
        self.assertNotIn("secondary_stage_not_distinct", audit_record["texture_stage_risk_tags"])

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = export_static_model(
                model_with_same_texture,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )
            baked_resource = next(
                resource
                for resource in result.resources
                if resource.kind == "Texture" and resource.path.endswith("cube_01_mat_0_uv1.rgba16")
            )
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")
            self.assertTrue(Path(baked_resource.file).exists())

        self.assertEqual(material_output.count("<SetTextureImage"), 2)
        self.assertIn("cube_01.rgba16", material_output)
        self.assertIn("cube_01_mat_0_uv1.rgba16", material_output)
        self.assertIn("G_CCMUX_TEXEL1", material_output)

    def test_material_uses_single_valid_raw_primary_when_parsed_primary_is_absent(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        parsed_primary = model.textures[0]
        raw_primary = replace(parsed_primary, index=1, name="cube_raw_primary")
        material = model.materials[0]
        inactive_mapper = replace(material.texture_mappers[1], index=-1)
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (2).to_bytes(4, "little")
        raw[0x124:0x126] = (1).to_bytes(2, "little")
        raw[0x126:0x128] = (2).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, -1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                inactive_mapper,
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
            raw_material=bytes(raw),
        )
        model_with_raw_primary = replace(
            model,
            textures=(parsed_primary, raw_primary),
            materials=(raw_material,),
        )

        self.assertEqual(first_valid_texture(raw_material), 0)
        self.assertEqual(
            material_primary_texture_index(model_with_raw_primary, raw_material),
            1,
        )
        self.assertEqual(material_texture(model_with_raw_primary, raw_material), raw_primary)
        self.assertIsNone(
            secondary_material_texture(model_with_raw_primary, raw_material, 1)
        )

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            export_static_model(
                model_with_raw_primary,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")

        self.assertEqual(material_output.count("<SetTextureImage"), 1)
        self.assertIn("cube_raw_primary.rgba16", material_output)
        self.assertNotIn("cube_01.rgba16", material_output)
        self.assertNotIn("G_CCMUX_TEXEL1", material_output)

    def test_material_uses_single_raw_stage_as_primary_when_no_mapper(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        primary = model.textures[0]
        material = model.materials[0]
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (1).to_bytes(4, "little")
        raw[0x124:0x126] = (0).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(-1, -1, -1),
            texture_mappers=tuple(
                replace(mapper, index=-1) for mapper in material.texture_mappers
            ),
            texture_mappers_used=0,
            texture_coords_used=0,
            raw_material=bytes(raw),
        )
        model_with_raw_primary = replace(model, textures=(primary,), materials=(raw_material,))

        self.assertIsNone(first_valid_texture(raw_material))
        self.assertEqual(material_primary_texture_index(model_with_raw_primary, raw_material), 0)
        self.assertEqual(material_texture(model_with_raw_primary, raw_material), primary)

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            export_static_model(
                model_with_raw_primary,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/cube",
                    symbol="gOot3dCube",
                ),
            )
            material_output = (out / "gOot3dCube_mat_0").read_text(encoding="utf-8")

        self.assertEqual(material_output.count("<SetTextureImage"), 1)
        self.assertNotIn("G_CCMUX_TEXEL1", material_output)

    def test_material_audit_records_raw_oob_delta_signature(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        material = model.materials[0]
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (3).to_bytes(4, "little")
        raw[0x124:0x126] = (1).to_bytes(2, "little")
        raw[0x126:0x128] = (2).to_bytes(2, "little")
        raw[0x128:0x12A] = (3).to_bytes(2, "little")
        raw_material = replace(material, raw_material=bytes(raw))

        audit_record = material_audit_record(
            model,
            raw_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )
        bounds = audit_record["raw_texture_stage_slot_bounds"]

        self.assertEqual(bounds["active_raw_stage_slots"], [1, 2, 3])
        self.assertEqual(bounds["oob_deltas_from_texture_count"], [0, 1, 2])
        self.assertEqual(bounds["oob_delta_signature"], "delta_0_1_2")
        self.assertEqual(bounds["sequence_case"], "texture_count_boundary_oob_run")

    def test_material_audit_records_unexported_valid_raw_stage_textures(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        primary = model.textures[0]
        secondary = replace(primary, index=1, name="cube_raw_secondary")
        tertiary = replace(primary, index=2, name="cube_raw_tertiary")
        material = model.materials[0]
        secondary_mapper = replace(material.texture_mappers[1], index=1)
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (3).to_bytes(4, "little")
        raw[0x124:0x126] = (0).to_bytes(2, "little")
        raw[0x126:0x128] = (1).to_bytes(2, "little")
        raw[0x128:0x12A] = (2).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, 1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                secondary_mapper,
                material.texture_mappers[2],
            ),
            texture_mappers_used=2,
            texture_coords_used=2,
            raw_material=bytes(raw),
        )
        raw_model = replace(
            model,
            textures=(primary, secondary, tertiary),
            materials=(raw_material,),
        )

        audit_record = material_audit_record(
            raw_model,
            raw_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )

        self.assertEqual(audit_record["exported_texture_stage_count"], 2)
        self.assertEqual(audit_record["raw_texture_stage_export_gap"], 1)
        self.assertEqual(
            audit_record["raw_texture_stage_unexported_valid_textures"],
            [
                {
                    "stage_position": 2,
                    "raw_stage_index": 2,
                    "texture": {
                        "index": 2,
                        "name": "cube_raw_tertiary",
                        "width": tertiary.width,
                        "height": tertiary.height,
                        "format": f"0x{tertiary.texture_format:x}",
                        "data_type": f"0x{tertiary.data_type:x}",
                    },
                }
            ],
        )

    def test_material_audit_records_non_gap_unexported_raw_stage_textures(self) -> None:
        model = CmbModel.from_path(CUBE_CMB)
        primary = model.textures[0]
        exported_secondary = replace(primary, index=1, name="cube_exported_secondary")
        raw_secondary = replace(primary, index=2, name="cube_raw_secondary")
        material = model.materials[0]
        exported_secondary_mapper = replace(material.texture_mappers[1], index=1)
        raw = bytearray(material.raw_material)
        raw[0x120:0x124] = (2).to_bytes(4, "little")
        raw[0x124:0x126] = (0).to_bytes(2, "little")
        raw[0x126:0x128] = (2).to_bytes(2, "little")
        raw_material = replace(
            material,
            texture_indices=(0, 1, -1),
            texture_mappers=(
                material.texture_mappers[0],
                exported_secondary_mapper,
                material.texture_mappers[2],
            ),
            texture_mappers_used=2,
            texture_coords_used=2,
            raw_material=bytes(raw),
        )
        material_ref = replace(
            material,
            index=2,
            texture_indices=(2, -1, -1),
            texture_mappers=(
                replace(material.texture_mappers[0], index=2),
                material.texture_mappers[1],
                material.texture_mappers[2],
            ),
            texture_mappers_used=1,
            texture_coords_used=1,
        )
        raw_model = replace(
            model,
            textures=(primary, exported_secondary, raw_secondary),
            materials=(raw_material, material, material_ref),
        )

        audit_record = material_audit_record(
            raw_model,
            raw_material,
            "objects/oot3d/cube",
            "gOot3dCube",
        )

        self.assertEqual(audit_record["exported_texture_stage_count"], 2)
        self.assertEqual(audit_record["raw_texture_stage_export_gap"], 0)
        self.assertEqual(
            audit_record["raw_texture_stage_candidate_summary"]["exported_candidate_count"],
            1,
        )
        self.assertEqual(
            audit_record["raw_texture_stage_unexported_valid_textures"],
            [
                {
                    "stage_position": 1,
                    "raw_stage_index": 2,
                    "texture": {
                        "index": 2,
                        "name": "cube_raw_secondary",
                        "width": raw_secondary.width,
                        "height": raw_secondary.height,
                        "format": f"0x{raw_secondary.texture_format:x}",
                        "data_type": f"0x{raw_secondary.data_type:x}",
                    },
                }
            ],
        )
        self.assertEqual(
            audit_record["raw_texture_stage_unexported_material_refs"],
            [],
        )
        self.assertEqual(
            audit_record["raw_texture_stage_unexported_material_index_refs"],
            [
                {
                    "stage_position": 1,
                    "raw_stage_index": 2,
                    "material_ref": {
                        "material_position": 2,
                        "material_index": 2,
                        "primary_texture_index": 0,
                        "primary_texture": {
                            "index": 0,
                            "name": "cube_01",
                            "width": primary.width,
                            "height": primary.height,
                            "format": f"0x{primary.texture_format:x}",
                            "data_type": f"0x{primary.data_type:x}",
                        },
                    },
                }
            ],
        )


@unittest.skipUnless(DK_LIGHTBOX_ZAR.exists(), "local OOT3D dk_lightbox.zar fixture is not present")
class ZarConversionTests(unittest.TestCase):
    def test_export_zar_embedded_cmb_with_duplicate_texture_names(self) -> None:
        archive = ZarArchive.from_path(DK_LIGHTBOX_ZAR)
        cmb_file = archive.cmb_files()[0]
        model = CmbModel.parse(archive.read_file(cmb_file), f"{DK_LIGHTBOX_ZAR}!{cmb_file.name}")

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = export_static_model(
                model,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/dk_lightbox",
                    symbol="gOot3dDkLightbox",
                ),
            )
            texture_resources = [res for res in result.resources if res.kind == "Texture"]
            self.assertEqual(len(texture_resources), 2)
            self.assertEqual(len({res.path for res in texture_resources}), 2)
            self.assertTrue((out / "lightbox4_model.rgba16").exists())
            self.assertTrue((out / "lightbox4_model_1.rgba16").exists())


@unittest.skipUnless(ZELDA_BOX_ZAR.exists(), "local OOT3D zelda_box.zar fixture is not present")
class RigidMultiboneExportTests(unittest.TestCase):
    def test_export_rigid_multibone_l8_zar_cmb_with_textures(self) -> None:
        archive = ZarArchive.from_path(ZELDA_BOX_ZAR)
        cmb_file = next(
            file for file in archive.cmb_files() if file.name == "Model/demo_tre_lgt_mdl_info.cmb"
        )
        model = CmbModel.parse(archive.read_file(cmb_file), f"{ZELDA_BOX_ZAR}!{cmb_file.name}")

        self.assertTrue(model.is_rigid_export_candidate())
        self.assertEqual(model.textures[0].texture_format, PICA_TEXTURE_LUMINANCE)
        self.assertEqual(model.textures[0].data_type, 0x1401)

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = export_static_model(
                model,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/demo_tre_lgt",
                    symbol="gOot3dDemoTreLgt",
                ),
            )
            texture_resources = [res for res in result.resources if res.kind == "Texture"]
            self.assertGreaterEqual(len(texture_resources), 2)
            for resource in texture_resources[:2]:
                self.assertTrue(Path(resource.file).exists())
                self.assertGreater(Path(resource.file).stat().st_size, 0x40)

    def test_audit_raw_selector_exported_order_pair_is_resolved(self) -> None:
        archive = ZarArchive.from_path(ZELDA_BOX_ZAR)
        cmb_file = next(
            file for file in archive.cmb_files() if file.name == "Model/demo_tre_lgt_mdl_info.cmb"
        )
        model = CmbModel.parse(archive.read_file(cmb_file), f"{ZELDA_BOX_ZAR}!{cmb_file.name}")
        material = model.materials[0]

        audit_record = material_audit_record(
            model,
            material,
            "objects/oot3d/demo_tre_lgt",
            "gOot3dDemoTreLgt",
        )

        self.assertEqual(audit_record["texture_indices"], (0, 1, -1))
        self.assertEqual(audit_record["raw_texture_stage_selector"]["stage_indices"], [0, 1])
        self.assertEqual(audit_record["raw_texture_stage_selector"]["stage_count"], 2)
        self.assertEqual(audit_record["exported_texture_stage_count"], 2)
        self.assertEqual(audit_record["raw_texture_stage_export_gap"], 0)
        self.assertEqual(
            audit_record["raw_texture_stage_export_order_case"],
            "raw_prefix_matches_exported_order",
        )
        self.assertEqual(
            audit_record["texture_stage_selection_strategy"],
            "raw_selector_exported_order",
        )
        self.assertEqual(audit_record["texture_stage_confidence"], "raw_selector_exported_order")
        self.assertEqual(audit_record["texture_stage_risk_tags"], [])
        self.assertEqual(material_issues(audit_record), [])

    def test_export_rigid_multibone_zar_cmb_applies_bone_transform(self) -> None:
        archive = ZarArchive.from_path(ZELDA_BOX_ZAR)
        cmb_file = next(file for file in archive.cmb_files() if file.name == "Model/tr_box.cmb")
        model = CmbModel.parse(archive.read_file(cmb_file), f"{ZELDA_BOX_ZAR}!{cmb_file.name}")

        self.assertFalse(model.is_static_candidate())
        self.assertTrue(model.is_rigid_export_candidate())

        mesh = next(mesh for mesh in model.meshes if mesh.shape_index == 2)
        shape = model.shapes[mesh.shape_index]
        material = model.materials[mesh.material_index]
        texture = material_texture(model, material)
        primitive = shape.primitives[0]
        self.assertEqual(primitive.skinning_mode, 0)
        self.assertEqual(primitive.bone_indices, (2,))

        vertex_index = primitive.indices[0]
        plain_vertex = to_legacy_fast_resource_vertex(shape, texture, material, vertex_index)
        transformed_vertex = to_legacy_fast_resource_vertex(
            shape,
            texture,
            material,
            vertex_index,
            vertex_transform=skeleton_world_transforms(model.skeleton)[2],
        )
        self.assertNotEqual(
            (plain_vertex.x, plain_vertex.y, plain_vertex.z),
            (transformed_vertex.x, transformed_vertex.y, transformed_vertex.z),
        )
        self.assertGreater(abs(transformed_vertex.y), 1000)
        self.assertGreater(abs(transformed_vertex.z), 1000)

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = export_static_model(
                model,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/tr_box",
                    symbol="gOot3dTrBox",
                ),
            )
            manifest = json.loads(result.manifest_path.read_text(encoding="utf-8"))
            self.assertFalse(manifest["counts"]["static_candidate"])
            self.assertTrue(manifest["counts"]["rigid_export_candidate"])
            self.assertEqual(manifest["counts"]["skeleton"]["bone_count"], 3)
            self.assertTrue(list(out.glob("*_vtx")))


@unittest.skipUnless(ZELDA_XC_ZAR.exists(), "local OOT3D zelda_xc.zar fixture is not present")
class OptionalShapeAttributeTests(unittest.TestCase):
    def test_parse_rigid_shape_with_global_uv_vld_but_no_uv_flag(self) -> None:
        archive = ZarArchive.from_path(ZELDA_XC_ZAR)
        cmb_file = next(
            file for file in archive.cmb_files() if file.name == "Model/demo_ec_tfc_cp_modelT.cmb"
        )
        model = CmbModel.parse(archive.read_file(cmb_file), f"{ZELDA_XC_ZAR}!{cmb_file.name}")

        self.assertTrue(model.is_rigid_export_candidate())
        self.assertEqual(model.bone_count, 3)
        self.assertEqual(len(model.shapes), 2)
        self.assertNotEqual(model.shapes[0].uv0[1], Vec2(0.0, 0.0))
        self.assertEqual(len(model.shapes[1].uv0), len(model.shapes[1].positions))
        self.assertTrue(all(uv == Vec2(0.0, 0.0) for uv in model.shapes[1].uv0))


class LuminanceAlphaDecodeTests(unittest.TestCase):
    def test_la8_uses_native_alpha_luminance_byte_order(self) -> None:
        self.assertEqual(
            decode_la8_to_rgba16(bytes((0x00, 0xFF, 0xFF, 0x00)), 2),
            bytes.fromhex("fffe0001"),
        )


@unittest.skipUnless(ZELDA_FZ_ZAR.exists(), "local OOT3D zelda_fz.zar fixture is not present")
class LuminanceAlphaConversionTests(unittest.TestCase):
    def test_export_rigid_multibone_la8_zar_cmb_with_textures(self) -> None:
        archive = ZarArchive.from_path(ZELDA_FZ_ZAR)
        cmb_file = next(file for file in archive.cmb_files() if file.name == "Model/frezad.cmb")
        model = CmbModel.parse(archive.read_file(cmb_file), f"{ZELDA_FZ_ZAR}!{cmb_file.name}")

        self.assertTrue(model.is_rigid_export_candidate())
        self.assertGreaterEqual(len(model.materials), 1)
        material = model.materials[0]
        self.assertTrue(material.fragment_lighting_enabled)
        self.assertIsNotNone(material.lighting_block)
        assert material.lighting_block is not None
        self.assertTrue(material.lighting_block.pica_bump_mode_recognized)
        self.assertEqual(material.lighting_block.pica_bump_mode, 1)
        self.assertTrue(material.lighting_block.pica_bump_texture_unit_recognized)
        self.assertEqual(material.lighting_block.pica_bump_texture_unit, 0)
        self.assertEqual(material.texture_indices[0], 0)
        la8_texture = model.textures[0]
        self.assertEqual(la8_texture.texture_format, PICA_TEXTURE_LUMINANCE_ALPHA)
        self.assertEqual(la8_texture.data_type, 0x1401)
        self.assertEqual(len(la8_texture.data), la8_texture.width * la8_texture.height * 2)

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = export_static_model(
                model,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/frezad",
                    symbol="gOot3dFrezad",
                ),
            )
            texture_resources = [res for res in result.resources if res.kind == "Texture"]
            self.assertGreaterEqual(len(texture_resources), 2)
            texture_path = Path(texture_resources[0].file)
            expected_size = 0x40 + 0x1C + la8_texture.width * la8_texture.height * 2
            self.assertEqual(texture_path.stat().st_size, expected_size)


@unittest.skipUnless(ZELDA_KEEP_ZAR.exists(), "local OOT3D zelda_keep.zar fixture is not present")
class LuminanceAlpha4ConversionTests(unittest.TestCase):
    def test_export_la4_zar_cmb_with_textures(self) -> None:
        archive = ZarArchive.from_path(ZELDA_KEEP_ZAR)
        cmb_file = next(
            file for file in archive.cmb_files() if file.name == "magic_fire/model/acto_magic_fire.cmb"
        )
        model = CmbModel.parse(archive.read_file(cmb_file), f"{ZELDA_KEEP_ZAR}!{cmb_file.name}")

        self.assertTrue(model.is_rigid_export_candidate())
        la4_texture = model.textures[0]
        self.assertEqual(la4_texture.texture_format, PICA_TEXTURE_LUMINANCE_ALPHA)
        self.assertEqual(la4_texture.data_type, PICA_UNSIGNED_BYTE_4_4)
        self.assertEqual(len(la4_texture.data), la4_texture.width * la4_texture.height)

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = export_static_model(
                model,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/acto_magic_fire",
                    symbol="gOot3dActoMagicFire",
                ),
            )
            texture_resources = [res for res in result.resources if res.kind == "Texture"]
            la4_resource = next(
                res for res in texture_resources if Path(res.file).name == "acto_magic_fire.rgba16"
            )
            expected_size = 0x40 + 0x1C + la4_texture.width * la4_texture.height * 2
            self.assertEqual(Path(la4_resource.file).stat().st_size, expected_size)


@unittest.skipUnless(TOKINOMA_ROOM0_ZSI.exists(), "local OOT3D tokinoma room fixture is not present")
class Luminance4ConversionTests(unittest.TestCase):
    def test_export_rigid_multibone_l4_zsi_cmb_with_textures(self) -> None:
        model = ZsiFile.from_path(TOKINOMA_ROOM0_ZSI).embedded_cmbs()[0].model

        self.assertTrue(model.is_rigid_export_candidate())
        l4_texture = next(
            texture
            for texture in model.textures
            if texture.texture_format == PICA_TEXTURE_LUMINANCE
            and texture.data_type == PICA_UNSIGNED_4BITS
        )
        self.assertEqual(l4_texture.name, "toki_glass_01bx")
        self.assertEqual(len(l4_texture.data), l4_texture.width * l4_texture.height // 2)

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = export_static_model(
                model,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/tokinoma_room_0",
                    symbol="gOot3dTokinomaRoom0",
                ),
            )
            texture_resources = [res for res in result.resources if res.kind == "Texture"]
            l4_resource = next(res for res in texture_resources if Path(res.file).name == "toki_glass_01bx_4.rgba16")
            expected_size = 0x40 + 0x1C + l4_texture.width * l4_texture.height * 2
            self.assertEqual(Path(l4_resource.file).stat().st_size, expected_size)


@unittest.skipUnless(BDAN_OBJECTS_ZAR.exists(), "local OOT3D zelda_bdan_objects.zar fixture is not present")
class Etc1ConversionTests(unittest.TestCase):
    def test_export_static_zar_cmb_with_etc1_texture(self) -> None:
        archive = ZarArchive.from_path(BDAN_OBJECTS_ZAR)
        cmb_file = archive.cmb_files()[0]
        model = CmbModel.parse(archive.read_file(cmb_file), f"{BDAN_OBJECTS_ZAR}!{cmb_file.name}")
        self.assertEqual(model.textures[0].texture_format, 0x675A)

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = export_static_model(
                model,
                LegacyFastResourceOptions(
                    output_dir=out,
                    resource_root="objects/oot3d/bdan_door0",
                    symbol="gOot3dBdanDoor0",
                ),
            )
            texture_resources = [res for res in result.resources if res.kind == "Texture"]
            self.assertEqual(len(texture_resources), 1)
            texture_path = Path(texture_resources[0].file)
            self.assertTrue(texture_path.exists())
            expected_size = 0x40 + 0x1C + model.textures[0].width * model.textures[0].height * 2
            self.assertEqual(texture_path.stat().st_size, expected_size)

class ZsiCutsceneAuditTests(unittest.TestCase):
    @staticmethod
    def _write_minimal_strict_cutscene_scene(scene_dir: Path) -> None:
        zsi = bytearray(b"ZSI\x01")
        zsi.extend(b"\0" * (0x180 - len(zsi)))

        def put_u16(offset: int, value: int) -> None:
            zsi[offset : offset + 2] = value.to_bytes(2, "little", signed=False)

        def put_s16(offset: int, value: int) -> None:
            zsi[offset : offset + 2] = value.to_bytes(2, "little", signed=True)

        def put_u32(offset: int, value: int) -> None:
            zsi[offset : offset + 4] = value.to_bytes(4, "little", signed=False)

        def put_s32(offset: int, value: int) -> None:
            zsi[offset : offset + 4] = value.to_bytes(4, "little", signed=True)

        def put_f32(offset: int, value: float) -> None:
            zsi[offset : offset + 4] = struct.pack("<f", value)

        def put_camera_point(
            offset: int,
            continue_flag: int,
            roll: int,
            frame: int,
            view_angle: float,
            pos: tuple[int, int, int],
        ) -> None:
            zsi[offset] = continue_flag & 0xFF
            zsi[offset + 1] = roll & 0xFF
            put_u16(offset + 2, frame)
            put_f32(offset + 4, view_angle)
            put_s16(offset + 8, pos[0])
            put_s16(offset + 10, pos[1])
            put_s16(offset + 12, pos[2])
            put_s16(offset + 14, 0)

        put_u32(0x18, 0x15)
        put_u32(0x1C, 0)
        put_u32(0x20, 0x17)
        put_u32(0x24, 0x80)
        put_u32(0x28, 0x14)
        put_u32(0x2C, 0)

        cursor = 0x80 + 0x18
        put_s32(cursor, 3)
        put_s32(cursor + 4, 120)
        cursor += 8
        for command_id, pos in ((0x01, (10, 20, 30)), (0x02, (-10, 40, -30))):
            put_s32(cursor, command_id)
            cursor += 4
            put_u32(cursor, 1)
            put_u32(cursor + 4, 120)
            cursor += 8
            put_camera_point(cursor, -1, 3, 0, 45.0, pos)
            cursor += 16
        put_s32(cursor, 0x03E8)
        put_u32(cursor + 4, 1)
        put_u32(cursor + 8, 0)
        put_u32(cursor + 12, (120 << 16) | 120)

        (scene_dir / "test_info.zsi").write_bytes(zsi)

    def test_zsi_cutscene_metadata_audit_tracks_scene_command_and_strict_decode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scene_dir = root / "scene"
            scene_dir.mkdir()
            self._write_minimal_strict_cutscene_scene(scene_dir)


            output = root / "zsi_cutscene_metadata_audit.json"
            audit = audit_zsi_cutscene_metadata(scene_dir, output, sample_limit=4)

            self.assertTrue(output.exists())

        self.assertEqual(audit["format"], "oot3d_zsi_cutscene_metadata_audit_v2")
        self.assertEqual(audit["zsi_file_count"], 1)
        self.assertEqual(audit["role_counts"], {"scene": 1})
        self.assertEqual(audit["cutscene_file_count"], 1)
        self.assertEqual(audit["cutscene_role_counts"], {"scene": 1})
        self.assertEqual(audit["cutscene_setup_count"], 1)
        self.assertEqual(audit["cutscene_command_total"], 1)
        self.assertEqual(audit["unique_cutscene_data_offset_count"], 1)
        self.assertEqual(audit["argument_in_bounds_count"], 1)
        self.assertEqual(audit["argument_out_of_bounds_count"], 0)
        self.assertEqual(audit["header_candidate_counts_by_delta"]["+0x18"], 1)
        self.assertEqual(audit["strict_n64_decoded_count"], 1)
        self.assertEqual(audit["strict_n64_decode_error_count"], 0)
        self.assertEqual(audit["strict_n64_decode_counts_by_delta"], {"+0x18": 1})
        self.assertEqual(audit["strict_n64_command_total"], 3)
        self.assertEqual(audit["strict_n64_camera_point_total"], 2)
        self.assertEqual(
            audit["strict_n64_command_id_counts"],
            {"0x0001": 1, "0x0002": 1, "0x03e8": 1},
        )
        self.assertEqual(
            audit["strict_n64_command_category_counts"],
            {"camera_list": 2, "simple_16_byte": 1},
        )
        decoded = audit["records"][0]["strict_n64_decode"]
        self.assertTrue(decoded["decoded"])
        self.assertEqual(decoded["total_entries"], 3)
        self.assertEqual(decoded["end_frame"], 120)
        self.assertEqual(
            [command["command_id_hex"] for command in decoded["commands"]],
            ["0x0001", "0x0002", "0x03e8"],
        )
        self.assertEqual(decoded["commands"][0]["list_header"]["param"], 1)
        self.assertEqual(decoded["commands"][0]["list_header"]["end_frame"], 120)
        self.assertEqual(decoded["commands"][0]["camera_points"][0]["continue_flag"], -1)
        self.assertEqual(decoded["commands"][0]["camera_points"][0]["camera_roll"], 3)
        self.assertEqual(decoded["commands"][0]["camera_points"][0]["view_angle"], 45.0)
        self.assertEqual(decoded["commands"][0]["camera_points"][0]["pos"], {"x": 10, "y": 20, "z": 30})
        self.assertEqual(audit["parse_error_count"], 0)

    def test_zsi_cutscene_camera_export_writes_strict_blocks(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scene_dir = root / "scene"
            scene_dir.mkdir()
            self._write_minimal_strict_cutscene_scene(scene_dir)

            output = root / "zsi_cutscene_camera_export"
            manifest = export_zsi_cutscene_camera_data(scene_dir, output, sample_limit=4)
            exported_path = Path(manifest["records"][0]["export_file"])
            exported = json.loads(exported_path.read_text(encoding="utf-8"))

            self.assertTrue((output / "zsi_cutscene_camera_export_manifest.json").exists())
            self.assertTrue(exported_path.exists())

        self.assertEqual(manifest["format"], "oot3d_zsi_cutscene_camera_export_manifest_v1")
        self.assertEqual(manifest["source_cutscene_command_total"], 1)
        self.assertEqual(manifest["source_strict_n64_decoded_count"], 1)
        self.assertEqual(manifest["source_strict_n64_decode_error_count"], 0)
        self.assertEqual(manifest["exported_cutscene_count"], 1)
        self.assertEqual(manifest["exported_command_count"], 3)
        self.assertEqual(manifest["exported_camera_command_count"], 2)
        self.assertEqual(manifest["exported_camera_point_count"], 2)
        self.assertEqual(manifest["export_issue_count"], 0)
        self.assertEqual(manifest["issue_counts"], {"none": 1})
        self.assertEqual(manifest["command_id_counts"], {"0x0001": 1, "0x0002": 1, "0x03e8": 1})
        self.assertEqual(
            manifest["command_category_counts"],
            {"camera_list": 2, "simple_16_byte": 1},
        )
        self.assertEqual(exported["format"], "oot3d_zsi_strict_n64_cutscene_block_v1")
        self.assertEqual(exported["decode"]["camera_point_count"], 2)
        self.assertEqual(exported["decode"]["commands"][1]["camera_points"][0]["pos"]["x"], -10)


class CollisionPipelineTests(unittest.TestCase):
    def test_collision_reference_comparison_accepts_matching_floor(self) -> None:
        scene = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )

        comparison = compare_collision_scenes(
            scene,
            scene,
            floor_probe_count=1,
            floor_tolerance=1.0,
            bounds_tolerance=1.0,
        )

        self.assertTrue(comparison["accepted"])
        self.assertEqual(comparison["floor_probe_count"], 1)
        self.assertEqual(comparison["floor_hit_rate"], 1.0)
        self.assertEqual(comparison["floor_match_rate"], 1.0)
        self.assertTrue(comparison["polygon_category_ratio_summary"]["accepted"])

    def test_collision_reference_comparison_rejects_excessive_category_ratio(self) -> None:
        reference = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )
        duplicated = CollisionScene(
            vertices=reference.vertices,
            polygons=(
                CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),
                CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),
                CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),
                CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),
                CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),
            ),
            metadata=reference.metadata,
        )

        comparison = compare_collision_scenes(
            reference,
            duplicated,
            floor_probe_count=1,
            floor_tolerance=1.0,
            bounds_tolerance=1.0,
        )

        self.assertFalse(comparison["accepted"])
        self.assertIn("polygon_category_ratios", comparison["failed_checks"])
        self.assertEqual(
            comparison["polygon_category_ratio_summary"]["failed_records"][0]["category"],
            "floor",
        )
        self.assertEqual(
            comparison["polygon_category_ratio_summary"]["failed_records"][0]["ratio"],
            5.0,
        )

    def test_collision_reference_comparison_accepts_total_bounded_category_overage(self) -> None:
        vertices = ((0, 0, 0), (100, 0, 0), (0, 0, 100), (0, 100, 0), (0, 100, 100))
        floor = CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0)
        wall = CollisionPolygon(0, 0, 3, 4, 32767, 0, 0, 0)
        reference = CollisionScene(
            vertices=vertices,
            polygons=(floor, floor, wall, wall),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )
        converted = CollisionScene(
            vertices=vertices,
            polygons=(floor,) * 10 + (wall,) * 2,
            metadata=reference.metadata,
        )

        comparison = compare_collision_scenes(
            reference,
            converted,
            floor_probe_count=1,
            floor_tolerance=1.0,
            bounds_tolerance=1.0,
        )

        self.assertTrue(comparison["accepted"])
        ratio_summary = comparison["polygon_category_ratio_summary"]
        self.assertTrue(ratio_summary["accepted"])
        self.assertEqual(ratio_summary["total_ratio"], 3.0)
        self.assertEqual(
            ratio_summary["records"][0]["status"],
            "accepted_total_ratio_bounded_overage",
        )

    def test_collision_reference_comparison_rejects_shifted_floor(self) -> None:
        reference = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )
        shifted = CollisionScene(
            vertices=((0, 200, 0), (100, 200, 0), (0, 200, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, -200),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )

        comparison = compare_collision_scenes(
            reference,
            shifted,
            floor_probe_count=1,
            floor_tolerance=10.0,
            bounds_tolerance=500.0,
        )

        self.assertFalse(comparison["accepted"])
        self.assertIn("floor_match_rate", comparison["failed_checks"])
        self.assertEqual(comparison["floor_failure_count"], 1)

    def test_collision_reference_comparison_uses_geometry_bounds_for_gate(self) -> None:
        reference = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
            bounds_min=(-5000, -5000, -5000),
            bounds_max=(5000, 5000, 5000),
        )
        converted = CollisionScene(
            vertices=reference.vertices,
            polygons=reference.polygons,
            metadata=reference.metadata,
            bounds_min=(0, 0, 0),
            bounds_max=(100, 0, 100),
        )

        comparison = compare_collision_scenes(
            reference,
            converted,
            floor_probe_count=1,
            bounds_tolerance=1.0,
        )

        self.assertTrue(comparison["accepted"])
        self.assertEqual(comparison["max_bounds_delta"], 0)
        self.assertEqual(
            comparison["bounds_delta"]["bounds_source"],
            "collision_vertex_bounds",
        )
        self.assertGreater(
            max(comparison["resource_bounds_delta"]["absolute"]),
            1.0,
        )

    def test_collision_reference_comparison_reports_bad_source_spawn_floor(self) -> None:
        reference = CollisionScene(
            vertices=(
                (0, 0, 0),
                (0, 0, 100),
                (100, 0, 0),
                (200, 100, 0),
                (200, 100, 100),
                (300, 100, 0),
            ),
            polygons=(
                CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),
                CollisionPolygon(0, 3, 4, 5, 0, 32767, 0, -100),
            ),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )
        converted = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )

        comparison = compare_collision_scenes(
            reference,
            converted,
            floor_probe_count=1,
            floor_tolerance=10.0,
            bounds_tolerance=500.0,
            source_spawn_floor_probes=((225.0, 100.0, 25.0),),
        )

        self.assertTrue(comparison["accepted"])
        self.assertNotIn("source_spawn_floor_probes", comparison["failed_checks"])
        self.assertEqual(
            comparison["source_spawn_floor_probes"]["converted_failure_count"],
            1,
        )

    def test_collision_reference_comparison_ignores_unsupported_source_spawn_floor(self) -> None:
        scene = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )

        comparison = compare_collision_scenes(
            scene,
            scene,
            floor_probe_count=1,
            floor_tolerance=10.0,
            bounds_tolerance=1.0,
            source_spawn_floor_probes=((25.0, 100.0, 25.0),),
        )

        self.assertTrue(comparison["accepted"])
        self.assertEqual(
            comparison["source_spawn_floor_probes"]["unsupported_source_probe_count"],
            1,
        )
        self.assertEqual(comparison["source_spawn_floor_probes"]["converted_failure_count"], 0)

    def test_collision_reference_comparison_uses_runtime_floor_query_height(self) -> None:
        reference = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )
        stacked = CollisionScene(
            vertices=(
                (0, 0, 0),
                (100, 0, 0),
                (0, 0, 100),
                (0, 120, 0),
                (100, 120, 0),
                (0, 120, 100),
            ),
            polygons=(
                CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),
                CollisionPolygon(0, 3, 4, 5, 0, 32767, 0, -120),
            ),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )

        runtime_height = compare_collision_scenes(
            reference,
            stacked,
            floor_probe_count=1,
            floor_tolerance=10.0,
            bounds_tolerance=500.0,
        )
        too_high = compare_collision_scenes(
            reference,
            stacked,
            floor_probe_count=1,
            floor_query_above=200.0,
            floor_tolerance=10.0,
            bounds_tolerance=500.0,
        )

        self.assertTrue(runtime_height["accepted"])
        self.assertEqual(runtime_height["thresholds"]["floor_query_above"], 50.0)
        self.assertFalse(too_high["accepted"])
        self.assertEqual(too_high["floor_failures_sample"][0]["floor_y"], 120.0)

    def test_collision_reference_comparison_rejects_missing_surface_semantics(self) -> None:
        reference = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 1 << 17),)),
        )
        converted = CollisionScene(
            vertices=reference.vertices,
            polygons=reference.polygons,
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )

        comparison = compare_collision_scenes(reference, converted, floor_probe_count=1)

        self.assertFalse(comparison["accepted"])
        self.assertFalse(comparison["surface_semantics_present"])
        self.assertIn("surface_semantics_present", comparison["failed_checks"])
        self.assertIn("surface.hookshot", comparison["missing_reference_semantics"])

    def test_collision_reference_comparison_rejects_surface_exit_index_mismatch(self) -> None:
        reference = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(1 << 8, 0),)),
        )
        converted = CollisionScene(
            vertices=reference.vertices,
            polygons=reference.polygons,
            metadata=CollisionMetadata(surface_types=(SurfaceType(2 << 8, 0),)),
        )

        comparison = compare_collision_scenes(reference, converted, floor_probe_count=1)

        self.assertFalse(comparison["accepted"])
        self.assertIn("surface_exit_index_usage", comparison["failed_checks"])
        self.assertEqual(comparison["reference_exit_index_usage"], {1: 1})
        self.assertEqual(comparison["converted_exit_index_usage"], {2: 1})
        self.assertEqual(comparison["surface_exit_index_usage_summary"]["missing_values"], [1])
        self.assertEqual(comparison["surface_exit_index_usage_summary"]["extra_values"], [2])

    def test_collision_reference_comparison_accepts_triangulated_exit_surface_counts(self) -> None:
        reference = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(1 << 8, 0),)),
        )
        converted = CollisionScene(
            vertices=reference.vertices,
            polygons=(
                CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),
                CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),
                CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),
            ),
            metadata=reference.metadata,
        )

        comparison = compare_collision_scenes(
            reference,
            converted,
            floor_probe_count=1,
            floor_tolerance=1.0,
            bounds_tolerance=1.0,
        )

        self.assertTrue(comparison["accepted"])
        self.assertEqual(comparison["reference_exit_index_usage"], {1: 1})
        self.assertEqual(comparison["converted_exit_index_usage"], {1: 3})
        self.assertTrue(comparison["surface_exit_index_usage_summary"]["accepted"])
        self.assertEqual(comparison["surface_exit_index_usage_summary"]["records"][0]["ratio"], 3.0)

    def test_native_zsi_acceptance_gate_rejects_invalid_required_exit_lists(self) -> None:
        gate = native_zsi_collision_acceptance_gate(
            {
                "status": "decoded_source_player_spawn_floor_probes",
                "required_gameplay_probe_count": 1,
            },
            {
                "status": "decoded_source_exit_lists",
                "accepted": False,
                "exit_list_command_count": 1,
            },
            {
                "accepted": True,
                "failed_checks": [],
                "source_spawn_floor_probes": {"accepted": True},
                "reference_exit_index_usage": {1: 1},
                "converted_exit_index_usage": {1: 1},
            },
        )

        self.assertFalse(gate["accepted"])
        self.assertIn("source_exit_list_layout", gate["failed_checks"])
        self.assertTrue(gate["exit_usage_requires_source_list"])

    def test_native_zsi_acceptance_gate_allows_missing_exit_lists_without_exit_usage(self) -> None:
        gate = native_zsi_collision_acceptance_gate(
            {
                "status": "decoded_source_player_spawn_floor_probes",
                "required_gameplay_probe_count": 1,
            },
            {
                "status": "no_source_exit_lists",
                "accepted": False,
                "exit_list_command_count": 0,
            },
            {
                "accepted": True,
                "failed_checks": [],
                "source_spawn_floor_probes": {"accepted": True},
                "reference_exit_index_usage": {},
                "converted_exit_index_usage": {},
            },
        )

        self.assertTrue(gate["accepted"])
        self.assertFalse(gate["exit_usage_requires_source_list"])

    def test_native_zsi_acceptance_gate_rejects_bad_source_spawn_without_visual_diagnosis(self) -> None:
        gate = native_zsi_collision_acceptance_gate(
            {
                "status": "decoded_source_player_spawn_floor_probes",
                "required_gameplay_probe_count": 1,
            },
            {
                "status": "no_source_exit_lists",
                "accepted": False,
                "exit_list_command_count": 0,
            },
            {
                "accepted": True,
                "failed_checks": [],
                "source_spawn_floor_probes": {
                    "accepted": False,
                    "converted_failure_count": 1,
                    "converted_failures_sample": [{"probe_index": 0}],
                },
                "reference_exit_index_usage": {},
                "converted_exit_index_usage": {},
            },
        )

        self.assertFalse(gate["accepted"])
        self.assertIn("source_gameplay_spawn_probes_match_or_diagnosed", gate["failed_checks"])
        self.assertEqual(
            gate["source_spawn_floor_probe_policy"]["status"],
            "visual_source_spawn_floor_audit_unavailable",
        )

    def test_native_zsi_acceptance_gate_accepts_visual_spawn_divergence_diagnosis(self) -> None:
        gate = native_zsi_collision_acceptance_gate(
            {
                "status": "decoded_source_player_spawn_floor_probes",
                "required_gameplay_probe_count": 1,
            },
            {
                "status": "no_source_exit_lists",
                "accepted": False,
                "exit_list_command_count": 0,
            },
            {
                "accepted": True,
                "failed_checks": [],
                "source_spawn_floor_probes": {
                    "accepted": False,
                    "converted_failure_count": 1,
                    "converted_failures_sample": [{"probe_index": 0}],
                },
                "reference_exit_index_usage": {},
                "converted_exit_index_usage": {},
            },
            visual_source_spawn_floor_audit={
                "status": "decoded_visual_mesh_source_spawn_floor_audit",
                "visual_mesh": {
                    "failures_sample": [{"probe_index": 0, "delta_y": -1200.0}],
                },
            },
        )

        self.assertTrue(gate["accepted"])
        self.assertEqual(
            gate["source_spawn_floor_probe_policy"]["status"],
            "source_spawn_failures_match_oot3d_visual_divergence",
        )

    def test_native_zsi_acceptance_gate_rejects_when_visual_mesh_supports_failed_spawn(self) -> None:
        gate = native_zsi_collision_acceptance_gate(
            {
                "status": "decoded_source_player_spawn_floor_probes",
                "required_gameplay_probe_count": 1,
            },
            {
                "status": "no_source_exit_lists",
                "accepted": False,
                "exit_list_command_count": 0,
            },
            {
                "accepted": True,
                "failed_checks": [],
                "source_spawn_floor_probes": {
                    "accepted": False,
                    "converted_failure_count": 1,
                    "converted_failures_sample": [{"probe_index": 0}],
                },
                "reference_exit_index_usage": {},
                "converted_exit_index_usage": {},
            },
            visual_source_spawn_floor_audit={
                "status": "decoded_visual_mesh_source_spawn_floor_audit",
                "visual_mesh": {
                    "failures_sample": [],
                },
            },
        )

        self.assertFalse(gate["accepted"])
        self.assertEqual(
            gate["source_spawn_floor_probe_policy"]["status"],
            "visual_mesh_supports_failed_source_spawn_floor",
        )

    def test_visual_mesh_floor_audit_is_diagnostic_not_collision_input(self) -> None:
        visual_model = CmbModel(
            source="<test_visual_room>",
            name="test_visual_room",
            version=6,
            bone_count=0,
            skeleton=Skeleton(0, 0, ()),
            textures=(),
            materials=(),
            meshes=(Mesh(0, 0, 0, 0),),
            shapes=(
                Shape(
                    0,
                    0,
                    0,
                    (Vec3(0.0, 100.0, 0.0), Vec3(0.0, 100.0, 100.0), Vec3(100.0, 100.0, 0.0)),
                    (),
                    (),
                    (),
                    (Primitive((0, 1, 2), 0, ()),),
                ),
            ),
        )
        native = CollisionScene(
            vertices=((0, 100, 0), (0, 100, 100), (100, 100, 0)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, -100),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
            bounds_min=(-5000, -5000, -5000),
            bounds_max=(5000, 5000, 5000),
        )
        reference = CollisionScene(
            vertices=((0, 0, 0), (0, 0, 100), (100, 0, 0)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
            bounds_min=(-5000, -5000, -5000),
            bounds_max=(5000, 5000, 5000),
        )

        audit = visual_mesh_collision_floor_audit(
            [(0, visual_model)],
            native,
            reference_scene=reference,
            floor_probe_count=1,
            floor_tolerance=10.0,
        )

        self.assertEqual(audit["role"], "diagnostic_only_not_collision_source")
        self.assertEqual(audit["native_zsi"]["match_rate"], 1.0)
        self.assertEqual(
            audit["native_zsi"]["bounds_delta_from_visual"]["bounds_source"],
            "collision_vertex_bounds",
        )
        self.assertEqual(
            audit["native_zsi"]["bounds_delta_from_visual"]["absolute"],
            (0, 0, 0, 0, 0, 0),
        )
        self.assertEqual(audit["n64_reference"]["match_rate"], 0.0)
        self.assertEqual(
            audit["n64_reference"]["bounds_delta_from_visual"]["bounds_source"],
            "collision_vertex_bounds",
        )
        self.assertEqual(
            audit["diagnosis"],
            "oot3d_visual_geometry_diverges_from_n64_reference",
        )

    def test_visual_mesh_source_spawn_floor_audit_is_diagnostic_not_collision_input(self) -> None:
        visual_model = CmbModel(
            source="<test_visual_room>",
            name="test_visual_room",
            version=6,
            bone_count=0,
            skeleton=Skeleton(0, 0, ()),
            textures=(),
            materials=(),
            meshes=(Mesh(0, 0, 0, 0),),
            shapes=(
                Shape(
                    0,
                    0,
                    0,
                    (Vec3(0.0, 100.0, 0.0), Vec3(0.0, 100.0, 100.0), Vec3(100.0, 100.0, 0.0)),
                    (),
                    (),
                    (),
                    (Primitive((0, 1, 2), 0, ()),),
                ),
            ),
        )

        audit = visual_mesh_source_spawn_floor_audit(
            [(0, visual_model)],
            ((25.0, 100.0, 25.0), (25.0, 0.0, 25.0)),
            floor_query_above=200.0,
            floor_tolerance=10.0,
        )

        self.assertEqual(audit["status"], "decoded_visual_mesh_source_spawn_floor_audit")
        self.assertEqual(audit["role"], "diagnostic_only_not_collision_source")
        self.assertEqual(audit["visual_mesh"]["match_count"], 1)
        self.assertEqual(audit["visual_mesh"]["failure_count"], 1)
        self.assertEqual(audit["visual_mesh"]["max_abs_delta"], 100.0)

    def test_visual_mesh_floor_diagnosis_uses_relative_match_rate(self) -> None:
        diagnosis = visual_mesh_floor_diagnosis(
            {"match_rate": 0.02},
            {"match_rate": 0.76},
            min_floor_match_rate=0.85,
            match_rate_margin=0.15,
        )

        self.assertEqual(
            diagnosis,
            "native_zsi_collision_probably_mismatches_oot3d_visual_mesh",
        )

    def test_visual_diagnostic_policy_keeps_reference_failures_blocking(self) -> None:
        policy = collision_visual_diagnostic_policy(
            {
                "accepted": False,
                "failed_checks": ["water_box_coverage"],
            },
            {
                "accepted": True,
                "source_spawn_floor_probe_policy": {
                    "status": "source_spawn_failures_match_oot3d_visual_divergence",
                },
            },
            {
                "status": "decoded_visual_mesh_floor_audit",
                "diagnosis": "oot3d_visual_geometry_diverges_from_n64_reference",
            },
            {
                "status": "decoded_visual_mesh_source_spawn_floor_audit",
            },
        )

        self.assertFalse(policy["visual_mesh_is_collision_source"])
        self.assertEqual(policy["collision_source"], "oot3d_zsi_native_collision")
        self.assertEqual(
            policy["relaxed_checks_from_visual_diagnostics"],
            ["source_gameplay_spawn_probes_match_or_diagnosed"],
        )
        self.assertEqual(policy["reference_failed_checks_still_blocking"], ["water_box_coverage"])
        self.assertIn("water_box_coverage", policy["forbidden_visual_relaxations"])

    def test_visual_diagnostic_policy_has_no_relaxation_without_spawn_divergence(self) -> None:
        policy = collision_visual_diagnostic_policy(
            {
                "accepted": True,
                "failed_checks": [],
            },
            {
                "accepted": True,
                "source_spawn_floor_probe_policy": {
                    "status": "native_zsi_spawn_probes_match",
                },
            },
            None,
            None,
        )

        self.assertEqual(policy["relaxed_checks_from_visual_diagnostics"], [])
        self.assertIsNone(policy["visual_floor_audit_status"])
        self.assertIsNone(policy["visual_source_spawn_audit_status"])

    def test_collision_reference_comparison_rejects_metadata_mismatches(self) -> None:
        reference = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(
                camera_data=(CameraData(32, 3, 0),),
                camera_positions=(CameraPositionData((1, 2, 3), (4, 5, 6), (60, 10, -1)),),
                water_boxes=(WaterBox(0, 0, 0, 100, 100, 269),),
                surface_types=(SurfaceType(0, 0),),
            ),
        )
        converted = CollisionScene(
            vertices=reference.vertices,
            polygons=reference.polygons,
            metadata=CollisionMetadata(
                camera_data=(CameraData(32, 3, 1),),
                camera_positions=(CameraPositionData((1, 2, 3), (4, 5, 6), (55, 10, -1)),),
                water_boxes=(WaterBox(0, 8, 0, 100, 100, 269),),
                surface_types=(SurfaceType(0, 0),),
            ),
        )

        comparison = compare_collision_scenes(reference, converted, floor_probe_count=1)

        self.assertFalse(comparison["accepted"])
        self.assertIn("camera_data_exact", comparison["failed_checks"])
        self.assertIn("camera_position_exact", comparison["failed_checks"])
        self.assertIn("water_box_coverage", comparison["failed_checks"])
        self.assertEqual(comparison["metadata_match"]["camera_data"]["first_mismatch_index"], 0)
        self.assertEqual(comparison["metadata_match"]["camera_positions"]["first_mismatch_index"], 0)
        self.assertEqual(comparison["metadata_match"]["water_boxes"]["first_mismatch_index"], 0)

    def test_collision_reference_comparison_rejects_missing_polygon_flags(self) -> None:
        reference = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 0x2001, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )
        converted = CollisionScene(
            vertices=reference.vertices,
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )

        comparison = compare_collision_scenes(reference, converted, floor_probe_count=1)

        self.assertFalse(comparison["accepted"])
        self.assertIn("polygon_flag_coverage", comparison["failed_checks"])
        self.assertEqual(
            comparison["insufficient_polygon_flags"],
            [
                {
                    "name": "polygon.conveyor",
                    "reference_count": 1,
                    "converted_count": 0,
                    "ratio": 0.0,
                    "min_ratio": 0.85,
                    "status": "below_threshold",
                }
            ],
        )
        self.assertFalse(comparison["polygon_flag_coverage_summary"]["accepted"])

    def test_collision_reference_comparison_accepts_compatible_water_box_extents(self) -> None:
        reference = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(
                water_boxes=(WaterBox(-4678, -873, 2203, 680, 400, 258),),
                surface_types=(SurfaceType(0, 0),),
            ),
        )
        converted = CollisionScene(
            vertices=reference.vertices,
            polygons=reference.polygons,
            metadata=CollisionMetadata(
                water_boxes=(WaterBox(-4678, -873, 2220, 680, 440, 258),),
                surface_types=reference.metadata.surface_types,
            ),
        )

        comparison = compare_collision_scenes(reference, converted, floor_probe_count=1)

        self.assertTrue(comparison["accepted"])
        water_match = comparison["metadata_match"]["water_boxes"]
        self.assertTrue(water_match["accepted"])
        self.assertFalse(water_match["exact_match"])
        self.assertGreaterEqual(water_match["records"][0]["overlap_ratio"], 0.75)

    def test_collision_reference_comparison_accepts_exact_degenerate_water_box(self) -> None:
        water_box = WaterBox(0, 0, 0, 0, 0, 258)
        reference = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(
                water_boxes=(water_box,),
                surface_types=(SurfaceType(0, 0),),
            ),
        )
        converted = CollisionScene(
            vertices=reference.vertices,
            polygons=reference.polygons,
            metadata=reference.metadata,
        )

        comparison = compare_collision_scenes(reference, converted, floor_probe_count=1)

        self.assertTrue(comparison["accepted"])
        self.assertEqual(comparison["metadata_match"]["water_boxes"]["records"][0]["status"], "exact")

    def test_collision_reference_comparison_accepts_native_extra_water_box(self) -> None:
        reference = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )
        converted = CollisionScene(
            vertices=reference.vertices,
            polygons=reference.polygons,
            metadata=CollisionMetadata(
                water_boxes=(WaterBox(-600, -9, -1400, 1200, 1200, 260),),
                surface_types=reference.metadata.surface_types,
            ),
        )

        comparison = compare_collision_scenes(reference, converted, floor_probe_count=1)

        self.assertTrue(comparison["accepted"])
        water_match = comparison["metadata_match"]["water_boxes"]
        self.assertTrue(water_match["accepted"])
        self.assertEqual(water_match["extra_converted_count"], 1)
        self.assertFalse(water_match["exact_match"])
        self.assertEqual(
            water_match["match_policy"],
            "reference_coverage_allows_native_oot3d_extras",
        )

    def test_collision_reference_comparison_rejects_missing_reference_water_box(self) -> None:
        reference = CollisionScene(
            vertices=((0, 0, 0), (100, 0, 0), (0, 0, 100)),
            polygons=(CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
            metadata=CollisionMetadata(
                water_boxes=(WaterBox(0, 0, 0, 100, 100, 258),),
                surface_types=(SurfaceType(0, 0),),
            ),
        )
        converted = CollisionScene(
            vertices=reference.vertices,
            polygons=reference.polygons,
            metadata=CollisionMetadata(surface_types=reference.metadata.surface_types),
        )

        comparison = compare_collision_scenes(reference, converted, floor_probe_count=1)

        self.assertFalse(comparison["accepted"])
        self.assertIn("water_box_coverage", comparison["failed_checks"])
        self.assertEqual(
            comparison["metadata_match"]["water_boxes"]["failed_records"][0]["status"],
            "missing_converted",
        )

    def test_collision_reference_comparison_accepts_polygon_flag_count_ratio(self) -> None:
        vertices = ((0, 0, 0), (100, 0, 0), (0, 0, 100))
        flagged = CollisionPolygon(0, 0, 0x2001, 2, 0, 32767, 0, 0)
        reference = CollisionScene(
            vertices=vertices,
            polygons=(flagged,) * 10,
            metadata=CollisionMetadata(surface_types=(SurfaceType(0, 0),)),
        )
        converted = CollisionScene(
            vertices=vertices,
            polygons=(flagged,) * 9,
            metadata=reference.metadata,
        )

        comparison = compare_collision_scenes(
            reference,
            converted,
            floor_probe_count=1,
            bounds_tolerance=1.0,
        )

        self.assertTrue(comparison["accepted"])
        self.assertTrue(comparison["polygon_flag_coverage_summary"]["accepted"])
        self.assertEqual(
            comparison["polygon_flag_coverage_summary"]["records"][0]["ratio"],
            0.9,
        )

    def test_load_collision_scene_from_o2r_round_trips_binary_ocol(self) -> None:
        vertices = ((0, 0, 0), (100, 0, 0), (0, 0, 100))
        polygons = (CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),)
        metadata = CollisionMetadata(
            camera_data=(CameraData(32, 3, 0),),
            camera_positions=(CameraPositionData((1, 2, 3), (4, 5, 6), (60, 10, -1)),),
            water_boxes=(WaterBox(0, 0, 0, 100, 100, 0x3F000),),
            surface_types=(SurfaceType(0x12, 0x34),),
        )

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            collision_file = root / "testCollisionHeader"
            archive_path = root / "test.o2r"
            resource_path = "scenes/shared/test_scene/testCollisionHeader"
            write_collision_resource(collision_file, vertices, polygons, metadata)
            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.write(collision_file, resource_path)
            scene = load_collision_scene_from_o2r(archive_path, resource_path)

        self.assertEqual(scene.vertices, vertices)
        self.assertEqual(scene.polygons, polygons)
        self.assertEqual(scene.metadata.surface_types, metadata.surface_types)
        self.assertEqual(scene.metadata.camera_data, metadata.camera_data)
        self.assertEqual(scene.metadata.camera_positions, metadata.camera_positions)
        self.assertEqual(scene.metadata.water_boxes, metadata.water_boxes)


@unittest.skipUnless(SPOT04_ROOM0_ZSI.exists(), "local OOT3D spot04 room fixture is not present")
class ZsiConversionTests(unittest.TestCase):
    def test_parse_spot04_room_embedded_cmb(self) -> None:
        zsi = ZsiFile.from_path(SPOT04_ROOM0_ZSI)
        cmbs = zsi.embedded_cmbs()

        self.assertEqual(len(cmbs), 1)
        self.assertEqual(cmbs[0].offset, 844)
        self.assertEqual(cmbs[0].model.name, "spot04_00")
        self.assertTrue(cmbs[0].model.is_static_candidate())
        self.assertEqual(cmbs[0].model.summary()["triangle_count"], 6846)

    def test_spot04_room_materials_reference_distinct_textures(self) -> None:
        model = ZsiFile.from_path(SPOT04_ROOM0_ZSI).embedded_cmbs()[0].model
        first_textures = [material.texture_indices[0] for material in model.materials]

        self.assertEqual(len(model.textures), 21)
        self.assertEqual(first_textures, list(range(21)))
        self.assertEqual(model.materials[10].texture_indices, (10, 10, -1))
        self.assertEqual(model.materials[11].texture_indices, (11, 11, -1))
        self.assertEqual(model.materials[10].texture_mappers_used, 2)
        self.assertEqual(model.materials[10].texture_coords_used, 2)
        self.assertEqual(model.materials[10].texture_mappers[1].index, 10)
        self.assertEqual(
            secondary_material_texture(model, model.materials[10], 10).name,
            "s04_kawa_02",
        )
        self.assertEqual(model.materials[10].texture_mappers[0].wrap_s, 0x2901)
        self.assertAlmostEqual(model.materials[10].texture_coords[0].translation.y, -0.2617994)
        self.assertAlmostEqual(model.materials[10].texture_coords[1].scale.x, 2.0)
        self.assertTrue(model.materials[1].alpha_test)
        self.assertEqual(model.materials[1].alpha_reference, 127)
        self.assertEqual(model.materials[1].alpha_function, 0x0204)
        self.assertEqual(model.materials[10].blend_mode, 1)
        self.assertAlmostEqual(model.materials[10].blend_color_alpha, 0.65)

    def test_spot04_alpha_and_blend_material_export(self) -> None:
        model = ZsiFile.from_path(SPOT04_ROOM0_ZSI).embedded_cmbs()[0].model

        alpha_material = model.materials[1]
        alpha_texture_index = first_valid_texture(alpha_material)
        self.assertIsNotNone(alpha_texture_index)
        alpha_xml = material_xml(
            alpha_material,
            model.textures[alpha_texture_index or 0],
            "scenes/overworld/spot04/oot3d/spot04_room_0/s04_ha_01bx.rgba16",
        )
        self.assertIn('<SetBlendColor R="0" G="0" B="0" A="127"/>', alpha_xml)
        self.assertIn('<SetAlphaCompare Mode="1"/>', alpha_xml)
        self.assertIn('G_RM_AA_ZB_TEX_EDGE="1"', alpha_xml)
        self.assertIn('G_RM_AA_ZB_TEX_EDGE2="1"', alpha_xml)

        blend_material = model.materials[10]
        blend_texture_index = first_valid_texture(blend_material)
        self.assertIsNotNone(blend_texture_index)
        blend_xml = material_xml(
            blend_material,
            model.textures[blend_texture_index or 0],
            "scenes/overworld/spot04/oot3d/spot04_room_0/s04_mizu.rgba16",
        )
        self.assertIn('<SetAlphaCompare Mode="0"/>', blend_xml)
        self.assertIn('G_RM_AA_ZB_XLU_SURF="1"', blend_xml)
        self.assertIn('G_RM_AA_ZB_XLU_SURF2="1"', blend_xml)
        self.assertIn('<SetPrimColor M="0" L="0" R="255" G="255" B="255" A="166"/>', blend_xml)
        self.assertIn('Aa1="G_ACMUX_COMBINED"', blend_xml)
        self.assertIn('Ac1="G_ACMUX_PRIMITIVE"', blend_xml)

    @unittest.skipUnless(
        HIRAL_DEMO_SCENE_ZSI.exists(),
        "local OOT3D hiral_demo scene fixture is not present",
    )
    def test_parse_planar_hiral_demo_collision_keeps_full_native_tables(self) -> None:
        candidate = ZsiFile.from_path(HIRAL_DEMO_SCENE_ZSI).collision_header_candidates()[0]

        self.assertEqual(candidate.offset, 0x460)
        self.assertEqual(candidate.bounds_min, (-8000, 0, -8000))
        self.assertEqual(candidate.bounds_max, (8000, 0, 8000))
        self.assertEqual(candidate.vertex_count, 4)
        self.assertEqual(candidate.raw_polygon_count, 2)
        self.assertEqual(candidate.effective_polygon_count, 2)
        self.assertEqual(candidate.surface_type_count, 1)
        self.assertEqual(candidate.bgcam_count, 1)
        self.assertEqual(candidate.water_box_count, 0)
        self.assertEqual(candidate.vertex_offset, 0x400)
        self.assertEqual(candidate.effective_vertex_offset, 0x410)
        self.assertEqual(candidate.polygon_offset, 0x418)
        self.assertEqual(candidate.effective_polygon_offset, 0x428)
        self.assertEqual(candidate.surface_type_offset, 0x440)
        self.assertEqual(candidate.effective_surface_type_offset, 0x450)
        self.assertEqual(candidate.bgcam_offset, 0x448)
        self.assertEqual(candidate.effective_bgcam_offset, 0x458)
        self.assertEqual(candidate.vertices[0].summary(), {"x": -8000, "y": 0, "z": -8000})
        self.assertEqual(candidate.vertices[-1].summary(), {"x": 8000, "y": 0, "z": -8000})
        self.assertEqual(candidate.polygons[0].vertex_indices, (0, 1, 2))
        self.assertEqual(candidate.polygons[1].vertex_indices, (0, 2, 3))
        self.assertEqual(candidate.polygon_tail_hex, "")

    @unittest.skipUnless(
        HIRAL_DEMO_SCENE_ZSI.exists() and BASE_O2R.exists(),
        "local OOT3D hiral_demo scene and Shipwright oot.o2r fixtures are not present",
    )
    def test_hiral_demo_planar_collision_matches_original_shipwright_reference(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            result = export_zsi_scene_collision(
                HIRAL_DEMO_SCENE_ZSI,
                Path(tmp),
                "scenes/shared/hiral_demo_scene/hiral_demo_sceneCollisionHeader_003548",
                reference_o2r=BASE_O2R,
                require_reference_match=True,
            )

        comparison = result.stats["reference_comparison"]
        self.assertTrue(comparison["accepted"])
        self.assertEqual(comparison["failed_checks"], [])
        self.assertEqual(comparison["converted"]["bounds_min"], (-8000, 0, -8000))
        self.assertEqual(comparison["converted"]["bounds_max"], (8000, 0, 8000))
        self.assertEqual(comparison["converted"]["polygon_count"], 2)
        self.assertTrue(result.stats["native_zsi_acceptance"]["accepted"])

    def test_discover_spot04_scene_rooms(self) -> None:
        rooms = discover_scene_room_zsis(SCENE_DIR, "spot04")
        self.assertEqual([room for room, _path in rooms], [0, 1, 2])

    def test_audit_spot04_scene_material_texture_selection(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            audit_path = Path(tmp) / "spot04_material_audit.json"
            audit = audit_scene_materials(SCENE_DIR, "spot04", audit_path)

            self.assertTrue(audit_path.exists())

        self.assertEqual(audit["scene"], "spot04")
        self.assertEqual(audit["room_count"], 3)
        self.assertEqual(audit["cmb_count"], 3)
        self.assertGreater(audit["mesh_count"], 0)
        self.assertGreater(audit["material_count"], 0)
        self.assertGreater(audit["multi_texture_material_count"], 0)
        self.assertGreater(audit["alpha_test_material_count"], 0)
        self.assertGreater(audit["blended_material_count"], 0)
        self.assertEqual(audit["texture_stage_risk_material_count"], 0)
        self.assertEqual(audit["texture_stage_risk_mesh_count"], 0)
        self.assertEqual(
            audit["texture_stage_risk_counts"]["multi_texture_stage_selection_unverified"],
            0,
        )
        self.assertEqual(audit["texture_stage_risk_counts"]["secondary_stage_not_distinct"], 0)
        self.assertEqual(audit["texture_stage_risk_counts"]["secondary_stage_approximated"], 0)
        self.assertEqual(audit["texture_stage_risk_counts"]["repeated_active_texture_index"], 0)
        self.assertEqual(audit["texture_stage_risk_counts"]["raw_stage_count_exceeds_mapper_count"], 0)
        self.assertEqual(audit["texture_stage_risk_counts"]["raw_stage_export_gap"], 0)
        self.assertEqual(audit["raw_texture_stage_selector_counts"]["stage_count_1"], 37)
        self.assertEqual(audit["raw_texture_stage_selector_counts"]["stage_count_2"], 2)
        self.assertEqual(audit["raw_texture_stage_selector_counts"]["stage_count_3"], 2)
        self.assertEqual(audit["raw_texture_stage_mapper_mismatch_count"], 2)
        self.assertEqual(audit["raw_texture_stage_baked_extra_material_count"], 2)
        self.assertEqual(audit["raw_texture_stage_baked_extra_total"], 2)
        self.assertEqual(audit["raw_texture_stage_export_gap_material_count"], 0)
        self.assertEqual(audit["raw_texture_stage_export_gap_total"], 0)
        self.assertEqual(
            audit["raw_texture_stage_export_classification_counts"],
            {
                "covered_by_baked_extra_texture_stage": 2,
                "covered_by_current_export": 39,
            },
        )
        self.assertEqual(
            audit["raw_texture_stage_export_gap_classification_counts"],
            {},
        )
        self.assertEqual(
            audit["raw_texture_stage_alignment_case_counts"],
            {
                "covered_by_baked_extra_texture_stage": 2,
                "covered_by_current_export": 39,
            },
        )
        self.assertEqual(
            audit["raw_texture_stage_export_gap_alignment_case_counts"],
            {},
        )
        self.assertEqual(
            audit["raw_texture_stage_resolution_case_counts"],
            {
                "covered_by_baked_extra_texture_stage": 2,
                "covered_by_current_export": 39,
            },
        )
        self.assertEqual(
            audit["raw_texture_stage_export_gap_resolution_case_counts"],
            {},
        )
        self.assertEqual(
            audit["raw_texture_stage_export_order_case_counts"],
            {
                "raw_prefix_contains_oob_slot": 4,
                "raw_prefix_matches_exported_order": 25,
                "selected_primary_absent_from_raw_order": 12,
            },
        )
        self.assertEqual(
            audit["raw_texture_stage_export_gap_order_case_counts"],
            {},
        )
        self.assertEqual(
            audit["raw_texture_stage_f3d_limit_case_counts"],
            {},
        )
        self.assertEqual(
            audit["raw_texture_stage_export_gap_slot_bounds_case_counts"],
            {},
        )
        self.assertEqual(
            audit["raw_texture_stage_export_gap_slot_sequence_case_counts"],
            {},
        )
        self.assertEqual(
            audit["raw_texture_stage_export_gap_unexported_stage_position_counts"],
            {},
        )
        self.assertEqual(
            audit["raw_texture_stage_unexported_stage_position_counts"],
            {"stage_position_0": 12, "stage_position_1": 1},
        )
        self.assertEqual(
            audit["raw_texture_stage_non_gap_unexported_stage_position_counts"],
            {"stage_position_0": 12, "stage_position_1": 1},
        )
        self.assertEqual(
            audit["raw_texture_stage_unexported_material_ref_stage_position_counts"],
            {"stage_position_0": 8, "stage_position_1": 1},
        )
        self.assertEqual(
            audit["raw_texture_stage_non_gap_unexported_material_ref_stage_position_counts"],
            {"stage_position_0": 8, "stage_position_1": 1},
        )
        self.assertEqual(
            audit["raw_texture_stage_unexported_material_index_ref_stage_position_counts"],
            {"stage_position_0": 10, "stage_position_1": 1},
        )
        self.assertEqual(
            audit["raw_texture_stage_export_gap_unexported_material_index_ref_stage_position_counts"],
            {},
        )
        self.assertEqual(
            audit["raw_texture_stage_non_gap_unexported_material_index_ref_stage_position_counts"],
            {"stage_position_0": 10, "stage_position_1": 1},
        )
        self.assertEqual(
            audit["raw_texture_stage_export_blocker_counts"],
            {},
        )
        self.assertEqual(audit["primary_texture_coord_transform_count"], 2)
        self.assertEqual(audit["secondary_texture_coord_transform_count"], 2)
        self.assertEqual(audit["issue_counts"]["repeated_active_texture_index"], 0)
        self.assertEqual(audit["issue_counts"]["rotated_texture_coord"], 0)
        self.assertEqual(audit["issue_counts"]["secondary_texture_coord_transform_not_exported"], 0)

        room0 = audit["records"][0]
        self.assertEqual(room0["room"], 0)
        self.assertEqual(room0["texture_count"], 21)
        material10 = room0["materials"][10]
        self.assertEqual(material10["texture_indices"], (10, 10, -1))
        self.assertEqual(material10["active_texture_slot_count"], 2)
        self.assertEqual(material10["selected_primary_texture"]["name"], "s04_kawa_01")
        self.assertEqual(
            material10["texture_stage_confidence"],
            "raw_selector_repeated_primary_raw_secondary",
        )
        self.assertEqual(
            material10["texture_stage_selection_strategy"],
            "raw_selector_repeated_primary_raw_secondary",
        )
        self.assertEqual(material10["texture_stage_risk_tags"], [])
        self.assertEqual(material10["raw_texture_stage_selector"]["stage_count"], 2)
        self.assertEqual(material10["raw_texture_stage_selector"]["stage_indices"], [10, 11])
        self.assertTrue(material10["raw_texture_stage_selector"]["matches_texture_mappers_used"])
        self.assertEqual(material10["exported_texture_stage_count"], 2)
        self.assertEqual(material10["raw_texture_stage_export_gap"], 0)
        self.assertEqual(
            material10["raw_texture_stage_export_classification"]["status"],
            "covered_by_current_export",
        )
        self.assertEqual(material10["raw_material_analysis"]["size"], 0x15C)
        self.assertEqual(len(material10["raw_material_analysis"]["sha256"]), 64)
        self.assertEqual(material10["raw_material_analysis"]["texture_stage_candidate_range"], [0xA0, 0x130])
        self.assertGreater(
            len(material10["raw_material_analysis"]["texture_stage_candidate_nonzero_words"]),
            0,
        )
        self.assertTrue(material10["selected_primary_texture_coord_transformed"])
        self.assertAlmostEqual(
            material10["selected_primary_texture_coord"]["translation"][1],
            -0.2617994,
        )
        self.assertEqual(material10["selected_secondary_slot"], 1)
        self.assertEqual(material10["selected_secondary_texture"]["name"], "s04_kawa_02")
        self.assertEqual(material10["selected_secondary_texture_coord_export_status"], "baked_texture")
        self.assertTrue(material10["selected_secondary_texture_export_path"].endswith("s04_kawa_02_mat_10_uv1.rgba16"))
        self.assertTrue(material10["selected_secondary_texture_coord_transformed"])
        mesh10 = next(mesh for mesh in room0["meshes"] if mesh["material_index"] == 10)
        self.assertNotIn("repeated_active_texture_index", mesh10["issues"])
        self.assertNotIn("secondary_texture_coord_transform_not_exported", mesh10["issues"])
        self.assertEqual(mesh10["issues"], [])
        self.assertEqual(mesh10["texture_stage_risk_tags"], [])

        material11 = room0["materials"][11]
        self.assertEqual(material11["texture_indices"], (11, 11, -1))
        self.assertEqual(material11["active_texture_slot_count"], 2)
        self.assertEqual(material11["raw_texture_stage_selector"]["stage_indices"], [12, 13])
        self.assertEqual(material11["selected_primary_texture"]["name"], "s04_kawa_02")
        self.assertEqual(material11["selected_secondary_texture"]["name"], "s04_kawa_02")
        self.assertEqual(material11["selected_primary_slot"], 0)
        self.assertEqual(material11["selected_secondary_slot"], 1)
        self.assertEqual(
            material11["texture_stage_selection_strategy"],
            "same_texture_transformed_secondary",
        )
        self.assertEqual(material11["texture_stage_confidence"], "same_texture_transformed_secondary")
        self.assertEqual(material11["raw_texture_stage_export_gap"], 0)
        self.assertEqual(
            material11["raw_texture_stage_export_order_case"],
            "selected_primary_absent_from_raw_order",
        )
        self.assertTrue(material11["selected_secondary_texture_export_path"].endswith("s04_kawa_02_mat_11_uv1.rgba16"))
        self.assertEqual(material11["texture_stage_risk_tags"], [])
        self.assertNotIn("repeated_active_texture_index", material_issues(material11))

        for material_index, raw_stage_index, texture_name, raw_texture_name in (
            (12, 14, "s04_kawa_03", "s04_kusa_03"),
            (13, 15, "s04_ki_02", "s04_kusa_04"),
            (14, 16, "s04_kusa_03", "s04_kusa_05"),
            (15, 17, "s04_kusa_04", "s04_road_01r"),
            (16, 18, "s04_kusa_05", "s04_saku_01"),
            (17, 19, "s04_road_01r", "s04_soko_01x"),
            (18, 20, "s04_saku_01", "s04_yuka_01r"),
        ):
            material_record = room0["materials"][material_index]
            self.assertEqual(material_record["raw_texture_stage_export_gap"], 0)
            self.assertEqual(
                material_record["raw_texture_stage_selector"]["stage_indices"],
                [raw_stage_index],
            )
            self.assertEqual(material_record["selected_primary_texture"]["name"], texture_name)
            self.assertEqual(
                material_record["texture_stage_selection_strategy"],
                "first_valid_mapper",
            )
            self.assertEqual(
                material_record["texture_stage_confidence"],
                "single_texture_mapper",
            )
            self.assertEqual(material_record["texture_stage_risk_tags"], [])
            self.assertEqual(
                material_record["raw_texture_stage_export_order_case"],
                "selected_primary_absent_from_raw_order",
            )
            self.assertEqual(
                material_record["raw_texture_stage_unexported_valid_textures"][0]["texture"]["name"],
                raw_texture_name,
            )

        room1 = audit["records"][1]
        for material_index, raw_stage_index, texture_name in (
            (4, 8, "s04_ha_01bx"),
            (5, 9, "s04_ha_03"),
            (6, 10, "s04_ki_02"),
            (7, 11, "s04_kusa_03"),
        ):
            material_record = room1["materials"][material_index]
            self.assertEqual(
                material_record["raw_texture_stage_selector"]["stage_indices"],
                [raw_stage_index],
            )
            self.assertEqual(material_record["selected_primary_texture"]["name"], texture_name)
            self.assertEqual(
                material_record["texture_stage_selection_strategy"],
                "first_valid_mapper",
            )
            self.assertEqual(
                material_record["texture_stage_confidence"],
                "single_texture_mapper",
            )
            self.assertEqual(
                material_record["raw_texture_stage_export_order_case"],
                "selected_primary_absent_from_raw_order",
            )
            self.assertGreater(len(material_record["raw_texture_stage_unexported_valid_textures"]), 0)

        material2 = room1["materials"][2]
        self.assertEqual(material2["raw_texture_stage_selector"]["stage_count"], 3)
        self.assertEqual(material2["raw_texture_stage_selector"]["stage_indices"], [2, 3, 4])
        self.assertEqual(
            material2["raw_texture_stage_slot_bounds"]["case"],
            "stage3__valid_p0_p1_p2",
        )
        self.assertEqual(
            material2["raw_texture_stage_slot_bounds"]["sequence_case"],
            "contiguous_all_valid",
        )
        self.assertFalse(material2["raw_texture_stage_selector"]["matches_texture_mappers_used"])
        self.assertEqual(material2["exported_texture_stage_count"], 2)
        self.assertEqual(
            material2["texture_stage_selection_strategy"],
            "raw_selector_prefix_primary_secondary_baked_extra",
        )
        self.assertEqual(
            material2["texture_stage_confidence"],
            "raw_selector_prefix_primary_secondary_baked_extra",
        )
        self.assertTrue(
            material2["selected_secondary_texture_export_path"].endswith(
                "s04_deku_01b_mat_2_stage2_s04_deku_02.rgba16"
            )
        )
        self.assertEqual(material2["raw_texture_stage_export_gap"], 0)
        self.assertEqual(material2["raw_texture_stage_baked_extra_count"], 1)
        self.assertEqual(material2["raw_texture_stage_candidate_summary"]["valid_texture_candidate_count"], 3)
        self.assertEqual(material2["raw_texture_stage_candidate_summary"]["exported_candidate_count"], 3)
        self.assertEqual(material2["raw_texture_stage_candidate_summary"]["baked_extra_candidate_count"], 1)
        self.assertTrue(material2["raw_texture_stage_candidate_summary"]["selected_primary_in_raw"])
        self.assertTrue(material2["raw_texture_stage_candidate_summary"]["selected_secondary_in_raw"])
        self.assertIsNone(material2["raw_texture_stage_candidate_summary"]["first_unexported_valid_texture"])
        self.assertEqual(material2["raw_texture_stage_unexported_valid_textures"], [])
        self.assertEqual(len(material2["raw_texture_stage_baked_extra_textures"]), 1)
        self.assertEqual(
            material2["raw_texture_stage_baked_extra_textures"][0]["stage_position"],
            2,
        )
        self.assertEqual(
            material2["raw_texture_stage_baked_extra_textures"][0]["raw_stage_index"],
            4,
        )
        self.assertEqual(
            material2["raw_texture_stage_baked_extra_textures"][0]["texture"]["name"],
            "s04_deku_02",
        )
        self.assertEqual(material2["texture_stage_risk_tags"], [])
        self.assertEqual(
            material2["raw_texture_stage_export_classification"]["status"],
            "covered_by_baked_extra_texture_stage",
        )
        self.assertEqual(
            material2["raw_texture_stage_alignment_case"],
            "covered_by_baked_extra_texture_stage",
        )
        self.assertEqual(
            material2["raw_texture_stage_resolution_case"],
            "covered_by_baked_extra_texture_stage",
        )
        self.assertEqual(
            material2["raw_texture_stage_export_order_case"],
            "raw_prefix_matches_exported_order",
        )
        self.assertEqual(
            material2["raw_texture_stage_f3d_limit_case"],
            "not_f3d_limit",
        )
        self.assertEqual(
            material2["raw_texture_stage_export_classification"]["blockers"],
            [],
        )

        material3 = room1["materials"][3]
        self.assertEqual(material3["raw_texture_stage_selector"]["stage_indices"], [5, 6, 7])
        self.assertEqual(material3["selected_primary_slot"], 0)
        self.assertEqual(material3["selected_secondary_slot"], 1)
        self.assertEqual(material3["selected_primary_texture"]["name"], "s04_deku_02")
        self.assertEqual(material3["selected_secondary_texture"]["name"], "s04_deku_02b")
        self.assertEqual(
            material3["texture_stage_selection_strategy"],
            "first_valid_mapper_plus_distinct_secondary",
        )
        self.assertEqual(
            material3["texture_stage_confidence"],
            "unverified_multi_texture_mapper",
        )
        self.assertIsNone(material3["selected_secondary_texture_export_path"])
        self.assertEqual(
            material3["raw_texture_stage_export_order_case"],
            "raw_prefix_starts_with_exported_secondary",
        )
        self.assertFalse(material3["raw_texture_stage_candidate_summary"]["selected_primary_in_raw"])
        self.assertTrue(material3["raw_texture_stage_candidate_summary"]["selected_secondary_in_raw"])
        self.assertEqual(material3["raw_texture_stage_candidate_summary"]["exported_candidate_count"], 1)
        self.assertEqual(material3["raw_texture_stage_candidate_summary"]["baked_extra_candidate_count"], 0)
        self.assertEqual(
            material3["raw_texture_stage_candidate_summary"]["first_unexported_valid_texture"]["name"],
            "s04_ha_01bx",
        )
        self.assertEqual(material3["raw_texture_stage_export_gap"], 1)
        self.assertEqual(material3["raw_texture_stage_baked_extra_count"], 0)
        self.assertEqual(
            [entry["texture"]["name"] for entry in material3["raw_texture_stage_unexported_valid_textures"]],
            ["s04_ha_01bx", "s04_ha_03"],
        )
        self.assertEqual(
            material3["texture_stage_risk_tags"],
            [
                "multi_texture_stage_selection_unverified",
                "secondary_stage_approximated",
                "raw_stage_count_exceeds_mapper_count",
                "raw_stage_export_gap",
            ],
        )
        self.assertEqual(
            material3["raw_texture_stage_export_classification"]["status"],
            "blocked_f3d_texture_stage_limit",
        )
        self.assertEqual(
            material3["raw_texture_stage_alignment_case"],
            "blocked_f3d_texture_stage_limit",
        )
        self.assertEqual(
            material3["raw_texture_stage_f3d_limit_case"],
            "stage3_valid3_exported1_unexported2_raw_prefix_starts_with_exported_secondary_extra_p1_p2",
        )
        self.assertEqual(
            material3["raw_texture_stage_export_classification"]["blockers"],
            [
                "raw_stage_count_exceeds_f3d_two_texture_limit",
                "selected_primary_not_in_raw_selector",
                "no_free_f3d_texture_stage",
            ],
        )

        risk_keys = {
            (record["asset_id"], record["mesh_index"], record["material_index"])
            for record in audit["texture_stage_risk_records"]
        }
        self.assertEqual(
            risk_keys,
            {("spot04_room_1", 6, 3)},
        )
        for record in audit["texture_stage_risk_records"]:
            self.assertEqual(record["raw_material_analysis"]["size"], 0x15C)
            self.assertEqual(len(record["raw_material_analysis"]["sha256"]), 64)
            self.assertGreater(record["raw_material_analysis"]["nonzero_word_count"], 0)

    @unittest.skipUnless(SPOT04_ROOM1_ZSI.exists(), "local OOT3D spot04 room 1 fixture is not present")
    def test_export_spot04_room1_bakes_verified_third_raw_texture_stage(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scene_dir = root / "scene"
            scene_dir.mkdir()
            (scene_dir / "spot04_1_info.zsi").write_bytes(SPOT04_ROOM1_ZSI.read_bytes())

            manifest_path = convert_scene(
                SimpleNamespace(
                    scene_dir=scene_dir,
                    scene="spot04",
                    output=root / "converted_scene",
                    resource_prefix=None,
                    shipwright_root=None,
                    no_textures=False,
                    generate_collision=False,
                    no_collision=True,
                    collision_path=None,
                    base_o2r=None,
                    allow_nonstatic=False,
                )
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

            texture_resources = {
                Path(resource["file"]).name: resource
                for record in manifest["records"]
                for resource in record["resources"]
                if resource["kind"] == "Texture"
            }
            filename = "s04_deku_01b_mat_2_stage2_s04_deku_02.rgba16"
            self.assertIn(filename, texture_resources)
            baked_file = Path(texture_resources[filename]["file"])
            self.assertTrue(baked_file.exists())
            self.assertEqual(baked_file.stat().st_size, 0x40 + 0x1C + 256 * 256 * 2)
            material3_filename = "s04_ha_01bx_mat_3_stage2_s04_ha_03.rgba16"
            self.assertNotIn(material3_filename, texture_resources)

            material2 = (
                root / "converted_scene" / "spot04_room_1" / "gOot3dSpot04Room1_mat_2"
            ).read_text(encoding="utf-8")
            material3 = (
                root / "converted_scene" / "spot04_room_1" / "gOot3dSpot04Room1_mat_3"
            ).read_text(encoding="utf-8")
            self.assertIn("s04_deku_01b_mat_2_stage2_s04_deku_02.rgba16", material2)
            self.assertIn("s04_deku_02.rgba16", material3)
            self.assertIn("s04_deku_02b.rgba16", material3)
            self.assertNotIn("s04_ha_01bx_mat_3_stage2_s04_ha_03.rgba16", material3)

    @unittest.skipUnless(SPOT04_SCENE_ZSI.exists(), "local OOT3D spot04 scene fixture is not present")
    def test_parse_spot04_scene_collision_audit(self) -> None:
        zsi = ZsiFile.from_path(SPOT04_SCENE_ZSI)
        setups = zsi.scene_setups()

        self.assertEqual(len(setups), 13)
        self.assertEqual(setups[0].offset, 0x18)
        self.assertEqual(setups[0].commands[0].command_id, 0x15)
        self.assertEqual(setups[0].commands[-1].command_id, 0x14)

        collision_commands = [setup.first_command(0x03) for setup in setups]
        self.assertTrue(all(command is not None for command in collision_commands))
        self.assertEqual({command.argument for command in collision_commands if command}, {0x16D0C})

        candidates = zsi.collision_header_candidates()
        self.assertEqual(len(candidates), 1)
        candidate = candidates[0]
        self.assertEqual(candidate.command_argument, 0x16D0C)
        self.assertEqual(candidate.offset, 0x16D1C)
        self.assertEqual(candidate.bounds_min, (-2301, -157, -3012))
        self.assertEqual(candidate.bounds_max, (5872, 1179, 2327))
        self.assertEqual(candidate.vertex_count, 2315)
        self.assertEqual(candidate.raw_polygon_count, 3858)
        self.assertEqual(candidate.effective_polygon_count, 3858)
        self.assertEqual(candidate.surface_type_count, 47)
        self.assertEqual(candidate.bgcam_count, 15)
        self.assertEqual(candidate.water_box_count, 1)
        self.assertEqual(candidate.vertex_offset, 0x688)
        self.assertEqual(candidate.effective_vertex_offset, 0x698)
        self.assertEqual(candidate.polygon_offset, 0x3CCC)
        self.assertEqual(candidate.effective_polygon_offset, 0x3CDC)
        self.assertEqual(candidate.polygon_tail_hex, "")
        self.assertEqual(candidate.surface_type_offset, 0x16A34)
        self.assertEqual(candidate.effective_surface_type_offset, 0x16A44)
        self.assertEqual(candidate.bgcam_offset, 0x16BAC)
        self.assertEqual(candidate.effective_bgcam_offset, 0x16BBC)
        self.assertEqual(candidate.camera_position_offset, 0x16C34)
        self.assertEqual(candidate.camera_pointer_adjustment, 0x10)
        self.assertEqual(candidate.water_boxes_offset, 0x16CFC)
        self.assertEqual(candidate.vertices[0].summary(), {"x": -280, "y": 60, "z": -816})
        self.assertEqual(candidate.polygons[0].type, 0)
        self.assertEqual(candidate.polygons[0].vertex_indices, (1703, 1725, 1702))
        self.assertEqual(candidate.polygons[0].marker, 0x55DA)
        self.assertEqual(candidate.polygons[0].normal, (-26827, 4479, -18273))
        self.assertAlmostEqual(candidate.polygons[0].dist, 1179.157470703125)
        self.assertEqual(candidate.surface_types[0].summary(), {"data1": 0x090A08CC, "data2": 0x030D55DA})
        self.assertEqual(candidate.surface_types[-1].summary(), {"data1": 0x0000000B, "data2": 0x0000B7CA})
        self.assertEqual(candidate.effective_surface_types[0].summary(), {"data1": 0x0000000B, "data2": 0x00000FCA})
        self.assertEqual(candidate.effective_surface_types[-1].summary(), {"data1": 0x0020000B, "data2": 0x00000FC0})
        self.assertEqual(candidate.bg_cam_info[2].setting, 32)
        self.assertEqual(candidate.bg_cam_info[2].count, 3)
        self.assertEqual(candidate.bg_cam_info[2].data_offset, 0x16C24)
        self.assertEqual(candidate.effective_bg_cam_info[0].summary(), {"setting": 32, "count": 3, "data_offset": 0x16C24})
        self.assertEqual(candidate.effective_bg_cam_info[-1].summary(), {"setting": 3, "count": 0, "data_offset": 0})
        self.assertEqual(candidate.camera_position_indices, (0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 33, 0, 0, 0, 0))
        self.assertEqual(len(candidate.camera_position_vectors), 36)
        self.assertEqual(candidate.camera_position_vectors[0], (-1127, 282, 565))
        self.assertEqual(candidate.camera_position_vectors[-1], (60, 10, -1))
        self.assertEqual(
            candidate.effective_water_boxes,
            (ZsiWaterBox(73, -12, -588, 1400, 1040, 269),),
        )
        self.assertEqual(
            candidate.prefixed_water_boxes,
            (ZsiWaterBox(73, -12, -588, 1400, 1040, 269),),
        )

    @unittest.skipUnless(
        SPOT04_SCENE_ZSI.exists() and SPOT04_ROOM0_ZSI.exists(),
        "local OOT3D spot04 scene fixtures are not present",
    )
    def test_zsi_scene_metadata_audit_tracks_setup_commands_and_collision(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scene_dir = root / "scene"
            scene_dir.mkdir()
            (scene_dir / "spot04_info.zsi").write_bytes(SPOT04_SCENE_ZSI.read_bytes())
            (scene_dir / "spot04_0_info.zsi").write_bytes(SPOT04_ROOM0_ZSI.read_bytes())

            output = root / "zsi_scene_metadata_audit.json"
            audit = audit_zsi_scene_metadata(scene_dir, output, sample_limit=4)

            self.assertTrue(output.exists())

        self.assertEqual(audit["format"], "oot3d_zsi_scene_metadata_audit_v1")
        self.assertEqual(audit["zsi_file_count"], 2)
        self.assertEqual(audit["role_counts"], {"room": 1, "scene": 1})
        self.assertEqual(audit["scene_stem_count"], 1)
        self.assertEqual(audit["room_stem_count"], 1)
        self.assertEqual(audit["room_count_total"], 1)
        self.assertEqual(audit["room_counts_by_scene"], {"spot04": 1})
        self.assertEqual(audit["setup_file_count"], 2)
        self.assertEqual(audit["setup_total"], 26)
        self.assertNotIn("0", audit["setup_count_counts"])
        self.assertEqual(audit["setup_count_counts"]["13"], 2)
        self.assertEqual(audit["command_id_counts"]["0x15"], 13)
        self.assertEqual(audit["command_id_counts"]["0x16"], 13)
        self.assertEqual(audit["command_id_counts"]["0x01"], 7)
        self.assertEqual(audit["command_id_counts"]["0x03"], 13)
        self.assertEqual(audit["command_id_counts"]["0x14"], 26)
        self.assertEqual(audit["embedded_cmb_count_counts"], {"0": 1, "1": 1})
        self.assertEqual(audit["collision_file_count"], 1)
        self.assertEqual(audit["collision_candidate_total"], 1)
        self.assertEqual(audit["collision_candidate_count_counts"]["0"], 1)
        self.assertEqual(audit["collision_candidate_count_counts"]["1"], 1)
        self.assertEqual(audit["camera_position_vector_total"], 36)
        self.assertEqual(audit["water_box_total"], 1)
        self.assertEqual(audit["collision_vertex_total"], 2315)
        self.assertEqual(audit["collision_effective_polygon_total"], 3858)
        self.assertEqual(audit["parse_error_count"], 0)
        self.assertLessEqual(len(audit["sample_records"]), 4)
        self.assertEqual(len(audit["records"]), 2)

    @unittest.skipUnless(
        SPOT04_SCENE_ZSI.exists() and BASE_O2R.exists(),
        "local OOT3D spot04 scene and Shipwright oot.o2r fixtures are not present",
    )
    def test_zsi_collision_gate_audit_accepts_spot04_without_runtime_enablement(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scene_dir = root / "scene"
            scene_dir.mkdir()
            (scene_dir / "spot04_info.zsi").write_bytes(SPOT04_SCENE_ZSI.read_bytes())
            output = root / "collision_gate_audit.json"

            audit = audit_zsi_collision_gates(
                scene_dir,
                BASE_O2R,
                output,
                scenes=("spot04",),
                sample_limit=2,
            )

            self.assertTrue(output.exists())

        self.assertEqual(audit["format"], "oot3d_zsi_collision_gate_audit_v1")
        self.assertEqual(audit["scene_file_count"], 1)
        self.assertEqual(audit["status_counts"], {"accepted": 1})
        self.assertEqual(audit["failed_check_counts"], {})
        self.assertEqual(audit["accepted_count"], 1)
        self.assertEqual(audit["rejected_count"], 0)
        self.assertEqual(audit["fallback_count"], 0)
        self.assertEqual(audit["scene_status_counts"], {"accepted": 1})
        self.assertEqual(audit["accepted_scene_count"], 1)
        self.assertEqual(audit["blocked_scene_count"], 0)
        self.assertEqual(audit["fallback_scene_count"], 0)
        self.assertEqual(audit["scene_records"][0]["scene"], "spot04")
        self.assertEqual(audit["scene_records"][0]["status"], "accepted")
        self.assertEqual(audit["scene_records"][0]["activation_candidate_path"], "spot04_info.zsi")
        record = audit["records"][0]
        self.assertEqual(record["scene"], "spot04")
        self.assertEqual(record["status"], "accepted")
        self.assertEqual(record["reference_resource_path_source"], "reference_o2r_discovery")
        self.assertEqual(record["failed_checks"], [])
        self.assertEqual(record["vertex_count"], 2315)
        self.assertEqual(record["polygon_count"], 3858)
        self.assertTrue(record["native_zsi_acceptance"]["accepted"])
        self.assertTrue(record["polygon_category_ratio_accepted"])
        self.assertEqual(
            record["reference_exit_index_usage"],
            {2: 2, 3: 1, 4: 2, 5: 2, 6: 2, 7: 1, 9: 2, 10: 2, 11: 2},
        )
        self.assertEqual(record["converted_exit_index_usage"], record["reference_exit_index_usage"])
        self.assertTrue(record["source_exit_list_accepted"])

    @unittest.skipUnless(
        SPOT04_SCENE_ZSI.exists() and SPOT04_ROOM0_ZSI.exists() and BASE_O2R.exists(),
        "local OOT3D spot04 scene/room and Shipwright oot.o2r fixtures are not present",
    )
    def test_zsi_collision_gate_audit_can_attach_visual_mesh_diagnostics(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scene_dir = root / "scene"
            scene_dir.mkdir()
            (scene_dir / "spot04_info.zsi").write_bytes(SPOT04_SCENE_ZSI.read_bytes())
            output = root / "collision_gate_audit.json"

            audit = audit_zsi_collision_gates(
                scene_dir,
                BASE_O2R,
                output,
                scenes=("spot04",),
                sample_limit=2,
                visual_room_dir=SCENE_DIR,
                visual_floor_probe_count=64,
            )

        self.assertEqual(audit["status_counts"], {"accepted": 1})
        self.assertEqual(
            audit["visual_mesh_audit_status_counts"],
            {"decoded_visual_mesh_floor_audit": 1},
        )
        self.assertTrue(audit["visual_mesh_diagnosis_counts"])
        record = audit["records"][0]
        self.assertEqual(record["status"], "accepted")
        self.assertGreaterEqual(record["visual_room_model_count"], 1)
        self.assertIn(0, record["visual_room_indices"])
        visual_audit = record["visual_mesh_floor_audit"]
        self.assertEqual(visual_audit["role"], "diagnostic_only_not_collision_source")
        self.assertEqual(visual_audit["status"], "decoded_visual_mesh_floor_audit")
        self.assertEqual(visual_audit["floor_probe_count_limit"], 64)
        self.assertIn("native_zsi", visual_audit)
        self.assertIn("n64_reference", visual_audit)
        visual_policy = record["visual_diagnostic_policy"]
        self.assertEqual(visual_policy["collision_source"], "oot3d_zsi_native_collision")
        self.assertFalse(visual_policy["visual_mesh_is_collision_source"])
        self.assertEqual(visual_policy["visual_mesh_role"], "diagnostic_oracle_not_collision_input")
        self.assertEqual(visual_policy["reference_failed_checks_still_blocking"], [])
        self.assertEqual(visual_policy["relaxed_checks_from_visual_diagnostics"], [])

    def test_discover_scene_collision_path_finds_nonshared_exact_scene_dir(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            archive_path = Path(tmp) / "oot.o2r"
            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.writestr(
                    "scenes/nonmq/bdan_scene/bdan_sceneCollisionHeader_013054",
                    b"OCOL",
                )
                archive.writestr(
                    "scenes/shared/bdan_boss_scene/bdan_boss_sceneCollisionHeader_000E14",
                    b"OCOL",
                )

            discovered = discover_scene_collision_path_from_o2r(archive_path, "bdan")

        self.assertEqual(
            discovered,
            "scenes/nonmq/bdan_scene/bdan_sceneCollisionHeader_013054",
        )

    def test_cli_zsi_collision_resource_path_discovers_from_reference_o2r(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            archive_path = Path(tmp) / "oot.o2r"
            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.writestr(
                    "scenes/nonmq/bdan_scene/bdan_sceneCollisionHeader_013054",
                    b"OCOL",
                )

            resolved = zsi_collision_resource_path_for_cli(
                Path("bdan_info.zsi"),
                None,
                archive_path,
            )
            resolved_dd = zsi_collision_resource_path_for_cli(
                Path("bdan_dd_info.zsi"),
                None,
                archive_path,
            )

        self.assertEqual(
            resolved,
            "scenes/nonmq/bdan_scene/bdan_sceneCollisionHeader_013054",
        )
        self.assertEqual(resolved_dd, resolved)

    @unittest.skipUnless(BASE_O2R.exists(), "local Shipwright oot.o2r fixture is not present")
    def test_cli_zsi_collision_resource_path_discovers_spot04_from_reference_o2r(self) -> None:
        resolved = zsi_collision_resource_path_for_cli(
            Path("spot04_info.zsi"),
            None,
            BASE_O2R,
        )

        self.assertEqual(
            resolved,
            "scenes/shared/spot04_scene/spot04_sceneCollisionHeader_008918",
        )

    def test_cli_zsi_collision_resource_path_requires_explicit_path_without_reference(self) -> None:
        with self.assertRaisesRegex(ParseError, "--resource-path is required"):
            zsi_collision_resource_path_for_cli(Path("bdan_info.zsi"), None, None)

    @unittest.skipUnless(
        BDAN_SCENE_ZSI.exists() and BASE_O2R.exists(),
        "local OOT3D bdan scene and Shipwright oot.o2r fixtures are not present",
    )
    def test_bdan_surface_type_layout_matches_reference_exit_indices(self) -> None:
        candidate = ZsiFile.from_path(BDAN_SCENE_ZSI).collision_header_candidates()[0]
        self.assertEqual(candidate.effective_surface_type_offset, candidate.surface_type_offset + 0x10)

        with tempfile.TemporaryDirectory() as tmp:
            result = export_zsi_scene_collision(
                BDAN_SCENE_ZSI,
                Path(tmp),
                "scenes/nonmq/bdan_scene/bdan_sceneCollisionHeader_013054",
                reference_o2r=BASE_O2R,
                require_reference_match=True,
            )

        comparison = result.stats["reference_comparison"]
        self.assertTrue(comparison["accepted"])
        self.assertEqual(comparison["failed_checks"], [])
        self.assertEqual(comparison["reference_exit_index_usage"], {1: 3, 2: 2})
        self.assertEqual(comparison["converted_exit_index_usage"], {1: 3, 2: 2})
        self.assertTrue(result.stats["native_zsi_acceptance"]["accepted"])

    @unittest.skipUnless(
        KENJYANOMA_SCENE_ZSI.exists() and BASE_O2R.exists(),
        "local OOT3D kenjyanoma scene and Shipwright oot.o2r fixtures are not present",
    )
    def test_kenjyanoma_vertex_layout_prefers_structural_normal_alignment(self) -> None:
        candidate = ZsiFile.from_path(KENJYANOMA_SCENE_ZSI).collision_header_candidates()[0]
        self.assertEqual(candidate.effective_vertex_offset, candidate.vertex_offset + 0x10)
        self.assertEqual(candidate.vertices[0].summary(), {"x": 140, "y": -4, "z": 242})

        with tempfile.TemporaryDirectory() as tmp:
            result = export_zsi_scene_collision(
                KENJYANOMA_SCENE_ZSI,
                Path(tmp),
                "scenes/shared/kenjyanoma_scene/kenjyanoma_sceneCollisionHeader_00359C",
                reference_o2r=BASE_O2R,
            )

        comparison = result.stats["reference_comparison"]
        self.assertTrue(comparison["accepted"])
        self.assertEqual(comparison["failed_checks"], [])
        self.assertEqual(comparison["max_bounds_delta"], 0)
        self.assertEqual(comparison["floor_hit_rate"], 1.0)
        self.assertEqual(comparison["floor_match_rate"], 1.0)
        self.assertEqual(comparison["converted"]["bounds_min"], (-2387, -2021, -2091))
        self.assertEqual(comparison["converted"]["bounds_max"], (2467, 2690, 2112))

    def test_build_spot04_collision_filters_decorative_meshes(self) -> None:
        model = ZsiFile.from_path(SPOT04_ROOM0_ZSI).embedded_cmbs()[0].model
        collision = build_scene_collision([(0, model)])

        self.assertGreater(len(collision.vertices), 0)
        self.assertGreater(len(collision.polygons), 0)
        self.assertLessEqual(len(collision.vertices), 0x2000)
        self.assertGreater(collision.skipped["material_filtered"], 0)

        metadata = CollisionMetadata(
            camera_data=(CameraData(32, 3, 0),),
            camera_positions=(CameraPositionData((1, 2, 3), (4, 5, 6), (60, 10, -1)),),
            water_boxes=(WaterBox(73, -12, -588, 1400, 1040, 269),),
            surface_types=(SurfaceType(0, 0),),
        )
        xml = collision_header_xml(
            tuple(collision.vertices[:3]),
            tuple(collision.polygons[:1]),
            metadata,
        )
        self.assertIn("<CollisionHeader", xml)
        self.assertIn("<Polygon ", xml)
        self.assertIn('<PolygonType Data1="0" Data2="0"/>', xml)
        self.assertIn('<CameraData SType="32" NumData="3" CameraPosDataSeg="0"/>', xml)
        self.assertIn('<WaterBox XMin="73" Ysurface="-12"', xml)

    def test_surface_type_decoder_matches_shipwright_masks(self) -> None:
        data1 = (
            0x12
            | (0x05 << 8)
            | (0x08 << 13)
            | (0x03 << 18)
            | (0x04 << 21)
            | (0x09 << 26)
            | (1 << 30)
            | (1 << 31)
        )
        data2 = (
            0x0A
            | (0x02 << 4)
            | (0x12 << 6)
            | (0x21 << 11)
            | (1 << 17)
            | (0x05 << 18)
            | (0x2A << 21)
            | 0x08000000
        )

        self.assertEqual(
            decode_surface_type(SurfaceType(data1, data2)),
            {
                "cam_data_index": 0x12,
                "scene_exit_index": 0x05,
                "floor_type": 0x08,
                "surface_unk_18_20": 0x03,
                "wall_property": 0x04,
                "wall_flags": 8,
                "floor_property": 0x09,
                "floor_is_minus_one": True,
                "horse_blocked": True,
                "sfx_material": 0x0A,
                "slope": 0x02,
                "light_setting_index": 0x12,
                "echo": 0x21,
                "hookshot": True,
                "conveyor_speed": 0x05,
                "conveyor_direction": 0x2A,
                "wall_damage": True,
            },
        )

    def test_collision_polygon_flag_decoder_matches_shipwright_masks(self) -> None:
        self.assertEqual(
            decode_collision_polygon_flags(0xE123, 0x2124),
            {
                "vertex_a": 0x123,
                "vertex_b": 0x124,
                "flags_via_raw": 0xE000,
                "flags_vib_raw": 0x2000,
                "ignore_camera": True,
                "ignore_entities": True,
                "ignore_projectiles": True,
                "conveyor": True,
            },
        )

    @unittest.skipUnless(SPOT04_SCENE_ZSI.exists(), "local OOT3D spot04 scene fixture is not present")
    def test_export_spot04_scene_zsi_collision_preserves_surface_types(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = export_zsi_scene_collision(
                SPOT04_SCENE_ZSI,
                out,
                "scenes/shared/spot04_scene/spot04_sceneCollisionHeader_008918",
                resource_format="xml",
            )

            collision_path = Path(result.resource.file)
            xml = collision_path.read_text(encoding="utf-8")

        self.assertEqual(result.resource.kind, "CollisionHeader")
        self.assertEqual(result.stats["resource_format"], "xml")
        self.assertEqual(result.stats["vertex_count"], 2315)
        self.assertEqual(result.stats["source_polygon_count"], 3858)
        self.assertEqual(result.stats["polygon_count"], 3858)
        self.assertEqual(result.stats["skipped_degenerate_polygon_count"], 0)
        self.assertEqual(result.stats["surface_type_count"], 47)
        self.assertEqual(result.stats["water_box_count"], 1)
        self.assertEqual(result.stats["decoded_camera_record_count"], 15)
        self.assertEqual(result.stats["effective_camera_record_count"], 15)
        self.assertEqual(result.stats["exported_camera_record_count"], 15)
        self.assertEqual(result.stats["camera_position_vec_count"], 36)
        self.assertEqual(result.stats["camera_position_group_count"], 12)
        self.assertEqual(result.stats["source_spawn_floor_probe_count"], 14)
        self.assertEqual(
            result.stats["source_spawn_floor_probe_audit"]["status"],
            "decoded_source_player_spawn_floor_probes",
        )
        self.assertEqual(
            result.stats["source_spawn_floor_probe_audit"]["required_gameplay_probe_count"],
            14,
        )
        self.assertEqual(
            result.stats["source_spawn_floor_probe_audit"]["deferred_cutscene_probe_count"],
            7,
        )
        self.assertEqual(
            result.stats["source_spawn_floor_probe_audit"]["total_decoded_probe_count"],
            21,
        )
        self.assertEqual(
            result.stats["source_exit_list_audit"]["status"],
            "decoded_source_exit_lists",
        )
        self.assertTrue(result.stats["source_exit_list_audit"]["accepted"])
        self.assertEqual(result.stats["source_exit_list_audit"]["exit_list_command_count"], 13)
        self.assertEqual(result.stats["source_exit_list_audit"]["exit_value_candidate_total"], 48)
        self.assertEqual(
            result.stats["source_exit_list_audit"]["layout_validation_counts"],
            {"validated_prefixed_s16_exit_payload_window": 13},
        )
        self.assertNotIn("source_visual_floor_probe_audit", result.stats)
        self.assertNotIn("visual_spawn_floor_supplement", result.stats)
        self.assertEqual(result.stats["surface_type_offset"], 0x16A44)
        self.assertEqual(
            result.stats["surface_type_audit"],
            {
                "surface_type_count": 47,
                "raw_surface_type_count": 47,
                "max_polygon_type": 46,
                "covers_all_polygon_types": True,
                "raw_surface_table_end": 0x16BAC,
                "effective_surface_table_end": 0x16BBC,
                "bgcam_offset": 0x16BAC,
                "effective_bgcam_offset": 0x16BBC,
                "raw_ends_at_bgcam_offset": True,
                "effective_ends_at_effective_bgcam_offset": True,
                "surface_type_prefix_size": 16,
                "raw_first_data2_low16_is_polygon_marker": True,
                "effective_first_data2_low16_is_polygon_marker": False,
                "raw_surface_type_zero": {"data1": 0x090A08CC, "data2": 0x030D55DA},
                "effective_surface_type_zero": {"data1": 0x0000000B, "data2": 0x00000FCA},
            },
        )
        surface_semantics = result.stats["surface_type_semantic_audit"]
        self.assertEqual(len(surface_semantics["decoded_surface_types"]), 47)
        self.assertEqual(surface_semantics["decoded_surface_types"][0]["cam_data_index"], 0x0B)
        self.assertEqual(surface_semantics["invalid_camera_data_indices"], [])
        self.assertEqual(surface_semantics["decoded_surface_types"][-1]["cam_data_index"], 0x0B)
        self.assertEqual(surface_semantics["decoded_surface_types"][-1]["sfx_material"], 0x00)
        self.assertIn(0, surface_semantics["polygon_flag_counts"]["flags_via_values"])
        self.assertEqual(result.stats["camera_data_offset"], 0x16BBC)
        self.assertEqual(result.stats["camera_position_offset"], 0x16C34)
        self.assertEqual(
            result.stats["coordinate_transform"],
            "effective OOT3D collision vertex (x, y, z) -> Shipwright/N64 (x, y, z)",
        )
        self.assertEqual(
            result.stats["plane_source"],
            "native OOT3D polygon normal/dist; effective vertices are used for triangle membership and degenerate rejection",
        )
        self.assertEqual(
            result.stats["axis_audit"],
            {
                "coordinate_transform": "effective OOT3D collision vertex (x, y, z) -> Shipwright/N64 (x, y, z)",
                "runtime_header_offset": 0x16D1C,
                "command_argument": 0x16D0C,
                "command_argument_prefix_size": 16,
                "effective_vertex_offset": 0x698,
                "raw_vertex_offset": 0x688,
                "decoded_effective_vertex_bounds": ((-2301, -157, -3012), (5872, 1179, 2327)),
                "decoded_header_bounds": ((-2301, -157, -3012), (5872, 1179, 2327)),
                "exported_vertex_bounds": ((-2301, -157, -3012), (5872, 1179, 2327)),
                "exported_resource_bounds": ((-2301, -157, -3012), (5872, 1179, 2327)),
                "header_vs_exported_vertex_bounds": {
                    "axis_order": ("min_x", "min_y", "min_z", "max_x", "max_y", "max_z"),
                    "signed": (0, 0, 0, 0, 0, 0),
                    "absolute": (0, 0, 0, 0, 0, 0),
                    "max_abs_delta": 0,
                },
                "section_prefix_sizes": {
                    "command_argument": 16,
                    "vertices": 16,
                    "polygons": 16,
                    "surface_types": 16,
                    "camera_data": 16,
                    "water_boxes": 16,
                },
                "exported_bounds_source": "decoded native OOT3D collision header bounds",
                "notes": [
                    "The decoded header bounds are used to select the effective vertex table after any section prefix.",
                    "Shipwright resource bounds preserve the decoded native collision header bounds.",
                ],
            },
        )
        self.assertNotIn("spawn_floor_probe_filter", result.stats)
        self.assertIn('MinBoundsX="-2301" MinBoundsY="-157" MinBoundsZ="-3012"', xml)
        self.assertIn('MaxBoundsX="5872" MaxBoundsY="1179" MaxBoundsZ="2327"', xml)
        self.assertIn(
            '<Polygon Type="0" VertexA="1703" VertexB="1725" VertexC="1702" '
            'NormalX="-26827" NormalY="4479" NormalZ="-18273" Dist="1179"/>',
            xml,
        )
        self.assertEqual(xml.count("<PolygonType "), 47)
        self.assertIn(
            f'<PolygonType Data1="{0x0000000B}" Data2="{0x00000FCA}"/>',
            xml,
        )
        self.assertIn(
            f'<PolygonType Data1="{0x0020000B}" Data2="{0x00000FC0}"/>',
            xml,
        )
        self.assertIn('<CameraData SType="32" NumData="3" CameraPosDataSeg="0"/>', xml)
        self.assertIn('<CameraData SType="30" NumData="6" CameraPosDataSeg="27"/>', xml)
        self.assertIn('<CameraData SType="3" NumData="0" CameraPosDataSeg="0"/>', xml)
        self.assertEqual(xml.count("<CameraData "), 15)
        self.assertEqual(xml.count("<CameraPositionData "), 12)
        self.assertIn(
            '<CameraPositionData PosX="-1127" PosY="282" PosZ="565" '
            'RotX="4187" RotY="25304" RotZ="0" FOV="60" JfifID="10" Unknown="-1"/>',
            xml,
        )
        self.assertIn(
            '<WaterBox XMin="73" Ysurface="-12" ZMin="-588" '
            'XLength="1400" ZLength="1040" Properties="269"/>',
            xml,
        )

    @unittest.skipUnless(BASE_O2R.exists(), "local Shipwright oot.o2r fixture is not present")
    def test_parse_original_spot04_collision_metadata(self) -> None:
        metadata = load_collision_metadata_from_o2r(
            BASE_O2R,
            "scenes/shared/spot04_scene/spot04_sceneCollisionHeader_008918",
        )

        self.assertEqual(len(metadata.camera_data), 15)
        self.assertEqual(len(metadata.camera_positions), 12)
        self.assertEqual(len(metadata.surface_types), 46)
        self.assertEqual(len(metadata.water_boxes), 1)
        self.assertEqual(metadata.water_boxes[0].properties, 269)

    @unittest.skipUnless(
        SPOT04_SCENE_ZSI.exists() and BASE_O2R.exists(),
        "local OOT3D spot04 scene and Shipwright oot.o2r fixtures are not present",
    )
    def test_spot04_zsi_camera_metadata_matches_original_shipwright(self) -> None:
        candidate = ZsiFile.from_path(SPOT04_SCENE_ZSI).collision_header_candidates()[0]
        _vertices, _polygons, metadata = zsi_collision_candidate_to_shipwright(candidate)
        original = load_collision_metadata_from_o2r(
            BASE_O2R,
            "scenes/shared/spot04_scene/spot04_sceneCollisionHeader_008918",
        )

        self.assertEqual(metadata.camera_data, original.camera_data)
        self.assertEqual(metadata.camera_positions, original.camera_positions)

    @unittest.skipUnless(
        SPOT04_SCENE_ZSI.exists() and BASE_O2R.exists(),
        "local OOT3D spot04 scene and Shipwright oot.o2r fixtures are not present",
    )
    def test_spot04_zsi_collision_reference_gate_checks_gameplay_spawns(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            result = export_zsi_scene_collision(
                SPOT04_SCENE_ZSI,
                Path(tmp),
                "scenes/shared/spot04_scene/spot04_sceneCollisionHeader_008918",
                reference_o2r=BASE_O2R,
                require_reference_match=True,
            )

        comparison = result.stats["reference_comparison"]
        self.assertTrue(comparison["accepted"])
        self.assertEqual(comparison["source_spawn_floor_probes"]["probe_count"], 14)
        self.assertEqual(comparison["source_spawn_floor_probes"]["required_probe_count"], 14)
        self.assertEqual(comparison["source_spawn_floor_probes"]["converted_failure_count"], 0)
        self.assertEqual(comparison["source_spawn_floor_probes"]["converted_match_rate"], 1.0)
        self.assertEqual(comparison["source_spawn_floor_probes"]["unsupported_source_probe_count"], 0)
        self.assertEqual(
            result.stats["source_spawn_floor_probe_audit"]["deferred_cutscene_probe_count"],
            7,
        )


class SceneModPackagingTests(unittest.TestCase):
    @unittest.skipUnless(CUBE_CMB.exists(), "local OOT3D cube.cmb fixture is not present")
    def test_batch_static_manifest_records_generated_resources(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "cube.cmb").write_bytes(CUBE_CMB.read_bytes())

            manifest_path = batch_static(
                SimpleNamespace(
                    input=input_dir,
                    output=root / "out",
                    resource_prefix="objects/oot3d_batch_test",
                    limit=None,
                    no_textures=False,
                    only_rigid_multibone=False,
                    allow_nonstatic=False,
                )
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            audit = audit_static_batch(manifest_path, root / "batch_audit.json")

        self.assertEqual(manifest["considered"], 1)
        self.assertEqual(manifest["converted"], 1)
        record = manifest["records"][0]
        self.assertEqual(record["status"], "converted")
        self.assertGreater(record["resource_count"], 0)
        self.assertEqual(record["resource_count"], len(record["resources"]))
        self.assertIn("Texture", {resource["kind"] for resource in record["resources"]})
        self.assertEqual(audit["issue_counts"]["total"], 0)
        self.assertGreater(audit["material_display_list_count"], 0)
        self.assertGreater(audit["mesh_display_list_count"], 0)
        self.assertEqual(audit["negative_st_count"], 0)

    def test_batch_static_records_parse_failures_without_aborting(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "bad.cmb").write_bytes(b"not a cmb")

            manifest_path = batch_static(
                SimpleNamespace(
                    input=input_dir,
                    output=root / "out",
                    resource_prefix="objects/oot3d_batch_test",
                    limit=None,
                    no_textures=False,
                    only_rigid_multibone=False,
                    allow_nonstatic=False,
                )
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

        self.assertEqual(manifest["considered"], 0)
        self.assertEqual(manifest["converted"], 0)
        self.assertEqual(manifest["failed"], 0)
        self.assertEqual(manifest["parse_failed"], 1)
        self.assertEqual(manifest["records"][0]["status"], "parse_failed")
        self.assertIn("expected CMB magic", manifest["records"][0]["reason"])

    @unittest.skipUnless(
        CUBE_CMB.exists() and ZELDA_BOX_ZAR.exists(),
        "local OOT3D cube and zelda_box fixtures are not present",
    )
    def test_batch_static_can_filter_to_rigid_multibone_models(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "cube.cmb").write_bytes(CUBE_CMB.read_bytes())
            (input_dir / "zelda_box.zar").write_bytes(ZELDA_BOX_ZAR.read_bytes())

            manifest_path = batch_static(
                SimpleNamespace(
                    input=input_dir,
                    output=root / "out",
                    resource_prefix="objects/oot3d_rigid_batch_test",
                    limit=None,
                    no_textures=False,
                    only_rigid_multibone=True,
                    allow_nonstatic=False,
                )
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            audit = audit_static_batch(manifest_path, root / "batch_audit.json")

        self.assertEqual(manifest["filter"], "rigid_multibone")
        self.assertEqual(manifest["considered"], 2)
        self.assertEqual(manifest["converted"], 2)
        self.assertEqual(manifest["skipped"], 0)
        self.assertEqual(manifest["failed"], 0)
        self.assertEqual(
            {record["asset_id"] for record in manifest["records"]},
            {"zelda_box_model_demo_tre_lgt_mdl_info", "zelda_box_model_tr_box"},
        )
        self.assertTrue(all(record["summary"]["bone_count"] > 1 for record in manifest["records"]))
        self.assertEqual(audit["issue_counts"]["total"], 0)

    @unittest.skipUnless(ZELDA_BOX_ZAR.exists(), "local OOT3D zelda_box fixture is not present")
    def test_pack_static_batch_manifest_writes_auditable_o2r(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "zelda_box.zar").write_bytes(ZELDA_BOX_ZAR.read_bytes())

            manifest_path = batch_static(
                SimpleNamespace(
                    input=input_dir,
                    output=root / "out",
                    resource_prefix="objects/oot3d_static_package_test",
                    limit=None,
                    no_textures=False,
                    only_rigid_multibone=False,
                    allow_nonstatic=False,
                )
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            archive_path = root / "oot3d_static_batch.o2r"
            audit_path = root / "static_batch_package_audit.json"

            pack_static_batch_manifest(
                manifest_path,
                archive_path,
                name="OOT3D Static Batch",
                author="local",
                version="0.1.0",
            )
            audit = audit_static_batch_package(manifest_path, archive_path, audit_path)

            self.assertTrue(archive_path.exists())
            self.assertTrue(audit_path.exists())
            with zipfile.ZipFile(archive_path) as archive:
                archive_names = set(archive.namelist())
                first_resource_path = manifest["records"][0]["resources"][0]["path"]
                self.assertIn("manifest.json", archive_names)
                self.assertIn("oot3d_static_batch_manifest.json", archive_names)
                self.assertIn(first_resource_path, archive_names)

        self.assertEqual(audit["format"], "oot3d_static_batch_package_audit_v1")
        self.assertTrue(audit["has_manifest"])
        self.assertTrue(audit["has_static_batch_manifest"])
        self.assertTrue(audit["archived_static_batch_manifest_matches"])
        self.assertEqual(audit["converted_record_count"], 2)
        self.assertEqual(audit["expected_resource_count"], 78)
        self.assertEqual(audit["expected_resource_count"], audit["resource_entry_count"])
        self.assertEqual(audit["archive_entry_count"], 80)
        self.assertEqual(audit["missing_resource_entry_count"], 0)
        self.assertEqual(audit["extra_resource_entry_count"], 0)
        self.assertEqual(audit["duplicate_archive_entry_count"], 0)
        self.assertEqual(audit["duplicate_expected_resource_path_count"], 0)
        self.assertEqual(audit["invalid_resource_xml_count"], 0)
        self.assertEqual(audit["issue_counts"]["total"], 0)

    def test_audit_scene_package_matches_material_texture_and_mesh_calls(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            resource_root = "scenes/overworld/spot04/oot3d/spot04_room_0"
            texture_file = root / "tex0.rgba16"
            material_file = root / "gOot3dSpot04Room0_mat_0"
            mesh_file = root / "gOot3dSpot04Room0_mesh_0"
            texture_file.write_bytes(b"texture")
            material_file.write_text(
                '<DisplayList Version="0">\n'
                f'\t<SetTextureImage Path="{resource_root}/tex0.rgba16" '
                'Format="G_IM_FMT_RGBA" Size="G_IM_SIZ_16b" Width="32"/>\n'
                "\t<EndDisplayList/>\n"
                "</DisplayList>\n",
                encoding="utf-8",
            )
            mesh_file.write_text(
                '<DisplayList Version="0">\n'
                f'\t<CallDisplayList Path="{resource_root}/gOot3dSpot04Room0_mat_0"/>\n'
                "\t<EndDisplayList/>\n"
                "</DisplayList>\n",
                encoding="utf-8",
            )
            manifest_path = root / "scene_manifest.json"
            manifest_path.write_text(
                json.dumps(
                    {
                        "records": [
                            {
                                "status": "converted",
                                "asset_id": "spot04_room_0",
                                "resources": [
                                    {
                                        "kind": "Texture",
                                        "path": f"{resource_root}/tex0.rgba16",
                                        "file": str(texture_file),
                                    },
                                    {
                                        "kind": "DisplayList",
                                        "path": f"{resource_root}/gOot3dSpot04Room0_mat_0",
                                        "file": str(material_file),
                                    },
                                    {
                                        "kind": "DisplayList",
                                        "path": f"{resource_root}/gOot3dSpot04Room0_mesh_0",
                                        "file": str(mesh_file),
                                    },
                                ],
                            }
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            material_audit_path = root / "material_audit.json"
            material_audit_path.write_text(
                json.dumps(
                    {
                        "scene": "spot04",
                        "records": [
                            {
                                "asset_id": "spot04_room_0",
                                "materials": [
                                    {
                                        "material_index": 0,
                                        "generated_material_path": f"{resource_root}/gOot3dSpot04Room0_mat_0",
                                        "selected_primary_texture": {"index": 0, "name": "tex0"},
                                        "selected_secondary_texture": None,
                                        "exported_texture_stage_count": 1,
                                        "raw_texture_stage_selector": {"stage_count": 2},
                                        "raw_texture_stage_export_gap": 1,
                                    }
                                ],
                                "meshes": [
                                    {
                                        "asset_id": "spot04_room_0",
                                        "mesh_index": 0,
                                        "generated_mesh_path": f"{resource_root}/gOot3dSpot04Room0_mesh_0",
                                        "generated_material_path": f"{resource_root}/gOot3dSpot04Room0_mat_0",
                                    }
                                ],
                            }
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )

            archive_path = pack_scene_mod_manifest(
                manifest_path,
                root / "oot3d_kokiri_forest.o2r",
                name="OOT3D Kokiri Forest",
                author="test",
                version="0.1.0",
            )
            audit = audit_scene_package(
                manifest_path,
                material_audit_path,
                archive_path,
                root / "package_audit.json",
            )
            self.assertEqual(audit["issue_counts"]["total"], 0)
            self.assertEqual(audit["material_count"], 1)
            self.assertEqual(audit["mesh_count"], 1)
            self.assertEqual(audit["raw_texture_stage_export_gap_material_count"], 1)
            self.assertEqual(audit["raw_texture_stage_export_gap_total"], 1)
            self.assertEqual(audit["material_results"][0]["actual_texture_stage_count"], 1)
            self.assertEqual(audit["material_results"][0]["actual_raw_stage_export_gap"], 1)

            material_file.write_text(
                '<DisplayList Version="0">\n'
                f'\t<SetTextureImage Path="{resource_root}/wrong.rgba16" '
                'Format="G_IM_FMT_RGBA" Size="G_IM_SIZ_16b" Width="32"/>\n'
                "\t<EndDisplayList/>\n"
                "</DisplayList>\n",
                encoding="utf-8",
            )
            bad_archive_path = pack_scene_mod_manifest(
                manifest_path,
                root / "oot3d_kokiri_forest_bad.o2r",
                name="OOT3D Kokiri Forest",
                author="test",
                version="0.1.0",
            )
            bad_audit = audit_scene_package(
                manifest_path,
                material_audit_path,
                bad_archive_path,
                root / "bad_package_audit.json",
            )
            self.assertEqual(bad_audit["issue_counts"]["material_texture_path_mismatch"], 1)
            self.assertEqual(bad_audit["issue_counts"]["missing_actual_texture_entry"], 1)
            self.assertEqual(bad_audit["issue_counts"]["material_texture_stage_count_mismatch"], 0)
            self.assertEqual(bad_audit["issue_counts"]["raw_stage_export_gap_mismatch"], 0)

            material_file.write_text(
                '<DisplayList Version="0">\n'
                "\t<EndDisplayList/>\n"
                "</DisplayList>\n",
                encoding="utf-8",
            )
            missing_stage_archive_path = pack_scene_mod_manifest(
                manifest_path,
                root / "oot3d_kokiri_forest_missing_stage.o2r",
                name="OOT3D Kokiri Forest",
                author="test",
                version="0.1.0",
            )
            missing_stage_audit = audit_scene_package(
                manifest_path,
                material_audit_path,
                missing_stage_archive_path,
                root / "missing_stage_package_audit.json",
            )
            self.assertEqual(missing_stage_audit["issue_counts"]["material_texture_stage_count_mismatch"], 1)
            self.assertEqual(missing_stage_audit["issue_counts"]["raw_stage_export_gap_mismatch"], 1)

    def test_audit_scene_package_requires_accepted_native_zsi_collision_gate(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            collision_file = root / "spot04_sceneCollisionHeader_008918"
            write_collision_resource(
                collision_file,
                ((0, 0, 0), (100, 0, 0), (0, 0, 100)),
                (CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, 0),),
                CollisionMetadata(
                    camera_data=(CameraData(32, 3, 0),),
                    camera_positions=(
                        CameraPositionData((1, 2, 3), (4, 5, 6), (60, 10, -1)),
                    ),
                    water_boxes=(WaterBox(0, 0, 0, 10, 10, 0),),
                    surface_types=(SurfaceType(0, 0),),
                ),
            )
            collision_path = "scenes/shared/spot04_scene/spot04_sceneCollisionHeader_008918"
            material_audit_path = root / "material_audit.json"
            material_audit_path.write_text(
                json.dumps({"scene": "spot04", "records": []}) + "\n",
                encoding="utf-8-sig",
            )

            def write_test_collision_manifest(
                path: Path,
                resource_file: Path,
                *,
                resource_path: str = collision_path,
                summary_overrides: dict[str, object] | None = None,
            ) -> None:
                summary = {
                    "bounds_min": [0, 0, 0],
                    "bounds_max": [100, 0, 100],
                    "vertex_count": 3,
                    "polygon_count": 1,
                    "surface_type_count": 1,
                    "exported_camera_record_count": 1,
                    "camera_position_group_count": 1,
                    "water_box_count": 1,
                    "reference_comparison": {"accepted": True},
                    "native_zsi_acceptance": {"accepted": True},
                    "visual_diagnostic_policy": {
                        "format": "oot3d_collision_visual_diagnostic_policy_v1",
                        "collision_source": "oot3d_zsi_native_collision",
                        "visual_mesh_is_collision_source": False,
                        "reference_comparison_accepted": True,
                        "native_zsi_acceptance_accepted": True,
                        "relaxed_checks_from_visual_diagnostics": [],
                        "reference_failed_checks_still_blocking": [],
                    },
                }
                if summary_overrides:
                    summary.update(summary_overrides)
                path.write_text(
                    json.dumps(
                        {
                            "records": [
                                {
                                    "status": "converted",
                                    "kind": "CollisionHeader",
                                    "source": "oot3d_zsi_native_collision",
                                    "reference_resource_path_source": "reference_o2r_discovery",
                                    "summary": summary,
                                    "resources": [
                                        {
                                            "kind": "CollisionHeader",
                                            "path": resource_path,
                                            "file": str(resource_file),
                                        }
                                    ],
                                }
                            ],
                        },
                        indent=2,
                    )
                    + "\n",
                    encoding="utf-8",
                )

            manifest_path = root / "accepted_collision_manifest.json"
            manifest_path.write_text(
                json.dumps(
                    {
                        "records": [
                            {
                                "status": "converted",
                                "kind": "CollisionHeader",
                                "source": "oot3d_zsi_native_collision",
                                "reference_resource_path_source": "reference_o2r_discovery",
                                "summary": {
                                    "bounds_min": [0, 0, 0],
                                    "bounds_max": [100, 0, 100],
                                    "vertex_count": 3,
                                    "polygon_count": 1,
                                    "surface_type_count": 1,
                                    "exported_camera_record_count": 1,
                                    "camera_position_group_count": 1,
                                    "water_box_count": 1,
                                    "reference_comparison": {"accepted": True},
                                    "native_zsi_acceptance": {"accepted": True},
                                    "visual_diagnostic_policy": {
                                        "format": "oot3d_collision_visual_diagnostic_policy_v1",
                                        "collision_source": "oot3d_zsi_native_collision",
                                        "visual_mesh_is_collision_source": False,
                                        "reference_comparison_accepted": True,
                                        "native_zsi_acceptance_accepted": True,
                                        "relaxed_checks_from_visual_diagnostics": [],
                                        "reference_failed_checks_still_blocking": [],
                                    },
                                },
                                "resources": [
                                    {
                                        "kind": "CollisionHeader",
                                        "path": collision_path,
                                        "file": str(collision_file),
                                    }
                                ],
                            }
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            archive_path = pack_scene_mod_manifest(
                manifest_path,
                root / "accepted_collision.o2r",
                name="OOT3D Kokiri Forest",
                author="test",
                version="0.1.0",
            )
            audit = audit_scene_package(
                manifest_path,
                material_audit_path,
                archive_path,
                root / "accepted_collision_package_audit.json",
            )

            self.assertEqual(audit["collision_count"], 1)
            self.assertEqual(audit["issue_counts"]["total"], 0)
            self.assertEqual(audit["collision_results"][0]["resource_path"], collision_path)
            self.assertEqual(
                audit["collision_results"][0]["reference_resource_path_source"],
                "reference_o2r_discovery",
            )
            self.assertTrue(audit["collision_results"][0]["reference_comparison_accepted"])
            self.assertTrue(audit["collision_results"][0]["native_zsi_acceptance_accepted"])
            self.assertEqual(audit["collision_results"][0]["binary_resource"]["parse_status"], "ok")
            self.assertEqual(
                audit["collision_results"][0]["binary_resource"]["actual_counts"],
                {
                    "vertex_count": 3,
                    "polygon_count": 1,
                    "surface_type_count": 1,
                    "exported_camera_record_count": 1,
                    "camera_position_group_count": 1,
                    "water_box_count": 1,
                },
            )
            self.assertEqual(
                audit["collision_results"][0]["binary_resource"]["actual_bounds"],
                {
                    "bounds_min": (0, 0, 0),
                    "bounds_max": (100, 0, 100),
                },
            )
            self.assertEqual(
                audit["collision_results"][0]["binary_resource"]["expected_bounds"],
                {
                    "bounds_min": (0, 0, 0),
                    "bounds_max": (100, 0, 100),
                },
            )
            self.assertEqual(
                audit["collision_results"][0]["visual_diagnostic_policy"]["collision_source"],
                "oot3d_zsi_native_collision",
            )
            self.assertFalse(
                audit["collision_results"][0]["visual_diagnostic_policy"][
                    "visual_mesh_is_collision_source"
                ]
            )

            bad_count_manifest_path = root / "bad_collision_count_manifest.json"
            bad_count_manifest_path.write_text(
                json.dumps(
                    {
                        "records": [
                            {
                                "status": "converted",
                                "kind": "CollisionHeader",
                                "source": "oot3d_zsi_native_collision",
                                "reference_resource_path_source": "reference_o2r_discovery",
                                "summary": {
                                    "bounds_min": [0, 0, 0],
                                    "bounds_max": [100, 0, 100],
                                    "vertex_count": 4,
                                    "polygon_count": 1,
                                    "surface_type_count": 1,
                                    "exported_camera_record_count": 1,
                                    "camera_position_group_count": 1,
                                    "water_box_count": 1,
                                    "reference_comparison": {"accepted": True},
                                    "native_zsi_acceptance": {"accepted": True},
                                    "visual_diagnostic_policy": {
                                        "format": "oot3d_collision_visual_diagnostic_policy_v1",
                                        "collision_source": "oot3d_zsi_native_collision",
                                        "visual_mesh_is_collision_source": False,
                                        "reference_comparison_accepted": True,
                                        "native_zsi_acceptance_accepted": True,
                                        "relaxed_checks_from_visual_diagnostics": [],
                                        "reference_failed_checks_still_blocking": [],
                                    },
                                },
                                "resources": [
                                    {
                                        "kind": "CollisionHeader",
                                        "path": collision_path,
                                        "file": str(collision_file),
                                    }
                                ],
                            }
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            bad_count_archive_path = pack_scene_mod_manifest(
                bad_count_manifest_path,
                root / "bad_collision_count.o2r",
                name="OOT3D Kokiri Forest",
                author="test",
                version="0.1.0",
            )
            bad_count_audit = audit_scene_package(
                bad_count_manifest_path,
                material_audit_path,
                bad_count_archive_path,
                root / "bad_collision_count_package_audit.json",
            )

            self.assertEqual(
                bad_count_audit["issue_counts"]["collision_resource_vertex_count_mismatch"],
                1,
            )
            self.assertEqual(bad_count_audit["issue_counts"]["total"], 1)

            bad_bounds_manifest_path = root / "bad_collision_bounds_manifest.json"
            bad_bounds_manifest_path.write_text(
                json.dumps(
                    {
                        "records": [
                            {
                                "status": "converted",
                                "kind": "CollisionHeader",
                                "source": "oot3d_zsi_native_collision",
                                "reference_resource_path_source": "reference_o2r_discovery",
                                "summary": {
                                    "bounds_min": [0, -1000, 0],
                                    "bounds_max": [100, 1000, 100],
                                    "vertex_count": 3,
                                    "polygon_count": 1,
                                    "surface_type_count": 1,
                                    "exported_camera_record_count": 1,
                                    "camera_position_group_count": 1,
                                    "water_box_count": 1,
                                    "reference_comparison": {"accepted": True},
                                    "native_zsi_acceptance": {"accepted": True},
                                    "visual_diagnostic_policy": {
                                        "format": "oot3d_collision_visual_diagnostic_policy_v1",
                                        "collision_source": "oot3d_zsi_native_collision",
                                        "visual_mesh_is_collision_source": False,
                                        "reference_comparison_accepted": True,
                                        "native_zsi_acceptance_accepted": True,
                                        "relaxed_checks_from_visual_diagnostics": [],
                                        "reference_failed_checks_still_blocking": [],
                                    },
                                },
                                "resources": [
                                    {
                                        "kind": "CollisionHeader",
                                        "path": collision_path,
                                        "file": str(collision_file),
                                    }
                                ],
                            }
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            bad_bounds_archive_path = pack_scene_mod_manifest(
                bad_bounds_manifest_path,
                root / "bad_collision_bounds.o2r",
                name="OOT3D Kokiri Forest",
                author="test",
                version="0.1.0",
            )
            bad_bounds_audit = audit_scene_package(
                bad_bounds_manifest_path,
                material_audit_path,
                bad_bounds_archive_path,
                root / "bad_collision_bounds_package_audit.json",
            )

            self.assertEqual(
                bad_bounds_audit["issue_counts"]["collision_resource_bounds_min_mismatch"],
                1,
            )
            self.assertEqual(
                bad_bounds_audit["issue_counts"]["collision_resource_bounds_max_mismatch"],
                1,
            )
            self.assertEqual(bad_bounds_audit["issue_counts"]["total"], 2)

            bad_plane_file = root / "badPlaneCollisionHeader"
            write_collision_resource(
                bad_plane_file,
                ((0, 0, 0), (100, 0, 0), (0, 0, 100)),
                (CollisionPolygon(0, 0, 1, 2, 0, 32767, 0, -100),),
                CollisionMetadata(
                    camera_data=(CameraData(32, 3, 0),),
                    camera_positions=(
                        CameraPositionData((1, 2, 3), (4, 5, 6), (60, 10, -1)),
                    ),
                    water_boxes=(WaterBox(0, 0, 0, 10, 10, 0),),
                    surface_types=(SurfaceType(0, 0),),
                ),
            )
            bad_plane_manifest_path = root / "bad_collision_plane_manifest.json"
            write_test_collision_manifest(bad_plane_manifest_path, bad_plane_file)
            bad_plane_archive_path = pack_scene_mod_manifest(
                bad_plane_manifest_path,
                root / "bad_collision_plane.o2r",
                name="OOT3D Kokiri Forest",
                author="test",
                version="0.1.0",
            )
            bad_plane_audit = audit_scene_package(
                bad_plane_manifest_path,
                material_audit_path,
                bad_plane_archive_path,
                root / "bad_collision_plane_package_audit.json",
            )

            self.assertEqual(
                bad_plane_audit["issue_counts"]["collision_resource_polygon_plane_mismatch"],
                1,
            )
            self.assertEqual(bad_plane_audit["issue_counts"]["total"], 1)

            bad_index_file = root / "badIndexCollisionHeader"
            write_collision_resource(
                bad_index_file,
                ((0, 0, 0), (100, 0, 0), (0, 0, 100)),
                (CollisionPolygon(0, 0, 5, 2, 0, 32767, 0, 0),),
                CollisionMetadata(
                    camera_data=(CameraData(32, 3, 0),),
                    camera_positions=(
                        CameraPositionData((1, 2, 3), (4, 5, 6), (60, 10, -1)),
                    ),
                    water_boxes=(WaterBox(0, 0, 0, 10, 10, 0),),
                    surface_types=(SurfaceType(0, 0),),
                ),
            )
            bad_index_manifest_path = root / "bad_collision_index_manifest.json"
            write_test_collision_manifest(bad_index_manifest_path, bad_index_file)
            bad_index_archive_path = pack_scene_mod_manifest(
                bad_index_manifest_path,
                root / "bad_collision_index.o2r",
                name="OOT3D Kokiri Forest",
                author="test",
                version="0.1.0",
            )
            bad_index_audit = audit_scene_package(
                bad_index_manifest_path,
                material_audit_path,
                bad_index_archive_path,
                root / "bad_collision_index_package_audit.json",
            )

            self.assertEqual(
                bad_index_audit["issue_counts"][
                    "collision_resource_polygon_vertex_index_out_of_range"
                ],
                1,
            )
            self.assertEqual(bad_index_audit["issue_counts"]["total"], 1)

            bad_binary_file = root / "badCollisionHeader"
            bad_binary_file.write_bytes(b"not an OCOL resource")
            bad_binary_path = "scenes/shared/test_scene/badCollisionHeader"
            bad_binary_manifest_path = root / "bad_collision_binary_manifest.json"
            bad_binary_manifest_path.write_text(
                json.dumps(
                    {
                        "records": [
                            {
                                "status": "converted",
                                "kind": "CollisionHeader",
                                "source": "oot3d_zsi_native_collision",
                                "reference_resource_path_source": "reference_o2r_discovery",
                                "summary": {
                                    "vertex_count": 3,
                                    "reference_comparison": {"accepted": True},
                                    "native_zsi_acceptance": {"accepted": True},
                                    "visual_diagnostic_policy": {
                                        "format": "oot3d_collision_visual_diagnostic_policy_v1",
                                        "collision_source": "oot3d_zsi_native_collision",
                                        "visual_mesh_is_collision_source": False,
                                        "reference_comparison_accepted": True,
                                        "native_zsi_acceptance_accepted": True,
                                        "relaxed_checks_from_visual_diagnostics": [],
                                        "reference_failed_checks_still_blocking": [],
                                    },
                                },
                                "resources": [
                                    {
                                        "kind": "CollisionHeader",
                                        "path": bad_binary_path,
                                        "file": str(bad_binary_file),
                                    }
                                ],
                            }
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            bad_binary_archive_path = pack_scene_mod_manifest(
                bad_binary_manifest_path,
                root / "bad_collision_binary.o2r",
                name="OOT3D Kokiri Forest",
                author="test",
                version="0.1.0",
            )
            bad_binary_audit = audit_scene_package(
                bad_binary_manifest_path,
                material_audit_path,
                bad_binary_archive_path,
                root / "bad_collision_binary_package_audit.json",
            )

            self.assertEqual(bad_binary_audit["issue_counts"]["collision_resource_parse_error"], 1)
            self.assertEqual(bad_binary_audit["issue_counts"]["total"], 1)

            bad_manifest_path = root / "bad_collision_manifest.json"
            bad_manifest_path.write_text(
                json.dumps(
                    {
                        "records": [
                            {
                                "status": "converted",
                                "kind": "CollisionHeader",
                                "source": "visual_mesh_collision",
                                "reference_resource_path_source": "manual_resource_path",
                                "summary": {
                                    "reference_comparison": {"accepted": False},
                                    "native_zsi_acceptance": {"accepted": False},
                                },
                                "resources": [
                                    {
                                        "kind": "CollisionHeader",
                                        "path": collision_path,
                                        "file": str(collision_file),
                                    }
                                ],
                            }
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            bad_archive_path = pack_scene_mod_manifest(
                bad_manifest_path,
                root / "bad_collision.o2r",
                name="OOT3D Kokiri Forest",
                author="test",
                version="0.1.0",
            )
            bad_audit = audit_scene_package(
                bad_manifest_path,
                material_audit_path,
                bad_archive_path,
                root / "bad_collision_package_audit.json",
            )

            self.assertEqual(bad_audit["collision_count"], 1)
            self.assertEqual(bad_audit["issue_counts"]["collision_source_not_native_zsi"], 1)
            self.assertEqual(
                bad_audit["issue_counts"][
                    "collision_reference_resource_path_not_reference_o2r_discovery"
                ],
                1,
            )
            self.assertEqual(bad_audit["issue_counts"]["collision_reference_comparison_not_accepted"], 1)
            self.assertEqual(bad_audit["issue_counts"]["collision_native_zsi_acceptance_not_accepted"], 1)
            self.assertEqual(bad_audit["issue_counts"]["collision_missing_visual_diagnostic_policy"], 1)
            self.assertEqual(bad_audit["issue_counts"]["total"], 5)

            bad_policy_manifest_path = root / "bad_collision_visual_policy_manifest.json"
            bad_policy_manifest_path.write_text(
                json.dumps(
                    {
                        "records": [
                            {
                                "status": "converted",
                                "kind": "CollisionHeader",
                                "source": "oot3d_zsi_native_collision",
                                "reference_resource_path_source": "reference_o2r_discovery",
                                "summary": {
                                    "reference_comparison": {"accepted": True},
                                    "native_zsi_acceptance": {"accepted": True},
                                    "visual_diagnostic_policy": {
                                        "format": "oot3d_collision_visual_diagnostic_policy_v1",
                                        "collision_source": "visual_mesh_collision",
                                        "visual_mesh_is_collision_source": True,
                                        "reference_comparison_accepted": True,
                                        "native_zsi_acceptance_accepted": True,
                                        "relaxed_checks_from_visual_diagnostics": [
                                            "water_box_coverage"
                                        ],
                                        "reference_failed_checks_still_blocking": [
                                            "surface_exit_index_usage"
                                        ],
                                    },
                                },
                                "resources": [
                                    {
                                        "kind": "CollisionHeader",
                                        "path": collision_path,
                                        "file": str(collision_file),
                                    }
                                ],
                            }
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            bad_policy_archive_path = pack_scene_mod_manifest(
                bad_policy_manifest_path,
                root / "bad_collision_visual_policy.o2r",
                name="OOT3D Kokiri Forest",
                author="test",
                version="0.1.0",
            )
            bad_policy_audit = audit_scene_package(
                bad_policy_manifest_path,
                material_audit_path,
                bad_policy_archive_path,
                root / "bad_collision_visual_policy_package_audit.json",
            )

            self.assertEqual(bad_policy_audit["issue_counts"]["collision_visual_policy_source_mismatch"], 1)
            self.assertEqual(bad_policy_audit["issue_counts"]["collision_visual_mesh_marked_as_source"], 1)
            self.assertEqual(
                bad_policy_audit["issue_counts"]["collision_visual_policy_unresolved_reference_failures"],
                1,
            )
            self.assertEqual(bad_policy_audit["issue_counts"]["collision_visual_policy_forbidden_relaxation"], 1)
            self.assertEqual(bad_policy_audit["issue_counts"]["total"], 4)

    @unittest.skipUnless(
        SPOT04_SCENE_ZSI.exists() and BASE_O2R.exists(),
        "local OOT3D spot04 scene and Shipwright oot.o2r fixtures are not present",
    )
    def test_collision_activation_manifest_exports_only_accepted_native_zsi_records(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scene_dir = root / "scene"
            scene_dir.mkdir()
            (scene_dir / "spot04_info.zsi").write_bytes(SPOT04_SCENE_ZSI.read_bytes())
            manifest_path = root / "spot04_collision_activation_manifest.json"
            resource_output_dir = root / "collision_resources"

            manifest = export_zsi_collision_activation_manifest(
                scene_dir,
                BASE_O2R,
                manifest_path,
                resource_output_dir,
                scenes=("spot04",),
                sample_limit=2,
            )

            self.assertTrue(manifest_path.exists())
            self.assertEqual(manifest["format"], "oot3d_zsi_collision_activation_manifest_v1")
            self.assertEqual(manifest["accepted_scene_count"], 1)
            self.assertEqual(manifest["fallback_scene_count"], 0)
            self.assertEqual(manifest["audit_status_counts"], {"accepted": 1})
            self.assertEqual(manifest["audit_scene_status_counts"], {"accepted": 1})
            record = manifest["records"][0]
            self.assertEqual(record["status"], "converted")
            self.assertEqual(record["kind"], "CollisionHeader")
            self.assertEqual(record["scene"], "spot04")
            self.assertEqual(record["source"], "oot3d_zsi_native_collision")
            self.assertEqual(record["source_zsi"], str(scene_dir / "spot04_info.zsi"))
            self.assertEqual(
                record["resource_path"],
                "scenes/shared/spot04_scene/spot04_sceneCollisionHeader_008918",
            )
            self.assertEqual(record["reference_resource_path_source"], "reference_o2r_discovery")
            self.assertTrue(record["summary"]["reference_comparison"]["accepted"])
            self.assertTrue(record["summary"]["native_zsi_acceptance"]["accepted"])
            self.assertEqual(record["summary"]["vertex_count"], 2315)
            self.assertEqual(record["summary"]["polygon_count"], 3858)
            resource = record["resources"][0]
            self.assertEqual(resource["kind"], "CollisionHeader")
            self.assertEqual(resource["path"], record["resource_path"])
            self.assertTrue(Path(resource["file"]).exists())

            material_audit_path = root / "material_audit.json"
            material_audit_path.write_text(
                json.dumps({"scene": "spot04", "records": []}) + "\n",
                encoding="utf-8",
            )
            archive_path = pack_scene_mod_manifest(
                manifest_path,
                root / "spot04_collision_activation.o2r",
                name="OOT3D Collision Activation",
                author="test",
                version="0.1.0",
            )
            package_audit = audit_scene_package(
                manifest_path,
                material_audit_path,
                archive_path,
                root / "spot04_collision_activation_package_audit.json",
            )

        self.assertEqual(package_audit["collision_count"], 1)
        self.assertEqual(package_audit["checked_collision_count"], 1)
        self.assertEqual(package_audit["issue_counts"]["total"], 0)
        self.assertTrue(package_audit["collision_results"][0]["reference_comparison_accepted"])
        self.assertTrue(package_audit["collision_results"][0]["native_zsi_acceptance_accepted"])

    @unittest.skipUnless(BASE_O2R.exists(), "local Shipwright oot.o2r fixture is not present")
    def test_collision_activation_manifest_keeps_no_candidate_scenes_as_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scene_dir = root / "scene"
            scene_dir.mkdir()
            (scene_dir / "empty_info.zsi").write_bytes(b"ZSI\x01" + b"\0" * 0x40)
            resource_output_dir = root / "collision_resources"

            manifest = export_zsi_collision_activation_manifest(
                scene_dir,
                BASE_O2R,
                root / "empty_collision_activation_manifest.json",
                resource_output_dir,
                scenes=("empty",),
                sample_limit=2,
            )

        self.assertEqual(manifest["accepted_scene_count"], 0)
        self.assertEqual(manifest["fallback_scene_count"], 1)
        self.assertEqual(manifest["audit_status_counts"], {"no_collision_candidate": 1})
        self.assertEqual(manifest["audit_scene_status_counts"], {"fallback": 1})
        self.assertEqual(manifest["records"], [])
        self.assertEqual(manifest["fallback_records"][0]["scene"], "empty")
        self.assertEqual(manifest["fallback_records"][0]["status"], "fallback")
        self.assertEqual(manifest["fallback_records"][0]["reason"], "n64_fallback_retained")
        self.assertEqual(list(resource_output_dir.rglob("*")), [])

    @unittest.skipUnless(
        GANON_TOU_SCENE_ZSI.exists() and BASE_O2R.exists(),
        "local OOT3D ganon_tou scene and Shipwright oot.o2r fixtures are not present",
    )
    def test_collision_activation_manifest_keeps_rejected_scene_blocker_details(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scene_dir = root / "scene"
            scene_dir.mkdir()
            (scene_dir / "ganon_tou_info.zsi").write_bytes(GANON_TOU_SCENE_ZSI.read_bytes())
            resource_output_dir = root / "collision_resources"

            manifest = export_zsi_collision_activation_manifest(
                scene_dir,
                BASE_O2R,
                root / "ganon_tou_collision_activation_manifest.json",
                resource_output_dir,
                scenes=("ganon_tou",),
                sample_limit=2,
            )

        self.assertEqual(manifest["accepted_scene_count"], 0)
        self.assertEqual(manifest["fallback_scene_count"], 1)
        self.assertEqual(manifest["audit_status_counts"], {"rejected": 1})
        self.assertEqual(manifest["audit_scene_status_counts"], {"blocked": 1})
        self.assertEqual(manifest["records"], [])
        fallback = manifest["fallback_records"][0]
        self.assertEqual(fallback["scene"], "ganon_tou")
        self.assertEqual(fallback["status"], "blocked")
        self.assertEqual(fallback["rejected_paths"], ["ganon_tou_info.zsi"])
        self.assertEqual(list(resource_output_dir.rglob("*")), [])
        blocker = fallback["rejected_records"][0]
        self.assertEqual(blocker["path"], "ganon_tou_info.zsi")
        self.assertIn("surface_semantics_present", blocker["failed_checks"])
        self.assertFalse(blocker["surface_semantics_present"])
        self.assertIn("surface.hookshot", blocker["missing_reference_semantics"])
        self.assertEqual(
            blocker["visual_diagnostic_policy"]["collision_source"],
            "oot3d_zsi_native_collision",
        )
        self.assertFalse(blocker["visual_diagnostic_policy"]["visual_mesh_is_collision_source"])

    @unittest.skipUnless(SPOT04_ROOM0_ZSI.exists(), "local OOT3D spot04 room fixture is not present")
    def test_audit_scene_package_verifies_exported_vertex_buffers(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scene_dir = root / "scene"
            scene_dir.mkdir()
            (scene_dir / "spot04_0_info.zsi").write_bytes(SPOT04_ROOM0_ZSI.read_bytes())

            manifest_path = convert_scene(
                SimpleNamespace(
                    scene_dir=scene_dir,
                    scene="spot04",
                    output=root / "converted_scene",
                    resource_prefix=None,
                    shipwright_root=None,
                    no_textures=False,
                    generate_collision=False,
                    no_collision=True,
                    collision_path=None,
                    base_o2r=None,
                    allow_nonstatic=False,
                )
            )
            material_audit_path = root / "spot04_material_audit.json"
            audit_scene_materials(scene_dir, "spot04", material_audit_path)
            archive_path = pack_scene_mod_manifest(
                manifest_path,
                root / "spot04_room0.o2r",
                name="OOT3D Kokiri Forest",
                author="test",
                version="0.1.0",
            )
            package_audit = audit_scene_package(
                manifest_path,
                material_audit_path,
                archive_path,
                root / "spot04_package_audit.json",
            )

        self.assertEqual(package_audit["issue_counts"]["total"], 0)
        self.assertGreater(package_audit["checked_vertex_buffer_count"], 0)
        self.assertGreater(package_audit["checked_vertex_count"], 0)
        self.assertEqual(
            package_audit["checked_vertex_buffer_count"],
            sum(result["expected_vertex_buffer_count"] for result in package_audit["mesh_results"]),
        )
        self.assertEqual(package_audit["issue_counts"]["vertex_buffer_mismatch"], 0)

    def test_pack_scene_mod_manifest_writes_o2r_entries(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            resource_file = root / "gOot3dSpot04Room0"
            resource_file.write_text("<DisplayList Version=\"0\"></DisplayList>\n", encoding="utf-8")
            manifest_path = root / "scene_manifest.json"
            manifest_path.write_text(
                """
{
  "records": [
    {
      "status": "converted",
      "resources": [
        {
          "kind": "DisplayList",
          "path": "scenes/overworld/spot04/oot3d/spot04_room_0/gOot3dSpot04Room0",
          "file": "gOot3dSpot04Room0"
        },
        {
          "kind": "CollisionHeader",
          "path": "scenes/shared/spot04_scene/spot04_sceneCollisionHeader_008918",
          "file": "spot04_sceneCollisionHeader_008918"
        }
      ]
    }
  ]
}
""".strip()
                + "\n",
                encoding="utf-8",
            )
            collision_file = root / "spot04_sceneCollisionHeader_008918"
            collision_file.write_text(
                '<CollisionHeader Version="0" MinBoundsX="0" MinBoundsY="0" MinBoundsZ="0" '
                'MaxBoundsX="1" MaxBoundsY="1" MaxBoundsZ="1"></CollisionHeader>\n',
                encoding="utf-8",
            )

            archive_path = pack_scene_mod_manifest(
                manifest_path,
                root / "oot3d_kokiri_forest.o2r",
                name="OOT3D Kokiri Forest",
                author="test",
                version="0.1.0",
            )
            archive_path_2 = pack_scene_mod_manifest(
                manifest_path,
                root / "oot3d_kokiri_forest_again.o2r",
                name="OOT3D Kokiri Forest",
                author="test",
                version="0.1.0",
            )

            with zipfile.ZipFile(archive_path) as archive:
                names = set(archive.namelist())
                self.assertIn("manifest.json", names)
                self.assertIn("oot3d_scene_manifest.json", names)
                self.assertTrue(all(info.date_time == (1980, 1, 1, 0, 0, 0) for info in archive.infolist()))
                self.assertIn(
                    "scenes/overworld/spot04/oot3d/spot04_room_0/gOot3dSpot04Room0",
                    names,
                )
                self.assertIn(
                    "scenes/shared/spot04_scene/spot04_sceneCollisionHeader_008918",
                    names,
                )
            self.assertEqual(archive_path.read_bytes(), archive_path_2.read_bytes())


class CharacterConversionManifestTests(unittest.TestCase):
    def test_runtime_transform_uses_native_special_bone_translation(self) -> None:
        transform = runtime_transform_policy(
            {},
            {
                "skeleton_bind_translations": [
                    [0.0, 0.0, 0.0],
                    [0.0, 2156.320068359375, 0.0],
                ],
                "dynamic_position_bounds": {"y": {"min": 999.0}},
            },
            {
                "special_bone": 1,
                "base_translation": [0.0, 3538.080078125, 0.0],
            },
        )

        self.assertEqual(
            transform["visual_origin_offset"],
            [0.0, -1381.760009765625, 0.0],
        )
        self.assertEqual(
            transform["visual_origin_offset_source"],
            "cmb_special_bone_bind_translation_minus_player_init_base_translation",
        )

    def test_character_skel_anime_sampling_contract_preserves_native_masks(self) -> None:
        semantics = {
            "format": "oot3d_character_runtime_semantics_v1",
            "profile_id": "link_child",
            "skel_anime_sampling_contract": {
                "format": "oot3d_skel_anime_sampling_contract_v1",
                "frame_data_path": 1,
                "special_bone": 1,
                "default_channel_mask": 2,
                "special_bone_channel_mask": 3,
                "base_translation": [0.0, 3538.080078125, 0.0],
                "evidence": {"authority": "oot3d_code_bin"},
            },
        }

        contract = skel_anime_sampling_contract(semantics, "link_child")

        self.assertIsNotNone(contract)
        self.assertEqual(contract["frame_data_path"], 1)
        self.assertEqual(contract["special_bone"], 1)
        self.assertEqual(contract["default_channel_mask"], 2)
        self.assertEqual(contract["special_bone_channel_mask"], 3)
        self.assertEqual(contract["base_translation"], [0.0, 3538.080078125, 0.0])

    def test_character_animation_controller_contract_resolves_native_variants(self) -> None:
        semantics = {
            "format": "oot3d_character_runtime_semantics_v1",
            "profile_id": "link_child",
            "animation_controller_contract": {
                "format": "oot3d_character_animation_controller_contract_v1",
                "controllers": [
                    {
                        "id": "player_forward_locomotion",
                        "notification_semantic": "player_forward_locomotion",
                        "phase_source": "player_locomotion_cycle",
                        "source_frame_span": 29.0,
                        "blend_source": "player_linear_velocity",
                        "blend_threshold": 3.7,
                        "blend_scale": 0.8,
                        "warmup_source": "player_locomotion_blend_weight",
                        "variants": [
                            {
                                "animation_type_index": 0,
                                "walk_csab_name": "child/anim/nml_walk_free.csab",
                                "run_csab_name": "child/anim/nml_run_free.csab",
                            }
                        ],
                    }
                ],
                "evidence": {"authority": "oot3d_code_bin"},
            },
        }
        animations = [
            {"csab_name": "child/anim/nml_walk_free.csab"},
            {"csab_name": "child/anim/nml_run_free.csab"},
        ]

        contract = animation_controller_contract(semantics, "link_child", animations)

        self.assertEqual(contract["status"], "ready")
        controller = contract["controllers"][0]
        self.assertEqual(controller["id"], "player_forward_locomotion")
        self.assertEqual(controller["variants"][0]["animation_type_index"], 0)
        self.assertEqual(
            controller["variants"][0]["run_csab_name"],
            "child/anim/nml_run_free.csab",
        )

    def test_character_animation_semantic_bindings_are_native_table_driven(self) -> None:
        semantics = {
            "format": "oot3d_character_runtime_semantics_v1",
            "profile_id": "link_child",
            "animation_semantic_binding_contract": {
                "format": "oot3d_character_animation_semantic_binding_contract_v1",
                "bindings": [
                    {
                        "n64_name": "gPlayerAnim_link_normal_jump_climb_up",
                        "csab_name": "boy/anim/nml_hang_up.csab",
                        "oot3d_group_index": 49,
                        "csab_type_local_index": 146,
                        "animation_type_indices": [1, 2],
                    }
                ],
                "evidence": {"authority": "oot3d_code_bin_and_zar"},
            },
        }
        animations = [{"csab_name": "boy/anim/nml_hang_up.csab"}]

        contract = animation_semantic_binding_contract(semantics, "link_child", animations)

        self.assertEqual(contract["status"], "ready")
        self.assertEqual(contract["bindings"][0]["oot3d_group_index"], 49)
        self.assertEqual(contract["bindings"][0]["csab_name"], "boy/anim/nml_hang_up.csab")

    def test_character_root_motion_ownership_contract_preserves_native_pose(self) -> None:
        semantics = {
            "format": "oot3d_character_runtime_semantics_v1",
            "profile_id": "link_child",
            "root_motion_ownership_contract": {
                "format": "oot3d_character_root_motion_ownership_contract_v1",
                "movement_enabled_flag": 1,
                "update_y_flag": 2,
                "translation_policy": "align_only_controller_consumed_axes",
                "xz_ownership": "controller_when_movement_enabled",
                "y_ownership": "controller_when_movement_enabled_and_update_y",
                "root_rotation_policy": "preserve_authored_oot3d",
                "evidence": {"native_pose_authority": "test_fixture"},
            },
        }

        contract = root_motion_ownership_contract(semantics, "link_child")

        self.assertEqual(contract["status"], "ready")
        self.assertEqual(contract["movement_enabled_flag"], 1)
        self.assertEqual(contract["update_y_flag"], 2)
        self.assertEqual(contract["root_rotation_policy"], "preserve_authored_oot3d")

    def test_character_animation_time_source_contract_is_native_data_driven(self) -> None:
        semantics = {
            "format": "oot3d_character_runtime_semantics_v1",
            "profile_id": "link_child",
            "animation_time_source_contract": {
                "format": "oot3d_character_animation_time_source_contract_v1",
                "default_source": "skel_animation_clock",
                "bindings": [
                    {
                        "csab_name": "child/anim/nml_run_free.csab",
                        "source": "player_locomotion_cycle",
                        "sample_mode": "native_frame_span_over_source_span",
                        "source_frame_span": 29.0,
                        "sample_offset": 0.0,
                    }
                ],
                "evidence": {"authority": "oot3d_code_bin"},
            },
        }
        animations = [
            {
                "csab_name": "child/anim/nml_run_free.csab",
                "frame_slot_count": 20,
            }
        ]

        contract = animation_time_source_contract(semantics, "link_child", animations)

        self.assertEqual(contract["status"], "ready")
        self.assertEqual(contract["default_source"], "skel_animation_clock")
        self.assertEqual(contract["bindings"][0]["source_frame_span"], 29.0)
        self.assertAlmostEqual(contract["bindings"][0]["sample_scale"], 20.0 / 29.0)
        self.assertEqual(contract["bindings"][0]["native_frame_span"], 20)
        self.assertEqual(
            contract["bindings"][0]["sample_scale_source"],
            "native_csab_frame_slot_count_over_source_frame_span",
        )

    def test_character_animation_time_source_contract_normalizes_full_player_timeline(self) -> None:
        semantics = {
            "format": "oot3d_character_runtime_semantics_v1",
            "profile_id": "link_child",
            "animation_time_source_contract": {
                "format": "oot3d_character_animation_time_source_contract_v1",
                "default_source": "skel_animation_clock",
                "bindings": [
                    {
                        "csab_name": "boy/anim/nml_100step_up.csab",
                        "source": "player_skel_animation_timeline",
                        "sample_mode": "native_frame_span_over_source_span",
                        "source_frame_span": 18.0,
                        "sample_offset": 0.0,
                    }
                ],
                "evidence": {"authority": "oot3d_code_bin"},
            },
        }
        animations = [
            {
                "csab_name": "boy/anim/nml_100step_up.csab",
                "frame_slot_count": 28,
            }
        ]

        contract = animation_time_source_contract(semantics, "link_child", animations)
        binding = contract["bindings"][0]

        self.assertEqual(binding["sample_mode"], "direct_clamped_normalized_frame")
        self.assertAlmostEqual(binding["sample_scale"], 27.0 / 17.0)
        self.assertAlmostEqual(binding["scaffold_playback_scale"], 18.0 / 28.0)
        self.assertEqual(binding["playback_mode"], "once_full_span")

    def test_character_manifest_composes_skinned_and_auxiliary_payloads(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bind_export = root / "bind.json"
            bind_export.write_text(
                json.dumps(
                    {
                        "format": "oot3d_skinned_bind_pose_export_v1",
                        "skeleton_bones": [
                            {
                                "index": index,
                                "parent_index": index - 1 if index > 0 else -1,
                                "bind_world_translation": [0.0, float(index), 0.0],
                            }
                            for index in range(25)
                        ],
                        "meshes": [],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            track_export = root / "track.json"
            track_export.write_text(
                json.dumps(
                    {
                        "format": "oot3d_csab_skeleton_track_export_v1",
                        "tracks": [
                            {
                                "bone_index": bone_index,
                                "node_index": bone_index - 1,
                                "channels": (
                                    [
                                        {"semantic": "translation_x"},
                                        {"semantic": "translation_y"},
                                        {"semantic": "translation_z"},
                                    ]
                                    if bone_index == 1
                                    else [{"semantic": "rotation_x"}]
                                ),
                            }
                            for bone_index in range(1, 22)
                        ]
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )

            skinned_binding_path = root / "skinned_binding.json"
            skinned_binding_path.write_text(
                json.dumps(
                    {
                        "targets": [
                            {
                                "target_id": "zelda_link_child_new_zar_child_model_childlink_v2_cmb",
                                "archive_path": "zelda_link_child_new.zar",
                                "target_cmb_name": "child/model/childlink_v2.cmb",
                                "model_name": "childlink_v2",
                                "bone_count": 25,
                                "support_status_counts": {"needs_skinning_mode_2_support": 1},
                                "target_resolution_status_counts": {"single_bone_count_match": 1},
                                "bind_pose": {
                                    "export": str(bind_export),
                                    "package_entry": "objects/oot3d/skinned_bind_pose/link_child.json",
                                    "counts": {
                                        "mesh_count": 21,
                                        "skinned_primitive_count": 23,
                                        "validation_error_count": 0,
                                    },
                                },
                                "animations": [
                                    {
                                        "csab_name": "boy/anim/wait.csab",
                                        "track_export_file": str(track_export),
                                        "track_package_entry": "animations/oot3d/csab/skinned/wait.json",
                                        "frame_slot_count": 20,
                                        "target_resolution_status": "single_bone_count_match",
                                        "target_support_status": "needs_skinning_mode_2_support",
                                        "counts": {
                                            "track_count": 1,
                                            "channel_count": 3,
                                            "encoding_counts": {"keyed_s16_rotation": 3},
                                        },
                                        "validation": {"valid": True, "status": "valid"},
                                    }
                                ],
                            }
                        ]
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            animation_like_path = root / "animation_like.json"
            animation_like_path.write_text(
                json.dumps(
                    {
                        "records": [
                            {
                                "archive_path": "zelda_link_child_new.zar",
                                "embedded_name": "boy/anim/wait.faceb",
                                "type": "faceb",
                                "size": 12,
                                "metadata": {
                                    "magic_status": "fkb01",
                                    "size_match_status": "matches",
                                    "entry_count": 1,
                                },
                            },
                            {
                                "archive_path": "zelda_link_child_ultra.zar",
                                "embedded_name": "boy/anim/clink_demo.anb",
                                "type": "anb",
                                "size": 8,
                                "metadata": {"frame_count_candidate": 30},
                            },
                        ]
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            cmab_path = root / "cmab.json"
            cmab_path.write_text(
                json.dumps(
                    {
                        "records": [
                            {
                                "archive_path": "zelda_link_child_new.zar",
                                "cmab_name": "child/misc/childlink_eye.cmab",
                                "target_resolution_status": "multiple_cmb_unresolved",
                                "frame_count_candidate": 4,
                                "loop_mode_candidate": 0,
                                "layout": {"mmad_count": 2},
                            }
                        ]
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            pose_root = root / "pose_samples"
            pose_root.mkdir()
            pose_sample_path = pose_root / "wait.json"
            pose_sample_path.write_text(
                json.dumps(
                    {
                        "validation": {"valid": True, "status": "valid"},
                        "position_bounds": {
                            "x": {"min": -1.0, "max": 3.0},
                            "y": {"min": 2.0, "max": 8.0},
                            "z": {"min": -4.0, "max": 1.0},
                        },
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            pose_batch_path = root / "pose_batch.json"
            pose_batch_path.write_text(
                json.dumps(
                    {
                        "output": str(root),
                        "records": [
                            {
                                "status": "exported",
                                "archive_path": "zelda_link_child_new.zar",
                                "target_cmb_name": "child/model/childlink_v2.cmb",
                                "csab_name": "boy/anim/wait.csab",
                                "pose_sample_export": "pose_samples/wait.json",
                                "frame_count_candidate": 20,
                                "sample_frames": [0, 10, 20],
                                "counts": {
                                    "sampled_vertex_rows": 30,
                                    "finite_pose_rows": 30,
                                    "validation_error_count": 0,
                                    "max_position_delta_from_bind": 7.5,
                                },
                            }
                        ],
                    },
                    indent=2,
                )
                + "\n",
                encoding="utf-8",
            )
            n64_player_animation_xml = root / "gameplay_keep.xml"
            n64_player_animation_xml.write_text(
                """<Root>
    <File Name="gameplay_keep" Segment="4">
        <PlayerAnimation Name="gPlayerAnim_link_wait" Offset="0x2310"/>
        <PlayerAnimation Name="gPlayerAnim_link_jump" Offset="0x2318"/>
    </File>
</Root>
""",
                encoding="utf-8",
            )
            n64_player_animation_data_xml = root / "link_animetion.xml"
            n64_player_animation_data_xml.write_text(
                """<Root>
    <File Name="link_animetion" Segment="0">
        <PlayerAnimationData Name="gPlayerAnimData_000000" FrameCount="20" Offset="0x0"/>
        <PlayerAnimationData Name="gPlayerAnimData_000A80" FrameCount="15" Offset="0xA80"/>
    </File>
</Root>
""",
                encoding="utf-8",
            )
            n64_limb_names = [
                "gLinkChildRootLimb",
                "gLinkChildWaistLimb",
                "gLinkChildLowerControlLimb",
                "gLinkChildRightThighLimb",
                "gLinkChildRightShinLimb",
                "gLinkChildRightFootLimb",
                "gLinkChildLeftThighLimb",
                "gLinkChildLeftShinLimb",
                "gLinkChildLeftFootLimb",
                "gLinkChildUpperControlLimb",
                "gLinkChildHeadLimb",
                "gLinkChildHatLimb",
                "gLinkChildCollarLimb",
                "gLinkChildLeftshoulderLimb",
                "gLinkChildLeftForearmLimb",
                "gLinkChildLeftHandLimb",
                "gLinkChildRightshoulderLimb",
                "gLinkChildRightForearmLimb",
                "gLinkChildRightHandLimb",
                "gLinkChildSwordAndSheathLimb",
                "gLinkChildTorsoLimb",
            ]

            def o2r_string(value: str) -> bytes:
                encoded = value.encode("utf-8")
                return struct.pack("<i", len(encoded)) + encoded

            def o2r_resource(body: bytes) -> bytes:
                return b"\0" * 64 + body

            def n64_test_skeleton_resource() -> bytes:
                limb_paths = [f"objects/object_link_child/{name}" for name in n64_limb_names]
                body = struct.pack("<bbIIbI", 1, 2, len(limb_paths), 0, 2, len(limb_paths))
                body += b"".join(o2r_string(path) for path in limb_paths)
                return o2r_resource(body)

            def n64_test_limb_resource(index: int) -> bytes:
                child_index = index + 1 if index + 1 < len(n64_limb_names) else 255
                sibling_index = 255
                joint_x = (index % 3 - 1) * 25
                joint_y = 40 + index
                joint_z = (index % 5 - 2) * 15
                body = struct.pack("<bb", 2, 0)
                body += o2r_string("")
                body += struct.pack("<HI", 0, 0)
                body += o2r_string("")
                body += struct.pack("<fffHHH", 0.0, 0.0, 0.0, 0, 0, 0)
                body += o2r_string("")
                body += o2r_string("")
                body += o2r_string("")
                body += o2r_string("")
                body += struct.pack("<hhhBB", joint_x, joint_y, joint_z, child_index, sibling_index)
                return o2r_resource(body)

            n64_base_o2r = root / "oot.o2r"
            with zipfile.ZipFile(n64_base_o2r, "w") as archive:
                for data_name, frame_count, base_value in (
                    ("gPlayerAnimData_000000", 20, 10),
                    ("gPlayerAnimData_000A80", 15, 100),
                ):
                    values = [
                        base_value + (index % 200)
                        for index in range(frame_count * 67)
                    ]
                    resource = (
                        b"\0" * 64
                        + struct.pack("<I", len(values))
                        + struct.pack("<" + "h" * len(values), *values)
                    )
                    archive.writestr(f"misc/link_animetion/{data_name}", resource)
                archive.writestr("objects/object_link_child/gLinkChildSkel", n64_test_skeleton_resource())
                for index, limb_name in enumerate(n64_limb_names):
                    archive.writestr(
                        f"objects/object_link_child/{limb_name}",
                        n64_test_limb_resource(index),
                    )
            n64_link_object_xml = root / "object_link_child.xml"
            n64_link_object_xml.write_text(
                (
                    """<Root>
    <File Name="object_link_child" Segment="6">
        <Skeleton Name="gLinkChildSkel" Type="Flex" LimbType="LOD" Offset="0x2CF6C"/>
"""
                    + "\n".join(
                        f'        <Limb Name="{name}" LimbType="LOD" Offset="0x{0x2CDC8 + index * 0x10:X}"/>'
                        for index, name in enumerate(n64_limb_names)
                    )
                    + """
        <DList Name="gLinkChildWaistNearDL" Offset="0x202A8"/>
        <Texture Name="gLinkChildEyesOpenTex" Format="ci8" Width="64" Height="32" Offset="0x0"/>
    </File>
</Root>
"""
                ),
                encoding="utf-8",
            )
            n64_reference_audit_path = root / "n64_reference.json"
            n64_reference_audit = audit_n64_animation_reference(
                skinned_binding_path,
                n64_player_animation_xml,
                n64_link_object_xml,
                n64_reference_audit_path,
                n64_player_animation_data_xml_path=n64_player_animation_data_xml,
                n64_base_o2r_path=n64_base_o2r,
                oot3d_pose_batch_manifest_path=pose_batch_path,
                profile_id="link_child",
                model_archive="zelda_link_child_new.zar",
                model_cmb="child/model/childlink_v2.cmb",
                n64_skeleton_name="gLinkChildSkel",
                n64_strip_prefixes=["link_"],
            )

            manifest = export_character_conversion_manifest(
                skinned_binding_path,
                animation_like_path,
                cmab_path,
                root / "character.json",
                profile_id="link_child",
                model_archive="zelda_link_child_new.zar",
                model_cmb="child/model/childlink_v2.cmb",
                auxiliary_archives=["zelda_link_child_ultra.zar"],
                pose_batch_manifest_path=pose_batch_path,
                n64_reference_audit_path=n64_reference_audit_path,
            )
            character_package_path = root / "link_child_character_package.o2r"
            pack_character_conversion_manifest(
                root / "character.json",
                character_package_path,
                name="OOT3D Link Child Character Test",
                version="0.1.0",
            )
            character_package_audit = audit_character_conversion_package(
                root / "character.json",
                character_package_path,
                root / "character_package_audit.json",
            )

        self.assertEqual(n64_reference_audit["blocker_counts"]["total"], 0)
        self.assertEqual(n64_reference_audit["n64_reference"]["player_animation_data_count"], 2)
        self.assertEqual(
            n64_reference_audit["n64_reference"]["player_animation_data_count_status"],
            "matched_by_xml_order",
        )
        self.assertEqual(n64_reference_audit["n64_reference"]["player_animation_data_summary"]["frame_count_min"], 15)
        self.assertEqual(n64_reference_audit["n64_reference"]["player_animation_data_summary"]["frame_count_max"], 20)
        self.assertEqual(n64_reference_audit["n64_reference"]["player_animation_payload_decode"]["status"], "decoded")
        self.assertEqual(n64_reference_audit["n64_reference"]["player_animation_payload_decode"]["decoded_count"], 2)
        self.assertEqual(n64_reference_audit["n64_reference"]["player_animation_payload_decode"]["issue_count"], 0)
        self.assertEqual(
            n64_reference_audit["n64_reference"]["player_animation_payload_decode"]["frame_ir"]["limb_count"],
            22,
        )
        sample_frame = n64_reference_audit["n64_reference"]["player_animation_payload_decode"][
            "sample_decoded_payloads"
        ][0]["frame_sample_signatures"][0]
        self.assertEqual(len(sample_frame["limb_vec3s"]), 22)
        self.assertEqual(sample_frame["trailing_s16"], 76)
        self.assertEqual(n64_reference_audit["candidate_mapping"]["matched_oot3d_count"], 1)
        self.assertEqual(
            n64_reference_audit["candidate_mapping"]["status_counts"],
            {"single_normalized_key_match": 1},
        )
        self.assertEqual(
            n64_reference_audit["candidate_mapping"]["timing_status_counts"],
            {"same_frame_count": 1},
        )
        self.assertEqual(n64_reference_audit["time_normalized_pose_samples"]["status"], "sampled")
        self.assertEqual(n64_reference_audit["time_normalized_pose_samples"]["compared_match_count"], 1)
        self.assertEqual(n64_reference_audit["time_normalized_pose_samples"]["sample_pair_count"], 3)
        self.assertEqual(n64_reference_audit["time_normalized_pose_samples"]["issue_count"], 0)
        self.assertEqual(n64_reference_audit["limb_mapping"]["status"], "candidate_ready_for_metric")
        self.assertEqual(n64_reference_audit["limb_mapping"]["mapping_count"], 22)
        self.assertEqual(n64_reference_audit["limb_mapping"]["oot3d_root_motion_bone"], 1)
        self.assertEqual(n64_reference_audit["limb_mapping"]["oot3d_auxiliary_animated_bones"], [])
        self.assertEqual(n64_reference_audit["n64_reference"]["skeleton_pose_reference"]["status"], "decoded")
        self.assertEqual(n64_reference_audit["n64_reference"]["skeleton_pose_reference"]["limb_count"], 21)
        self.assertEqual(n64_reference_audit["n64_reference"]["skeleton_pose_reference"]["issue_count"], 0)
        self.assertEqual(n64_reference_audit["pose_error_metric"]["status"], "measured")
        self.assertEqual(n64_reference_audit["pose_error_metric"]["acceptance_status"], "requires_thresholds")
        self.assertEqual(n64_reference_audit["pose_error_metric"]["compared_match_count"], 1)
        self.assertEqual(n64_reference_audit["pose_error_metric"]["sample_pair_count"], 3)
        self.assertEqual(n64_reference_audit["pose_error_metric"]["measured_pair_count"], 3)
        self.assertEqual(n64_reference_audit["pose_error_metric"]["issue_count"], 0)
        self.assertEqual(manifest["format"], "oot3d_character_conversion_manifest_v1")
        self.assertEqual(manifest["target"]["status"], "resolved")
        self.assertEqual(manifest["csab_animation_tracks"]["count"], 1)
        self.assertEqual(
            manifest["auxiliary_animation_payloads"]["payload_type_counts"],
            {"anb": 1, "faceb": 1},
        )
        self.assertEqual(
            manifest["auxiliary_animation_payloads"]["stem_overlap"]["csab_faceb_overlap_count"],
            1,
        )
        self.assertEqual(manifest["material_animation_payloads"]["unresolved_or_ambiguous_count"], 1)
        self.assertFalse(manifest["runtime_readiness"]["cmab_material_binding_ready"])
        self.assertTrue(manifest["runtime_readiness"]["pose_volume_validation_ready"])
        self.assertEqual(manifest["pose_volume_validation"]["status"], "validated")
        self.assertEqual(manifest["pose_volume_validation"]["pose_export_count"], 1)
        self.assertEqual(manifest["pose_volume_validation"]["frame_count_min"], 20)
        self.assertEqual(manifest["pose_volume_validation"]["frame_count_max"], 20)
        self.assertEqual(manifest["pose_volume_validation"]["position_extent"], {"x": 4.0, "y": 6.0, "z": 5.0})
        self.assertEqual(manifest["pose_volume_validation"]["aabb_volume"], 120.0)
        self.assertEqual(
            manifest["n64_reference_validation"]["status"],
            "candidate_mapping_frame_counts_payload_decode_time_samples_limb_mapping_and_pose_metric_measured",
        )
        self.assertEqual(manifest["n64_reference_validation"]["matched_oot3d_count"], 1)
        self.assertEqual(manifest["n64_reference_validation"]["n64_player_animation_data_count"], 2)
        self.assertEqual(manifest["n64_reference_validation"]["n64_payload_decode_status"], "decoded")
        self.assertEqual(manifest["n64_reference_validation"]["n64_payload_decode_count"], 2)
        self.assertEqual(manifest["n64_reference_validation"]["n64_time_normalized_pose_sample_status"], "sampled")
        self.assertEqual(manifest["n64_reference_validation"]["n64_time_normalized_compared_match_count"], 1)
        self.assertEqual(manifest["n64_reference_validation"]["n64_time_normalized_sample_pair_count"], 3)
        self.assertEqual(manifest["n64_reference_validation"]["n64_time_normalized_pose_sample_issue_count"], 0)
        self.assertEqual(manifest["n64_reference_validation"]["n64_limb_mapping_status"], "candidate_ready_for_metric")
        self.assertEqual(manifest["n64_reference_validation"]["n64_limb_mapping_count"], 22)
        self.assertEqual(manifest["n64_reference_validation"]["n64_limb_mapping_issue_count"], 0)
        self.assertEqual(manifest["n64_reference_validation"]["n64_skeleton_pose_reference_status"], "decoded")
        self.assertEqual(manifest["n64_reference_validation"]["n64_skeleton_pose_reference_limb_count"], 21)
        self.assertEqual(manifest["n64_reference_validation"]["n64_pose_error_metric_status"], "measured")
        self.assertEqual(
            manifest["n64_reference_validation"]["n64_pose_error_metric_acceptance_status"],
            "requires_thresholds",
        )
        self.assertEqual(manifest["n64_reference_validation"]["n64_pose_error_metric_compared_match_count"], 1)
        self.assertEqual(manifest["n64_reference_validation"]["n64_pose_error_metric_sample_pair_count"], 3)
        self.assertEqual(manifest["n64_reference_validation"]["n64_pose_error_metric_measured_pair_count"], 3)
        self.assertEqual(manifest["n64_reference_validation"]["n64_pose_error_metric_issue_count"], 0)
        self.assertEqual(manifest["n64_reference_validation"]["timing_status_counts"], {"same_frame_count": 1})
        self.assertTrue(manifest["runtime_readiness"]["n64_reference_candidate_mapping_ready"])
        self.assertTrue(manifest["runtime_readiness"]["n64_reference_frame_counts_ready"])
        self.assertTrue(manifest["runtime_readiness"]["n64_reference_payload_decode_ready"])
        self.assertTrue(manifest["runtime_readiness"]["n64_time_normalized_pose_samples_ready"])
        self.assertTrue(manifest["runtime_readiness"]["n64_limb_mapping_ready"])
        self.assertTrue(manifest["runtime_readiness"]["n64_pose_error_metric_measured"])
        self.assertTrue(manifest["runtime_readiness"]["n64_numeric_pose_comparison_ready"])
        self.assertEqual(manifest["issue_counts"]["cmab_target_unresolved_or_ambiguous"], 1)
        self.assertEqual(character_package_audit["format"], "oot3d_character_conversion_package_audit_v1")
        self.assertEqual(character_package_audit["profile_id"], "link_child")
        self.assertTrue(character_package_audit["runtime_profile_valid"])
        self.assertEqual(character_package_audit["runtime_profile_resource_status"], "matched")
        self.assertEqual(character_package_audit["expected_resource_count"], 2)
        self.assertEqual(character_package_audit["expected_unique_resource_count"], 2)
        self.assertEqual(character_package_audit["resource_entry_count"], 2)
        self.assertEqual(
            character_package_audit["resource_kind_counts_expected"],
            {"bind_pose": 1, "csab_track": 1},
        )
        self.assertEqual(
            character_package_audit["archived_resource_format_counts"],
            {
                "oot3d_csab_skeleton_track_export_v1": 1,
                "oot3d_skinned_bind_pose_export_v1": 1,
            },
        )
        self.assertEqual(character_package_audit["archive_entry_count"], 7)
        self.assertEqual(character_package_audit["issue_counts"]["total"], 6)
        self.assertEqual(character_package_audit["issue_counts"]["character_manifest_issue"], 1)
        self.assertEqual(character_package_audit["issue_counts"]["invalid_runtime_skinning_contract"], 1)
        self.assertEqual(
            character_package_audit["issue_counts"]["invalid_runtime_selected_draw_skinning_contract"],
            1,
        )
        self.assertEqual(
            character_package_audit["issue_counts"]["invalid_runtime_native_bind_pose_contract"],
            1,
        )
        self.assertEqual(
            character_package_audit["issue_counts"]["invalid_runtime_material_animation_contract"],
            1,
        )
        self.assertEqual(character_package_audit["issue_counts"]["invalid_runtime_face_state_contract"], 1)


if __name__ == "__main__":
    unittest.main()
