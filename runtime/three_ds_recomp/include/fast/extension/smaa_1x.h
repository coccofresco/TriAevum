#pragma once

#include <string>

namespace Fast::Extension {

enum class Smaa1xStage {
    EdgeDetection,
    BlendWeightCalculation,
    NeighborhoodBlending,
};

[[nodiscard]] std::string BuildSmaa1xComputeShader(
    Smaa1xStage stage);

} // namespace Fast::Extension
