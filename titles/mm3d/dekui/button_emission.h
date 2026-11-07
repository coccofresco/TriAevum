// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_inventory.h"
namespace Mm3dDekui::ButtonEmission {
using NativeInventory::U32;
// EUR v1.1 Widget renderer. This load is reached only after the native
// visibility, effective alpha and render-pass tests, including attached
// Pictograms that do not belong to the LayoutBase tree (notebook/dialogue).
constexpr U32 Pc=0x00134de4, Word=0xe5940028, Svc=0x00fffa40;
constexpr std::array<std::pair<U32,U32>,10> Guards{{
    {Pc,Word},{0x134db4,0xe3120001},{0x134dcc,0x9a00007d},
    {0x134ddc,0xe312000c},{0x134de0,0x0a000078},
    {0x134df0,0xe3500000},{0x134df4,0x0a00000c},{0x134e2c,0xe1d48ddd},
    {0x134df8,0xe1d000b4},{0x134e20,0xeb004030}}};
template<class M> bool CodeMatches(M& m) {
    for(auto [pc,word]:Guards)if(m.Read(pc)!=std::optional<U32>(word))return false;
    return true;
}
}
