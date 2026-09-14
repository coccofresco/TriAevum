#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Fast::Renderer {

// Fixed-memory host-frame statistics. Quantiles are conservative bucket upper
// bounds; the overflow bucket uses the measured maximum, never a clipped value.
class FrameTimeDistribution {
  public:
    static constexpr double ResolutionMs = 0.125;
    static constexpr std::size_t RegularBuckets = 8192;
    static constexpr double RangeMs = RegularBuckets * ResolutionMs;

    void RecordMilliseconds(double milliseconds) noexcept {
        if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
            ++mInvalid;
            return;
        }
        const auto bucket = milliseconds >= RangeMs ? RegularBuckets
            : static_cast<std::size_t>(milliseconds / ResolutionMs);
        ++mBuckets[bucket];
        ++mCount;
        mSum += milliseconds;
        if (milliseconds > mMaximum) mMaximum = milliseconds;
        mOver60Hz += milliseconds > 1000.0 / 60.0;
        mOver30Hz += milliseconds > 1000.0 / 30.0;
    }

    [[nodiscard]] std::optional<double> QuantileUpperMs(double fraction) const noexcept {
        if (!mCount || !std::isfinite(fraction) || fraction < 0.0 || fraction > 1.0)
            return std::nullopt;
        const auto rank = fraction == 0.0 ? uint64_t{1}
            : static_cast<uint64_t>(std::ceil(fraction * static_cast<double>(mCount)));
        uint64_t cumulative = 0;
        for (std::size_t bucket = 0; bucket < mBuckets.size(); ++bucket) {
            cumulative += mBuckets[bucket];
            if (cumulative >= rank) {
                const double upper = (bucket + 1) * ResolutionMs;
                return bucket == RegularBuckets || upper > mMaximum ? mMaximum : upper;
            }
        }
        return mMaximum;
    }

    [[nodiscard]] uint64_t Count() const noexcept { return mCount; }
    [[nodiscard]] uint64_t Invalid() const noexcept { return mInvalid; }
    [[nodiscard]] uint64_t Overflow() const noexcept { return mBuckets.back(); }
    [[nodiscard]] uint64_t Over60Hz() const noexcept { return mOver60Hz; }
    [[nodiscard]] uint64_t Over30Hz() const noexcept { return mOver30Hz; }
    [[nodiscard]] double MaximumMs() const noexcept { return mMaximum; }
    [[nodiscard]] double MeanMs() const noexcept { return mCount ? mSum / mCount : 0.0; }

  private:
    std::array<uint64_t, RegularBuckets + 1> mBuckets{};
    uint64_t mCount = 0, mInvalid = 0, mOver60Hz = 0, mOver30Hz = 0;
    double mSum = 0.0, mMaximum = 0.0;
};

} // namespace Fast::Renderer
