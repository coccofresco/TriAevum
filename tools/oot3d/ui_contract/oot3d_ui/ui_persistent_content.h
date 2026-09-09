#pragma once

#include "oot3d_ui/ui_content_value.h"
#include "oot3d_ui/ui_semantics.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

template <typename T, std::size_t Size>
using UiPersistentContentArray = std::array<UiContentValue<T>, Size>;

struct UiPlayerNameContent {
    UiPersistentContentArray<std::int16_t, kOot3dPlayerNameLength> raw_code_units;
    UiContentValue<std::uint8_t> stored_length;
    UiContentValue<std::uint8_t> validated_length;
    UiPersistentContentArray<std::uint16_t, kOot3dPlayerNameLength> active_code_units;
};

struct UiSaveIdentityContent {
    UiContentValue<std::int32_t> entrance_index;
    UiContentValue<std::int32_t> cutscene_index;
    UiContentValue<std::uint16_t> day_time;
    UiContentValue<std::int32_t> night_flag;
    UiPlayerNameContent player_name;
    UiContentValue<std::uint8_t> z_targeting_setting;
    UiContentValue<std::uint16_t> checksum;
    UiContentValue<std::int32_t> file_num;
    UiContentValue<std::int32_t> game_mode;
};

struct UiPersistentPlayerContent {
    UiContentValue<LinkAge> link_age;
    UiContentValue<bool> master_quest;
    UiContentValue<std::uint16_t> sword_health;
    UiContentValue<std::uint16_t> navi_timer;
    UiContentValue<std::int8_t> biggoron_sword_flag;
    UiContentValue<std::int8_t> defense_hearts;
};

struct UiSavedLoadoutContent {
    UiPersistentContentArray<ItemId, kOot3dButtonItemCount> button_items;
    UiPersistentContentArray<InventorySlot, kOot3dAssignableButtonCount> button_slots;
    UiContentValue<std::uint16_t> equipment;
};

struct UiInventoryCacheContent {
    UiPersistentContentArray<ItemId, kOot3dInventoryItemCount> item_ids;
};

struct UiQuestProgressContent {
    UiPersistentContentArray<std::uint8_t, kOot3dGsFlagCount> gold_skulltula_flags;
    UiPersistentContentArray<std::uint16_t, kOot3dEventCheckCount> event_check_info;
    UiPersistentContentArray<std::uint16_t, kOot3dItemGetInfoCount> item_get_info;
    UiPersistentContentArray<std::uint16_t, kOot3dInfoTableCount> info_table;
    UiContentValue<std::uint32_t> world_map_area_data;
    UiPersistentContentArray<std::uint32_t, kOot3dBossChallengeCount> boss_battle_victories;
    UiPersistentContentArray<std::uint32_t, kOot3dBossChallengeCount> boss_battle_scores;
};

struct UiPersistentContentSnapshot {
    UiSaveIdentityContent identity;
    UiPersistentPlayerContent player;
    UiSavedLoadoutContent child_loadout;
    UiSavedLoadoutContent adult_loadout;
    UiSavedLoadoutContent current_loadout;
    UiInventoryCacheContent inventory_cache;
    UiQuestProgressContent quest_progress;
};

// Pure read-only projection of persistent and SaveContext-backed content not
// already represented by the HUD, inventory, or pause projections.
UiPersistentContentSnapshot BuildOot3dUiPersistentContent(
    const Oot3dUiSemanticState& state) noexcept;

static_assert(kOot3dPlayerNameLength == 8);
static_assert(kOot3dButtonItemCount == 5);
static_assert(kOot3dAssignableButtonCount == 4);
static_assert(kOot3dInventoryItemCount == 26);
static_assert(kOot3dBossChallengeCount == 9);

} // namespace oot3d::ui
