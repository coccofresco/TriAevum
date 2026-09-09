#pragma once

#include "oot3d_ui/ui_content_value.h"
#include "oot3d_ui/ui_semantics.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

inline constexpr std::array<EquipmentType, kEquipmentTypeValueCount> kUiEquipmentTypes = {
    EquipmentType::EQUIP_TYPE_SWORD,
    EquipmentType::EQUIP_TYPE_SHIELD,
    EquipmentType::EQUIP_TYPE_TUNIC,
    EquipmentType::EQUIP_TYPE_BOOTS,
};

inline constexpr std::array<UpgradeType, kUpgradeTypeValueCount> kUiUpgradeTypes = {
    UpgradeType::UPG_QUIVER,
    UpgradeType::UPG_BOMB_BAG,
    UpgradeType::UPG_STRENGTH,
    UpgradeType::UPG_SCALE,
    UpgradeType::UPG_WALLET,
    UpgradeType::UPG_BULLET_BAG,
    UpgradeType::UPG_DEKU_STICKS,
    UpgradeType::UPG_DEKU_NUTS,
};

inline constexpr std::array<QuestItem, kQuestItemValueCount> kUiQuestItems = {
    QuestItem::QUEST_MEDALLION_FOREST,
    QuestItem::QUEST_MEDALLION_FIRE,
    QuestItem::QUEST_MEDALLION_WATER,
    QuestItem::QUEST_MEDALLION_SPIRIT,
    QuestItem::QUEST_MEDALLION_SHADOW,
    QuestItem::QUEST_MEDALLION_LIGHT,
    QuestItem::QUEST_SONG_MINUET,
    QuestItem::QUEST_SONG_BOLERO,
    QuestItem::QUEST_SONG_SERENADE,
    QuestItem::QUEST_SONG_REQUIEM,
    QuestItem::QUEST_SONG_NOCTURNE,
    QuestItem::QUEST_SONG_PRELUDE,
    QuestItem::QUEST_SONG_LULLABY,
    QuestItem::QUEST_SONG_EPONA,
    QuestItem::QUEST_SONG_SARIA,
    QuestItem::QUEST_SONG_SUN,
    QuestItem::QUEST_SONG_TIME,
    QuestItem::QUEST_SONG_STORMS,
    QuestItem::QUEST_KOKIRI_EMERALD,
    QuestItem::QUEST_GORON_RUBY,
    QuestItem::QUEST_ZORA_SAPPHIRE,
    QuestItem::QUEST_STONE_OF_AGONY,
    QuestItem::QUEST_GERUDOS_CARD,
    QuestItem::QUEST_SKULL_TOKEN,
    QuestItem::QUEST_HEART_PIECE,
};

inline constexpr std::array<DungeonItem, kDungeonItemValueCount> kUiDungeonItems = {
    DungeonItem::DUNGEON_BOSS_KEY,
    DungeonItem::DUNGEON_COMPASS,
    DungeonItem::DUNGEON_MAP,
};

struct UiEquipmentCategoryContent {
    EquipmentType category = EquipmentType::EQUIP_TYPE_SWORD;
    UiContentValue<std::uint8_t> equipped_value;
    UiContentValue<std::uint8_t> owned_value_mask;
};

struct UiUpgradeContent {
    UpgradeType upgrade = UpgradeType::UPG_QUIVER;
    UiContentValue<std::uint8_t> level;
    UiContentValue<std::uint16_t> capacity;
};

struct UiQuestItemContent {
    QuestItem item = QuestItem::QUEST_MEDALLION_FOREST;
    UiContentValue<bool> owned;
};

struct UiDungeonItemContent {
    DungeonItem item = DungeonItem::DUNGEON_BOSS_KEY;
    UiContentValue<bool> owned;
};

struct UiPauseItemCellContent {
    PauseItemGridPosition position;
    UiContentValue<InventorySlot> inventory_slot;
    UiContentValue<ItemId> item_id;
    UiContentValue<std::int8_t> ammo;
};

using UiEquipmentContentArray =
    std::array<UiEquipmentCategoryContent, kEquipmentTypeValueCount>;
using UiUpgradeContentArray = std::array<UiUpgradeContent, kUpgradeTypeValueCount>;
using UiQuestContentArray = std::array<UiQuestItemContent, kQuestItemValueCount>;
using UiDungeonContentArray =
    std::array<UiDungeonItemContent, kDungeonItemValueCount>;
using UiDungeonContentTable =
    std::array<UiDungeonContentArray, kOot3dDungeonItemCount>;
using UiPauseItemGridContent =
    std::array<UiPauseItemCellContent, kPauseItemGridPositionCount>;

struct UiInventoryContentSnapshot {
    UiEquipmentContentArray equipment;
    UiUpgradeContentArray upgrades;
    UiQuestContentArray quest_items;
    UiDungeonContentTable dungeon_items;
    UiPauseItemGridContent active_item_grid;
};

// Safe host-side equivalent of the native gItemSlots lookup.  Inputs outside
// the exact 56-entry OoT3D table are explicitly non-applicable.
UiContentValue<InventorySlot> ResolveOot3dInventorySlotForItem(
    ItemId item_id) noexcept;

// Builds a semantic content model from the existing read-only snapshot.  It
// neither reads guest memory nor mutates the native SaveContext/controllers.
UiInventoryContentSnapshot BuildOot3dUiInventoryContent(
    const Oot3dUiSemanticState& state) noexcept;

static_assert(kUiEquipmentTypes.size() == 4);
static_assert(kUiUpgradeTypes.size() == 8);
static_assert(kUiQuestItems.size() == 25);
static_assert(kUiDungeonItems.size() == 3);

} // namespace oot3d::ui
