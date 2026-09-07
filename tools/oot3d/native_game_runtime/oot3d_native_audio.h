#pragma once

#include "oot3d_native_audio_dsp.h"
#include "oot3d_native_audio_resampler.h"
#include "oot3d_native_cseq.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Oot3dNativeGame {

constexpr uint32_t kNativeAudioControlFrameMilliseconds = 5;

enum class NativeAudioCodec : uint8_t {
    Pcm8 = 0,
    Pcm16 = 1,
    DspAdpcm = 2,
    ImaAdpcm = 3,
};

struct NativeBcwav {
    NativeAudioCodec Codec = NativeAudioCodec::Pcm8;
    uint32_t SampleRate = 0;
    uint32_t LoopStart = 0;
    uint32_t SampleCount = 0;
    bool Looping = false;
    uint16_t ChannelCount = 0;
    std::vector<int16_t> InterleavedSamples;
};

struct NativeBcsarFileEntry {
    bool Internal = false;
    uint32_t Offset = 0;
    uint32_t Size = 0;
    std::string Path;
};

struct NativeBcsarSound3dInfo {
    uint32_t Flags = 0;
    float DecayRatio = 1.0f;
    uint8_t DecayCurve = 1;
    uint8_t DopplerFactor = 0;
};

struct NativeBcsarSoundEntry {
    uint32_t Index = 0;
    uint32_t FileId = 0;
    uint32_t PlayerReference = 0;
    uint8_t ArchiveVolume = 127;
    uint8_t RemoteFilter = 0;
    uint16_t Type = 0;
    uint32_t NameId = 0;
    std::string Name;
    uint32_t OptionMask = 0;
    std::array<std::optional<uint32_t>, 32> Options;
    std::optional<NativeBcsarSound3dInfo> Sound3d;
    uint32_t SequenceOffset = 0;
    std::vector<uint8_t> BankIds;

    std::optional<uint32_t> Option(uint8_t bit) const;
    uint8_t PanMode() const;
    uint8_t PanCurve() const;
    uint8_t Priority() const;
    bool Persistent() const;
    std::optional<uint32_t> PlayerIndex() const;
};

struct NativeBcsarPlayerEntry {
    uint32_t Index = 0;
    uint32_t PlayableSoundLimit = 0;
    uint32_t OptionMask = 0;
    std::array<std::optional<uint32_t>, 32> Options;
    uint32_t NameId = 0xffffffff;
    std::string Name;

    std::optional<uint32_t> Option(uint8_t bit) const;
};

struct NativeBcsarNamedFileEntry {
    uint32_t FileId = 0;
    uint32_t NameId = 0xffffffff;
    std::string Name;
};

struct NativeBcsarCatalog {
    uint32_t FileSectionOffset = 0;
    std::vector<std::string> Strings;
    std::vector<NativeBcsarFileEntry> Files;
    std::vector<NativeBcsarSoundEntry> Sounds;
    std::vector<NativeBcsarNamedFileEntry> Banks;
    std::vector<NativeBcsarNamedFileEntry> WaveArchives;
    std::vector<NativeBcsarPlayerEntry> Players;

    const NativeBcsarSoundEntry* ResolveSoundId(uint32_t soundId) const;
    const NativeBcsarPlayerEntry* ResolvePlayerReference(
        uint32_t playerReference) const;
    std::span<const uint8_t> EmbeddedFile(std::span<const uint8_t> archive,
                                          uint32_t fileId) const;
};

struct NativeAudioCue {
    uint32_t SoundId = 0;
    std::string Name;
    uint8_t Program = 0;
    uint8_t Note = 0;
    uint8_t Velocity = 0;
    uint32_t DurationTicks = 0;
    uint8_t BankId = 0;
    uint32_t WaveArchiveIndex = 0;
    uint32_t WaveIndex = 0;
    uint32_t RootKey = 0;
    uint32_t Volume = 0;
    float Pitch = 1.0f;
    int8_t SurroundPan = 0;
    uint8_t InterpolationType = 0;
    bool IgnoreNoteOff = false;
    uint8_t ArchiveVolume = 127;
    uint8_t RemoteFilter = 0;
    uint8_t PanMode = 0;
    uint8_t PanCurve = 0;
    uint32_t Pan = 0;
    uint8_t BankSlot = 0;
    uint8_t TrackVolume = 127;
    uint8_t Expression = 127;
    uint8_t TrackPan = 64;
    uint8_t MasterVolume = 127;
    int8_t Transpose = 0;
    int8_t PitchBend = 0;
    uint8_t PitchBendRange = 2;
    uint8_t VelocityRange = 127;
    int8_t BiquadType = 0;
    int8_t BiquadValue = 0;
    int8_t ModPhase = 0;
    uint8_t ModCurve = 0;
    int8_t ModDepth = 0;
    int8_t ModSpeed = 16;
    uint8_t ModType = 0;
    int8_t ModRange = 1;
    int16_t ModDelay = 0;
    uint16_t ModPeriod = 0;
    int8_t PortamentoKey = 0;
    bool PortamentoEnabled = false;
    uint8_t PortamentoTime = 0;
    int16_t SweepPitch = 0;
    int8_t TrackSurroundPan = 0;
    int8_t LpfCutoff = 64;
    int8_t FxSendA = 0;
    int8_t FxSendB = 0;
    int8_t FxSendC = 0;
    int8_t MainSend = 127;
    int8_t InitialPan = 64;
    bool Damper = false;
    uint8_t Attack = 0;
    uint8_t Decay = 0;
    uint8_t Sustain = 0;
    uint8_t Hold = 0;
    uint8_t Release = 0;
    std::shared_ptr<const NativeBcwav> Wave;
};

struct NativeAudioSequenceEvent {
    uint64_t StartTick = 0;
    uint32_t DurationTicks = 0;
    uint8_t Track = 0;
    bool Silent = false;
    bool Tie = false;
    NativeAudioCue Cue;
};

struct NativeAudioSequence {
    uint32_t SoundId = 0;
    std::string Name;
    uint16_t Timebase = 48;
    uint16_t InitialTempo = 120;
    uint64_t EndTick = 0;
    uint64_t LoopStartTick = 0;
    bool Looping = false;
    bool ControlOnly = false;
    uint16_t InitialTrackEnableMask = 0xffff;
    std::bitset<256> CseqCommandUsage;
    std::bitset<256> CseqExtendedCommandUsage;
    std::bitset<4> CseqTimeModifierUsage;
    std::array<std::bitset<256>, 4> CseqTimedCommandUsage;
    std::array<uint32_t, 2> CseqTieCommandCount{};
    std::vector<NativeCseqTempoEvent> TempoEvents;
    std::vector<NativeCseqControlEvent> ControlEvents;
    std::vector<NativeAudioSequenceEvent> Events;
};

struct NativeAudioEnvelopeLayout {
    uint32_t CodeBaseAddress = 0;
    uint32_t FloorDbAddress = 0;
    uint32_t SustainTableAddress = 0;
    uint32_t AttackTableAddress = 0;
    uint32_t GainTableAddress = 0;
    uint32_t DecayImmediateAddress = 0;
    uint32_t DecayNearImmediateAddress = 0;
    uint32_t DecayScaleAddress = 0;
    uint32_t DecayLowSlopeAddress = 0;
    uint32_t DecayHighNumeratorAddress = 0;
    uint32_t ReleaseImmediateAddress = 0;
    uint32_t ReleaseNearImmediateAddress = 0;
    uint32_t ReleaseScaleAddress = 0;
    uint32_t ReleaseLowSlopeAddress = 0;
    uint32_t ReleaseHighNumeratorAddress = 0;
    uint32_t LevelToDbScaleAddress = 0;
    uint32_t GainMinDbAddress = 0;
    uint32_t GainMaxDbAddress = 0;
    uint32_t GainIndexScaleAddress = 0;
    uint32_t GainTableCount = 0;
};

NativeAudioEnvelopeLayout Oot3dEurRev0AudioEnvelopeLayout();

struct NativeAudioEnvelopeRateProfile {
    float Immediate = 0.0f;
    float NearImmediate = 0.0f;
    float Scale = 0.0f;
    float LowSlope = 0.0f;
    float HighNumerator = 0.0f;
};

struct NativeAudioEnvelopeProfile {
    float FloorDb = 0.0f;
    float LevelToDbScale = 0.0f;
    float GainMinDb = 0.0f;
    float GainMaxDb = 0.0f;
    float GainIndexScale = 0.0f;
    NativeAudioEnvelopeRateProfile Decay;
    NativeAudioEnvelopeRateProfile Release;
    std::array<int16_t, 128> SustainLevels{};
    std::array<float, 128> AttackMultipliers{};
    std::vector<float> GainCurve;

    uint16_t HoldTicks(uint8_t code) const;
    float DecayStep(uint8_t code) const;
    float ReleaseStep(uint8_t code) const;
    float GainForLevel(float level) const;
    float GainForDb(float db) const;
};

NativeAudioEnvelopeProfile ParseNativeAudioEnvelopeProfile(
    std::span<const uint8_t> codeBin,
    const NativeAudioEnvelopeLayout& layout);

struct NativeAudioModulationLayout {
    uint32_t CodeBaseAddress = 0;
    uint32_t PhaseScaleAddress = 0;
    uint32_t SineTablePointerAddress = 0;
    uint32_t OutputScaleAddress = 0;
    uint32_t PhaseAdvanceScaleAddress = 0;
    uint32_t UpdateMillisecondsInstructionAddress = 0;
    uint32_t SineQuarterTableCount = 0;
};

NativeAudioModulationLayout Oot3dEurRev0AudioModulationLayout();

struct NativeAudioModulationProfile {
    float PhaseScale = 0.0f;
    float OutputScale = 0.0f;
    float PhaseAdvanceScale = 0.0f;
    uint32_t UpdateMilliseconds = 0;
    std::vector<int8_t> SineQuarterTable;

    float SampleSine(float phase) const;
};

NativeAudioModulationProfile ParseNativeAudioModulationProfile(
    std::span<const uint8_t> codeBin,
    const NativeAudioModulationLayout& layout);

struct NativeAudioMixLayout {
    uint32_t CodeBaseAddress = 0;
    uint32_t StandardPanCurvePointerTableAddress = 0;
    uint32_t AlternatePanCurvePointerTableAddress = 0;
    uint32_t PanScaleAddress = 0;
    uint32_t SurroundLowerScaleAddress = 0;
    uint32_t SurroundUpperScaleAddress = 0;
};

NativeAudioMixLayout Oot3dEurRev0AudioMixLayout();

struct NativeAudioMixProfile {
    std::array<std::array<float, 257>, 3> StandardPanCurves{};
    std::array<std::array<float, 257>, 3> AlternatePanCurves{};
    float PanScale = 0.0f;
    float SurroundLowerScale = 0.0f;
    float SurroundUpperScale = 0.0f;
};

enum class NativeAudioOutputMode : uint8_t {
    Mono = 0,
    Stereo = 1,
    Surround = 2,
};

struct NativeAudioChannelGains {
    float FrontLeft = 0.0f;
    float FrontRight = 0.0f;
    float RearLeft = 0.0f;
    float RearRight = 0.0f;
};

NativeAudioMixProfile ParseNativeAudioMixProfile(
    std::span<const uint8_t> codeBin,
    const NativeAudioMixLayout& layout);
float NormalizeNativeAudioPan(uint8_t pan,
                              const NativeAudioMixProfile& profile);
float NormalizeNativeAudioSurroundPan(
    uint8_t surroundPan, const NativeAudioMixProfile& profile);
float CalculateNativeAudioPanGain(const NativeAudioMixProfile& profile,
                                  uint8_t panCurve, float pan,
                                  bool alternateCurves = false);
float CalculateNativeAudioSurroundGain(
    const NativeAudioMixProfile& profile, uint8_t panCurve,
    float surroundPan);
NativeAudioChannelGains CalculateNativeAudioChannelGains(
    const NativeAudioMixProfile& profile, NativeAudioOutputMode outputMode,
    uint16_t sourceChannelCount, uint16_t sourceChannel,
    uint8_t panMode, uint8_t panCurve, float pan, float surroundPan);

struct NativeAudioFilterLayout {
    uint32_t CodeBaseAddress = 0;
    uint32_t InputMinimumAddress = 0;
    uint32_t InputMaximumAddress = 0;
    uint32_t MinimumCutoffThresholdAddress = 0;
    uint32_t MaximumCutoffThresholdAddress = 0;
    uint32_t CutoffLookupBaseAddress = 0;
    uint32_t CutoffLookupScaleAddress = 0;
    uint32_t CutoffLookupPointerAddress = 0;
    uint32_t FrequencyScaleAddress = 0;
    uint32_t FilterOffsetAddress = 0;
    uint32_t FilterUnitAddress = 0;
    uint32_t CoefficientScaleAddress = 0;
    uint32_t CosinePeriodAddress = 0;
    uint32_t CosineLookupPointerAddress = 0;
    uint32_t CutoffLookupCount = 0;
    uint32_t CosineLookupCount = 0;
    uint32_t CosineLookupStride = 0;
};

NativeAudioFilterLayout Oot3dEurRev0AudioFilterLayout();

struct NativeAudioCosineEntry {
    float Value = 0.0f;
    float Slope = 0.0f;
};

struct NativeAudioFilterProfile {
    float InputMinimum = 0.0f;
    float InputMaximum = 0.0f;
    float MinimumCutoffThreshold = 0.0f;
    float MaximumCutoffThreshold = 0.0f;
    float CutoffLookupBase = 0.0f;
    float CutoffLookupScale = 0.0f;
    float FrequencyScale = 0.0f;
    float FilterOffset = 0.0f;
    float FilterUnit = 0.0f;
    float CoefficientScale = 0.0f;
    float CosinePeriod = 0.0f;
    std::vector<uint16_t> CutoffFrequencies;
    std::vector<NativeAudioCosineEntry> CosineLookup;
};

struct NativeAudioSimpleFilterCoefficients {
    int16_t B0 = 0;
    int16_t A1 = 0;
};

NativeAudioFilterProfile ParseNativeAudioFilterProfile(
    std::span<const uint8_t> codeBin,
    const NativeAudioFilterLayout& layout);
uint16_t CalculateNativeAudioCutoffFrequency(
    const NativeAudioFilterProfile& profile, float cutoff);
NativeAudioSimpleFilterCoefficients CalculateNativeAudioSimpleFilter(
    const NativeAudioFilterProfile& profile, uint16_t frequency);

NativeBcwav DecodeNativeBcwav(std::span<const uint8_t> bytes);
NativeBcwav DecodeNativeBcwavFile(const std::filesystem::path& path);
NativeBcwav DecodeNativeBcstm(std::span<const uint8_t> bytes);
NativeBcwav DecodeNativeBcstmFile(const std::filesystem::path& path);
NativeBcsarCatalog ParseNativeBcsar(std::span<const uint8_t> bytes);
NativeBcsarCatalog ParseNativeBcsarFile(const std::filesystem::path& path);
NativeAudioCue ResolveNativeAudioCue(std::span<const uint8_t> archive,
                                     const NativeBcsarCatalog& catalog,
                                     uint32_t soundId);
NativeAudioSequence ResolveNativeAudioSequence(std::span<const uint8_t> archive,
                                               const NativeBcsarCatalog& catalog,
                                               uint32_t soundId,
                                               uint32_t randomSeed = 0,
                                               const NativeCseqInitialState* initialState = nullptr);
NativeBcwav ResolveNativeAudioStream(const std::filesystem::path& archivePath,
                                     const NativeBcsarCatalog& catalog,
                                     uint32_t soundId);
uint32_t ResolveNativeCodeU32TableEntry(std::span<const uint8_t> codeBin,
                                        uint32_t codeBaseAddress,
                                        uint32_t tableAddress,
                                        uint32_t index);

class NativeAudioMixer {
  public:
    explicit NativeAudioMixer(uint32_t outputSampleRate = 44100);

    void SetEnvelopeProfile(
        std::shared_ptr<const NativeAudioEnvelopeProfile> profile);
    void SetModulationProfile(
        std::shared_ptr<const NativeAudioModulationProfile> profile);
    void SetMixProfile(std::shared_ptr<const NativeAudioMixProfile> profile);
    void SetFilterProfile(
        std::shared_ptr<const NativeAudioFilterProfile> profile);
    void SetResamplerProfile(
        std::shared_ptr<const NativeAudioResamplerProfile> profile);
    void SetOutputMode(NativeAudioOutputMode outputMode);
    void ConfigureAuxBus(size_t bus,
                         const NativeAudioAuxBusSettings& settings);
    const NativeAudioAuxBusSettings& AuxBusSettings(size_t bus) const;

    void StartOrRefreshVoice(uint64_t ownerId,
                             std::shared_ptr<const NativeAudioCue> cue);
    void StartOrRefreshStream(uint64_t ownerId,
                              std::shared_ptr<const NativeBcwav> stream,
                              uint8_t volume = 127, uint8_t pan = 64);
    void StartOrRefreshSequence(uint64_t ownerId,
                                std::shared_ptr<const NativeAudioSequence> sequence,
                                double pitchScale = 1.0,
                                double gainScale = 1.0,
                                double spatialPan = 0.0,
                                double spatialSurroundPan = 0.0,
                                double distanceFilter = 0.0,
                                double spatialAuxSend = 0.0,
                                uint32_t fadeInFrames = 0);
    bool SetSequencePlayerVolume(uint64_t ownerId, uint8_t layer,
                                 uint8_t volume, uint32_t fadeFrames = 0);
    bool SetSequenceTrackEnabled(uint64_t ownerId, uint8_t firstTrack,
                                 uint8_t lastTrack, bool enabled);
    bool ReconfigureSequence(
        uint64_t ownerId,
        std::shared_ptr<const NativeAudioSequence> currentCycle,
        std::shared_ptr<const NativeAudioSequence> nextLoop);
    std::optional<double> SequencePlayerGain(uint64_t ownerId) const;
    std::optional<uint64_t> SequenceTimelineTick(uint64_t ownerId) const;
    std::optional<uint64_t> SequenceLoopCount(uint64_t ownerId) const;
    void StopVoice(uint64_t ownerId, uint32_t fadeOutFrames = 0);
    std::vector<int16_t> MixStereo(size_t frameCount);
    size_t ActiveVoiceCount() const;
    size_t ActiveMixedVoiceCount() const;
    uint64_t MixedVoiceSampleCount() const;
    size_t MaximumMixedVoiceCount() const;
    bool IsVoiceActive(uint64_t ownerId) const;
    void StopAll();

  private:
    enum class EnvelopePhase : uint8_t {
        Attack = 0,
        Hold = 1,
        Decay = 2,
        Sustain = 3,
        Release = 4,
    };

    struct EnvelopeState {
        EnvelopePhase Phase = EnvelopePhase::Attack;
        float CurrentLevel = 0.0f;
        float DecayStep = 0.0f;
        float ReleaseStep = 0.0f;
        float AttackMultiplier = 0.0f;
        uint16_t HoldTicks = 0;
        uint16_t HoldRemaining = 0;
        uint8_t Sustain = 0;
        double FramesUntilAdvance = 0.0;
        bool Enabled = false;
    };

    struct ModulationState {
        float Phase = 0.0f;
        float Value = 0.0f;
        uint32_t DelayMilliseconds = 0;
        uint32_t ElapsedMilliseconds = 0;
        double FramesUntilAdvance = 0.0;
        bool Enabled = false;
    };

    struct PortamentoState {
        float StartPitch = 0.0f;
        float CurrentPitch = 0.0f;
        uint32_t DurationMilliseconds = 0;
        uint32_t ElapsedMilliseconds = 0;
        bool Enabled = false;
    };

    struct Voice {
        std::shared_ptr<const NativeAudioCue> Cue;
        std::shared_ptr<const NativeBcwav> Stream;
        double SourceFrame = 0.0;
        uint8_t Volume = 127;
        uint8_t Pan = 64;
        bool SourceLooped = false;
        double PitchScale = 1.0;
        double GainScale = 1.0;
        double SpatialPan = 0.0;
        double SpatialSurroundPan = 0.0;
        double DistanceFilter = 0.0;
        double SpatialAuxSend = 0.0;
        size_t DelayFrames = 0;
        uint64_t NoteOnFramesRemaining = ~uint64_t{0};
        uint8_t SequenceTrack = 0;
        bool SequenceVoice = false;
        EnvelopeState Envelope;
        ModulationState Modulation;
        PortamentoState Portamento;
        std::array<int32_t, 2> SimpleFilterHistory{};
        double PlayerGain = 1.0;
        double PlayerGainTarget = 1.0;
        double PlayerGainStep = 0.0;
        uint64_t PlayerGainFramesRemaining = 0;
        bool StopAfterPlayerFade = false;
    };

    struct SequenceControlState {
        float StartValue = 0.0f;
        float TargetValue = 0.0f;
        uint64_t StartFrame = 0;
        uint64_t EndFrame = 0;
        bool Active = false;

        float Value(uint64_t frame) const;
    };

    struct SequencePlayback {
        std::shared_ptr<const NativeAudioSequence> Sequence;
        uint64_t TimelineFrame = 0;
        size_t NextEvent = 0;
        size_t NextControlEvent = 0;
        double PitchScale = 1.0;
        double GainScale = 1.0;
        double SpatialPan = 0.0;
        double SpatialSurroundPan = 0.0;
        double DistanceFilter = 0.0;
        double SpatialAuxSend = 0.0;
        uint64_t OutputFrame = 0;
        SequenceControlState HandleGain;
        std::array<SequenceControlState, 5> PlayerVolumeControls;
        bool Stopping = false;
        std::array<std::array<SequenceControlState, 256>,
                   NativeCseqControlKindCount> TrackControls;
        SequenceControlState MasterVolumeControl;
        uint16_t TrackEnableMask = 0xffff;
        uint64_t LoopCount = 0;
        std::shared_ptr<const NativeAudioSequence> NextLoopSequence;
        std::vector<Voice> Voices;
    };

    void InitializeEnvelope(Voice& voice) const;
    void InitializeModulation(Voice& voice) const;
    void AdvanceModulation(Voice& voice, int8_t depth, int8_t speed,
                           uint8_t type, int8_t range) const;
    bool AdvanceEnvelope(Voice& voice, uint32_t ticks) const;
    double EnvelopeGain(const Voice& voice) const;
    uint64_t ControlFramesToSamples(uint32_t controlFrames) const;
    static void ConfigureVoiceGainFade(Voice& voice, double target,
                                       uint64_t sampleFrames, bool stopAfter);
    static double SequencePlayerGainAt(const SequencePlayback& playback,
                                       uint64_t frame);

    uint32_t mOutputSampleRate = 44100;
    std::shared_ptr<const NativeAudioEnvelopeProfile> mEnvelopeProfile;
    std::shared_ptr<const NativeAudioModulationProfile> mModulationProfile;
    std::shared_ptr<const NativeAudioMixProfile> mMixProfile;
    std::shared_ptr<const NativeAudioFilterProfile> mFilterProfile;
    std::shared_ptr<const NativeAudioResamplerProfile> mResamplerProfile;
    NativeAudioOutputMode mOutputMode = NativeAudioOutputMode::Stereo;
    NativeAudioDspEffectChain mDspEffects;
    std::unordered_map<uint64_t, Voice> mVoices;
    std::unordered_map<uint64_t, SequencePlayback> mSequences;
    uint64_t mMixedVoiceSampleCount = 0;
    size_t mMaximumMixedVoiceCount = 0;
};

} // namespace Oot3dNativeGame
