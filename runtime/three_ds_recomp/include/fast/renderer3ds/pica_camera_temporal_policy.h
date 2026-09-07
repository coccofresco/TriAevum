#pragma once

#include "fast/renderer3ds/pica_shader_hooks.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace Fast::Renderer3ds {

struct PicaCameraTemporalDelta {
    double BasisChord = 0;
    double NearPlaneTravel = 0;
    double ProjectionDelta = 0;

    [[nodiscard]] bool Discontinuous() const noexcept {
        // Presentation safety limits, not game camera rules. A cut must not
        // sweep through an invented view just because geometry identities match.
        return BasisChord > 0.5 || NearPlaneTravel > 32.0 || ProjectionDelta > 0.5;
    }
};

template <typename Uniforms>
[[nodiscard]] std::optional<PicaCameraTemporalDelta>
MeasurePicaCameraTemporalDelta(const PicaVertexTransformLayout& layout, const Uniforms& previous,
                               const Uniforms& current) noexcept {
    if (!layout.Available())
        return std::nullopt;
    const auto eye = [&](const Uniforms& uniforms) -> std::optional<std::array<double, 3>> {
        double rows[3][4];
        for (size_t i = 0; i < 3; ++i)
            for (size_t j = 0; j < 4; ++j) {
                rows[i][j] = uniforms[layout.ViewFirstUniform + i][j];
                if (!std::isfinite(rows[i][j]))
                    return std::nullopt;
            }
        for (size_t column = 0; column < 3; ++column) {
            size_t pivot = column;
            for (size_t row = column + 1; row < 3; ++row)
                if (std::abs(rows[row][column]) > std::abs(rows[pivot][column]))
                    pivot = row;
            if (std::abs(rows[pivot][column]) < 1.0e-8)
                return std::nullopt;
            for (size_t j = 0; j < 4; ++j)
                std::swap(rows[column][j], rows[pivot][j]);
            const double scale = rows[column][column];
            for (double& value : rows[column])
                value /= scale;
            for (size_t row = 0; row < 3; ++row) {
                if (row == column)
                    continue;
                const double factor = rows[row][column];
                for (size_t j = 0; j < 4; ++j)
                    rows[row][j] -= factor * rows[column][j];
            }
        }
        return std::array<double, 3>{ -rows[0][3], -rows[1][3], -rows[2][3] };
    };
    const auto oldEye = eye(previous), newEye = eye(current);
    if (!oldEye || !newEye)
        return std::nullopt;
    PicaCameraTemporalDelta delta;
    for (size_t row = 0; row < 3; ++row) {
        const auto& a = previous[layout.ViewFirstUniform + row];
        const auto& b = current[layout.ViewFirstUniform + row];
        double aa = 0, bb = 0, ab = 0;
        for (size_t j = 0; j < 3; ++j) {
            aa += a[j] * a[j];
            bb += b[j] * b[j];
            ab += a[j] * b[j];
        }
        if (!(aa > 1.0e-12 && bb > 1.0e-12))
            return std::nullopt;
        delta.BasisChord = std::max(
            delta.BasisChord, std::sqrt(std::max(0.0, 2.0 - 2.0 * std::clamp(ab / std::sqrt(aa * bb), -1.0, 1.0))));
    }
    // Use the projection's depth scale as a unit, never title/world-unit constants.
    const auto nearDistance = [&](const Uniforms& u) {
        const auto& z = u[layout.ProjectionFirstUniform + 2];
        const auto& w = u[layout.ProjectionFirstUniform + 3];
        double distance = 0;
        // PICA's native clip-depth interval is [-w, 0], before backend conversion.
        for (double boundary : { -1.0, 0.0 }) {
            const double denominator = z[2] - boundary * w[2];
            if (std::abs(denominator) < 1.0e-8)
                continue;
            const double candidate = std::abs((z[3] - boundary * w[3]) / denominator);
            if (std::isfinite(candidate) && candidate > 1.0e-8)
                distance = distance == 0 ? candidate : std::min(distance, candidate);
        }
        return distance;
    };
    const double near = std::max(nearDistance(previous), nearDistance(current));
    double travel2 = 0;
    for (size_t i = 0; i < 3; ++i)
        travel2 += std::pow((*oldEye)[i] - (*newEye)[i], 2);
    double viewScale2 = 0;
    for (size_t row = 0; row < 3; ++row)
        for (size_t col = 0; col < 3; ++col) {
            const double value = current[layout.ViewFirstUniform + row][col];
            viewScale2 += value * value / 3.0;
        }
    if (near > 0)
        delta.NearPlaneTravel = std::sqrt(travel2 * viewScale2) / near;
    for (size_t row = 0; row < 4; ++row)
        for (size_t col = 0; col < 4; ++col) {
            const double a = previous[layout.ProjectionFirstUniform + row][col];
            const double b = current[layout.ProjectionFirstUniform + row][col];
            if (!std::isfinite(a) || !std::isfinite(b))
                return std::nullopt;
            delta.ProjectionDelta =
                std::max(delta.ProjectionDelta, std::abs(a - b) / std::max({ 1.0, std::abs(a), std::abs(b) }));
        }
    return delta;
}

// Producers may precompose the camera into the model rows and leave view as
// identity. Measure the effective rigid transform, not a presumed uniform owner.
// Callers must exclude animated palettes and vote across independent world draws.
template <typename Uniforms>
[[nodiscard]] std::optional<PicaCameraTemporalDelta>
MeasurePicaRigidViewTemporalDelta(const PicaVertexTransformLayout& layout, const Uniforms& previous,
                                  const Uniforms& current) noexcept {
    if (!layout.Available())
        return std::nullopt;
    const auto composed = [&](const Uniforms& input) {
        auto output = input;
        for (size_t row = 0; row < 3; ++row)
            for (size_t col = 0; col < 4; ++col) {
                double value = col == 3 ? input[layout.ViewFirstUniform + row][3] : 0;
                for (size_t j = 0; j < 3; ++j)
                    value += static_cast<double>(input[layout.ViewFirstUniform + row][j]) *
                             input[layout.ModelFirstUniform + j][col];
                output[layout.ViewFirstUniform + row][col] = static_cast<float>(value);
            }
        return output;
    };
    return MeasurePicaCameraTemporalDelta(layout, composed(previous), composed(current));
}

} // namespace Fast::Renderer3ds
