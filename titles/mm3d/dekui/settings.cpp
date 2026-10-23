// Ported from mm3d-dekui src/mm3d/settings.cpp (tgacquin/mm3d-dekui).
// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings.h"
#include "bloom_sampling.h"
#include "bottle_healing.h"
#include "camera_live.h"
#include "death_penalties.h"
#include "difficulty.h"
#include "dialogue_scale.h"
#include "minimap.h"
#include "music.h"
#include "native_minimap.h"
#include "player_hud.h"
#include "prices.h"
#include "single_screen_text.h"
#include "widescreen.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Mm3dDekui {
namespace {
using Json = nlohmann::json;
constexpr std::size_t MaxSettingsBytes = 1024 * 1024;

const Json& Get(const Json& object, std::string_view key) {
    static const Json missing;
    if(!object.is_object()) return missing;
    const auto it=object.find(key);
    return it==object.end() ? missing : *it;
}

// Match Qt's typed fallbacks: strings/bools do not become numbers; fractional
// numbers and values outside signed-int range do not become integers.
double Number(const Json& object, std::string_view key, double fallback) {
    const auto& n=Get(object,key);
    return n.is_number() ? n.get<double>() : fallback;
}
bool Boolean(const Json& object, std::string_view key, bool fallback) {
    const auto& n=Get(object,key);
    return n.is_boolean() ? n.get<bool>() : fallback;
}
int Integer(const Json& object, std::string_view key, int fallback=0) {
    const double n=Number(object,key,fallback);
    if(!std::isfinite(n) || n<std::numeric_limits<int>::min() ||
       n>std::numeric_limits<int>::max() || std::trunc(n)!=n) return fallback;
    return static_cast<int>(n);
}

std::uint64_t ParseHex(const Json& value, bool& ok) {
    ok=false;
    if(!value.is_string()) return 0;
    std::string_view s=value.get_ref<const std::string&>();
    const auto begin=s.find_first_not_of(" \t\n\r\f\v");
    if(begin==s.npos) return 0;
    s=s.substr(begin,s.find_last_not_of(" \t\n\r\f\v")-begin+1);
    if(s.starts_with('+')) s.remove_prefix(1);
    if(s.starts_with("0x") || s.starts_with("0X")) s.remove_prefix(2);
    std::uint64_t result=0;
    const auto parsed=std::from_chars(s.data(),s.data()+s.size(),result,16);
    ok=parsed.ec==std::errc{} && parsed.ptr==s.data()+s.size();
    return ok ? result : 0;
}
std::string Hex(std::uint64_t value) {
    char buffer[16];
    const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value,16);
    return std::string(buffer,result.ptr);
}

// QSaveFile equivalent: exclusively create a sibling, flush and close, then
// replace atomically. Never truncate the destination on any failure path.
bool AtomicSave(const std::filesystem::path& path, const std::string& bytes) {
    static std::atomic<unsigned long long> serial{0};
    if(path.filename().empty()) return false;
#ifdef _WIN32
    const auto pid=GetCurrentProcessId();
#else
    const auto pid=getpid();
#endif
    std::filesystem::path temp;
    struct Cleanup {
        std::filesystem::path& path;
        ~Cleanup() { if(!path.empty()) {std::error_code ignored;std::filesystem::remove(path,ignored);} }
    } cleanup{temp};
    for(unsigned attempt=0;attempt<128;++attempt) {
        auto candidate=path;
        candidate+=".dekui-tmp-"+std::to_string(pid)+"-"+std::to_string(serial.fetch_add(1));
#ifdef _WIN32
        const HANDLE file=CreateFileW(candidate.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) {
            if(GetLastError()==ERROR_FILE_EXISTS || GetLastError()==ERROR_ALREADY_EXISTS) continue;
            return false;
        }
        temp=candidate;
        DWORD written=0;
        const bool size_ok=bytes.size()<=std::numeric_limits<DWORD>::max();
        const bool write_ok=size_ok && WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) && written==bytes.size();
        const bool flush_ok=write_ok && FlushFileBuffers(file);
        const bool close_ok=CloseHandle(file);
        if(!flush_ok || !close_ok) return false;
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return false;
#else
        const int file=open(candidate.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
        if(file<0) {if(errno==EEXIST) continue;return false;}
        temp=candidate;
        bool write_ok=true;
        std::size_t offset=0;
        while(offset<bytes.size()) {
            const auto count=write(file,bytes.data()+offset,bytes.size()-offset);
            if(count<0 && errno==EINTR) continue;
            if(count<=0) {write_ok=false;break;}
            offset+=static_cast<std::size_t>(count);
        }
        const bool flush_ok=write_ok && fsync(file)==0;
        const bool close_ok=close(file)==0;
        if(!flush_ok || !close_ok) return false;
        std::error_code error;
        std::filesystem::rename(temp,path,error);
        if(error) return false;
#endif
        temp.clear();
        return true;
    }
    return false;
}
} // namespace

std::string_view DefaultSettingsJson() noexcept {
    static constexpr char data[]=
#include "defaults/presentation_json.inc"
    ;
    return {data,sizeof(data)-1};
}

bool Settings::Load(const std::filesystem::path& path) {
    std::error_code error;
    save_blocked=std::filesystem::exists(path,error);
    if(error) save_blocked=true;
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file) return false;
    save_blocked=true;
    const auto size=file.tellg();
    if(size<0 || size>static_cast<std::streamoff>(MaxSettingsBytes)) return false;
    std::string text(static_cast<std::size_t>(size),'\0');
    file.seekg(0);
    if(!file.read(text.data(),static_cast<std::streamsize>(text.size()))) return false;
    // Detect a file that grew after the size check as well.
    if(file.peek()!=std::char_traits<char>::eof() || file.bad()) return false;
    return LoadFromString(text);
}

bool Settings::LoadFromString(std::string_view text) {
    save_blocked=true;
    if(text.size()>MaxSettingsBytes) return false;
    const auto object=Json::parse(text.begin(),text.end(),nullptr,false);
    if(!object.is_object() || Integer(object,"version")!=1) return false;
    const auto value=[&](std::string_view key,float fallback,float lo,float hi) {
        const double number=Number(object,key,fallback);
        return std::isfinite(number) ? static_cast<float>(std::clamp(number,double(lo),double(hi))) : fallback;
    };
    for(const auto& f:SubmenuHeader::Fields)
        submenu_header.*f.member=value(f.key,SubmenuHeader::Options{}.*f.member,f.lo,f.hi);
    if(object.contains("inventory_native")) {
        inventory_menu.enabled=Boolean(object,"inventory_native",true);
        inventory_menu.x=value("inventory_panel_x",0.f,-320.f,320.f);
        inventory_menu.y=value("inventory_panel_y",32.f,-240.f,240.f);
        inventory_menu.scale=value("inventory_panel_scale",.8f,.5f,1.5f);
    }
    inventory_menu.masks_x=value("inventory_masks_x",0.f,-320.f,320.f);
    notebook_live.schedule_prompt=Boolean(object,"notebook_schedule_prompt",true);
    notebook_live.schedule_prompt_y=value("notebook_schedule_prompt_y",-40,-240,240);
    notebook_live.map_prompt=Boolean(object,"notebook_map_prompt",true);
    notebook_live.map_prompt_y=value("notebook_map_prompt_y",-20,-240,240);
    notebook_live.event_prompt=Boolean(object,"notebook_event_prompt",true);
    notebook_live.prompt_x=value("notebook_buttons_x",0,-400,400);
    notebook_live.prompt_y=value("notebook_prompt_y",0,-240,240);
    notebook_live.prompt_scale=value("notebook_buttons_scale",1,.5f,2);
    notebook_live.prompt_icon_scale=value("notebook_buttons_icon_scale",1,.5f,2);
    if(!object.contains("notebook_buttons_x") && notebook_live.map_prompt_y==notebook_live.prompt_y)
        notebook_live.map_prompt_y=std::max(-240.f,notebook_live.prompt_y-20.f);
    notebook_live.popup_width=value("notebook_popup_width",1.f,.5f,3.f);
    notebook_live.popup_height=value("notebook_popup_height",1.f,.5f,3.f);
    notebook_live.expanded_rows=Boolean(object,"notebook_expanded_rows",true);
    notebook_live.enabled=Boolean(object,"notebook_native",true);
    world_map_native_zoom={value("world_map_native_zoom_x",0,-800,800),value("world_map_native_zoom_y",0,-480,480),
        value("world_map_native_zoom_scale",1,.1f,3)};
    world_map_zoom_prompt={value("world_map_zoom_prompt_x",0,-400,400),value("world_map_zoom_prompt_y",0,-240,240),
        value("world_map_zoom_prompt_scale",.8f,.25f,2)};
    dungeon_map.enabled=Boolean(object,"dungeon_map_top",true);
    dungeon_map.side_panels=Boolean(object,"dungeon_map_side_panels",true);
    dungeon_map.top_strip=Boolean(object,"dungeon_map_top_strip",true);
    dungeon_map.top_panel={value("dungeon_map_top_panel_x",0,-1024,1024),value("dungeon_map_top_panel_y",0,-480,480),
        value("dungeon_map_top_panel_scale",.3125f,.1f,3)};
    dungeon_map.panel_darkness=value("dungeon_map_panel_darkness",0,0,1);
    for(unsigned i=0;i<2;++i) {
        const auto key=std::string("dungeon_map_panel_")+std::to_string(i)+"_";const auto d=DungeonMap::Options{}.panels[i];
        dungeon_map.panels[i]={value((key+"x"),d.x,-512,512),
            value((key+"y"),d.y,-480,480),value((key+"scale"),d.scale,.1f,3)};
    }
    for(unsigned i=0;i<DungeonMap::Count;++i) {
        const auto key=std::string("dungeon_map_")+std::to_string(i)+"_";
        auto& p=dungeon_map.poses[i];
        const bool model=i==DungeonMap::Model,description=i==DungeonMap::Description;
        p={value((key+"x"),model||description?0:40,model?-20:description?-320:-512,model?20:description?320:512),
           value((key+"y"),0,model?-20:-240,model?20:240),
           value((key+"scale"),1,model||description?.25f:.1f,model||description?2.f:3.f)};
    }
    banners_migrated=Boolean(object,"banners_native",false);
    for(unsigned i=0;i<2;++i){const auto key=std::string("banner_")+std::to_string(i)+"_";auto& p=banners.parts[i];
        p.height=value((key+"height"),1,.25f,3);
        p.y=value((key+"y"),0,-240,240);
        p.text_scale=value((key+"text_scale"),1,.25f,3);
        p.text_x=value((key+"text_x"),i?200:40,-400,400);
        p.text_y=value((key+"text_y"),0,-240,240);
        p.brightness=value((key+"brightness"),1,0,1);
    }
    const auto read_diamond=[&](const std::string& key,HudButtons::Diamond& d){
        d.enabled=Boolean(object,key+"enabled",false);
        d.x=value((key+"x"),350,-512,512);d.y=value((key+"y"),55,-240,240);
        d.spacing=value((key+"spacing"),24,0,160);d.rotation=value((key+"rotation"),0,-180,180);};
    read_diamond("hud_diamond_",hud_buttons.diamond);
    for(unsigned i=0;i<3;++i)read_diamond(std::string("hud_button_layout_")+std::to_string(i+1)+"_diamond_",hud_buttons.preset_diamonds[i]);
    const auto read_buttons=[&](const std::string& prefix,HudButtons::Layout& layout){
        for(unsigned i=0;i<HudButtons::Count;++i){const auto key=prefix+std::to_string(i)+"_";
            layout[i]={value((key+"x"),0,-512,512),value((key+"y"),HudButtons::Default()[i].y,-240,240),value((key+"scale"),1,.1f,3)};}};
    read_buttons("hud_button_",hud_buttons.buttons);
    hud_buttons.flip_peak=value("hud_buttons_flip_peak",1.2f,1,1.2f);
    hud_buttons.scale=value("hud_buttons_scale",hud_buttons.buttons[1].scale,.1f,3);
    for(unsigned i=0;i<3;++i){const auto key=std::string("hud_button_layout_")+std::to_string(i+1)+"_";
        hud_buttons.saved[i]=Boolean(object,key+"saved",false);read_buttons(key,hud_buttons.presets[i]);
        hud_buttons.preset_flip_peaks[i]=value((key+"flip_peak"),1.2f,1,1.2f);
        hud_buttons.preset_scales[i]=value((key+"scale"),hud_buttons.presets[i][1].scale,.1f,3);}
    song_reset.enabled=Boolean(object,"song_reset_top",true);
    song_reset.background_x=value("song_reset_background_x",40,-512,512);
    for(unsigned i=0;i<SongReset::Count;++i){const auto key=std::string("song_reset_")+std::to_string(i)+"_";
        song_reset.poses[i]={value((key+"x"),SongReset::Options{}.poses[i].x,-512,512),value((key+"y"),0,-240,240),value((key+"scale"),1,.1f,3)};}
    double_time.enabled=Boolean(object,"double_time_top",true);
    double_time.current_day_only=Boolean(object,"double_time_current_day",true);
    for(unsigned i=0;i<DoubleTime::Count;++i){
        const auto key=std::string("double_time_")+std::to_string(i)+"_";
        double_time.poses[i]={value((key+"x"),40,-512,512),value((key+"y"),0,-240,240),value((key+"scale"),1,.1f,3)};
    }
    song_list.enabled=Boolean(object,"song_list_top",true);
    song_list.background_x=value("song_list_background_x",-230.f,-512.f,512.f);
    song_list.spacing=value("song_list_spacing",16.f,0.f,60.f);
    song_list.column_spacing=value("song_list_column_spacing",62.f,40.f,180.f);
    song_list.songs={value("song_list_x",24.f,-512.f,512.f),value("song_list_y",12.f,-240.f,240.f),value("song_list_scale",.35f,.1f,2.f)};
    song_list.staff={value("song_staff_x",90.f,-512.f,512.f),value("song_staff_y",0.f,-240.f,240.f),value("song_staff_scale",.8f,.1f,2.f)};
    ocarina_prompt.enabled=Boolean(object,"ocarina_prompts",true);
    ocarina_prompt.x=value("ocarina_prompt_x",0.f,-400.f,400.f);
    ocarina_prompt.y=value("ocarina_prompt_y",0.f,-240.f,240.f);
    ocarina_prompt.scale=value("ocarina_prompt_scale",1.f,.5f,2.f);
    ocarina_prompt.spacing=value("ocarina_prompt_spacing",22.f,0.f,60.f);
    ocarina_prompt.icon_scale=value("ocarina_icon_scale",1.f,.5f,2.f);
    file_select_prompt.enabled=Boolean(object,"file_select_prompts",true);
    file_select_prompt.x=value("file_select_prompt_x",0.0f,-400.0f,400.0f);
    file_select_prompt.y=value("file_select_prompt_y",0.0f,-240.0f,240.0f);
    file_select_prompt.scale=value("file_select_prompt_scale",1.0f,0.5f,2.0f);
    file_select_prompt.spacing=value("file_select_prompt_spacing",22.0f,0.0f,60.0f);
    file_select_prompt.icon_scale=value("file_select_prompt_icon_scale",1.0f,0.5f,2.0f);
    file_select.stone_spacing=value("file_select_stone_spacing",0.f,0.f,80.f);
    file_select.enabled=Boolean(object,"file_select_native",true);
    startup.logo.x=value("startup_logo_x",0,-400,400);
    startup.logo.y=value("startup_logo_y",0,-240,240);
    startup.logo.scale=value("startup_logo_scale",1,.1f,3);
    startup.copyright.x=value("startup_copyright_x",0,-400,400);
    startup.copyright.y=value("startup_copyright_y",0,-240,240);
    startup.copyright.scale=value("startup_copyright_scale",1,.1f,3);
    startup.press_start.x=value("startup_press_start_x",0,-400,400);
    startup.press_start.y=value("startup_press_start_y",0,-240,240);
    startup.press_start.scale=value("startup_press_start_scale",1,.1f,3);
    startup.loading_icon.x=value("startup_loading_icon_x",0,-400,400);
    startup.loading_icon.y=value("startup_loading_icon_y",0,-240,240);
    startup.loading_icon.scale=value("startup_loading_icon_scale",1,.1f,3);
    item_present.enabled=Boolean(object,"item_present_native",true);
    item_present.help_pose={value("item_present_help_x",280,-512,512),value("item_present_help_y",180,-512,512),value("item_present_help_scale",.8f,.1f,2)};
    item_present.help_spacing=value("item_present_help_spacing",6,0,100);
    item_present.horizontal=Boolean(object,"item_present_horizontal",false);
    for(unsigned i=0;i<NativeItemPresent::Count;++i) {
        const std::string key=std::string("item_present_")+std::to_string(i)+"_";auto& p=item_present.poses[i];
        p.x=value((key+"x"),p.x,-512.f,512.f);
        if(i==NativeItemPresent::Background)continue;
        p.y=value((key+"y"),p.y,-512.f,512.f);
        p.scale=value((key+"scale"),p.scale,.1f,2.f);
    }
    for(unsigned i=0;i<NativeFileSelect::Count;++i) {
        const std::string key=std::string("file_select_")+std::to_string(i)+"_";auto& p=file_select.poses[i];
        p.x=value((key+"x"),p.x,-512.f,512.f);
        p.y=value((key+"y"),p.y,-512.f,512.f);
        p.scale=value((key+"scale"),p.scale,.1f,2.f);
    }
    for(unsigned i=0;i<NativeNotebook::Count;++i) {
        const std::string key=std::string("notebook_live_")+std::to_string(i)+"_";auto& p=notebook_live.poses[i];
        p.x=value((key+"x"),p.x,-512.f,512.f);
        p.y=value((key+"y"),p.y,-512.f,512.f);
        p.scale=value((key+"scale"),p.scale,.1f,2.f);
    }
    inventory_menu.gear.enabled=Boolean(object,"gear_enabled",true);
    inventory_menu.gear.horizontal=Boolean(object,"gear_horizontal",true);
    for(unsigned i=0;i<GearMenu::Count;++i) {
        if(i==GearMenu::Background||i==GearMenu::Model||i==GearMenu::Description)continue; // Shared inventory settings.
        const std::string key=std::string("gear_")+std::to_string(i)+"_";
        auto& p=inventory_menu.gear.poses[i];
        const auto def=GearMenu::Options{}.poses[i];
        const bool model=i==GearMenu::Model;
        p.x=value((key+"x"),def.x,model?-20.f:-320.f,model?20.f:320.f);
        if(i==GearMenu::Background)continue;
        p.y=value((key+"y"),def.y,model?-20.f:-240.f,model?20.f:240.f);
        p.scale=value((key+"scale"),def.scale,.25f,2.f);
    }
    inventory_menu.masks_y=value("inventory_masks_y",0.f,-240.f,240.f);
    inventory_menu.masks_scale=value("inventory_masks_scale",1.f,.5f,1.5f);
    inventory_menu.high_res_pause=Boolean(object,"inventory_high_res_pause",true);
    inventory_menu.shared_slide=Boolean(object,"inventory_shared_slide",true);
    inventory_menu.bar_enabled=Boolean(object,"inventory_bar_enabled",true);
    inventory_menu.bar_scale=value("inventory_bar_scale",.65f,.35f,1.5f);
    inventory_menu.bar_spacing=value("inventory_bar_spacing",64.f,40.f,120.f);
    inventory_menu.bar_x=value("inventory_bar_x",inventory_menu.x,-320.f,320.f);
    inventory_menu.bar_y=value("inventory_bar_y",inventory_menu.y-64.f*inventory_menu.bar_scale-4.f,-240.f,240.f);
    inventory_menu.description_x=value("inventory_description_x",0.f,-320.f,320.f);
    inventory_menu.description_y=value("inventory_description_y",0.f,-240.f,240.f);
    inventory_menu.description_scale=value("inventory_description_scale",1.f,.25f,2.f);
    inventory_menu.model_x=value("inventory_model_x",0.f,-20.f,20.f);
    inventory_menu.model_y=value("inventory_model_y",0.f,-20.f,20.f);
    inventory_menu.model_scale=value("inventory_model_scale",1.f,.25f,2.f);
    inventory_menu.gear_button.x=value("inventory_button_gear_x",0.f,-400.f,400.f);
    inventory_menu.gear_button.y=value("inventory_button_gear_y",0.f,-240.f,240.f);
    inventory_menu.gear_button.scale=value("inventory_button_gear_scale",1.f,.25f,2.f);
    inventory_menu.help.enabled=Boolean(object,"inventory_help_enabled",true);
    inventory_menu.help.horizontal=Boolean(object,"inventory_help_horizontal",false);
    inventory_menu.help.spacing=value("inventory_help_spacing",6,0,100);
    inventory_menu.help.button_gap=value("inventory_help_button_gap",4,0,40);
    inventory_menu.help.button_scale=value("inventory_help_button_scale",1,.25f,3);
    inventory_menu.help.text_scale=value("inventory_help_text_scale",1,.25f,3);
    inventory_menu.help.opacity=value("inventory_prompt_opacity",1,0,1);
    inventory_menu.help.pose={value("inventory_help_0_x",280,-512,512),value("inventory_help_0_y",148,-240,240),
        value("inventory_help_0_scale",.8f,.1f,2)};
    inventory_menu.map_prompt.x=value("inventory_map_prompt_x",ItemsMapPrompt::DefaultPose.x,-400.f,400.f);
    inventory_menu.map_prompt.y=value("inventory_map_prompt_y",ItemsMapPrompt::DefaultPose.y,-240.f,240.f);
    inventory_menu.map_prompt.scale=value("inventory_map_prompt_scale",ItemsMapPrompt::DefaultPose.scale,.25f,2.f);
    for(unsigned i=0;i<3;++i) {
        const char* names[]={"r","arrow","text"};
        const auto prefix=std::string("inventory_map_prompt_")+names[i]+"_";
        auto& p=inventory_menu.map_prompt_elements[i];
        p.x=value((prefix+"x"),ItemsMapPrompt::DefaultElements[i].x,-400.f,400.f);
        p.y=value((prefix+"y"),ItemsMapPrompt::DefaultElements[i].y,-240.f,240.f);
        p.scale=value((prefix+"scale"),ItemsMapPrompt::DefaultElements[i].scale,.25f,4.f);
    }
    // Older settings mirrored the inventory prompt; carry that exact layout
    // forward until the map prompt receives its own saved controls.
    map_items_prompt.pose={value("map_items_prompt_x",-inventory_menu.map_prompt.x,-400,400),
        value("map_items_prompt_y",inventory_menu.map_prompt.y,-240,240),
        value("map_items_prompt_scale",inventory_menu.map_prompt.scale,.25f,2.f)};
    for(unsigned i=0;i<3;++i) {
        const char* names[]={"l","arrow","text"};
        const auto prefix=std::string("map_items_prompt_")+names[i]+"_";
        const auto& old=inventory_menu.map_prompt_elements[i];auto& p=map_items_prompt.elements[i];
        p.x=value((prefix+"x"),-old.x,-400,400);
        p.y=value((prefix+"y"),old.y,-240,240);
        p.scale=value((prefix+"scale"),old.scale,.25f,4.f);
    }
    inventory_menu.buttons_enabled=Boolean(object,"inventory_buttons_enabled",true);
    inventory_menu.shadow_width=value("brown_shadow_width",value("inventory_shadow_width",1.f,0.f,3.f),0.f,3.f);
    inventory_menu.shadow_opacity=value("brown_shadow_opacity",value("inventory_shadow_opacity",1.f,0.f,1.f),0.f,1.f);
    inventory_menu.background_enabled=Boolean(object,"inventory_background_enabled",true);
    inventory_menu.background.x=value("inventory_background_x",0.f,-400.f,400.f);
    inventory_menu.background.y=value("brown_background_y",value("inventory_background_y",0.f,-240.f,240.f),-240.f,240.f);
    inventory_menu.background.scale=value("brown_background_scale",value("inventory_background_scale",1.f,.25f,2.f),.25f,2.f);
    for(unsigned i=0;i<inventory_menu.buttons.size();++i) {
        const char* names[]={"back","masks","items"};
        const auto prefix=std::string("inventory_button_")+names[i]+"_";
        auto& b=inventory_menu.buttons[i];
        b.x=value((prefix+"x"),0.f,-400.f,400.f);
        b.y=value((prefix+"y"),0.f,-240.f,240.f);
        b.scale=value((prefix+"scale"),1.f,.25f,2.f);
    }
    player_hud.hide_minimap=Boolean(object,"player_hud_hide_minimap",false);
    for(unsigned i=0;i<6;++i) {
        const auto key=std::string("player_hud_button_")+HudAdjustments::ButtonKeys[i]+"_";
        player_hud.button_x[i]=int(value((key+"x"),0,-400,400));
        player_hud.button_y[i]=int(value((key+"y"),0,-240,240));
    }
    player_hud.hide_menu_prompts=Boolean(object,"player_hud_hide_menu_prompts",false);
    player_hud.heart_spacing=int(value("player_hud_heart_spacing",0,
        -HudAdjustments::HeartSpacingLimit,HudAdjustments::HeartSpacingLimit));
    player_hud.magic_height_steps=int(value("player_hud_magic_height",0,-10,10));
    player_hud.overall=int(value("player_hud_scale",0,-10,10));
    player_hud.top=int(value("player_hud_top_padding",0,0,40));
    player_hud.bottom=int(value("player_hud_bottom_padding",0,0,40));
    for(unsigned i=0;i<HudAdjustments::Count;++i) {
        const auto key=std::string("player_hud_")+HudAdjustments::Keys[i]+"_scale";
        player_hud.elements[i]=int(value(key,0,-10,10));
        const auto prefix=std::string("player_hud_")+HudAdjustments::Keys[i]+"_";
        player_hud.x[i]=int(value((prefix+"x"),0,-400,400));
        player_hud.y[i]=int(value((prefix+"y"),0,-240,240));
    }
    accurate_hle_audio=Boolean(object,"accurate_hle_audio",true);
    mute_music=Boolean(object,"mute_music",false);
    disable_heart_drops=Boolean(object,"disable_heart_drops",false);
    disable_rupee_drops=Boolean(object,"disable_rupee_drops",false);
    disable_fairy_drops=Boolean(object,"disable_fairy_drops",false);
    disable_magic_drops=Boolean(object,"disable_magic_drops",false);
    disable_ammo_pickups=Boolean(object,"disable_ammo_pickups",false);
    lose_rupees_on_death=Boolean(object,"lose_rupees_on_death",false);
    world_ends_on_death=Boolean(object,"world_ends_on_death",false);
    damage_multiplier=int(value("damage_multiplier",1.f,1.f,16.f));
    dialogue_scale=value("dialogue_scale",1.f,.5f,1.f);
    dialogue_y=value("dialogue_y",0.f,-120.f,120.f);
    dialogue_background_width=value("dialogue_background_width",1.f,1.f,2.f);
    dialogue_indicator_x=value("dialogue_indicator_x",0.f,-200.f,200.f);
    dialogue_indicator_y=value("dialogue_indicator_y",0.f,-64.f,64.f);
    double_prices = Boolean(object,"double_prices",false);
    single_screen_text = Boolean(object,"single_screen_text",true);
    fix_bloom = Boolean(object,"fix_bloom",true);
    red_potion_hearts = std::clamp(Integer(object,"red_potion_hearts",20),1,20);
    blue_potion_hearts = std::clamp(Integer(object,"blue_potion_hearts",20),1,20);
    bottled_fairy_hearts = std::clamp(Integer(object,"bottled_fairy_hearts",10),1,20);
    show_performance = Boolean(object,"show_performance",false);
    indicator = Boolean(object,"indicator",true);
    widescreen = Boolean(object,"widescreen",false);
    camera_zoom_out = value("camera_zoom_out", 1.f, 1.f, 1.5f);
    game_camera_fov = value("game_camera_fov", 65.f, 45.f, 70.f);
    restoration_hud_scale = value("restoration_hud_scale", 1.f, .5f, 1.5f);
    const OptionsMenu::Options options_defaults;
    options_menu.x=value("options_menu_x",options_defaults.x,-400.f,400.f);
    options_menu.y=value("options_menu_y",options_defaults.y,-240.f,240.f);
    options_menu.scale=value("options_menu_scale",options_defaults.scale,.1f,2.f);
    options_menu.spacing=value("options_menu_spacing",options_defaults.spacing,0.f,120.f);
    options_menu.description.x=value("options_description_x",0.f,-320.f,320.f);
    options_menu.description.y=value("options_description_y",0.f,-240.f,240.f);
    options_menu.description.scale=value("options_description_scale",1.f,.25f,2.f);
    carrots.x=value("carrots_x",0.f,-400.f,400.f);
    carrots.y=value("carrots_y",0.f,-240.f,240.f);
    carrots.scale=value("carrots_scale",1.f,.25f,3.f);
    rupee_keys.x=value("rupee_keys_x",0.f,-400.f,400.f);
    rupee_keys.y=value("rupee_keys_y",0.f,-240.f,240.f);
    rupee_keys.scale=value("rupee_keys_scale",1.f,.25f,3.f);
    minigame_hud.x=value("minigame_hud_x",0.f,-400.f,400.f);
    minigame_hud.y=value("minigame_hud_y",0.f,-240.f,240.f);
    minigame_hud.scale=value("minigame_hud_scale",1.f,.25f,3.f);
    magic_border.enabled=Boolean(object,"magic_border_enabled",true);
    magic_border.shadow_enabled=Boolean(object,"magic_shadow_enabled",true);
    magic_border.shadow_size=value("magic_shadow_size",2.5f,0.f,8.f);
    magic_border.shadow_opacity=value("magic_shadow_opacity",.8f,0.f,1.f);
    magic_border.thickness=value("magic_border_thickness",1.f,1.f,3.f);
    magic_border.color={value("magic_border_r",1.f,0.f,1.f),value("magic_border_g",1.f,0.f,1.f),value("magic_border_b",1.f,0.f,1.f)};
    magic_bar_y = value("magic_bar_y", 0.f, -64.f, 64.f);
    magic_bar_height = value("magic_bar_height", 1.f, .5f, 2.f);
    dpad_y = value("dpad_y", 0.f, -120.f, 120.f);
    minimap = Boolean(object,"minimap",true);
    minimap_scale = value("minimap_scale", .36f, .2f, .6f);
    minimap_opacity = value("minimap_opacity", .85f, .2f, 1.f);
    minimap_right_margin = value("minimap_right_margin", 8.f, -64.f, 120.f);
    minimap_bottom_margin = value("minimap_bottom_margin", 8.f, -64.f, 120.f);
    fullscreen_draws.clear();
    auto draws=Get(object,"fullscreen_draws");
    if(!draws.is_array()) draws=Json::array();
    // Older versions saved one rule. An explicit new empty array stays empty.
    if(!object.contains("fullscreen_draws") && Get(object,"fullscreen_draw").is_object())
        draws.push_back(Get(object,"fullscreen_draw"));
    for(const auto& draw:draws){
    if(fullscreen_draws.size()>=128)break;
    bool shader_ok=false, swizzle_ok=false;
    WidescreenProbe::Draw rule;
    rule.shader=ParseHex(Get(draw,"shader"),shader_ok);
    rule.swizzle=ParseHex(Get(draw,"swizzle"),swizzle_ok);
    bool valid=shader_ok && swizzle_ok;
    const auto read=[&](const char* key,unsigned& out) {
        const double n=Number(draw,key,-1);
        if(!std::isfinite(n) || n<0 || n>4294967295.0 || std::floor(n)!=n) valid=false;
        else out=static_cast<unsigned>(n);
    };
    read("vertices",rule.vertices); read("width",rule.width); read("height",rule.height);
    read("textures",rule.textures); read("blend",rule.blend); read("depth",rule.depth);
    if(valid && rule.vertices>0 && rule.vertices<=24 && rule.width>0 && rule.height>0)
        if(rule!=WidescreenProbe::TransitionOverlay && std::find(fullscreen_draws.begin(),fullscreen_draws.end(),rule)==fullscreen_draws.end())
            fullscreen_draws.push_back(rule);
    }
    scale = value("scale", 1, .5f, 2.f);
    opacity = value("opacity", .9f, .2f, 1.f);
    x = value("x", .02f, 0.f, .75f);
    y = value("y", .02f, 0.f, .9f);
    save_blocked = false;
    return true;
}
bool Settings::Save(const std::filesystem::path& path) const {
    if(save_blocked) return false;
    Json object{{"version", 1}, {"accurate_hle_audio",accurate_hle_audio}, {"damage_multiplier",damage_multiplier}, {"disable_heart_drops",disable_heart_drops}, {"disable_rupee_drops",disable_rupee_drops}, {"disable_fairy_drops",disable_fairy_drops}, {"disable_magic_drops",disable_magic_drops}, {"disable_ammo_pickups",disable_ammo_pickups}, {"lose_rupees_on_death",lose_rupees_on_death}, {"world_ends_on_death",world_ends_on_death}, {"double_prices",double_prices}, {"inventory_panel_x",inventory_menu.x}, {"inventory_panel_y",inventory_menu.y}, {"inventory_panel_scale",inventory_menu.scale}, {"dialogue_scale",dialogue_scale}, {"dialogue_y",dialogue_y}, {"dialogue_background_width",dialogue_background_width}, {"dialogue_indicator_x",dialogue_indicator_x}, {"dialogue_indicator_y",dialogue_indicator_y}, {"indicator", indicator}, {"show_performance", show_performance},
        {"scale", scale}, {"opacity", opacity}, {"x", x}, {"y", y},
        {"single_screen_text", single_screen_text}, {"fix_bloom", fix_bloom}, {"red_potion_hearts", red_potion_hearts}, {"blue_potion_hearts", blue_potion_hearts}, {"bottled_fairy_hearts", bottled_fairy_hearts}, {"widescreen", widescreen}, {"camera_zoom_out", camera_zoom_out}, {"game_camera_fov", game_camera_fov}, {"restoration_hud_scale", restoration_hud_scale}, {"minigame_hud_x",minigame_hud.x},{"minigame_hud_y",minigame_hud.y},{"minigame_hud_scale",minigame_hud.scale},{"magic_shadow_enabled",magic_border.shadow_enabled},{"magic_shadow_size",magic_border.shadow_size},{"magic_shadow_opacity",magic_border.shadow_opacity},{"magic_border_enabled",magic_border.enabled},{"magic_border_thickness",magic_border.thickness},{"magic_border_r",magic_border.color[0]},{"magic_border_g",magic_border.color[1]},{"magic_border_b",magic_border.color[2]}, {"magic_bar_y", magic_bar_y}, {"magic_bar_height", magic_bar_height}, {"dpad_y", dpad_y},
        {"minimap", minimap}, {"minimap_scale", minimap_scale}, {"minimap_opacity", minimap_opacity},
        {"minimap_right_margin", minimap_right_margin}, {"minimap_bottom_margin", minimap_bottom_margin}};
    for(const auto& f:SubmenuHeader::Fields) object[f.key]=submenu_header.*f.member;
    object["player_hud_hide_minimap"]=player_hud.hide_minimap;
    object["mute_music"]=mute_music;
    for(unsigned i=0;i<6;++i) {
        const auto key=std::string("player_hud_button_")+HudAdjustments::ButtonKeys[i]+"_";
        object[key+"x"]=player_hud.button_x[i];object[key+"y"]=player_hud.button_y[i];
    }
    object["player_hud_hide_menu_prompts"]=player_hud.hide_menu_prompts;
    object["player_hud_heart_spacing"]=player_hud.heart_spacing;
    object["player_hud_magic_height"]=player_hud.magic_height_steps;
    object["player_hud_scale"]=player_hud.overall;
    object["player_hud_top_padding"]=player_hud.top;
    object["player_hud_bottom_padding"]=player_hud.bottom;
    for(unsigned i=0;i<HudAdjustments::Count;++i) {
        const auto prefix=std::string("player_hud_")+HudAdjustments::Keys[i]+"_";
        object[prefix+"scale"]=player_hud.elements[i];
        object[prefix+"x"]=player_hud.x[i];object[prefix+"y"]=player_hud.y[i];
    }
    object["inventory_native"]=inventory_menu.enabled;
    object["notebook_popup_width"]=notebook_live.popup_width;
    object["notebook_popup_height"]=notebook_live.popup_height;
    object["notebook_schedule_prompt"]=notebook_live.schedule_prompt;
    object["notebook_schedule_prompt_y"]=notebook_live.schedule_prompt_y;
    object["options_menu_x"]=options_menu.x;
    object["options_menu_y"]=options_menu.y;
    object["options_menu_scale"]=options_menu.scale;
    object["options_menu_spacing"]=options_menu.spacing;
    object["options_description_x"]=options_menu.description.x;
    object["options_description_y"]=options_menu.description.y;
    object["options_description_scale"]=options_menu.description.scale;
    object["carrots_x"]=carrots.x;
    object["carrots_y"]=carrots.y;
    object["carrots_scale"]=carrots.scale;
    object["rupee_keys_x"]=rupee_keys.x;
    object["rupee_keys_y"]=rupee_keys.y;
    object["rupee_keys_scale"]=rupee_keys.scale;
    object["notebook_map_prompt"]=notebook_live.map_prompt;
    object["notebook_map_prompt_y"]=notebook_live.map_prompt_y;
    object["notebook_event_prompt"]=notebook_live.event_prompt;
    object["notebook_buttons_x"]=notebook_live.prompt_x;
    object["notebook_prompt_y"]=notebook_live.prompt_y;
    object["notebook_buttons_scale"]=notebook_live.prompt_scale;
    object["notebook_buttons_icon_scale"]=notebook_live.prompt_icon_scale;
    object["notebook_native"]=notebook_live.enabled;
    object["notebook_expanded_rows"]=notebook_live.expanded_rows;
    object["world_map_native_zoom_x"]=world_map_native_zoom.x;
    object["world_map_native_zoom_y"]=world_map_native_zoom.y;
    object["world_map_native_zoom_scale"]=world_map_native_zoom.scale;
    object["world_map_zoom_prompt_x"]=world_map_zoom_prompt.x;
    object["world_map_zoom_prompt_y"]=world_map_zoom_prompt.y;
    object["world_map_zoom_prompt_scale"]=world_map_zoom_prompt.scale;
    object["dungeon_map_top"]=dungeon_map.enabled;
    object["dungeon_map_side_panels"]=dungeon_map.side_panels;
    object["dungeon_map_top_strip"]=dungeon_map.top_strip;
    object["dungeon_map_top_panel_x"]=dungeon_map.top_panel.x;
    object["dungeon_map_top_panel_y"]=dungeon_map.top_panel.y;
    object["dungeon_map_top_panel_scale"]=dungeon_map.top_panel.scale;
    object["dungeon_map_panel_darkness"]=dungeon_map.panel_darkness;
    for(unsigned i=0;i<2;++i) {
        const auto key=std::string("dungeon_map_panel_")+std::to_string(i)+"_";const auto p=dungeon_map.panels[i];
        object[key+"x"]=p.x;object[key+"y"]=p.y;object[key+"scale"]=p.scale;
    }
    for(unsigned i=0;i<DungeonMap::Count;++i) {
        const auto key=std::string("dungeon_map_")+std::to_string(i)+"_";const auto p=dungeon_map.poses[i];
        object[key+"x"]=p.x;object[key+"y"]=p.y;object[key+"scale"]=p.scale;
    }
    object["banners_native"]=banners_migrated;
    for(unsigned i=0;i<2;++i){const auto key=std::string("banner_")+std::to_string(i)+"_";const auto& p=banners.parts[i];
        object[key+"height"]=p.height;object[key+"y"]=p.y;object[key+"text_scale"]=p.text_scale;
        object[key+"text_x"]=p.text_x;object[key+"text_y"]=p.text_y;object[key+"brightness"]=p.brightness;}
    const auto write_diamond=[&](const std::string& key,const HudButtons::Diamond& d){
        object[key+"enabled"]=d.enabled;object[key+"x"]=d.x;object[key+"y"]=d.y;
        object[key+"spacing"]=d.spacing;object[key+"rotation"]=d.rotation;};
    write_diamond("hud_diamond_",hud_buttons.diamond);
    for(unsigned i=0;i<3;++i)write_diamond(std::string("hud_button_layout_")+std::to_string(i+1)+"_diamond_",hud_buttons.preset_diamonds[i]);
    const auto write_buttons=[&](const std::string& prefix,const HudButtons::Layout& layout){
        for(unsigned i=0;i<HudButtons::Count;++i){const auto key=prefix+std::to_string(i)+"_";const auto& p=layout[i];
            object[key+"x"]=p.x;object[key+"y"]=p.y;object[key+"scale"]=p.scale;}};
    write_buttons("hud_button_",hud_buttons.buttons);
    object["hud_buttons_scale"]=hud_buttons.scale;
    object["hud_buttons_flip_peak"]=hud_buttons.flip_peak;
    for(unsigned i=0;i<3;++i){const auto key=std::string("hud_button_layout_")+std::to_string(i+1)+"_";
        object[key+"saved"]=hud_buttons.saved[i];write_buttons(key,hud_buttons.presets[i]);object[key+"scale"]=hud_buttons.preset_scales[i];object[key+"flip_peak"]=hud_buttons.preset_flip_peaks[i];}
    object["song_reset_top"]=song_reset.enabled;object["song_reset_background_x"]=song_reset.background_x;
    for(unsigned i=0;i<SongReset::Count;++i){const auto key=std::string("song_reset_")+std::to_string(i)+"_";const auto& p=song_reset.poses[i];
        object[key+"x"]=p.x;object[key+"y"]=p.y;object[key+"scale"]=p.scale;}
    object["double_time_top"]=double_time.enabled;object["double_time_current_day"]=double_time.current_day_only;
    for(unsigned i=0;i<DoubleTime::Count;++i){const auto key=std::string("double_time_")+std::to_string(i)+"_";const auto p=double_time.poses[i];
        object[key+"x"]=p.x;object[key+"y"]=p.y;object[key+"scale"]=p.scale;}
    object["song_list_column_spacing"]=song_list.column_spacing;
    object["song_list_top"]=song_list.enabled;object["song_list_background_x"]=song_list.background_x;object["song_list_spacing"]=song_list.spacing;
    object["song_list_x"]=song_list.songs.x;object["song_list_y"]=song_list.songs.y;object["song_list_scale"]=song_list.songs.scale;
    object["song_staff_x"]=song_list.staff.x;object["song_staff_y"]=song_list.staff.y;object["song_staff_scale"]=song_list.staff.scale;
    object["ocarina_prompts"]=ocarina_prompt.enabled;
    object["ocarina_prompt_x"]=ocarina_prompt.x;
    object["ocarina_prompt_y"]=ocarina_prompt.y;
    object["ocarina_prompt_scale"]=ocarina_prompt.scale;
    object["ocarina_prompt_spacing"]=ocarina_prompt.spacing;
    object["ocarina_icon_scale"]=ocarina_prompt.icon_scale;
    object["file_select_prompts"]=file_select_prompt.enabled;
    object["file_select_prompt_x"]=file_select_prompt.x;
    object["file_select_prompt_y"]=file_select_prompt.y;
    object["file_select_prompt_scale"]=file_select_prompt.scale;
    object["file_select_prompt_spacing"]=file_select_prompt.spacing;
    object["file_select_prompt_icon_scale"]=file_select_prompt.icon_scale;
    object["file_select_stone_spacing"]=file_select.stone_spacing;
    object["file_select_native"]=file_select.enabled;
    object["startup_logo_x"]=startup.logo.x;
    object["startup_logo_y"]=startup.logo.y;
    object["startup_logo_scale"]=startup.logo.scale;
    object["startup_copyright_x"]=startup.copyright.x;
    object["startup_copyright_y"]=startup.copyright.y;
    object["startup_copyright_scale"]=startup.copyright.scale;
    object["startup_press_start_x"]=startup.press_start.x;
    object["startup_press_start_y"]=startup.press_start.y;
    object["startup_press_start_scale"]=startup.press_start.scale;
    object["startup_loading_icon_x"]=startup.loading_icon.x;
    object["startup_loading_icon_y"]=startup.loading_icon.y;
    object["startup_loading_icon_scale"]=startup.loading_icon.scale;
    object["item_present_native"]=item_present.enabled;
    object["item_present_help_x"]=item_present.help_pose.x;object["item_present_help_y"]=item_present.help_pose.y;
    object["item_present_help_scale"]=item_present.help_pose.scale;object["item_present_help_spacing"]=item_present.help_spacing;
    object["item_present_horizontal"]=item_present.horizontal;
    for(unsigned i=0;i<NativeItemPresent::Count;++i) {
        const std::string key=std::string("item_present_")+std::to_string(i)+"_";const auto& p=item_present.poses[i];
        object[key+"x"]=p.x;
        if(i!=NativeItemPresent::Background){object[key+"y"]=p.y;object[key+"scale"]=p.scale;}
    }
    for(unsigned i=0;i<NativeFileSelect::Count;++i) {
        const std::string key=std::string("file_select_")+std::to_string(i)+"_";const auto& p=file_select.poses[i];
        object[key+"x"]=p.x;object[key+"y"]=p.y;object[key+"scale"]=p.scale;
    }
    for(unsigned i=0;i<NativeNotebook::Count;++i) {
        const std::string key=std::string("notebook_live_")+std::to_string(i)+"_";const auto& p=notebook_live.poses[i];
        object[key+"x"]=p.x;object[key+"y"]=p.y;object[key+"scale"]=p.scale;
    }
    object["gear_enabled"]=inventory_menu.gear.enabled;
    object["gear_horizontal"]=inventory_menu.gear.horizontal;
    for(unsigned i=0;i<GearMenu::Count;++i) {
        if(i==GearMenu::Background||i==GearMenu::Model||i==GearMenu::Description)continue; // Shared inventory settings.
        const std::string key=std::string("gear_")+std::to_string(i)+"_";
        const auto& p=inventory_menu.gear.poses[i];
        object[key+"x"]=p.x;
        if(i!=GearMenu::Background){object[key+"y"]=p.y;object[key+"scale"]=p.scale;}
    }
    object["inventory_masks_x"]=inventory_menu.masks_x;
    object["inventory_masks_y"]=inventory_menu.masks_y;
    object["inventory_masks_scale"]=inventory_menu.masks_scale;
    object["inventory_high_res_pause"]=inventory_menu.high_res_pause;
    object["inventory_shared_slide"]=inventory_menu.shared_slide;
    object["inventory_bar_enabled"]=inventory_menu.bar_enabled;
    object["inventory_bar_x"]=inventory_menu.bar_x;
    object["inventory_bar_y"]=inventory_menu.bar_y;
    object["inventory_bar_scale"]=inventory_menu.bar_scale;
    object["inventory_bar_spacing"]=inventory_menu.bar_spacing;
    object["inventory_description_x"]=inventory_menu.description_x;
    object["inventory_description_y"]=inventory_menu.description_y;
    object["inventory_description_scale"]=inventory_menu.description_scale;
    object["inventory_model_x"]=inventory_menu.model_x;
    object["inventory_model_y"]=inventory_menu.model_y;
    object["inventory_model_scale"]=inventory_menu.model_scale;
    object["inventory_button_gear_x"]=inventory_menu.gear_button.x;
    object["inventory_button_gear_y"]=inventory_menu.gear_button.y;
    object["inventory_button_gear_scale"]=inventory_menu.gear_button.scale;
    object["inventory_help_enabled"]=inventory_menu.help.enabled;
    object["inventory_help_horizontal"]=inventory_menu.help.horizontal;
    object["inventory_help_spacing"]=inventory_menu.help.spacing;
    object["inventory_help_button_gap"]=inventory_menu.help.button_gap;
    object["inventory_help_button_scale"]=inventory_menu.help.button_scale;
    object["inventory_help_text_scale"]=inventory_menu.help.text_scale;
    object["inventory_prompt_opacity"]=inventory_menu.help.opacity;
    object["inventory_help_0_x"]=inventory_menu.help.pose.x;
    object["inventory_help_0_y"]=inventory_menu.help.pose.y;
    object["inventory_help_0_scale"]=inventory_menu.help.pose.scale;
    object["inventory_map_prompt_x"]=inventory_menu.map_prompt.x;
    object["inventory_map_prompt_y"]=inventory_menu.map_prompt.y;
    object["inventory_map_prompt_scale"]=inventory_menu.map_prompt.scale;
    for(unsigned i=0;i<3;++i) {
        const char* names[]={"r","arrow","text"};
        const auto prefix=std::string("inventory_map_prompt_")+names[i]+"_";
        const auto& p=inventory_menu.map_prompt_elements[i];
        object[prefix+"x"]=p.x;object[prefix+"y"]=p.y;object[prefix+"scale"]=p.scale;
    }
    object["map_items_prompt_x"]=map_items_prompt.pose.x;
    object["map_items_prompt_y"]=map_items_prompt.pose.y;
    object["map_items_prompt_scale"]=map_items_prompt.pose.scale;
    for(unsigned i=0;i<3;++i) {
        const char* names[]={"l","arrow","text"};
        const auto prefix=std::string("map_items_prompt_")+names[i]+"_";
        const auto& p=map_items_prompt.elements[i];
        object[prefix+"x"]=p.x;object[prefix+"y"]=p.y;object[prefix+"scale"]=p.scale;
    }
    object["inventory_buttons_enabled"]=inventory_menu.buttons_enabled;
    object["brown_shadow_width"]=inventory_menu.shadow_width;
    object["brown_shadow_opacity"]=inventory_menu.shadow_opacity;
    object["inventory_background_enabled"]=inventory_menu.background_enabled;
    object["inventory_background_x"]=inventory_menu.background.x;
    object["brown_background_y"]=inventory_menu.background.y;
    object["brown_background_scale"]=inventory_menu.background.scale;
    for(unsigned i=0;i<inventory_menu.buttons.size();++i) {
        const char* names[]={"back","masks","items"};
        const auto prefix=std::string("inventory_button_")+names[i]+"_";
        const auto& b=inventory_menu.buttons[i];
        object[prefix+"x"]=b.x;object[prefix+"y"]=b.y;object[prefix+"scale"]=b.scale;
    }
    Json saved_draws=Json::array();
    for(const auto& d:fullscreen_draws) {
        saved_draws.push_back(Json{
            {"shader",Hex(d.shader)}, {"swizzle",Hex(d.swizzle)},
            {"vertices",double(d.vertices)}, {"width",double(d.width)}, {"height",double(d.height)},
            {"textures",double(d.textures)}, {"blend",double(d.blend)}, {"depth",double(d.depth)}});
    }
    object["fullscreen_draws"]=saved_draws;
    return AtomicSave(path, object.dump(4) + "\n");
}

void ApplySettings(const Settings& settings) {
    HudAdjustments::Apply(settings.player_hud);
    PlayerHud::ApplyRupeeKeys(settings.rupee_keys);
    PlayerHud::ApplyCarrots(settings.carrots);
    PlayerHud::ApplyMagicHeight(settings.magic_bar_height);
    SettingsHost::accurate_hle_audio.store(settings.accurate_hle_audio);
    Music::muted.store(settings.mute_music);
    SingleScreenText::enabled.store(settings.single_screen_text);
    BloomSampling::enabled.store(settings.fix_bloom);
    BottleHealing::red_hearts.store(settings.red_potion_hearts);
    BottleHealing::blue_hearts.store(settings.blue_potion_hearts);
    BottleHealing::fairy_hearts.store(settings.bottled_fairy_hearts);
    Difficulty::damage_multiplier.store(settings.damage_multiplier);
    Prices::Initialize(settings.double_prices);
    Difficulty::disable_heart_drops.store(settings.disable_heart_drops);
    Difficulty::disable_rupee_drops.store(settings.disable_rupee_drops);
    Difficulty::disable_fairy_drops.store(settings.disable_fairy_drops);
    Difficulty::disable_magic_drops.store(settings.disable_magic_drops);
    Difficulty::disable_ammo_pickups.store(settings.disable_ammo_pickups);
    DeathPenalties::lose_rupees.store(settings.lose_rupees_on_death);
    DeathPenalties::world_ends.store(settings.world_ends_on_death);
    InventoryMenu::Apply(settings.inventory_menu);
    NativeNotebook::Apply(settings.notebook_live);
    NativeFileSelect::Apply(settings.file_select);
    OcarinaPrompt::Apply(settings.ocarina_prompt);
    FileSelectPrompt::Apply(settings.file_select_prompt);
    MagicBorder::Apply(settings.magic_border);
    SongList::Apply(settings.song_list);
    OptionsMenu::Apply(settings.options_menu);
    DoubleTime::Apply(settings.double_time);
    SongReset::Apply(settings.song_reset);
    HudButtons::Apply(settings.hud_buttons);
    MinigameHud::Apply(settings.minigame_hud);
    Banners::Apply(settings.banners);
    DungeonMap::Apply(settings.dungeon_map);
    ItemsMapPrompt::ApplyReturn(settings.map_items_prompt);
    WorldMapPrompt::Apply(settings.world_map_zoom_prompt);
    WorldMapPrompt::ApplyNative(settings.world_map_native_zoom);
    NativeItemPresent::Apply(settings.item_present);
    NativeStartup::Apply(settings.startup);
    DialogueScale::scale.store(settings.dialogue_scale);
    DialogueScale::y_offset.store(settings.dialogue_y);
    DialogueScale::background_width.store(settings.dialogue_background_width);
    DialogueScale::indicator_x.store(settings.dialogue_indicator_x);
    DialogueScale::indicator_y.store(settings.dialogue_indicator_y);
    CameraLive::SetFov(settings.game_camera_fov);
    Minimap::enabled.store(settings.minimap);
    Minimap::scale.store(settings.minimap_scale);
    Minimap::opacity.store(settings.minimap_opacity);
    NativeMinimap::right_margin.store(settings.minimap_right_margin);
    NativeMinimap::bottom_margin.store(settings.minimap_bottom_margin);
    Widescreen::enabled.store(settings.widescreen);
    Widescreen::camera_zoom_out.store(settings.camera_zoom_out);
    {
        std::scoped_lock lock(WidescreenProbe::mutex);
        WidescreenProbe::saved_draws=settings.fullscreen_draws;
    }
}

void ApplyStartupSettings(const Settings& settings) {
    HudAdjustments::minimap_visibility_request.store(-1);
    ApplySettings(settings);
}

bool MigrateLegacyBanners(Settings& settings, const LegacyBannerSettings& old) {
    if(settings.banners_migrated) return false;
    auto& p=settings.banners.parts[0];
    p.height=old.telop_scale;p.y=old.telop_y;p.text_scale=old.telop_text_scale;
    p.text_x=old.telop_x*(1+old.telop_text_scale-old.telop_scale);
    p.text_y=old.telop_text_y+66*(old.telop_text_scale-old.telop_scale);
    p.brightness=old.telop_brightness;settings.banners.parts[1].brightness=p.brightness;
    settings.banners_migrated=true;Banners::Apply(settings.banners);
    return true;
}
} // namespace Mm3dDekui
