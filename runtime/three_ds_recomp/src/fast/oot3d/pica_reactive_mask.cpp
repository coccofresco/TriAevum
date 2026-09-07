#include "fast/oot3d/pica_reactive_mask.h"

namespace Fast::Oot3d {
namespace {

using BlendEquation = ::Oot3d::Renderer::NativeBlendEquation;
using BlendFactor = ::Oot3d::Renderer::NativeBlendFactor;

bool ReadsDestination(BlendFactor factor) noexcept {
    switch (factor) {
        case BlendFactor::DestColor:
        case BlendFactor::OneMinusDestColor:
        case BlendFactor::DestAlpha:
        case BlendFactor::OneMinusDestAlpha:
        case BlendFactor::SourceAlphaSaturate:
            return true;
        default:
            return false;
    }
}

bool UsesSourceAlpha(BlendFactor factor) noexcept {
    return factor == BlendFactor::SourceAlpha ||
           factor == BlendFactor::OneMinusSourceAlpha ||
           factor == BlendFactor::SourceAlphaSaturate;
}

bool UsesSourceColor(BlendFactor factor) noexcept {
    return factor == BlendFactor::SourceColor ||
           factor == BlendFactor::OneMinusSourceColor;
}

} // namespace

PicaReactiveMaskPlan ResolvePicaReactiveMaskPlan(
    const PicaReactiveDrawInfo& draw) noexcept {
    const auto& blend = draw.Blend;
    if (draw.CompositionDomain !=
            ::Oot3d::Renderer::PicaCompositionDomain::Scene ||
        draw.FragmentOperationMode != 0U || !draw.DepthTestEnabled ||
        (draw.ColorWriteMask & 0x7U) == 0U || !blend.Enabled) {
        return {};
    }

    const bool minMax =
        blend.EquationRgb == BlendEquation::Min ||
        blend.EquationRgb == BlendEquation::Max;
    const bool destinationDependent =
        minMax || blend.DestRgb != BlendFactor::Zero ||
        ReadsDestination(blend.SourceRgb);
    if (!destinationDependent) {
        return {};
    }
    if (minMax) {
        return {PicaReactiveCoverage::Full};
    }
    if (UsesSourceAlpha(blend.SourceRgb) ||
        UsesSourceAlpha(blend.DestRgb)) {
        return {PicaReactiveCoverage::SourceAlpha};
    }
    if (UsesSourceColor(blend.SourceRgb) ||
        UsesSourceColor(blend.DestRgb)) {
        return {PicaReactiveCoverage::SourceColor};
    }
    return {PicaReactiveCoverage::Full};
}

std::string BuildPicaReactiveMaskAssignment(
    const PicaReactiveMaskPlan& plan) {
    switch (plan.Coverage) {
        case PicaReactiveCoverage::SourceAlpha:
            return "    pica_material_guide.a = "
                   "clamp(pica_color.a, 0.0, 1.0); "
                   "// OOT3D reactive world draw: source alpha\n";
        case PicaReactiveCoverage::SourceColor:
            return "    pica_material_guide.a = clamp(max(max("
                   "abs(pica_color.r), abs(pica_color.g)), "
                   "abs(pica_color.b)), 0.0, 1.0); "
                   "// OOT3D reactive world draw: source color\n";
        case PicaReactiveCoverage::Full:
            return "    pica_material_guide.a = 1.0; "
                   "// OOT3D reactive world draw: destination dependent\n";
        case PicaReactiveCoverage::None:
            return {};
    }
    return {};
}

uint64_t ResolvePicaReactiveFragmentKey(
    uint64_t fragmentKey, PicaReactiveCoverage coverage) noexcept {
    return fragmentKey ^ 0x6d6f74696f6e0000ULL ^
           static_cast<uint64_t>(coverage);
}

PicaReactiveShaderVariant BuildPicaReactiveShaderVariant(
    std::string_view source, uint64_t fragmentKey,
    const PicaReactiveDrawInfo& draw) {
    PicaReactiveShaderVariant result{fragmentKey, {}, false};
    const auto plan = ResolvePicaReactiveMaskPlan(draw);
    if (!plan.Reactive() ||
        source.find("pica_material_guide") == std::string_view::npos ||
        (plan.Coverage != PicaReactiveCoverage::Full &&
         source.find("pica_color") == std::string_view::npos)) {
        return result;
    }
    result.Source.assign(source);
    const size_t closingBrace = result.Source.find_last_of('}');
    if (closingBrace == std::string::npos) return result;
    result.Source.insert(
        closingBrace, BuildPicaReactiveMaskAssignment(plan));
    result.FragmentKey = ResolvePicaReactiveFragmentKey(
        fragmentKey, plan.Coverage);
    result.Reactive = true;
    return result;
}

} // namespace Fast::Oot3d
