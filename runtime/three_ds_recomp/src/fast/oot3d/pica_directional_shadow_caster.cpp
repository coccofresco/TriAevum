#include "fast/oot3d/pica_directional_shadow_caster.h"

namespace Fast::Oot3d {
namespace {

constexpr uint64_t kDirectionalShadowCasterKey =
    0x534841444f574341ULL;

constexpr std::string_view kShadowTransformDeclaration =
    "layout(set=0,binding=1,std140) uniform "
    "Oot3dDirectionalShadowTransform {\n"
    "    mat4 view_to_world;\n"
    "    mat4 world_to_light_clip;\n"
    "} oot3d_shadow;\n";

constexpr std::string_view kShadowPositionOverride =
    "    gl_Position = oot3d_shadow.world_to_light_clip *\n"
    "        (oot3d_shadow.view_to_world * vec4(-pica_view, 1.0));\n";

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

} // namespace Fast::Oot3d
