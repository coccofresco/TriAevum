#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Oot3dNativeGame {

struct NativeAudioResamplerProfile {
    static constexpr size_t PhaseCount = 128;
    static constexpr size_t TapCount = 4;

    uint32_t DspComponentOffset = 0;
    uint32_t DspComponentSize = 0;
    uint32_t PolyphaseTableOffset = 0;
    uint32_t PolyphaseTableWordAddress = 0;
    std::array<std::array<int16_t, TapCount>, PhaseCount>
        PolyphaseCoefficients{};
};

NativeAudioResamplerProfile ParseNativeAudioResamplerProfile(
    std::span<const uint8_t> codeBin);

} // namespace Oot3dNativeGame
