#pragma once

#include "oot3d_ui/ui_content_value.h"
#include "oot3d_ui/ui_semantics.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

inline constexpr std::int16_t kOot3dHealthUnitsPerHeart = 16;

enum class UiHudTimerKind : std::uint8_t {
    Primary,
    Secondary,
};

struct UiHudHealthContent {
    UiContentValue<std::int16_t> current_units;
    UiContentValue<std::uint16_t> capacity_units;
    UiContentValue<std::int16_t> current_full_hearts;
    UiContentValue<std::uint8_t> current_fraction_units;
    UiContentValue<std::uint16_t> capacity_hearts;
    UiContentValue<std::int16_t> pending_delta_units;
    UiContentValue<bool> double_defense_acquired;
};

struct UiHudMagicContent {
    UiContentValue<std::int8_t> current;
    UiContentValue<std::int16_t> capacity;
    UiContentValue<std::int8_t> level;
    UiContentValue<bool> acquired;
    UiContentValue<bool> double_magic_acquired;
    UiContentValue<MagicState> state;
    UiContentValue<MagicState> previous_state;
    UiContentValue<std::int16_t> fill_target;
    UiContentValue<std::int16_t> target;
};

struct UiHudCounterContent {
    UiContentValue<std::int16_t> rupees;
    UiContentValue<std::int16_t> pending_rupee_delta;
    UiContentValue<std::int16_t> gold_skulltula_tokens;
    UiContentValue<std::int8_t> current_dungeon_keys;
};

struct UiHudButtonContent {
    Oot3dButton button = Oot3dButton::B;
    UiContentValue<ItemId> item_id;
    UiContentValue<InventorySlot> inventory_slot;
    UiContentValue<ButtonStatus> status;
    UiContentValue<std::int8_t> ammo;
};

struct UiHudTimerContent {
    UiHudTimerKind kind = UiHudTimerKind::Primary;
    UiContentValue<std::int16_t> state;
    UiContentValue<std::int16_t> seconds;
    UiContentValue<std::int16_t> x;
    UiContentValue<std::int16_t> y;
};

struct UiHudVisibilityContent {
    UiContentValue<HudVisibilityMode> current;
    UiContentValue<HudVisibilityMode> next;
    UiContentValue<HudVisibilityMode> previous;
    UiContentValue<std::uint16_t> transition_timer;
    UiContentValue<std::uint8_t> force_rising_button_alphas;
};

struct UiHudMinigameContent {
    UiContentValue<std::uint16_t> state;
    UiContentValue<std::uint16_t> score;
};

struct UiHudWorldContext {
    UiContentValue<std::uint16_t> scene_id;
    UiContentValue<std::uint16_t> map_index;
};

using UiHudButtonContentArray =
    std::array<UiHudButtonContent, kOot3dButtonItemCount>;
using UiHudTimerContentArray = std::array<UiHudTimerContent, kOot3dTimerCount>;

struct UiHudContentSnapshot {
    UiHudHealthContent health;
    UiHudMagicContent magic;
    UiHudCounterContent counters;
    UiHudButtonContentArray buttons;
    UiHudTimerContentArray timers;
    UiHudVisibilityContent visibility;
    UiHudMinigameContent minigame;
    UiHudWorldContext world;
};

// Pure read-only projection of already captured OoT3D state. It preserves all
// five native buttons and exposes no guest layout, renderer, or action path.
UiHudContentSnapshot BuildOot3dUiHudContent(
    const Oot3dUiSemanticState& state) noexcept;

static_assert(kOot3dButtonItemCount == 5);
static_assert(kOot3dButtonStatusCount == kOot3dButtonItemCount);
static_assert(kOot3dTimerCount == 2);

} // namespace oot3d::ui
