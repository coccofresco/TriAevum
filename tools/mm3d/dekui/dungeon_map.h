// Ported from mm3d-dekui src/mm3d/dungeon_map.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"
#include "native_minimap.h"
#include "item_preview.h"
#include <cstdio>
#include "schedule_detail.h"
#include "notebook_prompt.h"
#include "ocarina_prompt.h"

namespace Mm3dDekui::DungeonMap {
using namespace NativeInventory;
enum Part : unsigned { Map, Background, Items, Floors, Nameplate, Return, Zoom, Model, Description, Loupe, Count };
struct Options {
    bool enabled=true;
    bool side_panels=true,top_strip=true;
    ButtonPose top_panel{0,0,.3125f};
    float panel_darkness=0;
    std::array<ButtonPose,2> panels{{{0,120,.35f},{288,120,.35f}}};
    std::array<GearMenu::Pose,Count> poses;
    Options() { for(auto& p:poses)p={40,0,1};poses[Model]=poses[Description]={0,0,1}; }
};
inline std::atomic<bool> enabled{true},side_panels{true},top_strip{true};
inline AtomicButtonPose top_panel{0,0,.3125f};
inline std::atomic<float> panel_darkness{0};
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
    top_strip=o.top_strip;top_panel.x=o.top_panel.x;top_panel.y=o.top_panel.y;top_panel.scale=o.top_panel.scale;
    for(unsigned i=0;i<2;++i){panels[i].x=o.panels[i].x;panels[i].y=o.panels[i].y;panels[i].scale=o.panels[i].scale;}
    for(unsigned i=0;i<Count;++i){poses[i].x=o.poses[i].x;poses[i].y=o.poses[i].y;poses[i].scale=o.poses[i].scale;}
}
// Installed Restoration v110; original map renderer/input addresses minus 0x170.
constexpr std::array<U32,5> PCs{0x00596d94,0x00596e28,0x00596e2c,0x005945f0,0x00596e5c}; // ref 0x596c24, 0x596cb8, 0x596cbc, 0x594480, 0x596cec
constexpr std::array<U32,5> Words{0xe1a04000,0x0bf430bc,0xe5d4074c,0x15d6000c,0xe594001c}; // ref 0xbf4316b
constexpr U32 WorldEnd=0x00597164; // ref 0x596ff4
constexpr U32 FirstSVC=0xffffa0;
inline U32 Instruction(U32 pc,U32 word) {
    for(unsigned i=0;i<PCs.size();++i)if(pc==PCs[i]&&word==Words[i])
        return (word&0xf0000000)|0x0f000000|FirstSVC+i;
    return word;
}
template<class M> bool CodeMatches(M& m) {
    for(unsigned i=0;i<PCs.size();++i)if(m.Read(PCs[i])!=std::optional<U32>(Words[i]))return false;
    return m.Read(0x00596d90)==std::optional<U32>(0xe92d4ff3)&& // ref 0x596c20
        m.Read(0x00596e24)==std::optional<U32>(0x03510000)&& // ref 0x596cb4
        m.Read(0x005945f4)==std::optional<U32>(0x13100001)&& // ref 0x594484
        m.Read(0x00596e58)==std::optional<U32>(0xebef9f27)&& // ref 0x596ce8, 0xebef9fda
        m.Read(WorldEnd)==std::optional<U32>(0xe5940020)&&
        m.Read(0x0036e25c)==std::optional<U32>(0x6890f4)&& // ref 0x36e294
        m.Read(0x00175ad4)==std::optional<U32>(NativeMinimap::Manager); // ref 0x175c30
}
template<class M> std::optional<bool> Dungeon(M& m,U32 owner) {
    if(!enabled||!Pointer(owner,0x794))return {};
    auto layout=m.Read(owner+0x18),bits=m.Read(owner+0x78c);
    if(!layout||!LayoutNamed(m,*layout,"MenuMapDungeon")||!bits)return {};
    const unsigned outside=(*bits>>8)&255,soaring=(*bits>>16)&255;
    if(outside>1||soaring!=0)return {}; // Soaring/warp UI keeps its native behavior.
    return outside==0;
}
template<class M> U32 FilterA(M& m,U32 owner,U32 buttons) {
    auto mode=Dungeon(m,owner);return mode ? buttons&~1u : buttons;
}
struct Hook {
    NativeInventory::Hook helper;
    ItemPreview::Hook preview;
    NotebookPrompt::Hook shadow_scratch,panel_scratch;
    OcarinaPrompt::Hook map_prompts;
    U32 controller=0;
    float header_y=0;
    using Write=NativeInventory::Hook::Write;
    U32 owner=0,panel=0,cursor=0,pass=~0u;
    std::array<U32,2> backgrounds{};
    std::array<GearMenu::Pose,Count> frame;
    bool dungeon=false,valid=false,armed=false;
    std::vector<NativeMinimap::Entry> before;
    std::vector<U32> routed;
    std::vector<std::vector<Write>> frames;
    template<class M> void Capture(M& m,U32 page,U32 screen_context=0) {
        controller=screen_context;
        owner=page;valid=false;armed=false;routed.clear();status=0;
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
        unsigned found=0;
        for(unsigned i=0;i<*count;++i){
            auto child=m.Read(*list+i*4);if(!child)return false;
            unsigned j=0;for(;j<names.size();++j)if(Named(m,*child,names[j]))break;
            if(j==names.size()||(found&(1u<<j))||!transform(*child,parts[j]))return false;
            found|=1u<<j;
        }
        return found==0x7ff;
    }
    template<class M> void Pass(M& m,U32 manager,U32 screen) {
        pass=screen;if(screen!=4)return;routed.clear();header_y=0;
        // Read the same native title displacement used by the pause header.
        helper.slide_active=true;helper.ReadHeaderTransition(m,manager);helper.slide_active=false;
        header_y=helper.header_native_y;
        auto mode=Dungeon(m,owner);if(!valid||!mode||*mode!=dungeon)return;
        if(!Pointer(manager,0x24))return;
        std::array<U32,4> addresses{};
        std::array<NativeInventory::Hook::Buckets,4> queues;
        constexpr std::array<unsigned,4> screens{4,5,1,3};
        for(unsigned i=0;i<4;++i){auto q=m.Read(manager+8+screens[i]*4);if(!q||!helper.ReadQueue(m,*q,queues[i]))return;addresses[i]=*q;
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
        for(unsigned q=0;q<4;++q)for(unsigned p=0;p<16;++p)for(unsigned i=0;i<queues[q][p].size();++i)
            if(!m.Read(addresses[q]+4+p*0x80+i*4))return;
        for(unsigned q=0;q<4;++q){unsigned total=0;
            for(unsigned p=0;p<16;++p){auto& b=queues[q][p];for(unsigned i=0;i<b.size();++i)m.Write(addresses[q]+4+p*0x80+i*4,b[i]);
                m.Write(addresses[q]+0x804+p*4,b.size());total+=b.size();}
            m.Write(addresses[q]+0x844,total);
        }
        routed=std::move(moved);
    }
    template<class M> void Begin(M& m,U32 layout) {
        frames.emplace_back();if(!valid||!dungeon||(pass!=1&&pass!=3)||std::find(routed.begin(),routed.end(),layout)==routed.end())return;
        std::vector<Write> writes;if(!Prepare(m,layout,writes))return;
        for(auto w:writes)m.Write(w.address,w.after);frames.back()=std::move(writes);
    }
    template<class M> U32 PromptRoot(M& m,U32 layout,U32 sp,bool wide) {
        map_prompts.map_mode=true;map_prompts.pass=~0u;
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
    template<class M> U32 PanelRoot(M& m,U32 layout,U32 sp) {
        panel_scratch.frames.emplace_back();auto& saved=panel_scratch.frames.back();
        const U32 root=layout+8;
        const bool sides=side_panels.load(),top=top_strip.load();
        if((!sides&&!top)||!valid||!dungeon||pass!=1||layout!=backgrounds[0]||
           !LayoutNamed(m,layout,"CommonBottomBg")||
           std::find(routed.begin(),routed.end(),layout)==routed.end())return root;
        const auto all=panel_scratch.Child(m,root,"all");
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
        }
        for(U32 i=0;i<words.size();++i)saved.writes.push_back({base+4*i,*m.Read(base+4*i),words[i]});
        for(auto w:saved.writes)m.Write(w.address,w.after);saved.sp=sp;return base;
    }
    template<class M> U32 ShadowRoot(M& m,U32 layout,U32 sp) {
        shadow_scratch.frames.emplace_back();auto& saved=shadow_scratch.frames.back();
        const U32 root=layout+8;
        if(!valid||!dungeon||pass!=1||layout!=backgrounds[1]||
           !LayoutNamed(m,layout,"BackGround")||
           std::find(routed.begin(),routed.end(),layout)==routed.end())return root;
        const float width=NativeInventory::shadow_width.load(),opacity=NativeInventory::shadow_opacity.load();
        if(!std::isfinite(width)||width<=0||width>3||!std::isfinite(opacity)||opacity<=0||opacity>1)return root;
        auto all=shadow_scratch.Child(m,root,"backGround");
        auto source=all?shadow_scratch.Child(m,all,"map_base"):0;
        if(!source)return root;
        auto geometry=m.Read(source+0x28),flags=m.Read(source+0xd4);
        if(!geometry||!Pointer(*geometry,0x7c)||!flags||!(*flags&1)||
           m.Read(source+0xe4)!=std::optional<U32>(0)||m.Read(source+0xf0)!=std::optional<U32>(0))return root;
        auto type=m.Read(*geometry+4);if(!type||(*type&65535)!=6)return root;
        const auto read=[&](U32 at)->std::optional<float>{auto bits=m.Read(at);if(!bits)return {};
            float v=std::bit_cast<float>(*bits);return std::isfinite(v)?std::optional<float>(v):std::nullopt;};
        auto x=read(source+0xec),sx=read(source+0xe0),gx=read(*geometry+0x14),gw=read(*geometry+0x20),alpha=read(source+0x134);
        if(!x||!sx||!gx||!gw||!alpha||*sx<=0||*gw<=0||*alpha<=0||*alpha>1)return root;
        const float left=*x+*gx**sx,right=left+*gw**sx,band_width=16.f**sx*width/12.f;
        constexpr U32 Size=0x3200;
        if(sp<0x104000||(sp&7))return root;const U32 base=sp-Size;
        for(U32 at=base-Size;at<sp;at+=4)if(!m.Read(at))return root;
        std::array<U32,Size/4> words{};
        const auto put=[&](U32 at,U32 value){words[at/4]=value;};
        const auto f=[&](U32 at,float value){put(at,std::bit_cast<U32>(value));};
        const auto copy=[&](U32 at,U32 from,U32 size){for(U32 j=0;j<size;j+=4){auto v=m.Read(from+j);if(!v)return false;put(at+j,*v);}return true;};
        if(!copy(0,root,0x140))return root;
        put(0x14,base+0x140);put(0x18,25);put(0x20,0);put(0x24,0xffff);put(0x28,0);
        put(0x140,root);
        // Match the brown background's twelve-band quadratic falloff. Bands
        // sit entirely beyond either edge, so they cannot darken the map itself.
        for(unsigned side=0;side<2;++side)for(unsigned band=0;band<12;++band) {
            const unsigned index=side*12+band;
            const U32 node=0x200+index*0x200,geo=node+0x140;
            if(!copy(node,source,0x140)||!copy(geo,*geometry,0x7c))return root;
            put(0x144+index*4,base+node);put(node+0x28,base+geo);
            put(node+0x14,0);put(node+0x18,0);put(node+0x20,0);put(node+0x24,0xffff);
            const float scale=band_width / *gw,t=(band+.5f)/12.f;
            const float x=side?left-(band+1)*band_width:right+band*band_width;
            f(node+0xe0,scale);f(node+0xec,x-*gx*scale);
            for(unsigned c=0;c<3;++c)f(node+0x128+c*4,0);
            f(node+0x134,*alpha*opacity*.5f*(1-t)*(1-t));
        }
        for(U32 i=0;i<words.size();++i)saved.writes.push_back({base+4*i,*m.Read(base+4*i),words[i]});
        for(auto w:saved.writes)m.Write(w.address,w.after);saved.sp=sp;return base;
    }
    template<class M> void End(M& m) {
        if(frames.empty())return;for(auto it=frames.back().rbegin();it!=frames.back().rend();++it)m.Write(it->address,it->before);frames.pop_back();
    }
    template<class M> void BeforeMap(M& m) {
        armed=valid&&NativeMinimap::Hook::Queue(m,6,before);
    }
    template<class M> void AfterMap(M& m) {
        if(!std::exchange(armed,false)||!valid)return;
        using namespace NativeMinimap;
        std::vector<Entry> bottom,top,selected,retained;
        if(!NativeMinimap::Hook::Queue(m,6,bottom)||!NativeMinimap::Hook::Queue(m,4,top))return;
        auto map0=m.Read(0x6890ec),map1=m.Read(0x6890f0),radar=m.Read(0x727970+0x828);
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
