#pragma once

#include "mm3d_timing_observer.h"
#include "oot3d_native_frame_rate.h"
#include "a32_runtime.h"
#include <stdexcept>
#include <utility>

namespace Oot3dNativeGame::Mm3dTiming {
// EUR v1.1: native ADD/STR VSync counter (no OoT modulo-0x7fffffff reset).
inline constexpr NativeFramePacingLayout PacingLayout{0xC8,0xCC,0xD0,VsyncCounter,uint64_t{1}<<32};
inline constexpr uint32_t ManagerPointer=0x6A3A4C;
// Data-flow proofs, not new observer PCs. Getter, deadline init, constructor,
// VSync increment and limiter clamp are independently guarded.
inline constexpr std::array<Mm3dHookSite,7> PacingProofs{{
    {"ManagerGetter",0x1D6F7C,{0xE59F0004,0xE5900004,0xE12FFF1E,0x006A3A48}},
    {"DeadlineInit",0x108F5C,{0xE5901010,0xE590001C,0xE0800001,0xE58B00D0}},
    {"IntervalInit",0x109048,{0xE280001C,0xE3A01010,0xE8800022,0xE280000C}},
    {"NativeIntervalInit",0x1090C8,{0xE5A06010,0xE580500C,0xE1A00004,0xE8BD81F0}},
    {"VsyncIncrement",0x115A08,{0xE5912010,0xE2822001,0xE5812010,0xE5912008}},
    {"IntervalClamp",0x124F98,{0xE59410C8,0xE1500001,0xC1A00001,0xE5D41005}},
    {"IntervalStore",0x124FA8,{0xE58400CC,0xE3510000,0x0A000003,0xE2410001}},
}};
inline constexpr std::array<std::pair<uint32_t,uint32_t>,6> PacingLiterals{{
    {0x108F7C,0x6A39F8}, {0x109000,0xE3A05002}, {0x1090DC,0x6A39F8},
    {0x115A2C,0x6A39F8}, {0x124FE8,0x6A39F8}, {0x124F58,0xE59F6088},
}};

// The shared OoT presentation policy, with MM facts and native30 admission.
// Never calls ApplyNativeTimeScaleAtGameStateUpdate: MM has no such rate field.
class FramePacing {
public:
    FramePacing():mPolicy(ResolveNativeFrameRateContract(GameplayTimingMode::Native30NoInterpolation,60),PacingLayout) {}
    template<class HasBlock> bool Verify(oot3d::recomp::a32::MemoryBus& memory,HasBlock hasBlock) {
        mVerified=false; Reset();
        for(unsigned i=2;i<Sites.size();++i)
            if(!hasBlock(Sites[i].Pc) || !Matches(Sites[i],memory))return false;
        for(const auto& site:PacingProofs)if(!Matches(site,memory))return false;
        for(const auto& [p,expected]:PacingLiterals) {
            uint32_t word=0;if(!memory.Read32(p,&word) || word!=expected)return false;
        }
        return mVerified=true;
    }
    bool Verified() const {return mVerified;}
    void Reset() {mActive=false;mCommitted=false;mState=0;mPolicy.ResetTemporalState();++Resets;}
    const NativeFrameRatePolicyStats& Stats() const {return mPolicy.Stats();}
    template<class Registers> void Observe(uint32_t pc,const Registers& cpu,oot3d::recomp::a32::MemoryBus& memory) {
        if(!mVerified || (pc!=Sites[2].Pc && pc!=Sites[3].Pc && pc!=Sites[4].Pc))return;
        const bool begin=pc==Sites[2].Pc;
        const uint32_t state=begin?cpu.r[0]:cpu.r[4];
        // Restore may resume inside the loop. Do not invent an unobserved begin
        // or consume another frame's decision; wait for the next real entry.
        if(!begin && !mActive) {++Unpaired;return;}
        uint32_t manager=0,native=0,maximum=0,interval=0;
        if(!state || (state&3) || state>UINT32_MAX-0xD4 ||
           !memory.Read32(ManagerPointer,&manager) || manager!=state ||
           !memory.Read32(NativeInterval,&native) || native!=2 ||
           !memory.Read32(state+0xC8,&maximum) || (maximum!=16 && maximum!=2) ||
           !memory.Read32(state+0xCC,&interval) || interval>16 ||
           (!begin && (state!=mState || cpu.r[6]!=0x6A39F8))) {
            ++Rejected;Reset();return;
        }
        if(begin) {
            // A prior commit has now actually run; this is an output receipt,
            // unlike Observer::CommitIntervals (pre-instruction inputs).
            if(mActive && mState==state && mCommitted) {
                ++OutputSamples;
                if(maximum!=2 || interval>2)++OutputViolations;
            }
            mState=state;mActive=true;mCommitted=false;
            mPolicy.BeginFramePacing(state);
        } else {
            const bool commit=pc==Sites[4].Pc;
            const bool ok=commit?mPolicy.ApplyFramePacingInterval(state,memory):
                                 mPolicy.ApplyFramePacingDeadline(state,memory);
            if(!ok)throw std::runtime_error("MM3D frame pacing: "+mPolicy.LastError());
            if(commit)mCommitted=true;
        }
    }
    uint64_t Resets=0,Rejected=0,Unpaired=0,OutputSamples=0,OutputViolations=0;
private:
    static bool Matches(const Mm3dHookSite& site,oot3d::recomp::a32::MemoryBus& memory) {
        return Mm3dHookSiteMatches(site,[&](uint32_t p,uint32_t& w){return memory.Read32(p,&w);});
    }
    NativeFrameRatePolicy mPolicy;
    uint32_t mState=0;
    bool mVerified=false,mActive=false,mCommitted=false;
};
} // namespace Oot3dNativeGame::Mm3dTiming
