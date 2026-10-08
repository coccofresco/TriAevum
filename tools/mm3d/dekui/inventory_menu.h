// Ported from mm3d-dekui src/mm3d/inventory_menu.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include "native_inventory.h"
#include "pause_capture.h"
#include "item_preview.h"
#include "items_map_prompt.h"
#include "menu_tabs.h"
#include "menu_help.h"

namespace Mm3dDekui::InventoryMenu {
struct Options {
    MenuHelp::Options help;
    GearMenu::Options gear;
    bool enabled=true; float x=0.f,y=32.f,scale=.8f;
    float masks_x=0.f,masks_y=0.f,masks_scale=1.f;
    bool shared_slide=true;
    bool high_res_pause=true;
    bool bar_enabled=true; float bar_x=0.f,bar_y=-13.6f,bar_scale=.65f,bar_spacing=64.f;
    float description_x=0.f,description_y=0.f,description_scale=1.f;
    float model_x=0.f,model_y=0.f,model_scale=1.f;
    NativeInventory::ButtonPose map_prompt=ItemsMapPrompt::DefaultPose,gear_button{};
    std::array<NativeInventory::ButtonPose,3> map_prompt_elements=ItemsMapPrompt::DefaultElements;
    bool buttons_enabled=true;
    std::array<NativeInventory::ButtonPose,3> buttons{};
    bool background_enabled=true;
    NativeInventory::ButtonPose background{};
    float shadow_width=1.f,shadow_opacity=1.f;
};
inline void Apply(const Options& o) {
    MenuHelp::Apply(o.help);
    GearMenu::Apply(o.gear);
    MenuTabs::gear_pose.x=o.gear_button.x;
    MenuTabs::gear_pose.y=o.gear_button.y;
    MenuTabs::gear_pose.scale=o.gear_button.scale;
    ItemsMapPrompt::pose.x=o.map_prompt.x;
    ItemsMapPrompt::pose.y=o.map_prompt.y;
    ItemsMapPrompt::pose.scale=o.map_prompt.scale;
    for(unsigned i=0;i<o.map_prompt_elements.size();++i) {
        ItemsMapPrompt::elements[i].x=o.map_prompt_elements[i].x;
        ItemsMapPrompt::elements[i].y=o.map_prompt_elements[i].y;
        ItemsMapPrompt::elements[i].scale=o.map_prompt_elements[i].scale;
    }
    NativeInventory::x.store(o.x); NativeInventory::y.store(o.y);
    NativeInventory::scale.store(o.scale); NativeInventory::enabled.store(o.enabled);
    NativeInventory::masks_x.store(o.masks_x); NativeInventory::masks_y.store(o.masks_y);
    NativeInventory::masks_scale.store(o.masks_scale);
    NativeInventory::shared_slide.store(o.shared_slide);
    PauseCapture::enabled.store(o.high_res_pause);
    NativeInventory::bar_enabled.store(o.bar_enabled);
    NativeInventory::bar_x.store(o.bar_x); NativeInventory::bar_y.store(o.bar_y);
    NativeInventory::bar_scale.store(o.bar_scale); NativeInventory::bar_spacing.store(o.bar_spacing);
    DialogueScale::item_x.store(o.description_x); DialogueScale::item_y.store(o.description_y);
    DialogueScale::item_scale.store(o.description_scale);
    ItemPreview::model_x.store(o.model_x); ItemPreview::model_y.store(o.model_y);
    ItemPreview::model_scale.store(o.model_scale);
    NativeInventory::buttons_enabled.store(o.buttons_enabled);
    NativeInventory::shadow_width.store(o.shadow_width);
    NativeInventory::shadow_opacity.store(o.shadow_opacity);
    NativeInventory::background_enabled.store(o.background_enabled);
    NativeInventory::background_pose.x.store(o.background.x);
    NativeInventory::background_pose.y.store(o.background.y);
    NativeInventory::background_pose.scale.store(o.background.scale);
    for(unsigned i=0;i<o.buttons.size();++i) {
        NativeInventory::buttons[i].x.store(o.buttons[i].x);
        NativeInventory::buttons[i].y.store(o.buttons[i].y);
        NativeInventory::buttons[i].scale.store(o.buttons[i].scale);
    }
}
// Undo only the four widgets edited by the old experiment; preserve other mods.
// Host setup/file mutation is outside the guest component.
inline std::string RestoreLegacy() {
    return "Legacy inventory archive restoration is unavailable in the guest core.";
}
}
