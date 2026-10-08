// Ported from mm3d-dekui src/mm3d/options_menu.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "song_list.h"
namespace Mm3dDekui::OptionsMenu {
using namespace NativeInventory;
// USA v110: Options draw and the presentation-only page switch. Selection is
// already a native 0..7 list; omit calibration (3), retaining native edits/help/B.
constexpr U32 DrawPC=0x005a3330,DrawWord=0xe1a04000,DrawSVC=0xffff99; // ref 0x5a31c0
constexpr U32 PagePC=0x005a1d84; // ref 0x5a1c14
constexpr U32 SelectPC=0x005a1fa0,SelectSVC=0xffff9a; // ref 0x5a1e30
constexpr U32 CalibrationPC=0x005a2e5c,CalibrationWord=0x1a000014; // ref 0x5a2cec
inline U32 Instruction(U32 pc,U32 word) {
    if(pc==DrawPC&&word==DrawWord)return 0xef000000|DrawSVC;
    if(pc==SelectPC&&word==0xe1a04000)return 0xef000000|SelectSVC;
    // Always skip the entire activation block, including its sound, service
    // initialization and modal state change. Also protects old save states.
    if(pc==CalibrationPC&&word==CalibrationWord)return 0xea000014;
    if(pc==PagePC&&word==0xe92d4010)return 0xe3a00000; // return false: no page transition
    if(pc==PagePC+4&&word==0xe1a04000)return 0xe12fff1e;
    return word;
}
template<class M> bool CodeMatches(M& m) {
    constexpr std::array<std::pair<U32,U32>,17> guards{{
        {DrawPC-4,0xe92d4030},{DrawPC,DrawWord},{DrawPC+4,0xe5910004},
        {DrawPC+8,0xe24dd014},{DrawPC+12,0xe590016c},
        {SelectPC-4,0xe92d47f0},{SelectPC,0xe1a04000},{SelectPC+4,0xe1a05002},{SelectPC+8,0xe1a07003},
        {CalibrationPC,CalibrationWord},{CalibrationPC-4,0x03500001},{CalibrationPC+4,0xe3160001},{0x005a2eb4,0xe5942144}, // ref 0x5a2d44
        {PagePC,0xe92d4010},{PagePC+4,0xe1a04000},{PagePC+8,0xed2d8b02},{PagePC+12,0xe24dd008}
    }};
    for(auto [at,word]:guards)if(m.Read(at)!=word)return false;
    return true;
}
inline constexpr std::array<const char*,8> Rows{
    "b_plate_attention_l","b_plate_ex_slidepad_l","b_plate_freecam_l","extra_btn_g",
    "b_plate_subjective_cam_l","b_plate_motion_l","b_plate_swim_l","b_plate_volume_l"};
struct Options {float x=154.f,y=30.f,scale=.3f,spacing=48.f;GearMenu::Pose description;};
inline std::atomic<float> x{154.f},y{30.f},scale{.3f},spacing{48.f};
inline AtomicButtonPose description;
inline void Apply(const Options& o){x=o.x;y=o.y;scale=o.scale;spacing=o.spacing;
    description.x=o.description.x;description.y=o.description.y;description.scale=o.description.scale;}
inline std::array<float,2> Position(unsigned i,const Options& o){return {o.x,o.y+(i>3?i-1:i)*o.spacing*o.scale};}
template<class M> U32 Selection(M& m,U32 screen,U32 requested) {
    if(requested!=3||!Pointer(screen,0x1b8))return requested;
    auto layout=m.Read(screen+0x10),state=m.Read(screen+8);
    if(!layout||!state||!LayoutNamed(m,*layout,"MenuOption"))return requested;
    const unsigned current=(*state>>16)&255;
    return current>3&&current<8?2:4;
}
struct Hook {
    SongList::Hook helper;
    Options frame;
    U32 owner=0,panel=0,cursor=0,background=0,pass=~0u;
    using Write=NativeInventory::Hook::Write;
    std::vector<std::vector<Write>> frames;
    template<class M> void Capture(M& m,U32 screen) {
        owner=0;
        auto layout=m.Read(screen+0x10);
        if(Pointer(screen,0x1b8)&&layout&&LayoutNamed(m,*layout,"MenuOption"))owner=screen;
    }
    template<class M> bool Place(M& m,U32 node,unsigned slot,std::vector<Write>& writes) {
        auto x=m.Read(node+0xec),y=m.Read(node+0xfc);if(!x||!y)return false;
        const float px=std::bit_cast<float>(*x),py=std::bit_cast<float>(*y);
        if(!std::isfinite(px)||!std::isfinite(py))return false;
        const auto target=Position(slot,frame);std::vector<U32> seen;
        return helper.helper.Transform(m,node,writes,seen,{frame.scale,0,0,frame.scale,target[0]-px*frame.scale,target[1]-py*frame.scale});
    }
    template<class M> bool Prepare(M& m,std::vector<Write>& writes) {
        const auto all=helper.Child(m,panel+8,"all");if(!all)return false;
        for(const char* name:{"btn_return_l","btn_page1_g","btn_page2_g"}) {
            const auto node=helper.Child(m,all,name);if(!node||!helper.Hide(m,node,writes))return false;
        }
        for(unsigned page=0;page<2;++page) {
            const auto group=helper.Child(m,all,page?"page2_g":"page1_g");if(!group)return false;
            for(unsigned i=page*4;i<page*4+4;++i) {
                auto node=helper.Child(m,group,Rows[i]);
                if(i==2)node=helper.Child(m,helper.Child(m,group,"grayout_freecam_g"),Rows[i]);
                if(!node)return false;
                if(i==3){if(!helper.Hide(m,node,writes))return false;}
                else if(!Place(m,node,i,writes))return false;
            }
        }
        return true;
    }
    template<class M> void Pass(M& m,U32 manager,U32 screen) {
        pass=screen;if(screen!=4)return;
        panel=cursor=background=0;
        if(!owner)return;
        frame={x.load(),y.load(),scale.load(),spacing.load(),{description.x.load(),description.y.load(),description.scale.load()}};
        for(float v:{frame.x,frame.y,frame.scale,frame.spacing})if(!std::isfinite(v))return;
        if(frame.scale<.1f||frame.scale>2.f||frame.spacing<0||frame.spacing>120)return;
        auto p=m.Read(owner+0x10),c=m.Read(owner+0x18),b=m.Read(owner+0x14);
        if(!p||!c||!b||!LayoutNamed(m,*p,"MenuOption")||!LayoutNamed(m,*c,"CursorOption")||
           !LayoutNamed(m,*b,"CommonBottomBg"))return;
        auto bottom=m.Read(manager+28),top=m.Read(manager+20),bg=m.Read(manager+24);
        NativeInventory::Hook::Buckets from,to,back;
        if(!bottom||!top||!bg||!helper.helper.ReadQueue(m,*bottom,from)||
           !helper.helper.ReadQueue(m,*top,to)||!helper.helper.ReadQueue(m,*bg,back))return;
        unsigned count=0;bool draw_cursor=false;
        for(auto& bucket:from)for(auto layout:bucket){count+=layout==*p;draw_cursor|=layout==*c;}
        if(count!=1||to[15].size()+1+unsigned(draw_cursor)>32)return;
        for(auto& bucket:to)for(auto layout:bucket)if(layout==*p||layout==*c)return;
        panel=*p;cursor=*c;background=*b;
        std::vector<Write> probe;
        if(!Prepare(m,probe)){panel=cursor=background=0;return;}
        for(auto& bucket:from){std::erase(bucket,panel);std::erase(bucket,cursor);}
        for(auto& bucket:back)std::erase(bucket,background);
        to[15].push_back(panel);if(draw_cursor)to[15].push_back(cursor);
        for(auto [queue,buckets]:{std::pair{*bottom,&from},std::pair{*top,&to},std::pair{*bg,&back}})
            for(unsigned priority=0;priority<16;++priority)for(unsigned i=0;i<(*buckets)[priority].size();++i)
                if(!m.Read(queue+4+priority*0x80+i*4)){panel=cursor=background=0;return;}
        for(auto [queue,buckets]:{std::pair{*bottom,&from},std::pair{*top,&to},std::pair{*bg,&back}}) {
            unsigned total=0;
            for(unsigned priority=0;priority<16;++priority){const auto& bucket=(*buckets)[priority];
                for(unsigned i=0;i<bucket.size();++i)m.Write(queue+4+priority*0x80+i*4,bucket[i]);
                m.Write(queue+0x804+priority*4,bucket.size());total+=bucket.size();}
            m.Write(queue+0x844,total);
        }
    }
    template<class M> bool IsDescription(M& m,U32 layout) const {
        if(!panel||!owner||m.Read(owner+0x10)!=panel||!LayoutNamed(m,panel,"MenuOption")||
           !LayoutNamed(m,layout,"MessageWindow"))return false;
        auto state=m.Read(owner+8),id=m.Read(layout+0x3f4);
        // Options help-message tables (native 620db8..620e08). Exclude modal
        // calibration/controller dialogs and every other screen's messages.
        return state&&((*state>>8)&255)==0&&id&&*id>=0x5745&&*id<=0x5758;
    }
    template<class M> void Begin(M& m,U32 layout) {
        frames.emplace_back();if(!panel||pass!=3)return;
        std::vector<Write> writes;
        if(layout==panel){if(!Prepare(m,writes))return;}
        else if(layout==cursor) {
            auto state=m.Read(owner+8);if(!state)return;const unsigned selected=(*state>>16)&255;
            if(selected>=Rows.size())return;
            if(selected==3){if(!helper.Hide(m,cursor+8,writes))return;}
            else if(!Place(m,cursor+8,selected,writes))return;
        }else return;
        for(auto w:writes)m.Write(w.address,w.after);frames.back()=std::move(writes);
    }
    template<class M> void End(M& m) {
        if(frames.empty())return;
        for(auto i=frames.back().rbegin();i!=frames.back().rend();++i)m.Write(i->address,i->before);
        frames.pop_back();
    }
};
}
