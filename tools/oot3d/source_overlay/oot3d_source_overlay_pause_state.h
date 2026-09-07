#pragma once

#include "oot3d_source_overlay_abi.h"

#include <array>
#include <cstdint>

namespace Oot3dSourceOverlay::PauseState {

inline constexpr std::uint32_t kInitializeEntry = 0x00435C54U;

inline constexpr std::array<Oot3dSourceOverlayEntry, 1> kEntries{{
    {kInitializeEntry, 0U, "ui.pause.items",
     "PauseItemsPage_InitializeState"},
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

} // namespace Oot3dSourceOverlay::PauseState
