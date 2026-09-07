#include "oot3d_native_audio.h"
#include "oot3d_native_audio_service.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

void Expect(bool condition, const char* role) {
    if (!condition) {
        throw std::runtime_error(role);
    }
}

std::vector<uint8_t> ReadFile(const char* path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("failed to open test archive");
    }
    const auto length = input.tellg();
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    input.seekg(0);
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), length)) {
        throw std::runtime_error("failed to read test archive");
    }
    return bytes;
}

void PutU16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    bytes.at(offset) = static_cast<uint8_t>(value);
    bytes.at(offset + 1) = static_cast<uint8_t>(value >> 8);
}

void PutU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes.at(offset) = static_cast<uint8_t>(value);
    bytes.at(offset + 1) = static_cast<uint8_t>(value >> 8);
    bytes.at(offset + 2) = static_cast<uint8_t>(value >> 16);
    bytes.at(offset + 3) = static_cast<uint8_t>(value >> 24);
}

void PutMagic(std::vector<uint8_t>& bytes, size_t offset, const char (&magic)[5]) {
    std::copy_n(reinterpret_cast<const uint8_t*>(magic), 4, bytes.begin() + offset);
}

std::vector<uint8_t> MakeMonoBcwav(
    Oot3dNativeGame::NativeAudioCodec codec, std::span<const uint8_t> payload,
    uint32_t sampleCount, int16_t history = 0, int16_t stepIndex = 0) {
    constexpr size_t kInfoOffset = 0x40;
    constexpr size_t kChannelOffset = 0x68;
    constexpr size_t kCodecInfoOffset = 0x7c;
    constexpr size_t kDataOffset = 0x90;
    const size_t fileSize = kDataOffset + 8 + payload.size();
    std::vector<uint8_t> bytes(fileSize, 0);

    PutMagic(bytes, 0, "CWAV");
    PutU16(bytes, 4, 0xfeff);
    PutU16(bytes, 6, 0x40);
    PutU32(bytes, 8, 0x01020000);
    PutU32(bytes, 12, static_cast<uint32_t>(fileSize));
    PutU32(bytes, 16, 2);
    PutU16(bytes, 20, 0x7000);
    PutU32(bytes, 24, kInfoOffset);
    PutU32(bytes, 28, kDataOffset - kInfoOffset);
    PutU16(bytes, 32, 0x7001);
    PutU32(bytes, 36, kDataOffset);
    PutU32(bytes, 40, static_cast<uint32_t>(fileSize - kDataOffset));

    PutMagic(bytes, kInfoOffset, "INFO");
    PutU32(bytes, kInfoOffset + 4, kDataOffset - kInfoOffset);
    bytes[kInfoOffset + 8] = static_cast<uint8_t>(codec);
    PutU32(bytes, kInfoOffset + 12, 8000);
    PutU32(bytes, kInfoOffset + 20, sampleCount);
    PutU16(bytes, kInfoOffset + 28, 1);
    PutU16(bytes, kInfoOffset + 32, 0x7100);
    PutU32(bytes, kInfoOffset + 36,
           static_cast<uint32_t>(kChannelOffset - (kInfoOffset + 28)));

    PutU16(bytes, kChannelOffset, 0x1f00);
    PutU32(bytes, kChannelOffset + 4, 0);
    if (codec == Oot3dNativeGame::NativeAudioCodec::ImaAdpcm) {
        PutU16(bytes, kChannelOffset + 8, 0x0301);
        PutU32(bytes, kChannelOffset + 12,
               static_cast<uint32_t>(kCodecInfoOffset - kChannelOffset));
        PutU16(bytes, kCodecInfoOffset, static_cast<uint16_t>(history));
        PutU16(bytes, kCodecInfoOffset + 2, static_cast<uint16_t>(stepIndex));
    }

    PutMagic(bytes, kDataOffset, "DATA");
    PutU32(bytes, kDataOffset + 4,
           static_cast<uint32_t>(fileSize - kDataOffset));
    std::copy(payload.begin(), payload.end(), bytes.begin() + kDataOffset + 8);
    return bytes;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 6) {
            std::cerr << "usage: oot3d_native_audio_tests QueenSound.bcsar sample.bcwav code.bin stream.bcstm QueenStream.bcsar\n";
            return 2;
        }
        const auto archive = ReadFile(argv[1]);
        const auto catalog = Oot3dNativeGame::ParseNativeBcsar(archive);
        Expect(catalog.Sounds.size() == 1524, "QueenSound sound count");
        const std::array<uint32_t, 10> expectedPlayerLimits{
            3, 3, 5, 4, 2, 1, 2, 1, 6, 16,
        };
        Expect(catalog.Players.size() == expectedPlayerLimits.size(),
               "QueenSound native sound-player count");
        for (size_t index = 0; index < expectedPlayerLimits.size(); ++index) {
            Expect(catalog.Players[index].PlayableSoundLimit ==
                       expectedPlayerLimits[index] &&
                       catalog.Players[index].Name.rfind("PLAYER_", 0) == 0,
                   "QueenSound native sound-player metadata");
        }
        size_t sound3dCount = 0;
        size_t exponentialDecayCount = 0;
        size_t linearDecayCount = 0;
        size_t dopplerSoundCount = 0;
        std::array<size_t, 6> sound3dFlagCounts{};
        for (const auto& sound : catalog.Sounds) {
            if (!sound.Sound3d.has_value()) {
                continue;
            }
            ++sound3dCount;
            Expect(std::isfinite(sound.Sound3d->DecayRatio),
                   "native Sound3DInfo finite decay ratio");
            exponentialDecayCount += sound.Sound3d->DecayCurve == 1;
            linearDecayCount += sound.Sound3d->DecayCurve == 2;
            dopplerSoundCount += sound.Sound3d->DopplerFactor != 0;
            for (size_t bit = 0; bit < sound3dFlagCounts.size(); ++bit) {
                sound3dFlagCounts[bit] +=
                    (sound.Sound3d->Flags & (uint32_t{1} << bit)) != 0;
            }
        }
        Expect(sound3dCount != 0 &&
                   exponentialDecayCount + linearDecayCount != 0,
               "native BCSAR Sound3DInfo coverage");
        std::cout << "native Sound3DInfo: total=" << sound3dCount
                  << " exponential=" << exponentialDecayCount
                  << " linear=" << linearDecayCount
                  << " doppler=" << dopplerSoundCount << " flags=";
        for (size_t bit = 0; bit < sound3dFlagCounts.size(); ++bit) {
            std::cout << (bit == 0 ? "" : ",") << bit << ":"
                      << sound3dFlagCounts[bit];
        }
        std::cout << '\n';
        const auto* waterWall = catalog.ResolveSoundId(0x0100017f);
        Expect(waterWall != nullptr, "water-wall sound ID resolution");
        Expect(waterWall->Name == "NA_SE_EV_WATER_WALL", "water-wall native name");
        Expect(waterWall->Type == 0x2203 && waterWall->FileId == 3,
               "water-wall native sequence binding");
        Expect(waterWall->SequenceOffset == 0x66b, "water-wall sequence offset");
        Expect(waterWall->PlayerReference == 0x04000002 &&
                   waterWall->ArchiveVolume == 45 &&
                   waterWall->OptionMask == 0x80020107 &&
                   waterWall->Option(0).value_or(0xffffffff) == 0x17f &&
                   waterWall->Option(2).value_or(0) == 0x41,
               "water-wall native common sound metadata");
        Expect(waterWall->PlayerIndex().value_or(0xff) == 2 &&
                   catalog.ResolvePlayerReference(waterWall->PlayerReference) ==
                       &catalog.Players[2],
               "water-wall native sound-player binding");
        Expect(waterWall->BankIds.size() == 1 && waterWall->BankIds[0] == 2,
               "water-wall sequence bank");
        Expect(waterWall->Sound3d.has_value() &&
                   (waterWall->Sound3d->Flags & 1u) != 0,
               "water-wall native Sound3DInfo attenuation flag");
        const auto* kokiriBgm = catalog.ResolveSoundId(0x010005a9);
        Expect(kokiriBgm != nullptr && kokiriBgm->Type == 0x2203 &&
                   kokiriBgm->Name == "NA_BGM_KOKIRI",
               "Kokiri native ZSI BGM binding");
        const auto kokiriSequence = Oot3dNativeGame::ResolveNativeAudioSequence(
            archive, catalog, 0x010005a9, 1);
        Expect(kokiriSequence.Timebase == 96 &&
                   kokiriSequence.InitialTempo == 145 &&
                   kokiriSequence.EndTick == 11904 &&
                   kokiriSequence.Looping &&
                   kokiriSequence.LoopStartTick == 1920,
               "Kokiri native CSEQ big-endian command operands");
        const auto* natureBgm = catalog.ResolveSoundId(0x010005b9);
        const auto* specialBgm = catalog.ResolveSoundId(0x010005d9);
        Expect(natureBgm != nullptr && natureBgm->Type == 0x2203 &&
                   specialBgm != nullptr && specialBgm->Type == 0x2203,
               "native scene mode sequence bindings");
        const auto codeBin = ReadFile(argv[3]);
        const auto sceneProfile =
            Oot3dNativeGame::ParseNativeAudioSceneProfile(
                codeBin, Oot3dNativeGame::Oot3dEurRev0AudioSceneLayout());
        Expect(sceneProfile.NatureSequenceSoundId == 0x010005b9 &&
                   sceneProfile.SpecialSequenceSoundId == 0x010005d9 &&
                   sceneProfile.NatureChannelSoundIds[0] == 0x010005f0 &&
                   sceneProfile.NatureChannelSoundIds[1] == 0x010005f1 &&
                   sceneProfile.NatureChannelSoundIds[12] == 0x010005f1 &&
                   sceneProfile.NatureChannelSoundIds[13] == 0 &&
                   sceneProfile.NatureChannelSoundIds[14] == 0x010005f2 &&
                   sceneProfile.NatureChannelSoundIds[15] == 0x010005f3 &&
                   sceneProfile.NatureProfiles.size() == 20,
               "native AudioScene sequence-mode table header");
        const auto& kokiriNature = sceneProfile.NatureProfiles[4];
        Expect(kokiriNature.PlayerIo == 0xc01f &&
                   kokiriNature.ChannelMask == 0xc000 &&
                   kokiriNature.ChannelIo.size() == 18 &&
                   kokiriNature.ChannelIo.front().ChannelRange == 0 &&
                   kokiriNature.ChannelIo.front().Port == 2 &&
                   kokiriNature.ChannelIo.front().Value == 0 &&
                   kokiriNature.ChannelIo.back().ChannelRange == 4 &&
                   kokiriNature.ChannelIo.back().Port == 5 &&
                   kokiriNature.ChannelIo.back().Value == 0x2c,
               "native Kokiri nature ambience profile");
        const auto natureState =
            Oot3dNativeGame::BuildNativeNatureCseqInitialState(
                kokiriNature, 1);
        auto changedNatureState = natureState;
        changedNatureState.SequenceVariables[2] = 0x0c;
        auto scheduledNatureState = natureState;
        Oot3dNativeGame::NativeCseqInitialState::VariableUpdate
            scheduledWrite;
        scheduledWrite.FirstTrack = 0;
        scheduledWrite.LastTrack = 15;
        scheduledWrite.Variable = 2;
        scheduledWrite.Value = 0x0c;
        scheduledWrite.VariableScope = Oot3dNativeGame::
            NativeCseqInitialState::VariableUpdate::Scope::Sequence;
        scheduledNatureState.VariableUpdates.push_back(scheduledWrite);
        const auto baseNatureSequence =
            Oot3dNativeGame::ResolveNativeAudioSequence(
                archive, catalog, sceneProfile.NatureChannelSoundIds[1], 0,
                &natureState);
        const auto changedNatureSequence =
            Oot3dNativeGame::ResolveNativeAudioSequence(
                archive, catalog, sceneProfile.NatureChannelSoundIds[1], 0,
                &changedNatureState);
        const auto scheduledNatureSequence =
            Oot3dNativeGame::ResolveNativeAudioSequence(
                archive, catalog, sceneProfile.NatureChannelSoundIds[1], 0,
                &scheduledNatureState);
        const auto sameNatureEvents = [](const auto& lhs, const auto& rhs) {
            if (lhs.Events.size() != rhs.Events.size()) {
                return false;
            }
            for (size_t index = 0; index < lhs.Events.size(); ++index) {
                const auto& a = lhs.Events[index];
                const auto& b = rhs.Events[index];
                if (a.StartTick != b.StartTick || a.Track != b.Track ||
                    a.Silent != b.Silent || a.Cue.Program != b.Cue.Program ||
                    a.Cue.Note != b.Cue.Note ||
                    a.Cue.Velocity != b.Cue.Velocity) {
                    return false;
                }
            }
            return true;
        };
        Expect(natureState.SequenceVariables[1] == 1 &&
                   natureState.SequenceVariables[2] == 0x0d &&
                   changedNatureState.SequenceVariables[2] == 0x0c &&
                   sameNatureEvents(changedNatureSequence,
                                    scheduledNatureSequence),
               "native live CSEQ sequence-variable write scheduling");
        const auto soundSpecs =
            Oot3dNativeGame::ParseNativeAudioSoundSpecCatalog(
                codeBin,
                Oot3dNativeGame::Oot3dEurRev0AudioSoundSpecLayout());
        Expect(soundSpecs.Profiles.size() == 18,
               "native sound-spec profile count");
        const auto* kokiriDelay = std::get_if<
            Oot3dNativeGame::NativeAudioDelayEffect>(
                &soundSpecs.Profiles[1].AuxBus.Effect);
        Expect(soundSpecs.Profiles[1].DurationMilliseconds == 96 &&
                   std::abs(soundSpecs.Profiles[1].InputFeedback - 0.01875f) <
                       0.000001f &&
                   kokiriDelay != nullptr && kokiriDelay->Enabled &&
                   kokiriDelay->ChannelCount == 4 &&
                   kokiriDelay->FrameCount == 19 && kokiriDelay->G == 2 &&
                   kokiriDelay->A == 102 && kokiriDelay->B == 25,
               "native Kokiri sound-spec feedback delay");
        Expect(soundSpecs.Profiles[9].DurationMilliseconds == 320 &&
                   soundSpecs.Profiles[9].InputFeedback == 1.0f,
               "native sound-spec builder clamps");
        const auto nativeReverb =
            Oot3dNativeGame::ParseNativeAudioReverbEffect(
                codeBin,
                Oot3dNativeGame::Oot3dEurRev0AudioReverbLayout());
        Expect(nativeReverb.Enabled && nativeReverb.NativeSampleRate == 32728 &&
                   nativeReverb.DelayLengths ==
                       std::array<uint32_t, 5>{1920, 3200, 3040, 3680, 2080} &&
                   nativeReverb.Feedback0 == 109 &&
                   nativeReverb.Feedback1 == 105 &&
                   nativeReverb.Diffusion == 64 &&
                   nativeReverb.Damping == 51 &&
                   nativeReverb.WetGain == 51 &&
                   nativeReverb.DryGain == 76,
               "native global auxiliary-bus A reverb");
        const auto spatialProfile =
            Oot3dNativeGame::ParseNativeAudioSpatialProfile(
                codeBin,
                Oot3dNativeGame::Oot3dEurRev0AudioSpatialLayout());
        Expect(std::abs(spatialProfile.PanBaseAngle - 0.7853982f) <
                       0.000001f &&
                   std::abs(spatialProfile.PanFrontAngle - 0.5235988f) <
                       0.000001f &&
                   std::abs(spatialProfile.PanRearAngle - 2.0943952f) <
                       0.000001f &&
                   spatialProfile.PriorityScale == 32 &&
                   std::abs(spatialProfile.PanStrength - 0.9f) < 0.000001f &&
                   spatialProfile.ReferenceDistance == 1.0f &&
                   spatialProfile.DistanceScale == 1.0f &&
                   spatialProfile.DistanceFilterScale == 0.5f &&
                   spatialProfile.DistanceFilterMaximum == 1.0f &&
                   spatialProfile.DopplerBase == 0.0f &&
                   spatialProfile.DopplerScale == 0.03125f &&
                   spatialProfile.InitialEnvironmentAuxA == 0 &&
                   spatialProfile.InitialEnvironmentAuxB == 0 &&
                   std::abs(spatialProfile.AuxScale - 1.35f) < 0.000001f &&
                   std::abs(spatialProfile.AuxNormalization -
                            (1.0f / 127.0f)) < 0.000001f &&
                   spatialProfile.AuxMaximum == 1.0f,
               "native code.bin Sound3D manager and listener constants");
        Expect(Oot3dNativeGame::CalculateNativeAudioAuxSend(
                   spatialProfile, 4, 127, 127, 127) == 0.0f &&
                   std::abs(Oot3dNativeGame::CalculateNativeAudioAuxSend(
                                spatialProfile, 0, -3, 10, 20) -
                            (27.0f * 1.35f / 127.0f)) < 0.000001f &&
                   Oot3dNativeGame::CalculateNativeAudioAuxSend(
                       spatialProfile, 0, 127, 127, 127) == 1.0f,
               "native Audio_PlaySoundGeneral auxiliary-send law");
        const auto frontSpatial =
            Oot3dNativeGame::CalculateNativeAudioPanParameters(
                spatialProfile, {0.0f, 0.0f, -1.0f});
        const auto leftSpatial =
            Oot3dNativeGame::CalculateNativeAudioPanParameters(
                spatialProfile, {-1.0f, 0.0f, 0.0f});
        const auto rightSpatial =
            Oot3dNativeGame::CalculateNativeAudioPanParameters(
                spatialProfile, {1.0f, 0.0f, 0.0f});
        Expect(std::abs(frontSpatial.Pan) < 0.000001f &&
                   std::abs(leftSpatial.Pan + spatialProfile.PanStrength) <
                       0.000001f &&
                   std::abs(rightSpatial.Pan - spatialProfile.PanStrength) <
                       0.000001f,
               "native Sound3D angular pan curve");
        Oot3dNativeGame::NativeBcsarSound3dInfo linearSound3d;
        linearSound3d.DecayRatio = 0.5f;
        linearSound3d.DecayCurve = 2;
        const auto linearSpatial =
            Oot3dNativeGame::CalculateNativeAudioDistanceParameters(
                spatialProfile, linearSound3d, 2.0f);
        Expect(std::abs(linearSpatial.Gain - 0.5f) < 0.000001f &&
                   linearSpatial.PriorityAdjustment == -16 &&
                   std::abs(linearSpatial.DistanceFilter - 0.5f) <
                       0.000001f,
               "native linear Sound3D distance law");
        linearSound3d.DecayCurve = 1;
        const auto exponentialSpatial =
            Oot3dNativeGame::CalculateNativeAudioDistanceParameters(
                spatialProfile, linearSound3d, 3.0f);
        Expect(std::abs(exponentialSpatial.Gain - 0.25f) < 0.000001f &&
                   exponentialSpatial.PriorityAdjustment == -24 &&
                   exponentialSpatial.DistanceFilter == 1.0f,
               "native exponential Sound3D distance law");
        const auto envelopeProfile =
            Oot3dNativeGame::ParseNativeAudioEnvelopeProfile(
                codeBin, Oot3dNativeGame::Oot3dEurRev0AudioEnvelopeLayout());
        Expect(std::abs(envelopeProfile.FloorDb - -90.4f) < 0.001f &&
                   std::abs(envelopeProfile.LevelToDbScale - 0.1f) < 0.0001f &&
                   envelopeProfile.SustainLevels[0] == -723 &&
                   envelopeProfile.SustainLevels[127] == 0,
               "native envelope level tables");
        Expect(std::abs(envelopeProfile.AttackMultipliers[0] - 0.9992175f) <
                       0.000001f &&
                   envelopeProfile.AttackMultipliers[127] == 0.0f &&
                   envelopeProfile.HoldTicks(127) == 4096,
               "native attack and hold conversion");
        Expect(std::abs(envelopeProfile.DecayStep(126) - 24.0f) < 0.0001f &&
                   std::abs(envelopeProfile.ReleaseStep(120) - 2.0f) < 0.0001f &&
                   envelopeProfile.GainForLevel(-904.0f) == 0.0f &&
                   std::abs(envelopeProfile.GainForLevel(0.0f) - 1.0f) <
                       0.0001f,
               "native envelope rate and gain conversion");
        const auto modulationProfile =
            Oot3dNativeGame::ParseNativeAudioModulationProfile(
                codeBin,
                Oot3dNativeGame::Oot3dEurRev0AudioModulationLayout());
        Expect(std::abs(modulationProfile.PhaseScale - 128.0f) < 0.001f &&
                   std::abs(modulationProfile.OutputScale -
                            (1.0f / 127.0f)) < 0.000001f &&
                   std::abs(modulationProfile.PhaseAdvanceScale - 0.001f) <
                       0.000001f &&
                   modulationProfile.UpdateMilliseconds == 5,
               "native LFO constants and update cadence");
        Expect(modulationProfile.SampleSine(0.0f) == 0.0f &&
                   modulationProfile.SampleSine(0.25f) > 0.99f &&
                   std::abs(modulationProfile.SampleSine(0.5f)) < 0.0001f &&
                   modulationProfile.SampleSine(0.75f) < -0.99f,
               "native signed quarter-wave LFO table");
        const auto mixProfile =
            Oot3dNativeGame::ParseNativeAudioMixProfile(
                codeBin, Oot3dNativeGame::Oot3dEurRev0AudioMixLayout());
        Expect(std::abs(mixProfile.StandardPanCurves[0][64] -
                        0.8660254f) < 0.000001f &&
                   std::abs(mixProfile.StandardPanCurves[1][64] -
                            0.9238795f) < 0.000001f &&
                   mixProfile.StandardPanCurves[2][64] == 0.75f &&
                   mixProfile.StandardPanCurves[0][256] == 0.0f &&
                   mixProfile.AlternatePanCurves[0][0] == 0.8f,
               "native code.bin standard and surround pan LUTs");
        Expect(std::abs(Oot3dNativeGame::NormalizeNativeAudioPan(
                            0, mixProfile) + 1.0f) < 0.000001f &&
                   Oot3dNativeGame::NormalizeNativeAudioPan(
                       64, mixProfile) == 0.0f &&
                   std::abs(Oot3dNativeGame::NormalizeNativeAudioPan(
                                127, mixProfile) - 1.0f) < 0.000001f &&
                   std::abs(Oot3dNativeGame::NormalizeNativeAudioSurroundPan(
                                63, mixProfile) - 1.0f) < 0.000001f &&
                   Oot3dNativeGame::NormalizeNativeAudioSurroundPan(
                       127, mixProfile) == 2.0f,
               "native pan and surround byte normalization");
        Expect(std::abs(Oot3dNativeGame::CalculateNativeAudioPanGain(
                            mixProfile, 0, 0.0f) - 0.7071068f) < 0.000001f &&
                   std::abs(Oot3dNativeGame::CalculateNativeAudioPanGain(
                                mixProfile, 1, 0.0f) - 1.0f) < 0.000001f &&
                   Oot3dNativeGame::CalculateNativeAudioPanGain(
                       mixProfile, 2, -1.0f) == 1.0f &&
                   Oot3dNativeGame::CalculateNativeAudioSurroundGain(
                       mixProfile, 0, 0.0f) == 1.0f &&
                   Oot3dNativeGame::CalculateNativeAudioSurroundGain(
                       mixProfile, 0, 2.0f) == 0.0f,
               "native PanCurve gain modes");
        const auto stereoLeft =
            Oot3dNativeGame::CalculateNativeAudioChannelGains(
                mixProfile, Oot3dNativeGame::NativeAudioOutputMode::Stereo,
                2, 0, 0, 1, 0.0f, 0.0f);
        const auto stereoRight =
            Oot3dNativeGame::CalculateNativeAudioChannelGains(
                mixProfile, Oot3dNativeGame::NativeAudioOutputMode::Stereo,
                2, 1, 0, 1, 0.0f, 0.0f);
        Expect(stereoLeft.FrontLeft > 1.4f &&
                   stereoLeft.FrontRight == 0.0f &&
                   stereoRight.FrontLeft == 0.0f &&
                   stereoRight.FrontRight > 1.4f,
               "native stereo source channel separation");
        const auto filterProfile =
            Oot3dNativeGame::ParseNativeAudioFilterProfile(
                codeBin, Oot3dNativeGame::Oot3dEurRev0AudioFilterLayout());
        Expect(filterProfile.CutoffFrequencies.size() == 23 &&
                   filterProfile.CutoffFrequencies.front() == 80 &&
                   filterProfile.CutoffFrequencies[10] == 800 &&
                   filterProfile.CutoffFrequencies.back() == 12800 &&
                   filterProfile.CosineLookup.size() == 256,
               "native code.bin DSP low-pass tables");
        Expect(Oot3dNativeGame::CalculateNativeAudioCutoffFrequency(
                   filterProfile, 0.0f) == 80 &&
                   Oot3dNativeGame::CalculateNativeAudioCutoffFrequency(
                       filterProfile, 0.5f) == 800 &&
                   Oot3dNativeGame::CalculateNativeAudioCutoffFrequency(
                       filterProfile, 0.9f) == 16000,
               "native normalized cutoff conversion");
        const auto resamplerProfile =
            Oot3dNativeGame::ParseNativeAudioResamplerProfile(codeBin);
        Expect(resamplerProfile.DspComponentOffset == 0x44cc8c &&
                   resamplerProfile.DspComponentSize == 0xbe0c &&
                   resamplerProfile.PolyphaseTableOffset == 0xa994 &&
                   resamplerProfile.PolyphaseTableWordAddress == 0x3840,
               "native DSP1 polyphase table discovery");
        Expect(resamplerProfile.PolyphaseCoefficients.front() ==
                       std::array<int16_t, 4>{6600, 19426, 6722, 3} &&
                   resamplerProfile.PolyphaseCoefficients.back() ==
                       std::array<int16_t, 4>{3, 6722, 19426, 6600},
               "native DSP1 polyphase coefficients");
        const auto lowFilter =
            Oot3dNativeGame::CalculateNativeAudioSimpleFilter(
                filterProfile, 80);
        const auto passFilter =
            Oot3dNativeGame::CalculateNativeAudioSimpleFilter(
                filterProfile, 16000);
        Expect(lowFilter.B0 > 0 && lowFilter.A1 > passFilter.A1 &&
                   passFilter.B0 > lowFilter.B0 &&
                   std::abs(static_cast<int>(lowFilter.B0) +
                            lowFilter.A1 - 32768) <= 1 &&
                   std::abs(static_cast<int>(passFilter.B0) +
                            passFilter.A1 - 32768) <= 1,
               "native DSP simple-filter coefficients");
        Expect(Oot3dNativeGame::ResolveNativeCodeU32TableEntry(
                   codeBin, 0x00100000, 0x0052e344, 1) == 0x0100017f,
               "EnRiverSound native code.bin sound table");
        Expect(catalog.Banks.size() > 2 && catalog.Banks[2].Name == "BANK_ENVIROMENT",
               "water-wall native bank catalog");
        Expect(!catalog.WaveArchives.empty() && catalog.WaveArchives[0].Name == "WARC_SE",
               "water-wall native wave archive catalog");

        const auto cue = Oot3dNativeGame::ResolveNativeAudioCue(archive, catalog, 0x0100017f);
        const auto waterSequence = Oot3dNativeGame::ResolveNativeAudioSequence(
            archive, catalog, 0x0100017f);
        Expect(!waterSequence.ControlOnly && !waterSequence.Events.empty(),
               "water-wall native CSEQ event coverage");
        std::cout << "resolved cue: program=" << static_cast<unsigned>(cue.Program)
                  << " note=" << static_cast<unsigned>(cue.Note)
                  << " velocity=" << static_cast<unsigned>(cue.Velocity)
                  << " duration=" << cue.DurationTicks
                  << " bank=" << static_cast<unsigned>(cue.BankId)
                  << " war=" << cue.WaveArchiveIndex << " wave=" << cue.WaveIndex << '\n';
        Expect(cue.Program == 4 && cue.Note == 57 && cue.Velocity == 75,
               "water-wall native sequence note");
        Expect(cue.DurationTicks == 32000, "water-wall native sequence duration");
        Expect(cue.BankId == 2 && cue.WaveArchiveIndex == 0 && cue.WaveIndex == 62,
               "water-wall native bank wave binding");
        Expect(cue.RootKey == 60 && cue.Volume == 127 && cue.Pan == 64,
               "water-wall native note parameters");
        Expect(cue.ArchiveVolume == 45,
               "water-wall native archive volume binding");
        Expect(cue.Wave && !cue.Wave->InterleavedSamples.empty(),
               "resolved water-wall native wave");

        Oot3dNativeGame::NativeAudioDspEffectChain effects;
        std::vector<int32_t> impulse(
            Oot3dNativeGame::kNativeAudioDspFrameSize * 2, 0);
        impulse[0] = 1000;
        Expect(effects.ProcessStereoBus(0, impulse) == impulse,
               "disabled native auxiliary bus bypass");

        Oot3dNativeGame::NativeAudioDelayEffect delay;
        delay.Enabled = true;
        delay.FrameCount = 1;
        delay.A = 128;
        effects.ConfigureBus(0, {delay, 1.0f});
        const auto delayFirst = effects.ProcessStereoBus(0, impulse);
        const auto delaySecond = effects.ProcessStereoBus(
            0, std::vector<int32_t>(impulse.size(), 0));
        Expect(std::none_of(delayFirst.begin(), delayFirst.end(),
                            [](int32_t sample) { return sample != 0; }) &&
                   delaySecond[0] == impulse[0],
               "native 160-sample feedback delay kernel");

        std::vector<int32_t> surroundImpulse(
            Oot3dNativeGame::kNativeAudioDspFrameSize *
                Oot3dNativeGame::kNativeAudioDspChannelCount,
            0);
        surroundImpulse[2] = 1000;
        delay.ChannelCount = Oot3dNativeGame::kNativeAudioDspChannelCount;
        effects.ConfigureBus(0, {delay, 1.0f});
        const auto surroundDelayFirst = effects.ProcessBus(
            0, surroundImpulse,
            Oot3dNativeGame::kNativeAudioDspChannelCount);
        const auto surroundDelaySecond = effects.ProcessBus(
            0, std::vector<int32_t>(surroundImpulse.size(), 0),
            Oot3dNativeGame::kNativeAudioDspChannelCount);
        Expect(std::none_of(surroundDelayFirst.begin(),
                            surroundDelayFirst.end(),
                            [](int32_t sample) { return sample != 0; }) &&
                   surroundDelaySecond[2] == surroundImpulse[2],
               "native four-channel auxiliary delay topology");

        Oot3dNativeGame::NativeAudioReverbEffect reverb;
        reverb.Enabled = true;
        reverb.DelayLengths.fill(
            Oot3dNativeGame::kNativeAudioDspFrameSize * 2);
        reverb.DryGain = 128;
        effects.ConfigureBus(1, {reverb, 1.0f});
        const auto reverbFirst = effects.ProcessStereoBus(1, impulse);
        const auto reverbSecond = effects.ProcessStereoBus(
            1, std::vector<int32_t>(impulse.size(), 0));
        const auto reverbThird = effects.ProcessStereoBus(
            1, std::vector<int32_t>(impulse.size(), 0));
        Expect(std::none_of(reverbFirst.begin(), reverbFirst.end(),
                            [](int32_t sample) { return sample != 0; }) &&
                   std::none_of(reverbSecond.begin(), reverbSecond.end(),
                                [](int32_t sample) { return sample != 0; }) &&
                   reverbThird[0] == impulse[0],
               "native multi-delay reverb kernel");

        Oot3dNativeGame::NativeAudioDspEffectChain hostRateEffects;
        hostRateEffects.SetSampleRates(32728, 44100);
        delay.ChannelCount = 2;
        hostRateEffects.ConfigureBus(0, {delay, 1.0f});
        std::vector<int32_t> hostRateImpulse(217 * 2, 0);
        hostRateImpulse[0] = 1000;
        const auto hostRateDelay = hostRateEffects.ProcessStereoBus(
            0, hostRateImpulse);
        Expect(std::none_of(
                   hostRateDelay.begin(), hostRateDelay.begin() + 216 * 2,
                   [](int32_t sample) { return sample != 0; }) &&
                   hostRateDelay[216 * 2] == hostRateImpulse[0],
               "native DSP delay duration projected to host sample rate");

        auto sharedCue = std::make_shared<Oot3dNativeGame::NativeAudioCue>(cue);
        Oot3dNativeGame::NativeAudioMixer mixer(44100);
        mixer.SetResamplerProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioResamplerProfile>(
                resamplerProfile));
        mixer.SetEnvelopeProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioEnvelopeProfile>(
                envelopeProfile));
        mixer.SetModulationProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioModulationProfile>(
                modulationProfile));
        mixer.SetMixProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioMixProfile>(
                mixProfile));
        mixer.StartOrRefreshVoice(1, sharedCue);
        const auto mixed = mixer.MixStereo(1024);
        Expect(mixed.size() == 2048 && mixer.ActiveVoiceCount() == 1,
               "native audio mixer sustained voice");
        bool mixedNonZero = false;
        for (const auto sample : mixed) {
            mixedNonZero |= sample != 0;
        }
        Expect(mixedNonZero, "native audio mixer output is not silent");
        mixer.StopVoice(1);
        Expect(mixer.ActiveVoiceCount() == 0, "native audio mixer voice stop");

        auto polyphaseWave = std::make_shared<Oot3dNativeGame::NativeBcwav>();
        polyphaseWave->SampleRate = 44100;
        polyphaseWave->SampleCount = 8;
        polyphaseWave->ChannelCount = 1;
        polyphaseWave->InterleavedSamples.assign(8, 0);
        polyphaseWave->InterleavedSamples[4] = 32767;
        auto polyphaseCue = std::make_shared<Oot3dNativeGame::NativeAudioCue>();
        polyphaseCue->Note = 60;
        polyphaseCue->RootKey = 60;
        polyphaseCue->Velocity = 127;
        polyphaseCue->VelocityRange = 127;
        polyphaseCue->Volume = 127;
        polyphaseCue->ArchiveVolume = 127;
        polyphaseCue->Pan = 64;
        polyphaseCue->InterpolationType = 0;
        polyphaseCue->Wave = polyphaseWave;
        Oot3dNativeGame::NativeAudioMixer polyphaseMixer(44100);
        polyphaseMixer.SetResamplerProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioResamplerProfile>(
                resamplerProfile));
        polyphaseMixer.StartOrRefreshVoice(1, polyphaseCue);
        const auto polyphaseOutput = polyphaseMixer.MixStereo(8);
        Expect(polyphaseOutput[4] == 2 &&
                   polyphaseOutput[6] == 6721 &&
                   polyphaseOutput[8] == 19425 &&
                   polyphaseOutput[10] == 6599,
               "native DSP1 polyphase kernel reaches source playback");

        auto gainModulatedCue =
            std::make_shared<Oot3dNativeGame::NativeAudioCue>(cue);
        gainModulatedCue->SoundId = 0x01fffffd;
        gainModulatedCue->ModPhase = 32;
        gainModulatedCue->ModDepth = 24;
        gainModulatedCue->ModRange = 1;
        gainModulatedCue->ModSpeed = 0;
        gainModulatedCue->ModType = 1;
        Oot3dNativeGame::NativeAudioMixer gainModulatedMixer(44100);
        gainModulatedMixer.SetEnvelopeProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioEnvelopeProfile>(
                envelopeProfile));
        gainModulatedMixer.SetModulationProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioModulationProfile>(
                modulationProfile));
        gainModulatedMixer.StartOrRefreshVoice(1, gainModulatedCue);
        const auto gainModulated = gainModulatedMixer.MixStereo(256);
        auto unmodulatedCue =
            std::make_shared<Oot3dNativeGame::NativeAudioCue>(*gainModulatedCue);
        unmodulatedCue->ModDepth = 0;
        Oot3dNativeGame::NativeAudioMixer unmodulatedMixer(44100);
        unmodulatedMixer.SetEnvelopeProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioEnvelopeProfile>(
                envelopeProfile));
        unmodulatedMixer.SetModulationProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioModulationProfile>(
                modulationProfile));
        unmodulatedMixer.StartOrRefreshVoice(1, unmodulatedCue);
        const auto unmodulated = unmodulatedMixer.MixStereo(256);
        Expect(gainModulated != unmodulated,
               "native CSEQ gain LFO reaches the voice output");

        auto portamentoCue =
            std::make_shared<Oot3dNativeGame::NativeAudioCue>(*unmodulatedCue);
        portamentoCue->SoundId = 0x01fffffc;
        portamentoCue->PortamentoKey = static_cast<int8_t>(
            portamentoCue->Note - 12);
        portamentoCue->PortamentoEnabled = true;
        portamentoCue->PortamentoTime = 8;
        Oot3dNativeGame::NativeAudioMixer portamentoMixer(44100);
        portamentoMixer.SetEnvelopeProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioEnvelopeProfile>(
                envelopeProfile));
        portamentoMixer.SetModulationProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioModulationProfile>(
                modulationProfile));
        portamentoMixer.StartOrRefreshVoice(1, portamentoCue);
        const auto portamentoMixed = portamentoMixer.MixStereo(256);
        Expect(portamentoMixed != unmodulated,
               "native CSEQ portamento reaches the voice pitch path");

        auto auxiliaryCue =
            std::make_shared<Oot3dNativeGame::NativeAudioCue>(cue);
        auxiliaryCue->MainSend = 0;
        auxiliaryCue->FxSendA = 127;
        Oot3dNativeGame::NativeAudioMixer auxiliaryMixer(44100);
        auxiliaryMixer.StartOrRefreshVoice(1, auxiliaryCue);
        const auto auxiliaryMixed = auxiliaryMixer.MixStereo(1024);
        Expect(std::any_of(auxiliaryMixed.begin(), auxiliaryMixed.end(),
                           [](int16_t sample) { return sample != 0; }),
               "native CSEQ auxiliary send routing");

        auto sharedSequence =
            std::make_shared<Oot3dNativeGame::NativeAudioSequence>(waterSequence);
        mixer.StartOrRefreshSequence(3, sharedSequence);
        const auto sequenceMixed = mixer.MixStereo(1024);
        bool sequenceMixedNonZero = false;
        for (const auto sample : sequenceMixed) {
            sequenceMixedNonZero |= sample != 0;
        }
        Expect(sequenceMixedNonZero && mixer.ActiveVoiceCount() == 1,
               "native CSEQ scheduled mixer output");
        mixer.StopVoice(3);

        auto sequenceWave = std::make_shared<Oot3dNativeGame::NativeBcwav>(
            *cue.Wave);
        sequenceWave->Looping = true;
        sequenceWave->LoopStart = 0;
        Oot3dNativeGame::NativeAudioCue sequenceCue = cue;
        sequenceCue.Wave = sequenceWave;
        sequenceCue.ArchiveVolume = 127;
        sequenceCue.Volume = 64;

        auto makeControlSequence = [&]() {
            auto result = std::make_shared<
                Oot3dNativeGame::NativeAudioSequence>();
            result->SoundId = 0x01fffffb;
            result->Timebase = 1000;
            result->InitialTempo = 60;
            result->EndTick = 2000;
            Oot3dNativeGame::NativeAudioSequenceEvent event;
            event.DurationTicks = 2000;
            event.Track = 3;
            event.Cue = sequenceCue;
            event.Cue.DurationTicks = event.DurationTicks;
            result->Events.push_back(std::move(event));
            return result;
        };
        auto automatedSequence = makeControlSequence();
        automatedSequence->ControlEvents.push_back({
            0, 1000, 3, Oot3dNativeGame::NativeCseqControlKind::Volume,
            127.0f, 0.0f});
        auto constantSequence = makeControlSequence();
        Oot3dNativeGame::NativeAudioMixer automatedMixer(44100);
        Oot3dNativeGame::NativeAudioMixer constantMixer(44100);
        automatedMixer.StartOrRefreshSequence(1, automatedSequence);
        constantMixer.StartOrRefreshSequence(1, constantSequence);
        (void)automatedMixer.MixStereo(22050);
        (void)constantMixer.MixStereo(22050);
        const auto automatedRamp = automatedMixer.MixStereo(1024);
        const auto constantRamp = constantMixer.MixStereo(1024);
        const auto absoluteEnergy = [](const std::vector<int16_t>& samples) {
            uint64_t result = 0;
            for (const int16_t sample : samples) {
                result += static_cast<uint64_t>(std::abs(
                    static_cast<int32_t>(sample)));
            }
            return result;
        };
        Expect(absoluteEnergy(automatedRamp) * 3 <
                   absoluteEnergy(constantRamp) * 2,
               "native CSEQ timed volume controls active voices");

        auto dynamicGainSequence = makeControlSequence();
        dynamicGainSequence->ControlEvents.push_back({
            500, 0, 0, Oot3dNativeGame::NativeCseqControlKind::MasterVolume,
            127.0f, 32.0f});
        dynamicGainSequence->ControlEvents.push_back({
            500, 0, 3, Oot3dNativeGame::NativeCseqControlKind::Expression,
            127.0f, 64.0f});
        Oot3dNativeGame::NativeAudioMixer dynamicGainMixer(44100);
        Oot3dNativeGame::NativeAudioMixer referenceGainMixer(44100);
        dynamicGainMixer.StartOrRefreshSequence(1, dynamicGainSequence);
        referenceGainMixer.StartOrRefreshSequence(1, makeControlSequence());
        (void)dynamicGainMixer.MixStereo(22050);
        (void)referenceGainMixer.MixStereo(22050);
        const auto dynamicGainOutput = dynamicGainMixer.MixStereo(1024);
        const auto referenceGainOutput = referenceGainMixer.MixStereo(1024);
        Expect(absoluteEnergy(dynamicGainOutput) * 4 <
                   absoluteEnergy(referenceGainOutput),
               "native CSEQ master volume and expression update active voices");

        auto dynamicPitchSequence = makeControlSequence();
        dynamicPitchSequence->ControlEvents.push_back({
            500, 0, 3, Oot3dNativeGame::NativeCseqControlKind::PitchBendRange,
            2.0f, 12.0f});
        dynamicPitchSequence->ControlEvents.push_back({
            500, 0, 3, Oot3dNativeGame::NativeCseqControlKind::PitchBend,
            0.0f, 127.0f});
        Oot3dNativeGame::NativeAudioMixer dynamicPitchMixer(44100);
        Oot3dNativeGame::NativeAudioMixer referencePitchMixer(44100);
        dynamicPitchMixer.StartOrRefreshSequence(1, dynamicPitchSequence);
        referencePitchMixer.StartOrRefreshSequence(1, makeControlSequence());
        (void)dynamicPitchMixer.MixStereo(22050);
        (void)referencePitchMixer.MixStereo(22050);
        Expect(dynamicPitchMixer.MixStereo(1024) !=
                   referencePitchMixer.MixStereo(1024),
               "native CSEQ pitch bend updates active voices");

        auto dynamicModulationSequence = makeControlSequence();
        dynamicModulationSequence->Events.front().Cue.ModPhase = 32;
        dynamicModulationSequence->Events.front().Cue.ModDepth = 0;
        dynamicModulationSequence->Events.front().Cue.ModSpeed = 0;
        dynamicModulationSequence->Events.front().Cue.ModType = 1;
        dynamicModulationSequence->Events.front().Cue.ModRange = 1;
        dynamicModulationSequence->ControlEvents.push_back({
            500, 0, 3, Oot3dNativeGame::NativeCseqControlKind::ModDepth,
            0.0f, 24.0f});
        auto referenceModulationSequence = makeControlSequence();
        referenceModulationSequence->Events.front().Cue.ModPhase = 32;
        referenceModulationSequence->Events.front().Cue.ModDepth = 0;
        referenceModulationSequence->Events.front().Cue.ModSpeed = 0;
        referenceModulationSequence->Events.front().Cue.ModType = 1;
        referenceModulationSequence->Events.front().Cue.ModRange = 1;
        Oot3dNativeGame::NativeAudioMixer dynamicModulationMixer(44100);
        Oot3dNativeGame::NativeAudioMixer referenceModulationMixer(44100);
        dynamicModulationMixer.SetEnvelopeProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioEnvelopeProfile>(
                envelopeProfile));
        dynamicModulationMixer.SetModulationProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioModulationProfile>(
                modulationProfile));
        referenceModulationMixer.SetEnvelopeProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioEnvelopeProfile>(
                envelopeProfile));
        referenceModulationMixer.SetModulationProfile(
            std::make_shared<const Oot3dNativeGame::NativeAudioModulationProfile>(
                modulationProfile));
        dynamicModulationMixer.StartOrRefreshSequence(
            1, dynamicModulationSequence);
        referenceModulationMixer.StartOrRefreshSequence(
            1, referenceModulationSequence);
        (void)dynamicModulationMixer.MixStereo(22050);
        (void)referenceModulationMixer.MixStereo(22050);
        Expect(dynamicModulationMixer.MixStereo(1024) !=
                   referenceModulationMixer.MixStereo(1024),
               "native CSEQ modulation controls update active voices");

        auto dynamicSendSequence = makeControlSequence();
        dynamicSendSequence->Events.front().Cue.MainSend = 0;
        dynamicSendSequence->ControlEvents.push_back({
            500, 0, 3, Oot3dNativeGame::NativeCseqControlKind::FxSendA,
            0.0f, 127.0f});
        Oot3dNativeGame::NativeAudioMixer dynamicSendMixer(44100);
        dynamicSendMixer.StartOrRefreshSequence(1, dynamicSendSequence);
        (void)dynamicSendMixer.MixStereo(22050);
        const auto dynamicSendOutput = dynamicSendMixer.MixStereo(1024);
        Expect(std::any_of(dynamicSendOutput.begin(), dynamicSendOutput.end(),
                           [](int16_t sample) { return sample != 0; }),
               "native CSEQ auxiliary send updates active voices");

        auto invariantSequence = makeControlSequence();
        invariantSequence->ControlEvents.push_back({
            200, 300, 3, Oot3dNativeGame::NativeCseqControlKind::Volume,
            127.0f, 24.0f});
        invariantSequence->ControlEvents.push_back({
            650, 0, 3, Oot3dNativeGame::NativeCseqControlKind::Pan,
            64.0f, 112.0f});
        Oot3dNativeGame::NativeAudioMixer wholeBlockMixer(44100);
        Oot3dNativeGame::NativeAudioMixer splitBlockMixer(44100);
        wholeBlockMixer.StartOrRefreshSequence(1, invariantSequence);
        splitBlockMixer.StartOrRefreshSequence(1, invariantSequence);
        const auto wholeBlock = wholeBlockMixer.MixStereo(44100);
        std::vector<int16_t> splitBlock;
        for (const size_t frames : {7777u, 4321u, 16003u, 15999u}) {
            auto part = splitBlockMixer.MixStereo(frames);
            splitBlock.insert(splitBlock.end(), part.begin(), part.end());
        }
        Expect(splitBlock == wholeBlock,
               "native CSEQ scheduling is independent of host block size");

        auto loopingSequence = makeControlSequence();
        loopingSequence->EndTick = 100;
        loopingSequence->LoopStartTick = 0;
        loopingSequence->Looping = true;
        loopingSequence->Events.front().DurationTicks = 100;
        loopingSequence->Events.front().Cue.DurationTicks = 100;
        Oot3dNativeGame::NativeAudioMixer wholeLoopMixer(44100);
        Oot3dNativeGame::NativeAudioMixer splitLoopMixer(44100);
        wholeLoopMixer.StartOrRefreshSequence(1, loopingSequence);
        splitLoopMixer.StartOrRefreshSequence(1, loopingSequence);
        const auto wholeLoop = wholeLoopMixer.MixStereo(13230);
        std::vector<int16_t> splitLoop;
        for (const size_t frames : {1111u, 2222u, 3333u, 6564u}) {
            auto part = splitLoopMixer.MixStereo(frames);
            splitLoop.insert(splitLoop.end(), part.begin(), part.end());
        }
        Expect(splitLoop == wholeLoop && wholeLoopMixer.IsVoiceActive(1) &&
                   absoluteEnergy(std::vector<int16_t>(
                       wholeLoop.end() - 4410 * 2, wholeLoop.end())) != 0,
               "native CSEQ loop rollover is sample-accurate within a host block");

        Oot3dNativeGame::NativeAudioMixer fullGainMixer(44100);
        Oot3dNativeGame::NativeAudioMixer reducedGainMixer(44100);
        fullGainMixer.StartOrRefreshSequence(1, makeControlSequence(), 1.0, 1.0);
        reducedGainMixer.StartOrRefreshSequence(
            1, makeControlSequence(), 1.0, 0.25);
        const auto fullGainOutput = fullGainMixer.MixStereo(1024);
        const auto reducedGainOutput = reducedGainMixer.MixStereo(1024);
        Expect(absoluteEnergy(reducedGainOutput) * 3 <
                   absoluteEnergy(fullGainOutput),
               "native Audio_PlaySoundGeneral gain reaches active voices");

        const auto channelEnergy = [](const std::vector<int16_t>& samples,
                                      size_t channel) {
            uint64_t result = 0;
            for (size_t index = channel; index < samples.size(); index += 2) {
                result += static_cast<uint64_t>(std::abs(
                    static_cast<int32_t>(samples[index])));
            }
            return result;
        };
        Oot3dNativeGame::NativeAudioMixer leftSpatialMixer(44100);
        Oot3dNativeGame::NativeAudioMixer rightSpatialMixer(44100);
        leftSpatialMixer.StartOrRefreshSequence(
            1, makeControlSequence(), 1.0, 1.0, -0.9);
        rightSpatialMixer.StartOrRefreshSequence(
            1, makeControlSequence(), 1.0, 1.0, 0.9);
        const auto leftSpatialOutput = leftSpatialMixer.MixStereo(1024);
        const auto rightSpatialOutput = rightSpatialMixer.MixStereo(1024);
        Expect(channelEnergy(leftSpatialOutput, 0) >
                   channelEnergy(leftSpatialOutput, 1) * 4 &&
                   channelEnergy(rightSpatialOutput, 1) >
                       channelEnergy(rightSpatialOutput, 0) * 4,
               "native Sound3D pan reaches stereo mixer output");
        Oot3dNativeGame::NativeAudioMixer dryAuxMixer(44100);
        Oot3dNativeGame::NativeAudioMixer spatialAuxMixer(44100);
        dryAuxMixer.StartOrRefreshSequence(
            1, makeControlSequence(), 1.0, 1.0, 0.0, 0.0, 0.0);
        spatialAuxMixer.StartOrRefreshSequence(
            1, makeControlSequence(), 1.0, 1.0, 0.0, 0.0, 0.0, 0.5);
        const auto dryAuxOutput = dryAuxMixer.MixStereo(1024);
        const auto spatialAuxOutput = spatialAuxMixer.MixStereo(1024);
        Expect(absoluteEnergy(spatialAuxOutput) >
                   absoluteEnergy(dryAuxOutput),
               "native spatial auxiliary send reaches the DSP bus");

        Oot3dNativeGame::NativeAudioMixer unfilteredMixer(44100);
        Oot3dNativeGame::NativeAudioMixer distanceFilteredMixer(44100);
        const auto sharedFilterProfile =
            std::make_shared<const Oot3dNativeGame::NativeAudioFilterProfile>(
                filterProfile);
        unfilteredMixer.SetFilterProfile(sharedFilterProfile);
        distanceFilteredMixer.SetFilterProfile(sharedFilterProfile);
        unfilteredMixer.StartOrRefreshSequence(
            1, makeControlSequence(), 1.0, 1.0, 0.0, 0.0, 0.0, 0.0);
        distanceFilteredMixer.StartOrRefreshSequence(
            1, makeControlSequence(), 1.0, 1.0, 0.0, 0.0, 1.0, 0.0);
        const auto unfilteredOutput = unfilteredMixer.MixStereo(1024);
        const auto distanceFilteredOutput =
            distanceFilteredMixer.MixStereo(1024);
        Expect(distanceFilteredOutput != unfilteredOutput &&
                   absoluteEnergy(distanceFilteredOutput) <
                       absoluteEnergy(unfilteredOutput),
               "native Sound3D distance filter reaches the DSP low-pass");

        auto makeTieSequence = [&](bool tie) {
            auto result = std::make_shared<
                Oot3dNativeGame::NativeAudioSequence>();
            result->SoundId = tie ? 0x01fffffa : 0x01fffff9;
            result->Timebase = 1000;
            result->InitialTempo = 60;
            result->EndTick = 1000;
            for (size_t index = 0; index < 2; ++index) {
                Oot3dNativeGame::NativeAudioSequenceEvent event;
                event.DurationTicks = 1000;
                event.Track = 2;
                event.Tie = tie && index != 0;
                event.Cue = sequenceCue;
                event.Cue.DurationTicks = event.DurationTicks;
                result->Events.push_back(std::move(event));
            }
            return result;
        };
        Oot3dNativeGame::NativeAudioMixer tieMixer(44100);
        Oot3dNativeGame::NativeAudioMixer polyphonicMixer(44100);
        tieMixer.StartOrRefreshSequence(1, makeTieSequence(true));
        polyphonicMixer.StartOrRefreshSequence(1, makeTieSequence(false));
        const auto tiedOutput = tieMixer.MixStereo(256);
        const auto polyphonicOutput = polyphonicMixer.MixStereo(256);
        Expect(absoluteEnergy(tiedOutput) * 3 <
                   absoluteEnergy(polyphonicOutput) * 2,
               "native CSEQ tie reuses the active track voice");

        auto releaseSequence =
            std::make_shared<Oot3dNativeGame::NativeAudioSequence>();
        releaseSequence->SoundId = 0x01fffffe;
        releaseSequence->Timebase = 1000;
        releaseSequence->InitialTempo = 60;
        releaseSequence->EndTick = 1;
        Oot3dNativeGame::NativeAudioSequenceEvent releaseEvent;
        releaseEvent.DurationTicks = 1;
        releaseEvent.Cue = cue;
        releaseEvent.Cue.Attack = 127;
        releaseEvent.Cue.Decay = 127;
        releaseEvent.Cue.Sustain = 127;
        releaseEvent.Cue.Hold = 0;
        releaseEvent.Cue.Release = 120;
        releaseSequence->Events.push_back(std::move(releaseEvent));
        mixer.StartOrRefreshSequence(4, releaseSequence);
        mixer.MixStereo(128);
        Expect(mixer.IsVoiceActive(4),
               "native note remains active during release envelope");
        mixer.MixStereo(30000);
        Expect(!mixer.IsVoiceActive(4),
               "native release envelope reaches its code.bin floor");

        const auto wave = Oot3dNativeGame::DecodeNativeBcwavFile(argv[2]);
        Expect(wave.Codec == Oot3dNativeGame::NativeAudioCodec::DspAdpcm,
               "native DSP-ADPCM codec");
        Expect(wave.SampleRate == 22050 && wave.ChannelCount == 1,
               "native wave stream metadata");
        Expect(wave.InterleavedSamples.size() == wave.SampleCount,
               "decoded native wave sample count");
        bool nonZero = false;
        for (const auto sample : wave.InterleavedSamples) {
            nonZero |= sample != 0;
        }
        Expect(nonZero, "decoded native wave is not silent");

        constexpr std::array<uint8_t, 4> signedPcm8{0x80, 0xff, 0x00, 0x7f};
        const auto pcm8Wave = Oot3dNativeGame::DecodeNativeBcwav(
            MakeMonoBcwav(Oot3dNativeGame::NativeAudioCodec::Pcm8,
                          signedPcm8, signedPcm8.size()));
        Expect(pcm8Wave.InterleavedSamples ==
                   std::vector<int16_t>({-32768, -256, 0, 32512}),
               "native signed PCM8 decode");

        constexpr std::array<uint8_t, 2> imaPayload{0x11, 0x11};
        const auto imaWave = Oot3dNativeGame::DecodeNativeBcwav(
            MakeMonoBcwav(Oot3dNativeGame::NativeAudioCodec::ImaAdpcm,
                          imaPayload, 4));
        Expect(imaWave.InterleavedSamples ==
                   std::vector<int16_t>({1, 2, 3, 4}),
               "native BCWAV IMA-ADPCM low-nibble-first decode");

        const auto stream = Oot3dNativeGame::DecodeNativeBcstmFile(argv[4]);
        Expect(stream.Codec == Oot3dNativeGame::NativeAudioCodec::DspAdpcm,
               "native BCSTM DSP-ADPCM codec");
        Expect(stream.SampleRate == 32728 && stream.ChannelCount == 2,
               "native BCSTM stream metadata");
        Expect(!stream.Looping && stream.LoopStart < stream.SampleCount,
               "native BCSTM loop metadata");
        Expect(stream.InterleavedSamples.size() ==
                   static_cast<size_t>(stream.SampleCount) * stream.ChannelCount,
               "decoded native BCSTM sample count");
        auto sharedStream = std::make_shared<Oot3dNativeGame::NativeBcwav>(stream);
        mixer.StartOrRefreshStream(2, sharedStream);
        bool streamMixedNonZero = false;
        for (size_t block = 0; block < 8; ++block) {
            const auto streamMixed = mixer.MixStereo(1024);
            for (const auto sample : streamMixed) {
                streamMixedNonZero |= sample != 0;
            }
        }
        Expect(streamMixedNonZero, "native BCSTM mixer output is not silent");
        mixer.StopVoice(2);

        const auto streamArchive = ReadFile(argv[5]);
        const auto streamCatalog = Oot3dNativeGame::ParseNativeBcsar(streamArchive);
        Expect(streamCatalog.Sounds.size() == 1 && streamCatalog.Sounds[0].Type == 0x2201,
               "QueenStream external stream sound");
        Expect(streamCatalog.Players.size() == 1 &&
                   streamCatalog.Players[0].PlayableSoundLimit == 1,
               "QueenStream native sound-player limit");
        Expect(streamCatalog.Files.size() == 1 &&
                   streamCatalog.Files[0].Path == "stream/STRM_QUEEN_ROLL.bcstm",
               "QueenStream external BCSTM path");
        const auto resolvedStream = Oot3dNativeGame::ResolveNativeAudioStream(
            argv[5], streamCatalog, 0x01000000);
        Expect(resolvedStream.SampleRate == stream.SampleRate &&
                   resolvedStream.SampleCount == stream.SampleCount &&
                   resolvedStream.ChannelCount == stream.ChannelCount,
               "QueenStream BCSAR to BCSTM resolution");

        Oot3dNativeGame::NativeAudioService playerPolicyService(
            argv[1], argv[5], 44100);
        Oot3dNativeGame::NativeAudioRequest playerPolicyRequest;
        playerPolicyRequest.SoundId = 0x0100017f;
        for (uint64_t source = 1; source <= 6; ++source) {
            playerPolicyRequest.OwnerId = source;
            playerPolicyRequest.PositionIdentity = source;
            Expect(playerPolicyService.PlaySoundGeneral(playerPolicyRequest),
                   "native sound-player equal-priority replacement");
        }
        Expect(playerPolicyService.Stats().ActiveSoundCount == 5 &&
                   playerPolicyService.Stats().StartedSoundCount == 6 &&
                   playerPolicyService.Stats().PreemptedSoundCount == 1,
               "native BCSAR sound-player voice limit");
        playerPolicyService.StopAll();
        const uint64_t playerPreemptions =
            playerPolicyService.Stats().PreemptedSoundCount;
        for (uint8_t slot = 1; slot <= 7; ++slot) {
            Expect(playerPolicyService.QueueSequencePlayer(
                       slot, 0x010005a9),
                   "native BGM sound-player equal-priority replacement");
        }
        Expect(playerPolicyService.Stats().ActiveSequencePlayerCount == 6 &&
                   playerPolicyService.Stats().PreemptedSoundCount ==
                       playerPreemptions + 1,
               "native BCSAR BGM-player voice limit");

        Oot3dNativeGame::NativeAudioService service(argv[1], argv[5], 44100);
        service.MountBehaviorCatalog(
            argv[3], Oot3dNativeGame::Oot3dEurRev0AudioBehaviorLayout());
        service.MountEnvelopeProfile(
            argv[3], Oot3dNativeGame::Oot3dEurRev0AudioEnvelopeLayout());
        service.MountModulationProfile(
            argv[3], Oot3dNativeGame::Oot3dEurRev0AudioModulationLayout());
        service.MountSpatialProfile(
            argv[3], Oot3dNativeGame::Oot3dEurRev0AudioSpatialLayout());
        service.MountMixProfile(
            argv[3], Oot3dNativeGame::Oot3dEurRev0AudioMixLayout());
        service.MountFilterProfile(
            argv[3], Oot3dNativeGame::Oot3dEurRev0AudioFilterLayout());
        service.MountResamplerProfile(argv[3]);
        service.MountSceneProfile(
            argv[3], Oot3dNativeGame::Oot3dEurRev0AudioSceneLayout());
        service.MountSoundSpecCatalog(
            argv[3], Oot3dNativeGame::Oot3dEurRev0AudioSoundSpecLayout());
        service.MountReverbProfile(
            argv[3], Oot3dNativeGame::Oot3dEurRev0AudioReverbLayout());
        Expect(service.MixProfile() != nullptr,
               "native audio service mix profile mount");
        Expect(service.FilterProfile() != nullptr,
               "native audio service filter profile mount");
        Expect(service.SceneProfile() != nullptr &&
                   service.SceneProfile()->NatureProfiles.size() == 20,
               "native audio service scene profile mount");
        Expect(service.SoundSpecCatalog() != nullptr &&
                   service.SoundSpecCatalog()->Profiles.size() == 18,
               "native audio service sound-spec mount");
        service.SetListener({0.0f, 0.0f, 0.0f},
                            {0.0f, 0.0f, -1.0f},
                            {0.0f, 1.0f, 0.0f});
        Expect(service.SoundCatalog().Sounds.size() == catalog.Sounds.size() &&
                   service.StreamCatalog() != nullptr &&
                   service.StreamCatalog()->Sounds.size() == 1,
               "native audio service archive mounting");
        const auto* behaviorCatalog = service.BehaviorCatalog();
        Expect(service.EnvelopeProfile() != nullptr &&
                   service.EnvelopeProfile()->GainCurve.size() == 965,
               "native audio service envelope profile");
        Expect(service.ModulationProfile() != nullptr &&
                   service.ModulationProfile()->UpdateMilliseconds == 5,
               "native audio service modulation profile");
        Expect(service.SpatialProfile() != nullptr &&
                   service.SpatialProfile()->PriorityScale == 32,
               "native audio service spatial profile");
        Expect(behaviorCatalog != nullptr &&
                   behaviorCatalog->Categories.size() == 7 &&
                   behaviorCatalog->Entries.size() == 1523,
               "native code.bin audio behavior catalog");
        const std::array<uint8_t, 7> expectedThresholds{3, 2, 3, 2, 2, 1, 1};
        for (size_t category = 0; category < expectedThresholds.size(); ++category) {
            Expect(behaviorCatalog->Categories[category].VoiceThreshold ==
                       expectedThresholds[category] &&
                       !behaviorCatalog->Categories[category].Disabled,
                   "native audio category policy");
        }
        const auto* waterBehavior = behaviorCatalog->Resolve(0x0100017f);
        Expect(waterBehavior != nullptr && waterBehavior->Category == 2 &&
                   waterBehavior->Parameter == 48 && waterBehavior->Flags == 0,
               "water-wall native behavior word");
        Expect(behaviorCatalog->RandomMultiplier == 0x0019660d &&
                   std::abs(behaviorCatalog->RandomPitchStep -
                            0.005208333488f) < 1.0e-8f &&
                   behaviorCatalog->RandomPitchBase == 1.0f,
               "native random-pitch constants");
        Oot3dNativeGame::NativeAudioRequest request;
        request.SoundId = 0x0100017f;
        request.OwnerId = 77;
        request.PositionIdentity = 77;
        request.HasPosition = true;
        request.Position = {1.0f, 2.0f, 3.0f};
        request.Parameter4 = 0.75f;
        request.Parameter5 = 1.25f;
        request.Parameter6 = -4;
        Expect(service.PlaySoundGeneral(request) &&
                   service.IsSoundActive(77, 77, request.SoundId),
               "native Audio_PlaySoundGeneral request start");
        const auto requestPitch = service.SoundPitchScale(
            request.PositionIdentity, request.SoundId);
        Expect(requestPitch.has_value() &&
                   std::abs(*requestPitch - request.Parameter4) < 1.0e-9,
               "native Audio_PlaySoundGeneral pitch reaches active voices");
        const auto initialGain = service.SoundGainScale(
            request.PositionIdentity, request.SoundId);
        const auto expectedInitialSpatial =
            Oot3dNativeGame::CalculateNativeAudioDistanceParameters(
                spatialProfile, *waterWall->Sound3d,
                std::sqrt(1.0f + 4.0f + 9.0f));
        Expect(initialGain.has_value() &&
                   std::abs(*initialGain - request.Parameter5 *
                       expectedInitialSpatial.Gain) < 0.000001,
               "native Sound3D distance gain reaches active playback");
        service.UpdateOwnerPosition(77, {0.0f, 0.0f, 0.0f});
        const auto movedGain = service.SoundGainScale(
            request.PositionIdentity, request.SoundId);
        Expect(movedGain.has_value() &&
                   std::abs(*movedGain - request.Parameter5) < 0.000001,
               "moving native source refreshes Sound3D distance gain");
        const auto serviceStartStats = service.Stats();
        Expect(serviceStartStats.ActiveSoundCount == 1 &&
                   serviceStartStats.StartedSoundCount == 1,
               "native audio service initial request accounting");
        service.MixStereo(128);
        request.Position = {4.0f, 5.0f, 6.0f};
        Expect(service.PlaySoundGeneral(request),
               "native Audio_PlaySoundGeneral request refresh");
        const auto serviceRefreshStats = service.Stats();
        Expect(serviceRefreshStats.ActiveSoundCount == 1 &&
                   serviceRefreshStats.RefreshedSoundCount == 1,
               "native sound identity refresh without restart");

        auto concurrentRequest = request;
        concurrentRequest.PositionIdentity = 78;
        Expect(service.PlaySoundGeneral(concurrentRequest) &&
                   service.Stats().ActiveSoundCount == 2,
               "native source identity supports concurrent same-ID sounds");
        uint32_t actorSfxRequest = request.SoundId;
        Expect(service.ConsumeActorSfxRequest(
                   88, actorSfxRequest, {7.0f, 8.0f, 9.0f}) &&
                   actorSfxRequest == 0 && service.Stats().ActiveSoundCount == 3,
               "native Actor+0x24 sound request consumption");
        service.StopOwner(77);
        Expect(service.Stats().ActiveSoundCount == 1 &&
                   service.IsSoundActive(88, 88, request.SoundId),
               "native audio owner lifecycle stop");

        Expect(service.ApplySceneAudio(1, 4, 0x7f) &&
                   service.CurrentSoundSpecId() == 1 &&
                   service.CurrentNatureAmbienceId().value_or(0xff) == 4 &&
                   service.Stats().ActiveSequencePlayerCount == 1 &&
                   service.Stats().ActiveNatureChannelCount == 7,
               "native scene nature ambience start");
        const auto* activeSceneDelay = std::get_if<
            Oot3dNativeGame::NativeAudioDelayEffect>(
                &service.AuxBusSettings(1).Effect);
        Expect(activeSceneDelay != nullptr &&
                   activeSceneDelay->FrameCount == 19 &&
                   activeSceneDelay->ChannelCount == 4,
               "native scene sound spec configures auxiliary bus B");
        Expect(service.SetNatureChannelIo(1, 1, 0) &&
                   service.SetNatureChannelIo(1, 1, 1),
               "native scene nature channel enable updates");
        Expect(service.SetNatureChannelIo(1, 2, 0x0d) &&
                   service.SetNatureChannelIo(1, 3, 0x20) &&
                   service.SetNatureChannelIo(1, 4, 1) &&
                   service.SetNatureChannelIo(1, 5, 0x2c),
               "native scene live nature channel IO updates");

        Expect(service.QueueSequencePlayer(0, request.SoundId, 15) &&
                   service.Stats().ActiveSequencePlayerCount == 1 &&
                   service.Stats().ActiveNatureChannelCount == 0 &&
                   service.SequencePlayerGain(0).value_or(-1.0) == 0.0,
               "native sequence-player fade-in queue");
        service.MixStereo(441);
        const double partialFadeGain =
            service.SequencePlayerGain(0).value_or(-1.0);
        Expect(partialFadeGain > 0.0 && partialFadeGain < 1.0,
               "native five-millisecond sequence fade clock");
        Expect(service.QueueSequencePlayer(0, request.SoundId, 0) &&
                   service.Stats().ActiveSequencePlayerCount == 1 &&
                   service.SequencePlayerGain(0).value_or(-1.0) == 1.0,
               "native sequence-player replacement");
        Expect(service.SetSequencePlayerVolume(0, 2, 64, 0),
               "native sequence-player volume layer update");
        const double layerGain =
            service.SequencePlayerGain(0).value_or(-1.0);
        const double expectedLayerGain =
            (64.0 / 127.0) * (64.0 / 127.0);
        Expect(std::abs(layerGain - expectedLayerGain) < 0.000001,
               "native squared sequence-player volume curve");
        Expect(service.SetSequencePlayerVolume(0, 2, 127, 10),
               "native sequence-player volume layer fade");
        service.MixStereo(1103);
        const double partialLayerGain =
            service.SequencePlayerGain(0).value_or(-1.0);
        Expect(partialLayerGain > layerGain && partialLayerGain < 1.0,
               "native sequence-player volume fade progress");
        service.MixStereo(2205);
        Expect(std::abs(service.SequencePlayerGain(0).value_or(-1.0) - 1.0) <
                   0.000001,
               "native sequence-player volume fade completion");
        Expect(service.QueueSequencePlayer(1, request.SoundId, 0),
               "native secondary sequence-player queue");
        service.StopSequencePlayer(1, 10);
        Expect(service.Stats().ActiveSequencePlayerCount == 2,
               "native sequence-player deferred stop remains active");
        service.MixStereo(2206);
        Expect(service.Stats().ActiveSequencePlayerCount == 1,
               "native sequence-player fade-out release");
        Expect(service.QueueStreamPlayer(0, 0x01000000) &&
                   service.Stats().ActiveStreamPlayerCount == 1,
               "native external stream-player queue");
        Expect(service.QueueStreamPlayer(1, 0x01000000) &&
                   service.Stats().ActiveStreamPlayerCount == 1,
               "native stream sound-player replacement");
        service.StopStreamPlayer(1, 10);
        service.MixStereo(2206);
        Expect(service.Stats().ActiveStreamPlayerCount == 0,
               "native stream-player fade-out release");
        Expect(service.QueueStreamPlayer(0, 0x01000000) &&
                   service.Stats().ActiveStreamPlayerCount == 1,
               "native stream player restart after release");
        bool serviceMixedNonZero = false;
        for (const auto sample : service.MixStereo(1024)) {
            serviceMixedNonZero |= sample != 0;
        }
        Expect(serviceMixedNonZero, "native audio service mixed output");
        service.SetOutputSampleRate(48000);
        Expect(service.Stats().ActiveSoundCount == 1 &&
                   service.Stats().ActiveSequencePlayerCount == 1 &&
                   service.Stats().ActiveStreamPlayerCount == 1 &&
                   service.MixProfile() != nullptr &&
                   service.FilterProfile() != nullptr,
               "native audio service sample-rate rebuild");
        Expect(!service.PlaySoundGeneral(
                   Oot3dNativeGame::NativeAudioRequest{0x01ffffff}),
               "native audio service rejects unresolved sound ID");
        service.StopAll();
        const auto stoppedStats = service.Stats();
        Expect(stoppedStats.ActiveSoundCount == 0 &&
                   stoppedStats.ActiveSequencePlayerCount == 0 &&
                   stoppedStats.ActiveStreamPlayerCount == 0,
               "native audio service global stop");

        Oot3dNativeGame::NativeAudioService policyService(
            argv[1], argv[5], 44100);
        policyService.MountBehaviorCatalog(
            argv[3], Oot3dNativeGame::Oot3dEurRev0AudioBehaviorLayout());
        policyService.MountEnvelopeProfile(
            argv[3], Oot3dNativeGame::Oot3dEurRev0AudioEnvelopeLayout());
        policyService.MountResamplerProfile(argv[3]);

        const Oot3dNativeGame::NativeAudioBehaviorEntry* randomBehavior = nullptr;
        for (const auto& [soundId, behavior] : behaviorCatalog->Entries) {
            (void)soundId;
            if ((behavior.Flags & 0xc0) != 0) {
                randomBehavior = &behavior;
                break;
            }
        }
        Expect(randomBehavior != nullptr, "native random-pitch behavior exists");
        policyService.SetBehaviorRandomState(1);
        Oot3dNativeGame::NativeAudioRequest randomRequest;
        randomRequest.SoundId = randomBehavior->SoundId;
        randomRequest.OwnerId = 900;
        randomRequest.PositionIdentity = 900;
        Expect(policyService.PlaySoundGeneral(randomRequest),
               "native random-pitch request start");
        const uint8_t randomBits = (randomBehavior->Flags & 0xc0) == 0x40 ? 4
            : (randomBehavior->Flags & 0xc0) == 0x80 ? 5 : 6;
        const uint32_t expectedRandomState =
            uint32_t{1} * behaviorCatalog->RandomMultiplier + (uint32_t{1} >> 1);
        const double expectedPitch =
            static_cast<double>(behaviorCatalog->RandomPitchBase) +
            (expectedRandomState & ((uint32_t{1} << randomBits) - 1)) *
                behaviorCatalog->RandomPitchStep;
        const auto actualPitch = policyService.SoundPitchScale(
            randomRequest.PositionIdentity, randomRequest.SoundId);
        Expect(actualPitch.has_value() &&
                   std::abs(*actualPitch - expectedPitch) < 1.0e-9,
               "native random-pitch state transition");
        policyService.StopAll();

        std::array<uint32_t, 2> exclusionSounds{};
        uint8_t exclusionCategory = 0xff;
        size_t exclusionCount = 0;
        for (const auto& [soundId, behavior] : behaviorCatalog->Entries) {
            if ((behavior.Flags & 0x20) == 0 ||
                (exclusionCount != 0 && behavior.Category != exclusionCategory)) {
                continue;
            }
            exclusionCategory = behavior.Category;
            exclusionSounds[exclusionCount++] = soundId;
            if (exclusionCount == exclusionSounds.size()) {
                break;
            }
        }
        Expect(exclusionCount == exclusionSounds.size(),
               "native mutually-exclusive sound behaviors exist");
        Oot3dNativeGame::NativeAudioRequest exclusionRequest;
        exclusionRequest.SoundId = exclusionSounds[0];
        exclusionRequest.OwnerId = 901;
        exclusionRequest.PositionIdentity = 901;
        Expect(policyService.PlaySoundGeneral(exclusionRequest),
               "native mutually-exclusive sound starts");
        exclusionRequest.SoundId = exclusionSounds[1];
        Expect(!policyService.PlaySoundGeneral(exclusionRequest) &&
                   policyService.Stats().RejectedSoundCount == 1,
               "native flag 0x20 source/category exclusion");
        policyService.StopAll();

        struct PolicyCandidate {
            uint32_t SoundId = 0;
            uint8_t Priority = 0;
        };
        std::vector<PolicyCandidate> preemptionCandidates;
        uint8_t preemptionCategory = 0xff;
        uint8_t preemptionThreshold = 0;
        for (uint8_t category = 0;
             category < behaviorCatalog->Categories.size(); ++category) {
            std::vector<PolicyCandidate> candidates;
            for (const auto& [soundId, behavior] : behaviorCatalog->Entries) {
                if (behavior.Category != category || (behavior.Flags & 0x20) != 0) {
                    continue;
                }
                const auto* sound = catalog.ResolveSoundId(soundId);
                if (sound != nullptr) {
                    candidates.push_back({soundId, sound->Priority()});
                }
            }
            const uint8_t threshold =
                behaviorCatalog->Categories[category].VoiceThreshold;
            if (threshold != 0 && candidates.size() > threshold) {
                std::sort(candidates.begin(), candidates.end(),
                          [](const auto& lhs, const auto& rhs) {
                              return lhs.Priority < rhs.Priority;
                          });
                preemptionCandidates = std::move(candidates);
                preemptionCategory = category;
                preemptionThreshold = threshold;
                break;
            }
        }
        Expect(preemptionCategory != 0xff,
               "native priority-preemption candidates exist");
        Oot3dNativeGame::NativeAudioRequest preemptionRequest;
        preemptionRequest.OwnerId = 902;
        preemptionRequest.PositionIdentity = 902;
        for (uint8_t i = 0; i < preemptionThreshold; ++i) {
            preemptionRequest.SoundId = preemptionCandidates[i].SoundId;
            Expect(policyService.PlaySoundGeneral(preemptionRequest),
                   "native priority-preemption threshold fill");
        }
        preemptionRequest.SoundId = preemptionCandidates.back().SoundId;
        Expect(policyService.PlaySoundGeneral(preemptionRequest) &&
                   policyService.Stats().ActiveSoundCount == preemptionThreshold &&
                   policyService.Stats().PreemptedSoundCount == 1,
               "native category priority preemption");
        policyService.StopAll();

        size_t resolvedSequenceCount = 0;
        size_t controlOnlyCount = 0;
        size_t audibleEventCount = 0;
        size_t silentEventCount = 0;
        size_t pitchedRegionCount = 0;
        size_t ignoreNoteOffCount = 0;
        size_t filteredEventCount = 0;
        size_t modulatedEventCount = 0;
        size_t nonSineModulationCount = 0;
        size_t delayedModulationCount = 0;
        size_t periodModulationCount = 0;
        size_t portamentoEventCount = 0;
        size_t sweepPitchEventCount = 0;
        size_t effectSendEventCount = 0;
        size_t controlEventCount = 0;
        size_t tiedNoteEventCount = 0;
        std::array<size_t, 256> interpolationTypeCounts{};
        std::array<size_t, Oot3dNativeGame::NativeCseqControlKindCount>
            cseqControlKindCounts{};
        std::array<size_t, 256> cseqCommandSequenceCount{};
        std::array<size_t, 256> cseqExtendedCommandSequenceCount{};
        std::array<size_t, 4> cseqTimeModifierSequenceCount{};
        std::array<std::array<size_t, 256>, 4> cseqTimedCommandSequenceCount{};
        std::array<size_t, 2> cseqTieCommandCount{};
        std::vector<std::string> unresolvedSequences;
        std::vector<std::string> silentEvents;
        for (const auto& sound : catalog.Sounds) {
            try {
                const auto sequence = Oot3dNativeGame::ResolveNativeAudioSequence(
                    archive, catalog, 0x01000000u | sound.Index, sound.Index + 1);
                ++resolvedSequenceCount;
                controlOnlyCount += sequence.ControlOnly ? 1 : 0;
                for (size_t opcode = 0; opcode < 256; ++opcode) {
                    cseqCommandSequenceCount[opcode] +=
                        sequence.CseqCommandUsage.test(opcode);
                    cseqExtendedCommandSequenceCount[opcode] +=
                        sequence.CseqExtendedCommandUsage.test(opcode);
                }
                for (size_t modifier = 0; modifier < 4; ++modifier) {
                    cseqTimeModifierSequenceCount[modifier] +=
                        sequence.CseqTimeModifierUsage.test(modifier);
                    for (size_t opcode = 0; opcode < 256; ++opcode) {
                        cseqTimedCommandSequenceCount[modifier][opcode] +=
                            sequence.CseqTimedCommandUsage[modifier].test(opcode);
                    }
                }
                cseqTieCommandCount[0] += sequence.CseqTieCommandCount[0];
                cseqTieCommandCount[1] += sequence.CseqTieCommandCount[1];
                controlEventCount += sequence.ControlEvents.size();
                for (const auto& control : sequence.ControlEvents) {
                    ++cseqControlKindCounts[static_cast<size_t>(control.Kind)];
                }
                for (const auto& event : sequence.Events) {
                    tiedNoteEventCount += event.Tie ? 1 : 0;
                    if (event.Silent) {
                        ++silentEventCount;
                        std::string description = sequence.Name + " program=" +
                            std::to_string(event.Cue.Program) + " note=" +
                            std::to_string(event.Cue.Note) + " velocity=" +
                            std::to_string(event.Cue.Velocity) + " transpose=" +
                            std::to_string(event.Cue.Transpose) + " bank_slot=" +
                            std::to_string(event.Cue.BankSlot) + " bank=" +
                            std::to_string(event.Cue.BankId);
                        if (event.Cue.BankId < catalog.Banks.size()) {
                            description += "(" + catalog.Banks[event.Cue.BankId].Name + ")";
                        }
                        description += " declared_banks=";
                        for (size_t bank = 0; bank < sound.BankIds.size(); ++bank) {
                            description += (bank == 0 ? "" : ",") +
                                std::to_string(sound.BankIds[bank]);
                        }
                        silentEvents.push_back(std::move(description));
                    } else {
                        ++audibleEventCount;
                        pitchedRegionCount +=
                            std::abs(event.Cue.Pitch - 1.0f) > 0.000001f;
                        ignoreNoteOffCount += event.Cue.IgnoreNoteOff;
                        filteredEventCount += event.Cue.RemoteFilter != 0 ||
                            event.Cue.LpfCutoff != 64 ||
                            event.Cue.BiquadType != 0 ||
                            event.Cue.BiquadValue != 0;
                        modulatedEventCount += event.Cue.ModDepth != 0;
                        nonSineModulationCount += event.Cue.ModCurve != 0;
                        delayedModulationCount += event.Cue.ModDelay != 0;
                        periodModulationCount += event.Cue.ModPeriod != 0;
                        portamentoEventCount +=
                            event.Cue.PortamentoEnabled ||
                            event.Cue.PortamentoTime != 0;
                        sweepPitchEventCount += event.Cue.SweepPitch != 0;
                        effectSendEventCount += event.Cue.FxSendA != 0 ||
                            event.Cue.FxSendB != 0 ||
                            event.Cue.FxSendC != 0 ||
                            event.Cue.MainSend != 127;
                        ++interpolationTypeCounts[event.Cue.InterpolationType];
                        Expect(event.Cue.Wave &&
                                   !event.Cue.Wave->InterleavedSamples.empty(),
                               "resolved native CSEQ event wave");
                    }
                }
            } catch (const std::exception& ex) {
                unresolvedSequences.push_back(sound.Name + ": " + ex.what());
            }
        }
        std::cout << "resolved native sequences: " << resolvedSequenceCount << "/"
                  << catalog.Sounds.size() << ", audible_events=" << audibleEventCount
                  << ", silent_events=" << silentEventCount
                  << ", control_only=" << controlOnlyCount
                  << ", pitched_regions=" << pitchedRegionCount
                  << ", ignore_note_off=" << ignoreNoteOffCount
                  << ", filtered=" << filteredEventCount
                  << ", modulated=" << modulatedEventCount
                  << ", non_sine_modulation=" << nonSineModulationCount
                  << ", delayed_modulation=" << delayedModulationCount
                  << ", period_modulation=" << periodModulationCount
                  << ", portamento=" << portamentoEventCount
                  << ", sweep_pitch=" << sweepPitchEventCount
                  << ", effect_sends=" << effectSendEventCount << '\n';
        std::cout << "native CBNK interpolation coverage:";
        for (size_t type = 0; type < interpolationTypeCounts.size(); ++type) {
            if (interpolationTypeCounts[type] != 0) {
                std::cout << " " << type << "=" << interpolationTypeCounts[type];
            }
        }
        std::cout << '\n';
        for (const auto& event : silentEvents) {
            std::cout << "silent native CSEQ event: " << event << '\n';
        }
        const std::vector<std::string> expectedSilentEvents = {
            "NA_SE_PL_DUMMY_192 program=27 note=72 velocity=100 transpose=0 "
                "bank_slot=0 bank=0(BANK_PLAYER) declared_banks=0",
            "NA_BGM_DEMO_SE_SEQ_C program=27 note=56 velocity=92 transpose=0 "
                "bank_slot=1 bank=4(BANK_SYSTEM) declared_banks=2,4,0",
            "NA_BGM_ITEM_GET program=7 note=77 velocity=1 transpose=0 "
                "bank_slot=0 bank=39(BANK_FANFARE) declared_banks=39",
            "NA_BGM_S_ITEM_GET program=7 note=77 velocity=1 transpose=0 "
                "bank_slot=0 bank=39(BANK_FANFARE) declared_banks=39",
        };
        Expect(silentEvents == expectedSilentEvents,
               "native CBNK intentional null-region coverage");
        Expect(controlEventCount != 0 && tiedNoteEventCount != 0,
               "native CSEQ control and tie event materialization");
        for (const auto kind : {
                 Oot3dNativeGame::NativeCseqControlKind::MasterVolume,
                 Oot3dNativeGame::NativeCseqControlKind::Expression,
                 Oot3dNativeGame::NativeCseqControlKind::PitchBend,
                 Oot3dNativeGame::NativeCseqControlKind::PitchBendRange,
                 Oot3dNativeGame::NativeCseqControlKind::FxSendA,
                 Oot3dNativeGame::NativeCseqControlKind::FxSendB,
                 Oot3dNativeGame::NativeCseqControlKind::ModDepth,
                 Oot3dNativeGame::NativeCseqControlKind::ModSpeed,
                 Oot3dNativeGame::NativeCseqControlKind::ModType,
                 Oot3dNativeGame::NativeCseqControlKind::ModRange,
             }) {
            Expect(cseqControlKindCounts[static_cast<size_t>(kind)] != 0,
                   "native CSEQ dynamic control kind coverage");
        }
        std::cout << "native CSEQ command sequence coverage:";
        for (size_t opcode = 0; opcode < 256; ++opcode) {
            if (cseqCommandSequenceCount[opcode] != 0) {
                std::cout << " " << std::hex << opcode << std::dec << "="
                          << cseqCommandSequenceCount[opcode];
            }
        }
        std::cout << '\n' << "native CSEQ extended command sequence coverage:";
        for (size_t opcode = 0; opcode < 256; ++opcode) {
            if (cseqExtendedCommandSequenceCount[opcode] != 0) {
                std::cout << " " << std::hex << opcode << std::dec << "="
                          << cseqExtendedCommandSequenceCount[opcode];
            }
        }
        std::cout << '\n' << "native CSEQ time modifier sequence coverage:"
                  << " fixed=" << cseqTimeModifierSequenceCount[1]
                  << " random=" << cseqTimeModifierSequenceCount[2]
                  << " variable=" << cseqTimeModifierSequenceCount[3] << '\n';
        for (size_t modifier = 1; modifier < 4; ++modifier) {
            std::cout << "native CSEQ timed command coverage " << modifier << ":";
            for (size_t opcode = 0; opcode < 256; ++opcode) {
                if (cseqTimedCommandSequenceCount[modifier][opcode] != 0) {
                    std::cout << " " << std::hex << opcode << std::dec << "="
                              << cseqTimedCommandSequenceCount[modifier][opcode];
                }
            }
            std::cout << '\n';
        }
        std::cout << "native CSEQ tie commands: off=" << cseqTieCommandCount[0]
                  << " on=" << cseqTieCommandCount[1]
                  << " controls=" << controlEventCount
                  << " tied_notes=" << tiedNoteEventCount << '\n';
        for (const auto& unresolved : unresolvedSequences) {
            std::cout << "  unresolved: " << unresolved << '\n';
        }
        Expect(resolvedSequenceCount == catalog.Sounds.size() &&
                   unresolvedSequences.empty(),
               "complete native sequence archive coverage");
        Expect(audibleEventCount > resolvedSequenceCount,
               "native sequence multi-event coverage");
        Expect(modulatedEventCount != 0 && portamentoEventCount != 0 &&
                   effectSendEventCount != 0,
               "native extended voice parameter corpus coverage");
        Expect(nonSineModulationCount == 0 && delayedModulationCount != 0 &&
                   periodModulationCount == 0 && sweepPitchEventCount != 0,
               "OoT3D native modulation command corpus profile");
        std::cout << "oot3d_native_audio_tests: ok\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "oot3d_native_audio_tests: " << ex.what() << '\n';
        return 1;
    }
}
