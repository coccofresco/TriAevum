// Ported from mm3d-dekui src/mm3d/menu_close.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"
namespace Mm3dDekui::MenuClose {
using NativeInventory::U32;
// USA Restoration: retain native close animations, sound and cleanup. Gear's
// option edge becomes zero before it can choose the Options destination.
constexpr std::array<U32,3> PCs{0x005a5908,0x005a5924,0x00599a00}; // ref 0x5a5798, 0x5a57b4, 0x599890
constexpr std::array<U32,3> Words{0xe5942198,0xe3100002,0xe3100002};
constexpr std::array<U32,3> Replacements{0xe3a02000,0xe3100006,0xe3100006};
constexpr std::array<std::pair<U32,U32>,12> Guards{{
 {0x005a5900,0x0a000040},{0x005a5904,0xe594119c},{0x005a590c,0xe3a06000},{0x005a5910,0xe1c21001}, // ref 0x5a5790, 0x5a5794, 0x5a579c, 0x5a57a0
 {0x005a591c,0xe5846198},{0x005a5920,0xe5900034},{0x005a5928,0x03510000},{0x005a592c,0x0a00004c}, // ref 0x5a57ac, 0x5a57b0, 0x5a57b8, 0x5a57bc
 {0x005999f8,0xebffef37},{0x005999fc,0xe5d60034},{0x00599a04,0x0a00000d},{0x00599a08,0xe5d9008e} // ref 0x599888, 0x59988c, 0x599894, 0x599898
}};
inline U32 Instruction(U32 pc,U32 word){
 for(unsigned i=0;i<PCs.size();++i)if(pc==PCs[i]&&word==Words[i])return Replacements[i];
 return word;
}
template<class M> bool CodeMatches(M& m){
 for(unsigned i=0;i<PCs.size();++i)if(m.Read(PCs[i])!=Words[i])return false;
 for(auto [a,v]:Guards)if(m.Read(a)!=v)return false;
 return true;
}
}
