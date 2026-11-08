// Ported from mm3d-dekui src/mm3d/settings.h (tgacquin/mm3d-dekui).
// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "presentation_options.h"
#include "ocarina_prompt.h"
#include "file_select_prompt.h"
#include "magic_border.h"
#include "hud_adjustments.h"
#include "minigame_hud.h"
#include "song_list.h"
#include "options_menu.h"
#include "double_time.h"
#include "song_reset.h"
#include "hud_buttons.h"
#include "banners.h"
#include "dungeon_map.h"
#include "world_map_prompt.h"
#include <filesystem>
#include <string>
#include <string_view>
#include "submenu_header.h"
#include "inventory_menu.h"
#include "native_notebook.h"
#include "native_file_select.h"
#include "native_item_present.h"
#include "native_startup.h"
#include "widescreen_probe.h"
namespace Mm3dDekui {
struct Settings {
    SubmenuHeader::Options submenu_header;
    InventoryMenu::Options inventory_menu;
    ItemsMapPrompt::ReturnOptions map_items_prompt;
    NativeNotebook::Options notebook_live;
    OcarinaPrompt::Options ocarina_prompt;
    FileSelectPrompt::Options file_select_prompt;
    SongList::Options song_list;
    OptionsMenu::Options options_menu;
    DoubleTime::Options double_time;
    SongReset::Options song_reset;
    HudButtons::Options hud_buttons;
    HudAdjustments::Options player_hud=HudAdjustments::FreshDefaults();
    Banners::Options banners;
    bool banners_migrated=false;
    DungeonMap::Options dungeon_map;
    // Retain retired individual dungeon poses for profile roundtrips only.
    // Rendering uses dungeon_map.prompts for all four rows.
    NativeInventory::ButtonPose world_map_zoom_prompt{0,0,.8f},world_map_native_zoom,
        dungeon_map_zoom_prompt{0,-28,.69f},dungeon_map_move_prompt{0,-44,.69f};
    NativeFileSelect::Options file_select;
    NativeItemPresent::Options item_present;
    NativeItemPresent::Options mask_present;
    NativeStartup::Options startup;
    float actor_draw_distance = 1.f;
    float prop_draw_distance = 1.f;
    int damage_multiplier = 1;
    bool weaker_enhanced_defense = false;
    int red_potion_hearts = 20, blue_potion_hearts = 20, bottled_fairy_hearts = 10;
    bool disable_heart_drops = false;
    bool disable_rupee_drops = false;
    bool disable_fairy_drops = false;
    bool disable_magic_drops = false, disable_ammo_pickups = false;
    bool lose_rupees_on_death = false;
    bool world_ends_on_death = false;
    bool advance_six_hours_on_death = false;
    bool fast_time = false;
    bool double_prices = false;
    int price_multiplier = 2;
    int hle_audio_accuracy = 1; // 1: Balanced, 2: Accurate HLE. All pre-migration preferences become 1.
    bool mute_music = false;
    bool disable_low_health_beep = false;
    bool disable_tatl_reminders = false;
    bool single_screen_text = true;
    bool stick_click_controls = true; // First launch only; legacy settings retain their controls.
    bool fix_bloom = true;
    bool disable_fog = false;
    bool hd_pictograph_photos = true;
    DepthOfField::Options depth_of_field;
    AmbientOcclusion::Options ambient_occlusion;
    bool interpolation_60fps = true;
    bool interpolation_cpu_vertices = true;
    bool classic_saving = false;
    bool classic_double_time = false;
    bool sword_activate_owls = false;
    bool disable_bomber_approach = false;
    float dialogue_scale = 1.f;
    float dialogue_y = 0.f;
    float dialogue_background_width = 1.f;
    float dialogue_indicator_x = 0.f, dialogue_indicator_y = 0.f;
    bool indicator = true;
    bool show_performance = false;
    bool show_startup_settings_hint = true;
    bool widescreen = false;
    float camera_zoom_out = 1.f;
    float game_camera_fov = 65.f;
    float cutscene_camera_fov = 49.f;
    float restoration_hud_scale = 1.f;
    MagicBorder::Options magic_border;
    MinigameHud::Options minigame_hud;
    NativeInventory::ButtonPose rupee_keys,carrots;
    float magic_bar_y = 0.f;
    float magic_bar_height = 1.f;
    float dpad_y = 0.f;
    float dpad_offset_x = 0.f, dpad_offset_y = 0.f;
    bool minimap = true;
    float minimap_scale = .36f, minimap_opacity = .75f;
    float minimap_right_margin = 8.f, minimap_bottom_margin = 8.f;
    std::vector<WidescreenProbe::Draw> fullscreen_draws;
    float scale = 1.f;
    float opacity = .9f;
    float x = .02f, y = .02f;
    // Missing file: false, saving allowed. Invalid/unreadable existing file:
    // false, saving blocked until a subsequent successful load on this object.
    bool Load(const std::filesystem::path& path);
    // Same parser as Load, including the 1 MiB bound and version=1 requirement.
    bool LoadFromString(std::string_view text);
    // Atomic replacement, no direct-write fallback or directory creation.
    bool Save(const std::filesystem::path& path) const;
    // The text Save writes (LoadFromString reads it back), regardless of save_blocked.
    std::string SaveToString() const;
    bool SaveBlocked() const noexcept { return save_blocked; }
private:
    bool save_blocked = false;
};

// Immutable shipped JSON, available without a donor tree or runtime data path.
std::string_view DefaultSettingsJson() noexcept;

// Apply before guest execution, or while execution is quiescent. Prices retains
// the donor's one-time startup latch; subsequent calls do not change its value.
void ApplySettings(const Settings& settings);
// Also clears the pending minimap checkbox synchronization request, as the
// donor overlay does at startup. Ordinary reapplication leaves that request alone.
void ApplyStartupSettings(const Settings& settings);

// AudioCore belongs to the emulator, so the host consumes this applied value.
namespace SettingsHost {
inline std::atomic<int> hle_audio_accuracy{1};
// Compatibility observation for existing hosts; modes 1 and 2 remain distinct.
inline std::atomic<bool> accurate_hle_audio{false};
}

// Notebook's old presentation.json-independent telop values. Call only AFTER
// the host successfully removes the old installed telop override. No archive
// mutation or notebook.json loading is performed by this settings component.
struct LegacyBannerSettings {
    float telop_x=40, telop_scale=1, telop_text_scale=1;
    float telop_y=0, telop_text_y=0, telop_brightness=1;
};
// Converts and applies once. Caller persists Settings after successful migration.
bool MigrateLegacyBanners(Settings& settings, const LegacyBannerSettings& old);
} // namespace Mm3dDekui
