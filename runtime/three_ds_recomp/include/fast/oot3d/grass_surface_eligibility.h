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

// The first requirement an ineligible draw misses (the placement audit's
// reason), in the order above; nullptr when the draw is eligible.
[[nodiscard]] constexpr const char* GrassSurfaceIneligibility(
    const GrassSurfaceDrawInfo& draw) noexcept {
    if (!draw.ColorOperation) return "fragment_operation";
    if (!draw.WorldSurface) return "not_world_surface";
    if (!draw.HasTexture0) return "no_texture0";
    if (!draw.FragmentUsesTexture0) return "texture0_unsampled";
    if (!draw.HasPosition) return "no_position";
    if (!draw.HasTexCoord0) return "no_texcoord0";
    if (!draw.DepthTest) return "depth_test_off";
    if (!draw.DepthWrite) return "depth_write_off";
    if (draw.DepthCompareAlways) return "depth_compare_always";
    if (!draw.PerspectiveProjection) return "not_perspective";
    if (!draw.WritesRgb) return "no_rgb_write";
    if (draw.TranslucentBlend) return "translucent_blend";
    if (!draw.SupportedTopology) return "unsupported_topology";
    return nullptr;
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
