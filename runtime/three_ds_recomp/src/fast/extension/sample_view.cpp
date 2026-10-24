#include "fast/extension/sample_view.h"

#include "fast/extension/directional_shadows.h"
#include "fast/extension/pica_uniform_layout.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Fast::Extension {
namespace {

bool SameView(const SampleViewMatrix& left, const SampleViewMatrix& right) noexcept {
    for (size_t i = 0; i < left.size(); ++i) {
        const float tolerance = 1.0e-4F * std::max(1.0F, std::abs(left[i]));
        if (!(std::abs(left[i] - right[i]) <= tolerance)) return false;
    }
    return true;
}

bool SameProjection(const SampleProjection& left, const SampleProjection& right) noexcept {
    const auto close = [](float a, float b) { return std::abs(a - b) <= 1.0e-4F * std::max(1.0F, std::abs(a)); };
    return close(left.X, right.X) && close(left.Y, right.Y);
}

} // namespace

std::optional<SampleProjection> DecodePicaPerspectiveProjection(
    std::span<const uint8_t> packedVertexUniforms) noexcept {
    constexpr size_t kRows = 4U;
    if (packedVertexUniforms.size() < kPicaPackedVertexFloatOffset + kRows * kPicaPackedVec4Bytes)
        return std::nullopt;
    std::array<float, 16> rows{};
    std::memcpy(rows.data(), packedVertexUniforms.data() + kPicaPackedVertexFloatOffset, sizeof(rows));
    for (const float value : rows)
        if (!std::isfinite(value)) return std::nullopt;
    constexpr float kEpsilon = 1.0e-6F;
    const auto zero = [](float value) { return std::abs(value) <= kEpsilon; };
    // Perspective: clip w is the view depth alone.
    if (!zero(rows[12]) || !zero(rows[13]) || zero(rows[14]) || !zero(rows[15])) return std::nullopt;
    SampleProjection result;
    if (zero(rows[0]) && zero(rows[5])) {
        // Rotated with the framebuffer: clip x from view y, clip y from view x.
        result = { std::abs(rows[4]), std::abs(rows[1]) };
    } else if (zero(rows[1]) && zero(rows[4])) {
        result = { std::abs(rows[0]), std::abs(rows[5]) };
    } else {
        return std::nullopt;
    }
    if (!(result.X > kEpsilon) || !(result.Y > kEpsilon)) return std::nullopt;
    return result;
}

void SampleProjectionAccumulator::Observe(uint64_t frame, const SampleProjection& projection) noexcept {
    if (!std::isfinite(projection.X) || !std::isfinite(projection.Y)) return;
    if (frame != mFrame) {
        mFrame = frame;
        mCount = 0;
    }
    for (uint32_t i = 0; i < mCount; ++i) {
        if (SameProjection(mCandidates[i].Projection, projection)) {
            ++mCandidates[i].Draws;
            return;
        }
    }
    if (mCount < kCandidates) mCandidates[mCount++] = { projection, 1U };
}

std::optional<SampleProjection> SampleProjectionAccumulator::Resolve(uint64_t frame) const noexcept {
    if (frame != mFrame || mCount == 0U) return std::nullopt;
    const auto* best = &mCandidates[0];
    for (uint32_t i = 1; i < mCount; ++i)
        if (mCandidates[i].Draws > best->Draws) best = &mCandidates[i];
    return best->Projection;
}

PerspectiveViewState ApplySampleProjection(const PerspectiveViewState& guest,
                                           const SampleProjection& native) noexcept {
    const float guestX = std::abs(guest.Projection[0]);
    const float guestY = std::abs(guest.Projection[5]);
    if (!(guestX > 1.0e-6F) || !(guestY > 1.0e-6F)) return guest;
    const float scaleX = native.X / guestX;
    const float scaleY = native.Y / guestY;
    // A different camera, not an adjustment of this one.
    const auto plausible = [](float scale) { return std::isfinite(scale) && scale > 0.25F && scale < 4.0F; };
    if (!plausible(scaleX) || !plausible(scaleY)) return guest;
    PerspectiveViewState result = guest;
    // Column-major: row r of column c is [c * 4 + r].
    for (size_t column = 0; column < 4; ++column) {
        result.Projection[column * 4] *= scaleX;
        result.Projection[column * 4 + 1] *= scaleY;
        result.WorldToClip[column * 4] *= scaleX;
        result.WorldToClip[column * 4 + 1] *= scaleY;
    }
    result.Left /= scaleX;
    result.Right /= scaleX;
    result.Bottom /= scaleY;
    result.Top /= scaleY;
    return result;
}

void SampleViewAccumulator::Observe(uint64_t frame, const SampleViewMatrix& worldToView) noexcept {
    for (const float value : worldToView)
        if (!std::isfinite(value)) return;
    if (frame != mFrame) {
        mFrame = frame;
        mCount = 0;
    }
    for (uint32_t i = 0; i < mCount; ++i) {
        if (SameView(mCandidates[i].View, worldToView)) {
            ++mCandidates[i].Draws;
            return;
        }
    }
    if (mCount < kCandidates) mCandidates[mCount++] = { worldToView, 1U };
}

std::optional<SampleViewMatrix> SampleViewAccumulator::Resolve(uint64_t frame) const noexcept {
    if (frame != mFrame || mCount == 0U) return std::nullopt;
    const auto* best = &mCandidates[0];
    for (uint32_t i = 1; i < mCount; ++i)
        if (mCandidates[i].Draws > best->Draws) best = &mCandidates[i];
    return best->View;
}

uint32_t SampleViewAccumulator::DistinctViews(uint64_t frame) const noexcept {
    return frame == mFrame ? mCount : 0U;
}

PerspectiveViewState ResolveSamplePerspective(const PerspectiveViewState& guest,
                                              const SampleViewMatrix& worldToView) noexcept {
    DirectionalShadowMatrix viewToWorld{};
    if (!InvertDirectionalShadowMatrix(worldToView, viewToWorld)) return guest;
    PerspectiveViewState result = guest;
    result.Serial = 0U;
    for (size_t column = 0; column < 4; ++column)
        for (size_t row = 0; row < 4; ++row) {
            float value = 0.0F;
            for (size_t k = 0; k < 4; ++k) value += guest.Projection[k * 4 + row] * worldToView[column * 4 + k];
            result.WorldToClip[column * 4 + row] = value;
        }
    const float w = viewToWorld[15] != 0.0F ? viewToWorld[15] : 1.0F;
    result.Eye = { viewToWorld[12] / w, viewToWorld[13] / w, viewToWorld[14] / w };
    // The view looks down -Z.
    std::array<float, 3> forward{ -viewToWorld[8], -viewToWorld[9], -viewToWorld[10] };
    const float length = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (length <= 1.0e-12F) return guest;
    float distance = 1.0F;
    if (guest.CameraAvailable) {
        const float dx = guest.At[0] - guest.Eye[0], dy = guest.At[1] - guest.Eye[1], dz = guest.At[2] - guest.Eye[2];
        distance = std::max(std::sqrt(dx * dx + dy * dy + dz * dz), 1.0e-3F);
    }
    for (size_t i = 0; i < 3; ++i) result.At[i] = result.Eye[i] + forward[i] / length * distance;
    result.CameraAvailable = true;
    return result;
}

} // namespace Fast::Extension
