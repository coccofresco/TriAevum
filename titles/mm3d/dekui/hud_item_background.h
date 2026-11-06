// Ported from mm3d-dekui 4f2692ed src/mm3d/hud_item_background.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "double_time.h"
#include "hud_adjustments.h"

namespace Mm3dDekui::HudItemBackground {
using namespace NativeInventory;
struct Hook {
    DoubleTime::Hook helper;
    NotebookPrompt::Hook scratch;
    // Replace only the four item-slot background instances. ItemIconBase can
    // also be used by the real B button, so matching that layout alone is unsafe.
    template<class M> U32 Root(M& m,U32 layout,U32 content,U32 sp) {
        const bool nested=std::any_of(scratch.frames.begin(),scratch.frames.end(),
            [](const auto& f){return f.sp!=0;});
        scratch.frames.emplace_back();auto& frame=scratch.frames.back();
        if(nested||!HudAdjustments::mirror_item_buttons.load())return content;
        U32 root=0;
        if(LayoutNamed(m,layout,"HudItemIconSet"))root=layout+8;
        else if(LayoutNamed(m,layout,"Hud2")) {
            const auto all=scratch.Child(m,layout+8,"all");
            const auto group=scratch.Child(m,all,"item_button_g");
            const auto set=scratch.Child(m,group,"item_icon_set");
            if(set)root=helper.EmbeddedRoot(m,set);
        }
        if(!root)return content;
        const auto all=scratch.Child(m,root,"all");if(!all)return content;
        constexpr std::array<const char*,4> groups{
            "icon_item_x_g","icon_item_y_g","icon_item_1_g","icon_item_2_g"};
        constexpr std::array<const char*,4> bases{
            "icon_x_base","icon_y_base","icon_1_base","icon_2_base"};
        constexpr U32 Size=0x1200;
        if(sp<0x100000+Size*2||(sp&7))return content;
        const U32 base=sp-Size;
        for(U32 at=base-Size;at<sp;at+=4)if(!m.Read(at))return content;
        std::array<U32,Size/4> words{};
        std::vector<NativeInventory::Hook::Write> links;
        std::array<U32,4> owners{};
        const auto put=[&](U32 at,U32 v){words[at/4]=v;};
        const auto number=[&](U32 at,float v){put(at,std::bit_cast<U32>(v));};
        const auto copy=[&](U32 to,U32 from,U32 size){
            for(U32 i=0;i<size;i+=4){auto v=m.Read(from+i);if(!v)return false;put(to+i,*v);}return true;
        };
        if(!copy(0,content,0x140))return content;
        for(unsigned i=0;i<4;++i) {
            const auto group=scratch.Child(m,all,groups[i]);
            const auto instance=scratch.Child(m,group,bases[i]);
            const auto owner=helper.EmbeddedRoot(m,instance);
            const auto pane=scratch.Child(m,owner,"bb00_ybtn");
            if(!pane||m.Read(owner+0x18)!=std::optional<U32>(1))return content;
            if(std::find(owners.begin(),owners.end(),owner)!=owners.end())return content;
            owners[i]=owner;
            const auto geometry=m.Read(pane+0x28),children=m.Read(owner+0x14);
            if(!geometry||!children||!Pointer(*geometry,0x7c)||
               (m.Read(*geometry+4).value_or(0)&65535)!=6)return content;
            const auto read=[&](U32 offset)->std::optional<float>{
                auto v=m.Read(*geometry+offset);if(!v)return {};
                const float n=std::bit_cast<float>(*v);return std::isfinite(n)?std::optional<float>(n):std::nullopt;
            };
            const auto y=read(0x18),height=read(0x24),v=read(0x2c),dv=read(0x34);
            if(!y||!height||!v||!dv||*height<=0||*height>512||*dv==0||std::abs(*dv)>1)return content;
            const U32 block=0x180+i*0x400;
            for(unsigned half=0;half<2;++half) {
                const U32 node=block+0x10+half*0x200,geo=node+0x140;
                if(!copy(node,pane,0x140)||!copy(geo,*geometry,0x7c))return content;
                put(block+half*4,base+node);put(node+0x28,base+geo);
                put(node+0x14,0);put(node+0x18,0);put(node+0x20,0);put(node+0x24,0xffff);
                // Keep the original world matrix, tint, opacity and texture.
                // Top: bottom edge -> middle. Bottom: middle -> bottom edge.
                // Both halves meet exactly, with no stretch or changed bounds.
                number(geo+0x18,*y+half * *height*.5f);number(geo+0x24,*height*.5f);
                number(geo+0x2c,*v+*dv*(half?.5f:1.f));
                number(geo+0x34,*dv*(half?.5f:-.5f));
            }
            links.push_back({owner+0x14,*children,base+block});
            links.push_back({owner+0x18,1,2});
        }
        // Commit only after all four instances and the entire scratch area are
        // valid. Neither shared atlas data nor the native geometry is edited.
        for(U32 i=0;i<words.size();++i)frame.writes.push_back({base+i*4,*m.Read(base+i*4),words[i]});
        frame.writes.insert(frame.writes.end(),links.begin(),links.end());
        for(auto w:frame.writes)m.Write(w.address,w.after);
        frame.sp=sp;return base;
    }
};
}
