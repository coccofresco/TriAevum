// Ported from mm3d-dekui src/mm3d/menu_memory.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "menu_page_switch.h"

namespace Mm3dDekui::MenuMemory {
using namespace MenuPageSwitch;
constexpr U32 FirstSVC=0xfffe80;
constexpr std::array<U32,3> PCs{0x0010fd2c,0x0010fd50,0x005a6310}; // ref 0x10fd58, 0x10fd7c, 0x5a61a0
constexpr std::array<U32,3> Words{0xe12fff32,0xe5c40010,0xe5c06186};
inline U32 Instruction(U32 pc,U32 word) {
    for(unsigned i=0;i<PCs.size();++i)if(pc==PCs[i]&&word==Words[i])return 0xef000000|FirstSVC+i;
    return word;
}
template<class M> bool CodeMatches(M& m) {
    if(!MenuPageSwitch::CodeMatches(m))return false;
    for(unsigned i=0;i<PCs.size();++i)if(m.Read(PCs[i])!=Words[i])return false;
    // Verified no-op PrepareOpen on Gear/Map, and native capture-ready test.
    for(auto [at,word]:std::array<std::pair<U32,U32>,13>{{
        {MapVtable+0x10,0x00599a50},{GearVtable+0x10,0x005a6f00}, // ref 0x5998e0, 0x5a6d90
        {0x005a6f00,0xe12fff1e},{0x005a630c,0xe5970100}, // ref 0x5a6d90, 0x5a619c
        {0x00599a50,0xe12fff1e}, // ref 0x5998e0
        {0x0010fd28,0xe1a01004},{0x0010fd4c,0xe12fff32}, // ref 0x10fd54, 0x10fd78
        {0x001bdd2c,0xe1d008b6},{0x001bdd30,0xe3500008},{0x001bdd40,0x66889c}, // ref 0x1bdf34, 0x1bdf38, 0x1bdf48
        {0x001bb144,0x66889c},{0x005a5b28,0x656b00},{0x005c2940,0x656b00}}}) // ref 0x1bb34c, 0x5a59b8, 0x5c27d0
        if(m.Read(at)!=word)return false;
    return true;
}
// Session-only page identity, never a saved guest pointer or an old screenshot.
// Recalled Gear/Map prepare the shared inventory capture before opening.
struct Hook {
    U32 owner=0,game=0,last=0,source=0,target=0;
    bool capture_started=false;
    template<class M> U32 Page(M& m,U32 p) const {
        for(auto [slot,vt]:std::array<std::pair<U32,U32>,4>{{
            {0x656b10,ItemsVtable},{0x656b18,MasksVtable},
            {0x656b0c,GearVtable},{0x656b14,MapVtable}}})
            if(Pointer(p,0x794)&&m.Read(slot)==p&&m.Read(p)==vt)return vt;
        return 0;
    }
    template<class M> bool Pending(M& m,U32 ctx) const {
        auto ui=m.Read(ctx+4);
        return target&&ctx==owner&&ui&&Pointer(*ui,0x170)&&m.Read(*ui+0x16c)==game&&
            Byte(m,game+0x13d)==4&&m.Read(ctx+8)==source&&m.Read(ctx+12)==target&&
            m.Read(0x656b04)==source&&Page(m,target)==last;
    }
    template<class M> void Tick(M& m,U32 ctx) {
        if(!NativeInventory::enabled.load()||!GearMenu::enabled.load()||!Pointer(ctx,20)) {*this={};return;}
        auto ui=m.Read(ctx+4);auto g=ui&&Pointer(*ui,0x170)?m.Read(*ui+0x16c):std::nullopt;
        if(!g||!Pointer(*g,0x11030)||Byte(m,*g+0x13d)!=4){*this={};return;}
        if(owner!=ctx||game!=*g){*this={};owner=ctx;game=*g;}
        if(target) {
            if(Pending(m,ctx))return;
            source=target=0;capture_started=false;
        }
        if(Byte(m,ctx+16)!=1||Byte(m,ctx+17)!=1)return;
        auto current=m.Read(ctx+8),next=m.Read(ctx+12);
        if(!current||!next)return;
        const U32 page=Page(m,*current);
        if(!*next&&page) {
            // Do not remember Soaring or an inventory assignment/drag state.
            if(page==MapVtable&&Byte(m,*current+0x78e)!=0)return;
            if((page==ItemsVtable||page==MasksVtable)&&Byte(m,*current+0x9f)!=2)return;
            if(page==GearVtable&&Byte(m,*current+0x194)!=2)return;
            last=page;return;
        }
        // Only the ordinary inventory opener from gameplay. Dedicated map,
        // notebook, item-giving and Soaring requests keep their own destination.
        const U32 requested=Page(m,*next);
        if(!last||m.Read(0x656b04)!=current||(requested!=ItemsVtable&&requested!=MasksVtable))return;
        U32 slot=last==ItemsVtable?0x656b10:last==MasksVtable?0x656b18:last==GearVtable?0x656b0c:0x656b14;
        auto dest=m.Read(slot);if(!dest||Page(m,*dest)!=last)return;
        if(last==MapVtable) {
            auto mode=m.Read(*dest+0x78c);if(!mode)return;
            m.Write(*dest+0x78c,*mode&~0x00ff0000u);
        }
        source=*current;target=*dest;capture_started=false;
        m.Write(ctx+12,target);
    }
    // Replace only the verified empty Gear/Map PrepareOpen during recall with
    // the same native capture restart used by Items. Keep the original LR.
    template<class M,class R> bool Prepare(M& m,R& r) {
        r[14]=PCs[0]+4;
        if(!Pending(m,r[1])||r[0]!=target||capture_started||
           !((last==MapVtable&&r[2]==0x00599a50)||(last==GearVtable&&r[2]==0x005a6f00)))return false; // ref 0x5998e0, 0x5a6d90
        capture_started=true;r[0]=game+0x11010;r[1]=0;r[15]=0x001bb118;return true; // ref 0x1bb320
    }
    template<class M> bool CaptureReady(M& m) const {
        auto graphics=m.Read(0x66889c),textures=m.Read(game+0x11018);
        const auto state=graphics&&Pointer(*graphics,0x188)?m.Read(*graphics+0x184):std::nullopt;
        return capture_started&&state&&(*state>>16)==8&&textures&&Pointer(*textures,0xb0);
    }
    template<class M> void GearOpenCapture(M& m,U32 graphics,U32 value,U32 page,U32 g) const {
        // Open runs after the manager installs target as current, but before
        // it clears the pending pointer. Preserve only our completed capture.
        const bool own=last==GearVtable&&target&&g==game&&page==target&&
            m.Read(owner+8)==target&&m.Read(owner+12)==target&&
            Page(m,target)==GearVtable&&m.Read(game+0x100)==graphics&&
            m.Read(0x66889c)==graphics&&CaptureReady(m);
        if(own)return;
        // Original STRB r6,[r0,#0x186], including ordinary Gear entry.
        auto word=m.Read(graphics+0x184);
        if(word)m.Write(graphics+0x184,(*word&~0x00ff0000u)|((value&255u)<<16));
    }
    template<class M> void Ready(M& m,U32 ctx,U32 ready) const {
        if(Pending(m,ctx)&&(last==MapVtable||last==GearVtable)) {
            if(!CaptureReady(m))ready=0;
        }
        // Replay STRB r0,[r4,#0x10] without changing adjacent state bytes.
        auto word=m.Read(ctx+16);if(word)m.Write(ctx+16,(*word&~255u)|(ready&255u));
    }
};
}
