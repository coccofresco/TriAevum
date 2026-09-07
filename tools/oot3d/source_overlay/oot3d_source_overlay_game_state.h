#pragma once

#include "oot3d_source_overlay_abi.h"

#include <array>
#include <cstdint>

namespace Oot3dSourceOverlay::GameState {

inline constexpr std::uint32_t kUpdateEntry = 0x00417014U;
inline constexpr std::uint32_t kMainReturn = 0x00417024U;

inline constexpr std::array<Oot3dSourceOverlayEntry, 2> kEntries{{
    {kUpdateEntry, 0U, "game_state.update", "GameState_Update"},
    {kMainReturn, 0U, "game_state.update", "GameState_Update::after_main"},
}};

struct Services {
    const Oot3dSourceOverlayHostApi* Host = nullptr;
    void (*ResetSourceMemoryFault)() = nullptr;
    bool (*SourceMemoryFaulted)() = nullptr;
};

struct Stats {
    std::uint64_t UpdateCalls = 0U;
    std::uint64_t UpdateHandled = 0U;
    std::uint64_t MainReturnCalls = 0U;
    std::uint64_t MainReturnHandled = 0U;
};

int Execute(std::uint32_t entry, Oot3dSourceOverlayGuestState* state,
            Oot3dSourceOverlayExecutionResult* result,
            const Services& services);
Stats GetStats();

} // namespace Oot3dSourceOverlay::GameState
