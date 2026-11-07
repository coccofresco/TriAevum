#pragma once
#include "../adapter/mm3d_hook_sites.h"
#include "../../../tools/three_ds/downward_arena.h"
#include <cstdint>

namespace Oot3dNativeGame {
inline constexpr Mm3dHookSite kMm3dLightAllocatorSite{
    "LightContext_NewLights", 0x001EFC7C, {0xE1A03000, 0xE52D4004, 0xE591004C, 0xE3520000}};
inline constexpr Mm3dHookSite kMm3dLightEpochSite{
    "Play_Draw", 0x0015E9EC, {0xE92D4FF0, 0xE1A04000, 0xE2807B33, 0xE3A00000}};

inline constexpr std::array kMm3dArenaDataProofs{
    Mm3dHookSite{"light allocation",0x1EFC8C,{0xE3A02000,0xE2400C01,0x0A00000D,0xE5D34011}},
    Mm3dHookSite{"light tail store",0x1EFCA0,{0xE5D33013,0xE581004C,0xE5C04008,0xE5C04004}},
    Mm3dHookSite{"alternate light tail store",0x1EFCD0,{0xE5D3400E,0xE5D3C00F,0xE5D33010,0xE581004C}},
    Mm3dHookSite{"arena reset tail",0x1393A8,{0xE0822001,0xE4801004,0xE8800006,0xE12FFF1E}},
};

// EUR v1.1 P7: retained gameplay savestates' gfx+40/44/48/4C and
// LightContext_NewLights's downward 0x100-byte allocation. Refuse other layouts.
// Native capacity is 412, not OoT's 128. The head may grow upward: admission
// always compares the live head rather than assuming all capacity is available.
class Mm3dLightArena {
public:
    static constexpr ThreeDsRecomp::DownwardArena Native(unsigned bank) {
        return {0x0072F91C + bank * 0x1FE00, 0x19C00, 0x100};
    }
    static constexpr uint32_t MappingBase = 0x06100000, MappingSize = 0x102000;
    // Native double buffering must also apply to overflow: the previous bank's
    // records may still be referenced. First page is an identity header.
    static constexpr ThreeDsRecomp::DownwardArena Overflow(unsigned bank) {
        return {MappingBase + 0x1000 + bank * 0x81000, 0x80000, 0x100};
    }
    static constexpr uint32_t Magic0 = 0x4D4D4C41, Magic1 = 0x00000100;
    enum class Result { Inert, Native, Overflow, Relocated, Invalid, Unavailable, Exhausted };
    struct Statistics {
        uint64_t NativeAllocations = 0, OverflowAllocations = 0, Relocations = 0;
        uint64_t Invalid = 0, Unavailable = 0, Exhausted = 0;
        uint32_t BanksSeen = 0;
    };
    template<class M> bool Verify(M& memory) {
        const auto read = [&](uint32_t a, uint32_t& w) { return memory.Read32(a, &w); };
        mVerified = Mm3dHookSiteMatches(kMm3dLightAllocatorSite, read) &&
                    Mm3dHookSiteMatches(kMm3dLightEpochSite, read);
        // GameState_Update constructs bank base = 72F91C + (gfx[60]&1)*1FE00,
        // then initializes gfx+40 with size19C00. Data-proof only, not new hooks.
        constexpr uint32_t init[] = {0xE5D40060, 0xE3A02B67, 0xE2000001, 0xE0600400,
                                      0xE0815480, 0xE2840040, 0xE1A01005, 0xEB00CAF0};
        for (unsigned i=0;i<8;++i) {
            uint32_t w=0;
            mVerified = memory.Read32(0x1067BC+4*i,&w) && w==init[i] && mVerified;
        }
        constexpr Mm3dHookSite reset{"arena reset", 0x1393A0, {0xE1A00000,0xE4802004,0xE0822001,0xE4801004}};
        uint32_t base=0;
        mVerified = Mm3dHookSiteMatches(reset,read) && memory.Read32(0x106B90,&base) &&
                    base==Native(0).Base && mVerified;
        for (const auto& site : kMm3dArenaDataProofs)
            mVerified = Mm3dHookSiteMatches(site, read) && mVerified;
        return mVerified;
    }
    bool Verified() const { return mVerified; }
    void SetEnabled(bool enabled) { mEnabled = enabled; }
    const Statistics& Stats() const { return mStats; }

    // Called BEFORE the existing lighting observer, at its already registered
    // allocator PC. That observer must invalidate the actual destination record.
    // No registers or arena header fields other than tail are changed.
    template<class M, class EnsureMapping>
    Result BeforeAllocation(M& memory, uint32_t gfx, EnsureMapping&& ensureMapping) {
        if (!mVerified) return Result::Inert;
        if (!gfx || (gfx & 3) || gfx > UINT32_MAX - 0x50) return Bad();
        uint32_t size = 0, base = 0, head = 0, tail = 0;
        if (!memory.Read32(gfx + 0x40, &size) || !memory.Read32(gfx + 0x44, &base) ||
            !memory.Read32(gfx + 0x48, &head) || !memory.Read32(gfx + 0x4C, &tail)) return Bad();
        // Finish an already-active overflow epoch even if EDD is turned off.
        // The native reset, not a host frame counter or settings change, reclaims it.
        if (!mEnabled && !Overflow(0).ContainsCursor(tail) && !Overflow(1).ContainsCursor(tail)) return Result::Inert;
        const unsigned bank = base == Native(0).Base ? 0 : 1;
        const auto native = Native(bank), overflow = Overflow(bank);
        if (size != native.Size || base != native.Base || (head & 3)) return Bad();
        switch (ThreeDsRecomp::PlanDownwardAllocation(native, overflow, head, tail)) {
        case ThreeDsRecomp::ArenaAllocation::Native:
            mStats.BanksSeen |= 1U << bank;
            ++mStats.NativeAllocations; return Result::Native;
        case ThreeDsRecomp::ArenaAllocation::Overflow:
            if (!ensureMapping(false)) return Unavailable();
            ++mStats.OverflowAllocations; return Result::Overflow;
        case ThreeDsRecomp::ArenaAllocation::Relocate:
            if (!ensureMapping(true) || !memory.Write32(gfx + 0x4C, overflow.Top())) return Unavailable();
            ++mStats.Relocations; ++mStats.OverflowAllocations; return Result::Relocated;
        case ThreeDsRecomp::ArenaAllocation::Exhausted:
            ++mStats.Exhausted; return Result::Exhausted; // NEVER wrap into live records
        default: return Bad();
        }
    }
private:
    Result Bad() { ++mStats.Invalid; return Result::Invalid; }
    Result Unavailable() { ++mStats.Unavailable; return Result::Unavailable; }
    bool mVerified = false, mEnabled = false;
    Statistics mStats;
};
} // namespace Oot3dNativeGame
