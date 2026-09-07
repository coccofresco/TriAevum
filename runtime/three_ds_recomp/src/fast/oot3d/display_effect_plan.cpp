#include "fast/oot3d/display_effect_plan.h"

#include <algorithm>
#include <limits>

namespace Fast::Oot3d {
namespace {

[[nodiscard]] EffectResourceUse Read(EffectResource resource) {
    return { resource, EffectResourceAccess::Read };
}

[[nodiscard]] EffectResourceUse Write(EffectResource resource) {
    return { resource, EffectResourceAccess::Write };
}

void PopulateCompatibilityProjections(
    DisplayEffectPlan& plan, const DisplayEffectPlanInput& input) {
    plan.Cacao = plan.Enabled(DisplayEffectPass::Cacao);
    plan.Outline = plan.Enabled(DisplayEffectPass::Outline);
    plan.Reflections = plan.Enabled(DisplayEffectPass::Reflections);
    plan.FidelityFxSssr =
        plan.Reflections &&
        plan.ReflectionProvider == ReflectionMode::FidelityFxSssr;
    plan.TemporalMetadata =
        input.WorldSurface && input.PerspectiveAvailable &&
        input.CameraAvailable;
    plan.Taa = plan.Enabled(DisplayEffectPass::Taa);
    plan.Smaa = plan.Enabled(DisplayEffectPass::Smaa);
    plan.Nis = plan.Enabled(DisplayEffectPass::Nis);
    plan.TemporalUpscaler =
        plan.Enabled(DisplayEffectPass::TemporalUpscaler);
    plan.Fsr = plan.TemporalUpscaler &&
               plan.Upscaler == UpscalerProvider::Fsr;
    plan.Dlss = plan.TemporalUpscaler &&
                plan.Upscaler == UpscalerProvider::Dlss;
    plan.MotionVectors = plan.Enabled(DisplayEffectPass::Motion);
    plan.RequiresGuideTarget =
        !plan.Graph.Attachments.NativeColorOnly();
}

} // namespace

std::string_view DisplayEffectPassName(DisplayEffectPass pass) noexcept {
    switch (pass) {
        case DisplayEffectPass::Guides: return "Guides";
        case DisplayEffectPass::Cacao: return "CACAO";
        case DisplayEffectPass::HiZ: return "HiZ";
        case DisplayEffectPass::Reflections: return "Reflections";
        case DisplayEffectPass::Outline: return "Outline";
        case DisplayEffectPass::Composite: return "Composite";
        case DisplayEffectPass::Motion: return "Motion";
        case DisplayEffectPass::Taa: return "TAA";
        case DisplayEffectPass::TemporalUpscaler:
            return "TemporalUpscaler";
        case DisplayEffectPass::Nis: return "NIS";
        case DisplayEffectPass::Smaa: return "SMAA";
        case DisplayEffectPass::Scanout: return "Scanout";
        case DisplayEffectPass::WorkingColor: return "WorkingColor";
        case DisplayEffectPass::Count: return {};
    }
    return {};
}

const CompiledEffectPass* DisplayEffectPlan::FindPass(
    DisplayEffectPass pass) const noexcept {
    return Graph.FindPass(DisplayEffectPassName(pass));
}

bool DisplayEffectPlan::Enabled(DisplayEffectPass pass) const noexcept {
    return FindPass(pass) != nullptr;
}

std::span<const EffectResourceUse> DisplayEffectPlan::Bindings(
    DisplayEffectPass pass) const noexcept {
    const auto* compiled = FindPass(pass);
    return compiled != nullptr
               ? std::span<const EffectResourceUse>(compiled->Resources)
               : std::span<const EffectResourceUse>{};
}

size_t DisplayEffectPlan::DeclaredBindingCount() const noexcept {
    return Graph.DeclaredBindingCount();
}

DisplayEffectExecutionLedger::DisplayEffectExecutionLedger(
    const DisplayEffectPlan& plan) noexcept : mPlan(&plan) {
    mOutcomes.fill(DisplayEffectExecutionOutcome::Unresolved);
}

bool DisplayEffectExecutionLedger::Record(
    DisplayEffectPass pass,
    DisplayEffectExecutionOutcome outcome) noexcept {
    const size_t index = static_cast<size_t>(pass);
    if (mPlan == nullptr || index >= mOutcomes.size() ||
        outcome == DisplayEffectExecutionOutcome::Unresolved ||
        !mPlan->Enabled(pass)) {
        ++mUndeclaredRecordCount;
        return false;
    }
    if (mOutcomes[index] != DisplayEffectExecutionOutcome::Unresolved) {
        ++mDuplicateRecordCount;
        return false;
    }
    mOutcomes[index] = outcome;
    return true;
}

DisplayEffectExecutionSummary
DisplayEffectExecutionLedger::Summary() const noexcept {
    DisplayEffectExecutionSummary summary;
    summary.UndeclaredRecordCount = mUndeclaredRecordCount;
    summary.DuplicateRecordCount = mDuplicateRecordCount;
    if (mPlan == nullptr) {
        return summary;
    }

    for (size_t index = 0; index < mOutcomes.size(); ++index) {
        const auto pass = static_cast<DisplayEffectPass>(index);
        if (!mPlan->Enabled(pass)) {
            continue;
        }
        const uint32_t bit = 1U << static_cast<uint32_t>(index);
        summary.DeclaredPassMask |= bit;
        ++summary.DeclaredPassCount;
        switch (mOutcomes[index]) {
            case DisplayEffectExecutionOutcome::Executed:
                summary.ExecutedPassMask |= bit;
                ++summary.ExecutedPassCount;
                break;
            case DisplayEffectExecutionOutcome::Reused:
                summary.ReusedPassMask |= bit;
                ++summary.ReusedPassCount;
                break;
            case DisplayEffectExecutionOutcome::Fused:
                summary.FusedPassMask |= bit;
                ++summary.FusedPassCount;
                break;
            case DisplayEffectExecutionOutcome::Skipped:
                summary.SkippedPassMask |= bit;
                ++summary.SkippedPassCount;
                break;
            case DisplayEffectExecutionOutcome::Failed:
                summary.FailedPassMask |= bit;
                ++summary.FailedPassCount;
                break;
            case DisplayEffectExecutionOutcome::Unresolved:
                summary.UnresolvedPassMask |= bit;
                ++summary.UnresolvedPassCount;
                continue;
        }
        const size_t bindingCount = mPlan->Bindings(pass).size();
        if (mOutcomes[index] == DisplayEffectExecutionOutcome::Executed ||
            mOutcomes[index] == DisplayEffectExecutionOutcome::Reused ||
            mOutcomes[index] == DisplayEffectExecutionOutcome::Fused) {
            const size_t remaining =
                std::numeric_limits<uint32_t>::max() -
                summary.ActiveBindingCount;
            summary.ActiveBindingCount += static_cast<uint32_t>(
                std::min(bindingCount, remaining));
        }
    }
    return summary;
}

DisplayEffectPlan BuildDisplayEffectPlan(const DisplayEffectPlanInput& input) {
    DisplayEffectPlan plan;
    plan.ReflectionProvider = input.Reflections;
    plan.Upscaler = input.Upscaler;
    if (input.AlphaOverlay) {
        EffectGraph graph;
        graph.Add({ .Name = "Scanout",
                    .Stage = EffectStage::AfterUi,
                    .Contract = EffectContractKind::ComposerPass,
                    .Resources = { Read(EffectResource::SceneColor), Write(EffectResource::PresentationOutput) } });
        plan.Graph = graph.Compile();
        PopulateCompatibilityProjections(plan, input);
        return plan;
    }

    const bool cacao = input.WorldSurface &&
                       input.AmbientOcclusion ==
                           AmbientOcclusionMode::Cacao &&
                       input.PerspectiveAvailable;
    const bool outline = input.WorldSurface &&
                         input.Toon != ToonMode::Off &&
                         input.OutlineEnabled;
    const bool reflections = input.WorldSurface &&
                             input.Reflections != ReflectionMode::Off &&
                             input.PerspectiveAvailable;
    const bool fidelityFxSssr = reflections && input.Reflections == ReflectionMode::FidelityFxSssr;
    const bool temporalMetadata = input.WorldSurface &&
                                  input.PerspectiveAvailable &&
                                  input.CameraAvailable;
    const bool taa = temporalMetadata && (input.AntiAliasing == AntiAliasingMode::Taa || input.ForceTaa);
    const bool smaa = input.WorldSurface &&
                      input.AntiAliasing == AntiAliasingMode::Smaa1x &&
                      input.SmaaAvailable;
    const bool upscaler = input.WorldSurface &&
                          input.AntiAliasing == AntiAliasingMode::Upscaler;
    const bool nis = upscaler && input.Upscaler == UpscalerProvider::Nis;
    const bool fsr = temporalMetadata && upscaler && input.Upscaler == UpscalerProvider::Fsr;
    const bool dlss = temporalMetadata && upscaler && input.Upscaler == UpscalerProvider::Dlss;
    const bool temporalUpscaler = fsr || dlss;
    const bool hiZ = reflections || fsr;
    const bool motionVectors =
        temporalMetadata && (taa || temporalUpscaler || fidelityFxSssr || input.ForceMotion);

    const bool composite = (cacao || outline || reflections) &&
                           (taa || temporalUpscaler || smaa || nis);
    const bool workingColor = fidelityFxSssr ||
        ((taa || temporalUpscaler) && !composite);

    const bool normalGuide = cacao || outline || reflections;
    const bool materialGuide = reflections || motionVectors;
    const bool rigidMotionGuide = motionVectors || outline;
    const bool ambientGuide = cacao;
    std::vector<EffectResourceUse> guideOutputs;
    if (normalGuide) {
        guideOutputs.push_back(Write(EffectResource::NormalGuide));
    }
    if (materialGuide) {
        guideOutputs.push_back(Write(EffectResource::MaterialGuide));
    }
    if (rigidMotionGuide) {
        guideOutputs.push_back(Write(EffectResource::RigidMotionGuide));
    }
    if (ambientGuide) {
        guideOutputs.push_back(Write(EffectResource::AmbientGuide));
    }
    if (outline) {
        guideOutputs.push_back(Write(EffectResource::FogGuide));
        guideOutputs.push_back(Write(EffectResource::OutlineGeometryGuide));
    }

    EffectGraph graph;
    graph.Add({ .Name = "Guides",
                .Enabled = !guideOutputs.empty(),
                .Stage = EffectStage::NativeLighting,
                .Contract = EffectContractKind::AuxiliaryOutput,
                .Resources = std::move(guideOutputs) });
    graph.Add({ .Name = "WorkingColor",
                .Enabled = workingColor,
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::SceneColor),
                               Write(EffectResource::LinearWorkingColor) } });
    graph.Add({ .Name = "CACAO",
                .DependsOn = { "Guides" },
                .Enabled = cacao,
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::NativeSceneView), Read(EffectResource::NativeDepth),
                               Read(EffectResource::NormalGuide), Write(EffectResource::AmbientOcclusion) } });
    graph.Add({ .Name = "HiZ",
                .Enabled = hiZ,
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::NativeDepth), Write(EffectResource::HierarchicalDepth) } });
    graph.Add({ .Name = "Motion",
                .DependsOn = { "Guides" },
                .Enabled = motionVectors,
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::NativeSceneView), Read(EffectResource::NativeDepth),
                               Read(EffectResource::MaterialGuide), Read(EffectResource::RigidMotionGuide),
                               Write(EffectResource::MotionVectors),
                               Write(EffectResource::ReactiveMask) } });

    std::vector<std::string> reflectionDependencies{ "Guides", "HiZ" };
    std::vector<EffectResourceUse> reflectionResources;
    if (fidelityFxSssr) {
        reflectionDependencies.push_back("WorkingColor");
        reflectionDependencies.push_back("Motion");
        reflectionResources.push_back(Read(EffectResource::PicaSceneFrame));
    }
    reflectionResources.push_back(Read(EffectResource::NativeSceneView));
    reflectionResources.push_back(Read(
        fidelityFxSssr ? EffectResource::LinearWorkingColor
                       : EffectResource::SceneColor));
    if (fidelityFxSssr) {
        reflectionResources.push_back(Read(EffectResource::NativeDepth));
    }
    reflectionResources.push_back(Read(EffectResource::NormalGuide));
    reflectionResources.push_back(Read(EffectResource::MaterialGuide));
    reflectionResources.push_back(Read(EffectResource::HierarchicalDepth));
    if (fidelityFxSssr) {
        reflectionResources.push_back(Read(EffectResource::MotionVectors));
    }
    reflectionResources.push_back(Write(EffectResource::ReflectionColor));
    graph.Add({ .Name = "Reflections",
                .DependsOn = std::move(reflectionDependencies),
                .Enabled = reflections,
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = std::move(reflectionResources) });
    graph.Add({ .Name = "Outline",
                .DependsOn = { "Guides" },
                .Enabled = outline,
                .Stage = EffectStage::AfterTransparent,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::NativeDepth), Read(EffectResource::OutlineGeometryGuide),
                               Read(EffectResource::NormalGuide), Read(EffectResource::RigidMotionGuide),
                               Read(EffectResource::FogGuide) } });

    const EffectResource baseColor = workingColor
        ? EffectResource::LinearWorkingColor
        : EffectResource::SceneColor;
    std::vector<EffectResourceUse> compositeResources{ Read(baseColor) };
    if (cacao) {
        compositeResources.push_back(Read(EffectResource::AmbientOcclusion));
        compositeResources.push_back(Read(EffectResource::AmbientGuide));
    }
    if (reflections) {
        compositeResources.push_back(Read(EffectResource::ReflectionColor));
        compositeResources.push_back(Read(EffectResource::MaterialGuide));
    }
    if (outline) {
        compositeResources.push_back(Read(EffectResource::NativeDepth));
        compositeResources.push_back(Read(EffectResource::NormalGuide));
        compositeResources.push_back(Read(EffectResource::OutlineGeometryGuide));
        compositeResources.push_back(Read(EffectResource::RigidMotionGuide));
        compositeResources.push_back(Read(EffectResource::FogGuide));
    }
    compositeResources.push_back(Write(EffectResource::CompositeColor));
    graph.Add({ .Name = "Composite",
                .DependsOn = { "CACAO", "Reflections", "Outline",
                               "WorkingColor" },
                .Enabled = composite,
                .Stage = EffectStage::AfterTransparent,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = std::move(compositeResources) });
    const EffectResource sourceColor = composite
        ? EffectResource::CompositeColor
        : baseColor;
    graph.Add({ .Name = "TAA",
                .DependsOn = { "Composite", "Motion", "WorkingColor" },
                .Enabled = taa,
                .Stage = EffectStage::BeforeUi,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(sourceColor), Read(EffectResource::NativeDepth),
                               Read(EffectResource::MotionVectors), Write(EffectResource::TemporalColor) } });
    std::vector<EffectResourceUse> temporalUpscalerResources{
        Read(EffectResource::NativeSceneView),
        Read(sourceColor),
        Read(fsr ? EffectResource::HierarchicalDepth
                 : EffectResource::NativeDepth),
        Read(EffectResource::MotionVectors),
        Read(EffectResource::ReactiveMask),
        Write(EffectResource::UpscaledColor),
    };
    graph.Add({ .Name = "TemporalUpscaler",
                .DependsOn = { "Composite", "Motion", "HiZ",
                               "WorkingColor" },
                .Enabled = temporalUpscaler,
                .Stage = EffectStage::BeforeUi,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = std::move(temporalUpscalerResources) });
    graph.Add({ .Name = "NIS",
                .DependsOn = { "Composite" },
                .Enabled = nis,
                .Stage = EffectStage::BeforeUi,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(sourceColor), Write(EffectResource::UpscaledColor) } });
    graph.Add({ .Name = "SMAA",
                .DependsOn = { "Composite" },
                .Enabled = smaa,
                .Stage = EffectStage::BeforeUi,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(sourceColor), Write(EffectResource::AntiAliasedColor) } });

    EffectResource finalColor = sourceColor;
    if (taa)
        finalColor = EffectResource::TemporalColor;
    if (temporalUpscaler || nis) {
        finalColor = EffectResource::UpscaledColor;
    }
    if (smaa)
        finalColor = EffectResource::AntiAliasedColor;
    std::vector<EffectResourceUse> scanoutResources{ Read(finalColor) };
    if (!composite && cacao) {
        scanoutResources.push_back(Read(EffectResource::AmbientOcclusion));
        scanoutResources.push_back(Read(EffectResource::AmbientGuide));
    }
    if (!composite && reflections) {
        scanoutResources.push_back(Read(EffectResource::ReflectionColor));
        scanoutResources.push_back(Read(EffectResource::MaterialGuide));
    }
    if (!composite && outline) {
        scanoutResources.push_back(Read(EffectResource::NativeDepth));
        scanoutResources.push_back(Read(EffectResource::NormalGuide));
        scanoutResources.push_back(Read(EffectResource::OutlineGeometryGuide));
        scanoutResources.push_back(Read(EffectResource::RigidMotionGuide));
        scanoutResources.push_back(Read(EffectResource::FogGuide));
    }
    if (input.GuideDiagnostics && cacao && (composite || !outline)) {
        scanoutResources.push_back(Read(EffectResource::NativeDepth));
        scanoutResources.push_back(Read(EffectResource::NormalGuide));
    }
    scanoutResources.push_back(Write(EffectResource::PresentationOutput));
    graph.Add({ .Name = "Scanout",
                .DependsOn = { "CACAO", "Reflections", "Outline",
                               "Composite", "TAA", "TemporalUpscaler",
                               "NIS", "SMAA", "WorkingColor" },
                .Stage = EffectStage::AfterUi,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = std::move(scanoutResources) });
    plan.Graph = graph.Compile();
    PopulateCompatibilityProjections(plan, input);
    return plan;
}

} // namespace Fast::Oot3d
