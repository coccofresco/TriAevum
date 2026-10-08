// Ported from mm3d-dekui src/mm3d/menu_page_switch.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"

namespace Mm3dDekui::MenuPageSwitch {
using NativeInventory::U32;
using NativeInventory::Pointer;
using NativeInventory::LayoutNamed;
// Restoration + HD HUD v110 USA. Keep the screen manager's Prepare/Open/Close
// calls and resource-ready checks; only this requested handoff skips animation.
constexpr U32 FirstSVC=0xffff90;
constexpr std::array<U32,8> PCs{0x00104da4,0x0010fd78,0x001619a4,0x005c2928,0x005c25d8,0x005a6164,0x005a6060,0x0059747c}; // ref 0x104dd0, 0x10fda4, 0x161aec, 0x5c27b8, 0x5c2468, 0x5a5ff4, 0x5a5ef0, 0x59730c
constexpr std::array<U32,8> Words{0xe1a04000,0xe12fff32,0xe1a04000,0xeaefe1fa,0x1bef584a,0xebefc967,0xeddf0a32,0xebf1a414}; // ref 0xeaefe2d8, 0x1bef58fd, 0xebefca1a, 0xebf1a4f4
constexpr U32 ItemsVtable=0x00639ee4,MasksVtable=0x00639f94,GearVtable=0x0063a290,MapVtable=0x00639e04; // ref 0x639ec4, 0x639f74, 0x63a270, 0x639de4
constexpr std::array<std::pair<U32,U32>,23> Guards{{
    {0x00104dac,0xeb002bad},{0x00104de0,0xe5912030}, // ref 0x104dd8, 0x104e0c
    {0x00104df8,0xe591201c},{0x00104e1c,0xe5912020}, // ref 0x104e24, 0x104e48
    {0x0010fd74,0xe1a01004},{0x0010fd7c,0xe5c40011}, // ref 0x10fda0, 0x10fda8
    {0x001619a0,0xe92d4030},{0x001619b0,0xeeb09a40}, // ref 0x161ae8, 0x161af8
    {ItemsVtable+0x1c,0x0059e734},{ItemsVtable+0x20,0x0059e9d4}, // ref 0x59e5c4, 0x59e864
    {GearVtable+0x1c,0x005a62c8},{GearVtable+0x20,0x005a6cb0}, // ref 0x5a6158, 0x5a6b40
    {0x005c2924,0xe2800010},{0x005a6168,0xe5940100}, // ref 0x5c27b4, 0x5a5ff8
    {0x005a616c,0xe5c06186},{0x005c25d4,0x12800010},{0x005a6130,0x3f000000}, // ref 0x5a5ffc, 0x5c2464, 0x5a5fc0
    {MasksVtable+0x1c,0x0059f934},{MasksVtable+0x20,0x0059fbf0}, // ref 0x59f7c4, 0x59fa80
    {MapVtable+0x1c,0x0059771c},{MapVtable+0x20,0x005994ec}, // ref 0x5975ac, 0x59937c
    {0x00597474,0xe59f013c},{0x00597480,0xe5970004} // ref 0x597304, 0x597310
}};
inline U32 Instruction(U32 pc,U32 word) {
    for(unsigned i=0;i<PCs.size();++i)
        if(pc==PCs[i]&&word==Words[i])return (word&0xf0000000)|0x0f000000|(FirstSVC+i);
    return word;
}
template<class M> bool CodeMatches(M& m) {
    for(unsigned i=0;i<PCs.size();++i)if(m.Read(PCs[i])!=Words[i])return false;
    for(auto [at,word]:Guards)if(m.Read(at)!=word)return false;
    return true;
}
template<class M> std::optional<U32> Byte(M& m,U32 at) {
    auto w=m.Read(at&~3u);if(!w)return {};return (*w>>((at&3)*8))&255;
}
// Items/Masks capture conversion keeps one quarter of scene brightness.
// Gear instead draws a black overlay; use 75% alpha for the same dimming.
inline U32 GearBackdropAlpha() {
    return std::bit_cast<U32>(GearMenu::enabled.load()?.75f:.5f);
}
struct Hook {
    U32 context=0,source=0,target=0,target_vtable=0;
    unsigned opening_ticks=0,waiting_ticks=0;
    bool preserve_capture=false;
    template<class M> bool PairPending(M& m,U32 ctx) const {
        return context&&ctx==context&&m.Read(ctx+8)==source&&m.Read(ctx+12)==target;
    }
    template<class M,class Registers> void MapCloseSound(M& m,Registers& r) const {
        // Map's Close unconditionally plays the exit sound. A bumper handoff
        // is still inside the menu; keep the native sound for real exits.
        const bool page_switch=PairPending(m,context)&&target_vtable==ItemsVtable&&
            m.Read(source)==MapVtable&&m.Read(target)==ItemsVtable;
        r[14]=PCs[7]+4;
        r[15]=page_switch?r[14]:0x002004d4; // ref 0x2006e4
    }
    template<class M> bool KeepCapture(M& m,U32 capture) const {
        if(!preserve_capture||!PairPending(m,context))return false;
        auto ui=m.Read(context+4);
        if(!ui||!Pointer(*ui,0x170))return false;
        auto game=m.Read(*ui+0x16c);
        return game&&Pointer(*game,0x11030)&&Byte(m,*game+0x13d)==4&&capture==*game+0x11010;
    }
    // Replay the original branches when no page handoff owns this capture.
    // Gear also clears a byte on its graphics owner after Close. Preserve that
    // byte only if the owner is the shared capture state; otherwise replay it.
    template<class M,class Registers> void CaptureCall(M& m,Registers& r,unsigned site) const {
        const bool keep=KeepCapture(m,r[0]);
        if(site==3) {r[15]=keep?r[14]:0x001bb118;return;} // tail B: preserve LR // ref 0x1bb320
        r[14]=PCs[site]+4;
        r[15]=keep?r[14]:0x00198708; // ref 0x198864
        if(keep&&site==5) {
            auto owner=m.Read((r[0]-0x11010)+0x100),state=m.Read(0x66889c);
            if(owner&&state&&Pointer(*owner,0x188)&&*owner==*state)r[15]=0x005a6170; // ref 0x5a6000
        }
    }
    template<class M> void Tick(M& m,U32 ctx) {
        if(context) {
            if(ctx!=context||++waiting_ticks>120) {*this={};return;}
            if(PairPending(m,ctx))return;
            if(m.Read(ctx+8)==target&&m.Read(ctx+12)==0&&opening_ticks) {
                --opening_ticks;return;
            }
            *this={};
        }
        if(!NativeInventory::enabled.load()||!GearMenu::enabled.load()||!Pointer(ctx,20)||
           m.Read(ctx+12)!=0||Byte(m,ctx+16)!=1||Byte(m,ctx+17)!=1)return;
        auto current=m.Read(ctx+8),ui=m.Read(ctx+4);
        if(!current||!ui||!Pointer(*ui,0x170))return;
        auto game=m.Read(*ui+0x16c);
        if(!game||!Pointer(*game,0x140)||Byte(m,*game+0x13d)!=4)return;
        auto pressed=m.Read(*game+0x34),items=m.Read(0x656b10),gear=m.Read(0x656b0c),
             masks=m.Read(0x656b18),map=m.Read(0x656b14);
        if(!pressed||!items||!gear||!masks||!map||
           !Pointer(*items,0xc4)||!Pointer(*masks,0xc4)||!Pointer(*gear,0x198)||!Pointer(*map,0x794)||
           m.Read(*items)!=ItemsVtable||m.Read(*masks)!=MasksVtable||
           m.Read(*gear)!=GearVtable||m.Read(*map)!=MapVtable)return;
        // Gear <- Masks <-> Items -> Map. The middle pair remains native.
        if(*pressed&0xc0f)return;
        const U32 shoulder=*pressed&0x300;
        if((*current==*items&&shoulder==0x100)||(*current==*masks&&shoulder==0x200)) {
            // Native inventory accepts shoulders in both browsing (2) and
            // selected-item (3) states. Otherwise selection falls through to
            // its original two-page toggle instead of our outer-page route.
            const auto state=Byte(m,*current+0x9f);
            if(state!=2&&state!=3)return;
            auto layout=m.Read(*current+0xc0);
            const bool from_items=*current==*items;
            if(!layout||!LayoutNamed(m,*layout,from_items?"MenuItem":"MenuMask"))return;
            if(from_items) {
                auto mode=m.Read(*map+0x78c);if(!mode)return;
                // Same reset as Restoration's map shortcut: never reopen Soaring.
                m.Write(*map+0x78c,*mode&~0x00ff0000u);
                target=*map;target_vtable=MapVtable;
            } else {target=*gear;target_vtable=GearVtable;}
        } else if(*current==*gear&&shoulder==0x100) {
            if(Byte(m,*gear+0x194)!=2)return;
            auto layout=m.Read(*gear+0x14);
            if(!layout||!LayoutNamed(m,*layout,"MenuCollect"))return;
            target=*masks;target_vtable=MasksVtable;
        } else if(*current==*map&&shoulder==0x200) {
            if(Byte(m,*map+0x78e)!=0)return;
            auto layout=m.Read(*map+0x18);
            if(!layout||!LayoutNamed(m,*layout,"MenuMapDungeon"))return;
            target=*items;target_vtable=ItemsVtable;
        } else return;
        // A map opened independently may not own an inventory capture. In that
        // case allow native Items PrepareOpen to create one normally.
        auto capture=m.Read(*game+0x11018);
        preserve_capture=capture&&Pointer(*capture,0xb0);
        context=ctx;source=*current;opening_ticks=1;waiting_ticks=0;
        m.Write(ctx+12,target);
        m.Write(*game+0x34,*pressed&~shoulder); // consume only this pressed edge
    }
    template<class M> void FinishMapBackground(M& m,U32 layout) const {
        if(Byte(m,target+0x78d)!=0||Byte(m,target+0x78e)!=0||
           !LayoutNamed(m,layout,"BackGround"))return;
        // BackGround is shared with gameplay. Direct menu entry must select
        // its native reveal track as well as seek it; a hidden/outgoing track
        // inherited from the previous screen cannot be fixed by a larger delta.
        auto player=m.Read(layout+0x170),intro=m.Read(layout+0x174);
        if(!player||!intro||!Pointer(*player,0x18)||!Pointer(*intro,0x18))return;
        auto name=m.Read(*intro+8),duration=m.Read(*intro+12),flags=m.Read(*player+8);
        if(!name||!duration||!*duration||*duration>120||!flags||
           !Mm3dDekui::DialogueScale::Name(m,*name,"in_background_anim"))return;
        m.Write(*player+12,*intro);
        m.Write(*player+0x10,std::bit_cast<U32>(float(*duration)));
        m.Write(*player+8,(*flags&0xff000000u)|0x10001); // Play, no loop, dirty
    }
    template<class M> void FinishMapHud(M& m,U32 layout) const {
        // The title and X prompt share UpWorldMapHud's six-frame intro. Seek
        // that track before its native evaluation, including its delayed keys.
        // Keep zoom/detail and exit tracks under normal native control.
        if(!LayoutNamed(m,layout,"UpWorldMapHud"))return;
        auto player=m.Read(layout+0x19c),intro=m.Read(layout+0x1a4);
        if(!player||!intro||!Pointer(*player,0x18)||!Pointer(*intro,0x18)||
           m.Read(*player+12)!=intro)return;
        auto flags=m.Read(*player+8),duration=m.Read(*intro+12),name=m.Read(*intro+8);
        if(!flags||(*flags&0xffff)!=1||!duration||!*duration||*duration>120||!name||
           !Mm3dDekui::DialogueScale::Name(m,*name,"in_wmap_hud_anim"))return;
        m.Write(*player+0x10,std::bit_cast<U32>(float(*duration)));
        m.Write(*player+8,*flags|0x10000); // native SetFrame's dirty flag
    }
    template<class M> bool FinishLayout(M& m,U32 layout) const {
        if(!context||m.Read(context+8)!=target||m.Read(context+12)!=0||
           m.Read(target)!=target_vtable)return false;
        // Only layouts owned by the incoming page. The large native layout step
        // evaluates animation endpoints before drawing; gameplay time is untouched.
        if(target_vtable==GearVtable) {
            for(U32 offset:{0xcu,0x10u,0x14u,0x18u,0x130u})
                if(m.Read(target+offset)==layout)return layout!=0;
        } else if(target_vtable==MapVtable) {
            for(U32 offset:{0xcu,0x10u,0x14u,0x18u,0x1cu,0x20u,0x24u,0x2cu})
                if(layout&&m.Read(target+offset)==layout) {
                    // Native map reveal counter is clamped to five by Calc.
                    m.Write(target+0x750,5);
                    if(m.Read(target+0x14)==layout)FinishMapBackground(m,layout);
                    if(m.Read(target+0x1c)==layout)FinishMapHud(m,layout);
                    return true;
                }
        } else {
            for(U32 offset:{4u,8u,12u,0x24u,0x28u,0x68u,0xc0u})
                if(m.Read(target+offset)==layout)return layout!=0;
        }
        return false;
    }
};
}
