#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Oot3dNativeGame {

class NativePcmContinuityDiagnostics {
  public:
    void ObserveFrame(uint64_t dspFrame,
                      std::span<const int16_t> interleavedStereo);
    nlohmann::json ToJson() const;

  private:
    struct BoundaryEvent {
        uint64_t DspFrame = 0;
        uint32_t Channel = 0;
        int32_t Previous = 0;
        int32_t Current = 0;
        uint32_t AbsoluteDelta = 0;
    };

    std::array<int32_t, 2> mPrevious{};
    std::array<bool, 2> mHasPrevious{};
    uint64_t mDspFrames = 0;
    uint64_t mStereoFrames = 0;
    uint64_t mMalformedFrames = 0;
    uint64_t mFullScaleSamples = 0;
    uint64_t mAdjacentComparisons = 0;
    uint64_t mAbsoluteDeltaSum = 0;
    uint32_t mMaximumAbsoluteDelta = 0;
    uint64_t mLargeDelta4096 = 0;
    uint64_t mLargeDelta8192 = 0;
    uint64_t mLargeDelta16384 = 0;
    uint64_t mBoundaryComparisons = 0;
    uint64_t mBoundaryAbsoluteDeltaSum = 0;
    uint32_t mMaximumBoundaryAbsoluteDelta = 0;
    uint64_t mBoundaryLargeDelta4096 = 0;
    uint64_t mBoundaryLargeDelta8192 = 0;
    uint64_t mBoundaryLargeDelta16384 = 0;
    std::vector<BoundaryEvent> mBoundaryEvents;
};

void WriteStereoPcm16Wave(const std::filesystem::path& path,
                          uint32_t sampleRate,
                          std::span<const int16_t> interleavedStereo);

} // namespace Oot3dNativeGame
