// Ported from mm3d-dekui src/mm3d/inventory_navigation.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"
#include "native_item_present.h"
#include <cstdio>
namespace Mm3dDekui::InventoryNavigation {
using namespace NativeInventory;
constexpr U32 VerticalPC=0x005a0ff8,HorizontalPC=0x005a1608,FirstSVC=0xffffc0; // ref 0x5a0e88, 0x5a1498
inline U32 Instruction(U32 pc,U32 word) {
    if(pc==VerticalPC&&word==0xe5d41019)return 0xef000000|FirstSVC;
    if(pc==HorizontalPC&&word==0xe5d41018)return 0xef000000|(FirstSVC+1);
    if(pc==0x005a5830&&word==0xe182200c)return 0xefffffc2; // ref 0x5a56c0
    return word;
}
constexpr std::array<std::pair<U32,U32>,12> Guards{{
    {VerticalPC,0xe5d41019},{HorizontalPC,0xe5d41018},
    {0x005a0ff4,0xe12fff3c},{0x005a1604,0xe12fff3c}, // ref 0x5a0e84, 0x5a1494
    {0x005a1020,0xe5940000},{0x005a1630,0xe5940000}, // ref 0x5a0eb0, 0x5a14c0
    {0x005a0fc8,0xe92d41f0},{0x005a15d8,0xe92d41f0}, // ref 0x5a0e58, 0x5a1468
    {0x005a5830,0xe182200c},{0x005a58bc,0xe5c02195},{0x005a58d4,0x620364},{0x005a5850,0xe8bd8010} // ref 0x5a56c0, 0x5a574c, 0x5a5764, 0x5a56e0
}};
template<class M> bool CodeMatches(M& m) {
    for(auto [at,word]:Guards)if(m.Read(at)!=std::optional<U32>(word))return false;
    return true;
}
template<class M> bool PresentMove(M& m,U32 owner,U32 sp,bool vertical,U32 delta) {
    namespace P=NativeItemPresent;
    if(!P::enabled||!P::horizontal||!Pointer(owner,0xac)||!Pointer(sp,24)||(delta!=1&&delta!=0xffffffff))return false;
    auto caller=m.Read(sp+20),layout=m.Read(owner+0x28),state=m.Read(owner+0x18);
    if(!caller||!layout||!state||!LayoutNamed(m,*layout,"MenuItemPresen"))return false;
    if(vertical?(*caller!=0x005bca08&&*caller!=0x005bca38):(*caller!=0x005bca6c&&*caller!=0x005bca9c))return false; // ref 0x5bc898, 0x5bc8c8, 0x5bc8fc, 0x5bc92c
    unsigned c=*state&255,r=(*state>>8)&255,nc=c,nr=r;if(c>5||r>3)return false;
    const int step=delta==1?1:-1;
    if(c<5){if(vertical&&!(r==3&&step>0))return false;if(!vertical)nc=std::clamp(int(c)+step,0,4);}
    else if(!vertical)nr=std::clamp(int(r)+step,0,3);
    if(vertical&&((c<5&&step>0)||(c==5&&step<0))) {
        const float gx=P::poses[P::Panel].x.load(),gs=P::poses[P::Panel].scale.load(),bx=P::poses[P::Right].x.load(),bs=P::poses[P::Right].scale.load();
        if(!std::isfinite(gx)||!std::isfinite(gs)||!std::isfinite(bx)||!std::isfinite(bs)||gs<.1f||gs>2||bs<.1f||bs>2)return false;
        std::array<float,5> grid{};std::array<float,4> bar{};
        for(unsigned i=0;i<5;++i){auto icon=m.Read(owner+0x5c+(15+i)*4);if(!icon||!LayoutNamed(m,*icon,"ItemIconButton"))return false;auto px=m.Read(*icon+0x44);if(!px)return false;grid[i]=gx+gs*std::bit_cast<float>(*px);if(!std::isfinite(grid[i]))return false;}
        for(unsigned i=0;i<4;++i)bar[i]=bx+bs*(29+47*i);
        const auto nearest=[](const auto& a,float x){unsigned best=0;for(unsigned i=1;i<a.size();++i)if(std::abs(a[i]-x)<std::abs(a[best]-x))best=i;return best;};
        if(c<5){nc=5;nr=nearest(bar,grid[c]);}else{nc=nearest(grid,bar[r]);nr=3;}
    }
    m.Write(owner+0x18,(*state&0xffff0000u)|nc|(nr<<8));return true;
}
// Return a native Gear slot, or no override for unchanged navigation.
template<class M> std::optional<U32> GearMove(M& m,U32 owner,U32 buttons) {
    if(!GearMenu::enabled||!GearMenu::horizontal||!Pointer(owner,0x198))return {};
    auto layout=m.Read(owner+0x14),state=m.Read(owner+0x194);
    if(!layout||!state||!LayoutNamed(m,*layout,"MenuCollect"))return {};
    unsigned slot=(*state>>8)&255;if(slot>20)return {};
    const unsigned dir=buttons&0x40000040?0:buttons&0x80000080?1:buttons&0x20000020?2:buttons&0x10000010?3:4;
    if(dir==4)return {};
    const auto panel=GearMenu::Get(GearMenu::Panel),bar=GearMenu::Get(GearMenu::Right);
    if(!std::isfinite(panel.x)||!std::isfinite(panel.y)||!std::isfinite(panel.scale)||!std::isfinite(bar.x)||!std::isfinite(bar.y)||!std::isfinite(bar.scale)||panel.scale<=0||bar.scale<=0)return {};
    std::array<float,21> xs{},ys{};float bottom=-10000,top=10000;
    for(unsigned i=4;i<=20;++i){auto x=m.Read(owner+0x34+i*12),y=m.Read(owner+0x38+i*12);if(!x||!y)return {};xs[i]=panel.x+panel.scale*std::bit_cast<float>(*x);ys[i]=std::bit_cast<float>(*y);if(!std::isfinite(xs[i])||!std::isfinite(ys[i]))return {};bottom=std::max(bottom,ys[i]);top=std::min(top,ys[i]);}
    // Match the editor's rendered strip center (Y + 28 * scale).
    const bool above=bar.y+28*bar.scale<panel.y+(top+bottom)*.5f*panel.scale;
    const unsigned enter=above?0u:1u,leave=above?1u:0u;
    const float edge=above?top:bottom;
    if(slot<4&&dir!=leave)return dir==enter?slot:dir==2?(slot?slot-1:0):std::min(slot+1,3u);
    if(slot<4) {
        float origin=260+bar.x+bar.scale*(29+47*slot),distance=1e30f;unsigned best=4;
        for(unsigned i=4;i<=20;++i)if(std::abs(ys[i]-edge)<1.f&&std::abs(xs[i]-origin)<distance){distance=std::abs(xs[i]-origin);best=i;}
        return best;
    }
    if(dir==enter&&std::abs(ys[slot]-edge)<1.f){unsigned best=0;float distance=1e30f;for(unsigned i=0;i<4;++i){float d=std::abs(260+bar.x+bar.scale*(29+47*i)-xs[slot]);if(d<distance){best=i;distance=d;}}return best;}
    auto word=m.Read((0x620364+slot*4+dir)&~3u);if(!word)return {};
    unsigned next=(*word>>(dir*8))&255;
    if(next<4)return slot; // remove old side exits into the relocated strip
    return {};
}
// Run between the native deselect and select callbacks. Only replace the
// coordinate update; native highlights, description, equip logic and repeat stay intact.
template<class M> bool Move(M& m,U32 owner,U32 sp,bool vertical,U32 delta) {
    if(!enabled.load()||!bar_enabled.load()||!Pointer(owner,0x120)||!Pointer(sp,24)||
       (delta!=1&&delta!=0xffffffff))return false;
    auto caller=m.Read(sp+20),layout=m.Read(owner+0xc0),state=m.Read(owner+0x18),limit=m.Read(owner+0xa0);
    if(!caller||!layout||!state||!limit)return false;
    const bool masks=LayoutNamed(m,*layout,"MenuMask");
    if(!masks&&!LayoutNamed(m,*layout,"MenuItem"))return false;
    const unsigned columns=masks?6u:5u;
    if(vertical?(*caller!=0x005c15a4&&*caller!=0x005c15d4):(*caller!=0x005c160c&&*caller!=0x005c1644))return false; // ref 0x5c1434, 0x5c1464, 0x5c149c, 0x5c14d4
    const unsigned column=*state&255,row=(*state>>8)&255,max_column=*limit>>24;
    if(max_column!=columns||column>columns||row>=4)return false;
    unsigned next_column=column,next_row=row;
    const int step=delta==1?1:-1;
    if(column<columns) {
        if(vertical&&!(row==0&&step<0))return false; // ordinary grid rows remain native
        if(!vertical){next_column=unsigned(std::clamp(int(column)+step,0,int(columns)-1));}
    } else if(!vertical)next_row=unsigned(std::clamp(int(row)+step,0,3));
    if(vertical&&((column<columns&&row==0&&step<0)||(column==columns&&step>0))) {
        float gx=x.load(),gs=scale.load();
        const float bx=bar_x.load(),bs=bar_scale.load(),gap=bar_spacing.load();
        if(!std::isfinite(gx)||!std::isfinite(gs)||!std::isfinite(bx)||!std::isfinite(bs)||!std::isfinite(gap)||gs<.5f||gs>1.5f||bs<.35f||bs>1.5f||gap<40||gap>120)return false;
        if(masks) {
            const float mx=masks_x.load(),ms=masks_scale.load();
            if(!std::isfinite(mx)||!std::isfinite(ms))return false;
            gx+=std::clamp(mx,-320.f,320.f);gs*=std::clamp(ms,.5f,1.5f);
        }
        std::vector<float> grid(columns);std::array<float,4> bar{};
        for(unsigned i=0;i<columns;++i) {
            auto icon=m.Read(owner+0xd0+i*4);
            if(!icon||!LayoutNamed(m,*icon,"ItemIconButton"))return false;
            auto pos=m.Read(*icon+0x44);if(!pos)return false;
            const float px=std::bit_cast<float>(*pos);if(!std::isfinite(px))return false;
            grid[i]=gx+gs*px;
        }
        for(unsigned i=0;i<4;++i)bar[i]=bx+bs*(32+i*gap);
        const auto nearest=[](const auto& targets,float origin) {
            unsigned best=0;for(unsigned i=1;i<targets.size();++i)
                if(std::abs(targets[i]-origin)<std::abs(targets[best]-origin))best=i;
            return best;
        };
        if(column<columns){next_column=columns;next_row=nearest(bar,grid[column]);}
        else {next_column=nearest(grid,bar[row]);next_row=0;}
    }
    // Up at the equipped row and left/right at edges stay put, without wrapping
    // into the old vertical sidebar. Retain alternate-selection bytes +1A/+1B.
    m.Write(owner+0x18,(*state&0xffff0000u)|next_column|(next_row<<8));
    return true;
}
}
