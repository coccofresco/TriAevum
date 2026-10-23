// Ported from mm3d-dekui src/mm3d/bottle_healing.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace Mm3dDekui::BottleHealing {
using U32 = std::uint32_t;
inline std::atomic<int> red_hearts{20}, blue_hearts{20}, fairy_hearts{10};
constexpr U32 FirstSVC = 0xfffc83;
constexpr std::array<U32,3> PCs{0x001e6c70,0x002ac82c,0x0021f140}; // ref 0x1e6e80, 0x2ac978, 0x21f350
constexpr std::array<U32,3> Words{0xe3a00d05,0xe3a000a0,0xe3a000a0};
constexpr U32 HealingBase = 0x7787d8;
struct Signature { U32 start, size, hash; };
// Includes potion branch selection, fairy roles and the bottle-release spawn.
constexpr std::array<Signature,7> Signatures{{
    {0x001e6c50,0x90,0x7d419610}, {0x002ac818,0x38,0xae904853}, // ref 0x1e6e60, 0x2ac964
    {0x0021f138,0x28,0xb871e997}, {0x001dee2c,0x80,0xf5c12778}, // ref 0x21f348, 0x1df03c, 0x574e68e0
    {0x0031f37c,0x64,0xb9e48199}, {0x0031f4ac,0x28,0xa938fead}, // ref 0x31f430, 0xa69d7990, 0x31f560, 0xb6f25b9d
    {0x0031f4f8,0x38,0xaa4669d2}, // ref 0x31f5ac
}};
template<class M> bool CodeMatches(M& m) {
    for(const auto& s:Signatures) {
        U32 hash=2166136261u;
        for(U32 off=0;off<s.size;off+=4) {
            auto word=m.Read(s.start+off);if(!word)return false;
            for(unsigned byte=0;byte<4;++byte)hash=(hash^((*word>>(byte*8))&255))*16777619u;
        }
        if(hash!=s.hash)return false;
    }
    return m.Read(0x001e6fac)==std::optional<U32>(HealingBase)&& // ref 0x1e71bc
           m.Read(0x002ac99c)==std::optional<U32>(HealingBase)&& // ref 0x2acae8
           m.Read(0x0021f2ec)==std::optional<U32>(HealingBase); // ref 0x21f4fc
}
inline U32 Instruction(U32 pc,U32 word) {
    for(unsigned i=0;i<PCs.size();++i)
        if(pc==PCs[i]&&word==Words[i]) return 0xef000000|FirstSVC+i;
    return word;
}
template<class M> bool Play(M& m,U32 ctx) {
    const auto v=m.Read(ctx+0x13c);
    return ctx>=0x100000 && ctx<0x40000000 && !(ctx&3) && v && (*v&0xffff)==0x0402;
}
template<class M> bool Actor(M& m,U32 actor,U32 id) {
    if(actor<0x100000 || actor>=0x40000000 || (actor&3))return false;
    auto v=m.Read(actor);return v&&(*v&0xffff)==id;
}
inline U32 Amount(int hearts) {return U32(std::clamp(hearts,1,20))*16;}
template<class M,class R> void Apply(M& m,R& r,unsigned hook,int red,int blue,int fairy) {
    // Replace only the MOV that supplies the pending healing amount. Native
    // consumption, animation, health-cap clamp, magic refill and flags continue.
    r[0]=hook==0?0x140:0xa0;
    if(hook==0) {
        if(!Play(m,r[7])||!Actor(m,r[4],0)||r[8]!=r[4]+0x100||r[6]!=HealingBase)return;
        const auto action=m.Read(r[4]+0x1fc);
        if(!action)return;
        const U32 type=(*action>>16)&255;
        if(type==0x23)r[0]=Amount(red);
        if(type==0x24)r[0]=Amount(blue);
    } else if(hook==1) {
        if(!Play(m,r[7])||!Actor(m,r[5],0x10)||r[1]!=HealingBase)return;
        const auto params=m.Read(r[5]+0x1c);
        // Type1 is manually released from a bottle; type5 is revival.
        // Wild/pot fairies (type2), fountain fairies and companions stay native.
        if(params&&((*params&0xffff)==1||(*params&0xffff)==5))r[0]=Amount(fairy);
    } else if(hook==2) {
        if(Play(m,r[5])&&Actor(m,r[4],0)&&r[11]==HealingBase&&r[7]==r[4]+0x11e00)
            r[0]=Amount(fairy);
    }
}
}
