#include "fast/oot3d/directional_shadows.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace Fast::Oot3d {
namespace {

constexpr float kEpsilon = 1.0e-5F;

struct Vec3 {
    float X = 0.0F;
    float Y = 0.0F;
    float Z = 0.0F;
};

struct Vec4 {
    float X = 0.0F;
    float Y = 0.0F;
    float Z = 0.0F;
    float W = 1.0F;
};

Vec3 Add(Vec3 left, Vec3 right) {
    return { left.X + right.X, left.Y + right.Y, left.Z + right.Z };
}

Vec3 Scale(Vec3 value, float scale) {
    return { value.X * scale, value.Y * scale, value.Z * scale };
}

float Dot(Vec3 left, Vec3 right) {
    return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
}

Vec3 Cross(Vec3 left, Vec3 right) {
    return {
        left.Y * right.Z - left.Z * right.Y,
        left.Z * right.X - left.X * right.Z,
        left.X * right.Y - left.Y * right.X,
    };
}

float Length(Vec3 value) {
    return std::sqrt(Dot(value, value));
}

bool Normalize(Vec3& value) {
    const float length = Length(value);
    if (!std::isfinite(length) || length <= kEpsilon) {
        return false;
    }
    value = Scale(value, 1.0F / length);
    return true;
}

Vec4 Transform(const DirectionalShadowMatrix& matrix, Vec4 value) {
    return {
        matrix[0] * value.X + matrix[4] * value.Y + matrix[8] * value.Z + matrix[12] * value.W,
        matrix[1] * value.X + matrix[5] * value.Y + matrix[9] * value.Z + matrix[13] * value.W,
        matrix[2] * value.X + matrix[6] * value.Y + matrix[10] * value.Z + matrix[14] * value.W,
        matrix[3] * value.X + matrix[7] * value.Y + matrix[11] * value.Z + matrix[15] * value.W,
    };
}

bool Invert(const DirectionalShadowMatrix& matrix, DirectionalShadowMatrix& inverse) {
    double augmented[4][8] = {};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            augmented[row][column] = matrix[column * 4 + row];
        }
        augmented[row][row + 4] = 1.0;
    }

    for (size_t column = 0; column < 4; ++column) {
        size_t pivot = column;
        double pivotMagnitude = std::abs(augmented[pivot][column]);
        for (size_t row = column + 1; row < 4; ++row) {
            const double candidate = std::abs(augmented[row][column]);
            if (candidate > pivotMagnitude) {
                pivot = row;
                pivotMagnitude = candidate;
            }
        }
        if (!std::isfinite(pivotMagnitude) || pivotMagnitude <= std::numeric_limits<double>::epsilon()) {
            return false;
        }
        if (pivot != column) {
            std::swap(augmented[pivot], augmented[column]);
        }

        const double divisor = augmented[column][column];
        for (double& value : augmented[column]) {
            value /= divisor;
        }
        for (size_t row = 0; row < 4; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = augmented[row][column];
            for (size_t item = 0; item < 8; ++item) {
                augmented[row][item] -= factor * augmented[column][item];
            }
        }
    }

    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            inverse[column * 4 + row] = static_cast<float>(augmented[row][column + 4]);
        }
    }
    return true;
}

} // namespace

DirectionalShadowMatrix IdentityDirectionalShadowMatrix() noexcept {
    return {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
}

bool InvertDirectionalShadowMatrix(const DirectionalShadowMatrix& matrix, DirectionalShadowMatrix& inverse) noexcept {
    return Invert(matrix, inverse);
}

DirectionalShadowMatrix MultiplyDirectionalShadowMatrices(const DirectionalShadowMatrix& left,
                                                          const DirectionalShadowMatrix& right) noexcept {
    DirectionalShadowMatrix result{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            for (size_t component = 0; component < 4; ++component) {
                result[column * 4 + row] += left[component * 4 + row] * right[column * 4 + component];
            }
        }
    }
    return result;
}

DirectionalShadowFramePlan BuildDirectionalShadowFramePlan(const DirectionalShadowFrameInput& input) noexcept {
    DirectionalShadowFramePlan plan;
    if (input.Settings.Mode != DirectionalShadowMode::VirtualShadowMaps)
        return plan;
    Vec3 direction{ input.LightDirectionTowardSource[0], input.LightDirectionTowardSource[1],
                    input.LightDirectionTowardSource[2] };
    if (!Normalize(direction)) {
        plan.Status = DirectionalShadowPlanStatus::InvalidLightDirection;
        return plan;
    }
    plan.Virtual = ::Fast::Renderer3ds::BuildVirtualShadowLayout(input.CameraWorld, input.LightDirectionTowardSource,
                                                                 input.Settings.Resolution,
                                                                 input.Settings.MaximumDistance, input.PreviousLayout);
    if (!plan.Virtual.Valid()) {
        plan.Status = DirectionalShadowPlanStatus::InvalidCamera;
        return plan;
    }
    plan.LightDirectionTowardSource = plan.Virtual.LightDirection;
    plan.DepthBiasConstant = input.Settings.DepthBiasConstant;
    plan.DepthBiasSlope = input.Settings.DepthBiasSlope;
    plan.Strength = std::clamp(input.Settings.Strength, 0.0F, 4.0F);
    plan.PcfRadius = std::min<uint8_t>(input.Settings.PcfRadius, 2U);
    plan.NormalBias = std::isfinite(input.Settings.NormalBias) ? std::clamp(input.Settings.NormalBias, 0.0F, 10.0F)
                                                               : 0.0F;
    plan.CullCasters = input.CullCasters;
    plan.ContactShadows = input.Settings.ContactShadows;
    plan.Status = DirectionalShadowPlanStatus::Ready;
    return plan;
}

bool UsesPicaPerspectiveProjection(std::span<const uint8_t> packedVertexUniforms) noexcept {
    constexpr size_t kFloatUniformOffset = 80U;
    constexpr size_t kVec4Bytes = 4U * sizeof(float);
    constexpr uint32_t kHomogeneousProjectionSlot = 3U;
    const size_t offset = kFloatUniformOffset + kHomogeneousProjectionSlot * kVec4Bytes;
    if (offset + kVec4Bytes > packedVertexUniforms.size()) {
        return false;
    }
    std::array<float, 4> homogeneousRow{};
    std::memcpy(homogeneousRow.data(), packedVertexUniforms.data() + offset, kVec4Bytes);
    if (!std::all_of(homogeneousRow.begin(), homogeneousRow.end(), [](float value) { return std::isfinite(value); })) {
        return false;
    }
    return std::abs(homogeneousRow[0]) <= kEpsilon && std::abs(homogeneousRow[1]) <= kEpsilon &&
           std::abs(homogeneousRow[2]) > kEpsilon && std::abs(homogeneousRow[3]) <= kEpsilon;
}

DirectionalShadowVector TransformPicaLightDirectionToWorld(const DirectionalShadowVector& viewDirectionTowardSource,
                                                           const DirectionalShadowVector& eye,
                                                           const DirectionalShadowVector& at) noexcept {
    Vec3 forward{ at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] };
    if (!Normalize(forward)) {
        return {};
    }
    Vec3 side = Cross(forward, { 0.0F, 1.0F, 0.0F });
    if (!Normalize(side)) {
        return {};
    }
    const Vec3 up = Cross(side, forward);
    Vec3 world = Add(Add(Scale(side, viewDirectionTowardSource[0]), Scale(up, viewDirectionTowardSource[1])),
                     Scale(forward, -viewDirectionTowardSource[2]));
    if (!Normalize(world)) {
        return {};
    }
    return { world.X, world.Y, world.Z };
}

DirectionalShadowVector TransformPicaLightDirectionToWorld(const DirectionalShadowVector& viewDirectionTowardSource,
                                                           const DirectionalShadowMatrix& viewToWorld) noexcept {
    const Vec4 transformed = Transform(viewToWorld, { viewDirectionTowardSource[0], viewDirectionTowardSource[1],
                                                      viewDirectionTowardSource[2], 0.0F });
    Vec3 world{ transformed.X, transformed.Y, transformed.Z };
    if (!Normalize(world)) {
        return {};
    }
    return { world.X, world.Y, world.Z };
}

std::string_view DirectionalShadowPlanStatusName(DirectionalShadowPlanStatus status) noexcept {
    switch (status) {
        case DirectionalShadowPlanStatus::Disabled:
            return "disabled";
        case DirectionalShadowPlanStatus::InvalidCamera:
            return "invalid_camera";
        case DirectionalShadowPlanStatus::InvalidLightDirection:
            return "invalid_light_direction";
        case DirectionalShadowPlanStatus::Ready:
            return "ready";
    }
    return "unknown";
}

} // namespace Fast::Oot3d
