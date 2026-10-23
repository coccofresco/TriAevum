// Ported from mm3d-dekui src/mm3d/double_time.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "song_list.h"

namespace Mm3dDekui::DoubleTime {
using namespace NativeInventory;
enum Part : unsigned { Background, Prompt, Timeline, Instructions, Buttons, Count };
struct Options {
    bool enabled=true,current_day_only=true;
    std::array<GearMenu::Pose,Count> poses;
    Options(){for(auto& p:poses)p={40,0,1};}
};
inline std::atomic<bool> enabled{true},current_day_only{true};
inline std::array<GearMenu::AtomicPose,Count> poses;
inline void Apply(const Options& o){
    enabled=o.enabled;current_day_only=o.current_day_only;
    for(unsigned i=0;i<Count;++i){poses[i].x=o.poses[i].x;poses[i].y=o.poses[i].y;poses[i].scale=o.poses[i].scale;}
}
struct Hook {
    SongList::Hook helper;
    using Write=NativeInventory::Hook::Write;
    U32 panel=0,background=0,pass=~0u;
    std::array<GearMenu::Pose,Count> frame;
    std::vector<std::vector<Write>> frames;
    // Follow the same embedded-layout/group references as the native tree walk.
    template<class M> U32 EmbeddedRoot(M& m,U32 node){
        auto embedded=m.Read(node+0x20),group=m.Read(node+0x24);
        if(!embedded||!group)return 0;
        if(*embedded)return Pointer(*embedded,0x148)?*embedded+8:0;
        if((*group&0xffff)==0xffff)return 0;
        auto owner=m.Read(node+4);if(!owner||!Pointer(*owner,0x14c))return 0;
        auto count=m.Read(*owner+0x148),list=m.Read(*owner+0x144);
        if(!count||!list||*count>1024||(*group&0xffff)>=*count||!Pointer(*list,*count*4))return 0;
        auto target=m.Read(*list+4*(*group&0xffff));return target&&Pointer(*target,0x144)?*target+4:0;
    }
    template<class M> bool Prepare(M& m,std::vector<Write>& w){
        auto all=helper.Child(m,panel+8,"all");if(!all)return false;
        const std::array<std::pair<const char*,Part>,9> groups{{
            {"message_g",Prompt},{"link_icon_g",Timeline},{"disp_time_g",Timeline},
            {"sche_base_g",Timeline},{"popup_window00_g",Instructions},
            {"btn_advance_g",Buttons},{"btn_quit_g",Buttons},
            {"touch_rect_center_l",Timeline},{"touch_rect_link_l",Timeline}}};
        for(auto [name,part]:groups){
            auto node=helper.Child(m,all,name);if(!node)return false;
            auto p=frame[part];std::vector<U32> seen;
            if(!helper.helper.Transform(m,node,w,seen,{p.scale,0,0,p.scale,p.x,p.y}))return false;
        }
        if(current_day_only){
            // Original5F4C68 stores the opening/current day (0..2) at220.
            // Native day animation already positions that day's card and markers.
            auto day=m.Read(panel+0x220);if(!day||*day>2)return false;
            auto base=helper.Child(m,all,"sche_base_g");
            auto link=helper.Child(m,base,"sche_base_l");if(!link)return false;
            auto root=EmbeddedRoot(m,link);if(!root)return false;
            auto days=helper.Child(m,root,"all");if(!days)return false;
            constexpr const char* names[]{"sche_base0","sche_base1","sche_base2"};
            for(unsigned i=0;i<3;++i){
                auto node=helper.Child(m,days,names[i]);if(!node)return false;
                if(i!=*day&&!helper.Hide(m,node,w))return false;
            }
        }
        return true;
    }
    template<class M> void Pass(M& m,U32 manager,U32 screen){
        pass=screen;if(screen!=4)return;
        panel=background=0;if(!enabled)return;
        NativeInventory::Hook::Buckets from,to,bf,bt;
        auto bottom=m.Read(manager+28),top=m.Read(manager+20),bgfrom=m.Read(manager+24),bgto=m.Read(manager+12);
        if(!bottom||!top||!bgfrom||!bgto||*bottom==*top||*bgfrom==*bgto||
           !helper.helper.ReadQueue(m,*bottom,from)||!helper.helper.ReadQueue(m,*top,to)||
           !helper.helper.ReadQueue(m,*bgfrom,bf)||!helper.helper.ReadQueue(m,*bgto,bt))return;
        for(auto& bucket:from)for(U32 p:bucket)if(LayoutNamed(m,p,"LapTimeSong")){if(panel){panel=0;return;}panel=p;}
        if(!panel)return;
        for(auto& bucket:bf)for(U32 p:bucket)if(LayoutNamed(m,p,"CommonBottomBg")){if(background){panel=0;return;}background=p;}
        for(unsigned i=0;i<Count;++i){frame[i]={poses[i].x.load(),poses[i].y.load(),poses[i].scale.load()};auto p=frame[i];
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.scale)||p.scale<.1f||p.scale>3){panel=0;return;}}
        std::vector<Write> preview;
        if(!Prepare(m,preview)||to[15].size()>=32||(background&&bt[0].size()>=32)){panel=0;return;}
        for(auto& bucket:to)if(std::find(bucket.begin(),bucket.end(),panel)!=bucket.end()){panel=0;return;}
        to[15].push_back(panel);for(auto& bucket:from)std::erase(bucket,panel);
        if(background){for(auto& bucket:bf)std::erase(bucket,background);bt[0].push_back(background);}
        const std::array queues{std::pair{*bottom,&from},std::pair{*top,&to},std::pair{*bgfrom,&bf},std::pair{*bgto,&bt}};
        for(auto [q,buckets]:queues)for(unsigned p=0;p<16;++p)for(unsigned i=0;i<(*buckets)[p].size();++i)
            if(!m.Read(q+4+p*0x80+i*4)){panel=0;return;}
        for(auto [q,buckets]:queues){unsigned total=0;for(unsigned p=0;p<16;++p){auto& bucket=(*buckets)[p];
            for(unsigned i=0;i<bucket.size();++i)m.Write(q+4+p*0x80+i*4,bucket[i]);
            m.Write(q+0x804+p*4,bucket.size());total+=bucket.size();}m.Write(q+0x844,total);}
    }
    template<class M> void Begin(M& m,U32 layout){
        frames.emplace_back();if(!panel)return;std::vector<Write> w;
        if(layout==panel&&pass==3){if(!Prepare(m,w))return;}
        else if(layout==background&&pass==1){auto p=frame[Background];std::vector<U32> seen;
            if(!helper.helper.Transform(m,layout+8,w,seen,{p.scale,0,0,p.scale,p.x,p.y}))return;}
        else return;
        for(auto write:w)m.Write(write.address,write.after);frames.back()=std::move(w);
    }
    template<class M> void End(M& m){if(frames.empty())return;
        for(auto i=frames.back().rbegin();i!=frames.back().rend();++i)m.Write(i->address,i->before);frames.pop_back();}
};
}
