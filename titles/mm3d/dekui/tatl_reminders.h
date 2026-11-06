// Ported from tgacquin/mm3d-dekui 6e36dfb4 src/mm3d/tatl_reminders.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <utility>
namespace Mm3dDekui::TatlReminders {
using U32=std::uint32_t;
inline std::atomic<bool> disabled{false};
// Native Tatl prompt requester. 0x34 plays NA_SE_SY_CHAT_ALARM_INFO;
// 0x2a/0x2b are ordinary reminders/hints; 0x2c dismisses a prompt.
constexpr U32 PC=0x0021f54c,Word=0xe1a04001,SVC=0xfffe20;
constexpr bool Regular(U32 type){return type==0x2a||type==0x2b;}
inline U32 Instruction(U32 pc,U32 word){return pc==PC&&word==Word?0xef000000|SVC:word;}
inline constexpr std::array<std::pair<U32,U32>,9> Guards{{
    {0x0021f548,0xe92d4070},{PC,Word},{0x0021f550,0xe354002a},
    {0x0021f554,0xe2805b21},{0x0021f558,0x1354002b},{0x0021f55c,0xe2855f8a},
    {0x0021f560,0x13540034},{0x0021f59c,0xe354002b},{0x0021f628,0x010007df}
}};
template<class M> bool CodeMatches(M& m){for(auto [at,w]:Guards)if(m.Read(at)!=w)return false;return true;}
template<class M> U32 Request(M& m,U32 context,U32 type) {
    if(!disabled||!Regular(type))return type;
    // Retire an already-visible regular prompt, but never clear a queued alarm.
    if(context>=0x100000&&context<=0x3fff79a4&&!(context&3)) {
        auto current=m.Read(context+0x8658);
        if(current&&Regular(*current&65535))m.Write(context+0x8658,*current&0xffff0000u);
    }
    return 0; // Native requester ignores unknown/zero requests.
}
}
