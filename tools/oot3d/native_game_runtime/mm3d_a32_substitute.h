#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <optional>

namespace Oot3dNativeGame {

// Executes one A32 instruction word that replaces the guest's instruction at
// pc (mm3d-dekui substitutes plain instructions as well as SVC traps): the
// data-processing immediate forms, B/BL, BX and LDMIA. Returns the PC the
// guest continues at, or nothing for a form outside that set (the caller
// must not have changed any state then). A failed condition skips the word.
std::optional<std::uint32_t> ExecuteMm3dSubstitute(std::uint32_t word, std::uint32_t pc,
                                                   oot3d::recomp::a32::GuestState& state,
                                                   oot3d::recomp::a32::MemoryBus& memory);

} // namespace Oot3dNativeGame
