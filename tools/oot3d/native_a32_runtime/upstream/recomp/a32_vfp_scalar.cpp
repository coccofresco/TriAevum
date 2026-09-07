#include "a32_vfp_scalar.h"

#include "a32_vfp_binary64.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace oot3d::recomp::a32 {
namespace {

constexpr std::uint32_t kConditionMask = 0xF0000000U;
constexpr std::uint32_t kSignBit = 0x80000000U;
constexpr std::uint32_t kExponentMask = 0x7F800000U;
constexpr std::uint32_t kFractionMask = 0x007FFFFFU;
constexpr std::uint32_t kQuietBit = 0x00400000U;
constexpr std::uint32_t kPositiveInfinity = 0x7F800000U;
constexpr std::uint32_t kMaximumFinite = 0x7F7FFFFFU;
constexpr std::uint32_t kDefaultNan = 0x7FC00000U;

constexpr std::uint32_t kFpscrIoc = 1U << 0U;
constexpr std::uint32_t kFpscrDzc = 1U << 1U;
constexpr std::uint32_t kFpscrOfc = 1U << 2U;
constexpr std::uint32_t kFpscrUfc = 1U << 3U;
constexpr std::uint32_t kFpscrIxc = 1U << 4U;
constexpr std::uint32_t kFpscrIdc = 1U << 7U;
constexpr std::uint32_t kExceptionFlags =
    kFpscrIoc | kFpscrDzc | kFpscrOfc | kFpscrUfc | kFpscrIxc |
    kFpscrIdc;
constexpr std::uint32_t kFpscrVectorModeMask = 0x00370000U;
constexpr std::uint32_t kFpscrExceptionEnableMask = 0x00009F00U;
constexpr std::uint32_t kFpscrFlushToZero = 1U << 24U;
constexpr std::uint32_t kFpscrDefaultNan = 1U << 25U;
constexpr std::uint32_t kNzcvMask = kFlagN | kFlagZ | kFlagC | kFlagV;

constexpr std::uint32_t kNzcvEqual = 0x60000000U;
constexpr std::uint32_t kNzcvLess = 0x80000000U;
constexpr std::uint32_t kNzcvGreater = 0x20000000U;
constexpr std::uint32_t kNzcvUnordered = 0x30000000U;

constexpr std::uint32_t kTernaryMask = 0x0FB00F50U;
constexpr std::uint32_t kUnaryMask = 0x0FBF0FD0U;

enum class RoundingMode : std::uint8_t {
    NearestEven = 0,
    PlusInfinity = 1,
    MinusInfinity = 2,
    TowardZero = 3,
};

enum class ValueKind : std::uint8_t {
    Zero,
    Finite,
    Infinity,
    QuietNan,
    SignalingNan,
};

struct Control {
    RoundingMode rounding{};
    bool default_nan{};
    bool flush_to_zero{};
};

struct Result {
    std::uint32_t value{};
    std::uint32_t flags{};
};

struct Unpacked {
    ValueKind kind{ValueKind::Zero};
    bool sign{};
    std::uint32_t significand{};
    int exponent{};
    std::uint32_t bits{};
};

constexpr bool Matches(
    std::uint32_t raw,
    std::uint32_t mask,
    std::uint32_t value) noexcept {
    return (raw & mask) == value;
}

constexpr std::uint8_t Bits(
    std::uint32_t raw,
    unsigned shift,
    std::uint32_t mask) noexcept {
    return static_cast<std::uint8_t>((raw >> shift) & mask);
}

constexpr std::uint8_t DestinationLane(std::uint32_t raw) noexcept {
    return static_cast<std::uint8_t>(
        Bits(raw, 12U, 0xFU) * 2U + Bits(raw, 22U, 1U));
}

constexpr std::uint8_t LeftLane(std::uint32_t raw) noexcept {
    return static_cast<std::uint8_t>(
        Bits(raw, 16U, 0xFU) * 2U + Bits(raw, 7U, 1U));
}

constexpr std::uint8_t RightLane(std::uint32_t raw) noexcept {
    return static_cast<std::uint8_t>(
        Bits(raw, 0U, 0xFU) * 2U + Bits(raw, 5U, 1U));
}

constexpr Control ControlFromFpscr(std::uint32_t fpscr) noexcept {
    return {
        static_cast<RoundingMode>((fpscr >> 22U) & 3U),
        (fpscr & kFpscrDefaultNan) != 0U,
        (fpscr & kFpscrFlushToZero) != 0U,
    };
}

unsigned BitLength(std::uint64_t value) noexcept {
    unsigned length = 0U;
    while (value != 0U) {
        ++length;
        value >>= 1U;
    }
    return length;
}

std::uint64_t ShiftRightJam(std::uint64_t value, unsigned shift) noexcept {
    if (shift == 0U) {
        return value;
    }
    if (shift >= 64U) {
        return value == 0U ? 0U : 1U;
    }
    const std::uint64_t discarded = value & ((std::uint64_t{1} << shift) - 1U);
    return (value >> shift) | (discarded != 0U ? 1U : 0U);
}

Unpacked Unpack(
    std::uint32_t bits,
    const Control& control,
    std::uint32_t* flags,
    bool arithmetic = true) noexcept {
    const bool sign = (bits & kSignBit) != 0U;
    const std::uint32_t exponent_field = (bits >> 23U) & 0xFFU;
    const std::uint32_t fraction = bits & kFractionMask;
    if (exponent_field == 0xFFU) {
        if (fraction == 0U) {
            return {ValueKind::Infinity, sign, 0U, 0, bits};
        }
        return {
            (fraction & kQuietBit) != 0U ? ValueKind::QuietNan
                                        : ValueKind::SignalingNan,
            sign,
            0U,
            0,
            bits,
        };
    }
    if (exponent_field == 0U) {
        if (fraction == 0U) {
            return {ValueKind::Zero, sign, 0U, 0, bits};
        }
        if (arithmetic && control.flush_to_zero) {
            *flags |= kFpscrIdc;
            return {ValueKind::Zero, false, 0U, 0, 0U};
        }
        return {ValueKind::Finite, sign, fraction, -149, bits};
    }
    return {
        ValueKind::Finite,
        sign,
        (1U << 23U) | fraction,
        static_cast<int>(exponent_field) - 150,
        bits,
    };
}

bool IsNan(ValueKind kind) noexcept {
    return kind == ValueKind::QuietNan || kind == ValueKind::SignalingNan;
}

bool PropagateNan(
    const Unpacked* operands,
    std::size_t count,
    const Control& control,
    std::uint32_t flags,
    Result* result) noexcept {
    bool has_nan = false;
    bool signaling = false;
    for (std::size_t index = 0; index < count; ++index) {
        has_nan |= IsNan(operands[index].kind);
        signaling |= operands[index].kind == ValueKind::SignalingNan;
    }
    if (!has_nan) {
        return false;
    }
    if (signaling) {
        flags |= kFpscrIoc;
    }
    if (control.default_nan) {
        *result = {kDefaultNan, flags};
        return true;
    }
    for (ValueKind kind : {ValueKind::SignalingNan, ValueKind::QuietNan}) {
        for (std::size_t index = 0; index < count; ++index) {
            if (operands[index].kind == kind) {
                *result = {operands[index].bits | kQuietBit, flags};
                return true;
            }
        }
    }
    return false;
}

constexpr Result Invalid(std::uint32_t flags = 0U) noexcept {
    return {kDefaultNan, flags | kFpscrIoc};
}

int FloorLog2Ratio(std::uint64_t numerator, std::uint64_t denominator) noexcept {
    int estimate = static_cast<int>(BitLength(numerator)) -
                   static_cast<int>(BitLength(denominator));
    if (estimate >= 0) {
        if (numerator < (denominator << static_cast<unsigned>(estimate))) {
            --estimate;
        }
    } else if ((numerator << static_cast<unsigned>(-estimate)) < denominator) {
        --estimate;
    }
    return estimate;
}

bool Increment(
    std::uint64_t quotient,
    std::uint64_t remainder,
    std::uint64_t denominator,
    bool sign,
    RoundingMode mode) noexcept {
    if (remainder == 0U) {
        return false;
    }
    if (mode == RoundingMode::NearestEven) {
        const std::uint64_t twice = remainder << 1U;
        return twice > denominator ||
               (twice == denominator && (quotient & 1U) != 0U);
    }
    if (mode == RoundingMode::PlusInfinity) {
        return !sign;
    }
    if (mode == RoundingMode::MinusInfinity) {
        return sign;
    }
    return false;
}

constexpr Result Overflow(bool sign, RoundingMode mode) noexcept {
    const bool infinity =
        mode == RoundingMode::NearestEven ||
        (mode == RoundingMode::PlusInfinity && !sign) ||
        (mode == RoundingMode::MinusInfinity && sign);
    const std::uint32_t magnitude = infinity ? kPositiveInfinity : kMaximumFinite;
    return {(sign ? kSignBit : 0U) | magnitude, kFpscrOfc | kFpscrIxc};
}

Result RoundFinite(
    std::uint64_t numerator,
    std::uint64_t denominator,
    int exponent,
    bool sign,
    const Control& control) noexcept {
    if (numerator == 0U) {
        return {sign ? kSignBit : 0U, 0U};
    }
    int binary_exponent = FloorLog2Ratio(numerator, denominator) + exponent;
    if (control.flush_to_zero && binary_exponent < -126) {
        return {0U, kFpscrUfc};
    }
    if (binary_exponent > 127) {
        return Overflow(sign, control.rounding);
    }

    if (binary_exponent >= -126) {
        const int shift = exponent - binary_exponent + 23;
        std::uint64_t scaled_numerator = numerator;
        std::uint64_t scaled_denominator = denominator;
        if (shift >= 0) {
            scaled_numerator <<= static_cast<unsigned>(shift);
        } else {
            scaled_denominator <<= static_cast<unsigned>(-shift);
        }
        std::uint64_t significand = scaled_numerator / scaled_denominator;
        const std::uint64_t remainder = scaled_numerator % scaled_denominator;
        if (Increment(
                significand,
                remainder,
                scaled_denominator,
                sign,
                control.rounding)) {
            ++significand;
        }
        const bool inexact = remainder != 0U;
        if (significand >= (std::uint64_t{1} << 24U)) {
            significand >>= 1U;
            ++binary_exponent;
        }
        if (binary_exponent > 127) {
            return Overflow(sign, control.rounding);
        }
        return {
            (sign ? kSignBit : 0U) |
                (static_cast<std::uint32_t>(binary_exponent + 127) << 23U) |
                (static_cast<std::uint32_t>(significand) & kFractionMask),
            inexact ? kFpscrIxc : 0U,
        };
    }

    const int shift = exponent + 149;
    std::uint64_t significand = 0U;
    std::uint64_t remainder = numerator;
    std::uint64_t divisor = denominator;
    bool virtual_large_divisor = false;
    if (shift >= 0) {
        const std::uint64_t scaled_numerator =
            numerator << static_cast<unsigned>(shift);
        significand = scaled_numerator / denominator;
        remainder = scaled_numerator % denominator;
    } else {
        const unsigned right_shift = static_cast<unsigned>(-shift);
        if (right_shift >= 64U ||
            denominator >
                (std::numeric_limits<std::uint64_t>::max() >> right_shift)) {
            virtual_large_divisor = true;
        } else {
            divisor = denominator << right_shift;
            significand = numerator / divisor;
            remainder = numerator % divisor;
        }
    }
    bool increment = false;
    if (remainder != 0U) {
        if (virtual_large_divisor) {
            increment =
                (control.rounding == RoundingMode::PlusInfinity && !sign) ||
                (control.rounding == RoundingMode::MinusInfinity && sign);
        } else {
            increment = Increment(
                significand,
                remainder,
                divisor,
                sign,
                control.rounding);
        }
    }
    if (increment) {
        ++significand;
    }
    const bool inexact = remainder != 0U;
    if (significand >= (std::uint64_t{1} << 23U)) {
        return {
            (sign ? kSignBit : 0U) | (1U << 23U),
            inexact ? kFpscrIxc : 0U,
        };
    }
    return {
        (sign ? kSignBit : 0U) | static_cast<std::uint32_t>(significand),
        inexact ? (kFpscrUfc | kFpscrIxc) : 0U,
    };
}

constexpr Result F32Neg(std::uint32_t bits) noexcept {
    return {bits ^ kSignBit, 0U};
}

constexpr Result F32Abs(std::uint32_t bits) noexcept {
    return {bits & ~kSignBit, 0U};
}

Result F32Add(
    std::uint32_t a,
    std::uint32_t b,
    const Control& control) noexcept {
    std::uint32_t flags = 0U;
    const std::array<Unpacked, 2> values = {
        Unpack(a, control, &flags),
        Unpack(b, control, &flags),
    };
    const Unpacked& left = values[0];
    const Unpacked& right = values[1];
    Result nan{};
    if (PropagateNan(values.data(), values.size(), control, flags, &nan)) {
        return nan;
    }
    if (left.kind == ValueKind::Infinity || right.kind == ValueKind::Infinity) {
        if (left.kind == ValueKind::Infinity &&
            right.kind == ValueKind::Infinity && left.sign != right.sign) {
            return Invalid(flags);
        }
        const Unpacked& value =
            left.kind == ValueKind::Infinity ? left : right;
        return {(value.sign ? kSignBit : 0U) | kPositiveInfinity, flags};
    }
    if (left.kind == ValueKind::Zero && right.kind == ValueKind::Zero) {
        const bool sign = left.sign == right.sign
                              ? left.sign
                              : control.rounding == RoundingMode::MinusInfinity;
        return {sign ? kSignBit : 0U, flags};
    }
    if (left.kind == ValueKind::Zero) {
        return {right.bits, flags};
    }
    if (right.kind == ValueKind::Zero) {
        return {left.bits, flags};
    }

    const int difference = left.exponent - right.exponent;
    std::int64_t total = 0;
    int common_exponent = 0;
    if (difference >= -32 && difference <= 32) {
        common_exponent =
            left.exponent < right.exponent ? left.exponent : right.exponent;
        std::uint64_t left_integer = left.significand;
        std::uint64_t right_integer = right.significand;
        left_integer <<= static_cast<unsigned>(left.exponent - common_exponent);
        right_integer <<= static_cast<unsigned>(right.exponent - common_exponent);
        total = (left.sign ? -static_cast<std::int64_t>(left_integer)
                           : static_cast<std::int64_t>(left_integer)) +
                (right.sign ? -static_cast<std::int64_t>(right_integer)
                            : static_cast<std::int64_t>(right_integer));
    } else {
        const Unpacked& large = difference > 0 ? left : right;
        const Unpacked& small = difference > 0 ? right : left;
        const unsigned shift = static_cast<unsigned>(
            large.exponent - small.exponent);
        const std::uint64_t large_integer =
            static_cast<std::uint64_t>(large.significand) << 3U;
        const std::uint64_t small_integer = ShiftRightJam(
            static_cast<std::uint64_t>(small.significand) << 3U, shift);
        total = (large.sign ? -static_cast<std::int64_t>(large_integer)
                            : static_cast<std::int64_t>(large_integer)) +
                (small.sign ? -static_cast<std::int64_t>(small_integer)
                            : static_cast<std::int64_t>(small_integer));
        common_exponent = large.exponent - 3;
    }
    if (total == 0) {
        return {
            control.rounding == RoundingMode::MinusInfinity ? kSignBit : 0U,
            flags,
        };
    }
    const bool sign = total < 0;
    const std::uint64_t magnitude = sign
                                        ? static_cast<std::uint64_t>(-total)
                                        : static_cast<std::uint64_t>(total);
    const Result rounded =
        RoundFinite(magnitude, 1U, common_exponent, sign, control);
    return {rounded.value, flags | rounded.flags};
}

Result F32Sub(
    std::uint32_t a,
    std::uint32_t b,
    const Control& control) noexcept {
    std::uint32_t flags = 0U;
    const Unpacked right = Unpack(b, control, &flags);
    const Result result = F32Add(a, right.bits ^ kSignBit, control);
    return {result.value, result.flags | flags};
}

Result F32Mul(
    std::uint32_t a,
    std::uint32_t b,
    const Control& control) noexcept {
    std::uint32_t flags = 0U;
    const std::array<Unpacked, 2> values = {
        Unpack(a, control, &flags),
        Unpack(b, control, &flags),
    };
    const Unpacked& left = values[0];
    const Unpacked& right = values[1];
    Result nan{};
    if (PropagateNan(values.data(), values.size(), control, flags, &nan)) {
        return nan;
    }
    const bool sign = left.sign != right.sign;
    if ((left.kind == ValueKind::Zero && right.kind == ValueKind::Infinity) ||
        (left.kind == ValueKind::Infinity && right.kind == ValueKind::Zero)) {
        return Invalid(flags);
    }
    if (left.kind == ValueKind::Infinity || right.kind == ValueKind::Infinity) {
        return {(sign ? kSignBit : 0U) | kPositiveInfinity, flags};
    }
    if (left.kind == ValueKind::Zero || right.kind == ValueKind::Zero) {
        return {sign ? kSignBit : 0U, flags};
    }
    const Result rounded = RoundFinite(
        static_cast<std::uint64_t>(left.significand) * right.significand,
        1U,
        left.exponent + right.exponent,
        sign,
        control);
    return {rounded.value, flags | rounded.flags};
}

Result F32Div(
    std::uint32_t a,
    std::uint32_t b,
    const Control& control) noexcept {
    std::uint32_t flags = 0U;
    const std::array<Unpacked, 2> values = {
        Unpack(a, control, &flags),
        Unpack(b, control, &flags),
    };
    const Unpacked& left = values[0];
    const Unpacked& right = values[1];
    Result nan{};
    if (PropagateNan(values.data(), values.size(), control, flags, &nan)) {
        return nan;
    }
    const bool sign = left.sign != right.sign;
    if ((left.kind == ValueKind::Zero && right.kind == ValueKind::Zero) ||
        (left.kind == ValueKind::Infinity &&
         right.kind == ValueKind::Infinity)) {
        return Invalid(flags);
    }
    if (left.kind == ValueKind::Infinity) {
        return {(sign ? kSignBit : 0U) | kPositiveInfinity, flags};
    }
    if (right.kind == ValueKind::Infinity) {
        return {sign ? kSignBit : 0U, flags};
    }
    if (right.kind == ValueKind::Zero) {
        return {
            (sign ? kSignBit : 0U) | kPositiveInfinity,
            flags | kFpscrDzc,
        };
    }
    if (left.kind == ValueKind::Zero) {
        return {sign ? kSignBit : 0U, flags};
    }
    const Result rounded = RoundFinite(
        left.significand,
        right.significand,
        left.exponent - right.exponent,
        sign,
        control);
    return {rounded.value, flags | rounded.flags};
}

std::uint64_t IntegerSquareRoot(std::uint64_t value) noexcept {
    std::uint64_t root = 0U;
    std::uint64_t bit = std::uint64_t{1} << 62U;
    while (bit > value) {
        bit >>= 2U;
    }
    while (bit != 0U) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1U) + bit;
        } else {
            root >>= 1U;
        }
        bit >>= 2U;
    }
    return root;
}

int FloorDivideByTwo(int value) noexcept {
    return value >= 0 ? value / 2 : -((-value + 1) / 2);
}

Result F32Sqrt(std::uint32_t bits, const Control& control) noexcept {
    std::uint32_t flags = 0U;
    const Unpacked value = Unpack(bits, control, &flags);
    Result nan{};
    if (PropagateNan(&value, 1U, control, flags, &nan)) {
        return nan;
    }
    if (value.kind == ValueKind::Zero) {
        return {value.bits, flags};
    }
    if (value.sign) {
        return Invalid(flags);
    }
    if (value.kind == ValueKind::Infinity) {
        return {kPositiveInfinity, flags};
    }
    const int input_log2 =
        static_cast<int>(BitLength(value.significand)) - 1 + value.exponent;
    int output_exponent = FloorDivideByTwo(input_log2);
    const int shift = value.exponent - (2 * output_exponent) + 46;
    const std::uint64_t radicand =
        static_cast<std::uint64_t>(value.significand) <<
        static_cast<unsigned>(shift);
    std::uint64_t significand = IntegerSquareRoot(radicand);
    const std::uint64_t remainder =
        radicand - significand * significand;
    if (remainder != 0U) {
        bool increment = false;
        if (control.rounding == RoundingMode::NearestEven) {
            increment = (radicand << 2U) >
                        ((significand * significand << 2U) +
                         (significand << 2U) + 1U);
        } else if (control.rounding == RoundingMode::PlusInfinity) {
            increment = true;
        }
        if (increment) {
            ++significand;
        }
    }
    if (significand >= (std::uint64_t{1} << 24U)) {
        significand >>= 1U;
        ++output_exponent;
    }
    return {
        (static_cast<std::uint32_t>(output_exponent + 127) << 23U) |
            (static_cast<std::uint32_t>(significand) & kFractionMask),
        flags | (remainder != 0U ? kFpscrIxc : 0U),
    };
}

constexpr Result Combine(Result first, Result second) noexcept {
    return {second.value, first.flags | second.flags};
}

Result F32Mla(
    std::uint32_t accumulator,
    std::uint32_t left,
    std::uint32_t right,
    const Control& control) noexcept {
    const Result product = F32Mul(left, right, control);
    return Combine(product, F32Add(accumulator, product.value, control));
}

Result F32Mls(
    std::uint32_t accumulator,
    std::uint32_t left,
    std::uint32_t right,
    const Control& control) noexcept {
    const Result product = F32Mul(left, right, control);
    return Combine(
        product,
        F32Add(accumulator, F32Neg(product.value).value, control));
}

Result F32Nmla(
    std::uint32_t accumulator,
    std::uint32_t left,
    std::uint32_t right,
    const Control& control) noexcept {
    const Result product = F32Mul(left, right, control);
    return Combine(
        product,
        F32Add(
            F32Neg(accumulator).value,
            F32Neg(product.value).value,
            control));
}

Result F32Nmls(
    std::uint32_t accumulator,
    std::uint32_t left,
    std::uint32_t right,
    const Control& control) noexcept {
    const Result product = F32Mul(left, right, control);
    return Combine(
        product,
        F32Add(F32Neg(accumulator).value, product.value, control));
}

Result F32Nmul(
    std::uint32_t left,
    std::uint32_t right,
    const Control& control) noexcept {
    const Result product = F32Mul(left, right, control);
    return {F32Neg(product.value).value, product.flags};
}

Result F32FromUnsigned(std::uint32_t value, const Control& control) noexcept {
    return RoundFinite(value, 1U, 0, false, control);
}

Result F32FromSigned(std::uint32_t value, const Control& control) noexcept {
    const std::int64_t signed_value = static_cast<std::int32_t>(value);
    const bool sign = signed_value < 0;
    const std::uint64_t magnitude = sign
                                        ? static_cast<std::uint64_t>(-signed_value)
                                        : static_cast<std::uint64_t>(signed_value);
    return RoundFinite(magnitude, 1U, 0, sign, control);
}

Result F32ToInteger(
    std::uint32_t bits,
    bool is_signed,
    const Control& control) noexcept {
    std::uint32_t flags = 0U;
    const Unpacked value = Unpack(bits, control, &flags);
    if (IsNan(value.kind)) {
        return {0U, flags | kFpscrIoc};
    }
    if (value.kind == ValueKind::Zero) {
        return {0U, flags};
    }
    if (value.kind == ValueKind::Infinity) {
        if (is_signed) {
            return {
                value.sign ? 0x80000000U : 0x7FFFFFFFU,
                flags | kFpscrIoc,
            };
        }
        return {value.sign ? 0U : 0xFFFFFFFFU, flags | kFpscrIoc};
    }

    std::uint64_t magnitude = 0U;
    bool inexact = false;
    bool magnitude_overflow = false;
    if (value.exponent >= 0) {
        const unsigned shift = static_cast<unsigned>(value.exponent);
        if (shift >= 64U ||
            value.significand >
                (std::numeric_limits<std::uint64_t>::max() >> shift)) {
            magnitude_overflow = true;
        } else {
            magnitude = static_cast<std::uint64_t>(value.significand) << shift;
        }
    } else {
        const unsigned shift = static_cast<unsigned>(-value.exponent);
        if (shift >= 64U) {
            magnitude = 0U;
            inexact = value.significand != 0U;
        } else {
            magnitude =
                static_cast<std::uint64_t>(value.significand) >> shift;
            const std::uint64_t mask =
                shift == 0U ? 0U : (std::uint64_t{1} << shift) - 1U;
            inexact = (value.significand & mask) != 0U;
        }
    }

    if (is_signed) {
        const std::uint64_t limit = value.sign ? 0x80000000ULL : 0x7FFFFFFFULL;
        if (magnitude_overflow || magnitude > limit) {
            return {
                value.sign ? 0x80000000U : 0x7FFFFFFFU,
                flags | kFpscrIoc,
            };
        }
        const std::int64_t integer = value.sign
                                         ? -static_cast<std::int64_t>(magnitude)
                                         : static_cast<std::int64_t>(magnitude);
        return {
            static_cast<std::uint32_t>(integer),
            flags | (inexact ? kFpscrIxc : 0U),
        };
    }
    if (value.sign) {
        if (magnitude_overflow || magnitude != 0U) {
            return {0U, flags | kFpscrIoc};
        }
        return {0U, flags | (inexact ? kFpscrIxc : 0U)};
    }
    if (magnitude_overflow || magnitude > 0xFFFFFFFFULL) {
        return {0xFFFFFFFFU, flags | kFpscrIoc};
    }
    return {
        static_cast<std::uint32_t>(magnitude),
        flags | (inexact ? kFpscrIxc : 0U),
    };
}

ValueKind Classify(std::uint32_t bits) noexcept {
    const std::uint32_t exponent = bits & kExponentMask;
    const std::uint32_t fraction = bits & kFractionMask;
    if (exponent == kExponentMask) {
        if (fraction == 0U) {
            return ValueKind::Infinity;
        }
        return (fraction & kQuietBit) != 0U ? ValueKind::QuietNan
                                            : ValueKind::SignalingNan;
    }
    if (exponent == 0U && fraction == 0U) {
        return ValueKind::Zero;
    }
    return ValueKind::Finite;
}

Result F32Compare(
    std::uint32_t a,
    std::uint32_t b,
    bool signal_all_nans) noexcept {
    const ValueKind left_kind = Classify(a);
    const ValueKind right_kind = Classify(b);
    if (IsNan(left_kind) || IsNan(right_kind)) {
        const bool invalid = left_kind == ValueKind::SignalingNan ||
                             right_kind == ValueKind::SignalingNan ||
                             signal_all_nans;
        return {kNzcvUnordered, invalid ? kFpscrIoc : 0U};
    }
    if ((a & ~kSignBit) == 0U && (b & ~kSignBit) == 0U) {
        return {kNzcvEqual, 0U};
    }
    if (a == b) {
        return {kNzcvEqual, 0U};
    }
    const bool left_sign = (a & kSignBit) != 0U;
    const bool right_sign = (b & kSignBit) != 0U;
    if (left_sign != right_sign) {
        return {left_sign ? kNzcvLess : kNzcvGreater, 0U};
    }
    const bool less = left_sign ? (a & ~kSignBit) > (b & ~kSignBit) : a < b;
    return {less ? kNzcvLess : kNzcvGreater, 0U};
}

void CommitScalarResult(
    GuestState& state,
    std::uint8_t destination,
    Result result) noexcept {
    state.vfp[destination] = result.value;
    state.fpscr |= result.flags & kExceptionFlags;
}

ExecutionResult Fallthrough(GuestState& state, std::uint32_t pc) noexcept {
    state.r[15] = pc + 4U;
    return {ExitKind::Fallthrough, pc + 4U, FallbackReason::None, 0U};
}

ExecutionResult Unsupported(
    GuestState& state,
    std::uint32_t raw,
    std::uint32_t pc) noexcept {
    state.r[15] = pc;
    return {ExitKind::Unsupported, pc, FallbackReason::Unsupported, raw};
}

}  // namespace

VfpBinary32Result VfpBinary32Multiply(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept {
    const Result result = F32Mul(left, right, ControlFromFpscr(fpscr));
    return {result.value, result.flags & kExceptionFlags};
}

VfpBinary32Result VfpBinary32MultiplyAccumulate(
    std::uint32_t accumulator,
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) noexcept {
    const Result result =
        F32Mla(accumulator, left, right, ControlFromFpscr(fpscr));
    return {result.value, result.flags & kExceptionFlags};
}

VfpBinary32Result VfpBinary32FromSigned(
    std::uint32_t value,
    std::uint32_t fpscr) noexcept {
    const Result result = F32FromSigned(value, ControlFromFpscr(fpscr));
    return {result.value, result.flags & kExceptionFlags};
}

VfpBinary32Result VfpBinary32Compare(
    std::uint32_t left,
    std::uint32_t right,
    bool signal_all_nans) noexcept {
    const Result result = F32Compare(left, right, signal_all_nans);
    return {result.value, result.flags & kExceptionFlags};
}

ExecutionResult ExecuteVfpScalar(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state) {
    if (VfpBinary64Supported(raw)) {
        return ExecuteVfpBinary64(raw, pc, state);
    }
    if ((raw & kConditionMask) == kConditionMask ||
        (state.fpscr &
         (kFpscrVectorModeMask | kFpscrExceptionEnableMask)) != 0U) {
        return Unsupported(state, raw, pc);
    }

    const Control control = ControlFromFpscr(state.fpscr);
    const std::uint8_t destination = DestinationLane(raw);
    const std::uint8_t left = LeftLane(raw);
    const std::uint8_t right = RightLane(raw);
    const std::uint32_t ternary = raw & kTernaryMask;
    Result result{};

    switch (ternary) {
    case 0x0E000A00U:
        result = F32Mla(
            state.vfp[destination], state.vfp[left], state.vfp[right], control);
        break;
    case 0x0E000A40U:
        result = F32Mls(
            state.vfp[destination], state.vfp[left], state.vfp[right], control);
        break;
    case 0x0E100A40U:
        result = F32Nmla(
            state.vfp[destination], state.vfp[left], state.vfp[right], control);
        break;
    case 0x0E100A00U:
        result = F32Nmls(
            state.vfp[destination], state.vfp[left], state.vfp[right], control);
        break;
    case 0x0E200A00U:
        result = F32Mul(state.vfp[left], state.vfp[right], control);
        break;
    case 0x0E200A40U:
        result = F32Nmul(state.vfp[left], state.vfp[right], control);
        break;
    case 0x0E300A00U:
        result = F32Add(state.vfp[left], state.vfp[right], control);
        break;
    case 0x0E300A40U:
        result = F32Sub(state.vfp[left], state.vfp[right], control);
        break;
    case 0x0E800A00U:
        result = F32Div(state.vfp[left], state.vfp[right], control);
        break;
    default: {
        const std::uint32_t unary = raw & kUnaryMask;
        switch (unary) {
        case 0x0EB00AC0U:
            result = F32Abs(state.vfp[right]);
            break;
        case 0x0EB10A40U:
            result = F32Neg(state.vfp[right]);
            break;
        case 0x0EB10AC0U:
            result = F32Sqrt(state.vfp[right], control);
            break;
        case 0x0EB80AC0U:
            result = F32FromSigned(state.vfp[right], control);
            break;
        case 0x0EB80A40U:
            result = F32FromUnsigned(state.vfp[right], control);
            break;
        case 0x0EBD0AC0U:
            result = F32ToInteger(state.vfp[right], true, control);
            break;
        case 0x0EBC0AC0U:
            result = F32ToInteger(state.vfp[right], false, control);
            break;
        case 0x0EB40A40U:
        case 0x0EB40AC0U: {
            const Result comparison = F32Compare(
                state.vfp[destination],
                state.vfp[right],
                unary == 0x0EB40AC0U);
            state.fpscr =
                (state.fpscr & ~kNzcvMask) | (comparison.value & kNzcvMask);
            state.fpscr |= comparison.flags & kExceptionFlags;
            return Fallthrough(state, pc);
        }
        default:
            return Unsupported(state, raw, pc);
        }
        break;
    }
    }

    CommitScalarResult(state, destination, result);
    return Fallthrough(state, pc);
}

}  // namespace oot3d::recomp::a32
