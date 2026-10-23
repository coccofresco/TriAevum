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
    plan.AmbientOcclusion = plan.Enabled(DisplayEffectPass::AmbientOcclusion);
    plan.Outline = plan.Enabled(DisplayEffectPass::Outline);
    plan.Reflections = plan.Enabled(DisplayEffectPass::Reflections);
    plan.GlobalIllumination = plan.Enabled(DisplayEffectPass::GlobalIllumination);
    plan.FidelityFxSssr =
        plan.Reflections &&
        plan.ReflectionProvider == ReflectionMode::FidelityFxSssr;
    plan.ReflectionsFromGlobalIllumination =
        plan.Reflections && plan.ReflectionProvider == ReflectionMode::Off;
    plan.ReflectionMissesFromGlobalIllumination =
        plan.Reflections && plan.GlobalIllumination && input.GlobalIlluminationSpecular &&
        !input.GlobalIlluminationScreenSpace && plan.ReflectionProvider == ReflectionMode::HiZ;
    plan.TemporalMetadata =
        input.WorldSurface && input.PerspectiveAvailable &&
        input.CameraAvailable &&
        input.Purpose == Renderer3ds::PicaCompositionPurpose::Presentation;
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
    const auto readsSceneBeforeUi = [&plan](DisplayEffectPass pass) {
        const auto* compiled = plan.FindPass(pass);
        return compiled != nullptr && compiled->ReadsResource(EffectResource::SceneBeforeUi);
    };
    plan.NativeUiOverlay = readsSceneBeforeUi(DisplayEffectPass::TemporalUpscaler) ||
                           readsSceneBeforeUi(DisplayEffectPass::Nis);
    plan.RequiresGuideTarget =
        !plan.Graph.Attachments.NativeColorOnly();
}

} // namespace

namespace {
// A pass belongs to a domain by its declared stage. The domains are adjacent by
// construction, so a pass in neither (atmosphere) is kept only as a dependency
// of a domain pass that reads it.
[[nodiscard]] bool InDisplayEffectDomain(EffectStage stage,
                                         DisplayEffectDomain domain) noexcept {
    const uint8_t order = static_cast<uint8_t>(stage);
    return domain == DisplayEffectDomain::Scene
        ? order <= static_cast<uint8_t>(EffectStage::AfterTransparent)
        : order >= static_cast<uint8_t>(EffectStage::BeforeUi);
}
} // namespace

std::vector<EffectPass> ProjectDisplayEffectPasses(
    const CompiledEffectGraph& graph, DisplayEffectDomain domain,
    std::span<const EffectResource> resolvedResources,
    EffectResource resolvedInput) {
    std::vector<EffectPass> projected;
    if (!graph.Valid()) {
        return projected;
    }
    constexpr size_t kResourceCount =
        static_cast<size_t>(EffectResource::Count);
    std::array<bool, kResourceCount> resolved{};
    for (const EffectResource resource : resolvedResources) {
        resolved[static_cast<size_t>(resource)] = true;
    }
    std::array<bool, kResourceCount> needed{};
    std::vector<EffectPass> reversed;
    reversed.reserve(graph.Passes.size());
    for (auto pass = graph.Passes.rbegin(); pass != graph.Passes.rend();
         ++pass) {
        // A contribution an earlier domain already applied to the colour is not
        // re-derived here.
        const bool alreadyResolved = std::any_of(
            pass->Resources.begin(), pass->Resources.end(),
            [&resolved](const EffectResourceUse& use) {
                return WritesEffectResource(use.Access) &&
                       resolved[static_cast<size_t>(use.Resource)];
            });
        if (alreadyResolved) {
            continue;
        }
        bool keep = InDisplayEffectDomain(pass->Stage, domain);
        if (!keep) {
            keep = std::any_of(
                pass->Resources.begin(), pass->Resources.end(),
                [&needed](const EffectResourceUse& use) {
                    return WritesEffectResource(use.Access) &&
                           needed[static_cast<size_t>(use.Resource)];
                });
        }
        if (!keep) {
            continue;
        }
        EffectPass kept;
        kept.Name = pass->Name;
        kept.Stage = pass->Stage;
        kept.Contract = pass->Contract;
        for (const EffectResourceUse& use : pass->Resources) {
            EffectResourceUse declared = use;
            if (ReadsEffectResource(use.Access) &&
                resolved[static_cast<size_t>(use.Resource)]) {
                declared.Resource = resolvedInput;
            }
            if (std::find(kept.Resources.begin(), kept.Resources.end(),
                          declared) != kept.Resources.end()) {
                // The resolved colour is read once, however many resolved
                // contributions it replaced.
                continue;
            }
            kept.Resources.push_back(declared);
        }
        for (const EffectResourceUse& use : kept.Resources) {
            if (ReadsEffectResource(use.Access)) {
                needed[static_cast<size_t>(use.Resource)] = true;
            }
        }
        reversed.push_back(std::move(kept));
    }
    projected.assign(reversed.rbegin(), reversed.rend());
    // Rebuild the dependency edges from the resource edges that survived, so a
    // kept pass still runs after whatever produces what it reads.
    std::array<std::string, kResourceCount> writer{};
    for (EffectPass& pass : projected) {
        for (const EffectResourceUse& use : pass.Resources) {
            if (!ReadsEffectResource(use.Access)) {
                continue;
            }
            const std::string& producer =
                writer[static_cast<size_t>(use.Resource)];
            if (!producer.empty() && producer != pass.Name &&
                std::find(pass.DependsOn.begin(), pass.DependsOn.end(),
                          producer) == pass.DependsOn.end()) {
                pass.DependsOn.push_back(producer);
            }
        }
        for (const EffectResourceUse& use : pass.Resources) {
            if (WritesEffectResource(use.Access)) {
                writer[static_cast<size_t>(use.Resource)] = pass.Name;
            }
        }
    }
    return projected;
}

std::string_view DisplayEffectPassName(DisplayEffectPass pass) noexcept {
    switch (pass) {
        case DisplayEffectPass::Guides: return "Guides";
        case DisplayEffectPass::AmbientOcclusion: return "AmbientOcclusion";
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
        case DisplayEffectPass::SceneComposition: return "SceneComposition";
        case DisplayEffectPass::GlobalIllumination: return "GlobalIllumination";
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

    // Scene stages are declared whenever the feature applies. Whether they run
    // at this boundary is decided by the declared domain projection below, not
    // by a list of names.
    const bool extensionWorld = input.WorldSurface &&
        input.Purpose == Renderer3ds::PicaCompositionPurpose::Presentation;
    const bool ao = extensionWorld &&
                       input.AmbientOcclusion != AmbientOcclusionMode::Off &&
                       input.PerspectiveAvailable;
    const bool outline = extensionWorld &&
                         input.Toon != ToonMode::Off &&
                         input.OutlineEnabled;
    const bool screenSpaceReflections = extensionWorld &&
                                        input.Reflections != ReflectionMode::Off &&
                                        input.PerspectiveAvailable;
    const bool fidelityFxSssr = screenSpaceReflections && input.Reflections == ReflectionMode::FidelityFxSssr;
    const bool temporalMetadata = extensionWorld &&
                                  input.PerspectiveAvailable &&
                                  input.CameraAvailable;
    // Temporal AO reprojects its history with the scene's motion vectors.
    const bool temporalAo = ao && temporalMetadata && input.AmbientOcclusionTemporal;
    const bool faceNormalAo = ao && input.AmbientOcclusionFaceNormal;
    // GI keeps a temporal history (motion vectors) and adds light in the
    // linear domain through the composite.
    const bool globalIllumination = temporalMetadata && input.GlobalIllumination;
    // The screen-space gather reads the geometry guide's face normal and the
    // normal guide's shading normal (M8), and has no specular term.
    const bool screenSpaceGi = globalIllumination && input.GlobalIlluminationScreenSpace;
    // GI specular is a reflection source: the Reflections pass resolves it
    // through the material guide and the composite mixes it like any
    // reflection. With HiZ it fills the misses (HiZ reports its hit
    // confidence); FidelityFX SSSR shades its own misses and owns the term.
    const bool giSpecular = globalIllumination && !screenSpaceGi && input.GlobalIlluminationSpecular &&
                            (!screenSpaceReflections || input.Reflections == ReflectionMode::HiZ);
    const bool reflections = screenSpaceReflections || giSpecular;
    // Same-extent anti-aliasing is a scene stage: it resolves the scene before
    // native UI, at the target's boundary before that UI when there is one
    // (the UI then draws over the resolved scene), otherwise in the
    // presentation of a display that carries no UI. A raw-copy boundary never
    // runs it, so a frame anti-aliases one scene version once.
    const bool sceneAntiAliasing = !input.NativeSceneBoundary || input.BeforeNativeUi;
    const bool taa = sceneAntiAliasing && temporalMetadata &&
        (input.AntiAliasing == AntiAliasingMode::Taa || input.ForceTaa);
    const bool smaa = sceneAntiAliasing && extensionWorld &&
                      input.AntiAliasing == AntiAliasingMode::Smaa1x &&
                      input.SmaaAvailable;
    // A native boundary has no presentation stages, so its inputs never declare
    // them: the domain projection below is what enforces that structurally, and
    // these keep the boundary's derived requirements free of presentation
    // resources.
    const bool upscaler = !input.NativeSceneBoundary && extensionWorld &&
                          input.AntiAliasing == AntiAliasingMode::Upscaler &&
                          input.UpscalerAvailable;
    const bool nis = upscaler && input.Upscaler == UpscalerProvider::Nis;
    const bool fsr = temporalMetadata && upscaler && input.Upscaler == UpscalerProvider::Fsr;
    const bool dlss = temporalMetadata && upscaler && input.Upscaler == UpscalerProvider::Dlss;
    const bool temporalUpscaler = fsr || dlss;
    // An upscaler changes the extent, so it cannot resolve before native UI
    // like TAA and SMAA. When the display also carries its resolved scene
    // before that UI, the upscaler reconstructs that scene instead of the
    // display and the display's UI is laid over its output: the UI never enters
    // it. Every read of the display's scene colour then reads that scene.
    const bool nativeUiOverlay = input.SceneBeforeUi && input.SceneEffectsResolved && input.WorldSurface &&
                                 !input.NativeSceneBoundary && (temporalUpscaler || nis);
    const EffectResource displayScene =
        nativeUiOverlay ? EffectResource::SceneBeforeUi : EffectResource::SceneColor;
    const bool hiZ = screenSpaceReflections || fsr;
    const bool motionVectors =
        temporalMetadata && (taa || temporalUpscaler || fidelityFxSssr || globalIllumination || temporalAo ||
                             (!input.NativeSceneBoundary && input.ForceMotion));

    // A native boundary materializes the composed scene for its consumer at
    // the boundary's own order, so the scene stages must actually be baked into
    // the boundary output rather than fused into presentation.
    const bool composite = (ao || outline || reflections || globalIllumination) &&
                           (taa || temporalUpscaler || smaa || nis ||
                            input.NativeSceneBoundary || globalIllumination);
    const bool workingColor = fidelityFxSssr ||
        ((taa || temporalUpscaler) && !composite);

    const bool normalGuide = ao || outline || reflections || globalIllumination;
    const bool materialGuide = reflections || motionVectors;
    const bool rigidMotionGuide = motionVectors || outline;
    const bool ambientGuide = ao;
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
    }
    if (outline || faceNormalAo || screenSpaceGi) {
        guideOutputs.push_back(Write(EffectResource::OutlineGeometryGuide));
    }
    if (globalIllumination) {
        guideOutputs.push_back(Write(EffectResource::DiffuseAlbedoGuide));
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
                .Resources = { Read(displayScene),
                               Write(EffectResource::LinearWorkingColor) } });
    std::vector<EffectResourceUse> aoResources{ Read(EffectResource::NativeSceneView),
                                                Read(EffectResource::NativeDepth),
                                                Read(EffectResource::NormalGuide) };
    if (temporalAo)
        aoResources.push_back(Read(EffectResource::MotionVectors));
    if (faceNormalAo)
        aoResources.push_back(Read(EffectResource::OutlineGeometryGuide));
    aoResources.push_back(Write(EffectResource::AmbientOcclusion));
    graph.Add({ .Name = "AmbientOcclusion",
                .DependsOn = temporalAo ? std::vector<std::string>{ "Guides", "Motion" }
                                        : std::vector<std::string>{ "Guides" },
                .Enabled = ao,
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = std::move(aoResources) });
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
    if (giSpecular && screenSpaceReflections) {
        reflectionDependencies.push_back("GlobalIllumination");
        reflectionResources.insert(reflectionResources.end() - 1, Read(EffectResource::SpecularIllumination));
    } else if (giSpecular) {
        reflectionDependencies = { "Guides", "GlobalIllumination" };
        reflectionResources = { Read(EffectResource::NativeSceneView), Read(EffectResource::NormalGuide),
                                Read(EffectResource::MaterialGuide), Read(EffectResource::SpecularIllumination),
                                Write(EffectResource::ReflectionColor) };
    }
    graph.Add({ .Name = "Reflections",
                .DependsOn = std::move(reflectionDependencies),
                .Enabled = reflections,
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = std::move(reflectionResources) });
    std::vector<EffectResourceUse> globalIlluminationResources = screenSpaceGi
        ? std::vector<EffectResourceUse>{
              Read(EffectResource::NativeSceneView), Read(EffectResource::DiffuseAlbedoGuide),
              Read(EffectResource::NativeDepth), Read(EffectResource::OutlineGeometryGuide),
              Read(EffectResource::NormalGuide), Read(EffectResource::MotionVectors),
              Write(EffectResource::DiffuseIllumination) }
        : std::vector<EffectResourceUse>{
              Read(EffectResource::NativeSceneView), Read(EffectResource::DiffuseAlbedoGuide),
              Read(EffectResource::NativeDepth), Read(EffectResource::NormalGuide),
              Read(EffectResource::MaterialGuide), Read(EffectResource::MotionVectors),
              Write(EffectResource::DiffuseIllumination) };
    if (giSpecular) globalIlluminationResources.push_back(Write(EffectResource::SpecularIllumination));
    graph.Add({ .Name = "GlobalIllumination",
                .DependsOn = { "Guides", "Motion" },
                .Enabled = globalIllumination,
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = std::move(globalIlluminationResources) });
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
        : displayScene;
    std::vector<EffectResourceUse> compositeResources{ Read(baseColor) };
    if (ao) {
        compositeResources.push_back(Read(EffectResource::AmbientOcclusion));
        compositeResources.push_back(Read(EffectResource::AmbientGuide));
    }
    if (reflections) {
        compositeResources.push_back(Read(EffectResource::ReflectionColor));
        compositeResources.push_back(Read(EffectResource::MaterialGuide));
    }
    if (globalIllumination) {
        compositeResources.push_back(Read(EffectResource::DiffuseIllumination));
        compositeResources.push_back(Read(EffectResource::DiffuseAlbedoGuide));
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
                .DependsOn = { "AmbientOcclusion", "Reflections", "Outline",
                               "WorkingColor", "GlobalIllumination" },
                .Enabled = composite,
                .Stage = EffectStage::AfterTransparent,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = std::move(compositeResources) });
    const EffectResource sourceColor = composite
        ? EffectResource::CompositeColor
        : baseColor;
    plan.SceneOutput = sourceColor;
    // Same-extent anti-aliasing completes the scene (see sceneAntiAliasing):
    // a boundary before native UI resolves it into its composition, and a
    // display whose scene that boundary resolved finds it already applied.
    graph.Add({ .Name = "TAA",
                .DependsOn = { "Composite", "Motion", "WorkingColor" },
                .Enabled = taa,
                .Stage = EffectStage::AfterTransparent,
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
    };
    // The display's UI laid over the upscaled scene: where the display differs
    // from its scene before UI.
    const auto readUi = [&](std::vector<EffectResourceUse>& resources) {
        if (!nativeUiOverlay)
            return;
        resources.push_back(Read(EffectResource::SceneColor));
        if (sourceColor != EffectResource::SceneBeforeUi)
            resources.push_back(Read(EffectResource::SceneBeforeUi));
    };
    readUi(temporalUpscalerResources);
    temporalUpscalerResources.push_back(Write(EffectResource::UpscaledColor));
    std::vector<EffectResourceUse> nisResources{ Read(sourceColor) };
    readUi(nisResources);
    nisResources.push_back(Write(EffectResource::UpscaledColor));
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
                .Resources = std::move(nisResources) });
    graph.Add({ .Name = "SMAA",
                .DependsOn = { "Composite" },
                .Enabled = smaa,
                .Stage = EffectStage::AfterTransparent,
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
    std::vector<EffectResourceUse> terminalResources{ Read(finalColor) };
    if (!composite && ao) {
        terminalResources.push_back(Read(EffectResource::AmbientOcclusion));
        terminalResources.push_back(Read(EffectResource::AmbientGuide));
    }
    if (!composite && reflections) {
        terminalResources.push_back(Read(EffectResource::ReflectionColor));
        terminalResources.push_back(Read(EffectResource::MaterialGuide));
    }
    if (!composite && outline) {
        terminalResources.push_back(Read(EffectResource::NativeDepth));
        terminalResources.push_back(Read(EffectResource::NormalGuide));
        terminalResources.push_back(Read(EffectResource::OutlineGeometryGuide));
        terminalResources.push_back(Read(EffectResource::RigidMotionGuide));
        terminalResources.push_back(Read(EffectResource::FogGuide));
    }
    if (input.GuideDiagnostics && ao && (composite || !outline)) {
        terminalResources.push_back(Read(EffectResource::NativeDepth));
        terminalResources.push_back(Read(EffectResource::NormalGuide));
    }
    // The two declared terminals of this graph: the scene output a native
    // boundary's consumer samples, and the presentation scanout. Which one a
    // plan describes is its domain, so neither is selected by an effect name.
    std::vector<EffectResourceUse> compositionResources = terminalResources;
    compositionResources.push_back(Write(EffectResource::SceneComposition));
    graph.Add({ .Name = "SceneComposition",
                .DependsOn = { "AmbientOcclusion", "Reflections", "Outline",
                               "Composite", "TAA", "SMAA", "WorkingColor" },
                .Enabled = input.NativeSceneBoundary,
                .Stage = EffectStage::AfterTransparent,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = std::move(compositionResources) });
    terminalResources.push_back(Write(EffectResource::PresentationOutput));
    graph.Add({ .Name = "Scanout",
                .DependsOn = { "AmbientOcclusion", "Reflections", "Outline",
                               "Composite", "TAA", "TemporalUpscaler",
                               "NIS", "SMAA", "WorkingColor" },
                .Enabled = !input.NativeSceneBoundary,
                .Stage = EffectStage::AfterUi,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = std::move(terminalResources) });
    plan.Graph = graph.Compile();
    plan.SceneOutput = sourceColor;
    const bool sceneEffectsResolved =
        input.WorldSurface && input.SceneEffectsResolved;
    if (plan.Graph.Valid() &&
        (input.NativeSceneBoundary || sceneEffectsResolved)) {
        // Everything a scene stage resolves into the colour is already there:
        // the resources are collected from the same declaration the boundary
        // projects, so a new scene pass needs no entry here. Auxiliary scene
        // data (guides, depth, motion) stays re-derivable for the stages that
        // consume it.
        std::array<EffectResource, static_cast<size_t>(EffectResource::Count)>
            resolvedColour{};
        size_t resolvedCount = 0U;
        if (sceneEffectsResolved) {
            for (const auto& pass : plan.Graph.Passes) {
                if (!InDisplayEffectDomain(pass.Stage,
                                           DisplayEffectDomain::Scene)) {
                    continue;
                }
                for (const auto& use : pass.Resources) {
                    if (!WritesEffectResource(use.Access) ||
                        IsAuxiliarySceneData(use.Resource) ||
                        IsLinearWorkingDomain(use.Resource) ||
                        // This plan's own terminal is what the boundary
                        // materializes, not a contribution already in the
                        // colour, so it is never collected as resolved.
                        use.Resource == EffectResource::SceneComposition) {
                        continue;
                    }
                    if (std::find(resolvedColour.begin(),
                                  resolvedColour.begin() +
                                      static_cast<ptrdiff_t>(resolvedCount),
                                  use.Resource) ==
                        resolvedColour.begin() +
                            static_cast<ptrdiff_t>(resolvedCount)) {
                        resolvedColour[resolvedCount++] = use.Resource;
                    }
                }
            }
        }
        EffectGraph projected;
        // A composed scene that was built from the linear working domain is
        // still read in that domain: the conversion is not a scene effect, so
        // the resolved colour is converted for the stages that declared it
        // instead of being handed encoded colour. The resolved colour is the
        // scene before native UI when an upscaler lays the UI over its output.
        EffectResource resolvedSceneInput = displayScene;
        if (sceneEffectsResolved) {
            for (const auto& pass : plan.Graph.Passes) {
                const bool writesComposedScene = std::any_of(
                    pass.Resources.begin(), pass.Resources.end(),
                    [&plan](const EffectResourceUse& use) {
                        return WritesEffectResource(use.Access) &&
                               use.Resource == plan.SceneOutput;
                    });
                if (!writesComposedScene) {
                    continue;
                }
                const bool composedFromLinearDomain = std::any_of(
                    pass.Resources.begin(), pass.Resources.end(),
                    [](const EffectResourceUse& use) {
                        return ReadsEffectResource(use.Access) &&
                               IsLinearWorkingDomain(use.Resource);
                    });
                if (composedFromLinearDomain) {
                    resolvedSceneInput = EffectResource::LinearWorkingColor;
                }
            }
        }
        const auto projectedPasses = ProjectDisplayEffectPasses(
            plan.Graph,
            input.NativeSceneBoundary ? DisplayEffectDomain::Scene
                                      : DisplayEffectDomain::Presentation,
            sceneEffectsResolved
                ? std::span<const EffectResource>(resolvedColour.data(),
                                                  resolvedCount)
                : std::span<const EffectResource>{},
            resolvedSceneInput);
        for (auto pass : projectedPasses) {
            projected.Add(std::move(pass));
        }
        if (input.NativeSceneBoundary) {
            // The boundary's declared output is sampled by a native consumer,
            // which is what retires it readable for that consumer.
            projected.Export(EffectResource::SceneComposition);
        }
        if (!input.NativeSceneBoundary || sceneEffectsResolved) {
            // No scene stage applies to this colour: either none is declared, or
            // the version this plan consumes already carries them, so the native
            // target itself is the composed scene.
            plan.SceneOutput = EffectResource::SceneColor;
        } else {
            // The colour the boundary's composition reads: the scene stages'
            // colour, anti-aliased when the boundary precedes native UI.
            plan.SceneOutput = finalColor;
        }
        plan.Graph = projected.Compile();
    }
    PopulateCompatibilityProjections(plan, input);
    return plan;
}

} // namespace Fast::Oot3d
