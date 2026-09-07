#pragma once

#include <cstdint>

#include "a32_runtime.h"

namespace oot3d::recomp::a32 {

// Execute one instruction from the exact bit-preserving VFP transport subset,
// including the observed S/D register and core-pair transfers.
// The A32 condition has already been evaluated by ExecuteBlock.  A successful
// transport returns Fallthrough at pc + 4; malformed or out-of-subset raw
// encodings return Unsupported at the precise instruction PC without changing
// data registers, flags, VFP lanes or memory.
ExecutionResult ExecuteVfpTransport(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory);

}  // namespace oot3d::recomp::a32
