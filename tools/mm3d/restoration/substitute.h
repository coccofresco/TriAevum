// SPDX-License-Identifier: GPL-2.0-or-later
// Additional A32 forms present in Project Restoration HKS, kept title-local.
#pragma once
#include "mm3d_a32_substitute.h"
#include "mm3d_guest_patches.h"
#include "rst_guest.h"
#include <stdexcept>
namespace Mm3dRestoration {
inline std::optional<u32> Substitute(u32 word,u32 pc,oot3d::recomp::a32::GuestState& state,oot3d::recomp::a32::MemoryBus& memory) {
    if(!Oot3dNativeGame::Mm3dConditionPassed(word,state.cpsr))return pc+4;
    if((word&0x0fffffff)==0x0320f000)return pc+4;
    if(auto result=Oot3dNativeGame::ExecuteMm3dSubstitute(word,pc,state,memory))return result;
    Guest g(memory);auto& r=state.r;
    if((word&0x0fff0ff0)==0x01a00000){r[(word>>12)&15]=r[word&15];return pc+4;}
    if((word&0x0e000000)==0x04000000) {
        const u32 rn=(word>>16)&15,rd=(word>>12)&15,base=rn==15?pc+8:r[rn];
        const bool pre=word&0x1000000,up=word&0x800000,byte=word&0x400000,write=word&0x200000,load=word&0x100000;
        const u32 next=up?base+(word&0xfff):base-(word&0xfff),at=pre?next:base;
        if(load)r[rd]=byte?g.Read8(at):g.Read32(at);else if(byte)g.Write8(at,u8(r[rd]));else g.Write32(at,r[rd]);
        if(write||!pre)r[rn]=next;return load&&rd==15?r[15]:pc+4;
    }
    // UpdateTime's displaced STRH r0,[r9,#0xa2].
    if(word==0xe1c90ab2){g.Write16(r[9]+0xa2,u16(r[0]));return pc+4;}
    // VLDR S, PC-relative: Goht's eye scale replacement.
    if((word&0x0f3f0f00)==0x0d1f0a00){const u32 sn=((word>>12)&15)*2+((word>>22)&1),offset=(word&255)*4;state.vfp[sn]=g.Read32((pc+8)+((word&0x800000)?offset:0u-offset));return pc+4;}
    throw std::logic_error("Unsupported verified Restoration replacement");
}
} // namespace Mm3dRestoration
