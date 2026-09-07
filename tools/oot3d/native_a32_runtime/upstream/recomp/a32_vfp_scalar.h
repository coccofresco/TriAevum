#pragma once

#include <cstdint>

#include "a32_runtime.h"

namespace oot3d::recomp::a32 {

struct VfpBinary32Result {
    std::uint32_t value{};
    std::uint32_t exception_flags{};
};

VfpBinary32Result VfpBinary32Multiply(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept;
VfpBinary32Result VfpBinary32MultiplyAccumulate(
    std::uint32_t accumulator,
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept;
VfpBinary32Result VfpBinary32FromSigned(
    std::uint32_t value,
    std::uint32_t fpscr) noexcept;
VfpBinary32Result VfpBinary32Compare(
    std::uint32_t left,
    std::uint32_t right,
    bool signal_all_nans) noexcept;

// Execute one instruction from the exact scalar binary32/binary64 VFP subset.
// The A32 condition has already been evaluated by ExecuteBlock.  Unsupported
// encodings and dynamic FPSCR modes return Unsupported without committing a
// scalar result, allowing the caller to retain the existing fallback path.
ExecutionResult ExecuteVfpScalar(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state);

}  // namespace oot3d::recomp::a32
