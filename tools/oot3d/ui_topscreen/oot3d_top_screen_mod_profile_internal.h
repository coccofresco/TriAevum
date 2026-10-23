#pragma once

// Private to the oot3d_top_screen_mod_profile*.cpp units: their includes and the verified
// layout tables and helpers they share (file-local to each unit).
#include "oot3d_top_screen_mod_profile.h"

#include "oot3d_native_a32_memory.h"

#include "oot3d_ui/ui_backend.h"
#include "oot3d_ui/ui_value_domains.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <sstream>
#include <utility>

namespace Oot3dNativeGame {

namespace {

constexpr float kNativeTopScreenWidth = 400.0F;
constexpr float kNativeTopScreenHeight = 240.0F;
constexpr float kNativeTouchScreenWidth = 320.0F;
constexpr float kPromotedPauseHorizontalScale =
    kNativeTopScreenWidth / kNativeTouchScreenWidth;

// Values recovered from the EUR TopScreenMod IPS and tied to native pause-page
// state or named consumers. Instruction edits remain source-level contracts.
constexpr std::array kVerifiedLayoutPatches{
    TopScreenLayoutPatch{0x00506B30U,
                         {0x00000000U, 0x00000000U},
                         {0xBF800000U, 0x00000000U},
                         2U,
                         "pause_items_page_origin"},
    TopScreenLayoutPatch{0x00506B38U,
                         {0x42F80000U, 0x00000000U},
                         {0x42F60000U, 0x00000000U},
                         2U,
                         "items_page_horizontal_extent"},
    TopScreenLayoutPatch{0x004673BCU,
                         {0x00002601U, 0U},
                         {0x00002600U, 0U},
                         1U,
                         "gear_main_texture_filter"},
    TopScreenLayoutPatch{0x00467688U,
                         {0x00002601U, 0U},
                         {0x00002600U, 0U},
                         1U,
                         "gear_auxiliary_texture_filter"},
    TopScreenLayoutPatch{0x00504904U,
                         {0x43200000U, 0x42400000U},
                         {0x43208000U, 0x42420000U},
                         2U,
                         "gear_page_panel_extent"},
    TopScreenLayoutPatch{0x005047A4U,
                         {0x42380000U, 0x42300000U},
                         {0x42340000U, 0x422C0000U},
                         2U,
                         "gear_page_icon_extent"},
    TopScreenLayoutPatch{0x005049DCU,
                         {0x00000000U, 0x43560000U},
                         {0x3F000000U, 0x43568000U},
                         2U,
                         "gear_page_cursor_bounds"},
    TopScreenLayoutPatch{0x0050487CU,
                         {0x42280000U, 0x42280000U},
                         {0x42240000U, 0x42240000U},
                         2U,
                         "gear_page_cursor_extent"},
    // 2.1.1 dungeon panel, columns and native floor/icon producer inputs.
    TopScreenLayoutPatch{0x00506D60U,
                         {0x42600000U, 0U},
                         {0x41C00000U, 0U},
                         1U,
                         "dungeon_map_panel_x"},
    TopScreenLayoutPatch{0x00506EF0U,
                         {0x43500000U, 0U},
                         {0x43880000U, 0U},
                         1U,
                         "dungeon_map_panel_width"},
    TopScreenLayoutPatch{0x00506DE0U,
                         {0x42800000U, 0U},
                         {0x41C00000U, 0U},
                         1U,
                         "dungeon_map_title_x"},
    TopScreenLayoutPatch{0x00506F70U,
                         {0x43400000U, 0U},
                         {0x43880000U, 0U},
                         1U,
                         "dungeon_map_title_width"},
    TopScreenLayoutPatch{0x00506D68U,
                         {0x00000000U, 0U},
                         {0xC2200000U, 0U},
                         1U,
                         "dungeon_map_left_column_x"},
    TopScreenLayoutPatch{0x00506D70U,
                         {0x43800000U, 0U},
                         {0x43940000U, 0U},
                         1U,
                         "dungeon_map_right_column_x"},
    TopScreenLayoutPatch{0x00506DC8U,
                         {0x41500000U, 0U},
                         {0xC1D80000U, 0U},
                         1U,
                         "dungeon_map_icon_background_0_x"},
    TopScreenLayoutPatch{0x00506DD0U,
                         {0x41500000U, 0U},
                         {0xC1D80000U, 0U},
                         1U,
                         "dungeon_map_icon_background_1_x"},
    TopScreenLayoutPatch{0x00506DD8U,
                         {0x41500000U, 0U},
                         {0xC1D80000U, 0U},
                         1U,
                         "dungeon_map_icon_background_2_x"},
    TopScreenLayoutPatch{0x00506DE8U,
                         {0x00000000U, 0U},
                         {0xC2200000U, 0U},
                         1U,
                         "dungeon_map_count_background_x"},
    TopScreenLayoutPatch{0x00506DF0U,
                         {0x42400000U, 0U},
                         {0x41000000U, 0U},
                         1U,
                         "dungeon_map_count_edge_x"},
    TopScreenLayoutPatch{0x00506DF8U,
                         {0x40000000U, 0U},
                         {0xC2180000U, 0U},
                         1U,
                         "dungeon_map_count_inset_x"},
    TopScreenLayoutPatch{0x004D4E94U,
                         {0x43830000U, 0U},
                         {0x43970000U, 0U},
                         1U,
                         "dungeon_map_floor_outer_0_x"},
    TopScreenLayoutPatch{0x004D4E9CU,
                         {0x43830000U, 0U},
                         {0x43970000U, 0U},
                         1U,
                         "dungeon_map_floor_outer_1_x"},
    TopScreenLayoutPatch{0x004D4EA4U,
                         {0x43830000U, 0U},
                         {0x43970000U, 0U},
                         1U,
                         "dungeon_map_floor_outer_2_x"},
    TopScreenLayoutPatch{0x004D4EACU,
                         {0x43830000U, 0U},
                         {0x43970000U, 0U},
                         1U,
                         "dungeon_map_floor_outer_3_x"},
    TopScreenLayoutPatch{0x004D4EB4U,
                         {0x43830000U, 0U},
                         {0x43970000U, 0U},
                         1U,
                         "dungeon_map_floor_outer_4_x"},
    TopScreenLayoutPatch{0x004D4EBCU,
                         {0x43840000U, 0U},
                         {0x43980000U, 0U},
                         1U,
                         "dungeon_map_floor_inner_0_x"},
    TopScreenLayoutPatch{0x004D4EC4U,
                         {0x43840000U, 0U},
                         {0x43980000U, 0U},
                         1U,
                         "dungeon_map_floor_inner_1_x"},
    TopScreenLayoutPatch{0x004D4ECCU,
                         {0x43840000U, 0U},
                         {0x43980000U, 0U},
                         1U,
                         "dungeon_map_floor_inner_2_x"},
    TopScreenLayoutPatch{0x004D4ED4U,
                         {0x43840000U, 0U},
                         {0x43980000U, 0U},
                         1U,
                         "dungeon_map_floor_inner_3_x"},
    TopScreenLayoutPatch{0x004D4EDCU,
                         {0x43840000U, 0U},
                         {0x43980000U, 0U},
                         1U,
                         "dungeon_map_floor_inner_4_x"},
    TopScreenLayoutPatch{0x004D4EE4U,
                         {0x43890000U, 0U},
                         {0x439D0000U, 0U},
                         1U,
                         "dungeon_map_floor_label_0_x"},
    TopScreenLayoutPatch{0x004D4EECU,
                         {0x43890000U, 0U},
                         {0x439D0000U, 0U},
                         1U,
                         "dungeon_map_floor_label_1_x"},
    TopScreenLayoutPatch{0x004D4EF4U,
                         {0x43890000U, 0U},
                         {0x439D0000U, 0U},
                         1U,
                         "dungeon_map_floor_label_2_x"},
    TopScreenLayoutPatch{0x004D4EFCU,
                         {0x43890000U, 0U},
                         {0x439D0000U, 0U},
                         1U,
                         "dungeon_map_floor_label_3_x"},
    TopScreenLayoutPatch{0x004D4F04U,
                         {0x43890000U, 0U},
                         {0x439D0000U, 0U},
                         1U,
                         "dungeon_map_floor_label_4_x"},
    TopScreenLayoutPatch{0x002F0404U,
                         {0x43820000U, 0U},
                         {0x43960000U, 0U},
                         1U,
                         "dungeon_map_floor_cursor_x"},
    TopScreenLayoutPatch{0x002F0410U,
                         {0x41000000U, 0U},
                         {0xC2000000U, 0U},
                         1U,
                         "dungeon_map_item_cursor_x"},
    TopScreenLayoutPatch{0x0044262CU,
                         {0x43828000U, 0U},
                         {0x43968000U, 0U},
                         1U,
                         "dungeon_map_current_floor_x"},
    TopScreenLayoutPatch{0x0044264CU,
                         {0x43980000U, 0U},
                         {0x43AC0000U, 0U},
                         1U,
                         "dungeon_map_boss_floor_x"},
    TopScreenLayoutPatch{0x0042D9F0U,
                         {0x41A00000U, 0U},
                         {0xC1A00000U, 0U},
                         1U,
                         "dungeon_map_count_x"},
    TopScreenLayoutPatch{0x004D4DBCU,
                         {0x43D00000U, 0U},
                         {0x43D08000U, 0U},
                         1U,
                         "dungeon_map_floor_number_0_u"},
    TopScreenLayoutPatch{0x004D4DC4U,
                         {0x43D00000U, 0U},
                         {0x43D08000U, 0U},
                         1U,
                         "dungeon_map_floor_number_1_u"},
    TopScreenLayoutPatch{0x004D4DCCU,
                         {0x43D00000U, 0U},
                         {0x43D08000U, 0U},
                         1U,
                         "dungeon_map_floor_number_2_u"},
    TopScreenLayoutPatch{0x004D4DD4U,
                         {0x43D00000U, 0U},
                         {0x43D08000U, 0U},
                         1U,
                         "dungeon_map_floor_number_3_u"},
    TopScreenLayoutPatch{0x004D4DDCU,
                         {0x43D00000U, 0U},
                         {0x43D08000U, 0U},
                         1U,
                         "dungeon_map_floor_number_4_u"},
    TopScreenLayoutPatch{0x004D4DE4U,
                         {0x43D00000U, 0U},
                         {0x43D08000U, 0U},
                         1U,
                         "dungeon_map_floor_number_5_u"},
    TopScreenLayoutPatch{0x004D4DECU,
                         {0x43D00000U, 0U},
                         {0x43D08000U, 0U},
                         1U,
                         "dungeon_map_floor_number_6_u"},
    TopScreenLayoutPatch{0x004D4DF4U,
                         {0x43D00000U, 0U},
                         {0x43D08000U, 0U},
                         1U,
                         "dungeon_map_floor_number_7_u"},
    TopScreenLayoutPatch{0x004D4DFCU,
                         {0x43A00000U, 0U},
                         {0x43A08000U, 0U},
                         1U,
                         "dungeon_map_basement_number_0_u"},
    TopScreenLayoutPatch{0x004D4E04U,
                         {0x43A00000U, 0U},
                         {0x43A08000U, 0U},
                         1U,
                         "dungeon_map_basement_number_1_u"},
    TopScreenLayoutPatch{0x004D4E0CU,
                         {0x43A00000U, 0U},
                         {0x43A08000U, 0U},
                         1U,
                         "dungeon_map_basement_number_2_u"},
    TopScreenLayoutPatch{0x004D4E14U,
                         {0x43A00000U, 0U},
                         {0x43A08000U, 0U},
                         1U,
                         "dungeon_map_basement_number_3_u"},
    TopScreenLayoutPatch{0x005075C0U,
                         {0x41000000U, 0U},
                         {0x40F00000U, 0U},
                         1U,
                         "map_page_shared_uv_extent_0"},
    TopScreenLayoutPatch{0x005075E0U,
                         {0x41000000U, 0U},
                         {0x40F00000U, 0U},
                         1U,
                         "map_page_shared_uv_extent_1"},
    TopScreenLayoutPatch{0x005075F0U,
                         {0xC1000000U, 0U},
                         {0xC0F00000U, 0U},
                         1U,
                         "map_page_shared_uv_extent_mirrored"},
    TopScreenLayoutPatch{0x005076E0U,
                         {0x43B00000U, 0U},
                         {0x43AFC000U, 0U},
                         1U,
                         "map_page_shared_uv_origin"},
    TopScreenLayoutPatch{0x004D2E6CU,
                         {0x42F00000U, 0x42E00000U},
                         {0U, 0U},
                         2U,
                         "pause_overlay_layout_widths_3_4"},
    TopScreenLayoutPatch{0x004D2E84U,
                         {0x42000000U, 0x42000000U},
                         {0U, 0U},
                         2U,
                         "pause_overlay_layout_heights_3_4"},
    TopScreenLayoutPatch{0x00506F40U,
                         {0x42480000U, 0x42180000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "dungeon_map_page_anchor_0"},
    TopScreenLayoutPatch{0x00506F48U,
                         {0x41000000U, 0x42180000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "dungeon_map_page_anchor_1"},
    TopScreenLayoutPatch{0x00506F50U,
                         {0x42600000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "dungeon_map_page_anchor_2"},
    TopScreenLayoutPatch{0x00507028U,
                         {0x42200000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "dungeon_map_page_anchor_3"},
    TopScreenLayoutPatch{0x00507030U,
                         {0x41800000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "dungeon_map_page_anchor_4"},
    TopScreenLayoutPatch{0x005074C8U,
                         {0x42480000U, 0x42180000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "map_page_shared_anchor_0"},
    TopScreenLayoutPatch{0x005074D0U,
                         {0x41000000U, 0x42180000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "map_page_shared_anchor_1"},
    TopScreenLayoutPatch{0x005074D8U,
                         {0x42600000U, 0x40800000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "map_page_shared_anchor_2"},
    TopScreenLayoutPatch{0x005074E0U,
                         {0x42600000U, 0x41800000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "map_page_shared_anchor_3"},
    TopScreenLayoutPatch{0x005074E8U,
                         {0x42600000U, 0x41800000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "map_page_shared_anchor_4"},
    TopScreenLayoutPatch{0x00507528U,
                         {0x42200000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "map_page_shared_anchor_5"},
    TopScreenLayoutPatch{0x00507530U,
                         {0x41800000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "map_page_shared_anchor_6"},
    TopScreenLayoutPatch{0x0050A834U,
                         {0x42480000U, 0x42180000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "system_page_anchor_0"},
    TopScreenLayoutPatch{0x0050A83CU,
                         {0x41000000U, 0x42180000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "system_page_anchor_1"},
    TopScreenLayoutPatch{0x0050A844U,
                         {0x42600000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "system_page_anchor_2"},
    TopScreenLayoutPatch{0x0050B09CU,
                         {0x40400000U, 0x00000000U},
                         {0x41300000U, 0x00000000U},
                         1U,
                         "system_page_layout_inset"},
    TopScreenLayoutPatch{0x004D31BCU,
                         {0x41400000U, 0x42680000U},
                         {0x40800000U, 0x430C0000U},
                         2U,
                         "quest_auxiliary_top_left"},
    TopScreenLayoutPatch{0x004D31C8U,
                         {0x42700000U, 0x42680000U},
                         {0x42500000U, 0x430C0000U},
                         2U,
                         "quest_auxiliary_top_right"},
    TopScreenLayoutPatch{0x004D31D4U,
                         {0x41400000U, 0x41200000U},
                         {0x40800000U, 0x42B80000U},
                         2U,
                         "quest_auxiliary_bottom_left"},
    TopScreenLayoutPatch{0x004D31E0U,
                         {0x42700000U, 0x41200000U},
                         {0x42500000U, 0x42B80000U},
                         2U,
                         "quest_auxiliary_bottom_right"},
    TopScreenLayoutPatch{0x004D3EB8U,
                         {0x43200000U, 0xC3200000U},
                         {0x431E0000U, 0xC31E0000U},
                         2U,
                         "pause_projection_horizontal_bounds"},
    TopScreenLayoutPatch{0x004D3EC8U,
                         {0x00000000U, 0x43200000U},
                         {0x3F800000U, 0x431F0000U},
                         2U,
                         "pause_projection_vertical_bounds"},
    TopScreenLayoutPatch{0x004D25E0U,
                         {0x43870000U, 0U},
                         {0x43858000U, 0U},
                         1U,
                         "pause_quad_group_0_x"},
    TopScreenLayoutPatch{0x004D25E8U,
                         {0x43870000U, 0U},
                         {0x43860000U, 0U},
                         1U,
                         "pause_quad_group_0_y"},
    TopScreenLayoutPatch{0x004D2600U,
                         {0x41500000U, 0U},
                         {0x41900000U, 0U},
                         1U,
                         "pause_quad_group_1_x"},
    TopScreenLayoutPatch{0x004D2608U,
                         {0x41500000U, 0U},
                         {0x41900000U, 0U},
                         1U,
                         "pause_quad_group_1_y"},
    TopScreenLayoutPatch{0x004D2620U,
                         {0x42840000U, 0U},
                         {0x427C0000U, 0U},
                         1U,
                         "pause_quad_group_2_x"},
    TopScreenLayoutPatch{0x004D2628U,
                         {0x42A40000U, 0U},
                         {0x42A00000U, 0U},
                         1U,
                         "pause_quad_group_2_y"},
    TopScreenLayoutPatch{0x004D2640U,
                         {0x43870000U, 0U},
                         {0x43858000U, 0U},
                         1U,
                         "pause_quad_group_3_x"},
    TopScreenLayoutPatch{0x004D2648U,
                         {0x43870000U, 0U},
                         {0x43860000U, 0U},
                         1U,
                         "pause_quad_group_3_y"},
    TopScreenLayoutPatch{0x004D2660U,
                         {0x41500000U, 0U},
                         {0x41900000U, 0U},
                         1U,
                         "pause_quad_group_4_x"},
    TopScreenLayoutPatch{0x004D2668U,
                         {0x41500000U, 0U},
                         {0x41900000U, 0U},
                         1U,
                         "pause_quad_group_4_y"},
    TopScreenLayoutPatch{0x004D2680U,
                         {0x42840000U, 0U},
                         {0x427C0000U, 0U},
                         1U,
                         "pause_quad_group_5_x"},
    TopScreenLayoutPatch{0x004D2688U,
                         {0x42A40000U, 0U},
                         {0x42A00000U, 0U},
                         1U,
                         "pause_quad_group_5_y"},
    // File Select and Name Entry page anchors, zeroed identically by 2.1.1
    // and SingleScreen 3.0 (the Items page keeps its native anchors in 3.0).
    TopScreenLayoutPatch{0x00505968U,
                         {0x42480000U, 0x42180000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "file_select_anchor_0"},
    TopScreenLayoutPatch{0x00505970U,
                         {0x41000000U, 0x42180000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "file_select_anchor_1"},
    TopScreenLayoutPatch{0x00505978U,
                         {0x42600000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "file_select_anchor_2"},
    TopScreenLayoutPatch{0x00505B20U,
                         {0x42200000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "file_select_anchor_3"},
    TopScreenLayoutPatch{0x00505B28U,
                         {0x41800000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "file_select_anchor_4"},
    TopScreenLayoutPatch{0x00507C1CU,
                         {0x42480000U, 0x42180000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "name_entry_anchor_0"},
    TopScreenLayoutPatch{0x00507C24U,
                         {0x41000000U, 0x42180000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "name_entry_anchor_1"},
    TopScreenLayoutPatch{0x00507C2CU,
                         {0x42600000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "name_entry_anchor_2"},
    TopScreenLayoutPatch{0x00507C34U,
                         {0x42200000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "name_entry_anchor_3"},
    TopScreenLayoutPatch{0x00507C3CU,
                         {0x41800000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "name_entry_anchor_4"},
};

// Recovered directly from the five native page constructors and
// PauseRenderBuffer_SetQuadRects/SetQuadTexcoords. The owner tables remain the
// authoritative source; these descriptors only reconnect already-constructed
// streams after a savestate restore.
constexpr std::array kVerifiedRuntimeQuadStreams{
    TopScreenRuntimeQuadStreamContract{0x005066F8U, 0x24U, 0xC0U, 0x1E8U,
                                       0x438U, 0x310U, 37U, "items_main"},
    TopScreenRuntimeQuadStreamContract{0x0050446CU, 0x0CU, 0x70U, 0x1D0U,
                                       0x490U, 0x330U, 41U, "gear_main"},
    TopScreenRuntimeQuadStreamContract{0x00506CB0U, 0x14U, 0xB0U, 0x240U,
                                       0x560U, 0x3D0U, 50U,
                                       "dungeon_map_main"},
    TopScreenRuntimeQuadStreamContract{0x00506CB0U, 0x10U, 0x6F0U, 0x7E0U,
                                       0x9C0U, 0x8D0U, 30U,
                                       "map_page_shared"},
    TopScreenRuntimeQuadStreamContract{0x0050A508U, 0x08U, 0x44U, 0x2B4U,
                                       0x794U, 0x524U, 78U,
                                       "system_menu_main"},
};

struct StagedTopScreenGeometryWord {
  std::uint32_t Address = 0;
  std::uint32_t Value = 0;
};

bool ReadTopScreenGeometryWord(const NativeA32Memory &memory,
                               std::uint32_t address,
                               std::uint32_t *value) {
  return value != nullptr && memory.ReadFast(address, value);
}

bool ReadTopScreenGeometryFloat(const NativeA32Memory &memory,
                                std::uint32_t address, float *value) {
  std::uint32_t bits = 0;
  if (value == nullptr || !ReadTopScreenGeometryWord(memory, address, &bits)) {
    return false;
  }
  *value = std::bit_cast<float>(bits);
  return std::isfinite(*value);
}

void StageTopScreenGeometryFloat(
    std::vector<StagedTopScreenGeometryWord> &writes, std::uint32_t address,
    float value) {
  writes.push_back({address, std::bit_cast<std::uint32_t>(value)});
}

constexpr std::array kVerifiedItemQueryContracts{
    TopScreenItemQueryContract{TopScreenItemQuery::ItemIPressed, 0x00349504U,
                               0x3CU, 0x04000000U, "item_i_pressed"},
    TopScreenItemQueryContract{TopScreenItemQuery::ItemIIPressed, 0x003494F4U,
                               0x40U, 0x02000000U, "item_ii_pressed"},
    TopScreenItemQueryContract{TopScreenItemQuery::ItemIHeld, 0x002C3960U,
                               0x44U, 0x04000000U, "item_i_held"},
    TopScreenItemQueryContract{TopScreenItemQuery::ItemIIHeld, 0x002C3950U,
                               0x48U, 0x02000000U, "item_ii_held"},
};

// 2.1.1 also branches 0x0041EB40 -> 0x0041EC10, skipping the pause scene dim
// (EnvironmentRenderer_SubmitViewportConfig with a translucent grey). At
// 400x240 the page covers the scene; wider output shows it beside the page, so
// the port keeps the native dim there. With no page beside the scene (the
// View Model and the System page) the candidate dispatch takes the branch.
constexpr std::array kVerifiedControlFlowContracts{
    TopScreenControlFlowContract{0x0042BF6CU, 0x0042BF84U,
                                 "pause_quest_prompt_use_top_screen_geometry"},
    TopScreenControlFlowContract{
        0x002E27C0U, 0x002E2A04U,
        "gameplay_draw_skip_native_lower_screen_composition"},
    TopScreenControlFlowContract{0x004848F0U, 0x004848F4U,
                                 "hint_movie_skip_renderer_4e4_submission"},
    TopScreenControlFlowContract{0x00484944U, 0x00484948U,
                                 "hint_movie_skip_renderer_4e8_submission"},
    TopScreenControlFlowContract{0x004849B0U, 0x004849B4U,
                                 "hint_movie_skip_renderer_4ec_submission"},
    TopScreenControlFlowContract{0x00324638U, 0x0032463CU,
                                 "transform_uniforms_skip_command_bind"},
    TopScreenControlFlowContract{0x0032464CU, 0x00324650U,
                                 "transform_uniforms_skip_block0_upload"},
    TopScreenControlFlowContract{0x00324658U, 0x0032465CU,
                                 "transform_uniforms_skip_block4_upload"},
    TopScreenControlFlowContract{0x00324664U, 0x00324668U,
                                 "transform_uniforms_skip_command_stats"},
};

constexpr std::array kVerifiedFloatLoadContracts{
    TopScreenFloatLoadContract{0x00469AE8U, 0U, 0x42040000U,
                               "quest_auxiliary_renderer_origin_x"},
    TopScreenFloatLoadContract{0x00469AECU, 1U, 0x42DC0000U,
                               "quest_auxiliary_renderer_origin_y"},
};

struct NativeTouchCopyContract {
  std::uint8_t SourceQuad;
  std::int16_t SourceCenterX;
  std::int16_t SourceCenterY;
  std::int16_t DestinationX;
  std::int16_t DestinationY;
  float Scale;
};

struct NativeItemIconRegion {
  std::int16_t MinimumX;
  std::int16_t MaximumX;
  std::int16_t MinimumY;
  std::int16_t MaximumY;
  std::int16_t SourceCenterX;
  std::int16_t SourceCenterY;
  std::int16_t DestinationCenterX;
  std::int16_t DestinationCenterY;
  float Scale;
};

// Payload table 0x005D33C8. Regions 0..3 receive the four live button
// depression offsets through the jump table at 0x005CABB8.
constexpr std::array kNativeItemIconRegions{
    NativeItemIconRegion{250, 340, 0, 60, 266, 2, 366, 28, 0.6F},
    NativeItemIconRegion{250, 340, 60, 106, 282, 65, 347, 9, 0.6F},
    NativeItemIconRegion{250, 340, 106, 160, 264, 107, 323, 33, 0.6F},
    NativeItemIconRegion{250, 340, 160, 260, 266, 186, 342, 52, 0.6F},
    NativeItemIconRegion{-20, 60, 160, 260, 2, 186, 22, 76, 0.38F},
    NativeItemIconRegion{340, 420, 140, 260, 370, 215, 240, 24, 0.6F},
};

constexpr std::array<float, 10> kCounterAtlasY{
    0.0F, 12.0F, 24.0F, 42.0F, 60.0F, 84.0F, 108.0F, 60.0F, 60.0F, 60.0F};
constexpr std::array<float, 10> kCounterGlyphHeight{
    12.0F, 12.0F, 14.0F, 14.0F, 21.0F, 21.0F, 21.0F, 21.0F, 21.0F, 21.0F};

std::size_t AppendTopScreenCounterDigits(
    std::uint32_t ownerAddress, std::uint32_t descriptorAddress,
    std::uint32_t value, std::uint8_t type, float originX, float originY,
    float scale, const oot3d::ui::UiTextureIdentity &numberGlyphs,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  const std::uint32_t digits =
      type <= 3U ? 2U : (type == 8U ? 5U : (type == 9U ? 4U : 3U));
  const std::size_t metric = std::min<std::size_t>(type, 9U);
  std::uint32_t divisor = 1U;
  const std::size_t startSize = output.size();
  for (std::uint32_t index = 0U; index < digits; ++index) {
    if (index != 0U && value < divisor) {
      divisor *= 10U;
      continue;
    }
    const std::uint32_t digit = (value / divisor) % 10U;
    const float slotX =
        originX + static_cast<float>(digits - index - 1U) * 10.0F * scale;
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::CounterDigit;
    primitive.owner_address = ownerAddress;
    primitive.descriptor_address = descriptorAddress;
    primitive.source_quad = index;
    primitive.texture = numberGlyphs;
    primitive.destination = {slotX, originY, 11.0F * scale,
                             kCounterGlyphHeight[metric] * scale};
    primitive.uv = {static_cast<float>(digit) * 12.0F / 256.0F,
                    kCounterAtlasY[metric] / 128.0F, 12.0F / 256.0F,
                    kCounterGlyphHeight[metric] / 128.0F};
    primitive.layer = 14U;
    output.push_back(std::move(primitive));
    divisor *= 10U;
  }
  return output.size() - startSize;
}

// TopScreen 2.1.1 table 005E1734, consumed by FUN_005D4A8C.
constexpr std::array kNativeTouchCopyContracts{
    NativeTouchCopyContract{34U, 278, 149, 229, 11, 1.0F},
    NativeTouchCopyContract{35U, 278, 149, 229, 11, 1.0F},
    NativeTouchCopyContract{26U, 2, 111, 4, 216, 1.0F},
    NativeTouchCopyContract{27U, 2, 151, 4, 196, 1.0F},
    NativeTouchCopyContract{86U, 88, 8, 128, 210, 1.0F},
    NativeTouchCopyContract{87U, 88, 8, 128, 210, 1.0F},
    NativeTouchCopyContract{88U, 88, 8, 128, 210, 1.0F},
    NativeTouchCopyContract{89U, 88, 8, 128, 210, 1.0F},
    NativeTouchCopyContract{90U, 88, 8, 128, 210, 1.0F},
    NativeTouchCopyContract{91U, 88, 8, 128, 210, 1.0F},
};

constexpr std::size_t ItemQueryIndex(TopScreenItemQuery query) noexcept {
  return static_cast<std::size_t>(query);
}

void SetError(std::string *error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

oot3d::ui::UiRect
NativePicaQuadUvToHost(const std::array<float, 8> &uvs) noexcept {
  // Native UI geometry stores V in PICA's bottom-origin convention.  Host UI
  // atlases are decoded top-down, so copied guest quads need one conversion at
  // the ownership boundary; profile-authored atlas rectangles do not.
  return {uvs[0], 1.0F - uvs[1], uvs[2] - uvs[0], uvs[1] - uvs[5]};
}

} // namespace

} // namespace Oot3dNativeGame
