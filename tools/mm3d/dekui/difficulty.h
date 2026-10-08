// Ported from mm3d-dekui src/mm3d/difficulty.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <optional>

namespace Mm3dDekui::Difficulty {
using U32=std::uint32_t;
inline std::atomic<int> damage_multiplier{1};
inline std::atomic<bool> disable_heart_drops{false};
inline std::atomic<bool> disable_rupee_drops{false};
inline std::atomic<bool> disable_fairy_drops{false};
inline std::atomic<bool> disable_magic_drops{false}, disable_ammo_pickups{false};
constexpr U32 OriginalPC=0x21e34c,RestorationPC=0x0021e34c,Word=0xe0822004,SVC=0xffffb5; // ref 0x21e55c
// Health-change routine, USA Rev 1 and Restoration v110. Both bodies match,
// including defense reduction, healing sound, signed-health clamp and return.
constexpr std::array<U32,29> HealthCode{
    0xe92d4010,0xe2514000,0xda000002,0xe59f0054,0xebff886a,0xea000004,
    0xe59f004c,0xe5d00148,0xe3500000,0x13540000,0xb1a040c1,0xe59f003c,
    0xe1d023bc,0xe1d013fa,Word,0xe6bf2072,0xe1520001,0xe1c023bc,
    0xc1c013bc,0xe1d013fc,0xe3510000,0xd3a01000,0xd1c013bc,0xd1a00001,
    0xc3a00001,0xe8bd8010,0x01000777,0x007751d8,0x007752d8};
inline U32 Instruction(U32 pc,U32 word) {
    return (pc==OriginalPC||pc==RestorationPC)&&word==Word?0xef000000|SVC:word;
}
template<class M> bool CodeMatches(M& m,U32 pc) {
    if(pc!=OriginalPC&&pc!=RestorationPC)return false;
    for(unsigned i=0;i<HealthCode.size();++i)
        if(m.Read(pc-0x38+4*i)!=std::optional<U32>(HealthCode[i]))return false;
    return true;
}
// Replay ADD r2,r2,r4 after the native defense reduction. Only damage changes;
// clamp amplified loss to current health before native SXTH to prevent wraparound.
inline U32 ApplyDamage(U32 health,U32 delta,int multiplier) {
    const auto signed_delta=std::bit_cast<std::int32_t>(delta);
    multiplier=std::clamp(multiplier,1,16);
    if(multiplier==1||signed_delta>=0||health>0x7fff)return health+delta;
    const auto loss=-std::int64_t(signed_delta)*multiplier;
    return loss>=health?0:health-U32(loss);
}
constexpr U32 SpawnPC=0x22053c,RestorationSpawnPC=0x0022053c,SpawnWord=0xe92d4070,SpawnSVC=0xffffb4; // ref 0x22074c
constexpr std::array<U32,14> SpawnCode{SpawnWord,0xe24dd020,0xe28d4030,0xe3a0c001,
    0xecd41a03,0xe28de00c,0xe06c550c,0xe3e04000,0xe3a06000,0xe88e1070,
    0xeccd1a03,0xebff5112,0xe28dd020,0xe8bd8070};
inline U32 SpawnInstruction(U32 pc,U32 word) {
    return (pc==SpawnPC||pc==RestorationSpawnPC)&&word==SpawnWord?0xef000000|SpawnSVC:word;
}
template<class M> bool SpawnCodeMatches(M& m,U32 pc) {
    if(pc!=SpawnPC&&pc!=RestorationSpawnPC)return false;
    for(unsigned i=0;i<SpawnCode.size();++i)
        if(m.Read(pc+4*i)!=std::optional<U32>(SpawnCode[i]))return false;
    return true;
}
inline bool SuppressDrop(U32 actor,U32 params,bool hearts,bool rupees=false,bool fairies=false,bool magic=false,bool ammo=false) {
    // En_Item00: recovery heart=3; rupees=0,1,2,0x13,0x14.
    // Magic jars=0xe/0xf. Ammo: bombs=4/0xb, arrows=5/8/9/0xa,
    // nuts=0xc/0x17, stick=0xd. Zero-ammo bomb placeholder0x19 is not a refill.
    // Verified against the native pickup switch at 0x428c24. High flags do not
    // change type. Other collectibles, chest actors and direct rewards stay native.
    // En_Elf actor0x10 uses the low nibble for its role. Type2 is the timed
    // healing fairy used by the native drop paths (238504/547884), including
    // high-bit spawn flags. Other roles, including companion and revival, pass.
    if(fairies&&actor==0x10&&(params&15)==2)return true;
    const U32 type=params&255;
    return actor==0xe&&((hearts&&type==3)||
        (rupees&&(type<=2||type==0x13||type==0x14))||
        (magic&&(type==0xe||type==0xf))||
        (ammo&&(type==4||type==5||(type>=8&&type<=0xd)||type==0x17)));
}
template<class M,class Registers> void Spawn(M& m,Registers& regs,bool hearts,bool rupees=false,bool fairies=false,bool magic=false,bool ammo=false) {
    // Actor_Spawn wrapper: r2=actor ID, seventh argument at entry SP+8=params.
    const U32 sp=regs[13];auto params=m.Read(sp+8);
    if(params&&SuppressDrop(regs[2],*params,hearts,rupees,fairies,magic,ammo)) {
        regs[0]=0;regs[15]=regs[14];return; // normal null-spawn result, no allocation
    }
    // Replay STMFD sp!,{r4-r6,lr}; leave flags and the caller's arguments intact.
    for(unsigned i=0;i<3;++i)m.Write(sp-16+4*i,regs[4+i]);
    m.Write(sp-4,regs[14]);regs[13]=sp-16;
}

}

// Compatibility with the already integrated settings-based difficulty adapter.
namespace Mm3dDekui::Difficulty {
struct Settings {
    int DamageMultiplier = 1;
    bool DisableHeartDrops = false, DisableRupeeDrops = false, DisableFairyDrops = false;
    bool DisableMagicDrops = false, DisableAmmoPickups = false;
};
inline constexpr U32 kDamagePc=OriginalPC, kDamageWord=Word;
inline constexpr auto kHealthCode=HealthCode;
inline constexpr U32 kHealthCodeAddress=kDamagePc-0x38;
inline constexpr U32 kSpawnPc=SpawnPC, kSpawnWord=SpawnWord;
inline constexpr auto kSpawnCode=SpawnCode;
inline bool SuppressDrop(U32 actor,U32 params,const Settings& s) {
    return SuppressDrop(actor,params,s.DisableHeartDrops,s.DisableRupeeDrops,
                        s.DisableFairyDrops,s.DisableMagicDrops,s.DisableAmmoPickups);
}
template<class M,class R> U32 Spawn(M& m,R& r,const Settings& s) {
    const U32 pc=r[15]; r[15]=kSpawnPc+4;
    Spawn(m,r,s.DisableHeartDrops,s.DisableRupeeDrops,s.DisableFairyDrops,
          s.DisableMagicDrops,s.DisableAmmoPickups);
    const U32 next=r[15]; r[15]=pc; return next;
}
}
