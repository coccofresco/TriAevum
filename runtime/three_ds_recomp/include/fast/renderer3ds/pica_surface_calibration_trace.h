#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Fast::Renderer3ds {

// Render-thread diagnostic. No draw reads, formatting or storage allocations
// occur while disabled. Selected frames retain counts and only the 24 largest
// scene presentation draws (vertex count, which includes indexed elements).
class PicaSurfaceCalibrationTrace {
public:
    static constexpr size_t MaximumDraws = 24;
    static bool Requested(const char* value) noexcept { return value != nullptr && std::string_view(value) == "1"; }
    explicit PicaSurfaceCalibrationTrace(bool enabled = Requested(std::getenv("TRIAEVUM_SURFACE_CALIBRATION_TRACE")))
        : mEnabled(enabled) {}

    void BeginFrame(uint64_t frame) {
        if (!mEnabled) return;
        mFrame = frame;
        mSampling = frame == 1 || frame % 120 == 0;
        if (mSampling) { mClasses.clear(); mLargest.clear(); mDrawCount = 0; }
    }
    [[nodiscard]] bool Sampling() const noexcept { return mSampling; }
    void Count(const std::string& classification, uint32_t vertices) {
        if (!mSampling) return;
        auto& count = mClasses[classification];
        ++count.Draws;
        count.Vertices += vertices;
        ++mDrawCount;
    }
    [[nodiscard]] bool WouldKeep(uint32_t vertices) const noexcept {
        return mSampling && (mLargest.size() < MaximumDraws || vertices > mLargest.back().Vertices);
    }
    void Keep(uint32_t vertices, std::string line) {
        if (!WouldKeep(vertices)) return;
        const auto at = std::find_if(mLargest.begin(), mLargest.end(),
            [vertices](const Row& row) { return vertices > row.Vertices; });
        mLargest.insert(at, {vertices, std::move(line)});
        if (mLargest.size() > MaximumDraws) mLargest.pop_back();
    }
    // Called once after this frame's submissions. Tests inspect the same text
    // sent to stderr by the backend, without launching a renderer or a game.
    std::string FinishFrame(std::string_view context) {
        if (!mSampling) return {};
        mSampling = false;
        std::ostringstream text;
        const auto prefix = "TRIAEVUM_SURFACE_CALIBRATION frame=" + std::to_string(mFrame);
        text << prefix << " kind=summary draws=" << mDrawCount << " largest=" << mLargest.size()
             << " period=120 " << context << '\n';
        for (const auto& [key, count] : mClasses)
            text << prefix << " kind=class " << key << " draws=" << count.Draws << " elements=" << count.Vertices << '\n';
        for (size_t i = 0; i < mLargest.size(); ++i)
            text << prefix << " kind=draw rank=" << i + 1 << ' ' << mLargest[i].Text << '\n';
        return text.str();
    }

private:
    struct CountValue { uint32_t Draws = 0; uint64_t Vertices = 0; };
    struct Row { uint32_t Vertices; std::string Text; };
    bool mEnabled = false, mSampling = false;
    uint64_t mFrame = 0;
    uint32_t mDrawCount = 0;
    std::map<std::string, CountValue> mClasses;
    std::vector<Row> mLargest;
};

} // namespace Fast::Renderer3ds
