#pragma once

#include "fast/renderer3ds/pica_frame_timing.h"

namespace Fast::Oot3d {

using NativeVisualInterpolationMode =
    ::Fast::Renderer3ds::PicaVisualInterpolationMode;
using NativeFrameTemporalSampleKind =
    ::Fast::Renderer3ds::PicaFrameTemporalSampleKind;
using NativeFrameCompositionPolicy =
    ::Fast::Renderer3ds::PicaFrameCompositionPolicy;
using NativeFrameTemporalSample =
    ::Fast::Renderer3ds::PicaFrameTemporalSample;

inline constexpr uint32_t kNativeFrameTemporalSampleSchemaVersion =
    ::Fast::Renderer3ds::kPicaFrameTemporalSampleSchemaVersion;

[[nodiscard]] constexpr const char* NativeVisualInterpolationModeName(
    NativeVisualInterpolationMode mode) noexcept {
    return ::Fast::Renderer3ds::PicaVisualInterpolationModeName(mode);
}

[[nodiscard]] inline NativeFrameTemporalSample
BuildNativeFrameTemporalSample(
    const NativeFrameCompositionPolicy& policy,
    NativeFrameTemporalSampleKind kind, uint64_t previousSourceFrameId,
    uint64_t currentSourceFrameId, uint64_t continuityEpoch, float alpha,
    float adaptiveSampleDeltaSeconds = 0.0F) noexcept {
    return ::Fast::Renderer3ds::BuildPicaFrameTemporalSample(
        policy, kind, previousSourceFrameId, currentSourceFrameId,
        continuityEpoch, alpha, adaptiveSampleDeltaSeconds);
}

} // namespace Fast::Oot3d
