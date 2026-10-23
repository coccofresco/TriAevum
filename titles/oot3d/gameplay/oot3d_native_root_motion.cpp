#include "oot3d_native_root_motion.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {

float SinBinaryAngle(int16_t angle) {
    return std::sin(static_cast<float>(angle) * std::numbers::pi_v<float> / 32768.0f);
}

float CosBinaryAngle(int16_t angle) {
    return std::cos(static_cast<float>(angle) * std::numbers::pi_v<float> / 32768.0f);
}

Vec3f RotateRootTranslation(float x, float z, int16_t yaw) {
    const float sin = SinBinaryAngle(yaw);
    const float cos = CosBinaryAngle(yaw);
    return { x * cos + z * sin, 0.0f, z * cos - x * sin };
}

} // namespace

Vec3f ExtractRootMotion(RootMotionState& state, std::span<AnimTransform> jointMatrices,
                        int16_t actorYaw) {
    if (state.SpecialLimb >= jointMatrices.size()) {
        throw std::out_of_range("OOT3D root-motion special limb is outside the joint table");
    }

    auto& root = jointMatrices[state.SpecialLimb];
    const Vec3f current = { root.Rows[0][3], root.Rows[1][3], root.Rows[2][3] };
    const bool suppressMovement = (state.MovementFlags & kRootMotionNoMoveOnce) != 0;

    Vec3f delta;
    if (!suppressMovement) {
        const Vec3f currentWorld = RotateRootTranslation(current.X, current.Z, actorYaw);
        const Vec3f previousWorld =
            RotateRootTranslation(state.PreviousTranslation.X, state.PreviousTranslation.Z,
                                  state.PreviousRotation);
        delta.X = currentWorld.X - previousWorld.X;
        delta.Z = currentWorld.Z - previousWorld.Z;
    }

    state.PreviousRotation = actorYaw;
    state.PreviousTranslation.X = current.X;
    state.PreviousTranslation.Z = current.Z;
    root.Rows[0][3] = state.BaseTranslation.X;
    root.Rows[2][3] = state.BaseTranslation.Z;

    if ((state.MovementFlags & kRootMotionUpdateY) == 0) {
        state.PreviousTranslation.Y = current.Y;
    } else {
        if (!suppressMovement) {
            delta.Y = current.Y - state.PreviousTranslation.Y;
        }
        state.PreviousTranslation.Y = current.Y;
        root.Rows[1][3] = state.BaseTranslation.Y;
    }

    state.MovementFlags &= static_cast<uint8_t>(~kRootMotionNoMoveOnce);
    return delta;
}

Vec3f ApplyRootMotionToActor(RootMotionState& state, std::span<AnimTransform> jointMatrices,
                             int16_t actorYaw, float verticalMovementScale,
                             ActorTransform& actor) {
    const Vec3f delta = ExtractRootMotion(state, jointMatrices, actorYaw);
    actor.Position.X += delta.X * actor.Scale.X;
    actor.Position.Y += delta.Y * actor.Scale.Y * verticalMovementScale;
    actor.Position.Z += delta.Z * actor.Scale.Z;
    return delta;
}

} // namespace Oot3dNativeGame
