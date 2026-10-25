// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstring>
#include "audio_core/interpolate.h"
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#include "common/assert.h"

namespace AudioCore::AudioInterp {

// Calculations are done in fixed point with 24 fractional bits.
// (This is not verified. This was chosen for minimal error.)
constexpr u64 scale_factor = 1 << 24;
constexpr u64 scale_mask = scale_factor - 1;

/// Here we step over the input in steps of rate, until we consume all of the input.
/// Three adjacent samples are passed to fn each step.
template <typename Function>
static void StepOverSamples(State& state, StereoBuffer16& input, float rate, StereoFrame16& output,
                            std::size_t& outputi, Function fn) {
    ASSERT(rate > 0);

    if (input.empty())
        return;

    // The two historical samples are a virtual prefix, not inserted into storage.
    const auto sample = [&](std::size_t index) -> const std::array<s16, 2>& {
        if (index == 0) return state.xn2;
        if (index == 1) return state.xn1;
        return input[index - 2];
    };

    const u64 step_size = static_cast<u64>(rate * scale_factor);
    u64 fposition = state.fposition;
    std::size_t inputi = 0;

    while (outputi < output.size()) {
        inputi = static_cast<std::size_t>(fposition / scale_factor);

        if (inputi >= input.size()) {
            inputi = input.size();
            break;
        }

        u64 fraction = fposition & scale_mask;
        output[outputi++] = fn(fraction, sample(inputi), sample(inputi + 1), sample(inputi + 2));

        fposition += step_size;
    }

    const auto xn2 = sample(inputi);
    const auto xn1 = sample(inputi + 1);
    state.xn2 = xn2;
    state.xn1 = xn1;
    state.fposition = fposition - inputi * scale_factor;

    input.Consume(inputi);
}

/// TriAevum: StepOverSamples for None/Linear without the per-output virtual-index branches. The
/// stepping (fposition, the outputs written, inputi, xn1/xn2, consumed samples) is the same
/// sequence of operations; only the reads of the outputs at virtual index >= 2 go straight to the
/// input, and the number of such outputs is computed up front instead of tested per output.
template <bool kLinear>
static void StepOverSamplesFast(State& state, StereoBuffer16& input, float rate,
                                StereoFrame16& output, std::size_t& outputi) {
    ASSERT(rate > 0);

    if (input.empty())
        return;

    using Sample = std::array<s16, 2>;
    const Sample* const in = input.data();
    const std::size_t size = input.size();
    const auto sample = [&](std::size_t index) -> const Sample& {
        if (index == 0) return state.xn2;
        if (index == 1) return state.xn1;
        return in[index - 2];
    };
    const auto emit = [](u64 fraction, const Sample& x0, const Sample& x1) -> Sample {
        if constexpr (!kLinear) {
            return x0;
        } else {
            // Note on accuracy: Some values that this produces are +/- 1 from the actual firmware.
            // This is a saturated subtraction. (Verified by black-box fuzzing.)
            const s64 delta0 = std::clamp<s64>(x1[0] - x0[0], -32768, 32767);
            const s64 delta1 = std::clamp<s64>(x1[1] - x0[1], -32768, 32767);
            // fraction * delta / scale_factor in u64 has the same low 16 bits as the signed
            // product shifted right by 24 (|product| < 2^39): the s16 result is the same.
            return Sample{
                static_cast<s16>(x0[0] + static_cast<s32>((static_cast<s64>(fraction) * delta0) >> 24)),
                static_cast<s16>(x0[1] + static_cast<s32>((static_cast<s64>(fraction) * delta1) >> 24)),
            };
        }
    };

    const u64 step_size = static_cast<u64>(rate * scale_factor);
    if (step_size > (u64{1} << 40) || state.fposition > (u64{1} << 60)) {
        // Absurd rates (or positions) can wrap the 64-bit position within a frame: keep the
        // original loop, whose behaviour then is the reference.
        StepOverSamples(state, input, rate, output, outputi,
                        [&](u64 fraction, const auto& x0, const auto& x1, const auto&) {
                            return emit(fraction, x0, x1);
                        });
        return;
    }
    u64 fposition = state.fposition;
    std::size_t inputi = 0;
    const std::size_t outputs = output.size();

    // Outputs whose current sample is xn2 or xn1 (at most a few at the start of a buffer).
    while (outputi < outputs) {
        inputi = static_cast<std::size_t>(fposition / scale_factor);
        if (inputi >= 2 || inputi >= size) break;
        output[outputi++] = emit(fposition & scale_mask, sample(inputi), sample(inputi + 1));
        fposition += step_size;
    }

    if (outputi < outputs && inputi < size) {
        // Every output k from here on has 2 <= inputi_k (positions only grow); the ones with
        // inputi_k < size are those with fposition_k < size << 24.
        const u64 end = static_cast<u64>(size) * scale_factor;
        std::size_t count = outputs - outputi;
        if (step_size != 0) {
            const u64 fit = (end - fposition + step_size - 1) / step_size;
            if (fit < count) count = static_cast<std::size_t>(fit);
        }
        Sample* __restrict out = output.data() + outputi;
#if defined(__aarch64__) && defined(__ARM_NEON)
        if constexpr (kLinear) {
            // Both channels in one D register: x0 and x1 are adjacent, so one 8-byte load. Same
            // integer operations as emit(): saturating difference (the clamp), exact 64-bit
            // product, arithmetic shift by 24 and the low 16 bits of x0 + result.
            for (std::size_t k = 0; k < count; ++k) {
                const std::size_t i = static_cast<std::size_t>(fposition >> 24);
                const int16x4_t x = vld1_s16(in[i - 2].data()); // x0.L x0.R x1.L x1.R
                const int16x4_t delta = vqsub_s16(vext_s16(x, x, 2), x);
                const int64x2_t product = vmull_n_s32(vget_low_s32(vmovl_s16(delta)),
                                                      static_cast<s32>(fposition & scale_mask));
                const int16x4_t shifted = vreinterpret_s16_s32(vshrn_n_s64(product, 24));
                const int16x4_t result = vadd_s16(x, vuzp1_s16(shifted, shifted));
                const u32 both = vget_lane_u32(vreinterpret_u32_s16(result), 0);
                std::memcpy(&out[k], &both, sizeof(both));
                fposition += step_size;
            }
        } else
#endif
        for (std::size_t k = 0; k < count; ++k) {
            const std::size_t i = static_cast<std::size_t>(fposition >> 24);
            out[k] = emit(fposition & scale_mask, in[i - 2], in[i - 1]);
            fposition += step_size;
        }
        outputi += count;
        // The original's last iteration: either the output that filled the frame (inputi of the
        // last emitted position) or the position that reached the end of the input.
        if (outputi < outputs) {
            inputi = size;
        } else {
            inputi = static_cast<std::size_t>((fposition - step_size) / scale_factor);
        }
    } else if (outputi < outputs) {
        inputi = size;
    }

    const auto xn2 = sample(inputi);
    const auto xn1 = sample(inputi + 1);
    state.xn2 = xn2;
    state.xn1 = xn1;
    state.fposition = fposition - inputi * scale_factor;

    input.Consume(inputi);
}
void None(State& state, StereoBuffer16& input, float rate, StereoFrame16& output,
          std::size_t& outputi) {
    StepOverSamplesFast<false>(state, input, rate, output, outputi);
}

void Linear(State& state, StereoBuffer16& input, float rate, StereoFrame16& output,
            std::size_t& outputi) {
    StepOverSamplesFast<true>(state, input, rate, output, outputi);
}

} // namespace AudioCore::AudioInterp
