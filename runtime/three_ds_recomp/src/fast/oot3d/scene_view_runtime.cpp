#include "fast/oot3d/scene_view_runtime.h"

#include <cmath>

namespace Fast::Oot3d {
namespace {

bool ValidPerspective(float left, float right, float bottom, float top, float nearPlane, float farPlane) {
    return std::isfinite(left) && std::isfinite(right) && std::isfinite(bottom) && std::isfinite(top) &&
           std::isfinite(nearPlane) && std::isfinite(farPlane) && left != right && bottom != top && nearPlane > 0.0F &&
           farPlane > nearPlane;
}

bool ValidCamera(const std::array<float, 3>& eye, const std::array<float, 3>& at) {
    for (const float value : eye)
        if (!std::isfinite(value))
            return false;
    for (const float value : at)
        if (!std::isfinite(value))
            return false;
    const float x = at[0] - eye[0];
    const float y = at[1] - eye[1];
    const float z = at[2] - eye[2];
    return x * x + y * y + z * z > 1.0e-12F;
}

PerspectiveViewState BuildPerspective(uint32_t guestFunction, uint32_t guestReturnAddress, float left, float right,
                                      float bottom, float top, float nearPlane, float farPlane) {
    PerspectiveViewState view{};
    view.GuestFunction = guestFunction;
    view.GuestReturnAddress = guestReturnAddress;
    view.Left = left;
    view.Right = right;
    view.Bottom = bottom;
    view.Top = top;
    view.NearPlane = nearPlane;
    view.FarPlane = farPlane;

    // Column-major, right-handed, Vulkan/D3D depth range [0, 1]. Provider
    // adapters own any storage rotation or positive-depth basis conversion.
    view.Projection[0] = 2.0F * nearPlane / (right - left);
    view.Projection[5] = 2.0F * nearPlane / (top - bottom);
    view.Projection[8] = (left + right) / (right - left);
    view.Projection[9] = (top + bottom) / (top - bottom);
    view.Projection[10] = farPlane / (nearPlane - farPlane);
    view.Projection[11] = -1.0F;
    view.Projection[14] = nearPlane * farPlane / (nearPlane - farPlane);
    return view;
}

std::array<float, 3> Normalize(std::array<float, 3> value) {
    const float length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    if (length <= 1.0e-6F)
        return {};
    for (float& component : value)
        component /= length;
    return value;
}

std::array<float, 3> Cross(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

float Dot(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void RebuildWorldToClip(PerspectiveViewState& state) {
    if (!state.CameraAvailable)
        return;
    const auto forward =
        Normalize({ state.At[0] - state.Eye[0], state.At[1] - state.Eye[1], state.At[2] - state.Eye[2] });
    const auto side = Normalize(Cross(forward, { 0.0F, 1.0F, 0.0F }));
    const auto up = Cross(side, forward);
    if (side == std::array<float, 3>{})
        return;

    // Column-major matrices, directly consumable by GLSL mat4.
    std::array<float, 16> view{ side[0],
                                up[0],
                                -forward[0],
                                0.0F,
                                side[1],
                                up[1],
                                -forward[1],
                                0.0F,
                                side[2],
                                up[2],
                                -forward[2],
                                0.0F,
                                -Dot(side, state.Eye),
                                -Dot(up, state.Eye),
                                Dot(forward, state.Eye),
                                1.0F };
    std::array<float, 16> projection{};
    projection[0] = 2.0F * state.NearPlane / (state.Right - state.Left);
    projection[5] = 2.0F * state.NearPlane / (state.Top - state.Bottom);
    projection[8] = (state.Right + state.Left) / (state.Right - state.Left);
    projection[9] = (state.Top + state.Bottom) / (state.Top - state.Bottom);
    projection[10] = state.FarPlane / (state.NearPlane - state.FarPlane);
    projection[11] = -1.0F;
    projection[14] = state.NearPlane * state.FarPlane / (state.NearPlane - state.FarPlane);
    for (size_t column = 0; column < 4; ++column) {
        for (size_t row = 0; row < 4; ++row) {
            float value = 0.0F;
            for (size_t k = 0; k < 4; ++k) {
                value += projection[k * 4 + row] * view[column * 4 + k];
            }
            state.WorldToClip[column * 4 + row] = value;
        }
    }
}

} // namespace

SceneViewRuntime& SceneViewRuntime::Instance() {
    static SceneViewRuntime runtime;
    return runtime;
}

void SceneViewRuntime::PublishPerspective(uint32_t guestFunction, uint32_t guestReturnAddress, float left, float right,
                                          float bottom, float top, float nearPlane, float farPlane) {
    if (!ValidPerspective(left, right, bottom, top, nearPlane, farPlane)) {
        return;
    }

    PerspectiveViewState view =
        BuildPerspective(guestFunction, guestReturnAddress, left, right, bottom, top, nearPlane, farPlane);

    std::scoped_lock lock(mMutex);
    if (mLatest.has_value() && mLatest->CameraAvailable) {
        view.Eye = mLatest->Eye;
        view.At = mLatest->At;
        view.CameraAvailable = true;
        RebuildWorldToClip(view);
    }
    view.Serial = mNextSerial++;
    mLatest = view;
}

void SceneViewRuntime::PublishPerspectiveCamera(uint32_t guestFunction, uint32_t guestReturnAddress, float left,
                                                float right, float bottom, float top, float nearPlane, float farPlane,
                                                const std::array<float, 3>& eye, const std::array<float, 3>& at) {
    if (!ValidPerspective(left, right, bottom, top, nearPlane, farPlane) || !ValidCamera(eye, at)) {
        return;
    }
    PerspectiveViewState view =
        BuildPerspective(guestFunction, guestReturnAddress, left, right, bottom, top, nearPlane, farPlane);
    view.Eye = eye;
    view.At = at;
    view.CameraAvailable = true;
    RebuildWorldToClip(view);

    std::scoped_lock lock(mMutex);
    view.Serial = mNextSerial++;
    mLatest = view;
}

void SceneViewRuntime::PublishCamera(const std::array<float, 3>& eye, const std::array<float, 3>& at) {
    if (!ValidCamera(eye, at))
        return;
    std::scoped_lock lock(mMutex);
    if (!mLatest.has_value())
        mLatest.emplace();
    mLatest->Eye = eye;
    mLatest->At = at;
    mLatest->CameraAvailable = true;
    if (mLatest->NearPlane > 0.0F && mLatest->FarPlane > mLatest->NearPlane && mLatest->Left != mLatest->Right &&
        mLatest->Bottom != mLatest->Top) {
        RebuildWorldToClip(*mLatest);
    }
    mLatest->Serial = mNextSerial++;
}

std::optional<PerspectiveViewState> SceneViewRuntime::LatestPerspective() const {
    std::scoped_lock lock(mMutex);
    return mLatest;
}

void SceneViewRuntime::Reset() {
    std::scoped_lock lock(mMutex);
    mLatest.reset();
}

} // namespace Fast::Oot3d
