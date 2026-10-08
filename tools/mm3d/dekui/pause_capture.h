// Ported from mm3d-dekui src/mm3d/pause_capture.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace Mm3dDekui::PauseCapture {
using U32=std::uint32_t;
inline std::atomic<bool> enabled{true};
inline std::atomic<unsigned> captured_scale{0},bound_textures{0};
constexpr U32 FirstSVC=0xffffb8;
constexpr std::array<U32,3> PCs{0x00120620,0x00141d44,0x0019870c}; // ref 0x120648, 0x141d88, 0x198868
constexpr std::array<U32,3> Words{0xe0870108,0xe280c01c,0xe1a04000};
inline U32 Instruction(U32 pc,U32 word) {
    for(unsigned i=0;i<PCs.size();++i)if(pc==PCs[i]&&word==Words[i])return 0xef000000|FirstSVC+i;
    return word;
}
template<class M> bool CodeMatches(M& m) {
    for(unsigned i=0;i<PCs.size();++i)if(m.Read(PCs[i])!=std::optional<U32>(Words[i]))return false;
    return m.Read(0x00120630)==std::optional<U32>(0xe3550000)&& // ref 0x120658
        m.Read(0x00141d50)==std::optional<U32>(0xec800a07)&&m.Read(0x00198710)==std::optional<U32>(0xe5900000); // ref 0x141d94, 0x19886c
}
// The capture reads the game's linear display buffers, never a presented HUD frame.
inline U32 Physical(U32 p) {
    if(p>=0x14000000&&p<0x1c000000)return p-0x14000000+0x20000000;
    if(p>=0x30000000&&p<0x40000000)return p-0x30000000+0x20000000;
    if(p>=0x1f000000&&p<0x1f600000)return p-0x1f000000+0x18000000;
    return 0;
}
constexpr std::array<std::array<U32,2>,6> ProbePoints{{{25,16},{151,16},{325,100},{25,100},{151,220},{325,220}}};
using Probes=std::array<U32,ProbePoints.size()>;
struct Backend {
    virtual ~Backend()=default;
    virtual void CapturePause(U32 source,unsigned eye)=0;
    virtual void BindPause(U32 texture,unsigned eye,const Probes& probes)=0;
    virtual void ResetPause()=0;
};
// Renderer replacement/reboot happens with guest execution stopped, like memory
// rasterizer callbacks. Never share raw Vulkan resources with the CPU hook.
inline std::atomic<Backend*> backend{nullptr};
struct Hook {
    U32 manager=0;
    template<class M> void Convert(M& m,U32 owner,U32 index,U32 eye,U32 mode) {
        if(mode!=1||eye>1||index>1||owner<0x100000||owner>0x3fffffe0)return;
        if(eye==0)manager=owner;
        if(manager!=owner)return;
        auto src=m.Read(owner+0xc+index*4+eye*8);
        if(auto* b=backend.load();b&&enabled.load()&&src&&Physical(*src))b->CapturePause(Physical(*src),eye);
    }
    template<class M> void Bind(M& m,U32 descriptor,U32 data) {
        auto* b=backend.load();
        if(!b||!enabled.load()||!manager||!Physical(data)||descriptor<0x100000||descriptor>0x3fffffe4)return;
        // Only the 512x256 RGB/UNSIGNED_SHORT_5_6_5 upload made from this capture.
        if(m.Read(descriptor+0x10)!=std::optional<U32>(0x1000200)||
           m.Read(descriptor+8)!=std::optional<U32>(0x1907)||m.Read(descriptor+12)!=std::optional<U32>(0x8363))return;
        auto raw=m.Read(descriptor+0x14);if(!raw||*raw<0x100000||*raw>0x3ffc0000)return;
        for(unsigned eye=0;eye<2;++eye) {
            if(m.Read(manager+4+eye*4)!=raw)continue;
            Probes probes{};
            for(unsigned i=0;i<probes.size();++i) {
                const U32 at=*raw+2*(ProbePoints[i][1]*512+ProbePoints[i][0]);
                auto value=m.Read(at&~3u);if(!value)return;
                probes[i]=(*value>>((at&2)*8))&0xffff;
            }
            b->BindPause(Physical(data),eye,probes);return;
        }
    }
    void Reset() {manager=0;if(auto* b=backend.load())b->ResetPause();}
};
// Unswizzle a few pixels solely to validate the exact upload and its Y orientation.
// Replacement is rejected if neither orientation agrees with the native raw buffer.
template<class Read16> unsigned Orientation(Read16 read,const Probes& expected) {
    for(unsigned flipped=0;flipped<2;++flipped) {
        bool match=true;
        for(unsigned i=0;i<expected.size();++i) {
            const U32 x=ProbePoints[i][0],y=flipped?255-ProbePoints[i][1]:ProbePoints[i][1];
            U32 morton=0;for(unsigned bit=0;bit<3;++bit)morton|=((x>>bit)&1)<<(2*bit)|((y>>bit)&1)<<(2*bit+1);
            auto pixel=read(2*((y/8)*512*8+(x/8)*64+morton));
            if(!pixel||*pixel!=expected[i]){match=false;break;}
        }
        if(match)return flipped+1;
    }
    return 0;
}
}
