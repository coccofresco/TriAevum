// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone tests for the settings port from mm3d-dekui.
#include "settings.h"
#include "bloom_sampling.h"
#include "bottle_healing.h"
#include "camera_live.h"
#include "death_penalties.h"
#include "difficulty.h"
#include "minimap.h"
#include "music.h"
#include "native_minimap.h"
#include "player_hud.h"
#include "prices.h"
#include "widescreen.h"

#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
using namespace Mm3dDekui;
using Json=nlohmann::json;
namespace fs=std::filesystem;
unsigned checks=0;
void Check(bool value, const char* message) {
    ++checks;
    if(!value) throw std::runtime_error(message);
}
void Near(float actual, float expected) {
    ++checks;
    if(std::abs(actual-expected)>0.0001f || !std::isfinite(actual))
        throw std::runtime_error("float mismatch: "+std::to_string(actual)+" != "+std::to_string(expected));
}
std::string Read(const fs::path& path) {
    std::ifstream file(path,std::ios::binary);
    Check(bool(file),"read test file");
    return {std::istreambuf_iterator<char>(file),{}};
}
void Write(const fs::path& path, std::string_view bytes) {
    std::ofstream file(path,std::ios::binary);
    file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    Check(bool(file),"write test fixture");
}
Json Saved(const Settings& settings, const fs::path& path) {
    Check(settings.Save(path),"Save succeeds");
    const auto bytes=Read(path);
    Check(bytes.find('\r')==std::string::npos,"Save emits LF");
    return Json::parse(bytes);
}
template<class A,class B> void Pose(const A& actual, const B& expected) {
    Near(actual.x,expected.x);Near(actual.y,expected.y);Near(actual.scale,expected.scale);
}
Json Draw(unsigned shader=42) {
    return {{"shader",std::to_string(shader)},{"swizzle","ffffffffffffffff"},
            {"vertices",4},{"width",320},{"height",240},{"textures",0},{"blend",3},{"depth",0}};
}

void Shipped(const fs::path& defaults, const fs::path& scratch) {
    Settings settings,embedded;
    const auto bytes=Read(defaults);
    Check(bytes==DefaultSettingsJson(),"embedded defaults exactly match shipped bytes");
    Check(settings.Load(defaults),"load shipped file");
    Check(embedded.LoadFromString(DefaultSettingsJson()),"load embedded fallback");
    const auto canonical=Saved(settings,scratch/"shipped.json");
    Check(canonical==Saved(embedded,scratch/"embedded.json"),"file/string equivalence");

    Near(settings.song_list.background_x,-118.3f);
    Pose(settings.song_list.songs,GearMenu::Pose{-3.9f,38.4f,.58f});
    Pose(settings.song_list.staff,GearMenu::Pose{111.8f,154.f,.75f});
    Near(settings.song_list.spacing,34.6f);Near(settings.song_list.column_spacing,65.6f);
    const std::array<GearMenu::Pose,SongReset::Count> reset{{
        {-13.5f,37.6f,.57f},{-9.6f,43,.54f},{-9.7f,50.2f,.54f},{94.3f,0,.67f}}};
    const std::array<GearMenu::Pose,DoubleTime::Count> time{{
        {-414.7f,78.8f,.55f},{108.8f,35.8f,.62f},{78.4f,44.8f,.81f},
        {85.9f,52.8f,.77f},{512,240,1}}};
    for(unsigned i=0;i<reset.size();++i) Pose(settings.song_reset.poses[i],reset[i]);
    for(unsigned i=0;i<time.size();++i) Pose(settings.double_time.poses[i],time[i]);
    Check(settings.song_list.enabled && settings.double_time.enabled,"enabled donor dialogs");
    Check(!settings.song_reset.enabled,"shipped song_reset_top=false is preserved");
    Check(settings.damage_multiplier==1 && !settings.double_prices,"donor gameplay defaults");
    Check(settings.banners_migrated,"shipped banners already migrated");
    HudAdjustments::minimap_visibility_request=1;
    ApplyStartupSettings(settings);
    Check(HudAdjustments::minimap_visibility_request==-1,"startup clears pending visibility request");
    Pose(SongList::songs,settings.song_list.songs);Pose(SongList::staff,settings.song_list.staff);
    for(unsigned i=0;i<reset.size();++i) Pose(SongReset::poses[i],reset[i]);
    for(unsigned i=0;i<time.size();++i) Pose(DoubleTime::poses[i],time[i]);
    Check(!SongReset::enabled && SongList::enabled && DoubleTime::enabled,"applied dialog toggles");
    Near(DialogueScale::scale,settings.dialogue_scale);
    Near(DialogueScale::y_offset,settings.dialogue_y);
    Near(DialogueScale::background_width,settings.dialogue_background_width);
    Near(DialogueScale::indicator_x,settings.dialogue_indicator_x);
    Near(DialogueScale::indicator_y,settings.dialogue_indicator_y);
    Near(NativeInventory::scale,settings.inventory_menu.scale);
    Near(OptionsMenu::scale,settings.options_menu.scale);
    Near(PlayerHud::magic_height,settings.magic_bar_height);
    Pose(PlayerHud::rupee_keys,settings.rupee_keys);Pose(PlayerHud::carrots,settings.carrots);
    Pose(WorldMapPrompt::pose,settings.world_map_zoom_prompt);
    Pose(WorldMapPrompt::native_pose,settings.world_map_native_zoom);
    Near(MagicBorder::thickness,settings.magic_border.thickness);
    Near(Banners::parts[0].text_x,settings.banners.parts[0].text_x);
    Check(WidescreenProbe::saved_draws==settings.fullscreen_draws,"applied renderer draw rules");

    Settings loaded;
    Check(loaded.Load(scratch/"shipped.json"),"roundtrip reload");
    Check(Saved(loaded,scratch/"roundtrip.json")==canonical,"all serialized settings roundtrip");
    auto changed=canonical;
    // Exercise each serialized scalar with a different value, then require
    // idempotence after the donor's clamps/legacy rules normalize that input.
    for(auto& [key,value]:changed.items()) {
        if(key=="version") continue;
        if(value.is_boolean()) value=!value.get<bool>();
        else if(value.is_number()) value=value.get<double>()*0.73+0.25;
    }
    Settings varied;
    Check(varied.LoadFromString(changed.dump()),"varied settings accepted");
    const auto normalized=Saved(varied,scratch/"varied.json");
    Check(loaded.Load(scratch/"varied.json"),"varied reload");
    Check(Saved(loaded,scratch/"varied-again.json")==normalized,"varied full roundtrip");
    std::cout << "PASS shipped poses, embedded bytes, atomics, " << canonical.size() << " serialized keys roundtrip\n";
}

void DefaultsAndMigration(const fs::path& scratch) {
    Settings base,unknown,wrong;
    Check(base.LoadFromString(R"({"version":1})"),"minimal versioned object");
    Check(unknown.LoadFromString(R"({"version":1,"future_feature":{"enabled":true}})"),"ignore unknown field");
    Check(Saved(base,scratch/"minimal.json")==Saved(unknown,scratch/"unknown.json"),"unknown keys leave defaults");
    Near(base.song_list.songs.x,24);Near(base.song_list.songs.y,12);Near(base.song_list.songs.scale,.35f);
    Check(base.song_reset.enabled && base.double_time.enabled,"missing keys use donor Load defaults");
    Near(base.song_reset.poses[SongReset::Description].x,0);
    Near(base.double_time.poses[DoubleTime::Prompt].x,40);
    Near(base.inventory_menu.bar_y,base.inventory_menu.y-64*.65f-4);
    Check(wrong.LoadFromString(R"({"version":1,"song_list_x":"12","song_list_top":0,
        "red_potion_hearts":4.5,"blue_potion_hearts":2147483648,
        "bottled_fairy_hearts":true,"minimap_scale":null,"widescreen":"true"})"),"wrong types use fallback");
    Check(Saved(wrong,scratch/"types.json")==Saved(base,scratch/"base.json"),"typed fallback equivalence");
    Check(wrong.LoadFromString(R"({"version":1,"song_list_x":-999,"song_list_scale":99,
        "damage_multiplier":4.9,"red_potion_hearts":0,"blue_potion_hearts":40,
        "dialogue_scale":0,"minimap_opacity":99,"header_width":-1,
        "player_hud_heart_spacing":-999,"player_hud_top_padding":99})"),"out-of-range inputs");
    Near(wrong.song_list.songs.x,-512);Near(wrong.song_list.songs.scale,2);
    Check(wrong.damage_multiplier==4,"float-backed integer truncation");
    Check(wrong.red_potion_hearts==1 && wrong.blue_potion_hearts==20,"healing clamps");
    Near(wrong.dialogue_scale,.5f);Near(wrong.minimap_opacity,1);Near(wrong.submenu_header.width,400);
    Check(wrong.player_hud.heart_spacing==-40 && wrong.player_hud.top==40,"HUD clamps");

    Settings old;
    Check(old.LoadFromString(R"({"version":1,"inventory_native":true,"inventory_panel_x":12,
        "inventory_panel_y":70,"inventory_bar_scale":0.5,"inventory_shadow_width":1.5,
        "inventory_background_y":25,"inventory_map_prompt_x":18,"inventory_map_prompt_y":11,
        "inventory_map_prompt_r_x":19,"notebook_map_prompt_y":4,"notebook_prompt_y":4,
        "hud_button_1_scale":0.7,"hud_button_layout_1_1_scale":0.9})"),"legacy settings");
    Near(old.inventory_menu.bar_x,12);Near(old.inventory_menu.bar_y,34);
    Near(old.inventory_menu.shadow_width,1.5f);Near(old.inventory_menu.background.y,25);
    Near(old.map_items_prompt.pose.x,-18);Near(old.map_items_prompt.pose.y,11);
    Near(old.map_items_prompt.elements[0].x,-19);Near(old.notebook_live.map_prompt_y,-16);
    Near(old.hud_buttons.scale,.7f);Near(old.hud_buttons.preset_scales[0],.9f);
    Check(!old.banners_migrated,"legacy banners remain pending until archive restoration");
    Check(old.LoadFromString(R"({"version":1,"inventory_shadow_width":1.5,"brown_shadow_width":2,
        "inventory_background_y":25,"brown_background_y":40,"inventory_map_prompt_x":18,
        "map_items_prompt_x":22,"notebook_map_prompt_y":4,"notebook_prompt_y":4,"notebook_buttons_x":0})"),"new key precedence");
    Near(old.inventory_menu.shadow_width,2);Near(old.inventory_menu.background.y,40);
    Near(old.map_items_prompt.pose.x,22);Near(old.notebook_live.map_prompt_y,4);
    // Donor intentionally retains current poses for these missing keys, and
    // inventory panel controls are conditional on inventory_native's presence.
    old.file_select.poses[0].x=77;old.item_present.poses[0].x=88;
    Check(old.LoadFromString(R"({"version":1,"inventory_panel_x":-55})"),"partial reload");
    Near(old.inventory_menu.x,12);Near(old.file_select.poses[0].x,77);Near(old.item_present.poses[0].x,88);
    LegacyBannerSettings legacy{50,1.2f,.8f,3,7,.6f};
    Check(MigrateLegacyBanners(old,legacy),"banner migration after host restoration");
    Near(old.banners.parts[0].text_x,30);Near(old.banners.parts[0].text_y,-19.4f);
    Near(old.banners.parts[0].height,1.2f);Near(old.banners.parts[1].brightness,.6f);
    Near(Banners::parts[0].text_x,30);
    Check(!MigrateLegacyBanners(old,{}),"banner migration runs once");
    Check(old.Save(scratch/"migrated.json"),"migration persists");
    Settings reload;Check(reload.Load(scratch/"migrated.json") && reload.banners_migrated,"migration marker reloads");
    std::cout << "PASS typed fallbacks, missing/unknown keys, bounds and legacy migrations\n";
}

void DrawRules(const fs::path& scratch) {
    Settings settings;
    auto draw=Draw();
    Json input={{"version",1},{"fullscreen_draw",draw}};
    Check(settings.LoadFromString(input.dump()) && settings.fullscreen_draws.size()==1,"legacy single draw migration");
    Check(settings.fullscreen_draws[0].shader==0x42 && settings.fullscreen_draws[0].swizzle==~std::uint64_t{},"64-bit hex precision");
    input["fullscreen_draws"]=Json::array();
    Check(settings.LoadFromString(input.dump()) && settings.fullscreen_draws.empty(),"explicit empty array suppresses legacy rule");
    auto transition=draw;
    transition.update({{"shader","89b1ab98da3d9241"},{"swizzle","6f0b398b1fc186a9"},
                       {"width",480},{"height",400},{"textures",0},{"blend",65654},{"depth",3856}});
    auto invalid=draw;invalid["vertices"]=4.5;
    auto overflow=draw;overflow["swizzle"]="10000000000000000";
    auto huge=draw;huge["width"]=4294967296.0;
    auto zero=draw;zero["height"]=0;
    auto number=draw;number["shader"]=42;
    auto trailing=draw;trailing["shader"]="1z";
    auto decorated=draw;decorated["shader"]=" +0x2A ";
    input["fullscreen_draws"]={draw,draw,transition,invalid,overflow,huge,zero,number,trailing,decorated,42,nullptr};
    Check(settings.LoadFromString(input.dump()) && settings.fullscreen_draws.size()==2,"deduplication and invalid draw filtering");
    Check(settings.fullscreen_draws[1].shader==42,"Qt-compatible hex sign/prefix/whitespace");
    input["fullscreen_draws"]=Json::array();
    for(unsigned i=1;i<150;++i) input["fullscreen_draws"].push_back(Draw(i));
    Check(settings.LoadFromString(input.dump()) && settings.fullscreen_draws.size()==128,"128-rule limit");
    const auto canonical=Saved(settings,scratch/"draws.json");
    Settings reload;Check(reload.Load(scratch/"draws.json"),"draw rules reload");
    Check(reload.fullscreen_draws==settings.fullscreen_draws,"draw signatures lossless roundtrip");
    Check(Saved(reload,scratch/"draws-again.json")==canonical,"draw serialization stable");
    std::cout << "PASS legacy/fullscreen draw filtering, cap and 64-bit roundtrip\n";
}

void InvalidAndPersistence(const fs::path& scratch) {
    Settings settings;
    Check(settings.LoadFromString(DefaultSettingsJson()),"seed valid settings");
    const auto before=settings.song_list.songs;
    const std::string bad[]={"", "{", "[]", "null", "{}", R"({"version":2})",
        R"({"version":"1"})", R"({"version":true})", R"({"version":1.5})",
        R"({"version":1,"scale":1e999})", R"({"version":1,})", R"({"version":1} trailing)"};
    for(const auto& bytes:bad) {
        Check(!settings.LoadFromString(bytes),"invalid input rejected");
        Check(settings.SaveBlocked() && !settings.Save(scratch/"must-not-exist.json"),"invalid input blocks Save");
        Pose(settings.song_list.songs,before);
    }
    Check(!fs::exists(scratch/"must-not-exist.json"),"failed save creates no file");
    const auto corrupt=scratch/"corrupt.json";
    Write(corrupt,"{broken original");
    Check(!settings.Load(corrupt) && !settings.Save(corrupt),"existing invalid file protected");
    Check(Read(corrupt)=="{broken original","invalid file unmodified");
    Write(scratch/"oversize.json",std::string(1024*1024+1,' '));
    Check(!settings.Load(scratch/"oversize.json") && settings.SaveBlocked(),"oversize file rejected");
    Check(!settings.LoadFromString(std::string(1024*1024+1,' ')),"oversize string rejected");
    Check(!settings.Load(scratch) && settings.SaveBlocked(),"unreadable/non-file destination protected");
    Check(!settings.Load(scratch/"missing.json") && !settings.SaveBlocked(),"missing file is normal first launch");
    Check(settings.Save(scratch/"missing.json"),"first-launch Save succeeds");
    Check(settings.LoadFromString(R"({"version":1.0})"),"integer-valued JSON float version accepted");
    auto exact=std::string(R"({"version":1})");exact.resize(1024*1024,' ');
    Check(settings.LoadFromString(exact),"1 MiB inclusive limit");
    const auto unicode=scratch/fs::path(u8"config-città-設定.json");
    Check(settings.Save(unicode) && settings.Load(unicode),"Unicode filesystem paths");
    settings.damage_multiplier=7;
    Check(settings.Save(unicode),"atomic replacement of existing file");
    Settings reload;Check(reload.Load(unicode) && reload.damage_multiplier==7,"replacement visible");
    const auto directory=scratch/"existing-directory";fs::create_directory(directory);
    Write(directory/"sentinel","keep");
    Check(!settings.Save(directory),"failed atomic replacement reported");
    Check(Read(directory/"sentinel")=="keep","failed replacement preserves original");
    Check(!settings.Save(scratch/"absent-parent"/"file.json"),"Save does not create parents");
    for(const auto& item:fs::directory_iterator(scratch))
        Check(item.path().filename().generic_u8string().find(u8".dekui-tmp-")==std::u8string::npos,"temporary files cleaned up");
    std::cout << "PASS invalid/oversize input, save blocking, Unicode paths and atomic replacement\n";
}

void Application() {
    Settings settings;
    Check(settings.LoadFromString(R"({"version":1,"song_reset_top":true,"damage_multiplier":4,
        "red_potion_hearts":3,"blue_potion_hearts":4,"bottled_fairy_hearts":5,"double_prices":true,
        "disable_heart_drops":true,"disable_rupee_drops":true,"disable_fairy_drops":true,
        "disable_magic_drops":true,"disable_ammo_pickups":true,"lose_rupees_on_death":true,
        "world_ends_on_death":true,"accurate_hle_audio":false,"mute_music":true,
        "single_screen_text":false,"fix_bloom":false,"minimap":false,"minimap_scale":0.42,
        "minimap_opacity":0.7,"minimap_right_margin":17,"minimap_bottom_margin":21,
        "widescreen":true,"camera_zoom_out":1.2,"game_camera_fov":61,
        "dialogue_scale":0.7,"dialogue_y":-17,"dialogue_background_width":1.3,
        "dialogue_indicator_x":19,"dialogue_indicator_y":12})"),"application fixture");
    HudAdjustments::minimap_visibility_request=1;
    ApplySettings(settings);
    Check(HudAdjustments::minimap_visibility_request==1,"ordinary Apply preserves pending visibility request");
    Check(SongReset::enabled && Difficulty::damage_multiplier==4,"profile overrides apply");
    Check(BottleHealing::red_hearts==3 && BottleHealing::blue_hearts==4 && BottleHealing::fairy_hearts==5,"healing applies");
    Check(Difficulty::disable_heart_drops && Difficulty::disable_rupee_drops && Difficulty::disable_fairy_drops &&
          Difficulty::disable_magic_drops && Difficulty::disable_ammo_pickups,"drop settings apply");
    Check(DeathPenalties::lose_rupees && DeathPenalties::world_ends,"death settings apply");
    Check(!SettingsHost::accurate_hle_audio && Music::muted && !SingleScreenText::enabled && !BloomSampling::enabled,"audio/text/bloom applied state");
    Check(!Minimap::enabled && Widescreen::enabled,"map/widescreen toggles");
    Near(Minimap::scale,.42f);Near(Minimap::opacity,.7f);
    Near(NativeMinimap::right_margin,17);Near(NativeMinimap::bottom_margin,21);
    Near(Widescreen::camera_zoom_out,1.2f);Near(CameraLive::desired_fov,61);
    Near(DialogueScale::scale,.7f);Near(DialogueScale::y_offset,-17);
    Near(DialogueScale::background_width,1.3f);Near(DialogueScale::indicator_x,19);Near(DialogueScale::indicator_y,12);
    Check(Prices::initialized && !Prices::enabled,"Prices stays at first startup choice despite reapply");
    std::cout << "PASS gameplay/audio/renderer/camera/dialogue application and one-time price latch\n";
}
void UpdateSchema(const fs::path& defaults,const fs::path& scratch) {
    Settings settings;
    Check(settings.Load(defaults),"updated defaults load");
    const auto shipped=Json::parse(Read(defaults));
    const auto saved=Saved(settings,scratch/"update-defaults.json");
    Check(shipped.size()==608,"tip 4f2692ed has 608 shipped keys");
    for(const auto& [key,value]:shipped.items()) {
        Check(saved.contains(key),("shipped key retained: "+key).c_str());
        if(key=="accurate_hle_audio") {
            Check(saved[key]==(settings.hle_audio_accuracy==2),"legacy audio output derives from migrated mode");
        } else if(value.is_number()) {
            Near(saved[key].get<float>(),value.get<float>());
        } else Check(saved[key]==value,("shipped default retained: "+key).c_str());
    }
    for(const auto& input:std::array<Json,6>{
        Json{{"version",1},{"accurate_hle_audio",true}},
        Json{{"version",1},{"accurate_hle_audio",false}},
        Json{{"version",1},{"hle_audio_accuracy",2}},
        Json{{"version",1},{"hle_audio_accuracy",2},{"hle_audio_mode_version",0}},
        Json{{"version",1},{"hle_audio_accuracy",1.5},{"hle_audio_mode_version",1}},
        Json{{"version",1},{"hle_audio_accuracy",3},{"hle_audio_mode_version",1}}}) {
        Check(settings.LoadFromString(input.dump()),"old/invalid audio mode loads");
        Check(settings.hle_audio_accuracy==1,"all pre-migration/invalid preferences become Balanced");
    }
    Check(settings.LoadFromString(R"({"version":1,"hle_audio_accuracy":2,"hle_audio_mode_version":1})"),"versioned accurate mode loads");
    Check(settings.hle_audio_accuracy==2,"preserve post-migration Accurate HLE");
    ApplySettings(settings);
    Check(SettingsHost::hle_audio_accuracy==2&&SettingsHost::accurate_hle_audio,"host receives audio mode 2");
    Check(settings.LoadFromString(R"({"version":1,"song_list_x":40,"song_list_y":30,"song_list_scale":0.5,
        "song_list_column_spacing":80,"dialogue_y":-22,"dialogue_scale":0.7,
        "item_present_1_x":71,"player_hud_opacity":0,"minimap_opacity":0,
        "actor_draw_distance":99,"prop_draw_distance":-1,"price_multiplier":99,
        "file_select_entrance_ease":9,"ambient_occlusion_strength":99,"depth_of_field_focus":0})"),"new schema migration fixture");
    Near(settings.song_list.banner.x,40);Near(settings.song_list.banner.y,10);
    Near(settings.song_list.banner_height,18);Near(settings.song_list.banner_width,107);
    Near(settings.song_list.panel_left,36);Near(settings.song_list.panel_right,151);
    Near(settings.mask_present.poses[NativeItemPresent::Panel].x,71);
    Near(settings.mask_present.poses[NativeItemPresent::Description].y,-22);
    Near(settings.mask_present.poses[NativeItemPresent::Description].scale,.7f);
    Check(!settings.mask_present.horizontal,"independent mask migration remains vertical");
    Near(settings.player_hud.opacity,0);Near(settings.minimap_opacity,0);
    Near(settings.actor_draw_distance,3);Near(settings.prop_draw_distance,1);
    Check(settings.price_multiplier==3,"three-times price bound");
    Near(settings.file_select.entrance_ease,3);
    Near(settings.ambient_occlusion.strength,1.5f);Near(settings.depth_of_field.focus,50);
    const auto once=Saved(settings,scratch/"update-migration.json");
    Check(settings.Load(scratch/"update-migration.json"),"update migration reload");
    Check(Saved(settings,scratch/"update-migration-again.json")==once,"update migration stable on second load");
    std::cout<<"PASS update defaults coverage, independent poses, audio migration and new bounds\n";
}
// S2: tip 4f2692ed HUD layout versions, padding/base scale/magic width/mirror keys, the dungeon
// prompt fallback and the parser-only roundtrip keys.
void HudLayoutSchema(const fs::path& defaults,const fs::path& scratch) {
    const auto shipped=Json::parse(Read(defaults));
    Check(shipped.size()==608,"tip has 608 shipped keys");
    for(const char* key:{"dungeon_map_arrows_scale","dungeon_map_arrow_up_x","dungeon_map_arrow_up_y",
        "dungeon_map_arrow_down_x","dungeon_map_arrow_down_y","dungeon_map_arrow_left_x","dungeon_map_arrow_left_y",
        "dungeon_map_arrow_right_x","dungeon_map_arrow_right_y","dungeon_map_prompts_x","dungeon_map_prompts_y",
        "dungeon_map_prompts_scale","dungeon_map_prompts_icon_scale","dungeon_map_prompts_spacing",
        "player_hud_base_scale","player_hud_layout_version","player_hud_left_padding","player_hud_right_padding",
        "player_hud_magic_width","player_hud_mirror_item_buttons"})
        Check(shipped.contains(key),(std::string("tip key shipped: ")+key).c_str());
    // Fresh install: the shipped file is layout version 2 (FreshDefaults).
    Settings fresh;
    Check(fresh.Load(defaults),"fresh defaults load");
    const auto f=HudAdjustments::FreshDefaults();
    Check(fresh.player_hud.layout_version==2&&fresh.player_hud.mirror_item_buttons,"fresh: layout 2, mirrored buttons");
    Check(fresh.player_hud.base_scale==f.base_scale&&fresh.player_hud.heart_spacing==f.heart_spacing&&
        fresh.player_hud.magic_height_steps==f.magic_height_steps,"fresh: base scale, heart spacing, magic height");
    for(unsigned i=0;i<HudAdjustments::Count;++i)
        Check(fresh.player_hud.elements[i]==f.elements[i]&&fresh.player_hud.x[i]==f.x[i]&&fresh.player_hud.y[i]==f.y[i],
            (std::string("fresh element equals FreshDefaults: ")+HudAdjustments::Keys[i]).c_str());
    Near(fresh.player_hud.magic_width,f.magic_width);
    Near(fresh.minimap_opacity,HudAdjustments::DefaultMinimapOpacity(fresh.player_hud));
    Check(fresh.stick_click_controls,"fresh install ships stick-click controls on");
    // Parser-only keys: absent from the shipped file, always saved, roundtrip exactly.
    const auto saved=Saved(fresh,scratch/"hud-fresh.json");
    for(const char* key:{"dungeon_map_buttons_x","dungeon_map_buttons_y","dungeon_map_buttons_scale",
        "dungeon_map_buttons_icon_scale","dungeon_map_zoom_prompt_x","dungeon_map_zoom_prompt_y",
        "dungeon_map_zoom_prompt_scale","dungeon_map_move_prompt_x","dungeon_map_move_prompt_y",
        "dungeon_map_move_prompt_scale"}) {
        Check(!shipped.contains(key),(std::string("parser-only key not shipped: ")+key).c_str());
        Check(saved.contains(key),(std::string("parser-only key saved: ")+key).c_str());
    }
    Settings parser;
    Check(parser.LoadFromString(R"({"version":1,"dungeon_map_buttons_x":12,"dungeon_map_buttons_y":-7,
        "dungeon_map_buttons_scale":1.5,"dungeon_map_buttons_icon_scale":0.75,"dungeon_map_zoom_prompt_x":3,
        "dungeon_map_zoom_prompt_y":-30,"dungeon_map_zoom_prompt_scale":0.5,"dungeon_map_move_prompt_x":-4,
        "dungeon_map_move_prompt_y":-50,"dungeon_map_move_prompt_scale":1.25})"),"parser-only fixture");
    const auto p=Saved(parser,scratch/"hud-parser.json");
    Near(p["dungeon_map_buttons_x"].get<float>(),12);Near(p["dungeon_map_buttons_y"].get<float>(),-7);
    Near(p["dungeon_map_buttons_scale"].get<float>(),1.5f);Near(p["dungeon_map_buttons_icon_scale"].get<float>(),.75f);
    Near(p["dungeon_map_zoom_prompt_y"].get<float>(),-30);Near(p["dungeon_map_move_prompt_scale"].get<float>(),1.25f);
    Check(parser.Load(scratch/"hud-parser.json")&&Saved(parser,scratch/"hud-parser-again.json")==p,"parser-only roundtrip");
    // Legacy (version 0) profile: no layout version key. It keeps its own sizes: no base
    // scale, no padding, magic width 1, mirror off, its own minimap opacity and controls.
    Settings legacy;
    Check(legacy.LoadFromString(R"({"version":1,"player_hud_scale":3,"player_hud_hearts_scale":0,
        "player_hud_heart_spacing":0,"player_hud_magic_height":0,"minimap_opacity":0.85,"stick_click_controls":false,
        "notebook_buttons_scale":1.3,"notebook_buttons_icon_scale":0.8,"notebook_prompt_y":10,"notebook_map_prompt_y":-15})"),
        "legacy profile loads");
    Check(legacy.player_hud.layout_version==0&&!legacy.player_hud.mirror_item_buttons,"legacy stays layout 0, no mirror");
    Check(legacy.player_hud.base_scale==0&&legacy.player_hud.left==0&&legacy.player_hud.right==0&&
        legacy.player_hud.overall==3,"legacy keeps its own scale, no base scale or padding");
    Near(legacy.player_hud.magic_width,1);Near(legacy.minimap_opacity,.85f);
    Check(!legacy.stick_click_controls,"legacy keeps its controls");
    // Missing dungeon prompt keys fall back to the Notebook values the old hints borrowed.
    Near(legacy.dungeon_map.prompts.scale,1.3f);Near(legacy.dungeon_map.prompts.icon_scale,.8f);
    Near(legacy.dungeon_map.prompts.spacing,25);Near(legacy.dungeon_map.prompts.x,0);
    const auto l=Saved(legacy,scratch/"hud-legacy.json");
    Check(l["player_hud_layout_version"]==0&&l["player_hud_mirror_item_buttons"]==false,"legacy saves layout 0");
    Settings legacy_again;
    Check(legacy_again.Load(scratch/"hud-legacy.json")&&Saved(legacy_again,scratch/"hud-legacy-again.json")==l,"legacy roundtrip stable");
    // Present dungeon prompt keys win over the Notebook fallback.
    Check(legacy.LoadFromString(R"({"version":1,"notebook_buttons_scale":1.3,"dungeon_map_prompts_scale":0.9,
        "dungeon_map_prompts_spacing":-5})"),"explicit dungeon prompt keys");
    Near(legacy.dungeon_map.prompts.scale,.9f);Near(legacy.dungeon_map.prompts.spacing,-5);
    // Version 1 is kept; out-of-range versions fall back to 0; bounds clamp.
    Check(legacy.LoadFromString(R"({"version":1,"player_hud_layout_version":1})")&&legacy.player_hud.layout_version==1,"layout 1 kept");
    for(const char* v:{"3","-1","1.5","true"}) {
        Check(legacy.LoadFromString(std::string(R"({"version":1,"player_hud_layout_version":)")+v+"}"),"invalid layout loads");
        Check(legacy.player_hud.layout_version==0,"invalid layout version becomes 0");
    }
    Check(legacy.LoadFromString(R"({"version":1,"player_hud_left_padding":500,"player_hud_right_padding":-500,
        "player_hud_top_padding":-99,"player_hud_bottom_padding":99,"player_hud_magic_width":9,"player_hud_base_scale":-30})"),"bounds fixture");
    Check(legacy.player_hud.left==100&&legacy.player_hud.right==-100&&legacy.player_hud.top==-40&&
        legacy.player_hud.bottom==40&&legacy.player_hud.base_scale==-10,"padding and base scale bounds");
    Near(legacy.player_hud.magic_width,3);
    // Application: padding moves the left column right and the right column left.
    ApplySettings(legacy);
    Check(HudAdjustments::HorizontalPadding(HudAdjustments::Hearts)==100,"left padding moves hearts");
    Check(HudAdjustments::HorizontalPadding(HudAdjustments::Buttons)==100,"negative right padding mirrors to buttons");
    Check(HudAdjustments::HorizontalPadding(HudAdjustments::Clock)==0,"centred clock keeps its position");
    Near(HudAdjustments::magic_width,3);
    ApplySettings(fresh);
    Check(HudAdjustments::mirror_item_buttons.load(),"fresh applies mirrored backgrounds");
    Near(HudAdjustments::OverallScale(),HudAdjustments::Factor(f.base_scale)*HudAdjustments::Factor(fresh.player_hud.overall));
    std::cout<<"PASS HUD layout versions 0/1/2, padding/base scale/magic width/mirror, dungeon prompt fallback, parser-only keys\n";
}
} // namespace

int main(int argc,char** argv) {
    if(argc!=3) {std::cerr << "Usage: settings_tests <shipped-presentation.json> <existing-empty-scratch-directory>\n";return 2;}
    try {
        const fs::path scratch=argv[2];
        Check(fs::is_directory(scratch) && fs::is_empty(scratch),"scratch must be an existing empty directory");
        Shipped(argv[1],scratch);DefaultsAndMigration(scratch);DrawRules(scratch);
        InvalidAndPersistence(scratch);Application();UpdateSchema(argv[1],scratch);HudLayoutSchema(argv[1],scratch);
        std::cout << "PASS " << checks << " settings checks\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';return 1;
    }
}
