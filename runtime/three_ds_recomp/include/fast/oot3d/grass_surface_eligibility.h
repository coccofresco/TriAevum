#pragma once

#include <string_view>

namespace Fast::Oot3d {

struct GrassSurfaceDrawInfo {
    bool ColorOperation = false;
    bool WorldSurface = false;
    bool HasTexture0 = false;
    bool FragmentUsesTexture0 = false;
    bool HasPosition = false;
    bool HasTexCoord0 = false;
    bool DepthTest = false;
    bool DepthWrite = false;
    bool DepthCompareAlways = true;
    bool PerspectiveProjection = false;
    bool WritesRgb = false;
    bool TranslucentBlend = false;
    bool SupportedTopology = false;
};

[[nodiscard]] constexpr bool IsGrassSurfaceDrawEligible(
    const GrassSurfaceDrawInfo& draw) noexcept {
    return draw.ColorOperation && draw.WorldSurface &&
           draw.HasTexture0 && draw.FragmentUsesTexture0 &&
           draw.HasPosition && draw.HasTexCoord0 &&
           draw.DepthTest && draw.DepthWrite &&
           !draw.DepthCompareAlways &&
           draw.PerspectiveProjection && draw.WritesRgb &&
           !draw.TranslucentBlend && draw.SupportedTopology;
}

[[nodiscard]] inline bool PicaFragmentSamplesGrassTexture0(
    std::string_view source) noexcept {
    const auto main = source.find("void main");
    const auto body = main == std::string_view::npos
                          ? source
                          : source.substr(main);
    return body.find("texture(pica_texture0") != std::string_view::npos ||
           body.find("textureProj(pica_texture0") != std::string_view::npos ||
           body.find("textureLod(pica_texture0") != std::string_view::npos ||
           body.find("textureOffset(pica_texture0") != std::string_view::npos ||
           body.find("texelFetch(pica_texture0") != std::string_view::npos ||
           body.find("pica_sample_texture0(") != std::string_view::npos;
}

} // namespace Fast::Oot3d
