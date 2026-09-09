#include "oot3d_ui/ui_inventory_content.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

namespace {

// Exact OoT3D tables at 0x0053CB08-0x0053CC7B.  N64 sources organize the
// shared packed fields, but these values and the extended item mapping are
// pinned from the original 3DS binary.
inline constexpr std::array<std::uint8_t, 4> kEquipmentShifts = {0, 4, 8, 12};
inline constexpr std::array<std::uint16_t, 4> kEquipmentMasks = {
    0x000F, 0x00F0, 0x0F00, 0xF000,
};
inline constexpr std::array<std::uint8_t, 8> kUpgradeShifts = {
    0, 3, 6, 9, 12, 14, 17, 20,
};
inline constexpr std::array<std::uint32_t, 8> kUpgradeMasks = {
    0x00000007, 0x00000038, 0x000001C0, 0x00000E00,
    0x00003000, 0x0001C000, 0x000E0000, 0x00700000,
};
inline constexpr std::array<std::array<std::uint16_t, 4>, 8> kUpgradeCapacities = {{
    {{0, 30, 40, 50}},
    {{0, 20, 30, 40}},
    {{0, 0, 0, 0}},
    {{0, 0, 0, 0}},
    {{99, 200, 500, 500}},
    {{0, 30, 40, 50}},
    {{0, 10, 20, 30}},
    {{0, 20, 30, 40}},
}};
inline constexpr std::array<std::uint8_t, kOot3dItemSlotMappingCount>
    kItemToInventorySlot = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x07, 0x08, 0x09, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
        0x0E, 0x0F, 0x10, 0x11, 0x12, 0x12, 0x12, 0x12,
        0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
        0x12, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17,
        0x17, 0x17, 0x17, 0x17, 0x17, 0x16, 0x16, 0x16,
        0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16,
    };

constexpr std::size_t EquipmentIndex(EquipmentType type) noexcept {
    return static_cast<std::size_t>(UiValueRaw(type));
}

constexpr std::size_t UpgradeIndex(UpgradeType type) noexcept {
    return static_cast<std::size_t>(UiValueRaw(type));
}

template <typename T>
UiContentValue<T> FromSemanticValue(const SemanticValue<T>& source) noexcept {
    return source.known ? KnownUiContentValue(source.value) : UiContentValue<T>{};
}

UiContentValue<std::uint8_t> ResolveEquipmentField(
    const SemanticValue<std::uint16_t>& packed, EquipmentType type) noexcept {
    if (!packed.known) {
        return {};
    }
    const std::size_t index = EquipmentIndex(type);
    return KnownUiContentValue(static_cast<std::uint8_t>(
        (packed.value & kEquipmentMasks[index]) >> kEquipmentShifts[index]));
}

UiUpgradeContent ResolveUpgrade(const InventoryState& inventory,
                                UpgradeType upgrade) noexcept {
    UiUpgradeContent result;
    result.upgrade = upgrade;
    if (!inventory.upgrades.known) {
        return result;
    }
    const std::size_t index = UpgradeIndex(upgrade);
    const std::uint8_t level = static_cast<std::uint8_t>(
        (inventory.upgrades.value & kUpgradeMasks[index]) >> kUpgradeShifts[index]);
    result.level = KnownUiContentValue(level);
    result.capacity = level < kUpgradeCapacities[index].size()
                          ? KnownUiContentValue(kUpgradeCapacities[index][level])
                          : NotApplicableUiContentValue<std::uint16_t>();
    return result;
}

UiContentValue<bool> ResolveBit(const SemanticValue<std::uint32_t>& packed,
                                std::uint8_t bit) noexcept {
    return packed.known
               ? KnownUiContentValue((packed.value & (std::uint32_t{1} << bit)) != 0)
               : UiContentValue<bool>{};
}

UiContentValue<bool> ResolveBit(const SemanticValue<std::uint8_t>& packed,
                                std::uint8_t bit) noexcept {
    return packed.known
               ? KnownUiContentValue((packed.value & (std::uint8_t{1} << bit)) != 0)
               : UiContentValue<bool>{};
}

const SemanticArray<InventorySlot, kOot3dItemMenuSlotCount>* ActiveItemGrid(
    const Oot3dUiSemanticState& state) noexcept {
    if (!state.link_age.known) {
        return nullptr;
    }
    switch (state.link_age.value) {
    case LinkAge::Adult:
        return &state.pause.adult_item_menu_slots;
    case LinkAge::Child:
        return &state.pause.child_item_menu_slots;
    }
    return nullptr;
}

UiPauseItemCellContent ResolveItemCell(
    const Oot3dUiSemanticState& state,
    const SemanticArray<InventorySlot, kOot3dItemMenuSlotCount>* grid,
    std::size_t index) noexcept {
    UiPauseItemCellContent result;
    result.position = PauseItemGridPosition::FromIndex(static_cast<std::uint32_t>(index));
    if (grid == nullptr || !(*grid)[index].known) {
        return result;
    }

    const InventorySlot slot = (*grid)[index].value;
    result.inventory_slot = KnownUiContentValue(slot);
    const std::size_t slot_index = static_cast<std::size_t>(UiValueRaw(slot));
    if (slot_index >= state.inventory.items.size()) {
        result.item_id = NotApplicableUiContentValue<ItemId>();
        result.ammo = NotApplicableUiContentValue<std::int8_t>();
        return result;
    }

    result.item_id = FromSemanticValue(state.inventory.items[slot_index]);
    result.ammo = slot_index < state.inventory.ammo.size()
                      ? FromSemanticValue(state.inventory.ammo[slot_index])
                      : NotApplicableUiContentValue<std::int8_t>();
    return result;
}

static_assert(kEquipmentShifts.size() == kEquipmentTypeValueCount);
static_assert(kUpgradeShifts.size() == kUpgradeTypeValueCount);
static_assert(kItemToInventorySlot.size() == kOot3dItemSlotMappingCount);
static_assert(kUpgradeCapacities[0][3] == 50);
static_assert(kUpgradeCapacities[4][2] == 500);

} // namespace

UiContentValue<InventorySlot> ResolveOot3dInventorySlotForItem(
    ItemId item_id) noexcept {
    const std::size_t item_index = static_cast<std::size_t>(UiValueRaw(item_id));
    if (item_index >= kItemToInventorySlot.size()) {
        return NotApplicableUiContentValue<InventorySlot>();
    }
    return KnownUiContentValue(
        static_cast<InventorySlot>(kItemToInventorySlot[item_index]));
}

UiInventoryContentSnapshot BuildOot3dUiInventoryContent(
    const Oot3dUiSemanticState& state) noexcept {
    UiInventoryContentSnapshot result;

    for (std::size_t index = 0; index < result.equipment.size(); ++index) {
        UiEquipmentCategoryContent& category = result.equipment[index];
        category.category = kUiEquipmentTypes[index];
        category.equipped_value =
            ResolveEquipmentField(state.current_equips.equipment, category.category);
        category.owned_value_mask =
            ResolveEquipmentField(state.inventory.equipment, category.category);
    }

    for (std::size_t index = 0; index < result.upgrades.size(); ++index) {
        result.upgrades[index] = ResolveUpgrade(state.inventory, kUiUpgradeTypes[index]);
    }

    for (std::size_t index = 0; index < result.quest_items.size(); ++index) {
        result.quest_items[index].item = kUiQuestItems[index];
        // QUEST_HEART_PIECE is a pause-cursor identity at 0x18.  The packed
        // heart-piece count lives at bit 0x1C and is not a boolean ownership
        // flag, so do not manufacture one from the equal cursor value.
        result.quest_items[index].owned =
            kUiQuestItems[index] == QuestItem::QUEST_HEART_PIECE
                ? NotApplicableUiContentValue<bool>()
                : ResolveBit(
                      state.inventory.quest_items,
                      static_cast<std::uint8_t>(UiValueRaw(kUiQuestItems[index])));
    }

    for (std::size_t dungeon = 0; dungeon < result.dungeon_items.size(); ++dungeon) {
        for (std::size_t item = 0; item < result.dungeon_items[dungeon].size(); ++item) {
            result.dungeon_items[dungeon][item].item = kUiDungeonItems[item];
            result.dungeon_items[dungeon][item].owned = ResolveBit(
                state.inventory.dungeon_items[dungeon],
                static_cast<std::uint8_t>(UiValueRaw(kUiDungeonItems[item])));
        }
    }

    const auto* grid = ActiveItemGrid(state);
    for (std::size_t index = 0; index < result.active_item_grid.size(); ++index) {
        result.active_item_grid[index] = ResolveItemCell(state, grid, index);
    }
    return result;
}

} // namespace oot3d::ui
