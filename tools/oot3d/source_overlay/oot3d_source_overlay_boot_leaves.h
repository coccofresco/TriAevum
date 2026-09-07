#pragma once

#include "oot3d_source_overlay_abi.h"

#include <array>
#include <cstdint>

namespace Oot3dSourceOverlay::BootLeaves {

inline constexpr std::uint32_t kMemsetEntry = 0x00303B14U;
inline constexpr std::uint32_t kIdentityDestructorEntry = 0x003FFB7CU;

inline constexpr std::array<Oot3dSourceOverlayEntry, 2> kEntries{{
    {kMemsetEntry, 0U, "boot.leaves", "memset"},
    {kIdentityDestructorEntry, 0U, "boot.leaves",
     "MemoryStreamDestructor"},
}};

struct Stats {
    std::uint64_t Calls = 0U;
    std::uint64_t Handled = 0U;
    std::uint64_t Rejected = 0U;
};

int Execute(std::uint32_t entry, Oot3dSourceOverlayGuestState* state,
            Oot3dSourceOverlayExecutionResult* result,
            const Oot3dSourceOverlayHostApi* host);
Stats GetStats();

} // namespace Oot3dSourceOverlay::BootLeaves
