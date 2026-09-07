#pragma once

#include "recomp/a32_vfp_scalar.h"

#include <cstdint>

namespace oot3d::recomp::a32 {

VfpBinary32Result VfpBinary32Add(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept;
VfpBinary32Result VfpBinary32Subtract(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept;
VfpBinary32Result VfpBinary32Divide(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept;
VfpBinary32Result VfpBinary32MultiplySubtract(
    std::uint32_t accumulator,
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept;
VfpBinary32Result VfpBinary32NegativeMultiplyAccumulate(
    std::uint32_t accumulator,
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept;
VfpBinary32Result VfpBinary32NegativeMultiplySubtract(
    std::uint32_t accumulator,
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept;
VfpBinary32Result VfpBinary32NegativeMultiply(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept;
VfpBinary32Result VfpBinary32SquareRoot(
    std::uint32_t value,
    std::uint32_t fpscr) noexcept;
VfpBinary32Result VfpBinary32FromUnsigned(
    std::uint32_t value,
    std::uint32_t fpscr) noexcept;
VfpBinary32Result VfpBinary32ToUnsigned(
    std::uint32_t value,
    std::uint32_t fpscr) noexcept;
VfpBinary32Result VfpBinary32ToSigned(
    std::uint32_t value,
    std::uint32_t fpscr) noexcept;

} // namespace oot3d::recomp::a32
