#pragma once

#include "mm3d_hook_sites.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace Oot3dNativeGame::Mm3dTiming {
// EUR v1.1, MAP_4 G5.01/.02/.04. Observers only: native instructions,
// the three-day clock, folded simulation constants and interval stay untouched.
inline constexpr std::array<Mm3dHookSite, 5> Sites{{
    {"GameState_Update", 0x106750, {0xE92D5FF3,0xE1A04000,0xED2D8B06,0xE24DDE76}},
    {"Player_Update", 0x1F9C70, {0xE92D4FF0,0xE1A04000,0xE2807A11,0xE24DD014}},
    {"LimiterBegin", 0x124EC0, {0xE92D41F0,0xE1A04000,0xE1A06002,0xE5D00004}},
    {"LimiterDecision", 0x124F6C, {0xE5960010,0xE59410D0,0xE1510000,0xCAFFFFF9}},
    {"LimiterCommit", 0x124F7C, {0xE1C40CDC,0xE5B62010,0xE596300C,0xE0421001}},
}};
inline constexpr uint32_t VsyncCounter = 0x6A3A08;
inline constexpr uint32_t NativeInterval = 0x6A3A14;

struct GameSnapshot {
    uint32_t State=0, Frame=0, Main=0, Vsync=0, Interval=0;
    bool Valid=false;
};
struct PlayerSnapshot {
    uint32_t Player=0, Play=0, Frame=0, FocusActor=0, Ledge=0, Form=0;
    std::array<uint32_t,3> Flags{};
    float SpeedXZ=0;
    bool Valid=false;
};
struct LimiterSnapshot {
    uint32_t State=0, Maximum=0, Interval=0, Deadline=0, Vsync=0, Native=0;
    bool Valid=false;
};
class Observer {
public:
    template<class Memory, class HasBlock>
    bool Verify(Memory& memory, HasBlock hasBlock) {
        mVerified=false;
        Reset();
        for (const auto& site:Sites)
            if (!hasBlock(site.Pc) || !Mm3dHookSiteMatches(site,
                [&](uint32_t p,uint32_t& w){return memory.Read32(p,&w);})) return false;
        return mVerified=true;
    }
    bool Verified() const { return mVerified; }
    // Preserve aggregate counters, but never compare across a restore/reset.
    void Reset() { Game={}; Player={}; Limiter={}; ++Resets; }

    template<class Registers, class Memory>
    void Observe(uint32_t pc, const Registers& cpu, Memory& memory) {
        if (!mVerified) return;
        if (pc==Sites[0].Pc) {
            ++GameCalls;
            GameSnapshot next;
            next.State=cpu.r[1]; // r0 is graphics context, NOT GameState.
            if (!Pointer(next.State,0x13C) ||
                !memory.Read32(next.State+0x138,&next.Frame) ||
                !memory.Read32(next.State+0x104,&next.Main) ||
                !memory.Read32(VsyncCounter,&next.Vsync) ||
                !memory.Read32(NativeInterval,&next.Interval)) {
                Game={}; ++ReadFailures; return;
            }
            if (Game.Valid && (Game.State!=next.State || next.Frame<Game.Frame)) ++EpochChanges;
            next.Valid=true; Game=next; ++GameSamples;
        } else if (pc==Sites[1].Pc) {
            ++PlayerCalls;
            PlayerSnapshot next;
            next.Player=cpu.r[0]; next.Play=cpu.r[1];
            uint32_t owner=0, identity=0, speed=0, form=0;
            if (!Pointer(next.Player,0x12CB8) || !Pointer(next.Play,0x20E0) ||
                !memory.Read32(next.Play+0x20DC,&owner) || owner!=next.Player ||
                !memory.Read32(next.Player,&identity) || (identity&0xFFFFFF)!=0x020000 ||
                !memory.Read32(next.Play+0x138,&next.Frame) ||
                !memory.Read32(next.Player+0xDFC,&next.FocusActor) ||
                !memory.Read32(next.Player+0x11DB0,&next.Flags[0]) ||
                !memory.Read32(next.Player+0x11DB4,&next.Flags[1]) ||
                !memory.Read32(next.Player+0x11DB8,&next.Flags[2]) ||
                !memory.Read32(next.Player+0x11E30,&speed) ||
                !memory.Read32(next.Player+0x11EC4,&next.Ledge) ||
                !memory.Read32(0x7751D8+0x24,&form) ||
                !std::isfinite(std::bit_cast<float>(speed))) {
                Player={}; ++ReadFailures; return;
            }
            next.SpeedXZ=std::bit_cast<float>(speed);
            next.Form=(form>>16)&0xFF; // SaveContext form at +0x26.
            next.Ledge&=0xFFFF; // type/timer bytes only, no adjacent state.
            next.Valid=true; Player=next; ++PlayerSamples;
        } else if (pc==Sites[2].Pc || pc==Sites[3].Pc || pc==Sites[4].Pc) {
            const unsigned stage=pc==Sites[2].Pc?0:pc==Sites[3].Pc?1:2;
            ++LimiterCalls[stage];
            LimiterSnapshot next;
            next.State=stage==0?cpu.r[0]:cpu.r[4];
            if (!Pointer(next.State,0xD4) ||
                !memory.Read32(next.State+0xC8,&next.Maximum) ||
                !memory.Read32(next.State+0xCC,&next.Interval) ||
                !memory.Read32(next.State+0xD0,&next.Deadline) ||
                !memory.Read32(VsyncCounter,&next.Vsync) ||
                !memory.Read32(NativeInterval,&next.Native)) {
                Limiter={}; ++ReadFailures; return;
            }
            // Match the guest signed BGT, not an invented modulo clock policy.
            if (stage==1 && std::bit_cast<int32_t>(next.Deadline)>
                           std::bit_cast<int32_t>(next.Vsync)) ++WaitDecisions;
            if (stage==2) ++CommitIntervals[next.Interval<16?next.Interval:16];
            if (next.Native!=2) ++NonNative30Samples;
            next.Valid=true; Limiter=next; ++LimiterSamples[stage];
        }
    }
    GameSnapshot Game;
    PlayerSnapshot Player;
    LimiterSnapshot Limiter;
    uint64_t GameCalls=0, GameSamples=0, PlayerCalls=0, PlayerSamples=0;
    uint64_t ReadFailures=0, EpochChanges=0, Resets=0, WaitDecisions=0, NonNative30Samples=0;
    std::array<uint64_t,3> LimiterCalls{}, LimiterSamples{};
    // Bounded pre-instruction receipt: bin16 means >=16, NOT committed output.
    std::array<uint64_t,17> CommitIntervals{};
private:
    static bool Pointer(uint32_t p,uint32_t size) { return p!=0 && !(p&3) && p<=UINT32_MAX-size; }
    bool mVerified=false;
};
} // namespace Oot3dNativeGame::Mm3dTiming
