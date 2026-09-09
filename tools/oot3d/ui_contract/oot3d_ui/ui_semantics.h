#pragma once

#include "oot3d_ui/ui_inventory_grid.h"
#include "oot3d_ui/ui_pause_gear_semantics.h"
#include "oot3d_ui/ui_pause_items_semantics.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

// These capacities are part of the OoT3D data model. They deliberately do not
// use the smaller N64 button or inventory layouts.
inline constexpr std::size_t kOot3dButtonItemCount = 5;       // B, Y, X, I, II
inline constexpr std::size_t kOot3dAssignableButtonCount = 4; // Y, X, I, II
inline constexpr std::size_t kOot3dInventoryItemCount = 26;
inline constexpr std::size_t kOot3dAmmoCount = 16;
inline constexpr std::size_t kOot3dDungeonItemCount = 20;
inline constexpr std::size_t kOot3dDungeonKeyCount = 19;
inline constexpr std::size_t kOot3dPlayerNameLength = 8;
inline constexpr std::size_t kOot3dGsFlagCount = 22;
inline constexpr std::size_t kOot3dEventCheckCount = 14;
inline constexpr std::size_t kOot3dItemGetInfoCount = 4;
inline constexpr std::size_t kOot3dInfoTableCount = 30;
inline constexpr std::size_t kOot3dBossChallengeCount = 9;
inline constexpr std::size_t kOot3dButtonStatusCount = 5;
inline constexpr std::size_t kOot3dTimerCount = 2;
inline constexpr std::size_t kOot3dEventInfoCount = 4;
inline constexpr std::size_t kOot3dDungeonFloorMetadataCount = 8;
inline constexpr std::size_t kOot3dTouchActionItemCount = 6;
inline constexpr std::size_t kOot3dFileSelectVisibleSlotCount = 3;
inline constexpr std::size_t kOot3dFileSelectSlotBufferCount = 6;
inline constexpr std::size_t kOot3dEditableNameLength = 8;

enum class Oot3dButton : std::uint8_t {
    B = 0,
    Y = 1,
    X = 2,
    I = 3,
    II = 4,
};

enum class Oot3dAssignableButton : std::uint8_t {
    Y = 0,
    X = 1,
    I = 2,
    II = 3,
};

enum class PausePage : std::uint8_t {
    None,
    Items,
    Equipment,
    QuestStatus,
    Map,
    System,
};

template <typename T>
struct SemanticValue {
    T value{};
    bool known = false;
};

template <typename T, std::size_t Size>
using SemanticArray = std::array<SemanticValue<T>, Size>;

struct ItemEquipsState {
    SemanticArray<ItemId, kOot3dButtonItemCount> button_items;
    SemanticArray<InventorySlot, kOot3dAssignableButtonCount> button_slots;
    SemanticValue<std::uint16_t> equipment;
};

struct InventoryState {
    SemanticArray<ItemId, kOot3dInventoryItemCount> items;
    SemanticArray<std::int8_t, kOot3dAmmoCount> ammo;
    SemanticValue<std::uint16_t> equipment;
    SemanticValue<std::uint32_t> upgrades;
    SemanticValue<std::uint32_t> quest_items;
    SemanticArray<std::uint8_t, kOot3dDungeonItemCount> dungeon_items;
    SemanticArray<std::int8_t, kOot3dDungeonKeyCount> dungeon_keys;
    SemanticValue<std::int8_t> defense_hearts;
    SemanticValue<std::int16_t> gold_skulltula_tokens;
};

struct HudState {
    SemanticValue<std::int16_t> health;
    SemanticValue<std::uint16_t> health_capacity;
    SemanticValue<std::int8_t> magic;
    SemanticValue<std::int16_t> magic_capacity;
    SemanticValue<std::int16_t> rupees;
    SemanticValue<HudVisibilityMode> next_visibility_mode;
    SemanticValue<HudVisibilityMode> visibility_mode;
    SemanticValue<std::uint16_t> visibility_transition_timer;
    SemanticValue<MagicState> magic_state;
    SemanticValue<std::int16_t> magic_fill_target;
    SemanticValue<std::int16_t> magic_target;
    SemanticValue<std::uint16_t> minigame_state;
    SemanticValue<std::uint16_t> minigame_score;
};

struct PauseItemsState {
    SemanticValue<std::int32_t> controller_state;
    SemanticValue<std::int32_t> arrow_selector_state;
    SemanticValue<std::int32_t> mode;
    SemanticValue<std::int32_t> source_grid_position;
    SemanticValue<std::int32_t> destination_grid_position;
    SemanticValue<std::int32_t> selected_slot;
    SemanticValue<std::int32_t> previous_slot;
    SemanticValue<std::int32_t> selected_item_id;
    SemanticValue<std::int32_t> arrow_choice_index;
    SemanticValue<std::int32_t> cursor_column;
    SemanticValue<std::int32_t> cursor_row;
    SemanticValue<std::int32_t> selection_mode;
    SemanticValue<std::int32_t> animation_step;
    SemanticValue<std::int32_t> animation_x;
    SemanticValue<std::int32_t> animation_y;
    SemanticValue<std::int32_t> pending_arrow_item_id;
    SemanticValue<std::int32_t> snapshotted_grid_column;
    SemanticValue<std::int32_t> snapshotted_grid_row;
    SemanticValue<std::int32_t> arrow_choice_column;
    SemanticValue<std::int32_t> arrow_choice_row;
    SemanticValue<std::int32_t> horizontal_input_latch;
    SemanticValue<std::int32_t> vertical_input_latch;
    SemanticValue<bool> suppress_focused_grid_icon;
};

struct PauseEquipmentState {
    SemanticValue<std::int32_t> controller_state;
    SemanticValue<std::int32_t> transition_frame;
    SemanticValue<std::int32_t> focus_slot;
    SemanticValue<std::int32_t> detail_slot;
    SemanticValue<std::int32_t> touch_slot;
    SemanticValue<bool> touch_open_request_a_latch;
    SemanticValue<bool> touch_open_request_b_latch;
    SemanticValue<std::int32_t> equip_animation_step;
    SemanticValue<std::int32_t> pending_equip_category;
    SemanticValue<std::int32_t> pending_equip_tier;
    SemanticValue<std::int32_t> equip_animation_x;
    SemanticValue<std::int32_t> pending_item_id;
    SemanticValue<bool> suppress_focused_slot_enlargement;
};

struct PauseQuestState {
    SemanticValue<std::int32_t> preview_variant_mode;
    SemanticValue<std::int32_t> selection_state;
    SemanticValue<std::int32_t> selected_entry;
    SemanticValue<std::int32_t> previous_entry;
    SemanticValue<std::int32_t> selection_animation_frame;
    SemanticValue<std::int32_t> prompt_mode;
    SemanticValue<std::int32_t> auxiliary_selection;
    SemanticValue<bool> active;
};

struct PauseWorldMapState {
    SemanticValue<std::int32_t> controller_state;
    SemanticValue<std::int32_t> cursor_column;
    SemanticValue<std::int32_t> cursor_row;
    SemanticValue<std::int32_t> previous_destination;
    SemanticValue<std::int32_t> selection_state;
    SemanticValue<bool> overlay_visible;
    SemanticValue<std::uint32_t> active_action;
    SemanticValue<std::int32_t> marker_animation_frame;
    SemanticValue<std::int32_t> marker_variant;
    SemanticValue<std::int32_t> transition_frame;
    SemanticValue<bool> destination_available;
    SemanticValue<bool> detail_available;
    SemanticValue<bool> enabled;
    SemanticValue<std::uint32_t> active_input_mask;
    SemanticValue<bool> touch_owned;
};

struct PauseDungeonMapState {
    SemanticValue<std::int32_t> cursor_selection;
    SemanticValue<std::int32_t> controller_state;
    SemanticValue<std::int32_t> transition_frame;
    SemanticValue<std::int32_t> world_destination;
    SemanticValue<std::int32_t> horizontal_input_latch;
    SemanticValue<std::int32_t> vertical_input_latch;
    SemanticValue<std::int32_t> restricted_scene;
    SemanticArray<std::uint8_t, kOot3dDungeonFloorMetadataCount> floor_metadata;
};

struct PauseSystemMenuState {
    SemanticValue<std::int32_t> save_flow_state;
    SemanticValue<std::int32_t> mode;
    SemanticValue<std::int32_t> transition_frame;
    SemanticValue<std::int32_t> option_index;
    SemanticValue<std::int32_t> primary_choice;
    SemanticValue<std::int32_t> secondary_choice;
    SemanticValue<std::int32_t> options_state;
    SemanticValue<std::int32_t> loaded_option_index;
};

struct PauseRepeatInputState {
    SemanticValue<std::int32_t> analog_direction;
    SemanticValue<std::uint32_t> held_buttons;
    SemanticValue<std::uint32_t> analog_pressed;
    SemanticValue<std::uint32_t> button_pressed;
    SemanticValue<std::int32_t> analog_hold_frames;
    SemanticValue<std::int32_t> analog_repeat_frames;
    SemanticValue<std::int32_t> button_hold_frames;
    SemanticValue<std::int32_t> button_repeat_frames;
    SemanticValue<std::uint32_t> analog_repeated;
    SemanticValue<std::uint32_t> buttons_repeated;
};

struct PauseTouchButtonsState {
    SemanticValue<std::int32_t> panel_state;
    SemanticValue<std::int32_t> selected_action;
    SemanticValue<std::int32_t> phase;
    SemanticValue<std::int32_t> selection;
    SemanticValue<std::int32_t> input_state;
    SemanticValue<std::uint32_t> disabled_mask;
    SemanticValue<std::int32_t> default_item_id;
    SemanticArray<ItemId, kOot3dTouchActionItemCount> action_item_ids;
    SemanticValue<std::int32_t> focus_state;
    SemanticValue<bool> player_action_allowed;
};

struct PauseTouchInputState {
    SemanticValue<bool> touch_pressed;
    SemanticValue<std::uint16_t> touch_x;
    SemanticValue<std::uint16_t> touch_y;
    SemanticValue<std::uint16_t> held_mask;
    SemanticValue<std::uint16_t> pressed_mask;
    SemanticValue<std::uint16_t> released_mask;
    SemanticValue<std::uint16_t> hold_frames;
    SemanticValue<std::uint16_t> repeat_frames;
    SemanticValue<std::uint16_t> repeated_mask;
};

struct PauseState {
    SemanticValue<bool> open;
    // OoT3D updates independent page controllers; no native global page field
    // has been projected into this N64-shaped convenience value.
    SemanticValue<PausePage> active_page;
    SemanticValue<std::int32_t> item_focus_slot;
    SemanticValue<std::int32_t> equipment_focus_slot;
    SemanticValue<std::uint32_t> lifecycle_state;
    SemanticValue<std::uint32_t> interaction_state;
    SemanticValue<std::uint32_t> optional_quest_panel_state;
    SemanticValue<bool> initialized;
    SemanticValue<bool> omote_ura_selector_enabled;
    SemanticValue<bool> touch_pressed;
    SemanticValue<std::uint16_t> touch_x;
    SemanticValue<std::uint16_t> touch_y;
    SemanticArray<InventorySlot, kOot3dItemMenuSlotCount> child_item_menu_slots;
    SemanticArray<InventorySlot, kOot3dItemMenuSlotCount> adult_item_menu_slots;
    PauseItemsState items;
    PauseEquipmentState equipment;
    PauseQuestState quest;
    PauseWorldMapState world_map;
    PauseDungeonMapState dungeon_map;
    PauseSystemMenuState system_menu;
    PauseRepeatInputState repeat_input;
    PauseTouchButtonsState touch_buttons;
    PauseTouchInputState touch_input;
};

struct SaveIdentityState {
    SemanticValue<std::int32_t> entrance_index;
    SemanticValue<std::int32_t> cutscene_index;
    SemanticValue<std::uint16_t> day_time;
    SemanticValue<std::int32_t> night_flag;
    SemanticArray<std::int16_t, kOot3dPlayerNameLength> player_name;
    SemanticValue<std::uint8_t> player_name_length;
    SemanticValue<std::uint8_t> z_targeting_setting;
    SemanticValue<std::uint16_t> checksum;
    SemanticValue<std::int32_t> file_num;
    SemanticValue<std::int32_t> game_mode;
};

struct QuestProgressState {
    SemanticArray<std::uint8_t, kOot3dGsFlagCount> gold_skulltula_flags;
    SemanticArray<std::uint16_t, kOot3dEventCheckCount> event_check_info;
    SemanticArray<std::uint16_t, kOot3dItemGetInfoCount> item_get_info;
    SemanticArray<std::uint16_t, kOot3dInfoTableCount> info_table;
    SemanticValue<std::uint32_t> world_map_area_data;
    SemanticArray<std::uint32_t, kOot3dBossChallengeCount> boss_battle_victories;
    SemanticArray<std::uint32_t, kOot3dBossChallengeCount> boss_battle_scores;
};

struct RuntimeUiState {
    SemanticValue<std::int16_t> rupee_accumulator;
    SemanticValue<std::int16_t> timer_state;
    SemanticValue<std::int16_t> timer_seconds;
    SemanticValue<std::int16_t> sub_timer_state;
    SemanticValue<std::int16_t> sub_timer_seconds;
    SemanticArray<std::int16_t, kOot3dTimerCount> timer_x;
    SemanticArray<std::int16_t, kOot3dTimerCount> timer_y;
    SemanticArray<ButtonStatus, kOot3dButtonStatusCount> button_status;
    SemanticValue<std::uint8_t> force_rising_button_alphas;
    SemanticValue<HudVisibilityMode> previous_hud_visibility_mode;
    SemanticValue<MagicState> previous_magic_state;
    SemanticArray<std::uint16_t, kOot3dEventInfoCount> event_info;
    SemanticValue<std::uint16_t> map_index;
    SemanticValue<std::uint16_t> next_cutscene_index;
    SemanticValue<std::uint8_t> cutscene_trigger;
    SemanticValue<std::int16_t> health_accumulator;
};

struct FileSelectSlotSummaryState {
    SemanticValue<std::uint8_t> format_version;
    SemanticValue<std::uint8_t> validation_failed;
    SemanticValue<std::uint16_t> initialization_marker;
    SemanticValue<std::int16_t> health_capacity;
    SemanticValue<std::int16_t> health;
    SemanticValue<std::uint32_t> quest_item_bits;
    SemanticValue<std::int32_t> timestamp_year;
    SemanticValue<std::int32_t> timestamp_month;
    SemanticValue<std::int32_t> timestamp_day;
    SemanticValue<std::int32_t> timestamp_hour;
    SemanticValue<std::int32_t> timestamp_minute;
    SemanticValue<std::uint16_t> checksum;
};

struct FileSelectState {
    SemanticValue<std::int32_t> controller_state;
    SemanticValue<std::int32_t> selected_slot;
    SemanticValue<std::int32_t> confirmation_choice;
    SemanticValue<std::int32_t> copy_source_slot;
    SemanticValue<std::int32_t> copy_target_slot;
    SemanticValue<std::int32_t> delete_target_slot;
    SemanticValue<float> prompt_target_y;
    SemanticValue<float> prompt_y;
    SemanticValue<float> prompt_velocity_y;
    SemanticArray<std::uint32_t, kOot3dFileSelectSlotBufferCount> slot_valid;
    std::array<FileSelectSlotSummaryState, kOot3dFileSelectSlotBufferCount> slots;
};

struct NameEntryState {
    SemanticValue<std::int32_t> controller_state;
    SemanticValue<std::int32_t> keyboard_page;
    SemanticValue<std::int32_t> previous_keyboard_page;
    SemanticValue<std::int32_t> cursor_column;
    SemanticValue<std::int32_t> cursor_row;
    SemanticValue<std::int32_t> touch_cursor_x;
    SemanticValue<std::int32_t> touch_cursor_y;
    SemanticValue<std::int32_t> latin_variant;
    SemanticValue<std::int32_t> name_length;
    SemanticValue<std::int32_t> action_choice;
    SemanticValue<std::int32_t> confirmation_choice;
    SemanticValue<std::int32_t> touch_key_code;
    SemanticValue<std::int32_t> touch_column;
    SemanticValue<std::int32_t> touch_row;
    SemanticValue<std::int32_t> save_commit_timer;
    SemanticValue<std::int32_t> language_index;
    SemanticValue<std::int32_t> transition_frame;
    SemanticValue<std::int32_t> cursor_blink_timer;
    SemanticArray<std::uint16_t, kOot3dEditableNameLength> editable_name;
};

// Host-facing semantic snapshot. It is not a binary overlay for SaveContext:
// guest layout decoding stays in the OoT3D adapter, while every UI format sees
// the same named values and explicit known/unknown state.
struct Oot3dUiSemanticState {
    SaveIdentityState save;
    SemanticValue<LinkAge> link_age;
    SemanticValue<bool> master_quest;
    SemanticValue<std::uint16_t> scene_id;
    SemanticValue<std::int8_t> magic_level;
    SemanticValue<std::uint16_t> sword_health;
    SemanticValue<std::uint16_t> navi_timer;
    SemanticValue<bool> magic_acquired;
    SemanticValue<bool> double_magic_acquired;
    SemanticValue<bool> double_defense_acquired;
    SemanticValue<std::int8_t> biggoron_sword_flag;
    ItemEquipsState child_equips;
    ItemEquipsState adult_equips;
    ItemEquipsState current_equips;
    InventoryState inventory;
    SemanticArray<ItemId, kOot3dInventoryItemCount> cached_inventory_item_ids;
    QuestProgressState quest;
    HudState hud;
    PauseState pause;
    RuntimeUiState runtime;
    FileSelectState file_select;
    NameEntryState name_entry;
};

} // namespace oot3d::ui
