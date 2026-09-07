#include "oot3d_title_intro_logo_runtime.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"

TitleIntroLogoRuntime LoadTitleIntroLogoRuntime(
    const std::filesystem::path& romfsRoot,
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    const Oot3dTitleIntroOpeningActorBindingRow* logoBinding,
    const std::string& logoBindingStatus) {
    TitleIntroLogoRuntime runtime;
    runtime.OpeningActorBindingRow = logoBinding;
    runtime.OpeningActorBindingStatus = logoBindingStatus;
    const char* bindingStatus =
        Oot3d_TitleIntroOpeningLogoRuntimeGetActorBindingStatus(orchestration, logoBinding);
    if (bindingStatus != nullptr) {
        runtime.Status = bindingStatus;
        return runtime;
    }

    const uint16_t orchestrationIndex = orchestration->orchestrationIndex;
    runtime.ActorInitRow =
        Oot3d_TitleIntroOpeningActorRuntimeGetActorInitSourceRow(logoBinding->actorBindingIndex);
    runtime.Decoded = runtime.ActorInitRow != nullptr && orchestration->titleLogoComponentRefCount > 0;
    const char* decodeStatus =
        Oot3d_TitleIntroOpeningLogoRuntimeGetDecodeStatus(
            runtime.ActorInitRow,
            orchestration->titleLogoComponentRefCount);
    const char* variantStatus = nullptr;
    const char* logoVariant = Oot3d_TitleIntroSourceSelectLogoVariant(&variantStatus);
    runtime.VariantStatus = variantStatus != nullptr ? variantStatus : "";
    runtime.Variant = logoVariant != nullptr ? logoVariant : "";
    if (!runtime.Decoded) {
        runtime.Status = decodeStatus != nullptr ? decodeStatus : "";
        return runtime;
    }

    bool allLoaded = true;
    runtime.Components.reserve(orchestration->titleLogoComponentRefCount);
    for (uint16_t i = 0; i < orchestration->titleLogoComponentRefCount; ++i) {
        const auto* row = Oot3d_TitleIntroOpeningLogoRuntimeGetComponentSourceRow(orchestrationIndex, i);
        TitleIntroLogoComponentRuntime component;
        component.Row = row;
        component.Variant = runtime.Variant;
        if (row == nullptr) {
            component.Status =
                Oot3d_TitleIntroOpeningLogoRuntimeComponentSourceRowMissingStatus();
            allLoaded = false;
            runtime.Components.push_back(std::move(component));
            continue;
        }
        component.ArchivePath = romfsRoot / (row->archivePath != nullptr ? row->archivePath : "");
        const bool useUsVariant =
            Oot3d_TitleIntroOpeningLogoRuntimeIsUsVariant(runtime.Variant.c_str()) != 0;
        component.CmbName = useUsVariant ? (row->cmbNameUs != nullptr ? row->cmbNameUs : "")
                                         : (row->cmbNameJpeu != nullptr ? row->cmbNameJpeu : "");
        component.CsabName = useUsVariant ? (row->csabNameUs != nullptr ? row->csabNameUs : "")
                                          : (row->csabNameJpeu != nullptr ? row->csabNameJpeu : "");
        component.MaterialAnimationRuntimeLoopOverrideResolved =
            row->materialAnimationRuntimeLoopOverrideValid != 0;
        component.MaterialAnimationRuntimeLoopMode = row->materialAnimationRuntimeLoopMode;
        component.MaterialAnimationRuntimeLoopFieldOffset =
            row->materialAnimationRuntimeLoopFieldOffset;
        component.MaterialAnimationRuntimeLoopSource =
            row->materialAnimationBindingBasis != nullptr ? row->materialAnimationBindingBasis : "";

        if (runtime.ArchivePath.empty()) {
            runtime.ArchivePath = component.ArchivePath;
        }
        if (!std::filesystem::is_regular_file(component.ArchivePath)) {
            component.Status = Oot3d_TitleIntroOpeningLogoRuntimeComponentArchiveMissingStatus();
            allLoaded = false;
            runtime.Components.push_back(std::move(component));
            continue;
        }
        if (component.CmbName.empty()) {
            component.Status =
                Oot3d_TitleIntroOpeningLogoRuntimeComponentCmbNameMissingStatus();
            allLoaded = false;
            runtime.Components.push_back(std::move(component));
            continue;
        }

        try {
            const auto cmbBytes = ThreeDsRecomp::Oot3d::ExtractZarFileBytes(component.ArchivePath, component.CmbName);
            component.Model = ThreeDsRecomp::Oot3d::ParseCmbModelBytes(
                cmbBytes, component.ArchivePath.string() + "!" + component.CmbName);
            ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelBuildOptions renderOptions;
            renderOptions.BakeTransformIntoVertices = false;
            component.BaseRenderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(component.Model, renderOptions);
            component.BaseRenderModelBuilt = !component.BaseRenderModel.Batches.empty();
            component.BindWorldTransforms =
                ThreeDsRecomp::Oot3d::BuildCmbSkeletonWorldTransforms(component.Model.Skeleton);
            if (!component.CsabName.empty()) {
                component.CsabBytes =
                    ThreeDsRecomp::Oot3d::ExtractZarFileBytes(component.ArchivePath, component.CsabName);
                component.Csab = ThreeDsRecomp::Oot3d::ParseCsabMetadataBytes(component.CsabBytes);
            }
            const std::string cmabName = row->cmabName != nullptr ? row->cmabName : "";
            if (!cmabName.empty()) {
                const auto cmabBytes =
                    ThreeDsRecomp::Oot3d::ExtractZarFileBytes(component.ArchivePath, cmabName);
                component.MaterialAnimations.push_back(
                    ThreeDsRecomp::Oot3d::ParseCmabMaterialAnimationBytes(
                        cmabBytes, component.ArchivePath.string() + "!" + cmabName));
                component.MaterialAnimationNames.push_back(cmabName);
            }
            component.Loaded = true;
            component.Status = Oot3d_TitleIntroOpeningLogoRuntimeComponentLoadedFromNativeZarStatus();
        } catch (const std::exception& ex) {
            component.Status = std::string("native_title_logo_component_load_failed: ") + ex.what();
            allLoaded = false;
        }
        runtime.Components.push_back(std::move(component));
    }

    runtime.DrawRows.reserve(orchestration->titleLogoDrawRefCount);
    for (uint16_t i = 0; i < orchestration->titleLogoDrawRefCount; ++i) {
        const auto* row = Oot3d_TitleIntroOpeningLogoRuntimeGetDrawSourceRow(orchestrationIndex, i);
        if (row == nullptr) {
            continue;
        }
        runtime.DrawRows.push_back(row);
    }
    runtime.DrawRouteDecoded =
        Oot3d_TitleIntroOpeningLogoRuntimeIsDrawRouteDecoded(orchestrationIndex) != 0;

    runtime.DrawContextRows.reserve(gOot3dTitleIntroLogoDrawContextRowCount);
    for (uint32_t i = 0; i < gOot3dTitleIntroLogoDrawContextRowCount; ++i) {
        const auto* row = Oot3d_TitleIntroOpeningLogoRuntimeGetDrawContextSourceRow(static_cast<uint16_t>(i));
        if (row == nullptr) {
            continue;
        }
        runtime.DrawContextRows.push_back(row);
    }
    runtime.DrawContextDecoded =
        Oot3d_TitleIntroOpeningLogoRuntimeIsDrawContextDecoded() != 0;
    runtime.DrawContextStatus =
        Oot3d_TitleIntroOpeningLogoRuntimeGetDrawContextStatus(runtime.DrawContextDecoded ? 1 : 0);
    runtime.DrawStatus =
        Oot3d_TitleIntroOpeningLogoRuntimeGetDrawStatus(runtime.DrawRouteDecoded ? 1 : 0,
                                                        runtime.DrawContextDecoded ? 1 : 0);

    runtime.UpdateRows.reserve(orchestration->titleLogoUpdateRefCount);
    for (uint16_t i = 0; i < orchestration->titleLogoUpdateRefCount; ++i) {
        const auto* row = Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateSourceRow(orchestrationIndex, i);
        if (row == nullptr) {
            continue;
        }
        runtime.UpdateRows.push_back(row);
    }
    runtime.UpdateRouteDecoded =
        Oot3d_TitleIntroOpeningLogoRuntimeIsUpdateRouteDecoded(orchestrationIndex) != 0;
    runtime.UpdateStatus =
        Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateStatus(runtime.UpdateRouteDecoded ? 1 : 0);

    runtime.Loaded = allLoaded && !runtime.Components.empty();
    runtime.Status = runtime.Loaded
                         ? Oot3d_TitleIntroOpeningLogoRuntimeLoadedFromOpeningActorBindingStatus()
                         : Oot3d_TitleIntroOpeningLogoRuntimeComponentAssetsIncompleteStatus();
    return runtime;
}

namespace {

void RecordTitleIntroLogoPhase(TitleIntroLogoAlphaState& state,
                               const Oot3dTitleIntroLogoUpdateRow* row) {
    if (row != nullptr && row->phaseRole != nullptr) {
        if (state.AppliedPhaseRoles.empty() || state.AppliedPhaseRoles.back() != row->phaseRole) {
            state.AppliedPhaseRoles.emplace_back(row->phaseRole);
        }
    }
}

} // namespace

TitleIntroLogoAlphaState SimulateTitleIntroLogoAlphaState(const TitleIntroLogoRuntime& runtime,
                                                          double titleFrame) {
    TitleIntroLogoAlphaState state;
    Oot3dTitleIntroOpeningLogoState logoState{};
    Oot3dTitleIntroOpeningLogoStepResult step{};
    bool stepFailed = false;
    state.Decoded = runtime.UpdateRouteDecoded;
    state.InputFrame = titleFrame;
    state.UpdateTicks =
        static_cast<uint32_t>(std::floor(std::max(0.0, titleFrame)));
    if (!runtime.UpdateRouteDecoded) {
        state.Status = Oot3d_TitleIntroOpeningLogoRuntimeUpdateRouteNotDecodedStatus();
        return state;
    }

    if (Oot3d_TitleIntroOpeningLogoRuntimeInitDefault(&logoState) !=
        OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_OK) {
        state.Status = Oot3d_TitleIntroOpeningLogoRuntimeInitFailedStatus();
        return state;
    }

    state.NativeRowsUsed = true;
    state.Simulated = true;
    state.Env3ScheduleSource =
        Oot3d_TitleIntroOpeningLogoRuntimeDiagnosticEnv3ScheduleSource();

    for (uint32_t tick = 0; tick < state.UpdateTicks; ++tick) {
        Oot3dTitleIntroOpeningLogoStepInput input{};
        input.envFlag3 = logoState.state == 0u ? 1u : 0u;
        if (input.envFlag3 != 0u) {
            state.Env3Triggered = true;
        }
        const auto status =
            Oot3d_TitleIntroOpeningLogoRuntimeStep(&logoState, &input, &step);
        if (status != OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_OK &&
            status != OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NO_MATCHING_STEP) {
            state.Status = Oot3d_TitleIntroOpeningLogoRuntimeStepFailedStatus();
            stepFailed = true;
            break;
        }
        RecordTitleIntroLogoPhase(state, step.sourceUpdateRow);
    }

    state.State = logoState.state;
    state.Substate = logoState.substate;
    state.DelayTimer = logoState.delayTimer;
    state.Timer = logoState.timer;
    state.TitleTextAlpha = logoState.titleTextAlpha;
    state.MainLogoAlpha = logoState.mainLogoAlpha;
    state.CopyrightAlpha = logoState.copyrightAlpha;
    state.EffectAlpha = logoState.effectAlpha;
    state.CopyrightAlphaStep = logoState.copyrightAlphaStep;
    state.FadeOutAlphaStep = logoState.fadeOutAlphaStep;
    state.DisplayReached = logoState.state >= 2u;
    if (!stepFailed) {
        state.Status = Oot3d_TitleIntroOpeningLogoRuntimeDiagnosticAlphaStateStatus(
            state.DisplayReached ? 1u : 0u);
    }
    return state;
}

TitleIntroLogoAlphaState TitleIntroLogoAlphaStateFromOpeningFrameRuntime(
    const TitleIntroLogoRuntime& runtime,
    const Oot3dTitleIntroOpeningLogoState& logoState,
    const Oot3dTitleIntroOpeningLogoStepResult& logoStep,
    double titleFrame) {
    TitleIntroLogoAlphaState state;
    state.Decoded = runtime.UpdateRouteDecoded;
    state.NativeRowsUsed = runtime.UpdateRouteDecoded;
    state.UsedForRender = true;
    state.InputFrame = titleFrame;
    state.UpdateTicks =
        static_cast<uint32_t>(std::floor(std::max(0.0, titleFrame)));
    state.State = logoState.state;
    state.Substate = logoState.substate;
    state.DelayTimer = logoState.delayTimer;
    state.Timer = logoState.timer;
    state.TitleTextAlpha = logoState.titleTextAlpha;
    state.MainLogoAlpha = logoState.mainLogoAlpha;
    state.CopyrightAlpha = logoState.copyrightAlpha;
    state.EffectAlpha = logoState.effectAlpha;
    state.CopyrightAlphaStep = logoState.copyrightAlphaStep;
    state.FadeOutAlphaStep = logoState.fadeOutAlphaStep;
    state.DisplayReached = logoState.state >= 2u;
    state.Env3ScheduleSource = Oot3d_TitleIntroOpeningLogoRuntimeOpeningFrameEnv3ScheduleSource();
    state.FrameBoundaryTracePending = false;
    if (logoStep.sourceUpdateRow != nullptr) {
        RecordTitleIntroLogoPhase(state, logoStep.sourceUpdateRow);
    }
    state.Status = Oot3d_TitleIntroOpeningLogoRuntimeOpeningFrameUsedForRenderStatus();
    return state;
}

std::vector<TitleIntroLogoAlphaState> BuildTitleIntroLogoAlphaReferenceSamples(
    const TitleIntroLogoRuntime& runtime) {
    std::vector<TitleIntroLogoAlphaState> samples;
    if (!runtime.UpdateRouteDecoded) {
        return samples;
    }

    const auto* bindCsab = Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(3u);
    const auto* wait = Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(4u);
    const auto* mainFade = Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(5u);
    const auto* init = Oot3d_TitleIntroOpeningLogoRuntimeGetUpdateRuntimeRow(0u);
    if (bindCsab == nullptr || wait == nullptr || mainFade == nullptr || init == nullptr ||
        init->copyrightAlphaStepDefault <= 0) {
        return samples;
    }

    const double copyrightTicks = std::ceil(init->maxAlpha /
                                           static_cast<double>(init->copyrightAlphaStepDefault));
    const double displayProbeFrame =
        2.0 + bindCsab->timerInitialValue + wait->timerInitialValue +
        mainFade->timerInitialValue + copyrightTicks + 5.0;
    samples.push_back(SimulateTitleIntroLogoAlphaState(runtime, 0.0));
    samples.push_back(SimulateTitleIntroLogoAlphaState(runtime, displayProbeFrame));
    return samples;
}

bool IsTitleIntroLogoRenderModel(const ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel& model) {
    return model.Name.rfind("title_intro:logo:", 0) == 0;
}
