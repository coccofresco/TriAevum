// Ported from tgacquin/mm3d-dekui 6e36dfb4; EUR v1.1 stock windows.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Mm3dDekui::ActorDrawDistance {
using U32=std::uint32_t;
inline std::atomic<float> multiplier{1.f};
constexpr U32 PC=0x1754d8,OriginalPC=PC,Word=0xeeb00a48,SVC=0xfffe73;
// Actor_DrawAll passes projected position F0, distance100, extent104,
// downward extent108 and projected W FC to native visibility test190EA4.
// Change only its distance argument (S0), keeping native frustum checks,
// fade counters, room/lens rules, actor initialization and draw callbacks.
// USA Rev1 and Restoration v110 bodies are byte-identical at this offset.
constexpr std::array<U32,19> Caller{0xe5d48124,0xe284a0f0,0xedd49a3f,0xed948a40,0xe1a00005,0xeb0055f3,0xe3500000,0x15d40020,0xe2842f41,0xeef01a69,0x13100008,0x1eb08a68,0xecd20a02,0xe1a0100a,0xe1a00005,0xeeb00a48,0xeb006e19,0xe21000ff,0xe5c40124};
constexpr std::array<U32,49> Cull{0xee300a20,0xed912a02,0xeeb42ac0,0xeef1fa10,0xa3a00002,0xaa000026,0xeeb10a60,0xeeb40ac2,0xeef1fa10,0x2a000021,0xee112a90,0xe35205fe,0xe5902210,0xbddf1a1f,0xe2823103,0xe2533627,0x0eb00a60,0x0a000008,0xed902ab2,0xed9f3a1a,0xed900aad,0xeddf2a19,0xee222a03,0xee200a80,0xee600a82,0xee200a22,0xee221a01,0xed912a00,0xeeb02ac2,0xee320a40,0xeeb40ae1,0xeef1fa10,0x2a00000a,0xed910a01,0xeeb12a61,0xee301a01,0xeeb41ac2,0xeef1fa10,0xda000004,0xee300a60,0xeeb40ae1,0xeef1fa10,0x33a00000,0x3a000000,0xe3a00001,0xe12fff1e,0x3f800000,0x3fddb3d8,0x4038c089};
inline U32 Instruction(U32 pc,U32 word) {
    return (pc==PC||pc==OriginalPC)&&word==Word?0xef000000|SVC:word;
}
template<class M> bool CodeMatches(M& m,U32 pc) {
    if(pc!=PC&&pc!=OriginalPC)return false;
    const U32 offset=0;
    for(unsigned i=0;i<Caller.size();++i)
        if(m.Read(0x17549c-offset+i*4)!=std::optional<U32>(Caller[i]))return false;
    for(unsigned i=0;i<Cull.size();++i)
        if(m.Read(0x190d48-offset+i*4)!=std::optional<U32>(Cull[i]))return false;
    return true;
}
inline float Clamp(float value) {return std::isfinite(value)?std::clamp(value,1.f,3.f):1.f;}
inline U32 ScaleDistance(U32 original,float factor) {
    // Exact replay at 1x; never amplify malformed distances.
    factor=Clamp(factor);
    if(factor==1.f)return original;
    const float distance=std::bit_cast<float>(original);
    if(!std::isfinite(distance)||distance<=0.f)return original;
    const float extended=distance*factor;
    return std::isfinite(extended)?std::bit_cast<U32>(extended):original;
}
inline U32 Distance(U32 original,unsigned category,float factor) {
    return category==4||category==5?ScaleDistance(original,factor):original;
}
template<class M> U32 Apply(M& m,U32 actor,U32 original,float factor) {
    if(Clamp(factor)==1.f||actor<0x100000||actor>0x3ffffe00)return original;
    const auto header=m.Read(actor);
    return header?Distance(original,(*header>>16)&255,factor):original;
}
}
