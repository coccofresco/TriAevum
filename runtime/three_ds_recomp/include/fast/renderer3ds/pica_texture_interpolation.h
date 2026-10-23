#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace Fast::Renderer3ds {

enum class PicaTextureWrap : uint8_t { NonPeriodic, Repeat, MirroredRepeat };

// One consumer of an adapter-proven uniform translation. The adapter must
// describe EVERY enabled consumer, including non-periodic samplers. Slots in
// the two endpoint descriptions identify the same texture unit and axis.
// Final coordinate = linear terms + CoordinateScale * uniform + CoordinateBias.
// CoordinateScale is +1 or -1: the uniform is already in final UV units, with
// an optional native V flip. Input/model-space offsets are not this contract.
struct PicaTextureTranslation {
    uint8_t UniformRow = 0xffU;
    uint8_t UniformComponent = 3U;
    uint8_t CoordinateSet = 0xffU;
    uint8_t InputRegister = 0xffU;
    PicaTextureWrap Wrap = PicaTextureWrap::NonPeriodic;
    float CoordinateScale = 1.0F;
    double CoordinateBias = 0.0;

    [[nodiscard]] bool Available() const noexcept {
        return UniformRow < 96U && UniformComponent < 4U && CoordinateSet < 3U && InputRegister < 16U;
    }
};
using PicaTextureTranslations = std::array<PicaTextureTranslation, 6>;

// Refine an existing scalar sample, touching only translations with a common
// periodic interpretation at both endpoints. Linear parts, ordinary scrolls,
// unknown rows and exact endpoints retain the caller's original values.
inline void InterpolatePicaTextureTranslations(
    std::span<const std::array<float, 4>> previous,
    std::span<const std::array<float, 4>> current,
    const PicaTextureTranslations& before, const PicaTextureTranslations& after,
    float alpha, std::span<std::array<float, 4>> output) {
    if (!(alpha > 0.0F && alpha < 1.0F) || previous.size() != current.size() ||
        output.size() < current.size()) return;
    std::array<bool, 96U * 4U> visited{};
    const auto period = [](PicaTextureWrap wrap) {
        return wrap == PicaTextureWrap::Repeat ? 1.0 :
               wrap == PicaTextureWrap::MirroredRepeat ? 2.0 : 0.0;
    };
    for (const auto& candidate : after) {
        if (!candidate.Available() || candidate.UniformRow >= current.size()) continue;
        const auto row = candidate.UniformRow, component = candidate.UniformComponent;
        const size_t key = size_t(row) * 4U + component;
        if (visited[key]) continue;
        visited[key] = true;
        double commonPeriod = 0.0, biasDelta = 0.0;
        bool valid = true, first = true;
        for (size_t slot = 0; slot < after.size(); ++slot) {
            const auto& a = before[slot];
            const auto& b = after[slot];
            const bool usesBefore = a.Available() && a.UniformRow == row && a.UniformComponent == component;
            const bool usesAfter = b.Available() && b.UniformRow == row && b.UniformComponent == component;
            if (!usesBefore && !usesAfter) continue;
            if (!usesBefore || !usesAfter || a.CoordinateSet != b.CoordinateSet ||
                a.InputRegister != b.InputRegister || a.Wrap != b.Wrap || period(b.Wrap) == 0.0 ||
                a.CoordinateScale != b.CoordinateScale || std::abs(b.CoordinateScale) != 1.0F ||
                !std::isfinite(a.CoordinateBias) || !std::isfinite(b.CoordinateBias)) {
                valid = false;
                break;
            }
            const double delta = (b.CoordinateBias - a.CoordinateBias) / b.CoordinateScale;
            if (!std::isfinite(delta) || (!first && delta != biasDelta)) {
                valid = false;
                break;
            }
            biasDelta = delta;
            first = false;
            // Period 2 is also a period of Repeat. A row feeding both Repeat
            // and MirroredRepeat must use their common period, not unwrap twice.
            commonPeriod = std::max(commonPeriod, period(b.Wrap));
        }
        const double a = previous[row][component], b = current[row][component];
        if (!valid || first || !std::isfinite(a) || !std::isfinite(b)) continue;
        const double delta = b - a + biasDelta;
        if (!std::isfinite(delta)) continue;
        double shortest = std::fmod(delta, commonPeriod);
        if (shortest > commonPeriod * 0.5) shortest -= commonPeriod;
        if (shortest < -commonPeriod * 0.5) shortest += commonPeriod;
        if (shortest == delta) continue; // preserve scalar rounding and half-period ties
        const double value = a + double(alpha) * (b - a + shortest - delta);
        if (std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max())
            output[row][component] = static_cast<float>(value);
    }
}

} // namespace Fast::Renderer3ds
