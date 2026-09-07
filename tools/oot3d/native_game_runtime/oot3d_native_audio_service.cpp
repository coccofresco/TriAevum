#include "oot3d_native_audio_service.h"

#include <bit>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace Oot3dNativeGame {
namespace {

std::vector<uint8_t> ReadBinary(const std::filesystem::path& path,
                                const char* role) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error(std::string("failed to open native ") + role +
                                 ": " + path.string());
    }
    const std::streamoff length = input.tellg();
    if (length < 0) {
        throw std::runtime_error(std::string("failed to size native ") + role);
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    input.seekg(0);
    if (!bytes.empty() &&
        !input.read(reinterpret_cast<char*>(bytes.data()), length)) {
        throw std::runtime_error(std::string("failed to read native ") + role);
    }
    return bytes;
}

size_t CodeOffset(std::span<const uint8_t> codeBin, uint32_t baseAddress,
                  uint32_t address, size_t size) {
    if (address < baseAddress) {
        throw std::runtime_error("native audio behavior address precedes code.bin");
    }
    const size_t offset = static_cast<size_t>(address - baseAddress);
    if (offset > codeBin.size() || size > codeBin.size() - offset) {
        throw std::runtime_error("native audio behavior address exceeds code.bin");
    }
    return offset;
}

uint32_t CodeU32(std::span<const uint8_t> codeBin, uint32_t baseAddress,
                 uint32_t address) {
    const size_t offset = CodeOffset(codeBin, baseAddress, address, 4);
    return static_cast<uint32_t>(codeBin[offset]) |
           (static_cast<uint32_t>(codeBin[offset + 1]) << 8) |
           (static_cast<uint32_t>(codeBin[offset + 2]) << 16) |
           (static_cast<uint32_t>(codeBin[offset + 3]) << 24);
}

uint16_t CodeU16(std::span<const uint8_t> codeBin, uint32_t baseAddress,
                 uint32_t address) {
    const size_t offset = CodeOffset(codeBin, baseAddress, address, 2);
    return static_cast<uint16_t>(codeBin[offset]) |
           static_cast<uint16_t>(codeBin[offset + 1] << 8);
}

uint8_t CodeU8(std::span<const uint8_t> codeBin, uint32_t baseAddress,
               uint32_t address) {
    return codeBin[CodeOffset(codeBin, baseAddress, address, 1)];
}

float CodeF32(std::span<const uint8_t> codeBin, uint32_t baseAddress,
              uint32_t address) {
    return std::bit_cast<float>(CodeU32(codeBin, baseAddress, address));
}

uint32_t RotateRight(uint32_t value, uint32_t shift) {
    shift &= 31;
    return shift == 0 ? value : (value >> shift) | (value << (32 - shift));
}

int32_t DecodeArmMovImmediate(uint32_t instruction) {
    if ((instruction & 0x0fe00000u) != 0x03a00000u) {
        throw std::runtime_error(
            "native audio spatial priority scale is not an ARM MOV immediate");
    }
    return static_cast<int32_t>(RotateRight(
        instruction & 0xffu, ((instruction >> 8) & 0xfu) * 2));
}

uint32_t DecodeArmMovImmediateAt(std::span<const uint8_t> codeBin,
                                 uint32_t baseAddress, uint32_t address,
                                 uint8_t destinationRegister) {
    const uint32_t instruction = CodeU32(codeBin, baseAddress, address);
    if (((instruction >> 12) & 0x0fu) != destinationRegister) {
        throw std::runtime_error(
            "native audio ARM MOV writes an unexpected register");
    }
    return static_cast<uint32_t>(DecodeArmMovImmediate(instruction));
}

std::pair<uint8_t, uint8_t> DecodeNatureChannelRange(uint8_t channelRange) {
    uint8_t first = channelRange >> 4;
    const uint8_t last = channelRange & 0x0f;
    if (first == 0) {
        first = last;
    }
    if (first > last || last >= 16) {
        throw std::runtime_error("native nature ambience channel range is invalid");
    }
    return {first, last};
}

NativeCseqInitialState BuildNatureCseqInitialState(
    const NativeAudioNatureProfile& profile, uint8_t channel) {
    if (channel >= 16) {
        throw std::runtime_error("native nature ambience channel is invalid");
    }
    NativeCseqInitialState state;
    const uint16_t channelBit = static_cast<uint16_t>(uint16_t{1} << channel);
    if ((profile.PlayerIo & channelBit) != 0 &&
        (profile.ChannelMask & channelBit) == 0) {
        state.SequenceVariables[1] = 1;
    }
    for (const NativeAudioNatureIo& io : profile.ChannelIo) {
        const auto [first, last] = DecodeNatureChannelRange(io.ChannelRange);
        if (channel >= first && channel <= last) {
            state.SequenceVariables[io.Port] = io.Value;
        }
    }
    return state;
}

} // namespace

NativeAudioReverbLayout Oot3dEurRev0AudioReverbLayout() {
    return {
        0x00100000,
        0x00465ad8,
        0x00465ae0,
        0x00465ae8,
        0x00465b40,
        0x00465b3c,
        0x00465b48,
        0x004131f8,
        {0x004131dc, 0x004131e4, 0x004131ec},
        0x00465b4c,
        0x00465b3c,
        0x00497c64,
        0x00497c68,
        0x00497c6c,
        0x00497c70,
        0x00497c74,
        0x00497c78,
        0x00497c7c,
        0x00497c80,
    };
}

NativeAudioReverbEffect ParseNativeAudioReverbEffect(
    std::span<const uint8_t> codeBin,
    const NativeAudioReverbLayout& layout) {
    if (layout.CodeBaseAddress == 0 ||
        layout.InitialDelayInstructionAddress == 0 ||
        layout.DecayTimeInstructionAddress == 0 ||
        layout.SecondaryDelayInstructionAddress == 0 ||
        layout.DelayTablePointerLiteralAddress == 0 ||
        layout.DelayTableInitializerPointerLiteralAddress == 0) {
        throw std::runtime_error("native audio reverb layout is incomplete");
    }

    const uint32_t delayTableAddress = CodeU32(
        codeBin, layout.CodeBaseAddress,
        layout.DelayTablePointerLiteralAddress);
    if (delayTableAddress == 0 || delayTableAddress != CodeU32(
            codeBin, layout.CodeBaseAddress,
            layout.DelayTableInitializerPointerLiteralAddress)) {
        throw std::runtime_error(
            "native audio reverb delay-table references disagree");
    }

    const uint32_t initialDelay = DecodeArmMovImmediateAt(
        codeBin, layout.CodeBaseAddress,
        layout.InitialDelayInstructionAddress, 0);
    const uint32_t decayTime = DecodeArmMovImmediateAt(
        codeBin, layout.CodeBaseAddress,
        layout.DecayTimeInstructionAddress, 0);
    const uint32_t secondaryDelay = DecodeArmMovImmediateAt(
        codeBin, layout.CodeBaseAddress,
        layout.SecondaryDelayInstructionAddress, 0);

    std::array<uint32_t, 3> delayTable{};
    for (size_t index = 0; index < delayTable.size(); ++index) {
        const uint32_t address =
            layout.DelayTableValueInstructionAddresses[index];
        if (address == 0) {
            throw std::runtime_error(
                "native audio reverb delay initializer is incomplete");
        }
        delayTable[index] = DecodeArmMovImmediateAt(
            codeBin, layout.CodeBaseAddress, address, 1);
        const uint32_t store = CodeU32(
            codeBin, layout.CodeBaseAddress, address + 4);
        if ((store & 0x0fff0000u) != 0x05800000u ||
            ((store >> 12) & 0x0fu) != 1 ||
            (store & 0x0fffu) != index * 4) {
            throw std::runtime_error(
                "native audio reverb delay initializer store is invalid");
        }
    }

    const float diffusion = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.DiffusionLiteralAddress);
    const float damping = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.DampingLiteralAddress);
    const float dryGain = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.DryGainLiteralAddress);
    const float wetGain = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.WetGainLiteralAddress);
    const float minimumDelay = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.MinimumDelayLiteralAddress);
    const float delayScale = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.DelayScaleLiteralAddress);
    const float millisecondsScale = CodeF32(
        codeBin, layout.CodeBaseAddress,
        layout.MillisecondsScaleLiteralAddress);
    const float decayExponent = CodeF32(
        codeBin, layout.CodeBaseAddress,
        layout.DecayExponentLiteralAddress);
    const float nativeSampleRate = CodeF32(
        codeBin, layout.CodeBaseAddress,
        layout.NativeSampleRateLiteralAddress);
    const float feedbackPowerBase = CodeF32(
        codeBin, layout.CodeBaseAddress,
        layout.FeedbackPowerBaseLiteralAddress);
    const float coefficientScale = CodeF32(
        codeBin, layout.CodeBaseAddress,
        layout.CoefficientScaleLiteralAddress);
    const float dampingMaximum = CodeF32(
        codeBin, layout.CodeBaseAddress,
        layout.DampingMaximumLiteralAddress);

    const std::array<float, 12> constants{
        diffusion, damping, dryGain, wetGain, minimumDelay, delayScale,
        millisecondsScale, decayExponent, nativeSampleRate,
        feedbackPowerBase, coefficientScale, dampingMaximum,
    };
    if (std::any_of(constants.begin(), constants.end(),
                    [](float value) { return !std::isfinite(value); }) ||
        initialDelay == 0 || decayTime == 0 || secondaryDelay == 0 ||
        diffusion < 0.0f || diffusion > 1.0f || damping < 0.0f ||
        dampingMaximum <= 0.0f || dampingMaximum > 1.0f ||
        dryGain < 0.0f || wetGain < 0.0f || minimumDelay <= 0.0f ||
        delayScale <= 0.0f || millisecondsScale <= 0.0f ||
        decayExponent >= 0.0f || nativeSampleRate <= 0.0f ||
        feedbackPowerBase <= 0.0f || coefficientScale <= 0.0f ||
        std::any_of(delayTable.begin(), delayTable.end(),
                    [](uint32_t value) { return value == 0; })) {
        throw std::runtime_error("native audio reverb constants are invalid");
    }

    NativeAudioReverbEffect result;
    result.Enabled = true;
    result.NativeSampleRate = static_cast<uint32_t>(nativeSampleRate);
    result.DelayLengths = {
        static_cast<uint32_t>(
            std::max(static_cast<float>(initialDelay), minimumDelay) *
            delayScale) * static_cast<uint32_t>(kNativeAudioDspFrameSize),
        static_cast<uint32_t>(
            std::max(static_cast<float>(secondaryDelay), minimumDelay) *
            delayScale) * static_cast<uint32_t>(kNativeAudioDspFrameSize),
        delayTable[0], delayTable[1], delayTable[2],
    };
    const float decaySamples = static_cast<float>(decayTime) *
                               millisecondsScale * nativeSampleRate;
    const auto feedback = [&](uint32_t delay) {
        const float exponent = static_cast<float>(delay) * decayExponent /
                               decaySamples;
        return static_cast<int16_t>(
            std::pow(feedbackPowerBase, exponent) * coefficientScale);
    };
    result.Feedback0 = feedback(result.DelayLengths[2]);
    result.Feedback1 = feedback(result.DelayLengths[3]);
    result.Diffusion = static_cast<int16_t>(diffusion * coefficientScale);
    result.Damping = static_cast<int16_t>(
        std::min(damping, dampingMaximum) * coefficientScale);
    result.WetGain = static_cast<int16_t>(wetGain * coefficientScale);
    result.DryGain = static_cast<int16_t>(dryGain * coefficientScale);
    return result;
}

NativeCseqInitialState BuildNativeNatureCseqInitialState(
    const NativeAudioNatureProfile& profile, uint8_t channel) {
    return BuildNatureCseqInitialState(profile, channel);
}

NativeAudioSoundSpecLayout Oot3dEurRev0AudioSoundSpecLayout() {
    return {
        0x00100000,
        0x00465ba4,
        0x00465ca0,
        0x00465cbc,
        0x00465cc0,
        0x00465bac,
        0x00465d38,
        0x00465d60,
        0x002d3c74,
        0x002d3c78,
        0x10,
        0x12,
    };
}

NativeAudioSoundSpecCatalog ParseNativeAudioSoundSpecCatalog(
    std::span<const uint8_t> codeBin,
    const NativeAudioSoundSpecLayout& layout) {
    if (layout.CodeBaseAddress == 0 || layout.BuilderBeginAddress == 0 ||
        layout.BuilderEndAddress <= layout.BuilderBeginAddress ||
        ((layout.BuilderEndAddress - layout.BuilderBeginAddress) & 3u) != 0 ||
        layout.ProfileStride < 8 || layout.ProfileCount == 0) {
        throw std::runtime_error("native audio sound-spec layout is incomplete");
    }

    struct RawProfile {
        uint32_t Duration = 0;
        uint32_t FeedbackBits = 0;
        bool HasDuration = false;
        bool HasFeedback = false;
    };
    std::vector<RawProfile> raw(layout.ProfileCount);
    std::array<uint32_t, 16> registers{};
    std::array<uint32_t, 32> scalarRegisters{};

    for (uint32_t address = layout.BuilderBeginAddress;
         address < layout.BuilderEndAddress; address += 4) {
        const uint32_t instruction = CodeU32(
            codeBin, layout.CodeBaseAddress, address);
        if ((instruction & 0x0fe00000u) == 0x03a00000u) {
            registers[(instruction >> 12) & 0xfu] =
                static_cast<uint32_t>(DecodeArmMovImmediate(instruction));
            continue;
        }
        if ((instruction & 0x0f3f0f00u) == 0x0d1f0a00u) {
            const uint32_t scalar = (((instruction >> 12) & 0xfu) << 1) |
                                    ((instruction >> 22) & 1u);
            const uint32_t displacement = (instruction & 0xffu) * 4;
            const uint32_t literalAddress = (instruction & (1u << 23)) != 0
                ? address + 8 + displacement
                : address + 8 - displacement;
            scalarRegisters[scalar] = CodeU32(
                codeBin, layout.CodeBaseAddress, literalAddress);
            continue;
        }
        if ((instruction & 0x0fff0000u) == 0x058c0000u) {
            const uint32_t offset = instruction & 0xfffu;
            if (offset < layout.ProfileStride * layout.ProfileCount &&
                offset % layout.ProfileStride == 0) {
                RawProfile& profile = raw[offset / layout.ProfileStride];
                profile.Duration = registers[(instruction >> 12) & 0xfu];
                profile.HasDuration = true;
            }
            continue;
        }
        if ((instruction & 0x0f3f0f00u) == 0x0d0c0a00u) {
            const uint32_t offset = (instruction & 0xffu) * 4;
            if (offset < layout.ProfileStride * layout.ProfileCount &&
                offset % layout.ProfileStride == 4) {
                const uint32_t scalar =
                    (((instruction >> 12) & 0xfu) << 1) |
                    ((instruction >> 22) & 1u);
                RawProfile& profile = raw[offset / layout.ProfileStride];
                profile.FeedbackBits = scalarRegisters[scalar];
                profile.HasFeedback = true;
            }
        }
    }

    const uint32_t scaleInstruction = CodeU32(
        codeBin, layout.CodeBaseAddress,
        layout.DurationScaleInstructionAddress);
    if ((scaleInstruction & 0x0fe00070u) != 0x01a00000u ||
        ((scaleInstruction >> 12) & 0xfu) !=
            (scaleInstruction & 0xfu)) {
        throw std::runtime_error(
            "native sound-spec duration scale instruction is invalid");
    }
    const uint32_t durationShift = (scaleInstruction >> 7) & 0x1fu;
    const uint32_t maximumInstruction = CodeU32(
        codeBin, layout.CodeBaseAddress,
        layout.DurationMaximumInstructionAddress);
    if ((maximumInstruction & 0x0ff00000u) != 0x03500000u) {
        throw std::runtime_error(
            "native sound-spec duration maximum instruction is invalid");
    }
    const uint32_t durationMaximum = RotateRight(
        maximumInstruction & 0xffu,
        ((maximumInstruction >> 8) & 0xfu) * 2);
    const uint32_t channelMode = static_cast<uint32_t>(DecodeArmMovImmediate(
        CodeU32(codeBin, layout.CodeBaseAddress,
                layout.ChannelModeInstructionAddress)));
    const uint8_t channelCount = channelMode == 0 ? 2 : 4;
    const float commonFeedback = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.CommonFeedbackAddress);
    const float feedbackMaximum = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.FeedbackMaximumAddress);
    const uint32_t frameMultiplier = CodeU32(
        codeBin, layout.CodeBaseAddress, layout.DelayFrameMultiplierAddress);
    const float coefficientScale = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.CoefficientScaleAddress);
    if (durationShift >= 32 || durationMaximum == 0 || channelCount > 4 ||
        !std::isfinite(commonFeedback) || commonFeedback < 0.0f ||
        commonFeedback > feedbackMaximum ||
        !std::isfinite(feedbackMaximum) || feedbackMaximum > 1.0f ||
        frameMultiplier == 0 || !std::isfinite(coefficientScale) ||
        coefficientScale <= 0.0f) {
        throw std::runtime_error("native audio sound-spec constants are invalid");
    }

    NativeAudioSoundSpecCatalog result;
    result.Profiles.reserve(raw.size());
    for (const RawProfile& source : raw) {
        if (!source.HasDuration || !source.HasFeedback) {
            throw std::runtime_error(
                "native audio sound-spec builder left an incomplete profile");
        }
        const uint32_t duration = std::min<uint32_t>(
            source.Duration << durationShift, durationMaximum);
        const float inputFeedback = std::min(
            std::bit_cast<float>(source.FeedbackBits), 1.0f);
        if (!std::isfinite(inputFeedback) || inputFeedback < 0.0f) {
            throw std::runtime_error(
                "native audio sound-spec feedback is invalid");
        }
        NativeAudioDelayEffect delay;
        delay.Enabled = true;
        delay.ChannelCount = channelCount;
        delay.FrameCount = std::max<uint32_t>(
            static_cast<uint32_t>(
                (static_cast<uint64_t>(duration) * 1000u * frameMultiplier) >>
                44),
            1);
        delay.G = static_cast<int16_t>(inputFeedback * coefficientScale);
        delay.A = static_cast<int16_t>(
            (1.0f - commonFeedback) * coefficientScale);
        delay.B = static_cast<int16_t>(commonFeedback * coefficientScale);
        result.Profiles.push_back({
            duration,
            inputFeedback,
            commonFeedback,
            {delay, 1.0f},
        });
    }
    return result;
}

NativeAudioSceneLayout Oot3dEurRev0AudioSceneLayout() {
    return {
        0x00100000,
        0x0054add2,
        0x0033c934,
        0x0033c93c,
        {
            0x0047d964,
            0x0047d968, 0x0047d968, 0x0047d968, 0x0047d968,
            0x0047d968, 0x0047d968, 0x0047d968, 0x0047d968,
            0x0047d968, 0x0047d968, 0x0047d968, 0x0047d968,
            0,
            0x0047d96c,
            0x0047d970,
        },
        0x68,
        20,
        100,
    };
}

NativeAudioSceneProfile ParseNativeAudioSceneProfile(
    std::span<const uint8_t> codeBin,
    const NativeAudioSceneLayout& layout) {
    if (layout.CodeBaseAddress == 0 ||
        layout.NatureProfileTableAddress == 0 ||
        layout.NatureSequenceSoundIdLiteralAddress == 0 ||
        layout.SpecialSequenceSoundIdLiteralAddress == 0 ||
        layout.NatureProfileStride < 5 || layout.NatureProfileCount == 0 ||
        layout.NatureIoCapacity == 0 ||
        layout.NatureIoCapacity > layout.NatureProfileStride - 4) {
        throw std::runtime_error("native audio scene layout is incomplete");
    }

    NativeAudioSceneProfile result;
    result.NatureSequenceSoundId = CodeU32(
        codeBin, layout.CodeBaseAddress,
        layout.NatureSequenceSoundIdLiteralAddress);
    result.SpecialSequenceSoundId = CodeU32(
        codeBin, layout.CodeBaseAddress,
        layout.SpecialSequenceSoundIdLiteralAddress);
    for (size_t channel = 0;
         channel < layout.NatureChannelSoundIdLiteralAddresses.size();
         ++channel) {
        const uint32_t address =
            layout.NatureChannelSoundIdLiteralAddresses[channel];
        if (address != 0) {
            result.NatureChannelSoundIds[channel] = CodeU32(
                codeBin, layout.CodeBaseAddress, address);
        }
    }
    result.NatureProfiles.reserve(layout.NatureProfileCount);
    for (uint32_t index = 0; index < layout.NatureProfileCount; ++index) {
        const uint32_t address = layout.NatureProfileTableAddress +
            index * layout.NatureProfileStride;
        NativeAudioNatureProfile profile;
        profile.PlayerIo = CodeU16(codeBin, layout.CodeBaseAddress, address);
        profile.ChannelMask = CodeU16(
            codeBin, layout.CodeBaseAddress, address + 2);
        bool terminated = false;
        for (uint32_t offset = 0; offset < layout.NatureIoCapacity;) {
            const uint8_t channelRange = CodeU8(
                codeBin, layout.CodeBaseAddress, address + 4 + offset);
            if (channelRange == 0xff) {
                terminated = true;
                break;
            }
            if (offset + 3 > layout.NatureIoCapacity) {
                break;
            }
            const uint8_t first = channelRange >> 4;
            const uint8_t last = channelRange & 0x0f;
            if ((first != 0 && first > last) || last >= 16) {
                throw std::runtime_error(
                    "native nature ambience channel range is invalid");
            }
            const uint8_t port = CodeU8(
                codeBin, layout.CodeBaseAddress, address + 5 + offset);
            if (port >= 16) {
                throw std::runtime_error(
                    "native nature ambience channel port is invalid");
            }
            profile.ChannelIo.push_back({
                channelRange,
                port,
                CodeU8(codeBin, layout.CodeBaseAddress,
                       address + 6 + offset),
            });
            offset += 3;
        }
        if (!terminated) {
            throw std::runtime_error(
                "native nature ambience profile lacks its terminator");
        }
        result.NatureProfiles.push_back(std::move(profile));
    }
    return result;
}

NativeAudioBehaviorLayout Oot3dEurRev0AudioBehaviorLayout() {
    return {
        0x00100000,
        0x0054ac00,
        0x0055a138,
        0x0054ac2c,
        0x0054ac25,
        0x0054ac1c,
        0x00375710,
        0x00375714,
        0x00375718,
        7,
    };
}

NativeAudioSpatialLayout Oot3dEurRev0AudioSpatialLayout() {
    return {
        0x00100000,
        0x00402ff0,
        0x00402ff4,
        0x00402ff8,
        0x00402ffc,
        0x004030a0,
        0x004030dc,
        0x004030e0,
        0x0040328c,
        0x00403290,
        0x00403954,
        0x0054abc0,
        0x0054abc4,
        0x00464af4,
        0x00464af8,
        0x00464afc,
    };
}

NativeAudioSpatialProfile ParseNativeAudioSpatialProfile(
    std::span<const uint8_t> codeBin,
    const NativeAudioSpatialLayout& layout) {
    if (layout.CodeBaseAddress == 0) {
        throw std::runtime_error("native audio spatial layout is incomplete");
    }
    NativeAudioSpatialProfile profile;
    profile.PanBaseAngle = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.PanBaseAngleAddress);
    profile.PanFrontAngle = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.PanFrontAngleAddress);
    profile.PanRearAngle = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.PanRearAngleAddress);
    profile.SurroundOffset = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.SurroundOffsetAddress);
    profile.PriorityScale = DecodeArmMovImmediate(CodeU32(
        codeBin, layout.CodeBaseAddress,
        layout.PriorityScaleInstructionAddress));
    profile.PanStrength = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.PanStrengthAddress);
    profile.DopplerBase = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.DopplerBaseAddress);
    const float unit = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.ListenerUnitAddress);
    profile.PanDistance = unit;
    profile.ReferenceDistance = unit;
    profile.DistanceScale = unit;
    profile.DistanceFilterScale = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.ListenerHalfAddress);
    profile.DistanceFilterMaximum = unit;
    profile.DopplerScale = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.DopplerScaleAddress);
    profile.InitialEnvironmentAuxA = static_cast<int32_t>(CodeU32(
        codeBin, layout.CodeBaseAddress, layout.EnvironmentAuxAAddress));
    profile.InitialEnvironmentAuxB = static_cast<int32_t>(CodeU32(
        codeBin, layout.CodeBaseAddress, layout.EnvironmentAuxBAddress));
    profile.AuxScale = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.AuxScaleAddress);
    profile.AuxNormalization = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.AuxNormalizationAddress);
    profile.AuxMaximum = CodeF32(
        codeBin, layout.CodeBaseAddress, layout.AuxMaximumAddress);
    if (!std::isfinite(profile.PanBaseAngle) ||
        !std::isfinite(profile.PanFrontAngle) ||
        !std::isfinite(profile.PanRearAngle) ||
        !std::isfinite(profile.SurroundOffset) ||
        !std::isfinite(profile.PanStrength) ||
        !std::isfinite(profile.DopplerBase) ||
        !std::isfinite(profile.PanDistance) || profile.PanDistance <= 0.0f ||
        profile.ReferenceDistance < 0.0f || profile.DistanceScale <= 0.0f ||
        profile.PriorityScale < 0 ||
        !std::isfinite(profile.DistanceFilterScale) ||
        !std::isfinite(profile.DistanceFilterMaximum) ||
        !std::isfinite(profile.DopplerScale) ||
        !std::isfinite(profile.AuxScale) ||
        !std::isfinite(profile.AuxNormalization) ||
        !std::isfinite(profile.AuxMaximum)) {
        throw std::runtime_error("native audio spatial profile is invalid");
    }
    return profile;
}

NativeAudioSpatialResult CalculateNativeAudioDistanceParameters(
    const NativeAudioSpatialProfile& profile,
    const NativeBcsarSound3dInfo& sound3d, float distance) {
    NativeAudioSpatialResult result;
    distance = std::max(0.0f, distance);
    if (distance > profile.ReferenceDistance) {
        const float normalized =
            (distance - profile.ReferenceDistance) / profile.DistanceScale;
        const uint8_t decayCurve = sound3d.DecayCurve == 2 ? 2 : 1;
        if (decayCurve == 1) {
            result.Gain = std::pow(sound3d.DecayRatio, normalized);
        } else {
            result.Gain = std::max(
                0.0f, 1.0f - normalized * (1.0f - sound3d.DecayRatio));
        }
        result.DistanceFilter = std::min(
            normalized * profile.DistanceFilterScale,
            profile.DistanceFilterMaximum);
    }
    result.PriorityAdjustment = -static_cast<int32_t>(
        (1.0f - result.Gain) * static_cast<float>(profile.PriorityScale));
    return result;
}

namespace {

float LinearMap(float value, float inputStart, float inputEnd,
                float outputStart, float outputEnd) {
    if (inputStart == inputEnd) {
        return (outputStart + outputEnd) * 0.5f;
    }
    return value * (outputStart - outputEnd) / (inputStart - inputEnd) +
           (inputStart * outputEnd - inputEnd * outputStart) /
               (inputStart - inputEnd);
}

} // namespace

NativeAudioSpatialResult CalculateNativeAudioPanParameters(
    const NativeAudioSpatialProfile& profile,
    const std::array<float, 3>& listenerSpacePosition) {
    NativeAudioSpatialResult result;
    const float distance = std::sqrt(
        listenerSpacePosition[0] * listenerSpacePosition[0] +
        listenerSpacePosition[1] * listenerSpacePosition[1] +
        listenerSpacePosition[2] * listenerSpacePosition[2]);
    float projectedX = 0.0f;
    float projectedZ = 0.0f;
    if (distance != 0.0f) {
        projectedX = listenerSpacePosition[0];
        projectedZ = listenerSpacePosition[2];
        const float horizontal = std::sqrt(
            projectedX * projectedX + projectedZ * projectedZ);
        if (horizontal > profile.PanDistance) {
            const float scale = profile.PanDistance / horizontal;
            projectedX *= scale;
            projectedZ *= scale;
        }
        const float projectedLength = std::sqrt(
            projectedX * projectedX + projectedZ * projectedZ);
        projectedX = listenerSpacePosition[0] * projectedLength / distance;
        projectedZ = listenerSpacePosition[2] * projectedLength / distance;
    }
    const float angle = std::atan2(projectedX, -projectedZ);
    const float radial = std::sqrt(
        projectedX * projectedX + projectedZ * projectedZ) /
        profile.PanDistance;
    const float front = profile.PanFrontAngle;
    const float rear = profile.PanRearAngle;
    const float halfPi = std::numbers::pi_v<float> * 0.5f;
    const float pi = std::numbers::pi_v<float>;
    float panCurve = 0.0f;
    float surroundCurve = 0.0f;
    if (angle < -rear) {
        panCurve = LinearMap(angle, -pi, -rear, 0.0f, -1.0f);
        surroundCurve = 1.0f;
    } else if (angle < -halfPi) {
        panCurve = -1.0f;
        surroundCurve = LinearMap(
            angle, -rear, -halfPi, 1.0f, 0.0f);
    } else if (angle < -front) {
        panCurve = -1.0f;
        surroundCurve = LinearMap(
            angle, -halfPi, -front, 0.0f, -1.0f);
    } else if (angle < front) {
        panCurve = LinearMap(angle, -front, front, -1.0f, 1.0f);
        surroundCurve = -1.0f;
    } else if (angle < halfPi) {
        panCurve = 1.0f;
        surroundCurve = LinearMap(
            angle, front, halfPi, -1.0f, 0.0f);
    } else if (angle < rear) {
        panCurve = 1.0f;
        surroundCurve = LinearMap(
            angle, halfPi, rear, 0.0f, 1.0f);
    } else {
        panCurve = LinearMap(angle, rear, pi, 1.0f, 0.0f);
        surroundCurve = 1.0f;
    }
    result.Pan = panCurve * profile.PanStrength * radial;
    const float midpoint =
        (std::sin(front) + std::sin(rear)) * 0.5f;
    result.SurroundPan =
        surroundCurve * profile.PanStrength * radial +
        (midpoint / (midpoint - std::sin(rear))) * (1.0f - radial) +
        1.0f + profile.SurroundOffset;
    return result;
}

float CalculateNativeAudioAuxSend(const NativeAudioSpatialProfile& profile,
                                  uint8_t mode, int8_t requestAux,
                                  int32_t environmentAuxA,
                                  int32_t environmentAuxB) {
    if (mode == 4) {
        return 0.0f;
    }
    const int32_t combined = std::min<int32_t>(
        environmentAuxA + environmentAuxB + requestAux, 127);
    return std::min(
        static_cast<float>(combined) * profile.AuxScale *
            profile.AuxNormalization,
        profile.AuxMaximum);
}

const NativeAudioBehaviorEntry* NativeAudioBehaviorCatalog::Resolve(
    uint32_t soundId) const {
    const auto entry = Entries.find(soundId);
    return entry == Entries.end() ? nullptr : &entry->second;
}

NativeAudioBehaviorCatalog ParseNativeAudioBehaviorCatalog(
    std::span<const uint8_t> codeBin,
    const NativeAudioBehaviorLayout& layout,
    uint32_t soundCount) {
    if (layout.CodeBaseAddress == 0 || layout.CategoryCount == 0 ||
        soundCount == 0) {
        throw std::runtime_error("native audio behavior layout is incomplete");
    }
    NativeAudioBehaviorCatalog catalog;
    catalog.InitialRandomState = CodeU32(
        codeBin, layout.CodeBaseAddress, layout.RandomStateAddress);
    catalog.RandomMultiplier = CodeU32(
        codeBin, layout.CodeBaseAddress, layout.RandomMultiplierLiteralAddress);
    catalog.RandomPitchStep = std::bit_cast<float>(CodeU32(
        codeBin, layout.CodeBaseAddress, layout.RandomPitchStepLiteralAddress));
    catalog.RandomPitchBase = std::bit_cast<float>(CodeU32(
        codeBin, layout.CodeBaseAddress, layout.RandomPitchBaseLiteralAddress));
    catalog.Categories.reserve(layout.CategoryCount);

    std::vector<uint32_t> categoryPointers;
    categoryPointers.reserve(layout.CategoryCount);
    for (uint8_t category = 0; category < layout.CategoryCount; ++category) {
        const uint32_t firstSoundId = CodeU32(
            codeBin, layout.CodeBaseAddress,
            layout.CategoryBaseTableAddress + category * 4);
        if ((firstSoundId >> 24) != 1 ||
            (firstSoundId & 0x00ffffff) > soundCount) {
            throw std::runtime_error(
                "native audio behavior category base is invalid");
        }
        if (!catalog.Categories.empty() &&
            firstSoundId <= catalog.Categories.back().FirstSoundId) {
            throw std::runtime_error(
                "native audio behavior category bases are not ordered");
        }
        catalog.Categories.push_back({
            firstSoundId,
            CodeU8(codeBin, layout.CodeBaseAddress,
                   layout.CategoryVoiceLimitTableAddress + category),
            CodeU8(codeBin, layout.CodeBaseAddress,
                   layout.CategoryDisabledTableAddress + category) != 0,
        });
        categoryPointers.push_back(CodeU32(
            codeBin, layout.CodeBaseAddress,
            layout.BehaviorPointerTableAddress + category * 4));
    }

    for (uint8_t category = 0; category < layout.CategoryCount; ++category) {
        const uint32_t first = catalog.Categories[category].FirstSoundId;
        const uint32_t end = category + 1 < layout.CategoryCount
            ? catalog.Categories[category + 1].FirstSoundId
            : 0x01000000u + soundCount;
        for (uint32_t soundId = first; soundId < end; ++soundId) {
            const uint32_t packed = CodeU32(
                codeBin, layout.CodeBaseAddress,
                categoryPointers[category] + (soundId - first) * 4);
            catalog.Entries.emplace(soundId, NativeAudioBehaviorEntry{
                soundId,
                category,
                static_cast<uint16_t>(packed & 0xffff),
                static_cast<uint16_t>(packed >> 16),
            });
        }
    }
    return catalog;
}

NativeAudioService::Archive NativeAudioService::LoadArchive(
    const std::filesystem::path& path, const char* role) {
    Archive archive;
    archive.Path = path;
    archive.Bytes = ReadBinary(path, role);
    archive.Catalog = ParseNativeBcsar(archive.Bytes);
    return archive;
}

NativeAudioService::NativeAudioService(
    std::filesystem::path soundArchivePath,
    std::optional<std::filesystem::path> streamArchivePath,
    uint32_t outputSampleRate)
    : mSoundArchive(LoadArchive(soundArchivePath, "QueenSound.bcsar")),
      mOutputSampleRate(outputSampleRate),
      mMixer(std::make_unique<NativeAudioMixer>(outputSampleRate)) {
    if (streamArchivePath.has_value()) {
        mStreamArchive = LoadArchive(*streamArchivePath, "QueenStream.bcsar");
    }
}

size_t NativeAudioService::SoundKeyHash::operator()(
    const SoundKey& key) const noexcept {
    size_t hash = std::hash<uint64_t>{}(key.PositionIdentity);
    hash ^= std::hash<uint32_t>{}(key.SoundId) + 0x9e3779b9u +
            (hash << 6) + (hash >> 2);
    return hash;
}

uint64_t NativeAudioService::AllocateMixerVoiceId() {
    if (mNextMixerVoiceId == std::numeric_limits<uint64_t>::max()) {
        throw std::runtime_error("native audio mixer voice ID space exhausted");
    }
    return mNextMixerVoiceId++;
}

std::shared_ptr<const NativeAudioSequence>
NativeAudioService::ResolveSequence(
    uint32_t soundId, const NativeCseqInitialState* initialState) {
    if (initialState == nullptr) {
        const auto cached = mSequenceCache.find(soundId);
        if (cached != mSequenceCache.end()) {
            return cached->second;
        }
    }
    const auto* sound = mSoundArchive.Catalog.ResolveSoundId(soundId);
    if (sound == nullptr || sound->Type != 0x2203) {
        return {};
    }
    auto sequence = std::make_shared<NativeAudioSequence>(
        ResolveNativeAudioSequence(mSoundArchive.Bytes, mSoundArchive.Catalog,
                                   soundId, 0, initialState));
    if (initialState != nullptr) {
        return sequence;
    }
    return mSequenceCache.emplace(soundId, std::move(sequence)).first->second;
}

std::shared_ptr<const NativeBcwav>
NativeAudioService::ResolveStream(uint32_t soundId) {
    const auto cached = mStreamCache.find(soundId);
    if (cached != mStreamCache.end()) {
        return cached->second;
    }
    if (!mStreamArchive.has_value()) {
        return {};
    }
    const auto* sound = mStreamArchive->Catalog.ResolveSoundId(soundId);
    if (sound == nullptr || sound->Type != 0x2201) {
        return {};
    }
    auto stream = std::make_shared<NativeBcwav>(ResolveNativeAudioStream(
        mStreamArchive->Path, mStreamArchive->Catalog, soundId));
    return mStreamCache.emplace(soundId, std::move(stream)).first->second;
}

bool NativeAudioService::PlaySoundGeneral(const NativeAudioRequest& request) {
    ++mSubmittedSoundRequestCount;
    const auto sequence = ResolveSequence(request.SoundId);
    if (!sequence) {
        return false;
    }

    const SoundKey key{request.PositionIdentity, request.SoundId};
    auto active = mActiveSounds.find(key);
    if (active != mActiveSounds.end() &&
        mMixer->IsVoiceActive(active->second.MixerVoiceId)) {
        active->second.Request = request;
        const auto spatial = SpatialParameters(request);
        active->second.SpatialGain = spatial.Gain;
        active->second.SpatialPan = spatial.Pan;
        active->second.SpatialSurroundPan = spatial.SurroundPan;
        active->second.DistanceFilter = spatial.DistanceFilter;
        active->second.SpatialAuxSend = spatial.AuxSend;
        RefreshActivePlayback(active->second);
        ++mRefreshedSoundCount;
        return true;
    }
    if (active != mActiveSounds.end()) {
        mActiveSounds.erase(active);
    }
    const auto* sound = mSoundArchive.Catalog.ResolveSoundId(request.SoundId);
    const auto* behavior = mBehaviorCatalog
        ? mBehaviorCatalog->Resolve(request.SoundId) : nullptr;
    NativeAudioBehaviorEntry fallbackBehavior;
    if (behavior == nullptr) {
        if (mBehaviorCatalog.has_value()) {
            ++mRejectedSoundCount;
            return false;
        }
        fallbackBehavior.SoundId = request.SoundId;
        behavior = &fallbackBehavior;
    }
    const auto spatial = SpatialParameters(request);
    const uint8_t basePriority = sound != nullptr ? sound->Priority() : 0x40;
    const uint8_t priority = static_cast<uint8_t>(std::clamp(
        static_cast<int32_t>(basePriority) + spatial.PriorityAdjustment,
        0, 0xff));
    if (!ApplyNativeVoicePolicy(key, *behavior, priority)) {
        ++mRejectedSoundCount;
        return false;
    }
    const double pitchScale = NextBehaviorPitchScale(behavior->Flags);
    const uint64_t mixerVoiceId = AllocateMixerVoiceId();
    mMixer->StartOrRefreshSequence(mixerVoiceId, sequence,
                                   pitchScale * request.Parameter4,
                                   request.Parameter5 * spatial.Gain,
                                   spatial.Pan, spatial.SurroundPan,
                                   spatial.DistanceFilter,
                                   spatial.AuxSend);
    mActiveSounds.emplace(key, ActiveSound{
        mixerVoiceId, request, behavior->Category, priority,
        behavior->Flags, pitchScale, spatial.Gain, spatial.Pan,
        spatial.SurroundPan, spatial.DistanceFilter, spatial.AuxSend});
    ++mStartedSoundCount;
    return true;
}

NativeAudioSpatialResult NativeAudioService::SpatialParameters(
    const NativeAudioRequest& request) const {
    if (!mSpatialProfile.has_value()) {
        return {};
    }
    NativeAudioSpatialResult result;
    result.AuxSend = CalculateNativeAudioAuxSend(
        *mSpatialProfile, request.Mode, request.Parameter6,
        mEnvironmentAuxA, mEnvironmentAuxB);
    if (!mListenerValid || !request.HasPosition) {
        return result;
    }
    const auto* sound = mSoundArchive.Catalog.ResolveSoundId(request.SoundId);
    if (sound == nullptr || !sound->Sound3d.has_value()) {
        return result;
    }
    const float x = request.Position[0] - mListenerPosition[0];
    const float y = request.Position[1] - mListenerPosition[1];
    const float z = request.Position[2] - mListenerPosition[2];
    const auto distance = CalculateNativeAudioDistanceParameters(
        *mSpatialProfile, *sound->Sound3d,
        std::sqrt(x * x + y * y + z * z));
    if ((sound->Sound3d->Flags & 1u) != 0) {
        result.Gain = distance.Gain;
    }
    if ((sound->Sound3d->Flags & 2u) != 0) {
        result.PriorityAdjustment = distance.PriorityAdjustment;
    }
    if ((sound->Sound3d->Flags & 0x10u) != 0) {
        result.DistanceFilter = distance.DistanceFilter;
    }
    if ((sound->Sound3d->Flags & (4u | 8u)) != 0) {
        const std::array<float, 3> listenerSpace{
            x * mListenerRight[0] + y * mListenerRight[1] +
                z * mListenerRight[2],
            x * mListenerUp[0] + y * mListenerUp[1] +
                z * mListenerUp[2],
            -(x * mListenerForward[0] + y * mListenerForward[1] +
              z * mListenerForward[2]),
        };
        const auto pan = CalculateNativeAudioPanParameters(
            *mSpatialProfile, listenerSpace);
        if ((sound->Sound3d->Flags & 4u) != 0) {
            result.Pan = pan.Pan;
        }
        if ((sound->Sound3d->Flags & 8u) != 0) {
            result.SurroundPan = pan.SurroundPan;
        }
    }
    return result;
}

void NativeAudioService::RefreshActivePlayback(ActiveSound& active) {
    const auto sequence = ResolveSequence(active.Request.SoundId);
    if (!sequence) {
        return;
    }
    if (const auto* sound =
            mSoundArchive.Catalog.ResolveSoundId(active.Request.SoundId)) {
        const auto spatial = SpatialParameters(active.Request);
        active.Priority = static_cast<uint16_t>(std::clamp(
            static_cast<int32_t>(sound->Priority()) +
                spatial.PriorityAdjustment,
            0, 0xff));
    }
    mMixer->StartOrRefreshSequence(
        active.MixerVoiceId, sequence,
        active.PitchScale * active.Request.Parameter4,
        active.Request.Parameter5 * active.SpatialGain,
        active.SpatialPan, active.SpatialSurroundPan,
        active.DistanceFilter,
        active.SpatialAuxSend);
}

bool NativeAudioService::ApplyNativeVoicePolicy(
    const SoundKey& key, const NativeAudioBehaviorEntry& behavior,
    uint8_t priority) {
    if (!mBehaviorCatalog.has_value()) {
        return ApplyNativeSoundPlayerPolicy(behavior.SoundId, priority);
    }
    if (behavior.Category >= mBehaviorCatalog->Categories.size() ||
        mBehaviorCatalog->Categories[behavior.Category].Disabled) {
        return false;
    }
    if ((behavior.Flags & 0x20) != 0) {
        for (const auto& [activeKey, active] : mActiveSounds) {
            if (activeKey.PositionIdentity == key.PositionIdentity &&
                active.Category == behavior.Category &&
                (active.Flags & 0x20) != 0) {
                return false;
            }
        }
    }

    const uint8_t threshold =
        mBehaviorCatalog->Categories[behavior.Category].VoiceThreshold;
    size_t sameSourceCategory = 0;
    auto preemption = mActiveSounds.end();
    for (auto it = mActiveSounds.begin(); it != mActiveSounds.end(); ++it) {
        if (it->first.PositionIdentity != key.PositionIdentity ||
            it->second.Category != behavior.Category) {
            continue;
        }
        ++sameSourceCategory;
        if (preemption == mActiveSounds.end() ||
            it->second.Priority < preemption->second.Priority) {
            preemption = it;
        }
    }
    if (threshold <= sameSourceCategory) {
        if (preemption == mActiveSounds.end() ||
            preemption->second.Priority > priority) {
            return false;
        }
        mMixer->StopVoice(preemption->second.MixerVoiceId);
        mActiveSounds.erase(preemption);
        ++mPreemptedSoundCount;
    }
    return ApplyNativeSoundPlayerPolicy(behavior.SoundId, priority);
}

bool NativeAudioService::ApplyNativeSoundPlayerPolicy(
    uint32_t soundId, uint8_t priority, uint64_t excludedMixerVoiceId) {
    const auto* sound = mSoundArchive.Catalog.ResolveSoundId(soundId);
    if (sound == nullptr) {
        return false;
    }
    const auto playerIndex = sound->PlayerIndex();
    const auto* player = mSoundArchive.Catalog.ResolvePlayerReference(
        sound->PlayerReference);
    if (!playerIndex.has_value() || player == nullptr) {
        return false;
    }

    enum class PreemptionKind : uint8_t {
        None,
        Sound,
        Sequence,
        Nature,
    };
    PreemptionKind preemptionKind = PreemptionKind::None;
    uint8_t preemptionPriority = 0xff;
    SoundKey preemptionSoundKey{};
    uint8_t preemptionSlot = 0;
    uint64_t preemptionVoiceId = 0;
    size_t activeCount = 0;
    const auto consider = [&](uint64_t mixerVoiceId, uint32_t activeSoundId,
                              uint8_t activePriority,
                              PreemptionKind kind, const SoundKey& soundKey,
                              uint8_t slot) {
        if (mixerVoiceId == excludedMixerVoiceId ||
            !mMixer->IsVoiceActive(mixerVoiceId)) {
            return;
        }
        const auto* activeSound = mSoundArchive.Catalog.ResolveSoundId(
            activeSoundId);
        if (activeSound == nullptr ||
            activeSound->PlayerIndex() != playerIndex) {
            return;
        }
        ++activeCount;
        if (preemptionKind == PreemptionKind::None ||
            activePriority < preemptionPriority) {
            preemptionKind = kind;
            preemptionPriority = activePriority;
            preemptionSoundKey = soundKey;
            preemptionSlot = slot;
            preemptionVoiceId = mixerVoiceId;
        }
    };
    for (auto it = mActiveSounds.begin(); it != mActiveSounds.end(); ++it) {
        consider(it->second.MixerVoiceId, it->second.Request.SoundId,
                 static_cast<uint8_t>(it->second.Priority),
                 PreemptionKind::Sound, it->first, 0);
    }
    for (const auto& [slot, active] : mSequencePlayers) {
        consider(active.MixerVoiceId, active.SoundId, active.Priority,
                 PreemptionKind::Sequence, {}, slot);
    }
    for (const auto& [slot, active] : mNatureChannels) {
        const auto* activeSound = mSoundArchive.Catalog.ResolveSoundId(
            active.SoundId);
        consider(active.MixerVoiceId, active.SoundId,
                 activeSound != nullptr ? activeSound->Priority() : 0,
                 PreemptionKind::Nature, {}, slot);
    }
    if (activeCount < player->PlayableSoundLimit) {
        return true;
    }
    if (preemptionKind == PreemptionKind::None ||
        preemptionPriority > priority) {
        return false;
    }
    mMixer->StopVoice(preemptionVoiceId);
    switch (preemptionKind) {
        case PreemptionKind::Sound:
            mActiveSounds.erase(preemptionSoundKey);
            break;
        case PreemptionKind::Sequence:
            mSequencePlayers.erase(preemptionSlot);
            break;
        case PreemptionKind::Nature:
            mNatureChannels.erase(preemptionSlot);
            if (mNatureChannels.empty()) {
                mNatureRuntimeStateAvailable = false;
            }
            break;
        case PreemptionKind::None:
            break;
    }
    ++mPreemptedSoundCount;
    return true;
}

bool NativeAudioService::ApplyNativeStreamPlayerPolicy(
    uint32_t soundId, uint8_t priority, uint64_t excludedMixerVoiceId) {
    if (!mStreamArchive.has_value()) {
        return false;
    }
    const auto* sound = mStreamArchive->Catalog.ResolveSoundId(soundId);
    if (sound == nullptr) {
        return false;
    }
    const auto playerIndex = sound->PlayerIndex();
    const auto* player = mStreamArchive->Catalog.ResolvePlayerReference(
        sound->PlayerReference);
    if (!playerIndex.has_value() || player == nullptr) {
        return false;
    }

    size_t activeCount = 0;
    auto preemption = mStreamPlayers.end();
    for (auto it = mStreamPlayers.begin(); it != mStreamPlayers.end(); ++it) {
        if (it->second.MixerVoiceId == excludedMixerVoiceId ||
            !mMixer->IsVoiceActive(it->second.MixerVoiceId)) {
            continue;
        }
        const auto* activeSound = mStreamArchive->Catalog.ResolveSoundId(
            it->second.SoundId);
        if (activeSound == nullptr ||
            activeSound->PlayerIndex() != playerIndex) {
            continue;
        }
        ++activeCount;
        if (preemption == mStreamPlayers.end() ||
            it->second.Priority < preemption->second.Priority) {
            preemption = it;
        }
    }
    if (activeCount < player->PlayableSoundLimit) {
        return true;
    }
    if (preemption == mStreamPlayers.end() ||
        preemption->second.Priority > priority) {
        return false;
    }
    mMixer->StopVoice(preemption->second.MixerVoiceId);
    mStreamPlayers.erase(preemption);
    ++mPreemptedSoundCount;
    return true;
}

double NativeAudioService::NextBehaviorPitchScale(uint16_t flags) {
    if (!mBehaviorCatalog.has_value() || (flags & 0xc0) == 0) {
        return 1.0;
    }
    const uint8_t randomBits = (flags & 0xc0) == 0x40 ? 4
        : (flags & 0xc0) == 0x80 ? 5 : 6;
    mBehaviorRandomState =
        mBehaviorRandomState * mBehaviorCatalog->RandomMultiplier +
        (mBehaviorRandomState >> 1);
    const uint32_t randomValue =
        mBehaviorRandomState & ((uint32_t{1} << randomBits) - 1);
    return static_cast<double>(mBehaviorCatalog->RandomPitchBase) +
           randomValue * mBehaviorCatalog->RandomPitchStep;
}

bool NativeAudioService::PlayActorSound2(
    uint64_t actorOwnerId, uint32_t soundId,
    const std::array<float, 3>& position) {
    NativeAudioRequest request;
    request.SoundId = soundId;
    request.OwnerId = actorOwnerId;
    request.PositionIdentity = actorOwnerId;
    request.HasPosition = true;
    request.Position = position;
    return PlaySoundGeneral(request);
}

bool NativeAudioService::ConsumeActorSfxRequest(
    uint64_t actorOwnerId, uint32_t& sfxRequest,
    const std::array<float, 3>& position) {
    if (sfxRequest == 0) {
        return false;
    }
    const uint32_t soundId = sfxRequest;
    sfxRequest = 0;
    return PlayActorSound2(actorOwnerId, soundId, position);
}

bool NativeAudioService::QueueSequencePlayer(
    uint8_t playerIndex, uint32_t soundId, uint32_t fadeInFrames) {
    const auto sequence = ResolveSequence(soundId);
    if (!sequence) {
        return false;
    }
    if (playerIndex == 0) {
        StopNatureChannels();
    }
    auto player = mSequencePlayers.find(playerIndex);
    const uint64_t existingVoiceId = player != mSequencePlayers.end()
        ? player->second.MixerVoiceId
        : 0;
    const auto* sound = mSoundArchive.Catalog.ResolveSoundId(soundId);
    const uint8_t priority = sound != nullptr ? sound->Priority() : 0;
    if (!ApplyNativeSoundPlayerPolicy(soundId, priority, existingVoiceId)) {
        return false;
    }
    if (player == mSequencePlayers.end()) {
        player = mSequencePlayers.emplace(
            playerIndex, ActivePlayer{AllocateMixerVoiceId()}).first;
    } else {
        mMixer->StopVoice(player->second.MixerVoiceId);
    }
    player->second.SoundId = soundId;
    player->second.FadeInFrames = fadeInFrames;
    player->second.Priority = priority;
    mMixer->StartOrRefreshSequence(player->second.MixerVoiceId, sequence,
                                   1.0, 1.0, 0.0, 0.0, 0.0, 0.0,
                                   fadeInFrames);
    return true;
}

bool NativeAudioService::SetSequenceTrackEnabled(
    uint8_t playerIndex, uint8_t channelRange, bool enabled) {
    const auto player = mSequencePlayers.find(playerIndex);
    if (player == mSequencePlayers.end()) {
        return false;
    }
    const auto [first, last] = DecodeNatureChannelRange(channelRange);
    return mMixer->SetSequenceTrackEnabled(
        player->second.MixerVoiceId, first, last, enabled);
}

bool NativeAudioService::StartNatureAmbience(uint8_t natureAmbienceId) {
    if (!mSceneProfile.has_value() ||
        natureAmbienceId >= mSceneProfile->NatureProfiles.size()) {
        return false;
    }
    constexpr uint8_t kMainSequencePlayer = 0;
    const NativeAudioNatureProfile& profile =
        mSceneProfile->NatureProfiles[natureAmbienceId];
    StopSequencePlayer(kMainSequencePlayer);
    StopNatureChannels();
    for (uint8_t channel = 0; channel < 16; ++channel) {
        if ((profile.PlayerIo & (uint16_t{1} << channel)) == 0) {
            continue;
        }
        const uint32_t soundId =
            mSceneProfile->NatureChannelSoundIds[channel];
        if (soundId == 0) {
            continue;
        }
        NativeCseqInitialState initialState =
            BuildNativeNatureCseqInitialState(profile, channel);
        const auto sequence = ResolveSequence(soundId, &initialState);
        if (!sequence) {
            StopNatureChannels();
            return false;
        }
        const auto* sound = mSoundArchive.Catalog.ResolveSoundId(soundId);
        if (sound == nullptr || !ApplyNativeSoundPlayerPolicy(
                soundId, sound->Priority())) {
            StopNatureChannels();
            return false;
        }
        NatureChannelPlayback playback;
        playback.MixerVoiceId = AllocateMixerVoiceId();
        playback.SoundId = soundId;
        playback.CycleState = initialState;
        playback.LiveState = std::move(initialState);
        mMixer->StartOrRefreshSequence(playback.MixerVoiceId, sequence);
        mNatureChannels.emplace(channel, std::move(playback));
    }
    if (mNatureChannels.empty()) {
        return false;
    }
    mCurrentNatureAmbienceId = natureAmbienceId;
    mNatureRuntimeStateAvailable = true;
    return true;
}

bool NativeAudioService::SetNatureChannelIo(
    uint8_t channelRange, uint8_t port, uint8_t value) {
    if (!mCurrentNatureAmbienceId.has_value() || port >= 16 ||
        !mNatureRuntimeStateAvailable) {
        return false;
    }
    const auto [first, last] = DecodeNatureChannelRange(channelRange);
    bool updated = false;
    for (uint8_t channel = first; channel <= last; ++channel) {
        auto found = mNatureChannels.find(channel);
        if (found == mNatureChannels.end()) {
            continue;
        }
        NatureChannelPlayback& playback = found->second;
        const auto timelineTick = mMixer->SequenceTimelineTick(
            playback.MixerVoiceId);
        const auto loopCount = mMixer->SequenceLoopCount(
            playback.MixerVoiceId);
        if (!timelineTick.has_value() || !loopCount.has_value()) {
            continue;
        }
        if (*loopCount != playback.ObservedLoopCount) {
            playback.CycleState = playback.LiveState;
            playback.CycleState.VariableUpdates.clear();
            playback.ObservedLoopCount = *loopCount;
        }
        playback.LiveState.SequenceVariables[port] = value;
        NativeCseqInitialState::VariableUpdate update;
        update.Tick = *timelineTick;
        update.FirstTrack = 0;
        update.LastTrack = 15;
        update.Variable = port;
        update.Value = value;
        update.VariableScope =
            NativeCseqInitialState::VariableUpdate::Scope::Sequence;
        playback.CycleState.VariableUpdates.push_back(update);
        const auto currentCycle = ResolveSequence(
            playback.SoundId, &playback.CycleState);
        const auto nextLoop = ResolveSequence(
            playback.SoundId, &playback.LiveState);
        if (!currentCycle || !nextLoop ||
            !mMixer->ReconfigureSequence(
                playback.MixerVoiceId, currentCycle, nextLoop)) {
            continue;
        }
        updated = true;
    }
    return updated;
}

bool NativeAudioService::ApplySceneAudio(
    uint8_t soundSpecId, uint8_t natureAmbienceId, uint32_t bgmSoundId) {
    constexpr uint8_t kMainSequencePlayer = 0;
    if (!ApplySoundSpec(soundSpecId)) {
        return false;
    }
    mCurrentSoundSpecId = soundSpecId;
    mCurrentNatureAmbienceId = natureAmbienceId;
    mNatureRuntimeStateAvailable = false;
    if (bgmSoundId == 0) {
        StopSequencePlayer(kMainSequencePlayer);
        return true;
    }
    if (bgmSoundId == 0x7f) {
        return StartNatureAmbience(natureAmbienceId);
    }
    return QueueSequencePlayer(kMainSequencePlayer, bgmSoundId);
}

bool NativeAudioService::QueueStreamPlayer(uint8_t playerIndex, uint32_t soundId,
                                           uint8_t volume, uint8_t pan) {
    const auto stream = ResolveStream(soundId);
    if (!stream) {
        return false;
    }
    auto player = mStreamPlayers.find(playerIndex);
    const uint64_t existingVoiceId = player != mStreamPlayers.end()
        ? player->second.MixerVoiceId
        : 0;
    const auto* sound = mStreamArchive->Catalog.ResolveSoundId(soundId);
    const uint8_t priority = sound != nullptr ? sound->Priority() : 0;
    if (!ApplyNativeStreamPlayerPolicy(soundId, priority, existingVoiceId)) {
        return false;
    }
    if (player == mStreamPlayers.end()) {
        player = mStreamPlayers.emplace(
            playerIndex, ActivePlayer{AllocateMixerVoiceId()}).first;
    } else {
        mMixer->StopVoice(player->second.MixerVoiceId);
    }
    player->second.SoundId = soundId;
    const uint8_t archiveVolume = sound != nullptr ? sound->ArchiveVolume : 127;
    player->second.Volume = static_cast<uint8_t>(
        (static_cast<uint32_t>(volume) * archiveVolume + 63) / 127);
    player->second.Pan = pan;
    player->second.Priority = priority;
    mMixer->StartOrRefreshStream(player->second.MixerVoiceId, stream,
                                 player->second.Volume, pan);
    return true;
}

bool NativeAudioService::SetSequencePlayerVolume(
    uint8_t playerIndex, uint8_t layer, uint8_t volume,
    uint32_t fadeFrames) {
    const auto player = mSequencePlayers.find(playerIndex);
    bool updated = player != mSequencePlayers.end() &&
        mMixer->SetSequencePlayerVolume(
            player->second.MixerVoiceId, layer, volume, fadeFrames);
    if (playerIndex == 0) {
        for (const auto& [channel, playback] : mNatureChannels) {
            (void)channel;
            updated = mMixer->SetSequencePlayerVolume(
                playback.MixerVoiceId, layer, volume, fadeFrames) || updated;
        }
    }
    return updated;
}

std::optional<double> NativeAudioService::SequencePlayerGain(
    uint8_t playerIndex) const {
    const auto player = mSequencePlayers.find(playerIndex);
    if (player != mSequencePlayers.end()) {
        return mMixer->SequencePlayerGain(player->second.MixerVoiceId);
    }
    if (playerIndex == 0 && !mNatureChannels.empty()) {
        return mMixer->SequencePlayerGain(
            mNatureChannels.begin()->second.MixerVoiceId);
    }
    return {};
}

void NativeAudioService::StopSequencePlayer(
    uint8_t playerIndex, uint32_t fadeOutFrames) {
    if (playerIndex == 0) {
        StopNatureChannels(fadeOutFrames);
    }
    const auto player = mSequencePlayers.find(playerIndex);
    if (player == mSequencePlayers.end()) {
        return;
    }
    mMixer->StopVoice(player->second.MixerVoiceId, fadeOutFrames);
    if (fadeOutFrames == 0) {
        mSequencePlayers.erase(player);
    }
}

void NativeAudioService::StopStreamPlayer(
    uint8_t playerIndex, uint32_t fadeOutFrames) {
    const auto player = mStreamPlayers.find(playerIndex);
    if (player == mStreamPlayers.end()) {
        return;
    }
    mMixer->StopVoice(player->second.MixerVoiceId, fadeOutFrames);
    if (fadeOutFrames == 0) {
        mStreamPlayers.erase(player);
    }
}

void NativeAudioService::StopOwner(uint64_t ownerId) {
    for (auto it = mActiveSounds.begin(); it != mActiveSounds.end();) {
        if (it->second.Request.OwnerId != ownerId) {
            ++it;
            continue;
        }
        mMixer->StopVoice(it->second.MixerVoiceId);
        it = mActiveSounds.erase(it);
    }
}

void NativeAudioService::StopAll() {
    mMixer->StopAll();
    mActiveSounds.clear();
    mSequencePlayers.clear();
    mNatureChannels.clear();
    mStreamPlayers.clear();
    mNatureRuntimeStateAvailable = false;
}

void NativeAudioService::StopNatureChannels(uint32_t fadeOutFrames) {
    for (const auto& [channel, playback] : mNatureChannels) {
        (void)channel;
        mMixer->StopVoice(playback.MixerVoiceId, fadeOutFrames);
    }
    if (fadeOutFrames == 0) {
        mNatureChannels.clear();
    }
    mNatureRuntimeStateAvailable = false;
}

void NativeAudioService::MountBehaviorCatalog(
    const std::filesystem::path& codeBinPath,
    const NativeAudioBehaviorLayout& layout) {
    const auto codeBin = ReadBinary(codeBinPath, "audio behavior code.bin");
    mBehaviorCatalog = ParseNativeAudioBehaviorCatalog(
        codeBin, layout, static_cast<uint32_t>(mSoundArchive.Catalog.Sounds.size()));
    mBehaviorRandomState = mBehaviorCatalog->InitialRandomState;
}

void NativeAudioService::MountEnvelopeProfile(
    const std::filesystem::path& codeBinPath,
    const NativeAudioEnvelopeLayout& layout) {
    const auto codeBin = ReadBinary(codeBinPath, "audio envelope code.bin");
    mEnvelopeProfile = std::make_shared<const NativeAudioEnvelopeProfile>(
        ParseNativeAudioEnvelopeProfile(codeBin, layout));
    mMixer->SetEnvelopeProfile(mEnvelopeProfile);
}

void NativeAudioService::MountModulationProfile(
    const std::filesystem::path& codeBinPath,
    const NativeAudioModulationLayout& layout) {
    const auto codeBin = ReadBinary(codeBinPath, "audio modulation code.bin");
    mModulationProfile = std::make_shared<const NativeAudioModulationProfile>(
        ParseNativeAudioModulationProfile(codeBin, layout));
    mMixer->SetModulationProfile(mModulationProfile);
}

void NativeAudioService::MountSpatialProfile(
    const std::filesystem::path& codeBinPath,
    const NativeAudioSpatialLayout& layout) {
    const auto codeBin = ReadBinary(codeBinPath, "audio spatial code.bin");
    mSpatialProfile = ParseNativeAudioSpatialProfile(codeBin, layout);
    mEnvironmentAuxA = mSpatialProfile->InitialEnvironmentAuxA;
    mEnvironmentAuxB = mSpatialProfile->InitialEnvironmentAuxB;
    for (auto& [key, active] : mActiveSounds) {
        (void)key;
        const auto spatial = SpatialParameters(active.Request);
        active.SpatialGain = spatial.Gain;
        active.SpatialPan = spatial.Pan;
        active.SpatialSurroundPan = spatial.SurroundPan;
        active.DistanceFilter = spatial.DistanceFilter;
        active.SpatialAuxSend = spatial.AuxSend;
        RefreshActivePlayback(active);
    }
}

void NativeAudioService::MountMixProfile(
    const std::filesystem::path& codeBinPath,
    const NativeAudioMixLayout& layout) {
    const auto codeBin = ReadBinary(codeBinPath, "audio mix code.bin");
    mMixProfile = std::make_shared<const NativeAudioMixProfile>(
        ParseNativeAudioMixProfile(codeBin, layout));
    mMixer->SetMixProfile(mMixProfile);
}

void NativeAudioService::MountFilterProfile(
    const std::filesystem::path& codeBinPath,
    const NativeAudioFilterLayout& layout) {
    const auto codeBin = ReadBinary(codeBinPath, "audio filter code.bin");
    mFilterProfile = std::make_shared<const NativeAudioFilterProfile>(
        ParseNativeAudioFilterProfile(codeBin, layout));
    mMixer->SetFilterProfile(mFilterProfile);
}

void NativeAudioService::MountResamplerProfile(
    const std::filesystem::path& codeBinPath) {
    const auto codeBin = ReadBinary(codeBinPath, "audio DSP code.bin");
    mResamplerProfile = std::make_shared<const NativeAudioResamplerProfile>(
        ParseNativeAudioResamplerProfile(codeBin));
    mMixer->SetResamplerProfile(mResamplerProfile);
}

void NativeAudioService::MountReverbProfile(
    const std::filesystem::path& codeBinPath,
    const NativeAudioReverbLayout& layout) {
    const auto codeBin = ReadBinary(codeBinPath, "audio reverb code.bin");
    mReverbProfile = ParseNativeAudioReverbEffect(codeBin, layout);
    ConfigureAuxBus(0, {*mReverbProfile, 1.0f});
}

void NativeAudioService::MountSceneProfile(
    const std::filesystem::path& codeBinPath,
    const NativeAudioSceneLayout& layout) {
    const auto codeBin = ReadBinary(codeBinPath, "audio scene code.bin");
    mSceneProfile = ParseNativeAudioSceneProfile(codeBin, layout);
    const auto requireSequence = [&](uint32_t soundId, const char* role) {
        const auto* sound = mSoundArchive.Catalog.ResolveSoundId(soundId);
        if (sound == nullptr || sound->Type != 0x2203) {
            throw std::runtime_error(std::string("native ") + role +
                                     " is not a BCSAR sequence cue");
        }
    };
    requireSequence(mSceneProfile->NatureSequenceSoundId,
                    "nature ambience sound ID");
    requireSequence(mSceneProfile->SpecialSequenceSoundId,
                    "special scene sound ID");
    for (uint32_t soundId : mSceneProfile->NatureChannelSoundIds) {
        if (soundId != 0) {
            requireSequence(soundId, "nature channel sound ID");
        }
    }
}

void NativeAudioService::MountSoundSpecCatalog(
    const std::filesystem::path& codeBinPath,
    const NativeAudioSoundSpecLayout& layout) {
    const auto codeBin = ReadBinary(codeBinPath, "audio sound-spec code.bin");
    mSoundSpecCatalog = ParseNativeAudioSoundSpecCatalog(codeBin, layout);
}

bool NativeAudioService::ApplySoundSpec(uint8_t soundSpecId) {
    if (!mSoundSpecCatalog.has_value()) {
        return true;
    }
    if (soundSpecId >= mSoundSpecCatalog->Profiles.size()) {
        return false;
    }
    ConfigureAuxBus(1, mSoundSpecCatalog->Profiles[soundSpecId].AuxBus);
    return true;
}

void NativeAudioService::SetOutputMode(NativeAudioOutputMode outputMode) {
    mOutputMode = outputMode;
    mMixer->SetOutputMode(outputMode);
}

void NativeAudioService::SetListener(
    const std::array<float, 3>& position,
    const std::array<float, 3>& target,
    const std::array<float, 3>& up) {
    mListenerPosition = position;
    const auto normalize = [](std::array<float, 3> value,
                              const std::array<float, 3>& fallback) {
        const float length = std::sqrt(value[0] * value[0] +
                                       value[1] * value[1] +
                                       value[2] * value[2]);
        if (length <= 0.000001f) {
            return fallback;
        }
        value[0] /= length;
        value[1] /= length;
        value[2] /= length;
        return value;
    };
    mListenerForward = normalize(
        {target[0] - position[0], target[1] - position[1],
         target[2] - position[2]},
        mListenerForward);
    mListenerRight = normalize(
        {mListenerForward[1] * up[2] - mListenerForward[2] * up[1],
         mListenerForward[2] * up[0] - mListenerForward[0] * up[2],
         mListenerForward[0] * up[1] - mListenerForward[1] * up[0]},
        mListenerRight);
    mListenerUp = normalize(
        {mListenerRight[1] * mListenerForward[2] -
             mListenerRight[2] * mListenerForward[1],
         mListenerRight[2] * mListenerForward[0] -
             mListenerRight[0] * mListenerForward[2],
         mListenerRight[0] * mListenerForward[1] -
             mListenerRight[1] * mListenerForward[0]},
        mListenerUp);
    mListenerValid = true;
    for (auto& [key, active] : mActiveSounds) {
        (void)key;
        const auto spatial = SpatialParameters(active.Request);
        active.SpatialGain = spatial.Gain;
        active.SpatialPan = spatial.Pan;
        active.SpatialSurroundPan = spatial.SurroundPan;
        active.DistanceFilter = spatial.DistanceFilter;
        active.SpatialAuxSend = spatial.AuxSend;
        RefreshActivePlayback(active);
    }
}

void NativeAudioService::UpdateOwnerPosition(
    uint64_t ownerId, const std::array<float, 3>& position) {
    for (auto& [key, active] : mActiveSounds) {
        (void)key;
        if (active.Request.OwnerId != ownerId ||
            !active.Request.HasPosition) {
            continue;
        }
        active.Request.Position = position;
        const auto spatial = SpatialParameters(active.Request);
        active.SpatialGain = spatial.Gain;
        active.SpatialPan = spatial.Pan;
        active.SpatialSurroundPan = spatial.SurroundPan;
        active.DistanceFilter = spatial.DistanceFilter;
        active.SpatialAuxSend = spatial.AuxSend;
        RefreshActivePlayback(active);
    }
}

void NativeAudioService::SetEnvironmentAux(int32_t auxA, int32_t auxB) {
    mEnvironmentAuxA = auxA;
    mEnvironmentAuxB = auxB;
    for (auto& [key, active] : mActiveSounds) {
        (void)key;
        const auto spatial = SpatialParameters(active.Request);
        active.SpatialGain = spatial.Gain;
        active.SpatialPan = spatial.Pan;
        active.SpatialSurroundPan = spatial.SurroundPan;
        active.DistanceFilter = spatial.DistanceFilter;
        active.SpatialAuxSend = spatial.AuxSend;
        RefreshActivePlayback(active);
    }
}

void NativeAudioService::SetBehaviorRandomState(uint32_t state) {
    mBehaviorRandomState = state;
}

void NativeAudioService::ConfigureAuxBus(
    size_t bus, const NativeAudioAuxBusSettings& settings) {
    if (bus >= mAuxBusSettings.size()) {
        throw std::out_of_range("native audio auxiliary bus");
    }
    mAuxBusSettings[bus] = settings;
    mMixer->ConfigureAuxBus(bus, settings);
}

const NativeAudioAuxBusSettings& NativeAudioService::AuxBusSettings(
    size_t bus) const {
    if (bus >= mAuxBusSettings.size()) {
        throw std::out_of_range("native audio auxiliary bus");
    }
    return mAuxBusSettings[bus];
}

void NativeAudioService::RestartActivePlayback() {
    for (const auto& [key, active] : mActiveSounds) {
        const auto sequence = ResolveSequence(key.SoundId);
        if (sequence) {
            mMixer->StartOrRefreshSequence(active.MixerVoiceId, sequence,
                                           active.PitchScale *
                                               active.Request.Parameter4,
                                           active.Request.Parameter5 *
                                               active.SpatialGain,
                                           active.SpatialPan,
                                           active.SpatialSurroundPan,
                                           active.DistanceFilter,
                                           active.SpatialAuxSend);
        }
    }
    for (const auto& [playerIndex, player] : mSequencePlayers) {
        (void)playerIndex;
        const auto sequence = ResolveSequence(player.SoundId);
        if (sequence) {
            mMixer->StartOrRefreshSequence(
                player.MixerVoiceId, sequence, 1.0, 1.0, 0.0, 0.0,
                0.0, 0.0, player.FadeInFrames);
        }
    }
    for (auto& [channel, playback] : mNatureChannels) {
        (void)channel;
        playback.CycleState = playback.LiveState;
        playback.CycleState.VariableUpdates.clear();
        playback.ObservedLoopCount = 0;
        const auto sequence = ResolveSequence(
            playback.SoundId, &playback.LiveState);
        if (sequence) {
            mMixer->StartOrRefreshSequence(
                playback.MixerVoiceId, sequence);
        }
    }
    for (const auto& [playerIndex, player] : mStreamPlayers) {
        (void)playerIndex;
        const auto stream = ResolveStream(player.SoundId);
        if (stream) {
            mMixer->StartOrRefreshStream(player.MixerVoiceId, stream,
                                         player.Volume, player.Pan);
        }
    }
}

void NativeAudioService::SetOutputSampleRate(uint32_t sampleRate) {
    if (sampleRate == mOutputSampleRate) {
        return;
    }
    if (sampleRate == 0) {
        throw std::runtime_error("native audio service output rate is zero");
    }
    mOutputSampleRate = sampleRate;
    mMixer = std::make_unique<NativeAudioMixer>(sampleRate);
    if (mEnvelopeProfile) {
        mMixer->SetEnvelopeProfile(mEnvelopeProfile);
    }
    if (mModulationProfile) {
        mMixer->SetModulationProfile(mModulationProfile);
    }
    if (mMixProfile) {
        mMixer->SetMixProfile(mMixProfile);
    }
    if (mFilterProfile) {
        mMixer->SetFilterProfile(mFilterProfile);
    }
    if (mResamplerProfile) {
        mMixer->SetResamplerProfile(mResamplerProfile);
    }
    mMixer->SetOutputMode(mOutputMode);
    for (size_t bus = 0; bus < mAuxBusSettings.size(); ++bus) {
        mMixer->ConfigureAuxBus(bus, mAuxBusSettings[bus]);
    }
    RestartActivePlayback();
}

void NativeAudioService::PruneCompleted() {
    for (auto it = mActiveSounds.begin(); it != mActiveSounds.end();) {
        if (mMixer->IsVoiceActive(it->second.MixerVoiceId)) {
            ++it;
        } else {
            it = mActiveSounds.erase(it);
        }
    }
    for (auto it = mSequencePlayers.begin(); it != mSequencePlayers.end();) {
        if (mMixer->IsVoiceActive(it->second.MixerVoiceId)) {
            ++it;
        } else {
            it = mSequencePlayers.erase(it);
        }
    }
    for (auto it = mNatureChannels.begin(); it != mNatureChannels.end();) {
        if (mMixer->IsVoiceActive(it->second.MixerVoiceId)) {
            ++it;
        } else {
            it = mNatureChannels.erase(it);
        }
    }
    if (mNatureChannels.empty()) {
        mNatureRuntimeStateAvailable = false;
    }
    for (auto it = mStreamPlayers.begin(); it != mStreamPlayers.end();) {
        if (mMixer->IsVoiceActive(it->second.MixerVoiceId)) {
            ++it;
        } else {
            it = mStreamPlayers.erase(it);
        }
    }
}

std::vector<int16_t> NativeAudioService::MixStereo(size_t frameCount) {
    auto output = mMixer->MixStereo(frameCount);
    PruneCompleted();
    return output;
}

NativeAudioServiceStats NativeAudioService::Stats() const {
    NativeAudioServiceStats stats;
    stats.CachedSequenceCount = mSequenceCache.size();
    stats.CachedStreamCount = mStreamCache.size();
    stats.ActiveSoundCount = mActiveSounds.size();
    stats.ActiveSequencePlayerCount = mSequencePlayers.size() +
        (!mNatureChannels.empty() ? 1 : 0);
    stats.ActiveNatureChannelCount = mNatureChannels.size();
    stats.ActiveStreamPlayerCount = mStreamPlayers.size();
    stats.SubmittedSoundRequestCount = mSubmittedSoundRequestCount;
    stats.StartedSoundCount = mStartedSoundCount;
    stats.RefreshedSoundCount = mRefreshedSoundCount;
    stats.BehaviorEntryCount = mBehaviorCatalog
        ? mBehaviorCatalog->Entries.size() : 0;
    stats.RejectedSoundCount = mRejectedSoundCount;
    stats.PreemptedSoundCount = mPreemptedSoundCount;
    stats.ActiveMixedVoiceCount = mMixer->ActiveMixedVoiceCount();
    stats.MaximumMixedVoiceCount = mMixer->MaximumMixedVoiceCount();
    stats.MixedVoiceSampleCount = mMixer->MixedVoiceSampleCount();
    for (const auto& [soundId, sequence] : mSequenceCache) {
        (void)soundId;
        if (!sequence) {
            continue;
        }
        stats.CachedSequenceEventCount += sequence->Events.size();
        uint64_t previousTick = 0;
        size_t eventsAtTick = 0;
        bool first = true;
        for (const auto& event : sequence->Events) {
            stats.CachedSequenceTickZeroEventCount += event.StartTick == 0 ? 1 : 0;
            if (first || event.StartTick != previousTick) {
                stats.MaximumSequenceEventsAtOneTick = std::max(
                    stats.MaximumSequenceEventsAtOneTick, eventsAtTick);
                previousTick = event.StartTick;
                eventsAtTick = 1;
                first = false;
            } else {
                ++eventsAtTick;
            }
        }
        stats.MaximumSequenceEventsAtOneTick = std::max(
            stats.MaximumSequenceEventsAtOneTick, eventsAtTick);
    }
    return stats;
}

bool NativeAudioService::IsSoundActive(uint64_t ownerId,
                                       uint64_t positionIdentity,
                                       uint32_t soundId) const {
    const auto active = mActiveSounds.find(SoundKey{positionIdentity, soundId});
    return active != mActiveSounds.end() &&
           active->second.Request.OwnerId == ownerId &&
           mMixer->IsVoiceActive(active->second.MixerVoiceId);
}

std::optional<double> NativeAudioService::SoundPitchScale(
    uint64_t positionIdentity, uint32_t soundId) const {
    const auto active = mActiveSounds.find(SoundKey{positionIdentity, soundId});
    if (active == mActiveSounds.end() ||
        !mMixer->IsVoiceActive(active->second.MixerVoiceId)) {
        return {};
    }
    return active->second.PitchScale * active->second.Request.Parameter4;
}

std::optional<double> NativeAudioService::SoundGainScale(
    uint64_t positionIdentity, uint32_t soundId) const {
    const auto active = mActiveSounds.find(SoundKey{positionIdentity, soundId});
    if (active == mActiveSounds.end() ||
        !mMixer->IsVoiceActive(active->second.MixerVoiceId)) {
        return {};
    }
    return active->second.Request.Parameter5 * active->second.SpatialGain;
}

const NativeBcsarCatalog& NativeAudioService::SoundCatalog() const {
    return mSoundArchive.Catalog;
}

const NativeBcsarCatalog* NativeAudioService::StreamCatalog() const {
    return mStreamArchive.has_value() ? &mStreamArchive->Catalog : nullptr;
}

const NativeAudioBehaviorCatalog* NativeAudioService::BehaviorCatalog() const {
    return mBehaviorCatalog ? &*mBehaviorCatalog : nullptr;
}

const NativeAudioEnvelopeProfile* NativeAudioService::EnvelopeProfile() const {
    return mEnvelopeProfile.get();
}

const NativeAudioModulationProfile* NativeAudioService::ModulationProfile() const {
    return mModulationProfile.get();
}

const NativeAudioSpatialProfile* NativeAudioService::SpatialProfile() const {
    return mSpatialProfile ? &*mSpatialProfile : nullptr;
}

const NativeAudioMixProfile* NativeAudioService::MixProfile() const {
    return mMixProfile.get();
}

const NativeAudioFilterProfile* NativeAudioService::FilterProfile() const {
    return mFilterProfile.get();
}

const NativeAudioReverbEffect* NativeAudioService::ReverbProfile() const {
    return mReverbProfile ? &*mReverbProfile : nullptr;
}

const NativeAudioSceneProfile* NativeAudioService::SceneProfile() const {
    return mSceneProfile ? &*mSceneProfile : nullptr;
}

const NativeAudioSoundSpecCatalog* NativeAudioService::SoundSpecCatalog() const {
    return mSoundSpecCatalog ? &*mSoundSpecCatalog : nullptr;
}

uint8_t NativeAudioService::CurrentSoundSpecId() const {
    return mCurrentSoundSpecId;
}

std::optional<uint8_t> NativeAudioService::CurrentNatureAmbienceId() const {
    return mCurrentNatureAmbienceId;
}

} // namespace Oot3dNativeGame
