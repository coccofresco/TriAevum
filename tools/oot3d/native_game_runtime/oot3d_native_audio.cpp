// DSP-ADPCM decoding arithmetic is derived from Azahar/Citra audio_core/codec.cpp.
// Copyright 2016 Citra Emulator Project. Licensed under GPLv2 or any later version.

#include "oot3d_native_audio.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {

class Reader {
  public:
    explicit Reader(std::span<const uint8_t> bytes) : mBytes(bytes) {}

    void Require(size_t offset, size_t size, const char* role) const {
        if (offset > mBytes.size() || size > mBytes.size() - offset) {
            throw std::runtime_error(std::string("truncated native audio ") + role);
        }
    }

    uint8_t U8(size_t offset) const {
        Require(offset, 1, "u8");
        return mBytes[offset];
    }

    uint16_t U16(size_t offset) const {
        Require(offset, 2, "u16");
        return static_cast<uint16_t>(mBytes[offset]) |
               static_cast<uint16_t>(mBytes[offset + 1] << 8);
    }

    int16_t S16(size_t offset) const {
        return static_cast<int16_t>(U16(offset));
    }

    uint32_t U32(size_t offset) const {
        Require(offset, 4, "u32");
        return static_cast<uint32_t>(mBytes[offset]) |
               (static_cast<uint32_t>(mBytes[offset + 1]) << 8) |
               (static_cast<uint32_t>(mBytes[offset + 2]) << 16) |
               (static_cast<uint32_t>(mBytes[offset + 3]) << 24);
    }

    float F32(size_t offset) const {
        return std::bit_cast<float>(U32(offset));
    }

    std::span<const uint8_t> Slice(size_t offset, size_t size) const {
        Require(offset, size, "slice");
        return mBytes.subspan(offset, size);
    }

    bool Magic(size_t offset, const char (&magic)[5]) const {
        Require(offset, 4, "magic");
        return std::memcmp(mBytes.data() + offset, magic, 4) == 0;
    }

  private:
    std::span<const uint8_t> mBytes;
};

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("failed to open native audio file: " + path.string());
    }
    const auto length = input.tellg();
    if (length < 0) {
        throw std::runtime_error("failed to size native audio file: " + path.string());
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    input.seekg(0);
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), length)) {
        throw std::runtime_error("failed to read native audio file: " + path.string());
    }
    return bytes;
}

size_t CheckedAdd(size_t lhs, uint32_t rhs, const char* role) {
    if (rhs > std::numeric_limits<size_t>::max() - lhs) {
        throw std::runtime_error(std::string("native audio offset overflow: ") + role);
    }
    return lhs + rhs;
}

std::string ReadCString(const Reader& reader, size_t offset, uint32_t size) {
    const auto bytes = reader.Slice(offset, size);
    const auto end = std::find(bytes.begin(), bytes.end(), uint8_t{0});
    return std::string(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<size_t>(end - bytes.begin()));
}

struct BcwavChannel {
    size_t SampleOffset = 0;
    std::array<int16_t, 16> Coefficients{};
    int16_t History1 = 0;
    int16_t History2 = 0;
    int16_t ImaStepIndex = 0;
};

struct BankNote {
    uint32_t WaveArchive = 0;
    uint32_t Wave = 0;
    uint32_t RootKey = 60;
    uint32_t Volume = 127;
    uint32_t Pan = 64;
    float Pitch = 1.0f;
    int8_t SurroundPan = 0;
    uint8_t InterpolationType = 0;
    bool IgnoreNoteOff = false;
    uint8_t Attack = 127;
    uint8_t Decay = 127;
    uint8_t Sustain = 127;
    uint8_t Hold = 127;
    uint8_t Release = 127;
};

std::optional<size_t> ResolveBankVariant(const Reader& reader, size_t reference,
                                         uint8_t selector) {
    const uint32_t type = reader.U32(reference);
    const size_t data = CheckedAdd(reference, reader.U32(reference + 4),
                                   "CBNK variant data");
    if (type == 0x6000) {
        return CheckedAdd(data, reader.U32(data + 4), "CBNK direct region");
    }
    if (type == 0x6001) {
        const uint32_t rangeCount = reader.U32(data);
        const size_t rangeEnds = data + 4;
        for (uint32_t index = 0; index < rangeCount; ++index) {
            if (selector <= reader.U8(rangeEnds + index)) {
                const size_t references = rangeEnds + ((rangeCount + 3) & ~uint32_t{3});
                return CheckedAdd(data, reader.U32(references + index * 8 + 4),
                                  "CBNK range region");
            }
        }
        return std::nullopt;
    }
    if (type == 0x6002) {
        const uint8_t start = reader.U8(data);
        const uint8_t end = reader.U8(data + 1);
        if (selector < start || selector > end) {
            return std::nullopt;
        }
        const size_t referenceOffset = data + 4 +
            static_cast<size_t>(selector - start) * 8;
        return CheckedAdd(data, reader.U32(referenceOffset + 4),
                          "CBNK indexed region");
    }
    throw std::runtime_error("unsupported native CBNK resource variant");
}

std::optional<BankNote> ResolveBankNote(std::span<const uint8_t> bytes,
                                        uint8_t program, uint8_t note,
                                        uint8_t velocity) {
    Reader reader(bytes);
    if (!reader.Magic(0, "CBNK") || reader.U16(4) != 0xfeff) {
        throw std::runtime_error("unsupported native CBNK header");
    }
    const size_t info = reader.U32(24);
    if (!reader.Magic(info, "INFO") || reader.U32(info + 8) != 0x100 ||
        reader.U32(info + 16) != 0x101) {
        throw std::runtime_error("invalid native CBNK INFO section");
    }
    const size_t waveTable = CheckedAdd(info + 8, reader.U32(info + 12), "CBNK waves");
    const size_t instrumentTable = CheckedAdd(info + 8, reader.U32(info + 20),
                                              "CBNK instruments");
    const uint32_t instrumentCount = reader.U32(instrumentTable);
    if (program >= instrumentCount) {
        return std::nullopt;
    }
    const size_t instrumentRef = instrumentTable + 4 + static_cast<size_t>(program) * 8;
    if (reader.U32(instrumentRef) != 0x5900) {
        return std::nullopt;
    }
    const size_t instrument = CheckedAdd(info + 24, reader.U32(instrumentRef + 4),
                                         "CBNK instrument");
    const auto noteOffset = ResolveBankVariant(reader, instrument, note);
    if (!noteOffset.has_value()) {
        return std::nullopt;
    }
    const auto waveMapField = ResolveBankVariant(reader, *noteOffset, velocity);
    if (!waveMapField.has_value()) {
        return std::nullopt;
    }
    const uint32_t waveMap = reader.U32(*waveMapField);
    const uint32_t waveCount = reader.U32(waveTable);
    if (waveMap >= waveCount) {
        throw std::runtime_error("native CBNK wave map is out of range");
    }
    const size_t waveReference = waveTable + 4 + static_cast<size_t>(waveMap) * 8;

    BankNote result;
    const uint32_t packedArchive = reader.U32(waveReference);
    if ((packedArchive & 0xff000000) != 0x05000000) {
        throw std::runtime_error("invalid native CBNK wave archive reference");
    }
    result.WaveArchive = packedArchive & 0x00ffffff;
    result.Wave = reader.U32(waveReference + 4);
    const uint32_t flagMask = reader.U32(*waveMapField + 4);
    std::array<std::optional<uint32_t>, 32> flags;
    size_t flagCursor = *waveMapField + 8;
    for (uint8_t bit = 0; bit < flags.size(); ++bit) {
        if ((flagMask & (uint32_t{1} << bit)) != 0) {
            flags[bit] = reader.U32(flagCursor);
            flagCursor += 4;
        }
    }
    result.RootKey = flags[0].value_or(60) & 0xff;
    result.Volume = flags[1].value_or(127) & 0xff;
    const uint32_t packedPan = flags[2].value_or(64);
    result.Pan = packedPan & 0xff;
    result.SurroundPan = static_cast<int8_t>((packedPan >> 8) & 0xff);
    result.Pitch = std::bit_cast<float>(flags[3].value_or(0x3f800000));
    const uint32_t noteParameters = flags[4].value_or(0);
    result.IgnoreNoteOff = (noteParameters & 0xff) != 0;
    result.InterpolationType = static_cast<uint8_t>(
        (noteParameters >> 16) & 0xff);
    if (flags[9].has_value() && *flags[9] != 0xffffffff) {
        const size_t envelopeReference = CheckedAdd(
            *waveMapField, *flags[9], "CBNK ADSHR reference");
        const size_t envelope = CheckedAdd(
            envelopeReference, reader.U32(envelopeReference + 4),
            "CBNK ADSHR values");
        result.Attack = reader.U8(envelope);
        result.Decay = reader.U8(envelope + 1);
        result.Sustain = reader.U8(envelope + 2);
        result.Hold = reader.U8(envelope + 3);
        result.Release = reader.U8(envelope + 4);
    }
    return result;
}

std::span<const uint8_t> ResolveWaveFromArchive(std::span<const uint8_t> bytes,
                                                uint32_t waveIndex) {
    Reader reader(bytes);
    if (!reader.Magic(0, "CWAR") || reader.U16(4) != 0xfeff) {
        throw std::runtime_error("unsupported native BCWAR header");
    }
    size_t info = 0;
    size_t files = 0;
    for (size_t index = 0; index < reader.U32(16); ++index) {
        const size_t reference = 20 + index * 12;
        if (reader.U16(reference) == 0x6800) {
            info = reader.U32(reference + 4);
        } else if (reader.U16(reference) == 0x6801) {
            files = reader.U32(reference + 4);
        }
    }
    if (info == 0 || files == 0 || !reader.Magic(info, "INFO") ||
        !reader.Magic(files, "FILE")) {
        throw std::runtime_error("native BCWAR has incomplete sections");
    }
    const uint32_t count = reader.U32(info + 8);
    if (waveIndex >= count) {
        throw std::runtime_error("native BCWAR wave index is out of range");
    }
    const size_t reference = info + 12 + static_cast<size_t>(waveIndex) * 12;
    if (reader.U16(reference) != 0x1f00) {
        throw std::runtime_error("invalid native BCWAR wave reference");
    }
    return reader.Slice(CheckedAdd(files + 8, reader.U32(reference + 4), "BCWAR wave"),
                        reader.U32(reference + 8));
}

std::vector<int16_t> DecodeDspAdpcm(const Reader& reader, const BcwavChannel& channel,
                                    uint32_t sampleCount) {
    constexpr size_t kFrameBytes = 8;
    constexpr uint32_t kSamplesPerFrame = 14;
    constexpr std::array<int, 16> kSignedNibbles{
        0, 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1,
    };
    std::vector<int16_t> result;
    result.reserve(sampleCount);
    int history1 = channel.History1;
    int history2 = channel.History2;
    const uint32_t frameCount = (sampleCount + kSamplesPerFrame - 1) / kSamplesPerFrame;
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        const size_t frameOffset = channel.SampleOffset + frame * kFrameBytes;
        const uint8_t header = reader.U8(frameOffset);
        const int scale = 1 << (header & 0x0f);
        const size_t coefficient = static_cast<size_t>((header >> 4) & 7) * 2;
        const int coefficient1 = channel.Coefficients[coefficient];
        const int coefficient2 = channel.Coefficients[coefficient + 1];
        for (uint32_t sample = 0; sample < kSamplesPerFrame && result.size() < sampleCount;
             ++sample) {
            const uint8_t packed = reader.U8(frameOffset + 1 + sample / 2);
            const uint8_t nibble = sample % 2 == 0 ? packed >> 4 : packed & 0x0f;
            const int input = kSignedNibbles[nibble] * scale;
            int value = ((input << 11) + 0x400 + coefficient1 * history1 +
                         coefficient2 * history2) >> 11;
            value = std::clamp(value, -32768, 32767);
            history2 = history1;
            history1 = value;
            result.push_back(static_cast<int16_t>(value));
        }
    }
    return result;
}

std::vector<int16_t> DecodeImaAdpcm(const Reader& reader, const BcwavChannel& channel,
                                    uint32_t sampleCount) {
    constexpr std::array<int, 89> kStepSizes{
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
        34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
        143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
        494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
        1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
        4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
        11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
        27086, 29794, 32767,
    };
    constexpr std::array<int, 16> kIndexAdjustments{
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8,
    };
    if (channel.ImaStepIndex < 0 || channel.ImaStepIndex >= kStepSizes.size()) {
        throw std::runtime_error("invalid native BCWAV IMA step index");
    }

    std::vector<int16_t> result;
    result.reserve(sampleCount);
    int history = channel.History1;
    int stepIndex = channel.ImaStepIndex;
    for (uint32_t sample = 0; sample < sampleCount; ++sample) {
        const uint8_t packed = reader.U8(channel.SampleOffset + sample / 2);
        const uint8_t code = sample % 2 == 0 ? packed & 0x0f : packed >> 4;
        const int step = kStepSizes[stepIndex];
        int delta = step >> 3;
        if ((code & 1) != 0) {
            delta += step >> 2;
        }
        if ((code & 2) != 0) {
            delta += step >> 1;
        }
        if ((code & 4) != 0) {
            delta += step;
        }
        history = std::clamp(history + ((code & 8) != 0 ? -delta : delta),
                             -32768, 32767);
        stepIndex = std::clamp(stepIndex + kIndexAdjustments[code], 0, 88);
        result.push_back(static_cast<int16_t>(history));
    }
    return result;
}

std::vector<int16_t> DecodeDspAdpcmBlock(
    const Reader& reader, size_t sampleOffset, uint32_t sampleCount,
    const std::array<int16_t, 16>& coefficients, int16_t initialHistory1,
    int16_t initialHistory2) {
    BcwavChannel channel;
    channel.SampleOffset = sampleOffset;
    channel.Coefficients = coefficients;
    channel.History1 = initialHistory1;
    channel.History2 = initialHistory2;
    return DecodeDspAdpcm(reader, channel, sampleCount);
}

} // namespace

NativeBcwav DecodeNativeBcwav(std::span<const uint8_t> bytes) {
    Reader reader(bytes);
    if (!reader.Magic(0, "CWAV") || reader.U16(4) != 0xfeff || reader.U16(6) != 0x40) {
        throw std::runtime_error("unsupported native BCWAV header");
    }
    if (reader.U32(12) != bytes.size() || reader.U32(16) != 2) {
        throw std::runtime_error("inconsistent native BCWAV size or section count");
    }

    size_t infoOffset = 0;
    size_t dataOffset = 0;
    for (size_t index = 0; index < 2; ++index) {
        const size_t reference = 20 + index * 12;
        const uint16_t type = reader.U16(reference);
        const uint32_t offset = reader.U32(reference + 4);
        if (type == 0x7000) {
            infoOffset = offset;
        } else if (type == 0x7001) {
            dataOffset = offset;
        }
    }
    if (infoOffset == 0 || dataOffset == 0 || !reader.Magic(infoOffset, "INFO") ||
        !reader.Magic(dataOffset, "DATA")) {
        throw std::runtime_error("native BCWAV has no INFO/DATA sections");
    }

    NativeBcwav wave;
    wave.Codec = static_cast<NativeAudioCodec>(reader.U8(infoOffset + 8));
    wave.Looping = (reader.U8(infoOffset + 9) & 1) != 0;
    wave.SampleRate = reader.U32(infoOffset + 12);
    wave.LoopStart = reader.U32(infoOffset + 16);
    wave.SampleCount = reader.U32(infoOffset + 20);
    wave.ChannelCount = reader.U16(infoOffset + 28);
    if (wave.SampleRate == 0 || wave.SampleCount == 0 || wave.ChannelCount == 0 ||
        wave.ChannelCount > 2 || wave.LoopStart > wave.SampleCount) {
        throw std::runtime_error("invalid native BCWAV stream metadata");
    }
    std::vector<BcwavChannel> channels;
    channels.reserve(wave.ChannelCount);
    for (uint16_t index = 0; index < wave.ChannelCount; ++index) {
        const size_t reference = infoOffset + 32 + index * 8;
        if (reader.U16(reference) != 0x7100) {
            throw std::runtime_error("invalid native BCWAV channel reference");
        }
        const size_t channelOffset = CheckedAdd(infoOffset + 28, reader.U32(reference + 4),
                                                "BCWAV channel");
        if (reader.U16(channelOffset) != 0x1f00) {
            throw std::runtime_error("invalid native BCWAV sample reference");
        }
        BcwavChannel channel;
        channel.SampleOffset = CheckedAdd(dataOffset + 8, reader.U32(channelOffset + 4),
                                          "BCWAV samples");
        if (wave.Codec == NativeAudioCodec::DspAdpcm) {
            if (reader.U16(channelOffset + 8) != 0x0300) {
                throw std::runtime_error("invalid native BCWAV DSP-ADPCM reference");
            }
            const size_t adpcmOffset = CheckedAdd(channelOffset, reader.U32(channelOffset + 12),
                                                  "BCWAV ADPCM info");
            for (size_t coefficient = 0; coefficient < channel.Coefficients.size(); ++coefficient) {
                channel.Coefficients[coefficient] = reader.S16(adpcmOffset + coefficient * 2);
            }
            channel.History1 = reader.S16(adpcmOffset + 34);
            channel.History2 = reader.S16(adpcmOffset + 36);
        } else if (wave.Codec == NativeAudioCodec::ImaAdpcm) {
            if (reader.U16(channelOffset + 8) != 0x0301) {
                throw std::runtime_error("invalid native BCWAV IMA-ADPCM reference");
            }
            const size_t adpcmOffset = CheckedAdd(channelOffset, reader.U32(channelOffset + 12),
                                                  "BCWAV IMA-ADPCM info");
            channel.History1 = reader.S16(adpcmOffset);
            channel.ImaStepIndex = reader.S16(adpcmOffset + 2);
        }
        channels.push_back(channel);
    }

    std::vector<std::vector<int16_t>> decoded;
    decoded.reserve(channels.size());
    for (const auto& channel : channels) {
        std::vector<int16_t> samples;
        samples.reserve(wave.SampleCount);
        if (wave.Codec == NativeAudioCodec::DspAdpcm) {
            samples = DecodeDspAdpcm(reader, channel, wave.SampleCount);
        } else if (wave.Codec == NativeAudioCodec::ImaAdpcm) {
            samples = DecodeImaAdpcm(reader, channel, wave.SampleCount);
        } else if (wave.Codec == NativeAudioCodec::Pcm16) {
            for (uint32_t index = 0; index < wave.SampleCount; ++index) {
                samples.push_back(reader.S16(channel.SampleOffset + index * 2));
            }
        } else if (wave.Codec == NativeAudioCodec::Pcm8) {
            for (uint32_t index = 0; index < wave.SampleCount; ++index) {
                samples.push_back(static_cast<int16_t>(
                    static_cast<int8_t>(reader.U8(channel.SampleOffset + index))) * 256);
            }
        } else {
            throw std::runtime_error("unknown native BCWAV codec");
        }
        decoded.push_back(std::move(samples));
    }

    wave.InterleavedSamples.reserve(static_cast<size_t>(wave.SampleCount) * wave.ChannelCount);
    for (uint32_t sample = 0; sample < wave.SampleCount; ++sample) {
        for (const auto& channel : decoded) {
            wave.InterleavedSamples.push_back(channel[sample]);
        }
    }
    return wave;
}

NativeBcwav DecodeNativeBcwavFile(const std::filesystem::path& path) {
    const auto bytes = ReadFile(path);
    return DecodeNativeBcwav(bytes);
}

NativeBcwav DecodeNativeBcstm(std::span<const uint8_t> bytes) {
    Reader reader(bytes);
    if (!reader.Magic(0, "CSTM") || reader.U16(4) != 0xfeff || reader.U16(6) != 0x40 ||
        reader.U32(12) != bytes.size()) {
        throw std::runtime_error("unsupported native BCSTM header");
    }
    size_t info = 0;
    size_t seek = 0;
    size_t data = 0;
    for (size_t index = 0; index < reader.U16(16); ++index) {
        const size_t reference = 20 + index * 12;
        const uint16_t type = reader.U16(reference);
        if (type == 0x4000) {
            info = reader.U32(reference + 4);
        } else if (type == 0x4001) {
            seek = reader.U32(reference + 4);
        } else if (type == 0x4002) {
            data = reader.U32(reference + 4);
        }
    }
    if (info == 0 || seek == 0 || data == 0 || !reader.Magic(info, "INFO") ||
        !reader.Magic(seek, "SEEK") || !reader.Magic(data, "DATA")) {
        throw std::runtime_error("native BCSTM has incomplete sections");
    }
    const size_t infoBase = info + 8;
    const size_t streamInfo = CheckedAdd(infoBase, reader.U32(infoBase + 4),
                                         "BCSTM stream info");
    const size_t channelTable = CheckedAdd(infoBase, reader.U32(infoBase + 20),
                                           "BCSTM channel table");

    NativeBcwav stream;
    stream.Codec = static_cast<NativeAudioCodec>(reader.U8(streamInfo));
    stream.Looping = reader.U8(streamInfo + 1) != 0;
    stream.ChannelCount = reader.U8(streamInfo + 2);
    stream.SampleRate = reader.U32(streamInfo + 4);
    stream.LoopStart = reader.U32(streamInfo + 8);
    const uint32_t declaredLoopEnd = reader.U32(streamInfo + 12);
    const uint32_t blockCount = reader.U32(streamInfo + 16);
    const uint32_t blockSize = reader.U32(streamInfo + 20);
    const uint32_t blockSamples = reader.U32(streamInfo + 24);
    const uint32_t lastBlockSize = reader.U32(streamInfo + 28);
    const uint32_t lastBlockSamples = reader.U32(streamInfo + 32);
    const uint32_t lastBlockPaddedSize = reader.U32(streamInfo + 36);
    const uint32_t seekEntrySize = reader.U32(streamInfo + 40);
    const uint32_t sampleDataOffset = reader.U32(streamInfo + 52);
    if (stream.Codec != NativeAudioCodec::DspAdpcm || stream.ChannelCount == 0 ||
        stream.ChannelCount > 2 || stream.SampleRate == 0 || blockCount == 0 ||
        blockSize == 0 || blockSamples == 0 || lastBlockSize == 0 ||
        lastBlockSamples == 0 || seekEntrySize < 4) {
        throw std::runtime_error("unsupported native BCSTM stream metadata");
    }
    const uint64_t expectedSamples = static_cast<uint64_t>(blockCount - 1) * blockSamples +
                                     lastBlockSamples;
    if (expectedSamples > std::numeric_limits<uint32_t>::max() ||
        stream.LoopStart >= expectedSamples || declaredLoopEnd < expectedSamples) {
        throw std::runtime_error("native BCSTM loop/block sample range is inconsistent");
    }
    stream.SampleCount = static_cast<uint32_t>(expectedSamples);

    std::vector<std::array<int16_t, 16>> coefficients(stream.ChannelCount);
    if (reader.U32(channelTable) != stream.ChannelCount) {
        throw std::runtime_error("native BCSTM channel table count is inconsistent");
    }
    for (uint16_t channel = 0; channel < stream.ChannelCount; ++channel) {
        const size_t channelReference = channelTable + 4 + static_cast<size_t>(channel) * 8;
        const size_t channelInfo = CheckedAdd(channelTable,
            reader.U32(channelReference + 4), "BCSTM channel info");
        const size_t codecInfo = CheckedAdd(channelInfo,
            reader.U32(channelInfo + 4), "BCSTM DSP info");
        for (size_t coefficient = 0; coefficient < 16; ++coefficient) {
            coefficients[channel][coefficient] = reader.S16(codecInfo + coefficient * 2);
        }
    }

    std::vector<std::vector<int16_t>> decoded(stream.ChannelCount);
    for (auto& channel : decoded) {
        channel.reserve(stream.SampleCount);
    }
    size_t payload = CheckedAdd(data + 8, sampleDataOffset, "BCSTM sample payload");
    for (uint32_t block = 0; block < blockCount; ++block) {
        const bool last = block + 1 == blockCount;
        const uint32_t samples = last ? lastBlockSamples : blockSamples;
        const uint32_t storedSize = last ? lastBlockPaddedSize : blockSize;
        for (uint16_t channel = 0; channel < stream.ChannelCount; ++channel) {
            const size_t seekEntry = seek + 8 +
                (static_cast<size_t>(block) * stream.ChannelCount + channel) * seekEntrySize;
            auto blockSamplesDecoded = DecodeDspAdpcmBlock(
                reader, payload, samples, coefficients[channel],
                reader.S16(seekEntry), reader.S16(seekEntry + 2));
            decoded[channel].insert(decoded[channel].end(),
                                    blockSamplesDecoded.begin(), blockSamplesDecoded.end());
            payload += storedSize;
        }
    }
    stream.InterleavedSamples.reserve(static_cast<size_t>(stream.SampleCount) *
                                      stream.ChannelCount);
    for (uint32_t sample = 0; sample < stream.SampleCount; ++sample) {
        for (const auto& channel : decoded) {
            stream.InterleavedSamples.push_back(channel[sample]);
        }
    }
    return stream;
}

NativeBcwav DecodeNativeBcstmFile(const std::filesystem::path& path) {
    const auto bytes = ReadFile(path);
    return DecodeNativeBcstm(bytes);
}

NativeBcsarCatalog ParseNativeBcsar(std::span<const uint8_t> bytes) {
    Reader reader(bytes);
    if (!reader.Magic(0, "CSAR") || reader.U16(4) != 0xfeff || reader.U16(6) != 0x40) {
        throw std::runtime_error("unsupported native BCSAR header");
    }
    if (reader.U32(12) != bytes.size() || reader.U32(16) < 3) {
        throw std::runtime_error("inconsistent native BCSAR size or section count");
    }

    size_t stringSection = 0;
    size_t infoSection = 0;
    NativeBcsarCatalog catalog;
    for (size_t index = 0; index < reader.U32(16); ++index) {
        const size_t reference = 20 + index * 12;
        const uint16_t type = reader.U16(reference);
        const uint32_t offset = reader.U32(reference + 4);
        if (type == 0x2000) {
            stringSection = offset;
        } else if (type == 0x2001) {
            infoSection = offset;
        } else if (type == 0x2002) {
            catalog.FileSectionOffset = offset;
        }
    }
    if (stringSection == 0 || infoSection == 0 || catalog.FileSectionOffset == 0 ||
        !reader.Magic(stringSection, "STRG") || !reader.Magic(infoSection, "INFO") ||
        !reader.Magic(catalog.FileSectionOffset, "FILE")) {
        throw std::runtime_error("native BCSAR has incomplete sections");
    }

    size_t stringTable = 0;
    for (size_t index = 0; index < 2; ++index) {
        const size_t reference = stringSection + 8 + index * 8;
        if (reader.U16(reference) == 0x2400) {
            stringTable = CheckedAdd(reference, reader.U32(reference + 4), "BCSAR strings");
        }
    }
    if (stringTable == 0) {
        throw std::runtime_error("native BCSAR has no string table");
    }
    const uint32_t stringCount = reader.U32(stringTable);
    catalog.Strings.reserve(stringCount);
    for (uint32_t index = 0; index < stringCount; ++index) {
        const size_t reference = stringTable + 4 + index * 12;
        if (reader.U16(reference) != 0x1f01) {
            throw std::runtime_error("invalid native BCSAR string reference");
        }
        const uint32_t size = reader.U32(reference + 8);
        const size_t offset = CheckedAdd(stringTable, reader.U32(reference + 4), "BCSAR string");
        catalog.Strings.push_back(ReadCString(reader, offset, size));
    }

    size_t soundTable = 0;
    size_t bankTable = 0;
    size_t waveArchiveTable = 0;
    size_t playerTable = 0;
    size_t fileTable = 0;
    for (size_t index = 0; index < 8; ++index) {
        const size_t reference = infoSection + 8 + index * 8;
        const uint16_t type = reader.U16(reference);
        const size_t offset = CheckedAdd(infoSection + 8, reader.U32(reference + 4),
                                         "BCSAR INFO table");
        if (type == 0x2100) {
            soundTable = offset;
        } else if (type == 0x2101) {
            bankTable = offset;
        } else if (type == 0x2103) {
            waveArchiveTable = offset;
        } else if (type == 0x2102) {
            playerTable = offset;
        } else if (type == 0x2106) {
            fileTable = offset;
        }
    }
    const auto parseOffsets = [&reader](size_t table, uint16_t expectedType) {
        std::vector<size_t> offsets;
        const uint32_t count = reader.U32(table);
        offsets.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            const size_t reference = table + 4 + index * 8;
            if (reader.U16(reference) != expectedType) {
                throw std::runtime_error("invalid native BCSAR INFO entry reference");
            }
            offsets.push_back(CheckedAdd(table, reader.U32(reference + 4), "BCSAR INFO entry"));
        }
        return offsets;
    };

    if (fileTable == 0 || soundTable == 0 || bankTable == 0 ||
        waveArchiveTable == 0 || playerTable == 0) {
        throw std::runtime_error("native BCSAR has incomplete sound resource tables");
    }
    const auto fileOffsets = parseOffsets(fileTable, 0x220a);
    catalog.Files.reserve(fileOffsets.size());
    for (const size_t offset : fileOffsets) {
        NativeBcsarFileEntry file;
        const uint16_t type = reader.U16(offset);
        if (type == 0x220c) {
            file.Internal = true;
            if (reader.U16(offset + 12) != 0xffff) {
                file.Offset = reader.U32(offset + 16);
                file.Size = reader.U32(offset + 20);
            }
        } else if (type != 0x220d) {
            throw std::runtime_error("unknown native BCSAR file entry type");
        } else {
            file.Path = ReadCString(reader, offset + 12,
                                    static_cast<uint32_t>(bytes.size() - offset - 12));
        }
        catalog.Files.push_back(file);
    }

    const auto soundOffsets = parseOffsets(soundTable, 0x2200);
    catalog.Sounds.reserve(soundOffsets.size());
    for (uint32_t index = 0; index < soundOffsets.size(); ++index) {
        const size_t offset = soundOffsets[index];
        NativeBcsarSoundEntry sound;
        sound.Index = index;
        sound.FileId = reader.U32(offset);
        sound.PlayerReference = reader.U32(offset + 4);
        sound.ArchiveVolume = reader.U8(offset + 8);
        sound.RemoteFilter = reader.U8(offset + 9);
        sound.Type = reader.U16(offset + 12);
        const size_t detailOffset = CheckedAdd(
            offset + 16, reader.U32(offset + 16), "BCSAR sound detail");
        sound.OptionMask = reader.U32(offset + 20);
        size_t optionOffset = offset + 24;
        for (uint8_t bit = 0; bit < 32; ++bit) {
            if ((sound.OptionMask & (uint32_t{1} << bit)) == 0) {
                continue;
            }
            if (optionOffset + sizeof(uint32_t) > detailOffset) {
                throw std::runtime_error(
                    "native BCSAR sound options overlap its detail record");
            }
            sound.Options[bit] = reader.U32(optionOffset);
            optionOffset += sizeof(uint32_t);
        }
        sound.NameId = sound.Option(0).value_or(0xffffffff);
        if (sound.NameId != 0xffffffff) {
            if (sound.NameId >= catalog.Strings.size()) {
                throw std::runtime_error("native BCSAR sound name is out of range");
            }
            sound.Name = catalog.Strings[sound.NameId];
        }
        if (const auto relative = sound.Option(8); relative.has_value()) {
            const size_t sound3dOffset = CheckedAdd(
                offset, *relative, "BCSAR Sound3DInfo");
            reader.Require(sound3dOffset, 10, "BCSAR Sound3DInfo");
            NativeBcsarSound3dInfo sound3d;
            sound3d.Flags = reader.U32(sound3dOffset);
            sound3d.DecayRatio = reader.F32(sound3dOffset + 4);
            sound3d.DecayCurve = reader.U8(sound3dOffset + 8);
            sound3d.DopplerFactor = reader.U8(sound3dOffset + 9);
            if (!std::isfinite(sound3d.DecayRatio)) {
                throw std::runtime_error(
                    "native BCSAR Sound3DInfo decay ratio is not finite");
            }
            sound.Sound3d = sound3d;
        }
        if (sound.Type == 0x2203) {
            sound.SequenceOffset = reader.U32(detailOffset);
            const uint32_t bankCount = reader.U32(detailOffset + 8);
            if (bankCount > 4) {
                throw std::runtime_error("native BCSAR sequence bank list is too large");
            }
            for (uint32_t bank = 0; bank < bankCount; ++bank) {
                const uint32_t reference = reader.U32(detailOffset + 12 + bank * 4);
                if ((reference & 0xff000000) != 0x03000000 ||
                    (reference & 0x00ffffff) > 0xff) {
                    throw std::runtime_error("invalid native BCSAR sequence bank reference");
                }
                sound.BankIds.push_back(static_cast<uint8_t>(reference & 0x00ffffff));
            }
        }
        catalog.Sounds.push_back(std::move(sound));
    }

    const auto parseNamedFileTable = [&](size_t table, uint16_t expectedType,
                                         size_t nameOffset) {
        std::vector<NativeBcsarNamedFileEntry> entries;
        const auto offsets = parseOffsets(table, expectedType);
        entries.reserve(offsets.size());
        for (const size_t offset : offsets) {
            NativeBcsarNamedFileEntry entry;
            entry.FileId = reader.U32(offset);
            entry.NameId = reader.U32(offset + nameOffset);
            if (entry.NameId != 0xffffffff) {
                if (entry.NameId >= catalog.Strings.size()) {
                    throw std::runtime_error("native BCSAR resource name is out of range");
                }
                entry.Name = catalog.Strings[entry.NameId];
            }
            entries.push_back(std::move(entry));
        }
        return entries;
    };
    catalog.Banks = parseNamedFileTable(bankTable, 0x2206, 16);
    catalog.WaveArchives = parseNamedFileTable(waveArchiveTable, 0x2207, 12);

    const auto playerOffsets = parseOffsets(playerTable, 0x2209);
    catalog.Players.reserve(playerOffsets.size());
    for (uint32_t index = 0; index < playerOffsets.size(); ++index) {
        const size_t offset = playerOffsets[index];
        NativeBcsarPlayerEntry player;
        player.Index = index;
        player.PlayableSoundLimit = reader.U32(offset);
        player.OptionMask = reader.U32(offset + 4);
        size_t optionOffset = offset + 8;
        for (uint8_t bit = 0; bit < 32; ++bit) {
            if ((player.OptionMask & (uint32_t{1} << bit)) == 0) {
                continue;
            }
            player.Options[bit] = reader.U32(optionOffset);
            optionOffset += sizeof(uint32_t);
        }
        player.NameId = player.Option(0).value_or(0xffffffff);
        if (player.PlayableSoundLimit == 0) {
            throw std::runtime_error(
                "native BCSAR sound player has a zero voice limit");
        }
        if (player.NameId != 0xffffffff) {
            if (player.NameId >= catalog.Strings.size()) {
                throw std::runtime_error(
                    "native BCSAR sound-player name is out of range");
            }
            player.Name = catalog.Strings[player.NameId];
        }
        catalog.Players.push_back(std::move(player));
    }

    for (const auto& sound : catalog.Sounds) {
        if (catalog.ResolvePlayerReference(sound.PlayerReference) == nullptr) {
            throw std::runtime_error(
                "native BCSAR sound has an invalid player reference");
        }
    }
    return catalog;
}

NativeBcsarCatalog ParseNativeBcsarFile(const std::filesystem::path& path) {
    const auto bytes = ReadFile(path);
    return ParseNativeBcsar(bytes);
}

const NativeBcsarSoundEntry* NativeBcsarCatalog::ResolveSoundId(uint32_t soundId) const {
    const uint32_t index = soundId & 0x00ffffff;
    if (index >= Sounds.size()) {
        return nullptr;
    }
    return &Sounds[index];
}

const NativeBcsarPlayerEntry* NativeBcsarCatalog::ResolvePlayerReference(
    uint32_t playerReference) const {
    if ((playerReference & 0xff000000u) != 0x04000000u) {
        return nullptr;
    }
    const uint32_t index = playerReference & 0x00ffffffu;
    return index < Players.size() ? &Players[index] : nullptr;
}

std::optional<uint32_t> NativeBcsarSoundEntry::Option(uint8_t bit) const {
    if (bit >= Options.size()) {
        return {};
    }
    return Options[bit];
}

std::optional<uint32_t> NativeBcsarSoundEntry::PlayerIndex() const {
    if ((PlayerReference & 0xff000000u) != 0x04000000u) {
        return {};
    }
    return PlayerReference & 0x00ffffffu;
}

std::optional<uint32_t> NativeBcsarPlayerEntry::Option(uint8_t bit) const {
    if (bit >= Options.size()) {
        return {};
    }
    return Options[bit];
}

uint8_t NativeBcsarSoundEntry::PanMode() const {
    return static_cast<uint8_t>(Option(1).value_or(0) & 0xff);
}

uint8_t NativeBcsarSoundEntry::PanCurve() const {
    return static_cast<uint8_t>((Option(1).value_or(0) >> 8) & 0xff);
}

uint8_t NativeBcsarSoundEntry::Priority() const {
    return static_cast<uint8_t>(Option(2).value_or(0x40) & 0xff);
}

bool NativeBcsarSoundEntry::Persistent() const {
    return (Option(31).value_or(0) & 0x80000000u) != 0;
}

std::span<const uint8_t> NativeBcsarCatalog::EmbeddedFile(
    std::span<const uint8_t> archive, uint32_t fileId) const {
    if (fileId >= Files.size() || !Files[fileId].Internal || Files[fileId].Size == 0) {
        return {};
    }
    const size_t offset = CheckedAdd(static_cast<size_t>(FileSectionOffset) + 8,
                                     Files[fileId].Offset, "BCSAR embedded file");
    if (offset > archive.size() || Files[fileId].Size > archive.size() - offset) {
        throw std::runtime_error("native BCSAR embedded file is truncated");
    }
    return archive.subspan(offset, Files[fileId].Size);
}

NativeAudioSequence ResolveNativeAudioSequence(std::span<const uint8_t> archive,
                                               const NativeBcsarCatalog& catalog,
                                               uint32_t soundId,
                                               uint32_t randomSeed,
                                               const NativeCseqInitialState* initialState) {
    const NativeBcsarSoundEntry* sound = catalog.ResolveSoundId(soundId);
    if (sound == nullptr || sound->Type != 0x2203 || sound->BankIds.empty()) {
        throw std::runtime_error("native sound ID is not a supported sequence cue");
    }

    const auto sequenceFile = catalog.EmbeddedFile(archive, sound->FileId);
    if (sequenceFile.empty()) {
        throw std::runtime_error("native sequence cue has no embedded CSEQ file");
    }
    const NativeCseqSequence parsed = ParseNativeCseq(
        sequenceFile, sound->SequenceOffset,
        randomSeed == 0 ? soundId : randomSeed, initialState);

    NativeAudioSequence sequence;
    sequence.SoundId = soundId;
    sequence.Name = sound->Name;
    sequence.Timebase = parsed.Timebase;
    sequence.InitialTempo = parsed.InitialTempo;
    sequence.EndTick = parsed.EndTick;
    sequence.LoopStartTick = parsed.LoopStartTick;
    sequence.Looping = parsed.Looping;
    sequence.ControlOnly = parsed.ControlOnly;
    sequence.CseqCommandUsage = parsed.CommandUsage;
    sequence.CseqExtendedCommandUsage = parsed.ExtendedCommandUsage;
    sequence.CseqTimeModifierUsage = parsed.TimeModifierUsage;
    sequence.CseqTimedCommandUsage = parsed.TimedCommandUsage;
    sequence.CseqTieCommandCount = parsed.TieCommandCount;
    sequence.TempoEvents = parsed.TempoEvents;
    sequence.ControlEvents = parsed.ControlEvents;
    sequence.Events.reserve(parsed.Notes.size());
    std::unordered_map<uint64_t, std::shared_ptr<const NativeBcwav>> decodedWaves;

    for (const NativeCseqNoteEvent& note : parsed.Notes) {
        uint8_t bankId = 0;
        if (note.BankSlot < sound->BankIds.size()) {
            bankId = sound->BankIds[note.BankSlot];
        } else {
            const auto directBank = std::find(sound->BankIds.begin(), sound->BankIds.end(),
                                              note.BankSlot);
            if (directBank == sound->BankIds.end()) {
                if (note.BankSlot >= catalog.Banks.size()) {
                    throw std::runtime_error("native sequence bank selector is unresolved");
                }
                bankId = note.BankSlot;
            } else {
                bankId = *directBank;
            }
        }
        if (bankId >= catalog.Banks.size()) {
            throw std::runtime_error("native sequence bank ID is out of range");
        }
        const auto bankFile = catalog.EmbeddedFile(
            archive, catalog.Banks[bankId].FileId);
        if (bankFile.empty()) {
            throw std::runtime_error("native sequence bank has no embedded CBNK file");
        }
        std::optional<BankNote> bank;
        try {
            bank = ResolveBankNote(bankFile, note.Program, note.Note, note.Velocity);
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string(ex.what()) + " program=" +
                                     std::to_string(note.Program) + " note=" +
                                     std::to_string(note.Note) + " bank=" +
                                     std::to_string(bankId));
        }
        if (!bank.has_value()) {
            NativeAudioCue cue;
            cue.SoundId = soundId;
            cue.Name = sound->Name;
            cue.Program = note.Program;
            cue.Note = note.Note;
            cue.Velocity = note.Velocity;
            cue.DurationTicks = note.DurationTicks;
            cue.BankId = bankId;
            cue.BankSlot = note.BankSlot;
            cue.Transpose = note.Transpose;
            sequence.Events.push_back({note.StartTick, note.DurationTicks,
                                       note.Track, true, note.Tie,
                                       std::move(cue)});
            continue;
        }
        if (bank->WaveArchive >= catalog.WaveArchives.size()) {
            throw std::runtime_error("native bank wave archive ID is out of range");
        }
        const auto waveArchive = catalog.EmbeddedFile(
            archive, catalog.WaveArchives[bank->WaveArchive].FileId);
        if (waveArchive.empty()) {
            throw std::runtime_error("native wave archive has no embedded BCWAR file");
        }

        NativeAudioCue cue;
        cue.SoundId = soundId;
        cue.Name = sound->Name;
        cue.Program = note.Program;
        cue.Note = note.Note;
        cue.Velocity = note.Velocity;
        cue.DurationTicks = note.DurationTicks;
        cue.BankId = bankId;
        cue.WaveArchiveIndex = bank->WaveArchive;
        cue.WaveIndex = bank->Wave;
        cue.RootKey = bank->RootKey;
        cue.Volume = bank->Volume;
        cue.Pitch = bank->Pitch;
        cue.SurroundPan = bank->SurroundPan;
        cue.InterpolationType = bank->InterpolationType;
        cue.IgnoreNoteOff = bank->IgnoreNoteOff;
        cue.ArchiveVolume = sound->ArchiveVolume;
        cue.RemoteFilter = sound->RemoteFilter;
        cue.PanMode = sound->PanMode();
        cue.PanCurve = sound->PanCurve();
        cue.Pan = bank->Pan;
        cue.BankSlot = note.BankSlot;
        cue.TrackVolume = note.Volume;
        cue.Expression = note.Expression;
        cue.TrackPan = note.Pan;
        cue.MasterVolume = note.MasterVolume;
        cue.Transpose = note.Transpose;
        cue.PitchBend = note.PitchBend;
        cue.PitchBendRange = note.PitchBendRange;
        cue.VelocityRange = note.VelocityRange;
        cue.BiquadType = note.BiquadType;
        cue.BiquadValue = note.BiquadValue;
        cue.ModPhase = note.ModPhase;
        cue.ModCurve = note.ModCurve;
        cue.ModDepth = note.ModDepth;
        cue.ModSpeed = note.ModSpeed;
        cue.ModType = note.ModType;
        cue.ModRange = note.ModRange;
        cue.ModDelay = note.ModDelay;
        cue.ModPeriod = note.ModPeriod;
        cue.PortamentoKey = note.PortamentoKey;
        cue.PortamentoEnabled = note.PortamentoEnabled;
        cue.PortamentoTime = note.PortamentoTime;
        cue.SweepPitch = note.SweepPitch;
        cue.TrackSurroundPan = note.SurroundPan;
        cue.LpfCutoff = note.LpfCutoff;
        cue.FxSendA = note.FxSendA;
        cue.FxSendB = note.FxSendB;
        cue.FxSendC = note.FxSendC;
        cue.MainSend = note.MainSend;
        cue.InitialPan = note.InitialPan;
        cue.Damper = note.Damper;
        cue.Attack = note.Attack < 0 ? bank->Attack : static_cast<uint8_t>(note.Attack);
        cue.Decay = note.Decay < 0 ? bank->Decay : static_cast<uint8_t>(note.Decay);
        cue.Sustain = note.Sustain < 0 ? bank->Sustain : static_cast<uint8_t>(note.Sustain);
        cue.Hold = note.Hold < 0 ? bank->Hold : static_cast<uint8_t>(note.Hold);
        cue.Release = note.Release < 0 ? bank->Release : static_cast<uint8_t>(note.Release);
        const uint64_t waveKey = (static_cast<uint64_t>(bank->WaveArchive) << 32) |
                                 bank->Wave;
        auto decoded = decodedWaves.find(waveKey);
        if (decoded == decodedWaves.end()) {
            auto wave = std::make_shared<NativeBcwav>(
                DecodeNativeBcwav(ResolveWaveFromArchive(waveArchive, bank->Wave)));
            decoded = decodedWaves.emplace(waveKey, std::move(wave)).first;
        }
        cue.Wave = decoded->second;
        sequence.Events.push_back({note.StartTick, note.DurationTicks, note.Track,
                                   false, note.Tie, std::move(cue)});
    }
    sequence.ControlOnly = std::none_of(
        sequence.Events.begin(), sequence.Events.end(),
        [](const NativeAudioSequenceEvent& event) { return !event.Silent; });
    return sequence;
}

NativeAudioCue ResolveNativeAudioCue(std::span<const uint8_t> archive,
                                     const NativeBcsarCatalog& catalog,
                                     uint32_t soundId) {
    NativeAudioSequence sequence = ResolveNativeAudioSequence(
        archive, catalog, soundId, soundId);
    const auto audible = std::find_if(
        sequence.Events.begin(), sequence.Events.end(),
        [](const NativeAudioSequenceEvent& event) { return !event.Silent; });
    if (audible == sequence.Events.end()) {
        throw std::runtime_error("native CSEQ is a control-only sequence");
    }
    return std::move(audible->Cue);
}

NativeBcwav ResolveNativeAudioStream(const std::filesystem::path& archivePath,
                                     const NativeBcsarCatalog& catalog,
                                     uint32_t soundId) {
    const NativeBcsarSoundEntry* sound = catalog.ResolveSoundId(soundId);
    if (sound == nullptr || sound->Type != 0x2201 || sound->FileId >= catalog.Files.size()) {
        throw std::runtime_error("native sound ID is not an external stream");
    }
    const NativeBcsarFileEntry& file = catalog.Files[sound->FileId];
    if (file.Internal || file.Path.empty()) {
        throw std::runtime_error("native stream has no external BCSTM path");
    }
    const std::filesystem::path root = archivePath.parent_path().lexically_normal();
    const std::filesystem::path streamPath =
        (root / std::filesystem::path(file.Path)).lexically_normal();
    const auto relative = streamPath.lexically_relative(root);
    if (relative.empty() || *relative.begin() == "..") {
        throw std::runtime_error("native stream path escapes its RomFS sound root");
    }
    return DecodeNativeBcstmFile(streamPath);
}

uint32_t ResolveNativeCodeU32TableEntry(std::span<const uint8_t> codeBin,
                                        uint32_t codeBaseAddress,
                                        uint32_t tableAddress,
                                        uint32_t index) {
    if (codeBaseAddress == 0 || tableAddress < codeBaseAddress) {
        throw std::runtime_error("native code table address precedes its image base");
    }
    const uint64_t offset = static_cast<uint64_t>(tableAddress - codeBaseAddress) +
                            static_cast<uint64_t>(index) * sizeof(uint32_t);
    if (offset + sizeof(uint32_t) > codeBin.size()) {
        throw std::runtime_error("native code table entry is out of range");
    }
    return static_cast<uint32_t>(codeBin[offset]) |
           (static_cast<uint32_t>(codeBin[offset + 1]) << 8) |
           (static_cast<uint32_t>(codeBin[offset + 2]) << 16) |
           (static_cast<uint32_t>(codeBin[offset + 3]) << 24);
}

NativeAudioEnvelopeLayout Oot3dEurRev0AudioEnvelopeLayout() {
    return {
        0x00100000,
        0x004eb7d0,
        0x004eb7d4,
        0x004eb8d4,
        0x004e7960,
        0x00309f7c,
        0x00309f80,
        0x00309f84,
        0x00309f88,
        0x00309f8c,
        0x0030a0d4,
        0x0030a0d8,
        0x0030a0dc,
        0x0030a0e0,
        0x0030a0e4,
        0x002cdec0,
        0x002cddc4,
        0x002cddc8,
        0x002cddcc,
        965,
    };
}

NativeAudioModulationLayout Oot3dEurRev0AudioModulationLayout() {
    return {
        0x00100000,
        0x002cde84,
        0x002cde88,
        0x002cde8c,
        0x00487a28,
        0x0047fbb0,
        33,
    };
}

NativeAudioMixLayout Oot3dEurRev0AudioMixLayout() {
    return {
        0x00100000,
        0x00558b18,
        0x00558b24,
        0x00309b54,
        0x00309b54,
        0x00309b58,
    };
}

NativeAudioFilterLayout Oot3dEurRev0AudioFilterLayout() {
    return {
        0x00100000,
        0x00486868,
        0x0048686c,
        0x00486870,
        0x00486874,
        0x00486878,
        0x0048687c,
        0x00486880,
        0x004859a4,
        0x004859a8,
        0x004859ac,
        0x004859b0,
        0x003722f4,
        0x003722fc,
        23,
        256,
        16,
    };
}

namespace {

size_t NativeCodeOffsetForBase(std::span<const uint8_t> codeBin,
                               uint32_t codeBaseAddress,
                               uint32_t address, size_t size,
                               const char* role) {
    if (codeBaseAddress == 0 || address < codeBaseAddress) {
        throw std::runtime_error(std::string("native audio ") + role +
                                 " address precedes code.bin");
    }
    const size_t offset = static_cast<size_t>(address - codeBaseAddress);
    if (offset > codeBin.size() || size > codeBin.size() - offset) {
        throw std::runtime_error(std::string("native audio ") + role +
                                 " address exceeds code.bin");
    }
    return offset;
}

uint32_t NativeCodeU32ForBase(std::span<const uint8_t> codeBin,
                              uint32_t codeBaseAddress,
                              uint32_t address, const char* role) {
    const size_t offset = NativeCodeOffsetForBase(
        codeBin, codeBaseAddress, address, 4, role);
    return static_cast<uint32_t>(codeBin[offset]) |
           (static_cast<uint32_t>(codeBin[offset + 1]) << 8) |
           (static_cast<uint32_t>(codeBin[offset + 2]) << 16) |
           (static_cast<uint32_t>(codeBin[offset + 3]) << 24);
}

float NativeCodeFloatForBase(std::span<const uint8_t> codeBin,
                             uint32_t codeBaseAddress,
                             uint32_t address, const char* role) {
    return std::bit_cast<float>(NativeCodeU32ForBase(
        codeBin, codeBaseAddress, address, role));
}

size_t NativeCodeOffset(std::span<const uint8_t> codeBin,
                        const NativeAudioEnvelopeLayout& layout,
                        uint32_t address, size_t size) {
    return NativeCodeOffsetForBase(
        codeBin, layout.CodeBaseAddress, address, size, "envelope");
}

uint32_t NativeCodeU32(std::span<const uint8_t> codeBin,
                       const NativeAudioEnvelopeLayout& layout,
                       uint32_t address) {
    return NativeCodeU32ForBase(
        codeBin, layout.CodeBaseAddress, address, "envelope");
}

float NativeCodeFloat(std::span<const uint8_t> codeBin,
                      const NativeAudioEnvelopeLayout& layout,
                      uint32_t address) {
    return std::bit_cast<float>(NativeCodeU32(codeBin, layout, address));
}

NativeAudioEnvelopeRateProfile ParseEnvelopeRate(
    std::span<const uint8_t> codeBin,
    const NativeAudioEnvelopeLayout& layout,
    uint32_t immediateAddress, uint32_t nearImmediateAddress,
    uint32_t scaleAddress, uint32_t lowSlopeAddress,
    uint32_t highNumeratorAddress) {
    return {
        NativeCodeFloat(codeBin, layout, immediateAddress),
        NativeCodeFloat(codeBin, layout, nearImmediateAddress),
        NativeCodeFloat(codeBin, layout, scaleAddress),
        NativeCodeFloat(codeBin, layout, lowSlopeAddress),
        NativeCodeFloat(codeBin, layout, highNumeratorAddress),
    };
}

float EnvelopeRateForCode(const NativeAudioEnvelopeRateProfile& rate,
                          uint8_t code) {
    if (code == 127) {
        return rate.Immediate;
    }
    if (code == 126) {
        return rate.NearImmediate;
    }
    const float raw = code < 50
        ? static_cast<float>(code * 2 + 1) * rate.LowSlope
        : rate.HighNumerator / static_cast<float>(126 - code);
    return raw * rate.Scale;
}

} // namespace

float NativeAudioModulationProfile::SampleSine(float phase) const {
    if (SineQuarterTable.size() < 33 || PhaseScale <= 0.0f) {
        return 0.0f;
    }
    phase -= std::floor(phase);
    const int index = std::clamp(
        static_cast<int>(phase * PhaseScale), 0, 127);
    int sample = 0;
    if (index < 32) {
        sample = SineQuarterTable[static_cast<size_t>(index)];
    } else if (index < 64) {
        sample = SineQuarterTable[static_cast<size_t>(64 - index)];
    } else if (index < 96) {
        sample = -SineQuarterTable[static_cast<size_t>(index - 64)];
    } else {
        sample = -SineQuarterTable[static_cast<size_t>(128 - index)];
    }
    return static_cast<float>(sample) * OutputScale;
}

NativeAudioModulationProfile ParseNativeAudioModulationProfile(
    std::span<const uint8_t> codeBin,
    const NativeAudioModulationLayout& layout) {
    if (layout.CodeBaseAddress == 0 || layout.SineQuarterTableCount < 33) {
        throw std::runtime_error("native audio modulation layout is incomplete");
    }
    NativeAudioModulationProfile profile;
    profile.PhaseScale = NativeCodeFloatForBase(
        codeBin, layout.CodeBaseAddress, layout.PhaseScaleAddress,
        "modulation");
    profile.OutputScale = NativeCodeFloatForBase(
        codeBin, layout.CodeBaseAddress, layout.OutputScaleAddress,
        "modulation");
    profile.PhaseAdvanceScale = NativeCodeFloatForBase(
        codeBin, layout.CodeBaseAddress, layout.PhaseAdvanceScaleAddress,
        "modulation");
    const uint32_t sineAddress = NativeCodeU32ForBase(
        codeBin, layout.CodeBaseAddress, layout.SineTablePointerAddress,
        "modulation");
    const size_t sineOffset = NativeCodeOffsetForBase(
        codeBin, layout.CodeBaseAddress, sineAddress,
        layout.SineQuarterTableCount, "modulation sine table");
    profile.SineQuarterTable.reserve(layout.SineQuarterTableCount);
    for (uint32_t index = 0; index < layout.SineQuarterTableCount; ++index) {
        profile.SineQuarterTable.push_back(
            static_cast<int8_t>(codeBin[sineOffset + index]));
    }

    const uint32_t updateInstruction = NativeCodeU32ForBase(
        codeBin, layout.CodeBaseAddress,
        layout.UpdateMillisecondsInstructionAddress, "modulation update");
    if ((updateInstruction & 0x0fe0f000u) != 0x03a01000u) {
        throw std::runtime_error(
            "native audio modulation cadence is not an ARM MOV r1 immediate");
    }
    const uint32_t immediate = updateInstruction & 0xffu;
    const uint32_t rotation = ((updateInstruction >> 8) & 0x0fu) * 2u;
    profile.UpdateMilliseconds = std::rotr(immediate, rotation);

    if (std::abs(profile.PhaseScale - 128.0f) > 0.001f ||
        profile.OutputScale <= 0.0f || profile.PhaseAdvanceScale <= 0.0f ||
        profile.UpdateMilliseconds == 0 ||
        profile.SineQuarterTable[0] != 0 ||
        profile.SineQuarterTable[32] <= 0) {
        throw std::runtime_error("native audio modulation profile is invalid");
    }
    return profile;
}

NativeAudioMixProfile ParseNativeAudioMixProfile(
    std::span<const uint8_t> codeBin,
    const NativeAudioMixLayout& layout) {
    if (layout.CodeBaseAddress == 0 ||
        layout.StandardPanCurvePointerTableAddress == 0 ||
        layout.AlternatePanCurvePointerTableAddress == 0) {
        throw std::runtime_error("native audio mix layout is incomplete");
    }

    NativeAudioMixProfile profile;
    profile.PanScale = NativeCodeFloatForBase(
        codeBin, layout.CodeBaseAddress, layout.PanScaleAddress, "mix pan scale");
    profile.SurroundLowerScale = NativeCodeFloatForBase(
        codeBin, layout.CodeBaseAddress, layout.SurroundLowerScaleAddress,
        "mix lower surround scale");
    profile.SurroundUpperScale = NativeCodeFloatForBase(
        codeBin, layout.CodeBaseAddress, layout.SurroundUpperScaleAddress,
        "mix upper surround scale");

    const auto readCurves = [&](uint32_t pointerTableAddress,
                                auto& destination, const char* role) {
        for (size_t family = 0; family < destination.size(); ++family) {
            const uint32_t curveAddress = NativeCodeU32ForBase(
                codeBin, layout.CodeBaseAddress,
                pointerTableAddress + static_cast<uint32_t>(family * 4), role);
            for (size_t index = 0; index < destination[family].size(); ++index) {
                destination[family][index] = NativeCodeFloatForBase(
                    codeBin, layout.CodeBaseAddress,
                    curveAddress + static_cast<uint32_t>(index * 4), role);
            }
        }
    };
    readCurves(layout.StandardPanCurvePointerTableAddress,
               profile.StandardPanCurves, "standard mix curve");
    readCurves(layout.AlternatePanCurvePointerTableAddress,
               profile.AlternatePanCurves, "alternate mix curve");

    const auto validCurves = [](const auto& curves) {
        for (const auto& curve : curves) {
            if (curve.back() != 0.0f) {
                return false;
            }
            for (size_t index = 0; index < curve.size(); ++index) {
                if (!std::isfinite(curve[index]) || curve[index] < 0.0f ||
                    curve[index] > 2.0f ||
                    (index != 0 && curve[index] > curve[index - 1])) {
                    return false;
                }
            }
        }
        return true;
    };
    if (std::abs(profile.PanScale - (1.0f / 63.0f)) > 0.000001f ||
        std::abs(profile.SurroundLowerScale - (1.0f / 63.0f)) > 0.000001f ||
        std::abs(profile.SurroundUpperScale - (1.0f / 64.0f)) > 0.000001f ||
        !validCurves(profile.StandardPanCurves) ||
        !validCurves(profile.AlternatePanCurves)) {
        throw std::runtime_error("native audio mix profile is invalid");
    }
    return profile;
}

float NormalizeNativeAudioPan(uint8_t pan,
                              const NativeAudioMixProfile& profile) {
    const int centered = pan <= 1 ? static_cast<int>(pan) - 63
                                  : static_cast<int>(pan) - 64;
    return static_cast<float>(centered) * profile.PanScale;
}

float NormalizeNativeAudioSurroundPan(
    uint8_t surroundPan, const NativeAudioMixProfile& profile) {
    if (surroundPan <= 63) {
        return static_cast<float>(surroundPan) * profile.SurroundLowerScale;
    }
    return static_cast<float>(static_cast<uint16_t>(surroundPan) + 1) *
           profile.SurroundUpperScale;
}

namespace {

struct NativePanCurveSelection {
    uint8_t Family = 0;
    bool Normalize = false;
    bool ClampToOne = false;
};

NativePanCurveSelection SelectNativePanCurve(uint8_t panCurve) {
    NativePanCurveSelection result;
    switch (panCurve) {
        case 1: result.Normalize = true; break;
        case 2: result.Normalize = true; result.ClampToOne = true; break;
        case 3: result.Family = 1; break;
        case 4: result.Family = 1; result.Normalize = true; break;
        case 5:
            result.Family = 1;
            result.Normalize = true;
            result.ClampToOne = true;
            break;
        case 6: result.Family = 2; break;
        case 7: result.Family = 2; result.Normalize = true; break;
        case 8:
            result.Family = 2;
            result.Normalize = true;
            result.ClampToOne = true;
            break;
        default: break;
    }
    return result;
}

size_t NativeMixCurveIndex(float value, float maximum) {
    const float clamped = std::clamp(value, 0.0f, maximum);
    return static_cast<size_t>(std::clamp(
        static_cast<int>(0.5f + clamped * (256.0f / maximum)), 0, 256));
}

} // namespace

float CalculateNativeAudioPanGain(const NativeAudioMixProfile& profile,
                                  uint8_t panCurve, float pan,
                                  bool alternateCurves) {
    const auto selection = SelectNativePanCurve(panCurve);
    const auto& curve = alternateCurves
        ? profile.AlternatePanCurves[selection.Family]
        : profile.StandardPanCurves[selection.Family];
    float gain = curve[NativeMixCurveIndex(pan + 1.0f, 2.0f)];
    if (selection.Normalize) {
        gain /= curve[128];
    }
    return std::clamp(gain, 0.0f, selection.ClampToOne ? 1.0f : 2.0f);
}

float CalculateNativeAudioSurroundGain(
    const NativeAudioMixProfile& profile, uint8_t panCurve,
    float surroundPan) {
    const auto selection = SelectNativePanCurve(panCurve);
    return std::clamp(
        profile.StandardPanCurves[selection.Family][
            NativeMixCurveIndex(surroundPan, 2.0f)],
        0.0f, 2.0f);
}

NativeAudioChannelGains CalculateNativeAudioChannelGains(
    const NativeAudioMixProfile& profile, NativeAudioOutputMode outputMode,
    uint16_t sourceChannelCount, uint16_t sourceChannel,
    uint8_t panMode, uint8_t panCurve, float pan, float surroundPan) {
    const bool alternateCurves = outputMode == NativeAudioOutputMode::Surround;
    float left = 0.0f;
    float right = 0.0f;
    if (outputMode == NativeAudioOutputMode::Mono) {
        left = CalculateNativeAudioPanGain(
            profile, panCurve, 0.0f, alternateCurves);
        right = left;
    } else if (sourceChannelCount > 1 && panMode == 1) {
        if (sourceChannel == 0) {
            left = CalculateNativeAudioPanGain(
                profile, panCurve, pan, alternateCurves);
        } else if (sourceChannel == 1) {
            right = CalculateNativeAudioPanGain(
                profile, panCurve, -pan, alternateCurves);
        }
    } else {
        float effectivePan = pan;
        if (sourceChannelCount == 2) {
            effectivePan += sourceChannel == 0 ? -1.0f : 1.0f;
        }
        left = CalculateNativeAudioPanGain(
            profile, panCurve, effectivePan, alternateCurves);
        right = CalculateNativeAudioPanGain(
            profile, panCurve, -effectivePan, alternateCurves);
    }

    float front = 0.0f;
    float rear = 0.0f;
    if (outputMode == NativeAudioOutputMode::Surround) {
        front = CalculateNativeAudioSurroundGain(
            profile, panCurve, surroundPan);
        rear = CalculateNativeAudioSurroundGain(
            profile, panCurve, 2.0f - surroundPan);
    } else {
        front = CalculateNativeAudioSurroundGain(profile, panCurve, 0.0f);
        rear = CalculateNativeAudioSurroundGain(profile, panCurve, 2.0f);
    }
    return {front * left, front * right, rear * left, rear * right};
}

NativeAudioFilterProfile ParseNativeAudioFilterProfile(
    std::span<const uint8_t> codeBin,
    const NativeAudioFilterLayout& layout) {
    if (layout.CodeBaseAddress == 0 || layout.CutoffLookupCount == 0 ||
        layout.CosineLookupCount == 0 || layout.CosineLookupStride < 16) {
        throw std::runtime_error("native audio filter layout is incomplete");
    }
    NativeAudioFilterProfile profile;
    const auto readFloat = [&](uint32_t address, const char* role) {
        return NativeCodeFloatForBase(
            codeBin, layout.CodeBaseAddress, address, role);
    };
    profile.InputMinimum = readFloat(
        layout.InputMinimumAddress, "filter input minimum");
    profile.InputMaximum = readFloat(
        layout.InputMaximumAddress, "filter input maximum");
    profile.MinimumCutoffThreshold = readFloat(
        layout.MinimumCutoffThresholdAddress, "filter minimum threshold");
    profile.MaximumCutoffThreshold = readFloat(
        layout.MaximumCutoffThresholdAddress, "filter maximum threshold");
    profile.CutoffLookupBase = readFloat(
        layout.CutoffLookupBaseAddress, "filter lookup base");
    profile.CutoffLookupScale = readFloat(
        layout.CutoffLookupScaleAddress, "filter lookup scale");
    profile.FrequencyScale = readFloat(
        layout.FrequencyScaleAddress, "filter frequency scale");
    profile.FilterOffset = readFloat(
        layout.FilterOffsetAddress, "filter offset");
    profile.FilterUnit = readFloat(
        layout.FilterUnitAddress, "filter unit");
    profile.CoefficientScale = readFloat(
        layout.CoefficientScaleAddress, "filter coefficient scale");
    profile.CosinePeriod = readFloat(
        layout.CosinePeriodAddress, "filter cosine period");

    const uint32_t cutoffAddress = NativeCodeU32ForBase(
        codeBin, layout.CodeBaseAddress, layout.CutoffLookupPointerAddress,
        "filter cutoff lookup");
    const size_t cutoffOffset = NativeCodeOffsetForBase(
        codeBin, layout.CodeBaseAddress, cutoffAddress,
        layout.CutoffLookupCount * sizeof(uint16_t), "filter cutoff lookup");
    profile.CutoffFrequencies.reserve(layout.CutoffLookupCount);
    for (uint32_t index = 0; index < layout.CutoffLookupCount; ++index) {
        const size_t offset = cutoffOffset + index * sizeof(uint16_t);
        profile.CutoffFrequencies.push_back(static_cast<uint16_t>(
            codeBin[offset] | static_cast<uint16_t>(codeBin[offset + 1] << 8)));
    }

    const uint32_t cosineAddress = NativeCodeU32ForBase(
        codeBin, layout.CodeBaseAddress, layout.CosineLookupPointerAddress,
        "filter cosine lookup");
    NativeCodeOffsetForBase(
        codeBin, layout.CodeBaseAddress, cosineAddress,
        static_cast<size_t>(layout.CosineLookupCount) *
            layout.CosineLookupStride,
        "filter cosine lookup");
    profile.CosineLookup.reserve(layout.CosineLookupCount);
    for (uint32_t index = 0; index < layout.CosineLookupCount; ++index) {
        const uint32_t entryAddress = cosineAddress +
            index * layout.CosineLookupStride;
        profile.CosineLookup.push_back({
            readFloat(entryAddress + 4, "filter cosine value"),
            readFloat(entryAddress + 12, "filter cosine slope"),
        });
    }

    const bool orderedCutoffs = std::is_sorted(
        profile.CutoffFrequencies.begin(), profile.CutoffFrequencies.end());
    if (profile.InputMinimum != 0.0f || profile.InputMaximum != 1.0f ||
        profile.MinimumCutoffThreshold <= profile.InputMinimum ||
        profile.MaximumCutoffThreshold >= profile.InputMaximum ||
        profile.MinimumCutoffThreshold >= profile.MaximumCutoffThreshold ||
        profile.CutoffLookupBase != profile.MinimumCutoffThreshold ||
        profile.CutoffLookupScale <= 0.0f ||
        profile.FrequencyScale <= 0.0f || profile.FilterOffset != 2.0f ||
        profile.FilterUnit != 1.0f || profile.CoefficientScale != 32768.0f ||
        profile.CosinePeriod <= 0.0f || !orderedCutoffs ||
        profile.CutoffFrequencies.front() != 80 ||
        profile.CutoffFrequencies.back() != 12800 ||
        std::abs(profile.CosineLookup.front().Value - 1.0f) > 0.000001f ||
        std::abs(profile.CosineLookup[128].Value + 1.0f) > 0.000001f) {
        throw std::runtime_error("native audio filter profile is invalid");
    }
    return profile;
}

uint16_t CalculateNativeAudioCutoffFrequency(
    const NativeAudioFilterProfile& profile, float cutoff) {
    cutoff = std::clamp(cutoff, profile.InputMinimum, profile.InputMaximum);
    if (cutoff < profile.MinimumCutoffThreshold) {
        return 80;
    }
    if (cutoff >= profile.MaximumCutoffThreshold) {
        return 16000;
    }
    const size_t index = static_cast<size_t>(std::clamp(
        static_cast<int>((cutoff - profile.CutoffLookupBase) *
                         profile.CutoffLookupScale),
        0, static_cast<int>(profile.CutoffFrequencies.size() - 1)));
    return profile.CutoffFrequencies[index];
}

NativeAudioSimpleFilterCoefficients CalculateNativeAudioSimpleFilter(
    const NativeAudioFilterProfile& profile, uint16_t frequency) {
    frequency = std::min<uint16_t>(frequency, 16000);
    float phase = static_cast<float>(frequency) * profile.FrequencyScale;
    phase = std::abs(phase);
    while (phase >= profile.CosinePeriod) {
        phase -= profile.CosinePeriod;
    }
    const uint32_t integerPhase = static_cast<uint32_t>(phase);
    const auto& cosineEntry = profile.CosineLookup[
        integerPhase & static_cast<uint32_t>(profile.CosineLookup.size() - 1)];
    const float cosine = cosineEntry.Value +
        (phase - static_cast<float>(integerPhase)) * cosineEntry.Slope;
    const float offset = profile.FilterOffset - cosine;
    const float feedback = std::sqrt(
        offset * offset - profile.FilterUnit) - offset;
    return {
        static_cast<int16_t>(static_cast<int32_t>(
            (feedback + profile.FilterUnit) * profile.CoefficientScale)),
        static_cast<int16_t>(-static_cast<int32_t>(
            feedback * profile.CoefficientScale)),
    };
}

uint16_t NativeAudioEnvelopeProfile::HoldTicks(uint8_t code) const {
    const uint32_t side = static_cast<uint32_t>(code) + 1;
    return static_cast<uint16_t>((side * side) / 4);
}

float NativeAudioEnvelopeProfile::DecayStep(uint8_t code) const {
    return EnvelopeRateForCode(Decay, code);
}

float NativeAudioEnvelopeProfile::ReleaseStep(uint8_t code) const {
    return EnvelopeRateForCode(Release, code);
}

float NativeAudioEnvelopeProfile::GainForLevel(float level) const {
    return GainForDb(level * LevelToDbScale);
}

float NativeAudioEnvelopeProfile::GainForDb(float db) const {
    if (GainCurve.empty() || GainIndexScale <= 0.0f) {
        return 0.0f;
    }
    db = std::clamp(db, GainMinDb, GainMaxDb);
    const long index = std::lround((db - GainMinDb) * GainIndexScale);
    return GainCurve[static_cast<size_t>(std::clamp(
        index, 0l, static_cast<long>(GainCurve.size() - 1)))];
}

NativeAudioEnvelopeProfile ParseNativeAudioEnvelopeProfile(
    std::span<const uint8_t> codeBin,
    const NativeAudioEnvelopeLayout& layout) {
    if (layout.CodeBaseAddress == 0 || layout.GainTableCount == 0) {
        throw std::runtime_error("native audio envelope layout is incomplete");
    }

    NativeAudioEnvelopeProfile profile;
    profile.FloorDb = NativeCodeFloat(codeBin, layout, layout.FloorDbAddress);
    profile.LevelToDbScale = NativeCodeFloat(
        codeBin, layout, layout.LevelToDbScaleAddress);
    profile.GainMinDb = NativeCodeFloat(
        codeBin, layout, layout.GainMinDbAddress);
    profile.GainMaxDb = NativeCodeFloat(
        codeBin, layout, layout.GainMaxDbAddress);
    profile.GainIndexScale = NativeCodeFloat(
        codeBin, layout, layout.GainIndexScaleAddress);
    profile.Decay = ParseEnvelopeRate(
        codeBin, layout, layout.DecayImmediateAddress,
        layout.DecayNearImmediateAddress, layout.DecayScaleAddress,
        layout.DecayLowSlopeAddress, layout.DecayHighNumeratorAddress);
    profile.Release = ParseEnvelopeRate(
        codeBin, layout, layout.ReleaseImmediateAddress,
        layout.ReleaseNearImmediateAddress, layout.ReleaseScaleAddress,
        layout.ReleaseLowSlopeAddress, layout.ReleaseHighNumeratorAddress);

    const size_t sustainOffset = NativeCodeOffset(
        codeBin, layout, layout.SustainTableAddress,
        profile.SustainLevels.size() * sizeof(int16_t));
    for (size_t index = 0; index < profile.SustainLevels.size(); ++index) {
        profile.SustainLevels[index] = static_cast<int16_t>(
            static_cast<uint16_t>(codeBin[sustainOffset + index * 2]) |
            static_cast<uint16_t>(codeBin[sustainOffset + index * 2 + 1] << 8));
    }
    for (size_t index = 0; index < profile.AttackMultipliers.size(); ++index) {
        profile.AttackMultipliers[index] = NativeCodeFloat(
            codeBin, layout,
            layout.AttackTableAddress + static_cast<uint32_t>(index * 4));
    }
    profile.GainCurve.reserve(layout.GainTableCount);
    for (uint32_t index = 0; index < layout.GainTableCount; ++index) {
        profile.GainCurve.push_back(NativeCodeFloat(
            codeBin, layout, layout.GainTableAddress + index * 4));
    }

    if (profile.LevelToDbScale <= 0.0f || profile.GainMinDb >= profile.GainMaxDb ||
        profile.GainIndexScale <= 0.0f ||
        std::abs(profile.FloorDb - profile.GainMinDb) > 0.001f) {
        throw std::runtime_error("native audio envelope profile is invalid");
    }
    return profile;
}

namespace {

double SequenceTickToSeconds(const NativeAudioSequence& sequence,
                             uint64_t tick) {
    uint64_t cursor = 0;
    uint16_t tempo = sequence.InitialTempo == 0 ? 120 : sequence.InitialTempo;
    double seconds = 0.0;
    for (const auto& change : sequence.TempoEvents) {
        if (change.Tick > tick) {
            break;
        }
        if (change.Tick > cursor) {
            seconds += static_cast<double>(change.Tick - cursor) * 60.0 /
                       (static_cast<double>(tempo) * sequence.Timebase);
        }
        cursor = change.Tick;
        if (change.Tempo != 0) {
            tempo = change.Tempo;
        }
    }
    if (tick > cursor) {
        seconds += static_cast<double>(tick - cursor) * 60.0 /
                   (static_cast<double>(tempo) * sequence.Timebase);
    }
    return seconds;
}

uint64_t SequenceTickToFrame(const NativeAudioSequence& sequence,
                             uint64_t tick, uint32_t sampleRate) {
    return static_cast<uint64_t>(std::max(
        std::llround(SequenceTickToSeconds(sequence, tick) * sampleRate),
        0ll));
}

uint64_t SequenceFrameToTick(const NativeAudioSequence& sequence,
                             uint64_t frame, uint32_t sampleRate) {
    uint64_t low = 0;
    uint64_t high = sequence.EndTick;
    while (low < high) {
        const uint64_t middle = low + (high - low + 1) / 2;
        if (SequenceTickToFrame(sequence, middle, sampleRate) <= frame) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

} // namespace

NativeAudioMixer::NativeAudioMixer(uint32_t outputSampleRate)
    : mOutputSampleRate(outputSampleRate) {
    if (mOutputSampleRate == 0) {
        throw std::runtime_error("native audio mixer output rate is zero");
    }
}

void NativeAudioMixer::SetEnvelopeProfile(
    std::shared_ptr<const NativeAudioEnvelopeProfile> profile) {
    if (!profile || profile->GainCurve.empty()) {
        throw std::runtime_error("native audio envelope profile is empty");
    }
    mEnvelopeProfile = std::move(profile);
}

void NativeAudioMixer::SetModulationProfile(
    std::shared_ptr<const NativeAudioModulationProfile> profile) {
    if (!profile || profile->SineQuarterTable.size() < 33 ||
        profile->UpdateMilliseconds == 0) {
        throw std::runtime_error("native audio modulation profile is empty");
    }
    mModulationProfile = std::move(profile);
}

void NativeAudioMixer::SetMixProfile(
    std::shared_ptr<const NativeAudioMixProfile> profile) {
    if (!profile || profile->PanScale <= 0.0f ||
        profile->SurroundLowerScale <= 0.0f ||
        profile->SurroundUpperScale <= 0.0f) {
        throw std::runtime_error("native audio mix profile is empty");
    }
    mMixProfile = std::move(profile);
}

void NativeAudioMixer::SetFilterProfile(
    std::shared_ptr<const NativeAudioFilterProfile> profile) {
    if (!profile || profile->CutoffFrequencies.empty() ||
        profile->CosineLookup.size() != 256) {
        throw std::runtime_error("native audio filter profile is empty");
    }
    mFilterProfile = std::move(profile);
}

void NativeAudioMixer::SetResamplerProfile(
    std::shared_ptr<const NativeAudioResamplerProfile> profile) {
    if (!profile) {
        throw std::runtime_error("native audio resampler profile is empty");
    }
    mResamplerProfile = std::move(profile);
}

void NativeAudioMixer::SetOutputMode(NativeAudioOutputMode outputMode) {
    mOutputMode = outputMode;
}

void NativeAudioMixer::ConfigureAuxBus(
    size_t bus, const NativeAudioAuxBusSettings& settings) {
    if (const auto* reverb =
            std::get_if<NativeAudioReverbEffect>(&settings.Effect);
        reverb != nullptr && reverb->Enabled &&
        reverb->NativeSampleRate != 0) {
        mDspEffects.SetSampleRates(
            reverb->NativeSampleRate, mOutputSampleRate);
    }
    mDspEffects.ConfigureBus(bus, settings);
}

const NativeAudioAuxBusSettings& NativeAudioMixer::AuxBusSettings(
    size_t bus) const {
    return mDspEffects.BusSettings(bus);
}

void NativeAudioMixer::InitializeEnvelope(Voice& voice) const {
    if (!voice.Cue || !mEnvelopeProfile) {
        return;
    }
    const NativeAudioCue& cue = *voice.Cue;
    auto& envelope = voice.Envelope;
    envelope.Phase = EnvelopePhase::Attack;
    envelope.CurrentLevel =
        mEnvelopeProfile->FloorDb / mEnvelopeProfile->LevelToDbScale;
    envelope.DecayStep = mEnvelopeProfile->DecayStep(cue.Decay);
    envelope.ReleaseStep = mEnvelopeProfile->ReleaseStep(cue.Release);
    envelope.AttackMultiplier = mEnvelopeProfile->AttackMultipliers[cue.Attack];
    envelope.HoldTicks = mEnvelopeProfile->HoldTicks(cue.Hold);
    envelope.HoldRemaining = 0;
    envelope.Sustain = cue.Sustain;
    envelope.FramesUntilAdvance = 0.0;
    envelope.Enabled = true;
}

void NativeAudioMixer::InitializeModulation(Voice& voice) const {
    if (!voice.Cue || !mModulationProfile) {
        return;
    }
    const NativeAudioCue& cue = *voice.Cue;
    auto& modulation = voice.Modulation;
    modulation.Enabled = cue.ModDepth != 0 && cue.ModRange != 0 &&
                         cue.ModCurve == 0 && cue.ModType <= 2;
    modulation.Phase = static_cast<float>(cue.ModPhase) /
                       mModulationProfile->PhaseScale;
    modulation.Phase -= std::floor(modulation.Phase);
    modulation.DelayMilliseconds = static_cast<uint32_t>(
        std::max<int>(cue.ModDelay, 0));
    modulation.ElapsedMilliseconds = 0;
    modulation.FramesUntilAdvance = 0.0;
    if (modulation.Enabled && modulation.DelayMilliseconds == 0) {
        modulation.Value = static_cast<float>(cue.ModRange) *
            mModulationProfile->SampleSine(modulation.Phase) *
            static_cast<float>(static_cast<uint8_t>(cue.ModDepth));
    }

    auto& portamento = voice.Portamento;
    const float sweepPitch =
        static_cast<float>(cue.PortamentoKey) - cue.Note +
        static_cast<float>(cue.SweepPitch) / 256.0f;
    if (cue.PortamentoEnabled && cue.PortamentoTime != 0 &&
        std::abs(sweepPitch) > 0.0f) {
        const float scaledDuration = std::abs(sweepPitch) *
            cue.PortamentoTime * cue.PortamentoTime;
        const uint32_t durationUnits =
            static_cast<uint32_t>(scaledDuration) >> 5;
        portamento.StartPitch = sweepPitch;
        portamento.CurrentPitch = sweepPitch;
        portamento.DurationMilliseconds = durationUnits *
            mModulationProfile->UpdateMilliseconds;
        portamento.ElapsedMilliseconds = 0;
        portamento.Enabled = portamento.DurationMilliseconds != 0;
    }
}

void NativeAudioMixer::AdvanceModulation(Voice& voice, int8_t depth,
                                         int8_t speed, uint8_t type,
                                         int8_t range) const {
    if (!mModulationProfile || !voice.Cue) {
        return;
    }
    const uint32_t elapsed = mModulationProfile->UpdateMilliseconds;
    auto& modulation = voice.Modulation;
    modulation.Enabled = depth != 0 && range != 0 &&
                         voice.Cue->ModCurve == 0 && type <= 2;
    if (modulation.Enabled) {
        modulation.ElapsedMilliseconds += elapsed;
        modulation.Phase += static_cast<float>(speed) * elapsed *
                            mModulationProfile->PhaseAdvanceScale;
        modulation.Phase -= std::floor(modulation.Phase);
        modulation.Value = modulation.ElapsedMilliseconds <
                                   modulation.DelayMilliseconds
            ? 0.0f
            : static_cast<float>(range) *
                  mModulationProfile->SampleSine(modulation.Phase) *
                  static_cast<float>(static_cast<uint8_t>(depth));
    } else {
        modulation.Value = 0.0f;
    }

    auto& portamento = voice.Portamento;
    if (portamento.Enabled) {
        portamento.ElapsedMilliseconds = std::min(
            portamento.ElapsedMilliseconds + elapsed,
            portamento.DurationMilliseconds);
        const float remaining = static_cast<float>(
            portamento.DurationMilliseconds - portamento.ElapsedMilliseconds);
        portamento.CurrentPitch = portamento.StartPitch * remaining /
                                  portamento.DurationMilliseconds;
        if (portamento.ElapsedMilliseconds ==
            portamento.DurationMilliseconds) {
            portamento.Enabled = false;
        }
    }
}

bool NativeAudioMixer::AdvanceEnvelope(Voice& voice, uint32_t ticks) const {
    auto& envelope = voice.Envelope;
    if (!envelope.Enabled || !mEnvelopeProfile) {
        return false;
    }
    switch (envelope.Phase) {
        case EnvelopePhase::Attack:
            for (uint32_t tick = 0; tick < ticks; ++tick) {
                envelope.CurrentLevel *= envelope.AttackMultiplier;
                if (envelope.CurrentLevel >= -0.03125f) {
                    envelope.CurrentLevel = 0.0f;
                    envelope.Phase = EnvelopePhase::Hold;
                    envelope.HoldRemaining = envelope.HoldTicks;
                    return false;
                }
            }
            return false;
        case EnvelopePhase::Hold:
            if (ticks < envelope.HoldRemaining) {
                envelope.HoldRemaining = static_cast<uint16_t>(
                    envelope.HoldRemaining - ticks);
                return false;
            }
            ticks -= envelope.HoldRemaining;
            envelope.HoldRemaining = 0;
            envelope.Phase = EnvelopePhase::Decay;
            [[fallthrough]];
        case EnvelopePhase::Decay: {
            const float sustain = static_cast<float>(
                mEnvelopeProfile->SustainLevels[envelope.Sustain]);
            envelope.CurrentLevel -= envelope.DecayStep * ticks;
            if (envelope.CurrentLevel < sustain) {
                envelope.CurrentLevel = sustain;
                envelope.Phase = EnvelopePhase::Sustain;
            }
            return false;
        }
        case EnvelopePhase::Sustain:
            return false;
        case EnvelopePhase::Release:
            envelope.CurrentLevel -= envelope.ReleaseStep * ticks;
            return envelope.CurrentLevel <=
                   mEnvelopeProfile->FloorDb /
                       mEnvelopeProfile->LevelToDbScale;
    }
    return false;
}

double NativeAudioMixer::EnvelopeGain(const Voice& voice) const {
    if (!voice.Envelope.Enabled || !mEnvelopeProfile) {
        return 1.0;
    }
    return mEnvelopeProfile->GainForLevel(voice.Envelope.CurrentLevel);
}

uint64_t NativeAudioMixer::ControlFramesToSamples(
    uint32_t controlFrames) const {
    return static_cast<uint64_t>(std::llround(
        static_cast<double>(controlFrames) * mOutputSampleRate *
        kNativeAudioControlFrameMilliseconds / 1000.0));
}

void NativeAudioMixer::ConfigureVoiceGainFade(
    Voice& voice, double target, uint64_t sampleFrames, bool stopAfter) {
    target = std::clamp(target, 0.0, 1.0);
    voice.PlayerGainTarget = target;
    voice.StopAfterPlayerFade = stopAfter;
    if (sampleFrames == 0) {
        voice.PlayerGain = target;
        voice.PlayerGainStep = 0.0;
        voice.PlayerGainFramesRemaining = 0;
        return;
    }
    voice.PlayerGainStep = (target - voice.PlayerGain) /
                           static_cast<double>(sampleFrames);
    voice.PlayerGainFramesRemaining = sampleFrames;
}

double NativeAudioMixer::SequencePlayerGainAt(
    const SequencePlayback& playback, uint64_t frame) {
    double gain = std::clamp(
        static_cast<double>(playback.HandleGain.Value(frame)), 0.0, 1.0);
    for (const auto& volume : playback.PlayerVolumeControls) {
        gain *= std::clamp(static_cast<double>(volume.Value(frame)), 0.0, 1.0);
    }
    return gain;
}

float NativeAudioMixer::SequenceControlState::Value(uint64_t frame) const {
    if (!Active || EndFrame <= StartFrame || frame >= EndFrame) {
        return TargetValue;
    }
    if (frame <= StartFrame) {
        return StartValue;
    }
    const float progress = static_cast<float>(frame - StartFrame) /
        static_cast<float>(EndFrame - StartFrame);
    return StartValue + (TargetValue - StartValue) * progress;
}

void NativeAudioMixer::StartOrRefreshVoice(
    uint64_t ownerId, std::shared_ptr<const NativeAudioCue> cue) {
    if (!cue || !cue->Wave || cue->Wave->SampleRate == 0 ||
        cue->Wave->SampleCount == 0 || cue->Wave->ChannelCount == 0 ||
        cue->Wave->InterleavedSamples.empty()) {
        throw std::runtime_error("native audio voice has no decoded wave");
    }
    const auto found = mVoices.find(ownerId);
    if (found != mVoices.end() && found->second.Cue &&
        found->second.Cue->SoundId == cue->SoundId) {
        found->second.Cue = std::move(cue);
        return;
    }
    Voice voice{std::move(cue), {}, 0.0, 127, 64};
    InitializeEnvelope(voice);
    InitializeModulation(voice);
    mVoices[ownerId] = std::move(voice);
}

void NativeAudioMixer::StartOrRefreshStream(
    uint64_t ownerId, std::shared_ptr<const NativeBcwav> stream,
    uint8_t volume, uint8_t pan) {
    if (!stream || stream->SampleRate == 0 || stream->SampleCount == 0 ||
        stream->ChannelCount == 0 || stream->InterleavedSamples.empty()) {
        throw std::runtime_error("native audio stream has no decoded samples");
    }
    const auto found = mVoices.find(ownerId);
    if (found != mVoices.end() && found->second.Stream == stream) {
        found->second.Volume = volume;
        found->second.Pan = pan;
        return;
    }
    mVoices[ownerId] = Voice{{}, std::move(stream), 0.0, volume, pan};
}

void NativeAudioMixer::StartOrRefreshSequence(
    uint64_t ownerId, std::shared_ptr<const NativeAudioSequence> sequence,
    double pitchScale, double gainScale, double spatialPan,
    double spatialSurroundPan, double distanceFilter,
    double spatialAuxSend, uint32_t fadeInFrames) {
    if (!sequence) {
        throw std::runtime_error("native audio sequence is null");
    }
    const auto found = mSequences.find(ownerId);
    if (found != mSequences.end() && found->second.Sequence &&
        found->second.Sequence->SoundId == sequence->SoundId) {
        found->second.TrackEnableMask = sequence->InitialTrackEnableMask;
        found->second.Sequence = std::move(sequence);
        found->second.PitchScale = pitchScale;
        found->second.GainScale = std::max(0.0, gainScale);
        found->second.SpatialPan = std::clamp(spatialPan, -1.0, 1.0);
        found->second.SpatialSurroundPan =
            std::clamp(spatialSurroundPan, 0.0, 2.0);
        found->second.DistanceFilter = std::clamp(distanceFilter, 0.0, 1.0);
        found->second.SpatialAuxSend = std::clamp(spatialAuxSend, 0.0, 1.0);
        for (auto& voice : found->second.Voices) {
            voice.PitchScale = pitchScale;
            voice.GainScale = found->second.GainScale;
            voice.SpatialPan = found->second.SpatialPan;
            voice.SpatialSurroundPan = found->second.SpatialSurroundPan;
            voice.DistanceFilter = found->second.DistanceFilter;
            voice.SpatialAuxSend = found->second.SpatialAuxSend;
        }
        std::erase_if(found->second.Voices, [&](const Voice& voice) {
            return (found->second.TrackEnableMask &
                    (uint16_t{1} << voice.SequenceTrack)) == 0;
        });
        return;
    }
    mVoices.erase(ownerId);
    SequencePlayback playback;
    playback.Sequence = std::move(sequence);
    playback.TrackEnableMask = playback.Sequence->InitialTrackEnableMask;
    playback.PitchScale = pitchScale;
    playback.GainScale = std::max(0.0, gainScale);
    playback.SpatialPan = std::clamp(spatialPan, -1.0, 1.0);
    playback.SpatialSurroundPan = std::clamp(spatialSurroundPan, 0.0, 2.0);
    playback.DistanceFilter = std::clamp(distanceFilter, 0.0, 1.0);
    playback.SpatialAuxSend = std::clamp(spatialAuxSend, 0.0, 1.0);
    playback.HandleGain.StartValue = fadeInFrames == 0 ? 1.0f : 0.0f;
    playback.HandleGain.TargetValue = 1.0f;
    playback.HandleGain.StartFrame = 0;
    playback.HandleGain.EndFrame = ControlFramesToSamples(fadeInFrames);
    playback.HandleGain.Active = fadeInFrames != 0;
    for (auto& volume : playback.PlayerVolumeControls) {
        volume.StartValue = 1.0f;
        volume.TargetValue = 1.0f;
    }
    mSequences[ownerId] = std::move(playback);
}

bool NativeAudioMixer::SetSequencePlayerVolume(
    uint64_t ownerId, uint8_t layer, uint8_t volume, uint32_t fadeFrames) {
    const auto found = mSequences.find(ownerId);
    if (found == mSequences.end() ||
        layer >= found->second.PlayerVolumeControls.size()) {
        return false;
    }
    SequencePlayback& playback = found->second;
    SequenceControlState& control = playback.PlayerVolumeControls[layer];
    const float current = control.Value(playback.OutputFrame);
    const float normalized = static_cast<float>(volume) / 127.0f;
    const float target = normalized * normalized;
    control.StartValue = fadeFrames == 0 ? target : current;
    control.TargetValue = target;
    control.StartFrame = playback.OutputFrame;
    control.EndFrame = playback.OutputFrame +
                       ControlFramesToSamples(fadeFrames);
    control.Active = fadeFrames != 0;
    return true;
}

bool NativeAudioMixer::SetSequenceTrackEnabled(
    uint64_t ownerId, uint8_t firstTrack, uint8_t lastTrack, bool enabled) {
    const auto found = mSequences.find(ownerId);
    if (found == mSequences.end() || firstTrack > lastTrack || lastTrack >= 16) {
        return false;
    }
    SequencePlayback& playback = found->second;
    for (uint8_t track = firstTrack; track <= lastTrack; ++track) {
        const uint16_t bit = uint16_t{1} << track;
        if (enabled) {
            playback.TrackEnableMask |= bit;
        } else {
            playback.TrackEnableMask &= static_cast<uint16_t>(~bit);
        }
    }
    if (!enabled) {
        std::erase_if(playback.Voices, [&](const Voice& voice) {
            return voice.SequenceTrack >= firstTrack &&
                   voice.SequenceTrack <= lastTrack;
        });
    }
    return true;
}

bool NativeAudioMixer::ReconfigureSequence(
    uint64_t ownerId,
    std::shared_ptr<const NativeAudioSequence> currentCycle,
    std::shared_ptr<const NativeAudioSequence> nextLoop) {
    const auto found = mSequences.find(ownerId);
    if (found == mSequences.end() || !currentCycle || !nextLoop ||
        !found->second.Sequence ||
        currentCycle->SoundId != found->second.Sequence->SoundId ||
        nextLoop->SoundId != found->second.Sequence->SoundId) {
        return false;
    }
    SequencePlayback& playback = found->second;
    const uint64_t currentTick = SequenceFrameToTick(
        *playback.Sequence, playback.TimelineFrame, mOutputSampleRate);
    playback.Sequence = std::move(currentCycle);
    playback.NextLoopSequence = std::move(nextLoop);
    playback.TimelineFrame = SequenceTickToFrame(
        *playback.Sequence,
        std::min(currentTick, playback.Sequence->EndTick),
        mOutputSampleRate);
    playback.NextEvent = static_cast<size_t>(std::upper_bound(
        playback.Sequence->Events.begin(), playback.Sequence->Events.end(),
        currentTick,
        [](uint64_t tick, const NativeAudioSequenceEvent& event) {
            return tick < event.StartTick;
        }) - playback.Sequence->Events.begin());
    playback.NextControlEvent = 0;
    playback.TrackControls = {};
    playback.MasterVolumeControl = {};
    for (const auto& control : playback.Sequence->ControlEvents) {
        if (control.StartTick > currentTick) {
            break;
        }
        ++playback.NextControlEvent;
        SequenceControlState& state =
            control.Kind == NativeCseqControlKind::MasterVolume
                ? playback.MasterVolumeControl
                : playback.TrackControls[
                      static_cast<size_t>(control.Kind)][control.Track];
        state.StartValue = control.StartValue;
        state.TargetValue = control.TargetValue;
        state.StartFrame = SequenceTickToFrame(
            *playback.Sequence, control.StartTick, mOutputSampleRate);
        state.EndFrame = SequenceTickToFrame(
            *playback.Sequence,
            control.StartTick + control.DurationTicks,
            mOutputSampleRate);
        state.Active = true;
    }
    return true;
}

std::optional<double> NativeAudioMixer::SequencePlayerGain(
    uint64_t ownerId) const {
    const auto found = mSequences.find(ownerId);
    if (found == mSequences.end()) {
        return {};
    }
    return SequencePlayerGainAt(found->second, found->second.OutputFrame);
}

std::optional<uint64_t> NativeAudioMixer::SequenceTimelineTick(
    uint64_t ownerId) const {
    const auto found = mSequences.find(ownerId);
    if (found == mSequences.end() || !found->second.Sequence) {
        return {};
    }
    return SequenceFrameToTick(*found->second.Sequence,
                               found->second.TimelineFrame,
                               mOutputSampleRate);
}

std::optional<uint64_t> NativeAudioMixer::SequenceLoopCount(
    uint64_t ownerId) const {
    const auto found = mSequences.find(ownerId);
    return found == mSequences.end()
        ? std::optional<uint64_t>{}
        : std::optional<uint64_t>{found->second.LoopCount};
}

void NativeAudioMixer::StopVoice(uint64_t ownerId, uint32_t fadeOutFrames) {
    if (fadeOutFrames == 0) {
        mVoices.erase(ownerId);
        mSequences.erase(ownerId);
        return;
    }
    if (const auto voice = mVoices.find(ownerId); voice != mVoices.end()) {
        ConfigureVoiceGainFade(voice->second, 0.0,
                               ControlFramesToSamples(fadeOutFrames), true);
    }
    if (const auto sequence = mSequences.find(ownerId);
        sequence != mSequences.end()) {
        SequencePlayback& playback = sequence->second;
        const float current = playback.HandleGain.Value(playback.OutputFrame);
        playback.HandleGain.StartValue = current;
        playback.HandleGain.TargetValue = 0.0f;
        playback.HandleGain.StartFrame = playback.OutputFrame;
        playback.HandleGain.EndFrame = playback.OutputFrame +
            ControlFramesToSamples(fadeOutFrames);
        playback.HandleGain.Active = true;
        playback.Stopping = true;
    }
}

std::vector<int16_t> NativeAudioMixer::MixStereo(size_t frameCount) {
    constexpr size_t kMixChannels = kNativeAudioDspChannelCount;
    std::vector<double> accumulator(frameCount * kMixChannels, 0.0);
    std::array<std::vector<int32_t>, 2> auxiliary{
        std::vector<int32_t>(frameCount * kMixChannels, 0),
        std::vector<int32_t>(frameCount * kMixChannels, 0),
    };
    const auto mixVoice = [&](Voice& voice,
                              const SequencePlayback* sequencePlayback,
                              size_t mixFrameCount,
                              size_t destinationFrame,
                              uint64_t sequenceStartFrame) {
        const NativeAudioCue* cue = voice.Cue.get();
        const auto& wave = cue != nullptr ? *cue->Wave : *voice.Stream;
        const double basePitchSemitones = cue != nullptr
            ? static_cast<double>(cue->Note) + cue->Transpose - cue->RootKey
            : 0.0;
        const double velocity = cue != nullptr
            ? static_cast<double>(cue->Velocity) * cue->VelocityRange /
                  (127.0 * 127.0)
            : 1.0;
        const auto controlValue = [&](NativeCseqControlKind kind,
                                      float fallback,
                                      uint64_t frame) {
            if (sequencePlayback == nullptr || !voice.SequenceVoice) {
                return fallback;
            }
            const SequenceControlState& state =
                kind == NativeCseqControlKind::MasterVolume
                    ? sequencePlayback->MasterVolumeControl
                    : sequencePlayback->TrackControls[
                          static_cast<size_t>(kind)][voice.SequenceTrack];
            return state.Active ? state.Value(frame) : fallback;
        };
        std::optional<NativeAudioSimpleFilterCoefficients> simpleFilter;
        if (cue != nullptr && mFilterProfile) {
            const float cutoff = std::clamp(
                static_cast<float>(cue->LpfCutoff) / 64.0f -
                    static_cast<float>(voice.DistanceFilter),
                0.0f, 1.0f);
            const uint16_t frequency = CalculateNativeAudioCutoffFrequency(
                *mFilterProfile, cutoff);
            if (frequency < 16000) {
                simpleFilter = CalculateNativeAudioSimpleFilter(
                    *mFilterProfile, frequency);
            } else {
                voice.SimpleFilterHistory = {};
            }
        }

        size_t outputFrame = 0;
        if (voice.DelayFrames >= mixFrameCount) {
            voice.DelayFrames -= mixFrameCount;
            return false;
        }
        outputFrame = voice.DelayFrames;
        voice.DelayFrames = 0;
        mMixedVoiceSampleCount += mixFrameCount - outputFrame;
        for (; outputFrame < mixFrameCount; ++outputFrame) {
            const uint64_t sequenceFrame = sequencePlayback != nullptr
                ? sequenceStartFrame + outputFrame
                : 0;
            if (voice.NoteOnFramesRemaining == 0) {
                if (!voice.Envelope.Enabled) {
                    return true;
                }
                voice.Envelope.Phase = EnvelopePhase::Release;
            }
            if (voice.Envelope.Enabled) {
                while (voice.Envelope.FramesUntilAdvance <= 0.0) {
                    if (AdvanceEnvelope(voice, 5)) {
                        return true;
                    }
                    voice.Envelope.FramesUntilAdvance +=
                        static_cast<double>(mOutputSampleRate) * 0.005;
                }
                voice.Envelope.FramesUntilAdvance -= 1.0;
            }
            if (mModulationProfile &&
                (voice.SequenceVoice || voice.Modulation.Enabled ||
                 voice.Portamento.Enabled)) {
                while (voice.Modulation.FramesUntilAdvance <= 0.0) {
                    const int8_t modDepth = static_cast<int8_t>(std::clamp(
                        std::lround(controlValue(
                            NativeCseqControlKind::ModDepth,
                            static_cast<float>(cue->ModDepth), sequenceFrame)),
                        -128l, 127l));
                    const int8_t modSpeed = static_cast<int8_t>(std::clamp(
                        std::lround(controlValue(
                            NativeCseqControlKind::ModSpeed,
                            static_cast<float>(cue->ModSpeed), sequenceFrame)),
                        -128l, 127l));
                    const uint8_t modType = static_cast<uint8_t>(std::clamp(
                        std::lround(controlValue(
                            NativeCseqControlKind::ModType,
                            static_cast<float>(cue->ModType), sequenceFrame)),
                        0l, 255l));
                    const int8_t modRange = static_cast<int8_t>(std::clamp(
                        std::lround(controlValue(
                            NativeCseqControlKind::ModRange,
                            static_cast<float>(cue->ModRange), sequenceFrame)),
                        -128l, 127l));
                    AdvanceModulation(voice, modDepth, modSpeed, modType,
                                      modRange);
                    voice.Modulation.FramesUntilAdvance +=
                        static_cast<double>(mOutputSampleRate) *
                        mModulationProfile->UpdateMilliseconds / 1000.0;
                }
                voice.Modulation.FramesUntilAdvance -= 1.0;
            }
            if (voice.SourceFrame >= wave.SampleCount) {
                if (!wave.Looping || wave.LoopStart >= wave.SampleCount) {
                    return true;
                }
                const double loopLength = wave.SampleCount - wave.LoopStart;
                voice.SourceFrame = wave.LoopStart +
                    std::fmod(voice.SourceFrame - wave.LoopStart, loopLength);
                voice.SourceLooped = true;
            }
            const uint32_t frame0 = static_cast<uint32_t>(voice.SourceFrame);
            const double fraction = voice.SourceFrame - frame0;
            const auto source = [&](int64_t frame, uint16_t channel) {
                if (wave.Looping && wave.LoopStart < wave.SampleCount) {
                    const int64_t loopStart = wave.LoopStart;
                    const int64_t loopLength = wave.SampleCount - wave.LoopStart;
                    if (voice.SourceLooped && frame < loopStart &&
                        loopStart - frame <= loopLength) {
                        frame = static_cast<int64_t>(wave.SampleCount) -
                                (loopStart - frame);
                    } else if (frame >=
                               static_cast<int64_t>(wave.SampleCount)) {
                        frame = loopStart +
                            (frame - static_cast<int64_t>(wave.SampleCount)) %
                                loopLength;
                    }
                }
                if (frame < 0 ||
                    frame >= static_cast<int64_t>(wave.SampleCount)) {
                    return int16_t{0};
                }
                const uint16_t selected = wave.ChannelCount == 1 ? 0 : channel;
                return wave.InterleavedSamples[
                    static_cast<size_t>(frame) * wave.ChannelCount + selected];
            };
            std::array<double, 2> sourceSamples{};
            const uint16_t sourceChannelCount = std::min<uint16_t>(
                wave.ChannelCount, 2);
            const uint8_t interpolationType = cue != nullptr
                ? cue->InterpolationType
                : 0;
            for (uint16_t sourceChannel = 0;
                 sourceChannel < sourceChannelCount; ++sourceChannel) {
                double sample = 0.0;
                if (interpolationType == 0 && mResamplerProfile) {
                    const size_t phase = std::min<size_t>(
                        static_cast<size_t>(fraction *
                            NativeAudioResamplerProfile::PhaseCount),
                        NativeAudioResamplerProfile::PhaseCount - 1);
                    const auto& coefficients =
                        mResamplerProfile->PolyphaseCoefficients[phase];
                    int64_t filtered = 0;
                    for (size_t tap = 0;
                         tap < NativeAudioResamplerProfile::TapCount; ++tap) {
                        const int64_t sourceFrame =
                            static_cast<int64_t>(frame0) +
                            static_cast<int64_t>(tap) - 1;
                        filtered += static_cast<int64_t>(coefficients[tap]) *
                                    source(sourceFrame, sourceChannel);
                    }
                    sample = static_cast<double>(std::clamp<int64_t>(
                        filtered >> 15, -32768, 32767));
                } else if (interpolationType <= 1) {
                    const int32_t first = source(frame0, sourceChannel);
                    const int32_t second = source(
                        static_cast<int64_t>(frame0) + 1, sourceChannel);
                    const int32_t delta = std::clamp(
                        second - first, -32768, 32767);
                    sample = first + delta * fraction;
                } else if (interpolationType == 2) {
                    sample = source(frame0, sourceChannel);
                } else {
                    throw std::runtime_error(
                        "unsupported native audio interpolation type");
                }
                if (simpleFilter.has_value()) {
                    const int32_t input = static_cast<int32_t>(std::clamp(
                        std::lround(sample), -32768l, 32767l));
                    const int64_t filtered =
                        static_cast<int64_t>(simpleFilter->B0) * input +
                        static_cast<int64_t>(simpleFilter->A1) *
                            voice.SimpleFilterHistory[sourceChannel];
                    const int32_t output = static_cast<int32_t>(std::clamp<int64_t>(
                        filtered >> 15, -32768, 32767));
                    voice.SimpleFilterHistory[sourceChannel] = output;
                    sample = output;
                }
                sourceSamples[sourceChannel] = sample;
            }
            const float modulationValue = voice.Modulation.Value;
            const uint8_t modulationType = cue != nullptr
                ? static_cast<uint8_t>(std::clamp(
                      std::lround(controlValue(
                          NativeCseqControlKind::ModType,
                          static_cast<float>(cue->ModType), sequenceFrame)),
                      0l, 255l))
                : 0;
            const double modulationGain =
                cue != nullptr && modulationType == 1 && mEnvelopeProfile
                ? mEnvelopeProfile->GainForDb(modulationValue * 6.0f)
                : 1.0;
            const double trackVolume = cue != nullptr
                ? std::clamp(static_cast<double>(controlValue(
                      NativeCseqControlKind::Volume,
                      static_cast<float>(cue->TrackVolume), sequenceFrame)),
                      0.0, 127.0)
                : 127.0;
            const double expression = cue != nullptr
                ? std::clamp(static_cast<double>(controlValue(
                      NativeCseqControlKind::Expression,
                      static_cast<float>(cue->Expression), sequenceFrame)),
                      0.0, 127.0)
                : 127.0;
            const double masterVolume = cue != nullptr
                ? std::clamp(static_cast<double>(controlValue(
                      NativeCseqControlKind::MasterVolume,
                      static_cast<float>(cue->MasterVolume), sequenceFrame)),
                      0.0, 127.0)
                : 127.0;
            const double baseAmplitude = cue != nullptr
                ? velocity * velocity *
                      (static_cast<double>(cue->Volume) / 127.0) *
                      (static_cast<double>(cue->ArchiveVolume) / 127.0) *
                      (trackVolume / 127.0) * (expression / 127.0) *
                      (masterVolume / 127.0)
                : static_cast<double>(voice.Volume) / 127.0;
            const double playerGain = sequencePlayback != nullptr
                ? SequencePlayerGainAt(
                      *sequencePlayback,
                      sequencePlayback->OutputFrame + destinationFrame +
                          outputFrame)
                : voice.PlayerGain;
            const double amplitude = baseAmplitude * EnvelopeGain(voice) *
                                     modulationGain * voice.GainScale *
                                     playerGain;
            const double trackPan = cue != nullptr
                ? controlValue(NativeCseqControlKind::Pan,
                               static_cast<float>(cue->TrackPan), sequenceFrame)
                : 64.0;
            const double basePan = std::clamp(
                cue != nullptr
                    ? static_cast<double>(cue->Pan) + trackPan - 64.0
                    : static_cast<double>(voice.Pan),
                0.0, 127.0);
            const double nativePanByte = std::clamp(
                basePan + (cue != nullptr && modulationType == 2
                               ? modulationValue
                               : 0.0),
                0.0, 127.0);
            std::array<double, kMixChannels> sample{};
            if (mMixProfile) {
                const uint8_t panByte = static_cast<uint8_t>(std::clamp(
                    std::lround(nativePanByte), 0l, 127l));
                const float nativePan = NormalizeNativeAudioPan(
                    panByte, *mMixProfile) +
                    static_cast<float>(voice.SpatialPan);
                const double trackSurroundPan = cue != nullptr
                    ? controlValue(
                          NativeCseqControlKind::SurroundPan,
                          static_cast<float>(cue->TrackSurroundPan),
                          sequenceFrame)
                    : 0.0;
                const int surroundValue = cue != nullptr
                    ? static_cast<int>(cue->SurroundPan) +
                          static_cast<int>(std::lround(trackSurroundPan))
                    : 0;
                const uint8_t surroundByte = static_cast<uint8_t>(
                    std::clamp(surroundValue, 0, 127));
                const float nativeSurroundPan = std::clamp(
                    NormalizeNativeAudioSurroundPan(
                        surroundByte, *mMixProfile) +
                        static_cast<float>(voice.SpatialSurroundPan),
                    0.0f, 2.0f);
                for (uint16_t sourceChannel = 0;
                     sourceChannel < sourceChannelCount; ++sourceChannel) {
                    const double sourceSample = sourceSamples[sourceChannel];
                    const auto gains = CalculateNativeAudioChannelGains(
                        *mMixProfile, mOutputMode, sourceChannelCount,
                        sourceChannel, cue != nullptr ? cue->PanMode : 0,
                        cue != nullptr ? cue->PanCurve : 0, nativePan,
                        nativeSurroundPan);
                    sample[0] += sourceSample * amplitude * gains.FrontLeft;
                    sample[1] += sourceSample * amplitude * gains.FrontRight;
                    sample[2] += sourceSample * amplitude * gains.RearLeft;
                    sample[3] += sourceSample * amplitude * gains.RearRight;
                }
            } else {
                const double fallbackPanByte = std::clamp(
                    nativePanByte + voice.SpatialPan * 64.0, 0.0, 127.0);
                const double left = sourceSamples[0];
                const double right = sourceChannelCount == 1
                    ? sourceSamples[0] : sourceSamples[1];
                const double leftGain = fallbackPanByte <= 64.0
                    ? 1.0 : (127.0 - fallbackPanByte) / 63.0;
                const double rightGain = fallbackPanByte >= 64.0
                    ? 1.0 : fallbackPanByte / 64.0;
                sample[0] = left * amplitude * leftGain;
                sample[1] = right * amplitude * rightGain;
            }
            const double mainSend = cue != nullptr
                ? std::clamp(static_cast<double>(controlValue(
                      NativeCseqControlKind::MainSend,
                      static_cast<float>(cue->MainSend), sequenceFrame)),
                      0.0, 127.0) / 127.0
                : 1.0;
            std::array<double, 2> effectSends = cue != nullptr
                ? std::array<double, 2>{
                      std::clamp(static_cast<double>(controlValue(
                          NativeCseqControlKind::FxSendA,
                          static_cast<float>(cue->FxSendA), sequenceFrame)),
                          0.0, 127.0) / 127.0,
                      std::clamp(static_cast<double>(controlValue(
                          NativeCseqControlKind::FxSendB,
                          static_cast<float>(cue->FxSendB), sequenceFrame)),
                          0.0, 127.0) / 127.0,
                  }
                : std::array<double, 2>{0.0, 0.0};
            effectSends[0] = std::clamp(
                effectSends[0] + voice.SpatialAuxSend, 0.0, 1.0);
            for (size_t channel = 0; channel < sample.size(); ++channel) {
                const size_t outputIndex =
                    (destinationFrame + outputFrame) * kMixChannels + channel;
                accumulator[outputIndex] += sample[channel] * mainSend;
                for (size_t bus = 0; bus < auxiliary.size(); ++bus) {
                    const long routed = std::lround(
                        sample[channel] * effectSends[bus]);
                    const int32_t routedSample = static_cast<int32_t>(
                        std::clamp(
                            routed,
                            static_cast<long>(std::numeric_limits<int32_t>::min()),
                            static_cast<long>(std::numeric_limits<int32_t>::max())));
                    auxiliary[bus][outputIndex] = static_cast<int32_t>(
                        static_cast<uint32_t>(auxiliary[bus][outputIndex]) +
                        static_cast<uint32_t>(routedSample));
                }
            }
            const double pitchModulation =
                voice.Portamento.CurrentPitch +
                (cue != nullptr && modulationType == 0
                     ? modulationValue
                     : 0.0);
            const double pitchBend = cue != nullptr
                ? controlValue(NativeCseqControlKind::PitchBend,
                               static_cast<float>(cue->PitchBend), sequenceFrame)
                : 0.0;
            const double pitchBendRange = cue != nullptr
                ? controlValue(NativeCseqControlKind::PitchBendRange,
                               static_cast<float>(cue->PitchBendRange),
                               sequenceFrame)
                : 0.0;
            const double pitch = cue != nullptr
                ? std::exp2((basePitchSemitones +
                             pitchBend * pitchBendRange / 128.0 +
                             pitchModulation) / 12.0) *
                      cue->Pitch * voice.PitchScale
                : 1.0;
            voice.SourceFrame += static_cast<double>(wave.SampleRate) /
                                 static_cast<double>(mOutputSampleRate) * pitch;
            if (voice.NoteOnFramesRemaining != ~uint64_t{0} &&
                voice.NoteOnFramesRemaining != 0) {
                --voice.NoteOnFramesRemaining;
            }
            if (sequencePlayback == nullptr &&
                voice.PlayerGainFramesRemaining != 0) {
                voice.PlayerGain += voice.PlayerGainStep;
                --voice.PlayerGainFramesRemaining;
                if (voice.PlayerGainFramesRemaining == 0) {
                    voice.PlayerGain = voice.PlayerGainTarget;
                    if (voice.StopAfterPlayerFade) {
                        return true;
                    }
                }
            }
        }
        return false;
    };

    std::vector<uint64_t> completed;
    mMaximumMixedVoiceCount = std::max(
        mMaximumMixedVoiceCount, ActiveMixedVoiceCount());
    for (auto& [ownerId, voice] : mVoices) {
        if (mixVoice(voice, nullptr, frameCount, 0, 0)) {
            completed.push_back(ownerId);
        }
    }
    for (const uint64_t ownerId : completed) {
        mVoices.erase(ownerId);
    }

    const auto tickToFrame = [&](const NativeAudioSequence& sequence, uint64_t tick) {
        return SequenceTickToFrame(sequence, tick, mOutputSampleRate);
    };
    const auto advanceSequenceControls = [&](SequencePlayback& playback,
                                             uint64_t frame) {
        const NativeAudioSequence& sequence = *playback.Sequence;
        while (playback.NextControlEvent < sequence.ControlEvents.size()) {
            const NativeCseqControlEvent& control =
                sequence.ControlEvents[playback.NextControlEvent];
            const uint64_t start = tickToFrame(sequence, control.StartTick);
            if (start > frame) {
                break;
            }
            ++playback.NextControlEvent;
            SequenceControlState& state =
                control.Kind == NativeCseqControlKind::MasterVolume
                    ? playback.MasterVolumeControl
                    : playback.TrackControls[
                          static_cast<size_t>(control.Kind)][control.Track];
            state.StartValue = control.StartValue;
            state.TargetValue = control.TargetValue;
            state.StartFrame = start;
            state.EndFrame = tickToFrame(
                sequence, control.StartTick + control.DurationTicks);
            state.Active = true;
        }
    };

    std::vector<uint64_t> completedSequences;
    for (auto& [ownerId, playback] : mSequences) {
        size_t destinationFrame = 0;
        while (destinationFrame < frameCount) {
            const NativeAudioSequence& sequence = *playback.Sequence;
            const uint64_t endFrame = tickToFrame(
                sequence, sequence.EndTick);
            const uint64_t loopFrame = tickToFrame(
                sequence, sequence.LoopStartTick);
            const bool validLoop =
                sequence.Looping && endFrame > loopFrame;
            if (validLoop && playback.TimelineFrame >= endFrame) {
                if (playback.NextLoopSequence) {
                    playback.Sequence = std::move(
                        playback.NextLoopSequence);
                }
                const NativeAudioSequence& next = *playback.Sequence;
                const uint64_t nextLoopFrame = tickToFrame(
                    next, next.LoopStartTick);
                playback.TimelineFrame = nextLoopFrame;
                playback.NextControlEvent = 0;
                playback.TrackControls = {};
                playback.MasterVolumeControl = {};
                advanceSequenceControls(playback, nextLoopFrame);
                playback.NextEvent = static_cast<size_t>(std::lower_bound(
                    next.Events.begin(), next.Events.end(),
                    next.LoopStartTick,
                    [](const NativeAudioSequenceEvent& event, uint64_t tick) {
                        return event.StartTick < tick;
                    }) - next.Events.begin());
                ++playback.LoopCount;
                continue;
            }
            const uint64_t cursor = playback.TimelineFrame;
            advanceSequenceControls(playback, cursor);
            while (playback.NextEvent < sequence.Events.size()) {
                const NativeAudioSequenceEvent& event =
                    sequence.Events[playback.NextEvent];
                const uint64_t startFrame = tickToFrame(
                    sequence, event.StartTick);
                if (startFrame > cursor) {
                    break;
                }
                ++playback.NextEvent;
                if ((playback.TrackEnableMask &
                     (uint16_t{1} << event.Track)) == 0 ||
                    event.Silent || !event.Cue.Wave ||
                    event.Cue.Wave->InterleavedSamples.empty()) {
                    continue;
                }
                std::shared_ptr<const NativeAudioCue> cue(
                    playback.Sequence, &event.Cue);
                if (event.Tie) {
                    const auto tied = std::find_if(
                        playback.Voices.rbegin(), playback.Voices.rend(),
                        [&](const Voice& voice) {
                            return voice.SequenceVoice &&
                                   voice.SequenceTrack == event.Track &&
                                   voice.NoteOnFramesRemaining != 0;
                        });
                    if (tied != playback.Voices.rend()) {
                        const auto previousWave = tied->Cue->Wave;
                        if (previousWave != cue->Wave) {
                            auto tiedCue =
                                std::make_shared<NativeAudioCue>(event.Cue);
                            tiedCue->Wave = previousWave;
                            cue = std::move(tiedCue);
                        }
                        tied->Cue = std::move(cue);
                        tied->PitchScale = playback.PitchScale;
                        tied->GainScale = playback.GainScale;
                        tied->SpatialPan = playback.SpatialPan;
                        tied->SpatialSurroundPan =
                            playback.SpatialSurroundPan;
                        tied->DistanceFilter = playback.DistanceFilter;
                        tied->SpatialAuxSend = playback.SpatialAuxSend;
                        const ModulationState modulation = tied->Modulation;
                        InitializeModulation(*tied);
                        tied->Modulation = modulation;
                        if (event.DurationTicks != 0 &&
                            !event.Cue.IgnoreNoteOff) {
                            const uint64_t endFrame = tickToFrame(
                                sequence,
                                event.StartTick + event.DurationTicks);
                            tied->NoteOnFramesRemaining =
                                std::max<uint64_t>(endFrame - startFrame, 1);
                        }
                        continue;
                    }
                }
                Voice voice{std::move(cue), {}, 0.0, 127, 64};
                voice.PitchScale = playback.PitchScale;
                voice.GainScale = playback.GainScale;
                voice.SpatialPan = playback.SpatialPan;
                voice.SpatialSurroundPan = playback.SpatialSurroundPan;
                voice.DistanceFilter = playback.DistanceFilter;
                voice.SpatialAuxSend = playback.SpatialAuxSend;
                voice.SequenceTrack = event.Track;
                voice.SequenceVoice = true;
                InitializeEnvelope(voice);
                InitializeModulation(voice);
                if (event.DurationTicks != 0 && !event.Cue.IgnoreNoteOff) {
                    const uint64_t endFrame = tickToFrame(
                        sequence, event.StartTick + event.DurationTicks);
                    voice.NoteOnFramesRemaining =
                        std::max<uint64_t>(endFrame - startFrame, 1);
                }
                playback.Voices.push_back(std::move(voice));
            }

            uint64_t boundary = cursor + (frameCount - destinationFrame);
            if (playback.NextEvent < sequence.Events.size()) {
                boundary = std::min(
                    boundary, tickToFrame(
                                  sequence,
                                  sequence.Events[playback.NextEvent].StartTick));
            }
            if (playback.NextControlEvent < sequence.ControlEvents.size()) {
                boundary = std::min(
                    boundary,
                    tickToFrame(sequence,
                                sequence.ControlEvents[
                                    playback.NextControlEvent].StartTick));
            }
            if (validLoop) {
                boundary = std::min(boundary, endFrame);
            }
            if (boundary <= cursor) {
                throw std::runtime_error(
                    "native CSEQ scheduler failed to advance");
            }
            const size_t segmentFrames = static_cast<size_t>(
                boundary - cursor);
            playback.Voices.erase(
                std::remove_if(playback.Voices.begin(), playback.Voices.end(),
                               [&](Voice& voice) {
                                   return mixVoice(
                                       voice, &playback, segmentFrames,
                                       destinationFrame, cursor);
                               }),
                playback.Voices.end());
            playback.TimelineFrame = boundary;
            destinationFrame += segmentFrames;
        }
        playback.OutputFrame += frameCount;

        if (playback.Stopping &&
            playback.OutputFrame >= playback.HandleGain.EndFrame) {
            completedSequences.push_back(ownerId);
            continue;
        }

        const NativeAudioSequence& sequence = *playback.Sequence;
        const uint64_t endFrame = tickToFrame(sequence, sequence.EndTick);
        const uint64_t loopFrame = tickToFrame(
            sequence, sequence.LoopStartTick);
        const bool validLoop = sequence.Looping && endFrame > loopFrame;
        if (!validLoop && playback.NextEvent == sequence.Events.size() &&
            playback.Voices.empty()) {
            completedSequences.push_back(ownerId);
        }
    }
    for (const uint64_t ownerId : completedSequences) {
        mSequences.erase(ownerId);
    }

    for (size_t bus = 0; bus < auxiliary.size(); ++bus) {
        const auto returned = mDspEffects.ProcessBus(
            bus, auxiliary[bus], kMixChannels);
        const double returnVolume = mDspEffects.BusSettings(bus).ReturnVolume;
        for (size_t sample = 0; sample < returned.size(); ++sample) {
            accumulator[sample] += returned[sample] * returnVolume;
        }
    }

    std::vector<int16_t> output(frameCount * 2);
    for (size_t frame = 0; frame < frameCount; ++frame) {
        const size_t source = frame * kMixChannels;
        const size_t destination = frame * 2;
        output[destination] = static_cast<int16_t>(std::clamp(
            std::lround(accumulator[source] + accumulator[source + 2]),
            -32768l, 32767l));
        output[destination + 1] = static_cast<int16_t>(std::clamp(
            std::lround(accumulator[source + 1] + accumulator[source + 3]),
            -32768l, 32767l));
    }
    return output;
}

size_t NativeAudioMixer::ActiveVoiceCount() const {
    return mVoices.size() + mSequences.size();
}

size_t NativeAudioMixer::ActiveMixedVoiceCount() const {
    size_t count = mVoices.size();
    for (const auto& [ownerId, playback] : mSequences) {
        (void)ownerId;
        count += playback.Voices.size();
    }
    return count;
}

uint64_t NativeAudioMixer::MixedVoiceSampleCount() const {
    return mMixedVoiceSampleCount;
}

size_t NativeAudioMixer::MaximumMixedVoiceCount() const {
    return mMaximumMixedVoiceCount;
}

bool NativeAudioMixer::IsVoiceActive(uint64_t ownerId) const {
    return mVoices.contains(ownerId) || mSequences.contains(ownerId);
}

void NativeAudioMixer::StopAll() {
    mVoices.clear();
    mSequences.clear();
    mDspEffects.Reset();
    mMixedVoiceSampleCount = 0;
    mMaximumMixedVoiceCount = 0;
}

} // namespace Oot3dNativeGame
