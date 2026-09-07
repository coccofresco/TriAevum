#pragma once

#include <cstdint>

#include "a32_runtime.h"

namespace oot3d::recomp::a32 {

// Execute one instruction from the conservative ARM core subset already
// accepted by the pinned Python classifier.  ExecuteBlock evaluates the A32
// condition before entering this helper.
ExecutionResult ExecuteCore(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory);

}  // namespace oot3d::recomp::a32
