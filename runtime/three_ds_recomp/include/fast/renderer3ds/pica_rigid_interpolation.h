#pragma once

#include <array>
#include <cmath>
#include <optional>

namespace Fast::Renderer3ds {

// Three consecutive PICA vertex-shader float uniforms holding an affine 3x4
// transform, one matrix row per uniform: (m_i0, m_i1, m_i2, t_i).
using PicaAffineRows = std::array<std::array<float, 4>, 3>;

// A transform of that form that is a rotation with a positive scale per row
// (S * R), plus its translation.
struct PicaRigidTransform {
    std::array<float, 4> Rotation{}; // unit quaternion (x, y, z, w)
    std::array<float, 3> Scale{};
    std::array<float, 3> Translation{};
};

// The rotation, scale and translation of `rows`, or nothing when the rows are
// not a scaled proper rotation (orthogonal after normalization, determinant
// +1): projections, mirrors, shears and other uniforms.
inline std::optional<PicaRigidTransform> DecomposePicaRigidRows(const PicaAffineRows& rows) {
    constexpr float kOrthogonality = 0.02F;
    PicaRigidTransform transform;
    float r[3][3]{};
    for (size_t row = 0; row < 3U; ++row) {
        for (size_t column = 0; column < 4U; ++column) {
            if (!std::isfinite(rows[row][column])) return std::nullopt;
        }
        const float length = std::sqrt(rows[row][0] * rows[row][0] + rows[row][1] * rows[row][1] +
                                       rows[row][2] * rows[row][2]);
        if (!(length > 1.0e-6F)) return std::nullopt;
        transform.Scale[row] = length;
        transform.Translation[row] = rows[row][3];
        for (size_t column = 0; column < 3U; ++column) r[row][column] = rows[row][column] / length;
    }
    for (size_t first = 0; first < 3U; ++first) {
        for (size_t second = first + 1U; second < 3U; ++second) {
            const float dot = r[first][0] * r[second][0] + r[first][1] * r[second][1] + r[first][2] * r[second][2];
            if (std::abs(dot) > kOrthogonality) return std::nullopt;
        }
    }
    const float determinant = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) -
                              r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                              r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (determinant < 0.9F) return std::nullopt;
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
    const float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0.0F) {
        const float s = std::sqrt(trace + 1.0F) * 2.0F;
        w = 0.25F * s;
        x = (r[2][1] - r[1][2]) / s;
        y = (r[0][2] - r[2][0]) / s;
        z = (r[1][0] - r[0][1]) / s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = std::sqrt(1.0F + r[0][0] - r[1][1] - r[2][2]) * 2.0F;
        w = (r[2][1] - r[1][2]) / s;
        x = 0.25F * s;
        y = (r[0][1] + r[1][0]) / s;
        z = (r[0][2] + r[2][0]) / s;
    } else if (r[1][1] > r[2][2]) {
        const float s = std::sqrt(1.0F + r[1][1] - r[0][0] - r[2][2]) * 2.0F;
        w = (r[0][2] - r[2][0]) / s;
        x = (r[0][1] + r[1][0]) / s;
        y = 0.25F * s;
        z = (r[1][2] + r[2][1]) / s;
    } else {
        const float s = std::sqrt(1.0F + r[2][2] - r[0][0] - r[1][1]) * 2.0F;
        w = (r[1][0] - r[0][1]) / s;
        x = (r[0][2] + r[2][0]) / s;
        y = (r[1][2] + r[2][1]) / s;
        z = 0.25F * s;
    }
    const float norm = std::sqrt(x * x + y * y + z * z + w * w);
    transform.Rotation = {x / norm, y / norm, z / norm, w / norm};
    return transform;
}

inline PicaAffineRows ComposePicaRigidRows(const PicaRigidTransform& transform) {
    const auto [x, y, z, w] = transform.Rotation;
    const float r[3][3] = {
        {1.0F - 2.0F * (y * y + z * z), 2.0F * (x * y - z * w), 2.0F * (x * z + y * w)},
        {2.0F * (x * y + z * w), 1.0F - 2.0F * (x * x + z * z), 2.0F * (y * z - x * w)},
        {2.0F * (x * z - y * w), 2.0F * (y * z + x * w), 1.0F - 2.0F * (x * x + y * y)},
    };
    PicaAffineRows rows{};
    for (size_t row = 0; row < 3U; ++row) {
        for (size_t column = 0; column < 3U; ++column)
            rows[row][column] = r[row][column] * transform.Scale[row];
        rows[row][3] = transform.Translation[row];
    }
    return rows;
}

// Flip-aware interpolation of two rigid transforms: the rotation follows the
// shortest arc (spherical), scale and translation are linear. A per-element
// blend of opposite orientations (a character turning round between two
// simulated frames) shrinks the rotation part towards zero and flattens the
// model for a frame; this keeps it rigid, turned part of the way instead.
// Nothing when either endpoint is not rigid: the caller blends per element.
inline std::optional<PicaAffineRows> InterpolatePicaRigidRows(const PicaAffineRows& previous,
                                                              const PicaAffineRows& current, float alpha) {
    const auto from = DecomposePicaRigidRows(previous);
    const auto to = DecomposePicaRigidRows(current);
    if (!from.has_value() || !to.has_value()) return std::nullopt;
    auto target = to->Rotation;
    float dot = 0.0F;
    for (size_t index = 0; index < 4U; ++index) dot += from->Rotation[index] * target[index];
    if (dot < 0.0F) {
        for (float& value : target) value = -value;
        dot = -dot;
    }
    float weightFrom = 1.0F - alpha;
    float weightTo = alpha;
    if (dot < 0.9995F) {
        const float angle = std::acos(std::fmin(dot, 1.0F));
        const float sine = std::sin(angle);
        weightFrom = std::sin((1.0F - alpha) * angle) / sine;
        weightTo = std::sin(alpha * angle) / sine;
    }
    PicaRigidTransform blended;
    float norm = 0.0F;
    for (size_t index = 0; index < 4U; ++index) {
        blended.Rotation[index] = from->Rotation[index] * weightFrom + target[index] * weightTo;
        norm += blended.Rotation[index] * blended.Rotation[index];
    }
    norm = std::sqrt(norm);
    for (float& value : blended.Rotation) value /= norm;
    for (size_t index = 0; index < 3U; ++index) {
        blended.Scale[index] = from->Scale[index] + (to->Scale[index] - from->Scale[index]) * alpha;
        blended.Translation[index] =
            from->Translation[index] + (to->Translation[index] - from->Translation[index]) * alpha;
    }
    return ComposePicaRigidRows(blended);
}

} // namespace Fast::Renderer3ds
