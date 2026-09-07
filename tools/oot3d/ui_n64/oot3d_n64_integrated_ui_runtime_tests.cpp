#include "oot3d_n64_integrated_ui_runtime.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_n64_integrated_ui_runtime_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

} // namespace

int main() {
    using namespace oot3d::ui;

    N64IntegratedUiRuntime runtime;
    std::string error;
    Require(ValidateUiBackendProfile(runtime.Profile(), &error),
            "N64 integrated UI profile is invalid");
    Require(runtime.Profile().id == "n64-integrated-shadow",
            "N64 integrated UI profile identity drifted");
    for (std::size_t index = 0; index < kUiSubsystemCount; ++index) {
        const auto subsystem = static_cast<UiSubsystem>(index);
        const UiFramePlan plan = runtime.PlanFrame(subsystem);
        Require(plan.use_guest_content && plan.run_guest_mechanics &&
                    plan.forward_input_to_guest &&
                    plan.run_guest_presentation && !plan.run_host_content &&
                    !plan.run_host_mechanics && !plan.route_input_to_host &&
                    !plan.run_host_presentation,
                "shadow runtime replaced a guest UI responsibility");
    }

    Oot3dUiSemanticState state;
    state.file_select.selected_slot = {2, true};
    state.file_select.controller_state = {7, true};
    state.name_entry.controller_state = {4, true};
    state.name_entry.keyboard_page = {2, true};
    state.name_entry.latin_variant = {0, true};
    state.name_entry.cursor_column = {6, true};
    state.name_entry.cursor_row = {3, true};
    state.name_entry.name_length = {2, true};
    state.name_entry.action_choice = {1, true};
    state.name_entry.confirmation_choice = {0, true};
    state.name_entry.save_commit_timer = {7, true};
    state.name_entry.editable_name[0] = {0x004C, true};
    state.name_entry.editable_name[1] = {0x0069, true};
    runtime.ObserveState(UiBackendStateView(state));
    const auto& stats = runtime.Stats();
    Require(stats.observed_states == 1U &&
                stats.known_file_select_states == 1U &&
                stats.active_file_select_states == 1U &&
                stats.selected_slot_known && stats.selected_slot == 2 &&
                stats.controller_state_known && stats.controller_state == 7 &&
                stats.known_name_entry_states == 1U &&
                stats.active_name_entry_states == 1U &&
                stats.name_entry_controller_state_known &&
                stats.name_entry_controller_state == 4 &&
                stats.name_entry_keyboard_page_known &&
                stats.name_entry_keyboard_page == 2 &&
                stats.name_entry_cursor_known &&
                stats.name_entry_cursor_column == 6 &&
                stats.name_entry_cursor_row == 3 &&
                stats.name_entry_length_known &&
                stats.name_entry_length == 2 &&
                stats.name_entry_action_choice_known &&
                stats.name_entry_action_choice == 1 &&
                stats.name_entry_confirmation_choice_known &&
                stats.name_entry_confirmation_choice == 0 &&
                stats.name_entry_save_commit_timer_known &&
                stats.name_entry_save_commit_timer == 7,
            "typed file-select projection did not reach the N64 UI runtime");

    Oot3dUiSemanticState inactiveState = state;
    inactiveState.file_select.controller_state = {0, true};
    std::vector<UiPrimitive> inactivePrimitives;
    runtime.AppendPresentation(UiSubsystem::FileSelect,
                               UiBackendStateView(inactiveState),
                               inactivePrimitives);
    Require(inactivePrimitives.empty(),
            "inactive native file-select state emitted presentation");

    std::vector<UiPrimitive> primitives;
    runtime.AppendPresentation(UiSubsystem::FileSelect,
                               UiBackendStateView(state), primitives);
    Require(!primitives.empty(),
            "shadow runtime did not build the typed file-select preview");
    Require(std::all_of(primitives.begin(), primitives.end(),
                        [](const UiPrimitive& primitive) {
                            return primitive.subsystem ==
                                   UiSubsystem::FileSelect;
                        }),
            "file-select preview emitted a foreign subsystem primitive");
    const auto highlight = std::find_if(
        primitives.begin(), primitives.end(), [](const UiPrimitive& primitive) {
            return primitive.texture.semantic_name ==
                   "n64/file_select/highlight";
        });
    Require(highlight != primitives.end() && highlight->source_quad == 2U &&
                highlight->destination.y > 120.0F,
            "file-select highlight did not follow the typed selected slot");

    std::vector<UiPrimitive> nameEntryPrimitives;
    runtime.AppendPresentation(UiSubsystem::NameEntry,
                               UiBackendStateView(state),
                               nameEntryPrimitives);
    Require(!nameEntryPrimitives.empty() &&
                std::all_of(nameEntryPrimitives.begin(),
                            nameEntryPrimitives.end(),
                            [](const UiPrimitive& primitive) {
                                return primitive.subsystem ==
                                       UiSubsystem::NameEntry;
                            }),
            "typed name-entry state did not produce an isolated preview");
    Require(std::any_of(
                nameEntryPrimitives.begin(), nameEntryPrimitives.end(),
                [](const UiPrimitive& primitive) {
                    return primitive.texture.semantic_name ==
                           "n64/glyph/4C";
                }),
            "editable OoT3D UTF-16 name did not reach the N64 glyph layer");
    Require(std::any_of(
                nameEntryPrimitives.begin(), nameEntryPrimitives.end(),
                [](const UiPrimitive& primitive) {
                    return primitive.texture.semantic_name ==
                           "n64/name_entry/highlight/character";
                }),
            "native name-entry cursor did not reach the N64 highlight");

    Oot3dUiSemanticState confirmationState = state;
    confirmationState.name_entry.keyboard_page = {4, true};
    confirmationState.name_entry.confirmation_choice = {1, true};
    std::vector<UiPrimitive> confirmationPrimitives;
    runtime.AppendPresentation(UiSubsystem::NameEntry,
                               UiBackendStateView(confirmationState),
                               confirmationPrimitives);
    Require(std::any_of(
                confirmationPrimitives.begin(),
                confirmationPrimitives.end(),
                [](const UiPrimitive& primitive) {
                    return primitive.texture.semantic_name ==
                               "n64/name_entry/confirm/prompt";
                }) &&
                std::any_of(
                    confirmationPrimitives.begin(),
                    confirmationPrimitives.end(),
                    [](const UiPrimitive& primitive) {
                        return primitive.texture.semantic_name ==
                                   "n64/file_select/highlight" &&
                               primitive.source_quad == 0U;
                    }),
            "native confirmation state did not select the N64 yes action");
    Require(std::none_of(
                confirmationPrimitives.begin(),
                confirmationPrimitives.end(),
                [](const UiPrimitive& primitive) {
                    return primitive.texture.semantic_name ==
                           "n64/name_entry/backspace";
                }),
            "confirmation state leaked the editing keyboard");

    inactiveState.name_entry.controller_state = {0, true};
    std::vector<UiPrimitive> inactiveNameEntryPrimitives;
    runtime.AppendPresentation(UiSubsystem::NameEntry,
                               UiBackendStateView(inactiveState),
                               inactiveNameEntryPrimitives);
    Require(inactiveNameEntryPrimitives.empty(),
            "inactive native name-entry state emitted presentation");

    constexpr std::array<ItemId, kOot3dButtonItemCount> hudItems = {
        ItemId::ITEM_SWORD_KOKIRI, ItemId::ITEM_DEKU_STICK,
        ItemId::ITEM_DEKU_NUT, ItemId::ITEM_BOMB,
        ItemId::ITEM_SLINGSHOT};
    for (std::size_t index = 0; index < hudItems.size(); ++index) {
        state.current_equips.button_items[index] = {hudItems[index], true};
        state.runtime.button_status[index] = {ButtonStatus::BTN_ENABLED,
                                              true};
    }
    std::vector<UiPrimitive> hudPrimitives;
    runtime.AppendPresentation(UiSubsystem::GameplayHud,
                               UiBackendStateView(state), hudPrimitives);
    Require(std::count_if(
                hudPrimitives.begin(), hudPrimitives.end(),
                [](const UiPrimitive& primitive) {
                    return primitive.role == UiPrimitiveRole::AssignedItem;
                }) == static_cast<std::ptrdiff_t>(kOot3dButtonItemCount),
            "integrated HUD route lost an OoT3D item lane");
    Require(std::all_of(
                hudPrimitives.begin(), hudPrimitives.end(),
                [](const UiPrimitive& primitive) {
                    return primitive.subsystem == UiSubsystem::GameplayHud &&
                           IsOot3dHudTextureIdentity(primitive.texture);
                }),
            "integrated HUD route emitted a non-OoT3D texture");

    std::vector<UiPrimitive> pausePrimitives;
    runtime.AppendPresentation(UiSubsystem::PauseShell,
                               UiBackendStateView(state), pausePrimitives);
    Require(pausePrimitives.empty(),
            "frontend preview leaked into another subsystem");
    Require(!HasUiBackendIntent(runtime.HandleInput({})),
            "shadow runtime consumed input before subsystem migration");

    std::cout << "oot3d_n64_integrated_ui_runtime_tests: ok\n";
    return 0;
}
