// Ported from mm3d-dekui src/mm3d/menu_sound.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "menu_page_switch.h"

namespace Mm3dDekui::MenuSound {
using namespace NativeInventory;
constexpr U32 PC=0x001391f4,Word=0x95c40000,SVC=0xffff98,Request=0x6a0dcc; // ref 0x13921c
// Native duplicate-effect callback: suppress requests while an identical voice
// is younger than the configured frame limit. Preserve its LS condition.
constexpr std::array<U32,24> Code{
    0xe92d4038,0xe1a04000,0xe1a01000,0xe1a0000d,0xeb010d37,0xe5940000, // ref 0xeb010d84
    0xe59f403c,0xe3500000,0x159000ac,0xe5941004,0x03e00000,0xe1500001,
    0x1a000006,0xe59d0000,0xe3500000,0x1b017e5c,0xe5941008,0xe1500001, // ref 0x1b017ea9
    0x93a00000,Word,0xe1a0000d,0xeb010d19,0xe8bd8038,Request}; // ref 0xeb010d66
inline U32 Instruction(U32 pc,U32 word){return pc==PC&&word==Word?0x9f000000|SVC:word;}
template<class M> bool CodeMatches(M& m) {
    for(unsigned i=0;i<Code.size();++i)if(m.Read(0x001391a8+4*i)!=Code[i])return false; // ref 0x1391d0
    return MenuPageSwitch::CodeMatches(m);
}
template<class M> bool MenuPage(M& m,U32 page) {
    auto v=m.Read(page);if(!v)return false;
    return *v==MenuPageSwitch::ItemsVtable||*v==MenuPageSwitch::MasksVtable||
        *v==MenuPageSwitch::GearVtable||*v==MenuPageSwitch::MapVtable;
}
template<class M> bool AllowRepeat(M& m,U32 ctx,U32 request) {
    if(request!=Request||m.Read(request+4)!=0x0100076cu||!Pointer(ctx,0x14))return false;
    auto current=m.Read(ctx+8),next=m.Read(ctx+12);
    // Only handoffs within these menu pages, or their settled open state.
    return current&&MenuPage(m,*current)&&next&&(!*next||MenuPage(m,*next));
}
template<class M> void Store(M& m,U32 ctx,U32 address,U32 value) {
    if(AllowRepeat(m,ctx,address))return;
    // Replay STRB, retaining the adjacent request fields.
    auto word=m.Read(address&~3u);if(!word)return;
    const unsigned shift=(address&3)*8;
    m.Write(address&~3u,(*word&~(255u<<shift))|((value&255)<<shift));
}
}
