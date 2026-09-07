#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace Fast {
class Fast3dWindow;
}

struct Oot3dDemoHostInputState {
    double MoveX = 0.0;
    double MoveY = 0.0;
    bool Fast = false;
    bool Reset = false;
    bool Exit = false;
    bool Scripted = false;
    int32_t ScriptSegmentIndex = -1;
};

struct Oot3dDemoHostInputDiagnostics {
    uint32_t SampledFrameCount = 0;
    uint32_t ScriptedFrameCount = 0;
    uint32_t MovementFrameCount = 0;
    uint32_t FastFrameCount = 0;
    uint32_t ResetFrameCount = 0;
    Oot3dDemoHostInputState LastState;

    void Observe(const Oot3dDemoHostInputState& state);
};

class Oot3dDemoHostInputTimeline {
  public:
    static Oot3dDemoHostInputTimeline LoadFile(const std::filesystem::path& path);

    bool Enabled() const;
    size_t SegmentCount() const;
    const std::filesystem::path& SourcePath() const;
    Oot3dDemoHostInputState Sample(uint32_t frame) const;

  private:
    struct Segment {
        uint32_t StartFrame = 0;
        uint32_t EndFrameExclusive = 0;
        double MoveX = 0.0;
        double MoveY = 0.0;
        bool Fast = false;
        bool Reset = false;
    };

    std::filesystem::path mSourcePath;
    std::vector<Segment> mSegments;
};

Oot3dDemoHostInputState ResolveOot3dDemoHostInput(
    Fast::Fast3dWindow& window, const Oot3dDemoHostInputTimeline& timeline,
    uint32_t frame);
