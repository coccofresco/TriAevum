#include "a32_vfp_binary64.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace oot3d::recomp::a32 {
namespace {

constexpr std::uint32_t kConditionMask = 0xF0000000U;
constexpr std::uint32_t kTernaryMask = 0x0FB00F50U;
constexpr std::uint32_t kUnaryMask = 0x0FBF0FD0U;
constexpr std::uint32_t kDoubleDestinationBit = 1U << 22U;
constexpr std::uint32_t kDoubleLeftBit = 1U << 7U;
constexpr std::uint32_t kDoubleRightBit = 1U << 5U;

constexpr std::uint64_t kSignBit64 = std::uint64_t{1} << 63U;
constexpr std::uint64_t kExponentMask64 = 0x7FF0000000000000ULL;
constexpr std::uint64_t kFractionMask64 = 0x000FFFFFFFFFFFFFULL;
constexpr std::uint64_t kQuietBit64 = 0x0008000000000000ULL;
constexpr std::uint64_t kPositiveInfinity64 = 0x7FF0000000000000ULL;
constexpr std::uint64_t kMaximumFinite64 = 0x7FEFFFFFFFFFFFFFULL;
constexpr std::uint64_t kDefaultNan64 = 0x7FF8000000000000ULL;

constexpr std::uint32_t kSignBit32 = 1U << 31U;
constexpr std::uint32_t kExponentMask32 = 0x7F800000U;
constexpr std::uint32_t kFractionMask32 = 0x007FFFFFU;
constexpr std::uint32_t kQuietBit32 = 0x00400000U;
constexpr std::uint32_t kPositiveInfinity32 = 0x7F800000U;
constexpr std::uint32_t kMaximumFinite32 = 0x7F7FFFFFU;
constexpr std::uint32_t kDefaultNan32 = 0x7FC00000U;

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

struct UInt128 {
    std::uint64_t high{};
    std::uint64_t low{};
};

struct Result64 {
    std::uint64_t value{};
    std::uint32_t flags{};
};

struct Unpacked64 {
    ValueKind kind{ValueKind::Zero};
    bool sign{};
    std::uint64_t significand{};
    int exponent{};
    std::uint64_t bits{};
};

struct Unpacked32 {
    ValueKind kind{ValueKind::Zero};
    bool sign{};
    std::uint32_t significand{};
    int exponent{};
    std::uint32_t bits{};
};

struct Division {
    std::uint64_t quotient{};
    UInt128 remainder{};
    UInt128 divisor{};
    bool virtual_large_divisor{};
};

struct Format {
    unsigned precision{};
    int minimum_exponent{};
    int maximum_exponent{};
    int bias{};
    std::uint64_t sign_bit{};
    std::uint64_t fraction_mask{};
    std::uint64_t infinity{};
    std::uint64_t maximum_finite{};
};

constexpr Format kBinary64 = {
    53U,
    -1022,
    1023,
    1023,
    kSignBit64,
    kFractionMask64,
    kPositiveInfinity64,
    kMaximumFinite64,
};

constexpr Format kBinary32 = {
    24U,
    -126,
    127,
    127,
    kSignBit32,
    kFractionMask32,
    kPositiveInfinity32,
    kMaximumFinite32,
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

constexpr std::uint8_t RightLane(std::uint32_t raw) noexcept {
    return static_cast<std::uint8_t>(
        Bits(raw, 0U, 0xFU) * 2U + Bits(raw, 5U, 1U));
}

constexpr std::uint8_t DestinationDouble(std::uint32_t raw) noexcept {
    return static_cast<std::uint8_t>(
        Bits(raw, 12U, 0xFU) + Bits(raw, 22U, 1U) * 16U);
}

constexpr std::uint8_t LeftDouble(std::uint32_t raw) noexcept {
    return static_cast<std::uint8_t>(
        Bits(raw, 16U, 0xFU) + Bits(raw, 7U, 1U) * 16U);
}

constexpr std::uint8_t RightDouble(std::uint32_t raw) noexcept {
    return static_cast<std::uint8_t>(
        Bits(raw, 0U, 0xFU) + Bits(raw, 5U, 1U) * 16U);
}

constexpr Control ControlFromFpscr(std::uint32_t fpscr) noexcept {
    return {
        static_cast<RoundingMode>((fpscr >> 22U) & 3U),
        (fpscr & kFpscrDefaultNan) != 0U,
        (fpscr & kFpscrFlushToZero) != 0U,
    };
}

constexpr bool IsZero(UInt128 value) noexcept {
    return value.high == 0U && value.low == 0U;
}

constexpr int Compare(UInt128 left, UInt128 right) noexcept {
    if (left.high != right.high) {
        return left.high < right.high ? -1 : 1;
    }
    if (left.low != right.low) {
        return left.low < right.low ? -1 : 1;
    }
    return 0;
}

constexpr UInt128 Add(UInt128 left, UInt128 right) noexcept {
    const std::uint64_t low = left.low + right.low;
    return {left.high + right.high + (low < left.low ? 1U : 0U), low};
}

constexpr UInt128 Subtract(UInt128 left, UInt128 right) noexcept {
    return {
        left.high - right.high - (left.low < right.low ? 1U : 0U),
        left.low - right.low,
    };
}

UInt128 ShiftLeft(UInt128 value, unsigned shift) noexcept {
    if (shift == 0U) {
        return value;
    }
    if (shift >= 128U) {
        return {};
    }
    if (shift >= 64U) {
        return {value.low << (shift - 64U), 0U};
    }
    return {
        (value.high << shift) | (value.low >> (64U - shift)),
        value.low << shift,
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

unsigned BitLength(UInt128 value) noexcept {
    return value.high != 0U ? 64U + BitLength(value.high)
                            : BitLength(value.low);
}

constexpr bool Bit(UInt128 value, unsigned index) noexcept {
    return index >= 64U ? ((value.high >> (index - 64U)) & 1U) != 0U
                        : ((value.low >> index) & 1U) != 0U;
}

UInt128 Multiply64(std::uint64_t left, std::uint64_t right) noexcept {
    const std::uint64_t left_low = static_cast<std::uint32_t>(left);
    const std::uint64_t left_high = left >> 32U;
    const std::uint64_t right_low = static_cast<std::uint32_t>(right);
    const std::uint64_t right_high = right >> 32U;
    const std::uint64_t p00 = left_low * right_low;
    const std::uint64_t p01 = left_low * right_high;
    const std::uint64_t p10 = left_high * right_low;
    const std::uint64_t p11 = left_high * right_high;
    const std::uint64_t middle =
        (p00 >> 32U) + static_cast<std::uint32_t>(p01) +
        static_cast<std::uint32_t>(p10);
    return {
        p11 + (p01 >> 32U) + (p10 >> 32U) + (middle >> 32U),
        (middle << 32U) | static_cast<std::uint32_t>(p00),
    };
}

Division Divide(UInt128 numerator, UInt128 denominator) noexcept {
    Division result{};
    result.divisor = denominator;
    if (Compare(numerator, denominator) < 0) {
        result.remainder = numerator;
        return result;
    }
    UInt128 remainder{};
    const unsigned length = BitLength(numerator);
    for (unsigned position = length; position-- > 0U;) {
        remainder = ShiftLeft(remainder, 1U);
        remainder.low |= Bit(numerator, position) ? 1U : 0U;
        if (Compare(remainder, denominator) >= 0) {
            remainder = Subtract(remainder, denominator);
            if (position < 64U) {
                result.quotient |= std::uint64_t{1} << position;
            }
        }
    }
    result.remainder = remainder;
    return result;
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

int FloorLog2Ratio(UInt128 numerator, UInt128 denominator) noexcept {
    int estimate = static_cast<int>(BitLength(numerator)) -
                   static_cast<int>(BitLength(denominator));
    if (estimate >= 0) {
        if (Compare(
                numerator,
                ShiftLeft(denominator, static_cast<unsigned>(estimate))) < 0) {
            --estimate;
        }
    } else if (
        Compare(
            ShiftLeft(numerator, static_cast<unsigned>(-estimate)),
            denominator) < 0) {
        --estimate;
    }
    return estimate;
}

Division ScaledDivision(
    UInt128 numerator,
    UInt128 denominator,
    int shift) noexcept {
    if (shift >= 0) {
        const unsigned amount = static_cast<unsigned>(shift);
        if (BitLength(numerator) + amount <= 128U) {
            return Divide(ShiftLeft(numerator, amount), denominator);
        }
    } else {
        const unsigned amount = static_cast<unsigned>(-shift);
        if (BitLength(denominator) + amount <= 128U) {
            return Divide(numerator, ShiftLeft(denominator, amount));
        }
        return {0U, numerator, denominator, true};
    }
    return {0U, numerator, denominator, true};
}

bool Increment(
    std::uint64_t quotient,
    UInt128 remainder,
    UInt128 denominator,
    bool sign,
    RoundingMode mode) noexcept {
    if (IsZero(remainder)) {
        return false;
    }
    if (mode == RoundingMode::NearestEven) {
        const int half = Compare(remainder, Subtract(denominator, remainder));
        return half > 0 || (half == 0 && (quotient & 1U) != 0U);
    }
    if (mode == RoundingMode::PlusInfinity) {
        return !sign;
    }
    if (mode == RoundingMode::MinusInfinity) {
        return sign;
    }
    return false;
}

Result64 Overflow(bool sign, RoundingMode mode, const Format& format) noexcept {
    const bool infinity =
        mode == RoundingMode::NearestEven ||
        (mode == RoundingMode::PlusInfinity && !sign) ||
        (mode == RoundingMode::MinusInfinity && sign);
    return {
        (sign ? format.sign_bit : 0U) |
            (infinity ? format.infinity : format.maximum_finite),
        kFpscrOfc | kFpscrIxc,
    };
}

Result64 RoundFinite(
    UInt128 numerator,
    UInt128 denominator,
    int exponent,
    bool sign,
    const Control& control,
    const Format& format) noexcept {
    if (IsZero(numerator)) {
        return {sign ? format.sign_bit : 0U, 0U};
    }
    int binary_exponent = FloorLog2Ratio(numerator, denominator) + exponent;
    if (control.flush_to_zero && binary_exponent < format.minimum_exponent) {
        return {0U, kFpscrUfc};
    }
    if (binary_exponent > format.maximum_exponent) {
        return Overflow(sign, control.rounding, format);
    }

    const unsigned fraction_bits = format.precision - 1U;
    if (binary_exponent >= format.minimum_exponent) {
        const int shift =
            exponent - binary_exponent + static_cast<int>(fraction_bits);
        const Division division = ScaledDivision(numerator, denominator, shift);
        std::uint64_t significand = division.quotient;
        if (Increment(
                significand,
                division.remainder,
                division.divisor,
                sign,
                control.rounding)) {
            ++significand;
        }
        const bool inexact = !IsZero(division.remainder);
        if (significand >= (std::uint64_t{1} << format.precision)) {
            significand >>= 1U;
            ++binary_exponent;
        }
        if (binary_exponent > format.maximum_exponent) {
            return Overflow(sign, control.rounding, format);
        }
        return {
            (sign ? format.sign_bit : 0U) |
                (static_cast<std::uint64_t>(binary_exponent + format.bias)
                 << fraction_bits) |
                (significand & format.fraction_mask),
            inexact ? kFpscrIxc : 0U,
        };
    }

    const int least_subnormal_exponent =
        format.minimum_exponent - static_cast<int>(fraction_bits);
    const Division division = ScaledDivision(
        numerator, denominator, exponent - least_subnormal_exponent);
    std::uint64_t significand = division.quotient;
    bool increment = false;
    if (!IsZero(division.remainder)) {
        if (division.virtual_large_divisor) {
            increment =
                (control.rounding == RoundingMode::PlusInfinity && !sign) ||
                (control.rounding == RoundingMode::MinusInfinity && sign);
        } else {
            increment = Increment(
                significand,
                division.remainder,
                division.divisor,
                sign,
                control.rounding);
        }
    }
    if (increment) {
        ++significand;
    }
    const bool inexact = !IsZero(division.remainder);
    if (significand >= (std::uint64_t{1} << fraction_bits)) {
        return {
            (sign ? format.sign_bit : 0U) |
                (std::uint64_t{1} << fraction_bits),
            inexact ? kFpscrIxc : 0U,
        };
    }
    return {
        (sign ? format.sign_bit : 0U) | significand,
        inexact ? (kFpscrUfc | kFpscrIxc) : 0U,
    };
}

Unpacked64 Unpack64(
    std::uint64_t bits,
    const Control& control,
    std::uint32_t* flags,
    bool arithmetic = true) noexcept {
    const bool sign = (bits & kSignBit64) != 0U;
    const std::uint64_t exponent = (bits >> 52U) & 0x7FFU;
    const std::uint64_t fraction = bits & kFractionMask64;
    if (exponent == 0x7FFU) {
        if (fraction == 0U) {
            return {ValueKind::Infinity, sign, 0U, 0, bits};
        }
        return {
            (fraction & kQuietBit64) != 0U ? ValueKind::QuietNan
                                          : ValueKind::SignalingNan,
            sign,
            0U,
            0,
            bits,
        };
    }
    if (exponent == 0U) {
        if (fraction == 0U) {
            return {ValueKind::Zero, sign, 0U, 0, bits};
        }
        if (arithmetic && control.flush_to_zero) {
            *flags |= kFpscrIdc;
            return {ValueKind::Zero, false, 0U, 0, 0U};
        }
        return {ValueKind::Finite, sign, fraction, -1074, bits};
    }
    return {
        ValueKind::Finite,
        sign,
        (std::uint64_t{1} << 52U) | fraction,
        static_cast<int>(exponent) - 1075,
        bits,
    };
}

Unpacked32 Unpack32(
    std::uint32_t bits,
    const Control& control,
    std::uint32_t* flags) noexcept {
    const bool sign = (bits & kSignBit32) != 0U;
    const std::uint32_t exponent = (bits >> 23U) & 0xFFU;
    const std::uint32_t fraction = bits & kFractionMask32;
    if (exponent == 0xFFU) {
        if (fraction == 0U) {
            return {ValueKind::Infinity, sign, 0U, 0, bits};
        }
        return {
            (fraction & kQuietBit32) != 0U ? ValueKind::QuietNan
                                          : ValueKind::SignalingNan,
            sign,
            0U,
            0,
            bits,
        };
    }
    if (exponent == 0U) {
        if (fraction == 0U) {
            return {ValueKind::Zero, sign, 0U, 0, bits};
        }
        if (control.flush_to_zero) {
            *flags |= kFpscrIdc;
            return {ValueKind::Zero, false, 0U, 0, 0U};
        }
        return {ValueKind::Finite, sign, fraction, -149, bits};
    }
    return {
        ValueKind::Finite,
        sign,
        (1U << 23U) | fraction,
        static_cast<int>(exponent) - 150,
        bits,
    };
}

constexpr bool IsNan(ValueKind kind) noexcept {
    return kind == ValueKind::QuietNan || kind == ValueKind::SignalingNan;
}

bool PropagateNan64(
    const Unpacked64* operands,
    std::size_t count,
    const Control& control,
    std::uint32_t flags,
    Result64* result) noexcept {
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
        *result = {kDefaultNan64, flags};
        return true;
    }
    for (ValueKind kind : {ValueKind::SignalingNan, ValueKind::QuietNan}) {
        for (std::size_t index = 0; index < count; ++index) {
            if (operands[index].kind == kind) {
                *result = {operands[index].bits | kQuietBit64, flags};
                return true;
            }
        }
    }
    return false;
}

constexpr Result64 Invalid64(std::uint32_t flags = 0U) noexcept {
    return {kDefaultNan64, flags | kFpscrIoc};
}

constexpr Result64 F64Neg(std::uint64_t bits) noexcept {
    return {bits ^ kSignBit64, 0U};
}

Result64 F64Add(
    std::uint64_t a,
    std::uint64_t b,
    const Control& control) noexcept {
    std::uint32_t flags = 0U;
    const std::array<Unpacked64, 2> values = {
        Unpack64(a, control, &flags),
        Unpack64(b, control, &flags),
    };
    const Unpacked64& left = values[0];
    const Unpacked64& right = values[1];
    Result64 nan{};
    if (PropagateNan64(values.data(), values.size(), control, flags, &nan)) {
        return nan;
    }
    if (left.kind == ValueKind::Infinity || right.kind == ValueKind::Infinity) {
        if (left.kind == ValueKind::Infinity &&
            right.kind == ValueKind::Infinity && left.sign != right.sign) {
            return Invalid64(flags);
        }
        const Unpacked64& value =
            left.kind == ValueKind::Infinity ? left : right;
        return {
            (value.sign ? kSignBit64 : 0U) | kPositiveInfinity64,
            flags,
        };
    }
    if (left.kind == ValueKind::Zero && right.kind == ValueKind::Zero) {
        const bool sign = left.sign == right.sign
                              ? left.sign
                              : control.rounding == RoundingMode::MinusInfinity;
        return {sign ? kSignBit64 : 0U, flags};
    }
    if (left.kind == ValueKind::Zero) {
        return {right.bits, flags};
    }
    if (right.kind == ValueKind::Zero) {
        return {left.bits, flags};
    }

    const int difference = left.exponent - right.exponent;
    UInt128 magnitude{};
    int common_exponent = 0;
    bool sign = false;
    if (difference >= -64 && difference <= 64) {
        common_exponent =
            left.exponent < right.exponent ? left.exponent : right.exponent;
        const UInt128 left_integer = ShiftLeft(
            {0U, left.significand},
            static_cast<unsigned>(left.exponent - common_exponent));
        const UInt128 right_integer = ShiftLeft(
            {0U, right.significand},
            static_cast<unsigned>(right.exponent - common_exponent));
        if (left.sign == right.sign) {
            magnitude = Add(left_integer, right_integer);
            sign = left.sign;
        } else {
            const int order = Compare(left_integer, right_integer);
            if (order == 0) {
                return {
                    control.rounding == RoundingMode::MinusInfinity
                        ? kSignBit64
                        : 0U,
                    flags,
                };
            }
            sign = order < 0 ? right.sign : left.sign;
            magnitude = order < 0 ? Subtract(right_integer, left_integer)
                                  : Subtract(left_integer, right_integer);
        }
    } else {
        const Unpacked64& large = difference > 0 ? left : right;
        const Unpacked64& small = difference > 0 ? right : left;
        const unsigned shift =
            static_cast<unsigned>(large.exponent - small.exponent);
        const std::uint64_t large_integer = large.significand << 3U;
        const std::uint64_t small_integer =
            ShiftRightJam(small.significand << 3U, shift);
        if (large.sign == small.sign) {
            magnitude = {0U, large_integer + small_integer};
            sign = large.sign;
        } else if (large_integer >= small_integer) {
            magnitude = {0U, large_integer - small_integer};
            sign = large.sign;
        } else {
            magnitude = {0U, small_integer - large_integer};
            sign = small.sign;
        }
        common_exponent = large.exponent - 3;
    }
    const Result64 rounded =
        RoundFinite(magnitude, {0U, 1U}, common_exponent, sign, control, kBinary64);
    return {rounded.value, flags | rounded.flags};
}

Result64 F64Sub(
    std::uint64_t a,
    std::uint64_t b,
    const Control& control) noexcept {
    std::uint32_t flags = 0U;
    const Unpacked64 right = Unpack64(b, control, &flags);
    const Result64 result = F64Add(a, right.bits ^ kSignBit64, control);
    return {result.value, result.flags | flags};
}

Result64 F64Mul(
    std::uint64_t a,
    std::uint64_t b,
    const Control& control) noexcept {
    std::uint32_t flags = 0U;
    const std::array<Unpacked64, 2> values = {
        Unpack64(a, control, &flags),
        Unpack64(b, control, &flags),
    };
    const Unpacked64& left = values[0];
    const Unpacked64& right = values[1];
    Result64 nan{};
    if (PropagateNan64(values.data(), values.size(), control, flags, &nan)) {
        return nan;
    }
    const bool sign = left.sign != right.sign;
    if ((left.kind == ValueKind::Zero && right.kind == ValueKind::Infinity) ||
        (left.kind == ValueKind::Infinity && right.kind == ValueKind::Zero)) {
        return Invalid64(flags);
    }
    if (left.kind == ValueKind::Infinity || right.kind == ValueKind::Infinity) {
        return {(sign ? kSignBit64 : 0U) | kPositiveInfinity64, flags};
    }
    if (left.kind == ValueKind::Zero || right.kind == ValueKind::Zero) {
        return {sign ? kSignBit64 : 0U, flags};
    }
    const Result64 rounded = RoundFinite(
        Multiply64(left.significand, right.significand),
        {0U, 1U},
        left.exponent + right.exponent,
        sign,
        control,
        kBinary64);
    return {rounded.value, flags | rounded.flags};
}

Result64 F64Div(
    std::uint64_t a,
    std::uint64_t b,
    const Control& control) noexcept {
    std::uint32_t flags = 0U;
    const std::array<Unpacked64, 2> values = {
        Unpack64(a, control, &flags),
        Unpack64(b, control, &flags),
    };
    const Unpacked64& left = values[0];
    const Unpacked64& right = values[1];
    Result64 nan{};
    if (PropagateNan64(values.data(), values.size(), control, flags, &nan)) {
        return nan;
    }
    const bool sign = left.sign != right.sign;
    if ((left.kind == ValueKind::Zero && right.kind == ValueKind::Zero) ||
        (left.kind == ValueKind::Infinity &&
         right.kind == ValueKind::Infinity)) {
        return Invalid64(flags);
    }
    if (left.kind == ValueKind::Infinity) {
        return {(sign ? kSignBit64 : 0U) | kPositiveInfinity64, flags};
    }
    if (right.kind == ValueKind::Infinity) {
        return {sign ? kSignBit64 : 0U, flags};
    }
    if (right.kind == ValueKind::Zero) {
        return {
            (sign ? kSignBit64 : 0U) | kPositiveInfinity64,
            flags | kFpscrDzc,
        };
    }
    if (left.kind == ValueKind::Zero) {
        return {sign ? kSignBit64 : 0U, flags};
    }
    const Result64 rounded = RoundFinite(
        {0U, left.significand},
        {0U, right.significand},
        left.exponent - right.exponent,
        sign,
        control,
        kBinary64);
    return {rounded.value, flags | rounded.flags};
}

std::uint64_t IntegerSquareRoot(UInt128 value) noexcept {
    std::uint64_t low = 0U;
    std::uint64_t high = std::uint64_t{1} << 53U;
    while (low < high) {
        const std::uint64_t middle = low + ((high - low + 1U) >> 1U);
        if (Compare(Multiply64(middle, middle), value) <= 0) {
            low = middle;
        } else {
            high = middle - 1U;
        }
    }
    return low;
}

int FloorDivideByTwo(int value) noexcept {
    return value >= 0 ? value / 2 : -((-value + 1) / 2);
}

Result64 F64Sqrt(std::uint64_t bits, const Control& control) noexcept {
    std::uint32_t flags = 0U;
    const Unpacked64 value = Unpack64(bits, control, &flags);
    Result64 nan{};
    if (PropagateNan64(&value, 1U, control, flags, &nan)) {
        return nan;
    }
    if (value.kind == ValueKind::Zero) {
        return {value.bits, flags};
    }
    if (value.sign) {
        return Invalid64(flags);
    }
    if (value.kind == ValueKind::Infinity) {
        return {kPositiveInfinity64, flags};
    }
    const int input_log2 =
        static_cast<int>(BitLength(value.significand)) - 1 + value.exponent;
    int output_exponent = FloorDivideByTwo(input_log2);
    const int shift = value.exponent - (2 * output_exponent) + 104;
    const UInt128 radicand =
        ShiftLeft({0U, value.significand}, static_cast<unsigned>(shift));
    std::uint64_t significand = IntegerSquareRoot(radicand);
    const UInt128 square = Multiply64(significand, significand);
    const UInt128 remainder = Subtract(radicand, square);
    if (!IsZero(remainder)) {
        bool increment = false;
        if (control.rounding == RoundingMode::NearestEven) {
            increment = Compare(remainder, {0U, significand}) > 0;
        } else if (control.rounding == RoundingMode::PlusInfinity) {
            increment = true;
        }
        if (increment) {
            ++significand;
        }
    }
    if (significand >= (std::uint64_t{1} << 53U)) {
        significand >>= 1U;
        ++output_exponent;
    }
    return {
        (static_cast<std::uint64_t>(output_exponent + 1023) << 52U) |
            (significand & kFractionMask64),
        flags | (!IsZero(remainder) ? kFpscrIxc : 0U),
    };
}

constexpr Result64 Combine(Result64 first, Result64 second) noexcept {
    return {second.value, first.flags | second.flags};
}

Result64 F64Mla(
    std::uint64_t accumulator,
    std::uint64_t left,
    std::uint64_t right,
    const Control& control) noexcept {
    const Result64 product = F64Mul(left, right, control);
    return Combine(product, F64Add(accumulator, product.value, control));
}

Result64 F64Mls(
    std::uint64_t accumulator,
    std::uint64_t left,
    std::uint64_t right,
    const Control& control) noexcept {
    const Result64 product = F64Mul(left, right, control);
    return Combine(
        product,
        F64Add(accumulator, F64Neg(product.value).value, control));
}

Result64 F64FromSigned(std::uint32_t value, const Control& control) noexcept {
    const std::int64_t signed_value = static_cast<std::int32_t>(value);
    const bool sign = signed_value < 0;
    const std::uint64_t magnitude = sign
                                        ? static_cast<std::uint64_t>(-signed_value)
                                        : static_cast<std::uint64_t>(signed_value);
    return RoundFinite(
        {0U, magnitude}, {0U, 1U}, 0, sign, control, kBinary64);
}

Result64 F64ToInteger(
    std::uint64_t bits,
    bool is_signed,
    const Control& control) noexcept {
    std::uint32_t flags = 0U;
    const Unpacked64 value = Unpack64(bits, control, &flags);
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
    bool overflow = false;
    if (value.exponent >= 0) {
        const unsigned shift = static_cast<unsigned>(value.exponent);
        if (shift >= 64U ||
            value.significand >
                (std::numeric_limits<std::uint64_t>::max() >> shift)) {
            overflow = true;
        } else {
            magnitude = value.significand << shift;
        }
    } else {
        const unsigned shift = static_cast<unsigned>(-value.exponent);
        if (shift >= 64U) {
            inexact = value.significand != 0U;
        } else {
            magnitude = value.significand >> shift;
            const std::uint64_t mask =
                shift == 0U ? 0U : (std::uint64_t{1} << shift) - 1U;
            inexact = (value.significand & mask) != 0U;
        }
    }
    if (is_signed) {
        const std::uint64_t limit = value.sign ? 0x80000000ULL : 0x7FFFFFFFULL;
        if (overflow || magnitude > limit) {
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
        if (overflow || magnitude != 0U) {
            return {0U, flags | kFpscrIoc};
        }
        return {0U, flags | (inexact ? kFpscrIxc : 0U)};
    }
    if (overflow || magnitude > 0xFFFFFFFFULL) {
        return {0xFFFFFFFFU, flags | kFpscrIoc};
    }
    return {
        static_cast<std::uint32_t>(magnitude),
        flags | (inexact ? kFpscrIxc : 0U),
    };
}

Result64 F64FromF32(std::uint32_t bits, const Control& control) noexcept {
    std::uint32_t flags = 0U;
    const Unpacked32 value = Unpack32(bits, control, &flags);
    if (IsNan(value.kind)) {
        if (value.kind == ValueKind::SignalingNan) {
            flags |= kFpscrIoc;
        }
        if (control.default_nan) {
            return {kDefaultNan64, flags};
        }
        const std::uint64_t payload =
            static_cast<std::uint64_t>(value.bits & ((1U << 22U) - 1U)) << 29U;
        return {
            (value.sign ? kSignBit64 : 0U) | kPositiveInfinity64 |
                kQuietBit64 | payload,
            flags,
        };
    }
    if (value.kind == ValueKind::Infinity) {
        return {
            (value.sign ? kSignBit64 : 0U) | kPositiveInfinity64,
            flags,
        };
    }
    if (value.kind == ValueKind::Zero) {
        return {value.sign ? kSignBit64 : 0U, flags};
    }
    const Result64 rounded = RoundFinite(
        {0U, value.significand},
        {0U, 1U},
        value.exponent,
        value.sign,
        control,
        kBinary64);
    return {rounded.value, flags | rounded.flags};
}

Result64 F32FromF64(std::uint64_t bits, const Control& control) noexcept {
    std::uint32_t flags = 0U;
    const Unpacked64 value = Unpack64(bits, control, &flags);
    if (IsNan(value.kind)) {
        if (value.kind == ValueKind::SignalingNan) {
            flags |= kFpscrIoc;
        }
        if (control.default_nan) {
            return {kDefaultNan32, flags};
        }
        const std::uint32_t payload = static_cast<std::uint32_t>(
            (value.bits >> 29U) & ((std::uint64_t{1} << 22U) - 1U));
        return {
            (value.sign ? kSignBit32 : 0U) | kPositiveInfinity32 |
                kQuietBit32 | payload,
            flags,
        };
    }
    if (value.kind == ValueKind::Infinity) {
        return {
            (value.sign ? kSignBit32 : 0U) | kPositiveInfinity32,
            flags,
        };
    }
    if (value.kind == ValueKind::Zero) {
        return {value.sign ? kSignBit32 : 0U, flags};
    }
    const Result64 rounded = RoundFinite(
        {0U, value.significand},
        {0U, 1U},
        value.exponent,
        value.sign,
        control,
        kBinary32);
    return {rounded.value, flags | rounded.flags};
}

ValueKind Classify64(std::uint64_t bits) noexcept {
    const std::uint64_t exponent = bits & kExponentMask64;
    const std::uint64_t fraction = bits & kFractionMask64;
    if (exponent == kExponentMask64) {
        if (fraction == 0U) {
            return ValueKind::Infinity;
        }
        return (fraction & kQuietBit64) != 0U ? ValueKind::QuietNan
                                              : ValueKind::SignalingNan;
    }
    if (exponent == 0U && fraction == 0U) {
        return ValueKind::Zero;
    }
    return ValueKind::Finite;
}

Result64 F64Compare(
    std::uint64_t a,
    std::uint64_t b,
    bool signal_all_nans) noexcept {
    const ValueKind left_kind = Classify64(a);
    const ValueKind right_kind = Classify64(b);
    if (IsNan(left_kind) || IsNan(right_kind)) {
        const bool invalid = left_kind == ValueKind::SignalingNan ||
                             right_kind == ValueKind::SignalingNan ||
                             signal_all_nans;
        return {kNzcvUnordered, invalid ? kFpscrIoc : 0U};
    }
    if ((a & ~kSignBit64) == 0U && (b & ~kSignBit64) == 0U) {
        return {kNzcvEqual, 0U};
    }
    if (a == b) {
        return {kNzcvEqual, 0U};
    }
    const bool left_sign = (a & kSignBit64) != 0U;
    const bool right_sign = (b & kSignBit64) != 0U;
    if (left_sign != right_sign) {
        return {left_sign ? kNzcvLess : kNzcvGreater, 0U};
    }
    const bool less = left_sign ? (a & ~kSignBit64) > (b & ~kSignBit64) : a < b;
    return {less ? kNzcvLess : kNzcvGreater, 0U};
}

std::uint64_t ReadDouble(const GuestState& state, std::uint8_t index) noexcept {
    const std::size_t lane = static_cast<std::size_t>(index) * 2U;
    return static_cast<std::uint64_t>(state.vfp[lane]) |
           (static_cast<std::uint64_t>(state.vfp[lane + 1U]) << 32U);
}

void CommitDouble(
    GuestState& state,
    std::uint8_t destination,
    Result64 result) noexcept {
    const std::size_t lane = static_cast<std::size_t>(destination) * 2U;
    state.vfp[lane] = static_cast<std::uint32_t>(result.value);
    state.vfp[lane + 1U] = static_cast<std::uint32_t>(result.value >> 32U);
    state.fpscr |= result.flags & kExceptionFlags;
}

void CommitSingle(
    GuestState& state,
    std::uint8_t destination,
    Result64 result) noexcept {
    state.vfp[destination] = static_cast<std::uint32_t>(result.value);
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

bool HasTernaryDoubleRegisters(std::uint32_t raw) noexcept {
    return (raw &
            (kDoubleDestinationBit | kDoubleLeftBit | kDoubleRightBit)) == 0U;
}

bool HasUnaryDoubleRegisters(std::uint32_t raw) noexcept {
    return (raw & (kDoubleDestinationBit | kDoubleRightBit)) == 0U;
}

}  // namespace

bool VfpBinary64Supported(std::uint32_t raw) noexcept {
    if ((raw & kConditionMask) == kConditionMask) {
        return false;
    }
    switch (raw & kTernaryMask) {
    case 0x0E000B00U:
    case 0x0E000B40U:
    case 0x0E200B00U:
    case 0x0E300B00U:
    case 0x0E300B40U:
    case 0x0E800B00U:
        return HasTernaryDoubleRegisters(raw);
    default:
        break;
    }
    switch (raw & kUnaryMask) {
    case 0x0EB10B40U:
    case 0x0EB10BC0U:
    case 0x0EB40B40U:
    case 0x0EB40BC0U:
        return HasUnaryDoubleRegisters(raw);
    case 0x0EB80BC0U:
    case 0x0EB70AC0U:
        return (raw & kDoubleDestinationBit) == 0U;
    case 0x0EBD0BC0U:
    case 0x0EBC0BC0U:
    case 0x0EB70BC0U:
        return (raw & kDoubleRightBit) == 0U;
    default:
        return false;
    }
}

ExecutionResult ExecuteVfpBinary64(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state) {
    if (!VfpBinary64Supported(raw) ||
        (state.fpscr &
         (kFpscrVectorModeMask | kFpscrExceptionEnableMask)) != 0U) {
        return Unsupported(state, raw, pc);
    }

    const Control control = ControlFromFpscr(state.fpscr);
    const std::uint8_t destination = DestinationDouble(raw);
    const std::uint8_t left = LeftDouble(raw);
    const std::uint8_t right = RightDouble(raw);
    Result64 result{};
    switch (raw & kTernaryMask) {
    case 0x0E000B00U:
        result = F64Mla(
            ReadDouble(state, destination),
            ReadDouble(state, left),
            ReadDouble(state, right),
            control);
        break;
    case 0x0E000B40U:
        result = F64Mls(
            ReadDouble(state, destination),
            ReadDouble(state, left),
            ReadDouble(state, right),
            control);
        break;
    case 0x0E200B00U:
        result = F64Mul(ReadDouble(state, left), ReadDouble(state, right), control);
        break;
    case 0x0E300B00U:
        result = F64Add(ReadDouble(state, left), ReadDouble(state, right), control);
        break;
    case 0x0E300B40U:
        result = F64Sub(ReadDouble(state, left), ReadDouble(state, right), control);
        break;
    case 0x0E800B00U:
        result = F64Div(ReadDouble(state, left), ReadDouble(state, right), control);
        break;
    default: {
        const std::uint32_t unary = raw & kUnaryMask;
        switch (unary) {
        case 0x0EB10B40U:
            result = F64Neg(ReadDouble(state, right));
            break;
        case 0x0EB10BC0U:
            result = F64Sqrt(ReadDouble(state, right), control);
            break;
        case 0x0EB80BC0U:
            result = F64FromSigned(state.vfp[RightLane(raw)], control);
            break;
        case 0x0EBD0BC0U:
            CommitSingle(
                state,
                DestinationLane(raw),
                F64ToInteger(ReadDouble(state, right), true, control));
            return Fallthrough(state, pc);
        case 0x0EBC0BC0U:
            CommitSingle(
                state,
                DestinationLane(raw),
                F64ToInteger(ReadDouble(state, right), false, control));
            return Fallthrough(state, pc);
        case 0x0EB70AC0U:
            result = F64FromF32(state.vfp[RightLane(raw)], control);
            break;
        case 0x0EB70BC0U:
            CommitSingle(
                state,
                DestinationLane(raw),
                F32FromF64(ReadDouble(state, right), control));
            return Fallthrough(state, pc);
        case 0x0EB40B40U:
        case 0x0EB40BC0U: {
            const Result64 comparison = F64Compare(
                ReadDouble(state, destination),
                ReadDouble(state, right),
                unary == 0x0EB40BC0U);
            state.fpscr =
                (state.fpscr & ~kNzcvMask) |
                (static_cast<std::uint32_t>(comparison.value) & kNzcvMask);
            state.fpscr |= comparison.flags & kExceptionFlags;
            return Fallthrough(state, pc);
        }
        default:
            return Unsupported(state, raw, pc);
        }
        break;
    }
    }
    CommitDouble(state, destination, result);
    return Fallthrough(state, pc);
}

}  // namespace oot3d::recomp::a32
