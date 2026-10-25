// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include "audio_core/audio_types.h"
#include "audio_core/codec.h"
#include "common/assert.h"
#include "common/common_types.h"

namespace AudioCore::Codec {

namespace {

using Sample = std::array<s16, 2>;

inline Sample Mono(s16 value) {
    return Sample{value, value};
}

/// Original decoder for `count` samples of one 8-byte frame (Azahar's decode_sample, verbatim).
void DecodeFrameExact(Sample* output, const u8* frame, std::size_t count, int coef1, int coef2,
                      int scale, int& yn1, int& yn2) {
    static constexpr std::array<int, 16> SIGNED_NIBBLES{
        0, 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1,
    };
    // Decodes an audio sample. One nibble produces one sample.
    const auto decode_sample = [&](const int nibble) -> s16 {
        const int xn = nibble * scale;
        // We first transform everything into 11 bit fixed point, perform the second order
        // digital filter, then transform back.
        // 0x400 == 0.5 in 11 bit fixed point.
        // Filter: y[n] = x[n] + 0.5 + c1 * y[n-1] + c2 * y[n-2]
        int val = ((xn << 11) + 0x400 + coef1 * yn1 + coef2 * yn2) >> 11;
        // Clamp to output range.
        val = std::clamp(val, -32768, 32767);
        // Advance output feedback.
        yn2 = yn1;
        yn1 = val;
        return (s16)val;
    };
    std::size_t datai = 1;
    for (std::size_t i = 0; i < count; i += 2) {
        output[i] = Mono(decode_sample(SIGNED_NIBBLES[frame[datai] >> 4]));
        output[i + 1] = Mono(decode_sample(SIGNED_NIBBLES[frame[datai] & 0xF]));
        datai++;
    }
}

/// A whole 14-sample frame without the clamp on the feedback path: when no intermediate value
/// leaves the s16 range the clamp is the identity and the result is the original one; otherwise
/// returns false and the caller decodes the frame again with DecodeFrameExact. The products and
/// sums are computed modulo 2^32, as the original int arithmetic does on every host we build for.
inline bool DecodeFrameUnclamped(Sample* __restrict output, const u8* __restrict frame,
                                 int coef1, int coef2, unsigned shift, int& yn1, int& yn2) {
    const u32 c1 = static_cast<u32>(coef1);
    const u32 c2 = static_cast<u32>(coef2);
    u32 y1 = static_cast<u32>(yn1);
    u32 y2 = static_cast<u32>(yn2);
    u32 outside = 0;
    const auto step = [&](int nibble, Sample& out) {
        // (xn << 11) + 0x400 + c2 * y[n-2] does not depend on y[n-1]. The empty asm keeps the
        // compiler from reassociating it into the recurrence: the loop-carried path is then one
        // multiply-add and one shift per sample instead of a multiply, two adds, a shift and the
        // two compare/selects of the clamp.
        u32 base = (static_cast<u32>(nibble) << shift) + 0x400U + c2 * y2;
#if defined(__GNUC__)
        asm("" : "+r"(base));
#endif
        const s32 value = static_cast<s32>(base + c1 * y1) >> 11;
        outside |= static_cast<u32>(value + 32768) > 0xFFFFU ? 1U : 0U;
        y2 = y1;
        y1 = static_cast<u32>(value);
        // Both channels in one store.
        const u32 both = (static_cast<u32>(value) & 0xFFFFU) * 0x10001U;
        std::memcpy(&out, &both, sizeof(both));
    };
    for (std::size_t i = 0; i < 7; ++i) {
        const u8 byte = frame[1 + i];
        step(static_cast<s8>(byte) >> 4, output[2 * i]);
        step(static_cast<s8>(static_cast<u8>(byte << 4)) >> 4, output[2 * i + 1]);
    }
    if (outside != 0) return false;
    yn1 = static_cast<s32>(y1);
    yn2 = static_cast<s32>(y2);
    return true;
}

} // namespace

void DecodeADPCMInto(Sample* const output, const u8* const data, const std::size_t sample_count,
                     const std::array<s16, 16>& adpcm_coeff, ADPCMState& state) {
    // GC-ADPCM with scale factor and variable coefficients.
    // Frames are 8 bytes long containing 14 samples each.
    // Samples are 4 bits (one nibble) long.
    constexpr std::size_t FRAME_LEN = 8;
    constexpr std::size_t SAMPLES_PER_FRAME = 14;

    // The original decodes pairs of samples, so an odd count also decodes (and feeds back) the
    // padding sample: exactly ADPCMOutputSamples(sample_count) samples are written.
    const std::size_t total = ADPCMOutputSamples(sample_count);

    int yn1 = state.yn1, yn2 = state.yn2;

    std::size_t outputi = 0;
    for (const u8* frame = data; outputi < total; frame += FRAME_LEN) {
        const std::size_t count = std::min(SAMPLES_PER_FRAME, total - outputi);
        const int frame_header = frame[0];
        const unsigned scale_shift = frame_header & 0xF;
        const int idx = (frame_header >> 4) & 0x7;

        // Coefficients are fixed point with 11 bits fractional part.
        const int coef1 = adpcm_coeff[idx * 2 + 0];
        const int coef2 = adpcm_coeff[idx * 2 + 1];

        if (count != SAMPLES_PER_FRAME ||
            !DecodeFrameUnclamped(output + outputi, frame, coef1, coef2, scale_shift + 11, yn1,
                                  yn2)) {
            DecodeFrameExact(output + outputi, frame, count, coef1, coef2, 1 << scale_shift, yn1,
                             yn2);
        }
        outputi += count;
    }

    state.yn1 = static_cast<s16>(yn1);
    state.yn2 = static_cast<s16>(yn2);
}

void DecodePCM8Into(Sample* const output, const unsigned num_channels, const u8* const data,
                    const std::size_t sample_count) {
    ASSERT(num_channels == 1 || num_channels == 2);

    const auto decode_sample = [](u8 sample) {
        return static_cast<s16>(static_cast<u16>(sample) << 8);
    };

    if (num_channels == 1) {
        for (std::size_t i = 0; i < sample_count; i++) {
            output[i] = Mono(decode_sample(data[i]));
        }
    } else {
        for (std::size_t i = 0; i < sample_count; i++) {
            output[i][0] = decode_sample(data[i * 2 + 0]);
            output[i][1] = decode_sample(data[i * 2 + 1]);
        }
    }
}

void DecodePCM16Into(Sample* const output, const unsigned num_channels, const u8* const data,
                     const std::size_t sample_count) {
    ASSERT(num_channels == 1 || num_channels == 2);

    if (num_channels == 1) {
        for (std::size_t i = 0; i < sample_count; i++) {
            s16 sample;
            std::memcpy(&sample, data + i * sizeof(s16), sizeof(s16));
            output[i] = Mono(sample);
        }
    } else {
        std::memcpy(output, data, sample_count * 2 * sizeof(s16));
    }
}

StereoBuffer16 DecodeADPCM(const u8* const data, const std::size_t sample_count,
                           const std::array<s16, 16>& adpcm_coeff, ADPCMState& state) {
    StereoBuffer16 ret(ADPCMOutputSamples(sample_count));
    DecodeADPCMInto(ret.data(), data, sample_count, adpcm_coeff, state);
    return ret;
}

StereoBuffer16 DecodePCM8(const unsigned num_channels, const u8* const data,
                          const std::size_t sample_count) {
    StereoBuffer16 ret(sample_count);
    DecodePCM8Into(ret.data(), num_channels, data, sample_count);
    return ret;
}

StereoBuffer16 DecodePCM16(const unsigned num_channels, const u8* const data,
                           const std::size_t sample_count) {
    StereoBuffer16 ret(sample_count);
    DecodePCM16Into(ret.data(), num_channels, data, sample_count);
    return ret;
}
} // namespace AudioCore::Codec
