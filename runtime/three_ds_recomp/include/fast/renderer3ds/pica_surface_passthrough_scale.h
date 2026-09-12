#pragma once

#include <cstdint>

namespace Fast::Renderer3ds {

// Only for the validated texture * primary + RGB pass-through capability.
// After the first byte round, power-of-two gains preserve the byte lattice;
// subsequent rounds are identities and saturation can move to the final gain.
constexpr uint32_t PicaSurfacePassthroughScale(uint32_t packedScales) {
    uint32_t exponent = 0;
    for (uint32_t stage = 0; stage < 6; ++stage)
        exponent += (packedScales >> (stage * 2)) & 3U;
    return 1U << exponent;
}

} // namespace Fast::Renderer3ds
