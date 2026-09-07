#include "fast/oot3d/temporal_history_manager.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace Fast::Oot3d {
namespace {

float DistanceSquared(const std::array<float, 3>& a,
                      const std::array<float, 3>& b) {
    float result = 0.0F;
    for (size_t i = 0; i < 3; ++i) {
        const float delta = a[i] - b[i];
        result += delta * delta;
    }
    return result;
}

float MaximumDifference(const std::array<float, 16>& a,
                        const std::array<float, 16>& b) {
    float result = 0.0F;
    for (size_t i = 0; i < a.size(); ++i)
        result = std::max(result, std::abs(a[i] - b[i]));
    return result;
}

} // namespace

bool InvertTemporalMatrix(const std::array<float, 16>& matrix,
                          std::array<float, 16>& inverse) {
    float augmented[4][8]{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column)
            augmented[row][column] = matrix[column * 4 + row];
        augmented[row][4 + row] = 1.0F;
    }
    for (size_t column = 0; column < 4; ++column) {
        size_t pivot = column;
        for (size_t row = column + 1; row < 4; ++row) {
            if (std::abs(augmented[row][column]) >
                std::abs(augmented[pivot][column])) pivot = row;
        }
        if (std::abs(augmented[pivot][column]) < 1.0e-8F) return false;
        if (pivot != column)
            for (size_t item = 0; item < 8; ++item)
                std::swap(augmented[pivot][item], augmented[column][item]);
        const float divisor = augmented[column][column];
        for (float& item : augmented[column]) item /= divisor;
        for (size_t row = 0; row < 4; ++row) {
            if (row == column) continue;
            const float factor = augmented[row][column];
            for (size_t item = 0; item < 8; ++item)
                augmented[row][item] -= factor * augmented[column][item];
        }
    }
    for (size_t row = 0; row < 4; ++row)
        for (size_t column = 0; column < 4; ++column)
            inverse[column * 4 + row] = augmented[row][4 + column];
    return true;
}

size_t TemporalHistoryManager::KeyHash::operator()(
    const SceneViewKey& key) const {
    size_t value = std::hash<uint64_t>{}(key.RenderTargetNamespace);
    value ^= std::hash<uint32_t>{}(key.ColorPhysicalAddress) + 0x9e3779b9U +
             (value << 6U) + (value >> 2U);
    value ^= std::hash<uint32_t>{}(key.DepthPhysicalAddress) + 0x9e3779b9U +
             (value << 6U) + (value >> 2U);
    return value;
}

TemporalViewState TemporalHistoryManager::Prepare(
    const TemporalViewInput& input) {
    if (const auto found = mRecords.find(input.Key);
        found != mRecords.end() &&
        found->second.Input.RenderedFrameId == input.RenderedFrameId) {
        return found->second.State;
    }
    TemporalViewState state{};
    state.CurrentWorldToClip = input.WorldToClip;
    state.PreviousWorldToClip = input.WorldToClip;
    state.CurrentJitterUv = input.JitterUv;
    state.PreviousJitterUv = input.JitterUv;
    state.HistoryEpoch = mEpoch;
    auto previous = mRecords.find(input.Key);
    TemporalResetReason reason = TemporalResetReason::None;
    if (!InvertTemporalMatrix(input.WorldToClip,
                              state.InverseCurrentWorldToClip)) {
        reason = TemporalResetReason::NonInvertibleViewProjection;
    } else if (previous == mRecords.end()) {
        reason = TemporalResetReason::FirstFrame;
    } else if (previous->second.Input.SourceResetEpoch !=
               input.SourceResetEpoch) {
        reason = TemporalResetReason::ExplicitReset;
    } else if (previous->second.Input.Width != input.Width ||
               previous->second.Input.Height != input.Height) {
        reason = TemporalResetReason::ResolutionChanged;
    } else if (MaximumDifference(previous->second.Input.Projection,
                                 input.Projection) > 1.0e-4F) {
        reason = TemporalResetReason::ProjectionChanged;
    } else if (DistanceSquared(previous->second.Input.Eye, input.Eye) >
               300.0F * 300.0F ||
               DistanceSquared(previous->second.Input.At, input.At) >
               300.0F * 300.0F) {
        reason = TemporalResetReason::CameraTeleport;
    }
    state.CameraCut = reason != TemporalResetReason::None;
    state.HistoryValid = !state.CameraCut;
    state.ResetReason = reason;
    if (state.HistoryValid) {
        state.PreviousWorldToClip = previous->second.Input.WorldToClip;
        state.PreviousJitterUv = previous->second.Input.JitterUv;
    }
    mRecords[input.Key] = {input, state};
    return state;
}

void TemporalHistoryManager::Reset() {
    ++mEpoch;
    mRecords.clear();
}

void TemporalHistoryManager::Invalidate(const SceneViewKey& key) {
    mRecords.erase(key);
}

uint64_t TemporalHistoryManager::Epoch() const { return mEpoch; }

} // namespace Fast::Oot3d
