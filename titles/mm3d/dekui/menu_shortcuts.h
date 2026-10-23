// Ported from mm3d-dekui src/mm3d/menu_shortcuts.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"
namespace Mm3dDekui::MenuShortcuts {
using NativeInventory::U32;
constexpr U32 Start=0x007d0660,PC=0x007d06c4,Word=0xe1110005,Return=0xe8bd81f0; // ref 0x7d0660 (Restoration source port), 0x7d06c4 (Restoration source port)
// Installed Restoration UiScheduleTriggerHook. Both non-ZR branches precede
// this site; returning here bypasses the ZR shortcuts without editing input.
constexpr std::array<U32,53> Code{{
    0xe92d41f0u,0xeb00019bu,0xe5904000u,0xe3540000u,0x8bd81f0u,0xe5d4013du,
    0xe3500004u,0x18bd81f0u,0xe5943030u,0xe5942034u,0xe2133902u,0xe16f3f13u,
    0x13a05001u,0x3a05000u,0xe2127008u,0x13a01001u,0x3a01000u,0xe1a02122u,
    0xe1a032a3u,0xe1120003u,0xe2026001u,0xe0033001u,0x1a000013u,0xe3530000u,
    0x1a000014u,0xe1110005u,0xa000001u,0xe3a00003u,0xeb000590u,0xe1160005u,
    0x8bd81f0u,0xe3a00005u,0xeb000566u,0xe3a02000u,0xe1a03000u,0xe3a00005u,
    0xe5c3278eu,0xeb000587u,0xe1c423d0u,0xe3c22004u,0xe3c33004u,0xe1c423f0u,
    0xe8bd81f0u,0xeb000581u,0xe3570000u,0x8bd81f0u,0xeb0001adu,0xe5d031fau,
    0xe3130004u,0xaffffeau,0xe3a00002u,0xeb000579u,0xeaffffe7u,
}};
inline U32 Instruction(U32 pc,U32 word){return pc==PC&&word==Word?Return:word;}
template<class M> bool CodeMatches(M& m){
    if(m.Read(0x0059bb80)!=0xea0b7295u||m.Read(0x00878470)!=0xebfd607au)return false; // ref 0x59ba10, 0x878470 (Restoration source port)
    for(unsigned i=0;i<Code.size();++i)if(m.Read(Start+i*4)!=Code[i])return false;
    return true;
}
}
