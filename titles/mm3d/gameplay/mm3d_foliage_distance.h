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
// MM3D actor ids of the three (F06), found offline by the actionFunc-store
// walk (HOOKS R8-F06-actionfunc-walk/room-scan): each profile's init stores
// the first actionFunc at actor+0x1F8, and that action reaches the sites.
//   ObjMure  profile 0x68B7D8 {id 0x3F, ITEMACTION, gameplay_keep, size
//     0x258, init 0x3B37B8, no draw}; init = ObjMure_Init (params chNum>>12,
//     ptn>>8&7, svNum>>5&3, type&0x1F, kill unless type 2..4), actionFunc =
//     0x2D296C (InitialAction: actionFunc = 0x526678, the spawn site).
//     The clip table 0x68B7F8 follows the profile (N64 sZClip in ObjMure data).
//   ObjMure2 profile 0x68B850 {id 0x7F, PROP, size 0x234, init 0x3DBD30};
//     actionFunc 0x2D2138 -> 0x519AA0.
//   ObjMure3 profile 0x68B8DC {id 0x9D, BG, size 0x21C, init 0x3DBD80};
//     actionFunc 0x2D2148 -> 0x519C0C (the spawn radius site 0x519C24).
// Room placements agree (params decode to valid types): ObjMure 26, ObjMure2 8,
// ObjMure3 2 in the uncompressed rooms. The N64 ids (0x4F...) do not apply.
inline constexpr uint16_t ObjMureId=0x3F,ObjMure2Id=0x7F,ObjMure3Id=0x9D;
inline constexpr std::array<std::pair<uint32_t,uint32_t>,12> IdProof{{
    {0x68B7D8,0x0007003F},{0x68B7E8,0x3B37B8},{0x3B3844,0x2D296C},{0x2D2978,0x526678},
    {0x68B850,0x0006007F},{0x68B860,0x3DBD30},{0x3DBD7C,0x2D2138},{0x2D2144,0x519AA0},
    {0x68B8DC,0x0001009D},{0x68B8EC,0x3DBD80},{0x3DBDCC,0x2D2148},{0x2D2154,0x519C0C}}};
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
    bool mVerified=false,mExtended=false,mIdsVerified=false;
    struct Seen {uint32_t Actor=0;bool Active=false;};
    std::array<Seen,32> mSeen{};
    static bool Pointer(uint32_t p,uint32_t size){return p>=0x08000000&&!(p&3)&&p<=0x40000000-size;}
    template<class M> void Census(uint32_t pc,uint32_t actor,M& m) {
        uint32_t word=0;
        if(!Pointer(actor,4)||!m.Read32(actor,&word)){++Stats.Invalid;return;}
        const uint16_t id=uint16_t(word&0xFFFFU);
        const int k=id==ObjMureId?0:id==ObjMure2Id?1:id==ObjMure3Id?2:-1;
        if(k<0){++Stats.Unexpected;return;}
        ++Stats.Calls[k];
        const bool active=pc==0x544ED8||pc==0x518078;
        for(auto& seen:mSeen) if(seen.Actor==actor) {
            if(!seen.Active&&active)++Stats.Spawned[k];
            if(seen.Active&&!active)++Stats.Retired[k];
            seen.Active=active;return;
        }
        ++Stats.Actors[k];
        for(auto& seen:mSeen) if(!seen.Actor){seen={actor,active};return;}
        mSeen[Stats.Actors[k]%mSeen.size()]={actor,active};
    }
public:
    struct Statistics {uint64_t TableChanges=0,RadiusChanges=0,PressureNative=0,Invalid=0;
        // Live census at the four sites by the actor's id (u16 at +0): calls,
        // distinct actors, and per actor the culled->active (spawned) and
        // active->culled (retired) transitions; Unexpected = another id.
        std::array<uint64_t,3> Calls{},Actors{},Spawned{},Retired{};uint64_t Unexpected=0;} Stats;
    bool IdsVerified()const{return mIdsVerified;}
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
        mIdsVerified=true;
        for(const auto&[p,expected]:IdProof){uint32_t w=0;mIdsVerified=m.Read32(p,&w)&&w==expected&&mIdsVerified;}
        return mVerified;
    }
    template<class CPU,class M> void Observe(uint32_t pc,CPU& cpu,M& m) {
        if(!mVerified)return;
        const bool table=pc==0x526678||pc==0x544ED8;
        const bool radius=pc==0x519C24||pc==0x518078;
        if(!table&&!radius)return;
        if(mIdsVerified)Census(pc,table?cpu.r[0]:pc==0x519C24?cpu.r[4]:cpu.r[5],m);
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
