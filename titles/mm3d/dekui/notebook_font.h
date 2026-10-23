// Ported from mm3d-dekui src/mm3d/notebook_font.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"
namespace Mm3dDekui::NotebookFont {
using namespace NativeInventory;
// Native font lookup0x610B90 and glyph emission record builder0x5FDCCC.
struct Glyph { std::array<U32,15> words{}; unsigned advance=0,bearing=0; };
template<class M> std::optional<U32> Half(M& m,U32 at) {
    auto v=m.Read(at&~3u);if(!v)return {};return (*v>>((at&2)*8))&65535;
}
template<class M> std::optional<Glyph> Lookup(M& m,U32 font,U32 character,float pen) {
    auto header=m.Read(font+4),pages=m.Read(font+0xc),chars=m.Read(font+0x10);
    if(!header||!pages||!chars||!Pointer(*header,0x2c))return {};
    auto page_stride=Half(m,*header+10),stride=Half(m,*header+12),page_count=m.Read(*header+0x10),count=m.Read(*header+0x14);
    auto flags=Half(m,*header+0x1c),cw=Half(m,*header+0x26),ch=Half(m,*header+0x28);
    if(!page_stride||!stride||!page_count||!count||!flags||!cw||!ch||*page_stride<8||*page_stride>64||
       *stride<12||*stride>64||!*count||*count>65536||!*cw||!*ch||*cw>128||*ch>128)return {};
    const unsigned format=(*flags&0x1ff)>>1;if(format>3)return {};
    unsigned lo=0,hi=*count;U32 record=0;
    while(lo<hi) {
        unsigned mid=lo+(hi-lo)/2;
        auto code=m.Read(*chars+mid**stride);if(!code)return {};
        if(*code==character){record=*chars+mid**stride;break;}
        if(*code<character)lo=mid+1;else hi=mid;
    }
    if(!record)return {}; // Do not silently substitute an unrelated glyph.
    auto advance=Half(m,record+4),page=Half(m,record+6),bearing=Half(m,record+8),cell=Half(m,record+10);
    if(!advance||!page||!bearing||!cell||*page>=*page_count||*advance>128||*bearing>128)return {};
    const U32 sheet=*pages+*page**page_stride;
    auto offset=m.Read(sheet),w=Half(m,sheet+4),h=Half(m,sheet+6);
    if(!offset||!w||!h||!*w||!*h||*w>4096||*h>4096||(*cell&255)**cw+*cw>*w||(*cell>>8)**ch+*ch>*h)return {};
    Glyph g;g.advance=*advance;g.bearing=*bearing;
    g.words[0]=*header+*offset;if(!Pointer(g.words[0]))return {};
    g.words[1]=format==3?0x6758:0x6756;
    g.words[2]=format<2?0x6761:format==2?0x1401:0x6760;
    g.words[3]=*w;g.words[4]=*h;
    const auto f=[&](unsigned i,float v){g.words[i]=std::bit_cast<U32>(v);};
    f(5,pen-*bearing);f(6,0);f(7,0);f(8,*cw);f(9,*ch);
    f(10,float((*cell&255)**cw)/ *w);f(11,float((*cell>>8)**ch)/ *h);
    f(12,float(*cw)/ *w);f(13,float(*ch)/ *h);g.words[14]=0xffffffff;
    return g;
}
}
