#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeSourceProvider.h"

namespace ThreeDsRecomp::Oot3d {

struct NativeActorArchiveModel {
    std::string MemberName;
    CmbModel Model;
    mutable Oot3dNativeRenderModel RenderModel;
    std::vector<Matrix4f> BindWorldTransforms;
    mutable bool RenderModelTexturePayloadsStripped = false;
};

struct NativeActorArchiveSource {
    std::string SourceContainer;
    std::string ResourcePath;
    std::shared_ptr<const NativeSource> Source;
    ZarArchive Archive;
    std::vector<NativeActorArchiveModel> Models;
    std::vector<CmabMaterialAnimation> MaterialAnimations;
    std::string Status;
    std::string Error;

    bool Ready() const;
};

struct NativeActorRenderSource {
    const AssetCatalogRecord* CatalogRecord = nullptr;
    std::shared_ptr<const NativeActorArchiveSource> Archive;
    const NativeActorArchiveModel* Model = nullptr;
    std::vector<const AssetCatalogRecord*> AnimationRecords;
    std::string Status;
    std::string Error;

    bool Ready() const;
};

struct NativeActorAnimationSample {
    const AssetCatalogRecord* AnimationRecord = nullptr;
    CsabPose Pose;
    float SampledFrame = 0.0f;
    uint32_t FrameCount = 0;
    bool NativeClockApplied = false;
    std::string NativeClockStatus;
    std::string Status;
    std::string Error;

    bool Ready() const;
};

enum class NativeActorAnimationPlaybackMode : uint8_t {
    Clamp = 0,
    Loop = 1,
};

struct NativeActorAnimationTimeInput {
    float SourceFrame = 0.0f;
    float SourceStartFrame = 0.0f;
    float SourceEndFrame = 0.0f;
    float SourcePlaybackSpeed = 1.0f;
    uint64_t SourceUpdateSerial = 0;
    bool SourceUpdateSerialValid = false;
    std::string BindingKey;
    NativeActorAnimationPlaybackMode PlaybackMode = NativeActorAnimationPlaybackMode::Loop;
};

struct NativeActorAnimationClockState {
    bool Initialized = false;
    std::string BindingKey;
    float PreviousSourceFrame = 0.0f;
    uint64_t PreviousSourceUpdateSerial = 0;
    float NativeFrame = 0.0f;
    float ObservedSourceStep = 0.0f;
    int SourceDirection = 1;
    uint64_t BindingResetCount = 0;
    uint64_t SourceRestartCount = 0;
    uint64_t SourceLoopCount = 0;
    uint64_t PlaybackSpeedAdvanceCount = 0;
    uint64_t RenderHoldCount = 0;
    std::string Status;
};

struct NativeActorAnimationTimeSourceBinding {
    bool Available = false;
    bool ContractPresent = false;
    bool Matched = false;
    bool DirectSample = false;
    bool ClampSample = false;
    bool AdjustScaffoldPlayback = false;
    std::string Source = "skel_animation_clock";
    float SourceFrameSpan = 0.0f;
    float NativeFrameSpan = 0.0f;
    float SampleScale = 1.0f;
    float SampleOffset = 0.0f;
    float ScaffoldPlaybackScale = 1.0f;
    std::string Status;
    std::string Error;
};

NativeActorAnimationTimeSourceBinding ResolveNativeActorAnimationTimeSourceBinding(
    const nlohmann::json& profile, std::string_view csabName);
float ResolveNativeActorAnimationDirectSampleFrame(
    const NativeActorAnimationTimeSourceBinding& binding, float sourceFrame);
float ResolveNativeActorAnimationDirectSampleFrame(
    const NativeActorAnimationTimeSourceBinding& binding, float sourceFrame,
    float runtimeSourceStartFrame, float runtimeSourceEndFrame);
float ResolveNativeActorAnimationScaffoldPlaybackSpeed(
    const NativeActorAnimationTimeSourceBinding& binding, float playbackSpeed,
    float sourceStartFrame, float sourceEndFrame, bool playsOnce);

struct NativeActorAnimationSamplingBinding {
    bool Available = false;
    bool ContractPresent = false;
    CsabPoseSamplingPolicy Policy;
    std::array<float, 3> BaseTranslation = { 0.0f, 0.0f, 0.0f };
    std::string Status;
    std::string Error;
};

NativeActorAnimationSamplingBinding ResolveNativeActorAnimationSamplingBinding(
    const nlohmann::json& profile);

struct NativeActorAnimationBindingIdentity {
    std::string LookupName;
    std::string Value;
};

struct NativeActorAnimationBinding {
    bool Available = false;
    std::string CsabName;
    std::string ResourcePath;
    std::string LookupName;
    std::string LookupValue;
    std::string Status;
    std::string Error;
};

NativeActorAnimationBinding ResolveNativeActorAnimationBinding(
    const nlohmann::json& profile,
    const std::vector<NativeActorAnimationBindingIdentity>& identities);

struct NativeActorAnimationControllerBinding {
    bool Available = false;
    std::string ControllerId;
    std::string NotificationSemantic;
    std::string PhaseSource;
    float SourceFrameSpan = 0.0f;
    std::string BlendSource;
    float BlendThreshold = 0.0f;
    float BlendScale = 0.0f;
    std::string WarmupSource;
    std::string WalkCsabName;
    std::string RunCsabName;
    std::string Status;
    std::string Error;
};

NativeActorAnimationControllerBinding ResolveNativeActorAnimationControllerBinding(
    const nlohmann::json& profile, std::string_view notificationSemantic,
    uint32_t animationTypeIndex);

float ResolveNativeActorAnimationFrame(const CsabMetadata& animation,
                                       const NativeActorAnimationTimeInput& input);
float AdvanceNativeActorAnimationClock(const CsabMetadata& animation,
                                       const NativeActorAnimationTimeInput& input,
                                       NativeActorAnimationClockState& state);

struct NativeActorAnimationMorphTimeInput {
    float SourceWeight = 0.0f;
    float SourceRate = 0.0f;
    uint64_t SourceUpdateSerial = 0;
    bool SourceUpdateSerialValid = false;
    std::string BindingKey;
};

struct NativeActorAnimationMorphClockState {
    bool Initialized = false;
    std::string BindingKey;
    float PreviousSourceWeight = 0.0f;
    uint64_t PreviousSourceUpdateSerial = 0;
    float NativeWeight = 0.0f;
    uint64_t BindingResetCount = 0;
    uint64_t SourceRestartCount = 0;
    uint64_t SourceAdvanceCount = 0;
    uint64_t SourceHoldAdvanceCount = 0;
    uint64_t RenderHoldCount = 0;
    std::string Status;
};

float AdvanceNativeActorAnimationMorphClock(
    const NativeActorAnimationMorphTimeInput& input,
    NativeActorAnimationMorphClockState& state);

struct NativeActorFaceSample {
    bool Available = false;
    bool EyeSelected = false;
    bool MouthSelected = false;
    uint8_t EyeIndex = 0;
    uint8_t MouthIndex = 0;
    uint8_t HoldValue = 0xFF;
    uint32_t AnimationTypeLocalIndex = 0;
    bool MaterialAnimationSelectorAvailable = false;
    uint32_t MaterialAnimationTypeLocalIndex = 0;
    std::string SourceMember;
    std::string Status;
};

struct NativeActorFaceRuntimeState {
    bool Initialized = false;
    uint8_t EyeIndex = 0;
    uint8_t MouthIndex = 0;
};

NativeActorFaceSample ResolveNativeActorFaceState(const NativeActorFaceSample& sample,
                                                  NativeActorFaceRuntimeState& state);

struct NativeActorAnimationSpatialBinding {
    bool Available = false;
    bool SegmentContractPresent = false;
    bool SegmentMatched = false;
    int32_t RootMotionBone = -1;
    std::array<float, 3> SegmentNormalizationOffset = { 0.0f, 0.0f, 0.0f };
    std::string Status;
    std::string Error;
};

NativeActorAnimationSpatialBinding ResolveNativeActorAnimationSpatialBinding(
    const nlohmann::json& profile, std::string_view csabName);

struct NativeActorRootMotionOwnership {
    bool Available = false;
    bool ContractPresent = false;
    uint8_t MovementEnabledFlag = 0;
    uint8_t UpdateYFlag = 0;
    bool AlignXzWhenMovementEnabled = false;
    bool AlignYWhenMovementEnabledAndUpdateY = false;
    bool PreserveAuthoredRootRotation = false;
    std::string Status;
    std::string Error;
};

NativeActorRootMotionOwnership ResolveNativeActorRootMotionOwnership(
    const nlohmann::json& profile);

struct NativeActorRootMotionAlignment {
    bool Available = false;
    bool ControllerOwnsXz = false;
    bool ControllerOwnsY = false;
    std::array<float, 3> Translation = { 0.0f, 0.0f, 0.0f };
    std::string Status;
};

NativeActorRootMotionAlignment ResolveNativeActorRootMotionAlignment(
    const NativeActorRootMotionOwnership& ownership, uint8_t movementFlags,
    std::array<float, 3> consumedRootBaseTranslation,
    std::array<float, 3> nativeRootTranslation);

struct NativePlayerModelResourceState {
    uint8_t AgeIndex = 0;
    uint8_t ModelGroup = 0;
    uint8_t LeftHandType = 0;
    uint8_t RightHandType = 0;
    uint8_t SheathType = 0;
    uint8_t Shield = 0;
};

struct NativeActorResourceVisibility {
    bool Available = false;
    std::vector<uint8_t> ResourceVisibility;
    std::vector<uint8_t> ActiveResourceIds;
    uint8_t WaistType = 0;
    std::string Status;
    std::string Error;
};

struct NativeEnKoRuntimeBinding {
    bool Available = false;
    uint32_t Subtype = 0;
    uint32_t SemanticAnimationIndex = 0;
    uint32_t ModelClassIndex = 0;
    std::string ModelAssetId;
    std::string FaceModelAssetId;
    std::string AnimationAssetId;
    std::string AnimationMember;
    float ModelScale = 1.0f;
    float PlaybackSpeed = 1.0f;
    float StartFrame = 0.0f;
    float EndFrame = -1.0f;
    uint8_t PlaybackMode = 0;
    float MorphFrames = 0.0f;
    uint32_t FaceAnimationSelector = 0;
    uint32_t TorsoLimbIndex = 0;
    uint32_t HeadLimbIndex = 0;
    std::array<uint8_t, 4> TunicColor = { 255, 255, 255, 255 };
    std::array<uint8_t, 4> BootsColor = { 255, 255, 255, 255 };
    std::vector<uint8_t> ResourceVisibilityClearIds;
    std::string Status;
    std::string Error;
};

struct NativeActorPoseRotationOverride {
    uint32_t BoneIndex = 0;
    int16_t RotationX = 0;
    int16_t RotationY = 0;
};

struct NativeActorPoseMatrixOverride {
    uint32_t BoneIndex = 0;
    Matrix4f LocalPostTransform{};
};

bool ApplyNativeActorPoseRotationOverrides(
    const CmbSkeleton& skeleton, CsabPose& pose,
    const std::vector<NativeActorPoseRotationOverride>& overrides);
bool ApplyNativeActorPoseMatrixOverrides(
    const CmbSkeleton& skeleton, CsabPose& pose,
    const std::vector<NativeActorPoseMatrixOverride>& overrides);

NativeEnKoRuntimeBinding ResolveNativeEnKoRuntimeBinding(
    const nlohmann::json& contract, uint32_t subtype,
    uint32_t semanticAnimationIndex);
NativeActorResourceVisibility ResolveNativeEnKoResourceVisibility(
    const NativeEnKoRuntimeBinding& binding, const CmbModel& model);
size_t ApplyNativeActorModelOpacity(Oot3dNativeRenderModel& model,
                                    uint8_t alpha,
                                    std::string_view source);

NativeActorResourceVisibility ResolveNativePlayerModelResourceVisibility(
    const nlohmann::json& profile, const CmbModel& model,
    const NativePlayerModelResourceState& state);

class NativeActorRenderProvider {
  public:
    NativeActorRenderProvider(const AssetCatalog& catalog, NativeSourceProvider& sources);
    std::shared_ptr<const NativeActorRenderSource> Resolve(std::string_view modelAssetId);
    NativeActorAnimationSample SampleAnimation(const NativeActorRenderSource& actor,
                                               std::string_view animationAssetId,
                                               float frame) const;
    NativeActorAnimationSample SampleAnimation(const NativeActorRenderSource& actor,
                                               std::string_view animationAssetId,
                                               float frame,
                                               const CsabPoseSamplingPolicy& policy) const;
    NativeActorAnimationSample SampleAnimation(
        const NativeActorRenderSource& actor, std::string_view animationAssetId,
        const NativeActorAnimationTimeInput& time) const;
    NativeActorAnimationSample SampleAnimation(
        const NativeActorRenderSource& actor, std::string_view animationAssetId,
        const NativeActorAnimationTimeInput& time,
        NativeActorAnimationClockState& clock) const;
    NativeActorAnimationSample SampleAnimation(
        const NativeActorRenderSource& actor, std::string_view animationAssetId,
        const NativeActorAnimationTimeInput& time,
        NativeActorAnimationClockState& clock,
        const CsabPoseSamplingPolicy& policy) const;
    const AssetCatalogRecord* FindAnimation(const NativeActorRenderSource& actor,
                                            std::string_view memberName) const;
    NativeActorFaceSample SampleFace(const NativeActorRenderSource& actor,
                                    const AssetCatalogRecord& animation,
                                    float frame) const;
    NativeActorResourceVisibility ResolvePlayerResourceVisibility(
        const NativeActorRenderSource& actor, const NativePlayerModelResourceState& state);
    NativeEnKoRuntimeBinding ResolveEnKoRuntimeBinding(
        uint32_t subtype, uint32_t semanticAnimationIndex) const;
    Oot3dNativeRenderModel BuildPosedRenderModel(const NativeActorRenderSource& actor,
                                                 const CsabPose& pose,
                                                 const NativeActorFaceSample* face = nullptr,
                                                 const std::vector<uint8_t>* resourceVisibility = nullptr,
                                                 const NativeActorRenderSource* materialAnimationSource = nullptr) const;
    bool UpdatePosedRenderModel(Oot3dNativeRenderModel& model,
                                const NativeActorRenderSource& actor,
                                const CsabPose& pose,
                                const NativeActorFaceSample* face = nullptr,
                                const NativeActorRenderSource* materialAnimationSource = nullptr) const;
    void Clear();

  private:
    struct AnimationCacheEntry {
        bool MetadataReady = false;
        CsabMetadata Metadata;
        bool PoseReady = false;
        uint32_t SampledFrameBits = 0;
        CsabPose Pose;
    };

    std::shared_ptr<const NativeActorArchiveSource> ResolveArchive(std::string_view sourceContainer);
    CsabMetadata ResolveAnimationMetadata(std::string_view cacheKey,
                                          std::span<const uint8_t> bytes) const;

    const AssetCatalog& mCatalog;
    NativeSourceProvider& mSources;
    nlohmann::json mManifest;
    std::string mManifestError;
    bool mPlayerModelResourceProfileAttempted = false;
    nlohmann::json mPlayerModelResourceProfile;
    std::string mPlayerModelResourceProfileError;
    nlohmann::json mEnKoRuntimeContract;
    std::string mEnKoRuntimeContractError;
    mutable std::mutex mMutex;
    std::unordered_map<std::string, std::shared_ptr<const NativeActorArchiveSource>> mArchives;
    std::unordered_map<std::string, std::shared_ptr<const NativeActorRenderSource>> mModels;
    mutable std::unordered_map<std::string, AnimationCacheEntry> mAnimationCache;
};

} // namespace ThreeDsRecomp::Oot3d
