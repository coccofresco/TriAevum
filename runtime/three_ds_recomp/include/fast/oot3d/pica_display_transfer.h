#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Fast::Oot3d {

struct PicaDisplayTransferPlanInput {
    uint32_t NativeInputWidth = 0;
    uint32_t NativeInputHeight = 0;
    uint32_t NativeOutputWidth = 0;
    uint32_t NativeOutputHeight = 0;
    uint32_t SourceWidth = 0;
    uint32_t SourceHeight = 0;
    uint32_t Flags = 0;
};

struct PicaDisplayTransferPlan {
    uint32_t SourceWidth = 0;
    uint32_t SourceHeight = 0;
    uint32_t DestinationWidth = 0;
    uint32_t DestinationHeight = 0;
    uint32_t HorizontalSamples = 1;
    uint32_t VerticalSamples = 1;
    uint32_t ScalingMode = 0;
};

// Builds the GPU-image equivalent of the native PICA display transfer. The
// output dimensions preserve renderer supersampling while crop and 2:1 box
// filtering continue to operate in native PICA coordinates.
[[nodiscard]] std::optional<PicaDisplayTransferPlan>
BuildPicaDisplayTransferPlan(const PicaDisplayTransferPlanInput& input,
                             std::string* error = nullptr);

[[nodiscard]] std::string BuildPicaDisplayTransferComputeShader();

} // namespace Fast::Oot3d
