#include "mm3d_light_arena.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <source_location>
#include <unordered_map>
#include <vector>
using namespace Oot3dNativeGame;
using A = Mm3dLightArena;
using R = A::Result;
static void Check(bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) { std::cerr << "check failed at " << at.line() << '\n'; std::exit(1); }
}
struct Memory {
    std::unordered_map<uint32_t,uint32_t> words;
    unsigned reads=0, writes=0;
    bool failWrite=false;
    bool Read32(uint32_t p,uint32_t* w) { ++reads; const auto i=words.find(p); if(i==words.end())return false; *w=i->second;return true; }
    bool Write32(uint32_t p,uint32_t w) { ++writes; if(failWrite)return false;words[p]=w;return true; }
};
static constexpr uint32_t Gfx=0xFFFFD88;
static void Contract(Memory& m) {
    for(const auto& s:{kMm3dLightAllocatorSite,kMm3dLightEpochSite})
        for(unsigned i=0;i<4;++i)m.words[s.Pc+i*4]=s.Words[i];
    const uint32_t init[]={0xE5D40060,0xE3A02B67,0xE2000001,0xE0600400,0xE0815480,0xE2840040,0xE1A01005,0xEB00CAF0};
    for(unsigned i=0;i<8;++i)m.words[0x1067BC+i*4]=init[i];
    const uint32_t reset[]={0xE1A00000,0xE4802004,0xE0822001,0xE4801004};
    for(unsigned i=0;i<4;++i)m.words[0x1393A0+i*4]=reset[i];
    m.words[0x106B90]=A::Native(0).Base;
    for (const auto& s : kMm3dArenaDataProofs)
        for(unsigned i=0;i<4;++i)m.words[s.Pc+i*4]=s.Words[i];
}
static void Arena(Memory& m,unsigned bank,uint32_t tail) {
    m.words[Gfx+0x40]=A::Native(bank).Size;
    m.words[Gfx+0x44]=m.words[Gfx+0x48]=A::Native(bank).Base;
    m.words[Gfx+0x4C]=tail;
}
int main(int argc,char** argv) {
    Memory m; A a; unsigned maps=0;
    auto map=[&](bool){++maps;return true;};
    Check(a.BeforeAllocation(m,Gfx,map)==R::Inert && !m.reads && !m.writes && !maps);
    Contract(m); Check(a.Verify(m));
    // Every guard word must independently fail, including unreadable words.
    const auto proof=m.words;
    for(const auto& [p,w]:proof) {
        m.words[p]=w^1; Check(!a.Verify(m));
        m.words.erase(p); Check(!a.Verify(m));m.words[p]=w;
    }
    Check(a.Verify(m));
    Arena(m,0,A::Native(0).Base); Check(a.BeforeAllocation(m,Gfx,map)==R::Inert && !m.writes && !maps);
    a.SetEnabled(true);
    for(unsigned bank=0;bank<2;++bank) {
        Arena(m,bank,A::Native(bank).Top());
        // Exactly 412 native records, then overflow; no earlier relocation.
        for(unsigned i=0;i<412;++i) {
            Check(a.BeforeAllocation(m,Gfx,map)==R::Native);
            m.words[Gfx+0x4C]-=0x100;
        }
        Check(a.BeforeAllocation(m,Gfx,map)==R::Relocated);
        Check(m.words[Gfx+0x4C]==A::Overflow(bank).Top());
        // This is the cursor the downstream lighting observer must see.
        const uint32_t owner=m.words[Gfx+0x4C]-0x100;
        Check(owner==A::Overflow(bank).Top()-0x100);
        for(unsigned i=0;i<2048;++i) {
            Check(a.BeforeAllocation(m,Gfx,map)==R::Overflow);
            m.words[Gfx+0x4C]-=0x100;
        }
        const auto writes=m.writes;
        Check(a.BeforeAllocation(m,Gfx,map)==R::Exhausted && m.writes==writes);
        // A cursor from the other bank is not permission to reuse it.
        m.words[Gfx+0x4C]=A::Overflow(1-bank).Top();
        Check(a.BeforeAllocation(m,Gfx,map)==R::Invalid && m.writes==writes);
        Arena(m,bank,A::Native(bank).Top()); // native epoch reset
        Check(a.BeforeAllocation(m,Gfx,map)==R::Native);
    }
    Arena(m,0,A::Overflow(0).Top()); a.SetEnabled(false);
    Check(a.BeforeAllocation(m,Gfx,map)==R::Overflow); // finish in-flight epoch
    Arena(m,0,A::Native(0).Base);Check(a.BeforeAllocation(m,Gfx,map)==R::Inert);
    a.SetEnabled(true);
    Check(a.BeforeAllocation(m,Gfx,[](bool){return false;})==R::Unavailable);
    Check(m.words[Gfx+0x4C]==A::Native(0).Base);
    m.failWrite=true;Check(a.BeforeAllocation(m,Gfx,map)==R::Unavailable);m.failWrite=false;
    Check(m.words[Gfx+0x4C]==A::Native(0).Base);
    // Dynamic head reduces remaining space; an invalid/overlapping cursor fails.
    Arena(m,0,A::Native(0).Base+0x100);m.words[Gfx+0x48]+=4;
    Check(a.BeforeAllocation(m,Gfx,map)==R::Relocated);
    Arena(m,0,A::Native(0).Base+4);Check(a.BeforeAllocation(m,Gfx,map)==R::Invalid);
    Arena(m,0,A::Native(0).Base);m.words[Gfx+0x48]+=0x100;
    Check(a.BeforeAllocation(m,Gfx,map)==R::Invalid);
    Arena(m,0,A::Native(0).Top());m.words[Gfx+0x40]=0x4000;
    Check(a.BeforeAllocation(m,Gfx,map)==R::Invalid);
    m.words.erase(Gfx+0x44);Check(a.BeforeAllocation(m,Gfx,map)==R::Invalid);
    using namespace ThreeDsRecomp;
    Check(PlanDownwardAllocation({0x1000,0x100,0x100},{0x1000,0x200,0x100},0x1000,0x1100)==ArenaAllocation::Invalid);
    Check(PlanDownwardAllocation({0xFFFFFF00,0x200,0x100},A::Overflow(0),0,0)==ArenaAllocation::Invalid);
    // Optional original-image proof; fixtures alone are not revision evidence.
    if(argc==2) {
        std::ifstream f(argv[1],std::ios::binary);Check(bool(f));
        Memory rom;
        for(const auto& [p,w]:proof) {
            uint32_t word=0;f.seekg(p-0x100000);f.read(reinterpret_cast<char*>(&word),4);Check(bool(f));
            rom.words[p]=word;
        }
        Check(a.Verify(rom));
    }
    std::cout << "MM3D double-buffered light arena: PASS\n";
}
