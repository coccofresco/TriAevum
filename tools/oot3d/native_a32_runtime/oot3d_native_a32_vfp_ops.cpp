#include "oot3d_native_a32_vfp_ops.h"

// Compile the pinned implementation as the single authoritative scalar VFP
// translation unit, then expose operation-level entry points for generated
// AOT code without changing the provenance-checked snapshot.
#include "upstream/recomp/a32_vfp_scalar.cpp"

namespace oot3d::recomp::a32 {

VfpBinary32Result VfpBinary32Add(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept {
    const Result result = F32Add(left, right, ControlFromFpscr(fpscr));
    return {result.value, result.flags & kExceptionFlags};
}

VfpBinary32Result VfpBinary32Subtract(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept {
    const Result result = F32Sub(left, right, ControlFromFpscr(fpscr));
    return {result.value, result.flags & kExceptionFlags};
}

VfpBinary32Result VfpBinary32Divide(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept {
    const Result result = F32Div(left, right, ControlFromFpscr(fpscr));
    return {result.value, result.flags & kExceptionFlags};
}

VfpBinary32Result VfpBinary32MultiplySubtract(
    std::uint32_t accumulator,
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept {
    const Result result =
        F32Mls(accumulator, left, right, ControlFromFpscr(fpscr));
    return {result.value, result.flags & kExceptionFlags};
}

VfpBinary32Result VfpBinary32NegativeMultiplyAccumulate(
    std::uint32_t accumulator,
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept {
    const Result result =
        F32Nmla(accumulator, left, right, ControlFromFpscr(fpscr));
    return {result.value, result.flags & kExceptionFlags};
}

VfpBinary32Result VfpBinary32NegativeMultiplySubtract(
    std::uint32_t accumulator,
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept {
    const Result result =
        F32Nmls(accumulator, left, right, ControlFromFpscr(fpscr));
    return {result.value, result.flags & kExceptionFlags};
}

VfpBinary32Result VfpBinary32NegativeMultiply(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept {
    const Result result = F32Nmul(left, right, ControlFromFpscr(fpscr));
    return {result.value, result.flags & kExceptionFlags};
}

VfpBinary32Result VfpBinary32SquareRoot(
    std::uint32_t value,
    std::uint32_t fpscr) noexcept {
    const Result result = F32Sqrt(value, ControlFromFpscr(fpscr));
    return {result.value, result.flags & kExceptionFlags};
}

VfpBinary32Result VfpBinary32FromUnsigned(
    std::uint32_t value,
    std::uint32_t fpscr) noexcept {
    const Result result = F32FromUnsigned(value, ControlFromFpscr(fpscr));
    return {result.value, result.flags & kExceptionFlags};
}

VfpBinary32Result VfpBinary32ToUnsigned(
    std::uint32_t value,
    std::uint32_t fpscr) noexcept {
    const Result result = F32ToInteger(
        value, false, ControlFromFpscr(fpscr));
    return {result.value, result.flags & kExceptionFlags};
}

VfpBinary32Result VfpBinary32ToSigned(
    std::uint32_t value,
    std::uint32_t fpscr) noexcept {
    const Result result = F32ToInteger(
        value, true, ControlFromFpscr(fpscr));
    return {result.value, result.flags & kExceptionFlags};
}

} // namespace oot3d::recomp::a32
