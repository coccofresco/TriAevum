#include "oot3d_native_audio_resampler.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {

uint16_t ReadU16(std::span<const uint8_t> bytes, size_t offset) {
    if (offset > bytes.size() || 2 > bytes.size() - offset) {
        throw std::runtime_error("truncated native DSP1 u16");
    }
    return static_cast<uint16_t>(bytes[offset]) |
           static_cast<uint16_t>(bytes[offset + 1] << 8);
}

uint32_t ReadU32(std::span<const uint8_t> bytes, size_t offset) {
    if (offset > bytes.size() || 4 > bytes.size() - offset) {
        throw std::runtime_error("truncated native DSP1 u32");
    }
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

bool IsPolyphaseTable(
    const std::array<std::array<int16_t,
                               NativeAudioResamplerProfile::TapCount>,
                     NativeAudioResamplerProfile::PhaseCount>& coefficients) {
    for (size_t phase = 0;
         phase < NativeAudioResamplerProfile::PhaseCount; ++phase) {
        int32_t sum = 0;
        for (const int16_t coefficient : coefficients[phase]) {
            if (coefficient < 0) {
                return false;
            }
            sum += coefficient;
        }
        if (sum < 32000 || sum > 33500) {
            return false;
        }
        const auto& mirror = coefficients[
            NativeAudioResamplerProfile::PhaseCount - 1 - phase];
        for (size_t tap = 0;
             tap < NativeAudioResamplerProfile::TapCount; ++tap) {
            if (coefficients[phase][tap] !=
                mirror[NativeAudioResamplerProfile::TapCount - 1 - tap]) {
                return false;
            }
        }
    }
    return coefficients.front()[1] > coefficients.front()[0] &&
           coefficients.front()[1] > coefficients.front()[2];
}

} // namespace

NativeAudioResamplerProfile ParseNativeAudioResamplerProfile(
    std::span<const uint8_t> codeBin) {
    constexpr size_t kDspSignatureSize = 0x100;
    constexpr size_t kDspHeaderSize = 0x300;
    constexpr size_t kSegmentCountOffset = 0x10e;
    constexpr size_t kSegmentTableOffset = 0x120;
    constexpr size_t kSegmentSize = 0x30;
    constexpr size_t kSegmentTypeOffset = 0x0f;
    constexpr uint8_t kDataSegment = 2;
    constexpr size_t kTableByteSize =
        NativeAudioResamplerProfile::PhaseCount *
        NativeAudioResamplerProfile::TapCount * sizeof(int16_t);

    std::optional<NativeAudioResamplerProfile> result;
    for (size_t magicOffset = kDspSignatureSize;
         magicOffset + 8 <= codeBin.size(); ++magicOffset) {
        if (std::memcmp(codeBin.data() + magicOffset, "DSP1", 4) != 0) {
            continue;
        }
        const size_t componentOffset = magicOffset - kDspSignatureSize;
        const uint32_t componentSize = ReadU32(codeBin, magicOffset + 4);
        if (componentSize < kDspHeaderSize ||
            componentOffset > codeBin.size() ||
            componentSize > codeBin.size() - componentOffset) {
            continue;
        }
        const auto component = codeBin.subspan(componentOffset, componentSize);
        const uint8_t segmentCount = component[kSegmentCountOffset];
        if (segmentCount == 0 || segmentCount > 10 ||
            kSegmentTableOffset + segmentCount * kSegmentSize >
                component.size()) {
            continue;
        }
        for (size_t segmentIndex = 0; segmentIndex < segmentCount;
             ++segmentIndex) {
            const size_t descriptor =
                kSegmentTableOffset + segmentIndex * kSegmentSize;
            if (component[descriptor + kSegmentTypeOffset] != kDataSegment) {
                continue;
            }
            const uint32_t segmentOffset = ReadU32(component, descriptor);
            const uint32_t segmentWordAddress =
                ReadU32(component, descriptor + 4);
            const uint32_t segmentByteSize =
                ReadU32(component, descriptor + 8);
            if (segmentOffset > component.size() ||
                segmentByteSize > component.size() - segmentOffset ||
                segmentByteSize < kTableByteSize) {
                continue;
            }
            for (size_t relative = 0;
                 relative + kTableByteSize <= segmentByteSize;
                 relative += sizeof(int16_t)) {
                NativeAudioResamplerProfile candidate;
                candidate.DspComponentOffset =
                    static_cast<uint32_t>(componentOffset);
                candidate.DspComponentSize = componentSize;
                candidate.PolyphaseTableOffset =
                    segmentOffset + static_cast<uint32_t>(relative);
                candidate.PolyphaseTableWordAddress =
                    segmentWordAddress + static_cast<uint32_t>(relative / 2);
                size_t cursor = segmentOffset + relative;
                for (auto& phase : candidate.PolyphaseCoefficients) {
                    for (int16_t& coefficient : phase) {
                        coefficient = static_cast<int16_t>(
                            ReadU16(component, cursor));
                        cursor += sizeof(int16_t);
                    }
                }
                if (!IsPolyphaseTable(candidate.PolyphaseCoefficients)) {
                    continue;
                }
                if (result.has_value()) {
                    throw std::runtime_error(
                        "native DSP1 has multiple polyphase coefficient tables");
                }
                result = candidate;
            }
        }
    }
    if (!result.has_value()) {
        throw std::runtime_error(
            "native DSP1 polyphase coefficient table was not found");
    }
    return *result;
}

} // namespace Oot3dNativeGame
