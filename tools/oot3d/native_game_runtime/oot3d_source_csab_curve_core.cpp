#include "oot3d_source_csab_curve_core.h"

#include "oot3d/csab_curve_eval.h"
#include "oot3d_native_a32_vfp_ops.h"
#include "recomp/a32_vfp_scalar.h"

#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <immintrin.h>
#define OOT3D_CSAB_HAS_MXCSR 1
#elif defined(__SSE__)
#include <xmmintrin.h>
#define OOT3D_CSAB_HAS_MXCSR 1
#else
#define OOT3D_CSAB_HAS_MXCSR 0
#endif

namespace Oot3dNativeGame {
namespace {

constexpr int32_t kMaximumCurveKeys = 4096;
constexpr uint32_t kFpscrRoundingMask = 3U << 22U;
constexpr uint32_t kFpscrNzcvMask = 0xF0000000U;
constexpr uint32_t kFpscrExceptionEnableMask = 0x00009F00U;
constexpr uint32_t kFpscrFlushToZero = 1U << 24U;
constexpr uint32_t kFpscrDefaultNaN = 1U << 25U;

class TargetFloatingPointScope {
  public:
    explicit TargetFloatingPointScope(uint32_t fpscr) {
#if OOT3D_CSAB_HAS_MXCSR
        mPreviousMxcsr = _mm_getcsr();
        uint32_t targetMxcsr = mPreviousMxcsr;
        constexpr uint32_t kMxcsrDenormalsAreZero = 1U << 6U;
        constexpr uint32_t kMxcsrFlushToZero = 1U << 15U;
        if ((fpscr & kFpscrFlushToZero) != 0U) {
            targetMxcsr |= kMxcsrDenormalsAreZero | kMxcsrFlushToZero;
        } else {
            targetMxcsr &=
                ~(kMxcsrDenormalsAreZero | kMxcsrFlushToZero);
        }
        _mm_setcsr(targetMxcsr);
#else
        if ((fpscr & kFpscrFlushToZero) != 0U) {
            return;
        }
#endif
        if (std::fegetenv(&mPrevious) != 0 ||
            std::fesetround(HostRoundingMode(fpscr)) != 0 ||
            std::feclearexcept(FE_ALL_EXCEPT) != 0) {
#if OOT3D_CSAB_HAS_MXCSR
            _mm_setcsr(mPreviousMxcsr);
#endif
            return;
        }
        mActive = true;
    }

    TargetFloatingPointScope(const TargetFloatingPointScope&) = delete;
    TargetFloatingPointScope& operator=(
        const TargetFloatingPointScope&) = delete;

    ~TargetFloatingPointScope() {
        if (mActive) {
            std::fesetenv(&mPrevious);
#if OOT3D_CSAB_HAS_MXCSR
            _mm_setcsr(mPreviousMxcsr);
#endif
        }
    }

    bool Active() const noexcept {
        return mActive;
    }

    uint32_t TargetExceptionFlags() const noexcept {
        const int flags = std::fetestexcept(FE_ALL_EXCEPT);
        uint32_t result = 0U;
        result |= (flags & FE_INVALID) != 0 ? 1U << 0U : 0U;
        result |= (flags & FE_DIVBYZERO) != 0 ? 1U << 1U : 0U;
        result |= (flags & FE_OVERFLOW) != 0 ? 1U << 2U : 0U;
        result |= (flags & FE_UNDERFLOW) != 0 ? 1U << 3U : 0U;
        result |= (flags & FE_INEXACT) != 0 ? 1U << 4U : 0U;
        return result;
    }

  private:
    static int HostRoundingMode(uint32_t fpscr) noexcept {
        switch ((fpscr & kFpscrRoundingMask) >> 22U) {
        case 0U:
            return FE_TONEAREST;
        case 1U:
            return FE_UPWARD;
        case 2U:
            return FE_DOWNWARD;
        case 3U:
            return FE_TOWARDZERO;
        default:
            return FE_TONEAREST;
        }
    }

    std::fenv_t mPrevious{};
#if OOT3D_CSAB_HAS_MXCSR
    uint32_t mPreviousMxcsr = 0U;
#endif
    bool mActive = false;
};

size_t KeySize(bool f32, uint8_t interpolationType) {
    if (f32) {
        switch (interpolationType) {
        case 1U:
        case 3U:
            return sizeof(
                Oot3dSourceCsabCurve::Oot3dAnimationCurveF32LinearKey);
        case 2U:
            return sizeof(Oot3dSourceCsabCurve::Oot3dAnimationCurveF32Key);
        default:
            return 0U;
        }
    }
    return interpolationType == 1U || interpolationType == 2U
               ? sizeof(Oot3dSourceCsabCurve::Oot3dAnimationCurveS16Key)
               : 0U;
}

struct LastCurveComparison {
    bool Valid = false;
    uint32_t Left = 0U;
    uint32_t Right = 0U;
};

struct S16TanStackScratch {
    bool Valid = false;
    bool Exact = true;
    uint32_t RightValue = 0U;
    uint32_t LeftValue = 0U;
    uint32_t RightTangentValue = 0U;
    uint32_t LeftTangentValue = 0U;
    uint32_t LeftTangentArgument = 0U;
    uint32_t LeftTangentLocal = 0U;
    uint32_t RightTangentLocal = 0U;
    uint32_t RightTangentArgument = 0U;
    uint32_t ExceptionFlags = 0U;
    uint32_t Cpsr = 0U;
    bool CpsrValid = false;
};

struct CurveAbiSelection {
    uint32_t CurveAddress = 0U;
    uint32_t KeyBaseAddress = 0U;
    int32_t KeyCount = 0;
    int32_t Duration = 0;
    uint8_t InterpolationType = 0U;
    uint8_t Loop = 0U;
    int32_t UpperIndex = 0;
    int32_t LeftIndex = -1;
    int32_t RightIndex = -1;
    bool BeforeFirst = false;
    bool AfterLast = false;
    bool Wrapped = false;
};

void RecordComparison(
    LastCurveComparison* comparison, float left, float right) {
    comparison->Valid = true;
    comparison->Left = std::bit_cast<uint32_t>(left);
    comparison->Right = std::bit_cast<uint32_t>(right);
}

template <typename Key>
void TraceKeySearch(
    const Key* keys, int32_t keyCount, float frame,
    LastCurveComparison* comparison) {
    for (int32_t index = 0; index < keyCount; ++index) {
        const float keyFrame = static_cast<float>(keys[index].frame);
        RecordComparison(comparison, keyFrame, frame);
        if (keyFrame > frame) {
            break;
        }
    }
}

template <typename Key>
void TraceHermiteSelection(
    const Oot3dSourceCsabCurve::Oot3dAnimationCurveHeader& header,
    const Key* keys, uint8_t loop, float frame,
    LastCurveComparison* comparison) {
    if (header.keyCount == 1) {
        return;
    }
    if (loop != 0U) {
        RecordComparison(comparison, frame, 0.0F);
        if (frame < 0.0F) {
            RecordComparison(comparison, frame, 0.0F);
            return;
        }

        const float duration = static_cast<float>(header.duration);
        RecordComparison(comparison, duration, frame);
        if (frame > duration) {
            RecordComparison(comparison, frame, 0.0F);
            return;
        }
    }
    TraceKeySearch(keys, header.keyCount, frame, comparison);
}

LastCurveComparison TraceLastCurveComparison(
    bool f32, std::span<const uint8_t> curveBytes, uint8_t loop,
    float frame) {
    LastCurveComparison comparison;
    const auto* header = reinterpret_cast<
        const Oot3dSourceCsabCurve::Oot3dAnimationCurveHeader*>(
        curveBytes.data());
    const auto* keys = curveBytes.data() + sizeof(*header);

    if (f32) {
        if (header->interpolationType == 1U ||
            header->interpolationType == 3U) {
            if (header->keyCount > 1) {
                TraceKeySearch(
                    reinterpret_cast<const Oot3dSourceCsabCurve::
                                         Oot3dAnimationCurveF32LinearKey*>(
                        keys),
                    header->keyCount, frame, &comparison);
            }
        } else if (header->interpolationType == 2U) {
            TraceHermiteSelection(
                *header,
                reinterpret_cast<const Oot3dSourceCsabCurve::
                                     Oot3dAnimationCurveF32Key*>(keys),
                loop, frame, &comparison);
        }
    } else if (header->interpolationType == 2U) {
        TraceHermiteSelection(
            *header,
            reinterpret_cast<const Oot3dSourceCsabCurve::
                                 Oot3dAnimationCurveS16Key*>(keys),
            loop, frame, &comparison);
    }
    return comparison;
}

uint32_t EvaluateS16ScaledValue(
    int16_t value, uint32_t scale, uint32_t fpscr) {
    const auto converted =
        oot3d::recomp::a32::VfpBinary32FromSigned(
            static_cast<uint32_t>(static_cast<int32_t>(value)), fpscr);
    return oot3d::recomp::a32::VfpBinary32Multiply(
               converted.value, scale, fpscr)
        .value;
}

uint32_t EvaluateS16TangentArgument(
    int16_t value, uint32_t fpscr) {
    uint32_t result =
        EvaluateS16ScaledValue(value, 0x38490FDBU, fpscr);
    result = oot3d::recomp::a32::VfpBinary32Multiply(
                 result, 0x4222F983U, fpscr)
                 .value;
    return oot3d::recomp::a32::VfpBinary32Multiply(
               result, 0x3CC90FDBU, fpscr)
        .value;
}

float FastFloatFromBits(uint32_t bits) {
    return std::bit_cast<float>(bits);
}

uint32_t FastBitsFromFloat(float value) {
    return std::bit_cast<uint32_t>(value);
}

uint32_t FastAdd(uint32_t left, uint32_t right) {
    volatile float leftValue = FastFloatFromBits(left);
    volatile float rightValue = FastFloatFromBits(right);
    volatile float result = leftValue + rightValue;
    return FastBitsFromFloat(result);
}

uint32_t FastSubtract(uint32_t left, uint32_t right) {
    volatile float leftValue = FastFloatFromBits(left);
    volatile float rightValue = FastFloatFromBits(right);
    volatile float result = leftValue - rightValue;
    return FastBitsFromFloat(result);
}

uint32_t FastMultiply(uint32_t left, uint32_t right) {
    volatile float leftValue = FastFloatFromBits(left);
    volatile float rightValue = FastFloatFromBits(right);
    volatile float result = leftValue * rightValue;
    return FastBitsFromFloat(result);
}

uint32_t FastDivide(uint32_t left, uint32_t right) {
    volatile float leftValue = FastFloatFromBits(left);
    volatile float rightValue = FastFloatFromBits(right);
    volatile float result = leftValue / rightValue;
    return FastBitsFromFloat(result);
}

uint32_t FastFromSigned(int32_t value) {
    volatile int32_t integerValue = value;
    volatile float result = static_cast<float>(integerValue);
    return FastBitsFromFloat(result);
}

uint32_t FastToSigned(uint32_t value) {
    volatile float floatValue = FastFloatFromBits(value);
    return static_cast<uint32_t>(static_cast<int32_t>(floatValue));
}

struct VfpShadow {
    uint32_t Fpscr = 0U;
    uint32_t ExceptionFlags = 0U;
    bool Exact = false;

    uint32_t Add(uint32_t left, uint32_t right) {
        if (Exact) {
            const auto result = oot3d::recomp::a32::VfpBinary32Add(
                left, right, Fpscr);
            ExceptionFlags |= result.exception_flags;
            return result.value;
        }
        return FastAdd(left, right);
    }

    uint32_t Subtract(uint32_t left, uint32_t right) {
        if (Exact) {
            const auto result = oot3d::recomp::a32::VfpBinary32Subtract(
                left, right, Fpscr);
            ExceptionFlags |= result.exception_flags;
            return result.value;
        }
        return FastSubtract(left, right);
    }

    uint32_t Divide(uint32_t left, uint32_t right) {
        if (Exact) {
            const auto result = oot3d::recomp::a32::VfpBinary32Divide(
                left, right, Fpscr);
            ExceptionFlags |= result.exception_flags;
            return result.value;
        }
        return FastDivide(left, right);
    }

    uint32_t Multiply(uint32_t left, uint32_t right) {
        if (Exact) {
            const auto result = oot3d::recomp::a32::VfpBinary32Multiply(
                left, right, Fpscr);
            ExceptionFlags |= result.exception_flags;
            return result.value;
        }
        return FastMultiply(left, right);
    }

    uint32_t MultiplyAccumulate(
        uint32_t accumulator, uint32_t left, uint32_t right) {
        if (Exact) {
            const auto result =
                oot3d::recomp::a32::VfpBinary32MultiplyAccumulate(
                    accumulator, left, right, Fpscr);
            ExceptionFlags |= result.exception_flags;
            return result.value;
        }
        return FastAdd(accumulator, FastMultiply(left, right));
    }

    uint32_t MultiplySubtract(
        uint32_t accumulator, uint32_t left, uint32_t right) {
        if (Exact) {
            const auto result =
                oot3d::recomp::a32::VfpBinary32MultiplySubtract(
                    accumulator, left, right, Fpscr);
            ExceptionFlags |= result.exception_flags;
            return result.value;
        }
        return FastSubtract(accumulator, FastMultiply(left, right));
    }

    uint32_t NegativeMultiplySubtract(
        uint32_t accumulator, uint32_t left, uint32_t right) {
        if (Exact) {
            const auto result =
                oot3d::recomp::a32::VfpBinary32NegativeMultiplySubtract(
                    accumulator, left, right, Fpscr);
            ExceptionFlags |= result.exception_flags;
            return result.value;
        }
        return FastSubtract(FastMultiply(left, right), accumulator);
    }

    uint32_t FromSigned(uint32_t value) {
        if (Exact) {
            const auto result = oot3d::recomp::a32::VfpBinary32FromSigned(
                value, Fpscr);
            ExceptionFlags |= result.exception_flags;
            return result.value;
        }
        return FastFromSigned(static_cast<int32_t>(value));
    }

    uint32_t ToSigned(uint32_t value) {
        if (Exact) {
            const auto result = oot3d::recomp::a32::VfpBinary32ToSigned(
                value, Fpscr);
            ExceptionFlags |= result.exception_flags;
            return result.value;
        }
        return FastToSigned(value);
    }
};

struct MathTanFResult {
    bool Exact = true;
    uint32_t Value = 0U;
    uint32_t Local = 0U;
    uint32_t ExceptionFlags = 0U;
};

MathTanFResult EvaluateMathTanFUncached(
    uint32_t angle, uint32_t fpscr) {
    constexpr uint32_t kQuarterPiShifted = 0x7E921FB6U;
    constexpr uint32_t kTinyLimitShifted = 0x73000000U;
    constexpr uint32_t kTwoOverPi = 0x3F22F983U;
    constexpr uint32_t kRoundingMagic = 0x4B000000U;
    const uint32_t magnitudeShifted = angle << 1U;
    VfpShadow shadow{fpscr, 0U, true};
    uint32_t reduced = angle;
    uint32_t local = 0U;
    if (magnitudeShifted < kQuarterPiShifted) {
        local = magnitudeShifted >= kTinyLimitShifted ? 0U : 0xFFFFFFFFU;
        if (local == 0xFFFFFFFFU) {
            return {true, reduced, local, shadow.ExceptionFlags};
        }
        // For ordinary values below pi/4 the target skips range reduction,
        // but still evaluates the polynomial.  Only the negative-tiny
        // sentinel returns through the classifier path above.
    } else {
        // The native large-argument reduction helper has a separate
        // multiword implementation.  The CSAB tangent inputs observed in
        // the promoted owner stay on the inline path; leave this rare path
        // conservative until that helper is promoted as its own owner.
        if ((angle & 0x7FFFFFFFU) >= 0x46490E49U) {
            return {false, angle, 0U, 0U};
        }

        uint32_t rounded = shadow.Multiply(angle, kTwoOverPi);
        if ((angle & 0x80000000U) != 0U) {
            rounded = shadow.Subtract(rounded, kRoundingMagic);
            rounded = shadow.Add(rounded, kRoundingMagic);
        } else {
            rounded = shadow.Add(rounded, kRoundingMagic);
            rounded = shadow.Subtract(rounded, kRoundingMagic);
        }
        local = shadow.ToSigned(rounded) & 3U;
        reduced = shadow.MultiplySubtract(
            reduced, rounded, 0x3FC90000U);
        reduced = shadow.MultiplySubtract(
            reduced, rounded, 0x39FDA000U);
        reduced = shadow.MultiplySubtract(
            reduced, rounded, 0x33A22000U);
        reduced = shadow.MultiplySubtract(
            reduced, rounded, 0x2C34611AU);

        if (local == 0xFFFFFFFFU) {
            if ((reduced << 1U) >= 0xFF000000U) {
                reduced = shadow.Add(reduced, reduced);
            }
            return {true, reduced, local, shadow.ExceptionFlags};
        }
    }

    // Keep the operation order and constants of Math_TanF's VFP polynomial.
    // Each VMLA is a binary32 multiply followed by a binary32 add on the
    // target; VfpShadow's fast path models that split instead of delegating
    // to the host libm.
    constexpr uint32_t kNegativeOne = 0xBF800000U;
    const uint32_t square = shadow.Multiply(reduced, reduced);
    uint32_t polynomial = 0x3B066D0CU;
    polynomial = shadow.MultiplyAccumulate(
        polynomial, square, 0x3C22E9BBU);
    uint32_t next = 0x3CCE768AU;
    next = shadow.MultiplyAccumulate(next, square, polynomial);
    polynomial = 0x3D59B5ACU;
    polynomial = shadow.MultiplyAccumulate(
        polynomial, square, next);
    next = 0x3E08A135U;
    next = shadow.MultiplyAccumulate(next, square, polynomial);
    polynomial = 0x3EAAAA29U;
    polynomial = shadow.MultiplyAccumulate(
        polynomial, square, next);
    const uint32_t correction = shadow.Multiply(polynomial, square);
    uint32_t value = shadow.MultiplyAccumulate(
        reduced, reduced, correction);
    if ((local & 1U) != 0U) {
        value = shadow.Divide(kNegativeOne, value);
    }
    return {true, value, local, shadow.ExceptionFlags};
}

MathTanFResult EvaluateMathTanF(uint32_t angle, uint32_t fpscr) {
    static std::unordered_map<std::uint64_t, MathTanFResult> cache;
    const std::uint64_t key =
        (static_cast<std::uint64_t>(fpscr) << 32U) | angle;
    const auto cached = cache.find(key);
    if (cached != cache.end()) {
        return cached->second;
    }
    const MathTanFResult result = EvaluateMathTanFUncached(angle, fpscr);
    if (cache.size() >= 4096U) {
        cache.clear();
    }
    cache.emplace(key, result);
    return result;
}

struct MathTanFCallerAbi {
    uint32_t R0 = 0U;
    uint32_t R1 = 0U;
    uint32_t R2 = 0U;
    uint32_t R14 = 0U;
};

// This is the small native helper called by Math_TanF when its range-reduced
// result is the negative tiny sentinel.  The BIC in the original ARM body is
// r1 = 0xff000000 & ~(bits << 1), rather than a conventional field mask.
MathTanFCallerAbi EvaluateMathTanFCallerAbi(
    uint32_t entryArgument, uint32_t classificationBits,
    uint32_t local, uint32_t returnAddress) {
    if (local != 0xFFFFFFFFU) {
        return {local, entryArgument, 0U, returnAddress};
    }

    const uint32_t shifted = classificationBits << 1U;
    uint32_t classification = 0U;
    if ((shifted << 8U) != 0U) {
        classification = 4U;
    }
    if ((shifted >> 24U) != 0U) {
        classification |= 1U;
    }
    const uint32_t residual = 0xFF000000U & ~shifted;
    if (residual == 0U) {
        classification |= 2U;
    }
    if (classification == 1U) {
        classification = 5U;
    }
    return {classification, residual, 0xFF000000U, 0x0035569CU};
}

S16TanStackScratch BuildS16TanStackScratch(
    std::span<const uint8_t> curveBytes, uint8_t loop,
    float frame, uint32_t fpscr) {
    S16TanStackScratch scratch;
    const auto* header = reinterpret_cast<
        const Oot3dSourceCsabCurve::Oot3dAnimationCurveHeader*>(
        curveBytes.data());
    if (header->interpolationType != 2U || header->keyCount <= 1) {
        return scratch;
    }

    const auto* keys = reinterpret_cast<
        const Oot3dSourceCsabCurve::Oot3dAnimationCurveS16Key*>(
        curveBytes.data() + sizeof(*header));
    const auto* left =
        static_cast<const Oot3dSourceCsabCurve::Oot3dAnimationCurveS16Key*>(
            nullptr);
    const auto* right = left;
    if (loop != 0U &&
        (frame < 0.0F || frame > static_cast<float>(header->duration))) {
        left = &keys[header->keyCount - 1];
        right = &keys[0];
    } else {
        int32_t upperIndex = 0;
        while (upperIndex < header->keyCount &&
               !(static_cast<float>(keys[upperIndex].frame) > frame)) {
            ++upperIndex;
        }
        if (upperIndex == 0 || upperIndex == header->keyCount) {
            return scratch;
        }
        left = &keys[upperIndex - 1];
        right = &keys[upperIndex];
    }

    scratch.Valid = true;
    scratch.RightValue =
        EvaluateS16ScaledValue(right->value, 0x38C90FDBU, fpscr);
    scratch.LeftValue =
        EvaluateS16ScaledValue(left->value, 0x38C90FDBU, fpscr);
    scratch.LeftTangentArgument =
        EvaluateS16TangentArgument(left->tangentOut, fpscr);
    const auto leftTangent = EvaluateMathTanF(
        scratch.LeftTangentArgument, fpscr);
    scratch.LeftTangentLocal = leftTangent.Local;
    scratch.LeftTangentValue = leftTangent.Value;
    scratch.RightTangentArgument =
        EvaluateS16TangentArgument(right->tangentIn, fpscr);
    const auto rightTangent = EvaluateMathTanF(
        scratch.RightTangentArgument, fpscr);
    scratch.RightTangentLocal = rightTangent.Local;
    scratch.RightTangentValue = rightTangent.Value;
    scratch.Exact = leftTangent.Exact && rightTangent.Exact;
    scratch.ExceptionFlags = leftTangent.ExceptionFlags |
                             rightTangent.ExceptionFlags;
    // Math_TanF leaves APSR NZCV owned by its final classifier/TST path.
    // The right tangent is the last helper call in the CSAB owner, so its
    // flags are the flags observed by the caller on the successful path.
    scratch.Cpsr = rightTangent.Local == 0xFFFFFFFFU
                       ? 0x20000000U
                       : (rightTangent.Local & 1U) != 0U
                             ? 0x20000000U
                             : 0x60000000U;
    scratch.CpsrValid = true;
    return scratch;
}

CurveAbiSelection BuildCurveAbiSelection(
    bool f32, uint32_t curveAddress, std::span<const uint8_t> curveBytes,
    uint8_t loop, float frame) {
    CurveAbiSelection selection;
    const auto* header = reinterpret_cast<const Oot3dSourceCsabCurve::
                                             Oot3dAnimationCurveHeader*>(
        curveBytes.data());
    selection.CurveAddress = curveAddress;
    selection.KeyBaseAddress = curveAddress + sizeof(*header);
    selection.KeyCount = header->keyCount;
    selection.Duration = header->duration;
    selection.InterpolationType = header->interpolationType;
    selection.Loop = loop;

    if (selection.KeyCount <= 0) return selection;
    if (selection.KeyCount == 1) {
        selection.UpperIndex = 1;
        return selection;
    }

    if (selection.InterpolationType == 2U && loop != 0U &&
        (frame < 0.0F || frame > static_cast<float>(selection.Duration))) {
        selection.Wrapped = true;
        selection.LeftIndex = selection.KeyCount - 1;
        selection.RightIndex = 0;
        return selection;
    }

    const size_t stride = f32 && selection.InterpolationType == 2U
                              ? sizeof(Oot3dSourceCsabCurve::
                                       Oot3dAnimationCurveF32Key)
                          : f32 ? sizeof(Oot3dSourceCsabCurve::
                                       Oot3dAnimationCurveF32LinearKey)
                                : sizeof(Oot3dSourceCsabCurve::
                                             Oot3dAnimationCurveS16Key);
    const auto* keys = curveBytes.data() + sizeof(*header);
    for (int32_t index = 0; index < selection.KeyCount; ++index) {
        int32_t keyFrame = 0;
        const auto* keyBytes =
            keys + static_cast<size_t>(index) * stride;
        if (f32) {
            std::memcpy(&keyFrame, keyBytes, sizeof(keyFrame));
        } else {
            int16_t compactFrame = 0;
            std::memcpy(&compactFrame, keyBytes, sizeof(compactFrame));
            keyFrame = compactFrame;
        }
        if (!(static_cast<float>(keyFrame) > frame)) {
            selection.UpperIndex = index + 1;
            continue;
        }
        selection.UpperIndex = index;
        break;
    }
    if (selection.UpperIndex == 0) {
        selection.BeforeFirst = true;
    } else if (selection.UpperIndex >= selection.KeyCount) {
        selection.UpperIndex = selection.KeyCount;
        selection.AfterLast = true;
    } else {
        selection.LeftIndex = selection.UpperIndex - 1;
        selection.RightIndex = selection.UpperIndex;
    }
    return selection;
}

uint32_t CurveKeyAddress(const CurveAbiSelection& selection,
                         int32_t index, size_t stride) {
    return selection.KeyBaseAddress +
           static_cast<uint32_t>(static_cast<size_t>(index) * stride);
}

uint32_t CurveKeyFrameBits(
    bool f32, std::span<const uint8_t> curveBytes, int32_t index) {
    const auto* header = reinterpret_cast<const Oot3dSourceCsabCurve::
                                             Oot3dAnimationCurveHeader*>(
        curveBytes.data());
    const size_t stride = f32 && header->interpolationType == 2U
                              ? sizeof(Oot3dSourceCsabCurve::
                                       Oot3dAnimationCurveF32Key)
                          : f32 ? sizeof(Oot3dSourceCsabCurve::
                                       Oot3dAnimationCurveF32LinearKey)
                                : sizeof(Oot3dSourceCsabCurve::
                                             Oot3dAnimationCurveS16Key);
    uint32_t result = 0U;
    std::memcpy(&result, curveBytes.data() + sizeof(*header) +
                                  static_cast<size_t>(index) * stride,
                sizeof(result));
    return result;
}

void ApplyCurveCallerAbi(
    bool f32, std::span<const uint8_t> curveBytes,
    const CurveAbiSelection& selection, const S16TanStackScratch& tanScratch,
    oot3d::recomp::a32::GuestState& state) {
    const uint32_t keyCount = static_cast<uint32_t>(selection.KeyCount);
    if (selection.KeyCount <= 0) return;

    if (f32) {
        const size_t stride = selection.InterpolationType == 2U
                                  ? sizeof(Oot3dSourceCsabCurve::
                                               Oot3dAnimationCurveF32Key)
                                  : sizeof(Oot3dSourceCsabCurve::
                                               Oot3dAnimationCurveF32LinearKey);
        state.r[1] = keyCount;
        if (selection.KeyCount == 1) {
            state.r[2] = selection.CurveAddress;
            state.r[3] = selection.KeyBaseAddress;
            return;
        }
        if (selection.InterpolationType == 2U) {
            if (selection.BeforeFirst) {
                state.r[0] = 0U;
                state.r[2] = selection.KeyBaseAddress;
                state.r[3] = 0U;
                state.r[12] = 0U;
                return;
            }
            if (selection.AfterLast) {
                const uint32_t onePast = CurveKeyAddress(
                    selection, selection.KeyCount, stride);
                state.r[0] = onePast;
                state.r[2] = onePast;
                state.r[3] = 0U;
                state.r[12] = 0U;
                return;
            }
            if (selection.LeftIndex >= 0 && selection.RightIndex >= 0) {
                const uint32_t left = CurveKeyAddress(
                    selection, selection.LeftIndex, stride);
                const uint32_t right = CurveKeyAddress(
                    selection, selection.RightIndex, stride);
                state.r[0] = CurveKeyFrameBits(
                    f32, curveBytes, selection.RightIndex);
                state.r[2] = right;
                state.r[3] = right;
                state.r[12] = left;
            }
            return;
        }

        state.r[3] = selection.KeyBaseAddress;
        if (selection.BeforeFirst) {
            state.r[0] = 0U;
            state.r[2] = selection.KeyBaseAddress;
        } else if (selection.AfterLast) {
            const uint32_t onePast = CurveKeyAddress(
                selection, selection.KeyCount, stride);
            state.r[0] = onePast;
            state.r[2] = onePast;
        } else if (selection.LeftIndex >= 0 && selection.RightIndex >= 0) {
            const uint32_t right = CurveKeyAddress(
                selection, selection.RightIndex, stride);
            state.r[0] = right;
            state.r[1] = CurveKeyFrameBits(
                f32, curveBytes, selection.LeftIndex);
            state.r[2] = CurveKeyFrameBits(
                f32, curveBytes, selection.RightIndex);
        }
        return;
    }

    const size_t stride = sizeof(Oot3dSourceCsabCurve::
                                      Oot3dAnimationCurveS16Key);
    state.r[2] = keyCount;
    state.r[12] = selection.KeyBaseAddress;
    if (selection.KeyCount == 1) {
        state.r[1] = selection.KeyBaseAddress;
        state.r[3] = selection.InterpolationType;
        return;
    }
    if (selection.BeforeFirst) {
        state.r[1] = selection.KeyBaseAddress;
        state.r[3] = 0U;
        state.r[0] = 0U;
        return;
    }
    if (selection.AfterLast) {
        state.r[1] = selection.KeyBaseAddress;
        state.r[3] = 0U;
        const auto* header = reinterpret_cast<const Oot3dSourceCsabCurve::
                                                 Oot3dAnimationCurveHeader*>(
            curveBytes.data());
        const auto* keys = reinterpret_cast<const Oot3dSourceCsabCurve::
                                             Oot3dAnimationCurveS16Key*>(
            curveBytes.data() + sizeof(*header));
        state.r[0] = static_cast<uint32_t>(
            static_cast<int32_t>(keys[selection.KeyCount - 1].value));
        return;
    }
    if (selection.LeftIndex >= 0 && selection.RightIndex >= 0) {
        state.r[3] = CurveKeyAddress(
            selection, selection.LeftIndex, stride);
        const auto leftTan = EvaluateMathTanFCallerAbi(
            tanScratch.LeftTangentArgument, tanScratch.LeftTangentValue,
            tanScratch.LeftTangentLocal, 0x00308700U);
        state.r[0] = leftTan.R0;
        state.r[1] = leftTan.R1;
        state.r[2] = leftTan.R2 == 0U ? state.r[2] : leftTan.R2;
        state.r[14] = leftTan.R14;

        const auto rightTan = EvaluateMathTanFCallerAbi(
            tanScratch.RightTangentArgument, tanScratch.RightTangentValue,
            tanScratch.RightTangentLocal, 0x00308720U);
        state.r[0] = rightTan.R0;
        state.r[1] = rightTan.R1;
        state.r[2] = rightTan.R2 == 0U ? state.r[2] : rightTan.R2;
        state.r[14] = rightTan.R14;
    }
}

void ApplyCurveVfpAbi(
    bool f32, std::span<const uint8_t> curveBytes,
    const CurveAbiSelection& selection, const S16TanStackScratch& tanScratch,
    uint32_t frameBits, oot3d::recomp::a32::GuestState& state) {
    if (selection.InterpolationType != 2U ||
        selection.LeftIndex < 0 || selection.RightIndex < 0 ||
        (!f32 && !tanScratch.Exact)) {
        return;
    }

    VfpShadow shadow{state.fpscr, 0U, false};
    constexpr uint32_t kOne = 0x3F800000U;
    constexpr uint32_t kTwo = 0x40000000U;
    constexpr uint32_t kThree = 0x40400000U;
    const auto* header = reinterpret_cast<const Oot3dSourceCsabCurve::
                                             Oot3dAnimationCurveHeader*>(
        curveBytes.data());

    uint32_t elapsed = 0U;
    uint32_t interval = 0U;
    uint32_t leftValue = 0U;
    uint32_t rightValue = 0U;
    uint32_t leftTangent = 0U;
    uint32_t rightTangent = 0U;
    uint32_t leftFrame = 0U;
    uint32_t rightFrame = 0U;
    uint32_t effectiveFrame = frameBits;

    if (f32) {
        const auto* keys = reinterpret_cast<const Oot3dSourceCsabCurve::
                                 Oot3dAnimationCurveF32Key*>(
            curveBytes.data() + sizeof(*header));
        const auto& left = keys[selection.LeftIndex];
        const auto& right = keys[selection.RightIndex];
        leftFrame = shadow.FromSigned(
            static_cast<uint32_t>(left.frame));
        rightFrame = shadow.FromSigned(
            static_cast<uint32_t>(right.frame));
        if (selection.Wrapped) {
            rightFrame = shadow.FromSigned(static_cast<uint32_t>(
                static_cast<int64_t>(right.frame) + header->duration + 1));
            if (std::bit_cast<float>(frameBits) < 0.0F) {
                effectiveFrame = shadow.Add(
                    frameBits, shadow.FromSigned(static_cast<uint32_t>(
                        header->duration + 1)));
            }
        }
        leftValue = std::bit_cast<uint32_t>(left.value);
        rightValue = std::bit_cast<uint32_t>(right.value);
        leftTangent = std::bit_cast<uint32_t>(left.tangentOut);
        rightTangent = std::bit_cast<uint32_t>(right.tangentIn);
    } else {
        const auto* keys = reinterpret_cast<const Oot3dSourceCsabCurve::
                                 Oot3dAnimationCurveS16Key*>(
            curveBytes.data() + sizeof(*header));
        const auto& left = keys[selection.LeftIndex];
        const auto& right = keys[selection.RightIndex];
        leftFrame = shadow.FromSigned(static_cast<uint32_t>(
            static_cast<int32_t>(left.frame)));
        rightFrame = shadow.FromSigned(static_cast<uint32_t>(
            static_cast<int32_t>(right.frame)));
        if (selection.Wrapped) {
            rightFrame = shadow.FromSigned(static_cast<uint32_t>(
                static_cast<int64_t>(right.frame) + header->duration + 1));
            if (std::bit_cast<float>(frameBits) < 0.0F) {
                effectiveFrame = shadow.Add(
                    frameBits, shadow.FromSigned(static_cast<uint32_t>(
                        header->duration + 1)));
            }
        }
        leftValue = EvaluateS16ScaledValue(
            left.value, 0x38C90FDBU, state.fpscr);
        rightValue = EvaluateS16ScaledValue(
            right.value, 0x38C90FDBU, state.fpscr);
        const uint32_t difference = shadow.Subtract(rightValue, leftValue);
        const float differenceValue = FastFloatFromBits(difference);
        if (differenceValue < -3.1415927410125732F) {
            rightValue = shadow.Add(rightValue, 0x40C90FDBU);
        } else if (differenceValue > 3.1415927410125732F) {
            rightValue = shadow.Subtract(rightValue, 0x40C90FDBU);
        }
        leftTangent = tanScratch.LeftTangentValue;
        rightTangent = tanScratch.RightTangentValue;
    }

    elapsed = shadow.Subtract(effectiveFrame, leftFrame);
    interval = shadow.Subtract(rightFrame, leftFrame);
    const uint32_t inverseInterval = shadow.Divide(kOne, interval);
    const uint32_t t = shadow.Multiply(elapsed, inverseInterval);
    const uint32_t tMinusOne = shadow.Subtract(t, kOne);
    const uint32_t twoTMinusThree = shadow.NegativeMultiplySubtract(
        kThree, t, kTwo);
    const uint32_t valueDifference = shadow.Subtract(
        leftValue, rightValue);
    uint32_t valueTerm = shadow.Multiply(
        valueDifference, twoTMinusThree);
    valueTerm = shadow.Multiply(valueTerm, t);
    uint32_t tangentTerm = shadow.Multiply(
        tMinusOne, leftTangent);
    tangentTerm = shadow.MultiplyAccumulate(
        tangentTerm, t, rightTangent);
    const uint32_t elapsedTMinusOne = shadow.Multiply(
        elapsed, tMinusOne);
    uint32_t result = shadow.MultiplyAccumulate(
        leftValue, valueTerm, t);
    result = shadow.MultiplyAccumulate(
        result, elapsedTMinusOne, tangentTerm);
    (void)result;

    if (f32) {
        state.vfp[1] = elapsedTMinusOne;
        state.vfp[2] = elapsed;
        state.vfp[3] = tMinusOne;
        state.vfp[4] = tangentTerm;
        state.vfp[5] = valueTerm;
        state.vfp[6] = rightTangent;
        state.vfp[7] = valueDifference;
        state.vfp[8] = kTwo;
    } else {
        state.vfp[1] = elapsedTMinusOne;
        state.vfp[2] = tMinusOne;
        state.vfp[3] = rightTangent;
        state.vfp[4] = tangentTerm;
        state.vfp[5] = valueDifference;
        state.vfp[6] = kTwo;
    }
    // The maintained source evaluator already matches the target s0 result
    // under TargetFloatingPointScope.  Keep that validated value until the
    // shadow formula has been differentially proven for every curve family.
    state.fpscr |= shadow.ExceptionFlags | tanScratch.ExceptionFlags;
}

bool WriteTargetStackScratch(
    const SourceCsabCurveMemory& memory,
    const oot3d::recomp::a32::GuestState& state, bool f32,
    bool writesTanScratch, std::string* error) {
    constexpr uint32_t kF32StackBytes = 3U * sizeof(uint32_t);
    constexpr uint32_t kS16CoreStackBytes = 4U * sizeof(uint32_t);
    constexpr uint32_t kS16VfpStackBytes = 8U * sizeof(uint32_t);
    const uint32_t evaluatorStackBytes =
        f32 ? kF32StackBytes : kS16CoreStackBytes + kS16VfpStackBytes;
    const uint32_t stackBytes = writesTanScratch ? 64U : evaluatorStackBytes;
    if (memory.IsWritable == nullptr || memory.Write32 == nullptr ||
        state.r[13] < stackBytes ||
        !memory.IsWritable(memory.Context, state.r[13] - stackBytes,
                           stackBytes)) {
        if (error != nullptr) *error = "CSAB target stack is not writable";
        return false;
    }

    const uint32_t coreBase =
        state.r[13] - (f32 ? kF32StackBytes : kS16CoreStackBytes);
    if (!memory.Write32(memory.Context, coreBase, state.r[4]) ||
        !memory.Write32(memory.Context, coreBase + 4U, state.r[5]) ||
        !memory.Write32(memory.Context, coreBase + 8U, state.r[6]) ||
        (!f32 && !memory.Write32(memory.Context, coreBase + 12U,
                                 state.r[14]))) {
        if (error != nullptr) *error = "cannot reproduce CSAB core stack scratch";
        return false;
    }

    if (!f32) {
        const uint32_t vfpBase =
            state.r[13] - kS16CoreStackBytes - kS16VfpStackBytes;
        for (uint32_t index = 0U; index < 8U; ++index) {
            if (!memory.Write32(memory.Context, vfpBase + index * 4U,
                                state.vfp[16U + index])) {
                if (error != nullptr) *error = "cannot reproduce CSAB VFP stack scratch";
                return false;
            }
        }
    }
    return true;
}

bool WriteS16TanStackScratch(
    const SourceCsabCurveMemory& memory,
    const oot3d::recomp::a32::GuestState& state,
    const S16TanStackScratch& scratch) {
    if (!scratch.Valid) return true;
    if (memory.Write32 == nullptr) return false;

    const uint32_t evaluatorSp = state.r[13] - 48U;
    const uint32_t tanD8 = evaluatorSp - 12U;
    const uint32_t tanLr = evaluatorSp - 4U;
    const uint32_t tanLocal = evaluatorSp - 16U;
    const auto writeCall = [&](uint32_t returnAddress, uint32_t local) {
        return memory.Write32(memory.Context, tanLr, returnAddress) &&
               memory.Write32(memory.Context, tanD8, scratch.RightValue) &&
               memory.Write32(memory.Context, tanD8 + 4U, scratch.LeftValue) &&
               memory.Write32(memory.Context, tanLocal, local);
    };
    return writeCall(0x00308700U, scratch.LeftTangentLocal) &&
           writeCall(0x00308720U, scratch.RightTangentLocal);
}

bool LoadGuestCurve(
    const SourceCsabCurveMemory& memory, uint32_t stateAddress, bool f32,
    std::vector<uint8_t>* curveBytes, uint32_t* curveAddress, uint8_t* loop,
    uint64_t* guestBytesRead, std::string* error) {
    if (curveBytes == nullptr || curveAddress == nullptr || loop == nullptr ||
        guestBytesRead == nullptr ||
        memory.Read == nullptr) {
        return false;
    }
    uint32_t loadedCurveAddress = 0U;
    if (!memory.Read(memory.Context, stateAddress, &loadedCurveAddress,
                     sizeof(loadedCurveAddress)) ||
        !memory.Read(memory.Context, stateAddress + 4U, loop, sizeof(*loop)) ||
        loadedCurveAddress == 0U) {
        if (error != nullptr) *error = "cannot read CSAB curve state";
        return false;
    }
    *curveAddress = loadedCurveAddress;

    Oot3dSourceCsabCurve::Oot3dAnimationCurveHeader header{};
    if (!memory.Read(memory.Context, loadedCurveAddress, &header,
                     sizeof(header)) ||
        header.keyCount <= 0 || header.keyCount > kMaximumCurveKeys) {
        if (error != nullptr) *error = "invalid CSAB curve header";
        return false;
    }

    const size_t keySize = KeySize(f32, header.interpolationType);
    const size_t keyCount = static_cast<size_t>(header.keyCount);
    if (keySize != 0U &&
        keyCount > (std::numeric_limits<size_t>::max() - sizeof(header)) /
                       keySize) {
        if (error != nullptr) *error = "CSAB curve byte count overflow";
        return false;
    }
    const size_t size = sizeof(header) +
                        (keySize == 0U ? 0U : keyCount * keySize);
    curveBytes->resize(size);
    if (!memory.Read(memory.Context, loadedCurveAddress, curveBytes->data(),
                     size)) {
        if (error != nullptr) *error = "cannot read CSAB curve keys";
        return false;
    }
    *guestBytesRead += sizeof(uint32_t) + sizeof(uint8_t) + size;
    return true;
}

bool HasFiniteCurveInputs(
    bool f32, std::span<const uint8_t> curveBytes, float frame) {
    if (!std::isfinite(frame)) return false;
    if (!f32) return true;

    const auto* header = reinterpret_cast<
        const Oot3dSourceCsabCurve::Oot3dAnimationCurveHeader*>(
        curveBytes.data());
    const auto* keys = curveBytes.data() + sizeof(*header);
    if (header->interpolationType == 1U || header->interpolationType == 3U) {
        const auto* typed = reinterpret_cast<const Oot3dSourceCsabCurve::
            Oot3dAnimationCurveF32LinearKey*>(keys);
        for (int32_t index = 0; index < header->keyCount; ++index) {
            if (!std::isfinite(typed[index].value)) return false;
        }
    } else if (header->interpolationType == 2U) {
        const auto* typed = reinterpret_cast<const Oot3dSourceCsabCurve::
            Oot3dAnimationCurveF32Key*>(keys);
        for (int32_t index = 0; index < header->keyCount; ++index) {
            if (!std::isfinite(typed[index].value) ||
                !std::isfinite(typed[index].tangentIn) ||
                !std::isfinite(typed[index].tangentOut)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool SourceCsabCurveCore::Execute(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    const SourceCsabCurveMemory& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    uint32_t* blocksConsumed) {
    const bool f32 = pc == kSourceCsabCurveF32Entry;
    if ((!f32 && pc != kSourceCsabCurveS16Entry) || result == nullptr ||
        memory.Read == nullptr || memory.IsWritable == nullptr ||
        memory.Write32 == nullptr) {
        return false;
    }
    if ((state.fpscr & kFpscrExceptionEnableMask) != 0U) {
        ++mStats.Fallbacks;
        mLastError = "unsupported CSAB FPSCR mode";
        return false;
    }

    std::vector<uint8_t> curveBytes;
    uint32_t curveAddress = 0U;
    uint8_t loop = 0U;
    uint64_t bytesRead = 0U;
    std::string error;
    if (!LoadGuestCurve(memory, state.r[0], f32, &curveBytes, &curveAddress,
                        &loop, &bytesRead, &error)) {
        ++mStats.Fallbacks;
        mLastError = std::move(error);
        return false;
    }
    const uint32_t frameBits = state.vfp[0];
    const float frame = std::bit_cast<float>(frameBits);
    const uint32_t callerReturnAddress = state.r[14];
    const CurveAbiSelection selection = BuildCurveAbiSelection(
        f32, curveAddress, curveBytes, loop, frame);
    if (!HasFiniteCurveInputs(f32, curveBytes, frame)) {
        ++mStats.Fallbacks;
        mLastError = "non-finite CSAB input requires ARM fallback";
        return false;
    }

    TargetFloatingPointScope floatingPoint(state.fpscr);
    if (!floatingPoint.Active()) {
        ++mStats.Fallbacks;
        mLastError = "cannot install CSAB floating-point environment";
        return false;
    }
    const S16TanStackScratch tanScratch =
        f32 ? S16TanStackScratch{}
            : BuildS16TanStackScratch(curveBytes, loop, frame, state.fpscr);
    const Oot3dSourceCsabCurve::Oot3dAnimationCurveState curveState{
        curveBytes.data(), loop};
    const float value =
        f32 ? Oot3dSourceCsabCurve::Oot3d_EvalAnimationCurveF32(
                  &curveState, frame)
            : Oot3dSourceCsabCurve::Oot3d_EvalAnimationCurveS16(
                  &curveState, frame);
    if ((state.fpscr & kFpscrDefaultNaN) != 0U && !std::isfinite(value)) {
        ++mStats.Fallbacks;
        mLastError = "non-finite CSAB result requires ARM fallback";
        return false;
    }
    if (!WriteTargetStackScratch(memory, state, f32, tanScratch.Valid,
                                 &mLastError) ||
        !WriteS16TanStackScratch(memory, state, tanScratch)) {
        ++mStats.Fallbacks;
        if (mLastError.empty()) {
            mLastError = "cannot reproduce CSAB Math_TanF stack scratch";
        }
        return false;
    }

    uint32_t exceptionFlags = floatingPoint.TargetExceptionFlags();
    const LastCurveComparison comparison =
        TraceLastCurveComparison(f32, curveBytes, loop, frame);
    if (comparison.Valid) {
        const auto targetComparison =
            oot3d::recomp::a32::VfpBinary32Compare(
                comparison.Left, comparison.Right, true);
        state.fpscr = (state.fpscr & ~kFpscrNzcvMask) |
                      (targetComparison.value & kFpscrNzcvMask);
        state.cpsr = (state.cpsr & ~kFpscrNzcvMask) |
                     (targetComparison.value & kFpscrNzcvMask);
        exceptionFlags |= targetComparison.exception_flags;
    }
    if (!f32 && selection.AfterLast) {
        // The S16 key-search loop ends with an integer CMP after it has
        // consumed the final key.  That CMP, rather than the preceding VFP
        // key-vs-frame comparison, is the observable CPSR producer.
        const auto keySearchComparison = [&]() {
            const std::uint32_t left =
                static_cast<std::uint32_t>(selection.KeyCount);
            const std::uint32_t right =
                static_cast<std::uint32_t>(selection.UpperIndex);
            if (left == right) return 0x60000000U;
            return left < right ? 0x80000000U : 0x20000000U;
        }();
        state.cpsr = (state.cpsr & ~kFpscrNzcvMask) |
                     keySearchComparison;
    }
    if ((state.fpscr & exceptionFlags) != exceptionFlags) {
        ++mStats.FpscrExceptionUpdates;
    }
    state.fpscr |= exceptionFlags;
    state.vfp[0] = std::bit_cast<uint32_t>(value);
    ApplyCurveVfpAbi(f32, curveBytes, selection, tanScratch, frameBits,
                     state);
    ApplyCurveCallerAbi(f32, curveBytes, selection, tanScratch, state);
    if (!f32 && tanScratch.CpsrValid) {
        state.cpsr = (state.cpsr & ~kFpscrNzcvMask) |
                     tanScratch.Cpsr;
    }
    // The CSAB epilogue restores the entry LR into PC.  Math_TanF may leave
    // its own nested BL return address in live r14, which is observable in
    // the caller state but is not the function's return target.
    state.r[15] = callerReturnAddress;

    mStats.GuestBytesRead += bytesRead;
    if (f32) {
        ++mStats.F32Calls;
    } else {
        ++mStats.S16Calls;
    }
    mLastError.clear();
    *result = {oot3d::recomp::a32::ExitKind::Branch, callerReturnAddress,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr) *blocksConsumed = 1U;
    return true;
}

SourceCsabCurveStats SourceCsabCurveCore::Stats() const noexcept {
    return mStats;
}

void SourceCsabCurveCore::ResetStats() noexcept {
    mStats = {};
    mLastError.clear();
}

const std::string& SourceCsabCurveCore::LastError() const noexcept {
    return mLastError;
}

} // namespace Oot3dNativeGame
