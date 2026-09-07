#pragma once

#include <cstdint>

#include "a32_runtime.h"

namespace oot3d::recomp::a32 {

// True for the scalar binary64 forms backed by the local integer-only
// executor.  D16-D31 remain outside the guest register layout.
bool VfpBinary64Supported(std::uint32_t raw) noexcept;

// Execute one supported scalar binary64 VFP instruction without using host
// floating-point arithmetic.  This preserves guest FPSCR rounding, NaN and
// exception-flag behavior independently of the host architecture.
ExecutionResult ExecuteVfpBinary64(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state);

}  // namespace oot3d::recomp::a32
