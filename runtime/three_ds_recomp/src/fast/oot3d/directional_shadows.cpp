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

Vec3 Subtract(Vec3 left, Vec3 right) {
    return { left.X - right.X, left.Y - right.Y, left.Z - right.Z };
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

bool Unproject(const DirectionalShadowMatrix& clipToWorld, float x, float y, float z, Vec3& result) {
    const Vec4 homogeneous = Transform(clipToWorld, { x, y, z, 1.0F });
    if (!std::isfinite(homogeneous.W) || std::abs(homogeneous.W) <= kEpsilon) {
        return false;
    }
    const float inverseW = 1.0F / homogeneous.W;
    result = {
        homogeneous.X * inverseW,
        homogeneous.Y * inverseW,
        homogeneous.Z * inverseW,
    };
    return std::isfinite(result.X) && std::isfinite(result.Y) && std::isfinite(result.Z);
}

DirectionalShadowMatrix BuildLightView(Vec3 eye, Vec3 center, Vec3 lightDirection) {
    Vec3 forward = Subtract(center, eye);
    Normalize(forward);
    Vec3 up = std::abs(Dot(lightDirection, { 0.0F, 1.0F, 0.0F })) < 0.98F ? Vec3{ 0.0F, 1.0F, 0.0F }
                                                                          : Vec3{ 0.0F, 0.0F, 1.0F };
    Vec3 side = Cross(forward, up);
    Normalize(side);
    up = Cross(side, forward);

    return {
        side.X, up.X, -forward.X, 0.0F, side.Y,          up.Y,          -forward.Y,        0.0F,
        side.Z, up.Z, -forward.Z, 0.0F, -Dot(side, eye), -Dot(up, eye), Dot(forward, eye), 1.0F,
    };
}

DirectionalShadowMatrix BuildOrthographic(float minimumX, float maximumX, float minimumY, float maximumY,
                                          float minimumZ, float maximumZ) {
    const float inverseX = 1.0F / (maximumX - minimumX);
    const float inverseY = 1.0F / (maximumY - minimumY);
    const float inverseZ = 1.0F / (maximumZ - minimumZ);
    return {
        2.0F * inverseX,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        2.0F * inverseY,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        -inverseZ,
        0.0F,
        -(maximumX + minimumX) * inverseX,
        -(maximumY + minimumY) * inverseY,
        maximumZ * inverseZ,
        1.0F,
    };
}

} // namespace

DirectionalShadowMatrix IdentityDirectionalShadowMatrix() noexcept {
    return {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
}

bool InvertDirectionalShadowMatrix(
    const DirectionalShadowMatrix& matrix,
    DirectionalShadowMatrix& inverse) noexcept {
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
    if (input.Settings.Mode != DirectionalShadowMode::SingleCascade) {
        return plan;
    }

    const DirectionalShadowMatrix& clipToWorld = input.ClipToWorld;

    Vec3 lightDirection{
        input.LightDirectionTowardSource[0],
        input.LightDirectionTowardSource[1],
        input.LightDirectionTowardSource[2],
    };
    if (!Normalize(lightDirection)) {
        plan.Status = DirectionalShadowPlanStatus::InvalidLightDirection;
        return plan;
    }

    std::array<Vec3, 8> corners{};
    size_t cornerIndex = 0;
    const float maximumDistance = std::max(input.Settings.MaximumDistance, 1.0F);
    for (float y : { -1.0F, 1.0F }) {
        for (float x : { -1.0F, 1.0F }) {
            Vec3 nearCorner{};
            Vec3 farCorner{};
            if (!Unproject(clipToWorld, x, y, 0.0F, nearCorner) || !Unproject(clipToWorld, x, y, 1.0F, farCorner)) {
                plan.Status = DirectionalShadowPlanStatus::InvalidProjection;
                return plan;
            }
            Vec3 ray = Subtract(farCorner, nearCorner);
            const float rayLength = Length(ray);
            if (!std::isfinite(rayLength) || rayLength <= kEpsilon) {
                plan.Status = DirectionalShadowPlanStatus::DegenerateFrustum;
                return plan;
            }
            if (rayLength > maximumDistance) {
                ray = Scale(ray, maximumDistance / rayLength);
                farCorner = Add(nearCorner, ray);
            }
            corners[cornerIndex++] = nearCorner;
            corners[cornerIndex++] = farCorner;
        }
    }

    Vec3 center{};
    for (const Vec3 corner : corners) {
        center = Add(center, corner);
    }
    center = Scale(center, 1.0F / static_cast<float>(corners.size()));

    float radius = 0.0F;
    for (const Vec3 corner : corners) {
        radius = std::max(radius, Length(Subtract(corner, center)));
    }
    if (!std::isfinite(radius) || radius <= kEpsilon) {
        plan.Status = DirectionalShadowPlanStatus::DegenerateFrustum;
        return plan;
    }

    const float depthPadding = std::max(input.Settings.DepthPadding, 0.0F);
    const Vec3 eye = Add(center, Scale(lightDirection, radius + depthPadding + 1.0F));
    const DirectionalShadowMatrix lightView = BuildLightView(eye, center, lightDirection);

    float minimumX = std::numeric_limits<float>::infinity();
    float minimumY = std::numeric_limits<float>::infinity();
    float minimumZ = std::numeric_limits<float>::infinity();
    float maximumX = -std::numeric_limits<float>::infinity();
    float maximumY = -std::numeric_limits<float>::infinity();
    float maximumZ = -std::numeric_limits<float>::infinity();
    for (const Vec3 corner : corners) {
        const Vec4 lightSpace = Transform(lightView, { corner.X, corner.Y, corner.Z, 1.0F });
        minimumX = std::min(minimumX, lightSpace.X);
        minimumY = std::min(minimumY, lightSpace.Y);
        minimumZ = std::min(minimumZ, lightSpace.Z);
        maximumX = std::max(maximumX, lightSpace.X);
        maximumY = std::max(maximumY, lightSpace.Y);
        maximumZ = std::max(maximumZ, lightSpace.Z);
    }

    float halfExtent = 0.5F * std::max(maximumX - minimumX, maximumY - minimumY);
    if (!std::isfinite(halfExtent) || halfExtent <= kEpsilon || maximumZ - minimumZ <= kEpsilon) {
        plan.Status = DirectionalShadowPlanStatus::DegenerateFrustum;
        return plan;
    }
    halfExtent = std::max(halfExtent, 1.0F);
    float centerX = 0.5F * (minimumX + maximumX);
    float centerY = 0.5F * (minimumY + maximumY);
    const uint32_t resolution = std::max(input.Settings.Resolution, 1U);
    if (input.Settings.Stabilize) {
        const float texelWorldSize = (2.0F * halfExtent) / static_cast<float>(resolution);
        centerX = std::round(centerX / texelWorldSize) * texelWorldSize;
        centerY = std::round(centerY / texelWorldSize) * texelWorldSize;
    }
    minimumX = centerX - halfExtent;
    maximumX = centerX + halfExtent;
    minimumY = centerY - halfExtent;
    maximumY = centerY + halfExtent;
    minimumZ -= depthPadding;
    maximumZ += depthPadding;

    const DirectionalShadowMatrix lightProjection =
        BuildOrthographic(minimumX, maximumX, minimumY, maximumY, minimumZ, maximumZ);
    plan.WorldToLightClip = MultiplyDirectionalShadowMatrices(lightProjection, lightView);
    const DirectionalShadowMatrix clipToTexture = {
        0.5F, 0.0F, 0.0F, 0.0F, 0.0F, 0.5F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.5F, 0.5F, 0.0F, 1.0F,
    };
    plan.WorldToShadowTexture = MultiplyDirectionalShadowMatrices(clipToTexture, plan.WorldToLightClip);
    plan.ClipToWorld = clipToWorld;
    plan.LightDirectionTowardSource = { lightDirection.X, lightDirection.Y, lightDirection.Z };
    plan.Resolution = resolution;
    plan.DepthBiasConstant = input.Settings.DepthBiasConstant;
    plan.DepthBiasSlope = input.Settings.DepthBiasSlope;
    plan.Strength = std::clamp(input.Settings.Strength, 0.0F, 1.0F);
    plan.PcfRadius = std::min<uint8_t>(input.Settings.PcfRadius, 2U);
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
    if (!std::all_of(homogeneousRow.begin(), homogeneousRow.end(),
                     [](float value) { return std::isfinite(value); })) {
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

DirectionalShadowVector TransformPicaLightDirectionToWorld(
    const DirectionalShadowVector& viewDirectionTowardSource,
    const DirectionalShadowMatrix& viewToWorld) noexcept {
    const Vec4 transformed = Transform(
        viewToWorld,
        {viewDirectionTowardSource[0], viewDirectionTowardSource[1],
         viewDirectionTowardSource[2], 0.0F});
    Vec3 world{transformed.X, transformed.Y, transformed.Z};
    if (!Normalize(world)) {
        return {};
    }
    return {world.X, world.Y, world.Z};
}

std::string_view DirectionalShadowPlanStatusName(DirectionalShadowPlanStatus status) noexcept {
    switch (status) {
        case DirectionalShadowPlanStatus::Disabled:
            return "disabled";
        case DirectionalShadowPlanStatus::InvalidProjection:
            return "invalid_projection";
        case DirectionalShadowPlanStatus::InvalidLightDirection:
            return "invalid_light_direction";
        case DirectionalShadowPlanStatus::DegenerateFrustum:
            return "degenerate_frustum";
        case DirectionalShadowPlanStatus::Ready:
            return "ready";
    }
    return "unknown";
}

} // namespace Fast::Oot3d
