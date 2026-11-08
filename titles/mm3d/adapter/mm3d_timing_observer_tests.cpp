#include "mm3d_timing_observer.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <source_location>
#include <unordered_map>
using namespace Oot3dNativeGame::Mm3dTiming;
static void Check(bool ok,std::source_location at=std::source_location::current()) {
    if(!ok) {std::cerr<<"check failed at "<<at.line()<<'\n';std::exit(1);}
}
// Intentionally NO Write API: neither guest data nor CPU registers can change.
struct Memory {
    std::unordered_map<uint32_t,uint32_t> words;
    unsigned reads=0;
    bool Read32(uint32_t p,uint32_t* w) {
        ++reads;auto i=words.find(p);if(i==words.end())return false;*w=i->second;return true;
    }
};
struct Cpu {std::array<uint32_t,16> r{};};
int main(int argc,char** argv) {
    Memory m; Cpu cpu; Observer o;
    o.Observe(Sites[0].Pc,cpu,m); Check(m.reads==0);
    for(const auto& s:Sites)for(unsigned i=0;i<4;++i)m.words[s.Pc+4*i]=s.Words[i];
    const auto proof=m.words;
    auto has=[](uint32_t){return true;};
    Check(o.Verify(m,has));
    for(const auto& [p,w]:proof) {
        m.words[p]=w^1;Check(!o.Verify(m,has));
        m.words.erase(p);Check(!o.Verify(m,has));m.words[p]=w;
    }
    for(const auto& s:Sites)Check(!o.Verify(m,[&](uint32_t p){return p!=s.Pc;}));
    Check(o.Verify(m,has));
    constexpr uint32_t play=0x1000000,player=0x1100000,limiter=0x1200000;
    cpu.r[0]=0xDEADBEEF;cpu.r[1]=play;
    m.words[play+0x138]=42;m.words[play+0x104]=0x15F4FC;
    m.words[VsyncCounter]=100;m.words[NativeInterval]=2;
    o.Observe(Sites[0].Pc,cpu,m);
    Check(o.Game.Valid && o.Game.State==play && o.Game.Frame==42 && o.GameSamples==1);
    m.words[play+0x138]=2;o.Observe(Sites[0].Pc,cpu,m);Check(o.EpochChanges==1);
    o.Reset();m.words[play+0x138]=1;o.Observe(Sites[0].Pc,cpu,m);Check(o.EpochChanges==1);
    cpu.r[0]=player;m.words[play+0x20DC]=player;m.words[player]=0xFF020000;
    m.words[player+0xDFC]=0x11223344;m.words[player+0x11DFC]=0xBAD; // corrected focus offset
    m.words[player+0x11DB0]=1;m.words[player+0x11DB4]=2;m.words[player+0x11DB8]=3;
    m.words[player+0x11E30]=std::bit_cast<uint32_t>(4.5f);
    m.words[player+0x11EC4]=0xABCD9602;m.words[0x7751D8+0x24]=0xFF040000;
    const auto before=m.words;const auto regs=cpu.r;
    o.Observe(Sites[1].Pc,cpu,m);
    Check(o.Player.Valid && o.Player.FocusActor==0x11223344 && o.Player.SpeedXZ==4.5f &&
          o.Player.Ledge==0x9602 && o.Player.Form==4 && o.Player.Flags[2]==3);
    Check(m.words==before && cpu.r==regs);
    for(const auto& [p,w]:before) {
        if(p<player || p>player+0x11EC4 || p==player+0x11DFC)continue;
        m.words.erase(p);o.Observe(Sites[1].Pc,cpu,m);Check(!o.Player.Valid);m.words[p]=w;
    }
    m.words[player+0x11E30]=0x7FC00000;o.Observe(Sites[1].Pc,cpu,m);Check(!o.Player.Valid);
    m.words=before;m.words[play+0x20DC]=player+4;o.Observe(Sites[1].Pc,cpu,m);Check(!o.Player.Valid);
    m.words=before;cpu.r[0]=limiter;cpu.r[4]=limiter;
    m.words[limiter+0xC8]=16;m.words[limiter+0xCC]=2;m.words[limiter+0xD0]=101;
    for(unsigned i=2;i<5;++i)o.Observe(Sites[i].Pc,cpu,m);
    Check(o.Limiter.Valid && o.WaitDecisions==1 && o.CommitIntervals[2]==1 && !o.NonNative30Samples);
    m.words[limiter+0xD0]=0xFFFFFFFF;o.Observe(Sites[3].Pc,cpu,m);Check(o.WaitDecisions==1);
    m.words[limiter+0xCC]=1000;o.Observe(Sites[4].Pc,cpu,m);Check(o.CommitIntervals[16]==1);
    m.words[NativeInterval]=1;o.Observe(Sites[4].Pc,cpu,m);Check(o.NonNative30Samples==1);
    m.words.erase(limiter+0xD0);o.Observe(Sites[4].Pc,cpu,m);Check(!o.Limiter.Valid);
    cpu.r[1]=0xFFFFFFFC;auto reads=m.reads;o.Observe(Sites[0].Pc,cpu,m);Check(!o.Game.Valid && m.reads==reads);
    o.Observe(0,cpu,m);Check(m.reads==reads);
    o.Reset();Check(!o.Game.Valid && !o.Player.Valid && !o.Limiter.Valid);
    if(argc==2) {
        std::ifstream f(argv[1],std::ios::binary);Check(bool(f));Memory rom;
        for(const auto& [p,w]:proof) {
            uint32_t actual=0;f.seekg(p-0x100000);f.read(reinterpret_cast<char*>(&actual),4);
            Check(bool(f) && actual==w);rom.words[p]=actual;
        }
        Check(o.Verify(rom,has));
    }
    std::cout<<"MM3D read-only timing observer tests passed\n";
}
