#pragma once
#include <cstdint>
#include <limits>

namespace ThreeDsRecomp {
// Pure cursor planning for a downward guest arena. No wrap/recycling: every
// outstanding allocation keeps a distinct address until the native epoch resets.
struct DownwardArena {
    uint32_t Base = 0, Size = 0, Record = 0;
    bool Valid() const {
        return Base && Size && Record && Size % Record == 0 &&
            uint64_t(Base) + Size <= std::numeric_limits<uint32_t>::max();
    }
    bool ContainsCursor(uint32_t cursor) const {
        return Valid() && cursor >= Base && uint64_t(cursor) <= uint64_t(Base) + Size &&
            (Base + Size - cursor) % Record == 0;
    }
    uint32_t Top() const { return Base + Size; }
};
enum class ArenaAllocation { Invalid, Native, Overflow, Relocate, Exhausted };
inline ArenaAllocation PlanDownwardAllocation(DownwardArena native, DownwardArena overflow,
                                              uint32_t head, uint32_t tail) {
    if (!native.Valid() || !overflow.Valid() || native.Record != overflow.Record ||
        !(native.Top() <= overflow.Base || overflow.Top() <= native.Base) ||
        head < native.Base || head > native.Top()) return ArenaAllocation::Invalid;
    if (native.ContainsCursor(tail)) {
        if (tail < head) return ArenaAllocation::Invalid;
        return tail - head >= native.Record ? ArenaAllocation::Native : ArenaAllocation::Relocate;
    }
    if (!overflow.ContainsCursor(tail)) return ArenaAllocation::Invalid;
    return tail - overflow.Base >= overflow.Record ? ArenaAllocation::Overflow : ArenaAllocation::Exhausted;
}
} // namespace ThreeDsRecomp
