#include "fast/oot3d/pica_shader_instrumentation.h"
#include "fast/oot3d/display_effect_plan.h"
#include <iostream>
#include <stdexcept>

using namespace Fast::Oot3d;
static void check(bool value) {
    static int assertion = 0;
    ++assertion;
    if (!value) throw std::runtime_error("normal-guide assertion " + std::to_string(assertion));
}
int main() try {
    const std::string prefix = "#version 450\nlayout(location=0) out vec4 pica_color;\n"
        "layout(location=5) in vec4 pica_normquat;\nlayout(location=6) in vec3 pica_view;\n";
    const std::string native = "pica_color=vec4(1.0); gl_FragDepth=gl_FragCoord.z;";
    const std::string source = prefix + "void main(){" + native + "}";
    PicaFragmentInstrumentationRequest request;
    request.Source = source;
    request.RequestedFeatures = PicaShaderInstrumentationFeature::NormalGuide;
    auto result = BuildPicaFragmentInstrumentationVariant(request);
    check(result.Applied());
    check(result.Source.find(native) != std::string::npos);
    check(result.Source.find("oot3d_normal_guide_surface(pica_normquat, pica_view)") != std::string::npos);
    check(result.Source.find("cross(dFdx(to_eye), dFdy(to_eye))") != std::string::npos);
    check(result.Source.find("if (dot(q, q) >= 0.000001) return oot3d_normal_guide_rotate_z(q)") != std::string::npos);
    const std::string material = prefix + "void main(){vec3 normal=vec3(0,1,0);" + native + "}";
    request.Source = material;
    result = BuildPicaFragmentInstrumentationVariant(request);
    check(result.Applied());
    check(result.Source.find("pica_normal_guide = vec4(normal * 0.5 + 0.5, 1.0)") != std::string::npos);
    check(result.Source.find("dFdx") == std::string::npos);
    request.RequestedFeatures = PicaShaderInstrumentationFeature::None;
    result = BuildPicaFragmentInstrumentationVariant(request);
    check(!result.Applied());
    check(result.Source.empty());
    DisplayEffectPlanInput input;
    input.WorldSurface = input.PerspectiveAvailable = input.CameraAvailable = true;
    input.AmbientOcclusion = AmbientOcclusionMode::Cacao;
    auto plan = BuildDisplayEffectPlan(input);
    check(!plan.FindPass(DisplayEffectPass::Scanout)->ReadsResource(EffectResource::NormalGuide));
    input.GuideDiagnostics = true;
    for (const auto aa : {AntiAliasingMode::Off, AntiAliasingMode::Taa}) {
        input.AntiAliasing = aa;
        plan = BuildDisplayEffectPlan(input);
        check(plan.Graph.Valid());
        const auto* scanout = plan.FindPass(DisplayEffectPass::Scanout);
        check(scanout->ReadsResource(EffectResource::NormalGuide));
        check(scanout->ReadsResource(EffectResource::NativeDepth));
    }
    input.AlphaOverlay = true;
    plan = BuildDisplayEffectPlan(input);
    check(!plan.FindPass(DisplayEffectPass::Scanout)->ReadsResource(EffectResource::NormalGuide));
    std::cout << "normal-source selection, disabled identity, diagnostic graph reads and UI isolation passed\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
