#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Fast::Oot3d {

enum class PicaAmbientOcclusionGuideEligibility : uint8_t {
    AppliedExact,
    AppliedFallback,
    Disabled,
    UnsupportedShader,
};

struct PicaAmbientOcclusionGuideVariant {
    std::string Source;
    uint64_t FragmentKey = 0U;
    PicaAmbientOcclusionGuideEligibility Eligibility =
        PicaAmbientOcclusionGuideEligibility::Disabled;

    [[nodiscard]] bool Applied() const noexcept {
        return Eligibility ==
                   PicaAmbientOcclusionGuideEligibility::AppliedExact ||
               Eligibility ==
                   PicaAmbientOcclusionGuideEligibility::AppliedFallback;
    }
    [[nodiscard]] bool Exact() const noexcept {
        return Eligibility ==
            PicaAmbientOcclusionGuideEligibility::AppliedExact;
    }
};

// Adds the dedicated location-4 scene-domain guide. RGB retains the native
// material ambient response for diagnostics; alpha is one for every scene
// fragment and is later attenuated by top-screen overlays.
[[nodiscard]] PicaAmbientOcclusionGuideVariant
BuildPicaAmbientOcclusionGuideVariant(
    std::string_view source,
    uint64_t originalFragmentKey,
    bool enabled);

} // namespace Fast::Oot3d
