#include "mm3d_frame_pacing.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <source_location>
#include <unordered_map>
#include <vector>
using namespace Oot3dNativeGame::Mm3dTiming;
namespace {
void Check(bool ok,std::source_location at=std::source_location::current()) {
    if(!ok) {std::cerr<<"pacing check failed at "<<at.line()<<'\n';std::exit(1);}
}
constexpr uint32_t State=0x1200000;
struct Memory final:oot3d::recomp::a32::MemoryBus {
    std::unordered_map<uint32_t,uint32_t> words;
    std::vector<uint32_t> writes;
    uint32_t failWrite=0;
    bool Read32(uint32_t p,uint32_t* w) override {
        auto i=words.find(p);if(i==words.end())return false;*w=i->second;return true;
    }
    bool Write32(uint32_t p,uint32_t w) override {
        Check(p==State+0xC8 || p==State+0xCC || p==State+0xD0);
        if(p==failWrite)return false;
        writes.push_back(p);words[p]=w;return true;
    }
};
struct Cpu {std::array<uint32_t,16> r{};};
}
int main(int argc,char** argv) {
    Memory m;Cpu cpu;FramePacing policy;
    policy.Observe(Sites[2].Pc,cpu,m);Check(m.writes.empty());
    for(unsigned j=2;j<Sites.size();++j)for(unsigned i=0;i<4;++i)m.words[Sites[j].Pc+4*i]=Sites[j].Words[i];
    for(const auto& s:PacingProofs)for(unsigned i=0;i<4;++i)m.words[s.Pc+4*i]=s.Words[i];
    for(const auto& [p,w]:PacingLiterals)m.words[p]=w;
    const auto proof=m.words;auto has=[](uint32_t){return true;};
    Check(policy.Verify(m,has));
    for(const auto& [p,w]:proof) {
        m.words[p]=w^1;Check(!policy.Verify(m,has));
        policy.Observe(Sites[2].Pc,cpu,m);Check(m.writes.empty());
        m.words.erase(p);Check(!policy.Verify(m,has));m.words[p]=w;
    }
    for(unsigned i=2;i<Sites.size();++i)Check(!policy.Verify(m,[&](uint32_t p){return p!=Sites[i].Pc;}));
    Check(policy.Verify(m,has));
    cpu.r[0]=cpu.r[4]=State;cpu.r[6]=0x6A39F8;const auto regs=cpu.r;
    m.words[ManagerPointer]=State;m.words[NativeInterval]=2;m.words[VsyncCounter]=100;
    m.words[State+0xC8]=16;m.words[State+0xCC]=8;m.words[State+0xD0]=102;
    // No write on a restore entering the middle of an unobserved loop.
    policy.Observe(Sites[3].Pc,cpu,m);policy.Observe(Sites[4].Pc,cpu,m);
    Check(m.writes.empty() && policy.Unpaired==2);
    policy.Observe(Sites[2].Pc,cpu,m);Check(m.writes.empty());
    policy.Observe(Sites[3].Pc,cpu,m);Check(m.words[State+0xD0]==101);
    m.words[VsyncCounter]=101;policy.Observe(Sites[3].Pc,cpu,m);
    Check(m.words[State+0xD0]==101 && policy.Stats().FramePacingDeadlineWrites==1);
    policy.Observe(Sites[4].Pc,cpu,m);
    Check(m.words[State+0xC8]==2 && m.words[State+0xCC]==2);
    policy.Observe(Sites[2].Pc,cpu,m);Check(policy.OutputSamples==1 && !policy.OutputViolations);
    Check(cpu.r==regs && m.words[NativeInterval]==2 && m.words[ManagerPointer]==State);
    for(const auto& [p,w]:proof)Check(m.words[p]==w);
    // Long stall: the native commit arithmetic must still clamp its output.
    m.words[VsyncCounter]=1000;policy.Observe(Sites[3].Pc,cpu,m);
    Check(m.words[State+0xD0]==1001);
    m.words[VsyncCounter]=1005;policy.Observe(Sites[4].Pc,cpu,m);
    const auto raw=m.words[State+0xCC]+1005-m.words[State+0xD0]+m.words[NativeInterval];
    m.words[State+0xCC]=std::min(raw,m.words[State+0xC8]);
    policy.Observe(Sites[2].Pc,cpu,m);Check(policy.OutputSamples==2 && !policy.OutputViolations);
    // Native uint32 rollover, including the native signed-comparison boundary.
    for(uint32_t vsync:{0x7FFFFFFEU,0x7FFFFFFFU,0xFFFFFFFFU,0U}) {
        policy.Observe(Sites[2].Pc,cpu,m);m.words[VsyncCounter]=vsync;
        policy.Observe(Sites[3].Pc,cpu,m);Check(m.words[State+0xD0]==vsync+1U);
    }
    auto writes=m.writes.size();policy.Reset();
    policy.Observe(Sites[3].Pc,cpu,m);policy.Observe(Sites[4].Pc,cpu,m);Check(m.writes.size()==writes);
    // Revalidate owner, rate, bounded fields and stage-specific registers.
    const auto good=m.words;
    for(const auto [p,bad]:std::array<std::pair<uint32_t,uint32_t>,5>{{
        {ManagerPointer,State+4},{NativeInterval,1},{NativeInterval,3},{State+0xC8,15},{State+0xCC,0xFFFFFFFF}}}) {
        m.words=good;m.words[p]=bad;policy.Observe(Sites[2].Pc,cpu,m);
        policy.Observe(Sites[3].Pc,cpu,m);policy.Observe(Sites[4].Pc,cpu,m);Check(m.writes.size()==writes);
    }
    for(uint32_t p:{ManagerPointer,NativeInterval,State+0xC8,State+0xCC}) {
        m.words=good;m.words.erase(p);policy.Observe(Sites[2].Pc,cpu,m);Check(m.writes.size()==writes);
    }
    m.words=good;policy.Observe(Sites[2].Pc,cpu,m);cpu.r[6]=0;
    policy.Observe(Sites[3].Pc,cpu,m);Check(m.writes.size()==writes);cpu.r=regs;
    for(uint32_t bad:{0U,State+1,0xFFFFFFFCU}) {
        cpu.r[0]=bad;policy.Observe(Sites[2].Pc,cpu,m);Check(m.writes.size()==writes);
    }
    cpu.r=regs;
    // Read/write failures must stop execution, never continue a partial commit.
    for(bool writeFailure:{false,true}) {
        m.words=good;m.words[State+0xD0]=7;m.failWrite=writeFailure?State+0xD0:0;
        policy.Observe(Sites[2].Pc,cpu,m);
        if(!writeFailure)m.words.erase(State+0xD0);
        bool threw=false;try {policy.Observe(Sites[3].Pc,cpu,m);}catch(const std::runtime_error&) {threw=true;}
        Check(threw);
    }
    Check(!policy.Stats().UpdateRateWrites && !policy.Stats().GameStateUpdatesObserved);
    if(argc==2) {
        std::ifstream f(argv[1],std::ios::binary);Check(bool(f));Memory rom;
        for(const auto& [p,w]:proof) {
            uint32_t actual=0;f.seekg(p-0x100000);f.read(reinterpret_cast<char*>(&actual),4);
            if(actual!=w)std::cerr<<"guard mismatch "<<std::hex<<p<<'\n';
            Check(bool(f) && actual==w);rom.words[p]=actual;
        }
        Check(policy.Verify(rom,has));
    }
    std::cout<<"MM3D native30 bounded frame pacing tests passed\n";
}
