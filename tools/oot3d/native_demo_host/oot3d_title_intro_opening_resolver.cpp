#include "oot3d_title_intro_opening_resolver.h"

const Oot3dTitleIntroOpeningOrchestrationRow* ResolveTitleIntroOpeningOrchestrationFromNativeTable(
    uint16_t& outIndex,
    std::string& outStatus) {
    const char* status = nullptr;
    const auto* row = Oot3d_TitleIntroOpeningResolverFindNativeOrchestration(&outIndex, &status);
    outStatus = status != nullptr ? status : "";
    return row;
}

const Oot3dSceneCutsceneIntroCameraCutsceneRow* FindTitleIntroInitialSceneCutsceneRowForOpeningOrchestration(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    std::string& outStatus) {
    const char* status = nullptr;
    const auto* row =
        Oot3d_TitleIntroOpeningResolverFindInitialSceneCutsceneRow(orchestration, &status);
    outStatus = status != nullptr ? status : "";
    return row;
}

int ResolveTitleIntroInitialSceneSetupOverride(bool titleIntroPlaybackEnabled) {
    if (!titleIntroPlaybackEnabled) {
        return -1;
    }

    uint16_t orchestrationIndex = OOT3D_TITLE_INTRO_OPENING_NO_ORCHESTRATION_INDEX;
    std::string orchestrationStatus;
    std::string cutsceneStatus;
    const auto* orchestration =
        ResolveTitleIntroOpeningOrchestrationFromNativeTable(orchestrationIndex, orchestrationStatus);
    const auto* row = FindTitleIntroInitialSceneCutsceneRowForOpeningOrchestration(
        orchestration,
        cutsceneStatus);
    return row != nullptr ? static_cast<int>(row->setupIndex) : -1;
}

std::string_view ResolveTitleIntroInitialSceneSetupSource(bool titleIntroPlaybackEnabled) {
    return Oot3d_TitleIntroOpeningResolverInitialSceneSetupSource(titleIntroPlaybackEnabled ? 1u : 0u);
}

const Oot3dTitleIntroOpeningRequiredAssetRef* FindTitleIntroOpeningRequiredAssetRef(
    uint16_t orchestrationIndex,
    uint16_t assetRefIndex) {
    return Oot3d_TitleIntroOpeningResolverFindRequiredAssetRef(orchestrationIndex, assetRefIndex);
}

const Oot3dTitleIntroOpeningActorBindingRow* ResolveTitleIntroOpeningActorBinding(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    uint16_t actorKind,
    std::string& outStatus) {
    const char* status = nullptr;
    const auto* row = Oot3d_TitleIntroOpeningResolverFindActorBinding(orchestration, actorKind, &status);
    outStatus = status != nullptr ? status : "";
    if (row == nullptr && orchestration != nullptr &&
        Oot3d_TitleIntroOpeningResolverStatusIsRequiredAssetNotPresent(status) != 0) {
        for (uint32_t i = 0; i < gOot3dTitleIntroOpeningActorBindingRowCount; ++i) {
            const auto& binding = gOot3dTitleIntroOpeningActorBindingRows[i];
            if (binding.orchestrationIndex != orchestration->orchestrationIndex ||
                binding.actorKind != actorKind ||
                binding.requiredAssetRefIndex == OOT3D_TITLE_INTRO_OPENING_ACTOR_RUNTIME_NO_INDEX) {
                continue;
            }
            const auto* assetRef = FindTitleIntroOpeningRequiredAssetRef(
                orchestration->orchestrationIndex,
                binding.requiredAssetRefIndex);
            if (assetRef != nullptr && assetRef->required != 0 && assetRef->present == 0) {
                outStatus += ":";
                outStatus += assetRef->romfsPath != nullptr ? assetRef->romfsPath : "";
                break;
            }
        }
    }
    if (row == nullptr && Oot3d_TitleIntroOpeningResolverStatusIsMissingActorBindingForKind(status) != 0) {
        outStatus += "_" + std::to_string(actorKind);
    }
    return row;
}

const Oot3dTitleIntroQdbSourceRow* ResolveTitleIntroOpeningQdbRow(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    bool qdbIndexOverride,
    uint16_t qdbIndexOverrideValue,
    uint16_t& outQdbIndex,
    std::string& outStatus) {
    if (qdbIndexOverride) {
        outQdbIndex = qdbIndexOverrideValue;
        const auto* row = Oot3d_TitleIntroOpeningResolverFindQdbRow(outQdbIndex);
        outStatus = Oot3d_TitleIntroOpeningResolverQdbOverrideStatus(row != nullptr ? 1u : 0u);
        return row;
    }
    const char* status = nullptr;
    const auto* row = Oot3d_TitleIntroOpeningResolverResolveQdbRow(orchestration, &outQdbIndex, &status);
    outStatus = status != nullptr ? status : "";
    return row;
}

bool TitleIntroScenePathMatches(const char* lhs, const char* rhs) {
    return Oot3d_TitleIntroOpeningResolverScenePathMatches(lhs, rhs) != 0;
}

const Oot3dSceneCutsceneCameraBlobRow* FindTitleIntroOpeningCameraBlobRow(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration) {
    return Oot3d_TitleIntroOpeningResolverFindCameraBlobRow(orchestration);
}

const Oot3dSceneCutsceneCameraBlobSegmentRow* FindTitleIntroOpeningCameraSegmentRow(
    const Oot3dTitleIntroOpeningOrchestrationRow* orchestration,
    const Oot3dSceneCutsceneCameraBlobRow* blobRow,
    double frame) {
    return Oot3d_TitleIntroOpeningResolverFindCameraSegmentRow(
        orchestration,
        blobRow,
        static_cast<float>(frame));
}
