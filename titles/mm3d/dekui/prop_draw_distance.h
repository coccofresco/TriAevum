// Ported from tgacquin/mm3d-dekui 6e36dfb4; EUR v1.1 stock windows.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "actor_draw_distance.h"

namespace Mm3dDekui::PropDrawDistance {
using U32=std::uint32_t;
inline std::atomic<float> multiplier{1.f};
// Verified native profiles: En_Kusa65 and En_Kusa2 10A,
// category6, En_Kanban (signs) 78, En_Ishi (rocks) 7C/category6,
// plus Obj_Etcetera (Deku flowers) 11B/category1.
// Never extend every prop (bombs, puzzles, effects, etc.).
inline bool Eligible(U32 header) {
    const unsigned id=header&0xffff,category=(header>>16)&255;
    return (category==6&&(id==0x65||id==0x10a||id==0x78||id==0x7c))||(category==1&&id==0x11b);
}
template<class M> U32 Apply(M& m,U32 actor,U32 original,float factor) {
    if(ActorDrawDistance::Clamp(factor)==1.f||actor<0x100000||actor>0x3ffffe00)return original;
    const auto header=m.Read(actor);
    return header&&Eligible(*header)?ActorDrawDistance::ScaleDistance(original,factor):original;
}
// Bush draw functions ALSO impose an explicit 1200..1300 fade window.
// Move that window outward, preserving its 100-unit width and native alpha,
// water handling, sway distances, positions, collisions and cut/regrow states.
// Constants load into R1/R0/S2; no game data is written. Both game builds
// have identical instruction sequences, at the verified addresses below.
constexpr U32 FirstSVC=0xfffe74;
struct Hook {U32 pc,stock_pc,word;unsigned reg;bool floating;float value;};
constexpr std::array<Hook,6> Hooks{{
    {0x4cb240,0x4cb240,0xe59f1194,1,false,1200.f},
    {0x4cb254,0x4cb254,0xe59f0184,0,false,1300.f},
    {0x4cb378,0x4cb378,0xed9f1a1f,2,true,1300.f},
    {0x50965c,0x50965c,0xe59f1208,1,false,1200.f},
    {0x5097b4,0x5097b4,0xe59f10cc,1,false,1300.f},
    {0x5097f8,0x5097f8,0xed9f1a23,2,true,1300.f},
}};
constexpr std::array<U32,12> BushStart{0xed940a3e,0xe59f1194,0xee100a10,0xe1500001,0xda000005,0xe5d41258,0xe59f0184,0xe3110001,0x1e101a10,0x11500001,0xda00003f,0xe59f0174};
constexpr std::array<U32,9> BushFade{0xee101a10,0xe1510000,0xaafffff9,0xe594025c,0xed9f1a1f,0xeddf0a1f,0xe3500000,0xee310a40,0xee208a20};
constexpr std::array<U32,4> CircleStart{0xe59f1208,0xe59400f8,0xe1500001,0xca000051};
constexpr std::array<U32,3> CircleEnd{0xe59f10cc,0xe1510000,0xdafffff9};
constexpr std::array<U32,8> CircleFade{0xedd40a3e,0xed9f1a23,0xed9f0a23,0xe5940294,0xee710a60,0xe3500000,0xee200a80,0xeebd8ac0};
inline U32 Instruction(U32 pc,U32 word) {
    for(unsigned i=0;i<Hooks.size();++i)
        if((pc==Hooks[i].pc||pc==Hooks[i].stock_pc)&&word==Hooks[i].word)
            return 0xef000000|(FirstSVC+i);
    return word;
}
template<class M,std::size_t N> bool Matches(M& m,U32 at,const std::array<U32,N>& words) {
    for(unsigned i=0;i<N;++i)if(m.Read(at+4*i)!=std::optional<U32>(words[i]))return false;
    return true;
}
template<class M> bool CodeMatches(M& m,U32 pc) {
    for(unsigned i=0;i<Hooks.size();++i) {
        const auto& h=Hooks[i];
        if(pc!=h.pc&&pc!=h.stock_pc)continue;
        const U32 offset=0;
        if(i<3)return Matches(m,0x004cb23c+offset,BushStart)&&Matches(m,0x004cb368+offset,BushFade)&&
            m.Read(0x004cb3dc+offset)==std::optional<U32>(0x44960000)&&
            m.Read(0x004cb3e0+offset)==std::optional<U32>(0x44a28000)&&
            m.Read(0x004cb3fc+offset)==std::optional<U32>(0x44a28000)&&
            m.Read(0x004cb400+offset)==std::optional<U32>(0x3c23d70b);
        return Matches(m,0x0050965c+offset,CircleStart)&&Matches(m,0x005097b4+offset,CircleEnd)&&
            Matches(m,0x005097f4+offset,CircleFade)&&
            m.Read(0x0050986c+offset)==std::optional<U32>(0x44960000)&&
            m.Read(0x00509888+offset)==std::optional<U32>(0x44a28000)&&
            m.Read(0x0050988c+offset)==std::optional<U32>(0x44a28000)&&
            m.Read(0x00509890+offset)==std::optional<U32>(0x40233333);
    }
    return false;
}
inline U32 Threshold(float native,float factor) {
    return std::bit_cast<U32>(native+1200.f*(ActorDrawDistance::Clamp(factor)-1.f));
}
template<class Registers,class Floats> void Load(unsigned index,Registers& regs,Floats& floats,float factor) {
    if(index>=Hooks.size())return;
    const auto& h=Hooks[index];
    const auto value=Threshold(h.value,factor);
    if(h.floating)floats[h.reg]=value;else regs[h.reg]=value;
}
}
