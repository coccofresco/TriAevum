// Ported from mm3d-dekui src/mm3d/notebook_scrollbar.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"

namespace Mm3dDekui::NotebookScrollbar {
using namespace NativeInventory;
template<class M> bool Resize(M& m,U32 layout,NativeInventory::Hook& helper,
    std::vector<NativeInventory::Hook::Write>& writes,float bottom,float fraction,float visible_fraction) {
        std::vector<U32> pending{layout+8},visited;
        U32 middle=0,lower=0,thumb_top=0,thumb_middle=0,thumb_bottom=0;
        while(!pending.empty()) {
            U32 node=pending.back();pending.pop_back();
            if(visited.size()>64||std::find(visited.begin(),visited.end(),node)!=visited.end())return false;
            visited.push_back(node);
            if(Named(m,node,"bb_pane01"))middle=node;
            if(Named(m,node,"bb_pane02"))lower=node;
            if(Named(m,node,"bar00"))thumb_top=node;
            if(Named(m,node,"bar01"))thumb_middle=node;
            if(Named(m,node,"bar02"))thumb_bottom=node;
            auto list=m.Read(node+0x14),n=m.Read(node+0x18);
            if(!list||!n||*n>32)return false;
            for(unsigned j=0;j<*n;++j){auto child=m.Read(*list+4*j);if(!child)return false;pending.push_back(*child);}
        }
        if(!middle||!lower||!thumb_top||!thumb_middle||!thumb_bottom)return false;
        auto root_y=m.Read(layout+8+0xfc),basis=m.Read(layout+8+0xf4);
        auto ty=m.Read(thumb_top+0xfc),my=m.Read(thumb_middle+0xfc),by=m.Read(thumb_bottom+0xfc);
        auto middle_basis=m.Read(thumb_middle+0xf4),bottom_basis=m.Read(thumb_bottom+0xf4);
        if(!root_y||!basis||!ty||!my||!by||!middle_basis||!bottom_basis)return false;
        const float scale=std::bit_cast<float>(*basis),base=std::bit_cast<float>(*root_y);
        const float top=base+44.f*scale,old_bottom=base+228.f*scale;
        const float new_bottom=bottom-8.f*scale;
        if(!std::isfinite(new_bottom)||scale<=0||new_bottom<=top)return false;
        std::vector<U32> seen;
        const float stretch=(new_bottom-top)/(184.f*scale);
        if(!helper.Transform(m,middle,writes,seen,{1,0,0,stretch,0,top*(1-stretch)}))return false;
        if(!helper.Transform(m,lower,writes,seen,{1,0,0,1,0,new_bottom-old_bottom}))return false;
        const float thumb_start=base+41.f*scale,available=new_bottom-thumb_start;
        const float old_top=std::bit_cast<float>(*ty),old_middle=std::bit_cast<float>(*my);
        const float old_bottom_y=std::bit_cast<float>(*by),cap=old_middle-old_top;
        const float middle_scale=std::bit_cast<float>(*middle_basis),bottom_scale=std::bit_cast<float>(*bottom_basis);
        const float caps=cap+2.f*bottom_scale;
        if(!std::isfinite(cap)||cap<0||!std::isfinite(old_bottom_y)||old_bottom_y<=old_middle||
           !std::isfinite(middle_scale)||middle_scale<=0||!std::isfinite(bottom_scale)||bottom_scale<=0||
           !std::isfinite(visible_fraction)||!std::isfinite(fraction)||available<=caps)return false;
        const float height=std::min(available,std::max(caps+scale,
                                                     available*std::clamp(visible_fraction,0.f,1.f)));
        const float target=thumb_start+std::clamp(fraction,0.f,1.f)*(available-height);
        // ScrollBar.mfl: bar01_pane is one unit high. bar02_pane is
        // two units high at local Y=-3, so its bottom is anchor Y-1.
        // Native animation scales the strip independently of the cap anchor.
        const float thumb_stretch=(height-caps)/middle_scale;
        return helper.Transform(m,thumb_top,writes,seen,{1,0,0,1,0,target-old_top})&&
            helper.Transform(m,thumb_middle,writes,seen,{1,0,0,thumb_stretch,0,target+cap-thumb_stretch*old_middle})&&
            helper.Transform(m,thumb_bottom,writes,seen,{1,0,0,1,0,target+height+bottom_scale-old_bottom_y});
    }
}
