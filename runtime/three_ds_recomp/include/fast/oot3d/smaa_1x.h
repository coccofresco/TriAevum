#pragma once

#include <string>

namespace Fast::Oot3d {

enum class Smaa1xStage {
    EdgeDetection,
    BlendWeightCalculation,
    NeighborhoodBlending,
};

[[nodiscard]] std::string BuildSmaa1xComputeShader(
    Smaa1xStage stage);

} // namespace Fast::Oot3d
