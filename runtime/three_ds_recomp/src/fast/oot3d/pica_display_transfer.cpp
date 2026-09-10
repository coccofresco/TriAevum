#include "fast/oot3d/pica_display_transfer.h"

#include <algorithm>
#include <limits>

namespace Fast::Oot3d {
namespace {

void SetError(std::string* error, const char* message) {
    if (error != nullptr)
        *error = message;
}

std::optional<uint32_t> ScaleNativeExtent(uint32_t nativeOutput,
                                          uint32_t sourceExtent,
                                          uint32_t nativeInput) {
    const uint64_t numerator =
        static_cast<uint64_t>(nativeOutput) * sourceExtent;
    const uint64_t scaled =
        (numerator + static_cast<uint64_t>(nativeInput) / 2U) /
        nativeInput;
    if (scaled == 0U || scaled > std::numeric_limits<uint32_t>::max())
        return std::nullopt;
    return static_cast<uint32_t>(scaled);
}

} // namespace

std::optional<PicaDisplayTransferPlan> BuildPicaDisplayTransferPlan(
    const PicaDisplayTransferPlanInput& input, std::string* error) {
    if (input.NativeInputWidth == 0U || input.NativeInputHeight == 0U ||
        input.NativeOutputWidth == 0U || input.NativeOutputHeight == 0U ||
        input.SourceWidth == 0U || input.SourceHeight == 0U) {
        SetError(error, "PICA display transfer has a zero extent");
        return std::nullopt;
    }

    const uint32_t scaling = (input.Flags >> 24U) & 3U;
    if ((input.Flags & 8U) != 0U ||
        (input.Flags & 0x10000U) != 0U || scaling > 2U) {
        SetError(error, "PICA display transfer mode is unsupported");
        return std::nullopt;
    }
    const uint32_t horizontalSamples = scaling != 0U ? 2U : 1U;
    const uint32_t verticalSamples = scaling == 2U ? 2U : 1U;
    // Validate the guest command before host-resolution rounding. A valid box
    // filter over an odd host extent repeats its last texel (shader edge clamp).
    if (static_cast<uint64_t>(input.NativeOutputWidth) * horizontalSamples > input.NativeInputWidth ||
        static_cast<uint64_t>(input.NativeOutputHeight) * verticalSamples > input.NativeInputHeight) {
        SetError(error, "PICA display transfer samples outside its native source image");
        return std::nullopt;
    }

    const auto destinationWidth = ScaleNativeExtent(
        input.NativeOutputWidth, input.SourceWidth,
        input.NativeInputWidth);
    const auto destinationHeight = ScaleNativeExtent(
        input.NativeOutputHeight, input.SourceHeight,
        input.NativeInputHeight);
    if (!destinationWidth.has_value() || !destinationHeight.has_value()) {
        SetError(error, "PICA display transfer output extent is invalid");
        return std::nullopt;
    }

    PicaDisplayTransferPlan plan;
    plan.SourceWidth = input.SourceWidth;
    plan.SourceHeight = input.SourceHeight;
    plan.DestinationWidth = *destinationWidth;
    plan.DestinationHeight = *destinationHeight;
    plan.HorizontalSamples = horizontalSamples;
    plan.VerticalSamples = verticalSamples;
    plan.ScalingMode = scaling;

    if (!plan.SamplingFitsSource()) {
        SetError(error,
                 "PICA display transfer samples outside its source image");
        return std::nullopt;
    }
    return plan;
}

std::string BuildPicaDisplayTransferComputeShader() {
    return R"glsl(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D source_image;
layout(set=0,binding=1,rgba8) uniform writeonly image2D destination_image;
layout(set=0,binding=2) uniform sampler source_sampler;
layout(push_constant) uniform TransferState {
    uvec2 source_extent;
    uvec2 destination_extent;
    uvec2 sample_count;
} state;

uvec4 load_color(ivec2 pixel) {
    vec4 value=texelFetch(
        sampler2D(source_image,source_sampler),
        clamp(pixel,ivec2(0),ivec2(state.source_extent)-1),0);
    return uvec4(clamp(value,0.0,1.0)*255.0+0.5);
}

uvec4 average_pair(uvec4 left,uvec4 right) {
    return (left+right)/2u;
}

void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,ivec2(state.destination_extent)))) return;
    ivec2 source_pixel=pixel*ivec2(state.sample_count);
    uvec4 color=load_color(source_pixel);
    if(state.sample_count.x==2u) {
        color=average_pair(color,load_color(source_pixel+ivec2(1,0)));
    }
    if(state.sample_count.y==2u) {
        uvec4 lower=load_color(source_pixel+ivec2(0,1));
        if(state.sample_count.x==2u) {
            lower=average_pair(
                lower,load_color(source_pixel+ivec2(1,1)));
        }
        color=average_pair(color,lower);
    }
    imageStore(destination_image,pixel,vec4(color)/255.0);
}
)glsl";
}

} // namespace Fast::Oot3d
