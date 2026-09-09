#pragma once

#include "oot3d_ui/ui_content_value.h"
#include "oot3d_ui/ui_semantics.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

inline constexpr std::uint8_t kInvalidFileSelectSlotIndex = 0xFF;

class UiFileSelectSlotIndex {
public:
    constexpr UiFileSelectSlotIndex() noexcept = default;

    static constexpr UiFileSelectSlotIndex FromNative(
        std::int32_t value) noexcept {
        return value >= 0 && value <
                   static_cast<std::int32_t>(kOot3dFileSelectVisibleSlotCount)
                   ? UiFileSelectSlotIndex(static_cast<std::uint8_t>(value))
                   : UiFileSelectSlotIndex();
    }

    constexpr bool IsValid() const noexcept {
        return value_ < kOot3dFileSelectVisibleSlotCount;
    }

    constexpr std::uint8_t Raw() const noexcept {
        return value_;
    }

private:
    explicit constexpr UiFileSelectSlotIndex(std::uint8_t value) noexcept
        : value_(value) {}

    std::uint8_t value_ = kInvalidFileSelectSlotIndex;
};

struct UiFileSelectSlotSummaryContent {
    std::uint8_t storage_index = 0;
    UiContentValue<std::uint32_t> validity_flag;
    UiContentValue<bool> valid;
    UiContentValue<std::uint8_t> format_version;
    UiContentValue<std::uint8_t> validation_failed;
    UiContentValue<std::uint16_t> initialization_marker;
    UiContentValue<std::int16_t> health_capacity;
    UiContentValue<std::int16_t> health;
    UiContentValue<std::int16_t> current_full_hearts;
    UiContentValue<std::uint8_t> current_fraction_units;
    UiContentValue<std::int16_t> capacity_hearts;
    UiContentValue<std::uint32_t> quest_item_bits;
    UiContentValue<std::int32_t> timestamp_year;
    UiContentValue<std::int32_t> timestamp_month;
    UiContentValue<std::int32_t> timestamp_day;
    UiContentValue<std::int32_t> timestamp_hour;
    UiContentValue<std::int32_t> timestamp_minute;
    UiContentValue<std::uint16_t> checksum;
};

struct UiFileSelectContent {
    UiContentValue<std::int32_t> controller_state;
    UiContentValue<std::int32_t> selected_slot;
    UiContentValue<std::int32_t> confirmation_choice;
    UiContentValue<std::int32_t> copy_source_slot;
    UiContentValue<std::int32_t> copy_target_slot;
    UiContentValue<std::int32_t> delete_target_slot;
    UiContentValue<float> prompt_target_y;
    UiContentValue<float> prompt_y;
    UiContentValue<float> prompt_velocity_y;
    UiContentValue<UiFileSelectSlotIndex> selected_visible_slot;
    UiContentValue<UiFileSelectSlotIndex> copy_source_visible_slot;
    UiContentValue<UiFileSelectSlotIndex> copy_target_visible_slot;
    UiContentValue<UiFileSelectSlotIndex> delete_target_visible_slot;
    std::array<UiFileSelectSlotSummaryContent,
               kOot3dFileSelectSlotBufferCount> slots;
};

struct UiNameEntryContent {
    UiContentValue<std::int32_t> controller_state;
    UiContentValue<std::int32_t> keyboard_page;
    UiContentValue<std::int32_t> previous_keyboard_page;
    UiContentValue<std::int32_t> cursor_column;
    UiContentValue<std::int32_t> cursor_row;
    UiContentValue<std::int32_t> touch_cursor_x;
    UiContentValue<std::int32_t> touch_cursor_y;
    UiContentValue<std::int32_t> latin_variant;
    UiContentValue<std::int32_t> name_length;
    UiContentValue<std::int32_t> action_choice;
    UiContentValue<std::int32_t> confirmation_choice;
    UiContentValue<std::int32_t> touch_key_code;
    UiContentValue<std::int32_t> touch_column;
    UiContentValue<std::int32_t> touch_row;
    UiContentValue<std::int32_t> save_commit_timer;
    UiContentValue<std::int32_t> language_index;
    UiContentValue<std::int32_t> transition_frame;
    UiContentValue<std::int32_t> cursor_blink_timer;
    std::array<UiContentValue<std::uint16_t>, kOot3dEditableNameLength>
        editable_name;
    UiContentValue<std::uint8_t> validated_name_length;
    std::array<UiContentValue<std::uint16_t>, kOot3dEditableNameLength>
        active_name;
};

struct UiFrontendMenuContentSnapshot {
    UiFileSelectContent file_select;
    UiNameEntryContent name_entry;
};

UiFrontendMenuContentSnapshot BuildOot3dUiFrontendMenuContent(
    const Oot3dUiSemanticState& state) noexcept;

static_assert(sizeof(UiFileSelectSlotIndex) == sizeof(std::uint8_t));
static_assert(UiFileSelectSlotIndex::FromNative(2).IsValid());
static_assert(!UiFileSelectSlotIndex::FromNative(3).IsValid());
static_assert(kOot3dFileSelectSlotBufferCount == 6);
static_assert(kOot3dEditableNameLength == 8);

} // namespace oot3d::ui
