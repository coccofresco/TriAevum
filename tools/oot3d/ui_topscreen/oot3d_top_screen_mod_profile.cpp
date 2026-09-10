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
    TopScreenLayoutPatch{0x005068F8U,
                         {0x42480000U, 0x42180000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "items_page_anchor_0"},
    TopScreenLayoutPatch{0x00506900U,
                         {0x41000000U, 0x42180000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "items_page_anchor_1"},
    TopScreenLayoutPatch{0x00506908U,
                         {0x42600000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "items_page_anchor_2"},
    TopScreenLayoutPatch{0x005069B0U,
                         {0x42200000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "items_page_anchor_3"},
    TopScreenLayoutPatch{0x005069B8U,
                         {0x41800000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "items_page_anchor_4"},
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
    TopScreenLayoutPatch{0x00504704U,
                         {0x42480000U, 0x42180000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "gear_page_anchor_0"},
    TopScreenLayoutPatch{0x0050470CU,
                         {0x41000000U, 0x42180000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "gear_page_anchor_1"},
    TopScreenLayoutPatch{0x00504714U,
                         {0x42600000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "gear_page_anchor_2"},
    TopScreenLayoutPatch{0x0050472CU,
                         {0x42200000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "gear_page_anchor_3"},
    TopScreenLayoutPatch{0x00504734U,
                         {0x41800000U, 0x42100000U},
                         {0x00000000U, 0x00000000U},
                         2U,
                         "gear_page_anchor_4"},
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

constexpr std::array kVerifiedControlFlowContracts{
    TopScreenControlFlowContract{0x0042BF6CU, 0x0042BF84U,
                                 "pause_quest_prompt_use_top_screen_geometry"},
    TopScreenControlFlowContract{0x0041EB40U, 0x0041EC10U,
                                 "pause_update_skip_lower_screen_projection"},
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

// Payload table 0x005D3440, consumed by 0x005C9940.
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

const char *Oot3dUiProfileName(Oot3dUiProfile profile) noexcept {
  switch (profile) {
  case Oot3dUiProfile::Oot3d:
    return "oot3d";
  case Oot3dUiProfile::TopScreen:
    return "topscreen";
  }
  return "oot3d";
}

bool ParseOot3dUiProfile(std::string_view value,
                         Oot3dUiProfile *profile) noexcept {
  if (profile == nullptr) {
    return false;
  }
  if (value == "oot3d") {
    *profile = Oot3dUiProfile::Oot3d;
    return true;
  }
  if (value == "topscreen") {
    *profile = Oot3dUiProfile::TopScreen;
    return true;
  }
  return false;
}

bool ShouldPresentNativeBottomFrontend(
    Oot3dUiProfile profile, bool nativePresentationActive) noexcept {
  return profile == Oot3dUiProfile::Oot3d && nativePresentationActive;
}

bool ShouldSuppressTopScreenFrontendBackdrop(
    Oot3dUiProfile profile, bool nativePresentationActive) noexcept {
  return profile == Oot3dUiProfile::TopScreen && nativePresentationActive;
}

std::span<const TopScreenLayoutPatch>
TopScreenVerifiedLayoutPatches() noexcept {
  return kVerifiedLayoutPatches;
}

bool ApplyTopScreenVerifiedLayout(NativeA32Memory &memory,
                                  TopScreenLayoutApplyStats *stats,
                                  std::string *error) {
  TopScreenLayoutApplyStats result;

  // Complete validation precedes the first write. Already-applied values are
  // accepted so savestate resets and host reinitialization are idempotent.
  for (const auto &patch : kVerifiedLayoutPatches) {
    ++result.ContractsChecked;
    for (std::uint8_t word = 0; word < patch.WordCount; ++word) {
      std::uint32_t actual = 0;
      const std::uint32_t address =
          patch.Address + static_cast<std::uint32_t>(word) * 4U;
      if (!memory.Read32(address, &actual)) {
        std::ostringstream message;
        message << "cannot read TopScreen layout contract " << patch.Semantic
                << " at 0x" << std::hex << address;
        SetError(error, message.str());
        return false;
      }
      ++result.WordsChecked;
      if (actual != patch.Expected[word] && actual != patch.Replacement[word]) {
        std::ostringstream message;
        message << "TopScreen layout contract mismatch for " << patch.Semantic
                << " at 0x" << std::hex << address << ": expected 0x"
                << patch.Expected[word] << " but found 0x" << actual;
        SetError(error, message.str());
        return false;
      }
      result.WordsChanged += actual != patch.Replacement[word] ? 1U : 0U;
    }
  }

  for (const auto &patch : kVerifiedLayoutPatches) {
    for (std::uint8_t word = 0; word < patch.WordCount; ++word) {
      const std::uint32_t address =
          patch.Address + static_cast<std::uint32_t>(word) * 4U;
      if (!memory.WriteHost<std::uint32_t>(address, patch.Replacement[word])) {
        std::ostringstream message;
        message << "cannot write TopScreen layout contract " << patch.Semantic
                << " at 0x" << std::hex << address;
        SetError(error, message.str());
        return false;
      }
      ++result.WordsWritten;
    }
  }
  if (stats != nullptr) {
    *stats = result;
  }
  return true;
}

std::span<const TopScreenRuntimeQuadStreamContract>
TopScreenVerifiedRuntimeQuadStreams() noexcept {
  return kVerifiedRuntimeQuadStreams;
}

bool RebindTopScreenVerifiedRuntimeGeometry(
    NativeA32Memory &memory, TopScreenRuntimeGeometryApplyStats *stats,
    std::string *error) {
  TopScreenRuntimeGeometryApplyStats result;
  std::vector<StagedTopScreenGeometryWord> writes;

  for (const auto &contract : kVerifiedRuntimeQuadStreams) {
    ++result.ContractsChecked;

    std::uint32_t renderBuffer = 0;
    if (!ReadTopScreenGeometryWord(
            memory,
            contract.OwnerAddress + contract.RenderBufferPointerOffset,
            &renderBuffer)) {
      SetError(error, "TopScreen runtime geometry owner is unmapped: " +
                          std::string(contract.Semantic));
      return false;
    }
    if (renderBuffer == 0U) {
      ++result.StreamsNotInitialized;
      continue;
    }

    std::uint32_t nativeQuadCount = 0;
    std::uint32_t positions = 0;
    std::uint32_t texcoords = 0;
    float textureWidth = 0.0F;
    float textureHeight = 0.0F;
    if (!ReadTopScreenGeometryWord(memory, renderBuffer, &nativeQuadCount) ||
        nativeQuadCount != contract.QuadCount ||
        !ReadTopScreenGeometryFloat(memory, renderBuffer + 4U,
                                    &textureWidth) ||
        !ReadTopScreenGeometryFloat(memory, renderBuffer + 8U,
                                    &textureHeight) ||
        textureWidth <= 0.0F || textureHeight <= 0.0F ||
        !ReadTopScreenGeometryWord(memory, renderBuffer + 0x0CU,
                                   &positions) ||
        !ReadTopScreenGeometryWord(memory, renderBuffer + 0x14U,
                                   &texcoords) ||
        positions == 0U || texcoords == 0U ||
        !memory.IsMapped(positions,
                         static_cast<std::size_t>(contract.QuadCount) * 48U) ||
        !memory.IsMapped(texcoords,
                         static_cast<std::size_t>(contract.QuadCount) * 32U)) {
      SetError(error, "TopScreen runtime geometry binding is incompatible: " +
                          std::string(contract.Semantic));
      return false;
    }

    for (std::uint32_t quad = 0; quad < contract.QuadCount; ++quad) {
      const std::uint32_t sourceOffset = quad * 8U;
      float x = 0.0F;
      float y = 0.0F;
      float width = 0.0F;
      float height = 0.0F;
      float textureX = 0.0F;
      float textureY = 0.0F;
      float textureExtentX = 0.0F;
      float textureExtentY = 0.0F;
      if (!ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.RectOriginOffset +
                          sourceOffset,
              &x) ||
          !ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.RectOriginOffset +
                          sourceOffset + 4U,
              &y) ||
          !ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.RectExtentOffset +
                          sourceOffset,
              &width) ||
          !ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.RectExtentOffset +
                          sourceOffset + 4U,
              &height) ||
          !ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.TextureOriginOffset +
                          sourceOffset,
              &textureX) ||
          !ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.TextureOriginOffset +
                          sourceOffset + 4U,
              &textureY) ||
          !ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.TextureExtentOffset +
                          sourceOffset,
              &textureExtentX) ||
          !ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.TextureExtentOffset +
                          sourceOffset + 4U,
              &textureExtentY)) {
        SetError(error,
                 "TopScreen runtime geometry source is incompatible: " +
                     std::string(contract.Semantic));
        return false;
      }

      const std::uint32_t positionBase = positions + quad * 48U;
      StageTopScreenGeometryFloat(writes, positionBase, x);
      StageTopScreenGeometryFloat(writes, positionBase + 4U, y);
      StageTopScreenGeometryFloat(writes, positionBase + 12U, x + width);
      StageTopScreenGeometryFloat(writes, positionBase + 16U, y);
      StageTopScreenGeometryFloat(writes, positionBase + 24U, x);
      StageTopScreenGeometryFloat(writes, positionBase + 28U, y + height);
      StageTopScreenGeometryFloat(writes, positionBase + 36U, x + width);
      StageTopScreenGeometryFloat(writes, positionBase + 40U, y + height);

      const float u0 = textureX / textureWidth;
      const float u1 = (textureX + textureExtentX) / textureWidth;
      const float v0 = (textureHeight - textureY) / textureHeight;
      const float v1 =
          (textureHeight - (textureY + textureExtentY)) / textureHeight;
      const std::uint32_t texcoordBase = texcoords + quad * 32U;
      StageTopScreenGeometryFloat(writes, texcoordBase, u0);
      StageTopScreenGeometryFloat(writes, texcoordBase + 4U, v0);
      StageTopScreenGeometryFloat(writes, texcoordBase + 8U, u1);
      StageTopScreenGeometryFloat(writes, texcoordBase + 12U, v0);
      StageTopScreenGeometryFloat(writes, texcoordBase + 16U, u0);
      StageTopScreenGeometryFloat(writes, texcoordBase + 20U, v1);
      StageTopScreenGeometryFloat(writes, texcoordBase + 24U, u1);
      StageTopScreenGeometryFloat(writes, texcoordBase + 28U, v1);
    }

    ++result.StreamsRebound;
    result.QuadsRebound += contract.QuadCount;
  }

  for (const auto &write : writes) {
    if (!memory.WriteHost<std::uint32_t>(write.Address, write.Value)) {
      SetError(error, "TopScreen runtime geometry write failed");
      return false;
    }
  }
  result.WordsWritten = static_cast<std::uint32_t>(writes.size());
  if (stats != nullptr) {
    *stats = result;
  }
  return true;
}

bool ResolveTopScreenItemLane(const TopScreenItemLaneQuery &query) noexcept {
  if (query.CompatibilityModeActive)
    return true;
  if (!query.ExtendedButtonHeld) {
    return query.NativeResult;
  }
  return (query.SuppressionFlags & query.SuppressionMask) == 0U
             ? true
             : query.NativeResult;
}

std::span<const TopScreenItemQueryContract>
TopScreenVerifiedItemQueryContracts() noexcept {
  return kVerifiedItemQueryContracts;
}

std::array<bool, 4> BuildTopScreenItemCompatibilityOverrides(
    const TopScreenExtendedInputFrame &input, std::uint8_t itemISlotIdentity,
    std::uint8_t itemIISlotIdentity) noexcept {
  if (input.RestorationLayout) {
    return {};
  }
  const bool itemI = input.DpadLeftHeld && itemISlotIdentity == 0x45U;
  const bool itemII = input.DpadRightHeld && itemIISlotIdentity == 0x46U;
  return {itemI, itemII, itemI, itemII};
}

bool HasTopScreenSlotItemOverrideInput(
    const TopScreenExtendedInputFrame &input) noexcept {
  return !input.RestorationLayout &&
         (input.DpadLeftHeld || input.DpadRightHeld);
}

bool HasTopScreenItemQueryOverrideInput(
    const TopScreenExtendedInputFrame &input) noexcept {
  if (HasTopScreenSlotItemOverrideInput(input)) {
    return true;
  }
  if (input.RestorationLayout) {
    return (input.ZrHeld &&
            (input.XPressed || input.YPressed || input.XHeld || input.YHeld)) ||
           (input.ZrPressed && (input.XHeld || input.YHeld));
  }
  return input.ZrPressed || input.ZlPressed || input.ZrHeld || input.ZlHeld;
}

bool ResolveTopScreenItemQuery(TopScreenItemQuery query,
                               const TopScreenItemQueryState &state) noexcept {
  const auto index = ItemQueryIndex(query);
  const bool extendedButton = [&]() {
    if (state.Input.RestorationLayout) {
      switch (query) {
      case TopScreenItemQuery::ItemIPressed:
        return (state.Input.ZrHeld && state.Input.XPressed) ||
               (state.Input.ZrPressed && state.Input.XHeld);
      case TopScreenItemQuery::ItemIIPressed:
        return (state.Input.ZrHeld && state.Input.YPressed) ||
               (state.Input.ZrPressed && state.Input.YHeld);
      case TopScreenItemQuery::ItemIHeld:
        return state.Input.ZrHeld && state.Input.XHeld;
      case TopScreenItemQuery::ItemIIHeld:
        return state.Input.ZrHeld && state.Input.YHeld;
      }
      return false;
    }
    switch (query) {
    case TopScreenItemQuery::ItemIPressed:
      return state.Input.ZrPressed;
    case TopScreenItemQuery::ItemIIPressed:
      return state.Input.ZlPressed;
    case TopScreenItemQuery::ItemIHeld:
      return state.Input.ZrHeld;
    case TopScreenItemQuery::ItemIIHeld:
      return state.Input.ZlHeld;
    }
    return false;
  }();
  return ResolveTopScreenItemLane(
      {state.NativeResults[index], extendedButton,
       state.CompatibilityOverrides[index], state.SuppressionFlags,
       kVerifiedItemQueryContracts[index].SuppressionMask});
}

std::uint8_t
ResolveTopScreenSlotItemId(const TopScreenSlotItemQuery &query) noexcept {
  if (query.NativeSpecialStateActive) {
    return query.NativeItemId;
  }
  if (query.Slot == 3U && query.ItemICompatibilityOverride) {
    return 0x45U;
  }
  if (query.Slot == 4U && query.ItemIICompatibilityOverride) {
    return 0x46U;
  }
  return query.NativeItemId;
}

TopScreenHealthGeometry
BuildTopScreenHealthGeometry(std::uint16_t health, std::uint16_t healthCapacity,
                             bool alternateAtlasRow,
                             std::uint8_t pulsePhase) noexcept {
  constexpr std::array<float, 5> kAtlasRows{80.0F, 96.0F, 112.0F, 128.0F,
                                            144.0F};
  TopScreenHealthGeometry result;
  const std::uint32_t visibleHearts =
      (static_cast<std::uint32_t>(healthCapacity) + 15U) / 16U;
  std::uint32_t remainingHealth = health;
  for (std::size_t index = 0; index < result.Positions.size(); ++index) {
    const auto column = static_cast<float>(index % 10U);
    const auto row = static_cast<float>(index / 10U);
    result.Positions[index] =
        index < visibleHearts
            ? TopScreenVec2{8.0F + column * 12.0F, 6.0F + row * 9.75F}
            : TopScreenVec2{400.0F, 400.0F};
    result.Sizes[index] = {9.0F, 9.0F};
    result.AtlasSizes[index] = {12.0F, 12.0F};

    const std::size_t atlasRow = remainingHealth == 0U   ? 0U
                                 : remainingHealth < 5U  ? 1U
                                 : remainingHealth < 9U  ? 2U
                                 : remainingHealth < 13U ? 3U
                                                         : 4U;
    result.AtlasOrigins[index] = {
        240.0F, kAtlasRows[atlasRow] - (alternateAtlasRow ? 80.0F : 0.0F)};
    remainingHealth = remainingHealth > 16U ? remainingHealth - 16U : 0U;
  }

  const std::uint32_t currentHeart =
      health == 0U ? 0U : (static_cast<std::uint32_t>(health) + 15U) / 16U - 1U;
  result.PulseHeart = static_cast<std::uint8_t>(currentHeart);
  std::uint8_t pulseStep = 0U;
  if (health != 0U) {
    if (pulsePhase < 2U || (pulsePhase >= 12U && pulsePhase < 14U)) {
      pulseStep = 1U;
    } else if ((pulsePhase >= 2U && pulsePhase < 4U) ||
               (pulsePhase >= 10U && pulsePhase < 12U)) {
      pulseStep = 2U;
    } else if ((pulsePhase >= 4U && pulsePhase < 6U) ||
               (pulsePhase >= 8U && pulsePhase < 10U)) {
      pulseStep = 3U;
    } else if (pulsePhase >= 6U && pulsePhase < 8U) {
      pulseStep = 4U;
    }
  }
  result.PulseExpansion = static_cast<float>(pulseStep) * 0.75F;
  return result;
}

TopScreenMagicMeterGeometry
BuildTopScreenMagicMeterGeometry(const oot3d::ui::UiHudMagicContent &magic,
                                 std::uint16_t healthCapacity) noexcept {
  TopScreenMagicMeterGeometry result;
  if (!magic.acquired.IsKnown() || !magic.current.IsKnown() ||
      !magic.double_magic_acquired.IsKnown() || !magic.acquired.value) {
    return result;
  }
  result.Visible = true;
  result.AtlasOrigins = {
      {{136.0F, 120.0F}, {142.0F, 120.0F}, {143.0F, 120.0F}, {153.0F, 120.0F}}};
  result.AtlasSizes = {
      {{8.0F, 8.0F}, {1.0F, 8.0F}, {-8.0F, 8.0F}, {6.0F, 4.0F}}};

  const auto capacityHearts =
      (static_cast<std::uint32_t>(healthCapacity) + 15U) / 16U;
  const float y = capacityHearts > 10U ? 25.5F : 15.75F;
  const bool alternate = magic.double_magic_acquired.value;
  result.Positions[0] = {5.0F, y};
  result.Sizes[0] = {6.0F, 6.0F};
  result.Positions[1] = {11.0F, y};
  result.Sizes[1] = {alternate ? 111.0F : 49.5F, 6.0F};
  result.Positions[2] = {alternate ? 122.0F : 60.5F, y};
  result.Sizes[2] = {6.0F, 6.0F};
  result.Positions[3] = {6.5F, y + 1.5F};
  const float progressRange = (alternate ? 2.0F : 1.0F) * 48.0F;
  const float fullWidth = alternate ? 160.0F : 78.0F;
  result.Sizes[3] = {
      fullWidth * 0.75F *
          (static_cast<float>(magic.current.value) / progressRange),
      3.0F};
  return result;
}

TopScreenTouchClusterGeometry BuildTopScreenTouchClusterGeometry(
    const std::array<float, 4> &nativeVerticalOffsets,
    float nativeAlpha, TopScreenHudLayout layout) noexcept {
  // Payload arrays 0x005D33A0/0x005D33B4 and literals
  // 0x005CAF7C/0x005CAF9C/0x005CAFA0. FUN_005C7230 converts these atlas
  // rectangles to PICA UVs after the producer has supplied top-origin pixels.
  constexpr std::array<TopScreenVec2, 4> kNativeCenters{
      {{358.0F, 20.0F}, {334.0F, 44.0F}, {382.0F, 44.0F}, {358.0F, 68.0F}}};
  TopScreenTouchClusterGeometry result;
  for (std::size_t index = 0; index < kNativeCenters.size(); ++index) {
    result.Positions[index] = {kNativeCenters[index].X - 16.0F,
                               kNativeCenters[index].Y - 16.0F +
                                   nativeVerticalOffsets[index]};
    result.Sizes[index] = {32.0F, 32.0F};
    result.AtlasOrigins[index] = {386.0F, 201.0F};
    result.AtlasSizes[index] = {48.0F, 48.0F};
    result.Alpha[index] =
        layout == TopScreenHudLayout::Restoration && index >= 2U ? 0.0F
                                                                 : nativeAlpha;
  }

  result.Positions[4] =
      layout == TopScreenHudLayout::Restoration
          ? TopScreenVec2{342.0F, 52.0F}
          : TopScreenVec2{232.0F, 8.0F};
  result.Sizes[4] = {32.0F, 32.0F};
  result.AtlasOrigins[4] = {386.0F, 201.0F};
  result.AtlasSizes[4] = {48.0F, 48.0F};
  result.Alpha[4] = 1.0F;

  result.Positions[5] = {14.0F, 52.0F};
  result.Sizes[5] = {30.0F, 30.0F};
  result.AtlasOrigins[5] = {432.0F, 218.0F};
  result.AtlasSizes[5] = {30.0F, 30.0F};
  result.Alpha[5] = nativeAlpha;
  return result;
}

bool ReadTopScreenTouchDynamicState(
    NativeA32Memory &memory, const TopScreenExtendedInputFrame &extendedInput,
    TopScreenTouchDynamicState *state, std::string *error) {
  if (state == nullptr) {
    SetError(error, "TopScreen touch dynamic-state output is null");
    return false;
  }
  *state = {};

  constexpr std::uint32_t kPauseTouchButtonState = 0x0050AF34U;
  constexpr std::array<std::uint32_t, 4> kNativeButtonOffsets{0x4CU, 0x50U,
                                                              0x44U, 0x48U};
  std::array<std::uint32_t, 4> nativeButtons{};
  for (std::size_t index = 0; index < nativeButtons.size(); ++index) {
    if (!memory.Read32(kPauseTouchButtonState + kNativeButtonOffsets[index],
                       &nativeButtons[index])) {
      SetError(error, "cannot read native TopScreen touch-button state");
      return false;
    }
  }
  state->VerticalOffsets = {
      nativeButtons[0] != 0U ? 2.0F : 0.0F,
      nativeButtons[1] != 0U ? 2.0F : 0.0F,
      nativeButtons[2] != 0U || extendedInput.ZrHeld ? 2.0F : 0.0F,
      nativeButtons[3] != 0U || extendedInput.ZlHeld ? 2.0F : 0.0F};

  std::uint32_t renderer = 0U;
  std::uint32_t colorStream = 0U;
  if (!memory.Read32(kPauseTouchButtonState + 4U, &renderer) ||
      renderer == 0U || !memory.Read32(renderer + 0x18U, &colorStream) ||
      colorStream == 0U) {
    SetError(error, "native TopScreen touch color stream is unavailable");
    return false;
  }
  // Quad 34 is one of the native HUD controls copied by the payload and uses
  // the same visibility fade as its newly allocated control groups.
  constexpr std::uint32_t kVisibilitySourceQuad = 34U;
  std::array<float, 16> colors{};
  if (!memory.ReadBytes(colorStream + kVisibilitySourceQuad * 0x40U,
                        std::span<std::uint8_t>(
                            reinterpret_cast<std::uint8_t *>(colors.data()),
                            colors.size() * sizeof(float)))) {
    SetError(error, "cannot read native TopScreen touch visibility color");
    return false;
  }
  state->Alpha = std::clamp(
      (colors[3] + colors[7] + colors[11] + colors[15]) * 0.25F, 0.0F, 1.0F);
  return true;
}

bool ReadTopScreenHudCompositorGate(NativeA32Memory &memory,
                                    TopScreenHudCompositorGate *gate,
                                    std::string *error) {
  if (gate == nullptr) {
    SetError(error, "TopScreen HUD compositor gate output is null");
    return false;
  }
  *gate = {};
  constexpr std::uint32_t kPauseRoot = 0x005043D4U;
  constexpr std::uint32_t kSaveContext = 0x00587958U;
  constexpr std::uint32_t kRuntimeState = 0x00588958U;
  constexpr std::array<std::uint32_t, 4> kCompetingOwnerStates{
      0x0050446CU + 0x18U, 0x005066F8U + 0x34U, 0x00506CB0U + 0x38U,
      0x0050A508U + 0x28U};
  std::uint32_t playState = 0U;
  std::uint8_t stateType = 0U;
  std::uint8_t stateSubtype = 0U;
  std::uint16_t health = 0U;
  std::uint16_t playerTransition = 0U;
  std::uint8_t playerHudSuppressed = 0U;
  std::uint32_t runtimeMode = 0U;
  std::uint32_t alternateHudOwner = 0U;
  if (!memory.Read32(kPauseRoot + 0x0CU, &playState) || playState == 0U ||
      !memory.Read8(playState + 0x100U, &stateType) ||
      !memory.Read8(playState + 0x101U, &stateSubtype) ||
      !memory.Read16(kSaveContext + 0x44U, &health) ||
      !memory.Read16(playState + 0x318CU, &playerTransition) ||
      !memory.Read8(playState + 0x7F40U, &playerHudSuppressed) ||
      !memory.Read32(kRuntimeState + 0x4E4U, &runtimeMode) ||
      !memory.Read32(0x004FC648U + 0x48U, &alternateHudOwner)) {
    SetError(error, "cannot read native TopScreen HUD compositor gate");
    return false;
  }
  std::uint16_t playerHudReady = 0U;
  if (!memory.Read16(playState + 0x2E30U, &playerHudReady)) {
    SetError(error, "cannot read native TopScreen player HUD readiness");
    return false;
  }
  for (const auto address : kCompetingOwnerStates) {
    std::uint32_t active = 0U;
    if (!memory.Read32(address, &active)) {
      SetError(error, "cannot read native TopScreen competing UI owner");
      return false;
    }
    if (active != 0U) {
      return true;
    }
  }
  gate->Draw = stateType == 3U && stateSubtype == 2U && health != 0U &&
               playerTransition == 0U && runtimeMode == 0U &&
               (playerHudReady != 0U || alternateHudOwner != 0U) &&
               playerHudSuppressed == 0U;
  return true;
}

bool ApplyTopScreenNativeTouchQuad28Layout(
    NativeA32Memory &memory, TopScreenNativeTouchQuad28LayoutStats *stats,
    std::string *error) {
  TopScreenNativeTouchQuad28LayoutStats result;
  constexpr std::uint32_t kPauseTouchButtonState = 0x0050AF34U;
  constexpr std::uint32_t kPayloadTargetQuad = 28U;
  constexpr std::uint32_t kPositionStride = 0x30U;
  constexpr std::uint32_t kVertexStride = 0x0CU;
  constexpr std::uint32_t kLayoutAxisOffset = 4U;
  constexpr float kDefaultLowerEdge = 11.0F;
  constexpr float kAlternateLowerEdge = 3.0F;
  constexpr float kExtent = 48.0F;

  std::uint32_t renderer = 0U;
  std::uint32_t touchState = 0U;
  if (!memory.Read32(kPauseTouchButtonState + 4U, &renderer) ||
      !memory.Read32(kPauseTouchButtonState + 0x34U, &touchState)) {
    SetError(error, "cannot read native TopScreen touch-quad owner");
    return false;
  }
  result.RendererAddress = renderer;
  if (renderer == 0U || touchState == 0U) {
    if (stats != nullptr) {
      *stats = result;
    }
    return true;
  }

  std::uint32_t quadCount = 0U;
  std::uint32_t positions = 0U;
  if (!memory.Read32(renderer, &quadCount) ||
      !memory.Read32(renderer + 0x0CU, &positions)) {
    SetError(error, "cannot read native TopScreen touch-quad renderer");
    return false;
  }
  if (quadCount <= 91U || positions == 0U) {
    if (stats != nullptr) {
      *stats = result;
    }
    return true;
  }
  result.Eligible = true;

  float lowerEdge = kDefaultLowerEdge;
  std::uint32_t playState = 0U;
  if (!memory.Read32(kPauseTouchButtonState + 0x14U, &playState)) {
    SetError(error, "cannot read native TopScreen touch-quad PlayState owner");
    return false;
  }
  if (playState != 0U) {
    std::uint16_t playerHudMode = 0U;
    if (!memory.Read16(playState + 0x2DD4U, &playerHudMode)) {
      SetError(error, "cannot read native TopScreen touch-quad HUD mode");
      return false;
    }
    if (playerHudMode != 0U) {
      lowerEdge = kAlternateLowerEdge;
    }
  }
  const float upperEdge = lowerEdge + kExtent;

  const std::uint32_t quadPositions =
      positions + kPayloadTargetQuad * kPositionStride;
  std::array<float, 4> axis{};
  for (std::size_t vertex = 0U; vertex < axis.size(); ++vertex) {
    std::uint32_t bits = 0U;
    if (!memory.Read32(quadPositions +
                           static_cast<std::uint32_t>(vertex) * kVertexStride +
                           kLayoutAxisOffset,
                       &bits)) {
      SetError(error, "cannot read native TopScreen touch-quad vertices");
      return false;
    }
    axis[vertex] = std::bit_cast<float>(bits);
  }
  const auto [minimum, maximum] = std::minmax_element(axis.begin(), axis.end());
  const float center = (*minimum + *maximum) * 0.5F;
  for (std::size_t vertex = 0U; vertex < axis.size(); ++vertex) {
    const float value = axis[vertex] < center ? lowerEdge : upperEdge;
    if (!memory.Write32(quadPositions +
                            static_cast<std::uint32_t>(vertex) * kVertexStride +
                            kLayoutAxisOffset,
                        std::bit_cast<std::uint32_t>(value))) {
      SetError(error, "cannot write native TopScreen touch-quad vertices");
      return false;
    }
  }
  result.Transformed = true;
  result.LowerEdge = lowerEdge;
  result.UpperEdge = upperEdge;
  if (stats != nullptr) {
    *stats = result;
  }
  return true;
}

TopScreenPauseEdgeGeometry BuildTopScreenPauseEdgeGeometry(
    bool hasScene, std::uint8_t sceneMode, std::uint32_t runtimeMode,
    std::uint8_t sceneFade, std::int32_t transitionStep, float nativeVisibility,
    float globalColorScale) noexcept {
  float visibility = nativeVisibility;
  float sceneScale = 1.0F;
  if (hasScene && sceneMode == 3U) {
    if (runtimeMode == 1U || runtimeMode == 2U) {
      visibility = std::min(static_cast<float>(transitionStep) * 0.05F, 1.0F);
    }
    sceneScale =
        std::max(0.0F, 1.0F - static_cast<float>(sceneFade) * (1.0F / 255.0F));
  }

  const float retraction = (1.0F - visibility) * 32.0F;
  TopScreenPauseEdgeGeometry result;
  result.Positions = {{{288.0F + retraction, 0.0F}, {-retraction, 0.0F}}};
  result.Sizes = {{{32.0F, 240.0F}, {32.0F, 240.0F}}};
  result.AtlasOrigins = {{{192.0F, 0.0F}, {256.0F, 0.0F}}};
  result.AtlasSizes = {{{64.0F, 240.0F}, {-64.0F, 240.0F}}};
  const float payloadScale = sceneScale * globalColorScale * visibility;
  result.RgbScale = payloadScale < 1.0F ? payloadScale : 1.0F;
  return result;
}

std::size_t AppendTopScreenHealthPresentation(
    const oot3d::ui::UiHudContentSnapshot &content,
    const oot3d::ui::UiTextureIdentity &pauseTopPage, std::uint8_t pulsePhase,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  using oot3d::ui::HudVisibilityMode;
  if (pauseTopPage.semantic_name.empty() ||
      !content.health.current_units.IsKnown() ||
      !content.health.capacity_units.IsKnown()) {
    return 0U;
  }
  if (content.visibility.current.IsKnown()) {
    const auto visibility = content.visibility.current.value;
    if (visibility == HudVisibilityMode::HUD_VISIBILITY_NOTHING ||
        visibility == HudVisibilityMode::HUD_VISIBILITY_NOTHING_ALT ||
        visibility == HudVisibilityMode::HUD_VISIBILITY_NOTHING_INSTANT) {
      return 0U;
    }
  }

  const auto health = static_cast<std::uint16_t>(
      std::max<std::int32_t>(0, content.health.current_units.value));
  const bool alternateAtlasRow =
      content.health.double_defense_acquired.IsKnown() &&
      content.health.double_defense_acquired.value;
  const auto geometry =
      BuildTopScreenHealthGeometry(health, content.health.capacity_units.value,
                                   alternateAtlasRow, pulsePhase);
  const std::size_t startSize = output.size();
  const std::size_t visibleHearts = std::min<std::size_t>(
      geometry.Positions.size(),
      (static_cast<std::size_t>(content.health.capacity_units.value) + 15U) /
          16U);
  for (std::size_t index = 0; index < visibleHearts; ++index) {
    const float expansion =
        index == geometry.PulseHeart ? geometry.PulseExpansion : 0.0F;
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::Heart;
    primitive.source_quad = static_cast<std::uint32_t>(index + 4U);
    primitive.texture = pauseTopPage;
    primitive.destination = {geometry.Positions[index].X - expansion,
                             geometry.Positions[index].Y - expansion,
                             geometry.Sizes[index].X + expansion * 2.0F,
                             geometry.Sizes[index].Y + expansion * 2.0F};
    primitive.uv = {geometry.AtlasOrigins[index].X / 512.0F,
                    geometry.AtlasOrigins[index].Y / 256.0F,
                    geometry.AtlasSizes[index].X / 512.0F,
                    geometry.AtlasSizes[index].Y / 256.0F};
    primitive.layer = 10U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

std::size_t AppendTopScreenMagicMeterPresentation(
    const TopScreenMagicMeterGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (!geometry.Visible || pauseTopPage.semantic_name.empty()) {
    return 0U;
  }
  const std::size_t startSize = output.size();
  for (std::size_t index = 0; index < geometry.Positions.size(); ++index) {
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = index == 3U ? oot3d::ui::UiPrimitiveRole::MagicFill
                                 : oot3d::ui::UiPrimitiveRole::MagicFrame;
    primitive.source_quad = static_cast<std::uint32_t>(index);
    primitive.texture = pauseTopPage;
    primitive.destination = {geometry.Positions[index].X,
                             geometry.Positions[index].Y,
                             geometry.Sizes[index].X, geometry.Sizes[index].Y};
    primitive.uv = {geometry.AtlasOrigins[index].X / 512.0F,
                    geometry.AtlasOrigins[index].Y / 256.0F,
                    geometry.AtlasSizes[index].X / 512.0F,
                    geometry.AtlasSizes[index].Y / 256.0F};
    primitive.layer = 11U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

std::size_t AppendTopScreenTouchClusterPresentation(
    const TopScreenTouchClusterGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (pauseTopPage.semantic_name.empty()) {
    return 0U;
  }
  const std::size_t startSize = output.size();
  for (std::size_t index = 0; index < geometry.Positions.size(); ++index) {
    const float alpha = std::clamp(geometry.Alpha[index], 0.0F, 1.0F);
    if (alpha == 0.0F) {
      continue;
    }
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::TouchControl;
    primitive.owner_address = 0x005C9940U;
    primitive.source_quad = static_cast<std::uint32_t>(index);
    primitive.texture = pauseTopPage;
    primitive.destination = {geometry.Positions[index].X,
                             geometry.Positions[index].Y,
                             geometry.Sizes[index].X, geometry.Sizes[index].Y};
    // The host decoder already normalizes the PICA storage orientation.
    primitive.uv = {geometry.AtlasOrigins[index].X / 512.0F,
                    geometry.AtlasOrigins[index].Y / 256.0F,
                    geometry.AtlasSizes[index].X / 512.0F,
                    geometry.AtlasSizes[index].Y / 256.0F};
    primitive.color.alpha = alpha;
    primitive.layer = 12U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

std::size_t AppendTopScreenPauseEdgePresentation(
    const TopScreenPauseEdgeGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &itemPage,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (itemPage.semantic_name.empty())
    return 0U;
  const std::size_t startSize = output.size();
  for (std::size_t index = 0; index < geometry.Positions.size(); ++index) {
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::PauseBackground;
    primitive.owner_address = 0x005CD1E0U;
    primitive.source_quad = static_cast<std::uint32_t>(index);
    primitive.texture = itemPage;
    // Payload 0x005CD1E0 authors these edge quads in the promoted lower-screen
    // 320x240 domain. The native top target expands that domain to 400x240.
    // Host-output fitting belongs to the renderer presentation contract.
    primitive.destination = {
        geometry.Positions[index].X * kPromotedPauseHorizontalScale,
        geometry.Positions[index].Y,
        geometry.Sizes[index].X * kPromotedPauseHorizontalScale,
        geometry.Sizes[index].Y};
    // UiTextureIdentity payloads have already been decoded into top-down host
    // rows. FUN_005C7230's PICA V inversion belongs to the guest texture
    // upload boundary and must not be repeated by the source presenter.
    primitive.uv = {geometry.AtlasOrigins[index].X / 256.0F,
                    geometry.AtlasOrigins[index].Y / 256.0F,
                    geometry.AtlasSizes[index].X / 256.0F,
                    geometry.AtlasSizes[index].Y / 256.0F};
    primitive.color.red = geometry.RgbScale;
    primitive.color.green = geometry.RgbScale;
    primitive.color.blue = geometry.RgbScale;
    primitive.layer = 14U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

TopScreenFileSelectStripGeometry
BuildTopScreenFileSelectStripGeometry(bool active) noexcept {
  TopScreenFileSelectStripGeometry geometry;
  geometry.Active = active;
  geometry.Quad.Visible = active;
  geometry.Quad.Position = {66.0F, 212.0F};
  geometry.Quad.Size = {252.0F, 28.0F};
  geometry.Quad.AtlasOrigin = {0.0F, 152.0F};
  geometry.Quad.AtlasSize = {289.0F, 46.0F};
  geometry.Quad.Color = {0.74F, 0.74F, 0.74F, 1.0F};
  return geometry;
}

bool ReadTopScreenFileSelectStripActive(NativeA32Memory &memory, bool *active,
                                        std::string *error) {
  if (active == nullptr) {
    SetError(error, "TopScreen file-select strip output is null");
    return false;
  }
  *active = false;
  constexpr std::uint32_t kPauseRoot = 0x005043D4U;
  constexpr std::uint32_t kRuntimeState = 0x00588958U;
  constexpr std::uint32_t kDisplayState = 0x005C3F00U;
  std::uint32_t scene = 0U;
  std::uint32_t runtimeMode = 0U;
  std::uint8_t displayReadyA = 0U;
  std::uint8_t displayReadyB = 0U;
  if (!memory.Read32(kPauseRoot + 0x0CU, &scene) ||
      !memory.Read32(kRuntimeState + 0x4E4U, &runtimeMode) ||
      !memory.Read8(kDisplayState + 0x61U, &displayReadyA) ||
      !memory.Read8(kDisplayState + 0x62U, &displayReadyB)) {
    SetError(error, "cannot read native file-select strip gate");
    return false;
  }
  *active = scene != 0U && runtimeMode == 4U && displayReadyA != 0U &&
            displayReadyB != 0U;
  return true;
}

std::size_t AppendTopScreenFileSelectStripPresentation(
    const TopScreenFileSelectStripGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &fileSelectAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (!geometry.Active || !geometry.Quad.Visible ||
      fileSelectAtlas.semantic_name.empty()) {
    return 0U;
  }
  const auto &quad = geometry.Quad;
  oot3d::ui::UiPrimitive primitive;
  primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
  primitive.role = oot3d::ui::UiPrimitiveRole::PauseText;
  primitive.owner_address = 0x005CD64CU;
  primitive.texture = fileSelectAtlas;
  primitive.destination = {quad.Position.X, quad.Position.Y, quad.Size.X,
                           quad.Size.Y};
  primitive.uv = {quad.AtlasOrigin.X / 512.0F,
                  1.0F - quad.AtlasOrigin.Y / 256.0F, quad.AtlasSize.X / 512.0F,
                  -quad.AtlasSize.Y / 256.0F};
  primitive.color = quad.Color;
  primitive.layer = 22U;
  output.push_back(std::move(primitive));
  return 1U;
}

TopScreenExtendedItemButtonsGeometry
BuildTopScreenExtendedItemButtonsGeometry(bool itemIActive,
                                          bool itemIIActive) noexcept {
  TopScreenExtendedItemButtonsGeometry result;
  result.Quads[0] = {itemIActive,      {2.0F, 60.0F},  {14.0F, 14.0F},
                     {378.0F, 210.0F}, {42.0F, 42.0F}, {}};
  result.Quads[1] = {itemIIActive,     {44.0F, 60.0F}, {14.0F, 14.0F},
                     {420.0F, 210.0F}, {42.0F, 42.0F}, {}};
  return result;
}

std::size_t AppendTopScreenExtendedItemButtonsPresentation(
    const TopScreenExtendedItemButtonsGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &itemIcons,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (itemIcons.semantic_name.empty()) {
    return 0U;
  }
  const std::size_t startSize = output.size();
  for (std::size_t index = 0; index < geometry.Quads.size(); ++index) {
    const auto &quad = geometry.Quads[index];
    if (!quad.Visible) {
      continue;
    }
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::ActionButton;
    primitive.owner_address = 0x005C9940U;
    primitive.source_quad = static_cast<std::uint32_t>(index + 5U);
    primitive.texture = itemIcons;
    primitive.destination = {quad.Position.X, quad.Position.Y, quad.Size.X,
                             quad.Size.Y};
    primitive.uv = {quad.AtlasOrigin.X / 512.0F, quad.AtlasOrigin.Y / 256.0F,
                    quad.AtlasSize.X / 512.0F, quad.AtlasSize.Y / 256.0F};
    primitive.color = quad.Color;
    primitive.layer = 13U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

bool AppendTopScreenNativeItemIconCopies(
    NativeA32Memory &memory, const std::array<float, 4> &nativeVerticalOffsets,
    float nativeAlpha, const oot3d::ui::UiTextureIdentity &itemIcons,
    std::vector<oot3d::ui::UiPrimitive> &output, std::string *error,
    bool renderDpadIcons) {
  if (itemIcons.semantic_name.empty()) {
    SetError(error, "native TopScreen item-icon texture is unavailable");
    return false;
  }
  // FUN_005c9940 loads the active PlayState through
  // gPauseTouchButtonState+0x14, not gPauseUiRootState.  Keeping this owner
  // relationship is required because the two globals do not alias during
  // gameplay.
  constexpr std::uint32_t kPauseTouchButtonState = 0x0050AF34U;
  std::uint32_t playState = 0U;
  std::uint32_t renderer = 0U;
  if (!memory.Read32(kPauseTouchButtonState + 0x14U, &playState) ||
      playState == 0U || !memory.Read32(playState + 0x10CU, &renderer) ||
      renderer == 0U) {
    SetError(error, "native TopScreen PlayState item renderer is unavailable");
    return false;
  }
  std::array<std::uint32_t, 4> streams{};
  constexpr std::array<std::uint32_t, 4> kStreamOffsets{0x0CU, 0x14U, 0x18U,
                                                        0x1CU};
  for (std::size_t index = 0; index < streams.size(); ++index) {
    if (!memory.Read32(renderer + kStreamOffsets[index], &streams[index]) ||
        streams[index] == 0U) {
      SetError(error, "native TopScreen item geometry stream is unavailable");
      return false;
    }
  }
  const auto readFloats = [&memory](std::uint32_t address, auto &values) {
    return memory.ReadBytes(
        address,
        std::span<std::uint8_t>(reinterpret_cast<std::uint8_t *>(values.data()),
                                values.size() * sizeof(float)));
  };
  const float alphaScale = std::clamp(nativeAlpha, 0.0F, 1.0F);
  for (std::uint32_t source = 0U; source < 5U; ++source) {
    std::array<float, 12> positions{};
    std::array<float, 8> uvs{};
    std::array<float, 16> colors{};
    std::array<float, 2> translation{};
    if (!readFloats(streams[0] + source * 0x30U, positions) ||
        !readFloats(streams[1] + source * 0x20U, uvs) ||
        !readFloats(streams[2] + source * 0x40U, colors) ||
        !readFloats(streams[3] + source * 0x08U, translation)) {
      SetError(error, "cannot read a native TopScreen item source quad");
      return false;
    }
    const float centerX = translation[0] + (positions[0] + positions[9]) * 0.5F;
    const float centerY =
        translation[1] + (positions[1] + positions[10]) * 0.5F;
    const NativeItemIconRegion *region = nullptr;
    std::size_t regionIndex = 0U;
    for (; regionIndex < kNativeItemIconRegions.size(); ++regionIndex) {
      const auto &candidate = kNativeItemIconRegions[regionIndex];
      if (centerX >= candidate.MinimumX && centerX <= candidate.MaximumX &&
          centerY >= candidate.MinimumY && centerY <= candidate.MaximumY) {
        region = &candidate;
        break;
      }
    }
    // Region four is the native item/action lane relocated around the D-pad.
    // The remaining regions are face-button items and stay visible when the
    // independent 2.1.1 D-pad presentation option is disabled.
    if (!renderDpadIcons && regionIndex == 4U) {
      continue;
    }

    const float sourceCenterX =
        region != nullptr ? static_cast<float>(region->SourceCenterX) : centerX;
    const float sourceCenterY =
        region != nullptr ? static_cast<float>(region->SourceCenterY) : centerY;
    const float destinationCenterX =
        region != nullptr ? static_cast<float>(region->DestinationCenterX)
                          : 500.0F;
    const float destinationY =
        (region != nullptr ? static_cast<float>(region->DestinationCenterY)
                           : 300.0F) +
        (regionIndex < nativeVerticalOffsets.size()
             ? nativeVerticalOffsets[regionIndex]
             : 0.0F);
    const float scale = region != nullptr ? region->Scale : 1.0F;
    std::array<TopScreenVec2, 4> transformed{};
    for (std::size_t vertex = 0; vertex < transformed.size(); ++vertex) {
      transformed[vertex] = {
          destinationCenterX +
              (positions[vertex * 3U] + translation[0] - sourceCenterX) * scale,
          destinationY +
              (positions[vertex * 3U + 1U] + translation[1] - sourceCenterY) *
                  scale};
    }
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::ActionButton;
    primitive.owner_address = 0x005C9940U;
    primitive.descriptor_address = renderer;
    primitive.source_quad = source;
    primitive.texture = itemIcons;
    primitive.destination = {transformed[0].X, transformed[0].Y,
                             transformed[1].X - transformed[0].X,
                             transformed[2].Y - transformed[0].Y};
    primitive.uv = NativePicaQuadUvToHost(uvs);
    primitive.color = {colors[0], colors[1], colors[2], colors[3] * alphaScale};
    primitive.layer = 13U;
    primitive.visible = primitive.color.alpha != 0.0F;
    output.push_back(std::move(primitive));
  }
  return true;
}

bool AppendTopScreenNativeCounters(
    NativeA32Memory &memory, const std::array<float, 4> &nativeVerticalOffsets,
    const oot3d::ui::UiTextureIdentity &numberGlyphs,
    std::vector<oot3d::ui::UiPrimitive> &output, std::string *error,
    const TopScreenUiConfig *config,
    const TopScreenExtendedInputFrame *input) {
  if (numberGlyphs.semantic_name.empty()) {
    SetError(error, "native TopScreen number-glyph texture is unavailable");
    return false;
  }
  constexpr std::uint32_t kPauseTouchButtonState = 0x0050AF34U;
  constexpr std::uint32_t kSaveContext = 0x00587958U;
  std::uint16_t rupees = 0U;
  if (!memory.Read16(kSaveContext + 0x48U, &rupees)) {
    SetError(error, "cannot read native TopScreen rupee count");
    return false;
  }
  (void)AppendTopScreenCounterDigits(0x005C9940U, kSaveContext + 0x48U, rupees,
                                     4U, 24.0F, 218.0F, 0.85F, numberGlyphs,
                                     output);

  std::uint32_t playState = 0U;
  if (!memory.Read32(kPauseTouchButtonState + 0x14U, &playState)) {
    SetError(error, "cannot read native TopScreen PlayState");
    return false;
  }
  if (playState == 0U) {
    return true;
  }

  std::uint8_t stateType = 0U;
  std::uint8_t stateSubtype = 0U;
  std::uint16_t sceneId = 0U;
  std::uint16_t mapIndex = 0U;
  std::uint8_t keyCountRaw = 0U;
  if (!memory.Read8(playState + 0x100U, &stateType) ||
      !memory.Read8(playState + 0x101U, &stateSubtype) ||
      !memory.Read16(playState + 0x104U, &sceneId) ||
      !memory.Read16(kSaveContext + 0x1592U, &mapIndex) ||
      !memory.Read8(kSaveContext + 0xD4U + mapIndex, &keyCountRaw)) {
    SetError(error, "cannot read native TopScreen dungeon-key gate");
    return false;
  }
  const auto keyCount = static_cast<std::int8_t>(keyCountRaw);
  if (stateType == 3U && stateSubtype == 2U && sceneId >= 3U &&
      sceneId <= 16U && keyCount >= 0) {
    (void)AppendTopScreenCounterDigits(
        0x005C9940U, kSaveContext + 0xD4U + mapIndex,
        static_cast<std::uint32_t>(keyCount), 4U, 22.0F, 198.0F, 0.85F,
        numberGlyphs, output);
  }

  constexpr std::array<float, 4> kAmmoX{382.0F, 358.0F, 358.0F, 334.0F};
  constexpr std::array<float, 4> kAmmoY{44.0F, 68.0F, 20.0F, 44.0F};
  constexpr std::array<std::uint8_t, 4> kOffsetLane{2U, 3U, 0U, 1U};
  std::uint32_t activeSlots = 0U;
  if (!memory.Read32(playState + 0x224U, &activeSlots)) {
    SetError(error, "cannot read native TopScreen ammo slot count");
    return false;
  }
  for (std::uint32_t index = 0U; index < 4U; ++index) {
    if (config != nullptr &&
        config->HudLayout == TopScreenHudLayout::Restoration) {
      const bool zrHeld = input != nullptr && input->ZrHeld;
      if ((zrHeld && index >= 2U) || (!zrHeld && index < 2U)) {
        continue;
      }
    }
    std::uint8_t visible = 0U;
    std::uint32_t valueRaw = 0U;
    if (!memory.Read8(playState + 0x435U + index, &visible) ||
        !memory.Read32(playState + 0x32CU + index * 4U, &valueRaw)) {
      SetError(error, "cannot read native TopScreen ammo counter state");
      return false;
    }
    const auto value = static_cast<std::int32_t>(valueRaw);
    if (index + 1U > activeSlots || visible == 0U || value < 0) {
      continue;
    }

    std::uint32_t sourceCounter = 0U;
    if (!memory.Read32(playState + 0x10U + index * 4U, &sourceCounter)) {
      SetError(error, "cannot read native TopScreen source counter");
      return false;
    }
    bool copiedSpecialGeometry = false;
    if (sourceCounter != 0U) {
      std::uint32_t type = 0U;
      std::uint32_t digitCount = 0U;
      std::uint32_t geometry = 0U;
      if (!memory.Read32(sourceCounter, &type) ||
          !memory.Read32(sourceCounter + 4U, &digitCount) ||
          !memory.Read32(sourceCounter + 8U, &geometry)) {
        SetError(error, "cannot read native TopScreen source-counter owner");
        return false;
      }
      if (type != 8U && digitCount != 0U && geometry != 0U) {
        std::array<std::uint32_t, 4> streams{};
        constexpr std::array<std::uint32_t, 4> kStreamOffsets{0x0CU, 0x14U,
                                                              0x18U, 0x1CU};
        bool streamsAvailable = true;
        for (std::size_t stream = 0; stream < streams.size(); ++stream) {
          streamsAvailable &= memory.Read32(geometry + kStreamOffsets[stream],
                                            &streams[stream]) &&
                              streams[stream] != 0U;
        }
        if (streamsAvailable) {
          const std::uint32_t copiedDigits = std::min(digitCount, 2U);
          for (std::uint32_t digit = 0U; digit < copiedDigits; ++digit) {
            std::array<float, 12> positions{};
            std::array<float, 8> uvs{};
            std::array<float, 16> colors{};
            std::array<float, 2> translation{};
            const auto readFloats = [&memory](std::uint32_t address,
                                              auto &values) {
              return memory.ReadBytes(
                  address, std::span<std::uint8_t>(
                               reinterpret_cast<std::uint8_t *>(values.data()),
                               values.size() * sizeof(float)));
            };
            if (!readFloats(streams[0] + digit * 0x30U, positions) ||
                !readFloats(streams[1] + digit * 0x20U, uvs) ||
                !readFloats(streams[2] + digit * 0x40U, colors) ||
                !readFloats(streams[3] + digit * 0x08U, translation)) {
              SetError(error,
                       "cannot read native TopScreen special counter quad");
              return false;
            }
            oot3d::ui::UiPrimitive primitive;
            primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
            primitive.role = oot3d::ui::UiPrimitiveRole::AmmoCounter;
            primitive.owner_address = 0x005C9940U;
            primitive.descriptor_address = sourceCounter;
            primitive.source_quad = digit;
            primitive.texture = numberGlyphs;
            primitive.destination = {
                positions[0] + translation[0], positions[1] + translation[1],
                positions[3] - positions[0], positions[7] - positions[1]};
            primitive.uv = NativePicaQuadUvToHost(uvs);
            primitive.color = {colors[0], colors[1], colors[2], colors[3]};
            primitive.layer = 14U;
            primitive.visible = primitive.color.alpha != 0.0F;
            output.push_back(std::move(primitive));
          }
          copiedSpecialGeometry = true;
        }
      }
    }
    if (!copiedSpecialGeometry) {
      const auto offsetLane = kOffsetLane[index];
      (void)AppendTopScreenCounterDigits(
          0x005C9940U, playState + 0x32CU + index * 4U,
          static_cast<std::uint32_t>(value), 0U, kAmmoX[index],
          kAmmoY[index] + 3.0F + nativeVerticalOffsets[offsetLane], 0.7F,
          numberGlyphs, output);
    }
  }
  return true;
}

TopScreenTouchLabelsGeometry BuildTopScreenTouchLabelsGeometry(
    const std::array<float, 4> &nativeVerticalOffsets,
    float nativeAlpha, TopScreenHudLayout layout) noexcept {
  const float alpha =
      layout == TopScreenHudLayout::Restoration
          ? 0.0F
          : std::clamp(nativeAlpha * 0.9F, 0.0F, 1.0F);
  TopScreenTouchLabelsGeometry result;
  result.Quads[0] = {alpha != 0.0F,  {362.0F, nativeVerticalOffsets[2] + 33.0F},
                     {17.0F, 11.0F}, {440.0F, 190.0F},
                     {17.0F, 11.0F}, {1.0F, 1.0F, 1.0F, alpha}};
  result.Quads[1] = {alpha != 0.0F,  {338.0F, nativeVerticalOffsets[3] + 57.0F},
                     {17.0F, 11.0F}, {440.0F, 201.0F},
                     {17.0F, 11.0F}, {1.0F, 1.0F, 1.0F, alpha}};
  return result;
}

std::size_t AppendTopScreenTouchLabelsPresentation(
    const TopScreenTouchLabelsGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (pauseTopPage.semantic_name.empty()) {
    return 0U;
  }
  const std::size_t startSize = output.size();
  for (std::size_t index = 0; index < geometry.Quads.size(); ++index) {
    const auto &quad = geometry.Quads[index];
    if (!quad.Visible) {
      continue;
    }
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::TouchControl;
    primitive.owner_address = 0x005C9940U;
    primitive.source_quad = static_cast<std::uint32_t>(index);
    primitive.texture = pauseTopPage;
    primitive.destination = {quad.Position.X, quad.Position.Y, quad.Size.X,
                             quad.Size.Y};
    primitive.uv = {quad.AtlasOrigin.X / 512.0F, quad.AtlasOrigin.Y / 256.0F,
                    quad.AtlasSize.X / 512.0F, quad.AtlasSize.Y / 256.0F};
    primitive.color = quad.Color;
    primitive.layer = 13U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

bool ReadTopScreenAuxiliaryTouchInputs(NativeA32Memory &memory,
                                       TopScreenAuxiliaryTouchInputs *inputs,
                                       std::string *error) {
  if (inputs == nullptr) {
    SetError(error, "TopScreen auxiliary-touch input output is null");
    return false;
  }
  *inputs = {};

  constexpr std::uint32_t kPauseRoot = 0x005043D4U;
  constexpr std::uint32_t kPauseTouchButtonState = 0x0050AF34U;
  constexpr std::uint32_t kAlternateHudOwner = 0x004FC648U;
  std::uint32_t playState = 0U;
  std::uint32_t alternateRenderer = 0U;
  if (!memory.Read32(kPauseRoot + 0x0CU, &playState) ||
      !memory.Read32(kPauseTouchButtonState + 0x34U, &inputs->PauseState) ||
      !memory.Read32(kPauseTouchButtonState + 0x70U,
                     &inputs->TouchLayoutState) ||
      !memory.Read32(kAlternateHudOwner + 0x40U, &alternateRenderer)) {
    SetError(error, "cannot read native TopScreen auxiliary-touch state");
    return false;
  }
  inputs->AlternateHudRendererActive = alternateRenderer != 0U;
  if (playState == 0U) {
    return true;
  }

  inputs->HasPlayState = true;
  if (!memory.Read8(playState + 0x100U, &inputs->StateType) ||
      !memory.Read8(playState + 0x101U, &inputs->StateSubtype)) {
    SetError(error, "cannot read native TopScreen auxiliary PlayState mode");
    return false;
  }
  if (inputs->StateType == 3U) {
    std::uint32_t sceneOwner = 0U;
    if (!memory.Read32(playState + 0x20ACU, &sceneOwner)) {
      SetError(error, "cannot read native TopScreen nested scene owner");
      return false;
    }
    if (sceneOwner != 0U) {
      std::uint32_t nestedOwner = 0U;
      if (!memory.Read32(sceneOwner + 0x12B8U, &nestedOwner)) {
        SetError(error, "cannot read native TopScreen nested HUD owner");
        return false;
      }
      inputs->NestedSceneOwnerActive = nestedOwner != 0U;
    }
  }
  if (inputs->StateType == 3U && inputs->StateSubtype == 2U) {
    std::uint16_t playerHudMode = 0U;
    if (!memory.Read16(playState + 0x2DD4U, &playerHudMode)) {
      SetError(error, "cannot read native TopScreen player HUD mode");
      return false;
    }
    inputs->PlayerHudMode = static_cast<std::int16_t>(playerHudMode);
  }
  return true;
}

TopScreenAuxiliaryTouchGeometry BuildTopScreenAuxiliaryTouchGeometry(
    const TopScreenAuxiliaryTouchInputs &inputs,
    const std::array<float, 4> &nativeVerticalOffsets,
    float nativeAlpha, TopScreenHudLayout layout) noexcept {
  constexpr std::array<float, 4> kCentersX{358.0F, 334.0F, 382.0F, 358.0F};
  constexpr std::array<float, 4> kCentersY{20.0F, 44.0F, 44.0F, 68.0F};
  constexpr std::array<float, 4> kAtlasY{142.0F, 158.0F, 126.0F, 190.0F};
  const float auxiliaryAlpha = std::clamp(nativeAlpha * 0.9F, 0.0F, 1.0F);
  const bool conditionalVisible =
      inputs.AlternateHudRendererActive &&
      (inputs.NestedSceneOwnerActive || inputs.PauseState == 11U);

  TopScreenAuxiliaryTouchGeometry result;
  for (std::size_t index = 0U; index < 4U; ++index) {
    auto &quad = result.Quads[index];
    quad.Visible = auxiliaryAlpha != 0.0F &&
                   (index < 2U || (index == 2U && conditionalVisible));
    quad.Position = {500.0F, 300.0F};
    quad.Size = {16.0F, 16.0F};
    quad.AtlasOrigin = {158.0F, kAtlasY[index]};
    quad.AtlasSize = {16.0F, 16.0F};
    quad.Color = {1.0F, 1.0F, 1.0F, auxiliaryAlpha};
    if (index < 2U) {
      quad.Position = {kCentersX[index] - 20.0F,
                       kCentersY[index] - 14.0F + nativeVerticalOffsets[index]};
    } else if (index == 2U && conditionalVisible) {
      quad.Position =
          layout == TopScreenHudLayout::Restoration
              ? TopScreenVec2{337.0F, 54.0F}
              : TopScreenVec2{227.0F, 10.0F};
      quad.AtlasOrigin.Y = 174.0F;
    }
  }

  auto &layoutQuad = result.Quads[4];
  layoutQuad.Visible = nativeAlpha > 0.0F;
  layoutQuad.Color = {1.0F, 1.0F, 1.0F, std::clamp(nativeAlpha, 0.0F, 1.0F)};
  if (inputs.TouchLayoutState == 1U) {
    layoutQuad.Position = {21.0F, 43.0F};
    layoutQuad.Size = {20.0F, 10.0F};
    layoutQuad.AtlasOrigin = {176.0F, 56.0F};
    layoutQuad.AtlasSize = {48.0F, 24.0F};
  } else if (inputs.HasPlayState && inputs.StateType == 3U &&
             inputs.StateSubtype == 2U && inputs.PlayerHudMode != 0) {
    layoutQuad.Position = {22.0F, 35.0F};
    layoutQuad.Size = {16.0F, 16.0F};
    layoutQuad.AtlasOrigin = {176.0F, 0.0F};
    layoutQuad.AtlasSize = {48.0F, 48.0F};
  } else {
    layoutQuad.Position = {23.0F, 40.0F};
    layoutQuad.Size = {16.0F, 10.0F};
    layoutQuad.AtlasOrigin = {128.0F, 0.0F};
    layoutQuad.AtlasSize = {48.0F, 30.0F};
  }
  return result;
}

std::size_t AppendTopScreenAuxiliaryTouchPresentation(
    const TopScreenAuxiliaryTouchGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (pauseTopPage.semantic_name.empty()) {
    return 0U;
  }
  const std::size_t startSize = output.size();
  for (std::size_t index = 0U; index < geometry.Quads.size(); ++index) {
    const auto &quad = geometry.Quads[index];
    if (!quad.Visible || quad.Color.alpha == 0.0F) {
      continue;
    }
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::TouchControl;
    primitive.owner_address = 0x005C9940U;
    primitive.source_quad = static_cast<std::uint32_t>(34U + index);
    primitive.texture = pauseTopPage;
    primitive.destination = {quad.Position.X, quad.Position.Y, quad.Size.X,
                             quad.Size.Y};
    primitive.uv = {quad.AtlasOrigin.X / 512.0F, quad.AtlasOrigin.Y / 256.0F,
                    quad.AtlasSize.X / 512.0F, quad.AtlasSize.Y / 256.0F};
    primitive.color = quad.Color;
    primitive.layer = 13U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

bool AppendTopScreenNativeTouchCopies(
    NativeA32Memory &memory, const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output,
    TopScreenNativeTouchCopyStats *stats, std::string *error,
    const TopScreenUiConfig *config) {
  TopScreenNativeTouchCopyStats result;
  constexpr std::uint32_t kPauseTouchButtonState = 0x0050AF34U;
  std::uint32_t renderer = 0U;
  std::array<std::uint32_t, 4> streams{};
  if (!memory.Read32(kPauseTouchButtonState + 4U, &renderer) ||
      renderer == 0U) {
    SetError(error, "native PauseTouchButton renderer is unavailable");
    return false;
  }
  constexpr std::array<std::uint32_t, 4> kStreamOffsets{0x0CU, 0x14U, 0x18U,
                                                        0x1CU};
  for (std::size_t index = 0; index < streams.size(); ++index) {
    if (!memory.Read32(renderer + kStreamOffsets[index], &streams[index]) ||
        streams[index] == 0U) {
      SetError(error, "native PauseTouchButton geometry stream is unavailable");
      return false;
    }
  }

  const auto readFloats = [&memory](std::uint32_t address, auto &values) {
    return memory.ReadBytes(
        address,
        std::span<std::uint8_t>(reinterpret_cast<std::uint8_t *>(values.data()),
                                values.size() * sizeof(float)));
  };
  for (std::size_t contractIndex = 0U;
       contractIndex < kNativeTouchCopyContracts.size(); ++contractIndex) {
    const auto &contract = kNativeTouchCopyContracts[contractIndex];
    const std::uint32_t source = contract.SourceQuad;
    std::array<float, 12> positions{};
    std::array<float, 8> uvs{};
    std::array<float, 16> colors{};
    std::array<float, 2> translation{};
    if (!readFloats(streams[0] + source * 0x30U, positions) ||
        !readFloats(streams[1] + source * 0x20U, uvs) ||
        !readFloats(streams[2] + source * 0x40U, colors) ||
        !readFloats(streams[3] + source * 0x08U, translation)) {
      SetError(error, "cannot read a native PauseTouchButton source quad");
      return false;
    }
    ++result.SourceQuadsRead;
    const bool horseStamina = source >= 86U && source <= 91U;
    if (horseStamina) {
      ++result.HorseStaminaQuadsRead;
    }

    std::array<TopScreenVec2, 4> transformed{};
    const bool restoration =
        config != nullptr &&
        config->HudLayout == TopScreenHudLayout::Restoration;
    const float destinationX =
        restoration && contractIndex < 2U
            ? 339.0F
            : static_cast<float>(contract.DestinationX);
    const float destinationY =
        restoration && contractIndex < 2U
            ? 55.0F
            : static_cast<float>(contract.DestinationY);
    for (std::size_t vertex = 0; vertex < transformed.size(); ++vertex) {
      transformed[vertex] = {
          destinationX +
              ((positions[vertex * 3U] + translation[0]) -
               static_cast<float>(contract.SourceCenterX)) *
                  contract.Scale,
          destinationY +
              ((positions[vertex * 3U + 1U] + translation[1]) -
               static_cast<float>(contract.SourceCenterY)) *
                  contract.Scale};
    }
    if (source >= 86U) {
      uvs[5] += 1.0F / 256.0F;
      uvs[7] += 1.0F / 256.0F;
    }

    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = horseStamina
                         ? oot3d::ui::UiPrimitiveRole::HorseStamina
                         : oot3d::ui::UiPrimitiveRole::TouchControl;
    primitive.owner_address = 0x005C9940U;
    primitive.descriptor_address = renderer;
    primitive.source_quad = source;
    primitive.texture = pauseTopPage;
    primitive.destination = {transformed[0].X, transformed[0].Y,
                             transformed[1].X - transformed[0].X,
                             transformed[2].Y - transformed[0].Y};
    primitive.uv = NativePicaQuadUvToHost(uvs);
    primitive.color = {colors[0], colors[1], colors[2], colors[3]};
    primitive.layer = 13U;
    primitive.visible = primitive.color.alpha != 0.0F;
    if (primitive.visible) {
      ++result.AlphaVisiblePrimitives;
      if (horseStamina) {
        ++result.AlphaVisibleHorseStaminaPrimitives;
      }
    }
    output.push_back(std::move(primitive));
    ++result.PrimitivesEmitted;
  }
  if (stats != nullptr) {
    *stats = result;
  }
  return true;
}

void ApplyTopScreenGameplayCanvas(
    std::span<oot3d::ui::UiPrimitive> primitives) noexcept {
  const auto clipAxis = [](float minimum, float maximum, float *position,
                           float *extent, float *uvPosition,
                           float *uvExtent) noexcept {
    const float originalPosition = *position;
    const float originalExtent = *extent;
    if (originalExtent <= 0.0F) {
      return false;
    }
    const float clippedStart = std::max(originalPosition, minimum);
    const float clippedEnd =
        std::min(originalPosition + originalExtent, maximum);
    if (clippedEnd <= clippedStart) {
      return false;
    }
    const float startRatio = (clippedStart - originalPosition) / originalExtent;
    const float clippedRatio = (clippedEnd - clippedStart) / originalExtent;
    *position = clippedStart;
    *extent = clippedEnd - clippedStart;
    *uvPosition += *uvExtent * startRatio;
    *uvExtent *= clippedRatio;
    return true;
  };

  for (auto &primitive : primitives) {
    if (!primitive.visible ||
        !clipAxis(0.0F, kNativeTopScreenWidth, &primitive.destination.x,
                  &primitive.destination.width, &primitive.uv.x,
                  &primitive.uv.width) ||
        !clipAxis(0.0F, kNativeTopScreenHeight, &primitive.destination.y,
                  &primitive.destination.height, &primitive.uv.y,
                  &primitive.uv.height)) {
      primitive.visible = false;
      continue;
    }
  }
}

void ApplyTopScreenHudScale(
    std::span<oot3d::ui::UiPrimitive> primitives,
    const TopScreenUiConfig &config) noexcept {
  const float scale = config.HudScale;
  const float marginX = static_cast<float>(config.HudMarginX);
  const float marginY = static_cast<float>(config.HudMarginY);
  if (scale == 1.0F && marginX == 0.0F && marginY == 0.0F &&
      config.MagicBarY == 0U) {
    return;
  }

  for (auto &primitive : primitives) {
    auto &destination = primitive.destination;
    const float centerX = destination.x + destination.width * 0.5F;
    const float centerY = destination.y + destination.height * 0.5F;
    if (centerX > 430.0F || centerY > 280.0F) {
      continue;
    }

    const bool left = centerX < 200.0F;
    const bool top = centerY < 120.0F;
    const float pivotX = left ? 0.0F : 400.0F;
    const float pivotY = top ? 0.0F : 240.0F;
    const float insetX = left ? marginX : -marginX;
    const float insetY = top ? marginY : -marginY;
    destination.x =
        pivotX + (destination.x - pivotX) * scale + insetX;
    destination.y =
        pivotY + (destination.y - pivotY) * scale + insetY;
    if (primitive.role == oot3d::ui::UiPrimitiveRole::MagicFrame ||
        primitive.role == oot3d::ui::UiPrimitiveRole::MagicFill) {
      destination.y += static_cast<float>(config.MagicBarY);
    }
    destination.width *= scale;
    destination.height *= scale;
  }
}

TopScreenQuestGeometryStats TransformTopScreenQuestGeometry(
    std::span<std::array<TopScreenVec3, 4>> quads,
    const TopScreenQuestGeometryContext &context) noexcept {
  TopScreenQuestGeometryStats stats;
  if (context.NativePageGateActive) {
    return stats;
  }
  const auto translateTo = [&stats](std::array<TopScreenVec3, 4> &quad,
                                    float centerX, float centerY, float targetX,
                                    float targetY, bool hidden) {
    for (auto &vertex : quad) {
      vertex.X = (vertex.X - centerX) + targetX;
      vertex.Y = (vertex.Y - centerY) + targetY;
    }
    ++stats.QuadsTranslated;
    stats.QuadsHidden += hidden ? 1U : 0U;
  };

  for (auto &quad : quads) {
    ++stats.QuadsVisited;
    float centerX = 0.0F;
    float centerY = 0.0F;
    for (const auto &vertex : quad) {
      centerX += vertex.X * 0.25F;
      centerY += vertex.Y * 0.25F;
    }

    if (context.PauseState < 12U) {
      if (centerX <= 330.0F) {
        if (centerX < 110.0F && centerY > 130.0F) {
          if (context.LeftRegionOffsetX == 0.0F &&
              context.LeftRegionOffsetY == 0.0F) {
            translateTo(quad, centerX, centerY, 500.0F, 300.0F, true);
          } else {
            for (auto &vertex : quad) {
              vertex.X += context.LeftRegionOffsetX;
              vertex.Y += context.LeftRegionOffsetY;
            }
            ++stats.QuadsTranslated;
          }
        }
      } else if (centerX < 398.0F && centerY > 150.0F) {
        if (!context.PlayerSpecialState || centerY < 195.0F) {
          for (auto &vertex : quad) {
            vertex.X = 288.0F + (vertex.X - centerX) * 0.95F;
            vertex.Y = 24.0F + (vertex.Y - centerY) * 0.95F;
          }
          ++stats.QuadsTranslated;
          ++stats.QuadsScaled;
        } else {
          translateTo(quad, centerX, centerY, 500.0F, 300.0F, true);
        }
      }
    } else if (centerX > 330.0F && centerX < 398.0F && centerY > 150.0F) {
      translateTo(quad, centerX, centerY, 500.0F, 300.0F, true);
    }

    if (context.HudScale != 1.0F || context.HudMarginX != 0.0F ||
        context.HudMarginY != 0.0F) {
      centerX = 0.0F;
      centerY = 0.0F;
      for (const auto &vertex : quad) {
        centerX += vertex.X * 0.25F;
        centerY += vertex.Y * 0.25F;
      }
      if (centerX <= 430.0F && centerY <= 280.0F) {
        const bool left = centerX < 200.0F;
        const bool top = centerY < 120.0F;
        const float pivotX = left ? 0.0F : 400.0F;
        const float pivotY = top ? 0.0F : 240.0F;
        const float insetX = left ? context.HudMarginX : -context.HudMarginX;
        const float insetY = top ? context.HudMarginY : -context.HudMarginY;
        for (auto &vertex : quad) {
          vertex.X =
              pivotX + (vertex.X - pivotX) * context.HudScale + insetX;
          vertex.Y =
              pivotY + (vertex.Y - pivotY) * context.HudScale + insetY;
        }
        ++stats.QuadsScaled;
      }
    }
  }
  return stats;
}

bool ReadTopScreenQuestGeometryContext(
    NativeA32Memory &memory, TopScreenQuestGeometryContext *context,
    const TopScreenPauseProjectionState *projection, std::string *error) {
  if (context == nullptr) {
    if (error != nullptr)
      *error = "missing Quest geometry context output";
    return false;
  }
  constexpr std::uint32_t kPauseState = 0x0050AF68U;
  constexpr std::uint32_t kSceneOwner = 0x005043E0U;
  constexpr std::array<std::uint32_t, 4> kNativePageGates{
      0x00504484U, 0x0050672CU, 0x00506CE8U, 0x0050A530U};
  std::uint32_t pauseState = 0U;
  std::uint32_t sceneOwner = 0U;
  if (!memory.Read32(kPauseState, &pauseState) ||
      !memory.Read32(kSceneOwner, &sceneOwner)) {
    if (error != nullptr)
      *error = "cannot read Quest geometry state roots";
    return false;
  }
  bool nativePageGateActive = false;
  for (const auto gateAddress : kNativePageGates) {
    std::uint32_t gate = 0U;
    if (!memory.Read32(gateAddress, &gate)) {
      if (error != nullptr)
        *error = "cannot read Quest native-page gate";
      return false;
    }
    nativePageGateActive = nativePageGateActive || gate != 0U;
  }
  bool specialMode = false;
  if (sceneOwner != 0U) {
    std::uint8_t sceneMode = 0U;
    std::uint8_t sceneVariant = 0U;
    if (!memory.Read8(sceneOwner + 0x100U, &sceneMode) ||
        !memory.Read8(sceneOwner + 0x101U, &sceneVariant)) {
      if (error != nullptr)
        *error = "cannot read Quest scene mode";
      return false;
    }
    if (sceneMode == 3U && sceneVariant == 2U) {
      std::uint32_t sceneExtension = 0U;
      std::uint32_t specialOwner = 0U;
      std::uint32_t specialValue = 0U;
      if (!memory.Read32(sceneOwner + 0x20ACU, &sceneExtension) ||
          sceneExtension == 0U ||
          !memory.Read32(sceneExtension + 0x12B8U, &specialOwner)) {
        if (error != nullptr)
          *error = "cannot read Quest special-mode owner";
        return false;
      }
      if (specialOwner != 0U) {
        specialValue = specialOwner;
      }
      specialMode = specialValue != 0U;
    }
  }
  context->PauseState = pauseState;
  context->NativePageGateActive =
      nativePageGateActive ||
      (projection != nullptr && projection->NativeQuestGate);
  context->PlayerSpecialState = specialMode;
  context->LeftRegionOffsetX =
      projection != nullptr ? projection->OffsetX : 0.0F;
  context->LeftRegionOffsetY =
      projection != nullptr ? projection->OffsetY : 0.0F;
  return true;
}

bool ApplyTopScreenSystemMenuLayerSuppression(
    NativeA32Memory &memory, TopScreenSystemMenuLayerStats *stats,
    std::string *error) {
  TopScreenSystemMenuLayerStats result;
  constexpr std::uint32_t kSystemMenuOwner = 0x0050A530U;
  constexpr std::uint32_t kLayerRenderer = 0x0050A510U;

  std::uint32_t systemMenuOwner = 0U;
  if (!memory.Read32(kSystemMenuOwner, &systemMenuOwner)) {
    if (error != nullptr)
      *error = "cannot read TopScreen system-menu owner";
    return false;
  }
  result.Active = systemMenuOwner != 0U;
  if (!result.Active) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  std::uint32_t renderer = 0U;
  if (!memory.Read32(kLayerRenderer, &renderer)) {
    if (error != nullptr)
      *error = "cannot read TopScreen system-menu layer renderer";
    return false;
  }
  result.RendererAddress = renderer;
  if (renderer == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  std::uint32_t quadCount = 0U;
  std::uint32_t positions = 0U;
  std::uint32_t colors = 0U;
  if (!memory.Read32(renderer, &quadCount) ||
      !memory.Read32(renderer + 0x0CU, &positions) ||
      !memory.Read32(renderer + 0x18U, &colors)) {
    if (error != nullptr)
      *error = "cannot read TopScreen system-menu layer streams";
    return false;
  }
  if (quadCount == 0U || quadCount > 0xFFU || positions == 0U ||
      colors == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  constexpr std::array<float, 4> kSuppressedColor{1.0F, 1.0F, 1.0F, 0.0F};
  for (std::uint32_t quad = 0U; quad < quadCount; ++quad) {
    std::array<float, 12> vertices{};
    if (!memory.ReadBytes(
            positions + quad * 0x30U,
            std::span<std::uint8_t>(
                reinterpret_cast<std::uint8_t *>(vertices.data()),
                sizeof(vertices)))) {
      if (error != nullptr)
        *error = "cannot read TopScreen system-menu layer quad";
      return false;
    }
    ++result.QuadsVisited;

    float minimumX = vertices[0];
    float maximumX = vertices[0];
    float minimumY = vertices[1];
    float maximumY = vertices[1];
    for (std::size_t vertex = 1U; vertex < 4U; ++vertex) {
      minimumX = std::min(minimumX, vertices[vertex * 3U]);
      maximumX = std::max(maximumX, vertices[vertex * 3U]);
      minimumY = std::min(minimumY, vertices[vertex * 3U + 1U]);
      maximumY = std::max(maximumY, vertices[vertex * 3U + 1U]);
    }
    if (maximumX - minimumX <= 130.0F ||
        maximumY - minimumY <= 80.0F) {
      continue;
    }

    const std::uint32_t colorAddress = colors + quad * 0x40U;
    for (std::size_t vertex = 0U; vertex < 4U; ++vertex) {
      if (!memory.WriteBytes(
              colorAddress + static_cast<std::uint32_t>(vertex * 0x10U),
              std::span<const std::uint8_t>(
                  reinterpret_cast<const std::uint8_t *>(
                      kSuppressedColor.data()),
                  sizeof(kSuppressedColor)))) {
        if (error != nullptr)
          *error = "cannot write TopScreen system-menu layer color";
        return false;
      }
    }
    ++result.QuadsSuppressed;
  }
  if (stats != nullptr)
    *stats = result;
  return true;
}

bool ReadTopScreenOcarinaUiActive(NativeA32Memory &memory, bool *active,
                                  std::string *error) {
  if (active == nullptr) {
    if (error != nullptr)
      *error = "missing TopScreen Ocarina UI output";
    return false;
  }
  constexpr std::uint32_t kSceneOwnerRoot = 0x005043E0U;
  std::uint32_t scene = 0U;
  if (!memory.Read32(kSceneOwnerRoot, &scene)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Ocarina scene owner";
    return false;
  }
  if (scene == 0U) {
    *active = false;
    return true;
  }

  std::uint16_t ocarinaResult = 0U;
  std::uint32_t sceneExtension = 0U;
  if (!memory.Read16(scene + 0x2B82U, &ocarinaResult) ||
      !memory.Read32(scene + 0x20ACU, &sceneExtension)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Ocarina UI state";
    return false;
  }
  if (ocarinaResult != 0x00FFU) {
    *active = true;
    return true;
  }

  if (sceneExtension != 0U) {
    std::uint32_t stateFlags = 0U;
    if (!memory.Read32(sceneExtension + 0x1714U, &stateFlags)) {
      if (error != nullptr)
        *error = "cannot read TopScreen Ocarina state flags";
      return false;
    }
    if ((stateFlags & 0x01000000U) != 0U) {
      *active = true;
      return true;
    }
  }

  std::uint16_t ocarinaAction = 0U;
  if (!memory.Read16(scene + 0x2B80U, &ocarinaAction)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Ocarina action";
    return false;
  }
  *active = ocarinaAction > 1U;
  return true;
}

bool TransformTopScreenModelRegion(
    NativeA32Memory &memory, std::uint32_t renderBufferAddress,
    const TopScreenModelRegionTransform &transform,
    TopScreenModelRegionStats *stats, std::string *error) {
  TopScreenModelRegionStats result;
  if (renderBufferAddress == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  std::uint32_t quadCount = 0U;
  std::uint32_t positionsAddress = 0U;
  std::uint32_t translationsAddress = 0U;
  if (!memory.Read32(renderBufferAddress, &quadCount) ||
      !memory.Read32(renderBufferAddress + 0x0CU, &positionsAddress) ||
      !memory.Read32(renderBufferAddress + 0x1CU, &translationsAddress)) {
    if (error != nullptr)
      *error = "cannot read TopScreen model-region render-buffer header";
    return false;
  }
  if (quadCount == 0U || quadCount > 0xFFU || positionsAddress == 0U ||
      translationsAddress == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  struct ModelQuad {
    std::array<TopScreenVec3, 4> Positions{};
    float TranslationX = 0.0F;
    float TranslationY = 0.0F;
    float CenterX = 0.0F;
    float CenterY = 0.0F;
    bool Selected = false;
  };
  std::vector<ModelQuad> quads(quadCount);
  result.ContractMatched = true;
  float groupCenterX = 0.0F;
  float groupCenterY = 0.0F;
  for (std::uint32_t index = 0U; index < quadCount; ++index) {
    auto &quad = quads[index];
    const std::uint32_t positions =
        positionsAddress + index * static_cast<std::uint32_t>(0x30U);
    const std::uint32_t translation = translationsAddress + index * 8U;
    std::uint32_t translationXBits = 0U;
    std::uint32_t translationYBits = 0U;
    if (!memory.ReadBytes(
            positions,
            std::span<std::uint8_t>(
                reinterpret_cast<std::uint8_t *>(quad.Positions.data()),
                sizeof(quad.Positions))) ||
        !memory.Read32(translation, &translationXBits) ||
        !memory.Read32(translation + 4U, &translationYBits)) {
      if (error != nullptr)
        *error = "cannot read TopScreen model-region streams";
      return false;
    }
    quad.TranslationX = std::bit_cast<float>(translationXBits);
    quad.TranslationY = std::bit_cast<float>(translationYBits);
    ++result.QuadsVisited;
    quad.CenterX = quad.TranslationX;
    quad.CenterY = quad.TranslationY;
    for (const auto &vertex : quad.Positions) {
      quad.CenterX += vertex.X * 0.25F;
      quad.CenterY += vertex.Y * 0.25F;
    }
    quad.Selected =
        transform.MinimumX < quad.CenterX &&
        quad.CenterX < transform.MaximumX &&
        transform.MinimumY < quad.CenterY;
    if (quad.Selected) {
      groupCenterX += quad.CenterX;
      groupCenterY += quad.CenterY;
      ++result.QuadsSelected;
    }
  }

  if (result.QuadsSelected != 0U) {
    const float selectedCount = static_cast<float>(result.QuadsSelected);
    groupCenterX /= selectedCount;
    groupCenterY /= selectedCount;
    for (std::uint32_t index = 0U; index < quadCount; ++index) {
      auto &quad = quads[index];
      if (!quad.Selected)
        continue;
      for (auto &vertex : quad.Positions) {
        vertex.X =
            transform.TargetX +
            ((quad.TranslationX + vertex.X) - groupCenterX) *
                transform.Scale -
            quad.TranslationX;
        vertex.Y =
            transform.TargetY +
            ((quad.TranslationY + vertex.Y) - groupCenterY) *
                transform.Scale -
            quad.TranslationY;
        ++result.VerticesTransformed;
      }
      const std::uint32_t positions =
          positionsAddress + index * static_cast<std::uint32_t>(0x30U);
      if (!memory.WriteBytes(
              positions,
              std::span<const std::uint8_t>(
                  reinterpret_cast<const std::uint8_t *>(
                      quad.Positions.data()),
                  sizeof(quad.Positions)))) {
        if (error != nullptr)
          *error = "cannot write TopScreen model-region positions";
        return false;
      }
    }
  }

  if (stats != nullptr)
    *stats = result;
  return true;
}

bool ApplyTopScreenQuestSubmitModelTransforms(
    NativeA32Memory &memory, const TopScreenUiConfig &config,
    TopScreenQuestModelTransformStats *stats, std::string *error) {
  TopScreenQuestModelTransformStats result;
  constexpr std::uint32_t kQuestOwnerBinding = 0x004FC688U;
  std::uint32_t owner = 0U;
  if (!memory.Read32(kQuestOwnerBinding, &owner)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Quest model owner";
    return false;
  }
  if (owner == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  std::uint32_t mainRenderer = 0U;
  if (!memory.Read32(owner + 0x10CU, &mainRenderer)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Quest main model renderer";
    return false;
  }
  TopScreenModelRegionStats modelStats;
  if (!TransformTopScreenModelRegion(
          memory, mainRenderer,
          {300.0F, 430.0F, 140.0F, 500.0F, 300.0F, 1.0F},
          &modelStats, error)) {
    return false;
  }
  result.MainModelsTransformed =
      modelStats.VerticesTransformed != 0U ? 1U : 0U;

  std::uint32_t childCountBits = 0U;
  if (!memory.Read32(owner + 0x224U, &childCountBits)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Quest child-model count";
    return false;
  }
  const auto childCount = static_cast<std::int32_t>(childCountBits);
  if (childCount > 0) {
    const float scale = config.HudScale;
    const float baseX = config.HudLayout == TopScreenHudLayout::Restoration
                            ? 358.0F
                            : 248.0F;
    const float baseY = config.HudLayout == TopScreenHudLayout::Restoration
                            ? 68.0F
                            : 24.0F;
    const float targetX = 400.0F + (baseX - 400.0F) * scale -
                          static_cast<float>(config.HudMarginX);
    const float targetY = baseY * scale +
                          static_cast<float>(config.HudMarginY);
    const TopScreenModelRegionTransform childTransform{
        300.0F, 430.0F, 140.0F, targetX, targetY, scale * 0.6F};
    const std::uint32_t boundedCount =
        std::min<std::uint32_t>(static_cast<std::uint32_t>(childCount), 64U);
    for (std::uint32_t index = 0U; index < boundedCount; ++index) {
      std::uint32_t child = 0U;
      if (!memory.Read32(owner + 0x0CU + index * 4U, &child)) {
        if (error != nullptr)
          *error = "cannot read TopScreen Quest child-model owner";
        return false;
      }
      if (child == 0U)
        continue;
      ++result.ChildModelsVisited;
      std::uint32_t renderer = 0U;
      if (!memory.Read32(child + 8U, &renderer)) {
        if (error != nullptr)
          *error = "cannot read TopScreen Quest child-model renderer";
        return false;
      }
      modelStats = {};
      if (!TransformTopScreenModelRegion(memory, renderer, childTransform,
                                         &modelStats, error)) {
        return false;
      }
      result.ChildModelsTransformed +=
          modelStats.VerticesTransformed != 0U ? 1U : 0U;
    }
  }

  if (stats != nullptr)
    *stats = result;
  return true;
}

bool ApplyTopScreenQuestDrawModelTransform(
    NativeA32Memory &memory, std::uint32_t sceneContextAddress,
    const TopScreenUiConfig &config, TopScreenPauseProjectionState &state,
    TopScreenQuestModelTransformStats *stats, std::string *error) {
  TopScreenQuestModelTransformStats result;
  if (state.QuestDrawModelAdjusted || sceneContextAddress == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  std::uint8_t sceneMode = 0U;
  std::uint8_t sceneVariant = 0U;
  std::uint32_t sceneExtension = 0U;
  if (!memory.Read8(sceneContextAddress + 0x100U, &sceneMode) ||
      !memory.Read8(sceneContextAddress + 0x101U, &sceneVariant) ||
      !memory.Read32(sceneContextAddress + 0x20ACU, &sceneExtension)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Quest draw scene context";
    return false;
  }
  if (sceneMode != 3U || sceneVariant != 2U || sceneExtension == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  constexpr std::uint32_t kOptionalPanelState = 0x005043F0U;
  constexpr std::uint32_t kSystemMenuOwner = 0x0050A530U;
  constexpr std::uint32_t kGlobalActionState = 0x0050AF68U;
  constexpr std::uint32_t kQuestOwnerBinding = 0x004FC688U;
  constexpr std::uint32_t kQuestDrawBlockerA = 0x00588E3CU;
  constexpr std::uint32_t kQuestDrawBlockerB = 0x004FC6A0U;
  std::uint32_t optionalPanelState = 0U;
  std::uint32_t systemMenuOwner = 0U;
  std::uint32_t globalActionState = 0U;
  std::uint32_t owner = 0U;
  std::uint32_t blockerA = 0U;
  std::uint32_t blockerB = 0U;
  if (!memory.Read32(kOptionalPanelState, &optionalPanelState) ||
      !memory.Read32(kSystemMenuOwner, &systemMenuOwner) ||
      !memory.Read32(kGlobalActionState, &globalActionState) ||
      !memory.Read32(kQuestOwnerBinding, &owner) ||
      !memory.Read32(kQuestDrawBlockerA, &blockerA) ||
      !memory.Read32(kQuestDrawBlockerB, &blockerB)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Quest draw gates";
    return false;
  }
  if (optionalPanelState == 0U || systemMenuOwner != 0U ||
      globalActionState == 0U || owner == 0U || blockerA != 0U ||
      blockerB != 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  std::uint32_t modelState = 0U;
  std::uint32_t child = 0U;
  if (!memory.Read32(owner + 0x428U, &modelState) ||
      !memory.Read32(owner + 0x0CU, &child)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Quest draw-model owner";
    return false;
  }
  if ((modelState != 5U && modelState != 6U) || child == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }
  std::uint32_t renderer = 0U;
  if (!memory.Read32(child + 8U, &renderer)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Quest draw-model renderer";
    return false;
  }
  if (renderer == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  const float scale = config.HudScale;
  const float baseX = config.HudLayout == TopScreenHudLayout::Restoration
                          ? 358.0F
                          : 248.0F;
  const float baseY = config.HudLayout == TopScreenHudLayout::Restoration
                          ? 68.0F
                          : 24.0F;
  const float targetX = 400.0F + (baseX - 400.0F) * scale -
                        static_cast<float>(config.HudMarginX) + scale * 8.0F;
  const float targetY = baseY * scale +
                        static_cast<float>(config.HudMarginY) + scale * 10.0F;
  TopScreenModelRegionStats modelStats;
  if (!TransformTopScreenModelRegion(
          memory, renderer,
          {-9999.0F, 9999.0F, -9999.0F, targetX, targetY, scale * 0.7F},
          &modelStats, error)) {
    return false;
  }
  state.QuestDrawModelAdjusted = true;
  result.DrawModelsTransformed =
      modelStats.VerticesTransformed != 0U ? 1U : 0U;
  if (stats != nullptr)
    *stats = result;
  return true;
}

bool ApplyTopScreenPauseProjection(NativeA32Memory &memory,
                                   TopScreenPauseProjectionState &state,
                                   std::string *error,
                                   const TopScreenUiConfig *config) {
  constexpr std::uint32_t kProjectionOwnerRoot = 0x004FDA84U;
  constexpr std::array<std::uint32_t, 4> kIndicatorRendererRoots{
      0x004FDA8CU, 0x004FDA90U, 0x004FDA94U, 0x004FDA98U};
  constexpr std::uint32_t kSecondaryLayerState = 0x004FDABCU;
  constexpr std::uint32_t kCompactLayoutFlag = 0x00587966U;
  constexpr std::uint32_t kPauseState = 0x0050AF68U;
  constexpr std::uint32_t kSceneOwner = 0x005043E0U;
  constexpr std::uint32_t kQuestRenderer = 0x004FC688U;

  state.OffsetX = 0.0F;
  state.OffsetY = 0.0F;
  state.NativeQuestGate = false;

  if (!ApplyTopScreenSystemMenuLayerSuppression(memory, nullptr, error)) {
    return false;
  }

  std::uint32_t owner = 0U;
  std::uint32_t pauseState = 0U;
  std::uint32_t sceneOwner = 0U;
  std::uint32_t questRenderer = 0U;
  if (!memory.Read32(kProjectionOwnerRoot, &owner) ||
      !memory.Read32(kPauseState, &pauseState) ||
      !memory.Read32(kSceneOwner, &sceneOwner) ||
      !memory.Read32(kQuestRenderer, &questRenderer)) {
    if (error != nullptr)
      *error = "cannot read pause projection roots";
    return false;
  }

  bool specialMode = false;
  if (sceneOwner != 0U) {
    std::uint8_t mode = 0U;
    std::uint8_t variant = 0U;
    if (!memory.Read8(sceneOwner + 0x100U, &mode) ||
        !memory.Read8(sceneOwner + 0x101U, &variant)) {
      if (error != nullptr)
        *error = "cannot read pause projection scene mode";
      return false;
    }
    if (mode == 3U && variant == 2U) {
      std::uint32_t extension = 0U;
      std::uint32_t nestedOwner = 0U;
      if (!memory.Read32(sceneOwner + 0x20ACU, &extension)) {
        if (error != nullptr)
          *error = "cannot read pause projection extension";
        return false;
      }
      if (extension != 0U &&
          !memory.Read32(extension + 0x12B8U, &nestedOwner)) {
        if (error != nullptr)
          *error = "cannot read pause projection nested owner";
        return false;
      }
      specialMode = nestedOwner != 0U;
    }
  }
  state.NativeQuestGate =
      (specialMode || pauseState == 0x0BU) && questRenderer != 0U;

  if (owner == 0U)
    return true;
  std::uint32_t extents = 0U;
  std::uint32_t offset = 0U;
  if (!memory.Read32(owner + 0x0CU, &extents) ||
      !memory.Read32(owner + 0x1CU, &offset)) {
    if (error != nullptr)
      *error = "cannot read pause projection owners";
    return false;
  }
  if (extents == 0U || offset == 0U)
    return true;

  std::uint32_t extentABits = 0U;
  std::uint32_t extentBBits = 0U;
  std::uint32_t offsetXBits = 0U;
  std::uint32_t offsetYBits = 0U;
  std::uint32_t secondaryLayer = 0U;
  std::uint8_t compactLayout = 0U;
  if (!memory.Read32(extents, &extentABits) ||
      !memory.Read32(extents + 0x24U, &extentBBits) ||
      !memory.Read32(offset, &offsetXBits) ||
      !memory.Read32(offset + 4U, &offsetYBits) ||
      !memory.Read32(kSecondaryLayerState, &secondaryLayer) ||
      !memory.Read8(kCompactLayoutFlag, &compactLayout)) {
    if (error != nullptr)
      *error = "cannot read pause projection values";
    return false;
  }
  const float extentA = std::bit_cast<float>(extentABits);
  const float extentB = std::bit_cast<float>(extentBBits);
  const auto recoverOriginal = [](TopScreenPauseProjectionState::TrackedPosition
                                      &tracked,
                                  std::uint32_t address,
                                  std::uint32_t currentBits) {
    if (!tracked.Valid || tracked.Address != address ||
        tracked.LastWrittenBits != currentBits) {
      tracked.Address = address;
      tracked.OriginalBits = currentBits;
      tracked.Valid = true;
    }
    return std::bit_cast<float>(tracked.OriginalBits);
  };
  const float nativeOffsetX =
      recoverOriginal(state.MapX, offset, offsetXBits);
  float nativeOffsetY =
      recoverOriginal(state.MapY, offset + 4U, offsetYBits);
  if (nativeOffsetY >= 1500.0F) {
    nativeOffsetY -= 3000.0F;
    state.MapY.OriginalBits = std::bit_cast<std::uint32_t>(nativeOffsetY);
  }
  const float maximumExtent = std::max(extentA, extentB);
  float projectedOffsetX = nativeOffsetX;
  if (maximumExtent - std::min(extentA, extentB) > 0.0F) {
    const float rightEdge = secondaryLayer == 1U ? 425.0F : 438.0F;
    const float configuredMargin = config != nullptr
                                       ? static_cast<float>(config->HudMarginX)
                                       : 4.0F;
    const float compactInset = compactLayout != 0U ? 36.0F : 0.0F;
    state.OffsetX = rightEdge - (nativeOffsetX + maximumExtent) -
                    compactInset - configuredMargin;
    projectedOffsetX = nativeOffsetX + state.OffsetX;
  }
  float projectedOffsetY = nativeOffsetY;
  if (!state.AlternatePage) {
    projectedOffsetY += 3000.0F;
    state.OffsetY = 3000.0F;
  }
  state.MapX.LastWrittenBits =
      std::bit_cast<std::uint32_t>(projectedOffsetX);
  state.MapY.LastWrittenBits =
      std::bit_cast<std::uint32_t>(projectedOffsetY);
  if (!memory.Write32(offset, state.MapX.LastWrittenBits) ||
      !memory.Write32(offset + 4U, state.MapY.LastWrittenBits)) {
    if (error != nullptr)
      *error = "cannot write pause projection values";
    return false;
  }

  for (std::size_t group = 0; group < kIndicatorRendererRoots.size();
       ++group) {
    std::uint32_t renderer = 0U;
    if (!memory.Read32(kIndicatorRendererRoots[group], &renderer)) {
      if (error != nullptr)
        *error = "cannot read minimap indicator renderer root";
      return false;
    }
    if (renderer == 0U) {
      continue;
    }
    std::uint32_t count = 0U;
    std::uint32_t renderBuffer = 0U;
    if (!memory.Read32(renderer + 0x0CU, &count) ||
        !memory.Read32(renderer + 0x08U, &renderBuffer)) {
      if (error != nullptr)
        *error = "cannot read minimap indicator renderer";
      return false;
    }
    if (count == 0U || count > 64U || renderBuffer == 0U) {
      continue;
    }
    std::uint32_t positions = 0U;
    if (!memory.Read32(renderBuffer + 0x1CU, &positions)) {
      if (error != nullptr)
        *error = "cannot read minimap indicator position stream";
      return false;
    }
    if (positions == 0U) {
      continue;
    }
    for (std::uint32_t index = 0U; index < count; ++index) {
      const std::uint32_t address = positions + index * 8U;
      std::uint32_t currentBits = 0U;
      if (!memory.Read32(address, &currentBits)) {
        if (error != nullptr)
          *error = "cannot read minimap indicator position";
        return false;
      }
      auto &tracked = state.IconX[group * 64U + index];
      const float original = recoverOriginal(tracked, address, currentBits);
      if (original >= 400.0F) {
        continue;
      }
      const float projected =
          state.AlternatePage ? original + state.OffsetX : 400.0F;
      tracked.LastWrittenBits = std::bit_cast<std::uint32_t>(projected);
      if (!memory.Write32(address, tracked.LastWrittenBits)) {
        if (error != nullptr)
          *error = "cannot write minimap indicator position";
        return false;
      }
    }
  }
  return true;
}

bool TransformTopScreenQuestRenderBuffer(
    NativeA32Memory &memory, std::uint32_t renderBufferAddress,
    const TopScreenQuestGeometryContext &context,
    TopScreenQuestRenderBufferStats *stats, std::string *error) {
  constexpr std::uint32_t kNativeQuestQuadCount = 8U;
  constexpr std::size_t kPositionStride = 0x30U;
  std::uint32_t count = 0U;
  std::uint32_t positionsAddress = 0U;
  if (!memory.Read32(renderBufferAddress, &count) ||
      !memory.Read32(renderBufferAddress + 0x10U, &positionsAddress)) {
    if (error != nullptr)
      *error = "cannot read native Quest render-buffer header";
    return false;
  }
  if (count != kNativeQuestQuadCount || positionsAddress == 0U) {
    if (error != nullptr)
      *error = "native Quest render-buffer contract mismatch";
    return false;
  }
  std::array<std::array<TopScreenVec3, 4>, kNativeQuestQuadCount> quads{};
  static_assert(sizeof(quads) == kNativeQuestQuadCount * kPositionStride);
  if (!memory.IsMapped(positionsAddress, sizeof(quads)) ||
      !memory.IsWritable(positionsAddress, sizeof(quads)) ||
      !memory.ReadBytes(
          positionsAddress,
          std::span<std::uint8_t>(
              reinterpret_cast<std::uint8_t *>(quads.data()), sizeof(quads)))) {
    if (error != nullptr)
      *error = "native Quest materialized position stream is unavailable";
    return false;
  }
  TopScreenQuestRenderBufferStats result;
  result.QuadCount = count;
  result.Geometry = TransformTopScreenQuestGeometry(quads, context);
  if (!memory.WriteBytes(
          positionsAddress,
          std::span<const std::uint8_t>(
              reinterpret_cast<const std::uint8_t *>(quads.data()),
              sizeof(quads)))) {
    if (error != nullptr)
      *error = "cannot write native Quest materialized positions";
    return false;
  }
  if (stats != nullptr)
    *stats = result;
  return true;
}

TopScreenPauseTargetPlan
ResolveTopScreenPauseTargetCommand(const TopScreenPauseTargetCommand &command,
                                   bool routeActive) noexcept {
  TopScreenPauseTargetPlan result;
  if (!routeActive || !command.RendererEnabled) {
    return result;
  }
  result.Handled = true;
  result.StoredCommand = command.NativeCommand;
  if (command.NativeCommand == 0x400U) {
    result.BindTopTarget = true;
    result.FramebufferBindingOffset = 0x3CU;
    result.ViewportWidth = 240U;
    result.ViewportHeight = 320U;
  } else if (command.NativeCommand == 0x401U) {
    result.BindTopTarget = true;
    result.FramebufferBindingOffset = 0x38U;
    result.ViewportY = 40U;
    result.ViewportWidth = command.HalfHeightMode ? 240U : 480U;
    result.ViewportHeight = 320U;
  }
  return result;
}

TopScreenPauseTargetPlan ResolveTopScreenPausePageRedrawTargetCommand(
    const TopScreenPauseTargetCommand &command) noexcept {
  auto result = ResolveTopScreenPauseTargetCommand(command, true);
  if (result.BindTopTarget && command.NativeCommand == 0x400U) {
    result.ViewportY = 40U;
    result.ViewportWidth = 480U;
    result.ViewportHeight = 320U;
  }
  return result;
}

namespace {
constexpr std::array<std::uint32_t, 6> kTopScreenPauseChildStateAddresses{
    0x00504484U, 0x0050672CU, 0x00506CE8U,
    0x0050A530U, 0x005043F4U, 0x0050AF68U};
constexpr std::uint8_t kTopScreenPauseTailSuppressionMask = 0x2FU;
constexpr std::uint8_t kTopScreenPauseAuxiliarySuppressionMask = 0x3FU;
constexpr std::uint8_t kTopScreenPausePageRedrawSuppressionMask = 0x20U;
constexpr std::array<std::uint32_t, 4> kTopScreenAlternateRendererPointers{
    0x005C4CD4U, 0x005C4CD8U, 0x005C4CDCU, 0x005C4CE0U};

bool ReadRequired8(NativeA32Memory &memory, std::uint32_t address,
                   std::uint8_t *value, std::string *error) {
  if (memory.Read8(address, value))
    return true;
  if (error != nullptr)
    *error = "cannot read TopScreen pause byte state";
  return false;
}
} // namespace

bool ReadTopScreenPauseDrawInputs(NativeA32Memory &memory,
                                  TopScreenPauseDrawInputs *inputs,
                                  std::string *error) {
  if (inputs == nullptr) {
    if (error != nullptr)
      *error = "missing TopScreen pause input output";
    return false;
  }
  constexpr std::uint32_t kSceneOwnerRoot = 0x005043E0U;
  constexpr std::uint32_t kRuntimeMode = 0x00588E3CU;
  constexpr std::uint32_t kEntranceIndex = 0x00587960U;
  constexpr std::uint32_t kNativeTransitionState = 0x005C4344U;
  constexpr std::uint32_t kAlternatePathReady = 0x005C3F61U;
  constexpr std::uint32_t kAlternatePathVisible = 0x005C3F62U;
  constexpr std::uint32_t kSuppressionOverride = 0x005C3F65U;

  TopScreenPauseDrawInputs result;
  std::uint32_t scene = 0U;
  std::uint32_t transitionState = 0U;
  std::uint8_t alternatePathReady = 0U;
  std::uint8_t alternatePathVisible = 0U;
  std::uint8_t suppressionOverride = 0U;
  if (!memory.Read32(kSceneOwnerRoot, &scene) ||
      !memory.Read32(kRuntimeMode, &result.RuntimeMode) ||
      !memory.Read32(kEntranceIndex, &result.EntranceIndex) ||
      !memory.Read32(kNativeTransitionState, &transitionState) ||
      !ReadRequired8(memory, kAlternatePathReady, &alternatePathReady, error) ||
      !ReadRequired8(memory, kAlternatePathVisible, &alternatePathVisible,
                     error) ||
      !ReadRequired8(memory, kSuppressionOverride, &suppressionOverride,
                     error)) {
    if (error != nullptr && error->empty())
      *error = "cannot read TopScreen pause routing state";
    return false;
  }
  result.AlternatePathReady = alternatePathReady != 0U;
  result.AlternatePathVisible = alternatePathVisible != 0U;
  result.SuppressionOverride = suppressionOverride != 0U;
  result.NativeTransitionActive =
      static_cast<std::int32_t>(transitionState) >= 0;
  result.HasScene = scene != 0U;
  if (result.HasScene) {
    if (!memory.Read8(scene + 0x100U, &result.SceneMode) ||
        !memory.Read8(scene + 0x101U, &result.SceneVariant) ||
        !memory.Read16(scene + 0x318CU, &result.SceneSequence)) {
      if (error != nullptr)
        *error = "cannot read TopScreen pause scene state";
      return false;
    }
  }
  for (std::size_t index = 0; index < result.ChildStates.size(); ++index) {
    if (!memory.Read32(kTopScreenPauseChildStateAddresses[index],
                       &result.ChildStates[index])) {
      if (error != nullptr)
        *error = "cannot read TopScreen pause page gates";
      return false;
    }
    if (index < 4U)
      result.AnyPageGateActive |= result.ChildStates[index] != 0U;
  }
  *inputs = result;
  return true;
}

TopScreenPauseClosePlan
ResolveTopScreenPauseStartClose(const TopScreenPauseDrawInputs &inputs,
                                bool startPressed) noexcept {
  if (!startPressed || !inputs.AnyPageGateActive)
    return {};

  // 0x005C9D6C checks Items before Gear, Dungeon Map and System Menu.
  if (inputs.ChildStates[1] != 0U)
    return {TopScreenPauseClosePage::Items, 0x002EB628U};
  if (inputs.ChildStates[0] != 0U)
    return {TopScreenPauseClosePage::Gear, 0x002F8AF4U};
  if (inputs.ChildStates[2] != 0U)
    return {TopScreenPauseClosePage::DungeonMap, 0x002F0618U};
  if (inputs.ChildStates[3] != 0U)
    return {TopScreenPauseClosePage::SystemMenu, 0x002E9920U};
  return {};
}

TopScreenPauseSystemOpenPlan ResolveTopScreenPauseBSystemOpen(
    const TopScreenPauseDrawInputs &inputs, bool bPressed,
    TopScreenPauseSystemOpenState &state) noexcept {
  const bool ordinaryPageActive = inputs.ChildStates[0] != 0U ||
                                  inputs.ChildStates[1] != 0U ||
                                  inputs.ChildStates[2] != 0U;
  if (bPressed && ordinaryPageActive)
    state.RemainingTicks = 5U;
  if (state.RemainingTicks == 0U)
    return {};

  TopScreenPauseSystemOpenPlan plan;
  std::uint32_t pageState = 0U;
  // 0x005C9F14 checks Items, Gear, then Dungeon Map.
  if (inputs.ChildStates[1] != 0U) {
    plan = {TopScreenPauseClosePage::Items, 0x002EB628U};
    pageState = inputs.ChildStates[1];
  } else if (inputs.ChildStates[0] != 0U) {
    plan = {TopScreenPauseClosePage::Gear, 0x002F8AF4U};
    pageState = inputs.ChildStates[0];
  } else if (inputs.ChildStates[2] != 0U) {
    plan = {TopScreenPauseClosePage::DungeonMap, 0x002F0618U};
    pageState = inputs.ChildStates[2];
  } else {
    --state.RemainingTicks;
    return {};
  }

  --state.RemainingTicks;
  bool ready = pageState == 3U;
  switch (plan.Page) {
  case TopScreenPauseClosePage::Items:
    ready |= pageState == 0xEU;
    break;
  case TopScreenPauseClosePage::Gear:
    ready |= pageState == 0xAU;
    break;
  case TopScreenPauseClosePage::DungeonMap:
    ready |= (pageState & ~2U) == 8U;
    break;
  case TopScreenPauseClosePage::None:
  case TopScreenPauseClosePage::SystemMenu:
    break;
  }
  if (!ready)
    return {};

  state.RemainingTicks = 0U;
  return plan;
}

bool ReadTopScreenPausePageRedrawInputs(NativeA32Memory &memory,
                                        bool runtimeSceneLatch,
                                        TopScreenPausePageRedrawInputs *inputs,
                                        std::string *error) {
  if (inputs == nullptr) {
    if (error != nullptr)
      *error = "missing TopScreen pause page-redraw output";
    return false;
  }
  constexpr std::uint32_t kNativeHealthGate = 0x0058799CU;
  constexpr std::uint32_t kWorldMapControllerState = 0x005093F8U;
  TopScreenPausePageRedrawInputs result;
  std::uint16_t healthGate = 0U;
  if (!ReadTopScreenPauseDrawInputs(memory, &result.Pause, error) ||
      !memory.Read16(kNativeHealthGate, &healthGate) ||
      !memory.Read32(kWorldMapControllerState,
                     &result.WorldMapControllerState)) {
    if (error != nullptr && error->empty())
      *error = "cannot read TopScreen pause page-redraw state";
    return false;
  }
  result.NativeHealthGate = static_cast<std::int16_t>(healthGate);
  result.RuntimeSceneLatch = runtimeSceneLatch;
  *inputs = result;
  return true;
}

bool ResolveTopScreenPausePageRedraw(
    const TopScreenPausePageRedrawInputs &inputs,
    TopScreenPausePageRedrawState *state) noexcept {
  if (state == nullptr)
    return false;

  const auto &pause = inputs.Pause;
  if (IsTopScreenTitleDemoRuntime(pause) || inputs.RuntimeSceneLatch)
    return false;

  const std::uint16_t sceneSequence =
      pause.HasScene && pause.SceneMode == 3U ? pause.SceneSequence : 0U;
  if (sceneSequence != 0U || inputs.NativeHealthGate <= 0) {
    state->DelayCalls = 90U;
    return false;
  }
  if (state->DelayCalls != 0U && --state->DelayCalls != 0U)
    return false;

  // FUN_005C8EEC blocks this path while the native process entry or alternate
  // viewport is active, unless byte +0x65 explicitly bypasses that gate.
  const bool transitionBlocked =
      !pause.SuppressionOverride && (pause.NativeTransitionActive ||
                                     UseTopScreenAlternatePauseViewport(pause));
  return pause.AnyPageGateActive && !transitionBlocked &&
         inputs.WorldMapControllerState == 0U;
}

bool UseTopScreenAlternatePauseViewport(
    const TopScreenPauseDrawInputs &inputs) noexcept {
  return inputs.HasScene && inputs.RuntimeMode == 4U &&
         inputs.AlternatePathReady && inputs.AlternatePathVisible;
}

bool IsTopScreenTitleDemoRuntime(
    const TopScreenPauseDrawInputs &inputs) noexcept {
  // TopScreen 1.2 helper 0x005C8FA0 adds the SaveContext entrance-index gate
  // that was absent in 1.1. This prevents ordinary mode-3 scenes from being
  // routed as the title-screen demo.
  return inputs.HasScene && inputs.SceneMode == 3U &&
         inputs.RuntimeMode >= 1U && inputs.RuntimeMode <= 2U &&
         inputs.EntranceIndex == 0xFFF3U;
}

bool UseTopScreenSceneFiveViewport(
    const TopScreenPauseDrawInputs &inputs) noexcept {
  return inputs.HasScene && inputs.SceneMode == 5U;
}

bool ResolveTopScreenPauseRouteActive(
    const TopScreenPauseDrawInputs &inputs,
    TopScreenPauseRouteState *state) noexcept {
  if (state == nullptr)
    return false;

  const bool startsTransition =
      inputs.RuntimeMode == 0U &&
      (state->PreviousRuntimeMode == 1U || state->PreviousRuntimeMode == 2U ||
       state->PreviousRuntimeMode == 4U);
  if (startsTransition) {
    state->TransitionPhase = 1U;
    state->RemainingCalls = 900U;
  }
  state->PreviousRuntimeMode = inputs.RuntimeMode;

  if (state->TransitionPhase != 0U) {
    if (inputs.HasScene) {
      if (state->TransitionPhase == 1U && inputs.SceneVariant != 2U) {
        state->TransitionPhase = 2U;
      } else if (state->TransitionPhase == 2U && inputs.SceneVariant == 2U) {
        state->TransitionPhase = 0U;
      }
    }
    if (state->TransitionPhase != 0U) {
      if (state->RemainingCalls != 0U)
        --state->RemainingCalls;
      if (state->RemainingCalls != 0U)
        return true;
      state->TransitionPhase = 0U;
    }
  }

  return UseTopScreenSceneFiveViewport(inputs) ||
         UseTopScreenAlternatePauseViewport(inputs);
}

std::uint32_t ResolveTopScreenPauseRoutedCommand(std::uint32_t nativeCommand,
                                                 bool routeActive) noexcept {
  if (!routeActive)
    return nativeCommand;
  if (nativeCommand == 0x400U)
    return 0x401U;
  if (nativeCommand == 0x401U)
    return 0x400U;
  return nativeCommand;
}

bool ResolveTopScreenTouchCoordinateSuppression(
    const TopScreenPauseDrawInputs &inputs, bool routeActive,
    bool pauseCameraControlActive, bool secondaryLayerActive,
    TopScreenTouchCoordinateRouteState *state) noexcept {
  if (state == nullptr)
    return false;

  // Exact source equivalent of payload helper 0x005C8E0C. Mode 3 preserves
  // the previous value; every other inactive scene clears it.
  if (inputs.NativeTransitionActive) {
    state->RuntimeSceneLatch = true;
  } else if (!inputs.HasScene || inputs.SceneMode != 3U) {
    state->RuntimeSceneLatch = false;
  }

  const bool sceneRuntimeRoute = IsTopScreenTitleDemoRuntime(inputs);
  const bool sequenceRoute =
      !state->RuntimeSceneLatch && !inputs.NativeTransitionActive &&
      inputs.HasScene && inputs.SceneMode == 3U && inputs.SceneVariant == 2U &&
      inputs.SceneSequence >= 3U && inputs.SceneSequence <= 6U;
  return routeActive || sceneRuntimeRoute || sequenceRoute ||
         (pauseCameraControlActive && secondaryLayerActive);
}

bool ApplyTopScreenTouchCoordinateSuppression(NativeA32Memory &memory,
                                              std::string *error) {
  constexpr std::uint32_t kTouchState = 0x0050BB00U;
  constexpr std::array<std::pair<std::uint32_t, std::uint16_t>, 6> kWrites{
      {{0x3AU, 0x7FFFU},
       {0x3CU, 0x7FFFU},
       {0x3EU, 0U},
       {0x40U, 0U},
       {0x42U, 0U},
       {0x48U, 0U}}};
  for (const auto &[offset, value] : kWrites) {
    (void)value;
    if (!memory.IsWritable(kTouchState + offset, sizeof(std::uint16_t))) {
      if (error != nullptr)
        *error = "cannot preflight TopScreen touch-coordinate state";
      return false;
    }
  }
  for (const auto &[offset, value] : kWrites) {
    if (!memory.WriteHost<std::uint16_t>(kTouchState + offset, value)) {
      if (error != nullptr)
        *error = "cannot suppress TopScreen touch-coordinate state";
      return false;
    }
  }
  return true;
}

namespace {
bool ReconcileAndHideTopScreenRenderer(NativeA32Memory &memory,
                                       std::uint32_t controller,
                                       bool reconcileTables,
                                       bool *rendererConflict,
                                       std::string *error) {
  if (controller == 0U || rendererConflict == nullptr) {
    SetError(error, "missing TopScreen renderer controller state");
    return false;
  }
  bool conflict = false;
  std::array<std::pair<std::uint32_t, std::uint32_t>, 256> ownerWrites{};
  std::size_t ownerWriteCount = 0U;
  if (reconcileTables) {
    for (std::uint32_t offset = 0xAF8U; offset <= 0xEF4U; offset += 4U) {
      std::uint32_t renderer = 0U;
      if (!memory.Read32(controller + offset, &renderer)) {
        SetError(error, "cannot read TopScreen renderer table");
        return false;
      }
      if (renderer == 0U)
        continue;
      std::uint8_t visible = 0U;
      if (!memory.Read8(renderer + 0x6CU, &visible)) {
        SetError(error, "cannot read TopScreen renderer visibility");
        return false;
      }
      if (visible != 1U)
        continue;
      const std::uint32_t ownerSlot = controller + offset - 0x400U;
      std::uint32_t owner = 0U;
      if (!memory.Read32(ownerSlot, &owner) ||
          !memory.IsWritable(ownerSlot, sizeof(std::uint32_t))) {
        SetError(error, "cannot preflight TopScreen renderer owner table");
        return false;
      }
      if (owner == 0U || owner == renderer) {
        ownerWrites[ownerWriteCount++] = {ownerSlot, renderer};
      } else {
        conflict = true;
      }
    }
  }
  std::uint32_t renderer = 0U;
  if (!memory.Read32(controller + 0x6FCU, &renderer)) {
    SetError(error, "cannot read TopScreen terminal renderer");
    return false;
  }
  if (renderer != 0U && !memory.IsWritable(renderer + 0x6CU, 1U)) {
    SetError(error, "cannot preflight TopScreen terminal renderer");
    return false;
  }
  for (std::size_t index = 0U; index < ownerWriteCount; ++index) {
    const auto [ownerSlot, owner] = ownerWrites[index];
    if (!memory.WriteHost<std::uint32_t>(ownerSlot, owner)) {
      SetError(error, "cannot reconcile TopScreen renderer owner");
      return false;
    }
  }
  if (renderer != 0U && !memory.WriteHost<std::uint8_t>(renderer + 0x6CU, 0U)) {
    SetError(error, "cannot hide TopScreen terminal renderer");
    return false;
  }
  *rendererConflict = conflict;
  return true;
}

bool ApplyTopScreenRendererFade(NativeA32Memory &memory, std::int32_t fadeStep,
                                std::string *error) {
  constexpr std::uint32_t kRendererRoot = 0x005C588CU;
  std::uint32_t owner = 0U;
  std::uint32_t renderer = 0U;
  if (!memory.Read32(kRendererRoot, &owner) || owner == 0U ||
      !memory.Read32(owner + 0x6FCU, &renderer) || renderer == 0U) {
    return true;
  }
  const float alpha = 1.0F - static_cast<float>(fadeStep) / 24.0F;
  const std::uint8_t alphaByte =
      static_cast<std::uint8_t>(static_cast<std::uint32_t>(alpha * 255.0F));
  constexpr std::array<std::uint32_t, 5> kAlphaByteOffsets{0xA0U, 0xA1U, 0xA2U,
                                                           0xA3U, 0xAFU};
  for (const auto offset : kAlphaByteOffsets) {
    if (!memory.IsWritable(renderer + offset, 1U)) {
      SetError(error, "cannot preflight TopScreen renderer byte fade");
      return false;
    }
  }
  std::array<std::uint32_t, 2> colorBlocks{};
  if (!memory.Read32(renderer + 0x64U, &colorBlocks[0]) ||
      !memory.Read32(renderer + 0x68U, &colorBlocks[1])) {
    SetError(error, "cannot read TopScreen renderer color blocks");
    return false;
  }
  for (const auto block : colorBlocks) {
    if (block != 0U && !memory.IsWritable(block + 0xF0U, 16U)) {
      SetError(error, "cannot preflight TopScreen renderer float fade");
      return false;
    }
  }
  for (const auto offset : kAlphaByteOffsets) {
    if (!memory.WriteHost<std::uint8_t>(renderer + offset, alphaByte)) {
      SetError(error, "cannot write TopScreen renderer byte fade");
      return false;
    }
  }
  const auto alphaBits = std::bit_cast<std::uint32_t>(alpha);
  for (const auto block : colorBlocks) {
    if (block == 0U)
      continue;
    for (std::uint32_t component = 0U; component < 4U; ++component) {
      if (!memory.WriteHost<std::uint32_t>(block + 0xF0U + component * 4U,
                                           alphaBits)) {
        SetError(error, "cannot write TopScreen renderer float fade");
        return false;
      }
    }
  }
  return true;
}
} // namespace

bool ResolveTopScreenRendererVisibilityRoute(
    NativeA32Memory &memory, const TopScreenRendererVisibilityInputs &inputs,
    TopScreenRendererVisibilityRouteState *state,
    TopScreenRendererVisibilityAction *action, std::string *error) {
  if (state == nullptr || action == nullptr) {
    SetError(error, "missing TopScreen renderer visibility output");
    return false;
  }
  *action = TopScreenRendererVisibilityAction::NativeDraw;
  if (inputs.ControllerAddress == 0U || !inputs.HasCallScene)
    return true;

  if (inputs.CallSceneMode == 7U) {
    if (inputs.CallSceneSubmode != 4U) {
      state->RendererConflict = false;
      state->FadeStep = 0;
      state->DelayCalls = 0;
    } else if (!ReconcileAndHideTopScreenRenderer(
                   memory, inputs.ControllerAddress, true,
                   &state->RendererConflict, error)) {
      return false;
    }
    *action = TopScreenRendererVisibilityAction::SuppressNativeDraw;
    return true;
  }
  if (inputs.CallSceneMode != 3U)
    return true;

  if (inputs.GlobalSceneRuntimeRoute || inputs.RuntimeSceneLatch ||
      inputs.NativeRuntimeActive ||
      (inputs.CallSceneSequence == 0U && inputs.NativeFadeGate > 0)) {
    state->FadeStep = 0;
    state->DelayCalls = 0;
    return true;
  }
  state->RendererConflict = false;
  if (inputs.CallSceneSequence <= 3U) {
    state->FadeStep = 0;
    state->DelayCalls = 0;
    return true;
  }
  if (state->FadeStep <= 23) {
    if (state->DelayCalls <= 59) {
      ++state->DelayCalls;
      return true;
    }
    ++state->FadeStep;
    return ApplyTopScreenRendererFade(memory, state->FadeStep, error);
  }
  const bool reconcile =
      inputs.CallSceneSequence >= 4U && inputs.CallSceneSequence <= 7U;
  if (!ReconcileAndHideTopScreenRenderer(memory, inputs.ControllerAddress,
                                         reconcile, &state->RendererConflict,
                                         error)) {
    return false;
  }
  *action = TopScreenRendererVisibilityAction::SuppressNativeDraw;
  return true;
}

bool ReadTopScreenPauseControllerInputs(NativeA32Memory &memory,
                                        TopScreenPauseControllerInputs *inputs,
                                        std::string *error) {
  if (inputs == nullptr) {
    SetError(error, "missing TopScreen pause-controller inputs");
    return false;
  }
  TopScreenPauseControllerInputs result;
  if (!ReadTopScreenPauseDrawInputs(memory, &result.Pause, error) ||
      !memory.Read32(0x0050BB50U, &result.RendererAddress)) {
    SetError(error, "cannot read TopScreen pause-controller state");
    return false;
  }
  if (result.RendererAddress != 0U &&
      !memory.Read8(result.RendererAddress + 4U, &result.RendererPhase)) {
    SetError(error, "cannot read TopScreen pause renderer phase");
    return false;
  }
  *inputs = result;
  return true;
}

namespace {
bool ApplyTopScreenPauseControllerFade(NativeA32Memory &memory,
                                       std::uint32_t renderer,
                                       std::int32_t fadeStep,
                                       std::string *error) {
  constexpr std::array<std::uint16_t, 14> kAlphaOffsets{
      0x0054U, 0x0090U, 0x00CCU, 0x0108U, 0x0234U, 0x0270U, 0x02ACU,
      0x02E8U, 0x0324U, 0x0360U, 0x039CU, 0x03D8U, 0x0414U, 0x0450U};
  constexpr std::array<std::uint32_t, 2> kColorBlockPointers{0x05C4U, 0x05C8U};
  if (renderer == 0U)
    return true;
  for (const auto offset : kAlphaOffsets) {
    if (!memory.IsWritable(renderer + offset, sizeof(std::uint32_t))) {
      SetError(error, "cannot preflight TopScreen controller fade field");
      return false;
    }
  }
  std::array<std::uint32_t, 2> colorBlocks{};
  for (std::size_t index = 0U; index < kColorBlockPointers.size(); ++index) {
    if (!memory.Read32(renderer + kColorBlockPointers[index],
                       &colorBlocks[index]) ||
        (colorBlocks[index] != 0U &&
         !memory.IsWritable(colorBlocks[index] + 0xFCU,
                            sizeof(std::uint32_t)))) {
      SetError(error, "cannot preflight TopScreen controller color fade");
      return false;
    }
  }
  const auto alphaBits =
      std::bit_cast<std::uint32_t>(static_cast<float>(fadeStep) * 0.05F);
  for (const auto offset : kAlphaOffsets) {
    if (!memory.WriteHost<std::uint32_t>(renderer + offset, alphaBits)) {
      SetError(error, "cannot write TopScreen controller fade field");
      return false;
    }
  }
  for (const auto colorBlock : colorBlocks) {
    if (colorBlock != 0U &&
        !memory.WriteHost<std::uint32_t>(colorBlock + 0xFCU, alphaBits)) {
      SetError(error, "cannot write TopScreen controller color fade");
      return false;
    }
  }
  return true;
}
} // namespace

bool ResolveTopScreenPauseController(
    NativeA32Memory &memory, const TopScreenPauseControllerInputs &inputs,
    bool routeActive, TopScreenPauseControllerState *state,
    TopScreenPauseControllerAction *action, std::string *error) {
  if (state == nullptr || action == nullptr) {
    SetError(error, "missing TopScreen pause-controller output");
    return false;
  }
  if (!routeActive) {
    state->RoutedRendererLatched = false;
    state->RoutedFadeStep = 0;
  } else if (!state->RoutedRendererLatched && inputs.RendererAddress != 0U &&
             inputs.RendererPhase == 8U) {
    // Producer 0x005CE57C owns this transition in the binary mod. Observing
    // the same native phase here avoids recreating its payload-global latch.
    state->RoutedRendererLatched = true;
  }

  if (inputs.Pause.HasScene && inputs.Pause.SceneMode == 7U) {
    *action = TopScreenPauseControllerAction::SuppressNativeDraw;
    return true;
  }
  if (!routeActive) {
    *action =
        UseTopScreenAlternatePauseViewport(inputs.Pause)
            ? TopScreenPauseControllerAction::NativeDrawThenAlternateOverlay
            : TopScreenPauseControllerAction::NativeDraw;
    return true;
  }
  if (inputs.RendererAddress == 0U) {
    *action = TopScreenPauseControllerAction::SuppressNativeDraw;
    return true;
  }
  if (state->RoutedRendererLatched) {
    if (state->RoutedFadeStep <= 19) {
      ++state->RoutedFadeStep;
      if (!ApplyTopScreenPauseControllerFade(memory, inputs.RendererAddress,
                                             state->RoutedFadeStep, error)) {
        return false;
      }
    }
    *action = TopScreenPauseControllerAction::SuppressNativeDraw;
    return true;
  }
  *action = inputs.RendererPhase >= 1U && inputs.RendererPhase <= 2U
                ? TopScreenPauseControllerAction::NativeDraw
                : TopScreenPauseControllerAction::SuppressNativeDraw;
  return true;
}

bool PrepareTopScreenPauseAlternateRenderers(
    NativeA32Memory &memory, TopScreenPauseControllerState *state,
    std::array<std::uint32_t, 2> *visibleRenderers, std::string *error) {
  if (state == nullptr || visibleRenderers == nullptr) {
    SetError(error, "missing TopScreen alternate-renderer output");
    return false;
  }
  constexpr std::array<std::uint32_t, 2> kRendererSlots{0x005C4CD8U,
                                                        0x005C4CE0U};
  constexpr std::array<float, 2> kXOffsets{45.0F, -25.0F};
  std::array<std::uint32_t, 2> renderers{};
  for (std::size_t index = 0U; index < renderers.size(); ++index) {
    if (!memory.Read32(kRendererSlots[index], &renderers[index])) {
      SetError(error, "cannot read TopScreen alternate renderer");
      return false;
    }
  }
  if (!state->AlternateRendererPositionsCaptured ||
      state->AlternateRendererAddresses != renderers) {
    std::array<float, 2> originalX{};
    std::array<float, 2> originalY{};
    for (std::size_t index = 0U; index < renderers.size(); ++index) {
      if (renderers[index] == 0U)
        continue;
      std::uint32_t xBits = 0U;
      std::uint32_t yBits = 0U;
      if (!memory.Read32(renderers[index] + 0x80U, &xBits) ||
          !memory.Read32(renderers[index] + 0x84U, &yBits)) {
        SetError(error, "cannot capture TopScreen alternate renderer position");
        return false;
      }
      originalX[index] = std::bit_cast<float>(xBits);
      originalY[index] = std::bit_cast<float>(yBits);
    }
    state->AlternateRendererAddresses = renderers;
    state->AlternateRendererOriginalX = originalX;
    state->AlternateRendererOriginalY = originalY;
    state->AlternateRendererPositionsCaptured = true;
  }
  for (const auto renderer : renderers) {
    if (renderer != 0U &&
        (!memory.IsWritable(renderer + 0x80U, sizeof(std::uint32_t)) ||
         !memory.IsWritable(renderer + 0x84U, sizeof(std::uint32_t)))) {
      SetError(error, "cannot preflight TopScreen alternate renderer position");
      return false;
    }
  }
  *visibleRenderers = {};
  for (std::size_t index = 0U; index < renderers.size(); ++index) {
    const auto renderer = renderers[index];
    if (renderer == 0U)
      continue;
    const auto xBits = std::bit_cast<std::uint32_t>(
        state->AlternateRendererOriginalX[index] + kXOffsets[index]);
    const auto yBits = std::bit_cast<std::uint32_t>(
        state->AlternateRendererOriginalY[index] - 6.0F);
    std::uint8_t visible = 0U;
    if (!memory.WriteHost<std::uint32_t>(renderer + 0x80U, xBits) ||
        !memory.WriteHost<std::uint32_t>(renderer + 0x84U, yBits) ||
        !memory.Read8(renderer + 0x6CU, &visible)) {
      SetError(error, "cannot prepare TopScreen alternate renderer");
      return false;
    }
    if (visible != 0U)
      (*visibleRenderers)[index] = renderer;
  }
  return true;
}

bool BeginTopScreenAlternateRendererSuppression(
    NativeA32Memory &memory, TopScreenAlternateRendererState *saved,
    std::string *error) {
  if (saved == nullptr) {
    if (error != nullptr)
      *error = "missing alternate renderer-state output";
    return false;
  }
  TopScreenAlternateRendererState snapshot;
  std::array<std::uint32_t, 4> renderers{};
  for (std::size_t index = 0; index < renderers.size(); ++index) {
    if (!memory.Read32(kTopScreenAlternateRendererPointers[index],
                       &renderers[index])) {
      if (error != nullptr)
        *error = "cannot read alternate renderer pointer";
      return false;
    }
    if (renderers[index] != 0U &&
        (!memory.Read8(renderers[index] + 0x6CU, &snapshot.Visibility[index]) ||
         !memory.IsWritable(renderers[index] + 0x6CU, 1U))) {
      if (error != nullptr)
        *error = "cannot preflight alternate renderer";
      return false;
    }
  }
  for (const auto renderer : renderers) {
    if (renderer != 0U && !memory.Write8(renderer + 0x6CU, 0U)) {
      if (error != nullptr)
        *error = "cannot suppress alternate renderer";
      return false;
    }
  }
  *saved = snapshot;
  return true;
}

bool EndTopScreenAlternateRendererSuppression(
    NativeA32Memory &memory, const TopScreenAlternateRendererState &saved,
    std::string *error) {
  std::array<std::uint32_t, 4> renderers{};
  for (std::size_t index = 0; index < renderers.size(); ++index) {
    if (!memory.Read32(kTopScreenAlternateRendererPointers[index],
                       &renderers[index]) ||
        (renderers[index] != 0U &&
         !memory.IsWritable(renderers[index] + 0x6CU, 1U))) {
      if (error != nullptr)
        *error = "cannot preflight alternate renderer restore";
      return false;
    }
  }
  for (std::size_t index = 0; index < renderers.size(); ++index) {
    if (renderers[index] != 0U &&
        !memory.Write8(renderers[index] + 0x6CU, saved.Visibility[index])) {
      if (error != nullptr)
        *error = "cannot restore alternate renderer";
      return false;
    }
  }
  return true;
}

TopScreenPauseDrawAction ResolveTopScreenPauseDrawAction(
    const TopScreenPauseDrawInputs &inputs,
    TopScreenPauseDrawRoutingState *state) noexcept {
  if (state == nullptr)
    return TopScreenPauseDrawAction::NativeDraw;
  if (inputs.NativeTransitionActive) {
    state->NativeTransitionLatched = true;
  } else if (!inputs.HasScene || inputs.SceneMode != 3U) {
    state->NativeTransitionLatched = false;
  }
  const bool alternateRuntime = IsTopScreenTitleDemoRuntime(inputs);
  const bool alternateSequence =
      !state->NativeTransitionLatched && inputs.HasScene &&
      inputs.SceneMode == 3U && inputs.SceneVariant == 2U &&
      inputs.SceneSequence >= 3U && inputs.SceneSequence <= 6U;
  const bool alternateReady = inputs.HasScene && inputs.RuntimeMode == 4U &&
                              inputs.AlternatePathReady &&
                              inputs.AlternatePathVisible;
  if (alternateRuntime || alternateSequence || alternateReady ||
      state->NativeTransitionLatched || !inputs.AnyPageGateActive) {
    return TopScreenPauseDrawAction::NativeDraw;
  }
  const bool suppressionBlocked =
      state->SuppressionDelayCommands != 0U ||
      (!inputs.SuppressionOverride && inputs.NativeTransitionActive);
  return suppressionBlocked ? TopScreenPauseDrawAction::NativeDraw
                            : TopScreenPauseDrawAction::SuppressNativeChildren;
}

void ObserveTopScreenPauseTargetCommand(
    const TopScreenPauseDrawInputs &inputs, std::uint32_t nativeCommand,
    TopScreenPauseDrawRoutingState *state) noexcept {
  if (state == nullptr || nativeCommand != 0x400U)
    return;
  if (inputs.NativeTransitionActive) {
    if (inputs.SuppressionOverride) {
      state->SuppressionDelayArmed = true;
    } else if (state->SuppressionDelayArmed) {
      state->SuppressionDelayCommands = 180U;
    }
  } else {
    state->SuppressionDelayArmed = false;
  }
  if (state->SuppressionDelayCommands != 0U) {
    if (inputs.HasScene && inputs.RuntimeMode == 4U &&
        inputs.AlternatePathReady && inputs.AlternatePathVisible) {
      state->SuppressionDelayCommands = 0U;
    } else {
      --state->SuppressionDelayCommands;
    }
  }
}

bool ResolveTopScreenPauseIconBuild(TopScreenPauseIconBuild *build,
                                    const TopScreenUiConfig &config) noexcept {
  if (build == nullptr || build->Argument3 != 0xC5U)
    return false;

  const bool restoration =
      config.HudLayout == TopScreenHudLayout::Restoration;
  const float baseX = restoration ? 358.0F : 248.0F;
  const float baseY = restoration ? 68.0F : 24.0F;
  const float scale = config.HudScale;
  const float insetX = static_cast<float>(config.HudMarginX);
  const float insetY = static_cast<float>(config.HudMarginY);
  const auto size = static_cast<std::uint32_t>(std::lround(scale * 28.0F));
  const auto x = static_cast<std::uint32_t>(std::lround(
      400.0F + (baseX - 400.0F) * scale - insetX - scale * 14.0F));
  const auto y = static_cast<std::uint32_t>(
      std::lround(insetY + baseY * scale - scale * 14.0F));
  build->Argument2 = x;
  build->Argument3 = y;
  build->StackArgument0 = size;
  build->StackArgument1 = size;
  return true;
}

bool SuppressTopScreenTouchButtonDraw(std::uint32_t pauseState) noexcept {
  return pauseState >= 12U && pauseState <= 19U;
}

std::span<const TopScreenControlFlowContract>
TopScreenVerifiedControlFlowContracts() noexcept {
  return kVerifiedControlFlowContracts;
}

bool ResolveTopScreenControlFlow(std::uint32_t entry,
                                 std::uint32_t *target) noexcept {
  const auto contract =
      std::find_if(kVerifiedControlFlowContracts.begin(),
                   kVerifiedControlFlowContracts.end(),
                   [entry](const TopScreenControlFlowContract &candidate) {
                     return candidate.Entry == entry;
                   });
  if (contract == kVerifiedControlFlowContracts.end()) {
    return false;
  }
  if (target != nullptr) {
    *target = contract->Target;
  }
  return true;
}

std::span<const TopScreenFloatLoadContract>
TopScreenVerifiedFloatLoadContracts() noexcept {
  return kVerifiedFloatLoadContracts;
}

bool ResolveTopScreenFloatLoad(std::uint32_t entry, std::uint8_t *vfpLane,
                               std::uint32_t *valueBits) noexcept {
  const auto contract = std::find_if(
      kVerifiedFloatLoadContracts.begin(), kVerifiedFloatLoadContracts.end(),
      [entry](const TopScreenFloatLoadContract &candidate) {
        return candidate.Entry == entry;
      });
  if (contract == kVerifiedFloatLoadContracts.end()) {
    return false;
  }
  if (vfpLane != nullptr) {
    *vfpLane = contract->VfpLane;
  }
  if (valueBits != nullptr) {
    *valueBits = contract->ValueBits;
  }
  return true;
}

bool ApplyTopScreenTitleLogoFadeHold(NativeA32Memory &memory,
                                     std::uint32_t actorAddress,
                                     std::uint32_t actorStateAddress,
                                     std::uint32_t clampedAlphaBits,
                                     TopScreenTitleLogoFadeHoldResult *result,
                                     std::string *error) {
  constexpr std::uint32_t kAnimationOwnerLiteral = 0x001DAD08U;
  constexpr std::uint32_t kTitleAlpha = 0x1D0U;
  constexpr std::uint32_t kEffectAlpha = 0x1DCU;
  constexpr std::uint32_t kFadeSubstate = 0xC4U;
  constexpr std::uint32_t kFadeTimer = 0xC6U;

  if (result == nullptr) {
    if (error != nullptr)
      *error = "missing title-logo fade-hold output";
    return false;
  }

  std::uint32_t animationOwnerPointer = 0U;
  std::uint32_t animationOwner = 0U;
  if (!memory.Read32(kAnimationOwnerLiteral, &animationOwnerPointer) ||
      animationOwnerPointer == 0U ||
      !memory.Read32(animationOwnerPointer, &animationOwner) ||
      !memory.IsWritable(actorAddress + kTitleAlpha, sizeof(std::uint32_t)) ||
      !memory.IsWritable(actorAddress + kEffectAlpha, sizeof(std::uint32_t)) ||
      !memory.IsWritable(actorStateAddress + kFadeSubstate,
                         sizeof(std::uint16_t)) ||
      !memory.IsWritable(actorStateAddress + kFadeTimer,
                         sizeof(std::uint16_t))) {
    if (error != nullptr)
      *error = "cannot preflight title-logo fade-hold state";
    return false;
  }

  if (!memory.Write32(actorAddress + kTitleAlpha, clampedAlphaBits) ||
      !memory.Write32(actorAddress + kEffectAlpha, clampedAlphaBits) ||
      !memory.Write16(actorStateAddress + kFadeSubstate, 3U) ||
      !memory.Write16(actorStateAddress + kFadeTimer, 1U)) {
    if (error != nullptr)
      *error = "cannot apply title-logo fade-hold state";
    return false;
  }

  *result = {animationOwner, 1U, 0x001DAFF8U};
  return true;
}

TopScreenCameraNormal1Scalar
ResolveTopScreenCameraNormal1Scalar(std::uint32_t nativeValueBits,
                                    std::uint8_t zoomPercent) noexcept {
  TopScreenCameraNormal1Scalar result{nativeValueBits, nativeValueBits};
  if (zoomPercent != 100U) {
    result.ContinuedValueBits = std::bit_cast<std::uint32_t>(
        std::bit_cast<float>(nativeValueBits) *
        (static_cast<float>(zoomPercent) * 0.01F));
  }
  return result;
}

TopScreenItemsSelectionPlan ResolveTopScreenItemsSelectionBegin(
    const TopScreenItemsSelectionBegin &input) noexcept {
  if (input.NativePageState != 2U) {
    return {};
  }
  if (input.ZrPressed) {
    return {true, 5U};
  }
  if (input.ZlPressed) {
    return {true, 0x17U};
  }
  return {};
}

bool CompleteTopScreenItemsSelection(std::uint32_t pendingSelection,
                                     std::uint32_t nativeSelectionState,
                                     std::uint32_t *selection,
                                     std::uint16_t *cursorY) noexcept {
  if (pendingSelection == 0U || nativeSelectionState != 0x0BU) {
    return false;
  }
  if (selection != nullptr) {
    *selection = pendingSelection;
  }
  if (cursorY != nullptr) {
    *cursorY = pendingSelection == 5U ? 7U : 0xBFU;
  }
  return true;
}

bool BeginTopScreenPauseChildSuppression(NativeA32Memory &memory,
                                         TopScreenPauseChildSuppressionSet set,
                                         TopScreenPauseChildState *saved,
                                         std::string *error) {
  if (saved == nullptr) {
    if (error != nullptr)
      *error = "missing pause child-state output";
    return false;
  }
  TopScreenPauseChildState snapshot;
  switch (set) {
  case TopScreenPauseChildSuppressionSet::Tail:
    snapshot.SuppressedMask = kTopScreenPauseTailSuppressionMask;
    break;
  case TopScreenPauseChildSuppressionSet::AuxiliaryRedraw:
    snapshot.SuppressedMask = kTopScreenPauseAuxiliarySuppressionMask;
    break;
  case TopScreenPauseChildSuppressionSet::PageRedraw:
    snapshot.SuppressedMask = kTopScreenPausePageRedrawSuppressionMask;
    break;
  }
  for (std::size_t index = 0; index < kTopScreenPauseChildStateAddresses.size();
       ++index) {
    const auto address = kTopScreenPauseChildStateAddresses[index];
    if (!memory.Read32(address, &snapshot.Values[index]) ||
        ((snapshot.SuppressedMask & (1U << index)) != 0U &&
         !memory.IsWritable(address, sizeof(std::uint32_t)))) {
      if (error != nullptr)
        *error = "cannot preflight pause child state";
      return false;
    }
  }
  for (std::size_t index = 0; index < kTopScreenPauseChildStateAddresses.size();
       ++index) {
    if ((snapshot.SuppressedMask & (1U << index)) == 0U)
      continue;
    if (!memory.Write32(kTopScreenPauseChildStateAddresses[index], 0U)) {
      if (error != nullptr)
        *error = "cannot suppress pause child state";
      return false;
    }
  }
  *saved = snapshot;
  return true;
}

bool EndTopScreenPauseChildSuppression(NativeA32Memory &memory,
                                       const TopScreenPauseChildState &saved,
                                       std::string *error) {
  for (std::size_t index = 0; index < kTopScreenPauseChildStateAddresses.size();
       ++index) {
    if ((saved.SuppressedMask & (1U << index)) != 0U &&
        !memory.IsWritable(kTopScreenPauseChildStateAddresses[index],
                           sizeof(std::uint32_t))) {
      if (error != nullptr)
        *error = "cannot preflight pause child restore";
      return false;
    }
  }
  for (std::size_t index = 0; index < kTopScreenPauseChildStateAddresses.size();
       ++index) {
    if ((saved.SuppressedMask & (1U << index)) == 0U)
      continue;
    if (!memory.Write32(kTopScreenPauseChildStateAddresses[index],
                        saved.Values[index])) {
      if (error != nullptr)
        *error = "cannot restore pause child state";
      return false;
    }
  }
  return true;
}

} // namespace Oot3dNativeGame
