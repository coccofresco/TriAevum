#!/usr/bin/env python3
"""Build a source-oriented table for the OOT3D title intro target.

The target is the first title sequence in Hyrule Field: the title-specific
spot99 scene/room, spot00 external demo QDB data, the title logo, and
Link/Epona riding in the distance. This is separate from the regular scene
cutscene/transition handoff path.
"""

from __future__ import annotations

import csv
import json
import math
import re
import struct
import sys
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ASSET_TOOL_SRC = ROOT.parent / "oot3d_asset_tool" / "src"
if ASSET_TOOL_SRC.is_dir():
    sys.path.insert(0, str(ASSET_TOOL_SRC))

from oot3d_asset_tool.actor_inventory import csab_header_candidates
from oot3d_asset_tool.binary import BinaryView
from oot3d_asset_tool.cmb import CmbModel
from oot3d_asset_tool.zar import ZarArchive
from oot3d_asset_tool.zsi_cutscene_audit import parse_oot3d_native_cutscene_block


DEFAULT_ROMFS_ROOT = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\romfs")
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
DEFAULT_OUT_JSON = ROOT / "analysis" / "title_intro_source_table.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "title_intro_source_table.md"
DEFAULT_OUT_ASSET_CSV = ROOT / "analysis" / "title_intro_asset_table.csv"
DEFAULT_OUT_QDB_CSV = ROOT / "analysis" / "title_intro_scene_zar_qdb_table.csv"
DEFAULT_OUT_COMMAND_CSV = ROOT / "analysis" / "title_intro_scene_zar_qdb_command_table.csv"
DEFAULT_OUT_ACTOR_CUE_CSV = ROOT / "analysis" / "title_intro_epona_actor_cue_table.csv"
DEFAULT_OUT_TITLE_LOGO_ACTOR_INIT_CSV = ROOT / "analysis" / "title_intro_logo_actor_init_table.csv"
DEFAULT_OUT_TITLE_LOGO_COMPONENT_CSV = ROOT / "analysis" / "title_intro_logo_component_table.csv"
DEFAULT_OUT_TITLE_LOGO_DRAW_CSV = ROOT / "analysis" / "title_intro_logo_draw_table.csv"
DEFAULT_OUT_TITLE_LOGO_DRAW_CONTEXT_CSV = ROOT / "analysis" / "title_intro_logo_draw_context_table.csv"
DEFAULT_OUT_TITLE_LOGO_UPDATE_CSV = ROOT / "analysis" / "title_intro_logo_update_table.csv"
DEFAULT_OUT_TITLE_ACTOR_SCALE_CSV = ROOT / "analysis" / "title_intro_actor_scale_table.csv"
DEFAULT_OUT_TITLE_ACTOR_ANIMATION_CSV = ROOT / "analysis" / "title_intro_actor_animation_table.csv"
DEFAULT_OUT_TITLE_ACTOR_MOTION_ANIMATION_CSV = ROOT / "analysis" / "title_intro_actor_motion_animation_table.csv"
DEFAULT_OUT_TITLE_HORSE_STATE_ROUTE_CSV = ROOT / "analysis" / "title_intro_horse_state_route_table.csv"
DEFAULT_OUT_TITLE_HORSE_CUTSCENE_ACTION_ROUTE_CSV = (
    ROOT / "analysis" / "title_intro_horse_cutscene_action_route_table.csv"
)
DEFAULT_OUT_TITLE_SKEL_ANIME_TIMING_CSV = (
    ROOT / "analysis" / "title_intro_skel_anime_timing_table.csv"
)
DEFAULT_OUT_TITLE_MOUNTED_PLAYER_FRAME_CSV = (
    ROOT / "analysis" / "title_intro_mounted_player_animation_frame_table.csv"
)
DEFAULT_OUT_TITLE_LINK_CHILD_STATE_ROUTE_CSV = ROOT / "analysis" / "title_intro_link_child_state_route_table.csv"
DEFAULT_OUT_TITLE_LINK_BOY_PLAYER_ACTION_CSV = ROOT / "analysis" / "title_intro_link_boy_player_action_table.csv"
DEFAULT_OUT_SYMBOL_CSV = ROOT / "analysis" / "title_intro_runtime_symbol_candidates.csv"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "title_intro_source_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_title_intro_source_table.c"
NO_INDEX = 0xFFFF
NO_OFFSET = 0xFFFFFFFF

CODE_BIN_RUNTIME_BASE = 0x00100000
ENMAG_DESTROY_ADDR = 0x0018CF1C
ENMAG_INIT_ADDR = 0x0018CBB8
ENMAG_UPDATE_ADDR = 0x001DA9F8
ENMAG_DRAW_ADDR = 0x001DA4F4
ENMAG_DRAW_BASE_TRANSLATE_Z_ADDR = 0x001DA8A4
ENMAG_DRAW_ONE_ADDR = 0x001DA8A8
ENMAG_DRAW_SMALL_DEPTH_OFFSET_ADDR = 0x001DA8AC
ENMAG_DRAW_COPYRIGHT_TRANSLATE_Y_ADDR = 0x001DA8B0
ENMAG_DRAW_ALPHA_SCALE_ADDR = 0x001DA8B4
ENMAG_DRAW_RENDERER_FLAG_POINTER_ADDR = 0x001DA8B8
ENMAG_DRAW_RENDER_CONTEXT_POINTER_ADDR = 0x001DA8BC
ENMAG_DRAW_TITLE_TEXT_COLOR_POINTER_ADDR = 0x001DA8C0
ENMAG_DRAW_MAIN_COLOR_POINTER_ADDR = 0x001DA8CC
ENMAG_DRAW_MAIN_LIGHT_BLOCK_POINTER_ADDR = 0x001DA8D0
ENMAG_DRAW_EFFECT_VECTOR_DOUBLE_SCALE_ADDR = 0x001DA8D4
ENMAG_DRAW_EFFECT_VECTOR_HALF_SCALE_ADDR = 0x001DA8D8
ENMAG_DRAW_EFFECT_VECTOR_BIAS_ADDR = 0x001DA8DC
ENMAG_DRAW_ALTERNATE_RENDER_CONTEXT_POINTER_ADDR = 0x001DA9F0
ENMAG_DRAW_COPYRIGHT_COLOR_POINTER_ADDR = 0x001DA9F4

ENMAG_DRAW_MATERIAL_HANDLE_FUNCTION = 0x003687A8
ENMAG_DRAW_MATERIAL_SLOT_SELECT_FUNCTION = 0x003589CC
ENMAG_DRAW_MATERIAL_COLOR_APPLY_FUNCTION = 0x00358964
ENMAG_DRAW_MATRIX_COPY_FUNCTION = 0x003721E0
ENMAG_DRAW_SUBMIT_FUNCTION = 0x0033D220
ENMAG_DRAW_SMALL_QUEUE_RECORD_WRITE_FUNCTION = 0x0031487C

ACTOR_SET_SCALE_FUNCTION = 0x0037572C
TITLE_ACTOR_SCALE_DECODE_STATUS = "decoded_from_actor_setscale_code_bin_literals_and_ghidra_callsite"
TITLE_ACTOR_ANIMATION_BINDING_DECODE_STATUS = (
    "native_actor_zar_model_and_init_animation_table_decoded_title_cue_clip_binding_pending_oot3d_cutscene_consumer"
)
TITLE_ACTOR_MOTION_ANIMATION_DECODE_STATUS = (
    "decoded_from_oot3d_enhorse_update_ingo_horse_anim_speed_state_table"
)
TITLE_HORSE_STATE_ROUTE_DECODE_STATUS = "decoded_from_oot3d_enhorse_state_route_code_bin_and_ghidra_exports"
TITLE_HORSE_CUTSCENE_ACTION_ROUTE_DECODE_STATUS = (
    "decoded_from_oot3d_title_horse_cutscene_dispatch_code_bin_and_native_csab_table"
)
TITLE_LINK_CHILD_STATE_ROUTE_DECODE_STATUS = (
    "decoded_from_oot3d_enhorse_link_child_update_action_table_ghidra_exports_and_n64_structural_reference"
)
TITLE_LINK_BOY_PLAYER_ACTION_DECODE_STATUS = (
    "decoded_from_oot3d_spot00_demo_epona_qdb_cs_cmd_set_player_action_player_timeline"
)
ENHORSE_LINK_CHILD_UPDATE_FUNCTION = 0x001D8D7C
ENHORSE_LINK_CHILD_INIT_FUNCTION = 0x0018BF6C
ENHORSE_LINK_CHILD_DRAW_FUNCTION = 0x0022AAE4
ENHORSE_LINK_CHILD_ACTION_FIELD_OFFSET = 0x01A4
ENHORSE_LINK_CHILD_ANIMATION_INDEX_FIELD_OFFSET = 0x01A5
ENHORSE_LINK_CHILD_SPEED_FIELD_OFFSET = 0x006C
ENHORSE_LINK_CHILD_SKEL_ANIME_FIELD_OFFSET = 0x01B8
ENHORSE_LINK_CHILD_ACTION_TABLE_POINTER_ADDR = 0x001D8FC4
ENHORSE_LINK_CHILD_ANIMATION_TABLE_POINTER_ADDR = 0x0018C1CC
ENHORSE_UPDATE_INGO_HORSE_ANIM_FUNCTION = 0x0033D88C
ENHORSE_UPDATE_INGO_HORSE_ANIM_ZERO_SPEED_ADDR = 0x0033DA58
ENHORSE_UPDATE_INGO_HORSE_ANIM_BASE_PLAY_SPEED_SCALE_ADDR = 0x0033DA5C
ENHORSE_UPDATE_INGO_HORSE_ANIM_IDLE_PLAY_SPEED_SCALE_ADDR = 0x0033DA60
ENHORSE_UPDATE_INGO_HORSE_ANIM_WALK_THRESHOLD_ADDR = 0x0033DA64
ENHORSE_UPDATE_INGO_HORSE_ANIM_WALK_PLAY_SPEED_SCALE_ADDR = 0x0033DA68
ENHORSE_UPDATE_INGO_HORSE_ANIM_FAST_THRESHOLD_ADDR = 0x0033DA6C
ENHORSE_UPDATE_INGO_HORSE_ANIM_AUDIO_ID_ADDR = 0x0033DA70
ENHORSE_UPDATE_INGO_HORSE_ANIM_TROT_PLAY_SPEED_SCALE_ADDR = 0x0033DA74
ENHORSE_UPDATE_INGO_HORSE_ANIM_AUDIO_FREQ_ADDR = 0x0033DA78
ENHORSE_UPDATE_INGO_HORSE_ANIM_AUDIO_VOL_ADDR = 0x0033DA7C
ENHORSE_UPDATE_INGO_HORSE_ANIM_FAST_PLAY_SPEED_SCALE_ADDR = 0x0033DA80
ENHORSE_UPDATE_INGO_HORSE_ANIM_TABLE_POINTER_ADDR = 0x0033DA84
ENHORSE_UPDATE_INGO_HORSE_ANIM_CHANGE_SAME_STATE_CALLSITE = 0x0033DA04
ENHORSE_UPDATE_INGO_HORSE_ANIM_CHANGE_CHANGED_STATE_CALLSITE = 0x0033DA54
ENHORSE_START_MOVING_ANIMATION_FUNCTION = 0x0031C588
ENHORSE_START_MOVING_ANIMATION_TABLE_POINTER_ADDR = 0x0031C690
ENHORSE_SET_FOLLOW_ANIMATION_FUNCTION = 0x003352C8
ENHORSE_SET_FOLLOW_PLAYER_ACTOR_OFFSET_ADDR = 0x00335394
ENHORSE_SET_FOLLOW_FAR_DISTANCE_ADDR = 0x00335398
ENHORSE_SET_FOLLOW_NEAR_DISTANCE_U32_DELTA = 0x00320000
ENHORSE_IDLE_FUNCTION = 0x00390F98
ENHORSE_IDLE_ZERO_SPEED_ADDR = 0x003911DC
ENHORSE_IDLE_DREG_TRIGGER_ADDR = 0x003911E0
ENHORSE_IDLE_MIN_ANGLE_ADDR = 0x003911E4
ENHORSE_IDLE_NEIGH_SOUND_ID_ADDR = 0x003911E8
ENHORSE_IDLE_MIN_DISTANCE_ADDR = 0x003911EC
ENHORSE_IDLE_MORPH_FRAMES_ADDR = 0x003911F8
ENHORSE_UPDATE_SPEED_FUNCTION = 0x003182CC
ENHORSE_MOUNTED_IDLE_FUNCTION = 0x003A1538
ENHORSE_MOUNTED_WALK_FUNCTION = 0x003A1778
ENHORSE_MOUNTED_WALK_SPEED_ADDR = 0x003A1A8C
ENHORSE_MOUNTED_WALK_DELAY_ADDR = 0x003A1A90
ENHORSE_MOUNTED_WALK_UPDATE_ARG_ADDR = 0x003A1A94
ENHORSE_MOUNTED_WALK_ZERO_SPEED_ADDR = 0x003A1AA8
ENHORSE_MOUNTED_WALK_TO_TROT_THRESHOLD_ADDR = 0x003A1AAC
ENHORSE_MOUNTED_WALK_TABLE_POINTER_ADDR = 0x003A1AB4
ENHORSE_MOUNTED_WALK_INPUT_MAG_ADDR = 0x003A1AC0
ENHORSE_MOUNTED_TROT_FUNCTION = 0x003A83B4
ENHORSE_MOUNTED_TROT_UPDATE_ARG_ADDR = 0x003A86B4
ENHORSE_MOUNTED_TROT_TO_WALK_THRESHOLD_ADDR = 0x003A86CC
ENHORSE_MOUNTED_TROT_TABLE_POINTER_ADDR = 0x003A86D8
ENHORSE_MOUNTED_TROT_TO_GALLOP_THRESHOLD_ADDR = 0x003A86FC
ENHORSE_MOUNTED_GALLOP_FUNCTION = 0x003A7E14
ENHORSE_MOUNTED_GALLOP_FORCED_SPEED_ADDR = 0x003A816C
ENHORSE_MOUNTED_GALLOP_UPDATE_ARG_ADDR = 0x003A8170
ENHORSE_MOUNTED_GALLOP_CONTROL_BLOCK_POINTER_ADDR = 0x003A8184
ENHORSE_MOUNTED_GALLOP_PLAY_SPEED_SCALE_ADDR = 0x003A8188
ENHORSE_MOUNTED_GALLOP_SOUND_FIELD_OFFSET_ADDR = 0x003A818C
ENHORSE_MOUNTED_GALLOP_SOUND_REVERB_ADDR = 0x003A8190
ENHORSE_MOUNTED_GALLOP_SOUND_FREQ_ADDR = 0x003A8194
ENHORSE_MOUNTED_GALLOP_SOUND_ID_ADDR = 0x003A8198
ENHORSE_MOUNTED_GALLOP_BRAKE_INPUT_MAG_ADDR = 0x003A81A4
ENHORSE_MOUNTED_GALLOP_DOWNSHIFT_SPEED_ADDR = 0x003A81A8
ENHORSE_ACTION_FIELD_OFFSET = 0x01A4
ENHORSE_SPEED_FIELD_OFFSET = 0x006C
ENHORSE_ANIMATION_INDEX_FIELD_OFFSET = 0x0E74
ENHORSE_HORSE_TYPE_FIELD_OFFSET = 0x01B0
ENHORSE_SKEL_ANIME_FIELD_OFFSET = 0x01C4
ENHORSE_FOLLOW_TIMER_FIELD_OFFSET = 0x0EB4
ENHORSE_CUTSCENE_DISPATCH_FUNCTION = 0x0026A30C
ENHORSE_CUTSCENE_ACTION_MAP_POINTER_ADDR = 0x0026A594
ENHORSE_CUTSCENE_INIT_TABLE_POINTER_ADDR = 0x0026A598
ENHORSE_CUTSCENE_UPDATE_TABLE_POINTER_ADDR = 0x0026A59C
ENHORSE_CUTSCENE_ACTION_MAP_ENTRY_COUNT = 5
ENHORSE_CUTSCENE_ANIMATION_TABLE_OUTER_RUNTIME_ADDR = 0x005265E8
ENHORSE_CUTSCENE_STATE1_PLAY_SPEED_SCALE_ADDR = 0x0016CB20
ENHORSE_CUTSCENE_STATE1_YAW_STEP_ADDR = 0x003CF4F4
ENHORSE_CUTSCENE_STATE1_SPEED_ADDR = 0x003CF4F8
ENHORSE_CUTSCENE_STATE4_PLAY_SPEED_SCALE_ADDR = 0x002A8BBC
ENHORSE_CUTSCENE_STATE4_YAW_STEP_ADDR = 0x00230EB4
ENHORSE_CUTSCENE_STATE4_SPEED_ADDR = 0x00230EB8
ENHORSE_CUTSCENE_STATE4_SELECTOR_WORD_INDEX = 10
ENHORSE_CUTSCENE_STATE4_SELECTOR_BITS = 0x42200000
ENHORSE_CUTSCENE_STATE5_PLAY_SPEED_ADDR = 0x002B6D48
ENHORSE_CUTSCENE_STATE5_COMPLETION_PLAY_SPEED_ADDR = 0x00253754
ENHORSE_CUTSCENE_STATE5_COMPLETION_MORPH_ADDR = 0x00253758
ANIMATION_PLAY_ONCE_SET_SPEED_MODE_MOV_ADDR = 0x00374270
ANIMATION_PLAY_ONCE_SET_SPEED_MORPH_LITERAL_ADDR = 0x0037427C
ENHORSE_CUTSCENE_STATE5_INITIAL_MODE_MOV_ADDR = 0x002B6D08
ENHORSE_CUTSCENE_STATE5_INITIAL_MORPH_LITERAL_ADDR = 0x002B6D40
ENHORSE_CUTSCENE_STATE5_COMPLETION_FIRST_MODE_MOV_ADDR = 0x002536C4
ENHORSE_CUTSCENE_STATE5_COMPLETION_LOOP_MODE_MOV_ADDR = 0x00253704
SKEL_ANIME_SET_UPDATE_FUNCTION = 0x00320D28
SKEL_ANIME_UPDATE_FUNCTION = 0x0036B4EC
SKEL_ANIME_UPDATE_SCALE_LITERAL_ADDR = 0x0036B808
SKEL_ANIME_GLOBAL_CONTEXT_POINTER_ADDR = 0x0051B2F4
SKEL_ANIME_GLOBAL_UPDATE_RATE_OFFSET = 0x0110
GAME_STATE_INIT_FUNCTION = 0x00416F60
GAME_STATE_INIT_UPDATE_RATE_MOV_ADDR = 0x00416FF0
MOUNTED_PLAYER_FRAME_FUNCTION = 0x004C5510
MOUNTED_PLAYER_FRAME_SUPPORTED_INDEX_CMP_ADDRS = (
    0x004C5518,
    0x004C551C,
    0x004C5520,
    0x004C5524,
    0x004C5528,
    0x004C5530,
)
MOUNTED_PLAYER_FRAME_SCALE_LITERAL_ADDR = 0x004C5558
MOUNTED_PLAYER_FRAME_BIAS_LITERAL_ADDR = 0x004C555C
MOUNTED_PLAYER_FRAME_LOAD_INSTRUCTION_ADDR = 0x004C552C
MOUNTED_PLAYER_ACTION_FUNCTION = 0x002B7FD0
MOUNTED_PLAYER_FRAME_STORE_INSTRUCTION_ADDR = 0x002B84C0
MOUNTED_PLAYER_HORSE_ANIMATION_INDEX_OFFSET = 0x0E74
MOUNTED_PLAYER_HORSE_ANIMATION_FRAME_OFFSET = 0x0E78
MOUNTED_PLAYER_SKEL_ANIME_OFFSET = 0x0254
MOUNTED_PLAYER_SKEL_ANIME_CURRENT_FRAME_OFFSET = 0x003C
ENMAG_DRAW_LIGHT_CONFIG_RESET_FUNCTION = 0x0033D200
ENMAG_DRAW_LIGHT_CONFIG_APPLY_FUNCTION = 0x0033D174
ENMAG_DRAW_LIGHT_VECTOR_APPLY_FUNCTION = 0x0033D14C
ENMAG_DRAW_LAZY_GUARD_FUNCTION = 0x003679B4
ENMAG_DRAW_LAZY_INIT_FUNCTION = 0x0036788C
ENMAG_UPDATE_MAX_ALPHA_ADDR = 0x001DAD00
ENMAG_UPDATE_MIN_ALPHA_ADDR = 0x001DAD04
ENMAG_UPDATE_MAIN_ALPHA_STEP_ADDR = 0x001DB030
ENMAG_UPDATE_TITLE_TEXT_EFFECT_ALPHA_STEP_ADDR = 0x001DB034
ENMAG_UPDATE_COPYRIGHT_ALPHA_CLAMP_ADDR = 0x001DB038
ENMAG_UPDATE_FLAGS_GET_ENV_FUNCTION = 0x0035A3C4
ENMAG_UPDATE_GET_CSAB_BY_INDEX_FUNCTION = 0x0034807C
ENMAG_UPDATE_SET_CSAB_FUNCTION = 0x00348068
ENMAG_UPDATE_SET_CSAB_FRAME_FUNCTION = 0x00348054
ENMAG_UPDATE_SET_TITLE_ANIM_STATE_FUNCTION = 0x00347FBC
ENMAG_UPDATE_AUDIO_CUTSCENE_FLAG_FUNCTION = 0x0033D13C
ENMAG_UPDATE_AUDIO_PLAY_SOUND_FUNCTION = 0x0037547C
ENMAG_INIT_COPYRIGHT_ALPHA_STEP_DEFAULT = 6
ENMAG_INIT_FADE_OUT_ALPHA_STEP_DEFAULT = 10
ENMAG_UPDATE_TRANSITION_COPYRIGHT_ALPHA_STEP = 15
ENMAG_UPDATE_TRANSITION_FADE_OUT_ALPHA_STEP = 25
NATIVE_GLOBAL_CONTEXT_RUNTIME_ADDRESS = 0x005BE5B8
NATIVE_SUBMIT_MANAGER_RUNTIME_ADDRESS = 0x005BE738
NATIVE_GLOBAL_CONTEXT_SUBMIT_MANAGER_OFFSET = 0x0180
NATIVE_SUBMIT_MANAGER_CONSTRUCTOR_FUNCTION = 0x0041706C
NATIVE_SUBMIT_MANAGER_VTABLE_ADDRESS = 0x004EBD78
NATIVE_SUBMIT_MANAGER_STORAGE_SIZE = 0x2170
NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_COUNT_OFFSET = 0x212C
NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_STORAGE_OFFSET = 0x2130
NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_CAPACITY = 8
NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_RECORD_STRIDE = 8
NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_RECORD_STATE_BYTE_OFFSET = 4
NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_RECORD_STATE_BYTE_VALUE = 0
NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_DRAIN_FUNCTION = 0x0042250C
NATIVE_SUBMIT_MANAGER_PASS0_DRAIN_FUNCTION = 0x002FAE10
NATIVE_SUBMIT_MANAGER_PASS1_DRAIN_FUNCTION = 0x002FAD2C
NATIVE_DRAW_HANDLE_VTABLE_SUBMIT_SLOT_OFFSET = 0x0008
ENMAG_MATERIAL_ANIMATION_RUNTIME_OWNER_FIELD_OFFSET = 0x000C
ENMAG_MATERIAL_ANIMATION_RUNTIME_LOOP_FIELD_OFFSET = 0x0010
ENMAG_MATERIAL_ANIMATION_RUNTIME_INIT_FUNCTION = 0x00372D94
ENMAG_MATERIAL_ANIMATION_RUNTIME_STEP_FUNCTION = 0x00373BEC

TITLE_LOGO_COMPONENT_BINDINGS = [
    {
        "component_role": "title_logo_main",
        "handle_field_offset": 0x01A4,
        "alpha_field_offset": 0x01D4,
        "cmb_index_jpeu": 2,
        "csab_index_jpeu": 1,
        "cmb_index_us": 3,
        "csab_index_us": 2,
        "binding_basis": "EnMag_Init selects ZAR_GetCMBByIndex(resource, version in {0,2} ? 2 : 3), stores handle at actor+0x1A4, stores CSAB selector at actor+0x1CE, and EnMag_Draw consumes alpha actor+0x1D4.",
    },
    {
        "component_role": "title_text_g_title",
        "handle_field_offset": 0x01A8,
        "alpha_field_offset": 0x01D0,
        "cmb_index_jpeu": 0,
        "csab_index_jpeu": NO_INDEX,
        "cmb_index_us": 0,
        "csab_index_us": NO_INDEX,
        "cmab_index": 7,
        "cmab_name": "Misc/g_title_fire.cmab",
        "material_animation_runtime_owner_field_offset": ENMAG_MATERIAL_ANIMATION_RUNTIME_OWNER_FIELD_OFFSET,
        "material_animation_runtime_loop_field_offset": ENMAG_MATERIAL_ANIMATION_RUNTIME_LOOP_FIELD_OFFSET,
        "material_animation_runtime_loop_override_valid": 1,
        "material_animation_runtime_loop_mode": 1,
        "material_animation_runtime_init_function": ENMAG_MATERIAL_ANIMATION_RUNTIME_INIT_FUNCTION,
        "material_animation_runtime_step_function": ENMAG_MATERIAL_ANIMATION_RUNTIME_STEP_FUNCTION,
        "material_animation_binding_basis": "EnMag_Init obtains the native CMAB payload for the actor archive, calls FUN_00372D94 on g_title handle+0x0C, then writes 1 to material-animation runtime+0x10. FUN_00373BEC advances that runtime and treats a nonzero byte at +0x10 as loop mode. The archive CMAB type index/name pair is 7/Misc/g_title_fire.cmab.",
        "binding_basis": "EnMag_Init loads ZAR_GetCMBByIndex(resource, 0), stores handle at actor+0x1A8, binds the native g_title_fire CMAB runtime, and EnMag_Draw consumes alpha actor+0x1D0.",
    },
    {
        "component_role": "copyright_copy_nintendo",
        "handle_field_offset": 0x01AC,
        "alpha_field_offset": 0x01D8,
        "cmb_index_jpeu": 1,
        "csab_index_jpeu": NO_INDEX,
        "cmb_index_us": 1,
        "csab_index_us": NO_INDEX,
        "binding_basis": "EnMag_Init loads ZAR_GetCMBByIndex(resource, 1), stores handle at actor+0x1AC, and EnMag_Draw consumes alpha actor+0x1D8.",
    },
]

TITLE_LOGO_DRAW_BINDINGS = [
    {
        "component_role": "title_text_g_title",
        "submit_order": 0,
        "handle_field_offset": 0x01A8,
        "alpha_field_offset": 0x01D0,
        "effect_alpha_field_offset": NO_INDEX,
        "matrix_role": "title_text_local_z_0_01_composed_with_base_z_minus_34",
        "color_pointer_address": ENMAG_DRAW_TITLE_TEXT_COLOR_POINTER_ADDR,
        "light_block_pointer_address": NO_OFFSET,
        "matrix_source_addresses": f"0x{ENMAG_DRAW_BASE_TRANSLATE_Z_ADDR:08x};0x{ENMAG_DRAW_SMALL_DEPTH_OFFSET_ADDR:08x}",
        "visibility_condition": "handle actor+0x1A8 non-null and actor+0x1D0 alpha > 0",
        "draw_basis": "OOT3D EnMag_Draw gets material handle, selects slot 5, applies RGBA using actor+0x1D0*1/255, copies the composed matrix to handle+0x7C, sets handle+0xAC, and submits with FUN_0033d220.",
    },
    {
        "component_role": "title_logo_main",
        "submit_order": 1,
        "handle_field_offset": 0x01A4,
        "alpha_field_offset": 0x01D4,
        "effect_alpha_field_offset": 0x01DC,
        "matrix_role": "title_logo_base_z_minus_34",
        "color_pointer_address": ENMAG_DRAW_MAIN_COLOR_POINTER_ADDR,
        "light_block_pointer_address": ENMAG_DRAW_MAIN_LIGHT_BLOCK_POINTER_ADDR,
        "matrix_source_addresses": f"0x{ENMAG_DRAW_BASE_TRANSLATE_Z_ADDR:08x}",
        "visibility_condition": "handle actor+0x1A4 non-null and actor+0x1D4 alpha > 0",
        "draw_basis": "OOT3D EnMag_Draw applies the main logo slot-5 RGBA, light block/vector helpers driven by actor+0x1DC, copies the base z matrix to handle+0x7C, sets handle+0xAC, and submits with FUN_0033d220.",
    },
    {
        "component_role": "copyright_copy_nintendo",
        "submit_order": 2,
        "handle_field_offset": 0x01AC,
        "alpha_field_offset": 0x01D8,
        "effect_alpha_field_offset": NO_INDEX,
        "matrix_role": "copyright_local_y_minus_11_z_0_01_composed_with_base_z_minus_34",
        "color_pointer_address": ENMAG_DRAW_COPYRIGHT_COLOR_POINTER_ADDR,
        "light_block_pointer_address": NO_OFFSET,
        "matrix_source_addresses": f"0x{ENMAG_DRAW_BASE_TRANSLATE_Z_ADDR:08x};0x{ENMAG_DRAW_SMALL_DEPTH_OFFSET_ADDR:08x};0x{ENMAG_DRAW_COPYRIGHT_TRANSLATE_Y_ADDR:08x}",
        "visibility_condition": "handle actor+0x1AC non-null and actor+0x1D8 alpha > 0",
        "draw_basis": "OOT3D EnMag_Draw gets material handle, selects slot 5, applies RGBA using actor+0x1D8*1/255, copies the copyright y/z composed matrix to handle+0x7C, sets handle+0xAC, and submits with FUN_0033d220.",
    },
]

TITLE_LOGO_UPDATE_BINDINGS = [
    {
        "phase_role": "init_defaults",
        "state": 0,
        "substate": 0,
        "next_state": 0,
        "next_substate": 0,
        "timer_field_offset": 0x01C6,
        "timer_initial_value": 0x003C,
        "flag_id": NO_INDEX,
        "alpha_field_offsets": "0x01D0;0x01D4;0x01D8;0x01DC",
        "alpha_operation": "set_min_alpha",
        "literal_address": ENMAG_UPDATE_MIN_ALPHA_ADDR,
        "alpha_delta_source": "literal_min_alpha",
        "oot3d_basis": "EnMag_Init clears title_text/main/copyright/effect alpha fields to DAT_001dad04, sets global state actor+0x1C8 to 0, fade-in substate actor+0x1C4 to 0, and timer actor+0x1C6 to 0x3C.",
        "n64_reference": "N64 EnMag_Init clears effect/main/sub/copyright alpha fields and starts MAG_STATE_INITIAL; field layout differs because OOT3D stores CMB handles and compact title state fields.",
    },
    {
        "phase_role": "init_force_rising_button_alphas",
        "state": 0,
        "substate": 0,
        "next_state": 2,
        "next_substate": 0,
        "timer_field_offset": 0x01C0,
        "timer_initial_value": 0x001E,
        "flag_id": NO_INDEX,
        "alpha_field_offsets": "0x01D0;0x01D4;0x01D8;0x01DC",
        "alpha_operation": "set_max_alpha_and_bind_main_csab",
        "literal_address": ENMAG_UPDATE_MAX_ALPHA_ADDR,
        "alpha_delta_source": "literal_max_alpha",
        "oot3d_basis": "EnMag_Init handles the save-context force-rising-button flag by setting all title-logo alpha/effect fields to DAT_001dad00, state actor+0x1C8 to 2, delay actor+0x1C0 to 0x1E, enabling renderer flags, and binding the selected main-logo CSAB.",
        "n64_reference": "N64 forceRisingButtonAlphas jumps to MAG_STATE_DISPLAY with populated alphas; OOT3D equivalent is verified by code.bin fields and CMB/CSAB helper calls.",
    },
    {
        "phase_role": "initial_to_fade_in_on_env3",
        "state": 0,
        "substate": 0,
        "next_state": 1,
        "next_substate": 0,
        "timer_field_offset": 0x01C6,
        "timer_initial_value": 0x0028,
        "flag_id": 3,
        "alpha_field_offsets": "",
        "alpha_operation": "none",
        "literal_address": NO_OFFSET,
        "alpha_delta_source": "env_flag_transition",
        "oot3d_basis": "EnMag_Update calls Flags_GetEnv(play, 3) at 0x001DB004; when true it writes 0x28 to actor+0x1C6 and state 1 to actor+0x1C8.",
        "n64_reference": "N64 MAG_STATE_INITIAL also waits for Flags_GetEnv(play, 3) before entering MAG_STATE_FADE_IN.",
    },
    {
        "phase_role": "fade_in_substate_0_bind_csab",
        "state": 1,
        "substate": 0,
        "next_state": 1,
        "next_substate": 1,
        "timer_field_offset": 0x01C6,
        "timer_initial_value": 0x0026,
        "flag_id": NO_INDEX,
        "alpha_field_offsets": "",
        "alpha_operation": "bind_selected_main_logo_csab",
        "literal_address": NO_OFFSET,
        "alpha_delta_source": "csab_binding",
        "oot3d_basis": "EnMag_Update state 1/substate 0 resolves the scene resource, calls ZAR_GetCSABByIndex with actor+0x1CE, applies it to handle actor+0x1A4, then writes timer 0x26 and substate 1.",
        "n64_reference": "N64 has no CMB/CSAB backend here; its fade-in structure is only a timing/semantic reference.",
    },
    {
        "phase_role": "fade_in_substate_1_wait",
        "state": 1,
        "substate": 1,
        "next_state": 1,
        "next_substate": 2,
        "timer_field_offset": 0x01C6,
        "timer_initial_value": 0x0051,
        "flag_id": NO_INDEX,
        "alpha_field_offsets": "",
        "alpha_operation": "countdown_only",
        "literal_address": NO_OFFSET,
        "alpha_delta_source": "callsite_immediate",
        "oot3d_basis": "EnMag_Update state 1/substate 1 decrements actor+0x1C6; on zero it writes 0x51 to the same timer and substate 2 to actor+0x1C4.",
        "n64_reference": "N64 fade-in uses different timer constants; OOT3D timing is sourced from EnMag_Update immediates.",
    },
    {
        "phase_role": "fade_in_substate_2_main_logo_alpha",
        "state": 1,
        "substate": 2,
        "next_state": 1,
        "next_substate": 3,
        "timer_field_offset": 0x01C6,
        "timer_initial_value": 0x003C,
        "flag_id": NO_INDEX,
        "alpha_field_offsets": "0x01D4",
        "alpha_operation": "add_step_clamp_max",
        "literal_address": ENMAG_UPDATE_MAIN_ALPHA_STEP_ADDR,
        "alpha_delta_source": "DAT_001db030",
        "oot3d_basis": "EnMag_Update state 1/substate 2 adds DAT_001db030 to main-logo alpha actor+0x1D4 while decrementing actor+0x1C6; on zero it clamps actor+0x1D4 to DAT_001dad00, writes timer 0x3C, and enters substate 3.",
        "n64_reference": "N64 mainAlpha also fades in before subordinate logo text, but OOT3D's step 3.0 and 0x51-frame phase are native code.bin values.",
    },
    {
        "phase_role": "fade_in_substate_3_title_text_effect_alpha",
        "state": 1,
        "substate": 3,
        "next_state": 1,
        "next_substate": 4,
        "timer_field_offset": 0x01C6,
        "timer_initial_value": 0x0000,
        "flag_id": NO_INDEX,
        "alpha_field_offsets": "0x01D0;0x01DC",
        "alpha_operation": "add_step_clamp_max_and_set_anim_state_0",
        "literal_address": ENMAG_UPDATE_TITLE_TEXT_EFFECT_ALPHA_STEP_ADDR,
        "alpha_delta_source": "DAT_001db034",
        "oot3d_basis": "EnMag_Update state 1/substate 3 adds DAT_001db034 to title-text alpha actor+0x1D0 and effect/lod actor+0x1DC; on timer zero it clamps both to DAT_001dad00, sets substate 4, and calls FUN_00347fbc(handle, 0).",
        "n64_reference": "N64 separately drives subAlpha/effect colors; OOT3D keeps a CMB material/effect field and a g_title handle alpha.",
    },
    {
        "phase_role": "fade_in_substate_4_copyright_alpha",
        "state": 1,
        "substate": 4,
        "next_state": 2,
        "next_substate": 5,
        "timer_field_offset": 0x01C0,
        "timer_initial_value": 0x0014,
        "flag_id": NO_INDEX,
        "alpha_field_offsets": "0x01D8",
        "alpha_operation": "add_actor_0x1ca_clamp_max_then_display",
        "literal_address": ENMAG_UPDATE_COPYRIGHT_ALPHA_CLAMP_ADDR,
        "alpha_delta_source": "actor+0x1CA default 6",
        "oot3d_basis": "EnMag_Update state 1/substate 4 adds signed actor+0x1CA to copyright alpha actor+0x1D8 until it reaches DAT_001db038/DAT_001dad00, then writes state 2, substate 5, and display delay actor+0x1C0 = 0x14.",
        "n64_reference": "N64 copyrightAlpha rises after main/sub alpha and then enters MAG_STATE_DISPLAY with a delay timer.",
    },
    {
        "phase_role": "display_to_fade_out_on_env4",
        "state": 2,
        "substate": 5,
        "next_state": 3,
        "next_substate": 5,
        "timer_field_offset": NO_INDEX,
        "timer_initial_value": 0,
        "flag_id": 4,
        "alpha_field_offsets": "",
        "alpha_operation": "none",
        "literal_address": NO_OFFSET,
        "alpha_delta_source": "env_flag_transition",
        "oot3d_basis": "EnMag_Update state 2 reaches LAB_001DB044 and calls Flags_GetEnv(play, 4); when true it writes state 3 to actor+0x1C8.",
        "n64_reference": "N64 MAG_STATE_DISPLAY transitions to MAG_STATE_FADE_OUT on Flags_GetEnv(play, 4).",
    },
    {
        "phase_role": "button_to_transition_delay_state4",
        "state": 2,
        "substate": 5,
        "next_state": 4,
        "next_substate": 5,
        "timer_field_offset": 0x01C0,
        "timer_initial_value": 0x0019,
        "flag_id": NO_INDEX,
        "alpha_field_offsets": "",
        "alpha_operation": "input_sets_transition_delay",
        "literal_address": NO_OFFSET,
        "alpha_delta_source": "callsite_immediate",
        "oot3d_basis": "EnMag_Update button path for states 2/5 writes delay 0x19 to actor+0x1C0, state 4 to actor+0x1C8, and pending flag actor+0x1C2 = 1 before transition setup.",
        "n64_reference": "N64 display button path starts file-select transition and fade-out; OOT3D splits this through state 4 and compact transition fields.",
    },
    {
        "phase_role": "transition_delay_state4_to_fade_out",
        "state": 4,
        "substate": 5,
        "next_state": 3,
        "next_substate": 5,
        "timer_field_offset": 0x01C0,
        "timer_initial_value": 0,
        "flag_id": NO_INDEX,
        "alpha_field_offsets": "",
        "alpha_operation": "set_transition_and_fade_steps",
        "literal_address": NO_OFFSET,
        "alpha_delta_source": "actor+0x1CA=15 actor+0x1CC=25",
        "oot3d_basis": "EnMag_Update state 4 waits for actor+0x1C0 to reach zero, then sets game-mode/transition bytes, writes actor+0x1CA = 15 and actor+0x1CC = 25, and enters state 3.",
        "n64_reference": "N64 sets copyrightAlphaStep=15 and fadeOutAlphaStep=25 for the file-select transition fade.",
    },
    {
        "phase_role": "fade_out_state3",
        "state": 3,
        "substate": 5,
        "next_state": 5,
        "next_substate": 5,
        "timer_field_offset": NO_INDEX,
        "timer_initial_value": 0,
        "flag_id": NO_INDEX,
        "alpha_field_offsets": "0x01D0;0x01D4;0x01D8",
        "alpha_operation": "subtract_actor_0x1cc_clamp_min_then_post_display",
        "literal_address": ENMAG_UPDATE_MIN_ALPHA_ADDR,
        "alpha_delta_source": "actor+0x1CC default 10 or transition 25",
        "oot3d_basis": "EnMag_Update state 3 subtracts signed actor+0x1CC from title-text, main-logo, and copyright alpha fields, clamps each to DAT_001dad04, and writes state 5 when all three reach zero.",
        "n64_reference": "N64 MAG_STATE_FADE_OUT subtracts fadeOutAlphaStep from effect/main/sub and copyrightAlphaStep from copyright; OOT3D verified fields are the three CMB draw alpha fields.",
    },
    {
        "phase_role": "fade_out_state6_with_transition_guard",
        "state": 6,
        "substate": 5,
        "next_state": 5,
        "next_substate": 5,
        "timer_field_offset": 0x01C0,
        "timer_initial_value": 0,
        "flag_id": NO_INDEX,
        "alpha_field_offsets": "0x01D0;0x01D4;0x01D8",
        "alpha_operation": "subtract_actor_0x1cc_clamp_min_then_transition_and_post_display",
        "literal_address": ENMAG_UPDATE_MIN_ALPHA_ADDR,
        "alpha_delta_source": "actor+0x1CC default 10 or transition 25",
        "oot3d_basis": "EnMag_Update state 6 performs the same CMB alpha subtraction as state 3, then waits for actor+0x1C0 to be zero before writing state 5 and forcing the file-select transition bytes when needed.",
        "n64_reference": "State 6 is OOT3D-specific control-flow around the N64-equivalent fade-out and file-select transition behavior.",
    },
]

TARGET_ASSETS = [
    ("scene_main_zsi", "scene/spot99_info.zsi"),
    ("scene_room_zsi", "scene/spot99_0_info.zsi"),
    ("scene_zar_qdb_demo", "scene/spot00.zar"),
    ("actor_link_opening", "actor/zelda_link_opening.zar"),
    ("actor_epona_horse", "actor/zelda_horse.zar"),
    ("actor_normal_horse_alt", "actor/zelda_horse_normal.zar"),
    ("actor_title_logo", "actor/zelda_mag.zar"),
    ("actor_keep_opening_common", "actor/zelda_keep_opening.zar"),
    ("actor_spot00_objects", "actor/zelda_spot00_objects.zar"),
    ("actor_spot00_break", "actor/zelda_spot00_break.zar"),
    ("actor_opening_demo1_code_string", "actor/zelda_opening_demo1.zar"),
    ("ui_title_logo_it", "misc/eu/italian/menu_title_logo.ctxb"),
    ("ui_title_logo_us", "misc/us/english/menu_title_logo.ctxb"),
    ("ui_title_background_it", "menu/08_EU_ITALIAN/hud_menu_title00.ctxb"),
    ("ui_title_background_us", "menu/01_US_ENGLISH/hud_menu_title00.ctxb"),
]

SYMBOL_KEYWORDS = (
    "EnMag_",
    "EnHorseLinkChild_",
    "EnHorse_",
    "EnHorseNormal_",
    "Camera_Demo",
    "Cutscene_ProcessCommands",
)

TITLE_INTRO_ACTOR_CUE_COMMAND_IDS = {
    0x000A,
    0x003E,
}


def int_value(value: Any, default: int = 0) -> int:
    try:
        if value is None:
            return default
        return int(value)
    except (TypeError, ValueError):
        return default


def hex_u32(value: Any) -> str:
    return f"0x{int_value(value) & 0xFFFFFFFF:08x}"


def c_string(value: Any) -> str:
    text = "" if value is None else str(value)
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_u8(value: Any) -> str:
    return f"{int_value(value) & 0xFF}u"


def c_u16(value: Any) -> str:
    return f"{int_value(value) & 0xFFFF}u"


def c_u32(value: Any) -> str:
    return f"{int_value(value) & 0xFFFFFFFF}u"


def c_f32(value: Any) -> str:
    number = float(value)
    if not math.isfinite(number):
        number = 0.0
    text = f"{number:.9g}"
    if "e" not in text.lower() and "." not in text:
        text += ".0"
    return f"{text}f"


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True, allow_nan=False)
        handle.write("\n")


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def runtime_to_file_offset(runtime_addr: int) -> int:
    return int_value(runtime_addr) - CODE_BIN_RUNTIME_BASE


def read_u32_at_runtime(code_data: bytes, runtime_addr: int, default: int = 0) -> int:
    offset = runtime_to_file_offset(runtime_addr)
    if offset < 0 or offset + 4 > len(code_data):
        return default
    return int.from_bytes(code_data[offset : offset + 4], "little")


def read_f32_at_runtime(code_data: bytes, runtime_addr: int, default: float = 0.0) -> float:
    offset = runtime_to_file_offset(runtime_addr)
    if offset < 0 or offset + 4 > len(code_data):
        return default
    return float(struct.unpack_from("<f", code_data, offset)[0])


def read_arm_mov_immediate_at_runtime(
    code_data: bytes, runtime_addr: int, destination_register: int, default: int = 0
) -> int:
    instruction = read_u32_at_runtime(code_data, runtime_addr)
    if (instruction & 0x0FE0F000) != (0x03A00000 | (destination_register << 12)):
        return default
    immediate = instruction & 0xFF
    rotate = ((instruction >> 8) & 0xF) * 2
    if rotate == 0:
        return immediate
    return ((immediate >> rotate) | (immediate << (32 - rotate))) & 0xFFFFFFFF


def read_arm_cmp_immediate_at_runtime(
    code_data: bytes, runtime_addr: int, source_register: int, default: int = 0
) -> int:
    instruction = read_u32_at_runtime(code_data, runtime_addr)
    if (instruction & 0x0FF00000) != 0x03500000 or (
        (instruction >> 16) & 0xF
    ) != source_register:
        return default
    immediate = instruction & 0xFF
    rotate = ((instruction >> 8) & 0xF) * 2
    if rotate == 0:
        return immediate
    return ((immediate >> rotate) | (immediate << (32 - rotate))) & 0xFFFFFFFF


def read_f32_block_at_runtime(code_data: bytes, runtime_addr: int, count: int) -> list[float]:
    offset = runtime_to_file_offset(runtime_addr)
    if offset < 0 or offset + count * 4 > len(code_data):
        return []
    return [float(value) for value in struct.unpack_from("<" + "f" * count, code_data, offset)]


def matrix_row_major(values: list[float]) -> str:
    return ",".join(f"{value:.9g}" for value in values)


def normalized_rel(path: str) -> str:
    return path.replace("\\", "/").lower()


def discover_code_rom_strings(code_bin: Path) -> dict[str, list[dict[str, Any]]]:
    if not code_bin.is_file():
        return {}
    data = code_bin.read_bytes()
    discovered: dict[str, list[dict[str, Any]]] = {}
    for match in re.finditer(rb"[ -~]{4,}", data):
        text = match.group().decode("ascii", errors="replace")
        if not text.startswith("rom:/"):
            continue
        rel = normalized_rel(text[5:])
        if (
            "spot00" not in rel
            and "horse" not in rel
            and "opening" not in rel
            and "zelda_mag" not in rel
            and "title" not in rel
        ):
            continue
        discovered.setdefault(rel, []).append(
            {
                "address": match.start(),
                "address_hex": hex_u32(match.start()),
                "string": text,
            }
        )
    return discovered


def recover_enmag_actor_init(code_bin: Path) -> dict[str, Any]:
    row: dict[str, Any] = {
        "actor_init_index": 0,
        "actor_name": "ACTOR_EN_MAG",
        "object_name": "OBJECT_MAG",
        "decode_status": "missing_code_bin",
        "actor_init_file_offset": NO_OFFSET,
        "actor_init_file_offset_hex": hex_u32(NO_OFFSET),
        "actor_id": 0,
        "actor_category": 0,
        "flags": 0,
        "object_id": 0,
        "instance_size": 0,
        "init_function": 0,
        "destroy_function": ENMAG_DESTROY_ADDR,
        "update_function": ENMAG_UPDATE_ADDR,
        "draw_function": ENMAG_DRAW_ADDR,
        "basis": "OOT3D code.bin ActorInit row recovered by matching known EnMag Destroy/Update/Draw function pointers; N64 ACTOR_EN_MAG/OBJECT_MAG structure is used only as an architectural cross-check.",
    }
    if not code_bin.is_file():
        return row

    data = code_bin.read_bytes()
    for offset in range(0, len(data) - 0x20, 4):
        words = [int.from_bytes(data[offset + word * 4 : offset + word * 4 + 4], "little") for word in range(8)]
        if words[5] == ENMAG_DESTROY_ADDR and words[6] == ENMAG_UPDATE_ADDR and words[7] == ENMAG_DRAW_ADDR:
            packed_actor = words[0]
            row.update(
                {
                    "decode_status": "decoded_from_code_bin_actor_init_table",
                    "actor_init_file_offset": offset,
                    "actor_init_file_offset_hex": hex_u32(offset),
                    "actor_id": packed_actor & 0xFFFF,
                    "actor_category": (packed_actor >> 16) & 0xFF,
                    "flags": words[1],
                    "object_id": words[2],
                    "instance_size": words[3],
                    "init_function": words[4],
                    "destroy_function": words[5],
                    "update_function": words[6],
                    "draw_function": words[7],
                }
            )
            return row

    row["decode_status"] = "known_enmag_function_pointer_triplet_not_found"
    return row


def build_title_actor_scale_rows(code_bin: Path) -> list[dict[str, Any]]:
    configs = [
        {
            "scale_index": 0,
            "actor_role": "opening_link_adult",
            "actor_name": "PLAYER_ADULT",
            "archive_role": "actor_opening_link",
            "init_function": 0x00191844,
            "update_function": 0,
            "draw_function": 0x004BF618,
            "actor_set_scale_callsite": 0x004A3274,
            "actor_set_scale_function": 0x004A31E0,
            "scale_literal_address": 0x004A334C,
            "gravity_literal_address": 0,
            "shadow_y_offset_literal_address": 0,
            "shadow_scale_literal_address": 0,
            "focus_y_offset_literal_address": 0,
            "oot3d_basis": "The OOT3D actor spawn initializer at 0x004A31E0 writes code.bin literal 0x004A334C (0.01f) to actor scale fields +0x54/+0x58/+0x5C before Player_Init 0x00191844. The mounted title-intro Player trace validates those three native fields as 0.01f.",
            "n64_reference": "N64 actor initialization is used only as an architectural reference for the default actor-scale stage; the value and store addresses come from OOT3D code.bin.",
        },
        {
            "scale_index": 1,
            "actor_role": "opening_epona",
            "actor_name": "ACTOR_EN_HORSE",
            "archive_role": "actor_horse",
            "init_function": 0x001D9004,
            "update_function": 0,
            "draw_function": 0x001B46E8,
            "actor_set_scale_callsite": 0x001D927C,
            "scale_literal_address": 0x001D93EC,
            "gravity_literal_address": 0x001D93F0,
            "shadow_y_offset_literal_address": 0x001D93E0,
            "shadow_scale_literal_address": 0x001D93F4,
            "focus_y_offset_literal_address": 0x001D940C,
            "oot3d_basis": "EnHorse_Init loads scale 0.01f into VFP s0 from literal 0x001D93EC, branches to Actor_SetScale, then initializes gravity, ActorShape shadow scale, and focus y from the same code.bin literal pool.",
            "n64_reference": "soh/src/overlays/actors/ovl_En_Horse/z_en_horse.c uses Actor_SetScale(&actor, 0.01f), gravity -3.5f, ActorShadow_DrawHorse 20.0f, focus.y += 70.0f; used only to validate logical structure.",
        },
        {
            "scale_index": 2,
            "actor_role": "horse_normal_reference",
            "actor_name": "ACTOR_EN_HORSE_NORMAL",
            "archive_role": "actor_horse",
            "init_function": 0x00161A60,
            "update_function": 0x001004C8,
            "draw_function": 0x0014968C,
            "actor_set_scale_callsite": 0x00161A84,
            "scale_literal_address": 0x00161DA0,
            "gravity_literal_address": 0x00161DA4,
            "shadow_y_offset_literal_address": 0x00161DB0,
            "shadow_scale_literal_address": 0x00161DA8,
            "focus_y_offset_literal_address": 0x00161DB4,
            "oot3d_basis": "EnHorseNormal_Init loads scale 0.01f into VFP s0 from literal 0x00161DA0, branches to Actor_SetScale, then initializes gravity, ActorShape shadow scale, and focus y from the same code.bin literal pool.",
            "n64_reference": "soh/src/overlays/actors/ovl_En_Horse_Normal/z_en_horse_normal.c uses Actor_SetScale(&actor, 0.01f), gravity -3.5f, ActorShadow_DrawHorse 20.0f, focus.y += 70.0f; used only to validate logical structure.",
        },
    ]
    code_data = code_bin.read_bytes() if code_bin.is_file() else b""
    rows: list[dict[str, Any]] = []
    for config in configs:
        row = dict(config)
        row.setdefault("actor_set_scale_function", ACTOR_SET_SCALE_FUNCTION)
        row["actor_scale"] = read_f32_at_runtime(code_data, row["scale_literal_address"])
        row["gravity"] = read_f32_at_runtime(code_data, row["gravity_literal_address"])
        row["shadow_y_offset"] = read_f32_at_runtime(code_data, row["shadow_y_offset_literal_address"])
        row["shadow_scale"] = read_f32_at_runtime(code_data, row["shadow_scale_literal_address"])
        row["focus_y_offset"] = read_f32_at_runtime(code_data, row["focus_y_offset_literal_address"])
        row["decode_status"] = (
            TITLE_ACTOR_SCALE_DECODE_STATUS if code_data else "missing_code_bin"
        )
        for key in (
            "init_function",
            "update_function",
            "draw_function",
            "actor_set_scale_callsite",
            "actor_set_scale_function",
            "scale_literal_address",
            "gravity_literal_address",
            "shadow_y_offset_literal_address",
            "shadow_scale_literal_address",
            "focus_y_offset_literal_address",
        ):
            row[f"{key}_hex"] = hex_u32(row[key])
        rows.append(row)
    return rows


def model_animation_type_entry_by_name(
    rows: list[dict[str, Any]], archive_path: str, embedded_name: str
) -> tuple[dict[str, Any] | None, int]:
    expected_suffix = ".csab" if embedded_name.lower().endswith(".csab") else ".cmb"
    target = embedded_name.replace("\\", "/").lower()
    type_local_index = 0
    for row in rows:
        if row.get("archive_path") != archive_path:
            continue
        name = str(row.get("embedded_name", ""))
        if not name.lower().endswith(expected_suffix):
            continue
        if name.replace("\\", "/").lower() == target:
            return row, type_local_index
        type_local_index += 1
    return None, NO_INDEX


def model_animation_type_entry_by_index(
    rows: list[dict[str, Any]], archive_path: str, type_local_index: int, expected_suffix: str
) -> dict[str, Any] | None:
    current_index = 0
    for row in rows:
        if row.get("archive_path") != archive_path:
            continue
        if not str(row.get("embedded_name", "")).lower().endswith(expected_suffix.lower()):
            continue
        if current_index == type_local_index:
            return row
        current_index += 1
    return None


def build_title_actor_animation_rows(
    code_bin: Path, model_animation_rows: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    configs = [
        {
            "animation_index": 0,
            "actor_role": "opening_link_adult",
            "actor_name": "PLAYER_ADULT",
            "asset_role": "actor_link_opening",
            "archive_path": "actor/zelda_link_opening.zar",
            "cmb_name": "boy/model/link_opening.cmb",
            "title_visual_csab_name": "boy/anim/uma_anim_fastrun.csab",
            "animation_table_pointer_literal_address": 0x0018C1CC,
            "animation_table_outer_runtime_address": 0,
            "animation_table_outer_index": NO_INDEX,
            "init_animation_slot": 0,
            "actor_init_function": 0x00191844,
            "actor_update_function": 0,
            "actor_draw_function": 0x004BF618,
            "zar_get_cmb_by_index_callsite": 0,
            "animation_play_once_callsite": 0,
            "title_cue_semantic_reference_function": NO_OFFSET,
            "title_cue_role": "opening_link_adult_mounted_gallop_visual",
            "oot3d_basis": "The title-opening adult player uses zelda_link_opening.zar, boy/model/link_opening.cmb, and the mounted fast-run CSAB. Player_Draw is the OOT3D visual owner; the archive name boy denotes adult Link.",
            "n64_reference": "The player-action cutscene row selects the title clip and the EnHorse rider attachment supplies its mounted root. EnHorseLinkChild is retained only as unrelated actor-symbol evidence elsewhere in the extracted tables.",
        },
        {
            "animation_index": 1,
            "actor_role": "opening_epona",
            "actor_name": "ACTOR_EN_HORSE",
            "asset_role": "actor_epona_horse",
            "archive_path": "actor/zelda_horse.zar",
            "cmb_name": "Model/epona.cmb",
            "title_visual_csab_name": "Anim/hl_anim_fastrun2_30.csab",
            "animation_table_pointer_literal_address": 0x001D97DC,
            "animation_table_outer_runtime_address": 0,
            "animation_table_outer_index": 0,
            "init_animation_slot": 0,
            "actor_init_function": 0x001D9004,
            "actor_update_function": 0,
            "actor_draw_function": 0x001B46E8,
            "zar_get_cmb_by_index_callsite": 0x001D94E8,
            "animation_play_once_callsite": 0x001D9568,
            "title_cue_semantic_reference_function": 0x003A7E14,
            "title_cue_role": "opening_epona_cutscene_move_gallop_visual",
            "oot3d_basis": "EnHorse_Init calls ZAR_GetCMBByIndex(resource, 0), loads DAT_001D97DC as an actor-type CSAB table pointer, plays Epona table slot 0 at init, and zelda_horse.zar provides hl_anim_fastrun2_30.csab for the title-intro gallop visual.",
            "n64_reference": "soh EnHorse_CsMoveInit sets ENHORSE_ANIM_GALLOP and EnHorse_CsMoveToPoint drives speed/playSpeed; OOT3D native table and archive names are still used for actual CMB/CSAB binding.",
        },
    ]
    code_data = code_bin.read_bytes() if code_bin.is_file() else b""
    rows: list[dict[str, Any]] = []
    for config in configs:
        row = dict(config)
        pointer_literal = int_value(row["animation_table_pointer_literal_address"])
        outer_address = read_u32_at_runtime(code_data, pointer_literal, NO_OFFSET)
        if int_value(row["animation_table_outer_index"], NO_INDEX) == NO_INDEX:
            table_address = outer_address
        else:
            row["animation_table_outer_runtime_address"] = outer_address
            table_address = read_u32_at_runtime(
                code_data,
                outer_address + int_value(row["animation_table_outer_index"]) * 4,
                NO_OFFSET,
            )
        init_slot = int_value(row["init_animation_slot"])
        init_csab_type_index = read_u32_at_runtime(code_data, table_address + init_slot * 4, NO_INDEX)
        cmb_entry, cmb_type_index = model_animation_type_entry_by_name(
            model_animation_rows, str(row["archive_path"]), str(row["cmb_name"])
        )
        init_csab_entry = model_animation_type_entry_by_index(
            model_animation_rows, str(row["archive_path"]), init_csab_type_index, ".csab"
        )
        title_csab_entry, title_csab_type_index = model_animation_type_entry_by_name(
            model_animation_rows, str(row["archive_path"]), str(row["title_visual_csab_name"])
        )
        row.update(
            {
                "cmb_type_index": cmb_type_index,
                "cmb_file_index": int_value(cmb_entry.get("file_index"), NO_INDEX) if cmb_entry else NO_INDEX,
                "init_animation_table_runtime_address": table_address,
                "init_csab_type_index": init_csab_type_index,
                "init_csab_file_index": int_value(init_csab_entry.get("file_index"), NO_INDEX)
                if init_csab_entry
                else NO_INDEX,
                "init_csab_name": str(init_csab_entry.get("embedded_name", "")) if init_csab_entry else "",
                "title_visual_csab_type_index": title_csab_type_index,
                "title_visual_csab_file_index": int_value(title_csab_entry.get("file_index"), NO_INDEX)
                if title_csab_entry
                else NO_INDEX,
                "decode_status": TITLE_ACTOR_ANIMATION_BINDING_DECODE_STATUS
                if (
                    code_data
                    and cmb_type_index != NO_INDEX
                    and init_csab_type_index != NO_INDEX
                    and init_csab_entry is not None
                    and title_csab_type_index != NO_INDEX
                    and title_csab_entry is not None
                )
                else "incomplete_native_actor_animation_binding",
            }
        )
        if row["actor_role"] == "opening_link_adult" and row["decode_status"] != "incomplete_native_actor_animation_binding":
            row["decode_status"] = "native_opening_adult_player_draw_asset_binding"
        for key in (
            "animation_table_pointer_literal_address",
            "animation_table_outer_runtime_address",
            "init_animation_table_runtime_address",
            "actor_init_function",
            "actor_update_function",
            "actor_draw_function",
            "zar_get_cmb_by_index_callsite",
            "animation_play_once_callsite",
            "title_cue_semantic_reference_function",
        ):
            row[f"{key}_hex"] = hex_u32(row[key])
        rows.append(row)
    return rows


def build_title_actor_motion_animation_rows(
    code_bin: Path, model_animation_rows: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    code_data = code_bin.read_bytes() if code_bin.is_file() else b""
    archive_path = "actor/zelda_horse.zar"
    outer_address = read_u32_at_runtime(
        code_data, ENHORSE_UPDATE_INGO_HORSE_ANIM_TABLE_POINTER_ADDR, NO_OFFSET
    )
    horse_type_index = 0
    table_address = read_u32_at_runtime(code_data, outer_address + horse_type_index * 4, NO_OFFSET)
    animation_roles = {
        "idle": 0,
        "walk": 4,
        "trot": 5,
        "fast": 7,
    }
    row: dict[str, Any] = {
        "motion_index": 0,
        "actor_role": "opening_epona",
        "actor_name": "ACTOR_EN_HORSE",
        "asset_role": "actor_epona_horse",
        "archive_path": archive_path,
        "horse_type_index": horse_type_index,
        "action_field_offset": 0x01A4,
        "speed_field_offset": 0x006C,
        "animation_index_field_offset": 0x0E74,
        "horse_type_field_offset": 0x01B0,
        "skel_anime_field_offset": 0x01C4,
        "source_function": ENHORSE_UPDATE_INGO_HORSE_ANIM_FUNCTION,
        "table_pointer_literal_address": ENHORSE_UPDATE_INGO_HORSE_ANIM_TABLE_POINTER_ADDR,
        "animation_table_outer_runtime_address": outer_address,
        "animation_table_runtime_address": table_address,
        "zero_speed_literal_address": ENHORSE_UPDATE_INGO_HORSE_ANIM_ZERO_SPEED_ADDR,
        "zero_speed": read_f32_at_runtime(code_data, ENHORSE_UPDATE_INGO_HORSE_ANIM_ZERO_SPEED_ADDR),
        "walk_threshold_literal_address": ENHORSE_UPDATE_INGO_HORSE_ANIM_WALK_THRESHOLD_ADDR,
        "walk_threshold": read_f32_at_runtime(code_data, ENHORSE_UPDATE_INGO_HORSE_ANIM_WALK_THRESHOLD_ADDR),
        "fast_threshold_literal_address": ENHORSE_UPDATE_INGO_HORSE_ANIM_FAST_THRESHOLD_ADDR,
        "fast_threshold": read_f32_at_runtime(code_data, ENHORSE_UPDATE_INGO_HORSE_ANIM_FAST_THRESHOLD_ADDR),
        "base_play_speed_scale_literal_address": ENHORSE_UPDATE_INGO_HORSE_ANIM_BASE_PLAY_SPEED_SCALE_ADDR,
        "base_play_speed_scale": read_f32_at_runtime(
            code_data, ENHORSE_UPDATE_INGO_HORSE_ANIM_BASE_PLAY_SPEED_SCALE_ADDR
        ),
        "idle_play_speed_scale_literal_address": ENHORSE_UPDATE_INGO_HORSE_ANIM_IDLE_PLAY_SPEED_SCALE_ADDR,
        "idle_play_speed_scale": read_f32_at_runtime(
            code_data, ENHORSE_UPDATE_INGO_HORSE_ANIM_IDLE_PLAY_SPEED_SCALE_ADDR
        ),
        "walk_play_speed_scale_literal_address": ENHORSE_UPDATE_INGO_HORSE_ANIM_WALK_PLAY_SPEED_SCALE_ADDR,
        "walk_play_speed_scale": read_f32_at_runtime(
            code_data, ENHORSE_UPDATE_INGO_HORSE_ANIM_WALK_PLAY_SPEED_SCALE_ADDR
        ),
        "trot_play_speed_scale_literal_address": ENHORSE_UPDATE_INGO_HORSE_ANIM_TROT_PLAY_SPEED_SCALE_ADDR,
        "trot_play_speed_scale": read_f32_at_runtime(
            code_data, ENHORSE_UPDATE_INGO_HORSE_ANIM_TROT_PLAY_SPEED_SCALE_ADDR
        ),
        "fast_play_speed_scale_literal_address": ENHORSE_UPDATE_INGO_HORSE_ANIM_FAST_PLAY_SPEED_SCALE_ADDR,
        "fast_play_speed_scale": read_f32_at_runtime(
            code_data, ENHORSE_UPDATE_INGO_HORSE_ANIM_FAST_PLAY_SPEED_SCALE_ADDR
        ),
        "audio_id_literal_address": ENHORSE_UPDATE_INGO_HORSE_ANIM_AUDIO_ID_ADDR,
        "audio_id": read_u32_at_runtime(code_data, ENHORSE_UPDATE_INGO_HORSE_ANIM_AUDIO_ID_ADDR),
        "audio_freq_pointer_address": ENHORSE_UPDATE_INGO_HORSE_ANIM_AUDIO_FREQ_ADDR,
        "audio_freq_runtime_address": read_u32_at_runtime(
            code_data, ENHORSE_UPDATE_INGO_HORSE_ANIM_AUDIO_FREQ_ADDR
        ),
        "audio_vol_pointer_address": ENHORSE_UPDATE_INGO_HORSE_ANIM_AUDIO_VOL_ADDR,
        "audio_vol_runtime_address": read_u32_at_runtime(
            code_data, ENHORSE_UPDATE_INGO_HORSE_ANIM_AUDIO_VOL_ADDR
        ),
        "animation_change_same_state_callsite": ENHORSE_UPDATE_INGO_HORSE_ANIM_CHANGE_SAME_STATE_CALLSITE,
        "animation_change_changed_state_callsite": ENHORSE_UPDATE_INGO_HORSE_ANIM_CHANGE_CHANGED_STATE_CALLSITE,
        "oot3d_basis": "EnHorse_UpdateIngoHorseAnim writes actor+0x1A4=4, classifies actor+0x6C speed against OOT3D literal thresholds 0/3/6, writes actor+0x0E74 animation index 0/4/5/7, derives idle/walk/trot/fast play-speed factors 1/0.5/0.25/0.3 with the native 1.5 Animation_Change base multiplier, indexes DAT_0033DA84[actor+0x1B0][actor+0x0E74], and tail-calls Animation_Change_00375C08 on actor+0x1C4.",
        "n64_reference": "N64 EnHorse_CsMoveInit/CsMoveToPoint validates the gameplay shape: cutscene movement selects gallop and drives playSpeed from speed. OOT3D keeps the same semantic split but uses native CSAB table indices and literal thresholds here.",
    }
    decoded = bool(code_data and outer_address != NO_OFFSET and table_address != NO_OFFSET)
    for role, animation_index in animation_roles.items():
        csab_type_index = read_u32_at_runtime(code_data, table_address + animation_index * 4, NO_INDEX)
        csab_entry = model_animation_type_entry_by_index(
            model_animation_rows, archive_path, csab_type_index, ".csab"
        )
        row[f"{role}_animation_index"] = animation_index
        row[f"{role}_csab_type_index"] = csab_type_index
        row[f"{role}_csab_file_index"] = (
            int_value(csab_entry.get("file_index"), NO_INDEX) if csab_entry else NO_INDEX
        )
        row[f"{role}_csab_name"] = str(csab_entry.get("embedded_name", "")) if csab_entry else ""
        decoded = decoded and csab_type_index != NO_INDEX and csab_entry is not None
    row["decode_status"] = (
        TITLE_ACTOR_MOTION_ANIMATION_DECODE_STATUS
        if decoded
        else "incomplete_oot3d_enhorse_update_ingo_horse_anim_speed_state_table"
    )
    for key in (
        "action_field_offset",
        "speed_field_offset",
        "animation_index_field_offset",
        "horse_type_field_offset",
        "skel_anime_field_offset",
        "source_function",
        "table_pointer_literal_address",
        "animation_table_outer_runtime_address",
        "animation_table_runtime_address",
        "zero_speed_literal_address",
        "walk_threshold_literal_address",
        "fast_threshold_literal_address",
        "base_play_speed_scale_literal_address",
        "idle_play_speed_scale_literal_address",
        "walk_play_speed_scale_literal_address",
        "trot_play_speed_scale_literal_address",
        "fast_play_speed_scale_literal_address",
        "audio_id_literal_address",
        "audio_freq_pointer_address",
        "audio_freq_runtime_address",
        "audio_vol_pointer_address",
        "audio_vol_runtime_address",
        "animation_change_same_state_callsite",
        "animation_change_changed_state_callsite",
    ):
        row[f"{key}_hex"] = hex_u32(row[key])
    return [row]


def csab_resolution_summary(
    code_data: bytes,
    model_animation_rows: list[dict[str, Any]],
    archive_path: str,
    table_address: int,
    animation_indices: list[int],
) -> str:
    if table_address == NO_OFFSET:
        return ""
    parts: list[str] = []
    for animation_index in animation_indices:
        csab_type_index = read_u32_at_runtime(
            code_data, table_address + animation_index * 4, NO_INDEX
        )
        csab_entry = model_animation_type_entry_by_index(
            model_animation_rows, archive_path, csab_type_index, ".csab"
        )
        csab_name = str(csab_entry.get("embedded_name", "")) if csab_entry else ""
        parts.append(f"{animation_index}->{csab_type_index} {csab_name}".strip())
    return ";".join(parts)


def csab_resolution_detail(
    code_data: bytes,
    model_animation_rows: list[dict[str, Any]],
    archive_path: str,
    table_address: int,
    animation_index: int,
) -> dict[str, Any]:
    detail: dict[str, Any] = {
        "animation_index": animation_index,
        "csab_type_index": NO_INDEX,
        "csab_file_index": NO_INDEX,
        "csab_max_frame": 0,
        "csab_name": "",
    }
    if table_address == NO_OFFSET:
        return detail

    csab_type_index = read_u32_at_runtime(
        code_data, table_address + animation_index * 4, NO_INDEX
    )
    csab_entry = model_animation_type_entry_by_index(
        model_animation_rows, archive_path, csab_type_index, ".csab"
    )
    detail["csab_type_index"] = csab_type_index
    if csab_entry is not None:
        detail["csab_file_index"] = int_value(csab_entry.get("file_index"), NO_INDEX)
        detail["csab_max_frame"] = int_value(csab_entry.get("frame_count"), 0)
        detail["csab_name"] = str(csab_entry.get("embedded_name", ""))
    return detail


def build_title_horse_state_route_rows(
    code_bin: Path, model_animation_rows: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    code_data = code_bin.read_bytes() if code_bin.is_file() else b""
    archive_path = "actor/zelda_horse.zar"
    horse_type_index = 0

    start_outer = read_u32_at_runtime(
        code_data, ENHORSE_START_MOVING_ANIMATION_TABLE_POINTER_ADDR, NO_OFFSET
    )
    start_table = read_u32_at_runtime(code_data, start_outer + horse_type_index * 4, NO_OFFSET)
    walk_outer = read_u32_at_runtime(code_data, ENHORSE_MOUNTED_WALK_TABLE_POINTER_ADDR, NO_OFFSET)
    walk_table = read_u32_at_runtime(code_data, walk_outer + horse_type_index * 4, NO_OFFSET)
    trot_outer = read_u32_at_runtime(code_data, ENHORSE_MOUNTED_TROT_TABLE_POINTER_ADDR, NO_OFFSET)
    trot_table = read_u32_at_runtime(code_data, trot_outer + horse_type_index * 4, NO_OFFSET)
    gallop_control = read_u32_at_runtime(
        code_data, ENHORSE_MOUNTED_GALLOP_CONTROL_BLOCK_POINTER_ADDR, NO_OFFSET
    )
    gallop_outer = gallop_control + 0x2C if gallop_control != NO_OFFSET else NO_OFFSET
    gallop_table = read_u32_at_runtime(code_data, gallop_outer + horse_type_index * 4, NO_OFFSET)
    gallop_fast = csab_resolution_detail(
        code_data, model_animation_rows, archive_path, gallop_table, 7
    )
    gallop_carrot = csab_resolution_detail(
        code_data, model_animation_rows, archive_path, gallop_table, 9
    )

    follow_far_bits = read_u32_at_runtime(code_data, ENHORSE_SET_FOLLOW_FAR_DISTANCE_ADDR)
    follow_near_bits = (follow_far_bits - ENHORSE_SET_FOLLOW_NEAR_DISTANCE_U32_DELTA) & 0xFFFFFFFF
    follow_near_distance = struct.unpack("<f", follow_near_bits.to_bytes(4, "little"))[0]

    def base_row(
        *,
        index: int,
        route_role: str,
        source_function: int,
        source_export: str,
        action_value: int,
        animation_indices: str,
        table_pointer_literal_address: int,
        outer_address: int,
        table_address: int,
        literal_addresses: str,
        literal_values: str,
        resolved_csabs: str,
        oot3d_basis: str,
        n64_reference: str,
        unresolved_followup: str = "",
        forced_speed: float = 0.0,
        update_arg: int = 0,
        gallop_control_block_runtime_address: int = NO_OFFSET,
        gallop_play_speed_scale: float = 0.0,
        gallop_play_speed_switch: float = 0.0,
        gallop_play_speed_min: float = 0.0,
        gallop_play_speed_max: float = 0.0,
        gallop_brake_input_magnitude: float = 0.0,
        gallop_downshift_speed: float = 0.0,
        gallop_fast: dict[str, Any] | None = None,
        gallop_carrot: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        gallop_fast = gallop_fast or {}
        gallop_carrot = gallop_carrot or {}
        row = {
            "state_route_index": index,
            "route_role": route_role,
            "actor_role": "opening_epona",
            "actor_name": "ACTOR_EN_HORSE",
            "asset_role": "actor_epona_horse",
            "archive_path": archive_path,
            "source_function": source_function,
            "source_export": source_export,
            "horse_type_index": horse_type_index,
            "action_field_offset": ENHORSE_ACTION_FIELD_OFFSET,
            "speed_field_offset": ENHORSE_SPEED_FIELD_OFFSET,
            "animation_index_field_offset": ENHORSE_ANIMATION_INDEX_FIELD_OFFSET,
            "horse_type_field_offset": ENHORSE_HORSE_TYPE_FIELD_OFFSET,
            "skel_anime_field_offset": ENHORSE_SKEL_ANIME_FIELD_OFFSET,
            "follow_timer_field_offset": ENHORSE_FOLLOW_TIMER_FIELD_OFFSET,
            "action_value": action_value,
            "animation_indices": animation_indices,
            "table_pointer_literal_address": table_pointer_literal_address,
            "animation_table_outer_runtime_address": outer_address,
            "animation_table_runtime_address": table_address,
            "primary_literal_addresses": literal_addresses,
            "primary_literal_values": literal_values,
            "forced_speed": forced_speed,
            "update_arg": update_arg,
            "gallop_control_block_runtime_address": gallop_control_block_runtime_address,
            "gallop_play_speed_scale": gallop_play_speed_scale,
            "gallop_play_speed_switch": gallop_play_speed_switch,
            "gallop_play_speed_min": gallop_play_speed_min,
            "gallop_play_speed_max": gallop_play_speed_max,
            "gallop_brake_input_magnitude": gallop_brake_input_magnitude,
            "gallop_downshift_speed": gallop_downshift_speed,
            "gallop_fast_animation_index": int_value(gallop_fast.get("animation_index"), NO_INDEX),
            "gallop_fast_csab_type_index": int_value(gallop_fast.get("csab_type_index"), NO_INDEX),
            "gallop_fast_csab_file_index": int_value(gallop_fast.get("csab_file_index"), NO_INDEX),
            "gallop_fast_csab_name": str(gallop_fast.get("csab_name", "")),
            "gallop_carrot_animation_index": int_value(gallop_carrot.get("animation_index"), NO_INDEX),
            "gallop_carrot_csab_type_index": int_value(gallop_carrot.get("csab_type_index"), NO_INDEX),
            "gallop_carrot_csab_file_index": int_value(gallop_carrot.get("csab_file_index"), NO_INDEX),
            "gallop_carrot_csab_name": str(gallop_carrot.get("csab_name", "")),
            "resolved_csabs": resolved_csabs,
            "oot3d_basis": oot3d_basis,
            "n64_reference": n64_reference,
            "unresolved_followup": unresolved_followup,
            "decode_status": TITLE_HORSE_STATE_ROUTE_DECODE_STATUS,
        }
        for key in (
            "source_function",
            "action_field_offset",
            "speed_field_offset",
            "animation_index_field_offset",
            "horse_type_field_offset",
            "skel_anime_field_offset",
            "follow_timer_field_offset",
            "table_pointer_literal_address",
            "animation_table_outer_runtime_address",
            "animation_table_runtime_address",
            "gallop_control_block_runtime_address",
        ):
            row[f"{key}_hex"] = hex_u32(row[key])
        return row

    rows = [
        base_row(
            index=0,
            route_role="idle_follow_trigger",
            source_function=ENHORSE_IDLE_FUNCTION,
            source_export="analysis/title_intro_start_moving_callers_ghidra_export/decompiled/99002_00390f98_EnHorse_Idle.c",
            action_value=NO_INDEX,
            animation_indices="7 when direct horse call succeeds; otherwise delegated to EnHorse_SetFollowAnimation",
            table_pointer_literal_address=ENHORSE_START_MOVING_ANIMATION_TABLE_POINTER_ADDR,
            outer_address=start_outer,
            table_address=start_table,
            literal_addresses=(
                f"{hex_u32(ENHORSE_IDLE_ZERO_SPEED_ADDR)};"
                f"{hex_u32(ENHORSE_IDLE_DREG_TRIGGER_ADDR)};"
                f"{hex_u32(ENHORSE_IDLE_MIN_ANGLE_ADDR)};"
                f"{hex_u32(ENHORSE_IDLE_MIN_DISTANCE_ADDR)};"
                f"{hex_u32(ENHORSE_IDLE_MORPH_FRAMES_ADDR)}"
            ),
            literal_values=(
                f"zero_speed={read_f32_at_runtime(code_data, ENHORSE_IDLE_ZERO_SPEED_ADDR):.9g};"
                f"min_angle={read_f32_at_runtime(code_data, ENHORSE_IDLE_MIN_ANGLE_ADDR):.9g};"
                f"min_distance={read_f32_at_runtime(code_data, ENHORSE_IDLE_MIN_DISTANCE_ADDR):.9g};"
                f"morph_frames={read_f32_at_runtime(code_data, ENHORSE_IDLE_MORPH_FRAMES_ADDR):.9g}"
            ),
            resolved_csabs=csab_resolution_summary(
                code_data, model_animation_rows, archive_path, start_table, [7]
            ),
            oot3d_basis="EnHorse_Idle writes actor+0x6C=0, handles the horse-call trigger, clears follow timer actor+0xEB4, then calls EnHorse_StartMovingAnimation(actor, 7) or EnHorse_SetFollowAnimation(actor, play).",
            n64_reference="soh EnHorse_Idle has the same horse-call structure: DREG(53), optional spawn, SetFollowAnimation, direct StartMovingAnimation(gallop), and camera setup; OOT3D field offsets/literals are native.",
            unresolved_followup="This is follow/debug-call behavior, not the final title QDB cue consumer.",
        ),
        base_row(
            index=1,
            route_role="set_follow_animation_distance_hysteresis",
            source_function=ENHORSE_SET_FOLLOW_ANIMATION_FUNCTION,
            source_export="analysis/title_intro_start_moving_callers_ghidra_export/decompiled/99001_003352c8_EnHorse_SetFollowAnimation.c",
            action_value=NO_INDEX,
            animation_indices="4 walk;5 trot;7 gallop",
            table_pointer_literal_address=ENHORSE_START_MOVING_ANIMATION_TABLE_POINTER_ADDR,
            outer_address=start_outer,
            table_address=start_table,
            literal_addresses=(
                f"{hex_u32(ENHORSE_SET_FOLLOW_PLAYER_ACTOR_OFFSET_ADDR)};"
                f"{hex_u32(ENHORSE_SET_FOLLOW_FAR_DISTANCE_ADDR)}"
            ),
            literal_values=(
                f"player_actor_offset=0x{read_u32_at_runtime(code_data, ENHORSE_SET_FOLLOW_PLAYER_ACTOR_OFFSET_ADDR):04x};"
                f"near_distance={follow_near_distance:.9g};"
                f"far_distance={read_f32_at_runtime(code_data, ENHORSE_SET_FOLLOW_FAR_DISTANCE_ADDR):.9g}"
            ),
            resolved_csabs=csab_resolution_summary(
                code_data, model_animation_rows, archive_path, start_table, [4, 5, 7]
            ),
            oot3d_basis="EnHorse_SetFollowAnimation measures XZ distance to the player actor, applies native 300/400 distance hysteresis, and calls EnHorse_StartMovingAnimation with e74 candidate 4/5/7.",
            n64_reference="soh EnHorse_SetFollowAnimation uses the same 300/400 walk/trot/gallop hysteresis; OOT3D confirms the thresholds and native e74 values from code.bin.",
            unresolved_followup="This validates the follow state route; title cutscene cue dispatch is still a separate consumer.",
        ),
        base_row(
            index=2,
            route_role="start_moving_animation_native_table",
            source_function=ENHORSE_START_MOVING_ANIMATION_FUNCTION,
            source_export="analysis/title_intro_start_moving_callers_ghidra_export/decompiled/99000_0031c588_EnHorse_StartMovingAnimation.c",
            action_value=3,
            animation_indices="valid 4/5/7/9, default 4",
            table_pointer_literal_address=ENHORSE_START_MOVING_ANIMATION_TABLE_POINTER_ADDR,
            outer_address=start_outer,
            table_address=start_table,
            literal_addresses=hex_u32(ENHORSE_START_MOVING_ANIMATION_TABLE_POINTER_ADDR),
            literal_values=f"table_outer={hex_u32(start_outer)};table_epona={hex_u32(start_table)}",
            resolved_csabs=csab_resolution_summary(
                code_data, model_animation_rows, archive_path, start_table, [4, 5, 7, 9]
            ),
            oot3d_basis="EnHorse_StartMovingAnimation writes actor+0x1A4=3, validates requested e74 in {4,5,7,9}, clears actor+0xE54 bit 0x8000, stores actor+0xE74, indexes DAT_0031C690[actor+0x1B0][actor+0xE74], and calls Animation_Change_00375C08 on actor+0x1C4.",
            n64_reference="soh EnHorse_StartMovingAnimation is the same logical state helper: action becomes follow-player, walk/trot/gallop ids are validated, and sAnimationHeaders[type][animationIdx] drives Animation_Change.",
            unresolved_followup="OOT3D adds native e74=9 as a valid moving state; cue binding must use this table rather than N64 enum values.",
        ),
        base_row(
            index=3,
            route_role="update_speed_native_control",
            source_function=ENHORSE_UPDATE_SPEED_FUNCTION,
            source_export="analysis/title_intro_cue_consumer_ghidra_export/decompiled/99000_003182cc_EnHorse_UpdateSpeed.c",
            action_value=NO_INDEX,
            animation_indices="none",
            table_pointer_literal_address=NO_OFFSET,
            outer_address=NO_OFFSET,
            table_address=NO_OFFSET,
            literal_addresses="0x003186c4..0x003186fc plus VFP caller arguments",
            literal_values="updates actor+0x6C speed and actor+0x36/0xBE yaw; full typed VFP signature pending",
            resolved_csabs="",
            oot3d_basis="EnHorse_UpdateSpeed is the native speed/yaw integrator used by MountedWalk/Trot/Gallop. Ghidra has not recovered the VFP argument signature, so only observed field writes and literal pools are claimed here.",
            n64_reference="soh EnHorse_UpdateSpeed names the logical role and parameter family for mounted speed, brake, stick magnitude, decel, target speed, and yaw step; OOT3D field layout and caller literals remain authoritative.",
            unresolved_followup="Run a typed Ghidra pass or manual decompilation before materializing a C signature.",
        ),
        base_row(
            index=4,
            route_role="mounted_walk_state",
            source_function=ENHORSE_MOUNTED_WALK_FUNCTION,
            source_export="analysis/title_intro_cue_consumer_ghidra_export/decompiled/99003_003a1778_EnHorse_MountedWalk.c",
            action_value=8,
            animation_indices="4 walk, transitions to 5/6 when speed > 3",
            table_pointer_literal_address=ENHORSE_MOUNTED_WALK_TABLE_POINTER_ADDR,
            outer_address=walk_outer,
            table_address=walk_table,
            literal_addresses=(
                f"{hex_u32(ENHORSE_MOUNTED_WALK_SPEED_ADDR)};"
                f"{hex_u32(ENHORSE_MOUNTED_WALK_DELAY_ADDR)};"
                f"{hex_u32(ENHORSE_MOUNTED_WALK_UPDATE_ARG_ADDR)};"
                f"{hex_u32(ENHORSE_MOUNTED_WALK_ZERO_SPEED_ADDR)};"
                f"{hex_u32(ENHORSE_MOUNTED_WALK_TO_TROT_THRESHOLD_ADDR)};"
                f"{hex_u32(ENHORSE_MOUNTED_WALK_INPUT_MAG_ADDR)}"
            ),
            literal_values=(
                f"walk_speed={read_f32_at_runtime(code_data, ENHORSE_MOUNTED_WALK_SPEED_ADDR):.9g};"
                f"delay={read_f32_at_runtime(code_data, ENHORSE_MOUNTED_WALK_DELAY_ADDR):.9g};"
                f"update_arg=0x{read_u32_at_runtime(code_data, ENHORSE_MOUNTED_WALK_UPDATE_ARG_ADDR):x};"
                f"zero={read_f32_at_runtime(code_data, ENHORSE_MOUNTED_WALK_ZERO_SPEED_ADDR):.9g};"
                f"to_trot={read_f32_at_runtime(code_data, ENHORSE_MOUNTED_WALK_TO_TROT_THRESHOLD_ADDR):.9g};"
                f"input_mag={read_f32_at_runtime(code_data, ENHORSE_MOUNTED_WALK_INPUT_MAG_ADDR):.9g}"
            ),
            resolved_csabs=csab_resolution_summary(
                code_data, model_animation_rows, archive_path, walk_table, [4, 5, 6, 7]
            ),
            oot3d_basis="EnHorse_MountedWalk plays walking sound for e74=4, calls EnHorse_UpdateSpeed, enters idle at speed 0, and transitions to action 9 with e74 5/6 when speed exceeds native threshold 3.",
            n64_reference="soh EnHorse_MountedWalk has the same mounted low-speed state shape and transitions around 0 and 3 speed; OOT3D e74/table values are native.",
        ),
        base_row(
            index=5,
            route_role="mounted_trot_state",
            source_function=ENHORSE_MOUNTED_TROT_FUNCTION,
            source_export="analysis/title_intro_cue_consumer_ghidra_export/decompiled/99005_003a83b4_EnHorse_MountedTrot.c",
            action_value=9,
            animation_indices="5 trot, 4 walk on downshift, 7/8 gallop transition when speed >= 6",
            table_pointer_literal_address=ENHORSE_MOUNTED_TROT_TABLE_POINTER_ADDR,
            outer_address=trot_outer,
            table_address=trot_table,
            literal_addresses=(
                f"{hex_u32(ENHORSE_MOUNTED_TROT_UPDATE_ARG_ADDR)};"
                f"{hex_u32(ENHORSE_MOUNTED_TROT_TO_WALK_THRESHOLD_ADDR)};"
                f"{hex_u32(ENHORSE_MOUNTED_TROT_TABLE_POINTER_ADDR)};"
                f"{hex_u32(ENHORSE_MOUNTED_TROT_TO_GALLOP_THRESHOLD_ADDR)}"
            ),
            literal_values=(
                f"update_arg=0x{read_u32_at_runtime(code_data, ENHORSE_MOUNTED_TROT_UPDATE_ARG_ADDR):x};"
                f"to_walk={read_f32_at_runtime(code_data, ENHORSE_MOUNTED_TROT_TO_WALK_THRESHOLD_ADDR):.9g};"
                f"to_gallop={read_f32_at_runtime(code_data, ENHORSE_MOUNTED_TROT_TO_GALLOP_THRESHOLD_ADDR):.9g}"
            ),
            resolved_csabs=csab_resolution_summary(
                code_data, model_animation_rows, archive_path, trot_table, [4, 5, 7, 8]
            ),
            oot3d_basis="EnHorse_MountedTrot calls EnHorse_UpdateSpeed, downshifts to walking when speed < 3, loops e74=5 while under 6, and transitions to action 10 with e74 7/8 when speed reaches 6.",
            n64_reference="soh EnHorse_MountedTrot likewise uses UpdateSpeed and speed thresholds 3/6 to move between walk, trot, and gallop states; OOT3D native table indices provide the actual CSABs.",
        ),
        base_row(
            index=6,
            route_role="mounted_gallop_state",
            source_function=ENHORSE_MOUNTED_GALLOP_FUNCTION,
            source_export="analysis/title_intro_cue_consumer_ghidra_export/decompiled/99004_003a7e14_EnHorse_MountedGallop.c",
            action_value=10,
            animation_indices="7/9 gallop variants, 5/6 downshift, 10/11 jump/landing bypass states",
            table_pointer_literal_address=ENHORSE_MOUNTED_GALLOP_CONTROL_BLOCK_POINTER_ADDR,
            outer_address=gallop_outer,
            table_address=gallop_table,
            literal_addresses=(
                f"{hex_u32(ENHORSE_MOUNTED_GALLOP_FORCED_SPEED_ADDR)};"
                f"{hex_u32(ENHORSE_MOUNTED_GALLOP_UPDATE_ARG_ADDR)};"
                f"{hex_u32(ENHORSE_MOUNTED_GALLOP_CONTROL_BLOCK_POINTER_ADDR)};"
                f"{hex_u32(ENHORSE_MOUNTED_GALLOP_PLAY_SPEED_SCALE_ADDR)};"
                f"{hex_u32(ENHORSE_MOUNTED_GALLOP_BRAKE_INPUT_MAG_ADDR)};"
                f"{hex_u32(ENHORSE_MOUNTED_GALLOP_DOWNSHIFT_SPEED_ADDR)}"
            ),
            literal_values=(
                f"forced_speed={read_f32_at_runtime(code_data, ENHORSE_MOUNTED_GALLOP_FORCED_SPEED_ADDR):.9g};"
                f"update_arg=0x{read_u32_at_runtime(code_data, ENHORSE_MOUNTED_GALLOP_UPDATE_ARG_ADDR):x};"
                f"control_block={hex_u32(gallop_control)};"
                f"play_speed_scale={read_f32_at_runtime(code_data, ENHORSE_MOUNTED_GALLOP_PLAY_SPEED_SCALE_ADDR):.9g};"
                f"play_speed_switch={read_f32_at_runtime(code_data, gallop_control + 4):.9g};"
                f"play_speed_min={read_f32_at_runtime(code_data, gallop_control + 8):.9g};"
                f"play_speed_max={read_f32_at_runtime(code_data, gallop_control + 0x14):.9g};"
                f"brake_input_mag={read_f32_at_runtime(code_data, ENHORSE_MOUNTED_GALLOP_BRAKE_INPUT_MAG_ADDR):.9g};"
                f"downshift_speed={read_f32_at_runtime(code_data, ENHORSE_MOUNTED_GALLOP_DOWNSHIFT_SPEED_ADDR):.9g}"
            ),
            resolved_csabs=csab_resolution_summary(
                code_data, model_animation_rows, archive_path, gallop_table, [5, 6, 7, 8, 9, 10, 11]
            ),
            oot3d_basis="EnHorse_MountedGallop uses a native control block at DAT_003A8184, clamps animation play speed from actor+0x6C*0.45, switches e74 7/9 for gallop variants, downshifts below speed 6, and indexes the table at control_block+0x2C.",
            n64_reference="soh EnHorse_MountedGallop confirms the same high-speed mounted state and speed/playSpeed coupling, but OOT3D's control block, e74 values, and CSAB table are native.",
            unresolved_followup="This is mounted gameplay state; title QDB cue dispatch still needs the OOT3D consumer split before claiming exact intro movement state selection.",
            forced_speed=read_f32_at_runtime(code_data, ENHORSE_MOUNTED_GALLOP_FORCED_SPEED_ADDR),
            update_arg=read_u32_at_runtime(code_data, ENHORSE_MOUNTED_GALLOP_UPDATE_ARG_ADDR),
            gallop_control_block_runtime_address=gallop_control,
            gallop_play_speed_scale=read_f32_at_runtime(code_data, ENHORSE_MOUNTED_GALLOP_PLAY_SPEED_SCALE_ADDR),
            gallop_play_speed_switch=read_f32_at_runtime(code_data, gallop_control + 4),
            gallop_play_speed_min=read_f32_at_runtime(code_data, gallop_control + 8),
            gallop_play_speed_max=read_f32_at_runtime(code_data, gallop_control + 0x14),
            gallop_brake_input_magnitude=read_f32_at_runtime(code_data, ENHORSE_MOUNTED_GALLOP_BRAKE_INPUT_MAG_ADDR),
            gallop_downshift_speed=read_f32_at_runtime(code_data, ENHORSE_MOUNTED_GALLOP_DOWNSHIFT_SPEED_ADDR),
            gallop_fast=gallop_fast,
            gallop_carrot=gallop_carrot,
        ),
    ]
    return rows


def build_title_horse_cutscene_action_route_rows(
    code_bin: Path, model_animation_rows: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    code_data = code_bin.read_bytes() if code_bin.is_file() else b""
    archive_path = "actor/zelda_horse.zar"
    horse_type_index = 0
    action_map_address = read_u32_at_runtime(
        code_data, ENHORSE_CUTSCENE_ACTION_MAP_POINTER_ADDR, NO_OFFSET
    )
    init_table_address = read_u32_at_runtime(
        code_data, ENHORSE_CUTSCENE_INIT_TABLE_POINTER_ADDR, NO_OFFSET
    )
    update_table_address = read_u32_at_runtime(
        code_data, ENHORSE_CUTSCENE_UPDATE_TABLE_POINTER_ADDR, NO_OFFSET
    )
    animation_table_address = read_u32_at_runtime(
        code_data,
        ENHORSE_CUTSCENE_ANIMATION_TABLE_OUTER_RUNTIME_ADDR + horse_type_index * 4,
        NO_OFFSET,
    )

    dispatch: dict[int, int] = {}
    for entry_index in range(ENHORSE_CUTSCENE_ACTION_MAP_ENTRY_COUNT):
        entry_address = action_map_address + entry_index * 8
        action_id = read_u32_at_runtime(code_data, entry_address, NO_INDEX)
        substate = read_u32_at_runtime(code_data, entry_address + 4, NO_INDEX)
        dispatch[action_id] = substate

    route_specs = [
        {
            "route_role": "target_move_gallop",
            "action_id": 0x24,
            "reset_position_on_entry": 0,
            "initial_animation_index": 7,
            "initial_change_mode_instruction_address": ANIMATION_PLAY_ONCE_SET_SPEED_MODE_MOV_ADDR,
            "initial_morph_literal_address": ANIMATION_PLAY_ONCE_SET_SPEED_MORPH_LITERAL_ADDR,
            "alternate_animation_index": NO_INDEX,
            "completion_animation_index": NO_INDEX,
            "selector_word_index": NO_INDEX,
            "selector_bits": 0,
            "play_speed_scale_address": ENHORSE_CUTSCENE_STATE1_PLAY_SPEED_SCALE_ADDR,
            "fixed_speed_address": ENHORSE_CUTSCENE_STATE1_SPEED_ADDR,
            "yaw_step_address": ENHORSE_CUTSCENE_STATE1_YAW_STEP_ADDR,
            "source_evidence": (
                "State 1 init 0x0016ca48 selects e74=7; update 0x003cf3c4 rotates by "
                "the 0x10b literal, sets speed from the 8.0 literal, advances SkelAnime, "
                "and restarts the same native CSAB when it completes."
            ),
        },
        {
            "route_role": "reset_target_move_selector",
            "action_id": 0x40,
            "reset_position_on_entry": 1,
            "initial_animation_index": 7,
            "initial_change_mode_instruction_address": ANIMATION_PLAY_ONCE_SET_SPEED_MODE_MOV_ADDR,
            "initial_morph_literal_address": ANIMATION_PLAY_ONCE_SET_SPEED_MORPH_LITERAL_ADDR,
            "alternate_animation_index": 0,
            "completion_animation_index": NO_INDEX,
            "selector_word_index": ENHORSE_CUTSCENE_STATE4_SELECTOR_WORD_INDEX,
            "selector_bits": ENHORSE_CUTSCENE_STATE4_SELECTOR_BITS,
            "play_speed_scale_address": ENHORSE_CUTSCENE_STATE4_PLAY_SPEED_SCALE_ADDR,
            "fixed_speed_address": ENHORSE_CUTSCENE_STATE4_SPEED_ADDR,
            "yaw_step_address": ENHORSE_CUTSCENE_STATE4_YAW_STEP_ADDR,
            "source_evidence": (
                "State 4 init 0x002a8af8 resets actor position/rotation from the active "
                "12-word cue and selects e74=0 only when cue+0x28 is 0x42200000; its "
                "update 0x00230d84 uses the same target-move shape as state 1."
            ),
        },
        {
            "route_role": "hold_jump_then_idle",
            "action_id": 0x41,
            "reset_position_on_entry": 1,
            "initial_animation_index": 3,
            "initial_change_mode_instruction_address": ENHORSE_CUTSCENE_STATE5_INITIAL_MODE_MOV_ADDR,
            "initial_morph_literal_address": ENHORSE_CUTSCENE_STATE5_INITIAL_MORPH_LITERAL_ADDR,
            "alternate_animation_index": NO_INDEX,
            "completion_animation_index": 0,
            "selector_word_index": NO_INDEX,
            "selector_bits": 0,
            "play_speed_scale_address": ENHORSE_CUTSCENE_STATE5_PLAY_SPEED_ADDR,
            "fixed_speed_address": NO_OFFSET,
            "yaw_step_address": NO_OFFSET,
            "source_evidence": (
                "State 5 init 0x002b6c00 resets actor position/rotation, selects e74=3, "
                "and starts it once at speed 1.0. Update 0x002535f0 stops movement and, "
                "when SkelAnime completes, selects e74=0 with first mode 2/morph -3 and "
                "then mode 0 loops."
            ),
        },
    ]

    rows: list[dict[str, Any]] = []
    for route_index, spec in enumerate(route_specs):
        action_id = int(spec["action_id"])
        substate = dispatch.get(action_id, NO_INDEX)
        init_function = read_u32_at_runtime(
            code_data, init_table_address + substate * 4, NO_OFFSET
        )
        update_function = read_u32_at_runtime(
            code_data, update_table_address + substate * 4, NO_OFFSET
        )
        initial = csab_resolution_detail(
            code_data,
            model_animation_rows,
            archive_path,
            animation_table_address,
            int(spec["initial_animation_index"]),
        )
        alternate_index = int(spec["alternate_animation_index"])
        alternate = (
            csab_resolution_detail(
                code_data,
                model_animation_rows,
                archive_path,
                animation_table_address,
                alternate_index,
            )
            if alternate_index != NO_INDEX
            else {}
        )
        completion_index = int(spec["completion_animation_index"])
        completion = (
            csab_resolution_detail(
                code_data,
                model_animation_rows,
                archive_path,
                animation_table_address,
                completion_index,
            )
            if completion_index != NO_INDEX
            else {}
        )
        play_speed_scale_address = int(spec["play_speed_scale_address"])
        fixed_speed_address = int(spec["fixed_speed_address"])
        yaw_step_address = int(spec["yaw_step_address"])
        row = {
            "action_route_index": route_index,
            "route_role": str(spec["route_role"]),
            "actor_role": "opening_epona",
            "archive_path": archive_path,
            "action_id": action_id,
            "substate": substate,
            "reset_position_on_entry": int(spec["reset_position_on_entry"]),
            "dispatch_function": ENHORSE_CUTSCENE_DISPATCH_FUNCTION,
            "action_map_runtime_address": action_map_address,
            "init_table_runtime_address": init_table_address,
            "update_table_runtime_address": update_table_address,
            "init_function": init_function,
            "update_function": update_function,
            "animation_table_outer_runtime_address": ENHORSE_CUTSCENE_ANIMATION_TABLE_OUTER_RUNTIME_ADDR,
            "animation_table_runtime_address": animation_table_address,
            "initial_animation_index": int(initial["animation_index"]),
            "initial_csab_type_index": int(initial["csab_type_index"]),
            "initial_csab_file_index": int(initial["csab_file_index"]),
            "initial_csab_max_frame": int(initial["csab_max_frame"]),
            "initial_csab_name": str(initial["csab_name"]),
            "initial_change_mode_instruction_address": int(
                spec["initial_change_mode_instruction_address"]
            ),
            "initial_change_mode": read_arm_mov_immediate_at_runtime(
                code_data, int(spec["initial_change_mode_instruction_address"]), 2, NO_INDEX
            ),
            "initial_morph_literal_address": int(spec["initial_morph_literal_address"]),
            "initial_morph_frames": read_f32_at_runtime(
                code_data, int(spec["initial_morph_literal_address"])
            ),
            "alternate_animation_index": int_value(alternate.get("animation_index"), NO_INDEX),
            "alternate_csab_type_index": int_value(alternate.get("csab_type_index"), NO_INDEX),
            "alternate_csab_file_index": int_value(alternate.get("csab_file_index"), NO_INDEX),
            "alternate_csab_max_frame": int_value(alternate.get("csab_max_frame"), 0),
            "alternate_csab_name": str(alternate.get("csab_name", "")),
            "completion_animation_index": int_value(completion.get("animation_index"), NO_INDEX),
            "completion_csab_type_index": int_value(completion.get("csab_type_index"), NO_INDEX),
            "completion_csab_file_index": int_value(completion.get("csab_file_index"), NO_INDEX),
            "completion_csab_max_frame": int_value(completion.get("csab_max_frame"), 0),
            "completion_csab_name": str(completion.get("csab_name", "")),
            "selector_word_index": int(spec["selector_word_index"]),
            "selector_bits": int(spec["selector_bits"]),
            "play_speed_scale_literal_address": play_speed_scale_address,
            "play_speed_scale": read_f32_at_runtime(code_data, play_speed_scale_address),
            "fixed_speed_literal_address": fixed_speed_address,
            "fixed_speed": (
                read_f32_at_runtime(code_data, fixed_speed_address)
                if fixed_speed_address != NO_OFFSET
                else 0.0
            ),
            "yaw_step_literal_address": yaw_step_address,
            "yaw_step": (
                read_u32_at_runtime(code_data, yaw_step_address, 0)
                if yaw_step_address != NO_OFFSET
                else 0
            ),
            "target_epsilon": (
                read_f32_at_runtime(code_data, fixed_speed_address)
                if fixed_speed_address != NO_OFFSET
                else 0.0
            ),
            "completion_play_speed": (
                read_f32_at_runtime(code_data, ENHORSE_CUTSCENE_STATE5_COMPLETION_PLAY_SPEED_ADDR)
                if action_id == 0x41
                else 0.0
            ),
            "completion_morph_frames": (
                read_f32_at_runtime(code_data, ENHORSE_CUTSCENE_STATE5_COMPLETION_MORPH_ADDR)
                if action_id == 0x41
                else 0.0
            ),
            "completion_first_mode_instruction_address": (
                ENHORSE_CUTSCENE_STATE5_COMPLETION_FIRST_MODE_MOV_ADDR
                if action_id == 0x41
                else NO_OFFSET
            ),
            "completion_first_mode": (
                read_arm_mov_immediate_at_runtime(
                    code_data, ENHORSE_CUTSCENE_STATE5_COMPLETION_FIRST_MODE_MOV_ADDR, 2, NO_INDEX
                )
                if action_id == 0x41
                else NO_INDEX
            ),
            "completion_loop_mode_instruction_address": (
                ENHORSE_CUTSCENE_STATE5_COMPLETION_LOOP_MODE_MOV_ADDR
                if action_id == 0x41
                else NO_OFFSET
            ),
            "completion_loop_mode": (
                read_arm_mov_immediate_at_runtime(
                    code_data, ENHORSE_CUTSCENE_STATE5_COMPLETION_LOOP_MODE_MOV_ADDR, 2, NO_INDEX
                )
                if action_id == 0x41
                else NO_INDEX
            ),
            "source_evidence": str(spec["source_evidence"]),
            "decode_status": TITLE_HORSE_CUTSCENE_ACTION_ROUTE_DECODE_STATUS,
        }
        for key in (
            "action_id",
            "dispatch_function",
            "action_map_runtime_address",
            "init_table_runtime_address",
            "update_table_runtime_address",
            "init_function",
            "update_function",
            "animation_table_outer_runtime_address",
            "animation_table_runtime_address",
            "selector_bits",
            "play_speed_scale_literal_address",
            "fixed_speed_literal_address",
            "yaw_step_literal_address",
            "initial_change_mode_instruction_address",
            "initial_morph_literal_address",
            "completion_first_mode_instruction_address",
            "completion_loop_mode_instruction_address",
        ):
            row[f"{key}_hex"] = hex_u32(row[key])
        rows.append(row)
    return rows


def build_title_skel_anime_timing_rows(code_bin: Path) -> list[dict[str, Any]]:
    code_data = code_bin.read_bytes() if code_bin.is_file() else b""
    update_rate = read_arm_mov_immediate_at_runtime(
        code_data, GAME_STATE_INIT_UPDATE_RATE_MOV_ADDR, 0, 0
    )
    update_scale = read_f32_at_runtime(code_data, SKEL_ANIME_UPDATE_SCALE_LITERAL_ADDR)
    common = {
        "set_update_function": SKEL_ANIME_SET_UPDATE_FUNCTION,
        "update_function": SKEL_ANIME_UPDATE_FUNCTION,
        "update_scale_literal_address": SKEL_ANIME_UPDATE_SCALE_LITERAL_ADDR,
        "update_scale": update_scale,
        "global_context_pointer_address": SKEL_ANIME_GLOBAL_CONTEXT_POINTER_ADDR,
        "global_update_rate_offset": SKEL_ANIME_GLOBAL_UPDATE_RATE_OFFSET,
        "update_rate_initializer_function": GAME_STATE_INIT_FUNCTION,
        "update_rate_initializer_instruction_address": GAME_STATE_INIT_UPDATE_RATE_MOV_ADDR,
        "global_update_rate": update_rate,
        "source_evidence": (
            "SkelAnime_SetUpdate 0x00320d28 maps Animation_Change mode 0 to update mode 4 "
            "and mode 2 to update mode 6. SkelAnime_Update 0x0036b4ec multiplies playSpeed "
            "by the signed global-context +0x110 value and code.bin literal 1/3 at 0x0036b808. "
            "GameState_Init 0x00416f60 loads immediate 2 at 0x00416ff0 and stores it through "
            "global pointer 0x0051b2f4 at offset +0x110."
        ),
        "decode_status": "decoded_from_oot3d_skelanime_update_and_gamestate_init_code_bin",
    }
    rows = [
        {
            **common,
            "timing_index": 0,
            "timing_role": "loop_mode_0",
            "change_mode": 0,
            "update_mode": 4,
            "terminal_behavior": "wrap_inclusive_frame_span",
        },
        {
            **common,
            "timing_index": 1,
            "timing_role": "once_mode_2",
            "change_mode": 2,
            "update_mode": 6,
            "terminal_behavior": "clamp_terminal_then_complete_next_update",
        },
    ]
    for row in rows:
        for key in (
            "set_update_function",
            "update_function",
            "update_scale_literal_address",
            "global_context_pointer_address",
            "update_rate_initializer_function",
            "update_rate_initializer_instruction_address",
        ):
            row[f"{key}_hex"] = hex_u32(row[key])
    return rows


def build_title_mounted_player_animation_frame_rows(code_bin: Path) -> list[dict[str, Any]]:
    code_data = code_bin.read_bytes() if code_bin.is_file() else b""
    scaled_indices = [
        read_arm_cmp_immediate_at_runtime(code_data, address, 1, NO_INDEX)
        for address in MOUNTED_PLAYER_FRAME_SUPPORTED_INDEX_CMP_ADDRS
    ]
    scaled_mask = 0
    for index in scaled_indices:
        if 0 <= index < 32:
            scaled_mask |= 1 << index
    frame_scale = read_f32_at_runtime(code_data, MOUNTED_PLAYER_FRAME_SCALE_LITERAL_ADDR)
    frame_bias = read_f32_at_runtime(code_data, MOUNTED_PLAYER_FRAME_BIAS_LITERAL_ADDR)
    decoded = (
        sorted(scaled_indices) == [4, 5, 6, 7, 8, 9]
        and math.isclose(frame_scale, 2.0 / 3.0, rel_tol=1e-6)
        and math.isclose(frame_bias, 0.5, rel_tol=1e-6)
    )
    row = {
        "frame_route_index": 0,
        "frame_role": "mounted_player_pose_from_ride_actor_animation",
        "frame_function": MOUNTED_PLAYER_FRAME_FUNCTION,
        "frame_load_instruction_address": MOUNTED_PLAYER_FRAME_LOAD_INSTRUCTION_ADDR,
        "mounted_player_action_function": MOUNTED_PLAYER_ACTION_FUNCTION,
        "frame_store_instruction_address": MOUNTED_PLAYER_FRAME_STORE_INSTRUCTION_ADDR,
        "horse_animation_index_offset": MOUNTED_PLAYER_HORSE_ANIMATION_INDEX_OFFSET,
        "horse_animation_frame_offset": MOUNTED_PLAYER_HORSE_ANIMATION_FRAME_OFFSET,
        "player_skel_anime_offset": MOUNTED_PLAYER_SKEL_ANIME_OFFSET,
        "player_current_frame_offset": MOUNTED_PLAYER_SKEL_ANIME_CURRENT_FRAME_OFFSET,
        "scaled_horse_animation_indices": ";".join(str(value) for value in scaled_indices),
        "scaled_horse_animation_index_mask": scaled_mask,
        "frame_scale_literal_address": MOUNTED_PLAYER_FRAME_SCALE_LITERAL_ADDR,
        "frame_scale": frame_scale,
        "frame_bias_literal_address": MOUNTED_PLAYER_FRAME_BIAS_LITERAL_ADDR,
        "frame_bias": frame_bias,
        "quantization": "vcvt_s32_f32_round_toward_zero",
        "return_unmatched_horse_animation_frame": 1,
        "unmatched_horse_animation_behavior": "return_horse_animation_frame_unchanged",
        "source_evidence": (
            "FUN_004c5510 reads ride actor animation index at +0x0e74 and frame at +0x0e78; "
            "for code.bin CMP immediates 4,5,6,7,8,9 it computes int(frame * the literal "
            "at 0x004c5558 + the literal at 0x004c555c). Mounted Player action 0x002b7fd0 "
            "stores the VFP result to Player SkelAnime currentFrame at Player+0x0254+0x003c "
            "at 0x002b84c0."
        ),
        "decode_status": (
            "decoded_from_oot3d_mounted_player_frame_consumer_code_bin"
            if decoded
            else "mounted_player_frame_consumer_decode_mismatch"
        ),
    }
    for key in (
        "frame_function",
        "frame_load_instruction_address",
        "mounted_player_action_function",
        "frame_store_instruction_address",
        "frame_scale_literal_address",
        "frame_bias_literal_address",
        "scaled_horse_animation_index_mask",
    ):
        row[f"{key}_hex"] = hex_u32(row[key])
    return [row]


def build_title_link_child_state_route_rows(
    code_bin: Path, model_animation_rows: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    code_data = code_bin.read_bytes() if code_bin.is_file() else b""
    archive_path = "actor/zelda_link_opening.zar"
    action_table = read_u32_at_runtime(
        code_data, ENHORSE_LINK_CHILD_ACTION_TABLE_POINTER_ADDR, NO_OFFSET
    )
    animation_table = read_u32_at_runtime(
        code_data, ENHORSE_LINK_CHILD_ANIMATION_TABLE_POINTER_ADDR, NO_OFFSET
    )

    def link_csab_summary(animation_indices: list[int]) -> str:
        return csab_resolution_summary(
            code_data, model_animation_rows, archive_path, animation_table, animation_indices
        )

    def base_row(
        *,
        index: int,
        route_role: str,
        n64_reference_action: str,
        expected_animation_indices: list[int],
        expected_speed_values: str,
        oot3d_basis: str,
        n64_reference: str,
        unresolved_followup: str,
    ) -> dict[str, Any]:
        handler = read_u32_at_runtime(code_data, action_table + index * 4, NO_OFFSET)
        row = {
            "state_route_index": index,
            "route_role": route_role,
            "actor_role": "opening_link_boy",
            "actor_name": "ACTOR_EN_HORSE_LINK_CHILD",
            "asset_role": "actor_link_opening",
            "archive_path": archive_path,
            "update_function": ENHORSE_LINK_CHILD_UPDATE_FUNCTION,
            "init_function": ENHORSE_LINK_CHILD_INIT_FUNCTION,
            "draw_function": ENHORSE_LINK_CHILD_DRAW_FUNCTION,
            "action_field_offset": ENHORSE_LINK_CHILD_ACTION_FIELD_OFFSET,
            "animation_index_field_offset": ENHORSE_LINK_CHILD_ANIMATION_INDEX_FIELD_OFFSET,
            "speed_field_offset": ENHORSE_LINK_CHILD_SPEED_FIELD_OFFSET,
            "skel_anime_field_offset": ENHORSE_LINK_CHILD_SKEL_ANIME_FIELD_OFFSET,
            "action_table_pointer_literal_address": ENHORSE_LINK_CHILD_ACTION_TABLE_POINTER_ADDR,
            "action_table_runtime_address": action_table,
            "action_slot": index,
            "handler_function": handler,
            "animation_table_pointer_literal_address": ENHORSE_LINK_CHILD_ANIMATION_TABLE_POINTER_ADDR,
            "animation_table_runtime_address": animation_table,
            "expected_animation_indices": ";".join(str(value) for value in expected_animation_indices),
            "resolved_csabs": link_csab_summary(expected_animation_indices),
            "expected_speed_values": expected_speed_values,
            "n64_reference_action": n64_reference_action,
            "oot3d_basis": oot3d_basis,
            "n64_reference": n64_reference,
            "unresolved_followup": unresolved_followup,
            "decode_status": TITLE_LINK_CHILD_STATE_ROUTE_DECODE_STATUS,
        }
        for key in (
            "update_function",
            "init_function",
            "draw_function",
            "action_field_offset",
            "animation_index_field_offset",
            "speed_field_offset",
            "skel_anime_field_offset",
            "action_table_pointer_literal_address",
            "action_table_runtime_address",
            "handler_function",
            "animation_table_pointer_literal_address",
            "animation_table_runtime_address",
        ):
            row[f"{key}_hex"] = hex_u32(row[key])
        return row

    rows = [
        base_row(
            index=0,
            route_role="cycle_animation_once_idle",
            n64_reference_action="sActionFuncs[0] func_80A698F4 / func_80A6988C",
            expected_animation_indices=[0, 1, 2, 3, 4],
            expected_speed_values="native OOT3D speed=0; current animation updates once; actor+0x1A5 advances and wraps after native table slots 0..4",
            oot3d_basis="Ghidra export analysis/title_intro_link_child_state_ghidra_export/decompiled/99001_0037e568_oot3d_enhorse_link_child_action0_cycle_animation_once.c writes actor+0x6C=0, updates actor+0x1B8, advances actor+0x1A5, wraps after index 4, and plays from DAT_0037E5DC -> 0x0052711C. EnHorseLinkChild_Update dispatches actor+0x1A4 slot 0 through DAT_001D8FC4 -> 0x0052718C.",
            n64_reference="soh EnHorseLinkChild slot 0 waits for the current animation, advances animationIdx modulo sAnimations, plays once, and keeps speed zero.",
            unresolved_followup="Typed function signature cleanup remains; behavior role and literal fields are native OOT3D evidence.",
        ),
        base_row(
            index=1,
            route_role="approach_player_distance_gait",
            n64_reference_action="sActionFuncs[1] func_80A69C18",
            expected_animation_indices=[2, 3, 4, 0, 1],
            expected_speed_values="native OOT3D yaw_step=200; far_distance=1000; speed_fast=5; speed_mid=4; speed_near=2; idle_speed=0; player_actor_offset=0x20AC",
            oot3d_basis="Ghidra export analysis/title_intro_link_child_state_ghidra_export/decompiled/99004_0039c5e0_oot3d_enhorse_link_child_action1_approach_player_distance_gait.c rotates toward play+0x20AC with step 200 for animation indices 2/3/4, measures player distance, selects native speed literals 5/4/2/0, and reads animation table 0x0052711C.",
            n64_reference="soh EnHorseLinkChild slot 1 rotates toward player, then selects walk/trot/run animation indices 2/3/4 by distance, with idle fallback 0/1.",
            unresolved_followup="Typed VFP/control-flow cleanup remains before wiring exact branch ordering into runtime movement.",
        ),
        base_row(
            index=2,
            route_role="near_player_idle_or_enter_follow",
            n64_reference_action="sActionFuncs[2] func_80A699FC",
            expected_animation_indices=[0, 1],
            expected_speed_values="native OOT3D idle_speed=0; player_actor_offset=0x20AC; transitions to slot 1 when the decompiled distance condition enters the follow band",
            oot3d_basis="Ghidra export analysis/title_intro_link_child_state_ghidra_export/decompiled/99003_0039099c_oot3d_enhorse_link_child_action2_near_player_idle_or_follow.c reads play+0x20AC, uses the native animation table 0x0052711C, idles/toggles animation 0/1, and writes actor+0x1A4=1 with speed zero on the native follow transition.",
            n64_reference="soh EnHorseLinkChild slot 2 idles/toggles animation 0/1 unless player distance enters the follow band, then switches to slot 1.",
            unresolved_followup="Typed VFP/control-flow cleanup remains to name the exact OOT3D distance comparison.",
        ),
        base_row(
            index=3,
            route_role="ranch_home_or_scripted_idle",
            n64_reference_action="sActionFuncs[3] func_80A6A068",
            expected_animation_indices=[0, 1, 2, 3, 4],
            expected_speed_values="native OOT3D near_player_turn_distance=250; home_near_distance=200; distance_mid=300; speed_fast=5; speed_mid=4; speed_near=2; idle_speed=0; player_actor_offset=0x20AC",
            oot3d_basis="Ghidra export analysis/title_intro_link_child_state_ghidra_export/decompiled/99000_00292b50_oot3d_enhorse_link_child_action3_ranch_home_or_scripted_idle.c is the init-visible route because EnHorseLinkChild_Init writes actor+0x1A4=3. The handler reads play+0x20AC, uses native global 0x0051B2F4 and animation table 0x0052711C, writes transitions to slots 4/5, and carries OOT3D literals 250/300/200 and speed values 5/4/2/0.",
            n64_reference="soh EnHorseLinkChild slot 3 is the ranch/home behavior that idles or moves using animation indices 0..4 and speed bands derived from player/home distance.",
            unresolved_followup="Highest-priority typed cleanup: convert remaining Ghidra VFP/control-flow artifacts to named OOT3D conditions and identify the global title cue consumer that selects the intro gallop context.",
        ),
        base_row(
            index=4,
            route_role="timed_follow_then_return",
            n64_reference_action="sActionFuncs[4] func_80A6A7D0",
            expected_animation_indices=[2, 3, 4],
            expected_speed_values="native OOT3D timer_limit=300; force_return_distance=600; distance_fast=300; distance_mid=150; distance_near=70; speed_fast=5; speed_mid=4; speed_near=2; rotate_step=200",
            oot3d_basis="Ghidra export analysis/title_intro_link_child_state_ghidra_export/decompiled/99005_003a0ffc_oot3d_enhorse_link_child_action4_timed_follow_then_return.c increments actor+0xDCC, sets actor+0xDD0 after 300, rotates with EnHorse_RotateToPoint step 200, uses play+0x20AC, and selects native distance/speed literals 600/300/150/70 and 5/4/2.",
            n64_reference="soh EnHorseLinkChild slot 4 follows the player until a timer expires, then rotates/moves back toward home using walk/trot/run bands.",
            unresolved_followup="Typed VFP/control-flow cleanup remains before reproducing exact return-home branch order.",
        ),
        base_row(
            index=5,
            route_role="turn_to_player_neigh_idle",
            n64_reference_action="sActionFuncs[5] func_80A6A5A4",
            expected_animation_indices=[0, 1, 2],
            expected_speed_values="native OOT3D idle_speed=0; walk_speed=2; cos_turn_threshold=0.7071; rotate_step=200; sound_id=0x0100019B; player_actor_offset=0x20AC",
            oot3d_basis="Ghidra export analysis/title_intro_link_child_state_ghidra_export/decompiled/99002_00390760_oot3d_enhorse_link_child_action5_turn_to_player_neigh_idle.c reads global 0x0051B2F4, consumes flag +0x5BE, plays sound 0x0100019B, can switch to slot 4 with speed 2, otherwise idles at speed 0 and rotates toward play+0x20AC with step 200 when animation index 2 and the 0.7071 cosine threshold match.",
            n64_reference="soh EnHorseLinkChild slot 5 idles, optionally turns toward the player and plays neigh/groan behavior, then returns to an idle slot.",
            unresolved_followup="Typed VFP/control-flow cleanup remains; exact audio/global flag owner still needs to be named from OOT3D globals.",
        ),
    ]
    return rows


def build_title_link_boy_player_action_rows(
    actor_cue_rows: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for source_row in actor_cue_rows:
        if int_value(source_row.get("cue_command_id")) != 0x0000000A:
            continue
        if source_row.get("archive_path") != "scene/spot00.zar":
            continue
        if "demo_epona" not in str(source_row.get("embedded_name", "")).lower():
            continue

        cue_id = int_value(source_row.get("cue_id"))
        start_frame = int_value(source_row.get("start_frame"))
        end_frame = int_value(source_row.get("end_frame"))
        start_x = int_value(source_row.get("start_x"))
        start_y = int_value(source_row.get("start_y"))
        start_z = int_value(source_row.get("start_z"))
        end_x = int_value(source_row.get("end_x"))
        end_y = int_value(source_row.get("end_y"))
        end_z = int_value(source_row.get("end_z"))
        dx = end_x - start_x
        dy = end_y - start_y
        dz = end_z - start_z
        duration = max(0, end_frame - start_frame)
        rows.append(
            {
                "player_action_index": len(rows),
                "actor_cue_index": int_value(source_row.get("actor_cue_index")),
                "actor_role": "opening_link_adult",
                "actor_name": "PLAYER_ADULT",
                "asset_role": "actor_link_opening",
                "archive_path": "actor/zelda_link_opening.zar",
                "cmb_name": "boy/model/link_opening.cmb",
                "title_visual_csab_name": "boy/anim/uma_anim_fastrun.csab",
                "qdb_index": int_value(source_row.get("qdb_index")),
                "qdb_command_index": int_value(source_row.get("qdb_command_index")),
                "qdb_archive_path": source_row.get("archive_path", ""),
                "qdb_embedded_name": source_row.get("embedded_name", ""),
                "cue_command_id": int_value(source_row.get("cue_command_id")),
                "cue_command_id_hex": hex_u32(source_row.get("cue_command_id")),
                "cue_command_name": source_row.get("cue_command_name", ""),
                "cue_index": int_value(source_row.get("cue_index")),
                "cue_id": cue_id,
                "cue_role": f"player_action_cue_{cue_id}",
                "start_frame": start_frame,
                "end_frame": end_frame,
                "duration_frames": duration,
                "rot_x": int_value(source_row.get("rot_x")),
                "rot_y": int_value(source_row.get("rot_y")),
                "rot_z": int_value(source_row.get("rot_z")),
                "start_x": start_x,
                "start_y": start_y,
                "start_z": start_z,
                "end_x": end_x,
                "end_y": end_y,
                "end_z": end_z,
                "delta_x": dx,
                "delta_y": dy,
                "delta_z": dz,
                "normal_x": source_row.get("normal_x", 0.0),
                "normal_y": source_row.get("normal_y", 0.0),
                "normal_z": source_row.get("normal_z", 0.0),
                "oot3d_basis": "Decoded from OOT3D spot00.zar spot00_demo_epona_* QDB command 0x0A / CS_CMD_SET_PLAYER_ACTION 12-word cue entries. This is the title-intro adult Link/player-action timeline source; the actor asset binding is actor/zelda_link_opening.zar boy/model/link_opening.cmb.",
                "n64_reference": "soh/src/code/z_demo.c routes CS_CMD_SET_PLAYER_ACTION into csCtx->linkAction; soh/src/overlays/actors/ovl_player_actor/z_player.c maps linkAction->action through sCueToCsActionMap. This is used only as consumer architecture guidance pending the OOT3D player/cutscene consumer split.",
                "unresolved_followup": "Decompile the OOT3D player/cutscene consumer that maps cue IDs 36/37/38 to concrete adult-Link mounted animation/action state and verifies how boy/anim/uma_anim_fastrun.csab is selected during the title intro.",
                "decode_status": TITLE_LINK_BOY_PLAYER_ACTION_DECODE_STATUS,
            }
        )
    return rows


def title_logo_file_name(rows: list[dict[str, Any]], type_local_index: int, expected_suffix: str) -> str:
    current_index = 0
    for row in rows:
        if (
            row.get("asset_role") == "actor_title_logo"
            and str(row.get("embedded_name", "")).lower().endswith(expected_suffix.lower())
        ):
            if current_index == type_local_index:
                return str(row.get("embedded_name", ""))
            current_index += 1
    return ""


def build_title_logo_component_rows(model_animation_rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for component_index, binding in enumerate(TITLE_LOGO_COMPONENT_BINDINGS):
        cmb_index_jpeu = int_value(binding["cmb_index_jpeu"], NO_INDEX)
        cmb_index_us = int_value(binding["cmb_index_us"], NO_INDEX)
        csab_index_jpeu = int_value(binding["csab_index_jpeu"], NO_INDEX)
        csab_index_us = int_value(binding["csab_index_us"], NO_INDEX)
        rows.append(
            {
                "component_index": component_index,
                "component_role": binding["component_role"],
                "asset_index": 6,
                "archive_path": "actor/zelda_mag.zar",
                "handle_field_offset": int_value(binding["handle_field_offset"]),
                "handle_field_offset_hex": hex_u32(binding["handle_field_offset"]),
                "alpha_field_offset": int_value(binding["alpha_field_offset"]),
                "alpha_field_offset_hex": hex_u32(binding["alpha_field_offset"]),
                "cmb_index_jpeu": cmb_index_jpeu,
                "csab_index_jpeu": csab_index_jpeu,
                "cmb_name_jpeu": title_logo_file_name(model_animation_rows, cmb_index_jpeu, ".cmb"),
                "csab_name_jpeu": ""
                if csab_index_jpeu == NO_INDEX
                else title_logo_file_name(model_animation_rows, csab_index_jpeu, ".csab"),
                "cmb_index_us": cmb_index_us,
                "csab_index_us": csab_index_us,
                "cmb_name_us": title_logo_file_name(model_animation_rows, cmb_index_us, ".cmb"),
                "csab_name_us": ""
                if csab_index_us == NO_INDEX
                else title_logo_file_name(model_animation_rows, csab_index_us, ".csab"),
                "cmab_index": int_value(binding.get("cmab_index"), NO_INDEX),
                "cmab_name": str(binding.get("cmab_name", "")),
                "material_animation_runtime_owner_field_offset": int_value(
                    binding.get("material_animation_runtime_owner_field_offset"), NO_INDEX
                ),
                "material_animation_runtime_owner_field_offset_hex": hex_u32(
                    binding.get("material_animation_runtime_owner_field_offset", NO_INDEX)
                ),
                "material_animation_runtime_loop_field_offset": int_value(
                    binding.get("material_animation_runtime_loop_field_offset"), NO_INDEX
                ),
                "material_animation_runtime_loop_field_offset_hex": hex_u32(
                    binding.get("material_animation_runtime_loop_field_offset", NO_INDEX)
                ),
                "material_animation_runtime_loop_override_valid": int_value(
                    binding.get("material_animation_runtime_loop_override_valid"), 0
                ),
                "material_animation_runtime_loop_mode": int_value(
                    binding.get("material_animation_runtime_loop_mode"), 0
                ),
                "material_animation_runtime_init_function": int_value(
                    binding.get("material_animation_runtime_init_function"), NO_OFFSET
                ),
                "material_animation_runtime_init_function_hex": hex_u32(
                    binding.get("material_animation_runtime_init_function", NO_OFFSET)
                ),
                "material_animation_runtime_step_function": int_value(
                    binding.get("material_animation_runtime_step_function"), NO_OFFSET
                ),
                "material_animation_runtime_step_function_hex": hex_u32(
                    binding.get("material_animation_runtime_step_function", NO_OFFSET)
                ),
                "material_animation_binding_basis": str(
                    binding.get("material_animation_binding_basis", "")
                ),
                "binding_basis": binding["binding_basis"],
            }
        )
    return rows


def title_logo_draw_matrix_values(matrix_role: str, base_z: float, small_depth_offset: float, copyright_y: float) -> list[float]:
    tx = 0.0
    ty = 0.0
    tz = base_z
    if matrix_role.startswith("title_text_"):
        tz = base_z + small_depth_offset
    elif matrix_role.startswith("copyright_"):
        ty = copyright_y
        tz = base_z + small_depth_offset
    return [
        1.0,
        0.0,
        0.0,
        tx,
        0.0,
        1.0,
        0.0,
        ty,
        0.0,
        0.0,
        1.0,
        tz,
        0.0,
        0.0,
        0.0,
        1.0,
    ]


def build_title_logo_draw_rows(
    code_bin: Path, component_rows: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    code_data = code_bin.read_bytes() if code_bin.is_file() else b""
    decode_status = "decoded_from_enmag_draw_code_bin_literals" if code_data else "missing_code_bin"
    component_index_by_role = {
        str(row.get("component_role")): int_value(row.get("component_index"), NO_INDEX)
        for row in component_rows
    }

    base_z = read_f32_at_runtime(code_data, ENMAG_DRAW_BASE_TRANSLATE_Z_ADDR, -34.0)
    one_literal = read_f32_at_runtime(code_data, ENMAG_DRAW_ONE_ADDR, 1.0)
    small_depth_offset = read_f32_at_runtime(code_data, ENMAG_DRAW_SMALL_DEPTH_OFFSET_ADDR, 0.01)
    copyright_y = read_f32_at_runtime(code_data, ENMAG_DRAW_COPYRIGHT_TRANSLATE_Y_ADDR, -11.0)
    alpha_scale = read_f32_at_runtime(code_data, ENMAG_DRAW_ALPHA_SCALE_ADDR, 1.0 / 255.0)
    effect_vector_double_scale = read_f32_at_runtime(code_data, ENMAG_DRAW_EFFECT_VECTOR_DOUBLE_SCALE_ADDR, 2.0)
    effect_vector_half_scale = read_f32_at_runtime(code_data, ENMAG_DRAW_EFFECT_VECTOR_HALF_SCALE_ADDR, 0.5)
    effect_vector_bias = read_f32_at_runtime(code_data, ENMAG_DRAW_EFFECT_VECTOR_BIAS_ADDR, -0.5)
    renderer_flag_runtime_address = read_u32_at_runtime(code_data, ENMAG_DRAW_RENDERER_FLAG_POINTER_ADDR)
    render_context_runtime_address = read_u32_at_runtime(code_data, ENMAG_DRAW_RENDER_CONTEXT_POINTER_ADDR)
    alternate_render_context_runtime_address = read_u32_at_runtime(
        code_data, ENMAG_DRAW_ALTERNATE_RENDER_CONTEXT_POINTER_ADDR
    )

    rows: list[dict[str, Any]] = []
    for draw_index, binding in enumerate(TITLE_LOGO_DRAW_BINDINGS):
        color_pointer_address = int_value(binding["color_pointer_address"])
        light_block_pointer_address = int_value(binding["light_block_pointer_address"], NO_OFFSET)
        color_runtime_address = read_u32_at_runtime(code_data, color_pointer_address)
        color_values = read_f32_block_at_runtime(code_data, color_runtime_address, 4)
        if len(color_values) < 4:
            color_values = [one_literal, one_literal, one_literal, 0.0]
        light_block_runtime_address = 0
        light_values: list[float] = []
        if light_block_pointer_address != NO_OFFSET:
            light_block_runtime_address = read_u32_at_runtime(code_data, light_block_pointer_address)
            light_values = read_f32_block_at_runtime(code_data, light_block_runtime_address, 16)
        matrix_values = title_logo_draw_matrix_values(
            str(binding["matrix_role"]), base_z, small_depth_offset, copyright_y
        )
        rows.append(
            {
                "draw_index": draw_index,
                "submit_order": int_value(binding["submit_order"]),
                "component_index": component_index_by_role.get(
                    str(binding["component_role"]), NO_INDEX
                ),
                "component_role": binding["component_role"],
                "handle_field_offset": int_value(binding["handle_field_offset"]),
                "handle_field_offset_hex": hex_u32(binding["handle_field_offset"]),
                "alpha_field_offset": int_value(binding["alpha_field_offset"]),
                "alpha_field_offset_hex": hex_u32(binding["alpha_field_offset"]),
                "effect_alpha_field_offset": int_value(binding["effect_alpha_field_offset"]),
                "effect_alpha_field_offset_hex": hex_u32(binding["effect_alpha_field_offset"]),
                "draw_function": ENMAG_DRAW_ADDR,
                "draw_function_hex": hex_u32(ENMAG_DRAW_ADDR),
                "color_pointer_address": color_pointer_address,
                "color_pointer_address_hex": hex_u32(color_pointer_address),
                "color_runtime_address": color_runtime_address,
                "color_runtime_address_hex": hex_u32(color_runtime_address),
                "light_block_pointer_address": light_block_pointer_address,
                "light_block_pointer_address_hex": hex_u32(light_block_pointer_address),
                "light_block_runtime_address": light_block_runtime_address,
                "light_block_runtime_address_hex": hex_u32(light_block_runtime_address),
                "renderer_flag_pointer_address": ENMAG_DRAW_RENDERER_FLAG_POINTER_ADDR,
                "renderer_flag_pointer_address_hex": hex_u32(ENMAG_DRAW_RENDERER_FLAG_POINTER_ADDR),
                "renderer_flag_runtime_address": renderer_flag_runtime_address,
                "renderer_flag_runtime_address_hex": hex_u32(renderer_flag_runtime_address),
                "render_context_pointer_address": ENMAG_DRAW_RENDER_CONTEXT_POINTER_ADDR,
                "render_context_pointer_address_hex": hex_u32(ENMAG_DRAW_RENDER_CONTEXT_POINTER_ADDR),
                "render_context_runtime_address": render_context_runtime_address,
                "render_context_runtime_address_hex": hex_u32(render_context_runtime_address),
                "alternate_render_context_pointer_address": ENMAG_DRAW_ALTERNATE_RENDER_CONTEXT_POINTER_ADDR,
                "alternate_render_context_pointer_address_hex": hex_u32(
                    ENMAG_DRAW_ALTERNATE_RENDER_CONTEXT_POINTER_ADDR
                ),
                "alternate_render_context_runtime_address": alternate_render_context_runtime_address,
                "alternate_render_context_runtime_address_hex": hex_u32(
                    alternate_render_context_runtime_address
                ),
                "material_handle_function": ENMAG_DRAW_MATERIAL_HANDLE_FUNCTION,
                "material_handle_function_hex": hex_u32(ENMAG_DRAW_MATERIAL_HANDLE_FUNCTION),
                "material_slot_select_function": ENMAG_DRAW_MATERIAL_SLOT_SELECT_FUNCTION,
                "material_slot_select_function_hex": hex_u32(
                    ENMAG_DRAW_MATERIAL_SLOT_SELECT_FUNCTION
                ),
                "material_color_apply_function": ENMAG_DRAW_MATERIAL_COLOR_APPLY_FUNCTION,
                "material_color_apply_function_hex": hex_u32(
                    ENMAG_DRAW_MATERIAL_COLOR_APPLY_FUNCTION
                ),
                "matrix_copy_function": ENMAG_DRAW_MATRIX_COPY_FUNCTION,
                "matrix_copy_function_hex": hex_u32(ENMAG_DRAW_MATRIX_COPY_FUNCTION),
                "submit_function": ENMAG_DRAW_SUBMIT_FUNCTION,
                "submit_function_hex": hex_u32(ENMAG_DRAW_SUBMIT_FUNCTION),
                "light_config_reset_function": ENMAG_DRAW_LIGHT_CONFIG_RESET_FUNCTION
                if light_values
                else 0,
                "light_config_reset_function_hex": hex_u32(
                    ENMAG_DRAW_LIGHT_CONFIG_RESET_FUNCTION if light_values else 0
                ),
                "light_config_apply_function": ENMAG_DRAW_LIGHT_CONFIG_APPLY_FUNCTION
                if light_values
                else 0,
                "light_config_apply_function_hex": hex_u32(
                    ENMAG_DRAW_LIGHT_CONFIG_APPLY_FUNCTION if light_values else 0
                ),
                "light_vector_apply_function": ENMAG_DRAW_LIGHT_VECTOR_APPLY_FUNCTION
                if light_values
                else 0,
                "light_vector_apply_function_hex": hex_u32(
                    ENMAG_DRAW_LIGHT_VECTOR_APPLY_FUNCTION if light_values else 0
                ),
                "alpha_scale": alpha_scale,
                "base_translate_z": base_z,
                "small_depth_offset": small_depth_offset,
                "copyright_translate_y": copyright_y,
                "effect_vector_double_scale": effect_vector_double_scale,
                "effect_vector_half_scale": effect_vector_half_scale,
                "effect_vector_bias": effect_vector_bias,
                "base_color_r": color_values[0],
                "base_color_g": color_values[1],
                "base_color_b": color_values[2],
                "base_color_a": color_values[3],
                "matrix00": matrix_values[0],
                "matrix01": matrix_values[1],
                "matrix02": matrix_values[2],
                "matrix03": matrix_values[3],
                "matrix10": matrix_values[4],
                "matrix11": matrix_values[5],
                "matrix12": matrix_values[6],
                "matrix13": matrix_values[7],
                "matrix20": matrix_values[8],
                "matrix21": matrix_values[9],
                "matrix22": matrix_values[10],
                "matrix23": matrix_values[11],
                "matrix30": matrix_values[12],
                "matrix31": matrix_values[13],
                "matrix32": matrix_values[14],
                "matrix33": matrix_values[15],
                "matrix_row_major": matrix_row_major(matrix_values),
                "light_block_row_major": matrix_row_major(light_values) if light_values else "",
                "matrix_role": binding["matrix_role"],
                "matrix_source_addresses": binding["matrix_source_addresses"],
                "visibility_condition": binding["visibility_condition"],
                "draw_basis": binding["draw_basis"],
                "decode_status": decode_status,
            }
        )
    return rows


def build_title_logo_draw_context_rows(code_bin: Path) -> list[dict[str, Any]]:
    code_data = code_bin.read_bytes() if code_bin.is_file() else b""
    renderer_flag_runtime_address = read_u32_at_runtime(code_data, ENMAG_DRAW_RENDERER_FLAG_POINTER_ADDR)
    render_context_runtime_address = read_u32_at_runtime(code_data, ENMAG_DRAW_RENDER_CONTEXT_POINTER_ADDR)
    alternate_render_context_runtime_address = read_u32_at_runtime(
        code_data, ENMAG_DRAW_ALTERNATE_RENDER_CONTEXT_POINTER_ADDR
    )
    draw_context_decoded = (
        bool(code_data)
        and renderer_flag_runtime_address == 0x0055A21C
        and render_context_runtime_address == NATIVE_SUBMIT_MANAGER_RUNTIME_ADDRESS
        and alternate_render_context_runtime_address == NATIVE_GLOBAL_CONTEXT_RUNTIME_ADDRESS
    )
    decode_status = (
        "decoded_from_enmag_draw_submit_manager_code_bin_literals_and_ghidra_control_flow"
        if draw_context_decoded
        else "missing_or_unverified_enmag_draw_submit_manager_context"
    )
    return [
        {
            "context_index": 0,
            "context_role": "enmag_draw_small_queue_submit_manager_context",
            "draw_function": ENMAG_DRAW_ADDR,
            "draw_function_hex": hex_u32(ENMAG_DRAW_ADDR),
            "submit_function": ENMAG_DRAW_SUBMIT_FUNCTION,
            "submit_function_hex": hex_u32(ENMAG_DRAW_SUBMIT_FUNCTION),
            "renderer_flag_pointer_address": ENMAG_DRAW_RENDERER_FLAG_POINTER_ADDR,
            "renderer_flag_pointer_address_hex": hex_u32(ENMAG_DRAW_RENDERER_FLAG_POINTER_ADDR),
            "renderer_flag_runtime_address": renderer_flag_runtime_address,
            "renderer_flag_runtime_address_hex": hex_u32(renderer_flag_runtime_address),
            "render_context_pointer_address": ENMAG_DRAW_RENDER_CONTEXT_POINTER_ADDR,
            "render_context_pointer_address_hex": hex_u32(ENMAG_DRAW_RENDER_CONTEXT_POINTER_ADDR),
            "render_context_runtime_address": render_context_runtime_address,
            "render_context_runtime_address_hex": hex_u32(render_context_runtime_address),
            "alternate_render_context_pointer_address": ENMAG_DRAW_ALTERNATE_RENDER_CONTEXT_POINTER_ADDR,
            "alternate_render_context_pointer_address_hex": hex_u32(
                ENMAG_DRAW_ALTERNATE_RENDER_CONTEXT_POINTER_ADDR
            ),
            "alternate_render_context_runtime_address": alternate_render_context_runtime_address,
            "alternate_render_context_runtime_address_hex": hex_u32(
                alternate_render_context_runtime_address
            ),
            "global_context_runtime_address": NATIVE_GLOBAL_CONTEXT_RUNTIME_ADDRESS,
            "global_context_runtime_address_hex": hex_u32(NATIVE_GLOBAL_CONTEXT_RUNTIME_ADDRESS),
            "submit_manager_runtime_address": NATIVE_SUBMIT_MANAGER_RUNTIME_ADDRESS,
            "submit_manager_runtime_address_hex": hex_u32(NATIVE_SUBMIT_MANAGER_RUNTIME_ADDRESS),
            "global_context_submit_manager_offset": NATIVE_GLOBAL_CONTEXT_SUBMIT_MANAGER_OFFSET,
            "global_context_submit_manager_offset_hex": hex_u32(
                NATIVE_GLOBAL_CONTEXT_SUBMIT_MANAGER_OFFSET
            ),
            "submit_manager_constructor_function": NATIVE_SUBMIT_MANAGER_CONSTRUCTOR_FUNCTION,
            "submit_manager_constructor_function_hex": hex_u32(
                NATIVE_SUBMIT_MANAGER_CONSTRUCTOR_FUNCTION
            ),
            "submit_manager_vtable_address": NATIVE_SUBMIT_MANAGER_VTABLE_ADDRESS,
            "submit_manager_vtable_address_hex": hex_u32(NATIVE_SUBMIT_MANAGER_VTABLE_ADDRESS),
            "submit_manager_storage_size": NATIVE_SUBMIT_MANAGER_STORAGE_SIZE,
            "submit_manager_storage_size_hex": hex_u32(NATIVE_SUBMIT_MANAGER_STORAGE_SIZE),
            "small_queue_count_offset": NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_COUNT_OFFSET,
            "small_queue_count_offset_hex": hex_u32(NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_COUNT_OFFSET),
            "small_queue_storage_offset": NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_STORAGE_OFFSET,
            "small_queue_storage_offset_hex": hex_u32(NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_STORAGE_OFFSET),
            "small_queue_capacity": NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_CAPACITY,
            "small_queue_record_stride": NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_RECORD_STRIDE,
            "small_queue_record_state_byte_offset": NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_RECORD_STATE_BYTE_OFFSET,
            "small_queue_record_state_byte_value": NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_RECORD_STATE_BYTE_VALUE,
            "small_queue_record_write_function": ENMAG_DRAW_SMALL_QUEUE_RECORD_WRITE_FUNCTION,
            "small_queue_record_write_function_hex": hex_u32(ENMAG_DRAW_SMALL_QUEUE_RECORD_WRITE_FUNCTION),
            "small_queue_drain_function": NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_DRAIN_FUNCTION,
            "small_queue_drain_function_hex": hex_u32(NATIVE_SUBMIT_MANAGER_SMALL_QUEUE_DRAIN_FUNCTION),
            "pass0_drain_function": NATIVE_SUBMIT_MANAGER_PASS0_DRAIN_FUNCTION,
            "pass0_drain_function_hex": hex_u32(NATIVE_SUBMIT_MANAGER_PASS0_DRAIN_FUNCTION),
            "pass1_drain_function": NATIVE_SUBMIT_MANAGER_PASS1_DRAIN_FUNCTION,
            "pass1_drain_function_hex": hex_u32(NATIVE_SUBMIT_MANAGER_PASS1_DRAIN_FUNCTION),
            "lazy_guard_function": ENMAG_DRAW_LAZY_GUARD_FUNCTION,
            "lazy_guard_function_hex": hex_u32(ENMAG_DRAW_LAZY_GUARD_FUNCTION),
            "lazy_init_function": ENMAG_DRAW_LAZY_INIT_FUNCTION,
            "lazy_init_function_hex": hex_u32(ENMAG_DRAW_LAZY_INIT_FUNCTION),
            "draw_handle_vtable_submit_slot_offset": NATIVE_DRAW_HANDLE_VTABLE_SUBMIT_SLOT_OFFSET,
            "draw_handle_vtable_submit_slot_offset_hex": hex_u32(
                NATIVE_DRAW_HANDLE_VTABLE_SUBMIT_SLOT_OFFSET
            ),
            "oot3d_basis": "EnMag_Draw loads renderer flag 0x0055A21C, submit manager 0x005BE738, and global context 0x005BE5B8 from code.bin literals, lazily initializes the global context, then calls FUN_0033d220(handle). FUN_0033d220 writes an 8-byte small-queue record at manager+0x2130 using FUN_0031487C with state byte 0, increments manager+0x212C, and calls draw_handle.vtable+0x08.",
            "n64_reference": "N64 En_Mag confirms this actor owns title-logo draw/update state, but its draw backend is texture-rectangle/font RDP setup; it is not used as backend evidence for OOT3D CMB submit-manager routing.",
            "decode_status": decode_status,
        }
    ]


def build_title_logo_update_rows(code_bin: Path) -> list[dict[str, Any]]:
    code_data = code_bin.read_bytes() if code_bin.is_file() else b""
    decode_status = "decoded_from_enmag_update_code_bin_literals_and_callsite_immediates" if code_data else "missing_code_bin"
    max_alpha = read_f32_at_runtime(code_data, ENMAG_UPDATE_MAX_ALPHA_ADDR, 255.0)
    min_alpha = read_f32_at_runtime(code_data, ENMAG_UPDATE_MIN_ALPHA_ADDR, 0.0)
    main_alpha_step = read_f32_at_runtime(code_data, ENMAG_UPDATE_MAIN_ALPHA_STEP_ADDR, 3.0)
    title_text_effect_alpha_step = read_f32_at_runtime(
        code_data, ENMAG_UPDATE_TITLE_TEXT_EFFECT_ALPHA_STEP_ADDR, 4.25
    )
    copyright_alpha_clamp = read_f32_at_runtime(code_data, ENMAG_UPDATE_COPYRIGHT_ALPHA_CLAMP_ADDR, 255.0)

    rows: list[dict[str, Any]] = []
    for update_index, binding in enumerate(TITLE_LOGO_UPDATE_BINDINGS):
        literal_address = int_value(binding.get("literal_address"), NO_OFFSET)
        if literal_address == ENMAG_UPDATE_MAX_ALPHA_ADDR:
            literal_value = max_alpha
        elif literal_address == ENMAG_UPDATE_MIN_ALPHA_ADDR:
            literal_value = min_alpha
        elif literal_address == ENMAG_UPDATE_MAIN_ALPHA_STEP_ADDR:
            literal_value = main_alpha_step
        elif literal_address == ENMAG_UPDATE_TITLE_TEXT_EFFECT_ALPHA_STEP_ADDR:
            literal_value = title_text_effect_alpha_step
        elif literal_address == ENMAG_UPDATE_COPYRIGHT_ALPHA_CLAMP_ADDR:
            literal_value = copyright_alpha_clamp
        else:
            literal_value = 0.0
        rows.append(
            {
                "update_index": update_index,
                "phase_role": binding["phase_role"],
                "state": int_value(binding["state"]),
                "substate": int_value(binding["substate"]),
                "next_state": int_value(binding["next_state"]),
                "next_substate": int_value(binding["next_substate"]),
                "timer_field_offset": int_value(binding["timer_field_offset"], NO_INDEX),
                "timer_field_offset_hex": hex_u32(binding["timer_field_offset"]),
                "timer_initial_value": int_value(binding["timer_initial_value"]),
                "timer_initial_value_hex": hex_u32(binding["timer_initial_value"]),
                "flag_id": int_value(binding["flag_id"], NO_INDEX),
                "flag_id_hex": hex_u32(binding["flag_id"]),
                "literal_address": literal_address,
                "literal_address_hex": hex_u32(literal_address),
                "literal_value": literal_value,
                "max_alpha": max_alpha,
                "min_alpha": min_alpha,
                "main_alpha_step": main_alpha_step,
                "title_text_effect_alpha_step": title_text_effect_alpha_step,
                "copyright_alpha_clamp": copyright_alpha_clamp,
                "copyright_alpha_step_default": ENMAG_INIT_COPYRIGHT_ALPHA_STEP_DEFAULT,
                "fade_out_alpha_step_default": ENMAG_INIT_FADE_OUT_ALPHA_STEP_DEFAULT,
                "transition_copyright_alpha_step": ENMAG_UPDATE_TRANSITION_COPYRIGHT_ALPHA_STEP,
                "transition_fade_out_alpha_step": ENMAG_UPDATE_TRANSITION_FADE_OUT_ALPHA_STEP,
                "init_function": ENMAG_INIT_ADDR,
                "init_function_hex": hex_u32(ENMAG_INIT_ADDR),
                "update_function": ENMAG_UPDATE_ADDR,
                "update_function_hex": hex_u32(ENMAG_UPDATE_ADDR),
                "flags_get_env_function": ENMAG_UPDATE_FLAGS_GET_ENV_FUNCTION,
                "flags_get_env_function_hex": hex_u32(ENMAG_UPDATE_FLAGS_GET_ENV_FUNCTION),
                "get_csab_by_index_function": ENMAG_UPDATE_GET_CSAB_BY_INDEX_FUNCTION,
                "get_csab_by_index_function_hex": hex_u32(ENMAG_UPDATE_GET_CSAB_BY_INDEX_FUNCTION),
                "set_csab_function": ENMAG_UPDATE_SET_CSAB_FUNCTION,
                "set_csab_function_hex": hex_u32(ENMAG_UPDATE_SET_CSAB_FUNCTION),
                "set_csab_frame_function": ENMAG_UPDATE_SET_CSAB_FRAME_FUNCTION,
                "set_csab_frame_function_hex": hex_u32(ENMAG_UPDATE_SET_CSAB_FRAME_FUNCTION),
                "set_title_anim_state_function": ENMAG_UPDATE_SET_TITLE_ANIM_STATE_FUNCTION,
                "set_title_anim_state_function_hex": hex_u32(ENMAG_UPDATE_SET_TITLE_ANIM_STATE_FUNCTION),
                "audio_cutscene_flag_function": ENMAG_UPDATE_AUDIO_CUTSCENE_FLAG_FUNCTION,
                "audio_cutscene_flag_function_hex": hex_u32(ENMAG_UPDATE_AUDIO_CUTSCENE_FLAG_FUNCTION),
                "audio_play_sound_function": ENMAG_UPDATE_AUDIO_PLAY_SOUND_FUNCTION,
                "audio_play_sound_function_hex": hex_u32(ENMAG_UPDATE_AUDIO_PLAY_SOUND_FUNCTION),
                "alpha_field_offsets": binding["alpha_field_offsets"],
                "alpha_operation": binding["alpha_operation"],
                "alpha_delta_source": binding["alpha_delta_source"],
                "oot3d_basis": binding["oot3d_basis"],
                "n64_reference": binding["n64_reference"],
                "decode_status": decode_status,
            }
        )
    return rows


def parse_cmb_record(zar_path: str, file_index: int, name: str, data: bytes) -> dict[str, Any]:
    try:
        model = CmbModel.parse(data, f"{zar_path}!{name}")
    except Exception as exc:
        return {
            "file_index": file_index,
            "embedded_name": name,
            "parse_status": "error",
            "parse_error": str(exc),
        }
    skeleton = model.skeleton
    return {
        "file_index": file_index,
        "embedded_name": name,
        "parse_status": "decoded",
        "model_name": model.name,
        "bone_count": len(skeleton.bones) if skeleton is not None else 0,
        "material_count": len(model.materials),
        "mesh_count": len(model.meshes),
        "shape_count": len(model.shapes),
        "primitive_count": sum(len(shape.primitives) for shape in model.shapes),
        "vertex_count": sum(len(shape.positions) for shape in model.shapes),
    }


def parse_csab_record(file_index: int, name: str, data: bytes) -> dict[str, Any]:
    try:
        header = csab_header_candidates(data)
    except Exception as exc:
        return {
            "file_index": file_index,
            "embedded_name": name,
            "parse_status": "error",
            "parse_error": str(exc),
        }
    return {
        "file_index": file_index,
        "embedded_name": name,
        "parse_status": "decoded",
        "frame_count": header.get("frame_count_candidate"),
        "animated_bone_count": header.get("animated_bone_count_candidate"),
        "skeleton_bone_count": header.get("skeleton_bone_count_candidate"),
    }


def parse_qdb_record(
    asset_index: int,
    asset_role: str,
    zar_path: str,
    file_index: int,
    name: str,
    data: bytes,
    command_start: int,
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    commands: list[dict[str, Any]] = []
    actor_cue_rows: list[dict[str, Any]] = []
    view = BinaryView(data, name)
    try:
        decoded = parse_oot3d_native_cutscene_block(view, 0)
        decode_status = "decoded_qdb_native"
    except Exception as exc:
        decoded = {"commands": [], "error": str(exc)}
        decode_status = "decode_error"

    for command in decoded.get("commands", []):
        if not isinstance(command, dict):
            continue
        qdb_command_index = command_start + len(commands)
        command_id = int_value(command.get("command_id"))
        command_offset = int_value(command.get("offset"), NO_OFFSET)
        commands.append(
            {
                "qdb_command_index": qdb_command_index,
                "asset_index": asset_index,
                "asset_role": asset_role,
                "archive_path": zar_path,
                "embedded_index": file_index,
                "embedded_name": name,
                "local_command_index": int_value(command.get("index")),
                "command_offset": command_offset,
                "command_offset_hex": hex_u32(command.get("offset", NO_OFFSET)),
                "command_id": command_id,
                "command_id_hex": command.get("command_id_hex", ""),
                "command_name": command.get("name", ""),
                "category": command.get("category", ""),
                "entry_count": ""
                if command.get("entry_count") is None
                else int_value(command.get("entry_count")),
                "camera_point_count": int_value(command.get("camera_point_count")),
                "blob_size": "" if command.get("blob_size") is None else int_value(command.get("blob_size")),
                "packed_stride": int_value(command.get("packed_stride")),
                "payload_size": int_value(command.get("payload_size")),
                "total_size": int_value(command.get("total_size")),
                "raw_prefix_hex": command.get("raw_prefix_hex", ""),
            }
        )
        if (
            "demo_epona" in name.lower()
            and command.get("category") == "counted_12word_entries"
            and command_id in TITLE_INTRO_ACTOR_CUE_COMMAND_IDS
        ):
            actor_cue_rows.extend(
                parse_actor_cue_entries(
                    view,
                    asset_index,
                    asset_role,
                    zar_path,
                    file_index,
                    name,
                    qdb_command_index,
                    command,
                )
            )

    command_ids = ",".join(str(row["command_id_hex"]) for row in commands)
    qdb_row = {
        "qdb_index": 0,
        "asset_index": asset_index,
        "asset_role": asset_role,
        "archive_path": zar_path,
        "embedded_index": file_index,
        "embedded_name": name,
        "embedded_stem": Path(name.replace("\\", "/")).stem,
        "is_epona_title_demo_candidate": "demo_epona" in name.lower(),
        "size": len(data),
        "magic_hex": hex_u32(decoded.get("magic", 0)) if decode_status == "decoded_qdb_native" else "",
        "version_or_flags_hex": decoded.get("version_or_flags_hex", ""),
        "command_count": int_value(decoded.get("command_count")),
        "end_frame": int_value(decoded.get("end_frame")),
        "decoded_size": int_value(decoded.get("decoded_size")),
        "decode_status": decode_status,
        "decode_error": decoded.get("error", ""),
        "command_ref_start": command_start,
        "command_ref_count": len(commands),
        "command_ids": command_ids,
    }
    return qdb_row, commands, actor_cue_rows


def parse_actor_cue_entries(
    view: BinaryView,
    asset_index: int,
    asset_role: str,
    zar_path: str,
    file_index: int,
    name: str,
    qdb_command_index: int,
    command: dict[str, Any],
) -> list[dict[str, Any]]:
    command_offset = int_value(command.get("offset"), NO_OFFSET)
    entry_count = int_value(command.get("entry_count"))
    command_id = int_value(command.get("command_id"))
    rows: list[dict[str, Any]] = []
    for cue_index in range(entry_count):
        entry_offset = command_offset + 8 + cue_index * 0x30
        rows.append(
            {
                "actor_cue_index": 0,
                "qdb_index": NO_INDEX,
                "qdb_command_index": qdb_command_index,
                "asset_index": asset_index,
                "asset_role": asset_role,
                "archive_path": zar_path,
                "embedded_index": file_index,
                "embedded_name": name,
                "cue_command_id": command_id,
                "cue_command_id_hex": hex_u32(command_id),
                "cue_command_name": command.get("name", ""),
                "cue_index": cue_index,
                "entry_offset": entry_offset,
                "entry_offset_hex": hex_u32(entry_offset),
                "cue_id": view.s16(entry_offset + 0x00),
                "start_frame": view.s16(entry_offset + 0x02),
                "end_frame": view.s16(entry_offset + 0x04),
                "rot_x": view.s16(entry_offset + 0x06),
                "rot_y": view.s16(entry_offset + 0x08),
                "rot_z": view.s16(entry_offset + 0x0A),
                "start_x": view.s32(entry_offset + 0x0C),
                "start_y": view.s32(entry_offset + 0x10),
                "start_z": view.s32(entry_offset + 0x14),
                "end_x": view.s32(entry_offset + 0x18),
                "end_y": view.s32(entry_offset + 0x1C),
                "end_z": view.s32(entry_offset + 0x20),
                "normal_x": view.f32(entry_offset + 0x24),
                "normal_y": view.f32(entry_offset + 0x28),
                "normal_z": view.f32(entry_offset + 0x2C),
            }
        )
    return rows


def asset_primary_summary(row: dict[str, Any]) -> str:
    parts: list[str] = []
    if row.get("qdb_count"):
        parts.append(f"qdb={row.get('qdb_count')}")
    if row.get("cmb_count"):
        parts.append(f"cmb={row.get('cmb_count')}")
    if row.get("csab_count"):
        parts.append(f"csab={row.get('csab_count')}")
    if row.get("ctxb_count"):
        parts.append(f"ctxb={row.get('ctxb_count')}")
    if not row.get("exists"):
        parts.append("missing_from_romfs")
    return ", ".join(parts)


def scan_asset(
    asset_index: int,
    role: str,
    rel_path: str,
    romfs_root: Path,
    code_strings: dict[str, list[dict[str, Any]]],
    qdb_command_start: int,
) -> tuple[
    dict[str, Any],
    list[dict[str, Any]],
    list[dict[str, Any]],
    list[dict[str, Any]],
    list[dict[str, Any]],
]:
    rel_norm = normalized_rel(rel_path)
    path = romfs_root / rel_path
    row: dict[str, Any] = {
        "asset_index": asset_index,
        "role": role,
        "romfs_path": rel_path,
        "exists": path.is_file(),
        "file_size": path.stat().st_size if path.is_file() else 0,
        "code_bin_string_count": len(code_strings.get(rel_norm, [])),
        "code_bin_string_addresses": ";".join(
            item["address_hex"] for item in code_strings.get(rel_norm, [])
        ),
        "zar_file_count": 0,
        "cmb_count": 0,
        "csab_count": 0,
        "cmab_count": 0,
        "qdb_count": 0,
        "ctxb_count": 0,
        "faceb_count": 0,
        "type_counts": "",
        "primary_summary": "",
    }
    cmb_rows: list[dict[str, Any]] = []
    qdb_rows: list[dict[str, Any]] = []
    qdb_command_rows: list[dict[str, Any]] = []
    actor_cue_rows: list[dict[str, Any]] = []
    if not path.is_file() or path.suffix.lower() != ".zar":
        row["primary_summary"] = asset_primary_summary(row)
        return row, cmb_rows, qdb_rows, qdb_command_rows, actor_cue_rows

    archive = ZarArchive.from_path(path)
    type_counts = Counter(file.type_name for file in archive.files)
    row.update(
        {
            "zar_file_count": len(archive.files),
            "cmb_count": type_counts.get("cmb", 0),
            "csab_count": type_counts.get("csab", 0),
            "cmab_count": type_counts.get("cmab", 0),
            "qdb_count": type_counts.get("qdb", 0),
            "ctxb_count": type_counts.get("ctxb", 0),
            "faceb_count": type_counts.get("faceb", 0),
            "type_counts": json.dumps(dict(sorted(type_counts.items())), sort_keys=True),
        }
    )
    for file in archive.files:
        if file.type_name == "cmb" or file.name.lower().endswith(".cmb"):
            cmb_row = parse_cmb_record(rel_path, file.index, file.name, archive.read_file(file))
            cmb_row.update({"asset_index": asset_index, "asset_role": role, "archive_path": rel_path})
            cmb_rows.append(cmb_row)
        elif file.type_name == "csab" or file.name.lower().endswith(".csab"):
            csab_row = parse_csab_record(file.index, file.name, archive.read_file(file))
            csab_row.update({"asset_index": asset_index, "asset_role": role, "archive_path": rel_path})
            cmb_rows.append(csab_row)
        elif file.type_name == "qdb" or file.name.lower().endswith(".qdb"):
            qdb_row, command_rows, cue_rows = parse_qdb_record(
                asset_index,
                role,
                rel_path,
                file.index,
                file.name,
                archive.read_file(file),
                qdb_command_start + len(qdb_command_rows),
            )
            qdb_rows.append(qdb_row)
            qdb_command_rows.extend(command_rows)
            actor_cue_rows.extend(cue_rows)
    row["primary_summary"] = asset_primary_summary(row)
    return row, cmb_rows, qdb_rows, qdb_command_rows, actor_cue_rows


def load_symbol_candidates(symbols_path: Path) -> list[dict[str, Any]]:
    if not symbols_path.is_file():
        return []
    rows: list[dict[str, Any]] = []
    with symbols_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle, fieldnames=["address", "old_name", "name", "kind", "confidence", "source", "note"])
        for row in reader:
            name = row.get("name", "")
            if any(keyword in name for keyword in SYMBOL_KEYWORDS):
                rows.append(
                    {
                        "address_hex": f"0x{int(row.get('address', '0'), 16):08x}",
                        "symbol": name,
                        "kind": row.get("kind", ""),
                        "confidence": row.get("confidence", ""),
                        "source": row.get("source", ""),
                    }
                )
    rows.sort(key=lambda item: (item["symbol"], item["address_hex"]))
    return rows


def build_tables(
    romfs_root: Path = DEFAULT_ROMFS_ROOT,
    code_bin: Path = DEFAULT_CODE_BIN,
    symbols_path: Path = DEFAULT_SYMBOLS,
) -> dict[str, Any]:
    code_strings = discover_code_rom_strings(code_bin)
    asset_rows: list[dict[str, Any]] = []
    model_animation_rows: list[dict[str, Any]] = []
    qdb_rows: list[dict[str, Any]] = []
    qdb_command_rows: list[dict[str, Any]] = []
    actor_cue_rows: list[dict[str, Any]] = []
    for asset_index, (role, rel_path) in enumerate(TARGET_ASSETS):
        asset_row, model_rows, asset_qdb_rows, asset_command_rows, asset_actor_cue_rows = scan_asset(
            asset_index,
            role,
            rel_path,
            romfs_root,
            code_strings,
            len(qdb_command_rows),
        )
        asset_rows.append(asset_row)
        model_animation_rows.extend(model_rows)
        for qdb_row in asset_qdb_rows:
            qdb_row["qdb_index"] = len(qdb_rows)
            qdb_rows.append(qdb_row)
        qdb_command_rows.extend(asset_command_rows)
        actor_cue_rows.extend(asset_actor_cue_rows)

    qdb_index_by_key = {
        (row.get("archive_path"), row.get("embedded_index"), row.get("embedded_name")): row.get("qdb_index")
        for row in qdb_rows
    }
    for actor_cue_index, row in enumerate(actor_cue_rows):
        row["actor_cue_index"] = actor_cue_index
        row["qdb_index"] = qdb_index_by_key.get(
            (row.get("archive_path"), row.get("embedded_index"), row.get("embedded_name")),
            NO_INDEX,
        )

    title_relevant_code_strings = [
        {
            "romfs_path": rel,
            "addresses": ";".join(item["address_hex"] for item in refs),
            "count": len(refs),
        }
        for rel, refs in sorted(code_strings.items())
    ]
    symbol_candidates = load_symbol_candidates(symbols_path)
    title_logo_actor_init = [recover_enmag_actor_init(code_bin)]
    title_actor_scale_rows = build_title_actor_scale_rows(code_bin)
    title_actor_animation_rows = build_title_actor_animation_rows(code_bin, model_animation_rows)
    title_actor_motion_animation_rows = build_title_actor_motion_animation_rows(code_bin, model_animation_rows)
    title_horse_state_route_rows = build_title_horse_state_route_rows(code_bin, model_animation_rows)
    title_horse_cutscene_action_route_rows = build_title_horse_cutscene_action_route_rows(
        code_bin, model_animation_rows
    )
    title_skel_anime_timing_rows = build_title_skel_anime_timing_rows(code_bin)
    title_mounted_player_animation_frame_rows = (
        build_title_mounted_player_animation_frame_rows(code_bin)
    )
    title_link_child_state_route_rows = build_title_link_child_state_route_rows(
        code_bin, model_animation_rows
    )
    title_link_boy_player_action_rows = build_title_link_boy_player_action_rows(actor_cue_rows)
    title_logo_component_rows = build_title_logo_component_rows(model_animation_rows)
    title_logo_draw_rows = build_title_logo_draw_rows(code_bin, title_logo_component_rows)
    title_logo_draw_context_rows = build_title_logo_draw_context_rows(code_bin)
    title_logo_update_rows = build_title_logo_update_rows(code_bin)
    spot00_qdb_rows = [row for row in qdb_rows if row.get("archive_path") == "scene/spot00.zar"]
    summary = {
        "format": "oot3d_title_intro_source_table_v1",
        "target": "first title intro: Hyrule Field, title logo, Link/Epona gallop",
        "romfs_root": str(romfs_root),
        "code_bin": str(code_bin),
        "asset_count": len(asset_rows),
        "existing_asset_count": sum(1 for row in asset_rows if row.get("exists")),
        "missing_asset_count": sum(1 for row in asset_rows if not row.get("exists")),
        "target_qdb_count": len(qdb_rows),
        "target_qdb_decoded_count": sum(
            1 for row in qdb_rows if row.get("decode_status") == "decoded_qdb_native"
        ),
        "spot00_zar_qdb_count": len(spot00_qdb_rows),
        "spot00_zar_qdb_decoded_count": sum(
            1 for row in spot00_qdb_rows if row.get("decode_status") == "decoded_qdb_native"
        ),
        "epona_qdb_candidate_count": sum(
            1 for row in qdb_rows if row.get("is_epona_title_demo_candidate")
        ),
        "epona_actor_cue_count": len(actor_cue_rows),
        "epona_actor_cue_command_counts": dict(
            sorted(Counter(str(row.get("cue_command_id_hex")) for row in actor_cue_rows).items())
        ),
        "qdb_command_count": len(qdb_command_rows),
        "qdb_command_category_counts": dict(
            sorted(Counter(str(row.get("category")) for row in qdb_command_rows).items())
        ),
        "code_bin_target_rom_string_count": len(title_relevant_code_strings),
        "runtime_symbol_candidate_count": len(symbol_candidates),
        "title_logo_actor_init_decoded": title_logo_actor_init[0].get("decode_status")
        == "decoded_from_code_bin_actor_init_table",
        "title_actor_scale_row_count": len(title_actor_scale_rows),
        "title_actor_scales_decoded": all(
            row.get("decode_status") == TITLE_ACTOR_SCALE_DECODE_STATUS for row in title_actor_scale_rows
        ),
        "title_actor_animation_row_count": len(title_actor_animation_rows),
        "title_actor_animation_init_tables_decoded": all(
            row.get("decode_status") == TITLE_ACTOR_ANIMATION_BINDING_DECODE_STATUS
            for row in title_actor_animation_rows
        ),
        "title_actor_motion_animation_row_count": len(title_actor_motion_animation_rows),
        "title_actor_motion_animation_tables_decoded": all(
            row.get("decode_status") == TITLE_ACTOR_MOTION_ANIMATION_DECODE_STATUS
            for row in title_actor_motion_animation_rows
        ),
        "title_horse_state_route_row_count": len(title_horse_state_route_rows),
        "title_horse_state_route_decoded": all(
            row.get("decode_status") == TITLE_HORSE_STATE_ROUTE_DECODE_STATUS
            for row in title_horse_state_route_rows
        ),
        "title_horse_cutscene_action_route_row_count": len(
            title_horse_cutscene_action_route_rows
        ),
        "title_horse_cutscene_action_route_decoded": all(
            row.get("decode_status") == TITLE_HORSE_CUTSCENE_ACTION_ROUTE_DECODE_STATUS
            for row in title_horse_cutscene_action_route_rows
        ),
        "title_skel_anime_timing_row_count": len(title_skel_anime_timing_rows),
        "title_skel_anime_timing_decoded": all(
            row.get("decode_status")
            == "decoded_from_oot3d_skelanime_update_and_gamestate_init_code_bin"
            and row.get("global_update_rate") == 2
            and math.isclose(float(row.get("update_scale", 0.0)), 1.0 / 3.0, rel_tol=1e-6)
            for row in title_skel_anime_timing_rows
        ),
        "title_mounted_player_animation_frame_row_count": len(
            title_mounted_player_animation_frame_rows
        ),
        "title_mounted_player_animation_frame_decoded": all(
            row.get("decode_status")
            == "decoded_from_oot3d_mounted_player_frame_consumer_code_bin"
            for row in title_mounted_player_animation_frame_rows
        ),
        "title_link_child_state_route_row_count": len(title_link_child_state_route_rows),
        "title_link_child_state_route_decoded": all(
            row.get("decode_status") == TITLE_LINK_CHILD_STATE_ROUTE_DECODE_STATUS
            for row in title_link_child_state_route_rows
        ),
        "title_link_boy_player_action_row_count": len(title_link_boy_player_action_rows),
        "title_link_boy_player_action_decoded": all(
            row.get("decode_status") == TITLE_LINK_BOY_PLAYER_ACTION_DECODE_STATUS
            for row in title_link_boy_player_action_rows
        ),
        "title_logo_component_count": len(title_logo_component_rows),
        "title_logo_draw_row_count": len(title_logo_draw_rows),
        "title_logo_draw_static_literals_decoded": all(
            row.get("decode_status") == "decoded_from_enmag_draw_code_bin_literals"
            for row in title_logo_draw_rows
        ),
        "title_logo_draw_context_row_count": len(title_logo_draw_context_rows),
        "title_logo_draw_context_decoded": all(
            row.get("decode_status")
            == "decoded_from_enmag_draw_submit_manager_code_bin_literals_and_ghidra_control_flow"
            for row in title_logo_draw_context_rows
        ),
        "title_logo_update_row_count": len(title_logo_update_rows),
        "title_logo_update_static_literals_decoded": all(
            row.get("decode_status") == "decoded_from_enmag_update_code_bin_literals_and_callsite_immediates"
            for row in title_logo_update_rows
        ),
        "native_evidence": {
            "zar_qdb_provider": "ZARInfo::GetQDBByIndex / provider type slot qdb",
            "qdb_payload_layout": "QDB magic at +0x00, version/flags +0x04, command_count +0x08, end_frame +0x0C, command stream +0x10",
            "scene_zar": "spot00.zar contains external demo QDB payloads, including spot00_demo_epona_00/01/02",
            "title_logo_actor_init": "ACTOR_EN_MAG/OBJECT_MAG ActorInit recovered from OOT3D code.bin by matching EnMag Destroy/Update/Draw function pointers; EnMag_Init then binds zelda_mag CMB/CSAB indices.",
            "title_actor_scale_route": "EnHorseLinkChild_Init, EnHorse_Init, and EnHorseNormal_Init load Actor_SetScale scale through VFP s0 from native OOT3D code.bin literal pools; N64 horse sources only validate the gameplay init structure.",
            "title_actor_animation_route": "Opening Link boy/Epona CMB handles and actor-init CSAB table slots are decoded from OOT3D EnHorseLinkChild_Init/EnHorse_Init plus native ZAR contents; N64 cutscene horse state is used to validate that gallop is selected by cutscene/movement logic, not actor init.",
            "title_actor_motion_animation_route": "EnHorse_UpdateIngoHorseAnim classifies OOT3D actor+0x6C speed into native animation index actor+0x0E74 and indexes DAT_0033DA84[horseType][animationIndex]; for Epona, index 7 resolves through zelda_horse.zar to Anim/hl_anim_fastrun2_30.csab.",
            "title_horse_state_route": "EnHorse_Idle/SetFollowAnimation/StartMovingAnimation and MountedWalk/Trot/Gallop are decoded from OOT3D code.bin/Ghidra exports as native action/e74/table routes; N64 EnHorse is used to name and cross-check the gameplay structure only.",
            "title_horse_cutscene_action_route": "The OOT3D title-horse dispatch at 0x0026A30C maps native cue actions through 0x00526DFC to substates and init/update tables. Actions 0x24/0x40/0x41 now resolve their native e74 animation indices through 0x00526EB0, including the state-5 stand2-to-wait2 completion transition.",
            "title_skel_anime_timing": "Animation_Change mode 2 maps through SkelAnime_SetUpdate to update mode 6; SkelAnime_Update multiplies playSpeed by the GameState +0x110 update rate 2 and its code.bin 1/3 literal. Mode 0 maps to looping update mode 4 with the same native time scale.",
            "title_mounted_player_animation_frame": "Mounted Player action 0x002B7FD0 calls OOT3D frame consumer 0x004C5510, which reads the ride actor animation index/frame and writes Player SkelAnime currentFrame from code.bin scale, bias, supported-index comparisons, and VFP integer conversion semantics.",
            "title_link_boy_player_action_route": "spot00_demo_epona_* QDB command 0x0A / CS_CMD_SET_PLAYER_ACTION rows provide the native Link boy/player-action timeline for the opening title intro. N64 z_demo/z_player is only the architectural reference for the cue consumer; the exact OOT3D cue-id consumer remains to split/decompile.",
            "title_link_boy_actor_symbol_route": "EnHorseLinkChild_Update dispatches OOT3D actor+0x1A4 through DAT_001D8FC4 -> 0x0052718C; six handlers are split/exported from OOT3D Ghidra output and provide native speed/threshold/transition literals. This is retained as an actor/source-symbol route, not as the identity of the title-intro Link boy timeline.",
            "title_logo_draw_route": "EnMag_Draw native code.bin literals and helper callsites bind three zelda_mag CMB handles to slot-5 RGBA, local matrices, and FUN_0033d220 render submission.",
            "title_logo_draw_context_route": "FUN_0033d220 writes EnMag draw handles into the submit manager small queue at +0x212C/+0x2130 and immediately invokes draw_handle.vtable+0x08; this route is OOT3D CMB/submit-manager evidence, not the N64 texture-rectangle backend.",
            "title_logo_update_route": "EnMag_Init/EnMag_Update native code.bin literals and callsite immediates define title-logo alpha fields, fade states, CSAB binding, env flag gates 3/4, and file-select transition fade behavior; N64 En_Mag is used only to name/check gameplay structure.",
            "not_regular_room_cutscene": "This target is the title/game-state intro path, not the link-house transition/cutscene path.",
        },
    }
    return {
        "summary": summary,
        "assets": asset_rows,
        "model_animation_rows": model_animation_rows,
        "title_logo_actor_init": title_logo_actor_init,
        "title_actor_scale_rows": title_actor_scale_rows,
        "title_actor_animation_rows": title_actor_animation_rows,
        "title_actor_motion_animation_rows": title_actor_motion_animation_rows,
        "title_horse_state_route_rows": title_horse_state_route_rows,
        "title_horse_cutscene_action_route_rows": title_horse_cutscene_action_route_rows,
        "title_skel_anime_timing_rows": title_skel_anime_timing_rows,
        "title_mounted_player_animation_frame_rows": title_mounted_player_animation_frame_rows,
        "title_link_child_state_route_rows": title_link_child_state_route_rows,
        "title_link_boy_player_action_rows": title_link_boy_player_action_rows,
        "title_logo_component_rows": title_logo_component_rows,
        "title_logo_draw_rows": title_logo_draw_rows,
        "title_logo_draw_context_rows": title_logo_draw_context_rows,
        "title_logo_update_rows": title_logo_update_rows,
        "scene_zar_qdb_rows": qdb_rows,
        "scene_zar_qdb_command_rows": qdb_command_rows,
        "epona_actor_cue_rows": actor_cue_rows,
        "code_bin_rom_strings": title_relevant_code_strings,
        "runtime_symbol_candidates": symbol_candidates,
    }


def write_markdown(path: Path, tables: dict[str, Any]) -> None:
    summary = tables["summary"]
    assets = tables["assets"]
    qdb_rows = tables["scene_zar_qdb_rows"]
    actor_cue_rows = tables["epona_actor_cue_rows"]
    symbols = tables["runtime_symbol_candidates"]
    code_strings = tables["code_bin_rom_strings"]
    title_logo_actor_init = tables["title_logo_actor_init"]
    title_actor_scale_rows = tables["title_actor_scale_rows"]
    title_actor_animation_rows = tables["title_actor_animation_rows"]
    title_actor_motion_animation_rows = tables["title_actor_motion_animation_rows"]
    title_horse_state_route_rows = tables["title_horse_state_route_rows"]
    title_horse_cutscene_action_route_rows = tables["title_horse_cutscene_action_route_rows"]
    title_skel_anime_timing_rows = tables["title_skel_anime_timing_rows"]
    title_mounted_player_animation_frame_rows = tables[
        "title_mounted_player_animation_frame_rows"
    ]
    title_link_child_state_route_rows = tables["title_link_child_state_route_rows"]
    title_link_boy_player_action_rows = tables["title_link_boy_player_action_rows"]
    title_logo_components = tables["title_logo_component_rows"]
    title_logo_draw_rows = tables["title_logo_draw_rows"]
    title_logo_draw_context_rows = tables["title_logo_draw_context_rows"]
    title_logo_update_rows = tables["title_logo_update_rows"]
    lines = [
        "# OOT3D Title Intro Source Table",
        "",
        "Generated source map for the first title intro target: Hyrule Field backdrop, title logo, and Link/Epona riding. This is intentionally separate from the regular scene cutscene transition handoff path.",
        "",
        "## Summary",
        "",
        f"- Assets tracked: {summary['asset_count']} ({summary['existing_asset_count']} present, {summary['missing_asset_count']} missing from current ROMFS extract)",
        f"- Target ZAR QDB payloads decoded: {summary['target_qdb_decoded_count']}/{summary['target_qdb_count']}",
        f"- `spot00.zar` QDB payloads decoded: {summary['spot00_zar_qdb_decoded_count']}/{summary['spot00_zar_qdb_count']}",
        f"- Epona title-demo QDB candidates: {summary['epona_qdb_candidate_count']}",
        f"- Epona title-demo actor cue rows: {summary['epona_actor_cue_count']}",
        f"- QDB command rows: {summary['qdb_command_count']}",
        f"- `code.bin` ROM string targets: {summary['code_bin_target_rom_string_count']}",
        f"- Runtime symbol candidates: {summary['runtime_symbol_candidate_count']}",
        f"- Title logo actor init decoded: {summary['title_logo_actor_init_decoded']}",
        f"- Title actor scale rows decoded: {summary['title_actor_scales_decoded']} ({summary['title_actor_scale_row_count']} rows)",
        f"- Title actor animation rows decoded: {summary['title_actor_animation_init_tables_decoded']} ({summary['title_actor_animation_row_count']} rows)",
        f"- Title actor motion animation rows decoded: {summary['title_actor_motion_animation_tables_decoded']} ({summary['title_actor_motion_animation_row_count']} rows)",
        f"- Title horse state route rows decoded: {summary['title_horse_state_route_decoded']} ({summary['title_horse_state_route_row_count']} rows)",
        f"- Title horse cutscene action routes decoded: {summary['title_horse_cutscene_action_route_decoded']} ({summary['title_horse_cutscene_action_route_row_count']} rows)",
        f"- Title SkelAnime timing routes decoded: {summary['title_skel_anime_timing_decoded']} ({summary['title_skel_anime_timing_row_count']} rows)",
        f"- Mounted Player frame route decoded: {summary['title_mounted_player_animation_frame_decoded']} ({summary['title_mounted_player_animation_frame_row_count']} rows)",
        f"- Title Link boy player-action rows decoded: {summary['title_link_boy_player_action_decoded']} ({summary['title_link_boy_player_action_row_count']} rows)",
        f"- Title Link-boy actor-symbol route rows decoded: {summary['title_link_child_state_route_decoded']} ({summary['title_link_child_state_route_row_count']} rows)",
        f"- Title logo native components: {summary['title_logo_component_count']}",
        f"- Title logo draw rows decoded: {summary['title_logo_draw_row_count']} (static literals decoded: {summary['title_logo_draw_static_literals_decoded']})",
        f"- Title logo draw context decoded: {summary['title_logo_draw_context_decoded']} ({summary['title_logo_draw_context_row_count']} rows)",
        f"- Title logo update rows decoded: {summary['title_logo_update_row_count']} (static literals decoded: {summary['title_logo_update_static_literals_decoded']})",
        "",
        "## Native Evidence",
        "",
        "- `code.bin` contains direct ROM strings for `spot00_info.zsi`, `spot00.zar`, `zelda_link_opening.zar`, `zelda_keep_opening.zar`, `zelda_mag.zar`, and horse assets.",
        "- `spot00.zar` contains six decoded native QDB payloads; three are explicitly named `spot00_demo_epona_00/01/02`.",
        "- `zelda_link_opening.zar` contains the `link_opening` model and mounted/epona animation names (`uma_*`).",
        "- `zelda_mag.zar` contains `title_logo_jpeu` and `title_logo_us` CMBs plus their CSAB/CMAB animation assets.",
        "- `ACTOR_EN_MAG`/`OBJECT_MAG` ActorInit is recovered from `code.bin`; OOT3D `EnMag_Init` binds the title logo components by native ZAR CMB/CSAB type-local indices.",
        "- `EnHorseLinkChild_Init`, `EnHorse_Init`, and `EnHorseNormal_Init` load `Actor_SetScale` scale through VFP `s0`; OOT3D literal pools match the N64 actor-init scale/shape/focus structure, but the values below are read from `code.bin`.",
        "- Opening Link boy/Epona actor CMBs and init CSAB table slots are decoded from OOT3D actor init code and native ZAR type-local indices; the gallop visual clip is tracked separately until the OOT3D title-cutscene consumer is decompiled.",
        "- OOT3D `EnHorse_Idle`, `EnHorse_SetFollowAnimation`, `EnHorse_StartMovingAnimation`, `EnHorse_UpdateSpeed`, and mounted walk/trot/gallop states are indexed as a native gameplay horse route; N64 `EnHorse` is used to cross-check gameplay structure, while the fields, e74 values, and CSAB table are OOT3D evidence.",
        "- The title-specific horse consumer at `0x0026A30C` is decoded separately: its action map, state init/update tables, native Epona animation table, cue selector, and `stand2` to `wait2` completion route come from OOT3D `code.bin` and native CSAB metadata.",
        "- The title-intro Link boy timeline is decoded from `spot00_demo_epona_*` QDB command `0x0A` / `CS_CMD_SET_PLAYER_ACTION` rows. N64 `z_demo`/`z_player` is used only to identify the expected player-action consumer architecture.",
        "- OOT3D `EnHorseLinkChild_Update` dispatches `actor+0x1A4` through native action table `0x0052718C`; that evidence is retained as an actor/source-symbol route because it loads `actor/zelda_link_opening.zar`, not as proof that the title intro uses a child-Link gameplay identity.",
        "- `EnMag_Draw` decodes three native zelda_mag CMB handle submissions: slot-5 RGBA, local 3x4 matrix copy to handle+0x7C, handle+0xAC enable, and `FUN_0033d220` submit.",
        "- `FUN_0033d220` is decoded as the OOT3D title-logo small-queue submit path: it writes manager+0x2130 records using `FUN_0031487C`, increments manager+0x212C, and invokes draw_handle.vtable+0x08. N64 En_Mag only supplies actor-structure context; its texture-rectangle backend is not used.",
        "- `EnMag_Update` preserves the N64 title-logo gameplay state shape (`initial/fade-in/display/fade-out` and env flags 3/4) but OOT3D's alpha fields, timers, CMB/CSAB calls, and extra transition states are decoded from `code.bin`.",
        "- `EndTitle_*` is not treated as this target; it belongs to the end-title texture path and is only format-adjacent.",
        "",
        "## Assets",
        "",
        "| index | role | path | present | code refs | summary |",
        "| ---: | --- | --- | --- | ---: | --- |",
    ]
    for row in assets:
        lines.append(
            f"| {row['asset_index']} | `{row['role']}` | `{row['romfs_path']}` | {row['exists']} | {row['code_bin_string_count']} | {row['primary_summary']} |"
        )
    lines.extend(
        [
            "",
            "## `spot00.zar` Title-Demo QDB Payloads",
            "",
            "| index | embedded name | epona candidate | commands | end frame | ids |",
            "| ---: | --- | --- | ---: | ---: | --- |",
        ]
    )
    spot00_qdb_rows = [row for row in qdb_rows if row.get("archive_path") == "scene/spot00.zar"]
    for row in spot00_qdb_rows:
        lines.append(
            f"| {row['qdb_index']} | `{row['embedded_name']}` | {row['is_epona_title_demo_candidate']} | {row['command_count']} | {row['end_frame']} | `{row['command_ids']}` |"
        )
    other_qdb_rows = [row for row in qdb_rows if row.get("archive_path") != "scene/spot00.zar"]
    if other_qdb_rows:
        lines.extend(
            [
                "",
                "## Other Target-ZAR QDB Payloads",
                "",
                "These are indexed because they sit in target-related archives, but they are not treated as the Hyrule Field title gallop timeline unless later native code evidence binds them.",
                "",
                "| index | archive | embedded name | commands | end frame |",
                "| ---: | --- | --- | ---: | ---: |",
            ]
        )
        for row in other_qdb_rows:
            lines.append(
                f"| {row['qdb_index']} | `{row['archive_path']}` | `{row['embedded_name']}` | {row['command_count']} | {row['end_frame']} |"
            )
    lines.extend(
        [
            "",
            "## Title Logo Actor",
            "",
            "| actor | object | init row offset | init | destroy | update | draw | status |",
            "| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    for row in title_logo_actor_init:
        lines.append(
            f"| `{row['actor_name']}` `0x{row['actor_id']:04X}` | `{row['object_name']}` `0x{row['object_id']:04X}` | `{row['actor_init_file_offset_hex']}` | `0x{row['init_function']:08X}` | `0x{row['destroy_function']:08X}` | `0x{row['update_function']:08X}` | `0x{row['draw_function']:08X}` | {row['decode_status']} |"
        )
    lines.extend(
        [
            "",
            "## Title Actor Scale/Shape",
            "",
            "Rows are generated from OOT3D actor init callsites and literal pools. The N64 horse actor files are used only as semantic cross-checks for `Actor_SetScale`, gravity, shape shadow, and focus-y initialization.",
            "",
            "| role | actor | init | setscale call | scale literal | scale | gravity | shadow | focus y | status |",
            "| --- | --- | ---: | ---: | ---: | ---: | ---: | --- | ---: | --- |",
        ]
    )
    for row in title_actor_scale_rows:
        shadow = f"{row['shadow_y_offset']:.9g}/{row['shadow_scale']:.9g}"
        lines.append(
            f"| `{row['actor_role']}` | `{row['actor_name']}` | `{row['init_function_hex']}` | `{row['actor_set_scale_callsite_hex']}` | `{row['scale_literal_address_hex']}` | {row['actor_scale']:.9g} | {row['gravity']:.9g} | {shadow} | {row['focus_y_offset']:.9g} | {row['decode_status']} |"
        )
    lines.extend(
        [
            "",
            "## Title Actor Model/Animation Binding",
            "",
            "Rows separate actor-init animation tables from the title visual gallop clip. The init table indices are decoded from OOT3D `code.bin`; the N64 source is only the semantic reference that gallop belongs to cutscene/movement state, not the init default.",
            "",
            "| role | archive | CMB | init table | init CSAB | title visual CSAB | status |",
            "| --- | --- | --- | ---: | --- | --- | --- |",
        ]
    )
    for row in title_actor_animation_rows:
        init_table = f"{row['init_animation_table_runtime_address_hex']}[{row['init_animation_slot']}]"
        init_csab = f"{row['init_csab_type_index']} `{row['init_csab_name']}`"
        title_csab = f"{row['title_visual_csab_type_index']} `{row['title_visual_csab_name']}`"
        lines.append(
            f"| `{row['actor_role']}` | `{row['archive_path']}` | {row['cmb_type_index']} `{row['cmb_name']}` | `{init_table}` | {init_csab} | {title_csab} | {row['decode_status']} |"
        )
    lines.extend(
        [
            "",
            "## Title Link Boy Player-Action Timeline",
            "",
            "Rows are decoded from the native `spot00_demo_epona_*` QDB `CS_CMD_SET_PLAYER_ACTION` command. These rows are the Link boy/player timeline for the title intro. N64 `z_demo`/`z_player` is only the architectural reference for how a player-action cue is consumed; the exact OOT3D cue-id-to-action mapping remains pending native decompilation.",
            "",
            "| row | qdb | cue | frames | start xyz | end xyz | rot xyz | delta | status |",
            "| ---: | ---: | --- | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in title_link_boy_player_action_rows:
        delta = f"{row['delta_x']},{row['delta_y']},{row['delta_z']}"
        lines.append(
            f"| {row['player_action_index']} | {row['qdb_index']} | `{row['cue_role']}` | {row['start_frame']}..{row['end_frame']} | {row['start_x']},{row['start_y']},{row['start_z']} | {row['end_x']},{row['end_y']},{row['end_z']} | {row['rot_x']},{row['rot_y']},{row['rot_z']} | {delta} | {row['decode_status']} |"
        )
    lines.extend(
        [
            "",
            "## Title Actor Motion Animation Route",
            "",
            "Rows are generated from OOT3D `EnHorse_UpdateIngoHorseAnim` literal pools and native CSAB tables. The N64 horse cutscene code is used as a semantic cross-check that cutscene/movement state selects gallop and play speed; the field offsets, thresholds, table pointer, and CSAB names below are OOT3D evidence.",
            "",
            "| role | source | fields | thresholds | table | idle | walk | trot | fast | status |",
            "| --- | ---: | --- | --- | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in title_actor_motion_animation_rows:
        fields = (
            f"speed `{row['speed_field_offset_hex']}` anim `{row['animation_index_field_offset_hex']}` "
            f"type `{row['horse_type_field_offset_hex']}` skel `{row['skel_anime_field_offset_hex']}`"
        )
        thresholds = f"zero {row['zero_speed']:.9g}; walk <= {row['walk_threshold']:.9g}; fast > {row['fast_threshold']:.9g}"
        table = (
            f"`{row['table_pointer_literal_address_hex']}` -> `{row['animation_table_outer_runtime_address_hex']}`"
            f"[{row['horse_type_index']}] -> `{row['animation_table_runtime_address_hex']}`"
        )
        idle = f"{row['idle_animation_index']}->{row['idle_csab_type_index']} `{row['idle_csab_name']}`"
        walk = f"{row['walk_animation_index']}->{row['walk_csab_type_index']} `{row['walk_csab_name']}`"
        trot = f"{row['trot_animation_index']}->{row['trot_csab_type_index']} `{row['trot_csab_name']}`"
        fast = f"{row['fast_animation_index']}->{row['fast_csab_type_index']} `{row['fast_csab_name']}`"
        lines.append(
            f"| `{row['actor_role']}` | `{row['source_function_hex']}` | {fields} | {thresholds} | {table} | {idle} | {walk} | {trot} | {fast} | {row['decode_status']} |"
        )
    lines.extend(
        [
            "",
            "## Title Horse State Route",
            "",
            "Rows are generated from focused OOT3D Ghidra exports and `code.bin` literal/table reads. N64 `EnHorse` is used as the structural gameplay reference for naming and expected state shape; OOT3D source functions, fields, e74 values, thresholds, and CSAB names below are native evidence.",
            "",
            "| route | source | action | e74 route | table | literals | typed gallop | CSABs | unresolved | status |",
            "| --- | ---: | ---: | --- | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in title_horse_state_route_rows:
        action = "" if row["action_value"] == NO_INDEX else str(row["action_value"])
        table = ""
        if row["animation_table_runtime_address"] != NO_OFFSET:
            table = (
                f"`{row['table_pointer_literal_address_hex']}` -> "
                f"`{row['animation_table_outer_runtime_address_hex']}`[{row['horse_type_index']}] -> "
                f"`{row['animation_table_runtime_address_hex']}`"
            )
        typed_gallop = ""
        if row["route_role"] == "mounted_gallop_state":
            typed_gallop = (
                f"forced {row['forced_speed']:.9g}; speed*{row['gallop_play_speed_scale']:.9g} "
                f"clamp {row['gallop_play_speed_min']:.9g}-{row['gallop_play_speed_max']:.9g}; "
                f"switch {row['gallop_play_speed_switch']:.9g}; "
                f"fast {row['gallop_fast_animation_index']}->{row['gallop_fast_csab_type_index']} "
                f"`{row['gallop_fast_csab_name']}`; "
                f"carrot {row['gallop_carrot_animation_index']}->{row['gallop_carrot_csab_type_index']} "
                f"`{row['gallop_carrot_csab_name']}`"
            )
        lines.append(
            f"| `{row['route_role']}` | `{row['source_function_hex']}` | {action} | {row['animation_indices']} | {table} | `{row['primary_literal_values']}` | {typed_gallop} | `{row['resolved_csabs']}` | {row['unresolved_followup']} | {row['decode_status']} |"
        )
    lines.extend(
        [
            "",
            "## Title Horse Cutscene Action Route",
            "",
            "These rows are the title-specific OOT3D cue consumer, distinct from mounted gameplay states. Action ids and substates are read from the native dispatch map; init/update handlers, animation indices, CSAB names/max frames, selector bits, and completion parameters are code/archive evidence used directly by the runtime.",
            "",
            "| action | state | init/update | position | initial | alternate selector | completion | motion | status |",
            "| ---: | ---: | --- | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in title_horse_cutscene_action_route_rows:
        alternate = ""
        if row["alternate_animation_index"] != NO_INDEX:
            alternate = (
                f"word {row['selector_word_index']} == `{row['selector_bits_hex']}` -> "
                f"{row['alternate_animation_index']} `{row['alternate_csab_name']}`"
            )
        completion = ""
        if row["completion_animation_index"] != NO_INDEX:
            completion = (
                f"{row['completion_animation_index']} `{row['completion_csab_name']}`; "
                f"speed {row['completion_play_speed']:.9g}; morph {row['completion_morph_frames']:.9g}; "
                f"modes {row['completion_first_mode']}/{row['completion_loop_mode']} "
                f"at `{row['completion_first_mode_instruction_address_hex']}`/"
                f"`{row['completion_loop_mode_instruction_address_hex']}`"
            )
        motion = (
            f"speed {row['fixed_speed']:.9g}; yaw step {row['yaw_step']}; "
            f"epsilon {row['target_epsilon']:.9g}; anim scale {row['play_speed_scale']:.9g}"
        )
        lines.append(
            f"| `{row['action_id_hex']}` | {row['substate']} | `{row['init_function_hex']}` / `{row['update_function_hex']}` | reset={bool(row['reset_position_on_entry'])} | {row['initial_animation_index']} `{row['initial_csab_name']}` max {row['initial_csab_max_frame']}; mode {row['initial_change_mode']} at `{row['initial_change_mode_instruction_address_hex']}`; morph {row['initial_morph_frames']:.9g} at `{row['initial_morph_literal_address_hex']}` | {alternate} | {completion} | {motion} | {row['decode_status']} |"
        )
    lines.extend(
        [
            "",
            "## Native SkelAnime Timing",
            "",
            "These rows decode the shared OOT3D animation clock used by the title actors. The emulator trace validates the resulting 3.6 play speed to 2.4 visible-frame delta, but no captured value is used as runtime input.",
            "",
            "| role | change/update mode | update scale | global rate | boundary | functions | status |",
            "| --- | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in title_skel_anime_timing_rows:
        lines.append(
            f"| `{row['timing_role']}` | {row['change_mode']}/{row['update_mode']} | "
            f"`{row['update_scale_literal_address_hex']}` = {row['update_scale']:.9g} | "
            f"`{row['global_context_pointer_address_hex']}` + 0x{row['global_update_rate_offset']:x} = {row['global_update_rate']} "
            f"(`{row['update_rate_initializer_instruction_address_hex']}`) | "
            f"`{row['terminal_behavior']}` | `{row['set_update_function_hex']}` / "
            f"`{row['update_function_hex']}` | {row['decode_status']} |"
        )
    lines.extend(
        [
            "",
            "## Mounted Player Animation Frame",
            "",
            "This is the OOT3D mounted-Player frame consumer, separate from the horse's generic SkelAnime clock. Values and supported animation indices are decoded from `code.bin`; emulator traces are validation only.",
            "",
            "| role | source | supported horse animations | frame expression | target | status |",
            "| --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in title_mounted_player_animation_frame_rows:
        lines.append(
            f"| `{row['frame_role']}` | `{row['frame_function_hex']}` | "
            f"scaled {row['scaled_horse_animation_indices']} (`{row['scaled_horse_animation_index_mask_hex']}`); "
            f"otherwise `{row['unmatched_horse_animation_behavior']}` | "
            f"`int(frame * {row['frame_scale']:.9g} + {row['frame_bias']:.9g})` from "
            f"`{row['frame_scale_literal_address_hex']}` / `{row['frame_bias_literal_address_hex']}` | "
            f"Player+0x{row['player_skel_anime_offset']:x}+0x{row['player_current_frame_offset']:x} "
            f"at `{row['frame_store_instruction_address_hex']}` | {row['decode_status']} |"
        )
    lines.extend(
        [
            "",
            "## Title Link-Boy Actor-Symbol Route",
            "",
            "Rows are generated from OOT3D `EnHorseLinkChild_Update` action-table dispatch, native CSAB table reads, and the focused Ghidra export of all six action handlers. This route is retained because the native actor/source-symbol path loads `actor/zelda_link_opening.zar`; it is not the identity of the title-intro Link boy timeline. N64 `EnHorseLinkChild` is used only as a gameplay-structure naming reference, while handler addresses, fields, table addresses, speed/threshold literals, transitions, and CSAB names below are OOT3D evidence.",
            "",
            "| slot | route | handler | fields | action table | animation table | CSABs | N64 structure | unresolved | status |",
            "| ---: | --- | ---: | --- | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in title_link_child_state_route_rows:
        fields = (
            f"action `{row['action_field_offset_hex']}` anim `{row['animation_index_field_offset_hex']}` "
            f"speed `{row['speed_field_offset_hex']}` skel `{row['skel_anime_field_offset_hex']}`"
        )
        action_table = (
            f"`{row['action_table_pointer_literal_address_hex']}` -> "
            f"`{row['action_table_runtime_address_hex']}`[{row['action_slot']}]"
        )
        animation_table = (
            f"`{row['animation_table_pointer_literal_address_hex']}` -> "
            f"`{row['animation_table_runtime_address_hex']}` slots {row['expected_animation_indices']}"
        )
        lines.append(
            f"| {row['action_slot']} | `{row['route_role']}` | `{row['handler_function_hex']}` | {fields} | {action_table} | {animation_table} | `{row['resolved_csabs']}` | `{row['n64_reference_action']}` | {row['unresolved_followup']} | {row['decode_status']} |"
        )
    lines.extend(
        [
            "",
            "## Title Logo Components",
            "",
            "| component | handle | alpha | JP/EU CMB | JP/EU CSAB | US CMB | US CSAB | CMAB | runtime loop |",
            "| --- | ---: | ---: | --- | --- | --- | --- | --- | ---: |",
        ]
    )
    for row in title_logo_components:
        lines.append(
            f"| `{row['component_role']}` | `{row['handle_field_offset_hex']}` | `{row['alpha_field_offset_hex']}` | `{row['cmb_name_jpeu']}` | `{row['csab_name_jpeu']}` | `{row['cmb_name_us']}` | `{row['csab_name_us']}` | `{row['cmab_name']}` | `{row['material_animation_runtime_loop_mode'] if row['material_animation_runtime_loop_override_valid'] else ''}` |"
        )
    lines.extend(
        [
            "",
            "## Title Logo Draw Route",
            "",
            "Rows are generated from OOT3D `EnMag_Draw` code.bin literals and helper callsites. Matrices are expanded from the native affine 3x4 writes into row-major 4x4 diagnostics for the engine table; projection/render-context parity is intentionally not claimed here.",
            "",
            "| order | component | handle | alpha | effect | matrix | color ptr | rgba template | submit |",
            "| ---: | --- | ---: | ---: | ---: | --- | ---: | --- | ---: |",
        ]
    )
    for row in title_logo_draw_rows:
        color = f"{row['base_color_r']:.3g},{row['base_color_g']:.3g},{row['base_color_b']:.3g},{row['base_color_a']:.3g}"
        lines.append(
            f"| {row['submit_order']} | `{row['component_role']}` | `{row['handle_field_offset_hex']}` | `{row['alpha_field_offset_hex']}` | `{row['effect_alpha_field_offset_hex']}` | `{row['matrix_role']}` `{row['matrix_row_major']}` | `{row['color_runtime_address_hex']}` | `{color}` alpha*{row['alpha_scale']:.9g} | `{row['submit_function_hex']}` |"
        )
    lines.extend(
        [
            "",
            "## Title Logo Draw Context",
            "",
            "Rows are generated from OOT3D `EnMag_Draw` code.bin literals plus focused Ghidra control-flow exports for `FUN_0033d220`, `FUN_0031487C`, the submit-manager constructor, and the small-queue drain. The N64 source is used only to confirm that `En_Mag` owns the title-logo actor/update/draw responsibility.",
            "",
            "| role | manager | global | small queue | record | drain | callback | status |",
            "| --- | ---: | ---: | --- | --- | --- | --- | --- |",
        ]
    )
    for row in title_logo_draw_context_rows:
        small_queue = f"{row['small_queue_count_offset_hex']}/{row['small_queue_storage_offset_hex']} cap {row['small_queue_capacity']}"
        record = f"{row['small_queue_record_stride']}B state+{row['small_queue_record_state_byte_offset']}={row['small_queue_record_state_byte_value']}"
        drain = f"{row['small_queue_drain_function_hex']} pass0 {row['pass0_drain_function_hex']} pass1 {row['pass1_drain_function_hex']}"
        callback = f"draw_handle.vtable+{row['draw_handle_vtable_submit_slot_offset_hex']}"
        lines.append(
            f"| `{row['context_role']}` | `{row['submit_manager_runtime_address_hex']}` | `{row['global_context_runtime_address_hex']}` | `{small_queue}` | `{record}` via `{row['small_queue_record_write_function_hex']}` | `{drain}` | `{callback}` | {row['decode_status']} |"
        )
    lines.extend(
        [
            "",
            "## Title Logo Update Route",
            "",
            "Rows are generated from OOT3D `EnMag_Init`/`EnMag_Update` literals and callsite immediates. The N64 source is used as a semantic naming reference for the state shape, while the field offsets, timers, and alpha steps below are OOT3D evidence.",
            "",
            "| phase | state | sub | next | timer | flag | alpha fields | operation | literal |",
            "| --- | ---: | ---: | --- | --- | ---: | --- | --- | --- |",
        ]
    )
    for row in title_logo_update_rows:
        timer = "" if row["timer_field_offset"] == NO_INDEX else f"`{row['timer_field_offset_hex']}`={row['timer_initial_value']}"
        flag = "" if row["flag_id"] == NO_INDEX else str(row["flag_id"])
        literal = "" if row["literal_address"] == NO_OFFSET else f"`{row['literal_address_hex']}`={row['literal_value']:.9g}"
        lines.append(
            f"| `{row['phase_role']}` | {row['state']} | {row['substate']} | {row['next_state']}/{row['next_substate']} | {timer} | {flag} | `{row['alpha_field_offsets']}` | `{row['alpha_operation']}` | {literal} |"
        )
    lines.extend(
        [
            "",
            "## Epona Title-Demo Actor Cue Rows",
            "",
            "`0x0A` is the named player-action command; `0x3E` remains an OOT3D-native actor cue command until the runtime consumer is named from code. Rows below are decoded from the QDB 12-word entry layout, not from demo-side placement.",
            "",
            "| cue | qdb | command | cue id | frames | start xyz | end xyz | rot xyz |",
            "| ---: | ---: | --- | ---: | --- | --- | --- | --- |",
        ]
    )
    for row in actor_cue_rows[:40]:
        lines.append(
            f"| {row['actor_cue_index']} | {row['qdb_index']} | `{row['cue_command_id_hex']}` | {row['cue_id']} | {row['start_frame']}..{row['end_frame']} | {row['start_x']},{row['start_y']},{row['start_z']} | {row['end_x']},{row['end_y']},{row['end_z']} | {row['rot_x']},{row['rot_y']},{row['rot_z']} |"
        )
    lines.extend(
        [
            "",
            "## Code ROM Strings",
            "",
            "| path | addresses |",
            "| --- | --- |",
        ]
    )
    for row in code_strings:
        lines.append(f"| `{row['romfs_path']}` | `{row['addresses']}` |")
    lines.extend(
        [
            "",
            "## Runtime Candidates",
            "",
            "| address | symbol |",
            "| ---: | --- |",
        ]
    )
    for row in symbols[:80]:
        lines.append(f"| `{row['address_hex']}` | `{row['symbol']}` |")
    lines.extend(
        [
            "",
            "## Outputs",
            "",
            "- `analysis/title_intro_source_table.json`",
            "- `analysis/title_intro_asset_table.csv`",
            "- `analysis/title_intro_scene_zar_qdb_table.csv`",
            "- `analysis/title_intro_scene_zar_qdb_command_table.csv`",
            "- `analysis/title_intro_epona_actor_cue_table.csv`",
            "- `analysis/title_intro_logo_actor_init_table.csv`",
            "- `analysis/title_intro_actor_scale_table.csv`",
            "- `analysis/title_intro_actor_animation_table.csv`",
            "- `analysis/title_intro_actor_motion_animation_table.csv`",
            "- `analysis/title_intro_horse_state_route_table.csv`",
            "- `analysis/title_intro_link_boy_player_action_table.csv`",
            "- `analysis/title_intro_link_child_state_route_table.csv`",
            "- `analysis/title_intro_logo_component_table.csv`",
            "- `analysis/title_intro_logo_draw_table.csv`",
            "- `analysis/title_intro_logo_update_table.csv`",
            "- `analysis/title_intro_runtime_symbol_candidates.csv`",
            "- `include/oot3d/title_intro_source_table.h`",
            "- `src/code/z_title_intro_source_table.c`",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_header(path: Path) -> None:
    lines = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "typedef struct Oot3dTitleIntroAssetSourceRow {",
        "    uint16_t assetIndex;",
        "    const char* role;",
        "    const char* romfsPath;",
        "    uint8_t exists;",
        "    uint32_t fileSize;",
        "    uint16_t zarFileCount;",
        "    uint16_t cmbCount;",
        "    uint16_t csabCount;",
        "    uint16_t cmabCount;",
        "    uint16_t qdbCount;",
        "    uint16_t ctxbCount;",
        "    uint16_t facebCount;",
        "    const char* codeBinStringAddresses;",
        "    const char* primarySummary;",
        "} Oot3dTitleIntroAssetSourceRow;",
        "",
        "typedef struct Oot3dTitleIntroQdbSourceRow {",
        "    uint16_t qdbIndex;",
        "    uint16_t assetIndex;",
        "    uint16_t embeddedIndex;",
        "    uint8_t isEponaTitleDemoCandidate;",
        "    uint32_t size;",
        "    uint32_t commandCount;",
        "    int32_t endFrame;",
        "    uint32_t decodedSize;",
        "    uint32_t commandRefStart;",
        "    uint32_t commandRefCount;",
        "    const char* archivePath;",
        "    const char* embeddedName;",
        "    const char* commandIds;",
        "} Oot3dTitleIntroQdbSourceRow;",
        "",
        "typedef struct Oot3dTitleIntroQdbCommandRow {",
        "    uint32_t qdbCommandIndex;",
        "    uint16_t assetIndex;",
        "    uint16_t embeddedIndex;",
        "    uint16_t localCommandIndex;",
        "    uint32_t commandOffset;",
        "    uint32_t commandId;",
        "    uint32_t cameraPointCount;",
        "    uint32_t totalSize;",
        "    const char* archivePath;",
        "    const char* embeddedName;",
        "    const char* commandName;",
        "    const char* category;",
        "} Oot3dTitleIntroQdbCommandRow;",
        "",
        "typedef struct Oot3dTitleIntroActorCueRow {",
        "    uint32_t actorCueIndex;",
        "    uint16_t qdbIndex;",
        "    uint32_t qdbCommandIndex;",
        "    uint32_t cueCommandId;",
        "    uint16_t cueIndex;",
        "    int16_t cueId;",
        "    int16_t startFrame;",
        "    int16_t endFrame;",
        "    int16_t rotX;",
        "    int16_t rotY;",
        "    int16_t rotZ;",
        "    int32_t startX;",
        "    int32_t startY;",
        "    int32_t startZ;",
        "    int32_t endX;",
        "    int32_t endY;",
        "    int32_t endZ;",
        "    float normalX;",
        "    float normalY;",
        "    float normalZ;",
        "    const char* archivePath;",
        "    const char* embeddedName;",
        "} Oot3dTitleIntroActorCueRow;",
        "",
        "typedef struct Oot3dTitleIntroActorInitSourceRow {",
        "    uint16_t actorInitIndex;",
        "    uint32_t actorInitFileOffset;",
        "    uint16_t actorId;",
        "    uint8_t actorCategory;",
        "    uint32_t flags;",
        "    uint16_t objectId;",
        "    uint16_t instanceSize;",
        "    uint32_t initFunction;",
        "    uint32_t destroyFunction;",
        "    uint32_t updateFunction;",
        "    uint32_t drawFunction;",
        "    const char* actorName;",
        "    const char* objectName;",
        "    const char* decodeStatus;",
        "    const char* basis;",
        "} Oot3dTitleIntroActorInitSourceRow;",
        "",
        "typedef struct Oot3dTitleIntroActorScaleRow {",
        "    uint16_t scaleIndex;",
        "    const char* actorRole;",
        "    const char* actorName;",
        "    const char* archiveRole;",
        "    uint32_t initFunction;",
        "    uint32_t updateFunction;",
        "    uint32_t drawFunction;",
        "    uint32_t actorSetScaleCallsite;",
        "    uint32_t actorSetScaleFunction;",
        "    uint32_t scaleLiteralAddress;",
        "    float actorScale;",
        "    uint32_t gravityLiteralAddress;",
        "    float gravity;",
        "    uint32_t shadowYOffsetLiteralAddress;",
        "    float shadowYOffset;",
        "    uint32_t shadowScaleLiteralAddress;",
        "    float shadowScale;",
        "    uint32_t focusYOffsetLiteralAddress;",
        "    float focusYOffset;",
        "    const char* oot3dBasis;",
        "    const char* n64Reference;",
        "    const char* decodeStatus;",
        "} Oot3dTitleIntroActorScaleRow;",
        "",
        "typedef struct Oot3dTitleIntroActorAnimationRow {",
        "    uint16_t animationIndex;",
        "    const char* actorRole;",
        "    const char* actorName;",
        "    const char* assetRole;",
        "    const char* archivePath;",
        "    uint16_t cmbTypeIndex;",
        "    uint16_t cmbFileIndex;",
        "    const char* cmbName;",
        "    uint32_t animationTablePointerLiteralAddress;",
        "    uint32_t animationTableOuterRuntimeAddress;",
        "    uint16_t animationTableOuterIndex;",
        "    uint32_t initAnimationTableRuntimeAddress;",
        "    uint16_t initAnimationSlot;",
        "    uint16_t initCsabTypeIndex;",
        "    uint16_t initCsabFileIndex;",
        "    const char* initCsabName;",
        "    uint16_t titleVisualCsabTypeIndex;",
        "    uint16_t titleVisualCsabFileIndex;",
        "    const char* titleVisualCsabName;",
        "    uint32_t actorInitFunction;",
        "    uint32_t actorUpdateFunction;",
        "    uint32_t actorDrawFunction;",
        "    uint32_t zarGetCmbByIndexCallsite;",
        "    uint32_t animationPlayOnceCallsite;",
        "    uint32_t titleCueSemanticReferenceFunction;",
        "    const char* titleCueRole;",
        "    const char* oot3dBasis;",
        "    const char* n64Reference;",
        "    const char* decodeStatus;",
        "} Oot3dTitleIntroActorAnimationRow;",
        "",
        "typedef struct Oot3dTitleIntroActorMotionAnimationRow {",
        "    uint16_t motionIndex;",
        "    const char* actorRole;",
        "    const char* actorName;",
        "    const char* assetRole;",
        "    const char* archivePath;",
        "    uint16_t horseTypeIndex;",
        "    uint16_t actionFieldOffset;",
        "    uint16_t speedFieldOffset;",
        "    uint16_t animationIndexFieldOffset;",
        "    uint16_t horseTypeFieldOffset;",
        "    uint16_t skelAnimeFieldOffset;",
        "    uint32_t sourceFunction;",
        "    uint32_t tablePointerLiteralAddress;",
        "    uint32_t animationTableOuterRuntimeAddress;",
        "    uint32_t animationTableRuntimeAddress;",
        "    uint32_t zeroSpeedLiteralAddress;",
        "    float zeroSpeed;",
        "    uint32_t walkThresholdLiteralAddress;",
        "    float walkThreshold;",
        "    uint32_t fastThresholdLiteralAddress;",
        "    float fastThreshold;",
        "    uint32_t basePlaySpeedScaleLiteralAddress;",
        "    float basePlaySpeedScale;",
        "    uint32_t idlePlaySpeedScaleLiteralAddress;",
        "    float idlePlaySpeedScale;",
        "    uint32_t walkPlaySpeedScaleLiteralAddress;",
        "    float walkPlaySpeedScale;",
        "    uint32_t trotPlaySpeedScaleLiteralAddress;",
        "    float trotPlaySpeedScale;",
        "    uint32_t fastPlaySpeedScaleLiteralAddress;",
        "    float fastPlaySpeedScale;",
        "    uint32_t audioIdLiteralAddress;",
        "    uint32_t audioId;",
        "    uint32_t audioFreqPointerAddress;",
        "    uint32_t audioFreqRuntimeAddress;",
        "    uint32_t audioVolPointerAddress;",
        "    uint32_t audioVolRuntimeAddress;",
        "    uint32_t animationChangeSameStateCallsite;",
        "    uint32_t animationChangeChangedStateCallsite;",
        "    uint16_t idleAnimationIndex;",
        "    uint16_t idleCsabTypeIndex;",
        "    uint16_t idleCsabFileIndex;",
        "    const char* idleCsabName;",
        "    uint16_t walkAnimationIndex;",
        "    uint16_t walkCsabTypeIndex;",
        "    uint16_t walkCsabFileIndex;",
        "    const char* walkCsabName;",
        "    uint16_t trotAnimationIndex;",
        "    uint16_t trotCsabTypeIndex;",
        "    uint16_t trotCsabFileIndex;",
        "    const char* trotCsabName;",
        "    uint16_t fastAnimationIndex;",
        "    uint16_t fastCsabTypeIndex;",
        "    uint16_t fastCsabFileIndex;",
        "    const char* fastCsabName;",
        "    const char* oot3dBasis;",
        "    const char* n64Reference;",
        "    const char* decodeStatus;",
        "} Oot3dTitleIntroActorMotionAnimationRow;",
        "",
        "typedef struct Oot3dTitleIntroHorseStateRouteRow {",
        "    uint16_t stateRouteIndex;",
        "    const char* routeRole;",
        "    const char* actorRole;",
        "    const char* actorName;",
        "    const char* assetRole;",
        "    const char* archivePath;",
        "    uint32_t sourceFunction;",
        "    const char* sourceExport;",
        "    uint16_t horseTypeIndex;",
        "    uint16_t actionFieldOffset;",
        "    uint16_t speedFieldOffset;",
        "    uint16_t animationIndexFieldOffset;",
        "    uint16_t horseTypeFieldOffset;",
        "    uint16_t skelAnimeFieldOffset;",
        "    uint16_t followTimerFieldOffset;",
        "    uint16_t actionValue;",
        "    const char* animationIndices;",
        "    uint32_t tablePointerLiteralAddress;",
        "    uint32_t animationTableOuterRuntimeAddress;",
        "    uint32_t animationTableRuntimeAddress;",
        "    const char* primaryLiteralAddresses;",
        "    const char* primaryLiteralValues;",
        "    float forcedSpeed;",
        "    uint32_t updateArg;",
        "    uint32_t gallopControlBlockRuntimeAddress;",
        "    float gallopPlaySpeedScale;",
        "    float gallopPlaySpeedSwitch;",
        "    float gallopPlaySpeedMin;",
        "    float gallopPlaySpeedMax;",
        "    float gallopBrakeInputMagnitude;",
        "    float gallopDownshiftSpeed;",
        "    uint16_t gallopFastAnimationIndex;",
        "    uint16_t gallopFastCsabTypeIndex;",
        "    uint16_t gallopFastCsabFileIndex;",
        "    const char* gallopFastCsabName;",
        "    uint16_t gallopCarrotAnimationIndex;",
        "    uint16_t gallopCarrotCsabTypeIndex;",
        "    uint16_t gallopCarrotCsabFileIndex;",
        "    const char* gallopCarrotCsabName;",
        "    const char* resolvedCsabs;",
        "    const char* oot3dBasis;",
        "    const char* n64Reference;",
        "    const char* unresolvedFollowup;",
        "    const char* decodeStatus;",
        "} Oot3dTitleIntroHorseStateRouteRow;",
        "",
        "typedef struct Oot3dTitleIntroHorseCutsceneActionRouteRow {",
        "    uint16_t actionRouteIndex;",
        "    const char* routeRole;",
        "    const char* actorRole;",
        "    const char* archivePath;",
        "    uint16_t actionId;",
        "    uint16_t substate;",
        "    uint8_t resetPositionOnEntry;",
        "    uint32_t dispatchFunction;",
        "    uint32_t actionMapRuntimeAddress;",
        "    uint32_t initTableRuntimeAddress;",
        "    uint32_t updateTableRuntimeAddress;",
        "    uint32_t initFunction;",
        "    uint32_t updateFunction;",
        "    uint32_t animationTableOuterRuntimeAddress;",
        "    uint32_t animationTableRuntimeAddress;",
        "    uint16_t initialAnimationIndex;",
        "    uint16_t initialCsabTypeIndex;",
        "    uint16_t initialCsabFileIndex;",
        "    uint16_t initialCsabMaxFrame;",
        "    const char* initialCsabName;",
        "    uint32_t initialChangeModeInstructionAddress;",
        "    uint16_t initialChangeMode;",
        "    uint32_t initialMorphLiteralAddress;",
        "    float initialMorphFrames;",
        "    uint16_t alternateAnimationIndex;",
        "    uint16_t alternateCsabTypeIndex;",
        "    uint16_t alternateCsabFileIndex;",
        "    uint16_t alternateCsabMaxFrame;",
        "    const char* alternateCsabName;",
        "    uint16_t completionAnimationIndex;",
        "    uint16_t completionCsabTypeIndex;",
        "    uint16_t completionCsabFileIndex;",
        "    uint16_t completionCsabMaxFrame;",
        "    const char* completionCsabName;",
        "    uint16_t selectorWordIndex;",
        "    uint32_t selectorBits;",
        "    uint32_t playSpeedScaleLiteralAddress;",
        "    float playSpeedScale;",
        "    uint32_t fixedSpeedLiteralAddress;",
        "    float fixedSpeed;",
        "    uint32_t yawStepLiteralAddress;",
        "    uint16_t yawStep;",
        "    float targetEpsilon;",
        "    float completionPlaySpeed;",
        "    float completionMorphFrames;",
        "    uint32_t completionFirstModeInstructionAddress;",
        "    uint16_t completionFirstMode;",
        "    uint32_t completionLoopModeInstructionAddress;",
        "    uint16_t completionLoopMode;",
        "    const char* sourceEvidence;",
        "    const char* decodeStatus;",
        "} Oot3dTitleIntroHorseCutsceneActionRouteRow;",
        "",
        "typedef struct Oot3dTitleIntroSkelAnimeTimingRow {",
        "    uint16_t timingIndex;",
        "    const char* timingRole;",
        "    uint16_t changeMode;",
        "    uint16_t updateMode;",
        "    uint32_t setUpdateFunction;",
        "    uint32_t updateFunction;",
        "    uint32_t updateScaleLiteralAddress;",
        "    float updateScale;",
        "    uint32_t globalContextPointerAddress;",
        "    uint16_t globalUpdateRateOffset;",
        "    uint32_t updateRateInitializerFunction;",
        "    uint32_t updateRateInitializerInstructionAddress;",
        "    uint16_t globalUpdateRate;",
        "    const char* terminalBehavior;",
        "    const char* sourceEvidence;",
        "    const char* decodeStatus;",
        "} Oot3dTitleIntroSkelAnimeTimingRow;",
        "",
        "typedef struct Oot3dTitleIntroMountedPlayerAnimationFrameRow {",
        "    uint16_t frameRouteIndex;",
        "    const char* frameRole;",
        "    uint32_t frameFunction;",
        "    uint32_t frameLoadInstructionAddress;",
        "    uint32_t mountedPlayerActionFunction;",
        "    uint32_t frameStoreInstructionAddress;",
        "    uint16_t horseAnimationIndexOffset;",
        "    uint16_t horseAnimationFrameOffset;",
        "    uint16_t playerSkelAnimeOffset;",
        "    uint16_t playerCurrentFrameOffset;",
        "    uint32_t scaledHorseAnimationIndexMask;",
        "    uint32_t frameScaleLiteralAddress;",
        "    float frameScale;",
        "    uint32_t frameBiasLiteralAddress;",
        "    float frameBias;",
        "    uint8_t returnUnmatchedHorseAnimationFrame;",
        "    const char* quantization;",
        "    const char* unmatchedHorseAnimationBehavior;",
        "    const char* sourceEvidence;",
        "    const char* decodeStatus;",
        "} Oot3dTitleIntroMountedPlayerAnimationFrameRow;",
        "",
        "typedef struct Oot3dTitleIntroLinkChildStateRouteRow {",
        "    uint16_t stateRouteIndex;",
        "    const char* routeRole;",
        "    const char* actorRole;",
        "    const char* actorName;",
        "    const char* assetRole;",
        "    const char* archivePath;",
        "    uint32_t updateFunction;",
        "    uint32_t initFunction;",
        "    uint32_t drawFunction;",
        "    uint16_t actionFieldOffset;",
        "    uint16_t animationIndexFieldOffset;",
        "    uint16_t speedFieldOffset;",
        "    uint16_t skelAnimeFieldOffset;",
        "    uint32_t actionTablePointerLiteralAddress;",
        "    uint32_t actionTableRuntimeAddress;",
        "    uint16_t actionSlot;",
        "    uint32_t handlerFunction;",
        "    uint32_t animationTablePointerLiteralAddress;",
        "    uint32_t animationTableRuntimeAddress;",
        "    const char* expectedAnimationIndices;",
        "    const char* resolvedCsabs;",
        "    const char* expectedSpeedValues;",
        "    const char* n64ReferenceAction;",
        "    const char* oot3dBasis;",
        "    const char* n64Reference;",
        "    const char* unresolvedFollowup;",
        "    const char* decodeStatus;",
        "} Oot3dTitleIntroLinkChildStateRouteRow;",
        "",
        "typedef struct Oot3dTitleIntroLinkBoyPlayerActionRow {",
        "    uint16_t playerActionIndex;",
        "    uint32_t actorCueIndex;",
        "    const char* actorRole;",
        "    const char* actorName;",
        "    const char* assetRole;",
        "    const char* archivePath;",
        "    const char* cmbName;",
        "    const char* titleVisualCsabName;",
        "    uint16_t qdbIndex;",
        "    uint32_t qdbCommandIndex;",
        "    const char* qdbArchivePath;",
        "    const char* qdbEmbeddedName;",
        "    uint32_t cueCommandId;",
        "    uint16_t cueIndex;",
        "    int16_t cueId;",
        "    int16_t startFrame;",
        "    int16_t endFrame;",
        "    int16_t durationFrames;",
        "    int16_t rotX;",
        "    int16_t rotY;",
        "    int16_t rotZ;",
        "    int32_t startX;",
        "    int32_t startY;",
        "    int32_t startZ;",
        "    int32_t endX;",
        "    int32_t endY;",
        "    int32_t endZ;",
        "    int32_t deltaX;",
        "    int32_t deltaY;",
        "    int32_t deltaZ;",
        "    float normalX;",
        "    float normalY;",
        "    float normalZ;",
        "    const char* cueRole;",
        "    const char* oot3dBasis;",
        "    const char* n64Reference;",
        "    const char* unresolvedFollowup;",
        "    const char* decodeStatus;",
        "} Oot3dTitleIntroLinkBoyPlayerActionRow;",
        "",
        "typedef struct Oot3dTitleIntroLogoComponentRow {",
        "    uint16_t componentIndex;",
        "    uint16_t assetIndex;",
        "    uint16_t handleFieldOffset;",
        "    uint16_t alphaFieldOffset;",
        "    uint16_t cmbIndexJpeu;",
        "    uint16_t csabIndexJpeu;",
        "    uint16_t cmbIndexUs;",
        "    uint16_t csabIndexUs;",
        "    uint16_t cmabIndex;",
        "    uint16_t materialAnimationRuntimeOwnerFieldOffset;",
        "    uint16_t materialAnimationRuntimeLoopFieldOffset;",
        "    uint8_t materialAnimationRuntimeLoopOverrideValid;",
        "    uint8_t materialAnimationRuntimeLoopMode;",
        "    uint32_t materialAnimationRuntimeInitFunction;",
        "    uint32_t materialAnimationRuntimeStepFunction;",
        "    const char* componentRole;",
        "    const char* archivePath;",
        "    const char* cmbNameJpeu;",
        "    const char* csabNameJpeu;",
        "    const char* cmbNameUs;",
        "    const char* csabNameUs;",
        "    const char* cmabName;",
        "    const char* materialAnimationBindingBasis;",
        "    const char* bindingBasis;",
        "} Oot3dTitleIntroLogoComponentRow;",
        "",
        "typedef struct Oot3dTitleIntroLogoDrawRow {",
        "    uint16_t drawIndex;",
        "    uint16_t submitOrder;",
        "    uint16_t componentIndex;",
        "    uint16_t handleFieldOffset;",
        "    uint16_t alphaFieldOffset;",
        "    uint16_t effectAlphaFieldOffset;",
        "    uint32_t drawFunction;",
        "    uint32_t colorPointerAddress;",
        "    uint32_t colorRuntimeAddress;",
        "    uint32_t lightBlockPointerAddress;",
        "    uint32_t lightBlockRuntimeAddress;",
        "    uint32_t rendererFlagPointerAddress;",
        "    uint32_t rendererFlagRuntimeAddress;",
        "    uint32_t renderContextPointerAddress;",
        "    uint32_t renderContextRuntimeAddress;",
        "    uint32_t alternateRenderContextPointerAddress;",
        "    uint32_t alternateRenderContextRuntimeAddress;",
        "    uint32_t materialHandleFunction;",
        "    uint32_t materialSlotSelectFunction;",
        "    uint32_t materialColorApplyFunction;",
        "    uint32_t matrixCopyFunction;",
        "    uint32_t submitFunction;",
        "    uint32_t lightConfigResetFunction;",
        "    uint32_t lightConfigApplyFunction;",
        "    uint32_t lightVectorApplyFunction;",
        "    float alphaScale;",
        "    float baseTranslateZ;",
        "    float smallDepthOffset;",
        "    float copyrightTranslateY;",
        "    float effectVectorDoubleScale;",
        "    float effectVectorHalfScale;",
        "    float effectVectorBias;",
        "    float baseColorR;",
        "    float baseColorG;",
        "    float baseColorB;",
        "    float baseColorA;",
        "    float matrix00;",
        "    float matrix01;",
        "    float matrix02;",
        "    float matrix03;",
        "    float matrix10;",
        "    float matrix11;",
        "    float matrix12;",
        "    float matrix13;",
        "    float matrix20;",
        "    float matrix21;",
        "    float matrix22;",
        "    float matrix23;",
        "    float matrix30;",
        "    float matrix31;",
        "    float matrix32;",
        "    float matrix33;",
        "    const char* componentRole;",
        "    const char* matrixRole;",
        "    const char* matrixSourceAddresses;",
        "    const char* lightBlockRowMajor;",
        "    const char* visibilityCondition;",
        "    const char* drawBasis;",
        "    const char* decodeStatus;",
        "} Oot3dTitleIntroLogoDrawRow;",
        "",
        "typedef struct Oot3dTitleIntroLogoDrawContextRow {",
        "    uint16_t contextIndex;",
        "    const char* contextRole;",
        "    uint32_t drawFunction;",
        "    uint32_t submitFunction;",
        "    uint32_t rendererFlagPointerAddress;",
        "    uint32_t rendererFlagRuntimeAddress;",
        "    uint32_t renderContextPointerAddress;",
        "    uint32_t renderContextRuntimeAddress;",
        "    uint32_t alternateRenderContextPointerAddress;",
        "    uint32_t alternateRenderContextRuntimeAddress;",
        "    uint32_t globalContextRuntimeAddress;",
        "    uint32_t submitManagerRuntimeAddress;",
        "    uint16_t globalContextSubmitManagerOffset;",
        "    uint32_t submitManagerConstructorFunction;",
        "    uint32_t submitManagerVtableAddress;",
        "    uint32_t submitManagerStorageSize;",
        "    uint16_t smallQueueCountOffset;",
        "    uint16_t smallQueueStorageOffset;",
        "    uint16_t smallQueueCapacity;",
        "    uint16_t smallQueueRecordStride;",
        "    uint16_t smallQueueRecordStateByteOffset;",
        "    uint8_t smallQueueRecordStateByteValue;",
        "    uint32_t smallQueueRecordWriteFunction;",
        "    uint32_t smallQueueDrainFunction;",
        "    uint32_t pass0DrainFunction;",
        "    uint32_t pass1DrainFunction;",
        "    uint32_t lazyGuardFunction;",
        "    uint32_t lazyInitFunction;",
        "    uint16_t drawHandleVtableSubmitSlotOffset;",
        "    const char* oot3dBasis;",
        "    const char* n64Reference;",
        "    const char* decodeStatus;",
        "} Oot3dTitleIntroLogoDrawContextRow;",
        "",
        "typedef struct Oot3dTitleIntroLogoUpdateRow {",
        "    uint16_t updateIndex;",
        "    uint16_t state;",
        "    uint16_t substate;",
        "    uint16_t nextState;",
        "    uint16_t nextSubstate;",
        "    uint16_t timerFieldOffset;",
        "    int16_t timerInitialValue;",
        "    uint16_t flagId;",
        "    uint32_t literalAddress;",
        "    float literalValue;",
        "    float maxAlpha;",
        "    float minAlpha;",
        "    float mainAlphaStep;",
        "    float titleTextEffectAlphaStep;",
        "    float copyrightAlphaClamp;",
        "    int16_t copyrightAlphaStepDefault;",
        "    int16_t fadeOutAlphaStepDefault;",
        "    int16_t transitionCopyrightAlphaStep;",
        "    int16_t transitionFadeOutAlphaStep;",
        "    uint32_t initFunction;",
        "    uint32_t updateFunction;",
        "    uint32_t flagsGetEnvFunction;",
        "    uint32_t getCsabByIndexFunction;",
        "    uint32_t setCsabFunction;",
        "    uint32_t setCsabFrameFunction;",
        "    uint32_t setTitleAnimStateFunction;",
        "    uint32_t audioCutsceneFlagFunction;",
        "    uint32_t audioPlaySoundFunction;",
        "    const char* phaseRole;",
        "    const char* alphaFieldOffsets;",
        "    const char* alphaOperation;",
        "    const char* alphaDeltaSource;",
        "    const char* oot3dBasis;",
        "    const char* n64Reference;",
        "    const char* decodeStatus;",
        "} Oot3dTitleIntroLogoUpdateRow;",
        "",
        "extern const Oot3dTitleIntroAssetSourceRow gOot3dTitleIntroAssetSourceRows[];",
        "extern const uint32_t gOot3dTitleIntroAssetSourceRowCount;",
        "extern const Oot3dTitleIntroQdbSourceRow gOot3dTitleIntroQdbSourceRows[];",
        "extern const uint32_t gOot3dTitleIntroQdbSourceRowCount;",
        "extern const Oot3dTitleIntroQdbCommandRow gOot3dTitleIntroQdbCommandRows[];",
        "extern const uint32_t gOot3dTitleIntroQdbCommandRowCount;",
        "extern const Oot3dTitleIntroActorCueRow gOot3dTitleIntroActorCueRows[];",
        "extern const uint32_t gOot3dTitleIntroActorCueRowCount;",
        "extern const Oot3dTitleIntroActorInitSourceRow gOot3dTitleIntroActorInitSourceRows[];",
        "extern const uint32_t gOot3dTitleIntroActorInitSourceRowCount;",
        "extern const Oot3dTitleIntroActorScaleRow gOot3dTitleIntroActorScaleRows[];",
        "extern const uint32_t gOot3dTitleIntroActorScaleRowCount;",
        "extern const Oot3dTitleIntroActorAnimationRow gOot3dTitleIntroActorAnimationRows[];",
        "extern const uint32_t gOot3dTitleIntroActorAnimationRowCount;",
        "extern const Oot3dTitleIntroActorMotionAnimationRow gOot3dTitleIntroActorMotionAnimationRows[];",
        "extern const uint32_t gOot3dTitleIntroActorMotionAnimationRowCount;",
        "extern const Oot3dTitleIntroHorseStateRouteRow gOot3dTitleIntroHorseStateRouteRows[];",
        "extern const uint32_t gOot3dTitleIntroHorseStateRouteRowCount;",
        "extern const Oot3dTitleIntroHorseCutsceneActionRouteRow gOot3dTitleIntroHorseCutsceneActionRouteRows[];",
        "extern const uint32_t gOot3dTitleIntroHorseCutsceneActionRouteRowCount;",
        "extern const Oot3dTitleIntroSkelAnimeTimingRow gOot3dTitleIntroSkelAnimeTimingRows[];",
        "extern const uint32_t gOot3dTitleIntroSkelAnimeTimingRowCount;",
        "extern const Oot3dTitleIntroMountedPlayerAnimationFrameRow gOot3dTitleIntroMountedPlayerAnimationFrameRows[];",
        "extern const uint32_t gOot3dTitleIntroMountedPlayerAnimationFrameRowCount;",
        "extern const Oot3dTitleIntroLinkChildStateRouteRow gOot3dTitleIntroLinkChildStateRouteRows[];",
        "extern const uint32_t gOot3dTitleIntroLinkChildStateRouteRowCount;",
        "extern const Oot3dTitleIntroLinkBoyPlayerActionRow gOot3dTitleIntroLinkBoyPlayerActionRows[];",
        "extern const uint32_t gOot3dTitleIntroLinkBoyPlayerActionRowCount;",
        "extern const Oot3dTitleIntroLogoComponentRow gOot3dTitleIntroLogoComponentRows[];",
        "extern const uint32_t gOot3dTitleIntroLogoComponentRowCount;",
        "extern const Oot3dTitleIntroLogoDrawRow gOot3dTitleIntroLogoDrawRows[];",
        "extern const uint32_t gOot3dTitleIntroLogoDrawRowCount;",
        "extern const Oot3dTitleIntroLogoDrawContextRow gOot3dTitleIntroLogoDrawContextRows[];",
        "extern const uint32_t gOot3dTitleIntroLogoDrawContextRowCount;",
        "extern const Oot3dTitleIntroLogoUpdateRow gOot3dTitleIntroLogoUpdateRows[];",
        "extern const uint32_t gOot3dTitleIntroLogoUpdateRowCount;",
        "",
        "const Oot3dTitleIntroQdbSourceRow* Oot3d_TitleIntroSourceFindQdbRow(uint16_t qdbIndex);",
        "const Oot3dTitleIntroAssetSourceRow* Oot3d_TitleIntroSourceFindAssetRowByRole(const char* role);",
        "const Oot3dTitleIntroActorInitSourceRow* Oot3d_TitleIntroSourceFindActorInitRow(const char* actorName);",
        "const Oot3dTitleIntroActorScaleRow* Oot3d_TitleIntroSourceFindActorScaleRow(const char* actorRole);",
        "const Oot3dTitleIntroActorAnimationRow* Oot3d_TitleIntroSourceFindActorAnimationRow(const char* actorRole);",
        "const Oot3dTitleIntroActorMotionAnimationRow* Oot3d_TitleIntroSourceFindActorMotionAnimationRow(const char* actorRole);",
        "const Oot3dTitleIntroHorseStateRouteRow* Oot3d_TitleIntroSourceFindHorseStateRouteRow(const char* routeRole);",
        "const Oot3dTitleIntroHorseCutsceneActionRouteRow* Oot3d_TitleIntroSourceFindHorseCutsceneActionRouteRow(uint16_t actionId);",
        "const Oot3dTitleIntroSkelAnimeTimingRow* Oot3d_TitleIntroSourceFindSkelAnimeTimingRow(uint16_t changeMode);",
        "const Oot3dTitleIntroMountedPlayerAnimationFrameRow* Oot3d_TitleIntroSourceFindMountedPlayerAnimationFrameRow(uint16_t horseAnimationIndex);",
        "const char* Oot3d_TitleIntroSourceSelectLogoVariant(const char** outStatus);",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_source(path: Path, tables: dict[str, Any]) -> None:
    asset_rows = tables["assets"]
    qdb_rows = tables["scene_zar_qdb_rows"]
    command_rows = tables["scene_zar_qdb_command_rows"]
    actor_cue_rows = tables["epona_actor_cue_rows"]
    actor_init_rows = tables["title_logo_actor_init"]
    actor_scale_rows = tables["title_actor_scale_rows"]
    actor_animation_rows = tables["title_actor_animation_rows"]
    actor_motion_animation_rows = tables["title_actor_motion_animation_rows"]
    horse_state_route_rows = tables["title_horse_state_route_rows"]
    horse_cutscene_action_route_rows = tables["title_horse_cutscene_action_route_rows"]
    skel_anime_timing_rows = tables["title_skel_anime_timing_rows"]
    mounted_player_animation_frame_rows = tables[
        "title_mounted_player_animation_frame_rows"
    ]
    link_child_state_route_rows = tables["title_link_child_state_route_rows"]
    link_boy_player_action_rows = tables["title_link_boy_player_action_rows"]
    logo_component_rows = tables["title_logo_component_rows"]
    logo_draw_rows = tables["title_logo_draw_rows"]
    logo_draw_context_rows = tables["title_logo_draw_context_rows"]
    logo_update_rows = tables["title_logo_update_rows"]
    lines = [
        '#include "oot3d/title_intro_source_table.h"',
        "#include <string.h>",
        "",
        "const Oot3dTitleIntroAssetSourceRow gOot3dTitleIntroAssetSourceRows[] = {",
    ]
    for row in asset_rows:
        lines.append(
            "    { "
            f"{c_u16(row['asset_index'])}, {c_string(row['role'])}, {c_string(row['romfs_path'])}, "
            f"{c_u8(1 if row['exists'] else 0)}, {c_u32(row['file_size'])}, "
            f"{c_u16(row['zar_file_count'])}, {c_u16(row['cmb_count'])}, {c_u16(row['csab_count'])}, "
            f"{c_u16(row['cmab_count'])}, {c_u16(row['qdb_count'])}, {c_u16(row['ctxb_count'])}, "
            f"{c_u16(row['faceb_count'])}, {c_string(row['code_bin_string_addresses'])}, "
            f"{c_string(row['primary_summary'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroAssetSourceRowCount = sizeof(gOot3dTitleIntroAssetSourceRows) / sizeof(gOot3dTitleIntroAssetSourceRows[0]);",
            "",
            "const Oot3dTitleIntroQdbSourceRow gOot3dTitleIntroQdbSourceRows[] = {",
        ]
    )
    for row in qdb_rows:
        lines.append(
            "    { "
            f"{c_u16(row['qdb_index'])}, {c_u16(row['asset_index'])}, {c_u16(row['embedded_index'])}, "
            f"{c_u8(1 if row['is_epona_title_demo_candidate'] else 0)}, {c_u32(row['size'])}, "
            f"{c_u32(row['command_count'])}, {int_value(row['end_frame'])}, {c_u32(row['decoded_size'])}, "
            f"{c_u32(row['command_ref_start'])}, {c_u32(row['command_ref_count'])}, "
            f"{c_string(row['archive_path'])}, {c_string(row['embedded_name'])}, {c_string(row['command_ids'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroQdbSourceRowCount = sizeof(gOot3dTitleIntroQdbSourceRows) / sizeof(gOot3dTitleIntroQdbSourceRows[0]);",
            "",
            "const Oot3dTitleIntroQdbCommandRow gOot3dTitleIntroQdbCommandRows[] = {",
        ]
    )
    for row in command_rows:
        lines.append(
            "    { "
            f"{c_u32(row['qdb_command_index'])}, {c_u16(row['asset_index'])}, {c_u16(row['embedded_index'])}, "
            f"{c_u16(row['local_command_index'])}, {c_u32(row['command_offset'])}, "
            f"{c_u32(row['command_id'])}, {c_u32(row['camera_point_count'])}, {c_u32(row['total_size'])}, "
            f"{c_string(row['archive_path'])}, {c_string(row['embedded_name'])}, "
            f"{c_string(row['command_name'])}, {c_string(row['category'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroQdbCommandRowCount = sizeof(gOot3dTitleIntroQdbCommandRows) / sizeof(gOot3dTitleIntroQdbCommandRows[0]);",
            "",
            "const Oot3dTitleIntroActorCueRow gOot3dTitleIntroActorCueRows[] = {",
        ]
    )
    for row in actor_cue_rows:
        lines.append(
            "    { "
            f"{c_u32(row['actor_cue_index'])}, {c_u16(row['qdb_index'])}, "
            f"{c_u32(row['qdb_command_index'])}, {c_u32(row['cue_command_id'])}, "
            f"{c_u16(row['cue_index'])}, {int_value(row['cue_id'])}, "
            f"{int_value(row['start_frame'])}, {int_value(row['end_frame'])}, "
            f"{int_value(row['rot_x'])}, {int_value(row['rot_y'])}, {int_value(row['rot_z'])}, "
            f"{int_value(row['start_x'])}, {int_value(row['start_y'])}, {int_value(row['start_z'])}, "
            f"{int_value(row['end_x'])}, {int_value(row['end_y'])}, {int_value(row['end_z'])}, "
            f"{c_f32(row['normal_x'])}, {c_f32(row['normal_y'])}, {c_f32(row['normal_z'])}, "
            f"{c_string(row['archive_path'])}, {c_string(row['embedded_name'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroActorCueRowCount = sizeof(gOot3dTitleIntroActorCueRows) / sizeof(gOot3dTitleIntroActorCueRows[0]);",
            "",
            "const Oot3dTitleIntroActorInitSourceRow gOot3dTitleIntroActorInitSourceRows[] = {",
        ]
    )
    for row in actor_init_rows:
        lines.append(
            "    { "
            f"{c_u16(row['actor_init_index'])}, {c_u32(row['actor_init_file_offset'])}, "
            f"{c_u16(row['actor_id'])}, {c_u8(row['actor_category'])}, {c_u32(row['flags'])}, "
            f"{c_u16(row['object_id'])}, {c_u16(row['instance_size'])}, "
            f"{c_u32(row['init_function'])}, {c_u32(row['destroy_function'])}, "
            f"{c_u32(row['update_function'])}, {c_u32(row['draw_function'])}, "
            f"{c_string(row['actor_name'])}, {c_string(row['object_name'])}, "
            f"{c_string(row['decode_status'])}, {c_string(row['basis'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroActorInitSourceRowCount = sizeof(gOot3dTitleIntroActorInitSourceRows) / sizeof(gOot3dTitleIntroActorInitSourceRows[0]);",
            "",
            "const Oot3dTitleIntroActorScaleRow gOot3dTitleIntroActorScaleRows[] = {",
        ]
    )
    for row in actor_scale_rows:
        lines.append(
            "    { "
            f"{c_u16(row['scale_index'])}, {c_string(row['actor_role'])}, "
            f"{c_string(row['actor_name'])}, {c_string(row['archive_role'])}, "
            f"{c_u32(row['init_function'])}, {c_u32(row['update_function'])}, "
            f"{c_u32(row['draw_function'])}, {c_u32(row['actor_set_scale_callsite'])}, "
            f"{c_u32(row['actor_set_scale_function'])}, {c_u32(row['scale_literal_address'])}, "
            f"{c_f32(row['actor_scale'])}, {c_u32(row['gravity_literal_address'])}, "
            f"{c_f32(row['gravity'])}, {c_u32(row['shadow_y_offset_literal_address'])}, "
            f"{c_f32(row['shadow_y_offset'])}, {c_u32(row['shadow_scale_literal_address'])}, "
            f"{c_f32(row['shadow_scale'])}, {c_u32(row['focus_y_offset_literal_address'])}, "
            f"{c_f32(row['focus_y_offset'])}, {c_string(row['oot3d_basis'])}, "
            f"{c_string(row['n64_reference'])}, {c_string(row['decode_status'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroActorScaleRowCount = sizeof(gOot3dTitleIntroActorScaleRows) / sizeof(gOot3dTitleIntroActorScaleRows[0]);",
            "",
            "const Oot3dTitleIntroActorAnimationRow gOot3dTitleIntroActorAnimationRows[] = {",
        ]
    )
    for row in actor_animation_rows:
        lines.append(
            "    { "
            f"{c_u16(row['animation_index'])}, {c_string(row['actor_role'])}, "
            f"{c_string(row['actor_name'])}, {c_string(row['asset_role'])}, "
            f"{c_string(row['archive_path'])}, {c_u16(row['cmb_type_index'])}, "
            f"{c_u16(row['cmb_file_index'])}, {c_string(row['cmb_name'])}, "
            f"{c_u32(row['animation_table_pointer_literal_address'])}, "
            f"{c_u32(row['animation_table_outer_runtime_address'])}, "
            f"{c_u16(row['animation_table_outer_index'])}, "
            f"{c_u32(row['init_animation_table_runtime_address'])}, "
            f"{c_u16(row['init_animation_slot'])}, {c_u16(row['init_csab_type_index'])}, "
            f"{c_u16(row['init_csab_file_index'])}, {c_string(row['init_csab_name'])}, "
            f"{c_u16(row['title_visual_csab_type_index'])}, "
            f"{c_u16(row['title_visual_csab_file_index'])}, "
            f"{c_string(row['title_visual_csab_name'])}, "
            f"{c_u32(row['actor_init_function'])}, {c_u32(row['actor_update_function'])}, "
            f"{c_u32(row['actor_draw_function'])}, {c_u32(row['zar_get_cmb_by_index_callsite'])}, "
            f"{c_u32(row['animation_play_once_callsite'])}, "
            f"{c_u32(row['title_cue_semantic_reference_function'])}, "
            f"{c_string(row['title_cue_role'])}, {c_string(row['oot3d_basis'])}, "
            f"{c_string(row['n64_reference'])}, {c_string(row['decode_status'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroActorAnimationRowCount = sizeof(gOot3dTitleIntroActorAnimationRows) / sizeof(gOot3dTitleIntroActorAnimationRows[0]);",
            "",
            "const Oot3dTitleIntroActorMotionAnimationRow gOot3dTitleIntroActorMotionAnimationRows[] = {",
        ]
    )
    for row in actor_motion_animation_rows:
        lines.append(
            "    { "
            f"{c_u16(row['motion_index'])}, {c_string(row['actor_role'])}, "
            f"{c_string(row['actor_name'])}, {c_string(row['asset_role'])}, "
            f"{c_string(row['archive_path'])}, {c_u16(row['horse_type_index'])}, "
            f"{c_u16(row['action_field_offset'])}, {c_u16(row['speed_field_offset'])}, "
            f"{c_u16(row['animation_index_field_offset'])}, {c_u16(row['horse_type_field_offset'])}, "
            f"{c_u16(row['skel_anime_field_offset'])}, {c_u32(row['source_function'])}, "
            f"{c_u32(row['table_pointer_literal_address'])}, "
            f"{c_u32(row['animation_table_outer_runtime_address'])}, "
            f"{c_u32(row['animation_table_runtime_address'])}, "
            f"{c_u32(row['zero_speed_literal_address'])}, {c_f32(row['zero_speed'])}, "
            f"{c_u32(row['walk_threshold_literal_address'])}, {c_f32(row['walk_threshold'])}, "
            f"{c_u32(row['fast_threshold_literal_address'])}, {c_f32(row['fast_threshold'])}, "
            f"{c_u32(row['base_play_speed_scale_literal_address'])}, "
            f"{c_f32(row['base_play_speed_scale'])}, "
            f"{c_u32(row['idle_play_speed_scale_literal_address'])}, "
            f"{c_f32(row['idle_play_speed_scale'])}, "
            f"{c_u32(row['walk_play_speed_scale_literal_address'])}, "
            f"{c_f32(row['walk_play_speed_scale'])}, "
            f"{c_u32(row['trot_play_speed_scale_literal_address'])}, "
            f"{c_f32(row['trot_play_speed_scale'])}, "
            f"{c_u32(row['fast_play_speed_scale_literal_address'])}, "
            f"{c_f32(row['fast_play_speed_scale'])}, "
            f"{c_u32(row['audio_id_literal_address'])}, {c_u32(row['audio_id'])}, "
            f"{c_u32(row['audio_freq_pointer_address'])}, {c_u32(row['audio_freq_runtime_address'])}, "
            f"{c_u32(row['audio_vol_pointer_address'])}, {c_u32(row['audio_vol_runtime_address'])}, "
            f"{c_u32(row['animation_change_same_state_callsite'])}, "
            f"{c_u32(row['animation_change_changed_state_callsite'])}, "
            f"{c_u16(row['idle_animation_index'])}, {c_u16(row['idle_csab_type_index'])}, "
            f"{c_u16(row['idle_csab_file_index'])}, {c_string(row['idle_csab_name'])}, "
            f"{c_u16(row['walk_animation_index'])}, {c_u16(row['walk_csab_type_index'])}, "
            f"{c_u16(row['walk_csab_file_index'])}, {c_string(row['walk_csab_name'])}, "
            f"{c_u16(row['trot_animation_index'])}, {c_u16(row['trot_csab_type_index'])}, "
            f"{c_u16(row['trot_csab_file_index'])}, {c_string(row['trot_csab_name'])}, "
            f"{c_u16(row['fast_animation_index'])}, {c_u16(row['fast_csab_type_index'])}, "
            f"{c_u16(row['fast_csab_file_index'])}, {c_string(row['fast_csab_name'])}, "
            f"{c_string(row['oot3d_basis'])}, {c_string(row['n64_reference'])}, "
            f"{c_string(row['decode_status'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroActorMotionAnimationRowCount = sizeof(gOot3dTitleIntroActorMotionAnimationRows) / sizeof(gOot3dTitleIntroActorMotionAnimationRows[0]);",
            "",
            "const Oot3dTitleIntroHorseStateRouteRow gOot3dTitleIntroHorseStateRouteRows[] = {",
        ]
    )
    for row in horse_state_route_rows:
        lines.append(
            "    { "
            f"{c_u16(row['state_route_index'])}, {c_string(row['route_role'])}, "
            f"{c_string(row['actor_role'])}, {c_string(row['actor_name'])}, "
            f"{c_string(row['asset_role'])}, {c_string(row['archive_path'])}, "
            f"{c_u32(row['source_function'])}, {c_string(row['source_export'])}, "
            f"{c_u16(row['horse_type_index'])}, {c_u16(row['action_field_offset'])}, "
            f"{c_u16(row['speed_field_offset'])}, {c_u16(row['animation_index_field_offset'])}, "
            f"{c_u16(row['horse_type_field_offset'])}, {c_u16(row['skel_anime_field_offset'])}, "
            f"{c_u16(row['follow_timer_field_offset'])}, {c_u16(row['action_value'])}, "
            f"{c_string(row['animation_indices'])}, {c_u32(row['table_pointer_literal_address'])}, "
            f"{c_u32(row['animation_table_outer_runtime_address'])}, "
            f"{c_u32(row['animation_table_runtime_address'])}, "
            f"{c_string(row['primary_literal_addresses'])}, {c_string(row['primary_literal_values'])}, "
            f"{c_f32(row['forced_speed'])}, {c_u32(row['update_arg'])}, "
            f"{c_u32(row['gallop_control_block_runtime_address'])}, "
            f"{c_f32(row['gallop_play_speed_scale'])}, "
            f"{c_f32(row['gallop_play_speed_switch'])}, "
            f"{c_f32(row['gallop_play_speed_min'])}, "
            f"{c_f32(row['gallop_play_speed_max'])}, "
            f"{c_f32(row['gallop_brake_input_magnitude'])}, "
            f"{c_f32(row['gallop_downshift_speed'])}, "
            f"{c_u16(row['gallop_fast_animation_index'])}, "
            f"{c_u16(row['gallop_fast_csab_type_index'])}, "
            f"{c_u16(row['gallop_fast_csab_file_index'])}, "
            f"{c_string(row['gallop_fast_csab_name'])}, "
            f"{c_u16(row['gallop_carrot_animation_index'])}, "
            f"{c_u16(row['gallop_carrot_csab_type_index'])}, "
            f"{c_u16(row['gallop_carrot_csab_file_index'])}, "
            f"{c_string(row['gallop_carrot_csab_name'])}, "
            f"{c_string(row['resolved_csabs'])}, {c_string(row['oot3d_basis'])}, "
            f"{c_string(row['n64_reference'])}, {c_string(row['unresolved_followup'])}, "
            f"{c_string(row['decode_status'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroHorseStateRouteRowCount = sizeof(gOot3dTitleIntroHorseStateRouteRows) / sizeof(gOot3dTitleIntroHorseStateRouteRows[0]);",
            "",
            "const Oot3dTitleIntroHorseCutsceneActionRouteRow gOot3dTitleIntroHorseCutsceneActionRouteRows[] = {",
        ]
    )
    for row in horse_cutscene_action_route_rows:
        lines.append(
            "    { "
            f"{c_u16(row['action_route_index'])}, {c_string(row['route_role'])}, "
            f"{c_string(row['actor_role'])}, {c_string(row['archive_path'])}, "
            f"{c_u16(row['action_id'])}, {c_u16(row['substate'])}, "
            f"{c_u8(row['reset_position_on_entry'])}, {c_u32(row['dispatch_function'])}, "
            f"{c_u32(row['action_map_runtime_address'])}, "
            f"{c_u32(row['init_table_runtime_address'])}, "
            f"{c_u32(row['update_table_runtime_address'])}, "
            f"{c_u32(row['init_function'])}, {c_u32(row['update_function'])}, "
            f"{c_u32(row['animation_table_outer_runtime_address'])}, "
            f"{c_u32(row['animation_table_runtime_address'])}, "
            f"{c_u16(row['initial_animation_index'])}, "
            f"{c_u16(row['initial_csab_type_index'])}, "
            f"{c_u16(row['initial_csab_file_index'])}, "
            f"{c_u16(row['initial_csab_max_frame'])}, {c_string(row['initial_csab_name'])}, "
            f"{c_u32(row['initial_change_mode_instruction_address'])}, "
            f"{c_u16(row['initial_change_mode'])}, "
            f"{c_u32(row['initial_morph_literal_address'])}, {c_f32(row['initial_morph_frames'])}, "
            f"{c_u16(row['alternate_animation_index'])}, "
            f"{c_u16(row['alternate_csab_type_index'])}, "
            f"{c_u16(row['alternate_csab_file_index'])}, "
            f"{c_u16(row['alternate_csab_max_frame'])}, {c_string(row['alternate_csab_name'])}, "
            f"{c_u16(row['completion_animation_index'])}, "
            f"{c_u16(row['completion_csab_type_index'])}, "
            f"{c_u16(row['completion_csab_file_index'])}, "
            f"{c_u16(row['completion_csab_max_frame'])}, {c_string(row['completion_csab_name'])}, "
            f"{c_u16(row['selector_word_index'])}, {c_u32(row['selector_bits'])}, "
            f"{c_u32(row['play_speed_scale_literal_address'])}, {c_f32(row['play_speed_scale'])}, "
            f"{c_u32(row['fixed_speed_literal_address'])}, {c_f32(row['fixed_speed'])}, "
            f"{c_u32(row['yaw_step_literal_address'])}, {c_u16(row['yaw_step'])}, "
            f"{c_f32(row['target_epsilon'])}, {c_f32(row['completion_play_speed'])}, "
            f"{c_f32(row['completion_morph_frames'])}, "
            f"{c_u32(row['completion_first_mode_instruction_address'])}, "
            f"{c_u16(row['completion_first_mode'])}, "
            f"{c_u32(row['completion_loop_mode_instruction_address'])}, "
            f"{c_u16(row['completion_loop_mode'])}, "
            f"{c_string(row['source_evidence'])}, {c_string(row['decode_status'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroHorseCutsceneActionRouteRowCount = sizeof(gOot3dTitleIntroHorseCutsceneActionRouteRows) / sizeof(gOot3dTitleIntroHorseCutsceneActionRouteRows[0]);",
            "",
            "const Oot3dTitleIntroSkelAnimeTimingRow gOot3dTitleIntroSkelAnimeTimingRows[] = {",
        ]
    )
    for row in skel_anime_timing_rows:
        lines.append(
            "    { "
            f"{c_u16(row['timing_index'])}, {c_string(row['timing_role'])}, "
            f"{c_u16(row['change_mode'])}, {c_u16(row['update_mode'])}, "
            f"{c_u32(row['set_update_function'])}, {c_u32(row['update_function'])}, "
            f"{c_u32(row['update_scale_literal_address'])}, {c_f32(row['update_scale'])}, "
            f"{c_u32(row['global_context_pointer_address'])}, "
            f"{c_u16(row['global_update_rate_offset'])}, "
            f"{c_u32(row['update_rate_initializer_function'])}, "
            f"{c_u32(row['update_rate_initializer_instruction_address'])}, "
            f"{c_u16(row['global_update_rate'])}, {c_string(row['terminal_behavior'])}, "
            f"{c_string(row['source_evidence'])}, {c_string(row['decode_status'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroSkelAnimeTimingRowCount = sizeof(gOot3dTitleIntroSkelAnimeTimingRows) / sizeof(gOot3dTitleIntroSkelAnimeTimingRows[0]);",
            "",
            "const Oot3dTitleIntroMountedPlayerAnimationFrameRow gOot3dTitleIntroMountedPlayerAnimationFrameRows[] = {",
        ]
    )
    for row in mounted_player_animation_frame_rows:
        lines.append(
            "    { "
            f"{c_u16(row['frame_route_index'])}, {c_string(row['frame_role'])}, "
            f"{c_u32(row['frame_function'])}, {c_u32(row['frame_load_instruction_address'])}, "
            f"{c_u32(row['mounted_player_action_function'])}, "
            f"{c_u32(row['frame_store_instruction_address'])}, "
            f"{c_u16(row['horse_animation_index_offset'])}, "
            f"{c_u16(row['horse_animation_frame_offset'])}, "
            f"{c_u16(row['player_skel_anime_offset'])}, "
            f"{c_u16(row['player_current_frame_offset'])}, "
            f"{c_u32(row['scaled_horse_animation_index_mask'])}, "
            f"{c_u32(row['frame_scale_literal_address'])}, {c_f32(row['frame_scale'])}, "
            f"{c_u32(row['frame_bias_literal_address'])}, {c_f32(row['frame_bias'])}, "
            f"{c_u8(row['return_unmatched_horse_animation_frame'])}, "
            f"{c_string(row['quantization'])}, "
            f"{c_string(row['unmatched_horse_animation_behavior'])}, "
            f"{c_string(row['source_evidence'])}, "
            f"{c_string(row['decode_status'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroMountedPlayerAnimationFrameRowCount = sizeof(gOot3dTitleIntroMountedPlayerAnimationFrameRows) / sizeof(gOot3dTitleIntroMountedPlayerAnimationFrameRows[0]);",
            "",
            "const Oot3dTitleIntroLinkChildStateRouteRow gOot3dTitleIntroLinkChildStateRouteRows[] = {",
        ]
    )
    for row in link_child_state_route_rows:
        lines.append(
            "    { "
            f"{c_u16(row['state_route_index'])}, {c_string(row['route_role'])}, "
            f"{c_string(row['actor_role'])}, {c_string(row['actor_name'])}, "
            f"{c_string(row['asset_role'])}, {c_string(row['archive_path'])}, "
            f"{c_u32(row['update_function'])}, {c_u32(row['init_function'])}, "
            f"{c_u32(row['draw_function'])}, {c_u16(row['action_field_offset'])}, "
            f"{c_u16(row['animation_index_field_offset'])}, {c_u16(row['speed_field_offset'])}, "
            f"{c_u16(row['skel_anime_field_offset'])}, "
            f"{c_u32(row['action_table_pointer_literal_address'])}, "
            f"{c_u32(row['action_table_runtime_address'])}, {c_u16(row['action_slot'])}, "
            f"{c_u32(row['handler_function'])}, "
            f"{c_u32(row['animation_table_pointer_literal_address'])}, "
            f"{c_u32(row['animation_table_runtime_address'])}, "
            f"{c_string(row['expected_animation_indices'])}, {c_string(row['resolved_csabs'])}, "
            f"{c_string(row['expected_speed_values'])}, {c_string(row['n64_reference_action'])}, "
            f"{c_string(row['oot3d_basis'])}, {c_string(row['n64_reference'])}, "
            f"{c_string(row['unresolved_followup'])}, {c_string(row['decode_status'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroLinkChildStateRouteRowCount = sizeof(gOot3dTitleIntroLinkChildStateRouteRows) / sizeof(gOot3dTitleIntroLinkChildStateRouteRows[0]);",
            "",
            "const Oot3dTitleIntroLinkBoyPlayerActionRow gOot3dTitleIntroLinkBoyPlayerActionRows[] = {",
        ]
    )
    for row in link_boy_player_action_rows:
        lines.append(
            "    { "
            f"{c_u16(row['player_action_index'])}, {c_u32(row['actor_cue_index'])}, "
            f"{c_string(row['actor_role'])}, {c_string(row['actor_name'])}, "
            f"{c_string(row['asset_role'])}, {c_string(row['archive_path'])}, "
            f"{c_string(row['cmb_name'])}, {c_string(row['title_visual_csab_name'])}, "
            f"{c_u16(row['qdb_index'])}, {c_u32(row['qdb_command_index'])}, "
            f"{c_string(row['qdb_archive_path'])}, {c_string(row['qdb_embedded_name'])}, "
            f"{c_u32(row['cue_command_id'])}, {c_u16(row['cue_index'])}, {int_value(row['cue_id'])}, "
            f"{int_value(row['start_frame'])}, {int_value(row['end_frame'])}, "
            f"{int_value(row['duration_frames'])}, {int_value(row['rot_x'])}, "
            f"{int_value(row['rot_y'])}, {int_value(row['rot_z'])}, "
            f"{int_value(row['start_x'])}, {int_value(row['start_y'])}, {int_value(row['start_z'])}, "
            f"{int_value(row['end_x'])}, {int_value(row['end_y'])}, {int_value(row['end_z'])}, "
            f"{int_value(row['delta_x'])}, {int_value(row['delta_y'])}, {int_value(row['delta_z'])}, "
            f"{c_f32(row['normal_x'])}, {c_f32(row['normal_y'])}, {c_f32(row['normal_z'])}, "
            f"{c_string(row['cue_role'])}, {c_string(row['oot3d_basis'])}, "
            f"{c_string(row['n64_reference'])}, {c_string(row['unresolved_followup'])}, "
            f"{c_string(row['decode_status'])} }}," 
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroLinkBoyPlayerActionRowCount = sizeof(gOot3dTitleIntroLinkBoyPlayerActionRows) / sizeof(gOot3dTitleIntroLinkBoyPlayerActionRows[0]);",
            "",
            "const Oot3dTitleIntroLogoComponentRow gOot3dTitleIntroLogoComponentRows[] = {",
        ]
    )
    for row in logo_component_rows:
        lines.append(
            "    { "
            f"{c_u16(row['component_index'])}, {c_u16(row['asset_index'])}, "
            f"{c_u16(row['handle_field_offset'])}, {c_u16(row['alpha_field_offset'])}, "
            f"{c_u16(row['cmb_index_jpeu'])}, {c_u16(row['csab_index_jpeu'])}, "
            f"{c_u16(row['cmb_index_us'])}, {c_u16(row['csab_index_us'])}, "
            f"{c_u16(row['cmab_index'])}, "
            f"{c_u16(row['material_animation_runtime_owner_field_offset'])}, "
            f"{c_u16(row['material_animation_runtime_loop_field_offset'])}, "
            f"{c_u8(row['material_animation_runtime_loop_override_valid'])}, "
            f"{c_u8(row['material_animation_runtime_loop_mode'])}, "
            f"{c_u32(row['material_animation_runtime_init_function'])}, "
            f"{c_u32(row['material_animation_runtime_step_function'])}, "
            f"{c_string(row['component_role'])}, {c_string(row['archive_path'])}, "
            f"{c_string(row['cmb_name_jpeu'])}, {c_string(row['csab_name_jpeu'])}, "
            f"{c_string(row['cmb_name_us'])}, {c_string(row['csab_name_us'])}, "
            f"{c_string(row['cmab_name'])}, "
            f"{c_string(row['material_animation_binding_basis'])}, "
            f"{c_string(row['binding_basis'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroLogoComponentRowCount = sizeof(gOot3dTitleIntroLogoComponentRows) / sizeof(gOot3dTitleIntroLogoComponentRows[0]);",
            "",
            "const Oot3dTitleIntroLogoDrawRow gOot3dTitleIntroLogoDrawRows[] = {",
        ]
    )
    for row in logo_draw_rows:
        lines.append(
            "    { "
            f"{c_u16(row['draw_index'])}, {c_u16(row['submit_order'])}, "
            f"{c_u16(row['component_index'])}, {c_u16(row['handle_field_offset'])}, "
            f"{c_u16(row['alpha_field_offset'])}, {c_u16(row['effect_alpha_field_offset'])}, "
            f"{c_u32(row['draw_function'])}, {c_u32(row['color_pointer_address'])}, "
            f"{c_u32(row['color_runtime_address'])}, {c_u32(row['light_block_pointer_address'])}, "
            f"{c_u32(row['light_block_runtime_address'])}, {c_u32(row['renderer_flag_pointer_address'])}, "
            f"{c_u32(row['renderer_flag_runtime_address'])}, {c_u32(row['render_context_pointer_address'])}, "
            f"{c_u32(row['render_context_runtime_address'])}, {c_u32(row['alternate_render_context_pointer_address'])}, "
            f"{c_u32(row['alternate_render_context_runtime_address'])}, {c_u32(row['material_handle_function'])}, "
            f"{c_u32(row['material_slot_select_function'])}, {c_u32(row['material_color_apply_function'])}, "
            f"{c_u32(row['matrix_copy_function'])}, {c_u32(row['submit_function'])}, "
            f"{c_u32(row['light_config_reset_function'])}, {c_u32(row['light_config_apply_function'])}, "
            f"{c_u32(row['light_vector_apply_function'])}, {c_f32(row['alpha_scale'])}, "
            f"{c_f32(row['base_translate_z'])}, {c_f32(row['small_depth_offset'])}, "
            f"{c_f32(row['copyright_translate_y'])}, {c_f32(row['effect_vector_double_scale'])}, "
            f"{c_f32(row['effect_vector_half_scale'])}, {c_f32(row['effect_vector_bias'])}, "
            f"{c_f32(row['base_color_r'])}, {c_f32(row['base_color_g'])}, "
            f"{c_f32(row['base_color_b'])}, {c_f32(row['base_color_a'])}, "
            f"{c_f32(row['matrix00'])}, {c_f32(row['matrix01'])}, {c_f32(row['matrix02'])}, "
            f"{c_f32(row['matrix03'])}, {c_f32(row['matrix10'])}, {c_f32(row['matrix11'])}, "
            f"{c_f32(row['matrix12'])}, {c_f32(row['matrix13'])}, {c_f32(row['matrix20'])}, "
            f"{c_f32(row['matrix21'])}, {c_f32(row['matrix22'])}, {c_f32(row['matrix23'])}, "
            f"{c_f32(row['matrix30'])}, {c_f32(row['matrix31'])}, {c_f32(row['matrix32'])}, "
            f"{c_f32(row['matrix33'])}, {c_string(row['component_role'])}, "
            f"{c_string(row['matrix_role'])}, {c_string(row['matrix_source_addresses'])}, "
            f"{c_string(row['light_block_row_major'])}, {c_string(row['visibility_condition'])}, "
            f"{c_string(row['draw_basis'])}, {c_string(row['decode_status'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroLogoDrawRowCount = sizeof(gOot3dTitleIntroLogoDrawRows) / sizeof(gOot3dTitleIntroLogoDrawRows[0]);",
            "",
            "const Oot3dTitleIntroLogoDrawContextRow gOot3dTitleIntroLogoDrawContextRows[] = {",
        ]
    )
    for row in logo_draw_context_rows:
        lines.append(
            "    { "
            f"{c_u16(row['context_index'])}, {c_string(row['context_role'])}, "
            f"{c_u32(row['draw_function'])}, {c_u32(row['submit_function'])}, "
            f"{c_u32(row['renderer_flag_pointer_address'])}, {c_u32(row['renderer_flag_runtime_address'])}, "
            f"{c_u32(row['render_context_pointer_address'])}, {c_u32(row['render_context_runtime_address'])}, "
            f"{c_u32(row['alternate_render_context_pointer_address'])}, {c_u32(row['alternate_render_context_runtime_address'])}, "
            f"{c_u32(row['global_context_runtime_address'])}, {c_u32(row['submit_manager_runtime_address'])}, "
            f"{c_u16(row['global_context_submit_manager_offset'])}, "
            f"{c_u32(row['submit_manager_constructor_function'])}, {c_u32(row['submit_manager_vtable_address'])}, "
            f"{c_u32(row['submit_manager_storage_size'])}, {c_u16(row['small_queue_count_offset'])}, "
            f"{c_u16(row['small_queue_storage_offset'])}, {c_u16(row['small_queue_capacity'])}, "
            f"{c_u16(row['small_queue_record_stride'])}, {c_u16(row['small_queue_record_state_byte_offset'])}, "
            f"{c_u8(row['small_queue_record_state_byte_value'])}, {c_u32(row['small_queue_record_write_function'])}, "
            f"{c_u32(row['small_queue_drain_function'])}, {c_u32(row['pass0_drain_function'])}, "
            f"{c_u32(row['pass1_drain_function'])}, {c_u32(row['lazy_guard_function'])}, "
            f"{c_u32(row['lazy_init_function'])}, {c_u16(row['draw_handle_vtable_submit_slot_offset'])}, "
            f"{c_string(row['oot3d_basis'])}, {c_string(row['n64_reference'])}, "
            f"{c_string(row['decode_status'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroLogoDrawContextRowCount = sizeof(gOot3dTitleIntroLogoDrawContextRows) / sizeof(gOot3dTitleIntroLogoDrawContextRows[0]);",
            "",
            "const Oot3dTitleIntroLogoUpdateRow gOot3dTitleIntroLogoUpdateRows[] = {",
        ]
    )
    for row in logo_update_rows:
        lines.append(
            "    { "
            f"{c_u16(row['update_index'])}, {c_u16(row['state'])}, {c_u16(row['substate'])}, "
            f"{c_u16(row['next_state'])}, {c_u16(row['next_substate'])}, "
            f"{c_u16(row['timer_field_offset'])}, {int_value(row['timer_initial_value'])}, "
            f"{c_u16(row['flag_id'])}, {c_u32(row['literal_address'])}, {c_f32(row['literal_value'])}, "
            f"{c_f32(row['max_alpha'])}, {c_f32(row['min_alpha'])}, {c_f32(row['main_alpha_step'])}, "
            f"{c_f32(row['title_text_effect_alpha_step'])}, {c_f32(row['copyright_alpha_clamp'])}, "
            f"{int_value(row['copyright_alpha_step_default'])}, {int_value(row['fade_out_alpha_step_default'])}, "
            f"{int_value(row['transition_copyright_alpha_step'])}, {int_value(row['transition_fade_out_alpha_step'])}, "
            f"{c_u32(row['init_function'])}, {c_u32(row['update_function'])}, "
            f"{c_u32(row['flags_get_env_function'])}, {c_u32(row['get_csab_by_index_function'])}, "
            f"{c_u32(row['set_csab_function'])}, {c_u32(row['set_csab_frame_function'])}, "
            f"{c_u32(row['set_title_anim_state_function'])}, {c_u32(row['audio_cutscene_flag_function'])}, "
            f"{c_u32(row['audio_play_sound_function'])}, {c_string(row['phase_role'])}, "
            f"{c_string(row['alpha_field_offsets'])}, {c_string(row['alpha_operation'])}, "
            f"{c_string(row['alpha_delta_source'])}, {c_string(row['oot3d_basis'])}, "
            f"{c_string(row['n64_reference'])}, {c_string(row['decode_status'])} }},"
        )
    lines.extend(
        [
            "};",
            "const uint32_t gOot3dTitleIntroLogoUpdateRowCount = sizeof(gOot3dTitleIntroLogoUpdateRows) / sizeof(gOot3dTitleIntroLogoUpdateRows[0]);",
            "",
            "const Oot3dTitleIntroQdbSourceRow* Oot3d_TitleIntroSourceFindQdbRow(uint16_t qdbIndex) {",
            "    uint32_t i;",
            "    for (i = 0; i < gOot3dTitleIntroQdbSourceRowCount; ++i) {",
            "        if (gOot3dTitleIntroQdbSourceRows[i].qdbIndex == qdbIndex) return &gOot3dTitleIntroQdbSourceRows[i];",
            "    }",
            "    return 0;",
            "}",
            "",
            "const Oot3dTitleIntroAssetSourceRow* Oot3d_TitleIntroSourceFindAssetRowByRole(const char* role) {",
            "    uint32_t i;",
            "    if (role == 0) return 0;",
            "    for (i = 0; i < gOot3dTitleIntroAssetSourceRowCount; ++i) {",
            "        if (gOot3dTitleIntroAssetSourceRows[i].role != 0 && strcmp(gOot3dTitleIntroAssetSourceRows[i].role, role) == 0) return &gOot3dTitleIntroAssetSourceRows[i];",
            "    }",
            "    return 0;",
            "}",
            "",
            "const Oot3dTitleIntroActorInitSourceRow* Oot3d_TitleIntroSourceFindActorInitRow(const char* actorName) {",
            "    uint32_t i;",
            "    if (actorName == 0) return 0;",
            "    for (i = 0; i < gOot3dTitleIntroActorInitSourceRowCount; ++i) {",
            "        if (gOot3dTitleIntroActorInitSourceRows[i].actorName != 0 && strcmp(gOot3dTitleIntroActorInitSourceRows[i].actorName, actorName) == 0) return &gOot3dTitleIntroActorInitSourceRows[i];",
            "    }",
            "    return 0;",
            "}",
            "",
            "const Oot3dTitleIntroActorScaleRow* Oot3d_TitleIntroSourceFindActorScaleRow(const char* actorRole) {",
            "    uint32_t i;",
            "    if (actorRole == 0) return 0;",
            "    for (i = 0; i < gOot3dTitleIntroActorScaleRowCount; ++i) {",
            "        if (gOot3dTitleIntroActorScaleRows[i].actorRole != 0 && strcmp(gOot3dTitleIntroActorScaleRows[i].actorRole, actorRole) == 0) return &gOot3dTitleIntroActorScaleRows[i];",
            "    }",
            "    return 0;",
            "}",
            "",
            "const Oot3dTitleIntroActorAnimationRow* Oot3d_TitleIntroSourceFindActorAnimationRow(const char* actorRole) {",
            "    uint32_t i;",
            "    if (actorRole == 0) return 0;",
            "    for (i = 0; i < gOot3dTitleIntroActorAnimationRowCount; ++i) {",
            "        if (gOot3dTitleIntroActorAnimationRows[i].actorRole != 0 && strcmp(gOot3dTitleIntroActorAnimationRows[i].actorRole, actorRole) == 0) return &gOot3dTitleIntroActorAnimationRows[i];",
            "    }",
            "    return 0;",
            "}",
            "",
            "const Oot3dTitleIntroActorMotionAnimationRow* Oot3d_TitleIntroSourceFindActorMotionAnimationRow(const char* actorRole) {",
            "    uint32_t i;",
            "    if (actorRole == 0) return 0;",
            "    for (i = 0; i < gOot3dTitleIntroActorMotionAnimationRowCount; ++i) {",
            "        if (gOot3dTitleIntroActorMotionAnimationRows[i].actorRole != 0 && strcmp(gOot3dTitleIntroActorMotionAnimationRows[i].actorRole, actorRole) == 0) return &gOot3dTitleIntroActorMotionAnimationRows[i];",
            "    }",
            "    return 0;",
            "}",
            "",
            "const Oot3dTitleIntroHorseStateRouteRow* Oot3d_TitleIntroSourceFindHorseStateRouteRow(const char* routeRole) {",
            "    uint32_t i;",
            "    if (routeRole == 0) return 0;",
            "    for (i = 0; i < gOot3dTitleIntroHorseStateRouteRowCount; ++i) {",
            "        if (gOot3dTitleIntroHorseStateRouteRows[i].routeRole != 0 && strcmp(gOot3dTitleIntroHorseStateRouteRows[i].routeRole, routeRole) == 0) return &gOot3dTitleIntroHorseStateRouteRows[i];",
            "    }",
            "    return 0;",
            "}",
            "",
            "const Oot3dTitleIntroHorseCutsceneActionRouteRow* Oot3d_TitleIntroSourceFindHorseCutsceneActionRouteRow(uint16_t actionId) {",
            "    uint32_t i;",
            "    for (i = 0; i < gOot3dTitleIntroHorseCutsceneActionRouteRowCount; ++i) {",
            "        if (gOot3dTitleIntroHorseCutsceneActionRouteRows[i].actionId == actionId) return &gOot3dTitleIntroHorseCutsceneActionRouteRows[i];",
            "    }",
            "    return 0;",
            "}",
            "",
            "const Oot3dTitleIntroSkelAnimeTimingRow* Oot3d_TitleIntroSourceFindSkelAnimeTimingRow(uint16_t changeMode) {",
            "    uint32_t i;",
            "    for (i = 0; i < gOot3dTitleIntroSkelAnimeTimingRowCount; ++i) {",
            "        if (gOot3dTitleIntroSkelAnimeTimingRows[i].changeMode == changeMode) return &gOot3dTitleIntroSkelAnimeTimingRows[i];",
            "    }",
            "    return 0;",
            "}",
            "",
            "const Oot3dTitleIntroMountedPlayerAnimationFrameRow* Oot3d_TitleIntroSourceFindMountedPlayerAnimationFrameRow(uint16_t horseAnimationIndex) {",
            "    uint32_t i;",
            "    for (i = 0; i < gOot3dTitleIntroMountedPlayerAnimationFrameRowCount; ++i) {",
            "        const Oot3dTitleIntroMountedPlayerAnimationFrameRow* row = &gOot3dTitleIntroMountedPlayerAnimationFrameRows[i];",
            "        if ((horseAnimationIndex < 32u && (row->scaledHorseAnimationIndexMask & (1u << horseAnimationIndex)) != 0u) || row->returnUnmatchedHorseAnimationFrame != 0u) return row;",
            "    }",
            "    return 0;",
            "}",
            "",
            "const char* Oot3d_TitleIntroSourceSelectLogoVariant(const char** outStatus) {",
            "    const Oot3dTitleIntroAssetSourceRow* jpeu = Oot3d_TitleIntroSourceFindAssetRowByRole(\"ui_title_logo_it\");",
            "    const Oot3dTitleIntroAssetSourceRow* us = Oot3d_TitleIntroSourceFindAssetRowByRole(\"ui_title_logo_us\");",
            "    if (jpeu != 0 && jpeu->exists != 0) { if (outStatus != 0) *outStatus = \"selected_jpeu_from_current_oot3d_romfs_localized_title_logo_asset\"; return \"jpeu\"; }",
            "    if (us != 0 && us->exists != 0) { if (outStatus != 0) *outStatus = \"selected_us_from_current_oot3d_romfs_us_title_logo_asset\"; return \"us\"; }",
            "    if (outStatus != 0) *outStatus = \"selected_jpeu_by_default_pending_full_EnMag_version_selector_0xf3c_name\";",
            "    return \"jpeu\";",
            "}",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_outputs(tables: dict[str, Any]) -> None:
    write_json(DEFAULT_OUT_JSON, tables)
    write_markdown(DEFAULT_OUT_MD, tables)
    write_csv(
        DEFAULT_OUT_ASSET_CSV,
        tables["assets"],
        [
            "asset_index",
            "role",
            "romfs_path",
            "exists",
            "file_size",
            "code_bin_string_count",
            "code_bin_string_addresses",
            "zar_file_count",
            "cmb_count",
            "csab_count",
            "cmab_count",
            "qdb_count",
            "ctxb_count",
            "faceb_count",
            "type_counts",
            "primary_summary",
        ],
    )
    write_csv(
        DEFAULT_OUT_QDB_CSV,
        tables["scene_zar_qdb_rows"],
        [
            "qdb_index",
            "asset_index",
            "asset_role",
            "archive_path",
            "embedded_index",
            "embedded_name",
            "embedded_stem",
            "is_epona_title_demo_candidate",
            "size",
            "magic_hex",
            "version_or_flags_hex",
            "command_count",
            "end_frame",
            "decoded_size",
            "decode_status",
            "decode_error",
            "command_ref_start",
            "command_ref_count",
            "command_ids",
        ],
    )
    write_csv(
        DEFAULT_OUT_COMMAND_CSV,
        tables["scene_zar_qdb_command_rows"],
        [
            "qdb_command_index",
            "asset_index",
            "asset_role",
            "archive_path",
            "embedded_index",
            "embedded_name",
            "local_command_index",
            "command_offset_hex",
            "command_id_hex",
            "command_name",
            "category",
            "entry_count",
            "camera_point_count",
            "blob_size",
            "packed_stride",
            "payload_size",
            "total_size",
            "raw_prefix_hex",
        ],
    )
    write_csv(
        DEFAULT_OUT_ACTOR_CUE_CSV,
        tables["epona_actor_cue_rows"],
        [
            "actor_cue_index",
            "qdb_index",
            "qdb_command_index",
            "asset_index",
            "asset_role",
            "archive_path",
            "embedded_index",
            "embedded_name",
            "cue_command_id_hex",
            "cue_command_name",
            "cue_index",
            "entry_offset_hex",
            "cue_id",
            "start_frame",
            "end_frame",
            "rot_x",
            "rot_y",
            "rot_z",
            "start_x",
            "start_y",
            "start_z",
            "end_x",
            "end_y",
            "end_z",
            "normal_x",
            "normal_y",
            "normal_z",
        ],
    )
    write_csv(
        DEFAULT_OUT_TITLE_LOGO_ACTOR_INIT_CSV,
        tables["title_logo_actor_init"],
        [
            "actor_init_index",
            "actor_name",
            "object_name",
            "decode_status",
            "actor_init_file_offset_hex",
            "actor_id",
            "actor_category",
            "flags",
            "object_id",
            "instance_size",
            "init_function",
            "destroy_function",
            "update_function",
            "draw_function",
            "basis",
        ],
    )
    write_csv(
        DEFAULT_OUT_TITLE_ACTOR_SCALE_CSV,
        tables["title_actor_scale_rows"],
        [
            "scale_index",
            "actor_role",
            "actor_name",
            "archive_role",
            "decode_status",
            "init_function_hex",
            "update_function_hex",
            "draw_function_hex",
            "actor_set_scale_callsite_hex",
            "actor_set_scale_function_hex",
            "scale_literal_address_hex",
            "actor_scale",
            "gravity_literal_address_hex",
            "gravity",
            "shadow_y_offset_literal_address_hex",
            "shadow_y_offset",
            "shadow_scale_literal_address_hex",
            "shadow_scale",
            "focus_y_offset_literal_address_hex",
            "focus_y_offset",
            "oot3d_basis",
            "n64_reference",
        ],
    )
    write_csv(
        DEFAULT_OUT_TITLE_ACTOR_ANIMATION_CSV,
        tables["title_actor_animation_rows"],
        [
            "animation_index",
            "actor_role",
            "actor_name",
            "asset_role",
            "archive_path",
            "decode_status",
            "cmb_type_index",
            "cmb_file_index",
            "cmb_name",
            "animation_table_pointer_literal_address_hex",
            "animation_table_outer_runtime_address_hex",
            "animation_table_outer_index",
            "init_animation_table_runtime_address_hex",
            "init_animation_slot",
            "init_csab_type_index",
            "init_csab_file_index",
            "init_csab_name",
            "title_visual_csab_type_index",
            "title_visual_csab_file_index",
            "title_visual_csab_name",
            "actor_init_function_hex",
            "actor_update_function_hex",
            "actor_draw_function_hex",
            "zar_get_cmb_by_index_callsite_hex",
            "animation_play_once_callsite_hex",
            "title_cue_semantic_reference_function_hex",
            "title_cue_role",
            "oot3d_basis",
            "n64_reference",
        ],
    )
    write_csv(
        DEFAULT_OUT_TITLE_ACTOR_MOTION_ANIMATION_CSV,
        tables["title_actor_motion_animation_rows"],
        [
            "motion_index",
            "actor_role",
            "actor_name",
            "asset_role",
            "archive_path",
            "decode_status",
            "horse_type_index",
            "action_field_offset_hex",
            "speed_field_offset_hex",
            "animation_index_field_offset_hex",
            "horse_type_field_offset_hex",
            "skel_anime_field_offset_hex",
            "source_function_hex",
            "table_pointer_literal_address_hex",
            "animation_table_outer_runtime_address_hex",
            "animation_table_runtime_address_hex",
            "zero_speed_literal_address_hex",
            "zero_speed",
            "walk_threshold_literal_address_hex",
            "walk_threshold",
            "fast_threshold_literal_address_hex",
            "fast_threshold",
            "base_play_speed_scale_literal_address_hex",
            "base_play_speed_scale",
            "idle_play_speed_scale_literal_address_hex",
            "idle_play_speed_scale",
            "walk_play_speed_scale_literal_address_hex",
            "walk_play_speed_scale",
            "trot_play_speed_scale_literal_address_hex",
            "trot_play_speed_scale",
            "fast_play_speed_scale_literal_address_hex",
            "fast_play_speed_scale",
            "audio_id_literal_address_hex",
            "audio_id",
            "audio_freq_pointer_address_hex",
            "audio_freq_runtime_address_hex",
            "audio_vol_pointer_address_hex",
            "audio_vol_runtime_address_hex",
            "animation_change_same_state_callsite_hex",
            "animation_change_changed_state_callsite_hex",
            "idle_animation_index",
            "idle_csab_type_index",
            "idle_csab_file_index",
            "idle_csab_name",
            "walk_animation_index",
            "walk_csab_type_index",
            "walk_csab_file_index",
            "walk_csab_name",
            "trot_animation_index",
            "trot_csab_type_index",
            "trot_csab_file_index",
            "trot_csab_name",
            "fast_animation_index",
            "fast_csab_type_index",
            "fast_csab_file_index",
            "fast_csab_name",
            "oot3d_basis",
            "n64_reference",
        ],
    )
    write_csv(
        DEFAULT_OUT_TITLE_HORSE_STATE_ROUTE_CSV,
        tables["title_horse_state_route_rows"],
        [
            "state_route_index",
            "route_role",
            "actor_role",
            "actor_name",
            "asset_role",
            "archive_path",
            "decode_status",
            "source_function_hex",
            "source_export",
            "horse_type_index",
            "action_field_offset_hex",
            "speed_field_offset_hex",
            "animation_index_field_offset_hex",
            "horse_type_field_offset_hex",
            "skel_anime_field_offset_hex",
            "follow_timer_field_offset_hex",
            "action_value",
            "animation_indices",
            "table_pointer_literal_address_hex",
            "animation_table_outer_runtime_address_hex",
            "animation_table_runtime_address_hex",
            "primary_literal_addresses",
            "primary_literal_values",
            "forced_speed",
            "update_arg",
            "gallop_control_block_runtime_address_hex",
            "gallop_play_speed_scale",
            "gallop_play_speed_switch",
            "gallop_play_speed_min",
            "gallop_play_speed_max",
            "gallop_brake_input_magnitude",
            "gallop_downshift_speed",
            "gallop_fast_animation_index",
            "gallop_fast_csab_type_index",
            "gallop_fast_csab_file_index",
            "gallop_fast_csab_name",
            "gallop_carrot_animation_index",
            "gallop_carrot_csab_type_index",
            "gallop_carrot_csab_file_index",
            "gallop_carrot_csab_name",
            "resolved_csabs",
            "oot3d_basis",
            "n64_reference",
            "unresolved_followup",
        ],
    )
    write_csv(
        DEFAULT_OUT_TITLE_HORSE_CUTSCENE_ACTION_ROUTE_CSV,
        tables["title_horse_cutscene_action_route_rows"],
        [
            "action_route_index",
            "route_role",
            "actor_role",
            "archive_path",
            "action_id_hex",
            "substate",
            "reset_position_on_entry",
            "dispatch_function_hex",
            "action_map_runtime_address_hex",
            "init_table_runtime_address_hex",
            "update_table_runtime_address_hex",
            "init_function_hex",
            "update_function_hex",
            "animation_table_outer_runtime_address_hex",
            "animation_table_runtime_address_hex",
            "initial_animation_index",
            "initial_csab_type_index",
            "initial_csab_file_index",
            "initial_csab_max_frame",
            "initial_csab_name",
            "initial_change_mode_instruction_address_hex",
            "initial_change_mode",
            "initial_morph_literal_address_hex",
            "initial_morph_frames",
            "alternate_animation_index",
            "alternate_csab_type_index",
            "alternate_csab_file_index",
            "alternate_csab_max_frame",
            "alternate_csab_name",
            "completion_animation_index",
            "completion_csab_type_index",
            "completion_csab_file_index",
            "completion_csab_max_frame",
            "completion_csab_name",
            "selector_word_index",
            "selector_bits_hex",
            "play_speed_scale_literal_address_hex",
            "play_speed_scale",
            "fixed_speed_literal_address_hex",
            "fixed_speed",
            "yaw_step_literal_address_hex",
            "yaw_step",
            "target_epsilon",
            "completion_play_speed",
            "completion_morph_frames",
            "completion_first_mode_instruction_address_hex",
            "completion_first_mode",
            "completion_loop_mode_instruction_address_hex",
            "completion_loop_mode",
            "source_evidence",
            "decode_status",
        ],
    )
    write_csv(
        DEFAULT_OUT_TITLE_SKEL_ANIME_TIMING_CSV,
        tables["title_skel_anime_timing_rows"],
        [
            "timing_index",
            "timing_role",
            "change_mode",
            "update_mode",
            "set_update_function_hex",
            "update_function_hex",
            "update_scale_literal_address_hex",
            "update_scale",
            "global_context_pointer_address_hex",
            "global_update_rate_offset",
            "update_rate_initializer_function_hex",
            "update_rate_initializer_instruction_address_hex",
            "global_update_rate",
            "terminal_behavior",
            "source_evidence",
            "decode_status",
        ],
    )
    write_csv(
        DEFAULT_OUT_TITLE_LINK_CHILD_STATE_ROUTE_CSV,
        tables["title_link_child_state_route_rows"],
        [
            "state_route_index",
            "route_role",
            "actor_role",
            "actor_name",
            "asset_role",
            "archive_path",
            "decode_status",
            "update_function_hex",
            "init_function_hex",
            "draw_function_hex",
            "action_field_offset_hex",
            "animation_index_field_offset_hex",
            "speed_field_offset_hex",
            "skel_anime_field_offset_hex",
            "action_table_pointer_literal_address_hex",
            "action_table_runtime_address_hex",
            "action_slot",
            "handler_function_hex",
            "animation_table_pointer_literal_address_hex",
            "animation_table_runtime_address_hex",
            "expected_animation_indices",
            "resolved_csabs",
            "expected_speed_values",
            "n64_reference_action",
            "oot3d_basis",
            "n64_reference",
            "unresolved_followup",
        ],
    )
    write_csv(
        DEFAULT_OUT_TITLE_MOUNTED_PLAYER_FRAME_CSV,
        tables["title_mounted_player_animation_frame_rows"],
        [
            "frame_route_index",
            "frame_role",
            "frame_function_hex",
            "frame_load_instruction_address_hex",
            "mounted_player_action_function_hex",
            "frame_store_instruction_address_hex",
            "horse_animation_index_offset",
            "horse_animation_frame_offset",
            "player_skel_anime_offset",
            "player_current_frame_offset",
            "scaled_horse_animation_indices",
            "scaled_horse_animation_index_mask_hex",
            "frame_scale_literal_address_hex",
            "frame_scale",
            "frame_bias_literal_address_hex",
            "frame_bias",
            "return_unmatched_horse_animation_frame",
            "quantization",
            "unmatched_horse_animation_behavior",
            "source_evidence",
            "decode_status",
        ],
    )
    write_csv(
        DEFAULT_OUT_TITLE_LINK_BOY_PLAYER_ACTION_CSV,
        tables["title_link_boy_player_action_rows"],
        [
            "player_action_index",
            "actor_cue_index",
            "actor_role",
            "actor_name",
            "asset_role",
            "archive_path",
            "cmb_name",
            "title_visual_csab_name",
            "qdb_index",
            "qdb_command_index",
            "qdb_archive_path",
            "qdb_embedded_name",
            "cue_command_id_hex",
            "cue_command_name",
            "cue_index",
            "cue_id",
            "cue_role",
            "start_frame",
            "end_frame",
            "duration_frames",
            "rot_x",
            "rot_y",
            "rot_z",
            "start_x",
            "start_y",
            "start_z",
            "end_x",
            "end_y",
            "end_z",
            "delta_x",
            "delta_y",
            "delta_z",
            "normal_x",
            "normal_y",
            "normal_z",
            "oot3d_basis",
            "n64_reference",
            "unresolved_followup",
            "decode_status",
        ],
    )
    write_csv(
        DEFAULT_OUT_TITLE_LOGO_COMPONENT_CSV,
        tables["title_logo_component_rows"],
        [
            "component_index",
            "component_role",
            "asset_index",
            "archive_path",
            "handle_field_offset_hex",
            "alpha_field_offset_hex",
            "cmb_index_jpeu",
            "csab_index_jpeu",
            "cmb_name_jpeu",
            "csab_name_jpeu",
            "cmb_index_us",
            "csab_index_us",
            "cmb_name_us",
            "csab_name_us",
            "cmab_index",
            "cmab_name",
            "material_animation_runtime_owner_field_offset_hex",
            "material_animation_runtime_loop_field_offset_hex",
            "material_animation_runtime_loop_override_valid",
            "material_animation_runtime_loop_mode",
            "material_animation_runtime_init_function_hex",
            "material_animation_runtime_step_function_hex",
            "material_animation_binding_basis",
            "binding_basis",
        ],
    )
    write_csv(
        DEFAULT_OUT_TITLE_LOGO_DRAW_CSV,
        tables["title_logo_draw_rows"],
        [
            "draw_index",
            "submit_order",
            "component_index",
            "component_role",
            "handle_field_offset_hex",
            "alpha_field_offset_hex",
            "effect_alpha_field_offset_hex",
            "draw_function_hex",
            "color_pointer_address_hex",
            "color_runtime_address_hex",
            "light_block_pointer_address_hex",
            "light_block_runtime_address_hex",
            "renderer_flag_pointer_address_hex",
            "renderer_flag_runtime_address_hex",
            "render_context_pointer_address_hex",
            "render_context_runtime_address_hex",
            "alternate_render_context_pointer_address_hex",
            "alternate_render_context_runtime_address_hex",
            "material_handle_function_hex",
            "material_slot_select_function_hex",
            "material_color_apply_function_hex",
            "matrix_copy_function_hex",
            "submit_function_hex",
            "light_config_reset_function_hex",
            "light_config_apply_function_hex",
            "light_vector_apply_function_hex",
            "alpha_scale",
            "base_translate_z",
            "small_depth_offset",
            "copyright_translate_y",
            "effect_vector_double_scale",
            "effect_vector_half_scale",
            "effect_vector_bias",
            "base_color_r",
            "base_color_g",
            "base_color_b",
            "base_color_a",
            "matrix_row_major",
            "light_block_row_major",
            "matrix_role",
            "matrix_source_addresses",
            "visibility_condition",
            "draw_basis",
            "decode_status",
        ],
    )
    write_csv(
        DEFAULT_OUT_TITLE_LOGO_DRAW_CONTEXT_CSV,
        tables["title_logo_draw_context_rows"],
        [
            "context_index",
            "context_role",
            "decode_status",
            "draw_function_hex",
            "submit_function_hex",
            "renderer_flag_pointer_address_hex",
            "renderer_flag_runtime_address_hex",
            "render_context_pointer_address_hex",
            "render_context_runtime_address_hex",
            "alternate_render_context_pointer_address_hex",
            "alternate_render_context_runtime_address_hex",
            "global_context_runtime_address_hex",
            "submit_manager_runtime_address_hex",
            "global_context_submit_manager_offset_hex",
            "submit_manager_constructor_function_hex",
            "submit_manager_vtable_address_hex",
            "submit_manager_storage_size_hex",
            "small_queue_count_offset_hex",
            "small_queue_storage_offset_hex",
            "small_queue_capacity",
            "small_queue_record_stride",
            "small_queue_record_state_byte_offset",
            "small_queue_record_state_byte_value",
            "small_queue_record_write_function_hex",
            "small_queue_drain_function_hex",
            "pass0_drain_function_hex",
            "pass1_drain_function_hex",
            "lazy_guard_function_hex",
            "lazy_init_function_hex",
            "draw_handle_vtable_submit_slot_offset_hex",
            "oot3d_basis",
            "n64_reference",
        ],
    )
    write_csv(
        DEFAULT_OUT_TITLE_LOGO_UPDATE_CSV,
        tables["title_logo_update_rows"],
        [
            "update_index",
            "phase_role",
            "state",
            "substate",
            "next_state",
            "next_substate",
            "timer_field_offset_hex",
            "timer_initial_value",
            "timer_initial_value_hex",
            "flag_id",
            "flag_id_hex",
            "alpha_field_offsets",
            "alpha_operation",
            "alpha_delta_source",
            "literal_address_hex",
            "literal_value",
            "max_alpha",
            "min_alpha",
            "main_alpha_step",
            "title_text_effect_alpha_step",
            "copyright_alpha_clamp",
            "copyright_alpha_step_default",
            "fade_out_alpha_step_default",
            "transition_copyright_alpha_step",
            "transition_fade_out_alpha_step",
            "init_function_hex",
            "update_function_hex",
            "flags_get_env_function_hex",
            "get_csab_by_index_function_hex",
            "set_csab_function_hex",
            "set_csab_frame_function_hex",
            "set_title_anim_state_function_hex",
            "audio_cutscene_flag_function_hex",
            "audio_play_sound_function_hex",
            "oot3d_basis",
            "n64_reference",
            "decode_status",
        ],
    )
    write_csv(
        DEFAULT_OUT_SYMBOL_CSV,
        tables["runtime_symbol_candidates"],
        ["address_hex", "symbol", "kind", "confidence", "source"],
    )
    write_header(DEFAULT_OUT_HEADER)
    write_source(DEFAULT_OUT_SOURCE, tables)


def main() -> None:
    tables = build_tables()
    write_outputs(tables)
    summary = tables["summary"]
    print(
        "wrote title intro source table: "
        f"{summary['existing_asset_count']}/{summary['asset_count']} assets present, "
        f"{summary['spot00_zar_qdb_decoded_count']} spot00 QDB and {summary['target_qdb_decoded_count']} target QDB decoded, "
        f"{summary['epona_qdb_candidate_count']} epona candidates, "
        f"{summary['epona_actor_cue_count']} epona actor cues"
    )


if __name__ == "__main__":
    main()
