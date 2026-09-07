#pragma once

#include <algorithm>
#include <cmath>

#include "oot3d_demo_host_types.h"

extern "C" {
#include "oot3d/scene_cutscene_camera_runtime.h"
}

constexpr double kOot3dDemoPi = 3.14159265358979323846;

inline Vec3 ToVec3(const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& value) {
    return { value.X, value.Y, value.Z };
}

inline Vec3 ToVec3(const Oot3dCutsceneCameraVec3f& value) {
    return { value.x, value.y, value.z };
}

inline ThreeDsRecomp::Oot3d::Oot3dDemoVec3 TransformDemoPoint(
    const ThreeDsRecomp::Oot3d::Matrix4f& transform,
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 point) {
    const double x = transform.M[0][0] * point.X + transform.M[0][1] * point.Y +
                     transform.M[0][2] * point.Z + transform.M[0][3];
    const double y = transform.M[1][0] * point.X + transform.M[1][1] * point.Y +
                     transform.M[1][2] * point.Z + transform.M[1][3];
    const double z = transform.M[2][0] * point.X + transform.M[2][1] * point.Y +
                     transform.M[2][2] * point.Z + transform.M[2][3];
    const double w = transform.M[3][0] * point.X + transform.M[3][1] * point.Y +
                     transform.M[3][2] * point.Z + transform.M[3][3];
    const double invW = std::abs(w) > 0.000001 ? 1.0 / w : 1.0;
    return { x * invW, y * invW, z * invW };
}

inline ThreeDsRecomp::Oot3d::Matrix4f IdentityMatrix() {
    ThreeDsRecomp::Oot3d::Matrix4f matrix{};
    for (size_t i = 0; i < 4; ++i) {
        matrix.M[i][i] = 1.0f;
    }
    return matrix;
}

inline ThreeDsRecomp::Oot3d::Matrix4f MultiplyMatrix(const ThreeDsRecomp::Oot3d::Matrix4f& left,
                                            const ThreeDsRecomp::Oot3d::Matrix4f& right) {
    ThreeDsRecomp::Oot3d::Matrix4f out{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            float value = 0.0f;
            for (size_t k = 0; k < 4; ++k) {
                value += left.M[row][k] * right.M[k][col];
            }
            out.M[row][col] = value;
        }
    }
    return out;
}

inline bool InvertMatrix(const ThreeDsRecomp::Oot3d::Matrix4f& matrix, ThreeDsRecomp::Oot3d::Matrix4f& inverse) {
    double augmented[4][8]{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            augmented[row][column] = matrix.M[row][column];
        }
        augmented[row][row + 4] = 1.0;
    }

    for (size_t column = 0; column < 4; ++column) {
        size_t pivot = column;
        double pivotAbs = std::abs(augmented[pivot][column]);
        for (size_t row = column + 1; row < 4; ++row) {
            const double valueAbs = std::abs(augmented[row][column]);
            if (valueAbs > pivotAbs) {
                pivot = row;
                pivotAbs = valueAbs;
            }
        }
        if (pivotAbs <= 0.000000001) {
            return false;
        }
        if (pivot != column) {
            for (size_t k = 0; k < 8; ++k) {
                std::swap(augmented[column][k], augmented[pivot][k]);
            }
        }

        const double divisor = augmented[column][column];
        for (size_t k = 0; k < 8; ++k) {
            augmented[column][k] /= divisor;
        }

        for (size_t row = 0; row < 4; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = augmented[row][column];
            for (size_t k = 0; k < 8; ++k) {
                augmented[row][k] -= factor * augmented[column][k];
            }
        }
    }

    inverse = {};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            inverse.M[row][column] = static_cast<float>(augmented[row][column + 4]);
        }
    }
    return true;
}

inline ThreeDsRecomp::Oot3d::Matrix4f PerspectiveMatrix(double fovYRadians, double aspect, double zNear, double zFar) {
    ThreeDsRecomp::Oot3d::Matrix4f matrix{};
    const double f = 1.0 / std::tan(fovYRadians * 0.5);
    matrix.M[0][0] = static_cast<float>(f / aspect);
    matrix.M[1][1] = static_cast<float>(f);
    matrix.M[2][2] = static_cast<float>((zFar + zNear) / (zNear - zFar));
    matrix.M[2][3] = static_cast<float>((2.0 * zFar * zNear) / (zNear - zFar));
    matrix.M[3][2] = -1.0f;
    return matrix;
}

inline Vec3 Add(const Vec3& left, const Vec3& right) {
    return { left.X + right.X, left.Y + right.Y, left.Z + right.Z };
}

inline Vec3 Subtract(const Vec3& left, const Vec3& right) {
    return { left.X - right.X, left.Y - right.Y, left.Z - right.Z };
}

inline Vec3 Cross(const Vec3& left, const Vec3& right) {
    return {
        left.Y * right.Z - left.Z * right.Y,
        left.Z * right.X - left.X * right.Z,
        left.X * right.Y - left.Y * right.X,
    };
}

inline double Dot(const Vec3& left, const Vec3& right) {
    return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
}

inline Vec3 Normalize(const Vec3& value) {
    const double length = std::sqrt(Dot(value, value));
    if (length <= 0.000001) {
        return {};
    }
    return { value.X / length, value.Y / length, value.Z / length };
}

inline Vec3 NativeCutsceneCameraUpForDemoView(const Vec3& nativeUp) {
    return nativeUp;
}

inline double DegreesToRadians(double degrees) {
    return degrees * kOot3dDemoPi / 180.0;
}

inline double NormalizeAngleRadians(double angle) {
    while (angle <= -kOot3dDemoPi) {
        angle += kOot3dDemoPi * 2.0;
    }
    while (angle > kOot3dDemoPi) {
        angle -= kOot3dDemoPi * 2.0;
    }
    return angle;
}

inline double MoveAngleToward(double current, double target, double maxDelta) {
    const double delta = NormalizeAngleRadians(target - current);
    if (std::abs(delta) <= maxDelta) {
        return NormalizeAngleRadians(target);
    }
    return NormalizeAngleRadians(current + (delta > 0.0 ? maxDelta : -maxDelta));
}

inline double AsymStepToward(double current, double target, double incrementStep, double decrementStep) {
    const double step = target >= current ? std::max(0.0, incrementStep) : std::max(0.0, decrementStep);
    if (std::abs(target - current) <= step) {
        return target;
    }
    return current + (target > current ? step : -step);
}

inline double YawFromDirection(const Vec3& direction) {
    return std::atan2(direction.X, direction.Z);
}

inline Vec3 DirectionFromYaw(double yaw) {
    return { std::sin(yaw), 0.0, std::cos(yaw) };
}

inline ThreeDsRecomp::Oot3d::Matrix4f LookAtMatrix(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 forward = Normalize(Subtract(target, eye));
    const Vec3 side = Normalize(Cross(forward, up));
    const Vec3 cameraUp = Cross(side, forward);

    ThreeDsRecomp::Oot3d::Matrix4f view = IdentityMatrix();
    view.M[0][0] = static_cast<float>(side.X);
    view.M[0][1] = static_cast<float>(side.Y);
    view.M[0][2] = static_cast<float>(side.Z);
    view.M[0][3] = static_cast<float>(-Dot(side, eye));
    view.M[1][0] = static_cast<float>(cameraUp.X);
    view.M[1][1] = static_cast<float>(cameraUp.Y);
    view.M[1][2] = static_cast<float>(cameraUp.Z);
    view.M[1][3] = static_cast<float>(-Dot(cameraUp, eye));
    view.M[2][0] = static_cast<float>(-forward.X);
    view.M[2][1] = static_cast<float>(-forward.Y);
    view.M[2][2] = static_cast<float>(-forward.Z);
    view.M[2][3] = static_cast<float>(Dot(forward, eye));
    return view;
}

inline Vec3 CameraForward(const Camera& camera) {
    const double cp = std::cos(camera.Pitch);
    return { std::sin(camera.Yaw) * cp, std::sin(camera.Pitch), -std::cos(camera.Yaw) * cp };
}

inline Vec3 CameraRight(const Camera& camera) {
    return { std::cos(camera.Yaw), 0.0, std::sin(camera.Yaw) };
}

inline Vec3 CameraPlanarForward(const Camera& camera) {
    return Normalize({ std::sin(camera.Yaw), 0.0, -std::cos(camera.Yaw) });
}

inline Vec3 NativeCameraInputForward(const Camera& camera) {
    return DirectionFromYaw(camera.NativeInputYaw);
}

inline Vec3 NativeCameraInputRight(const Camera& camera) {
    return DirectionFromYaw(camera.NativeInputYaw - (kOot3dDemoPi * 0.5));
}

inline Vec3 Scale(const Vec3& value, double scale) {
    return { value.X * scale, value.Y * scale, value.Z * scale };
}

inline void AddScaled(Vec3& value, const Vec3& delta, double scale) {
    value.X += delta.X * scale;
    value.Y += delta.Y * scale;
    value.Z += delta.Z * scale;
}

inline void AddScaled(ThreeDsRecomp::Oot3d::Oot3dDemoVec3& value, const Vec3& delta, double scale) {
    value.X += delta.X * scale;
    value.Y += delta.Y * scale;
    value.Z += delta.Z * scale;
}

inline void LookAt(Camera& camera, const Vec3& target) {
    const Vec3 delta = Subtract(target, camera.Position);
    const double length = std::sqrt(Dot(delta, delta));
    if (length <= 0.000001) {
        return;
    }
    camera.Target = target;
    camera.Yaw = std::atan2(delta.X, -delta.Z);
    camera.Pitch = std::asin(std::clamp(delta.Y / length, -1.0, 1.0));
    camera.NativeInputYaw = NormalizeAngleRadians(std::atan2(delta.X, delta.Z));
    camera.NativeInputPitch = std::atan2(delta.Y, std::sqrt((delta.X * delta.X) + (delta.Z * delta.Z)));
}

inline void SyncNativeCameraInputDirection(Camera& camera) {
    const Vec3 delta = Subtract(camera.Target, camera.Position);
    const double horizontal = std::sqrt((delta.X * delta.X) + (delta.Z * delta.Z));
    if (horizontal > 0.000001) {
        camera.NativeInputYaw = NormalizeAngleRadians(std::atan2(delta.X, delta.Z));
    }
    const double length = std::sqrt(Dot(delta, delta));
    if (length > 0.000001) {
        camera.NativeInputPitch = std::atan2(delta.Y, horizontal);
    }
}

inline double SmoothScaleForTicks(double perTickScale, double dtTicks) {
    const double clamped = std::clamp(perTickScale, 0.0, 1.0);
    if (dtTicks <= 0.0) {
        return 0.0;
    }
    return 1.0 - std::pow(1.0 - clamped, dtTicks);
}

inline double SmoothStepToTarget(double current, double target, double perTickScale, double dtTicks) {
    return current + (target - current) * SmoothScaleForTicks(perTickScale, dtTicks);
}

inline Vec3 SmoothStepToTarget(const Vec3& current, const Vec3& target, double perTickScale, double dtTicks) {
    const double scale = SmoothScaleForTicks(perTickScale, dtTicks);
    return {
        current.X + (target.X - current.X) * scale,
        current.Y + (target.Y - current.Y) * scale,
        current.Z + (target.Z - current.Z) * scale,
    };
}

inline double NativeCameraLerpCeilF(double target, double current, double stepScale, double minDiff, double dtTicks) {
    if (std::abs(target - current) <= minDiff) {
        return target;
    }
    return current + (target - current) * SmoothScaleForTicks(stepScale, dtTicks);
}

inline Vec3 NativeCameraLerpCeilVec3(const Vec3& target, const Vec3& current, double yStepScale,
                                     double xzStepScale, double minDiff, double dtTicks) {
    return {
        NativeCameraLerpCeilF(target.X, current.X, xzStepScale, minDiff, dtTicks),
        NativeCameraLerpCeilF(target.Y, current.Y, yStepScale, minDiff, dtTicks),
        NativeCameraLerpCeilF(target.Z, current.Z, xzStepScale, minDiff, dtTicks),
    };
}

inline double NativeCameraInterpolateCurve(double a, double b) {
    constexpr double kCurveBlend = 0.4;
    const double absB = std::abs(b);
    if (a < absB || a <= 0.000001) {
        return 1.0;
    }

    const double earlySpan = a * (1.0 - kCurveBlend);
    if (earlySpan > absB) {
        return (b * b * (1.0 - kCurveBlend)) / (earlySpan * earlySpan);
    }

    const double lateSpan = kCurveBlend * a;
    return 1.0 - (((a - absB) * (a - absB) * kCurveBlend) / (lateSpan * lateSpan));
}

inline double WrapFrameSpan(double frame, double span) {
    if (span <= 0.0) {
        return 0.0;
    }
    frame = std::fmod(std::max(0.0, frame), span);
    return frame < 0.0 ? frame + span : frame;
}

inline double CsabInclusiveFrameSpan(uint32_t maxFrame) {
    return static_cast<double>(maxFrame) + 1.0;
}

inline double WrapAnimationFrame(double frame, uint32_t maxFrame) {
    return WrapFrameSpan(frame, CsabInclusiveFrameSpan(maxFrame));
}

inline ThreeDsRecomp::Oot3d::Matrix4f BuildViewToClipMatrix(const Camera& camera, double aspect,
                                                   double zNear, double zFar) {
    return PerspectiveMatrix(DegreesToRadians(camera.FovDegrees), std::max(0.1, aspect), zNear, zFar);
}

inline ThreeDsRecomp::Oot3d::Matrix4f BuildWorldToClipMatrix(const Camera& camera, double aspect,
                                                    double zNear, double zFar) {
    const auto projection = BuildViewToClipMatrix(camera, aspect, zNear, zFar);
    const Vec3 up = Dot(camera.Up, camera.Up) > 0.000001 ? Normalize(camera.Up) : Vec3{ 0.0, 1.0, 0.0 };
    const auto view = LookAtMatrix(camera.Position, camera.Target, up);
    return MultiplyMatrix(projection, view);
}

inline ThreeDsRecomp::Oot3d::Matrix4f BuildWorldToClipMatrix(const Camera& camera, double aspect,
                                                    const ThreeDsRecomp::Oot3d::Oot3dDemoBounds& bounds) {
    const double extent = std::max(ThreeDsRecomp::Oot3d::NativeDemoBoundsMaxExtent(bounds), 80.0);
    return BuildWorldToClipMatrix(camera, aspect, std::max(1.0, extent / 800.0),
                                  std::max(2000.0, extent * 10.0));
}
