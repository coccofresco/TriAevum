// Ported from mm3d-dekui src/mm3d/schedule_detail.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"
namespace Mm3dDekui::ScheduleDetail {
using namespace NativeInventory;
using Write=NativeInventory::Hook::Write;
// Clip axis-aligned native textured panes through their world and UV matrices.
// Geometry is supplied by Pane::BuildMatrix (original 0x15AD0C/0x15AE18).
// No shared pane resource is edited, including panes crossing midnight.
template<class M> bool Clip(M& m,U32 node,float left,float right,std::vector<Write>& writes) {
    auto geometry=m.Read(node+0x28);if(!geometry)return false;
    if(!*geometry)return true;
    auto type=m.Read(*geometry+4);if(!type)return false;
    if((*type&65535)<4||(*type&65535)>7)return (*type&65535)<3;
    const auto f=[&](U32 at)->std::optional<float>{auto b=m.Read(at);if(!b)return {};float v=std::bit_cast<float>(*b);if(!std::isfinite(v))return {};return v;};
    auto px=f(*geometry+0x14),width=f(*geometry+0x20),xx=f(node+0xe0),xy=f(node+0xe4),yx=f(node+0xf0),tx=f(node+0xec);
    if(!px||!width||!xx||!xy||!yx||!tx||*xx<=0||*width<=0||std::abs(*xy)>.0001f||std::abs(*yx)>.0001f)return false;
    const float x=*tx+*xx**px,w=*xx**width;
    const float lo=std::max(left,x),hi=std::min(right,x+w);
    NativeInventory::Hook helper;
    if(hi<=lo){auto flags=m.Read(node+0xd4);return flags&&helper.Stage(m,writes,node+0xd4,*flags&~1u);}
    if(lo==x&&hi==x+w)return true;
    const float start=(lo-x)/w,ratio=(hi-lo)/w;
    if(!helper.Stage(m,writes,node+0xe0,std::bit_cast<U32>(*xx*ratio))||
       !helper.Stage(m,writes,node+0xec,std::bit_cast<U32>(lo-*xx*ratio**px)))return false;
    auto u=f(*geometry+0x28),uw=f(*geometry+0x30);if(!u||!uw)return false;
    for(U32 row:{0x110u,0x11cu}) {
        auto basis=f(node+row),offset=f(node+row+8);if(!basis||!offset)return false;
        if(!helper.Stage(m,writes,node+row,std::bit_cast<U32>(*basis*ratio))||
           !helper.Stage(m,writes,node+row+8,std::bit_cast<U32>(*offset+*basis*(*u*(1-ratio)+start**uw))))return false;
    }
    return true;
}
template<class M> bool Prepare(M& m,U32 panel,U32 owner,NativeInventory::Hook& helper,std::vector<Write>& out) {
    if(!owner)return true;
    auto expected=m.Read(owner+0x44),selection=m.Read(owner+0x10);
    if(!expected||*expected!=panel||!selection)return true;
    const unsigned column=*selection&255;
    if(column>5)return false;
    auto attachment=m.Read(panel+0x23c);
    if(!attachment||!Named(m,*attachment,"sche_base_anim_null"))return false;
    // Parent is discovered from the guarded MenuSchedule tree, not assumed
    // from native parent-pointer internals.
    auto root_children=m.Read(panel+8+0x14);if(!root_children)return false;
    auto all=m.Read(*root_children);if(!all||!Named(m,*all,"all"))return false;
    auto children=m.Read(*all+0x14),count=m.Read(*all+0x18);if(!children||!count||*count!=15)return false;
    U32 strip=0,marker=0;
    std::vector<U32> fairies;
    for(unsigned i=0;i<*count;++i){auto node=m.Read(*children+4*i);if(!node)return false;
        if(Named(m,*node,"sche_base_anim"))strip=*node;
        if(Named(m,*node,"current_time01"))marker=*node;
        if(Named(m,*node,"elf_seal_free")||Named(m,*node,"elf_seal_fix"))fairies.push_back(*node);
    }
    if(!strip)return false;
    if(column==0) {
        // Native portrait mode parks the timeline beyond the 320px screen.
        // Relocation exposes its edge on the wider top screen: hide explicitly.
        auto hidden=fairies;hidden.push_back(strip);hidden.push_back(marker);
        for(auto node:hidden)if(node) {
            auto flags=m.Read(node+0xd4);
            if(!flags||!helper.Stage(m,out,node+0xd4,*flags&~1u))return false;
        }
        return true;
    }
    auto x=m.Read(strip+0xec),scale=m.Read(strip+0xe0),origin=m.Read(*all+0xec);
    if(!x||!scale||!origin)return false;
    const float sx=std::bit_cast<float>(*scale),base=std::bit_cast<float>(*origin);
    if(!std::isfinite(sx)||sx<=0||!std::isfinite(base))return false;
    const unsigned day=(column-1)/2;
    const float dx=base-std::bit_cast<float>(*x)-292.f*day*sx;
    std::vector<U32> nodes;std::vector<Write> transform;
    if(!helper.Transform(m,strip,transform,nodes,{1,0,0,1,dx,0}))return false;
    // The red time marker is a sibling of the timeline, not its descendant.
    // Give it the same day normalization and clip it to the selected day.
    if(marker&&!helper.Transform(m,marker,transform,nodes,{1,0,0,1,dx,0}))return false;
    // Fairy seals are sibling embedded layouts, so the strip traversal does
    // not reach them. Keep their anchor in the same selected-day coordinates.
    std::vector<U32> fairy_nodes;
    for(auto fairy:fairies)
        if(!helper.Transform(m,fairy,transform,fairy_nodes,{1,0,0,1,dx,0}))return false;
    // Apply temporarily to calculate clipping in the normalized coordinate space.
    for(auto w:transform)m.Write(w.address,w.after);
    std::vector<Write> clip;bool valid=true;
    for(auto fairy:fairies) {
        // The named node is an attachment at X=0. Tatl's actual position is
        // on its embedded layout root; testing the attachment hides every day.
        auto embedded=m.Read(fairy+0x20);
        if(!embedded||(*embedded&&!Pointer(*embedded,0x148))){valid=false;break;}
        const U32 anchor_node=*embedded?*embedded+8:fairy;
        auto x=m.Read(anchor_node+0xec),flags=m.Read(fairy+0xd4);
        if(!x||!flags||!std::isfinite(std::bit_cast<float>(*x))){valid=false;break;}
        const float anchor=std::bit_cast<float>(*x);
        if(anchor<base+15.f*sx||anchor>=base+305.f*sx)
            if(!helper.Stage(m,clip,fairy+0xd4,*flags&~1u)){valid=false;break;}
    }
    for(auto node:nodes)if(!Clip(m,node,base+15.f*sx,base+305.f*sx,clip)){valid=false;break;}
    for(auto i=transform.rbegin();i!=transform.rend();++i)m.Write(i->address,i->before);
    if(!valid)return false;
    out=std::move(transform);out.insert(out.end(),clip.begin(),clip.end());return true;
}
}
