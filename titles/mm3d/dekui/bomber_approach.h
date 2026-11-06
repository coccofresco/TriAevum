// Ported from tgacquin/mm3d-dekui 6e36dfb4 src/mm3d/bomber_approach.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <utility>
namespace Mm3dDekui::BomberApproach {
using U32=std::uint32_t;
inline std::atomic<bool> disabled{false};
constexpr U32 StatePC=0x0056dea0,StateWord=0xe1d508fc;
constexpr U32 ApproachPC=0x0056e290,ApproachWord=0xe3a0100f,ResetPC=0x0056e494;
constexpr U32 FirstSVC=0xfffe23;
inline U32 Instruction(U32 pc,U32 word) {
    if(pc==StatePC&&word==StateWord)return 0xef000000|FirstSVC;
    if(pc==ApproachPC&&word==ApproachWord)return 0xef000000|(FirstSVC+1);
    return word;
}
inline constexpr std::array<std::pair<U32,U32>,10> Guards{{
    {0x0056de74,0xe92d4ff0},{StatePC,StateWord},{ApproachPC,ApproachWord},
    {0x0056e294,0xe1a00004},{0x0056e298,0xebf6232b},{0x0056e29c,0xe3a00002},
    {ResetPC,0xeeb00a69},{0x0056e498,0xe3a01000},{0x003f6fe0,0x0056de74},
    {0x6717ec,0x0004020f}
}};
template<class M> bool CodeMatches(M& m){for(auto [a,w]:Guards)if(m.Read(a)!=w)return false;return true;}
template<class M> bool Suppress(M& m,U32 actor) {
    if(!disabled||actor<0x100000||actor>0x3ffff3f4||(actor&3))return false;
    auto id=m.Read(actor);
    return id&&(*id&65535)==0x20f&&m.Read(actor+0xb68)==0x0056de74;
}
inline U32 State(U32 original,bool suppress) {
    // Only the ordinary wandering routine: states2/3 approach/wait for Link.
    // State4 runs native stop/idle cleanup. Hide-and-seek uses other actions.
    return suppress&&(original==2||original==3)?4:original;
}
}
