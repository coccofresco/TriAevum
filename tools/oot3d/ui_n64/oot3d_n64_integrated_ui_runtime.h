#pragma once

#include "oot3d_n64_gameplay_hud.h"
#include "oot3d_ui/ui_backend.h"

#include <cstdint>

namespace oot3d::ui {

struct N64IntegratedUiRuntimeStats {
    std::uint64_t observed_states = 0;
    std::uint64_t known_file_select_states = 0;
    std::uint64_t active_file_select_states = 0;
    bool selected_slot_known = false;
    std::int32_t selected_slot = 0;
    bool controller_state_known = false;
    std::int32_t controller_state = 0;
    std::uint64_t known_name_entry_states = 0;
    std::uint64_t active_name_entry_states = 0;
    bool name_entry_controller_state_known = false;
    std::int32_t name_entry_controller_state = 0;
    bool name_entry_keyboard_page_known = false;
    std::int32_t name_entry_keyboard_page = 0;
    bool name_entry_cursor_known = false;
    std::int32_t name_entry_cursor_column = 0;
    std::int32_t name_entry_cursor_row = 0;
    bool name_entry_length_known = false;
    std::int32_t name_entry_length = 0;
    bool name_entry_action_choice_known = false;
    std::int32_t name_entry_action_choice = 0;
    bool name_entry_confirmation_choice_known = false;
    std::int32_t name_entry_confirmation_choice = 0;
    bool name_entry_save_commit_timer_known = false;
    std::int32_t name_entry_save_commit_timer = 0;
};

// Single-screen N64 UI owner. The initial profile deliberately observes the
// live OoT3D lifecycle without replacing it; subsystem ownership is migrated
// atomically only after its complete host workflow is available.
class N64IntegratedUiRuntime final : public UiBackend {
  public:
    N64IntegratedUiRuntime();

    const UiBackendProfile& Profile() const noexcept override;
    UiFramePlan PlanFrame(UiSubsystem subsystem) const noexcept override;
    void ObserveState(const UiBackendStateView& state) noexcept override;
    UiBackendIntent HandleInput(const UiInputEvent& input) noexcept override;
    void AppendPresentation(UiSubsystem subsystem,
                            const UiBackendStateView& state,
                            std::vector<UiPrimitive>& output) const override;

    void SetHudTextureSources(Oot3dHudTextureSources sources);

    const N64IntegratedUiRuntimeStats& Stats() const noexcept;

  private:
    UiBackendProfile profile_;
    N64IntegratedUiRuntimeStats stats_;
    Oot3dHudTextureSources hud_texture_sources_;
    Oot3dHudAssetCatalog hud_asset_catalog_;
    N64GameplayHudLayout hud_layout_;
};

} // namespace oot3d::ui
