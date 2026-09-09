#include "oot3d_ui/ui_pause_content.h"

#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

namespace {

template <typename T>
UiContentValue<T> FromSemanticValue(const SemanticValue<T>& source) noexcept {
    return source.known ? KnownUiContentValue(source.value) : UiContentValue<T>{};
}

template <typename T, std::size_t Size>
UiPauseContentArray<T, Size> FromSemanticArray(
    const SemanticArray<T, Size>& source) noexcept {
    UiPauseContentArray<T, Size> result{};
    for (std::size_t index = 0; index < Size; ++index) {
        result[index] = FromSemanticValue(source[index]);
    }
    return result;
}

UiContentValue<PauseItemGridPosition> ResolveGridPosition(
    const SemanticValue<std::int32_t>& source) noexcept {
    if (!source.known) {
        return {};
    }
    const PauseItemGridPosition position =
        PauseItemGridPosition::FromNativeResult(source.value);
    return position.IsValid()
               ? KnownUiContentValue(position)
               : NotApplicableUiContentValue<PauseItemGridPosition>();
}

constexpr bool IsVerifiedItemId(std::int32_t value) noexcept {
    return (value >= 0 && value <= UiValueRaw(ItemId::ITEM_DEKU_NUT_UPGRADE_40)) ||
           value == UiValueRaw(ItemId::ITEM_LAST_USED) ||
           value == UiValueRaw(ItemId::ITEM_NONE_FE) ||
           value == UiValueRaw(ItemId::ITEM_NONE);
}

UiContentValue<ItemId> ResolveItemId(
    const SemanticValue<std::int32_t>& source) noexcept {
    if (!source.known) {
        return {};
    }
    return IsVerifiedItemId(source.value)
               ? KnownUiContentValue(static_cast<ItemId>(source.value))
               : NotApplicableUiContentValue<ItemId>();
}

UiContentValue<Oot3dArrowTypeChoice> ResolveArrowChoice(
    const SemanticValue<std::int32_t>& source) noexcept {
    if (!source.known) {
        return {};
    }
    if (source.value < static_cast<std::int32_t>(Oot3dArrowTypeChoice::Bow) ||
        source.value > static_cast<std::int32_t>(Oot3dArrowTypeChoice::LightArrow)) {
        return NotApplicableUiContentValue<Oot3dArrowTypeChoice>();
    }
    return KnownUiContentValue(static_cast<Oot3dArrowTypeChoice>(source.value));
}

UiContentValue<Oot3dPauseGearPageState> ResolveGearPageState(
    const SemanticValue<std::int32_t>& source) noexcept {
    if (!source.known) {
        return {};
    }
    if (source.value < 0 ||
        source.value >= static_cast<std::int32_t>(kOot3dPauseGearPageStateCount)) {
        return NotApplicableUiContentValue<Oot3dPauseGearPageState>();
    }
    return KnownUiContentValue(static_cast<Oot3dPauseGearPageState>(source.value));
}

UiContentValue<Oot3dPauseGearSlot> ResolveGearSlotIdentity(
    const SemanticValue<std::int32_t>& source) noexcept {
    if (!source.known) {
        return {};
    }
    if (source.value < 0 ||
        source.value >= static_cast<std::int32_t>(kOot3dPauseGearSlotCount)) {
        return NotApplicableUiContentValue<Oot3dPauseGearSlot>();
    }
    return KnownUiContentValue(static_cast<Oot3dPauseGearSlot>(source.value));
}

UiContentValue<EquipmentType> ResolveGearEquipmentCategory(
    const SemanticValue<std::int32_t>& source) noexcept {
    if (!source.known) {
        return {};
    }
    if (source.value < UiValueRaw(EquipmentType::EQUIP_TYPE_SWORD) ||
        source.value > UiValueRaw(EquipmentType::EQUIP_TYPE_TUNIC)) {
        return NotApplicableUiContentValue<EquipmentType>();
    }
    return KnownUiContentValue(static_cast<EquipmentType>(source.value));
}

UiContentValue<std::uint8_t> ResolveGearEquipmentValue(
    const SemanticValue<std::int32_t>& zero_based_tier) noexcept {
    if (!zero_based_tier.known) {
        return {};
    }
    if (zero_based_tier.value < 0 || zero_based_tier.value > 2) {
        return NotApplicableUiContentValue<std::uint8_t>();
    }
    return KnownUiContentValue(
        static_cast<std::uint8_t>(zero_based_tier.value + 1));
}

inline constexpr std::array<std::uint8_t, 3> kGearEquipmentShifts = {0, 4, 8};
inline constexpr std::array<std::uint16_t, 3> kGearEquipmentMasks = {
    0x000F, 0x00F0, 0x0F00,
};
inline constexpr std::array<std::uint8_t, 8> kGearUpgradeShifts = {
    0, 3, 6, 9, 12, 14, 17, 20,
};
inline constexpr std::array<std::uint32_t, 8> kGearUpgradeMasks = {
    0x00000007, 0x00000038, 0x000001C0, 0x00000E00,
    0x00003000, 0x0001C000, 0x000E0000, 0x00700000,
};

UiContentValue<bool> ResolveGearAgeAllowed(
    const SemanticValue<LinkAge>& age,
    Oot3dPauseGearAgeRequirement requirement) noexcept {
    if (requirement == Oot3dPauseGearAgeRequirement::Any) {
        return KnownUiContentValue(true);
    }
    if (!age.known) {
        return {};
    }
    const auto required_age = static_cast<std::int32_t>(requirement);
    return KnownUiContentValue(UiValueRaw(age.value) == required_age);
}

UiContentValue<bool> ResolveGearQuestBit(
    const SemanticValue<std::uint32_t>& quest_items, std::int8_t bit) noexcept {
    if (!quest_items.known) {
        return {};
    }
    return KnownUiContentValue(
        (quest_items.value & (std::uint32_t{1} << static_cast<unsigned>(bit))) != 0);
}

std::uint8_t GearPackedUpgradeLevel(std::uint32_t upgrades,
                                    std::size_t index) noexcept {
    return static_cast<std::uint8_t>(
        (upgrades & kGearUpgradeMasks[index]) >> kGearUpgradeShifts[index]);
}

void ResolveGearUpgrade(UiPauseEquipmentContent::SlotContent& result,
                        const SemanticValue<std::uint32_t>& upgrades,
                        std::size_t upgrade_index, ItemId first_item,
                        std::uint8_t maximum_level) noexcept {
    result.item_id = {};
    result.owned = {};
    result.quantity = {};
    if (!upgrades.known) {
        return;
    }
    const std::uint8_t level = GearPackedUpgradeLevel(upgrades.value, upgrade_index);
    result.owned = KnownUiContentValue(level != 0);
    result.quantity = KnownUiContentValue(static_cast<std::uint16_t>(level));
    if (level == 0 || level > maximum_level) {
        result.item_id = NotApplicableUiContentValue<ItemId>();
        return;
    }
    result.item_id = KnownUiContentValue(static_cast<ItemId>(
        UiValueRaw(first_item) + level - 1));
}

UiPauseEquipmentContent::SlotContent ResolveGearSlot(
    const Oot3dUiSemanticState& state,
    const Oot3dPauseGearSlotDescriptor& descriptor) noexcept {
    UiPauseEquipmentContent::SlotContent result;
    result.slot = descriptor.slot;
    result.role = descriptor.role;
    result.directly_equippable = descriptor.directly_equippable;
    result.item_id = KnownUiContentValue(descriptor.base_item);
    result.age_allowed = ResolveGearAgeAllowed(state.link_age, descriptor.age_requirement);
    result.equipped = NotApplicableUiContentValue<bool>();
    result.quantity = NotApplicableUiContentValue<std::uint16_t>();

    switch (descriptor.role) {
    case Oot3dPauseGearSlotRole::Equipment: {
        const std::size_t category = static_cast<std::size_t>(descriptor.equipment_category);
        if (state.inventory.equipment.known) {
            const std::uint8_t owned_mask = static_cast<std::uint8_t>(
                (state.inventory.equipment.value & kGearEquipmentMasks[category]) >>
                kGearEquipmentShifts[category]);
            result.owned = KnownUiContentValue(
                (owned_mask & (std::uint8_t{1} << (descriptor.equipment_value - 1))) != 0);
        }
        if (state.current_equips.equipment.known) {
            const std::uint8_t equipped_value = static_cast<std::uint8_t>(
                (state.current_equips.equipment.value & kGearEquipmentMasks[category]) >>
                kGearEquipmentShifts[category]);
            result.equipped = KnownUiContentValue(
                equipped_value == descriptor.equipment_value);
        }
        break;
    }
    case Oot3dPauseGearSlotRole::QuestItem:
        result.owned = ResolveGearQuestBit(state.inventory.quest_items,
                                           descriptor.quest_bit);
        break;
    case Oot3dPauseGearSlotRole::Ocarina:
        result.item_id = {};
        if (state.inventory.items[UiValueRaw(InventorySlot::SLOT_OCARINA)].known) {
            const ItemId item =
                state.inventory.items[UiValueRaw(InventorySlot::SLOT_OCARINA)].value;
            const bool is_ocarina = item == ItemId::ITEM_OCARINA_FAIRY ||
                                    item == ItemId::ITEM_OCARINA_OF_TIME;
            result.item_id = is_ocarina
                                 ? KnownUiContentValue(item)
                                 : NotApplicableUiContentValue<ItemId>();
            result.owned = KnownUiContentValue(is_ocarina);
        }
        break;
    case Oot3dPauseGearSlotRole::TokenCount:
        if (state.inventory.gold_skulltula_tokens.known) {
            const std::int16_t tokens = state.inventory.gold_skulltula_tokens.value;
            result.owned = KnownUiContentValue(tokens != 0);
            result.quantity = tokens >= 0
                                  ? KnownUiContentValue(static_cast<std::uint16_t>(tokens))
                                  : NotApplicableUiContentValue<std::uint16_t>();
        } else {
            result.owned = {};
            result.quantity = {};
        }
        break;
    case Oot3dPauseGearSlotRole::AgeDependentProjectileUpgrade:
        result.item_id = {};
        result.owned = {};
        result.quantity = {};
        if (state.link_age.known) {
            if (state.link_age.value == LinkAge::Adult) {
                ResolveGearUpgrade(result, state.inventory.upgrades, 0,
                                   ItemId::ITEM_QUIVER_30, 3);
            } else if (state.link_age.value == LinkAge::Child) {
                ResolveGearUpgrade(result, state.inventory.upgrades, 5,
                                   ItemId::ITEM_BULLET_BAG_30, 3);
            }
        }
        break;
    case Oot3dPauseGearSlotRole::Upgrade: {
        const std::uint8_t maximum_level = descriptor.upgrade_index == 3 ? 2 : 3;
        ResolveGearUpgrade(result, state.inventory.upgrades,
                           static_cast<std::size_t>(descriptor.upgrade_index),
                           descriptor.base_item, maximum_level);
        break;
    }
    case Oot3dPauseGearSlotRole::HeartPieceCount:
        if (state.inventory.quest_items.known) {
            const std::uint16_t count = static_cast<std::uint16_t>(
                (state.inventory.quest_items.value >> 28) & 0xF);
            result.owned = KnownUiContentValue(count != 0);
            result.quantity = KnownUiContentValue(count);
        } else {
            result.owned = {};
            result.quantity = {};
        }
        break;
    }
    return result;
}

UiPauseRootContent ResolveRoot(const PauseState& source) noexcept {
    return {
        FromSemanticValue(source.open),
        FromSemanticValue(source.lifecycle_state),
        FromSemanticValue(source.interaction_state),
        FromSemanticValue(source.optional_quest_panel_state),
        FromSemanticValue(source.initialized),
        FromSemanticValue(source.omote_ura_selector_enabled),
        FromSemanticValue(source.touch_pressed),
        FromSemanticValue(source.touch_x),
        FromSemanticValue(source.touch_y),
    };
}

UiPauseItemsContent ResolveItems(const PauseItemsState& source) noexcept {
    UiPauseItemsContent result;
    result.controller_state = FromSemanticValue(source.controller_state);
    result.arrow_selector_state = FromSemanticValue(source.arrow_selector_state);
    result.mode = FromSemanticValue(source.mode);
    result.source_grid_position = FromSemanticValue(source.source_grid_position);
    result.destination_grid_position = FromSemanticValue(source.destination_grid_position);
    result.selected_slot = FromSemanticValue(source.selected_slot);
    result.previous_slot = FromSemanticValue(source.previous_slot);
    result.selected_item_id = FromSemanticValue(source.selected_item_id);
    result.arrow_choice_index = FromSemanticValue(source.arrow_choice_index);
    result.cursor_column = FromSemanticValue(source.cursor_column);
    result.cursor_row = FromSemanticValue(source.cursor_row);
    result.selection_mode = FromSemanticValue(source.selection_mode);
    result.animation_step = FromSemanticValue(source.animation_step);
    result.animation_x = FromSemanticValue(source.animation_x);
    result.animation_y = FromSemanticValue(source.animation_y);
    result.pending_arrow_item_id = FromSemanticValue(source.pending_arrow_item_id);
    result.snapshotted_grid_column = FromSemanticValue(source.snapshotted_grid_column);
    result.snapshotted_grid_row = FromSemanticValue(source.snapshotted_grid_row);
    result.arrow_choice_column = FromSemanticValue(source.arrow_choice_column);
    result.arrow_choice_row = FromSemanticValue(source.arrow_choice_row);
    result.horizontal_input_latch = FromSemanticValue(source.horizontal_input_latch);
    result.vertical_input_latch = FromSemanticValue(source.vertical_input_latch);
    result.suppress_focused_grid_icon = FromSemanticValue(source.suppress_focused_grid_icon);
    result.source_position = ResolveGridPosition(source.source_grid_position);
    result.destination_position = ResolveGridPosition(source.destination_grid_position);
    result.selected_grid_position = ResolveGridPosition(source.selected_slot);
    result.previous_grid_position = ResolveGridPosition(source.previous_slot);
    result.selected_item = ResolveItemId(source.selected_item_id);
    result.pending_arrow_item = ResolveItemId(source.pending_arrow_item_id);
    result.arrow_choice = ResolveArrowChoice(source.arrow_choice_index);
    return result;
}

UiPauseEquipmentContent ResolveEquipment(
    const Oot3dUiSemanticState& state) noexcept {
    const PauseEquipmentState& source = state.pause.equipment;
    UiPauseEquipmentContent result;
    result.controller_state = FromSemanticValue(source.controller_state);
    result.transition_frame = FromSemanticValue(source.transition_frame);
    result.focus_slot = FromSemanticValue(source.focus_slot);
    result.detail_slot = FromSemanticValue(source.detail_slot);
    result.touch_slot = FromSemanticValue(source.touch_slot);
    result.touch_open_request_a_latch =
        FromSemanticValue(source.touch_open_request_a_latch);
    result.touch_open_request_b_latch =
        FromSemanticValue(source.touch_open_request_b_latch);
    result.equip_animation_step = FromSemanticValue(source.equip_animation_step);
    result.pending_equip_category = FromSemanticValue(source.pending_equip_category);
    result.pending_equip_tier = FromSemanticValue(source.pending_equip_tier);
    result.equip_animation_x = FromSemanticValue(source.equip_animation_x);
    result.pending_item_id = FromSemanticValue(source.pending_item_id);
    result.suppress_focused_slot_enlargement =
        FromSemanticValue(source.suppress_focused_slot_enlargement);
    result.state = ResolveGearPageState(source.controller_state);
    result.focused_slot = ResolveGearSlotIdentity(source.focus_slot);
    result.touched_slot = ResolveGearSlotIdentity(source.touch_slot);
    result.pending_category =
        ResolveGearEquipmentCategory(source.pending_equip_category);
    result.pending_equipment_value =
        ResolveGearEquipmentValue(source.pending_equip_tier);
    result.pending_item = ResolveItemId(source.pending_item_id);
    const auto& descriptors = Oot3dPauseGearSlots();
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        result.slots[index] = ResolveGearSlot(state, descriptors[index]);
    }
    return result;
}

UiPauseQuestContent ResolveQuest(const PauseQuestState& source) noexcept {
    return {
        FromSemanticValue(source.preview_variant_mode),
        FromSemanticValue(source.selection_state),
        FromSemanticValue(source.selected_entry),
        FromSemanticValue(source.previous_entry),
        FromSemanticValue(source.selection_animation_frame),
        FromSemanticValue(source.prompt_mode),
        FromSemanticValue(source.auxiliary_selection),
        FromSemanticValue(source.active),
    };
}

UiPauseWorldMapContent ResolveWorldMap(
    const PauseWorldMapState& source) noexcept {
    return {
        FromSemanticValue(source.controller_state),
        FromSemanticValue(source.cursor_column),
        FromSemanticValue(source.cursor_row),
        FromSemanticValue(source.previous_destination),
        FromSemanticValue(source.selection_state),
        FromSemanticValue(source.overlay_visible),
        FromSemanticValue(source.active_action),
        FromSemanticValue(source.marker_animation_frame),
        FromSemanticValue(source.marker_variant),
        FromSemanticValue(source.transition_frame),
        FromSemanticValue(source.destination_available),
        FromSemanticValue(source.detail_available),
        FromSemanticValue(source.enabled),
        FromSemanticValue(source.active_input_mask),
        FromSemanticValue(source.touch_owned),
    };
}

UiPauseDungeonMapContent ResolveDungeonMap(
    const PauseDungeonMapState& source) noexcept {
    return {
        FromSemanticValue(source.cursor_selection),
        FromSemanticValue(source.controller_state),
        FromSemanticValue(source.transition_frame),
        FromSemanticValue(source.world_destination),
        FromSemanticValue(source.horizontal_input_latch),
        FromSemanticValue(source.vertical_input_latch),
        FromSemanticValue(source.restricted_scene),
        FromSemanticArray(source.floor_metadata),
    };
}

UiPauseSystemMenuContent ResolveSystemMenu(
    const PauseSystemMenuState& source) noexcept {
    return {
        FromSemanticValue(source.save_flow_state),
        FromSemanticValue(source.mode),
        FromSemanticValue(source.transition_frame),
        FromSemanticValue(source.option_index),
        FromSemanticValue(source.primary_choice),
        FromSemanticValue(source.secondary_choice),
        FromSemanticValue(source.options_state),
        FromSemanticValue(source.loaded_option_index),
    };
}

UiPauseRepeatInputContent ResolveRepeatInput(
    const PauseRepeatInputState& source) noexcept {
    return {
        FromSemanticValue(source.analog_direction),
        FromSemanticValue(source.held_buttons),
        FromSemanticValue(source.analog_pressed),
        FromSemanticValue(source.button_pressed),
        FromSemanticValue(source.analog_hold_frames),
        FromSemanticValue(source.analog_repeat_frames),
        FromSemanticValue(source.button_hold_frames),
        FromSemanticValue(source.button_repeat_frames),
        FromSemanticValue(source.analog_repeated),
        FromSemanticValue(source.buttons_repeated),
    };
}

UiPauseTouchButtonsContent ResolveTouchButtons(
    const PauseTouchButtonsState& source) noexcept {
    return {
        FromSemanticValue(source.panel_state),
        FromSemanticValue(source.selected_action),
        FromSemanticValue(source.phase),
        FromSemanticValue(source.selection),
        FromSemanticValue(source.input_state),
        FromSemanticValue(source.disabled_mask),
        FromSemanticValue(source.default_item_id),
        FromSemanticArray(source.action_item_ids),
        FromSemanticValue(source.focus_state),
        FromSemanticValue(source.player_action_allowed),
    };
}

UiPauseTouchInputContent ResolveTouchInput(
    const PauseTouchInputState& source) noexcept {
    return {
        FromSemanticValue(source.touch_pressed),
        FromSemanticValue(source.touch_x),
        FromSemanticValue(source.touch_y),
        FromSemanticValue(source.held_mask),
        FromSemanticValue(source.pressed_mask),
        FromSemanticValue(source.released_mask),
        FromSemanticValue(source.hold_frames),
        FromSemanticValue(source.repeat_frames),
        FromSemanticValue(source.repeated_mask),
    };
}

} // namespace

UiPauseContentSnapshot BuildOot3dUiPauseContent(
    const Oot3dUiSemanticState& state) noexcept {
    return {
        ResolveRoot(state.pause),
        ResolveItems(state.pause.items),
        ResolveEquipment(state),
        ResolveQuest(state.pause.quest),
        ResolveWorldMap(state.pause.world_map),
        ResolveDungeonMap(state.pause.dungeon_map),
        ResolveSystemMenu(state.pause.system_menu),
        ResolveRepeatInput(state.pause.repeat_input),
        ResolveTouchButtons(state.pause.touch_buttons),
        ResolveTouchInput(state.pause.touch_input),
    };
}

} // namespace oot3d::ui
