#pragma once

#include "oot3d_native_audio.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Oot3dNativeGame {

struct NativeAudioReverbLayout {
    uint32_t CodeBaseAddress = 0;
    uint32_t InitialDelayInstructionAddress = 0;
    uint32_t DecayTimeInstructionAddress = 0;
    uint32_t SecondaryDelayInstructionAddress = 0;
    uint32_t DiffusionLiteralAddress = 0;
    uint32_t DampingLiteralAddress = 0;
    uint32_t DelayTablePointerLiteralAddress = 0;
    uint32_t DelayTableInitializerPointerLiteralAddress = 0;
    std::array<uint32_t, 3> DelayTableValueInstructionAddresses{};
    uint32_t DryGainLiteralAddress = 0;
    uint32_t WetGainLiteralAddress = 0;
    uint32_t MinimumDelayLiteralAddress = 0;
    uint32_t DelayScaleLiteralAddress = 0;
    uint32_t MillisecondsScaleLiteralAddress = 0;
    uint32_t DecayExponentLiteralAddress = 0;
    uint32_t NativeSampleRateLiteralAddress = 0;
    uint32_t FeedbackPowerBaseLiteralAddress = 0;
    uint32_t CoefficientScaleLiteralAddress = 0;
    uint32_t DampingMaximumLiteralAddress = 0;
};

NativeAudioReverbLayout Oot3dEurRev0AudioReverbLayout();
NativeAudioReverbEffect ParseNativeAudioReverbEffect(
    std::span<const uint8_t> codeBin,
    const NativeAudioReverbLayout& layout);

struct NativeAudioSoundSpecLayout {
    uint32_t CodeBaseAddress = 0;
    uint32_t BuilderBeginAddress = 0;
    uint32_t BuilderEndAddress = 0;
    uint32_t DurationScaleInstructionAddress = 0;
    uint32_t DurationMaximumInstructionAddress = 0;
    uint32_t ChannelModeInstructionAddress = 0;
    uint32_t CommonFeedbackAddress = 0;
    uint32_t FeedbackMaximumAddress = 0;
    uint32_t DelayFrameMultiplierAddress = 0;
    uint32_t CoefficientScaleAddress = 0;
    uint16_t ProfileStride = 0;
    uint8_t ProfileCount = 0;
};

struct NativeAudioSoundSpecProfile {
    uint32_t DurationMilliseconds = 0;
    float InputFeedback = 0.0f;
    float CommonFeedback = 0.0f;
    NativeAudioAuxBusSettings AuxBus;
};

struct NativeAudioSoundSpecCatalog {
    std::vector<NativeAudioSoundSpecProfile> Profiles;
};

NativeAudioSoundSpecLayout Oot3dEurRev0AudioSoundSpecLayout();
NativeAudioSoundSpecCatalog ParseNativeAudioSoundSpecCatalog(
    std::span<const uint8_t> codeBin,
    const NativeAudioSoundSpecLayout& layout);

struct NativeAudioSceneLayout {
    uint32_t CodeBaseAddress = 0;
    uint32_t NatureProfileTableAddress = 0;
    uint32_t NatureSequenceSoundIdLiteralAddress = 0;
    uint32_t SpecialSequenceSoundIdLiteralAddress = 0;
    std::array<uint32_t, 16> NatureChannelSoundIdLiteralAddresses{};
    uint16_t NatureProfileStride = 0;
    uint8_t NatureProfileCount = 0;
    uint8_t NatureIoCapacity = 0;
};

struct NativeAudioNatureIo {
    uint8_t ChannelRange = 0;
    uint8_t Port = 0;
    uint8_t Value = 0;
};

struct NativeAudioNatureProfile {
    uint16_t PlayerIo = 0;
    uint16_t ChannelMask = 0;
    std::vector<NativeAudioNatureIo> ChannelIo;
};

NativeCseqInitialState BuildNativeNatureCseqInitialState(
    const NativeAudioNatureProfile& profile, uint8_t channel);

struct NativeAudioSceneProfile {
    uint32_t NatureSequenceSoundId = 0;
    uint32_t SpecialSequenceSoundId = 0;
    std::array<uint32_t, 16> NatureChannelSoundIds{};
    std::vector<NativeAudioNatureProfile> NatureProfiles;
};

NativeAudioSceneLayout Oot3dEurRev0AudioSceneLayout();
NativeAudioSceneProfile ParseNativeAudioSceneProfile(
    std::span<const uint8_t> codeBin,
    const NativeAudioSceneLayout& layout);

struct NativeAudioBehaviorLayout {
    uint32_t CodeBaseAddress = 0;
    uint32_t CategoryBaseTableAddress = 0;
    uint32_t BehaviorPointerTableAddress = 0;
    uint32_t CategoryVoiceLimitTableAddress = 0;
    uint32_t CategoryDisabledTableAddress = 0;
    uint32_t RandomStateAddress = 0;
    uint32_t RandomMultiplierLiteralAddress = 0;
    uint32_t RandomPitchStepLiteralAddress = 0;
    uint32_t RandomPitchBaseLiteralAddress = 0;
    uint8_t CategoryCount = 0;
};

NativeAudioBehaviorLayout Oot3dEurRev0AudioBehaviorLayout();

struct NativeAudioSpatialLayout {
    uint32_t CodeBaseAddress = 0;
    uint32_t PanBaseAngleAddress = 0;
    uint32_t PanFrontAngleAddress = 0;
    uint32_t PanRearAngleAddress = 0;
    uint32_t SurroundOffsetAddress = 0;
    uint32_t PriorityScaleInstructionAddress = 0;
    uint32_t PanStrengthAddress = 0;
    uint32_t DopplerBaseAddress = 0;
    uint32_t ListenerUnitAddress = 0;
    uint32_t ListenerHalfAddress = 0;
    uint32_t DopplerScaleAddress = 0;
    uint32_t EnvironmentAuxAAddress = 0;
    uint32_t EnvironmentAuxBAddress = 0;
    uint32_t AuxScaleAddress = 0;
    uint32_t AuxNormalizationAddress = 0;
    uint32_t AuxMaximumAddress = 0;
};

NativeAudioSpatialLayout Oot3dEurRev0AudioSpatialLayout();

struct NativeAudioSpatialProfile {
    float PanBaseAngle = 0.0f;
    float PanFrontAngle = 0.0f;
    float PanRearAngle = 0.0f;
    float SurroundOffset = 0.0f;
    int32_t PriorityScale = 0;
    float PanStrength = 0.0f;
    float DopplerBase = 0.0f;
    float PanDistance = 0.0f;
    float ReferenceDistance = 0.0f;
    float DistanceScale = 0.0f;
    float DistanceFilterScale = 0.0f;
    float DistanceFilterMaximum = 0.0f;
    float DopplerScale = 0.0f;
    int32_t InitialEnvironmentAuxA = 0;
    int32_t InitialEnvironmentAuxB = 0;
    float AuxScale = 0.0f;
    float AuxNormalization = 0.0f;
    float AuxMaximum = 0.0f;
};

struct NativeAudioSpatialResult {
    float Gain = 1.0f;
    int32_t PriorityAdjustment = 0;
    float DistanceFilter = 0.0f;
    float Pan = 0.0f;
    float SurroundPan = 0.0f;
    float AuxSend = 0.0f;
};

NativeAudioSpatialProfile ParseNativeAudioSpatialProfile(
    std::span<const uint8_t> codeBin,
    const NativeAudioSpatialLayout& layout);
NativeAudioSpatialResult CalculateNativeAudioDistanceParameters(
    const NativeAudioSpatialProfile& profile,
    const NativeBcsarSound3dInfo& sound3d, float distance);
NativeAudioSpatialResult CalculateNativeAudioPanParameters(
    const NativeAudioSpatialProfile& profile,
    const std::array<float, 3>& listenerSpacePosition);
float CalculateNativeAudioAuxSend(const NativeAudioSpatialProfile& profile,
                                  uint8_t mode, int8_t requestAux,
                                  int32_t environmentAuxA,
                                  int32_t environmentAuxB);

struct NativeAudioBehaviorEntry {
    uint32_t SoundId = 0;
    uint8_t Category = 0;
    uint16_t Parameter = 0;
    uint16_t Flags = 0;
};

struct NativeAudioBehaviorCategory {
    uint32_t FirstSoundId = 0;
    uint8_t VoiceThreshold = 0;
    bool Disabled = false;
};

struct NativeAudioBehaviorCatalog {
    std::vector<NativeAudioBehaviorCategory> Categories;
    std::unordered_map<uint32_t, NativeAudioBehaviorEntry> Entries;
    uint32_t InitialRandomState = 0;
    uint32_t RandomMultiplier = 0;
    float RandomPitchStep = 0.0f;
    float RandomPitchBase = 1.0f;

    const NativeAudioBehaviorEntry* Resolve(uint32_t soundId) const;
};

NativeAudioBehaviorCatalog ParseNativeAudioBehaviorCatalog(
    std::span<const uint8_t> codeBin,
    const NativeAudioBehaviorLayout& layout,
    uint32_t soundCount);

// Mirrors the public Audio_PlaySoundGeneral ABI recovered from code.bin. The
// final three fields preserve the pointed-to native values without assigning
// DSP semantics that the lower native audio closure has not established yet.
struct NativeAudioRequest {
    uint32_t SoundId = 0;
    uint64_t OwnerId = 0;
    uint64_t PositionIdentity = 0;
    bool HasPosition = false;
    std::array<float, 3> Position{};
    uint8_t Mode = 4;
    float Parameter4 = 1.0f;
    float Parameter5 = 1.0f;
    int8_t Parameter6 = 0;
};

struct NativeAudioServiceStats {
    size_t CachedSequenceCount = 0;
    size_t CachedStreamCount = 0;
    size_t ActiveSoundCount = 0;
    size_t ActiveSequencePlayerCount = 0;
    size_t ActiveNatureChannelCount = 0;
    size_t ActiveStreamPlayerCount = 0;
    uint64_t SubmittedSoundRequestCount = 0;
    uint64_t StartedSoundCount = 0;
    uint64_t RefreshedSoundCount = 0;
    size_t BehaviorEntryCount = 0;
    uint64_t RejectedSoundCount = 0;
    uint64_t PreemptedSoundCount = 0;
    size_t ActiveMixedVoiceCount = 0;
    size_t MaximumMixedVoiceCount = 0;
    uint64_t MixedVoiceSampleCount = 0;
    size_t CachedSequenceEventCount = 0;
    size_t CachedSequenceTickZeroEventCount = 0;
    size_t MaximumSequenceEventsAtOneTick = 0;
};

class NativeAudioService {
  public:
    NativeAudioService(std::filesystem::path soundArchivePath,
                       std::optional<std::filesystem::path> streamArchivePath = {},
                       uint32_t outputSampleRate = 44100);

    bool PlaySoundGeneral(const NativeAudioRequest& request);
    bool PlayActorSound2(uint64_t actorOwnerId, uint32_t soundId,
                         const std::array<float, 3>& position);
    bool ConsumeActorSfxRequest(uint64_t actorOwnerId, uint32_t& sfxRequest,
                                const std::array<float, 3>& position);

    bool QueueSequencePlayer(uint8_t playerIndex, uint32_t soundId,
                             uint32_t fadeInFrames = 0);
    bool QueueStreamPlayer(uint8_t playerIndex, uint32_t soundId,
                           uint8_t volume = 127, uint8_t pan = 64);
    bool SetSequencePlayerVolume(uint8_t playerIndex, uint8_t layer,
                                 uint8_t volume, uint32_t fadeFrames = 0);
    bool SetSequenceTrackEnabled(uint8_t playerIndex, uint8_t channelRange,
                                 bool enabled);
    bool ApplySceneAudio(uint8_t soundSpecId, uint8_t natureAmbienceId,
                         uint32_t bgmSoundId);
    bool StartNatureAmbience(uint8_t natureAmbienceId);
    bool SetNatureChannelIo(uint8_t channelRange, uint8_t port,
                            uint8_t value);
    std::optional<double> SequencePlayerGain(uint8_t playerIndex) const;
    void StopSequencePlayer(uint8_t playerIndex,
                            uint32_t fadeOutFrames = 0);
    void StopStreamPlayer(uint8_t playerIndex,
                          uint32_t fadeOutFrames = 0);
    void StopOwner(uint64_t ownerId);
    void StopAll();

    void MountBehaviorCatalog(const std::filesystem::path& codeBinPath,
                              const NativeAudioBehaviorLayout& layout);
    void MountEnvelopeProfile(const std::filesystem::path& codeBinPath,
                              const NativeAudioEnvelopeLayout& layout);
    void MountModulationProfile(const std::filesystem::path& codeBinPath,
                                const NativeAudioModulationLayout& layout);
    void MountSpatialProfile(const std::filesystem::path& codeBinPath,
                             const NativeAudioSpatialLayout& layout);
    void MountMixProfile(const std::filesystem::path& codeBinPath,
                         const NativeAudioMixLayout& layout);
    void MountFilterProfile(const std::filesystem::path& codeBinPath,
                            const NativeAudioFilterLayout& layout);
    void MountResamplerProfile(const std::filesystem::path& codeBinPath);
    void MountReverbProfile(const std::filesystem::path& codeBinPath,
                            const NativeAudioReverbLayout& layout);
    void MountSceneProfile(const std::filesystem::path& codeBinPath,
                           const NativeAudioSceneLayout& layout);
    void MountSoundSpecCatalog(const std::filesystem::path& codeBinPath,
                               const NativeAudioSoundSpecLayout& layout);
    void SetOutputMode(NativeAudioOutputMode outputMode);
    void SetListener(const std::array<float, 3>& position,
                     const std::array<float, 3>& target,
                     const std::array<float, 3>& up);
    void UpdateOwnerPosition(uint64_t ownerId,
                             const std::array<float, 3>& position);
    void SetEnvironmentAux(int32_t auxA, int32_t auxB);
    void SetBehaviorRandomState(uint32_t state);
    void ConfigureAuxBus(size_t bus,
                         const NativeAudioAuxBusSettings& settings);
    const NativeAudioAuxBusSettings& AuxBusSettings(size_t bus) const;

    void SetOutputSampleRate(uint32_t sampleRate);
    std::vector<int16_t> MixStereo(size_t frameCount);
    NativeAudioServiceStats Stats() const;
    bool IsSoundActive(uint64_t ownerId, uint64_t positionIdentity,
                       uint32_t soundId) const;
    std::optional<double> SoundPitchScale(uint64_t positionIdentity,
                                          uint32_t soundId) const;
    std::optional<double> SoundGainScale(uint64_t positionIdentity,
                                         uint32_t soundId) const;

    const NativeBcsarCatalog& SoundCatalog() const;
    const NativeBcsarCatalog* StreamCatalog() const;
    const NativeAudioBehaviorCatalog* BehaviorCatalog() const;
    const NativeAudioEnvelopeProfile* EnvelopeProfile() const;
    const NativeAudioModulationProfile* ModulationProfile() const;
    const NativeAudioSpatialProfile* SpatialProfile() const;
    const NativeAudioMixProfile* MixProfile() const;
    const NativeAudioFilterProfile* FilterProfile() const;
    const NativeAudioReverbEffect* ReverbProfile() const;
    const NativeAudioSceneProfile* SceneProfile() const;
    const NativeAudioSoundSpecCatalog* SoundSpecCatalog() const;
    uint8_t CurrentSoundSpecId() const;
    std::optional<uint8_t> CurrentNatureAmbienceId() const;

  private:
    struct Archive {
        std::filesystem::path Path;
        std::vector<uint8_t> Bytes;
        NativeBcsarCatalog Catalog;
    };

    struct SoundKey {
        uint64_t PositionIdentity = 0;
        uint32_t SoundId = 0;

        bool operator==(const SoundKey&) const = default;
    };

    struct SoundKeyHash {
        size_t operator()(const SoundKey& key) const noexcept;
    };

    struct ActiveSound {
        uint64_t MixerVoiceId = 0;
        NativeAudioRequest Request;
        uint8_t Category = 0;
        uint16_t Priority = 0;
        uint16_t Flags = 0;
        double PitchScale = 1.0;
        double SpatialGain = 1.0;
        double SpatialPan = 0.0;
        double SpatialSurroundPan = 0.0;
        double DistanceFilter = 0.0;
        double SpatialAuxSend = 0.0;
    };

    struct ActivePlayer {
        uint64_t MixerVoiceId = 0;
        uint32_t SoundId = 0;
        uint32_t FadeInFrames = 0;
        uint8_t Volume = 127;
        uint8_t Pan = 64;
        uint8_t Priority = 0;
    };

    struct NatureChannelPlayback {
        uint64_t MixerVoiceId = 0;
        uint32_t SoundId = 0;
        NativeCseqInitialState CycleState;
        NativeCseqInitialState LiveState;
        uint64_t ObservedLoopCount = 0;
    };

    static Archive LoadArchive(const std::filesystem::path& path,
                               const char* role);
    std::shared_ptr<const NativeAudioSequence> ResolveSequence(
        uint32_t soundId,
        const NativeCseqInitialState* initialState = nullptr);
    std::shared_ptr<const NativeBcwav> ResolveStream(uint32_t soundId);
    uint64_t AllocateMixerVoiceId();
    void PruneCompleted();
    void RestartActivePlayback();
    double NextBehaviorPitchScale(uint16_t flags);
    NativeAudioSpatialResult SpatialParameters(
        const NativeAudioRequest& request) const;
    void RefreshActivePlayback(ActiveSound& active);
    bool ApplyNativeVoicePolicy(const SoundKey& key,
                                const NativeAudioBehaviorEntry& behavior,
                                uint8_t priority);
    bool ApplyNativeSoundPlayerPolicy(uint32_t soundId, uint8_t priority,
                                      uint64_t excludedMixerVoiceId = 0);
    bool ApplyNativeStreamPlayerPolicy(uint32_t soundId, uint8_t priority,
                                       uint64_t excludedMixerVoiceId = 0);
    bool ApplySoundSpec(uint8_t soundSpecId);
    void StopNatureChannels(uint32_t fadeOutFrames = 0);

    Archive mSoundArchive;
    std::optional<Archive> mStreamArchive;
    std::optional<NativeAudioBehaviorCatalog> mBehaviorCatalog;
    std::shared_ptr<const NativeAudioEnvelopeProfile> mEnvelopeProfile;
    std::shared_ptr<const NativeAudioModulationProfile> mModulationProfile;
    std::optional<NativeAudioSpatialProfile> mSpatialProfile;
    std::shared_ptr<const NativeAudioMixProfile> mMixProfile;
    std::shared_ptr<const NativeAudioFilterProfile> mFilterProfile;
    std::shared_ptr<const NativeAudioResamplerProfile> mResamplerProfile;
    std::optional<NativeAudioReverbEffect> mReverbProfile;
    std::optional<NativeAudioSceneProfile> mSceneProfile;
    std::optional<NativeAudioSoundSpecCatalog> mSoundSpecCatalog;
    NativeAudioOutputMode mOutputMode = NativeAudioOutputMode::Stereo;
    std::array<float, 3> mListenerPosition{};
    std::array<float, 3> mListenerRight{1.0f, 0.0f, 0.0f};
    std::array<float, 3> mListenerUp{0.0f, 1.0f, 0.0f};
    std::array<float, 3> mListenerForward{0.0f, 0.0f, -1.0f};
    int32_t mEnvironmentAuxA = 0;
    int32_t mEnvironmentAuxB = 0;
    bool mListenerValid = false;
    std::array<NativeAudioAuxBusSettings, 2> mAuxBusSettings;
    uint32_t mOutputSampleRate = 44100;
    std::unique_ptr<NativeAudioMixer> mMixer;
    uint64_t mNextMixerVoiceId = 1;
    uint32_t mBehaviorRandomState = 0;
    std::unordered_map<uint32_t, std::shared_ptr<const NativeAudioSequence>>
        mSequenceCache;
    std::unordered_map<uint32_t, std::shared_ptr<const NativeBcwav>> mStreamCache;
    std::unordered_map<SoundKey, ActiveSound, SoundKeyHash> mActiveSounds;
    std::unordered_map<uint8_t, ActivePlayer> mSequencePlayers;
    std::unordered_map<uint8_t, ActivePlayer> mStreamPlayers;
    uint8_t mCurrentSoundSpecId = 0xff;
    std::optional<uint8_t> mCurrentNatureAmbienceId;
    std::unordered_map<uint8_t, NatureChannelPlayback> mNatureChannels;
    bool mNatureRuntimeStateAvailable = false;
    uint64_t mSubmittedSoundRequestCount = 0;
    uint64_t mStartedSoundCount = 0;
    uint64_t mRefreshedSoundCount = 0;
    uint64_t mRejectedSoundCount = 0;
    uint64_t mPreemptedSoundCount = 0;
};

} // namespace Oot3dNativeGame
