// Ported from mm3d-dekui src/mm3d/dungeon_map.h (tgacquin/mm3d-dekui 4f2692ed).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"
#include "native_minimap.h"
#include "item_preview.h"
#include <cstdio>
#include "schedule_detail.h"
#include "notebook_prompt.h"
#include "ocarina_prompt.h"
#include "message_layout.h"

namespace Mm3dDekui::DungeonMap {
using namespace NativeInventory;
enum Part : unsigned { Map, Background, Items, Floors, Nameplate, Return, Zoom, Model, Description, Loupe, Count };
struct Options {
    bool enabled=true;
    bool side_panels=true,top_strip=true;
    ButtonPose top_panel{0,0,.3125f};
    float panel_darkness=0;
    float arrow_scale=1;
    std::array<std::array<float,2>,4> arrow_offsets{}; // up, down, left, right
    OcarinaPrompt::Options prompts{true,0,0,1,1,20};
    // Retained global transform from older profiles; the editor now exposes
    // only prompts, shared by all four rows, without resetting saved placement.
    OcarinaPrompt::Group prompt_group;
    std::array<ButtonPose,2> panels{{{0,120,.35f},{288,120,.35f}}};
    std::array<GearMenu::Pose,Count> poses;
    Options() { for(auto& p:poses)p={40,0,1};poses[Model]=poses[Description]={0,0,1}; }
};
inline std::atomic<bool> enabled{true},side_panels{true},top_strip{true};
inline AtomicButtonPose top_panel{0,0,.3125f};
inline std::atomic<float> panel_darkness{0},arrow_scale{1};
inline std::array<std::array<std::atomic<float>,2>,4> arrow_offsets{};
inline constexpr std::array<const char*,4> ArrowNames{"up","down","left","right"};
inline std::atomic<float> prompt_x{0},prompt_y{0},prompt_scale{1},prompt_icon_scale{1},prompt_spacing{20};
inline AtomicButtonPose prompt_group_pose;
inline std::atomic<float> prompt_group_icon_scale{1};
inline OcarinaPrompt::Options PromptOptions() {
    return {true,prompt_x.load(),prompt_y.load(),prompt_scale.load(),prompt_icon_scale.load(),prompt_spacing.load()};
}
inline OcarinaPrompt::Group PromptGroup() {
    return {prompt_group_pose.x.load(),prompt_group_pose.y.load(),prompt_group_pose.scale.load(),prompt_group_icon_scale.load()};
}
inline std::array<AtomicButtonPose,2> panels{{{0,120,.35f},{288,120,.35f}}};
inline std::array<GearMenu::AtomicPose,Count> poses;
inline std::atomic<unsigned> status{0};
template<class M> void LogInput(M& m,U32 owner,U32 buttons){
    static FILE* f=[](){auto p=std::fopen("/tmp/mm3d-map-input.txt","w");if(p)std::fprintf(p,"owner buttons off74c off78c off34 off8c\n");return p;}();
    if(!f)return; auto read=[&](U32 at)->U32{auto v=m.Read(at);return v?*v:0;};
    std::fprintf(f,"%08x %08x %08x %08x %08x %08x\n",owner,buttons,read(owner+0x74c),read(owner+0x78c),read(owner+0x34),read(owner+0x8c));std::fflush(f);
}
// waiting, outside, dungeon, guarded
inline void Apply(const Options& o) {
    enabled=o.enabled;side_panels=o.side_panels;panel_darkness=o.panel_darkness;
    arrow_scale=o.arrow_scale;
    for(unsigned i=0;i<4;++i)for(unsigned c=0;c<2;++c)arrow_offsets[i][c]=o.arrow_offsets[i][c];
    prompt_group_pose.x=o.prompt_group.x;prompt_group_pose.y=o.prompt_group.y;
    prompt_group_pose.scale=o.prompt_group.scale;prompt_group_icon_scale=o.prompt_group.icon_scale;
    prompt_x=o.prompts.x;prompt_y=o.prompts.y;prompt_scale=o.prompts.scale;
    prompt_icon_scale=o.prompts.icon_scale;prompt_spacing=o.prompts.spacing;
    top_strip=o.top_strip;top_panel.x=o.top_panel.x;top_panel.y=o.top_panel.y;top_panel.scale=o.top_panel.scale;
    for(unsigned i=0;i<2;++i){panels[i].x=o.panels[i].x;panels[i].y=o.panels[i].y;panels[i].scale=o.panels[i].scale;}
    for(unsigned i=0;i<Count;++i){poses[i].x=o.poses[i].x;poses[i].y=o.poses[i].y;poses[i].scale=o.poses[i].scale;}
}
// Installed Restoration v110; original map renderer/input addresses minus 0x170.
// Translated to vanilla v1.1 (v110_address.h). Every word below is the original EUR v1.1 word
// (relocated B/BL words re-encoded for the translated target). Sites 5..7 are new in tip 4f2692ed.
constexpr std::array<U32,8> PCs{0x00596d94,0x00596e28,0x00596e2c,0x005945f0,0x00596e5c, // ref 0x596c24, 0x596cb8, 0x596cbc, 0x594480, 0x596cec
                                0x00594644,0x00597774,0x00594d98}; // ref 0x5944d4, 0x597604, 0x594c28
constexpr std::array<U32,8> Words{0xe1a04000,0x0bf430bc,0xe5d4074c,0x15d6000c,0xe594001c, // ref 0xbf4316b
                                  0xebf07902,0x03a00001,0xe2850fd7}; // ref 0xebf079db (BL SetZoom)
constexpr U32 SetZoom=0x001b2a54; // ref 0x1b2c48
constexpr std::array<std::pair<U32,U32>,14> ZoomGuards{{
    {0x0059463c,0x13a00003},{0x00594640,0x03a00001}, // ref 0x5944cc, 0x5944d0: accepted A toggle: modes 3/1
    {0x00597764,0xe3500002},{0x00597778,0x0bf06cb5}, // ref 0x5975f4, 0x597608 (0x0bf06d8e): mode 2 entry forces mode 1
    {0x001b2a54,0xe59f2018},{0x001b2a58,0xe3a01000},{0x001b2a5c,0xe3500003}, // ref 0x1b2c48..0x1b2c50
    {0x001b2a60,0xe5c21057},{0x001b2a64,0xe59f100c},{0x001b2a68,0x15c10522}, // ref 0x1b2c54..0x1b2c5c
    {0x001b2a6c,0xe5c10523},{0x001b2a70,0xe12fff1e},{0x001b2a74,0x6890f4},{0x001b2a78,0x7781d8} // ref 0x1b2c60..0x1b2c6c
}};
// The mode-3 branch moves the overview magnifier with the Circle Pad. Skip
// only its coordinate update; floor selection, A, and zoomed panning run elsewhere.
constexpr U32 OverviewCursorEnd=0x00594cd8; // ref 0x594b68
constexpr std::array<std::pair<U32,U32>,12> OverviewCursorGuards{{
    {0x00594cb4,0xebf07627},{0x00594cb8,0xe3500000},{0x00594cc0,0x0a000034}, // ref 0x594b44 (0xebf07700), 0x594b48, 0x594b50
    {0x00594da4,0xee0a0a81},{0x00594da8,0xee4a0a01},{0x00594df8,0xeaffffb6}, // ref 0x594c34, 0x594c38, 0x594c88
    {0x00594cd8,0xe320f000},{0x00594cdc,0xebf0761d}, // ref 0x594b68, 0x594b6c (0xebf076f6)
    {0x001b2558,0xe59f0010},{0x001b255c,0xe5d00523}, // ref 0x1b274c, 0x1b2750: native IsMapZoomed
    {0x001b2560,0xe3500003},{0x001b2570,0x7781d8} // ref 0x1b2754, 0x1b2764
}};
// Dungeon-map state 0x7781D8 (identity data address: literals 0x1B2A78, 0x1B2570, 0x1AEBDC and
// 0x36E268 hold it). Its word +0x520 carries the zoom mode in the top byte (+0x523).
constexpr U32 MapStateLiteral=0x0036e268; // ref 0x36e2a0
constexpr U32 MapState=0x007781d8,MapMode=MapState+0x520;
constexpr U32 WorldEnd=0x00597164; // ref 0x596ff4
constexpr U32 FirstSVC=0xffffa0;
inline U32 Instruction(U32 pc,U32 word) {
    for(unsigned i=0;i<PCs.size();++i)if(pc==PCs[i]&&word==Words[i])
        return (word & 0xf0000000) | 0x0f000000 | (FirstSVC + i);
    return word;
}
template<class M> bool CodeMatches(M& m) {
    for(unsigned i=0;i<PCs.size();++i)if(m.Read(PCs[i])!=std::optional<U32>(Words[i]))return false;
    for(auto [at,word]:ZoomGuards)if(m.Read(at)!=std::optional<U32>(word))return false;
    for(auto [at,word]:OverviewCursorGuards)if(m.Read(at)!=std::optional<U32>(word))return false;
    return m.Read(0x00596d90)==std::optional<U32>(0xe92d4ff3)&& // ref 0x596c20
        m.Read(0x00596e24)==std::optional<U32>(0x03510000)&& // ref 0x596cb4
        m.Read(0x005945f4)==std::optional<U32>(0x13100001)&& // ref 0x594484
        m.Read(0x00596e58)==std::optional<U32>(0xebef9f27)&& // ref 0x596ce8, 0xebef9fda
        m.Read(WorldEnd)==std::optional<U32>(0xe5940020)&&
        m.Read(0x0036e25c)==std::optional<U32>(0x6890f4)&& // ref 0x36e294
        m.Read(0x00175ad4)==std::optional<U32>(NativeMinimap::Manager); // ref 0x175c30
}
// Marker-style contract (S3 proof on vanilla v1.1: analysis/dekui/plan_2026-11/marker_style_writer.txt).
// Radar 0x727970 is the singleton returned by getter 0x1882F8 (0x188338 ldr r0,[pc,#8] -> literal
// 0x188348). Native MapTransformUpdate 0x1AE848 loads r6 = 0x7781D8 (pool 0x1AEBDC), reads the mode
// byte +0x523 and stores Radar+0x837 = (mode == 3) ? 0 : 1 (mode 3: 0x1AEBAC mov r1,#0 into the
// shared strb 0x1AEC08; mode 2: 0x1AEC04 mov r1,#1; other modes: 0x1AECB0/0x1AECB4). The marker
// builders read the byte (0x59C624 ldrb r5,[r0,#0x837]).
constexpr U32 Radar=0x00727970,MarkerStyle=Radar+0x834; // style = top byte (+0x837)
constexpr std::array<std::pair<U32,U32>,17> MarkerStyleGuards{{
    {0x00188338,0xe59f0008},{0x00188348,0x00727970}, // getter and its literal
    {0x001aeb18,0xe59f60bc},{0x001aeb1c,0xe5d60523},{0x001aeb20,0xe3500003},{0x001aeb24,0x0a000007},
    {0x001aeb28,0xe3500002},{0x001aeb2c,0x0a00002b},{0x001aebac,0xe3a01000},{0x001aebb4,0xea000013},
    {0x001aec04,0xe3a01001},{0x001aec08,0xe5c01837},{0x001aecb0,0xe3a01001},{0x001aecb4,0xe5c01837},
    {0x001aebdc,0x007781d8}, // writer pool
    {0x0059c624,0xe5d05837}, // marker builder reader
    {MapStateLiteral,MapState}
}};
template<class M> bool MarkerStyleContract(M& m) {
    for(auto [at,word]:MarkerStyleGuards)if(m.Read(at)!=std::optional<U32>(word))return false;
    return true;
}
// The native writer's value for a mode word: modes 1..3 only (upstream BeforeMap's bound).
inline std::optional<U32> MarkerStyleWord(U32 style,U32 mode) {
    if((mode>>24)<1||(mode>>24)>3)return {};
    return (style&0x00ffffffu)|((mode>>24)==3?0u:0x01000000u);
}
// The plan's REQUIRED T5 check gates the write: wft_pause_map states decoded offline must show
// +0x837 == 0 where the mode byte is 3 and 1 where it is 1 or 2 (W/tools/dekui/s3_marker_probe.py).
inline constexpr bool MarkerStyleConfirmed=false;
inline std::atomic<bool> marker_style_write{MarkerStyleConfirmed};
template<class M> std::optional<bool> Dungeon(M& m,U32 owner) {
    if(!enabled||!Pointer(owner,0x794))return {};
    auto layout=m.Read(owner+0x18),bits=m.Read(owner+0x78c);
    if(!layout||!LayoutNamed(m,*layout,"MenuMapDungeon")||!bits)return {};
    const unsigned outside=(*bits>>8)&255,soaring=(*bits>>16)&255;
    if(outside>1||soaring!=0)return {}; // Soaring/warp UI keeps its native behavior.
    return outside==0;
}
template<class M> U32 FilterA(M& m,U32 owner,U32 buttons) {
    auto mode=Dungeon(m,owner);return mode&&!*mode ? buttons&~1u : buttons;
}
template<class M> U32 OverviewCursorNext(M& m,U32 owner) {
    const auto dungeon=Dungeon(m,owner);
    const auto zoom=m.Read(MapMode);
    return dungeon&&*dungeon&&zoom&&(*zoom>>24)==3 ? OverviewCursorEnd : PCs[7]+4;
}
struct Hook {
    NativeInventory::Hook helper;
    ItemPreview::Hook preview;
    NotebookPrompt::Hook shadow_scratch,panel_scratch;
    OcarinaPrompt::Hook map_prompts;
    U32 controller=0,description_layout=0;
    float header_y=0;
    using Write=NativeInventory::Hook::Write;
    U32 owner=0,panel=0,cursor=0,pass=~0u;
    std::array<U32,2> backgrounds{};
    std::array<GearMenu::Pose,Count> frame;
    bool dungeon=false,valid=false,armed=false,border_overlay=false,arrows_early=false;
    std::vector<NativeMinimap::Entry> before;
    std::optional<U32> marker_style_restore;
    std::vector<U32> routed;
    std::vector<std::vector<Write>> frames;
    struct ZoomChoice {U32 game,scene,mode;};
    std::optional<ZoomChoice> zoom_choice;
    template<class M> static std::optional<U32> ZoomScene(M& m,U32 game) {
        if(!Pointer(game,0x170))return {};
        const auto state=m.Read(game+0x13c),scene=m.Read(game+0x148);
        if(!state||((*state>>8)&255)!=4||!scene||(*scene&65535)>0x74)return {};
        return *scene&65535;
    }
    template<class M> void RememberZoom(M& m,U32 page,U32 game,U32 mode) {
        // Observe only the accepted native A toggle, never the temporary
        // gameplay minimap transform or the menu's forced mode on entry/exit.
        const auto dungeon=Dungeon(m,page);
        const auto scene=ZoomScene(m,game);
        if(dungeon&&*dungeon&&scene&&(mode==1||mode==3))zoom_choice=ZoomChoice{game,*scene,mode};
    }
    template<class M> U32 EntryZoom(M& m,U32 game) const {
        // Applied before the native setter/initialization, so scale, icons,
        // prompts and shadow animations all see the same remembered choice.
        const auto scene=ZoomScene(m,game);
        if(enabled&&zoom_choice&&scene&&zoom_choice->game==game&&zoom_choice->scene==*scene)
            return zoom_choice->mode;
        return 1; // displaced MOVEQ r0,#1: retain the native first-entry default
    }
    template<class M> void Capture(M& m,U32 page,U32 screen_context=0) {
        controller=screen_context;
        owner=page;valid=false;armed=false;border_overlay=false;arrows_early=false;routed.clear();status=0;
        auto mode=Dungeon(m,page);if(!mode)return;
        panel=*m.Read(page+0x18);dungeon=*mode;
        auto a=m.Read(page+0x10),b=m.Read(page+0x14),c=m.Read(page+0x2c);
        if(!a||!b||!c)return;
        backgrounds={*a,*b};cursor=*c;
        // Shared-layout table: +0xC = CommonBottomBg, +4 = BackGround
        // (the map-base layer, distinct from the purple CommonUpBg).
        if(backgrounds[0]&&!LayoutNamed(m,backgrounds[0],"CommonBottomBg"))return;
        if(backgrounds[1]&&!LayoutNamed(m,backgrounds[1],"BackGround"))return;
        if(cursor&&!LayoutNamed(m,cursor,"CursorItemCollect"))return;
        for(unsigned i=0;i<Count;++i){
            frame[i]={poses[i].x.load(),poses[i].y.load(),poses[i].scale.load()};
            auto p=frame[i];if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.scale)||p.scale<.1f||p.scale>3)return;
        }
        valid=true;status=dungeon?2:1;
    }
    template<class M> std::optional<U32> ActiveContext(M& m) const {
        auto mode=Dungeon(m,owner);
        if(!valid||!mode||!*mode||!Pointer(controller,0x14)||
           m.Read(controller+8)!=std::optional<U32>(owner))return {};
        auto context=m.Read(controller+4);
        if(!context||!Pointer(*context,0x170))return {};
        return context;
    }
    // InterpolationIdentity (DungeonMapInterpolation: map pan identity for the renderer's
    // 60 Hz interpolation) is class (b) and stays with the renderer.
    template<class M> bool IsDescription(M& m,U32 layout) const {
        auto context=ActiveContext(m);
        if(!context||!LayoutNamed(m,layout,"MessageWindow"))return false;
        auto state=m.Read(*context+0x16c),window=m.Read(layout+0x444),id=m.Read(layout+0x3f4);
        // Native Map selection table6207FA: seven descriptions17AC..17B2.
        return state&&*state&&window&&*window==*state&&id&&*id>=0x17ac&&*id<=0x17b2;
    }
    template<class M> void ModelFrame(M& m,U32 context) {
        auto active=ActiveContext(m);
        preview.page=active?owner:0;preview.owner=active?*active:0;
        preview.ModelFrame(context);
        const auto& p=poses[Model];
        preview.sx=p.scale.load();preview.dx=p.x.load();preview.dy=p.y.load();
        if(!std::isfinite(preview.sx)||!std::isfinite(preview.dx)||!std::isfinite(preview.dy))preview.drawing=false;
        preview.sx=std::clamp(preview.sx,.25f,2.f);
        preview.dx=std::clamp(preview.dx,-20.f,20.f);preview.dy=std::clamp(preview.dy,-20.f,20.f);
    }
    template<class M> bool SkipWorld(M& m,U32 page) const {
        // Called after the purple UpWorldMapBG is submitted, before the world
        // map HUD, model and billboards. Resume at the shared title/cursor block.
        auto mode=Dungeon(m,page);
        if(!valid||page!=owner||!mode||!*mode)return false;
        auto bg=m.Read(page+0xc),hud=m.Read(page+0x1c);
        return bg&&hud&&LayoutNamed(m,*bg,"UpWorldMapBG")&&LayoutNamed(m,*hud,"UpWorldMapHud");
    }
    template<class M> bool AdjustArrows(M& m,U32 root,std::vector<Write>& writes) {
        const float scale=arrow_scale.load();
        if(!std::isfinite(scale)||scale<.1f||scale>3)return false;
        const auto p=frame[Map];
        for(unsigned i=0;i<4;++i) {
            const float x=arrow_offsets[i][0].load(),y=arrow_offsets[i][1].load();
            if(!std::isfinite(x)||!std::isfinite(y))return false;
            if(scale==1&&x==0&&y==0)continue; // old configurations are exact no-ops
            const std::string name=std::string("icon_")+ArrowNames[i]+"_direct";
            const U32 group=panel_scratch.Child(m,root,name.c_str());
            const U32 pane=group?panel_scratch.Child(m,group,(name+"_pane").c_str()):0;
            const auto geo=pane?m.Read(pane+0x28):std::optional<U32>{};
            if(!geo||!Pointer(*geo,0x7c))return false;
            const auto kind=m.Read(*geo+4);if(!kind||(*kind&65535)!=6)return false;
            const auto f=[&](U32 at)->std::optional<float>{const auto bits=m.Read(at);if(!bits)return {};
                const float v=std::bit_cast<float>(*bits);return std::isfinite(v)?std::optional<float>(v):std::nullopt;};
            const auto gx=f(*geo+0x14),gy=f(*geo+0x18),w=f(*geo+0x20),h=f(*geo+0x24),
                xx=f(pane+0xe0),xy=f(pane+0xe4),yx=f(pane+0xf0),yy=f(pane+0xf4),
                tx=f(pane+0xec),ty=f(pane+0xfc);
            if(!gx||!gy||!w||!h||!xx||!xy||!yx||!yy||!tx||!ty||*w<=0||*h<=0)return false;
            const float cx=*tx+*xx*(*gx+*w*.5f)+*xy*(*gy+*h*.5f),
                        cy=*ty+*yx*(*gx+*w*.5f)+*yy*(*gy+*h*.5f);
            // Scale each arrow about its own visual center. Append overrides
            // of the staged Map transform, leaving the touch rectangle alone.
            std::vector<U32> nodes;
            if(!helper.Transform(m,group,writes,nodes,{p.scale*scale,0,0,p.scale*scale,
                p.x+x+p.scale*(1-scale)*cx,p.y+y+p.scale*(1-scale)*cy}))return false;
        }
        return true;
    }
    template<class M> bool Prepare(M& m,U32 layout,std::vector<Write>& writes) {
        std::vector<U32> seen;
        const auto transform=[&](U32 node,unsigned part){auto p=frame[part];if(part==Nameplate)p.y+=header_y;return helper.Transform(m,node,writes,seen,{p.scale,0,0,p.scale,p.x,p.y});};
        if(layout==cursor){
            // Native MenuMapDungeon mode: 0=item strip, 1=map, 2=floors.
            auto bits=m.Read(panel+0x2c0);if(!bits)return false;
            const unsigned mode=(*bits>>8)&255;if(mode>2)return false;
            if(!transform(layout+8,mode==2?Floors:mode==0?Items:Map))return false;
            if(mode==2){
                for(auto w:writes)m.Write(w.address,w.after);
                std::vector<Write> clipped;bool ok=true;
                const auto bg=frame[Background];
                for(auto node:seen)if(!ScheduleDetail::Clip(m,node,-4096.f,bg.x+320.f*bg.scale,clipped)){ok=false;break;}
                for(auto it=writes.rbegin();it!=writes.rend();++it)m.Write(it->address,it->before);
                if(!ok)return false;
                writes.insert(writes.end(),clipped.begin(),clipped.end());
            }
            return true;
        }
        if(layout!=panel)return transform(layout+8,Background);
        auto roots=m.Read(layout+8+0x14),n=m.Read(layout+8+0x18);
        if(!roots||n!=std::optional<U32>(1))return false;
        auto all=m.Read(*roots);if(!all||!Named(m,*all,"all"))return false;
        auto list=m.Read(*all+0x14),count=m.Read(*all+0x18);
        if(!list||count!=std::optional<U32>(11))return false;
        constexpr std::array<const char*,11> names{"icon_touch_direct","cursor_loupe_g","disp_no_map_g","disp_wing_g","item_set_base_g","slide_gauge_g","btn_place_name_g","btn_expansion_g","btn_reduction_g","btn_return_l","bb_map_area_g"};
        constexpr std::array<unsigned,11> parts{Map,Loupe,Map,Map,Items,Floors,Nameplate,Zoom,Zoom,Return,Map};
        // Older installed archives hold both bb_map_area animations open.
        // Honor the native zoom state at draw time so those installs also hide
        // the inner shadows when enlarged, without rewriting owned resources.
        // Overview retains native visibility/fading; unrelated groups and the
        // custom outer shadows are unaffected. End restores the staged flags.
        const auto zoom=m.Read(MapStateLiteral)==std::optional<U32>(MapState)?
            m.Read(MapMode):std::optional<U32>{};
        const bool zoomed=zoom&&((*zoom>>24)==1||(*zoom>>24)==2);
        unsigned found=0;
        for(unsigned i=0;i<*count;++i){
            auto child=m.Read(*list+i*4);if(!child)return false;
            unsigned j=0;for(;j<names.size();++j)if(Named(m,*child,names[j]))break;
            if(j==names.size()||(found&(1u<<j))||!transform(*child,parts[j]))return false;
            if(j==0&&!AdjustArrows(m,*child,writes))return false;
            // The native touch Zoom buttons are replaced by the A/text prompt.
            // Only the four red direction arrows draw in the earlier pass.
            // The item/floor controls and labels retain their normal late pass.
            if(j==7||j==8||(j==10&&zoomed)||
               (arrows_early&&(pass==2||pass==3)&&((pass==2)!=(j==0)))) {
                auto flags=m.Read(*child+0xd4);
                if(!flags||!helper.Stage(m,writes,*child+0xd4,*flags&~1u))return false;
            }
            found|=1u<<j;
        }
        return found==0x7ff;
    }
    template<class M> void Pass(M& m,U32 manager,U32 screen) {
        pass=screen;if(screen!=4)return;routed.clear();border_overlay=false;arrows_early=false;header_y=0;
        // Read the same native title displacement used by the pause header.
        helper.slide_active=true;helper.ReadHeaderTransition(m,manager);helper.slide_active=false;
        header_y=helper.header_native_y;
        auto mode=Dungeon(m,owner);if(!valid||!mode||*mode!=dungeon)return;
        if(!Pointer(manager,0x24))return;
        std::array<U32,5> addresses{};
        std::array<NativeInventory::Hook::Buckets,5> queues;
        constexpr std::array<unsigned,5> screens{4,5,1,3,2};
        for(unsigned i=0;i<screens.size();++i){auto q=m.Read(manager+8+screens[i]*4);if(!q||!helper.ReadQueue(m,*q,queues[i]))return;addresses[i]=*q;
            for(unsigned j=0;j<i;++j)if(addresses[j]==*q)return;}
        unsigned submitted=0;for(auto& bucket:queues[1])submitted+=std::count(bucket.begin(),bucket.end(),panel);
        if(submitted!=1)return;
        std::vector<U32> moved;
        for(unsigned q=0;q<2;++q)for(unsigned p=0;p<16;++p){
            auto& bucket=queues[q][p];
            for(auto it=bucket.begin();it!=bucket.end();){
                const U32 entry=*it;
                const bool match=q==0?(entry&& (entry==backgrounds[0]||entry==backgrounds[1])):(entry==panel||(cursor&&entry==cursor));
                if(!match){++it;continue;}
                if(dungeon){
                    std::vector<Write> preview;if(!Prepare(m,entry,preview))return;
                    auto& target=queues[q+2][p];if(target.size()>=32)return;
                    for(auto& b:queues[q+2])if(std::find(b.begin(),b.end(),entry)!=b.end())return;
                    target.push_back(entry);moved.push_back(entry);
                }
                it=bucket.erase(it);
            }
        }
        // Native top-screen order is: background layout 1, map/radar batch 4,
        // layout 2 (banner/text), batch 5, layout 3 (menu controls). Insert the
        // border FIRST in layout 2, never around or after the menu's root.
        const bool overlay=dungeon&&(side_panels.load()||top_strip.load())&&
            std::find(moved.begin(),moved.end(),backgrounds[0])!=moved.end()&&
            std::find(moved.begin(),moved.end(),backgrounds[1])!=moved.end()&&MapWindow(m).has_value();
        const bool early=dungeon&&std::find(moved.begin(),moved.end(),panel)!=moved.end();
        std::vector<U32> prefix;
        if(overlay)prefix={backgrounds[0],backgrounds[1]};
        // Native MessageWindow draws before the final menu layer. Draw the
        // direction arrows before it as well, in their owning layout context.
        if(early)prefix.push_back(panel);
        for(const auto& bucket:queues[4])for(auto entry:prefix)
            if(std::find(bucket.begin(),bucket.end(),entry)!=bucket.end())return;
        auto& first=queues[4][0];if(first.size()+prefix.size()>32)return;
        first.insert(first.begin(),prefix.begin(),prefix.end());
        for(unsigned q=0;q<queues.size();++q)for(unsigned p=0;p<16;++p)for(unsigned i=0;i<queues[q][p].size();++i)
            if(!m.Read(addresses[q]+4+p*0x80+i*4))return;
        for(unsigned q=0;q<queues.size();++q){unsigned total=0;
            for(unsigned p=0;p<16;++p){auto& b=queues[q][p];for(unsigned i=0;i<b.size();++i)m.Write(addresses[q]+4+p*0x80+i*4,b[i]);
                m.Write(addresses[q]+0x804+p*4,b.size());total+=b.size();}
            m.Write(addresses[q]+0x844,total);
        }
        routed=std::move(moved);border_overlay=overlay;arrows_early=early;
    }
    template<class M> void Begin(M& m,U32 layout) {
        frames.emplace_back();if(!valid||!dungeon||(pass!=1&&pass!=2&&pass!=3)||std::find(routed.begin(),routed.end(),layout)==routed.end())return;
        std::vector<Write> writes;if(!Prepare(m,layout,writes))return;
        for(auto w:writes)m.Write(w.address,w.after);frames.back()=std::move(writes);
    }
    template<class M> U32 PromptRoot(M& m,U32 layout,U32 sp,bool wide) {
        map_prompts.map_mode=true;map_prompts.pass=~0u;
        map_prompts.group=PromptGroup();
        map_prompts.map_options=PromptOptions();
        if(valid&&dungeon&&pass==3&&layout==panel&&
           std::find(routed.begin(),routed.end(),layout)!=routed.end()) {
            std::vector<Write> probe;std::vector<U32> nodes;
            map_prompts.icon=map_prompts.text=0;map_prompts.texture=map_prompts.Parts(m);
            if(helper.Transform(m,layout+8,probe,nodes,{1,0,0,1,0,0}))for(auto node:nodes) {
                auto geo=m.Read(node+0x28);if(!geo)continue;auto kind=m.Read(*geo+4);if(!kind)continue;
                if(!map_prompts.icon&&(*kind&65535)==6&&Pointer(*geo,0x7c))map_prompts.icon=node;
                if(!map_prompts.text&&(*kind&65535)==3&&Pointer(*geo,0x30c)) {
                    auto font=m.Read(*geo+0x28);if(font&&Pointer(*font,0x14))map_prompts.text=node;
                }
            }
            auto group=shadow_scratch.Child(m,backgrounds[1]+8,"backGround");
            auto pane=group?shadow_scratch.Child(m,group,"map_base"):0;
            auto alpha=pane?m.Read(pane+0x134):std::optional<U32>{};
            if(alpha&&map_prompts.icon&&map_prompts.text&&map_prompts.texture) {
                float a=std::bit_cast<float>(*alpha);
                if(std::isfinite(a)&&a>=0&&a<=1){map_prompts.native_alpha=a;map_prompts.panel=layout;map_prompts.pass=3;}
            }
        }
        return map_prompts.Root(m,layout,sp,wide);
    }
    // Read the map's native backdrop before its temporary draw transform. Its
    // bounds, rather than the scrolling floor artwork, define the fixed window.
    template<class M> std::optional<std::array<float,3>> MapWindow(M& m) {
        if(!LayoutNamed(m,backgrounds[1],"BackGround"))return {};
        auto group=panel_scratch.Child(m,backgrounds[1]+8,"backGround");
        auto pane=group?panel_scratch.Child(m,group,"map_base"):0;
        auto geo=pane?m.Read(pane+0x28):std::optional<U32>{};
        if(!geo||!Pointer(*geo,0x7c))return {};
        auto kind=m.Read(*geo+4);if(!kind||(*kind&65535)!=6)return {};
        const auto f=[&](U32 at)->std::optional<float>{auto bits=m.Read(at);if(!bits)return {};
            float v=std::bit_cast<float>(*bits);return std::isfinite(v)?std::optional<float>(v):std::nullopt;};
        auto x=f(pane+0xec),y=f(pane+0xfc),sx=f(pane+0xe0),sy=f(pane+0xf4),
             xy=f(pane+0xe4),yx=f(pane+0xf0),gx=f(*geo+0x14),gy=f(*geo+0x18),w=f(*geo+0x20);
        if(!x||!y||!sx||!sy||!xy||!yx||!gx||!gy||!w||*sx<=0||*sy<=0||*w<=0||
           std::abs(*xy)>.0001f||std::abs(*yx)>.0001f)return {};
        const auto p=frame[Background];
        const float left=(*x+*sx**gx)*p.scale+p.x,right=left+*sx**w*p.scale,
                    top=(*y+*sy**gy)*p.scale+p.y;
        if(!std::isfinite(left)||!std::isfinite(right)||!std::isfinite(top)||right<=left)return {};
        return std::array{left,right,top};
    }
    template<class M> U32 PanelRoot(M& m,U32 layout,U32 sp,U32 content=0) {
        panel_scratch.frames.emplace_back();auto& saved=panel_scratch.frames.back();
        const U32 root=content?content:layout+8;
        const bool foreground=border_overlay&&pass==2&&layout==backgrounds[0];
        const bool sides=side_panels.load(),top=top_strip.load();
        if((!sides&&!top)||!valid||!dungeon||(!foreground&&(pass!=1||layout!=backgrounds[0]))||
           !LayoutNamed(m,backgrounds[0],"CommonBottomBg")||
           std::find(routed.begin(),routed.end(),layout)==routed.end())return root;
        const auto window=border_overlay?MapWindow(m):std::nullopt;
        if(foreground&&!window)return root;
        const auto all=panel_scratch.Child(m,backgrounds[0]+8,"all");
        std::array<U32,2> sources{panel_scratch.Child(m,all,"common_bottom_bg_pane0"),
                                 panel_scratch.Child(m,all,"common_bottom_bg_pane1")};
        if(!all||!sources[0]||!sources[1])return root;
        const float dark=panel_darkness.load();
        if(!std::isfinite(dark)||dark<0||dark>1)return root;
        std::array<ButtonPose,2> poses;
        for(unsigned i=0;i<2;++i) {
            poses[i]={panels[i].x.load(),panels[i].y.load(),panels[i].scale.load()};
            const auto p=poses[i];
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.scale)||p.scale<.1f||p.scale>3)return root;
        }
        const ButtonPose strip{top_panel.x.load(),top_panel.y.load(),top_panel.scale.load()};
        if(top&&(!std::isfinite(strip.x)||!std::isfinite(strip.y)||!std::isfinite(strip.scale)||strip.scale<.1f||strip.scale>3))return root;
        const unsigned side_count=sides?14:0,count=side_count+(top?8:0);
        constexpr U32 Size=0x3000;
        if(sp<0x104000||(sp&7))return root;const U32 base=sp-Size;
        for(U32 at=base-Size;at<sp;at+=4)if(!m.Read(at))return root;
        std::array<U32,Size/4> words{};
        const auto put=[&](U32 at,U32 v){words[at/4]=v;};
        const auto f=[&](U32 at,float v){put(at,std::bit_cast<U32>(v));};
        const auto copy=[&](U32 at,U32 from,U32 size){for(U32 j=0;j<size;j+=4){auto v=m.Read(from+j);if(!v)return false;put(at+j,*v);}return true;};
        if(!copy(0,root,0x140))return root;
        put(0x14,base+0x140);put(0x18,count);put(0x20,0);put(0x24,0xffff);put(0x28,0);
        // This root contains border pieces only. Existing UI roots remain in
        // their native queues, after the border, without any added nesting.
        // Both native halves form a complete320x240 panel. Repeat above it,
        // exactly like Items/Masks/Gear; never crop/stretch one half into a panel.
        for(unsigned i=0;i<count;++i) {
            const bool top_tile=i>=side_count,shadow=!top_tile&&i>=8;
            const unsigned side=shadow?(i-8)/3:i/4;
            const unsigned half=shadow?0:(top_tile?i-side_count:i)%2;
            const unsigned upper=shadow||top_tile?0:(i%4)/2,band=shadow?(i-8)%3:0;
            const auto source=sources[half];auto geo=m.Read(source+0x28),alpha=m.Read(source+0x134);
            if(!geo||!Pointer(*geo,0x7c)||!alpha)return root;
            auto kind=m.Read(*geo+4);if(!kind||(*kind&65535)!=6)return root;
            const float a=std::bit_cast<float>(*alpha);if(!std::isfinite(a)||a<0||a>1)return root;
            const U32 node=0x200+i*0x200,g=node+0x140;
            if(!copy(node,source,0x140)||!copy(g,*geo,0x7c))return root;
            put(0x140+i*4,base+node);put(node+0x28,base+g);
            put(node+0x14,0);put(node+0x18,0);put(node+0x20,0);put(node+0x24,0xffff);
            auto p=top_tile?strip:poses[side];
            // Four complete panels, edge-to-edge; one pose moves/scales them all.
            if(top_tile)p.x+=((i-side_count)/2)*320*p.scale;
            for(unsigned j=0;j<8;++j)f(node+0xe0+j*4,(j==0||j==5)?p.scale:0);
            f(node+0xec,p.x);f(node+0xfc,p.y+(shadow?240+2.f*band:upper?-240.f:0.f)*p.scale);
            for(unsigned c=0;c<3;++c) {
                const float color=std::bit_cast<float>(words[(node+0x128+c*4)/4]);
                if(!std::isfinite(color))return root;
                f(node+0x128+c*4,shadow?0:color*(1-dark));
            }
            if(shadow) {
                f(node+0x134,a*(.32f-.09f*band));
                f(g+0x14,0);f(g+0x18,0);f(g+0x20,320);f(g+0x24,2);
            }
            if(window) {
                // Split each tile into disjoint background/foreground pieces.
                // Keeping the UV transform preserves texel proportions, even
                // for the mirrored right half. Disjoint pieces also preserve
                // native fade opacity instead of drawing translucent tiles twice.
                const U32 axis=top_tile?1:0;
                const auto get=[&](U32 at){return std::bit_cast<float>(words[at/4]);};
                const U32 basis=node+(axis?0xf4:0xe0),translation=node+(axis?0xfc:0xec);
                const float origin=get(g+0x14+axis*4),extent=get(g+0x20+axis*4),scale=get(basis);
                const float begin=get(translation)+origin*scale,end=begin+extent*scale;
                const float edge=(*window)[top_tile?2:side?1:0];
                if(!std::isfinite(begin)||!std::isfinite(end)||end<=begin)return root;
                const bool keep_before=foreground?(top_tile||side==0):(!top_tile&&side==1);
                const float lo=keep_before?begin:std::max(begin,edge),hi=keep_before?std::min(end,edge):end;
                if(hi<=lo)put(node+0xd4,words[(node+0xd4)/4]&~1u);
                else if(lo!=begin||hi!=end) {
                    const float fraction=(lo-begin)/(end-begin),ratio=(hi-lo)/(end-begin);
                    f(basis,scale*ratio);f(translation,lo-origin*scale*ratio);
                    const float uv_origin=get(g+0x28+axis*4),uv_extent=get(g+0x30+axis*4);
                    if(!std::isfinite(uv_origin)||!std::isfinite(uv_extent))return root;
                    for(U32 row:{0x110u,0x11cu}) {
                        const float uv_basis=get(node+row+axis*4),offset=get(node+row+8);
                        if(!std::isfinite(uv_basis)||!std::isfinite(offset))return root;
                        f(node+row+axis*4,uv_basis*ratio);
                        f(node+row+8,offset+uv_basis*(uv_origin*(1-ratio)+fraction*uv_extent));
                    }
                }
            }
        }
        for(U32 i=0;i<words.size();++i)saved.writes.push_back({base+4*i,*m.Read(base+4*i),words[i]});
        for(auto w:saved.writes)m.Write(w.address,w.after);saved.sp=sp;return base;
    }
    template<class M> void RememberDescription(M& m,U32 layout) {
        if(IsDescription(m,layout))description_layout=layout;
    }
    // Exclude the live description plate from side-shadow geometry. The plate
    // is translucent: drawing shadows earlier still leaves dark bands visible
    // through it. Use its final world rectangle, including editor placement.
    template<class M> std::optional<std::array<float,4>> DescriptionBounds(M& m) const {
        // The shared message manager can point at another window. Prefer the
        // actual map description seen during native layout calculation, then
        // validate its context/message again before using it on this frame.
        U32 layout=description_layout;
        if(!IsDescription(m,layout))layout=m.Read(0x006b3700+0x2c).value_or(0); // ref 0x6b36f8 (message manager)
        if(!IsDescription(m,layout))return {};
        const auto panels=MessageLayout::Read(m,layout,false);
        std::optional<std::array<float,4>> bounds;
        for(const auto& p:panels) {
            const auto& x=p.transform[0];const auto& y=p.transform[1];
            // Opening animations and mirrored panes need not have perfectly
            // axis-aligned, positive bases. Bound all four transformed corners.
            std::array<float,4> rect{x[0],y[0],x[0],y[0]};
            for(unsigned corner=1;corner<4;++corner) {
                const float px=x[0]+(corner&1?x[3]:0)+(corner&2?x[2]:0),
                            py=y[0]+(corner&1?y[3]:0)+(corner&2?y[2]:0);
                rect[0]=std::min(rect[0],px);rect[1]=std::min(rect[1],py);
                rect[2]=std::max(rect[2],px);rect[3]=std::max(rect[3],py);
            }
            if(rect[0]>=rect[2]||rect[1]>=rect[3])continue;
            if(!bounds)bounds=rect;
            else {(*bounds)[0]=std::min((*bounds)[0],rect[0]);(*bounds)[1]=std::min((*bounds)[1],rect[1]);
                (*bounds)[2]=std::max((*bounds)[2],rect[2]);(*bounds)[3]=std::max((*bounds)[3],rect[3]);}
        }
        return bounds;
    }
    using Rectangle=std::array<float,4>;
    static std::vector<Rectangle> Outside(Rectangle box,const std::optional<Rectangle>& cover) {
        if(!cover)return {box};
        const float l=std::max(box[0],(*cover)[0]),t=std::max(box[1],(*cover)[1]),
                    r=std::min(box[2],(*cover)[2]),b=std::min(box[3],(*cover)[3]);
        if(l>=r||t>=b)return {box};
        std::vector<Rectangle> pieces;
        const auto add=[&](Rectangle part){if(part[0]<part[2]&&part[1]<part[3])pieces.push_back(part);};
        add({box[0],box[1],box[2],t});add({box[0],b,box[2],box[3]});
        add({box[0],t,l,b});add({r,t,box[2],b});
        return pieces;
    }
    template<class M> U32 ShadowRoot(M& m,U32 layout,U32 sp,U32 content=0) {
        shadow_scratch.frames.emplace_back();auto& saved=shadow_scratch.frames.back();
        const U32 root=content?content:layout+8;
        const bool foreground=border_overlay&&pass==2&&layout==backgrounds[1];
        // A disabled/invalid shadow must never redraw the map backdrop above
        // the floor artwork. Return an empty container for this extra pass.
        const auto fallback=[&]() -> U32 {
            if(!foreground)return root;
            constexpr U32 EmptySize=0x140;
            if(sp<0x104000||(sp&7))return root;
            const U32 base=sp-EmptySize;
            std::vector<Write> empty;
            for(U32 off=0;off<EmptySize;off+=4) {
                auto old=m.Read(base+off),value=m.Read(root+off);
                if(!old||!value)return root;
                if(off==0x14||off==0x18||off==0x20||off==0x28)*value=0;
                empty.push_back({base+off,*old,*value});
            }
            saved.writes=std::move(empty);saved.sp=sp;
            for(auto w:saved.writes)m.Write(w.address,w.after);
            return base;
        };
        if(!valid||!dungeon||(!foreground&&(pass!=1||layout!=backgrounds[1]))||
           !LayoutNamed(m,backgrounds[1],"BackGround")||
           std::find(routed.begin(),routed.end(),layout)==routed.end())return fallback();
        // The foreground map pass owns its shadow. Drawing the earlier copy too
        // would darken it twice while the menu fades.
        if(!foreground&&border_overlay)return fallback();
        const float width=NativeInventory::shadow_width.load(),opacity=NativeInventory::shadow_opacity.load();
        if(!std::isfinite(width)||width<=0||width>3||!std::isfinite(opacity)||opacity<=0||opacity>1)return fallback();
        auto all=shadow_scratch.Child(m,backgrounds[1]+8,"backGround");
        auto source=all?shadow_scratch.Child(m,all,"map_base"):0;
        if(!source)return fallback();
        auto geometry=m.Read(source+0x28),flags=m.Read(source+0xd4);
        const auto xy=m.Read(source+0xe4),yx=m.Read(source+0xf0);
        // Native animation can leave -0.0 in the off-axis terms. It describes
        // the same axis-aligned rectangle as +0.0; compare numbers, not bits.
        if(!geometry||!Pointer(*geometry,0x7c)||!flags||!(*flags&1)||!xy||!yx||
           std::bit_cast<float>(*xy)!=0.f||std::bit_cast<float>(*yx)!=0.f)return fallback();
        auto type=m.Read(*geometry+4);if(!type||(*type&65535)!=6)return fallback();
        const auto read=[&](U32 at)->std::optional<float>{auto bits=m.Read(at);if(!bits)return {};
            float v=std::bit_cast<float>(*bits);return std::isfinite(v)?std::optional<float>(v):std::nullopt;};
        auto x=read(source+0xec),sx=read(source+0xe0),gx=read(*geometry+0x14),gw=read(*geometry+0x20),alpha=read(source+0x134);
        if(!x||!sx||!gx||!gw||!alpha||*sx<=0||*gw<=0||*alpha<=0||*alpha>1)return fallback();
        // Begin has already applied the background pose in this layout.
        const auto background=GearMenu::Pose{};
        const float left=(*x+*gx**sx)*background.scale+background.x,
                    right=left+*gw**sx*background.scale,band_width=16.f**sx*background.scale*width/12.f;
        const auto description=DescriptionBounds(m);
        const U32 Size=description?0xc400:0x3200;
        if(sp<0x104000||(sp&7))return fallback();const U32 base=sp-Size;
        for(U32 at=base-Size;at<sp;at+=4)if(!m.Read(at))return fallback();
        std::vector<U32> words(Size/4);
        const auto put=[&](U32 at,U32 value){words[at/4]=value;};
        const auto f=[&](U32 at,float value){put(at,std::bit_cast<U32>(value));};
        const auto copy=[&](U32 at,U32 from,U32 size){for(U32 j=0;j<size;j+=4){auto v=m.Read(from+j);if(!v)return false;put(at+j,*v);}return true;};
        if(!copy(0,root,0x140))return fallback();
        put(0x14,base+0x140);put(0x18,foreground?24:25);put(0x20,0);put(0x24,0xffff);put(0x28,0);
        if(!foreground)put(0x140,root);
        // Match the brown background's twelve-band quadratic falloff. Bands
        // sit entirely beyond either edge, so they cannot darken the map itself.
        unsigned emitted=0;
        for(unsigned side=0;side<2;++side)for(unsigned band=0;band<12;++band) {
            const unsigned index=emitted;
            const U32 node=(description?0x300u:0x200u)+index*0x200,geo=node+0x140;
            if(!copy(node,source,0x140)||!copy(geo,*geometry,0x7c))return fallback();
            put(node+0x28,base+geo);
            put(node+0x14,0);put(node+0x18,0);put(node+0x20,0);put(node+0x24,0xffff);
            const float scale=band_width / *gw,t=(band+.5f)/12.f;
            const float x=side?left-(band+1)*band_width:right+band*band_width;
            f(node+0xe0,scale);f(node+0xec,x-*gx*scale);
            if(foreground) {
                const float y=std::bit_cast<float>(words[(node+0xfc)/4]);
                const float sy=std::bit_cast<float>(words[(node+0xf4)/4]);
                if(!std::isfinite(y)||!std::isfinite(sy))return fallback();
                f(node+0xfc,y*background.scale+background.y);f(node+0xf4,sy*background.scale);
            }
            for(unsigned c=0;c<3;++c)f(node+0x128+c*4,0);
            f(node+0x134,*alpha*opacity*.5f*(1-t)*(1-t));
            if(!description) {
                put(0x140+(emitted+++(foreground?0:1))*4,base+node);
                continue;
            }
            const auto get=[&](U32 at){return std::bit_cast<float>(words[at/4]);};
            const float gy=get(geo+0x18),gh=get(geo+0x24),sy=get(node+0xf4),y=get(node+0xfc)+gy*sy;
            if(!std::isfinite(y)||!std::isfinite(gh)||!std::isfinite(sy)||gh<=0||sy<=0)return fallback();
            const Rectangle full{x,y,x+band_width,y+gh*sy};
            std::array<U32,0x200/4> original{};
            std::copy_n(words.begin()+node/4,original.size(),original.begin());
            for(const auto& part:Outside(full,description)) {
                if(emitted>=96)return fallback();
                const U32 dest=(description?0x300u:0x200u)+emitted*0x200,dg=dest+0x140;
                std::copy(original.begin(),original.end(),words.begin()+dest/4);
                put(0x140+(emitted+++(foreground?0:1))*4,base+dest);put(dest+0x28,base+dg);
                for(unsigned axis=0;axis<2;++axis) {
                    const float extent=full[axis+2]-full[axis],ratio=(part[axis+2]-part[axis])/extent,
                                fraction=(part[axis]-full[axis])/extent;
                    const U32 basis=dest+(axis?0xf4:0xe0),translation=dest+(axis?0xfc:0xec);
                    const float scaled=get(basis)*ratio;
                    f(basis,scaled);f(translation,part[axis]-get(dg+0x14+axis*4)*scaled);
                    const float uv_origin=get(dg+0x28+axis*4),uv_extent=get(dg+0x30+axis*4);
                    for(U32 row:{0x110u,0x11cu}) {
                        const float uv_basis=get(dest+row+axis*4),offset=get(dest+row+8);
                        f(dest+row+axis*4,uv_basis*ratio);
                        f(dest+row+8,offset+uv_basis*(uv_origin*(1-ratio)+fraction*uv_extent));
                    }
                }
            }
        }
        put(0x18,emitted+(foreground?0:1));
        for(U32 i=0;i<words.size();++i)saved.writes.push_back({base+4*i,*m.Read(base+4*i),words[i]});
        for(auto w:saved.writes)m.Write(w.address,w.after);saved.sp=sp;return base;
    }
    template<class M> void End(M& m) {
        if(frames.empty())return;for(auto it=frames.back().rbegin();it!=frames.back().rend();++it)m.Write(it->address,it->before);frames.pop_back();
    }
    template<class M> void RestoreMarkerStyle(M& m) {
        if(marker_style_restore)m.Write(MarkerStyle,*marker_style_restore);
        marker_style_restore.reset();
    }
    template<class M> void BeforeMap(M& m) {
        RestoreMarkerStyle(m);
        armed=valid&&NativeMinimap::Hook::Queue(m,6,before);
        if(!armed||!dungeon||!marker_style_write.load()||!MarkerStyleContract(m))return;
        // Native MapTransformUpdate sets Radar+837 from the zoom mode during
        // Calc, but the first opening draw can precede that update. The map
        // builder reads the current mode while marker builders use this cached
        // byte (small atlas/quad for mode3, large for modes1/2). Match them for
        // this draw so the gameplay minimap's small icons cannot flash on entry.
        auto mode=m.Read(MapMode),style=m.Read(MarkerStyle);
        if(!mode||!style)return;
        const auto current=MarkerStyleWord(*style,*mode);
        if(current&&*current!=*style){marker_style_restore=*style;m.Write(MarkerStyle,*current);}
    }
    template<class M> void AfterMap(M& m) {
        RestoreMarkerStyle(m);
        if(!std::exchange(armed,false)||!valid)return;
        using namespace NativeMinimap;
        std::vector<Entry> bottom,top,selected,retained;
        if(!NativeMinimap::Hook::Queue(m,6,bottom)||!NativeMinimap::Hook::Queue(m,4,top))return;
        auto map0=m.Read(0x6890ec),map1=m.Read(0x6890f0),radar=m.Read(Radar+0x828);
        if(!map0||!map1||!radar)return;
        auto unmatched=before;
        for(auto e:bottom){
            auto old=std::find_if(unmatched.begin(),unmatched.end(),[&](auto v){return v.object==e.object&&v.priority==e.priority;});
            if(old!=unmatched.end()){unmatched.erase(old);retained.push_back(e);continue;}
            if(e.object!=*map0&&e.object!=*map1&&e.object!=*radar)return;
            selected.push_back(e);
        }
        if(!unmatched.empty()||selected.size()>2||top.size()+selected.size()>Capacity)return;
        std::vector<Write> writes;
        if(dungeon)for(auto e:selected){
            for(unsigned q=0;q<8;++q){std::vector<Entry> entries;if(!NativeMinimap::Hook::Queue(m,q,entries))return;
                unsigned count=0;for(auto v:entries)count+=v.object==e.object;if(count!=(q==6?1u:0u))return;}
            if(!NativeInventory::Pointer(e.object,0x3a8))return;
            auto n=m.Read(e.object+0x2c8),cap=m.Read(e.object+0x2c4),pos=m.Read(e.object+0x2b8),size=m.Read(e.object+0x2c0);
            if(!n||!cap||!pos||!size||*n>*cap||*n>4096||!NativeInventory::Pointer(*pos,*n*12)||!NativeInventory::Pointer(*size,*n*12))return;
            const auto p=frame[Map];
            for(U32 i=0;i<*n;++i)for(auto [at,offset]:std::array<std::pair<U32,float>,4>{{{*pos+i*12,p.x},{*pos+i*12+4,p.y},{*size+i*12,0},{*size+i*12+4,0}}}){
                auto bits=m.Read(at);if(!bits)return;float v=std::bit_cast<float>(*bits),result=v*p.scale+offset;
                if(!std::isfinite(v)||!std::isfinite(result)||!helper.Stage(m,writes,at,std::bit_cast<U32>(result)))return;
            }
            top.push_back(e);
        }
        std::stable_sort(top.begin(),top.end(),[](auto a,auto b){return a.priority<b.priority;});
        for(unsigned i=0;i<top.size();++i)if(!m.Read(NativeMinimap::Pointer(4,i))||!m.Read(Priority(4,i)))return;
        for(auto w:writes)m.Write(w.address,w.after);
        for(auto [q,entries]:{std::pair{6u,&retained},std::pair{4u,&top}}){
            for(unsigned i=0;i<entries->size();++i){m.Write(NativeMinimap::Pointer(q,i),(*entries)[i].object);m.Write(Priority(q,i),(*entries)[i].priority);}
            m.Write(NativeMinimap::Count(q),entries->size());
        }
    }
};
}
