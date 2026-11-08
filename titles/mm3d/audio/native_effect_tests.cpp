// SPDX-License-Identifier: GPL-2.0-or-later
// Offline execution of the user's EUR v1.1 code through the production A32
// registry. No game loop, device, ROM bytes or borrowed OoT effect layout.
#include "oot3d_native_a32_memory.h"
#include "oot3d_native_a32_registry.h"
#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {
using namespace Oot3dNativeGame;
namespace a32 = oot3d::recomp::a32;
using U = uint32_t;
constexpr U State = 0x08000000, Channels = State + 0x200, Rings = State + 0x1000;
constexpr U Stack = State + 0x1f000, Return = 0x00fff000;
size_t checks = 0, delayCalls = 0, reverbCalls = 0;
void Check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
U Read(NativeA32Memory& m, U at) { U v=0; Check(m.Read32(at,&v),"unreadable fixture"); return v; }
void Put(NativeA32Memory& m, U at, U v) { Check(m.Write32(at,v),"unwritable fixture"); }
int Scale(int value, int gain) { return value < 0 ? -((-value * gain) >> 7) : (value * gain) >> 7; }
void Run(NativeA32Memory& m, U pc, U r0=State, U r1=Channels, U r2=0) {
    a32::GuestState cpu{}; cpu.r[0]=r0; cpu.r[1]=r1; cpu.r[2]=r2; cpu.r[13]=Stack; cpu.r[14]=Return;
    cpu.thread_pointer=State+0x1e000;
    for(U i=4;i<12;++i)cpu.r[i]=0x31415900+i;
    for(U i=16;i<32;++i)cpu.vfp[i]=0x3f800000+i;
    const auto& registry = Oot3dNativeA32Registry();
    for (unsigned blocks=0; pc!=Return; ++blocks) {
        Check(blocks < 20000,"native effect exceeded block budget");
        if(pc==0x1d9a80)++delayCalls;
        if(pc==0x1d9b70)++reverbCalls;
        const auto* block=a32::FindBlock(registry,pc);
        if (!block) { std::cerr << "missing registry block " << std::hex << pc << '\n'; }
        Check(block!=nullptr,"MM registry is required (not OoT or whole-AOT-only)");
        for(U i=0;i<block->op_count;++i)
            Check(Read(m,block->pc+4*i)==block->ops[i].raw,"registry does not match user's code");
        const auto result=a32::ExecuteBlock(*block,cpu,m);
        Check(result.fallback==a32::FallbackReason::None,"native effect fallback");
        Check(result.kind==a32::ExitKind::Branch || result.kind==a32::ExitKind::Fallthrough,
              "native effect unexpected exit");
        pc=result.pc;
    }
    Check(cpu.r[13]==Stack,"effect did not restore stack");
    for(U i=4;i<12;++i)Check(cpu.r[i]==0x31415900+i,"effect violated integer callee-save ABI");
    for(U i=16;i<32;++i)Check(cpu.vfp[i]==0x3f800000+i,"effect violated VFP callee-save ABI");
}
void Mixer(NativeA32Memory& m, bool delay, bool both=false) {
    constexpr U object=State+0x600;
    Put(m,object+0x34,delay?State:0);Put(m,object+0x38,(!delay||both)?State:0);
    // A genuinely recursive owned lock exercises native acquire/release
    // without introducing an OS scheduler into the offline effect fixture.
    Put(m,object+0x48,State+0x1e000);Put(m,object+0x4c,3);
    const auto delays=delayCalls,reverbs=reverbCalls;
    Run(m,0x1d63bc,object,0,Rings);
    Check(delayCalls-delays==size_t(delay) && reverbCalls-reverbs==size_t(!delay),"native mixer duplicated or selected the wrong effect");
    Check(Read(m,object+0x48)==State+0x1e000 && Read(m,object+0x4c)==3,"mixer did not restore recursive lock ownership");
}
void Guard(NativeA32Memory& m) {
    // Initializer stores, mixer call sites, enable fields, rings, coefficient
    // loads and cursor stores re-found by meaning; see NATIVE_AUDIO.md.
    constexpr std::pair<U,U> words[] = {
        {0x1628b0,0xe3a0203c},{0x1628b4,0xe3a03efa},{0x1628b8,0xe3a0c064},
        {0x1628c0,0xe880100c},{0x1628d0,0xedc00a03},{0x1628dc,0xed800a07},
        {0x1628e8,0xe5801014},{0x1628fc,0x006a406c},
        {0x6a406c,0x00000be0},{0x6a4070,0x00000e60},{0x6a4074,0x00000820},
        {0x1d6408,0xeb000d9c},{0x1d641c,0x1b000dd3},
        {0x1d9a80,0xe5d02056},{0x1d9ac4,0xe5d01055},{0x1d9af0,0xe590a044},
        {0x1d9b0c,0xe1c0a4d8},{0x1d9b5c,0xe5801040},
        {0x1d9b70,0xe5d02104},{0x1d9bc0,0xe591507c},{0x1d9bcc,0xe5b2c05c},
        {0x1d9bf4,0xe59110d0},{0x1d9cb4,0xe59090e8},{0x1d9cc8,0xe590a0ec},
        {0x1d9d0c,0xe58210d0},{0x1d9d30,0xe58020a0},{0x1d9d90,0xe58020c8},
        {0x1d9f74,0x46ffb000},{0x1d9f78,0xc0400000}
    };
    for (auto [at,word]:words) Check(Read(m,at)==word,"EUR audio guard mismatch");
}
void Initializer(NativeA32Memory& m) {
    Run(m,0x1628b0);
    constexpr U values[]{60,4000,100,0x3f000000,0x3ecccccd,0x6a406c,0x3f19999a,0x3ecccccd};
    for (U i=0;i<8;++i) Check(Read(m,State+4*i)==values[i],"native default field differs");
    Check((Read(m,State+32)&255)==0,"native default mode differs");
}
void Parameters(NativeA32Memory& m) {
    // This is the effect object's layout, whose user parameter block starts
    // at +4; it is NOT the standalone initializer's parameter structure.
    struct Input { U pre, decay, early; float damping; };
    constexpr Input inputs[]={{0,100,0,0.f},{4,4000,100,.4f},{5,4000,100,.95f},
                              {60,2500,60,1.2f},{100,1000,5,.7f}};
    for(const auto in:inputs)for(U mode=0;mode<2;++mode) {
        for(U at=0;at<0x108;at+=4)Put(m,State+at,0);
        Put(m,State+4,in.pre);Put(m,State+8,in.decay);Put(m,State+12,in.early);
        Put(m,State+0x10,std::bit_cast<U>(.5f));Put(m,State+0x14,std::bit_cast<U>(in.damping));
        Put(m,State+0x1c,std::bit_cast<U>(.6f));Put(m,State+0x20,std::bit_cast<U>(.4f));
        Put(m,State+0x24,mode);Put(m,State+0x28,Rings);Put(m,State+0x2c,512);
        constexpr U taps[]={3040,3680,2080};for(U i=0;i<3;++i)Put(m,State+0x30+4*i,taps[i]);
        Put(m,State+0xd0,123);Put(m,State+0xd4,U(-321));
        for(U at=0;at<520;at+=4)Put(m,Rings+at,0xa5a5a5a5);
        // Executes the native powf and zero-fill tail as well as the actual
        // VFP conversion path; no injected math or intercepted guest calls.
        Run(m,0x1d9da0);
        const auto samples=[](U ms){return 160U*U(std::max(float(ms),4.888f)/4.888f);};
        Check(Read(m,State+0x9c)==samples(in.pre),"pre-delay block quantization differs");
        Check(Read(m,State+0xa4)==samples(in.early),"early-delay block quantization differs");
        Check(Read(m,State+0xac)==taps[0] && Read(m,State+0xb0)==taps[1] && Read(m,State+0xc4)==taps[2],"native delay lengths differ");
        for(U at:{0xa0,0xa8,0xb4,0xb8,0xc8})Check(Read(m,State+at)==0,"parameter update did not reset cursors");
        const float decaySamples=(float(in.decay)*.001f)*32728.f;
        for(U i=0;i<2;++i) {
            const int gain=int(std::pow(10.f,(-3.f*float(taps[i]))/decaySamples)*128.f);
            Check(Read(m,State+0xbc+4*i)==U(gain),"native decay gain differs from independent pow model");
        }
        Check(Read(m,State+0xcc)==64 && Read(m,State+0xe0)==76 && Read(m,State+0xe4)==51,"native wet/dry/diffusion conversion differs");
        const float damping=std::min(in.damping,.95f);
        const int feed=int((mode?damping-1.f:1.f-damping)*128.f);
        const int history=int(damping*(mode?-128.f:128.f));
        Check(Read(m,State+0xe8)==U(feed) && Read(m,State+0xec)==U(history),"native damping conversion/mode/clamp differs");
        Check(Read(m,State+0xd0)==123 && Read(m,State+0xd4)==U(-321),"parameter update unexpectedly erased guest filter history");
        for(U at=0;at<512;at+=4)Check(Read(m,Rings+at)==0,"native parameter update failed to clear ring storage");
        Check(Read(m,Rings+512)==0xa5a5a5a5 && Read(m,Rings+516)==0xa5a5a5a5,"native ring clear exceeded allocation");
    }
}
void Delay(NativeA32Memory& m) {
    // Four channels, two 160-sample blocks in each delay ring.
    m.Write8(State+0x56,1); m.Write8(State+0x55,4);
    Put(m,State+0x3c,2);Put(m,State+0x40,0);
    Put(m,State+0x44,64);Put(m,State+0x48,96);Put(m,State+0x4c,32);
    std::array<std::array<int,320>,4> ring{};
    std::array<int,4> damp{};
    for (U c=0;c<4;++c) {
        Put(m,Channels+4*c,Rings+c*0x280);
        Put(m,State+0x1c+4*c,Rings+0x4000+c*0x1000);
        Put(m,State+0x2c+4*c,0);
    }
    for (U pass=0;pass<5;++pass) {
        std::array<std::array<int,160>,4> expected{};
        for (U c=0;c<4;++c) for (U i=0;i<160;++i) {
            const int input=int((i*53+c*19+pass*7)%2001)-1000;
            Put(m,Rings+c*0x280+4*i,U(input));
            auto& delayed=ring[c][(pass%2)*160+i]; expected[c][i]=delayed;
            damp[c]=((input-Scale(delayed,64))*96+damp[c]*32)>>7;
            delayed=damp[c];
        }
        if(pass%2)Mixer(m,true,pass==3);else Run(m,0x1d9a80);
        for (U c=0;c<4;++c) {
            for(U i=0;i<160;++i) Check(Read(m,Rings+c*0x280+4*i)==U(expected[c][i]),"delay output differs");
            for(U i=0;i<320;++i) Check(Read(m,Rings+0x4000+c*0x1000+4*i)==U(ring[c][i]),"delay ring differs");
            Check(Read(m,State+0x2c+4*c)==U(damp[c]),"delay filter differs");
        }
        Check(Read(m,State+0x40)==(pass+1)%2,"delay cursor wrap differs");
    }
    // Disabled kernel must return without touching even invalid channel pointers.
    m.Write8(State+0x56,0); Put(m,Channels,0);
    const U cursor=Read(m,State+0x40); Run(m,0x1d9a80);
    Check(Read(m,State+0x40)==cursor,"disabled delay advanced");
}
void Reverb(NativeA32Memory& m) {
    m.Write8(State+0x104,1);
    constexpr U ptrs[]{0x3c,0x4c,0x5c,0x60,0x7c};
    constexpr U lengths[]{0x9c,0xa4,0xac,0xb0,0xc4};
    constexpr U cursors[]{0xa0,0xa8,0xb4,0xb8,0xc8};
    std::array<std::array<std::array<int,320>,5>,2> rings{};
    std::array<int,2> damping{};
    for(U n=0;n<5;++n){Put(m,State+lengths[n],320);Put(m,State+cursors[n],0);}
    for(U at:{0xbc,0xc0,0xcc,0xe0,0xe4})Put(m,State+at,64);
    Put(m,State+0xe8,112);Put(m,State+0xec,U(-16));
    for(U c=0;c<2;++c) {
        Put(m,Channels+4*c,Rings+c*0x280);
        Put(m,State+0xd0+4*c,0);
        for(U n=0;n<5;++n) {
            const U stride=(n==2||n==3)?8:4;
            Put(m,State+ptrs[n]+c*stride,Rings+0x2000+(c*5+n)*0x1000);
            for(U i=0;i<320;++i){const int v=int((i*37+c*11+n*71)%401)-200;rings[c][n][i]=v;Put(m,Rings+0x2000+(c*5+n)*0x1000+4*i,U(v));}
        }
    }
    for(U pass=0;pass<5;++pass) {
        std::array<std::array<int,160>,2> expected{};
        for(U c=0;c<2;++c)for(U i=0;i<160;++i) {
            const int input=int((i*53+c*19+pass*7)%2001)-1000;
            Put(m,Rings+c*0x280+4*i,U(input));const U at=(pass%2)*160+i;
            auto& r=rings[c];const int primary=r[0][at],secondary=r[1][at],a=r[2][at],b=r[3][at],feedback=r[4][at];
            r[0][at]=r[1][at]=input;r[2][at]=Scale(a,64)+secondary;r[3][at]=secondary+Scale(b,64);
            r[4][at]=a-b+Scale(feedback,64);
            const int diff=feedback-Scale(r[4][at],64);
            damping[c]=(diff*112+damping[c]*-16)>>7;
            expected[c][i]=(primary*64+damping[c]*64)>>7;
        }
        // Memory snapshot/restore at the effect boundary: no host-only tail.
        auto restored=m;
        if(pass%2){Mixer(m,false);Mixer(restored,false);}else{Run(m,0x1d9b70);Run(restored,0x1d9b70);}
        for(U c=0;c<2;++c) {
            for(U i=0;i<160;++i) {
                const U at=Rings+c*0x280+4*i;
                Check(Read(m,at)==U(expected[c][i]),"reverb output differs");
                Check(Read(m,at)==Read(restored,at),"restored reverb output differs");
            }
            for(U n=0;n<5;++n)for(U i=0;i<320;++i) {
                const U at=Rings+0x2000+(c*5+n)*0x1000+4*i;
                Check(Read(m,at)==U(rings[c][n][i]),"reverb ring differs");
                Check(Read(m,at)==Read(restored,at),"restored reverb tail differs");
            }
            Check(Read(m,State+0xd0+4*c)==U(damping[c]),"reverb damping differs");
            Check(Read(m,State+0xd0+4*c)==Read(restored,State+0xd0+4*c),"restored filter differs");
        }
        for(U at:cursors)Check(Read(m,State+at)==((pass+1)%2)*160,"reverb cursor wrap differs");
    }
    m.Write8(State+0x104,0);m.Write8(State+0x100,1);Put(m,Channels,0);
    const U before=Read(m,State+0xa0);Run(m,0x1d9b70);
    Check(Read(m,State+0xa0)==before,"OoT enable field leaked into MM");
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("usage: mm3d_native_effect_tests <user EUR code.bin>");
        std::ifstream file(argv[1],std::ios::binary);
        Check(bool(file),"cannot open code.bin");
        const std::vector<uint8_t> code((std::istreambuf_iterator<char>(file)),{});
        NativeA32Memory m;std::string error;
        Check(m.MapRegion({"code",0x100000,code.size(),false,true,code},&error),error.c_str());
        Check(m.MapRegion({"fixture",State,0x20000,true,false,{}},&error),error.c_str());
        Guard(m); Initializer(m); Parameters(m); Delay(m);
        // Fresh zeroed fixture, preserving only the caller's immutable code.
        Check(m.UnmapRegion(State,0x20000,&error),error.c_str());
        Check(m.MapRegion({"fixture",State,0x20000,true,false,{}},&error),error.c_str());
        Reverb(m);
        std::cout << "MM native initializer/parameters/powf/ABI/mixer/delay/reverb/restore: " << checks << " checks passed\n";
        return 0;
    } catch(const std::exception& e) {std::cerr << "FAIL: " << e.what() << '\n';return 1;}
}
