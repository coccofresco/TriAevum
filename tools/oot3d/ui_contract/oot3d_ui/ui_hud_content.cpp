#include "oot3d_ui/ui_hud_content.h"

#include "oot3d_ui/ui_inventory_content.h"

#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

namespace {

template <typename T>
UiContentValue<T> FromSemanticValue(const SemanticValue<T>& source) noexcept {
    return source.known ? KnownUiContentValue(source.value) : UiContentValue<T>{};
}

UiContentValue<InventorySlot> ResolveButtonSlot(
    const Oot3dUiSemanticState& state, std::size_t button_index) noexcept {
    if (button_index == static_cast<std::size_t>(Oot3dButton::B)) {
        const auto& item = state.current_equips.button_items[button_index];
        return item.known ? ResolveOot3dInventorySlotForItem(item.value)
                          : UiContentValue<InventorySlot>{};
    }
    const std::size_t assignable_index = button_index - 1;
    return FromSemanticValue(state.current_equips.button_slots[assignable_index]);
}

UiContentValue<std::int8_t> ResolveButtonAmmo(
    const Oot3dUiSemanticState& state,
    const UiContentValue<InventorySlot>& inventory_slot) noexcept {
    if (!inventory_slot.IsKnown()) {
        return inventory_slot.state == UiContentValueState::NotApplicable
                   ? NotApplicableUiContentValue<std::int8_t>()
                   : UiContentValue<std::int8_t>{};
    }
    const std::size_t slot = static_cast<std::size_t>(UiValueRaw(inventory_slot.value));
    return slot < state.inventory.ammo.size()
               ? FromSemanticValue(state.inventory.ammo[slot])
               : NotApplicableUiContentValue<std::int8_t>();
}

UiHudHealthContent ResolveHealth(const Oot3dUiSemanticState& state) noexcept {
    UiHudHealthContent result;
    result.current_units = FromSemanticValue(state.hud.health);
    result.capacity_units = FromSemanticValue(state.hud.health_capacity);
    result.pending_delta_units = FromSemanticValue(state.runtime.health_accumulator);
    result.double_defense_acquired = FromSemanticValue(state.double_defense_acquired);

    if (state.hud.health.known && state.hud.health.value >= 0) {
        result.current_full_hearts = KnownUiContentValue(
            static_cast<std::int16_t>(state.hud.health.value / kOot3dHealthUnitsPerHeart));
        result.current_fraction_units = KnownUiContentValue(
            static_cast<std::uint8_t>(state.hud.health.value % kOot3dHealthUnitsPerHeart));
    } else if (state.hud.health.known) {
        result.current_full_hearts = NotApplicableUiContentValue<std::int16_t>();
        result.current_fraction_units = NotApplicableUiContentValue<std::uint8_t>();
    }
    if (state.hud.health_capacity.known &&
        state.hud.health_capacity.value % kOot3dHealthUnitsPerHeart == 0) {
        result.capacity_hearts = KnownUiContentValue(static_cast<std::uint16_t>(
            state.hud.health_capacity.value / kOot3dHealthUnitsPerHeart));
    } else if (state.hud.health_capacity.known) {
        result.capacity_hearts = NotApplicableUiContentValue<std::uint16_t>();
    }
    return result;
}

UiHudMagicContent ResolveMagic(const Oot3dUiSemanticState& state) noexcept {
    return {
        FromSemanticValue(state.hud.magic),
        FromSemanticValue(state.hud.magic_capacity),
        FromSemanticValue(state.magic_level),
        FromSemanticValue(state.magic_acquired),
        FromSemanticValue(state.double_magic_acquired),
        FromSemanticValue(state.hud.magic_state),
        FromSemanticValue(state.runtime.previous_magic_state),
        FromSemanticValue(state.hud.magic_fill_target),
        FromSemanticValue(state.hud.magic_target),
    };
}

UiHudCounterContent ResolveCounters(const Oot3dUiSemanticState& state) noexcept {
    UiHudCounterContent result;
    result.rupees = FromSemanticValue(state.hud.rupees);
    result.pending_rupee_delta = FromSemanticValue(state.runtime.rupee_accumulator);
    result.gold_skulltula_tokens =
        FromSemanticValue(state.inventory.gold_skulltula_tokens);
    if (!state.runtime.map_index.known) {
        return result;
    }
    const std::size_t map_index = state.runtime.map_index.value;
    result.current_dungeon_keys =
        map_index < state.inventory.dungeon_keys.size()
            ? FromSemanticValue(state.inventory.dungeon_keys[map_index])
            : NotApplicableUiContentValue<std::int8_t>();
    return result;
}

UiHudTimerContent ResolveTimer(const Oot3dUiSemanticState& state,
                               std::size_t index) noexcept {
    UiHudTimerContent result;
    result.kind = index == 0 ? UiHudTimerKind::Primary : UiHudTimerKind::Secondary;
    result.state = index == 0 ? FromSemanticValue(state.runtime.timer_state)
                              : FromSemanticValue(state.runtime.sub_timer_state);
    result.seconds = index == 0 ? FromSemanticValue(state.runtime.timer_seconds)
                                : FromSemanticValue(state.runtime.sub_timer_seconds);
    result.x = FromSemanticValue(state.runtime.timer_x[index]);
    result.y = FromSemanticValue(state.runtime.timer_y[index]);
    return result;
}

} // namespace

UiHudContentSnapshot BuildOot3dUiHudContent(
    const Oot3dUiSemanticState& state) noexcept {
    UiHudContentSnapshot result;
    result.health = ResolveHealth(state);
    result.magic = ResolveMagic(state);
    result.counters = ResolveCounters(state);

    for (std::size_t index = 0; index < result.buttons.size(); ++index) {
        UiHudButtonContent& button = result.buttons[index];
        button.button = static_cast<Oot3dButton>(index);
        button.item_id = FromSemanticValue(state.current_equips.button_items[index]);
        button.inventory_slot = ResolveButtonSlot(state, index);
        button.status = FromSemanticValue(state.runtime.button_status[index]);
        button.ammo = ResolveButtonAmmo(state, button.inventory_slot);
    }
    for (std::size_t index = 0; index < result.timers.size(); ++index) {
        result.timers[index] = ResolveTimer(state, index);
    }

    result.visibility = {
        FromSemanticValue(state.hud.visibility_mode),
        FromSemanticValue(state.hud.next_visibility_mode),
        FromSemanticValue(state.runtime.previous_hud_visibility_mode),
        FromSemanticValue(state.hud.visibility_transition_timer),
        FromSemanticValue(state.runtime.force_rising_button_alphas),
    };
    result.minigame = {
        FromSemanticValue(state.hud.minigame_state),
        FromSemanticValue(state.hud.minigame_score),
    };
    result.world = {
        FromSemanticValue(state.scene_id),
        FromSemanticValue(state.runtime.map_index),
    };
    return result;
}

} // namespace oot3d::ui
