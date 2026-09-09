#include "oot3d_ui/ui_state_adapter.h"

#include "oot3d_ui/guest_ui_layout.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace oot3d::ui {

namespace {

template <typename T>
void ReadValue(const GuestMemoryReader& memory, std::uint32_t base, std::size_t offset,
               SemanticValue<T>& destination, UiStateCaptureResult& result) noexcept {
    T value{};
    if (memory.Read(base + static_cast<std::uint32_t>(offset), &value, sizeof(value))) {
        destination = {value, true};
        ++result.fields_read;
    } else {
        ++result.fields_missing;
    }
}

template <typename Enum, typename Storage>
void ReadEnumValue(const GuestMemoryReader& memory, std::uint32_t base, std::size_t offset,
                   SemanticValue<Enum>& destination,
                   UiStateCaptureResult& result) noexcept {
    static_assert(std::is_enum_v<Enum>);
    static_assert(sizeof(Enum) == sizeof(Storage));
    SemanticValue<Storage> source;
    ReadValue(memory, base, offset, source, result);
    if (source.known) {
        destination = {static_cast<Enum>(source.value), true};
    }
}

void CopyItemEquips(const guest_layout::ItemEquips& source, ItemEquipsState& destination) noexcept {
    for (std::size_t index = 0; index < destination.button_items.size(); ++index) {
        destination.button_items[index] = {
            static_cast<ItemId>(source.button_items[index]), true};
    }
    for (std::size_t index = 0; index < destination.button_slots.size(); ++index) {
        destination.button_slots[index] = {
            static_cast<InventorySlot>(source.button_slots[index]), true};
    }
    destination.equipment = {source.equipment, true};
}

void ReadItemEquips(const GuestMemoryReader& memory, std::uint32_t base, std::size_t offset,
                    ItemEquipsState& destination, UiStateCaptureResult& result) noexcept {
    guest_layout::ItemEquips source{};
    if (memory.Read(base + static_cast<std::uint32_t>(offset), &source, sizeof(source))) {
        CopyItemEquips(source, destination);
        result.fields_read += destination.button_items.size() + destination.button_slots.size() + 1;
    } else {
        result.fields_missing += destination.button_items.size() + destination.button_slots.size() + 1;
    }
}

void ReadInventory(const GuestMemoryReader& memory, std::uint32_t base, InventoryState& destination,
                   UiStateCaptureResult& result) noexcept {
    guest_layout::Inventory source{};
    if (!memory.Read(base + static_cast<std::uint32_t>(guest_layout::save_context::kInventory),
                     &source, sizeof(source))) {
        result.fields_missing += destination.items.size() + destination.ammo.size() +
                                 destination.dungeon_items.size() + destination.dungeon_keys.size() + 5;
        return;
    }

    for (std::size_t index = 0; index < destination.items.size(); ++index) {
        destination.items[index] = {static_cast<ItemId>(source.items[index]), true};
    }
    for (std::size_t index = 0; index < destination.ammo.size(); ++index) {
        destination.ammo[index] = {source.ammo[index], true};
    }
    for (std::size_t index = 0; index < destination.dungeon_items.size(); ++index) {
        destination.dungeon_items[index] = {source.dungeon_items[index], true};
    }
    for (std::size_t index = 0; index < destination.dungeon_keys.size(); ++index) {
        destination.dungeon_keys[index] = {source.dungeon_keys[index], true};
    }
    destination.equipment = {source.equipment, true};
    destination.upgrades = {source.upgrades, true};
    destination.quest_items = {source.quest_items, true};
    destination.defense_hearts = {source.defense_hearts, true};
    destination.gold_skulltula_tokens = {source.gold_skulltula_tokens, true};
    result.fields_read += destination.items.size() + destination.ammo.size() +
                          destination.dungeon_items.size() + destination.dungeon_keys.size() + 5;
}

template <typename T, std::size_t Size>
void ReadArray(const GuestMemoryReader& memory, std::uint32_t base, std::size_t offset,
               SemanticArray<T, Size>& destination, UiStateCaptureResult& result) noexcept {
    std::array<T, Size> source{};
    if (memory.Read(base + static_cast<std::uint32_t>(offset), source.data(), sizeof(source))) {
        for (std::size_t index = 0; index < source.size(); ++index) {
            destination[index] = {source[index], true};
        }
        result.fields_read += source.size();
    } else {
        result.fields_missing += source.size();
    }
}

template <typename Enum, typename Storage, std::size_t Size>
void ReadEnumArray(const GuestMemoryReader& memory, std::uint32_t base,
                   std::size_t offset, SemanticArray<Enum, Size>& destination,
                   UiStateCaptureResult& result) noexcept {
    static_assert(std::is_enum_v<Enum>);
    static_assert(sizeof(Enum) == sizeof(Storage));
    std::array<Storage, Size> source{};
    if (memory.Read(base + static_cast<std::uint32_t>(offset), source.data(),
                    sizeof(source))) {
        for (std::size_t index = 0; index < source.size(); ++index) {
            destination[index] = {static_cast<Enum>(source[index]), true};
        }
        result.fields_read += source.size();
    } else {
        result.fields_missing += source.size();
    }
}

void ReadBool(const GuestMemoryReader& memory, std::uint32_t base, std::size_t offset,
              SemanticValue<bool>& destination, UiStateCaptureResult& result) noexcept {
    SemanticValue<std::uint8_t> source;
    ReadValue(memory, base, offset, source, result);
    if (source.known) {
        destination = {source.value != 0, true};
    }
}

void ReadBool32(const GuestMemoryReader& memory, std::uint32_t base, std::size_t offset,
                SemanticValue<bool>& destination, UiStateCaptureResult& result) noexcept {
    SemanticValue<std::uint32_t> source;
    ReadValue(memory, base, offset, source, result);
    if (source.known) {
        destination = {source.value != 0, true};
    }
}

void ReadFileSelectSlotSummary(
    const GuestMemoryReader& memory, std::uint32_t base,
    FileSelectSlotSummaryState& destination,
    UiStateCaptureResult& result) noexcept {
    using namespace guest_layout::file_select;
    ReadValue(memory, base, kSlotFormatVersion, destination.format_version, result);
    ReadValue(memory, base, kSlotValidationFailed, destination.validation_failed, result);
    ReadValue(memory, base, kSlotInitializationMarker,
              destination.initialization_marker, result);
    ReadValue(memory, base, kSlotHealthCapacity, destination.health_capacity, result);
    ReadValue(memory, base, kSlotHealth, destination.health, result);
    ReadValue(memory, base, kSlotQuestItemBits, destination.quest_item_bits, result);
    ReadValue(memory, base, kSlotTimestampYear, destination.timestamp_year, result);
    ReadValue(memory, base, kSlotTimestampMonth, destination.timestamp_month, result);
    ReadValue(memory, base, kSlotTimestampDay, destination.timestamp_day, result);
    ReadValue(memory, base, kSlotTimestampHour, destination.timestamp_hour, result);
    ReadValue(memory, base, kSlotTimestampMinute, destination.timestamp_minute, result);
    ReadValue(memory, base, kSlotChecksum, destination.checksum, result);
}

} // namespace

Oot3dUiGuestRoots BuildVerifiedOot3dUiGuestRoots(
    std::uint32_t save_context_address) noexcept {
    return {
        save_context_address,
        guest_layout::pause_root::kAddress,
        guest_layout::pause_items::kAddress,
        guest_layout::pause_equipment::kAddress,
        guest_layout::pause_quest::kAddress,
        guest_layout::pause_world_map::kAddress,
        guest_layout::pause_dungeon_map::kAddress,
        guest_layout::pause_system_menu::kAddress,
        guest_layout::pause_repeat_input::kAddress,
        guest_layout::pause_touch_buttons::kAddress,
        guest_layout::pause_touch_input::kAddress,
        guest_layout::file_select::kAddress,
        guest_layout::file_select::kSlotValidAddress,
        guest_layout::file_select::kSlotBuffersAddress,
        guest_layout::name_entry::kAddress,
        guest_layout::name_entry::kEditableNameAddress,
    };
}

UiStateCaptureResult CaptureOot3dUiState(const GuestMemoryReader& memory,
                                         std::uint32_t save_context_address) noexcept {
    UiStateCaptureResult result;
    using namespace guest_layout::save_context;

    ReadValue(memory, save_context_address, kEntranceIndex, result.state.save.entrance_index, result);
    SemanticValue<std::int32_t> link_age;
    ReadValue(memory, save_context_address, kLinkAge, link_age, result);
    if (link_age.known) {
        result.state.link_age = {static_cast<LinkAge>(link_age.value), true};
    }

    ReadValue(memory, save_context_address, kCutsceneIndex, result.state.save.cutscene_index, result);
    ReadValue(memory, save_context_address, kDayTime, result.state.save.day_time, result);
    ReadBool(memory, save_context_address, kMasterQuestFlag, result.state.master_quest, result);
    ReadValue(memory, save_context_address, kNightFlag, result.state.save.night_flag, result);
    ReadArray(memory, save_context_address, kPlayerName, result.state.save.player_name, result);
    ReadValue(memory, save_context_address, kPlayerNameLength,
              result.state.save.player_name_length, result);
    ReadValue(memory, save_context_address, kZTargetingSetting,
              result.state.save.z_targeting_setting, result);

    ReadValue(memory, save_context_address, kSavedSceneId, result.state.scene_id, result);
    ReadValue(memory, save_context_address, kMagicLevel, result.state.magic_level, result);
    ReadValue(memory, save_context_address, kSwordHealth, result.state.sword_health, result);
    ReadValue(memory, save_context_address, kNaviTimer, result.state.navi_timer, result);
    ReadBool(memory, save_context_address, kMagicAcquired, result.state.magic_acquired, result);
    ReadBool(memory, save_context_address, kDoubleMagicAcquired,
             result.state.double_magic_acquired, result);
    ReadBool(memory, save_context_address, kDoubleDefenseAcquired,
             result.state.double_defense_acquired, result);
    ReadValue(memory, save_context_address, kBiggoronSwordFlag,
              result.state.biggoron_sword_flag, result);
    ReadItemEquips(memory, save_context_address, kChildEquips, result.state.child_equips, result);
    ReadItemEquips(memory, save_context_address, kAdultEquips, result.state.adult_equips, result);
    ReadItemEquips(memory, save_context_address, kCurrentEquips, result.state.current_equips, result);
    ReadInventory(memory, save_context_address, result.state.inventory, result);
    ReadEnumArray<ItemId, std::uint8_t>(memory, save_context_address,
                                        kCachedInventoryItemIds,
                                        result.state.cached_inventory_item_ids, result);
    ReadEnumArray<InventorySlot, std::uint8_t>(
        memory, save_context_address, kChildItemMenuSlots,
        result.state.pause.child_item_menu_slots, result);
    ReadEnumArray<InventorySlot, std::uint8_t>(
        memory, save_context_address, kAdultItemMenuSlots,
        result.state.pause.adult_item_menu_slots, result);

    ReadArray(memory, save_context_address, kGsFlags,
              result.state.quest.gold_skulltula_flags, result);
    ReadArray(memory, save_context_address, kEventCheckInfo,
              result.state.quest.event_check_info, result);
    ReadArray(memory, save_context_address, kItemGetInfo,
              result.state.quest.item_get_info, result);
    ReadArray(memory, save_context_address, kInfoTable, result.state.quest.info_table, result);
    ReadValue(memory, save_context_address, kWorldMapAreaData,
              result.state.quest.world_map_area_data, result);
    ReadArray(memory, save_context_address, kBossBattleVictories,
              result.state.quest.boss_battle_victories, result);
    ReadArray(memory, save_context_address, kBossBattleScores,
              result.state.quest.boss_battle_scores, result);
    ReadValue(memory, save_context_address, kChecksum, result.state.save.checksum, result);
    ReadValue(memory, save_context_address, kFileNum, result.state.save.file_num, result);
    ReadValue(memory, save_context_address, kGameMode, result.state.save.game_mode, result);

    ReadValue(memory, save_context_address, kHealth, result.state.hud.health, result);
    ReadValue(memory, save_context_address, kHealthCapacity, result.state.hud.health_capacity, result);
    ReadValue(memory, save_context_address, kMagic, result.state.hud.magic, result);
    ReadValue(memory, save_context_address, kRupees, result.state.hud.rupees, result);
    ReadEnumValue<HudVisibilityMode, std::uint16_t>(
        memory, save_context_address, kNextHudVisibilityMode,
        result.state.hud.next_visibility_mode, result);
    ReadEnumValue<HudVisibilityMode, std::uint16_t>(
        memory, save_context_address, kHudVisibilityMode,
        result.state.hud.visibility_mode, result);
    ReadValue(memory, save_context_address, kHudVisibilityModeTimer,
              result.state.hud.visibility_transition_timer, result);
    ReadEnumValue<MagicState, std::int16_t>(
        memory, save_context_address, kMagicState, result.state.hud.magic_state, result);
    ReadValue(memory, save_context_address, kMagicCapacity, result.state.hud.magic_capacity, result);
    ReadValue(memory, save_context_address, kMagicFillTarget,
              result.state.hud.magic_fill_target, result);
    ReadValue(memory, save_context_address, kMagicTarget, result.state.hud.magic_target, result);
    ReadValue(memory, save_context_address, kMinigameState, result.state.hud.minigame_state, result);
    ReadValue(memory, save_context_address, kMinigameScore, result.state.hud.minigame_score, result);
    ReadValue(memory, save_context_address, kRupeeAccumulator,
              result.state.runtime.rupee_accumulator, result);
    ReadValue(memory, save_context_address, kTimerState, result.state.runtime.timer_state, result);
    ReadValue(memory, save_context_address, kTimerSeconds,
              result.state.runtime.timer_seconds, result);
    ReadValue(memory, save_context_address, kSubTimerState,
              result.state.runtime.sub_timer_state, result);
    ReadValue(memory, save_context_address, kSubTimerSeconds,
              result.state.runtime.sub_timer_seconds, result);
    ReadArray(memory, save_context_address, kTimerX, result.state.runtime.timer_x, result);
    ReadArray(memory, save_context_address, kTimerY, result.state.runtime.timer_y, result);
    ReadEnumArray<ButtonStatus, std::uint8_t>(
        memory, save_context_address, kButtonStatus,
        result.state.runtime.button_status, result);
    ReadValue(memory, save_context_address, kForceRisingButtonAlphas,
              result.state.runtime.force_rising_button_alphas, result);
    ReadEnumValue<HudVisibilityMode, std::uint16_t>(
        memory, save_context_address, kPreviousHudVisibilityMode,
        result.state.runtime.previous_hud_visibility_mode, result);
    ReadEnumValue<MagicState, std::int16_t>(
        memory, save_context_address, kPreviousMagicState,
        result.state.runtime.previous_magic_state, result);
    ReadArray(memory, save_context_address, kEventInfo, result.state.runtime.event_info, result);
    ReadValue(memory, save_context_address, kMapIndex, result.state.runtime.map_index, result);
    ReadValue(memory, save_context_address, kNextCutsceneIndex,
              result.state.runtime.next_cutscene_index, result);
    ReadValue(memory, save_context_address, kCutsceneTrigger,
              result.state.runtime.cutscene_trigger, result);
    ReadValue(memory, save_context_address, kHealthAccumulator,
              result.state.runtime.health_accumulator, result);
    return result;
}

UiStateCaptureResult CaptureOot3dUiState(
    const GuestMemoryReader& memory,
    const Oot3dUiGuestRoots& roots) noexcept {
    UiStateCaptureResult result = CaptureOot3dUiState(memory, roots.save_context);

    using namespace guest_layout;

    ReadBool(memory, roots.pause_root, pause_root::kTouchPressed,
             result.state.pause.touch_pressed, result);
    ReadValue(memory, roots.pause_root, pause_root::kTouchX,
              result.state.pause.touch_x, result);
    ReadValue(memory, roots.pause_root, pause_root::kTouchY,
              result.state.pause.touch_y, result);
    ReadValue(memory, roots.pause_root, pause_root::kLifecycleState,
              result.state.pause.lifecycle_state, result);
    ReadValue(memory, roots.pause_root, pause_root::kInteractionState,
              result.state.pause.interaction_state, result);
    ReadValue(memory, roots.pause_root, pause_root::kOptionalQuestPanelState,
              result.state.pause.optional_quest_panel_state, result);
    ReadBool32(memory, roots.pause_root, pause_root::kOmoteUraSelectorEnabled,
               result.state.pause.omote_ura_selector_enabled, result);
    ReadBool32(memory, roots.pause_root, pause_root::kInitialized,
               result.state.pause.initialized, result);
    if (result.state.pause.lifecycle_state.known) {
        result.state.pause.open = {
            result.state.pause.lifecycle_state.value != 0,
            true,
        };
    }

    ReadValue(memory, roots.pause_items, pause_items::kControllerState,
              result.state.pause.items.controller_state, result);
    ReadValue(memory, roots.pause_items, pause_items::kArrowSelectorState,
              result.state.pause.items.arrow_selector_state, result);
    ReadValue(memory, roots.pause_items, pause_items::kMode,
              result.state.pause.items.mode, result);
    ReadValue(memory, roots.pause_items, pause_items::kSourceGridPosition,
              result.state.pause.items.source_grid_position, result);
    ReadValue(memory, roots.pause_items, pause_items::kDestinationGridPosition,
              result.state.pause.items.destination_grid_position, result);
    ReadValue(memory, roots.pause_items, pause_items::kSelectedSlot,
              result.state.pause.items.selected_slot, result);
    ReadValue(memory, roots.pause_items, pause_items::kPreviousSlot,
              result.state.pause.items.previous_slot, result);
    ReadValue(memory, roots.pause_items, pause_items::kSelectedItemId,
              result.state.pause.items.selected_item_id, result);
    ReadValue(memory, roots.pause_items, pause_items::kArrowChoiceIndex,
              result.state.pause.items.arrow_choice_index, result);
    ReadValue(memory, roots.pause_items, pause_items::kCursorColumn,
              result.state.pause.items.cursor_column, result);
    ReadValue(memory, roots.pause_items, pause_items::kCursorRow,
              result.state.pause.items.cursor_row, result);
    ReadValue(memory, roots.pause_items, pause_items::kSelectionMode,
              result.state.pause.items.selection_mode, result);
    ReadValue(memory, roots.pause_items, pause_items::kAnimationStep,
              result.state.pause.items.animation_step, result);
    ReadValue(memory, roots.pause_items, pause_items::kAnimationX,
              result.state.pause.items.animation_x, result);
    ReadValue(memory, roots.pause_items, pause_items::kAnimationY,
              result.state.pause.items.animation_y, result);
    ReadValue(memory, roots.pause_items, pause_items::kPendingArrowItemId,
              result.state.pause.items.pending_arrow_item_id, result);
    ReadValue(memory, roots.pause_items, pause_items::kSnapshottedGridColumn,
              result.state.pause.items.snapshotted_grid_column, result);
    ReadValue(memory, roots.pause_items, pause_items::kSnapshottedGridRow,
              result.state.pause.items.snapshotted_grid_row, result);
    ReadValue(memory, roots.pause_items, pause_items::kArrowChoiceColumn,
              result.state.pause.items.arrow_choice_column, result);
    ReadValue(memory, roots.pause_items, pause_items::kArrowChoiceRow,
              result.state.pause.items.arrow_choice_row, result);
    ReadValue(memory, roots.pause_items, pause_items::kHorizontalInputLatch,
              result.state.pause.items.horizontal_input_latch, result);
    ReadValue(memory, roots.pause_items, pause_items::kVerticalInputLatch,
              result.state.pause.items.vertical_input_latch, result);
    ReadBool32(memory, roots.pause_items, pause_items::kSuppressFocusedGridIcon,
               result.state.pause.items.suppress_focused_grid_icon, result);
    result.state.pause.item_focus_slot = result.state.pause.items.selected_slot;

    ReadValue(memory, roots.pause_equipment, pause_equipment::kControllerState,
              result.state.pause.equipment.controller_state, result);
    ReadValue(memory, roots.pause_equipment, pause_equipment::kTransitionFrame,
              result.state.pause.equipment.transition_frame, result);
    ReadValue(memory, roots.pause_equipment, pause_equipment::kFocusSlot,
              result.state.pause.equipment.focus_slot, result);
    ReadValue(memory, roots.pause_equipment, pause_equipment::kDetailSlot,
              result.state.pause.equipment.detail_slot, result);
    ReadValue(memory, roots.pause_equipment, pause_equipment::kTouchSlot,
              result.state.pause.equipment.touch_slot, result);
    ReadBool32(memory, roots.pause_equipment, pause_equipment::kTouchOpenRequestALatch,
               result.state.pause.equipment.touch_open_request_a_latch, result);
    ReadBool32(memory, roots.pause_equipment, pause_equipment::kTouchOpenRequestBLatch,
               result.state.pause.equipment.touch_open_request_b_latch, result);
    ReadValue(memory, roots.pause_equipment, pause_equipment::kEquipAnimationStep,
              result.state.pause.equipment.equip_animation_step, result);
    ReadValue(memory, roots.pause_equipment, pause_equipment::kPendingEquipCategory,
              result.state.pause.equipment.pending_equip_category, result);
    ReadValue(memory, roots.pause_equipment, pause_equipment::kPendingEquipTier,
              result.state.pause.equipment.pending_equip_tier, result);
    ReadValue(memory, roots.pause_equipment, pause_equipment::kEquipAnimationX,
              result.state.pause.equipment.equip_animation_x, result);
    ReadValue(memory, roots.pause_equipment, pause_equipment::kPendingItemId,
              result.state.pause.equipment.pending_item_id, result);
    ReadBool32(memory, roots.pause_equipment,
               pause_equipment::kSuppressFocusedSlotEnlargement,
               result.state.pause.equipment.suppress_focused_slot_enlargement, result);
    result.state.pause.equipment_focus_slot = result.state.pause.equipment.focus_slot;

    ReadValue(memory, roots.pause_quest, pause_quest::kPreviewVariantMode,
              result.state.pause.quest.preview_variant_mode, result);
    ReadValue(memory, roots.pause_quest, pause_quest::kSelectionState,
              result.state.pause.quest.selection_state, result);
    ReadValue(memory, roots.pause_quest, pause_quest::kSelectedEntry,
              result.state.pause.quest.selected_entry, result);
    ReadValue(memory, roots.pause_quest, pause_quest::kPreviousEntry,
              result.state.pause.quest.previous_entry, result);
    ReadValue(memory, roots.pause_quest, pause_quest::kSelectionAnimationFrame,
              result.state.pause.quest.selection_animation_frame, result);
    ReadValue(memory, roots.pause_quest, pause_quest::kPromptMode,
              result.state.pause.quest.prompt_mode, result);
    ReadValue(memory, roots.pause_quest, pause_quest::kAuxiliarySelection,
              result.state.pause.quest.auxiliary_selection, result);
    ReadBool32(memory, roots.pause_quest, pause_quest::kActive,
               result.state.pause.quest.active, result);

    ReadValue(memory, roots.pause_world_map, pause_world_map::kControllerState,
              result.state.pause.world_map.controller_state, result);
    ReadValue(memory, roots.pause_world_map, pause_world_map::kCursorColumn,
              result.state.pause.world_map.cursor_column, result);
    ReadValue(memory, roots.pause_world_map, pause_world_map::kCursorRow,
              result.state.pause.world_map.cursor_row, result);
    ReadValue(memory, roots.pause_world_map, pause_world_map::kPreviousDestination,
              result.state.pause.world_map.previous_destination, result);
    ReadValue(memory, roots.pause_world_map, pause_world_map::kSelectionState,
              result.state.pause.world_map.selection_state, result);
    ReadBool32(memory, roots.pause_world_map, pause_world_map::kOverlayVisible,
               result.state.pause.world_map.overlay_visible, result);
    ReadValue(memory, roots.pause_world_map, pause_world_map::kActiveAction,
              result.state.pause.world_map.active_action, result);
    ReadValue(memory, roots.pause_world_map, pause_world_map::kMarkerAnimationFrame,
              result.state.pause.world_map.marker_animation_frame, result);
    ReadValue(memory, roots.pause_world_map, pause_world_map::kMarkerVariant,
              result.state.pause.world_map.marker_variant, result);
    ReadValue(memory, roots.pause_world_map, pause_world_map::kTransitionFrame,
              result.state.pause.world_map.transition_frame, result);
    ReadBool32(memory, roots.pause_world_map, pause_world_map::kDestinationAvailable,
               result.state.pause.world_map.destination_available, result);
    ReadBool32(memory, roots.pause_world_map, pause_world_map::kDetailAvailable,
               result.state.pause.world_map.detail_available, result);
    ReadBool32(memory, roots.pause_world_map, pause_world_map::kEnabled,
               result.state.pause.world_map.enabled, result);
    ReadValue(memory, roots.pause_world_map, pause_world_map::kActiveInputMask,
              result.state.pause.world_map.active_input_mask, result);
    ReadBool32(memory, roots.pause_world_map, pause_world_map::kTouchOwned,
               result.state.pause.world_map.touch_owned, result);

    ReadValue(memory, roots.pause_dungeon_map, pause_dungeon_map::kCursorSelection,
              result.state.pause.dungeon_map.cursor_selection, result);
    ReadValue(memory, roots.pause_dungeon_map, pause_dungeon_map::kControllerState,
              result.state.pause.dungeon_map.controller_state, result);
    ReadValue(memory, roots.pause_dungeon_map, pause_dungeon_map::kTransitionFrame,
              result.state.pause.dungeon_map.transition_frame, result);
    ReadValue(memory, roots.pause_dungeon_map, pause_dungeon_map::kWorldDestination,
              result.state.pause.dungeon_map.world_destination, result);
    ReadValue(memory, roots.pause_dungeon_map, pause_dungeon_map::kHorizontalInputLatch,
              result.state.pause.dungeon_map.horizontal_input_latch, result);
    ReadValue(memory, roots.pause_dungeon_map, pause_dungeon_map::kVerticalInputLatch,
              result.state.pause.dungeon_map.vertical_input_latch, result);
    ReadValue(memory, roots.pause_dungeon_map, pause_dungeon_map::kRestrictedScene,
              result.state.pause.dungeon_map.restricted_scene, result);
    ReadArray(memory, roots.pause_dungeon_map, pause_dungeon_map::kFloorMetadata,
              result.state.pause.dungeon_map.floor_metadata, result);

    ReadValue(memory, roots.pause_system_menu, pause_system_menu::kSaveFlowState,
              result.state.pause.system_menu.save_flow_state, result);
    ReadValue(memory, roots.pause_system_menu, pause_system_menu::kMode,
              result.state.pause.system_menu.mode, result);
    ReadValue(memory, roots.pause_system_menu, pause_system_menu::kTransitionFrame,
              result.state.pause.system_menu.transition_frame, result);
    ReadValue(memory, roots.pause_system_menu, pause_system_menu::kOptionIndex,
              result.state.pause.system_menu.option_index, result);
    ReadValue(memory, roots.pause_system_menu, pause_system_menu::kPrimaryChoice,
              result.state.pause.system_menu.primary_choice, result);
    ReadValue(memory, roots.pause_system_menu, pause_system_menu::kSecondaryChoice,
              result.state.pause.system_menu.secondary_choice, result);
    ReadValue(memory, roots.pause_system_menu, pause_system_menu::kOptionsState,
              result.state.pause.system_menu.options_state, result);
    ReadValue(memory, roots.pause_system_menu, pause_system_menu::kLoadedOptionIndex,
              result.state.pause.system_menu.loaded_option_index, result);

    ReadValue(memory, roots.pause_repeat_input, pause_repeat_input::kAnalogDirection,
              result.state.pause.repeat_input.analog_direction, result);
    ReadValue(memory, roots.pause_repeat_input, pause_repeat_input::kHeldButtons,
              result.state.pause.repeat_input.held_buttons, result);
    ReadValue(memory, roots.pause_repeat_input, pause_repeat_input::kAnalogPressed,
              result.state.pause.repeat_input.analog_pressed, result);
    ReadValue(memory, roots.pause_repeat_input, pause_repeat_input::kButtonPressed,
              result.state.pause.repeat_input.button_pressed, result);
    ReadValue(memory, roots.pause_repeat_input, pause_repeat_input::kAnalogHoldFrames,
              result.state.pause.repeat_input.analog_hold_frames, result);
    ReadValue(memory, roots.pause_repeat_input, pause_repeat_input::kAnalogRepeatFrames,
              result.state.pause.repeat_input.analog_repeat_frames, result);
    ReadValue(memory, roots.pause_repeat_input, pause_repeat_input::kButtonHoldFrames,
              result.state.pause.repeat_input.button_hold_frames, result);
    ReadValue(memory, roots.pause_repeat_input, pause_repeat_input::kButtonRepeatFrames,
              result.state.pause.repeat_input.button_repeat_frames, result);
    ReadValue(memory, roots.pause_repeat_input, pause_repeat_input::kAnalogRepeated,
              result.state.pause.repeat_input.analog_repeated, result);
    ReadValue(memory, roots.pause_repeat_input, pause_repeat_input::kButtonsRepeated,
              result.state.pause.repeat_input.buttons_repeated, result);

    ReadValue(memory, roots.pause_touch_buttons, pause_touch_buttons::kPanelState,
              result.state.pause.touch_buttons.panel_state, result);
    ReadValue(memory, roots.pause_touch_buttons, pause_touch_buttons::kSelectedAction,
              result.state.pause.touch_buttons.selected_action, result);
    ReadValue(memory, roots.pause_touch_buttons, pause_touch_buttons::kPhase,
              result.state.pause.touch_buttons.phase, result);
    ReadValue(memory, roots.pause_touch_buttons, pause_touch_buttons::kSelection,
              result.state.pause.touch_buttons.selection, result);
    ReadValue(memory, roots.pause_touch_buttons, pause_touch_buttons::kInputState,
              result.state.pause.touch_buttons.input_state, result);
    ReadValue(memory, roots.pause_touch_buttons, pause_touch_buttons::kDisabledMask,
              result.state.pause.touch_buttons.disabled_mask, result);
    ReadValue(memory, roots.pause_touch_buttons, pause_touch_buttons::kDefaultItemId,
              result.state.pause.touch_buttons.default_item_id, result);
    ReadEnumArray<ItemId, std::uint8_t>(
        memory, roots.pause_touch_buttons, pause_touch_buttons::kActionItemIds,
        result.state.pause.touch_buttons.action_item_ids, result);
    ReadValue(memory, roots.pause_touch_buttons, pause_touch_buttons::kFocusState,
              result.state.pause.touch_buttons.focus_state, result);
    ReadBool32(memory, roots.pause_touch_buttons, pause_touch_buttons::kPlayerActionAllowed,
               result.state.pause.touch_buttons.player_action_allowed, result);

    ReadBool(memory, roots.pause_touch_input, pause_touch_input::kTouchPressed,
             result.state.pause.touch_input.touch_pressed, result);
    ReadValue(memory, roots.pause_touch_input, pause_touch_input::kTouchX,
              result.state.pause.touch_input.touch_x, result);
    ReadValue(memory, roots.pause_touch_input, pause_touch_input::kTouchY,
              result.state.pause.touch_input.touch_y, result);
    ReadValue(memory, roots.pause_touch_input, pause_touch_input::kHeldMask,
              result.state.pause.touch_input.held_mask, result);
    ReadValue(memory, roots.pause_touch_input, pause_touch_input::kPressedMask,
              result.state.pause.touch_input.pressed_mask, result);
    ReadValue(memory, roots.pause_touch_input, pause_touch_input::kReleasedMask,
              result.state.pause.touch_input.released_mask, result);
    ReadValue(memory, roots.pause_touch_input, pause_touch_input::kHoldFrames,
              result.state.pause.touch_input.hold_frames, result);
    ReadValue(memory, roots.pause_touch_input, pause_touch_input::kRepeatFrames,
              result.state.pause.touch_input.repeat_frames, result);
    ReadValue(memory, roots.pause_touch_input, pause_touch_input::kRepeatedMask,
              result.state.pause.touch_input.repeated_mask, result);

    {
        using namespace guest_layout::file_select;
        ReadValue(memory, roots.file_select, kControllerState,
                  result.state.file_select.controller_state, result);
        ReadValue(memory, roots.file_select, kSelectedSlot,
                  result.state.file_select.selected_slot, result);
        ReadValue(memory, roots.file_select, kConfirmationChoice,
                  result.state.file_select.confirmation_choice, result);
        ReadValue(memory, roots.file_select, kCopySourceSlot,
                  result.state.file_select.copy_source_slot, result);
        ReadValue(memory, roots.file_select, kCopyTargetSlot,
                  result.state.file_select.copy_target_slot, result);
        ReadValue(memory, roots.file_select, kDeleteTargetSlot,
                  result.state.file_select.delete_target_slot, result);
        ReadValue(memory, roots.file_select, kPromptTargetY,
                  result.state.file_select.prompt_target_y, result);
        ReadValue(memory, roots.file_select, kPromptY,
                  result.state.file_select.prompt_y, result);
        ReadValue(memory, roots.file_select, kPromptVelocityY,
                  result.state.file_select.prompt_velocity_y, result);
        ReadArray(memory, roots.file_select_slot_valid, 0,
                  result.state.file_select.slot_valid, result);
        for (std::size_t slot = 0; slot < result.state.file_select.slots.size();
             ++slot) {
            const std::uint32_t slot_base = roots.file_select_slot_buffers +
                static_cast<std::uint32_t>(slot * kSlotBufferStride);
            ReadFileSelectSlotSummary(
                memory, slot_base, result.state.file_select.slots[slot], result);
        }
    }

    {
        using namespace guest_layout::name_entry;
        ReadValue(memory, roots.name_entry, kControllerState,
                  result.state.name_entry.controller_state, result);
        ReadValue(memory, roots.name_entry, kKeyboardPage,
                  result.state.name_entry.keyboard_page, result);
        ReadValue(memory, roots.name_entry, kPreviousKeyboardPage,
                  result.state.name_entry.previous_keyboard_page, result);
        ReadValue(memory, roots.name_entry, kCursorColumn,
                  result.state.name_entry.cursor_column, result);
        ReadValue(memory, roots.name_entry, kCursorRow,
                  result.state.name_entry.cursor_row, result);
        ReadValue(memory, roots.name_entry, kTouchCursorX,
                  result.state.name_entry.touch_cursor_x, result);
        ReadValue(memory, roots.name_entry, kTouchCursorY,
                  result.state.name_entry.touch_cursor_y, result);
        ReadValue(memory, roots.name_entry, kLatinVariant,
                  result.state.name_entry.latin_variant, result);
        ReadValue(memory, roots.name_entry, kNameLength,
                  result.state.name_entry.name_length, result);
        ReadValue(memory, roots.name_entry, kActionChoice,
                  result.state.name_entry.action_choice, result);
        ReadValue(memory, roots.name_entry, kConfirmationChoice,
                  result.state.name_entry.confirmation_choice, result);
        ReadValue(memory, roots.name_entry, kTouchKeyCode,
                  result.state.name_entry.touch_key_code, result);
        ReadValue(memory, roots.name_entry, kTouchColumn,
                  result.state.name_entry.touch_column, result);
        ReadValue(memory, roots.name_entry, kTouchRow,
                  result.state.name_entry.touch_row, result);
        ReadValue(memory, roots.name_entry, kSaveCommitTimer,
                  result.state.name_entry.save_commit_timer, result);
        ReadValue(memory, roots.name_entry, kLanguageIndex,
                  result.state.name_entry.language_index, result);
        ReadValue(memory, roots.name_entry, kTransitionFrame,
                  result.state.name_entry.transition_frame, result);
        ReadValue(memory, roots.name_entry, kCursorBlinkTimer,
                  result.state.name_entry.cursor_blink_timer, result);
        ReadArray(memory, roots.name_entry_buffer, 0,
                  result.state.name_entry.editable_name, result);
    }

    return result;
}

} // namespace oot3d::ui
