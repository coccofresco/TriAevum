#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Oot3dSourcePlayerUpdate {

struct PlayerUpdateLiterals {
    std::array<std::uint32_t, 13> Words{};
};

inline constexpr std::array<std::uint32_t, 13>
    kLiteralCellAddresses{
        0x001E1DC8U,
        0x001E1DCCU,
        0x001E1DD0U,
        0x001E1DD4U,
        0x001E1DD8U,
        0x001E1DDCU,
        0x001E1DE0U,
        0x001E1DE4U,
        0x001E1DE8U,
        0x001E1DECU,
        0x001E1DF0U,
        0x001E1DF4U,
        0x001E1DF8U,
    };

const PlayerUpdateLiterals& ActiveLiterals();
void ReadGuestMemory(
    std::uint32_t address, void* destination, std::size_t size);
void WriteGuestMemory(
    std::uint32_t address, const void* source, std::size_t size);

template <typename T>
T ReadGuestPod(std::uint32_t address) {
    static_assert(std::is_trivially_copyable_v<T>);
    T value{};
    ReadGuestMemory(address, &value, sizeof(value));
    return value;
}

template <typename T>
void WriteGuestPod(std::uint32_t address, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    WriteGuestMemory(address, &value, sizeof(value));
}

std::int32_t Object_GetIndex(
    std::uint32_t objectContext, std::uint32_t objectId);
std::int32_t oot3d_static_init_guard_acquire(
    std::uint32_t guardAddress);
float oot3d_cos_idx8(std::int32_t angle);
float oot3d_sin_idx8(std::int32_t angle);
std::uint32_t Actor_Spawn(
    std::uint32_t actorContext, std::uint32_t play,
    std::int32_t actorId, float x, float y, float z,
    std::int32_t rotX, std::int32_t rotY, std::int32_t rotZ,
    std::int32_t params, std::int32_t initializeNow);
void FUN_0036b02c(std::uint32_t play, std::uint32_t player);
void Player_UpdateCommon(
    std::uint32_t player, std::uint32_t play,
    const std::array<std::uint32_t, 12>& input);

float TargetAdd(float left, float right);
float TargetMultiply(float left, float right);
float TargetMultiplyAccumulate(
    float accumulator, float left, float right);
float TargetMultiplySubtract(
    float accumulator, float left, float right);
std::int32_t TargetFloatToSigned(float value);

void Player_Update(std::uint32_t player, std::uint32_t play);

} // namespace Oot3dSourcePlayerUpdate
