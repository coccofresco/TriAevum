#pragma once

#include "oot3d_ui/ui_content_value.h"
#include "oot3d_ui/ui_semantics.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

template <typename T, std::size_t Size>
using UiPauseContentArray = std::array<UiContentValue<T>, Size>;

struct UiPauseRootContent {
    UiContentValue<bool> open;
    UiContentValue<std::uint32_t> lifecycle_state;
    UiContentValue<std::uint32_t> interaction_state;
    UiContentValue<std::uint32_t> optional_quest_panel_state;
    UiContentValue<bool> initialized;
    UiContentValue<bool> omote_ura_selector_enabled;
    UiContentValue<bool> touch_pressed;
    UiContentValue<std::uint16_t> touch_x;
    UiContentValue<std::uint16_t> touch_y;
};

struct UiPauseItemsContent {
    UiContentValue<std::int32_t> controller_state;
    UiContentValue<std::int32_t> arrow_selector_state;
    UiContentValue<std::int32_t> mode;
    UiContentValue<std::int32_t> source_grid_position;
    UiContentValue<std::int32_t> destination_grid_position;
    UiContentValue<std::int32_t> selected_slot;
    UiContentValue<std::int32_t> previous_slot;
    UiContentValue<std::int32_t> selected_item_id;
    UiContentValue<std::int32_t> arrow_choice_index;
    UiContentValue<std::int32_t> cursor_column;
    UiContentValue<std::int32_t> cursor_row;
    UiContentValue<std::int32_t> selection_mode;
    UiContentValue<std::int32_t> animation_step;
    UiContentValue<std::int32_t> animation_x;
    UiContentValue<std::int32_t> animation_y;
    UiContentValue<std::int32_t> pending_arrow_item_id;
    UiContentValue<std::int32_t> snapshotted_grid_column;
    UiContentValue<std::int32_t> snapshotted_grid_row;
    UiContentValue<std::int32_t> arrow_choice_column;
    UiContentValue<std::int32_t> arrow_choice_row;
    UiContentValue<std::int32_t> horizontal_input_latch;
    UiContentValue<std::int32_t> vertical_input_latch;
    UiContentValue<bool> suppress_focused_grid_icon;

    // Safe views keep native signed sentinel-bearing values above intact.
    UiContentValue<PauseItemGridPosition> source_position;
    UiContentValue<PauseItemGridPosition> destination_position;
    UiContentValue<PauseItemGridPosition> selected_grid_position;
    UiContentValue<PauseItemGridPosition> previous_grid_position;
    UiContentValue<ItemId> selected_item;
    UiContentValue<ItemId> pending_arrow_item;
    UiContentValue<Oot3dArrowTypeChoice> arrow_choice;
};

struct UiPauseEquipmentContent {
    UiContentValue<std::int32_t> controller_state;
    UiContentValue<std::int32_t> transition_frame;
    UiContentValue<std::int32_t> focus_slot;
    UiContentValue<std::int32_t> detail_slot;
    UiContentValue<std::int32_t> touch_slot;
    UiContentValue<bool> touch_open_request_a_latch;
    UiContentValue<bool> touch_open_request_b_latch;
    UiContentValue<std::int32_t> equip_animation_step;
    UiContentValue<std::int32_t> pending_equip_category;
    UiContentValue<std::int32_t> pending_equip_tier;
    UiContentValue<std::int32_t> equip_animation_x;
    UiContentValue<std::int32_t> pending_item_id;
    UiContentValue<bool> suppress_focused_slot_enlargement;

    // Validated views sit beside the exact signed native values above.
    UiContentValue<Oot3dPauseGearPageState> state;
    UiContentValue<Oot3dPauseGearSlot> focused_slot;
    UiContentValue<Oot3dPauseGearSlot> touched_slot;
    UiContentValue<EquipmentType> pending_category;
    UiContentValue<std::uint8_t> pending_equipment_value;
    UiContentValue<ItemId> pending_item;

    struct SlotContent {
        Oot3dPauseGearSlot slot = Oot3dPauseGearSlot::KokiriSword;
        Oot3dPauseGearSlotRole role = Oot3dPauseGearSlotRole::Equipment;
        bool directly_equippable = false;
        UiContentValue<ItemId> item_id;
        UiContentValue<bool> owned;
        UiContentValue<bool> age_allowed;
        UiContentValue<bool> equipped;
        UiContentValue<std::uint16_t> quantity;
    };
    std::array<SlotContent, kOot3dPauseGearSlotCount> slots;
};

struct UiPauseQuestContent {
    UiContentValue<std::int32_t> preview_variant_mode;
    UiContentValue<std::int32_t> selection_state;
    UiContentValue<std::int32_t> selected_entry;
    UiContentValue<std::int32_t> previous_entry;
    UiContentValue<std::int32_t> selection_animation_frame;
    UiContentValue<std::int32_t> prompt_mode;
    UiContentValue<std::int32_t> auxiliary_selection;
    UiContentValue<bool> active;
};

struct UiPauseWorldMapContent {
    UiContentValue<std::int32_t> controller_state;
    UiContentValue<std::int32_t> cursor_column;
    UiContentValue<std::int32_t> cursor_row;
    UiContentValue<std::int32_t> previous_destination;
    UiContentValue<std::int32_t> selection_state;
    UiContentValue<bool> overlay_visible;
    UiContentValue<std::uint32_t> active_action;
    UiContentValue<std::int32_t> marker_animation_frame;
    UiContentValue<std::int32_t> marker_variant;
    UiContentValue<std::int32_t> transition_frame;
    UiContentValue<bool> destination_available;
    UiContentValue<bool> detail_available;
    UiContentValue<bool> enabled;
    UiContentValue<std::uint32_t> active_input_mask;
    UiContentValue<bool> touch_owned;
};

struct UiPauseDungeonMapContent {
    UiContentValue<std::int32_t> cursor_selection;
    UiContentValue<std::int32_t> controller_state;
    UiContentValue<std::int32_t> transition_frame;
    UiContentValue<std::int32_t> world_destination;
    UiContentValue<std::int32_t> horizontal_input_latch;
    UiContentValue<std::int32_t> vertical_input_latch;
    UiContentValue<std::int32_t> restricted_scene;
    UiPauseContentArray<std::uint8_t, kOot3dDungeonFloorMetadataCount> floor_metadata;
};

struct UiPauseSystemMenuContent {
    UiContentValue<std::int32_t> save_flow_state;
    UiContentValue<std::int32_t> mode;
    UiContentValue<std::int32_t> transition_frame;
    UiContentValue<std::int32_t> option_index;
    UiContentValue<std::int32_t> primary_choice;
    UiContentValue<std::int32_t> secondary_choice;
    UiContentValue<std::int32_t> options_state;
    UiContentValue<std::int32_t> loaded_option_index;
};

struct UiPauseRepeatInputContent {
    UiContentValue<std::int32_t> analog_direction;
    UiContentValue<std::uint32_t> held_buttons;
    UiContentValue<std::uint32_t> analog_pressed;
    UiContentValue<std::uint32_t> button_pressed;
    UiContentValue<std::int32_t> analog_hold_frames;
    UiContentValue<std::int32_t> analog_repeat_frames;
    UiContentValue<std::int32_t> button_hold_frames;
    UiContentValue<std::int32_t> button_repeat_frames;
    UiContentValue<std::uint32_t> analog_repeated;
    UiContentValue<std::uint32_t> buttons_repeated;
};

struct UiPauseTouchButtonsContent {
    UiContentValue<std::int32_t> panel_state;
    UiContentValue<std::int32_t> selected_action;
    UiContentValue<std::int32_t> phase;
    UiContentValue<std::int32_t> selection;
    UiContentValue<std::int32_t> input_state;
    UiContentValue<std::uint32_t> disabled_mask;
    UiContentValue<std::int32_t> default_item_id;
    UiPauseContentArray<ItemId, kOot3dTouchActionItemCount> action_item_ids;
    UiContentValue<std::int32_t> focus_state;
    UiContentValue<bool> player_action_allowed;
};

struct UiPauseTouchInputContent {
    UiContentValue<bool> touch_pressed;
    UiContentValue<std::uint16_t> touch_x;
    UiContentValue<std::uint16_t> touch_y;
    UiContentValue<std::uint16_t> held_mask;
    UiContentValue<std::uint16_t> pressed_mask;
    UiContentValue<std::uint16_t> released_mask;
    UiContentValue<std::uint16_t> hold_frames;
    UiContentValue<std::uint16_t> repeat_frames;
    UiContentValue<std::uint16_t> repeated_mask;
};

struct UiPauseContentSnapshot {
    UiPauseRootContent root;
    UiPauseItemsContent items;
    UiPauseEquipmentContent equipment;
    UiPauseQuestContent quest;
    UiPauseWorldMapContent world_map;
    UiPauseDungeonMapContent dungeon_map;
    UiPauseSystemMenuContent system_menu;
    UiPauseRepeatInputContent repeat_input;
    UiPauseTouchButtonsContent touch_buttons;
    UiPauseTouchInputContent touch_input;
};

// Pure read-only projection. Numeric controller states and sentinels are kept;
// only independently validated item views are added.
UiPauseContentSnapshot BuildOot3dUiPauseContent(
    const Oot3dUiSemanticState& state) noexcept;

static_assert(kOot3dDungeonFloorMetadataCount == 8);
static_assert(kOot3dTouchActionItemCount == 6);

} // namespace oot3d::ui
