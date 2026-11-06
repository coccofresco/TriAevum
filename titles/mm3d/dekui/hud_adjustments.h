// Ported from tgacquin/mm3d-dekui 6e36dfb4 src/mm3d/hud_adjustments.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
namespace Mm3dDekui::HudAdjustments {
enum Part : unsigned { Dpad, Magic, Hearts, Buttons, Minimap, Clock, Rupees, Minigame, Carrots, Timer, Count };
inline constexpr std::array<const char*,Count> Keys{"dpad","magic","hearts","buttons","minimap","clock","rupees","minigame","carrots","timer"};
inline constexpr std::array<const char*,Count> Labels{"D-pad","Magic meter","Hearts","Buttons","Minimap","Clock","Rupees","Minigame counter","Epona carrots","Timer"};
inline constexpr std::array<const char*,6> ButtonKeys{"x","y","i","ii","a","b"};
inline constexpr std::array<const char*,6> ButtonLabels{"X","Y","I","II","A","B"};
struct Options {float opacity=1.f,banner_darkness=0.f;std::array<int,6> button_x{},button_y{};bool hide_minimap=false;bool hide_menu_prompts=false;int banner_y=0;int overall=0,top=0,bottom=0,left=0,right=0,heart_spacing=0,magic_height_steps=0;std::array<int,Count> elements{},x{},y{};float magic_width=1.f;int base_scale=0;unsigned layout_version=0;bool mirror_item_buttons=false;};
// Version 0 keeps existing profiles and their original F2 reference points.
// Versions 1/2 store their arranged layouts directly; F2 displays offsets from
// the matching baseline so an update never changes an existing user's readouts.
// Rendering always consumes the same absolute values, avoiding re-scaling or
// re-positioning an existing layout during an upgrade.
inline Options VersionOneDefaults(){
    Options o;o.layout_version=1;o.base_scale=2;
    o.elements[Hearts]=4;o.x[Hearts]=-3;o.y[Hearts]=1;o.heart_spacing=-17;
    o.magic_height_steps=-4;o.magic_width=1.04f;o.y[Magic]=5;
    o.elements[Buttons]=1;o.elements[Minimap]=2;
    o.elements[Rupees]=2;o.x[Rupees]=-3;
    o.x[Minigame]=3;o.y[Minigame]=13;
    return o;
}
inline Options FreshDefaults(){
    auto o=VersionOneDefaults();o.layout_version=2;
    o.elements[Hearts]=5;o.heart_spacing=-25;
    o.magic_height_steps=-1;o.y[Magic]=4;o.elements[Rupees]=1;
    o.mirror_item_buttons=true;
    return o;
}
inline Options UiDefaults(const Options& o,bool player){
    if(!player)return {};
    if(o.layout_version==1)return VersionOneDefaults();
    return o.layout_version==2?FreshDefaults():Options{};
}
inline float DefaultMinimapOpacity(const Options& o){return o.layout_version==1||o.layout_version==2?.75f:.85f;}
inline void ResetPlayer(Options& o){const bool prompts=o.hide_menu_prompts;o=FreshDefaults();o.hide_menu_prompts=prompts;}
inline std::atomic<float> opacity{1.f},banner_darkness{0.f};
inline std::atomic<int> banner_y{0};
inline int BannerY(){return std::clamp(banner_y.load(),-240,240);}
inline float Unit(float v,float fallback){return std::isfinite(v)?std::clamp(v,0.f,1.f):fallback;}
inline float Opacity(){return Unit(opacity.load(),1.f);}
inline float BannerBrightness(){return 1.f-Unit(banner_darkness.load(),0.f);}
inline std::atomic<bool> hide_menu_prompts{false},hide_minimap{false},mirror_item_buttons{false};
// CPU shortcut updates visibility immediately; the render/UI thread consumes
// the request to synchronize its checkbox and persist the ordinary HUD setting.
inline std::atomic<int> minimap_visibility_request{-1};
inline void ToggleMinimap(){
    bool before=hide_minimap.load();
    while(!hide_minimap.compare_exchange_weak(before,!before)){}
    minimap_visibility_request.store(!before?1:0);
}
inline std::array<std::atomic<int>,6> button_x{},button_y{};
inline std::atomic<int> base_scale{0},overall{0},top{0},bottom{0},left{0},right{0},heart_spacing{0};
inline constexpr int SidePaddingLimit=100,VerticalPaddingLimit=40;
inline std::atomic<int> magic_height_steps{0};
inline std::atomic<float> magic_width{1.f};
inline float MagicWidth(float value){return std::isfinite(value)?std::clamp(value,.5f,3.f):1.f;}
inline constexpr int HeartSpacingLimit=40;
inline std::array<std::atomic<int>,Count> elements{},x{},y{};
inline float Factor(int steps){return 1.f+.05f*std::clamp(steps,-10,10);}
inline float OverallScale(){return Factor(base_scale.load())*Factor(overall.load());}
inline float Scale(Part part){return OverallScale()*Factor(elements[part].load());}
inline int X(Part part){return std::clamp(x[part].load(),-400,400);}
inline int Y(Part part){return std::clamp(y[part].load(),-240,240);}
inline int Top(){return std::clamp(top.load(),-VerticalPaddingLimit,VerticalPaddingLimit);}
inline int Bottom(){return std::clamp(bottom.load(),-VerticalPaddingLimit,VerticalPaddingLimit);}
inline int HorizontalPadding(Part part){
    switch(part){
    case Dpad:case Magic:case Hearts:case Rupees:
        return std::clamp(left.load(),-SidePaddingLimit,SidePaddingLimit);
    case Buttons:case Minimap:case Minigame:
        return -std::clamp(right.load(),-SidePaddingLimit,SidePaddingLimit);
    default:return 0; // Centered timers, clock and carrots retain their positions.
    }
}
inline void Apply(const Options& o){hide_minimap=o.hide_minimap;mirror_item_buttons=o.mirror_item_buttons;
    opacity=Unit(o.opacity,1.f);banner_darkness=Unit(o.banner_darkness,0.f);banner_y=std::clamp(o.banner_y,-240,240);
    for(unsigned i=0;i<6;++i){button_x[i]=std::clamp(o.button_x[i],-400,400);button_y[i]=std::clamp(o.button_y[i],-240,240);}
    hide_menu_prompts=o.hide_menu_prompts;base_scale=std::clamp(o.base_scale,-10,10);overall=o.overall;top=o.top;bottom=o.bottom;
    left=std::clamp(o.left,-SidePaddingLimit,SidePaddingLimit);right=std::clamp(o.right,-SidePaddingLimit,SidePaddingLimit);
    magic_height_steps=std::clamp(o.magic_height_steps,-10,10);
    magic_width=MagicWidth(o.magic_width);
    heart_spacing=std::clamp(o.heart_spacing,-HeartSpacingLimit,HeartSpacingLimit);
    for(unsigned i=0;i<Count;++i){elements[i]=o.elements[i];x[i]=o.x[i];y[i]=o.y[i];}}
}
