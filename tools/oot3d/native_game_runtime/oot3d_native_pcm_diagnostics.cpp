#include "oot3d_native_pcm_diagnostics.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {

void WriteLe16(std::ostream& output, uint16_t value) {
    const std::array<char, 2> bytes{
        static_cast<char>(value), static_cast<char>(value >> 8U)};
    output.write(bytes.data(), bytes.size());
}

void WriteLe32(std::ostream& output, uint32_t value) {
    const std::array<char, 4> bytes{
        static_cast<char>(value), static_cast<char>(value >> 8U),
        static_cast<char>(value >> 16U), static_cast<char>(value >> 24U)};
    output.write(bytes.data(), bytes.size());
}

} // namespace

void NativePcmContinuityDiagnostics::ObserveFrame(
    uint64_t dspFrame, std::span<const int16_t> interleavedStereo) {
    if (interleavedStereo.size() % 2U != 0U) {
        ++mMalformedFrames;
        return;
    }
    ++mDspFrames;
    mStereoFrames += interleavedStereo.size() / 2U;
    for (size_t sampleFrame = 0;
         sampleFrame < interleavedStereo.size() / 2U; ++sampleFrame) {
        for (size_t channel = 0; channel < 2U; ++channel) {
            const int16_t sample =
                interleavedStereo[sampleFrame * 2U + channel];
            mFullScaleSamples +=
                sample == std::numeric_limits<int16_t>::min() ||
                        sample == std::numeric_limits<int16_t>::max()
                    ? 1U
                    : 0U;
            if (mHasPrevious[channel]) {
                const uint32_t delta = static_cast<uint32_t>(std::abs(
                    static_cast<int32_t>(sample) - mPrevious[channel]));
                ++mAdjacentComparisons;
                mAbsoluteDeltaSum += delta;
                mMaximumAbsoluteDelta =
                    std::max(mMaximumAbsoluteDelta, delta);
                mLargeDelta4096 += delta >= 4096U ? 1U : 0U;
                mLargeDelta8192 += delta >= 8192U ? 1U : 0U;
                mLargeDelta16384 += delta >= 16384U ? 1U : 0U;
                if (sampleFrame == 0U) {
                    ++mBoundaryComparisons;
                    mBoundaryAbsoluteDeltaSum += delta;
                    mMaximumBoundaryAbsoluteDelta =
                        std::max(mMaximumBoundaryAbsoluteDelta, delta);
                    mBoundaryLargeDelta4096 += delta >= 4096U ? 1U : 0U;
                    mBoundaryLargeDelta8192 += delta >= 8192U ? 1U : 0U;
                    mBoundaryLargeDelta16384 += delta >= 16384U ? 1U : 0U;
                    if (delta >= 8192U && mBoundaryEvents.size() < 64U) {
                        mBoundaryEvents.push_back(
                            {dspFrame, static_cast<uint32_t>(channel),
                             mPrevious[channel], sample, delta});
                    }
                }
            }
            mPrevious[channel] = sample;
            mHasPrevious[channel] = true;
        }
    }
}

nlohmann::json NativePcmContinuityDiagnostics::ToJson() const {
    nlohmann::json boundaryEvents = nlohmann::json::array();
    for (const auto& event : mBoundaryEvents) {
        boundaryEvents.push_back({
            {"dsp_frame", event.DspFrame},
            {"channel", event.Channel},
            {"previous", event.Previous},
            {"current", event.Current},
            {"absolute_delta", event.AbsoluteDelta},
        });
    }
    return {
        {"dsp_frames", mDspFrames},
        {"stereo_frames", mStereoFrames},
        {"malformed_frames", mMalformedFrames},
        {"full_scale_channel_samples", mFullScaleSamples},
        {"adjacent_comparisons", mAdjacentComparisons},
        {"mean_absolute_delta",
         mAdjacentComparisons != 0U
             ? static_cast<double>(mAbsoluteDeltaSum) /
                   static_cast<double>(mAdjacentComparisons)
             : 0.0},
        {"maximum_absolute_delta", mMaximumAbsoluteDelta},
        {"large_delta_4096", mLargeDelta4096},
        {"large_delta_8192", mLargeDelta8192},
        {"large_delta_16384", mLargeDelta16384},
        {"frame_boundary_comparisons", mBoundaryComparisons},
        {"frame_boundary_mean_absolute_delta",
         mBoundaryComparisons != 0U
             ? static_cast<double>(mBoundaryAbsoluteDeltaSum) /
                   static_cast<double>(mBoundaryComparisons)
             : 0.0},
        {"frame_boundary_maximum_absolute_delta",
         mMaximumBoundaryAbsoluteDelta},
        {"frame_boundary_large_delta_4096", mBoundaryLargeDelta4096},
        {"frame_boundary_large_delta_8192", mBoundaryLargeDelta8192},
        {"frame_boundary_large_delta_16384", mBoundaryLargeDelta16384},
        {"frame_boundary_events", boundaryEvents},
    };
}

void WriteStereoPcm16Wave(const std::filesystem::path& path,
                          uint32_t sampleRate,
                          std::span<const int16_t> interleavedStereo) {
    const uint64_t dataSize64 =
        interleavedStereo.size() * sizeof(int16_t);
    if (sampleRate == 0U || dataSize64 > UINT32_MAX - 36U) {
        throw std::runtime_error(
            "native PCM dump is too large or has no sample rate");
    }
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to create native PCM dump: " +
                                 path.string());
    }

    constexpr uint16_t channels = 2U;
    constexpr uint16_t bitsPerSample = 16U;
    constexpr uint16_t blockAlign = channels * sizeof(int16_t);
    const uint32_t dataSize = static_cast<uint32_t>(dataSize64);
    output.write("RIFF", 4);
    WriteLe32(output, 36U + dataSize);
    output.write("WAVEfmt ", 8);
    WriteLe32(output, 16U);
    WriteLe16(output, 1U);
    WriteLe16(output, channels);
    WriteLe32(output, sampleRate);
    WriteLe32(output, sampleRate * blockAlign);
    WriteLe16(output, blockAlign);
    WriteLe16(output, bitsPerSample);
    output.write("data", 4);
    WriteLe32(output, dataSize);
    if constexpr (std::endian::native == std::endian::little) {
        output.write(reinterpret_cast<const char*>(interleavedStereo.data()),
                     static_cast<std::streamsize>(dataSize));
    } else {
        for (const int16_t sample : interleavedStereo) {
            WriteLe16(output, static_cast<uint16_t>(sample));
        }
    }
    if (!output) {
        throw std::runtime_error("failed to write native PCM dump: " +
                                 path.string());
    }
}

} // namespace Oot3dNativeGame
