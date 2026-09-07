#pragma once

#include <cstdint>
#include <vector>

namespace Fast::Oot3d {

struct SmaaLookupData {
    static constexpr uint32_t AreaWidth = 160U;
    static constexpr uint32_t AreaHeight = 560U;
    static constexpr uint32_t SearchWidth = 64U;
    static constexpr uint32_t SearchHeight = 16U;

    std::vector<uint8_t> AreaRgba8;
    std::vector<uint8_t> SearchRgba8;

    [[nodiscard]] bool Valid() const noexcept;
};

// Decodes the official SMAA 2.8 precomputed lookups embedded in the library.
// Failure is contained: an empty result capability-gates SMAA without
// affecting FXAA, MSAA, TAA or Authentic.
[[nodiscard]] SmaaLookupData DecodeSmaaLookupData() noexcept;

} // namespace Fast::Oot3d
