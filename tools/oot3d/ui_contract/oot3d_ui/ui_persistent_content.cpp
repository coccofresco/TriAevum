#include "oot3d_ui/ui_persistent_content.h"

#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

namespace {

template <typename T>
UiContentValue<T> FromSemanticValue(const SemanticValue<T>& source) noexcept {
    return source.known ? KnownUiContentValue(source.value) : UiContentValue<T>{};
}

template <typename T, std::size_t Size>
UiPersistentContentArray<T, Size> FromSemanticArray(
    const SemanticArray<T, Size>& source) noexcept {
    UiPersistentContentArray<T, Size> result{};
    for (std::size_t index = 0; index < Size; ++index) {
        result[index] = FromSemanticValue(source[index]);
    }
    return result;
}

UiPlayerNameContent ResolvePlayerName(const SaveIdentityState& source) noexcept {
    UiPlayerNameContent result;
    result.raw_code_units = FromSemanticArray(source.player_name);
    result.stored_length = FromSemanticValue(source.player_name_length);
    if (!source.player_name_length.known) {
        return result;
    }
    if (source.player_name_length.value > kOot3dPlayerNameLength) {
        result.validated_length = NotApplicableUiContentValue<std::uint8_t>();
        result.active_code_units.fill(
            NotApplicableUiContentValue<std::uint16_t>());
        return result;
    }

    const std::size_t length = source.player_name_length.value;
    result.validated_length = KnownUiContentValue(source.player_name_length.value);
    for (std::size_t index = 0; index < result.active_code_units.size(); ++index) {
        if (index >= length) {
            result.active_code_units[index] =
                NotApplicableUiContentValue<std::uint16_t>();
        } else if (source.player_name[index].known) {
            result.active_code_units[index] = KnownUiContentValue(
                static_cast<std::uint16_t>(source.player_name[index].value));
        }
    }
    return result;
}

UiSaveIdentityContent ResolveIdentity(const Oot3dUiSemanticState& state) noexcept {
    return {
        FromSemanticValue(state.save.entrance_index),
        FromSemanticValue(state.save.cutscene_index),
        FromSemanticValue(state.save.day_time),
        FromSemanticValue(state.save.night_flag),
        ResolvePlayerName(state.save),
        FromSemanticValue(state.save.z_targeting_setting),
        FromSemanticValue(state.save.checksum),
        FromSemanticValue(state.save.file_num),
        FromSemanticValue(state.save.game_mode),
    };
}

UiPersistentPlayerContent ResolvePlayer(
    const Oot3dUiSemanticState& state) noexcept {
    return {
        FromSemanticValue(state.link_age),
        FromSemanticValue(state.master_quest),
        FromSemanticValue(state.sword_health),
        FromSemanticValue(state.navi_timer),
        FromSemanticValue(state.biggoron_sword_flag),
        FromSemanticValue(state.inventory.defense_hearts),
    };
}

UiSavedLoadoutContent ResolveLoadout(const ItemEquipsState& source) noexcept {
    return {
        FromSemanticArray(source.button_items),
        FromSemanticArray(source.button_slots),
        FromSemanticValue(source.equipment),
    };
}

UiQuestProgressContent ResolveQuestProgress(
    const QuestProgressState& source) noexcept {
    return {
        FromSemanticArray(source.gold_skulltula_flags),
        FromSemanticArray(source.event_check_info),
        FromSemanticArray(source.item_get_info),
        FromSemanticArray(source.info_table),
        FromSemanticValue(source.world_map_area_data),
        FromSemanticArray(source.boss_battle_victories),
        FromSemanticArray(source.boss_battle_scores),
    };
}

} // namespace

UiPersistentContentSnapshot BuildOot3dUiPersistentContent(
    const Oot3dUiSemanticState& state) noexcept {
    return {
        ResolveIdentity(state),
        ResolvePlayer(state),
        ResolveLoadout(state.child_equips),
        ResolveLoadout(state.adult_equips),
        ResolveLoadout(state.current_equips),
        {FromSemanticArray(state.cached_inventory_item_ids)},
        ResolveQuestProgress(state.quest),
    };
}

} // namespace oot3d::ui
