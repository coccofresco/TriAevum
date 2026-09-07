"""Integer-only IEEE-754 binary32 operations for the ARM11 VFP path.

The public functions consume and produce raw 32-bit words.  Each operation is
pure and returns the newly raised cumulative exception bits; callers make them
sticky with :func:`accumulate_fpscr`.  Exception traps and VFP vector mode are
deliberately outside this module.

This models the VFP11 choices used by the 3DS ARM11: with FZ enabled an input
subnormal is positive zero and raises IDC, while a tiny arithmetic result is
positive zero and raises UFC (without IXC).  Comparisons are non-arithmetic and
therefore do not flush subnormal operands.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from math import isqrt
from typing import NamedTuple


MASK32 = 0xFFFFFFFF
SIGN_BIT = 0x80000000
EXP_MASK = 0x7F800000
FRAC_MASK = 0x007FFFFF
QUIET_BIT = 0x00400000
POSITIVE_INFINITY = 0x7F800000
MAX_FINITE = 0x7F7FFFFF
DEFAULT_NAN = 0x7FC00000

# FPSCR cumulative exception bits.  Results use these native bit positions so
# they can be ORed into FPSCR without translation.
IOC = 1 << 0
DZC = 1 << 1
OFC = 1 << 2
UFC = 1 << 3
IXC = 1 << 4
IDC = 1 << 7
EXCEPTION_FLAGS = IOC | DZC | OFC | UFC | IXC | IDC

FPSCR_RMODE_MASK = 3 << 22
FPSCR_FZ = 1 << 24
FPSCR_DN = 1 << 25

NZCV_EQUAL = 0x60000000
NZCV_LESS = 0x80000000
NZCV_GREATER = 0x20000000
NZCV_UNORDERED = 0x30000000


class RoundingMode(IntEnum):
    """FPSCR.RMode encodings."""

    NEAREST_EVEN = 0
    PLUS_INFINITY = 1
    MINUS_INFINITY = 2
    TOWARD_ZERO = 3

    # Short ARM-manual spellings.
    RN = NEAREST_EVEN
    RP = PLUS_INFINITY
    RM = MINUS_INFINITY
    RZ = TOWARD_ZERO


@dataclass(frozen=True, slots=True)
class FPControl:
    rounding: RoundingMode = RoundingMode.NEAREST_EVEN
    default_nan: bool = False
    flush_to_zero: bool = False

    @classmethod
    def from_fpscr(cls, fpscr: int) -> "FPControl":
        return cls(
            rounding=RoundingMode((fpscr >> 22) & 3),
            default_nan=bool(fpscr & FPSCR_DN),
            flush_to_zero=bool(fpscr & FPSCR_FZ),
        )


DEFAULT_CONTROL = FPControl()


class FPResult(NamedTuple):
    """A raw result word (or NZCV for compare) and newly raised FPSCR flags."""

    value: int
    flags: int = 0

    @property
    def bits(self) -> int:
        return self.value

    def apply_fpscr(self, fpscr: int) -> int:
        return accumulate_fpscr(fpscr, self.flags)


@dataclass(frozen=True, slots=True)
class _Unpacked:
    kind: str
    sign: bool
    significand: int = 0
    exponent: int = 0
    bits: int = 0


def accumulate_fpscr(fpscr: int, flags: int) -> int:
    """OR newly raised cumulative flags into an existing FPSCR word."""

    return ((fpscr & MASK32) | (flags & EXCEPTION_FLAGS)) & MASK32


def _control(control: FPControl | int | None) -> FPControl:
    if control is None:
        return DEFAULT_CONTROL
    if isinstance(control, FPControl):
        return control
    return FPControl.from_fpscr(control)


def _rounding(value: RoundingMode | int) -> RoundingMode:
    return value if isinstance(value, RoundingMode) else RoundingMode(value)


def _classify(bits: int) -> str:
    exponent = bits & EXP_MASK
    fraction = bits & FRAC_MASK
    if exponent == EXP_MASK:
        if fraction == 0:
            return "infinity"
        return "qnan" if fraction & QUIET_BIT else "snan"
    if exponent == 0:
        return "zero" if fraction == 0 else "finite"
    return "finite"


def _unpack(bits: int, control: FPControl, *, arithmetic: bool = True) -> tuple[_Unpacked, int]:
    bits &= MASK32
    sign = bool(bits & SIGN_BIT)
    exponent_field = (bits >> 23) & 0xFF
    fraction = bits & FRAC_MASK
    if exponent_field == 0xFF:
        if fraction == 0:
            return _Unpacked("infinity", sign, bits=bits), 0
        kind = "qnan" if fraction & QUIET_BIT else "snan"
        return _Unpacked(kind, sign, bits=bits), 0
    if exponent_field == 0:
        if fraction == 0:
            return _Unpacked("zero", sign, bits=bits), 0
        if arithmetic and control.flush_to_zero:
            # This is an explicit VFP11 implementation choice: the replacement
            # zero is positive, not a sign-preserving IEEE zero.
            return _Unpacked("zero", False, bits=0), IDC
        return _Unpacked("finite", sign, fraction, -149, bits), 0
    return _Unpacked(
        "finite", sign, (1 << 23) | fraction, exponent_field - 150, bits
    ), 0


def _quiet_nan(bits: int) -> int:
    return (bits | QUIET_BIT) & MASK32


def _nan_result(operands: tuple[_Unpacked, ...], control: FPControl, flags: int) -> FPResult | None:
    nans = tuple(value for value in operands if value.kind in ("snan", "qnan"))
    if not nans:
        return None
    signaling = any(value.kind == "snan" for value in nans)
    if signaling:
        flags |= IOC
    if control.default_nan:
        return FPResult(DEFAULT_NAN, flags)
    for kind in ("snan", "qnan"):
        for value in operands:
            if value.kind == kind:
                return FPResult(_quiet_nan(value.bits), flags)
    raise AssertionError("unreachable NaN selection")


def _invalid(flags: int = 0) -> FPResult:
    return FPResult(DEFAULT_NAN, flags | IOC)


def _floor_log2_ratio(numerator: int, denominator: int) -> int:
    estimate = numerator.bit_length() - denominator.bit_length()
    if estimate >= 0:
        if numerator < (denominator << estimate):
            estimate -= 1
    elif (numerator << -estimate) < denominator:
        estimate -= 1
    return estimate


def _scaled_division(numerator: int, denominator: int, shift: int) -> tuple[int, int, int]:
    if shift >= 0:
        numerator <<= shift
    else:
        denominator <<= -shift
    quotient, remainder = divmod(numerator, denominator)
    return quotient, remainder, denominator


def _increment(quotient: int, remainder: int, denominator: int, sign: bool, mode: RoundingMode) -> bool:
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


def _overflow(sign: bool, mode: RoundingMode) -> FPResult:
    infinity = (
        mode == RoundingMode.NEAREST_EVEN
        or (mode == RoundingMode.PLUS_INFINITY and not sign)
        or (mode == RoundingMode.MINUS_INFINITY and sign)
    )
    magnitude = POSITIVE_INFINITY if infinity else MAX_FINITE
    return FPResult((SIGN_BIT if sign else 0) | magnitude, OFC | IXC)


def _round_finite(
    numerator: int,
    denominator: int,
    exponent: int,
    sign: bool,
    control: FPControl,
) -> FPResult:
    """Round positive ``numerator/denominator * 2**exponent`` to binary32."""

    if numerator == 0:
        return FPResult(SIGN_BIT if sign else 0)
    binary_exponent = _floor_log2_ratio(numerator, denominator) + exponent

    # VFP11 FZ detects tininess before rounding and returns positive zero.  It
    # raises only UFC for the output flush; it does not also raise IXC.
    if control.flush_to_zero and binary_exponent < -126:
        return FPResult(0, UFC)

    mode = control.rounding
    if binary_exponent > 127:
        return _overflow(sign, mode)

    if binary_exponent >= -126:
        shift = exponent - binary_exponent + 23
        significand, remainder, divisor = _scaled_division(numerator, denominator, shift)
        if _increment(significand, remainder, divisor, sign, mode):
            significand += 1
        inexact = remainder != 0
        if significand >= (1 << 24):
            significand >>= 1
            binary_exponent += 1
        if binary_exponent > 127:
            result = _overflow(sign, mode)
            return FPResult(result.value, result.flags)
        bits = (
            (SIGN_BIT if sign else 0)
            | ((binary_exponent + 127) << 23)
            | (significand & FRAC_MASK)
        )
        return FPResult(bits, IXC if inexact else 0)

    # Gradual-underflow path: round in units of the least subnormal, 2**-149.
    significand, remainder, divisor = _scaled_division(
        numerator, denominator, exponent + 149
    )
    if _increment(significand, remainder, divisor, sign, mode):
        significand += 1
    inexact = remainder != 0
    if significand >= (1 << 23):
        # Tininess-after-rounding: an inexact value rounded up to the smallest
        # normal raises IXC but not UFC.
        bits = (SIGN_BIT if sign else 0) | (1 << 23)
        return FPResult(bits, IXC if inexact else 0)
    bits = (SIGN_BIT if sign else 0) | significand
    flags = (UFC | IXC) if inexact else 0
    return FPResult(bits, flags)


def f32_neg(bits: int, control: FPControl | int | None = None) -> FPResult:
    """VFP FPNeg: flip the sign bit without classification or exceptions."""

    del control
    return FPResult((bits ^ SIGN_BIT) & MASK32)


def f32_abs(bits: int, control: FPControl | int | None = None) -> FPResult:
    """VFP FPAbs: clear the sign bit without classification or exceptions."""

    del control
    return FPResult(bits & ~SIGN_BIT & MASK32)


def f32_add(a: int, b: int, control: FPControl | int | None = None) -> FPResult:
    control = _control(control)
    left, left_flags = _unpack(a, control)
    right, right_flags = _unpack(b, control)
    flags = left_flags | right_flags
    nan = _nan_result((left, right), control, flags)
    if nan is not None:
        return nan

    if left.kind == "infinity" or right.kind == "infinity":
        if left.kind == right.kind == "infinity" and left.sign != right.sign:
            return _invalid(flags)
        value = left if left.kind == "infinity" else right
        return FPResult((SIGN_BIT if value.sign else 0) | POSITIVE_INFINITY, flags)

    if left.kind == right.kind == "zero":
        sign = left.sign if left.sign == right.sign else control.rounding == RoundingMode.MINUS_INFINITY
        return FPResult(SIGN_BIT if sign else 0, flags)
    if left.kind == "zero":
        return FPResult(right.bits, flags)
    if right.kind == "zero":
        return FPResult(left.bits, flags)

    common_exponent = min(left.exponent, right.exponent)
    left_integer = left.significand << (left.exponent - common_exponent)
    right_integer = right.significand << (right.exponent - common_exponent)
    if left.sign:
        left_integer = -left_integer
    if right.sign:
        right_integer = -right_integer
    total = left_integer + right_integer
    if total == 0:
        sign = control.rounding == RoundingMode.MINUS_INFINITY
        return FPResult(SIGN_BIT if sign else 0, flags)
    rounded = _round_finite(abs(total), 1, common_exponent, total < 0, control)
    return FPResult(rounded.value, flags | rounded.flags)


def f32_sub(a: int, b: int, control: FPControl | int | None = None) -> FPResult:
    # FPSub negates the *unpacked* second operand.  That ordering matters for
    # VFP11 FZ: either-sign subnormal first becomes +0 (and raises IDC), then
    # the subtraction turns it into -0.
    control = _control(control)
    right, flags = _unpack(b, control)
    negated = (right.bits ^ SIGN_BIT) & MASK32
    result = f32_add(a, negated, control)
    return FPResult(result.value, result.flags | flags)


def f32_mul(a: int, b: int, control: FPControl | int | None = None) -> FPResult:
    control = _control(control)
    left, left_flags = _unpack(a, control)
    right, right_flags = _unpack(b, control)
    flags = left_flags | right_flags
    nan = _nan_result((left, right), control, flags)
    if nan is not None:
        return nan
    sign = left.sign ^ right.sign
    if (
        (left.kind == "zero" and right.kind == "infinity")
        or (left.kind == "infinity" and right.kind == "zero")
    ):
        return _invalid(flags)
    if left.kind == "infinity" or right.kind == "infinity":
        return FPResult((SIGN_BIT if sign else 0) | POSITIVE_INFINITY, flags)
    if left.kind == "zero" or right.kind == "zero":
        return FPResult(SIGN_BIT if sign else 0, flags)
    rounded = _round_finite(
        left.significand * right.significand,
        1,
        left.exponent + right.exponent,
        sign,
        control,
    )
    return FPResult(rounded.value, flags | rounded.flags)


def f32_div(a: int, b: int, control: FPControl | int | None = None) -> FPResult:
    control = _control(control)
    left, left_flags = _unpack(a, control)
    right, right_flags = _unpack(b, control)
    flags = left_flags | right_flags
    nan = _nan_result((left, right), control, flags)
    if nan is not None:
        return nan
    sign = left.sign ^ right.sign
    if (left.kind == right.kind == "zero") or (
        left.kind == right.kind == "infinity"
    ):
        return _invalid(flags)
    if left.kind == "infinity":
        return FPResult((SIGN_BIT if sign else 0) | POSITIVE_INFINITY, flags)
    if right.kind == "infinity":
        return FPResult(SIGN_BIT if sign else 0, flags)
    if right.kind == "zero":
        return FPResult((SIGN_BIT if sign else 0) | POSITIVE_INFINITY, flags | DZC)
    if left.kind == "zero":
        return FPResult(SIGN_BIT if sign else 0, flags)
    rounded = _round_finite(
        left.significand,
        right.significand,
        left.exponent - right.exponent,
        sign,
        control,
    )
    return FPResult(rounded.value, flags | rounded.flags)


def f32_sqrt(bits: int, control: FPControl | int | None = None) -> FPResult:
    control = _control(control)
    value, flags = _unpack(bits, control)
    nan = _nan_result((value,), control, flags)
    if nan is not None:
        return nan
    if value.kind == "zero":
        return FPResult(value.bits, flags)
    if value.sign:
        return _invalid(flags)
    if value.kind == "infinity":
        return FPResult(POSITIVE_INFINITY, flags)

    # Scale sqrt(value) so that its integer part is the 24-bit result
    # significand.  For every finite binary32 input this shift is non-negative.
    input_log2 = value.significand.bit_length() - 1 + value.exponent
    output_exponent = input_log2 // 2
    shift = value.exponent - (2 * output_exponent) + 46
    radicand = value.significand << shift
    significand = isqrt(radicand)
    remainder = radicand - significand * significand
    if remainder:
        mode = control.rounding
        increment = False
        if mode == RoundingMode.NEAREST_EVEN:
            # Compare sqrt(radicand) with significand + 1/2 exactly.
            increment = (radicand << 2) > (
                (significand * significand << 2) + (significand << 2) + 1
            )
        elif mode == RoundingMode.PLUS_INFINITY:
            increment = True
        if increment:
            significand += 1
    if significand >= (1 << 24):
        significand >>= 1
        output_exponent += 1
    result = ((output_exponent + 127) << 23) | (significand & FRAC_MASK)
    return FPResult(result, flags | (IXC if remainder else 0))


def _combine(first: FPResult, second: FPResult) -> FPResult:
    return FPResult(second.value, first.flags | second.flags)


def f32_mla(accumulator: int, left: int, right: int, control: FPControl | int | None = None) -> FPResult:
    """Non-fused VMLA: rounded multiply followed by rounded add."""

    product = f32_mul(left, right, control)
    return _combine(product, f32_add(accumulator, product.value, control))


def f32_mls(accumulator: int, left: int, right: int, control: FPControl | int | None = None) -> FPResult:
    """Non-fused VMLS: rounded multiply followed by add of its negation."""

    product = f32_mul(left, right, control)
    negative_product = f32_neg(product.value)
    return _combine(product, f32_add(accumulator, negative_product.value, control))


def f32_nmla(accumulator: int, left: int, right: int, control: FPControl | int | None = None) -> FPResult:
    """Non-fused VNMLA: (-accumulator) + (-(left * right))."""

    product = f32_mul(left, right, control)
    result = f32_add(
        f32_neg(accumulator).value, f32_neg(product.value).value, control
    )
    return _combine(product, result)


def f32_nmls(accumulator: int, left: int, right: int, control: FPControl | int | None = None) -> FPResult:
    """Non-fused VNMLS: (-accumulator) + (left * right)."""

    product = f32_mul(left, right, control)
    result = f32_add(f32_neg(accumulator).value, product.value, control)
    return _combine(product, result)


def f32_nmul(left: int, right: int, control: FPControl | int | None = None) -> FPResult:
    product = f32_mul(left, right, control)
    return FPResult(f32_neg(product.value).value, product.flags)


def f32_from_u32(value: int, control: FPControl | int | None = None) -> FPResult:
    control = _control(control)
    magnitude = value & MASK32
    return _round_finite(magnitude, 1, 0, False, control)


def f32_from_i32(value: int, control: FPControl | int | None = None) -> FPResult:
    control = _control(control)
    signed = value & MASK32
    if signed & SIGN_BIT:
        signed -= 1 << 32
    return _round_finite(abs(signed), 1, 0, signed < 0, control)


def _round_to_integer(
    significand: int, exponent: int, sign: bool, mode: RoundingMode
) -> tuple[int, bool]:
    if exponent >= 0:
        magnitude = significand << exponent
        return (-magnitude if sign else magnitude), False
    divisor = 1 << -exponent
    magnitude, remainder = divmod(significand, divisor)
    if _increment(magnitude, remainder, divisor, sign, mode):
        magnitude += 1
    return (-magnitude if sign else magnitude), remainder != 0


def _f32_to_int(
    bits: int,
    signed: bool,
    control: FPControl | int | None,
    rounding: RoundingMode | int | None,
) -> FPResult:
    control = _control(control)
    mode = control.rounding if rounding is None else _rounding(rounding)
    value, flags = _unpack(bits, control)
    if value.kind in ("snan", "qnan"):
        return FPResult(0, flags | IOC)
    if value.kind == "zero":
        return FPResult(0, flags)
    if value.kind == "infinity":
        if signed:
            saturated = 0x80000000 if value.sign else 0x7FFFFFFF
        else:
            saturated = 0 if value.sign else MASK32
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
    if inexact:
        flags |= IXC
    return FPResult(integer & MASK32, flags)


def f32_to_i32(
    bits: int,
    control: FPControl | int | None = None,
    *,
    rounding: RoundingMode | int | None = RoundingMode.TOWARD_ZERO,
) -> FPResult:
    """Convert to a raw signed-32 word.

    The default is VCVT's forced round-to-zero.  Pass ``rounding=None`` for
    VCVTR, which takes FPSCR.RMode, or pass an explicit mode for direct use.
    """

    return _f32_to_int(bits, True, control, rounding)


def f32_to_u32(
    bits: int,
    control: FPControl | int | None = None,
    *,
    rounding: RoundingMode | int | None = RoundingMode.TOWARD_ZERO,
) -> FPResult:
    """Convert to a raw unsigned-32 word; rounding rules match f32_to_i32."""

    return _f32_to_int(bits, False, control, rounding)


def f32_compare(
    a: int,
    b: int,
    control: FPControl | int | None = None,
    *,
    signal_all_nans: bool = False,
) -> FPResult:
    """Return VFP compare NZCV in ``value`` and any newly raised IOC.

    ``signal_all_nans=False`` implements VCMP; true implements VCMPE.  VFP11
    comparisons do not flush subnormals and do not raise IDC.
    """

    del control
    a &= MASK32
    b &= MASK32
    left_kind = _classify(a)
    right_kind = _classify(b)
    if left_kind in ("snan", "qnan") or right_kind in ("snan", "qnan"):
        invalid = (
            left_kind == "snan"
            or right_kind == "snan"
            or signal_all_nans
        )
        return FPResult(NZCV_UNORDERED, IOC if invalid else 0)

    # Both signs of zero compare equal.
    if (a & ~SIGN_BIT) == 0 and (b & ~SIGN_BIT) == 0:
        return FPResult(NZCV_EQUAL)
    if a == b:
        return FPResult(NZCV_EQUAL)
    left_sign = bool(a & SIGN_BIT)
    right_sign = bool(b & SIGN_BIT)
    if left_sign != right_sign:
        return FPResult(NZCV_LESS if left_sign else NZCV_GREATER)
    if left_sign:
        less = (a & ~SIGN_BIT) > (b & ~SIGN_BIT)
    else:
        less = a < b
    return FPResult(NZCV_LESS if less else NZCV_GREATER)


__all__ = [
    "DEFAULT_NAN",
    "DZC",
    "EXCEPTION_FLAGS",
    "FPControl",
    "FPResult",
    "FPSCR_DN",
    "FPSCR_FZ",
    "FPSCR_RMODE_MASK",
    "IDC",
    "IOC",
    "IXC",
    "NZCV_EQUAL",
    "NZCV_GREATER",
    "NZCV_LESS",
    "NZCV_UNORDERED",
    "OFC",
    "RoundingMode",
    "UFC",
    "accumulate_fpscr",
    "f32_abs",
    "f32_add",
    "f32_compare",
    "f32_div",
    "f32_from_i32",
    "f32_from_u32",
    "f32_mla",
    "f32_mls",
    "f32_mul",
    "f32_neg",
    "f32_nmla",
    "f32_nmls",
    "f32_nmul",
    "f32_sqrt",
    "f32_sub",
    "f32_to_i32",
    "f32_to_u32",
]
