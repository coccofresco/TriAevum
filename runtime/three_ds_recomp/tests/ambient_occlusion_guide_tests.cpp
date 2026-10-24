#include "fast/extension/display_effect_plan.h"
#include <iostream>
#include <stdexcept>

using namespace Fast::Extension;
static void check(bool value) {
    static int assertion = 0;
    ++assertion;
    if (!value) throw std::runtime_error("normal-guide assertion " + std::to_string(assertion));
}
int main() try {
    DisplayEffectPlanInput input;
    input.WorldSurface = input.PerspectiveAvailable = input.CameraAvailable = true;
    input.AmbientOcclusion = AmbientOcclusionMode::Gtao;
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
    std::cout << "AO diagnostic graph reads and UI isolation passed\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
