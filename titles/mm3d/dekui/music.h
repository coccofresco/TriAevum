// Ported from mm3d-dekui src/mm3d/music.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <utility>

namespace Mm3dDekui::Music {
using U32 = std::uint32_t;
inline std::atomic<bool> muted{false};

// USA Restoration v110: BasicSound::UpdateParam's final volume command.
// Original USA Rev1: 0x5d3fec. The sound is r4, command r5, gain s16.
// Changing only this command preserves playback, native fades and save states.
constexpr U32 PC=0x005d3fec, Word=0xed858a05, SVC=0xffff9b; // ref 0x5d3e7c
constexpr U32 Manager=0x7cb49c, Initialized=0x6b0a3c;
inline U32 Instruction(U32 pc,U32 word) {
    return pc==PC&&word==Word ? 0xef000000|SVC : word;
}
inline constexpr std::array<U32,20> GainCode{
    0xe5940000,0xe5901020,0xe1a00004,0xe12fff31,0xe1a01000,0xe5851010,
    Word,0xedc5aa06,0xedc5ba09,0xe285101c,0xe1a00007,0xecc19a02,
    0xe1a01005,0xe5856028,0xed85ca0b,0xed85ba0c,0xed859a0d,
    0xedc58a0e,0xecbd8b0a,0xe8bd41f0};
inline constexpr std::array<std::pair<U32,U32>,10> Guards{{
    {0x005d3e84,0xe92d41f0},{0x005d3e88,0xe1a04000},{0x005d3e8c,0xed2d8b0a}, // ref 0x5d3d14, 0x5d3d18, 0x5d3d1c
    {0x0010a1f0,Initialized},{0x0010a1f4,Manager}, // ref 0x10a21c, 0x10a220
    // StreamMgr::GetCurrentStreamId indexes its five SoundHandles at +4cc.
    {0x001e0f90,0xe0800104},{0x001e0f94,0xe2801b01},{0x001e0f98,0xe28110cc}, // ref 0x1e11a0, 0x1e11a4, 0x1e11a8
    // BasicSound's archive ID is stored at +ac.
    {0x005d37c4,0xe59000ac},{0x005d3c5c,0xe58410ac} // ref 0x5d3654, 0x5d3aec
}};
template<class M> bool CodeMatches(M& m) {
    for(unsigned i=0;i<GainCode.size();++i)
        if(m.Read(0x005d3fd4+4*i)!=GainCode[i])return false; // ref 0x5d3e64
    for(auto [at,word]:Guards)if(m.Read(at)!=word)return false;
    return true;
}

// Stream IDs documented by Project Restoration's game/sound.h. Stream and
// effect IDs overlap, so this classification is used ONLY for StreamMgr handles.
inline bool IsMusic(U32 id) {
    if(id<0x01000002||id>0x01000086)return false;
    switch(id&0xff) {
    // IDs15/16/17 are Clock Town days1/2/3 despite their DUMMY names.
    // Native StreamMgr remaps MARKET1d by day at1dc7c8..1dc7f8.
    // Failure/victory/item/chest/goal fanfares and unused entries.
    case 0x08: case 0x09: case 0x19:
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x2b:
    case 0x37: case 0x39: case 0x3d: case 0x3f: case 0x41: case 0x56:
    // Ocarina demonstrations, played songs, warps and instrument sessions.
    case 0x32: case 0x33: case 0x34: case 0x35:
    case 0x47: case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c:
    case 0x4d: case 0x4e: case 0x4f: case 0x51: case 0x52: case 0x55:
    case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d:
    case 0x5e: case 0x5f: case 0x61: case 0x62: case 0x63: case 0x64:
    case 0x67: case 0x68: case 0x6c: case 0x6d: case 0x6e: case 0x7f:
    case 0x80: case 0x85: // cutscene effect sequence and Zora session
        return false;
    default:return true;
    }
}
template<class M> bool ShouldMute(M& m,U32 sound) {
    if(!muted.load()||sound<0x100000||sound>0x3fffff50||(sound&3))return false;
    const auto initialized=m.Read(Initialized);
    if(!initialized||!(*initialized&1))return false;
    bool stream=false;
    for(unsigned i=0;i<5;++i)if(m.Read(Manager+0x4cc+4*i)==sound){stream=true;break;}
    if(!stream)return false;
    // Read the active sound's ID rather than the requested track: the manager
    // clears its ID before a stopped track finishes fading out.
    const auto id=m.Read(sound+0xac);
    return id&&IsMusic(*id);
}
template<class M> void Store(M& m,U32 sound,U32 command,U32 volume_bits) {
    // Replay VSTR s16,[r5,#0x14], without changing any ARM/VFP register or flag.
    m.Write(command+0x14,ShouldMute(m,sound)?0u:volume_bits);
}
}
