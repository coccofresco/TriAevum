"""Integer-only IEEE-754 binary64 operations for the ARM11 VFP path.

The implementation mirrors :mod:`oot3d_pack.arm_softfloat` but keeps raw
64-bit values throughout.  Python integers provide the exact intermediate
precision used as the reference oracle for the portable C++ executor; host
``float``/``double`` arithmetic is never used.
"""

from __future__ import annotations

from dataclasses import dataclass
from math import isqrt

from .arm_softfloat import (
    DEFAULT_CONTROL,
    DZC,
    FPControl,
    FPResult,
    IDC,
    IOC,
    IXC,
    NZCV_EQUAL,
    NZCV_GREATER,
    NZCV_LESS,
    NZCV_UNORDERED,
    OFC,
    RoundingMode,
    UFC,
)


MASK32 = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
SIGN64 = 1 << 63
EXP64 = 0x7FF0000000000000
FRAC64 = 0x000FFFFFFFFFFFFF
QUIET64 = 1 << 51
INF64 = EXP64
MAX_FINITE64 = 0x7FEFFFFFFFFFFFFF
DEFAULT_NAN64 = 0x7FF8000000000000

SIGN32 = 1 << 31
EXP32 = 0x7F800000
FRAC32 = 0x007FFFFF
QUIET32 = 1 << 22
DEFAULT_NAN32 = 0x7FC00000


@dataclass(frozen=True, slots=True)
class _Unpacked:
    kind: str
    sign: bool
    significand: int = 0
    exponent: int = 0
    bits: int = 0


def _control(control: FPControl | int | None) -> FPControl:
    if control is None:
        return DEFAULT_CONTROL
    if isinstance(control, FPControl):
        return control
    return FPControl.from_fpscr(control)


def _rounding(value: RoundingMode | int) -> RoundingMode:
    return value if isinstance(value, RoundingMode) else RoundingMode(value)


def _unpack64(
    bits: int, control: FPControl, *, arithmetic: bool = True
) -> tuple[_Unpacked, int]:
    bits &= MASK64
    sign = bool(bits & SIGN64)
    exponent_field = (bits >> 52) & 0x7FF
    fraction = bits & FRAC64
    if exponent_field == 0x7FF:
        if fraction == 0:
            return _Unpacked("infinity", sign, bits=bits), 0
        kind = "qnan" if fraction & QUIET64 else "snan"
        return _Unpacked(kind, sign, bits=bits), 0
    if exponent_field == 0:
        if fraction == 0:
            return _Unpacked("zero", sign, bits=bits), 0
        if arithmetic and control.flush_to_zero:
            return _Unpacked("zero", False, bits=0), IDC
        return _Unpacked("finite", sign, fraction, -1074, bits), 0
    return (
        _Unpacked(
            "finite",
            sign,
            (1 << 52) | fraction,
            exponent_field - 1075,
            bits,
        ),
        0,
    )


def _unpack32(
    bits: int, control: FPControl, *, arithmetic: bool = True
) -> tuple[_Unpacked, int]:
    bits &= MASK32
    sign = bool(bits & SIGN32)
    exponent_field = (bits >> 23) & 0xFF
    fraction = bits & FRAC32
    if exponent_field == 0xFF:
        if fraction == 0:
            return _Unpacked("infinity", sign, bits=bits), 0
        kind = "qnan" if fraction & QUIET32 else "snan"
        return _Unpacked(kind, sign, bits=bits), 0
    if exponent_field == 0:
        if fraction == 0:
            return _Unpacked("zero", sign, bits=bits), 0
        if arithmetic and control.flush_to_zero:
            return _Unpacked("zero", False, bits=0), IDC
        return _Unpacked("finite", sign, fraction, -149, bits), 0
    return (
        _Unpacked(
            "finite",
            sign,
            (1 << 23) | fraction,
            exponent_field - 150,
            bits,
        ),
        0,
    )


def _nan64(
    operands: tuple[_Unpacked, ...], control: FPControl, flags: int
) -> FPResult | None:
    nans = tuple(value for value in operands if value.kind in {"snan", "qnan"})
    if not nans:
        return None
    if any(value.kind == "snan" for value in nans):
        flags |= IOC
    if control.default_nan:
        return FPResult(DEFAULT_NAN64, flags)
    for kind in ("snan", "qnan"):
        for value in operands:
            if value.kind == kind:
                return FPResult((value.bits | QUIET64) & MASK64, flags)
    raise AssertionError("unreachable NaN selection")


def _floor_log2_ratio(numerator: int, denominator: int) -> int:
    estimate = numerator.bit_length() - denominator.bit_length()
    if estimate >= 0:
        if numerator < denominator << estimate:
            estimate -= 1
    elif numerator << -estimate < denominator:
        estimate -= 1
    return estimate


def _scaled_division(
    numerator: int, denominator: int, shift: int
) -> tuple[int, int, int]:
    if shift >= 0:
        numerator <<= shift
    else:
        denominator <<= -shift
    quotient, remainder = divmod(numerator, denominator)
    return quotient, remainder, denominator


def _increment(
    quotient: int,
    remainder: int,
    denominator: int,
    sign: bool,
    mode: RoundingMode,
) -> bool:
    if remainder == 0:
        return False
    if mode == RoundingMode.NEAREST_EVEN:
        twice = remainder << 1
        return twice > denominator or (twice == denominator and bool(quotient & 1))
    if mode == RoundingMode.PLUS_INFINITY:
        return not sign
    if mode == RoundingMode.MINUS_INFINITY:
        return sign
    return False


def _overflow(
    sign: bool,
    control: FPControl,
    *,
    sign_bit: int,
    infinity: int,
    max_finite: int,
) -> FPResult:
    mode = control.rounding
    to_infinity = (
        mode == RoundingMode.NEAREST_EVEN
        or (mode == RoundingMode.PLUS_INFINITY and not sign)
        or (mode == RoundingMode.MINUS_INFINITY and sign)
    )
    magnitude = infinity if to_infinity else max_finite
    return FPResult((sign_bit if sign else 0) | magnitude, OFC | IXC)


def _round_finite(
    numerator: int,
    denominator: int,
    exponent: int,
    sign: bool,
    control: FPControl,
    *,
    precision: int,
    minimum_exponent: int,
    maximum_exponent: int,
    bias: int,
    sign_bit: int,
    fraction_mask: int,
    infinity: int,
    max_finite: int,
) -> FPResult:
    if numerator == 0:
        return FPResult(sign_bit if sign else 0)
    binary_exponent = _floor_log2_ratio(numerator, denominator) + exponent
    if control.flush_to_zero and binary_exponent < minimum_exponent:
        return FPResult(0, UFC)
    if binary_exponent > maximum_exponent:
        return _overflow(
            sign,
            control,
            sign_bit=sign_bit,
            infinity=infinity,
            max_finite=max_finite,
        )

    fraction_bits = precision - 1
    if binary_exponent >= minimum_exponent:
        shift = exponent - binary_exponent + fraction_bits
        significand, remainder, divisor = _scaled_division(
            numerator, denominator, shift
        )
        if _increment(significand, remainder, divisor, sign, control.rounding):
            significand += 1
        inexact = remainder != 0
        if significand >= 1 << precision:
            significand >>= 1
            binary_exponent += 1
        if binary_exponent > maximum_exponent:
            return _overflow(
                sign,
                control,
                sign_bit=sign_bit,
                infinity=infinity,
                max_finite=max_finite,
            )
        return FPResult(
            (sign_bit if sign else 0)
            | ((binary_exponent + bias) << fraction_bits)
            | (significand & fraction_mask),
            IXC if inexact else 0,
        )

    least_subnormal_exponent = minimum_exponent - fraction_bits
    significand, remainder, divisor = _scaled_division(
        numerator, denominator, exponent - least_subnormal_exponent
    )
    if _increment(significand, remainder, divisor, sign, control.rounding):
        significand += 1
    inexact = remainder != 0
    if significand >= 1 << fraction_bits:
        return FPResult(
            (sign_bit if sign else 0) | (1 << fraction_bits),
            IXC if inexact else 0,
        )
    return FPResult(
        (sign_bit if sign else 0) | significand,
        (UFC | IXC) if inexact else 0,
    )


def _round64(
    numerator: int,
    denominator: int,
    exponent: int,
    sign: bool,
    control: FPControl,
) -> FPResult:
    return _round_finite(
        numerator,
        denominator,
        exponent,
        sign,
        control,
        precision=53,
        minimum_exponent=-1022,
        maximum_exponent=1023,
        bias=1023,
        sign_bit=SIGN64,
        fraction_mask=FRAC64,
        infinity=INF64,
        max_finite=MAX_FINITE64,
    )


def _round32(
    numerator: int,
    denominator: int,
    exponent: int,
    sign: bool,
    control: FPControl,
) -> FPResult:
    return _round_finite(
        numerator,
        denominator,
        exponent,
        sign,
        control,
        precision=24,
        minimum_exponent=-126,
        maximum_exponent=127,
        bias=127,
        sign_bit=SIGN32,
        fraction_mask=FRAC32,
        infinity=EXP32,
        max_finite=0x7F7FFFFF,
    )


def f64_neg(bits: int, control: FPControl | int | None = None) -> FPResult:
    del control
    return FPResult((bits ^ SIGN64) & MASK64)


def f64_add(a: int, b: int, control: FPControl | int | None = None) -> FPResult:
    control = _control(control)
    left, left_flags = _unpack64(a, control)
    right, right_flags = _unpack64(b, control)
    flags = left_flags | right_flags
    nan = _nan64((left, right), control, flags)
    if nan is not None:
        return nan
    if left.kind == "infinity" or right.kind == "infinity":
        if left.kind == right.kind == "infinity" and left.sign != right.sign:
            return FPResult(DEFAULT_NAN64, flags | IOC)
        value = left if left.kind == "infinity" else right
        return FPResult((SIGN64 if value.sign else 0) | INF64, flags)
    if left.kind == right.kind == "zero":
        sign = (
            left.sign
            if left.sign == right.sign
            else control.rounding == RoundingMode.MINUS_INFINITY
        )
        return FPResult(SIGN64 if sign else 0, flags)
    if left.kind == "zero":
        return FPResult(right.bits, flags)
    if right.kind == "zero":
        return FPResult(left.bits, flags)

    common_exponent = min(left.exponent, right.exponent)
    left_integer = left.significand << (left.exponent - common_exponent)
    right_integer = right.significand << (right.exponent - common_exponent)
    total = (-left_integer if left.sign else left_integer) + (
        -right_integer if right.sign else right_integer
    )
    if total == 0:
        sign = control.rounding == RoundingMode.MINUS_INFINITY
        return FPResult(SIGN64 if sign else 0, flags)
    rounded = _round64(abs(total), 1, common_exponent, total < 0, control)
    return FPResult(rounded.value, flags | rounded.flags)


def f64_sub(a: int, b: int, control: FPControl | int | None = None) -> FPResult:
    control = _control(control)
    right, flags = _unpack64(b, control)
    result = f64_add(a, right.bits ^ SIGN64, control)
    return FPResult(result.value, result.flags | flags)


def f64_mul(a: int, b: int, control: FPControl | int | None = None) -> FPResult:
    control = _control(control)
    left, left_flags = _unpack64(a, control)
    right, right_flags = _unpack64(b, control)
    flags = left_flags | right_flags
    nan = _nan64((left, right), control, flags)
    if nan is not None:
        return nan
    sign = left.sign ^ right.sign
    if (left.kind == "zero" and right.kind == "infinity") or (
        left.kind == "infinity" and right.kind == "zero"
    ):
        return FPResult(DEFAULT_NAN64, flags | IOC)
    if left.kind == "infinity" or right.kind == "infinity":
        return FPResult((SIGN64 if sign else 0) | INF64, flags)
    if left.kind == "zero" or right.kind == "zero":
        return FPResult(SIGN64 if sign else 0, flags)
    rounded = _round64(
        left.significand * right.significand,
        1,
        left.exponent + right.exponent,
        sign,
        control,
    )
    return FPResult(rounded.value, flags | rounded.flags)


def f64_div(a: int, b: int, control: FPControl | int | None = None) -> FPResult:
    control = _control(control)
    left, left_flags = _unpack64(a, control)
    right, right_flags = _unpack64(b, control)
    flags = left_flags | right_flags
    nan = _nan64((left, right), control, flags)
    if nan is not None:
        return nan
    sign = left.sign ^ right.sign
    if left.kind == right.kind == "zero" or (left.kind == right.kind == "infinity"):
        return FPResult(DEFAULT_NAN64, flags | IOC)
    if left.kind == "infinity":
        return FPResult((SIGN64 if sign else 0) | INF64, flags)
    if right.kind == "infinity":
        return FPResult(SIGN64 if sign else 0, flags)
    if right.kind == "zero":
        return FPResult((SIGN64 if sign else 0) | INF64, flags | DZC)
    if left.kind == "zero":
        return FPResult(SIGN64 if sign else 0, flags)
    rounded = _round64(
        left.significand,
        right.significand,
        left.exponent - right.exponent,
        sign,
        control,
    )
    return FPResult(rounded.value, flags | rounded.flags)


def f64_sqrt(bits: int, control: FPControl | int | None = None) -> FPResult:
    control = _control(control)
    value, flags = _unpack64(bits, control)
    nan = _nan64((value,), control, flags)
    if nan is not None:
        return nan
    if value.kind == "zero":
        return FPResult(value.bits, flags)
    if value.sign:
        return FPResult(DEFAULT_NAN64, flags | IOC)
    if value.kind == "infinity":
        return FPResult(INF64, flags)

    input_log2 = value.significand.bit_length() - 1 + value.exponent
    output_exponent = input_log2 // 2
    shift = value.exponent - 2 * output_exponent + 104
    radicand = value.significand << shift
    significand = isqrt(radicand)
    remainder = radicand - significand * significand
    if remainder:
        increment = False
        if control.rounding == RoundingMode.NEAREST_EVEN:
            increment = (radicand << 2) > (
                (significand * significand << 2) + (significand << 2) + 1
            )
        elif control.rounding == RoundingMode.PLUS_INFINITY:
            increment = True
        if increment:
            significand += 1
    if significand >= 1 << 53:
        significand >>= 1
        output_exponent += 1
    return FPResult(
        ((output_exponent + 1023) << 52) | (significand & FRAC64),
        flags | (IXC if remainder else 0),
    )


def f64_mla(
    accumulator: int,
    left: int,
    right: int,
    control: FPControl | int | None = None,
) -> FPResult:
    product = f64_mul(left, right, control)
    result = f64_add(accumulator, product.value, control)
    return FPResult(result.value, product.flags | result.flags)


def f64_mls(
    accumulator: int,
    left: int,
    right: int,
    control: FPControl | int | None = None,
) -> FPResult:
    product = f64_mul(left, right, control)
    result = f64_add(accumulator, f64_neg(product.value).value, control)
    return FPResult(result.value, product.flags | result.flags)


def f64_from_i32(value: int, control: FPControl | int | None = None) -> FPResult:
    control = _control(control)
    signed = value & MASK32
    if signed & SIGN32:
        signed -= 1 << 32
    return _round64(abs(signed), 1, 0, signed < 0, control)


def _round_to_integer(
    significand: int,
    exponent: int,
    sign: bool,
    mode: RoundingMode,
) -> tuple[int, bool]:
    if exponent >= 0:
        magnitude = significand << exponent
        return (-magnitude if sign else magnitude), False
    divisor = 1 << -exponent
    magnitude, remainder = divmod(significand, divisor)
    if _increment(magnitude, remainder, divisor, sign, mode):
        magnitude += 1
    return (-magnitude if sign else magnitude), remainder != 0


def _f64_to_int(
    bits: int,
    signed: bool,
    control: FPControl | int | None,
    rounding: RoundingMode | int | None,
) -> FPResult:
    control = _control(control)
    mode = control.rounding if rounding is None else _rounding(rounding)
    value, flags = _unpack64(bits, control)
    if value.kind in {"snan", "qnan"}:
        return FPResult(0, flags | IOC)
    if value.kind == "zero":
        return FPResult(0, flags)
    if value.kind == "infinity":
        saturated = (
            (0x80000000 if value.sign else 0x7FFFFFFF)
            if signed
            else (0 if value.sign else MASK32)
        )
        return FPResult(saturated, flags | IOC)
    integer, inexact = _round_to_integer(
        value.significand, value.exponent, value.sign, mode
    )
    minimum = -(1 << 31) if signed else 0
    maximum = (1 << 31) - 1 if signed else MASK32
    if integer < minimum:
        return FPResult(minimum & MASK32, flags | IOC)
    if integer > maximum:
        return FPResult(maximum & MASK32, flags | IOC)
    return FPResult(integer & MASK32, flags | (IXC if inexact else 0))


def f64_to_i32(
    bits: int,
    control: FPControl | int | None = None,
    *,
    rounding: RoundingMode | int | None = RoundingMode.TOWARD_ZERO,
) -> FPResult:
    return _f64_to_int(bits, True, control, rounding)


def f64_to_u32(
    bits: int,
    control: FPControl | int | None = None,
    *,
    rounding: RoundingMode | int | None = RoundingMode.TOWARD_ZERO,
) -> FPResult:
    return _f64_to_int(bits, False, control, rounding)


def f64_compare(
    a: int,
    b: int,
    control: FPControl | int | None = None,
    *,
    signal_all_nans: bool = False,
) -> FPResult:
    del control
    a &= MASK64
    b &= MASK64
    left, _ = _unpack64(a, DEFAULT_CONTROL, arithmetic=False)
    right, _ = _unpack64(b, DEFAULT_CONTROL, arithmetic=False)
    if left.kind in {"snan", "qnan"} or right.kind in {"snan", "qnan"}:
        invalid = left.kind == "snan" or right.kind == "snan" or signal_all_nans
        return FPResult(NZCV_UNORDERED, IOC if invalid else 0)
    if (a & ~SIGN64) == 0 and (b & ~SIGN64) == 0:
        return FPResult(NZCV_EQUAL)
    if a == b:
        return FPResult(NZCV_EQUAL)
    left_sign = bool(a & SIGN64)
    right_sign = bool(b & SIGN64)
    if left_sign != right_sign:
        return FPResult(NZCV_LESS if left_sign else NZCV_GREATER)
    less = (a & ~SIGN64) > (b & ~SIGN64) if left_sign else a < b
    return FPResult(NZCV_LESS if less else NZCV_GREATER)


def f64_from_f32(bits: int, control: FPControl | int | None = None) -> FPResult:
    control = _control(control)
    value, flags = _unpack32(bits, control)
    if value.kind in {"snan", "qnan"}:
        if value.kind == "snan":
            flags |= IOC
        if control.default_nan:
            return FPResult(DEFAULT_NAN64, flags)
        sign = SIGN64 if value.sign else 0
        payload = (value.bits & ((1 << 22) - 1)) << 29
        return FPResult(sign | INF64 | QUIET64 | payload, flags)
    if value.kind == "infinity":
        return FPResult((SIGN64 if value.sign else 0) | INF64, flags)
    if value.kind == "zero":
        return FPResult(SIGN64 if value.sign else 0, flags)
    result = _round64(value.significand, 1, value.exponent, value.sign, control)
    return FPResult(result.value, flags | result.flags)


def f32_from_f64(bits: int, control: FPControl | int | None = None) -> FPResult:
    control = _control(control)
    value, flags = _unpack64(bits, control)
    if value.kind in {"snan", "qnan"}:
        if value.kind == "snan":
            flags |= IOC
        if control.default_nan:
            return FPResult(DEFAULT_NAN32, flags)
        sign = SIGN32 if value.sign else 0
        payload = (value.bits >> 29) & ((1 << 22) - 1)
        return FPResult(sign | EXP32 | QUIET32 | payload, flags)
    if value.kind == "infinity":
        return FPResult((SIGN32 if value.sign else 0) | EXP32, flags)
    if value.kind == "zero":
        return FPResult(SIGN32 if value.sign else 0, flags)
    result = _round32(value.significand, 1, value.exponent, value.sign, control)
    return FPResult(result.value, flags | result.flags)


__all__ = [
    "DEFAULT_NAN64",
    "f32_from_f64",
    "f64_add",
    "f64_compare",
    "f64_div",
    "f64_from_f32",
    "f64_from_i32",
    "f64_mla",
    "f64_mls",
    "f64_mul",
    "f64_neg",
    "f64_sqrt",
    "f64_sub",
    "f64_to_i32",
    "f64_to_u32",
]
