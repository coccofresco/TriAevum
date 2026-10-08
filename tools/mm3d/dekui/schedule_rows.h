// Ported from mm3d-dekui src/mm3d/schedule_rows.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"
#include "notebook_scrollbar.h"

namespace Mm3dDekui::ScheduleRows {
using namespace NativeInventory;
constexpr int Visible=7;
constexpr U32 FirstSVC=0xffffd4;
struct Patch { U32 pc,word,replacement; };
// Verified against Restoration + HD HUD v110, not original-ROM addresses.
constexpr std::array<Patch,10> Patches{{
    {0x005abd6c,0xe1a04000,0xef000000|FirstSVC}, // ref 0x5abbfc
    {0x005abd88,0xe2416002,0xe2416001}, // begin = first - one buffer // ref 0x5abc18
    {0x005abd90,0xe2817006,0xe2817007}, // exclusive end = first + seven // ref 0x5abc20
    {0x005abdb8,0xe3580002,0xe3580001}, // slot one remains attached when stopped // ref 0x5abc48
    {0x005b2634,0xe3a02000,0xef000000|(FirstSVC+1)}, // ref 0x5b24c4
    {0x005b2788,0xe3a02000,0xef000000|(FirstSVC+1)}, // ref 0x5b2618
    {0x005b2768,0xe3520003,0xe3520006}, // scroll at last visible row // ref 0x5b25f8
    {0x005b2794,0xe2400002,0xe2400005}, // pending first = selected - five // ref 0x5b2624
    {0x005aad2c,0xe2800004,0xe2800007}, // alarm-mode down arrow // ref 0x5aabbc
    {0x005af3a0,0xe2800004,0xe2800007}, // ordinary down arrow // ref 0x5af230
}};
inline U32 Instruction(U32 pc,U32 word) {
    for(auto p:Patches)if(pc==p.pc&&word==p.word)return p.replacement;
    return word;
}
template<class M> bool CodeMatches(M& m) {
    for(auto p:Patches)if(m.Read(p.pc)!=std::optional<U32>(p.word))return false;
    return m.Read(0x005abd68)==std::optional<U32>(0xe92d5ff0)&& // ref 0x5abbf8
        m.Read(0x005abd70)==std::optional<U32>(0xe3a0b000)&& // ref 0x5abc00
        m.Read(0x005b2630)==std::optional<U32>(0xe8900003)&& // ref 0x5b24c0
        m.Read(0x005b2638)==std::optional<U32>(0xebef7e1b)&& // ref 0x5b24c8, 0xebef7ece
        m.Read(0x005b2784)==std::optional<U32>(0xe5941500)&& // ref 0x5b2614
        m.Read(0x005b278c)==std::optional<U32>(0xebef7dc6); // ref 0x5b261c, 0xebef7e79
}
inline int Window(int first,int selected,int total) {
    const int last=std::max(0,total-Visible);
    first=std::clamp(first,0,last);
    return std::clamp(first,std::max(0,selected-Visible+1),std::min(selected,last));
}
struct Hook {
    using Write=NativeInventory::Hook::Write;
    U32 owner=0,layout=0;
    int first=0,total=0,selected=0;
    template<class M> void Enter(M& m,U32 object) {
        owner=layout=0;
        if(!Pointer(object,0x628))return;
        auto up=m.Read(object+0x38),state=m.Read(object+0xc),selection=m.Read(object+0x10),pending=m.Read(object+0x14);
        if(!up||!state||!selection||!pending||!LayoutNamed(m,*up,"UpSchedule"))return;
        total=int(*state>>24);selected=int((*selection>>8)&255);
        if(total>28||(total&&selected>=total))return;
        const int next=static_cast<std::int8_t>(*pending&255);
        first=total?Window(next>=0?next:int((*state>>16)&255),selected,total):0;
        NativeInventory::Hook helper;std::vector<Write> writes;
        // Native local positions are established before world calculation. Keep
        // the first row at Y=59, now in slot 1 rather than slot 2. The character
        // timeline, portrait, New label and seal consequently share one origin.
        for(unsigned i=0;i<8;++i)for(U32 offset:{0x4d8u,0x550u,0x570u}) {
            auto p=m.Read(object+offset+4*i);if(!p||!Pointer(*p,0x140))return;
            const U32 node=*p+(offset==0x550?8:0); // TextNew is a layout
            auto flags=m.Read(node+0x84),dirty=m.Read(node+0x88);
            if(!flags||!dirty)return;
            const float y=19.f+40.f*i-(offset==0x550?1.f:0.f);
            if(!helper.Stage(m,writes,node+0x40,std::bit_cast<U32>(y))||
               !helper.Stage(m,writes,node+0x84,*flags|0x200u)||
               !helper.Stage(m,writes,node+0x88,*dirty&~0x200u))return;
        }
        // Commit the pending scroll before population; no deferred animation is
        // needed to finish this transition. Preserve unrelated packed bytes.
        helper.Stage(m,writes,object+0xc,(*state&0xff00ffffu)|(U32(first)<<16));
        helper.Stage(m,writes,object+0x14,(*pending&0xffffff00u)|255u);
        for(auto w:writes)m.Write(w.address,w.after);
        owner=object;layout=*up;
    }
    template<class M> bool Expand(M& m,U32 current,NativeInventory::Hook& helper,std::vector<Write>& writes) {
        if(!owner||!layout)return true;
        auto up=m.Read(owner+0x38);if(!up||*up!=layout||!LayoutNamed(m,layout,"UpSchedule"))return true;
        auto cursor=m.Read(owner+0x508);
        std::vector<U32> seen;
        if(cursor&&current==*cursor) {
            auto node=m.Read(owner+0x54c),selection=m.Read(owner+0x10);
            if(!node||!selection||!Named(m,*node,"all"))return false;
            const int row=int((*selection>>8)&255)-first;
            if(row<0||row>=Visible||first+row>=total)return false;
            auto anchor=m.Read(owner+0x4d8+4*(row+1));
            if(!anchor||!Pointer(*anchor,0x140))return false;
            auto row_y=m.Read(*anchor+0xfc),scale=m.Read(*anchor+0xf4),y=m.Read(*node+0xfc);
            if(!row_y||!scale||!y)return false;
            // Native scrolling bakes animation offsets into the cursor's base
            // transform (+90); local Y alone cannot recover its rendered origin.
            // Align world-to-world with the actual selected slot. CursorScheFrame
            // rootall supplies -6, and the character-column animation adds +3.
            const float inset=(*selection&255)==0?-3.f:-6.f;
            const float target=std::bit_cast<float>(*row_y)+inset*std::bit_cast<float>(*scale);
            const float dy=target-std::bit_cast<float>(*y);
            return helper.Transform(m,*node,writes,seen,{1,0,0,1,0,dy});
        }
        if(current!=layout)return true;
        auto by=m.Read(layout+8+0xfc),sy=m.Read(layout+8+0xf4);
        if(!by||!sy)return false;
        const float base=std::bit_cast<float>(*by),scale=std::bit_cast<float>(*sy);
        if(!std::isfinite(base)||!std::isfinite(scale)||scale<=0)return false;
        // UpSchedule out tracks move slots 0..6 from X=0 to +400, with
        // starts at frames 2..8 and two-frame travel. Slot 7 has no track.
        // Recover the native clock, then apply the same stagger to our seven
        // visible slots (1..7), directed left. Leave entrance/scroll clips alone.
        bool exiting=false;
        auto player=m.Read(owner+0x5c4);
        if(player&&Pointer(*player,0x18)) {
            auto animation=m.Read(*player+0xc);
            if(animation&&Pointer(*animation,0x18)) {
                auto name=m.Read(*animation+8);
                exiting=name&&DialogueScale::Name(m,*name,"out_up_schedule_anim");
            }
        }
        std::array<float,8> row_dx{};
        if(exiting) {
            auto root_x=m.Read(layout+8+0xec),root_sx=m.Read(layout+8+0xe0);
            if(!root_x||!root_sx)return false;
            const float origin_x=std::bit_cast<float>(*root_x),sx=std::bit_cast<float>(*root_sx);
            if(!std::isfinite(origin_x)||!std::isfinite(sx)||sx<=0)return false;
            float frame=2.f;
            std::array<float,8> native_x{};
            for(unsigned i=0;i<8;++i) {
                auto anchor=m.Read(owner+0x4d8+4*i);
                if(!anchor)return false;
                // Animation is baked into the world matrix, not local pos.x.
                // Local X can remain zero throughout the entire exit clip.
                auto x=m.Read(*anchor+0xec);if(!x)return false;
                native_x[i]=(std::bit_cast<float>(*x)-origin_x)/sx;
                if(!std::isfinite(native_x[i])||native_x[i]<-.01f||native_x[i]>400.01f)return false;
                if(i<7&&native_x[i]>0)
                    frame=std::max(frame,float(i+2)+2.f*std::min(native_x[i]/400.f,1.f));
            }
            for(unsigned i=1;i<8;++i) {
                const float target=-400.f*std::clamp((frame-float(i+1))/2.f,0.f,1.f);
                row_dx[i]=(target-native_x[i])*sx;
                for(U32 offset:{0x4d8u,0x570u}) {
                    auto node=m.Read(owner+offset+4*i);if(!node)return false;
                    if(!helper.Transform(m,*node,writes,seen,{1,0,0,1,row_dx[i],0}))return false;
                }
            }
        }
        // TextNew is submitted as a separate layout.  Its native Y position
        // can remain on the old schedule row after the character slot has
        // been repositioned, leaving a badge detached from its icon.  Match
        // each badge to the rendered icon's current world Y while preserving
        // the badge's intentional left-side X offset.
        for(unsigned i=0;i<8;++i) {
            auto icon=m.Read(owner+0x4d8u+4*i),badge=m.Read(owner+0x550u+4*i);
            if(!icon||!badge||!Pointer(*icon,0x140)||!Pointer(*badge+8,0x140))return false;
            auto icon_y=m.Read(*icon+0xfc),badge_y=m.Read(*badge+8+0xfc);
            if(!icon_y||!badge_y)return false;
            const float dy=std::bit_cast<float>(*icon_y)-std::bit_cast<float>(*badge_y);
            if(!std::isfinite(dy)||std::abs(dy)>256.f*scale)return false;
            if(!helper.Transform(m,*badge+8,writes,seen,{1,0,0,1,row_dx[i],dy}))return false;
        }
        // Native footer starts below four rows (Y=220); extend by three pitches.
        std::vector<U32> pending{layout+8},visited;
        while(!pending.empty()) {
            U32 node=pending.back();pending.pop_back();
            if(!Pointer(node,0x140)||visited.size()>256||std::find(visited.begin(),visited.end(),node)!=visited.end())return false;
            visited.push_back(node);
            // Initialization still uses the native four-row arrow condition;
            // normalize visibility here as well as in both update functions.
            const bool up_arrow=Named(m,node,"direct_up"),down_arrow=Named(m,node,"direct_down");
            if(up_arrow||down_arrow) {
                auto flags=m.Read(node+0xd4);if(!flags)return false;
                const bool visible=up_arrow?first>0:first+Visible<total;
                if(!helper.Stage(m,writes,node+0xd4,visible?*flags|1u:*flags&~1u))return false;
            }
            bool footer=Named(m,node,"display")||Named(m,node,"line_decoration")||Named(m,node,"direct_down")||Named(m,node,"bb_for_icon_pane");
            if(footer) {
                if(!helper.Transform(m,node,writes,seen,{1,0,0,1,0,120.f*scale}))return false;
                continue;
            }
            if(Named(m,node,"current_time_pane1")) {
                const float origin=base+61.f*scale,stretch=297.f/177.f;
                if(!helper.Transform(m,node,writes,seen,{1,0,0,stretch,0,origin*(1-stretch)}))return false;
                continue;
            }
            auto children=m.Read(node+0x14),count=m.Read(node+0x18);
            if(!children||!count||*count>64)return false;
            for(unsigned i=0;i<*count;++i){auto child=m.Read(*children+4*i);if(!child)return false;pending.push_back(*child);}
        }
        // Same ScrollBar resource as Events, but a separate owner/member and a
        // seven-character range. Stage all modifications for exact draw restoration.
        auto bar=m.Read(owner+0x5c0);if(!bar||!LayoutNamed(m,*bar,"ScrollBar"))return false;
        const float fraction=total>Visible?float(first)/float(total-Visible):0.f;
        const float coverage=float(std::min(Visible,total))/float(std::max(1,total));
        return NotebookScrollbar::Resize(m,*bar,helper,writes,base+356.f*scale,fraction,coverage);
    }
};
}
