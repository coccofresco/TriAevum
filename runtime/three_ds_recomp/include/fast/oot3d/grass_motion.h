#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Fast::Oot3d {

[[nodiscard]] bool GrassMotionHistoryMatches(
    uint64_t previousFrame, uint64_t currentFrame,
    size_t previousVertexCount, size_t currentVertexCount);

[[nodiscard]] std::array<float, 2> ComputeGrassMotionUv(
    const std::array<float, 4>& currentClip,
    const std::array<float, 4>& previousClip, bool historyValid);

} // namespace Fast::Oot3d
