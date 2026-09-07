#pragma once

#include <cstdint>

#include "a32_runtime.h"

namespace oot3d::recomp::a32::internal {

ExecutionResult ExecuteCoreSystem(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state) noexcept;

// ExitKind::Unsupported means that this disjoint decoder did not recognize
// the raw instruction; other results are final architectural outcomes.
ExecutionResult ExecuteCoreAlu(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory);

ExecutionResult ExecuteCoreMemory(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory);

}  // namespace oot3d::recomp::a32::internal
