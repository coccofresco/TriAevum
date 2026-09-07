#include "fast/renderer3ds/pica_nri_shader_contract.h"

#include <algorithm>
#include <array>

namespace Fast::Renderer3ds {
namespace {

struct CombinedSamplerDeclaration {
    std::string_view SignedDeclaration;
    std::string_view UnsignedDeclaration;
    std::string_view Name;
    uint32_t TextureBinding = 0;
    uint32_t SamplerBinding = 0;
    bool Required = true;
};

constexpr std::array<CombinedSamplerDeclaration, 4> kCombinedSamplers{{
    {"layout(set=0,binding=1) uniform sampler2D pica_texture0;",
     "layout(set=0,binding=1) uniform usampler2D pica_texture0;",
     "pica_texture0", 1U, 7U, true},
    {"layout(set=0,binding=2) uniform sampler2D pica_texture1;",
     {}, "pica_texture1", 2U, 8U, true},
    {"layout(set=0,binding=3) uniform sampler2D pica_texture2;",
     {}, "pica_texture2", 3U, 9U, true},
    {{}, "layout(set=0,binding=13) uniform usampler2D pica_lighting_lut;",
     "pica_lighting_lut", 13U, 14U, false},
}};

bool ReplaceOnce(std::string& source, std::string_view needle,
                 const std::string& replacement) {
    const size_t first = source.find(needle);
    if (first == std::string::npos ||
        source.find(needle, first + needle.size()) != std::string::npos)
        return false;
    source.replace(first, needle.size(), replacement);
    return true;
}

std::string BuildSeparateDeclaration(
    const CombinedSamplerDeclaration& declaration, bool unsignedTexture) {
    const std::string name(declaration.Name);
    const std::string imageType = unsignedTexture ? "utexture2D" : "texture2D";
    const std::string samplerType =
        unsignedTexture ? "usampler2D" : "sampler2D";
    return "layout(set=0,binding=" +
           std::to_string(declaration.TextureBinding) + ") uniform " +
           imageType + " " + name + "_image;\n"
           "layout(set=0,binding=" +
           std::to_string(declaration.SamplerBinding) + ") uniform sampler " +
           name + "_sampler;\n#define " + name + " " + samplerType + "(" +
           name + "_image," + name + "_sampler)";
}

} // namespace

bool ValidatePicaNriDescriptorContract() {
    std::array<bool, kPicaNriDescriptorBindings.size()> seen{};
    for (const auto& binding : kPicaNriDescriptorBindings) {
        if (binding.Binding >= seen.size() || seen[binding.Binding])
            return false;
        seen[binding.Binding] = true;
    }
    return std::all_of(seen.begin(), seen.end(),
                       [](bool present) { return present; });
}

PicaNriFragmentShaderVariant BuildPicaNriFragmentShaderVariant(
    std::string_view source,
    const PicaNriFragmentShaderExtensions& extensions) {
    PicaNriFragmentShaderVariant result;
    result.Source.assign(source);
    if (!ValidatePicaNriDescriptorContract()) {
        result.Error = "invalid internal PICA NRI descriptor contract";
        return result;
    }

    for (const auto& declaration : kCombinedSamplers) {
        const bool hasSigned =
            !declaration.SignedDeclaration.empty() &&
            result.Source.find(declaration.SignedDeclaration) !=
                std::string::npos;
        const bool hasUnsigned =
            !declaration.UnsignedDeclaration.empty() &&
            result.Source.find(declaration.UnsignedDeclaration) !=
                std::string::npos;
        if (!hasSigned && !hasUnsigned && !declaration.Required) {
            continue;
        }
        if (hasSigned == hasUnsigned) {
            result.Error = "missing or ambiguous combined sampler declaration: " +
                           std::string(declaration.Name);
            result.Source.assign(source);
            return result;
        }
        const std::string_view original =
            hasUnsigned ? declaration.UnsignedDeclaration
                        : declaration.SignedDeclaration;
        if (!ReplaceOnce(result.Source, original,
                         BuildSeparateDeclaration(declaration, hasUnsigned))) {
            result.Error = "combined sampler declaration is not unique: " +
                           std::string(declaration.Name);
            result.Source.assign(source);
            return result;
        }
    }

    if (!extensions.DirectionalShadowSampler.empty()) {
        const std::string samplerName(
            extensions.DirectionalShadowSampler);
        const std::string declaration =
            "layout(set=0,binding=10) uniform sampler2D " +
            samplerName + ";";
        const CombinedSamplerDeclaration shadow{
            declaration, {}, extensions.DirectionalShadowSampler,
            10U, 11U, false};
        const bool present =
            result.Source.find(shadow.SignedDeclaration) !=
            std::string::npos;
        if (present &&
            !ReplaceOnce(result.Source, shadow.SignedDeclaration,
                         BuildSeparateDeclaration(shadow, false))) {
            result.Error =
                "combined extension sampler declaration is not unique: " +
                samplerName;
            result.Source.assign(source);
            return result;
        }
    }

    result.Applied = true;
    return result;
}

} // namespace Fast::Renderer3ds
