#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "oot3d_native_room_runtime.h"
#include "oot3d_room_compilation_unit.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRoomRenderProvider.h"
#include "three_ds_recomp/oot3d/Oot3dSemanticRouteCatalog.h"

namespace Oot3dNativeGame {

class NativeA32ExecutionRuntime;

struct NativeRoomTevAlphaEvaluation {
    bool Evaluated = false;
    uint32_t Selector = 0;
    int32_t RandomValue = 0;
    float Alpha = 0.0f;
    uint8_t AlphaU8 = 0;
    std::string Status;
    std::string Error;
};

NativeRoomTevAlphaEvaluation EvaluateNativeRoomMaterialTevAlphaOperation(
    const Oot3d::RoomCompilationMaterialTevAlphaOperation& operation,
    int32_t setupIndex,
    std::span<const ThreeDsRecomp::Oot3d::SemanticGameplayFact> facts,
    std::span<const uint16_t> drawParams, uint32_t& randomState,
    NativeA32ExecutionRuntime* nativeExecution);

class NativeRoomRenderRuntime {
  public:
    NativeRoomRenderRuntime(const ThreeDsRecomp::Oot3d::AssetCatalog& catalog,
                            ThreeDsRecomp::Oot3d::NativeSourceProvider& sources,
                            int32_t sceneId, int32_t setupIndex,
                            NativeA32ExecutionRuntime* nativeExecution);

    void CaptureInitialRoom(
        int32_t roomIndex,
        ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene);
    bool PrepareRoomRequest(
        int32_t roomIndex,
        const ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene);
    bool CommitPreparedRoom(int32_t roomIndex, int32_t droppedPreviousRoom);
    void DiscardPreparedRoom();
    void ReleaseCompletedCleanup();
    uint32_t AdvanceMaterialAnimations(
        double deltaSeconds, double ticksPerSecond,
        ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene);
    void ApplySceneCallbacks(
        const Oot3d::RoomCompilationSceneCallbackContract& callbacks,
        std::span<const ThreeDsRecomp::Oot3d::SemanticGameplayFact> facts,
        std::span<const uint16_t> drawParams, uint32_t& randomState,
        ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene);
    void ComposeResidentRooms(
        const NativeRoomRuntime& roomRuntime,
        ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene);

    nlohmann::json Diagnostics() const;

  private:
    struct RoomResource {
        int32_t RoomIndex = -1;
        std::shared_ptr<const ThreeDsRecomp::Oot3d::NativeRoomRenderSource> Source;
        std::vector<ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel> Models;
        std::vector<ThreeDsRecomp::Oot3d::CmabMaterialAnimation> MaterialAnimations;
        std::vector<std::string> MaterialAnimationNames;
        std::string MaterialAnimationManifestResource;
        std::string MaterialAnimationArchiveResource;
        float MaterialAnimationFrame = 0.0f;
        uint64_t MaterialAnimationUpdateCount = 0;
        uint64_t MaterialAnimationAppliedBatchCount = 0;
        std::string MaterialAnimationStatus;
        std::string Status;
        std::string Error;
    };

    bool LoadRoomMaterialAnimations(RoomResource& resource);
    bool ApplyRoomMaterialAnimations(
        RoomResource& resource,
        std::vector<ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel>& models);
    bool SynchronizeVisibleModels(
        const ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene);
    static void RebuildBounds(
        ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene);

    int32_t mSceneId = -1;
    int32_t mSetupIndex = -1;
    ThreeDsRecomp::Oot3d::NativeSourceProvider& mSources;
    NativeA32ExecutionRuntime* mNativeExecution = nullptr;
    ThreeDsRecomp::Oot3d::NativeRoomRenderProvider mProvider;
    std::map<int32_t, RoomResource> mResident;
    std::vector<RoomResource> mCleanupQueue;
    std::optional<RoomResource> mPrepared;
    std::vector<int32_t> mVisibleRoomOrder;
    double mPendingNativeRenderTicks = 0.0;
    uint64_t mPrepareCount = 0;
    uint64_t mInstallCount = 0;
    uint64_t mCleanupCount = 0;
    uint64_t mCallbackInvocationCount = 0;
    uint64_t mCallbackEvaluationCount = 0;
    uint64_t mCallbackAppliedOperationCount = 0;
    uint64_t mCallbackAppliedBatchCount = 0;
    uint32_t mLastCallbackAddress = 0;
    uint32_t mLastCallbackSelector = 0;
    int32_t mLastCallbackRandomValue = 0;
    float mLastCallbackAlpha = 0.0f;
    uint8_t mLastCallbackAlphaU8 = 0;
    uint64_t mLastCallbackAppliedOperationCount = 0;
    uint64_t mLastCallbackAppliedBatchCount = 0;
    std::string mCallbackStatus = "native_room_callback_not_evaluated";
    std::string mCallbackError;
    std::string mStatus;
    std::string mError;
};

} // namespace Oot3dNativeGame
