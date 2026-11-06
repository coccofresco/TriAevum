// Ported from mm3d-dekui src/mm3d/banners.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "song_list.h"
#include "hud_adjustments.h"
namespace Mm3dDekui::Banners {
using namespace NativeInventory;
struct Banner {float height=1,y=0,text_scale=1,text_x=40,text_y=0,brightness=1;};
struct Options {std::array<Banner,2> parts;Options(){parts[1].text_x=200;}};
struct AtomicBanner {std::atomic<float> height{1},y{0},text_scale{1},text_x{40},text_y{0},brightness{1};};
inline std::array<AtomicBanner,2> parts;
inline void Apply(const Options& o){for(unsigned i=0;i<2;++i){const auto& p=o.parts[i];auto& a=parts[i];
    a.height=p.height;a.y=p.y;a.text_scale=p.text_scale;a.text_x=p.text_x;a.text_y=p.text_y;a.brightness=p.brightness;}}
struct Hook {
    SongList::Hook helper;
    U32 owner=0;
    using Write=NativeInventory::Hook::Write;
    std::vector<std::vector<Write>> frames;
    template<class M> void Pass(M& m,U32 manager,U32 screen) {
        // Queue 0 runs before notebook/Items foreground (queues 2/3).
        // Wait until here so the inventory background has already been routed.
        if(screen!=0)return;
        owner=0;
        if(!Pointer(manager,0x24))return;
        using Buckets=NativeInventory::Hook::Buckets;
        std::array<U32,4> addresses{};std::array<Buckets,4> queues;
        bool menu=false,duplicate=false;U32 telop=0;
        for(unsigned i=0;i<4;++i) {
            auto address=m.Read(manager+8+4*i);
            if(!address||!Pointer(*address,0x848)||!helper.helper.ReadQueue(m,*address,queues[i]))return;
            addresses[i]=*address;
            for(unsigned j=0;j<i;++j)if(addresses[j]==*address)return;
            for(const auto& bucket:queues[i])for(auto entry:bucket) {
                if(LayoutNamed(m,entry,"Telop")) {
                    duplicate|=telop&&telop!=entry;telop=entry;
                }
                for(const char* name:{"MenuItem","MenuMask","MenuCollect","MenuQuestlist","MenuSchedule","UpQuestlist","UpSchedule","MenuMapDungeon","UpWorldMapBG","UpWorldMapHud"})
                    if(LayoutNamed(m,entry,name))menu=true;
            }
        }
        if(!menu){owner=duplicate?0:telop;return;}
        bool removed=false;
        for(unsigned i=0;i<4;++i)for(auto& bucket:queues[i]) {
            std::vector<U32> kept;
            for(auto entry:bucket) {
                if(LayoutNamed(m,entry,"Telop"))removed=true;
                else kept.push_back(entry);
            }
            bucket=std::move(kept);
        }
        if(!removed)return;
        // Telop is a live area/time announcement, not part of the captured
        // world. Drawing it behind opaque panels still exposes it over the
        // transparent preview backdrop. Omit this frame's queue entries;
        // leave the layout, animation, and next gameplay frame untouched.
        for(unsigned i=0;i<4;++i)for(unsigned p=0;p<16;++p)
            for(unsigned n=0;n<queues[i][p].size();++n)
                if(!m.Read(addresses[i]+4+p*0x80+n*4))return;
        for(unsigned i=0;i<4;++i) {
            U32 total=0;
            for(unsigned p=0;p<16;++p) {
                for(unsigned n=0;n<queues[i][p].size();++n)m.Write(addresses[i]+4+p*0x80+n*4,queues[i][p][n]);
                m.Write(addresses[i]+0x804+p*4,U32(queues[i][p].size()));total+=queues[i][p].size();
            }
            m.Write(addresses[i]+0x844,total);
        }
    }
    template<class M> void Begin(M& m,U32 layout,bool wide){
        frames.emplace_back();if(!LayoutNamed(m,layout,"Telop"))return;
        auto all=helper.Child(m,layout+8,"all");
        auto text=helper.Child(m,all,"text"),plate=helper.Child(m,all,"plate");
        if(!text||!plate)return;
        std::vector<Write> writes;
        constexpr const char* text_names[]{"e_map_telop_text_anim","e_time_telop_text_anim"};
        constexpr const char* pane_names[]{"mapTelop_pane00","timeTelop_pane00"};
        constexpr const char* anim_names[]{"mapTelop_anim","timeTelop_anim"};
        for(unsigned i=0;i<2;++i){
            const auto& a=parts[i];const Banner p{a.height.load(),a.y.load(),a.text_scale.load(),a.text_x.load(),a.text_y.load(),a.brightness.load()};
            for(float v:{p.height,p.y,p.text_scale,p.text_x,p.text_y,p.brightness})if(!std::isfinite(v))return;
            if(p.height<.25f||p.height>3||p.text_scale<.25f||p.text_scale>3||p.brightness<0||p.brightness>1)return;
            const auto anim=helper.Child(m,plate,anim_names[i]),pane=helper.Child(m,anim,pane_names[i]);
            const auto letters=helper.Child(m,text,text_names[i]);if(!pane||!letters)return;
            const float sx=wide?16.f/15.f:1.f,anchor_y=i?72.f:66.f;
            const float y=p.y+(i==0?HudAdjustments::BannerY():0);
            std::vector<U32> seen;
            if(!helper.helper.Transform(m,pane,writes,seen,{sx,0,0,p.height,200*(1-sx),anchor_y*(1-p.height)+y}))return;
            // Multiply RGB only. Keep the native gradient and animated opacity.
            for(U32 offset:{0x128u,0x12cu,0x130u}){
                auto bits=m.Read(pane+offset);if(!bits)return;
                const float c=std::bit_cast<float>(*bits);if(!std::isfinite(c))return;
                if(!helper.helper.Stage(m,writes,pane+offset,std::bit_cast<U32>(c*p.brightness*(i==0?HudAdjustments::BannerBrightness():1.f))))return;
            }
            seen.clear();const float text_anchor_x=i?200.f:40.f,text_anchor_y=i?120.f:66.f;
            if(!helper.helper.Transform(m,letters,writes,seen,{p.text_scale,0,0,p.text_scale,
                p.text_x-text_anchor_x*p.text_scale,text_anchor_y*(1-p.text_scale)+y+p.text_y}))return;
        }
        for(auto w:writes)m.Write(w.address,w.after);frames.back()=std::move(writes);
    }
    template<class M> void End(M& m){if(frames.empty())return;
        for(auto i=frames.back().rbegin();i!=frames.back().rend();++i)m.Write(i->address,i->before);frames.pop_back();}
};
}
