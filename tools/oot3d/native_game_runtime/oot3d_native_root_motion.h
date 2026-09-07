#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Oot3dNativeGame {

struct Vec3f {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

// Native OOT3D animation transform decoded from code.bin@002BD9EC.
struct AnimTransform {
    std::array<std::array<float, 4>, 3> Rows{};
    uint32_t TrailingWord = 0;
};

static_assert(sizeof(AnimTransform) == 0x34);
static_assert(offsetof(AnimTransform, Rows) == 0x00);
static_assert(offsetof(AnimTransform, TrailingWord) == 0x30);

inline constexpr uint8_t kRootMotionUpdateY = 0x02;
inline constexpr uint8_t kRootMotionNoMoveOnce = 0x10;

struct RootMotionState {
    uint8_t MovementFlags = 0;
    int16_t PreviousRotation = 0;
    Vec3f PreviousTranslation;
    Vec3f BaseTranslation;
    uint8_t SpecialLimb = 0;
};

struct ActorTransform {
    Vec3f Position;
    Vec3f Scale = { 1.0f, 1.0f, 1.0f };
};

Vec3f ExtractRootMotion(RootMotionState& state, std::span<AnimTransform> jointMatrices,
                        int16_t actorYaw);

Vec3f ApplyRootMotionToActor(RootMotionState& state, std::span<AnimTransform> jointMatrices,
                             int16_t actorYaw, float verticalMovementScale,
                             ActorTransform& actor);

} // namespace Oot3dNativeGame
