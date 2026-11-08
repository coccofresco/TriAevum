#pragma once
#include "../adapter/mm3d_hook_sites.h"
#include "triaevum_guest_transaction.h"
#include <array>
#include <bit>
#include <cmath>

namespace Oot3dNativeGame::Mm3dFoliage {
inline constexpr std::array Sites{
    Mm3dHookSite{"ObjMure spawn",0x526678,{0xE92D4FF0,0xE1A04000,0xE2800C02,0xE24DD024}},
    Mm3dHookSite{"ObjMure active",0x544ED8,{0xE92D5FF0,0xE3A05000,0xE1A03005,0xE280CC01}},
    Mm3dHookSite{"ObjMure3 spawn radius",0x519C24,{0xE59F1040,0xEE100A10,0xE1500001,0xAA00000C}},
    Mm3dHookSite{"ObjMure3 active radius",0x518078,{0xE59F10A8,0xEE100A10,0xE1510000,0xCA000025}},
};
inline constexpr std::array Proofs{
    Mm3dHookSite{"ObjMure clip load",0x526690,{0xE59F3260,0xEEB00AC0,0xE0832102,0xEDD20A00}},
    Mm3dHookSite{"ObjMure active clip load",0x544FA0,{0xED931A00,0xEE710A20,0xEEF40AC0,0xEEF1FA10}},
    Mm3dHookSite{"ObjMure3 spawn position",0x519C18,{0xEDD00A3E,0xED900A3C,0xEBFE37A4,0xE59F1040}},
    Mm3dHookSite{"ObjMure3 active position",0x51806C,{0xEDD50A3E,0xED950A3C,0xEBFE3E8F,0xE59F10A8}},
};
inline constexpr uint32_t ClipTable=0x68B7F8;
inline constexpr std::array<float,5> NativeClip{1600,1600,1000,1000,1000};
inline float Clip(unsigned i,bool extended,bool active) {
    // 2Ship projected-Z / multiplier semantics. Native active test adds40:
    // include the extra80 in the table while that consumer executes, rather
    // than patching/recompiling its text literal. Spawn retains exact3x.
    return NativeClip[i]*(extended?3.f:1.f)+(extended&&active?80.f:0.f);
}
inline bool OwnedClip(unsigned i,uint32_t word) {
    return word==std::bit_cast<uint32_t>(Clip(i,false,false))||
           word==std::bit_cast<uint32_t>(Clip(i,true,false))||
           word==std::bit_cast<uint32_t>(Clip(i,true,true));
}
class Source {
    bool mVerified=false,mExtended=false;
    static bool Pointer(uint32_t p,uint32_t size){return p>=0x08000000&&!(p&3)&&p<=0x40000000-size;}
public:
    struct Statistics {uint64_t TableChanges=0,RadiusChanges=0,PressureNative=0,Invalid=0;} Stats;
    void SetExtended(bool value){mExtended=value;}
    bool Verified()const{return mVerified;}
    template<class M,class HasBlock> bool Verify(M& m,HasBlock&& hasBlock) {
        const auto read=[&](uint32_t p,uint32_t& w){return m.Read32(p,&w);};
        mVerified=true;
        for(const auto& s:Sites)mVerified=Mm3dHookSiteMatches(s,read)&&hasBlock(s.Pc)&&mVerified;
        for(const auto& s:Proofs)mVerified=Mm3dHookSiteMatches(s,read)&&mVerified;
        constexpr std::array<std::pair<uint32_t,uint32_t>,7> words{{
            {0x5268F8,ClipTable},{0x545090,ClipTable},{0x54508C,0x42200000},
            {0x519C6C,0x49A17020},{0x518128,0x4A005390},
            {0x4A7AB8,0xEE200A00},{0x4A7ABC,0xEE000AA0}}};
        for(const auto&[p,expected]:words){uint32_t w=0;mVerified=m.Read32(p,&w)&&w==expected&&mVerified;}
        return mVerified;
    }
    template<class CPU,class M> void Observe(uint32_t pc,CPU& cpu,M& m) {
        if(!mVerified)return;
        const bool table=pc==0x526678||pc==0x544ED8;
        const bool radius=pc==0x519C24||pc==0x518078;
        if(!table&&!radius)return;
        if(!table&&!mExtended)return;
        uint32_t play=table?cpu.r[1]:cpu.r[5];
        if(pc==0x518078&&!m.Read32(cpu.r[13],&play)){++Stats.Invalid;return;}
        uint8_t count=255;
        if(!Pointer(play,0x11030)||!m.Read8(play+0x20BE,&count)){++Stats.Invalid;return;}
        const bool extended=mExtended&&count<165;
        if(mExtended&&!extended)++Stats.PressureNative;
        if(table) {
            std::array<uint32_t,5> target{};bool changed=false;
            for(unsigned i=0;i<5;++i) {
                uint32_t current=0;
                if(!m.Read32(ClipTable+i*4,&current)||!OwnedClip(i,current)){++Stats.Invalid;return;}
                target[i]=std::bit_cast<uint32_t>(Clip(i,extended,pc==0x544ED8));changed|=current!=target[i];
            }
            if(!changed)return;
            GuestByteAccess guest(m);GuestWriteTransaction tx(guest);
            if(!tx.Add(ClipTable,target)||!tx.Commit()){++Stats.Invalid;return;}
            ++Stats.TableChanges;
        } else if(extended) {
            const float squared=std::bit_cast<float>(cpu.vfp[0]);
            if(!std::isfinite(squared)||squared<0){++Stats.Invalid;return;}
            cpu.vfp[0]=std::bit_cast<uint32_t>(squared/9.f);
            ++Stats.RadiusChanges; // native radius literals and hysteresis stay intact
        }
    }
};
} // namespace Oot3dNativeGame::Mm3dFoliage
