// Ported from mm3d-dekui src/mm3d/song_reset.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "song_list.h"

namespace Mm3dDekui::SongReset {
using namespace NativeInventory;
enum Part : unsigned { Items, Right, Bottom, Description, Count };
struct Options {
    bool enabled=true;float background_x=40;
    std::array<GearMenu::Pose,Count> poses;
    Options(){for(auto& p:poses)p={40,0,1};poses[Description]={0,0,1};}
};
inline std::atomic<bool> enabled{true};
inline std::atomic<float> background_x{40};
inline std::array<GearMenu::AtomicPose,Count> poses;
inline void Apply(const Options& o){
    enabled=o.enabled;background_x=o.background_x;
    for(unsigned i=0;i<Count;++i){poses[i].x=o.poses[i].x;poses[i].y=o.poses[i].y;poses[i].scale=o.poses[i].scale;}
}
struct Hook {
    SongList::Hook helper;
    using Write=NativeInventory::Hook::Write;
    bool routed=false;
    U32 panel=0,background=0,pass=~0u;unsigned bg_draw=0;
    float transition_x=0;
    std::array<GearMenu::Pose,Count> frame;
    std::vector<std::vector<Write>> frames;
    template<class M> bool Prepare(M& m,std::vector<Write>& w){
        auto all=helper.Child(m,panel+8,"all");if(!all)return false;
        const std::array<std::pair<const char*,Part>,13> groups{{
            {"item_set_pos_g",Items},{"item_panel_g",Items},{"right_base_g",Right},
            {"under_set_icon_g",Bottom},{"under_set_panel_g",Bottom},
            {"lost_item_base_bin00",Items},{"lost_item_base_bin01",Items},{"lost_item_base_bin02",Items},
            {"lost_item_base_bin03",Items},{"lost_item_base_bin04",Items},{"lost_item_base_bin05",Items},
            {"lost_item_base_bin06",Items},{"lost_item_base_bow",Items}}};
        U32 main_node=helper.Child(m,all,"item_set_pos_g");
        float main_native=0,main_native_y=0;
        if(auto a=m.Read(main_node+0xec),b=m.Read(main_node+0x3c),c=m.Read(main_node+0xfc),d=m.Read(main_node+0x40);a&&b&&c&&d){
            main_native=std::bit_cast<float>(*a)-std::bit_cast<float>(*b);
            main_native_y=std::bit_cast<float>(*c)-std::bit_cast<float>(*d);
        }
        for(auto [name,part]:groups){
            auto node=helper.Child(m,all,name);if(!node)return false;
            float native=0,native_y=0;
            if(auto a=m.Read(node+0xec),b=m.Read(node+0x3c),c=m.Read(node+0xfc),d=m.Read(node+0x40);a&&b&&c&&d){
                native=std::bit_cast<float>(*a)-std::bit_cast<float>(*b);
                native_y=std::bit_cast<float>(*c)-std::bit_cast<float>(*d);
            }
            auto p=frame[part];p.x+=transition_x+main_native-native;p.y+=main_native_y-native_y;std::vector<U32> seen;
            if(!helper.helper.Transform(m,node,w,seen,{p.scale,0,0,p.scale,p.x,p.y}))return false;
        }
        return true;
    }
    template<class M> void Pass(M& m,U32 manager,U32 screen){
        pass=screen;if(screen!=4)return;
        routed=false;panel=background=0;bg_draw=0;transition_x=0;if(!enabled)return;
        NativeInventory::Hook::Buckets from,to,bf,bt;
        auto bottom=m.Read(manager+28),top=m.Read(manager+20),bgfrom=m.Read(manager+24),bgto=m.Read(manager+12);
        if(!bottom||!top||!bgfrom||!bgto||*bottom==*top||*bgfrom==*bgto||
           !helper.helper.ReadQueue(m,*bottom,from)||!helper.helper.ReadQueue(m,*top,to)||
           !helper.helper.ReadQueue(m,*bgfrom,bf)||!helper.helper.ReadQueue(m,*bgto,bt))return;
        for(auto& bucket:from)for(U32 p:bucket)if(LayoutNamed(m,p,"ReturnToStart")){if(panel){panel=0;return;}panel=p;}
        if(!panel)return;
        // ReturnToStart owns explicit native in/out tracks. Read their live frame
        // and apply one shared horizontal offset to the reset groups.
        if(auto players=m.Read(panel+0x14c),count=m.Read(panel+0x150); players&&count&&*count<=64&&NativeInventory::Pointer(*players,*count*4)) {
            for(unsigned i=0;i<*count;++i) {
                auto player=m.Read(*players+i*4);if(!player)continue;
                auto anim=m.Read(*player+0xc),frame_at=m.Read(*player+0x10);if(!anim||!frame_at)continue;
                auto name=m.Read(*anim+8);if(!name)continue;
                const bool opening=DialogueScale::Name(m,*name,"in_return_menu_anim");
                const bool closing=DialogueScale::Name(m,*name,"out_return_menu_anim");
                if(!opening&&!closing)continue;
                const float f=std::bit_cast<float>(*frame_at);if(!std::isfinite(f)||f<0||f>8)continue;
                const float progress=std::clamp(f/4.f,0.f,1.f);
                const float distance=std::clamp(520.f-std::abs(background_x.load()),320.f,760.f);
                transition_x=(opening?-(1.f-progress):-progress)*distance;break;
            }
        }
        for(auto& bucket:bf)for(U32 p:bucket)if(LayoutNamed(m,p,"CommonBottomBg")){if(background){panel=0;return;}background=p;}
        for(unsigned i=0;i<Count;++i){frame[i]={poses[i].x.load(),poses[i].y.load(),poses[i].scale.load()};auto p=frame[i];
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.scale)||p.scale<.1f||p.scale>3){panel=0;return;}}
        helper.helper.frame_background={background_x.load(),background_pose.y.load(),background_pose.scale.load()};
        const auto bg=helper.helper.frame_background;
        if(!std::isfinite(bg.x)||!std::isfinite(bg.y)||!std::isfinite(bg.scale)||bg.scale<.25f||bg.scale>2){panel=0;return;}
        std::vector<Write> preview;
        if(!Prepare(m,preview)||to[15].size()>=32||(background&&bt[0].size()>30)){panel=0;return;}
        for(auto& bucket:to)if(std::find(bucket.begin(),bucket.end(),panel)!=bucket.end()){panel=0;return;}
        to[15].push_back(panel);for(auto& bucket:from)std::erase(bucket,panel);
        if(background){for(auto& bucket:bf)std::erase(bucket,background);helper.helper.ConfigureBrownShadow(m,background,32-bt[0].size()-2,shadow_width.load(),shadow_opacity.load());
            bt[0].insert(bt[0].begin(),2+helper.helper.brown_shadow_steps,background);}
        const std::array queues{std::pair{*bottom,&from},std::pair{*top,&to},std::pair{*bgfrom,&bf},std::pair{*bgto,&bt}};
        for(auto [q,buckets]:queues)for(unsigned p=0;p<16;++p)for(unsigned i=0;i<(*buckets)[p].size();++i)
            if(!m.Read(q+4+p*0x80+i*4)){panel=0;return;}
        for(auto [q,buckets]:queues){unsigned total=0;for(unsigned p=0;p<16;++p){auto& bucket=(*buckets)[p];
            for(unsigned i=0;i<bucket.size();++i)m.Write(q+4+p*0x80+i*4,bucket[i]);
            m.Write(q+0x804+p*4,bucket.size());total+=bucket.size();}m.Write(q+0x844,total);}
        routed=true;
    }
    template<class M> bool IsDescription(M& m,U32 layout) const {
        // Scope the message pose to frames where the reset menu was successfully routed.
        return enabled.load()&&routed&&panel&&LayoutNamed(m,panel,"ReturnToStart")&&
            LayoutNamed(m,layout,"MessageWindow");
    }
    template<class M> void Begin(M& m,U32 layout){
        frames.emplace_back();if(!panel)return;std::vector<Write> w;
        if(layout==panel&&pass==3){if(!Prepare(m,w))return;}
        else if(layout==background&&pass==1){
            if(bg_draw<helper.helper.brown_shadow_steps){
                const auto saved=helper.helper.frame_background;
                helper.helper.frame_background.x+=transition_x;
                if(!helper.helper.PrepareBrownShadow(m,layout,bg_draw++,w)){helper.helper.frame_background=saved;return;}
                helper.helper.frame_background=saved;
            }
            else {auto p=helper.helper.frame_background;p.x+=transition_x;bool upper=(bg_draw++-helper.helper.brown_shadow_steps)%2;std::vector<U32> seen;
                if(!helper.helper.Transform(m,layout+8,w,seen,{p.scale,0,0,p.scale,p.x,p.y-(upper?240*p.scale:0)}))return;}}
        else return;
        for(auto write:w)m.Write(write.address,write.after);frames.back()=std::move(w);
    }
    template<class M> void End(M& m){if(frames.empty())return;
        for(auto i=frames.back().rbegin();i!=frames.back().rend();++i)m.Write(i->address,i->before);frames.pop_back();}
};
}
