#pragma once

#include "oot3d_ui/ui_frontend_menu_content.h"
#include "oot3d_ui/ui_hud_content.h"
#include "oot3d_ui/ui_inventory_content.h"
#include "oot3d_ui/ui_pause_content.h"
#include "oot3d_ui/ui_persistent_content.h"
#include "oot3d_ui/ui_semantics.h"

namespace oot3d::ui {

// Ephemeral backend view over one captured OoT3D state. Derived projections are
// built only when a backend requests them, so the native identity backend adds
// no per-frame content-building work.
class UiBackendStateView {
public:
    explicit UiBackendStateView(
        const Oot3dUiSemanticState& state) noexcept : state_(state) {}

    const Oot3dUiSemanticState& NativeSemanticState() const noexcept {
        return state_;
    }

    UiHudContentSnapshot BuildHudContent() const noexcept {
        return BuildOot3dUiHudContent(state_);
    }

    UiInventoryContentSnapshot BuildInventoryContent() const noexcept {
        return BuildOot3dUiInventoryContent(state_);
    }

    UiPauseContentSnapshot BuildPauseContent() const noexcept {
        return BuildOot3dUiPauseContent(state_);
    }

    UiPersistentContentSnapshot BuildPersistentContent() const noexcept {
        return BuildOot3dUiPersistentContent(state_);
    }

    UiFrontendMenuContentSnapshot BuildFrontendMenuContent() const noexcept {
        return BuildOot3dUiFrontendMenuContent(state_);
    }

private:
    const Oot3dUiSemanticState& state_;
};

} // namespace oot3d::ui
