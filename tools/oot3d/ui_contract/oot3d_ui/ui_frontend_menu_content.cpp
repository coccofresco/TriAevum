#include "oot3d_ui/ui_frontend_menu_content.h"

#include "oot3d_ui/ui_hud_content.h"

#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

namespace {

template <typename T>
UiContentValue<T> FromSemanticValue(const SemanticValue<T>& source) noexcept {
    return source.known ? KnownUiContentValue(source.value) : UiContentValue<T>{};
}

template <typename T, std::size_t Size>
std::array<UiContentValue<T>, Size> FromSemanticArray(
    const SemanticArray<T, Size>& source) noexcept {
    std::array<UiContentValue<T>, Size> result{};
    for (std::size_t index = 0; index < Size; ++index) {
        result[index] = FromSemanticValue(source[index]);
    }
    return result;
}

UiContentValue<UiFileSelectSlotIndex> ResolveVisibleSlot(
    const SemanticValue<std::int32_t>& source) noexcept {
    if (!source.known) {
        return {};
    }
    const UiFileSelectSlotIndex slot =
        UiFileSelectSlotIndex::FromNative(source.value);
    return slot.IsValid()
               ? KnownUiContentValue(slot)
               : NotApplicableUiContentValue<UiFileSelectSlotIndex>();
}

UiFileSelectSlotSummaryContent ResolveSlotSummary(
    std::size_t slot_index, const SemanticValue<std::uint32_t>& validity,
    const FileSelectSlotSummaryState& source) noexcept {
    UiFileSelectSlotSummaryContent result;
    result.storage_index = static_cast<std::uint8_t>(slot_index);
    result.validity_flag = FromSemanticValue(validity);
    if (validity.known) {
        result.valid = KnownUiContentValue(validity.value != 0);
    }
    result.format_version = FromSemanticValue(source.format_version);
    result.validation_failed = FromSemanticValue(source.validation_failed);
    result.initialization_marker = FromSemanticValue(source.initialization_marker);
    result.health_capacity = FromSemanticValue(source.health_capacity);
    result.health = FromSemanticValue(source.health);
    result.quest_item_bits = FromSemanticValue(source.quest_item_bits);
    result.timestamp_year = FromSemanticValue(source.timestamp_year);
    result.timestamp_month = FromSemanticValue(source.timestamp_month);
    result.timestamp_day = FromSemanticValue(source.timestamp_day);
    result.timestamp_hour = FromSemanticValue(source.timestamp_hour);
    result.timestamp_minute = FromSemanticValue(source.timestamp_minute);
    result.checksum = FromSemanticValue(source.checksum);

    if (source.health.known && source.health.value >= 0) {
        result.current_full_hearts = KnownUiContentValue(
            static_cast<std::int16_t>(source.health.value /
                                      kOot3dHealthUnitsPerHeart));
        result.current_fraction_units = KnownUiContentValue(
            static_cast<std::uint8_t>(source.health.value %
                                      kOot3dHealthUnitsPerHeart));
    } else if (source.health.known) {
        result.current_full_hearts =
            NotApplicableUiContentValue<std::int16_t>();
        result.current_fraction_units =
            NotApplicableUiContentValue<std::uint8_t>();
    }
    if (source.health_capacity.known && source.health_capacity.value >= 0 &&
        source.health_capacity.value % kOot3dHealthUnitsPerHeart == 0) {
        result.capacity_hearts = KnownUiContentValue(
            static_cast<std::int16_t>(source.health_capacity.value /
                                      kOot3dHealthUnitsPerHeart));
    } else if (source.health_capacity.known) {
        result.capacity_hearts =
            NotApplicableUiContentValue<std::int16_t>();
    }
    return result;
}

UiFileSelectContent ResolveFileSelect(const FileSelectState& source) noexcept {
    UiFileSelectContent result;
    result.controller_state = FromSemanticValue(source.controller_state);
    result.selected_slot = FromSemanticValue(source.selected_slot);
    result.confirmation_choice = FromSemanticValue(source.confirmation_choice);
    result.copy_source_slot = FromSemanticValue(source.copy_source_slot);
    result.copy_target_slot = FromSemanticValue(source.copy_target_slot);
    result.delete_target_slot = FromSemanticValue(source.delete_target_slot);
    result.prompt_target_y = FromSemanticValue(source.prompt_target_y);
    result.prompt_y = FromSemanticValue(source.prompt_y);
    result.prompt_velocity_y = FromSemanticValue(source.prompt_velocity_y);
    result.selected_visible_slot = ResolveVisibleSlot(source.selected_slot);
    result.copy_source_visible_slot = ResolveVisibleSlot(source.copy_source_slot);
    result.copy_target_visible_slot = ResolveVisibleSlot(source.copy_target_slot);
    result.delete_target_visible_slot = ResolveVisibleSlot(source.delete_target_slot);
    for (std::size_t slot = 0; slot < result.slots.size(); ++slot) {
        result.slots[slot] = ResolveSlotSummary(
            slot, source.slot_valid[slot], source.slots[slot]);
    }
    return result;
}

UiNameEntryContent ResolveNameEntry(const NameEntryState& source) noexcept {
    UiNameEntryContent result;
    result.controller_state = FromSemanticValue(source.controller_state);
    result.keyboard_page = FromSemanticValue(source.keyboard_page);
    result.previous_keyboard_page = FromSemanticValue(source.previous_keyboard_page);
    result.cursor_column = FromSemanticValue(source.cursor_column);
    result.cursor_row = FromSemanticValue(source.cursor_row);
    result.touch_cursor_x = FromSemanticValue(source.touch_cursor_x);
    result.touch_cursor_y = FromSemanticValue(source.touch_cursor_y);
    result.latin_variant = FromSemanticValue(source.latin_variant);
    result.name_length = FromSemanticValue(source.name_length);
    result.action_choice = FromSemanticValue(source.action_choice);
    result.confirmation_choice = FromSemanticValue(source.confirmation_choice);
    result.touch_key_code = FromSemanticValue(source.touch_key_code);
    result.touch_column = FromSemanticValue(source.touch_column);
    result.touch_row = FromSemanticValue(source.touch_row);
    result.save_commit_timer = FromSemanticValue(source.save_commit_timer);
    result.language_index = FromSemanticValue(source.language_index);
    result.transition_frame = FromSemanticValue(source.transition_frame);
    result.cursor_blink_timer = FromSemanticValue(source.cursor_blink_timer);
    result.editable_name = FromSemanticArray(source.editable_name);

    if (!source.name_length.known) {
        return result;
    }
    if (source.name_length.value < 0 ||
        source.name_length.value >
            static_cast<std::int32_t>(kOot3dEditableNameLength)) {
        result.validated_name_length =
            NotApplicableUiContentValue<std::uint8_t>();
        result.active_name.fill(
            NotApplicableUiContentValue<std::uint16_t>());
        return result;
    }
    const std::size_t length = static_cast<std::size_t>(source.name_length.value);
    result.validated_name_length = KnownUiContentValue(
        static_cast<std::uint8_t>(source.name_length.value));
    for (std::size_t index = 0; index < result.active_name.size(); ++index) {
        result.active_name[index] = index < length
            ? FromSemanticValue(source.editable_name[index])
            : NotApplicableUiContentValue<std::uint16_t>();
    }
    return result;
}

} // namespace

UiFrontendMenuContentSnapshot BuildOot3dUiFrontendMenuContent(
    const Oot3dUiSemanticState& state) noexcept {
    return {
        ResolveFileSelect(state.file_select),
        ResolveNameEntry(state.name_entry),
    };
}

} // namespace oot3d::ui
