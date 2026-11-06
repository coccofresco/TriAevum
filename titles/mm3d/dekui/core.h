// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from mm3d-dekui src/core/arm/dynarmic/arm_dynarmic.cpp.
// Copyright 2016-2026 Citra Emulator Project / Azahar Emulator Project.
#pragma once
#include "host_interfaces.h"
#include "tatl_reminders.h"
#include "bomber_approach.h"
#include "owl_intro.h"
#include "actor_draw_distance.h"
#include "prop_draw_distance.h"
#include "vegetation_draw_distance.h"

#include "form_camera.h"
#include "camera_live.h"
#include "minimap.h"
#include "native_minimap.h"
#include "minimap_shortcut.h"
#include "native_inventory.h"
#include "dungeon_map.h"
#include "difficulty.h"
#include "death_penalties.h"
#include "bottle_healing.h"
#include "inventory_shadow.h"
#include "items_map_prompt.h"
#include "menu_help.h"
#include "world_map_prompt.h"
#include "menu_tabs.h"
#include "pause_capture.h"
#include "native_notebook.h"
#include "native_file_select.h"
#include "file_select_prompt.h"
#include "ocarina_prompt.h"
#include "magic_border.h"
#include "minigame_hud.h"
#include "player_hud.h"
#include "song_list.h"
#include "double_time.h"
#include "song_reset.h"
#include "hud_buttons.h"
#include "hud_item_background.h"
#include "banners.h"
#include "song_effect.h"
#include "native_item_present.h"
#include "inventory_navigation.h"
#include "menu_page_switch.h"
#include "menu_memory.h"
#include "single_screen_text.h"
#include "prices.h"
#include "menu_sound.h"
#include "music.h"
#include "menu_close.h"
#include "menu_shortcuts.h"
#include "options_menu.h"
#include "native_startup.h"
#include "item_preview.h"
#include "dialogue_scale.h"
#include "widescreen.h"

namespace Mm3dDekui {
// Own one Core per guest execution context, as the donor owns its callbacks.
// Settings atomics retain donor process-wide ownership. Call on the guest thread.
class Core {
public:
    explicit Core(HostFacts facts = {}) : host(std::move(facts)) {}
    U32 Instruction(Memory&,U32 pc,U32 word);
    // Caller checks the replacement's condition and presets r15=pc+4.
    // Continue at r15 afterwards; unknown SVCs are inert, for caller routing.
    void CallSvc(U32 swi,Cpu&,Memory&);
    static std::vector<U32> CandidatePcs();
    HostFacts host;
private:
    std::int64_t GlobalTimeUs() const { return host.global_time_us ? host.global_time_us() : 0; }
    bool WidescreenActive() const {
        return host.widescreen_active ? host.widescreen_active() : Widescreen::Active();
    }

    Mm3dDekui::FormCamera::Hook form_camera;
    Mm3dDekui::FormCamera::ArrivalLens arrival_lens;
    Mm3dDekui::FormCamera::OcarinaReturnLens ocarina_return_lens;
    Mm3dDekui::FormCamera::DekuFreeCamera deku_free_camera;
    Mm3dDekui::FormCamera::EntryRecoveryOrbit entry_recovery_orbit;
    unsigned form_trace_count=0,form_projection_count=0;
    float form_last_native=-1.f,form_last_output=-1.f,form_last_radius=-1.f;
    U32 form_last_mode=~0u;
    std::array<U32,6> form_last_pose{};
    bool form_trace_was_active=false;
    Mm3dDekui::NativeMinimap::Hook minimap_hook;
    Mm3dDekui::DungeonMap::Hook dungeon_map_hook;
    Mm3dDekui::InventoryShadow::Hook inventory_shadow_hook;
    Mm3dDekui::ItemsMapPrompt::Hook items_map_prompt_hook,map_items_prompt_hook;
    Mm3dDekui::MenuHelp::Hook menu_help_hook,select_help_hook;
    Mm3dDekui::WorldMapPrompt::Hook world_map_prompt_hook;
    Mm3dDekui::MenuTabs::Hook menu_tabs_hook;
    Mm3dDekui::NativeInventory::Hook inventory_hook;
    Mm3dDekui::OptionsMenu::Hook options_hook;
    Mm3dDekui::MenuPageSwitch::Hook menu_page_switch;
    Mm3dDekui::MenuMemory::Hook menu_memory;
    U32 menu_sound_context=0;
    Mm3dDekui::PauseCapture::Hook pause_capture_hook;
    Mm3dDekui::NativeNotebook::Hook notebook_hook;
    Mm3dDekui::NativeFileSelect::Hook file_select_hook;
    Mm3dDekui::FileSelectPrompt::Hook file_select_prompt_hook;
    Mm3dDekui::OcarinaPrompt::Hook ocarina_hook;
    Mm3dDekui::MagicBorder::Hook magic_hook;
    Mm3dDekui::MinigameHud::Hook minigame_hook;
    Mm3dDekui::PlayerHud::Hook player_hud_hook;
    Mm3dDekui::SongList::Hook song_hook;
    Mm3dDekui::DoubleTime::Hook double_time_hook;
    Mm3dDekui::SongReset::Hook song_reset_hook;
    Mm3dDekui::HudButtons::Hook hud_buttons_hook;
    Mm3dDekui::HudItemBackground::Hook hud_item_background_hook;
    Mm3dDekui::Banners::Hook banner_hook;
    Mm3dDekui::NativeItemPresent::Hook item_present_hook;
    Mm3dDekui::NativeStartup::Hook startup_hook;
    Mm3dDekui::NativeStartup::PressHook press_hook;
    Mm3dDekui::NativeStartup::LoadingIconHook loading_icon_hook;
    Mm3dDekui::ItemPreview::Hook item_preview_hook;
    Mm3dDekui::DialogueScale::Hook dialogue_hook;
};
} // namespace Mm3dDekui
