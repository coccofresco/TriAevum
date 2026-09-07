#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Fast::Renderer3ds {

enum class PicaVisualInterpolationMode : uint8_t {
    Disabled,
    Fixed2x,
    Fixed3x,
    Adaptive,
};

enum class PicaFrameTemporalSampleKind : uint8_t {
    Authoritative,
    Transition,
    Resynchronized,
};

[[nodiscard]] constexpr const char* PicaVisualInterpolationModeName(
    PicaVisualInterpolationMode mode) noexcept {
    switch (mode) {
        case PicaVisualInterpolationMode::Disabled:
            return "disabled";
        case PicaVisualInterpolationMode::Fixed2x:
            return "fixed_2x";
        case PicaVisualInterpolationMode::Fixed3x:
            return "fixed_3x";
        case PicaVisualInterpolationMode::Adaptive:
            return "adaptive";
    }
    return "unknown";
}

struct PicaFrameCompositionPolicy {
    PicaVisualInterpolationMode Interpolation =
        PicaVisualInterpolationMode::Disabled;
    uint32_t NativeRateHz = 30U;
    uint32_t TargetPresentationRateHz = 30U;
    uint8_t FixedSampleMultiplier = 1U;
    bool PresentationRateLimited = true;

    [[nodiscard]] constexpr bool InterpolationEnabled() const noexcept {
        return Interpolation != PicaVisualInterpolationMode::Disabled;
    }

    [[nodiscard]] constexpr bool FixedMultiplier() const noexcept {
        return Interpolation == PicaVisualInterpolationMode::Fixed2x ||
               Interpolation == PicaVisualInterpolationMode::Fixed3x;
    }
};

inline constexpr uint32_t kPicaFrameTemporalSampleSchemaVersion = 1U;

// Describes a renderer presentation without changing a title's gameplay
// clock. Transition samples interpolate deferred PICA work between completed
// source frames; authoritative and resynchronized samples use one source.
struct PicaFrameTemporalSample {
    uint32_t SchemaVersion = 0U;
    PicaVisualInterpolationMode Interpolation =
        PicaVisualInterpolationMode::Disabled;
    PicaFrameTemporalSampleKind Kind =
        PicaFrameTemporalSampleKind::Authoritative;
    uint64_t PreviousSourceFrameId = 0U;
    uint64_t CurrentSourceFrameId = 0U;
    uint64_t ContinuityEpoch = 0U;
    uint32_t NativeRateHz = 0U;
    uint32_t TargetPresentationRateHz = 0U;
    uint8_t FixedSampleMultiplier = 1U;
    uint8_t SampleOrdinal = 0U;
    float Alpha = 1.0F;
    float SampleDeltaSeconds = 0.0F;
    bool Synthetic = false;
    bool HistoryReset = false;

    [[nodiscard]] bool Available() const noexcept {
        if (SchemaVersion != kPicaFrameTemporalSampleSchemaVersion ||
            NativeRateHz == 0U || ContinuityEpoch == 0U ||
            CurrentSourceFrameId == 0U || !std::isfinite(Alpha) ||
            Alpha < 0.0F || Alpha > 1.0F ||
            !std::isfinite(SampleDeltaSeconds) ||
            SampleDeltaSeconds <= 0.0F) {
            return false;
        }
        if (Interpolation == PicaVisualInterpolationMode::Fixed2x &&
            FixedSampleMultiplier != 2U) {
            return false;
        }
        if (Interpolation == PicaVisualInterpolationMode::Fixed3x &&
            FixedSampleMultiplier != 3U) {
            return false;
        }
        if (Interpolation == PicaVisualInterpolationMode::Disabled &&
            FixedSampleMultiplier != 1U) {
            return false;
        }
        return FixedSampleMultiplier == 0U ||
               SampleOrdinal < FixedSampleMultiplier;
    }
};

[[nodiscard]] inline PicaFrameTemporalSample
BuildPicaFrameTemporalSample(
    const PicaFrameCompositionPolicy& policy,
    PicaFrameTemporalSampleKind kind, uint64_t previousSourceFrameId,
    uint64_t currentSourceFrameId, uint64_t continuityEpoch, float alpha,
    float adaptiveSampleDeltaSeconds = 0.0F) noexcept {
    PicaFrameTemporalSample sample;
    sample.SchemaVersion = kPicaFrameTemporalSampleSchemaVersion;
    sample.Interpolation = policy.Interpolation;
    sample.Kind = kind;
    sample.PreviousSourceFrameId = previousSourceFrameId;
    sample.CurrentSourceFrameId = currentSourceFrameId;
    sample.ContinuityEpoch = continuityEpoch;
    sample.NativeRateHz = policy.NativeRateHz;
    sample.TargetPresentationRateHz = policy.TargetPresentationRateHz;
    sample.FixedSampleMultiplier = policy.FixedSampleMultiplier;
    sample.Alpha = std::clamp(
        std::isfinite(alpha) ? alpha : 1.0F, 0.0F, 1.0F);
    sample.Synthetic =
        kind == PicaFrameTemporalSampleKind::Transition &&
        sample.Alpha > 0.0F && sample.Alpha < 1.0F;
    sample.HistoryReset =
        kind == PicaFrameTemporalSampleKind::Resynchronized;
    if (policy.FixedSampleMultiplier != 0U) {
        const float ordinal =
            sample.Alpha * static_cast<float>(policy.FixedSampleMultiplier);
        sample.SampleOrdinal = static_cast<uint8_t>(std::min<uint32_t>(
            static_cast<uint32_t>(std::floor(ordinal + 1.0e-4F)),
            policy.FixedSampleMultiplier - 1U));
    }
    if (policy.TargetPresentationRateHz != 0U) {
        sample.SampleDeltaSeconds =
            1.0F / static_cast<float>(policy.TargetPresentationRateHz);
    } else if (std::isfinite(adaptiveSampleDeltaSeconds) &&
               adaptiveSampleDeltaSeconds > 0.0F) {
        sample.SampleDeltaSeconds = adaptiveSampleDeltaSeconds;
    } else {
        sample.SampleDeltaSeconds =
            1.0F / static_cast<float>(policy.NativeRateHz);
    }
    return sample;
}

} // namespace Fast::Renderer3ds
