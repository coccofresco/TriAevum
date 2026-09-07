#pragma once

#include <array>
#include <cstdint>

namespace Fast::Oot3d {

enum class CacaoQuality : uint8_t { Low, Medium };

enum class CacaoNormalSource : uint8_t {
    ReconstructedFromDepth = 0,
    PicaViewSpaceGuide = 1,
};

struct CacaoNormalInputDecision {
    CacaoNormalSource Source = CacaoNormalSource::ReconstructedFromDepth;

    [[nodiscard]] bool UsesPicaGuide() const noexcept {
        return Source == CacaoNormalSource::PicaViewSpaceGuide;
    }
};

[[nodiscard]] CacaoNormalInputDecision ResolveCacaoNormalInput(
    CacaoQuality quality, bool picaNormalGuideAvailable) noexcept;

// CACAO reconstructs positive view depth regardless of the source projection's
// handedness. Convert the shared view-space guide at the provider boundary.
[[nodiscard]] std::array<float, 16>
CacaoViewSpaceNormalTransform(const std::array<float, 16>& projection) noexcept;

} // namespace Fast::Oot3d
