#pragma once

#include <cstdint>
#include <functional>
#include <filesystem>
#include <list>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "oot3d_demo_host_actor_runtime.h"
#include "oot3d_native_a32_execution.h"
#include "oot3d_native_actor_core_contract.h"
#include "oot3d_native_audio_service.h"
#include "oot3d_native_closure_catalog.h"
#include "oot3d_native_limb_callback.h"
#include "oot3d_native_object_bank_runtime.h"
#include "oot3d_native_npc_tracking.h"
#include "oot3d_native_room_render_runtime.h"
#include "oot3d_native_room_runtime.h"
#include "oot3d_native_transition_actor_runtime.h"
#include "oot3d_native_abi_catalog.h"
#include "oot3d_room_compilation_unit.h"
#include "three_ds_recomp/oot3d/Oot3dNativeAssets.h"
#include "three_ds_recomp/oot3d/Oot3dNativeActorRenderProvider.h"
#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"
#include "three_ds_recomp/oot3d/Oot3dSemanticRouteCatalog.h"

namespace Oot3dNativeGame {

enum class NativeActorLifecycleState {
    PendingNativeInit,
    Active,
    ActiveExternalOwner,
    PendingNativeDestroy,
    Deleted,
};

struct NativeActorSpawnEntry {
    std::string InstanceKey;
    std::string ProfileKey;
    int EntryIndex = -1;
    int RoomIndex = -1;
    int16_t ActorId = -1;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 Position;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 Rotation;
    int16_t Params = 0;
    bool ExternalOwner = false;
    bool RenderBound = false;
};

struct NativeActorInstance {
    uint64_t RuntimeId = 0;
    NativeActorSpawnEntry Entry;
    ThreeDsRecomp::Oot3d::NativeActorProfile Profile;
    NativeActorLifecycleState State = NativeActorLifecycleState::PendingNativeInit;
    uint8_t Category = 0;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 HomePosition;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 WorldPosition;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 PreviousPosition;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 HomeRotation;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 WorldRotation;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 ShapeRotation;
    double UniformScale = 0.0;
    uint32_t SfxRequest = 0;
    uint64_t UpdateCount = 0;
};

struct NativeActorRuntimeConfig {
    std::shared_ptr<const ThreeDsRecomp::Oot3d::AssetCatalog> Assets;
    std::shared_ptr<const Oot3d::RoomCompilationUnit> CompilationUnit;
    std::function<ThreeDsRecomp::Oot3d::NativeSourceProvider::FileLoader()>
        NativeSourceLoaderFactory;
    std::vector<ThreeDsRecomp::Oot3d::SemanticGameplayFact> GameplayFacts;
    std::vector<int32_t> RoomRequestSmokeSequence;
    double MaterialAnimationTicksPerSecond = 0.0;
    std::string MaterialAnimationClockSource;
    std::filesystem::path NativeClosureManifestPath;
    std::filesystem::path NativeCodeBinPath;
    std::filesystem::path NativeAudioArchivePath;
    std::filesystem::path NativeStreamArchivePath;
    uint32_t NativeAudioOutputSampleRate = 44100;
    std::optional<NativeAudioBehaviorLayout> NativeAudioBehavior;
    std::optional<NativeAudioEnvelopeLayout> NativeAudioEnvelope;
    std::optional<NativeAudioModulationLayout> NativeAudioModulation;
    std::optional<NativeAudioSpatialLayout> NativeAudioSpatial;
    std::optional<NativeAudioMixLayout> NativeAudioMix;
    std::optional<NativeAudioFilterLayout> NativeAudioFilter;
    std::optional<NativeAudioReverbLayout> NativeAudioReverb;
    std::optional<NativeAudioSceneLayout> NativeAudioScene;
    std::optional<NativeAudioSoundSpecLayout> NativeAudioSoundSpec;
    uint32_t NativeCodeBaseAddress = 0;
    uint32_t EnRiverSoundIdTableAddress = 0;
    std::string EnRiverSoundIdTableSource;
};

struct NativeActorSemanticStateSelection {
    bool Available = false;
    uint32_t Index = 0;
    std::string Semantic;
    std::string Status;
    std::string Error;
};

NativeActorSemanticStateSelection ResolveNativeActorSemanticState(
    const nlohmann::json& states,
    const std::vector<ThreeDsRecomp::Oot3d::SemanticGameplayFact>& facts);

struct NativeActorBlinkProfile {
    bool Available = false;
    std::vector<uint8_t> Sequence;
    int16_t TimerBase = 0;
    int16_t TimerRange = 0;
    uint32_t RandomFunctionAddress = 0;
    uint32_t RandomFunctionSize = 0;
    std::string RandomFunctionSha256;
    uint32_t RandomStateAddress = 0;
    uint32_t RandomInitialState = 0;
    uint32_t RandomMultiplier = 0;
    uint32_t RandomIncrement = 0;
    std::string Status;
    std::string Error;
};

struct NativeActorBlinkState {
    int16_t Timer = 0;
    size_t SequenceIndex = 0;
    double PendingTicks = 0.0;
};

NativeActorBlinkProfile ResolveNativeActorBlinkProfile(const nlohmann::json& contract);
std::optional<float> NextNativeActorRandom(
    const NativeActorBlinkProfile& profile, uint32_t& state,
    NativeA32ExecutionRuntime& nativeExecution);
bool AdvanceNativeActorBlinkTick(const NativeActorBlinkProfile& profile,
                                 NativeActorBlinkState& state,
                                 uint32_t& randomState,
                                 NativeA32ExecutionRuntime& nativeExecution);

class NativeActorRuntime final : public Oot3dDemoHostActorRuntime {
  public:
    using Callback = std::function<void(NativeActorInstance&)>;

    explicit NativeActorRuntime(ActorCoreContract contract,
                                NativeActorRuntimeConfig config = {});

    void Initialize(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene) override;
    void SetFrameContext(const Oot3dDemoHostActorFrameContext& context) override;
    void Update(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
                double deltaSeconds) override;
    bool MixAudio(uint32_t sampleRate, size_t frameCount,
                  std::vector<int16_t>& stereoSamples) override;
    nlohmann::json Diagnostics() const override;

    NativeActorInstance* Spawn(const NativeActorSpawnEntry& entry,
                               const ThreeDsRecomp::Oot3d::NativeActorProfile& profile);
    NativeActorInstance* Find(int16_t actorId, uint8_t category);
    bool Kill(uint64_t runtimeId);
    bool ChangeCategory(uint64_t runtimeId, uint8_t category);
    void RegisterCallback(uint32_t address, Callback callback);
    bool RequestCompiledRoom(int32_t roomIndex);
    bool CompleteCompiledRoomRequest();
    bool PlayActorSound2(uint64_t runtimeId, uint32_t soundId);
    bool PlaySoundGeneral(const NativeAudioRequest& request);
    bool QueueAudioSequence(uint8_t playerIndex, uint32_t soundId,
                            uint32_t fadeInFrames = 0);
    bool QueueAudioStream(uint8_t playerIndex, uint32_t soundId,
                          uint8_t volume = 127, uint8_t pan = 64);
    bool SetAudioSequenceVolume(uint8_t playerIndex, uint8_t layer,
                                uint8_t volume, uint32_t fadeFrames = 0);
    void StopAudioSequence(uint8_t playerIndex,
                           uint32_t fadeOutFrames = 0);
    void StopAudioStream(uint8_t playerIndex,
                         uint32_t fadeOutFrames = 0);

    size_t LiveCount() const;
    const std::list<uint64_t>& CategoryList(uint8_t category) const;

  private:
    struct UnsupportedCallback {
        uint64_t RuntimeId = 0;
        int EntryIndex = -1;
        int16_t ActorId = -1;
        std::string ActorName;
        std::string Role;
        uint32_t Address = 0;
    };

    struct DeferredCompilationInstance {
        std::string InstanceKey;
        std::string ProfileKey;
        std::string Reason;
        int32_t ActorId = -1;
        int32_t RoomIndex = -1;
    };

    NativeActorInstance* FindByRuntimeId(uint64_t runtimeId);
    bool DispatchCallback(NativeActorInstance& actor, uint32_t address,
                          const char* role);
    void TryInitialize(NativeActorInstance& actor);
    void TryDestroy(NativeActorInstance& actor);
    void DeleteActor(NativeActorInstance& actor);
    void RecordUnsupported(const NativeActorInstance& actor, const char* role,
                           uint32_t address);
    void RecordProfileGap(const NativeActorSpawnEntry& entry, std::string reason);
    void InitializeFromCompilationUnit(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene);
    void InitializeLegacyScenePopulation(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene);
    void InitializeSceneAudio();
    void PrepareCompilationObjectBanks();
    bool CompilationObjectBankReady(int32_t objectId) const;
    bool SetCompilationObjectBankResidentRooms(std::vector<int32_t> roomIndices);
    std::vector<int32_t> CompilationObjectBankRoomsWithCleanup() const;
    void ReconcileCompilationResidency(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene);
    bool SpawnCompilationInstance(const Oot3d::RoomCompilationActorEntry& actor,
                                  const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene);
    void SetDeferredCompilationInstance(const Oot3d::RoomCompilationActorEntry& actor,
                                        std::string reason);
    void ClearDeferredCompilationInstance(std::string_view instanceKey);
    bool HasRenderBinding(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                          int actorEntryIndex, int32_t actorId) const;
    size_t RemoveArchiveSelectedVisualBinding(NativeActorInstance& actor);
    void InitializeNativeRenderServices();
    bool AdvanceActorBlink(const NativeActorBlinkProfile& profile,
                           NativeActorBlinkState& state);
    void RegisterEnKoCallbacks();
    void InitializeEnKo(NativeActorInstance& actor);
    void UpdateEnKo(NativeActorInstance& actor);
    void DestroyEnKo(NativeActorInstance& actor);
    void RefreshEnKoRenderModel(NativeActorInstance& actor);
    void LoadNamedActorContract(std::string_view resource,
                                std::string_view format,
                                std::string_view label);
    void RegisterNamedActorCallbacks(uint16_t actorId);
    void InitializeNamedActor(NativeActorInstance& actor);
    void UpdateNamedActor(NativeActorInstance& actor);
    void DestroyNamedActor(NativeActorInstance& actor);
    void RefreshNamedActorRenderModel(NativeActorInstance& actor);
    void LoadEnKusaContract();
    void RegisterEnKusaCallbacks();
    void InitializeEnKusa(NativeActorInstance& actor);
    void UpdateEnKusa(NativeActorInstance& actor);
    void DestroyEnKusa(NativeActorInstance& actor);
    void LoadObjHanaContract();
    void RegisterObjHanaCallbacks();
    void InitializeObjHana(NativeActorInstance& actor);
    void UpdateObjHana(NativeActorInstance& actor);
    void LoadRigidActorContracts();
    void RegisterRigidActorCallbacks(uint16_t actorId);
    void InitializeRigidActor(NativeActorInstance& actor);
    void UpdateRigidActor(NativeActorInstance& actor);
    void DestroyEnvironmentActor(NativeActorInstance& actor);
    void LoadEnHollContract();
    void RegisterEnHollCallbacks();
    void InitializeEnHoll(NativeActorInstance& actor);
    void UpdateEnHoll(NativeActorInstance& actor);
    void DestroyEnHoll(NativeActorInstance& actor);
    void RegisterEnRiverSoundCallbacks();
    void InitializeEnRiverSound(NativeActorInstance& actor);
    void UpdateEnRiverSound(NativeActorInstance& actor);
    void DestroyEnRiverSound(NativeActorInstance& actor);
    uint32_t ResolveEnRiverSoundNativeId(uint8_t selector) const;
    size_t FindActorVisual(std::string_view bindingKey) const;

    struct EnKoVisualState {
        ThreeDsRecomp::Oot3d::NativeEnKoRuntimeBinding Binding;
        std::shared_ptr<const ThreeDsRecomp::Oot3d::NativeActorRenderSource> Source;
        std::shared_ptr<const ThreeDsRecomp::Oot3d::NativeActorRenderSource> FaceSource;
        ThreeDsRecomp::Oot3d::NativeActorResourceVisibility Visibility;
        double AnimationFrame = 0.0;
        double PendingAnimationTicks = 0.0;
        uint32_t AnimationFrameCount = 0;
        NativeActorBlinkState Blink;
        uint8_t AppliedEyeIndex = 0;
        bool FaceMaterialApplied = false;
        NativeNpcTrackingState Tracking;
        NativeEnKoLimbCallbackState LimbCallback;
        std::string BindingKey;
        std::string Status;
        std::string Error;
    };

    struct NamedActorVisualState {
        uint16_t ActorId = 0;
        std::shared_ptr<const ThreeDsRecomp::Oot3d::NativeActorRenderSource> Source;
        std::string ModelAssetId;
        std::string AnimationAssetId;
        std::string AnimationMember;
        double ModelScale = 1.0;
        double PlaybackSpeed = 1.0;
        double StartFrame = 0.0;
        uint8_t PlaybackMode = 0;
        double AnimationFrame = 0.0;
        double PendingAnimationTicks = 0.0;
        uint32_t AnimationFrameCount = 0;
        uint8_t MouthFrame = 0;
        bool MouthSelected = false;
        NativeActorBlinkState Blink;
        uint8_t AppliedEyeIndex = 0;
        uint8_t AppliedMouthIndex = 0;
        bool FaceMaterialApplied = false;
        std::string BindingKey;
        std::string Status;
        std::string Error;
    };

    struct NamedActorDefinition {
        std::string Label;
        nlohmann::json Contract;
        NativeActorSemanticStateSelection SpawnState;
        NativeActorBlinkProfile BlinkProfile;
    };

    struct EnvironmentActorVisualState {
        uint16_t ActorId = 0;
        uint32_t Selector = 0;
        bool Destroyed = false;
        std::string ActorLabel;
        std::string ContractFamily;
        std::shared_ptr<const ThreeDsRecomp::Oot3d::NativeActorRenderSource> Source;
        std::string ModelAssetId;
        double ModelScale = 1.0;
        double ShapeYOffset = 0.0;
        double ShapeYawStepPerNativeTick = 0.0;
        double ShapeYawAccumulator = 0.0;
        ThreeDsRecomp::Oot3d::Oot3dDemoVec3 ModelLocalTranslation;
        bool InitialDrawSuppressed = false;
        uint32_t InitialDrawSuppressionMask = 0;
        std::vector<uint32_t> VisibleMeshIndices;
        std::string BindingKey;
        std::string Status;
        std::string Error;
    };

    struct RigidActorDefinition {
        std::string Label;
        nlohmann::json Contract;
    };

    struct EnHollRuntimeState {
        uint8_t ActionMode = 0;
        uint16_t TransitionIndex = 0;
        int32_t FrontRoom = -1;
        int32_t BackRoom = -1;
        bool WaitingForRoom = false;
        uint64_t RequestCount = 0;
        NativeEnHollTriggerEvaluation LastEvaluation;
        std::string Status;
    };

    struct EnRiverSoundRuntimeState {
        uint8_t SoundId = 0;
        uint8_t PathIndex = 0;
        uint32_t NativeSoundId = 0;
        uint64_t UpdateCount = 0;
        bool DrawInitialized = false;
        std::string Status;
    };

    ActorCoreContract mContract;
    NativeActorRuntimeConfig mConfig;
    std::vector<std::unique_ptr<NativeActorInstance>> mActors;
    std::vector<std::list<uint64_t>> mCategoryLists;
    std::unordered_map<uint32_t, Callback> mCallbacks;
    std::vector<UnsupportedCallback> mUnsupportedCallbacks;
    std::set<std::tuple<uint64_t, std::string, uint32_t>> mUnsupportedCallbackKeys;
    std::vector<std::pair<int, std::string>> mProfileGaps;
    std::unique_ptr<NativeObjectBankRuntime> mObjectBankRuntime;
    std::set<int32_t> mObjectBankCleanupRooms;
    std::vector<DeferredCompilationInstance> mDeferredCompilationInstances;
    std::unordered_map<std::string, uint64_t> mCompilationRuntimeIds;
    std::unique_ptr<NativeRoomRuntime> mRoomRuntime;
    uint64_t mNextRuntimeId = 1;
    uint64_t mFrameCount = 0;
    size_t mLiveCount = 0;
    size_t mSourceRoomActorCount = 0;
    size_t mSpawnedRoomActorCount = 0;
    size_t mCompiledActorInstanceCount = 0;
    size_t mCompiledActorProfileCount = 0;
    size_t mCompiledRoomCount = 0;
    size_t mCompiledObjectDependencyCount = 0;
    size_t mArchiveSelectedVisualReplacementCount = 0;
    bool mPopulationFromCompilationUnit = false;
    bool mInitialized = false;
    std::unique_ptr<ThreeDsRecomp::Oot3d::NativeSourceProvider> mSources;
    std::optional<Oot3d::NativeAbiCatalog> mNativeAbiCatalog;
    std::optional<NativeClosureCatalog> mNativeClosureCatalog;
    std::unique_ptr<NativeA32ExecutionRuntime> mNativeA32Execution;
    std::unique_ptr<NativeRoomRenderRuntime> mRoomRenderRuntime;
    std::unique_ptr<ThreeDsRecomp::Oot3d::NativeActorRenderProvider> mRenderProvider;
    nlohmann::json mEnKoRuntimeContract;
    std::optional<NativeActorSemanticStateSelection> mEnKoQuestState;
    NativeActorBlinkProfile mEnKoBlinkProfile;
    NativeNpcTrackingContract mEnKoTrackingContract;
    NativeEnKoLimbCallbackContract mEnKoLimbCallbackContract;
    uint32_t mNativeRandomState = 0;
    uint64_t mNativeRandomFailureCount = 0;
    std::string mNativeRandomStatus = "not_initialized";
    std::unordered_map<uint64_t, EnKoVisualState> mEnKoVisuals;
    std::unordered_map<uint16_t, NamedActorDefinition> mNamedActorDefinitions;
    std::unordered_map<uint64_t, NamedActorVisualState> mNamedActorVisuals;
    nlohmann::json mEnKusaRuntimeContract;
    nlohmann::json mObjHanaRuntimeContract;
    std::unordered_map<uint16_t, RigidActorDefinition> mRigidActorDefinitions;
    std::unordered_map<uint64_t, EnvironmentActorVisualState> mEnvironmentActorVisuals;
    NativeEnHollTriggerContract mEnHollContract;
    std::unordered_map<uint64_t, EnHollRuntimeState> mEnHollStates;
    std::unordered_map<uint64_t, EnRiverSoundRuntimeState> mEnRiverSoundStates;
    std::string mEnRiverSoundBindingStatus = "not_evaluated";
    std::vector<uint8_t> mNativeCodeBin;
    std::unique_ptr<NativeAudioService> mNativeAudioService;
    uint32_t mNativeAudioOutputSampleRate = 44100;
    std::string mSceneAudioStatus = "not_evaluated";
    bool mSceneBgmStarted = false;
    Oot3dDemoHostActorFrameContext mFrameContext;
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene* mCurrentScene = nullptr;
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene* mCurrentRenderScene = nullptr;
    double mCurrentDeltaSeconds = 0.0;
    double mActorCallbackSeconds = 0.0;
    double mRoomLifecycleSeconds = 0.0;
    double mRoomRenderSeconds = 0.0;
    double mAudioStateSeconds = 0.0;
};

const char* NativeActorLifecycleStateName(NativeActorLifecycleState state);

} // namespace Oot3dNativeGame
