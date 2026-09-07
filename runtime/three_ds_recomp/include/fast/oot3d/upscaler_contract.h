#pragma once

#include <cstdint>
#include <compare>

namespace Fast::Oot3d {

enum class UpscalerProvider : uint8_t { Nis, Fsr, Xess, Dlss };
enum class UpscalerQuality : uint8_t {
    Native,
    UltraQuality,
    Quality,
    Balanced,
    Performance,
    UltraPerformance,
};

struct UpscalerExtent {
    uint32_t Width = 0;
    uint32_t Height = 0;
    auto operator<=>(const UpscalerExtent&) const = default;
};

struct UpscalerContract {
    UpscalerProvider Provider = UpscalerProvider::Nis;
    UpscalerQuality Quality = UpscalerQuality::Quality;
    UpscalerExtent Output{};
    UpscalerExtent Render{};
    float ScalingFactor = 1.0F;
    uint32_t MinimumJitterPhases = 8;
    bool Temporal = false;
};

[[nodiscard]] float UpscalerScalingFactor(UpscalerQuality quality);
[[nodiscard]] UpscalerContract ResolveUpscalerContract(
    UpscalerProvider provider, UpscalerQuality quality,
    uint32_t outputWidth, uint32_t outputHeight);
[[nodiscard]] UpscalerContract ResolveUpscalerContractFromRender(
    UpscalerProvider provider, UpscalerQuality quality,
    uint32_t renderWidth, uint32_t renderHeight);
[[nodiscard]] bool IsTemporalUpscaler(UpscalerProvider provider);

} // namespace Fast::Oot3d
