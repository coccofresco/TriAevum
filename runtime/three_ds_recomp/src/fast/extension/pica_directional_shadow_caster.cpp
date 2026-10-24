#include "fast/extension/pica_directional_shadow_caster.h"

#include "fast/renderer3ds/pica_nri_shader_contract.h"

namespace Fast::Extension {
namespace {

constexpr uint64_t kDirectionalShadowCasterKey =
    0x56534d5041474532ULL;
constexpr uint64_t kAlphaShadowCasterKey =
    0x56534d414c504832ULL;

constexpr std::string_view kShadowTransformDeclaration =
    "layout(set=0,binding=30,std140) uniform "
    "Oot3dDirectionalShadowTransform {\n"
    "    mat4 view_to_world;\n"
    "    mat4 world_to_light_clip;\n"
    "} oot3d_shadow;\n"
    "layout(push_constant) uniform Oot3dVirtualShadowPage {\n"
    "    vec4 scale_offset;\n"
    "} oot3d_shadow_page;\n";

constexpr uint64_t kPointShadowCasterKey =
    0x50545348444f5731ULL;

constexpr std::string_view kPointTransformDeclaration =
    "layout(set=0,binding=30,std140) uniform "
    "Oot3dDirectionalShadowTransform {\n"
    "    mat4 view_to_world;\n"
    "    mat4 world_to_light_clip;\n"
    "} oot3d_shadow;\n"
    "layout(push_constant) uniform Oot3dPointShadowFace {\n"
    "    mat4 world_to_face_clip;\n"
    "} oot3d_point_face;\n";

constexpr std::string_view kPointPositionOverride =
    "    gl_Position = oot3d_point_face.world_to_face_clip *\n"
    "        (oot3d_shadow.view_to_world * vec4(-pica_view, 1.0));\n";

PicaDirectionalShadowCasterShader BuildCasterVariant(
    std::string_view source, uint64_t key, uint64_t salt,
    const ::Fast::Renderer3ds::PicaVertexShaderHookLayout& hooks,
    std::string_view declaration, std::string_view position) {
    using ::Fast::Renderer3ds::PicaVertexShaderHook;
    using ::Fast::Renderer3ds::PicaVertexShaderSemantic;
    PicaDirectionalShadowCasterShader result;
    if (source.empty()) return result;
    if (!hooks.ValidFor(source)) {
        result.Status = PicaDirectionalShadowCasterShaderStatus::InvalidHookContract;
        return result;
    }
    if (!hooks.Has(PicaVertexShaderSemantic::ViewPositionOutput)) {
        result.Status = PicaDirectionalShadowCasterShaderStatus::MissingViewPosition;
        return result;
    }
    const size_t declarations = hooks.Offset(PicaVertexShaderHook::RegisterStateEnd);
    const size_t mainEnd = hooks.Offset(PicaVertexShaderHook::MainBodyEnd);
    if (declarations > mainEnd || mainEnd > source.size()) {
        result.Status = PicaDirectionalShadowCasterShaderStatus::InvalidHookContract;
        return result;
    }
    result.Source.reserve(source.size() + declaration.size() + position.size());
    result.Source.append(source.substr(0U, declarations));
    result.Source.append(declaration);
    result.Source.append(source.substr(declarations, mainEnd - declarations));
    result.Source.append(position);
    result.Source.append(source.substr(mainEnd));
    result.Key = key ^ salt;
    result.Status = PicaDirectionalShadowCasterShaderStatus::Applied;
    return result;
}

constexpr std::string_view kNativeColorOutputDeclaration =
    "layout(location=0) out vec4 pica_color;";
constexpr std::string_view kPrivateColorDeclaration = "vec4 pica_color;";

constexpr std::string_view kShadowPositionOverride =
    "    gl_Position = oot3d_shadow.world_to_light_clip *\n"
    "        (oot3d_shadow.view_to_world * vec4(-pica_view, 1.0));\n"
    "    gl_Position.xy = gl_Position.xy * oot3d_shadow_page.scale_offset.xy +\n"
    "        oot3d_shadow_page.scale_offset.zw * gl_Position.w;\n";

} // namespace

PicaDirectionalShadowCasterShader
BuildPicaDirectionalShadowCasterShader(
    std::string_view source, uint64_t vertexKey,
    const ::Fast::Renderer3ds::PicaVertexShaderHookLayout& hooks) {
    using ::Fast::Renderer3ds::PicaVertexShaderHook;
    using ::Fast::Renderer3ds::PicaVertexShaderSemantic;

    PicaDirectionalShadowCasterShader result;
    if (source.empty()) {
        return result;
    }
    if (!hooks.ValidFor(source)) {
        result.Status =
            PicaDirectionalShadowCasterShaderStatus::InvalidHookContract;
        return result;
    }
    if (!hooks.Has(PicaVertexShaderSemantic::ViewPositionOutput)) {
        result.Status =
            PicaDirectionalShadowCasterShaderStatus::MissingViewPosition;
        return result;
    }

    const size_t declarations =
        hooks.Offset(PicaVertexShaderHook::RegisterStateEnd);
    const size_t mainEnd =
        hooks.Offset(PicaVertexShaderHook::MainBodyEnd);
    if (declarations > mainEnd || mainEnd > source.size()) {
        result.Status =
            PicaDirectionalShadowCasterShaderStatus::InvalidHookContract;
        return result;
    }

    result.Source.reserve(source.size() +
                          kShadowTransformDeclaration.size() +
                          kShadowPositionOverride.size());
    result.Source.append(source.substr(0U, declarations));
    result.Source.append(kShadowTransformDeclaration);
    result.Source.append(
        source.substr(declarations, mainEnd - declarations));
    result.Source.append(kShadowPositionOverride);
    result.Source.append(source.substr(mainEnd));
    result.Key = vertexKey ^ kDirectionalShadowCasterKey;
    result.Status = PicaDirectionalShadowCasterShaderStatus::Applied;
    return result;
}

PicaDirectionalShadowCasterShader
BuildPicaPointShadowCasterShader(
    std::string_view source, uint64_t vertexKey,
    const ::Fast::Renderer3ds::PicaVertexShaderHookLayout& hooks) {
    return BuildCasterVariant(source, vertexKey, kPointShadowCasterKey, hooks,
                              kPointTransformDeclaration, kPointPositionOverride);
}

PicaDirectionalShadowCasterShader
BuildPicaShadowCasterAlphaFragmentShader(
    std::string_view source, uint64_t fragmentKey,
    const ::Fast::Renderer3ds::PicaShaderHookLayout& hooks) {
    using ::Fast::Renderer3ds::PicaShaderHook;

    PicaDirectionalShadowCasterShader result;
    if (source.empty()) {
        return result;
    }
    if (!hooks.ValidFor(source) || !hooks.Supports(PicaShaderHook::AfterNativeTev)) {
        result.Status =
            PicaDirectionalShadowCasterShaderStatus::InvalidHookContract;
        return result;
    }
    if (!hooks.Supports(PicaShaderHook::BeforeDepth)) {
        result.Status = PicaDirectionalShadowCasterShaderStatus::MissingAlphaTest;
        return result;
    }
    // The native alpha test lies between the TEV result and the depth hook;
    // both precede everything the epilogue writes.
    const size_t tev = hooks.Offset(PicaShaderHook::AfterNativeTev);
    const size_t beforeDepth = hooks.Offset(PicaShaderHook::BeforeDepth);
    if (tev > beforeDepth || beforeDepth > hooks.Offset(PicaShaderHook::MainEpilogue)) {
        result.Status =
            PicaDirectionalShadowCasterShaderStatus::InvalidHookContract;
        return result;
    }
    // The map has no colour attachment. The published contract places the
    // only colour output at native location 0; its declaration becomes a
    // private variable, so nothing is exported and no code is rewritten.
    const size_t colour = source.find(kNativeColorOutputDeclaration);
    if (!hooks.Outputs.CanonicalNative() || colour == std::string_view::npos ||
        source.find(kNativeColorOutputDeclaration, colour + 1U) != std::string_view::npos ||
        colour > beforeDepth) {
        result.Status = PicaDirectionalShadowCasterShaderStatus::OutputContract;
        return result;
    }
    std::string truncated;
    truncated.reserve(beforeDepth + 2U);
    truncated.append(source.substr(0U, colour));
    truncated.append(kPrivateColorDeclaration);
    truncated.append(source.substr(colour + kNativeColorOutputDeclaration.size(),
                                   beforeDepth - colour - kNativeColorOutputDeclaration.size()));
    truncated.append("}\n");
    auto split = ::Fast::Renderer3ds::BuildPicaNriFragmentShaderVariant(truncated);
    if (!split.Applied) {
        result.Status = PicaDirectionalShadowCasterShaderStatus::SamplerContract;
        return result;
    }
    result.Source = std::move(split.Source);
    result.Key = fragmentKey ^ kAlphaShadowCasterKey;
    result.Status = PicaDirectionalShadowCasterShaderStatus::Applied;
    return result;
}

} // namespace Fast::Extension
